// SPDX-License-Identifier: GPL-2.0+
// kaiomy.h - Keytable for kaiomy Remote Controller
//
// keymap imported from ir-keymaps.c
//
// Copyright (c) 2010 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

/* Kaiomy TVnPC U2
   Mauro Carvalho Chehab <mchehab@kernel.org>
 */

static struct rc_map_table kaiomy[] = {
	{ 0x43, KEY_POWER2},
	{ 0x01, KEY_LIST},
	{ 0x0b, KEY_ZOOM},
	{ 0x03, KEY_POWER},

	{ 0x04, KEY_NUMERIC_1},
	{ 0x08, KEY_NUMERIC_2},
	{ 0x02, KEY_NUMERIC_3},

	{ 0x0f, KEY_NUMERIC_4},
	{ 0x05, KEY_NUMERIC_5},
	{ 0x06, KEY_NUMERIC_6},

	{ 0x0c, KEY_NUMERIC_7},
	{ 0x0d, KEY_NUMERIC_8},
	{ 0x0a, KEY_NUMERIC_9},

	{ 0x11, KEY_NUMERIC_0},

	{ 0x09, KEY_CHANNELUP},
	{ 0x07, KEY_CHANNELDOWN},

	{ 0x0e, KEY_VOLUMEUP},
	{ 0x13, KEY_VOLUMEDOWN},

	{ 0x10, KEY_HOME},
	{ 0x12, KEY_ENTER},

	{ 0x14, KEY_RECORD},
	{ 0x15, KEY_STOP},
	{ 0x16, KEY_PLAY},
	{ 0x17, KEY_MUTE},

	{ 0x18, KEY_UP},
	{ 0x19, KEY_DOWN},
	{ 0x1a, KEY_LEFT},
	{ 0x1b, KEY_RIGHT},

	{ 0x1c, KEY_RED},
	{ 0x1d, KEY_GREEN},
	{ 0x1e, KEY_YELLOW},
	{ 0x1f, KEY_BLUE},
};

static struct rc_map_list kaiomy_map = {
	.map = {
		.scan     = kaiomy,
		.size     = ARRAY_SIZE(kaiomy),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_KAIOMY,
	}
};

static int __init init_rc_map_kaiomy(void)
{
	return rc_map_register(&kaiomy_map);
}

static void __exit exit_rc_map_kaiomy(void)
{
	rc_map_unregister(&kaiomy_map);
}

module_init(init_rc_map_kaiomy)
module_exit(exit_rc_map_kaiomy)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_DESCRIPTION("Kaiomy TVnPC U2 remote controller keytable");
