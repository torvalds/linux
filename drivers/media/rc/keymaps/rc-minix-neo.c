// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (C) 2021 Christian Hewitt <christianshewitt@gmail.com>

#include <media/rc-map.h>
#include <linux/module.h>

//
// Keytable for the Minix NEO remote control
//

static struct rc_map_table minix_neo[] = {

	{ 0x118, KEY_POWER },

	{ 0x146, KEY_UP },
	{ 0x116, KEY_DOWN },
	{ 0x147, KEY_LEFT },
	{ 0x115, KEY_RIGHT },
	{ 0x155, KEY_ENTER },

	{ 0x110, KEY_VOLUMEDOWN },
	{ 0x140, KEY_BACK },
	{ 0x114, KEY_VOLUMEUP },

	{ 0x10d, KEY_HOME },
	{ 0x104, KEY_MENU },
	{ 0x112, KEY_CONFIG },

};

static struct rc_map_list minix_neo_map = {
	.map = {
		.scan     = minix_neo,
		.size     = ARRAY_SIZE(minix_neo),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_MINIX_NEO,
	}
};

static int __init init_rc_map_minix_neo(void)
{
	return rc_map_register(&minix_neo_map);
}

static void __exit exit_rc_map_minix_neo(void)
{
	rc_map_unregister(&minix_neo_map);
}

module_init(init_rc_map_minix_neo)
module_exit(exit_rc_map_minix_neo)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Hewitt <christianshewitt@gmail.com");
MODULE_DESCRIPTION("Minix NEO remote controller keytable");
