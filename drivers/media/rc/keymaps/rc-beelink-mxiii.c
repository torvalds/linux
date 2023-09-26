// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Christian Hewitt <christianshewitt@gmail.com>
 *
 */

#include <media/rc-map.h>
#include <linux/module.h>

/*
 * Keytable for the Beelink Mini MXIII remote control
 *
 */

static struct rc_map_table beelink_mxiii[] = {
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

static struct rc_map_list beelink_mxiii_map = {
	.map = {
		.scan     = beelink_mxiii,
		.size     = ARRAY_SIZE(beelink_mxiii),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_BEELINK_MXIII,
	}
};

static int __init init_rc_map_beelink_mxiii(void)
{
	return rc_map_register(&beelink_mxiii_map);
}

static void __exit exit_rc_map_beelink_mxiii(void)
{
	rc_map_unregister(&beelink_mxiii_map);
}

module_init(init_rc_map_beelink_mxiii)
module_exit(exit_rc_map_beelink_mxiii)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Hewitt <christianshewitt@gmail.com");
MODULE_DESCRIPTION("Beelink Mini MXIII remote controller keytable");
