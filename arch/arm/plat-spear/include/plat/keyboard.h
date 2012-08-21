/*
 * Copyright (C) 2010 ST Microelectronics
 * Rajeev Kumar<rajeev-dlh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __PLAT_KEYBOARD_H
#define __PLAT_KEYBOARD_H

#include <linux/bitops.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/types.h>

#define DECLARE_9x9_KEYMAP(_name) \
int _name[] = { \
	KEY(0, 0, KEY_ESC), \
	KEY(0, 1, KEY_1), \
	KEY(0, 2, KEY_2), \
	KEY(0, 3, KEY_3), \
	KEY(0, 4, KEY_4), \
	KEY(0, 5, KEY_5), \
	KEY(0, 6, KEY_6), \
	KEY(0, 7, KEY_7), \
	KEY(0, 8, KEY_8), \
	KEY(1, 0, KEY_9), \
	KEY(1, 1, KEY_MINUS), \
	KEY(1, 2, KEY_EQUAL), \
	KEY(1, 3, KEY_BACKSPACE), \
	KEY(1, 4, KEY_TAB), \
	KEY(1, 5, KEY_Q), \
	KEY(1, 6, KEY_W), \
	KEY(1, 7, KEY_E), \
	KEY(1, 8, KEY_R), \
	KEY(2, 0, KEY_T), \
	KEY(2, 1, KEY_Y), \
	KEY(2, 2, KEY_U), \
	KEY(2, 3, KEY_I), \
	KEY(2, 4, KEY_O), \
	KEY(2, 5, KEY_P), \
	KEY(2, 6, KEY_LEFTBRACE), \
	KEY(2, 7, KEY_RIGHTBRACE), \
	KEY(2, 8, KEY_ENTER), \
	KEY(3, 0, KEY_LEFTCTRL), \
	KEY(3, 1, KEY_A), \
	KEY(3, 2, KEY_S), \
	KEY(3, 3, KEY_D), \
	KEY(3, 4, KEY_F), \
	KEY(3, 5, KEY_G), \
	KEY(3, 6, KEY_H), \
	KEY(3, 7, KEY_J), \
	KEY(3, 8, KEY_K), \
	KEY(4, 0, KEY_L), \
	KEY(4, 1, KEY_SEMICOLON), \
	KEY(4, 2, KEY_APOSTROPHE), \
	KEY(4, 3, KEY_GRAVE), \
	KEY(4, 4, KEY_LEFTSHIFT), \
	KEY(4, 5, KEY_BACKSLASH), \
	KEY(4, 6, KEY_Z), \
	KEY(4, 7, KEY_X), \
	KEY(4, 8, KEY_C), \
	KEY(5, 0, KEY_V), \
	KEY(5, 1, KEY_B), \
	KEY(5, 2, KEY_N), \
	KEY(5, 3, KEY_M), \
	KEY(5, 4, KEY_COMMA), \
	KEY(5, 5, KEY_DOT), \
	KEY(5, 6, KEY_SLASH), \
	KEY(5, 7, KEY_RIGHTSHIFT), \
	KEY(5, 8, KEY_KPASTERISK), \
	KEY(6, 0, KEY_LEFTALT), \
	KEY(6, 1, KEY_SPACE), \
	KEY(6, 2, KEY_CAPSLOCK), \
	KEY(6, 3, KEY_F1), \
	KEY(6, 4, KEY_F2), \
	KEY(6, 5, KEY_F3), \
	KEY(6, 6, KEY_F4), \
	KEY(6, 7, KEY_F5), \
	KEY(6, 8, KEY_F6), \
	KEY(7, 0, KEY_F7), \
	KEY(7, 1, KEY_F8), \
	KEY(7, 2, KEY_F9), \
	KEY(7, 3, KEY_F10), \
	KEY(7, 4, KEY_NUMLOCK), \
	KEY(7, 5, KEY_SCROLLLOCK), \
	KEY(7, 6, KEY_KP7), \
	KEY(7, 7, KEY_KP8), \
	KEY(7, 8, KEY_KP9), \
	KEY(8, 0, KEY_KPMINUS), \
	KEY(8, 1, KEY_KP4), \
	KEY(8, 2, KEY_KP5), \
	KEY(8, 3, KEY_KP6), \
	KEY(8, 4, KEY_KPPLUS), \
	KEY(8, 5, KEY_KP1), \
	KEY(8, 6, KEY_KP2), \
	KEY(8, 7, KEY_KP3), \
	KEY(8, 8, KEY_KP0), \
}

#define DECLARE_6x6_KEYMAP(_name) \
int _name[] = { \
	KEY(0, 0, KEY_RESERVED), \
	KEY(0, 1, KEY_1), \
	KEY(0, 2, KEY_2), \
	KEY(0, 3, KEY_3), \
	KEY(0, 4, KEY_4), \
	KEY(0, 5, KEY_5), \
	KEY(1, 0, KEY_Q), \
	KEY(1, 1, KEY_W), \
	KEY(1, 2, KEY_E), \
	KEY(1, 3, KEY_R), \
	KEY(1, 4, KEY_T), \
	KEY(1, 5, KEY_Y), \
	KEY(2, 0, KEY_D), \
	KEY(2, 1, KEY_F), \
	KEY(2, 2, KEY_G), \
	KEY(2, 3, KEY_H), \
	KEY(2, 4, KEY_J), \
	KEY(2, 5, KEY_K), \
	KEY(3, 0, KEY_B), \
	KEY(3, 1, KEY_N), \
	KEY(3, 2, KEY_M), \
	KEY(3, 3, KEY_COMMA), \
	KEY(3, 4, KEY_DOT), \
	KEY(3, 5, KEY_SLASH), \
	KEY(4, 0, KEY_F6), \
	KEY(4, 1, KEY_F7), \
	KEY(4, 2, KEY_F8), \
	KEY(4, 3, KEY_F9), \
	KEY(4, 4, KEY_F10), \
	KEY(4, 5, KEY_NUMLOCK), \
	KEY(5, 0, KEY_KP2), \
	KEY(5, 1, KEY_KP3), \
	KEY(5, 2, KEY_KP0), \
	KEY(5, 3, KEY_KPDOT), \
	KEY(5, 4, KEY_RO), \
	KEY(5, 5, KEY_ZENKAKUHANKAKU), \
}

#define KEYPAD_9x9     0
#define KEYPAD_6x6     1
#define KEYPAD_2x2     2

/**
 * struct kbd_platform_data - spear keyboard platform data
 * keymap: pointer to keymap data (table and size)
 * rep: enables key autorepeat
 * mode: choose keyboard support(9x9, 6x6, 2x2)
 * suspended_rate: rate at which keyboard would operate in suspended mode
 *
 * This structure is supposed to be used by platform code to supply
 * keymaps to drivers that implement keyboards.
 */
struct kbd_platform_data {
	const struct matrix_keymap_data *keymap;
	bool rep;
	unsigned int mode;
	unsigned int suspended_rate;
};

#endif /* __PLAT_KEYBOARD_H */
