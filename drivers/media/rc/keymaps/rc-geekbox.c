// SPDX-License-Identifier: GPL-2.0-only
/*
 * Keytable for the GeekBox remote controller
 *
 * Copyright (C) 2017 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table geekbox[] = {
	{ 0x01, KEY_BACK },
	{ 0x02, KEY_DOWN },
	{ 0x03, KEY_UP },
	{ 0x07, KEY_OK },
	{ 0x0b, KEY_VOLUMEUP },
	{ 0x0e, KEY_LEFT },
	{ 0x13, KEY_MENU },
	{ 0x14, KEY_POWER },
	{ 0x1a, KEY_RIGHT },
	{ 0x48, KEY_HOME },
	{ 0x58, KEY_VOLUMEDOWN },
	{ 0x5c, KEY_SCREEN },
};

static struct rc_map_list geekbox_map = {
	.map = {
		.scan     = geekbox,
		.size     = ARRAY_SIZE(geekbox),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_GEEKBOX,
	}
};

static int __init init_rc_map_geekbox(void)
{
	return rc_map_register(&geekbox_map);
}

static void __exit exit_rc_map_geekbox(void)
{
	rc_map_unregister(&geekbox_map);
}

module_init(init_rc_map_geekbox)
module_exit(exit_rc_map_geekbox)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Blumenstingl <martin.blumenstingl@googlemail.com>");
MODULE_DESCRIPTION("GeekBox remote controller keytable");
