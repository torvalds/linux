// SPDX-License-Identifier: GPL-2.0+
// Keytable for Xbox DVD remote
// Copyright (c) 2018 by Benjamin Valentin <benpicco@googlemail.com>

#include <media/rc-map.h>
#include <linux/module.h>

/* based on lircd.conf.xbox */
static struct rc_map_table xbox_dvd[] = {
	{0x0b, KEY_OK},
	{0xa6, KEY_UP},
	{0xa7, KEY_DOWN},
	{0xa8, KEY_RIGHT},
	{0xa9, KEY_LEFT},
	{0xc3, KEY_INFO},

	{0xc6, KEY_9},
	{0xc7, KEY_8},
	{0xc8, KEY_7},
	{0xc9, KEY_6},
	{0xca, KEY_5},
	{0xcb, KEY_4},
	{0xcc, KEY_3},
	{0xcd, KEY_2},
	{0xce, KEY_1},
	{0xcf, KEY_0},

	{0xd5, KEY_ANGLE},
	{0xd8, KEY_BACK},
	{0xdd, KEY_PREVIOUSSONG},
	{0xdf, KEY_NEXTSONG},
	{0xe0, KEY_STOP},
	{0xe2, KEY_REWIND},
	{0xe3, KEY_FASTFORWARD},
	{0xe5, KEY_TITLE},
	{0xe6, KEY_PAUSE},
	{0xea, KEY_PLAY},
	{0xf7, KEY_MENU},
};

static struct rc_map_list xbox_dvd_map = {
	.map = {
		.scan     = xbox_dvd,
		.size     = ARRAY_SIZE(xbox_dvd),
		.rc_proto = RC_PROTO_UNKNOWN,
		.name     = RC_MAP_XBOX_DVD,
	}
};

static int __init init_rc_map(void)
{
	return rc_map_register(&xbox_dvd_map);
}

static void __exit exit_rc_map(void)
{
	rc_map_unregister(&xbox_dvd_map);
}

module_init(init_rc_map)
module_exit(exit_rc_map)

MODULE_LICENSE("GPL");
