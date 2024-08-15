// SPDX-License-Identifier: GPL-2.0+

// Keytable for the Pine64 IR Remote Controller
// Copyright (c) 2017 Jonas Karlman

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table pine64[] = {
	{ 0x40404d, KEY_POWER },
	{ 0x40401f, KEY_WWW },
	{ 0x40400a, KEY_MUTE },

	{ 0x404017, KEY_VOLUMEDOWN },
	{ 0x404018, KEY_VOLUMEUP },

	{ 0x404010, KEY_LEFT },
	{ 0x404011, KEY_RIGHT },
	{ 0x40400b, KEY_UP },
	{ 0x40400e, KEY_DOWN },
	{ 0x40400d, KEY_OK },

	{ 0x40401d, KEY_MENU },
	{ 0x40401a, KEY_HOME },

	{ 0x404045, KEY_BACK },

	{ 0x404001, KEY_NUMERIC_1 },
	{ 0x404002, KEY_NUMERIC_2 },
	{ 0x404003, KEY_NUMERIC_3 },
	{ 0x404004, KEY_NUMERIC_4 },
	{ 0x404005, KEY_NUMERIC_5 },
	{ 0x404006, KEY_NUMERIC_6 },
	{ 0x404007, KEY_NUMERIC_7 },
	{ 0x404008, KEY_NUMERIC_8 },
	{ 0x404009, KEY_NUMERIC_9 },
	{ 0x40400c, KEY_BACKSPACE },
	{ 0x404000, KEY_NUMERIC_0 },
	{ 0x404047, KEY_EPG }, // mouse
};

static struct rc_map_list pine64_map = {
	.map = {
		.scan     = pine64,
		.size     = ARRAY_SIZE(pine64),
		.rc_proto = RC_PROTO_NECX,
		.name     = RC_MAP_PINE64,
	}
};

static int __init init_rc_map_pine64(void)
{
	return rc_map_register(&pine64_map);
}

static void __exit exit_rc_map_pine64(void)
{
	rc_map_unregister(&pine64_map);
}

module_init(init_rc_map_pine64)
module_exit(exit_rc_map_pine64)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jonas Karlman");
