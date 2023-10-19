// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (C) 2019 Christian Hewitt <christianshewitt@gmail.com>

/*
 * Keytable for the Khadas VIM/EDGE SBC remote control
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table khadas[] = {
	{ 0x14, KEY_POWER },

	{ 0x03, KEY_UP },
	{ 0x02, KEY_DOWN },
	{ 0x0e, KEY_LEFT },
	{ 0x1a, KEY_RIGHT },
	{ 0x07, KEY_OK },

	{ 0x01, KEY_BACK },
	{ 0x5b, KEY_MUTE }, // mouse
	{ 0x13, KEY_MENU },

	{ 0x58, KEY_VOLUMEDOWN },
	{ 0x0b, KEY_VOLUMEUP },

	{ 0x48, KEY_HOME },
};

static struct rc_map_list khadas_map = {
	.map = {
		.scan     = khadas,
		.size     = ARRAY_SIZE(khadas),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_KHADAS,
	}
};

static int __init init_rc_map_khadas(void)
{
	return rc_map_register(&khadas_map);
}

static void __exit exit_rc_map_khadas(void)
{
	rc_map_unregister(&khadas_map);
}

module_init(init_rc_map_khadas)
module_exit(exit_rc_map_khadas)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Hewitt <christianshewitt@gmail.com>");
