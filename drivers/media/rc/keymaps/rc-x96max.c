// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (C) 2019 Christian Hewitt <christianshewitt@gmail.com>

#include <media/rc-map.h>
#include <linux/module.h>

//
// Keytable for the X96-max STB remote control
//

static struct rc_map_table x96max[] = {
	{ 0x140, KEY_POWER },

	// ** TV CONTROL **
	// SET
	// AV/TV
	// POWER
	// VOLUME UP
	// VOLUME DOWN

	{ 0x118, KEY_VOLUMEUP },
	{ 0x110, KEY_VOLUMEDOWN },

	{ 0x143, KEY_MUTE }, // config

	{ 0x100, KEY_EPG }, // mouse
	{ 0x119, KEY_BACK },

	{ 0x116, KEY_UP },
	{ 0x151, KEY_LEFT },
	{ 0x150, KEY_RIGHT },
	{ 0x11a, KEY_DOWN },
	{ 0x113, KEY_OK },

	{ 0x111, KEY_HOME },
	{ 0x14c, KEY_CONTEXT_MENU },

	{ 0x159, KEY_PREVIOUS },
	{ 0x15a, KEY_PLAYPAUSE },
	{ 0x158, KEY_NEXT },

	{ 0x147, KEY_MENU }, // @ key
	{ 0x101, KEY_NUMERIC_0 },
	{ 0x142, KEY_BACKSPACE },

	{ 0x14e, KEY_NUMERIC_1 },
	{ 0x10d, KEY_NUMERIC_2 },
	{ 0x10c, KEY_NUMERIC_3 },

	{ 0x14a, KEY_NUMERIC_4 },
	{ 0x109, KEY_NUMERIC_5 },
	{ 0x108, KEY_NUMERIC_6 },

	{ 0x146, KEY_NUMERIC_7 },
	{ 0x105, KEY_NUMERIC_8 },
	{ 0x104, KEY_NUMERIC_9 },
};

static struct rc_map_list x96max_map = {
	.map = {
		.scan     = x96max,
		.size     = ARRAY_SIZE(x96max),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_X96MAX,
	}
};

static int __init init_rc_map_x96max(void)
{
	return rc_map_register(&x96max_map);
}

static void __exit exit_rc_map_x96max(void)
{
	rc_map_unregister(&x96max_map);
}

module_init(init_rc_map_x96max)
module_exit(exit_rc_map_x96max)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Hewitt <christianshewitt@gmail.com");
