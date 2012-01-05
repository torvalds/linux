/* videomate-m1f.h - Keytable for videomate_m1f Remote Controller
 *
 * keymap imported from ir-keymaps.c
 *
 * Copyright (c) 2010 by Pavel Osnova <pvosnova@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table videomate_m1f[] = {
	{ 0x01, KEY_POWER },
	{ 0x31, KEY_TUNER },
	{ 0x33, KEY_VIDEO },
	{ 0x2f, KEY_RADIO },
	{ 0x30, KEY_CAMERA },
	{ 0x2d, KEY_NEW }, /* TV record button */
	{ 0x17, KEY_CYCLEWINDOWS },
	{ 0x2c, KEY_ANGLE },
	{ 0x2b, KEY_LANGUAGE },
	{ 0x32, KEY_SEARCH }, /* '...' button */
	{ 0x11, KEY_UP },
	{ 0x13, KEY_LEFT },
	{ 0x15, KEY_OK },
	{ 0x14, KEY_RIGHT },
	{ 0x12, KEY_DOWN },
	{ 0x16, KEY_BACKSPACE },
	{ 0x02, KEY_ZOOM }, /* WIN key */
	{ 0x04, KEY_INFO },
	{ 0x05, KEY_VOLUMEUP },
	{ 0x03, KEY_MUTE },
	{ 0x07, KEY_CHANNELUP },
	{ 0x06, KEY_VOLUMEDOWN },
	{ 0x08, KEY_CHANNELDOWN },
	{ 0x0c, KEY_RECORD },
	{ 0x0e, KEY_STOP },
	{ 0x0a, KEY_BACK },
	{ 0x0b, KEY_PLAY },
	{ 0x09, KEY_FORWARD },
	{ 0x10, KEY_PREVIOUS },
	{ 0x0d, KEY_PAUSE },
	{ 0x0f, KEY_NEXT },
	{ 0x1e, KEY_1 },
	{ 0x1f, KEY_2 },
	{ 0x20, KEY_3 },
	{ 0x21, KEY_4 },
	{ 0x22, KEY_5 },
	{ 0x23, KEY_6 },
	{ 0x24, KEY_7 },
	{ 0x25, KEY_8 },
	{ 0x26, KEY_9 },
	{ 0x2a, KEY_NUMERIC_STAR }, /* * key */
	{ 0x1d, KEY_0 },
	{ 0x29, KEY_SUBTITLE }, /* # key */
	{ 0x27, KEY_CLEAR },
	{ 0x34, KEY_SCREEN },
	{ 0x28, KEY_ENTER },
	{ 0x19, KEY_RED },
	{ 0x1a, KEY_GREEN },
	{ 0x1b, KEY_YELLOW },
	{ 0x1c, KEY_BLUE },
	{ 0x18, KEY_TEXT },
};

static struct rc_map_list videomate_m1f_map = {
	.map = {
		.scan    = videomate_m1f,
		.size    = ARRAY_SIZE(videomate_m1f),
		.rc_type = RC_TYPE_UNKNOWN,     /* Legacy IR type */
		.name    = RC_MAP_VIDEOMATE_M1F,
	}
};

static int __init init_rc_map_videomate_m1f(void)
{
	return rc_map_register(&videomate_m1f_map);
}

static void __exit exit_rc_map_videomate_m1f(void)
{
	rc_map_unregister(&videomate_m1f_map);
}

module_init(init_rc_map_videomate_m1f)
module_exit(exit_rc_map_videomate_m1f)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pavel Osnova <pvosnova@gmail.com>");
