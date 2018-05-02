// SPDX-License-Identifier: GPL-2.0+
// avermedia-a16d.h - Keytable for avermedia_a16d Remote Controller
//
// keymap imported from ir-keymaps.c
//
// Copyright (c) 2010 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table avermedia_a16d[] = {
	{ 0x20, KEY_LIST},
	{ 0x00, KEY_POWER},
	{ 0x28, KEY_1},
	{ 0x18, KEY_2},
	{ 0x38, KEY_3},
	{ 0x24, KEY_4},
	{ 0x14, KEY_5},
	{ 0x34, KEY_6},
	{ 0x2c, KEY_7},
	{ 0x1c, KEY_8},
	{ 0x3c, KEY_9},
	{ 0x12, KEY_SUBTITLE},
	{ 0x22, KEY_0},
	{ 0x32, KEY_REWIND},
	{ 0x3a, KEY_SHUFFLE},
	{ 0x02, KEY_PRINT},
	{ 0x11, KEY_CHANNELDOWN},
	{ 0x31, KEY_CHANNELUP},
	{ 0x0c, KEY_ZOOM},
	{ 0x1e, KEY_VOLUMEDOWN},
	{ 0x3e, KEY_VOLUMEUP},
	{ 0x0a, KEY_MUTE},
	{ 0x04, KEY_AUDIO},
	{ 0x26, KEY_RECORD},
	{ 0x06, KEY_PLAY},
	{ 0x36, KEY_STOP},
	{ 0x16, KEY_PAUSE},
	{ 0x2e, KEY_REWIND},
	{ 0x0e, KEY_FASTFORWARD},
	{ 0x30, KEY_TEXT},
	{ 0x21, KEY_GREEN},
	{ 0x01, KEY_BLUE},
	{ 0x08, KEY_EPG},
	{ 0x2a, KEY_MENU},
};

static struct rc_map_list avermedia_a16d_map = {
	.map = {
		.scan     = avermedia_a16d,
		.size     = ARRAY_SIZE(avermedia_a16d),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_AVERMEDIA_A16D,
	}
};

static int __init init_rc_map_avermedia_a16d(void)
{
	return rc_map_register(&avermedia_a16d_map);
}

static void __exit exit_rc_map_avermedia_a16d(void)
{
	rc_map_unregister(&avermedia_a16d_map);
}

module_init(init_rc_map_avermedia_a16d)
module_exit(exit_rc_map_avermedia_a16d)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
