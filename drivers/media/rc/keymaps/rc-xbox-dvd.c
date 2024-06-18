// SPDX-License-Identifier: GPL-2.0+
// Keytable for Xbox DVD remote
// Copyright (c) 2018 by Benjamin Valentin <benpicco@googlemail.com>

#include <media/rc-map.h>
#include <linux/module.h>

/* based on lircd.conf.xbox */
static struct rc_map_table xbox_dvd[] = {
	{0xa0b, KEY_OK},
	{0xaa6, KEY_UP},
	{0xaa7, KEY_DOWN},
	{0xaa8, KEY_RIGHT},
	{0xaa9, KEY_LEFT},
	{0xac3, KEY_INFO},

	{0xac6, KEY_NUMERIC_9},
	{0xac7, KEY_NUMERIC_8},
	{0xac8, KEY_NUMERIC_7},
	{0xac9, KEY_NUMERIC_6},
	{0xaca, KEY_NUMERIC_5},
	{0xacb, KEY_NUMERIC_4},
	{0xacc, KEY_NUMERIC_3},
	{0xacd, KEY_NUMERIC_2},
	{0xace, KEY_NUMERIC_1},
	{0xacf, KEY_NUMERIC_0},

	{0xad5, KEY_ANGLE},
	{0xad8, KEY_BACK},
	{0xadd, KEY_PREVIOUSSONG},
	{0xadf, KEY_NEXTSONG},
	{0xae0, KEY_STOP},
	{0xae2, KEY_REWIND},
	{0xae3, KEY_FASTFORWARD},
	{0xae5, KEY_TITLE},
	{0xae6, KEY_PAUSE},
	{0xaea, KEY_PLAY},
	{0xaf7, KEY_MENU},
};

static struct rc_map_list xbox_dvd_map = {
	.map = {
		.scan     = xbox_dvd,
		.size     = ARRAY_SIZE(xbox_dvd),
		.rc_proto = RC_PROTO_XBOX_DVD,
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
MODULE_DESCRIPTION("Xbox DVD remote controller keytable");
