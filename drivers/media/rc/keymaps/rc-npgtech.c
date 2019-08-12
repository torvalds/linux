// SPDX-License-Identifier: GPL-2.0+
// npgtech.h - Keytable for npgtech Remote Controller
//
// keymap imported from ir-keymaps.c
//
// Copyright (c) 2010 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table npgtech[] = {
	{ 0x1d, KEY_SWITCHVIDEOMODE },	/* switch inputs */
	{ 0x2a, KEY_FRONT },

	{ 0x3e, KEY_NUMERIC_1 },
	{ 0x02, KEY_NUMERIC_2 },
	{ 0x06, KEY_NUMERIC_3 },
	{ 0x0a, KEY_NUMERIC_4 },
	{ 0x0e, KEY_NUMERIC_5 },
	{ 0x12, KEY_NUMERIC_6 },
	{ 0x16, KEY_NUMERIC_7 },
	{ 0x1a, KEY_NUMERIC_8 },
	{ 0x1e, KEY_NUMERIC_9 },
	{ 0x3a, KEY_NUMERIC_0 },
	{ 0x22, KEY_NUMLOCK },		/* -/-- */
	{ 0x20, KEY_REFRESH },

	{ 0x03, KEY_BRIGHTNESSDOWN },
	{ 0x28, KEY_AUDIO },
	{ 0x3c, KEY_CHANNELUP },
	{ 0x3f, KEY_VOLUMEDOWN },
	{ 0x2e, KEY_MUTE },
	{ 0x3b, KEY_VOLUMEUP },
	{ 0x00, KEY_CHANNELDOWN },
	{ 0x07, KEY_BRIGHTNESSUP },
	{ 0x2c, KEY_TEXT },

	{ 0x37, KEY_RECORD },
	{ 0x17, KEY_PLAY },
	{ 0x13, KEY_PAUSE },
	{ 0x26, KEY_STOP },
	{ 0x18, KEY_FASTFORWARD },
	{ 0x14, KEY_REWIND },
	{ 0x33, KEY_ZOOM },
	{ 0x32, KEY_KEYBOARD },
	{ 0x30, KEY_GOTO },		/* Pointing arrow */
	{ 0x36, KEY_MACRO },		/* Maximize/Minimize (yellow) */
	{ 0x0b, KEY_RADIO },
	{ 0x10, KEY_POWER },

};

static struct rc_map_list npgtech_map = {
	.map = {
		.scan     = npgtech,
		.size     = ARRAY_SIZE(npgtech),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_NPGTECH,
	}
};

static int __init init_rc_map_npgtech(void)
{
	return rc_map_register(&npgtech_map);
}

static void __exit exit_rc_map_npgtech(void)
{
	rc_map_unregister(&npgtech_map);
}

module_init(init_rc_map_npgtech)
module_exit(exit_rc_map_npgtech)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
