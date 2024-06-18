// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2018 Christian Hewitt

#include <media/rc-map.h>
#include <linux/module.h>

/*
 * Keymap for the Tanix TX5 max STB remote control
 */

static struct rc_map_table tanix_tx5max[] = {
	{ 0x40404d, KEY_POWER },
	{ 0x404043, KEY_MUTE },

	{ 0x404017, KEY_VOLUMEDOWN },
	{ 0x404018, KEY_VOLUMEUP },

	{ 0x40400b, KEY_UP },
	{ 0x404010, KEY_LEFT },
	{ 0x404011, KEY_RIGHT },
	{ 0x40400e, KEY_DOWN },
	{ 0x40400d, KEY_OK },

	{ 0x40401a, KEY_HOME },
	{ 0x404045, KEY_MENU },
	{ 0x404042, KEY_BACK },

	{ 0x404001, KEY_1 },
	{ 0x404002, KEY_2 },
	{ 0x404003, KEY_3 },

	{ 0x404004, KEY_4 },
	{ 0x404005, KEY_5 },
	{ 0x404006, KEY_6 },

	{ 0x404007, KEY_7 },
	{ 0x404008, KEY_8 },
	{ 0x404009, KEY_9 },

	{ 0x404047, KEY_SUBTITLE }, // mouse
	{ 0x404000, KEY_0 },
	{ 0x40400c, KEY_DELETE },
};

static struct rc_map_list tanix_tx5max_map = {
	.map = {
		.scan     = tanix_tx5max,
		.size     = ARRAY_SIZE(tanix_tx5max),
		.rc_proto = RC_PROTO_NECX,
		.name     = RC_MAP_TANIX_TX5MAX,
	}
};

static int __init init_rc_map_tanix_tx5max(void)
{
	return rc_map_register(&tanix_tx5max_map);
}

static void __exit exit_rc_map_tanix_tx5max(void)
{
	rc_map_unregister(&tanix_tx5max_map);
}

module_init(init_rc_map_tanix_tx5max)
module_exit(exit_rc_map_tanix_tx5max)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Hewitt <christianshewitt@gmail.com>");
MODULE_DESCRIPTION("Tanix TX5 max STB remote controller keytable");
