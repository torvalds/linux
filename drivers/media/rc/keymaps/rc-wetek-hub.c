// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2018 Christian Hewitt

#include <media/rc-map.h>
#include <linux/module.h>

/*
 * This keymap is used with the WeTek Hub STB.
 */

static struct rc_map_table wetek_hub[] = {
	{ 0x77f1, KEY_POWER },

	{ 0x77f2, KEY_HOME },
	{ 0x77f3, KEY_MUTE }, // mouse

	{ 0x77f4, KEY_UP },
	{ 0x77f5, KEY_DOWN },
	{ 0x77f6, KEY_LEFT },
	{ 0x77f7, KEY_RIGHT },
	{ 0x77f8, KEY_OK },

	{ 0x77f9, KEY_BACK },
	{ 0x77fa, KEY_MENU },

	{ 0x77fb, KEY_VOLUMEUP },
	{ 0x77fc, KEY_VOLUMEDOWN },
};

static struct rc_map_list wetek_hub_map = {
	.map = {
		.scan     = wetek_hub,
		.size     = ARRAY_SIZE(wetek_hub),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_WETEK_HUB,
	}
};

static int __init init_rc_map_wetek_hub(void)
{
	return rc_map_register(&wetek_hub_map);
}

static void __exit exit_rc_map_wetek_hub(void)
{
	rc_map_unregister(&wetek_hub_map);
}

module_init(init_rc_map_wetek_hub)
module_exit(exit_rc_map_wetek_hub)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Hewitt <christianshewitt@gmail.com>");
