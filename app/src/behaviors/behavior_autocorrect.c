/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_autocorrect

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define AUTO_CORRECT_MAX_WORD_LEN 32

struct autocorrect_entry {
    const char *from;
    const char *to;
};

struct behavior_autocorrect_data {
    char current_word[AUTO_CORRECT_MAX_WORD_LEN];
    uint8_t current_word_len;
};

static const struct autocorrect_entry autocorrect_table[] = {
    {"teh", "the"},
    {"recive", "receive"},
    {"comming", "coming"},
    {"thier", "their"},
    {"accross", "across"},
};

static bool autocorrect_is_alpha_key(uint8_t keycode) {
    return keycode >= HID_USAGE_KEY_KEYBOARD_A && keycode <= HID_USAGE_KEY_KEYBOARD_Z;
}

static void autocorrect_clear_word(struct behavior_autocorrect_data *data) {
    memset(data->current_word, 0, sizeof(data->current_word));
    data->current_word_len = 0;
}

static void autocorrect_append_char(struct behavior_autocorrect_data *data, uint8_t keycode) {
    if (data->current_word_len >= AUTO_CORRECT_MAX_WORD_LEN - 1) {
        return;
    }

    if (!autocorrect_is_alpha_key(keycode)) {
        return;
    }

    data->current_word[data->current_word_len++] = 'a' + (keycode - HID_USAGE_KEY_KEYBOARD_A);
    data->current_word[data->current_word_len] = '\0';
}

static void autocorrect_send_backspace(uint8_t count) {
    for (int i = 0; i < count; i++) {
        zmk_hid_keyboard_press(HID_USAGE_KEY_KEYBOARD_BACKSPACE);
        zmk_hid_keyboard_release(HID_USAGE_KEY_KEYBOARD_BACKSPACE);
    }
}

static void autocorrect_send_text(const char *text) {
    for (const char *c = text; *c != '\0'; c++) {
        if (*c >= 'a' && *c <= 'z') {
            uint8_t hid_code = HID_USAGE_KEY_KEYBOARD_A + (*c - 'a');
            zmk_hid_keyboard_press(hid_code);
            zmk_hid_keyboard_release(hid_code);
        }
    }
}

static void autocorrect_replace_current_word(struct behavior_autocorrect_data *data) {
    if (data->current_word_len == 0) {
        return;
    }

    for (size_t i = 0; i < ARRAY_SIZE(autocorrect_table); i++) {
        const struct autocorrect_entry *entry = &autocorrect_table[i];
        if (strcmp(data->current_word, entry->from) != 0) {
            continue;
        }

        autocorrect_send_backspace(data->current_word_len);
        autocorrect_send_text(entry->to);
        autocorrect_clear_word(data);
        return;
    }

    autocorrect_clear_word(data);
}

static void autocorrect_on_spacebar(struct behavior_autocorrect_data *data) {
    autocorrect_replace_current_word(data);
}

static int on_autocorrect_binding_pressed(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_autocorrect_data *data = dev->data;

    autocorrect_replace_current_word(data);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_autocorrect_binding_released(struct zmk_behavior_binding *binding,
                                          struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_autocorrect_driver_api = {
    .binding_pressed = on_autocorrect_binding_pressed,
    .binding_released = on_autocorrect_binding_released,
};

static int autocorrect_keycode_state_changed_listener(const zmk_event_t *eh);

ZMK_LISTENER(behavior_autocorrect, autocorrect_keycode_state_changed_listener);
ZMK_SUBSCRIPTION(behavior_autocorrect, zmk_keycode_state_changed);

#define GET_DEV(inst) DEVICE_DT_INST_GET(inst),
static const struct device *devs[] = {DT_INST_FOREACH_STATUS_OKAY(GET_DEV)};

static int autocorrect_keycode_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    for (int i = 0; i < ARRAY_SIZE(devs); i++) {
        const struct device *dev = devs[i];
        struct behavior_autocorrect_data *data = dev->data;

        if (ev->usage_page == HID_USAGE_KEY && ev->keycode == HID_USAGE_KEY_KEYBOARD_SPACEBAR) {
            autocorrect_on_spacebar(data);
            continue;
        }

        if (ev->usage_page == HID_USAGE_KEY && autocorrect_is_alpha_key(ev->keycode)) {
            autocorrect_append_char(data, ev->keycode);
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

#define AC_INST(n)                                                                              \
    static struct behavior_autocorrect_data behavior_autocorrect_data_##n = {                     \
        .current_word = {0},                                                                     \
        .current_word_len = 0,                                                                  \
    };                                                                                          \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, &behavior_autocorrect_data_##n, NULL, POST_KERNEL,    \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_autocorrect_driver_api);

DT_INST_FOREACH_STATUS_OKAY(AC_INST)
