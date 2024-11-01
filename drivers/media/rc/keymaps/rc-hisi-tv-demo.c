// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Keytable for remote controller of HiSilicon tv demo board.
 *
 * Copyright (c) 2017 HiSilicon Technologies Co., Ltd.
 */

#include <linux/module.h>
#include <media/rc-map.h>

static struct rc_map_table hisi_tv_demo_keymap[] = {
	{ 0x00000092, KEY_NUMERIC_1},
	{ 0x00000093, KEY_NUMERIC_2},
	{ 0x000000cc, KEY_NUMERIC_3},
	{ 0x0000009f, KEY_NUMERIC_4},
	{ 0x0000008e, KEY_NUMERIC_5},
	{ 0x0000008f, KEY_NUMERIC_6},
	{ 0x000000c8, KEY_NUMERIC_7},
	{ 0x00000094, KEY_NUMERIC_8},
	{ 0x0000008a, KEY_NUMERIC_9},
	{ 0x0000008b, KEY_NUMERIC_0},
	{ 0x000000ce, KEY_ENTER},
	{ 0x000000ca, KEY_UP},
	{ 0x00000099, KEY_LEFT},
	{ 0x00000084, KEY_PAGEUP},
	{ 0x000000c1, KEY_RIGHT},
	{ 0x000000d2, KEY_DOWN},
	{ 0x00000089, KEY_PAGEDOWN},
	{ 0x000000d1, KEY_MUTE},
	{ 0x00000098, KEY_VOLUMEDOWN},
	{ 0x00000090, KEY_VOLUMEUP},
	{ 0x0000009c, KEY_POWER},
	{ 0x000000d6, KEY_STOP},
	{ 0x00000097, KEY_MENU},
	{ 0x000000cb, KEY_BACK},
	{ 0x000000da, KEY_PLAYPAUSE},
	{ 0x00000080, KEY_INFO},
	{ 0x000000c3, KEY_REWIND},
	{ 0x00000087, KEY_HOMEPAGE},
	{ 0x000000d0, KEY_FASTFORWARD},
	{ 0x000000c4, KEY_SOUND},
	{ 0x00000082, BTN_1},
	{ 0x000000c7, BTN_2},
	{ 0x00000086, KEY_PROGRAM},
	{ 0x000000d9, KEY_SUBTITLE},
	{ 0x00000085, KEY_ZOOM},
	{ 0x0000009b, KEY_RED},
	{ 0x0000009a, KEY_GREEN},
	{ 0x000000c0, KEY_YELLOW},
	{ 0x000000c2, KEY_BLUE},
	{ 0x0000009d, KEY_CHANNELDOWN},
	{ 0x000000cf, KEY_CHANNELUP},
};

static struct rc_map_list hisi_tv_demo_map = {
	.map = {
		.scan	  = hisi_tv_demo_keymap,
		.size	  = ARRAY_SIZE(hisi_tv_demo_keymap),
		.rc_proto = RC_PROTO_NEC,
		.name	  = RC_MAP_HISI_TV_DEMO,
	}
};

static int __init init_rc_map_hisi_tv_demo(void)
{
	return rc_map_register(&hisi_tv_demo_map);
}

static void __exit exit_rc_map_hisi_tv_demo(void)
{
	rc_map_unregister(&hisi_tv_demo_map);
}

module_init(init_rc_map_hisi_tv_demo)
module_exit(exit_rc_map_hisi_tv_demo)

MODULE_LICENSE("GPL v2");
