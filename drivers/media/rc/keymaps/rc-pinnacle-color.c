// SPDX-License-Identifier: GPL-2.0+
// pinnacle-color.h - Keytable for pinnacle_color Remote Controller
//
// keymap imported from ir-keymaps.c
//
// Copyright (c) 2010 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table pinnacle_color[] = {
	{ 0x59, KEY_MUTE },
	{ 0x4a, KEY_POWER },

	{ 0x18, KEY_TEXT },
	{ 0x26, KEY_TV },
	{ 0x3d, KEY_PRINT },

	{ 0x48, KEY_RED },
	{ 0x04, KEY_GREEN },
	{ 0x11, KEY_YELLOW },
	{ 0x00, KEY_BLUE },

	{ 0x2d, KEY_VOLUMEUP },
	{ 0x1e, KEY_VOLUMEDOWN },

	{ 0x49, KEY_MENU },

	{ 0x16, KEY_CHANNELUP },
	{ 0x17, KEY_CHANNELDOWN },

	{ 0x20, KEY_UP },
	{ 0x21, KEY_DOWN },
	{ 0x22, KEY_LEFT },
	{ 0x23, KEY_RIGHT },
	{ 0x0d, KEY_SELECT },

	{ 0x08, KEY_BACK },
	{ 0x07, KEY_REFRESH },

	{ 0x2f, KEY_ZOOM },
	{ 0x29, KEY_RECORD },

	{ 0x4b, KEY_PAUSE },
	{ 0x4d, KEY_REWIND },
	{ 0x2e, KEY_PLAY },
	{ 0x4e, KEY_FORWARD },
	{ 0x53, KEY_PREVIOUS },
	{ 0x4c, KEY_STOP },
	{ 0x54, KEY_NEXT },

	{ 0x69, KEY_NUMERIC_0 },
	{ 0x6a, KEY_NUMERIC_1 },
	{ 0x6b, KEY_NUMERIC_2 },
	{ 0x6c, KEY_NUMERIC_3 },
	{ 0x6d, KEY_NUMERIC_4 },
	{ 0x6e, KEY_NUMERIC_5 },
	{ 0x6f, KEY_NUMERIC_6 },
	{ 0x70, KEY_NUMERIC_7 },
	{ 0x71, KEY_NUMERIC_8 },
	{ 0x72, KEY_NUMERIC_9 },

	{ 0x74, KEY_CHANNEL },
	{ 0x0a, KEY_BACKSPACE },
};

static struct rc_map_list pinnacle_color_map = {
	.map = {
		.scan     = pinnacle_color,
		.size     = ARRAY_SIZE(pinnacle_color),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_PINNACLE_COLOR,
	}
};

static int __init init_rc_map_pinnacle_color(void)
{
	return rc_map_register(&pinnacle_color_map);
}

static void __exit exit_rc_map_pinnacle_color(void)
{
	rc_map_unregister(&pinnacle_color_map);
}

module_init(init_rc_map_pinnacle_color)
module_exit(exit_rc_map_pinnacle_color)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
