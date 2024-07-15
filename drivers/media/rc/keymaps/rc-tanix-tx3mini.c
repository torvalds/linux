// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2018 Christian Hewitt

#include <media/rc-map.h>
#include <linux/module.h>

/*
 * Keymap for the Tanix TX3 mini STB remote control
 */

static struct rc_map_table tanix_tx3mini[] = {
	{ 0x8051, KEY_POWER },
	{ 0x804d, KEY_MUTE },

	{ 0x8009, KEY_RED },
	{ 0x8011, KEY_GREEN },
	{ 0x8054, KEY_YELLOW },
	{ 0x804f, KEY_BLUE },

	{ 0x8056, KEY_VOLUMEDOWN },
	{ 0x80bd, KEY_PREVIOUS },
	{ 0x80bb, KEY_NEXT },
	{ 0x804e, KEY_VOLUMEUP },

	{ 0x8053, KEY_HOME },
	{ 0x801b, KEY_BACK },

	{ 0x8026, KEY_UP },
	{ 0x8028, KEY_DOWN },
	{ 0x8025, KEY_LEFT },
	{ 0x8027, KEY_RIGHT },
	{ 0x800d, KEY_OK },

	{ 0x8049, KEY_MENU },
	{ 0x8052, KEY_EPG }, // mouse

	{ 0x8031, KEY_1 },
	{ 0x8032, KEY_2 },
	{ 0x8033, KEY_3 },

	{ 0x8034, KEY_4 },
	{ 0x8035, KEY_5 },
	{ 0x8036, KEY_6 },

	{ 0x8037, KEY_7 },
	{ 0x8038, KEY_8 },
	{ 0x8039, KEY_9 },

	{ 0x8058, KEY_SUBTITLE }, // 1/a
	{ 0x8030, KEY_0 },
	{ 0x8044, KEY_DELETE },
};

static struct rc_map_list tanix_tx3mini_map = {
	.map = {
		.scan     = tanix_tx3mini,
		.size     = ARRAY_SIZE(tanix_tx3mini),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_TANIX_TX3MINI,
	}
};

static int __init init_rc_map_tanix_tx3mini(void)
{
	return rc_map_register(&tanix_tx3mini_map);
}

static void __exit exit_rc_map_tanix_tx3mini(void)
{
	rc_map_unregister(&tanix_tx3mini_map);
}

module_init(init_rc_map_tanix_tx3mini)
module_exit(exit_rc_map_tanix_tx3mini)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Hewitt <christianshewitt@gmail.com>");
MODULE_DESCRIPTION("Tanix TX3 mini STB remote controller keytable");
