// SPDX-License-Identifier: GPL-2.0-or-later
/* videomate-k100.h - Keytable for videomate_k100 Remote Controller
 *
 * keymap imported from ir-keymaps.c
 *
 * Copyright (c) 2010 by Pavel Osnova <pvosnova@gmail.com>
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table videomate_k100[] = {
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
	{ 0x1e, KEY_NUMERIC_1 },
	{ 0x1f, KEY_NUMERIC_2 },
	{ 0x20, KEY_NUMERIC_3 },
	{ 0x21, KEY_NUMERIC_4 },
	{ 0x22, KEY_NUMERIC_5 },
	{ 0x23, KEY_NUMERIC_6 },
	{ 0x24, KEY_NUMERIC_7 },
	{ 0x25, KEY_NUMERIC_8 },
	{ 0x26, KEY_NUMERIC_9 },
	{ 0x2a, KEY_NUMERIC_STAR }, /* * key */
	{ 0x1d, KEY_NUMERIC_0 },
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

static struct rc_map_list videomate_k100_map = {
	.map = {
		.scan     = videomate_k100,
		.size     = ARRAY_SIZE(videomate_k100),
		.rc_proto = RC_PROTO_UNKNOWN,     /* Legacy IR type */
		.name     = RC_MAP_VIDEOMATE_K100,
	}
};

static int __init init_rc_map_videomate_k100(void)
{
	return rc_map_register(&videomate_k100_map);
}

static void __exit exit_rc_map_videomate_k100(void)
{
	rc_map_unregister(&videomate_k100_map);
}

module_init(init_rc_map_videomate_k100)
module_exit(exit_rc_map_videomate_k100)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pavel Osnova <pvosnova@gmail.com>");
