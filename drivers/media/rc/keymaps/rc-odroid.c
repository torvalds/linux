// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (C) 2019 Christian Hewitt <christianshewitt@gmail.com>

#include <media/rc-map.h>
#include <linux/module.h>

//
// Keytable for the HardKernel ODROID remote control
//

static struct rc_map_table odroid[] = {
	{ 0xb2dc, KEY_POWER },

	{ 0xb288, KEY_MUTE },
	{ 0xb282, KEY_HOME },

	{ 0xb2ca, KEY_UP },
	{ 0xb299, KEY_LEFT },
	{ 0xb2ce, KEY_OK },
	{ 0xb2c1, KEY_RIGHT },
	{ 0xb2d2, KEY_DOWN },

	{ 0xb2c5, KEY_MENU },
	{ 0xb29a, KEY_BACK },

	{ 0xb281, KEY_VOLUMEDOWN },
	{ 0xb280, KEY_VOLUMEUP },
};

static struct rc_map_list odroid_map = {
	.map = {
		.scan     = odroid,
		.size     = ARRAY_SIZE(odroid),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_ODROID,
	}
};

static int __init init_rc_map_odroid(void)
{
	return rc_map_register(&odroid_map);
}

static void __exit exit_rc_map_odroid(void)
{
	rc_map_unregister(&odroid_map);
}

module_init(init_rc_map_odroid)
module_exit(exit_rc_map_odroid)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Hewitt <christianshewitt@gmail.com");
