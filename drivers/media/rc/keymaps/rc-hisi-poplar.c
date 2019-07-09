// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Keytable for remote controller of HiSilicon poplar board.
 *
 * Copyright (c) 2017 HiSilicon Technologies Co., Ltd.
 */

#include <linux/module.h>
#include <media/rc-map.h>

static struct rc_map_table hisi_poplar_keymap[] = {
	{ 0x0000b292, KEY_1},
	{ 0x0000b293, KEY_2},
	{ 0x0000b2cc, KEY_3},
	{ 0x0000b28e, KEY_4},
	{ 0x0000b28f, KEY_5},
	{ 0x0000b2c8, KEY_6},
	{ 0x0000b28a, KEY_7},
	{ 0x0000b28b, KEY_8},
	{ 0x0000b2c4, KEY_9},
	{ 0x0000b287, KEY_0},
	{ 0x0000b282, KEY_HOMEPAGE},
	{ 0x0000b2ca, KEY_UP},
	{ 0x0000b299, KEY_LEFT},
	{ 0x0000b2c1, KEY_RIGHT},
	{ 0x0000b2d2, KEY_DOWN},
	{ 0x0000b2c5, KEY_DELETE},
	{ 0x0000b29c, KEY_MUTE},
	{ 0x0000b281, KEY_VOLUMEDOWN},
	{ 0x0000b280, KEY_VOLUMEUP},
	{ 0x0000b2dc, KEY_POWER},
	{ 0x0000b29a, KEY_MENU},
	{ 0x0000b28d, KEY_SETUP},
	{ 0x0000b2c5, KEY_BACK},
	{ 0x0000b295, KEY_PLAYPAUSE},
	{ 0x0000b2ce, KEY_ENTER},
	{ 0x0000b285, KEY_CHANNELUP},
	{ 0x0000b286, KEY_CHANNELDOWN},
	{ 0x0000b2da, KEY_NUMERIC_STAR},
	{ 0x0000b2d0, KEY_NUMERIC_POUND},
};

static struct rc_map_list hisi_poplar_map = {
	.map = {
		.scan	  = hisi_poplar_keymap,
		.size	  = ARRAY_SIZE(hisi_poplar_keymap),
		.rc_proto = RC_PROTO_NEC,
		.name	  = RC_MAP_HISI_POPLAR,
	}
};

static int __init init_rc_map_hisi_poplar(void)
{
	return rc_map_register(&hisi_poplar_map);
}

static void __exit exit_rc_map_hisi_poplar(void)
{
	rc_map_unregister(&hisi_poplar_map);
}

module_init(init_rc_map_hisi_poplar)
module_exit(exit_rc_map_hisi_poplar)

MODULE_LICENSE("GPL v2");
