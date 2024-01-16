// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2019 Clément Péron

#include <media/rc-map.h>
#include <linux/module.h>

/*
 * Keymap for the Beelink GS1 remote control
 */

static struct rc_map_table beelink_gs1_table[] = {
	/*
	 * TV Keys (Power, Learn and Volume)
	 * { 0x40400d, KEY_TV },
	 * { 0x80f1, KEY_TV },
	 * { 0x80f3, KEY_TV },
	 * { 0x80f4, KEY_TV },
	 */

	{ 0x8051, KEY_POWER },
	{ 0x804d, KEY_MUTE },
	{ 0x8040, KEY_CONFIG },

	{ 0x8026, KEY_UP },
	{ 0x8028, KEY_DOWN },
	{ 0x8025, KEY_LEFT },
	{ 0x8027, KEY_RIGHT },
	{ 0x800d, KEY_OK },

	{ 0x8053, KEY_HOME },
	{ 0x80bc, KEY_MEDIA },
	{ 0x801b, KEY_BACK },
	{ 0x8049, KEY_MENU },

	{ 0x804e, KEY_VOLUMEUP },
	{ 0x8056, KEY_VOLUMEDOWN },

	{ 0x8054, KEY_SUBTITLE }, /* Web */
	{ 0x8052, KEY_EPG }, /* Media */

	{ 0x8041, KEY_CHANNELUP },
	{ 0x8042, KEY_CHANNELDOWN },

	{ 0x8031, KEY_1 },
	{ 0x8032, KEY_2 },
	{ 0x8033, KEY_3 },

	{ 0x8034, KEY_4 },
	{ 0x8035, KEY_5 },
	{ 0x8036, KEY_6 },

	{ 0x8037, KEY_7 },
	{ 0x8038, KEY_8 },
	{ 0x8039, KEY_9 },

	{ 0x8044, KEY_DELETE },
	{ 0x8030, KEY_0 },
	{ 0x8058, KEY_MODE }, /* # Input Method */
};

static struct rc_map_list beelink_gs1_map = {
	.map = {
		.scan     = beelink_gs1_table,
		.size     = ARRAY_SIZE(beelink_gs1_table),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_BEELINK_GS1,
	}
};

static int __init init_rc_map_beelink_gs1(void)
{
	return rc_map_register(&beelink_gs1_map);
}

static void __exit exit_rc_map_beelink_gs1(void)
{
	rc_map_unregister(&beelink_gs1_map);
}

module_init(init_rc_map_beelink_gs1)
module_exit(exit_rc_map_beelink_gs1)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Clément Péron <peron.clem@gmail.com>");
