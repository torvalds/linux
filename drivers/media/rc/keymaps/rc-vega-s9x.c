// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (C) 2019 Christian Hewitt <christianshewitt@gmail.com>

#include <media/rc-map.h>
#include <linux/module.h>

//
// Keytable for the Tronsmart Vega S9x remote control
//

static struct rc_map_table vega_s9x[] = {
	{ 0x18, KEY_POWER },
	{ 0x17, KEY_MUTE }, // mouse

	{ 0x46, KEY_UP },
	{ 0x47, KEY_LEFT },
	{ 0x55, KEY_OK },
	{ 0x15, KEY_RIGHT },
	{ 0x16, KEY_DOWN },

	{ 0x06, KEY_HOME },
	{ 0x42, KEY_PLAYPAUSE},
	{ 0x40, KEY_BACK },

	{ 0x14, KEY_VOLUMEDOWN },
	{ 0x04, KEY_MENU },
	{ 0x10, KEY_VOLUMEUP },
};

static struct rc_map_list vega_s9x_map = {
	.map = {
		.scan     = vega_s9x,
		.size     = ARRAY_SIZE(vega_s9x),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_VEGA_S9X,
	}
};

static int __init init_rc_map_vega_s9x(void)
{
	return rc_map_register(&vega_s9x_map);
}

static void __exit exit_rc_map_vega_s9x(void)
{
	rc_map_unregister(&vega_s9x_map);
}

module_init(init_rc_map_vega_s9x)
module_exit(exit_rc_map_vega_s9x)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Hewitt <christianshewitt@gmail.com");
MODULE_DESCRIPTION("Tronsmart Vega S9x remote controller keytable");
