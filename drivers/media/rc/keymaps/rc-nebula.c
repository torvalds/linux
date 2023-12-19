// SPDX-License-Identifier: GPL-2.0+
// nebula.h - Keytable for nebula Remote Controller
//
// keymap imported from ir-keymaps.c
//
// Copyright (c) 2010 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table nebula[] = {
	{ 0x0000, KEY_NUMERIC_0 },
	{ 0x0001, KEY_NUMERIC_1 },
	{ 0x0002, KEY_NUMERIC_2 },
	{ 0x0003, KEY_NUMERIC_3 },
	{ 0x0004, KEY_NUMERIC_4 },
	{ 0x0005, KEY_NUMERIC_5 },
	{ 0x0006, KEY_NUMERIC_6 },
	{ 0x0007, KEY_NUMERIC_7 },
	{ 0x0008, KEY_NUMERIC_8 },
	{ 0x0009, KEY_NUMERIC_9 },
	{ 0x000a, KEY_TV },
	{ 0x000b, KEY_AUX },
	{ 0x000c, KEY_DVD },
	{ 0x000d, KEY_POWER },
	{ 0x000e, KEY_CAMERA },	/* labelled 'Picture' */
	{ 0x000f, KEY_AUDIO },
	{ 0x0010, KEY_INFO },
	{ 0x0011, KEY_F13 },	/* 16:9 */
	{ 0x0012, KEY_F14 },	/* 14:9 */
	{ 0x0013, KEY_EPG },
	{ 0x0014, KEY_EXIT },
	{ 0x0015, KEY_MENU },
	{ 0x0016, KEY_UP },
	{ 0x0017, KEY_DOWN },
	{ 0x0018, KEY_LEFT },
	{ 0x0019, KEY_RIGHT },
	{ 0x001a, KEY_ENTER },
	{ 0x001b, KEY_CHANNELUP },
	{ 0x001c, KEY_CHANNELDOWN },
	{ 0x001d, KEY_VOLUMEUP },
	{ 0x001e, KEY_VOLUMEDOWN },
	{ 0x001f, KEY_RED },
	{ 0x0020, KEY_GREEN },
	{ 0x0021, KEY_YELLOW },
	{ 0x0022, KEY_BLUE },
	{ 0x0023, KEY_SUBTITLE },
	{ 0x0024, KEY_F15 },	/* AD */
	{ 0x0025, KEY_TEXT },
	{ 0x0026, KEY_MUTE },
	{ 0x0027, KEY_REWIND },
	{ 0x0028, KEY_STOP },
	{ 0x0029, KEY_PLAY },
	{ 0x002a, KEY_FASTFORWARD },
	{ 0x002b, KEY_F16 },	/* chapter */
	{ 0x002c, KEY_PAUSE },
	{ 0x002d, KEY_PLAY },
	{ 0x002e, KEY_RECORD },
	{ 0x002f, KEY_F17 },	/* picture in picture */
	{ 0x0030, KEY_KPPLUS },	/* zoom in */
	{ 0x0031, KEY_KPMINUS },	/* zoom out */
	{ 0x0032, KEY_F18 },	/* capture */
	{ 0x0033, KEY_F19 },	/* web */
	{ 0x0034, KEY_EMAIL },
	{ 0x0035, KEY_PHONE },
	{ 0x0036, KEY_PC },
};

static struct rc_map_list nebula_map = {
	.map = {
		.scan     = nebula,
		.size     = ARRAY_SIZE(nebula),
		.rc_proto = RC_PROTO_RC5,
		.name     = RC_MAP_NEBULA,
	}
};

static int __init init_rc_map_nebula(void)
{
	return rc_map_register(&nebula_map);
}

static void __exit exit_rc_map_nebula(void)
{
	rc_map_unregister(&nebula_map);
}

module_init(init_rc_map_nebula)
module_exit(exit_rc_map_nebula)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_DESCRIPTION("nebula remote controller keytable");
