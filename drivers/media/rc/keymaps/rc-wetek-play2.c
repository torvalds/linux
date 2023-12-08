// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (C) 2019 Christian Hewitt <christianshewitt@gmail.com>

#include <media/rc-map.h>
#include <linux/module.h>

//
// Keytable for the WeTek Play 2 STB remote control
//

static struct rc_map_table wetek_play2[] = {
	{ 0x5e5f02, KEY_POWER },
	{ 0x5e5f46, KEY_SLEEP }, // tv
	{ 0x5e5f10, KEY_MUTE },

	{ 0x5e5f22, KEY_1 },
	{ 0x5e5f23, KEY_2 },
	{ 0x5e5f24, KEY_3 },

	{ 0x5e5f25, KEY_4 },
	{ 0x5e5f26, KEY_5 },
	{ 0x5e5f27, KEY_6 },

	{ 0x5e5f28, KEY_7 },
	{ 0x5e5f29, KEY_8 },
	{ 0x5e5f30, KEY_9 },

	{ 0x5e5f71, KEY_BACK },
	{ 0x5e5f21, KEY_0 },
	{ 0x5e5f72, KEY_CAPSLOCK },

	// outer ring clockwide from top
	{ 0x5e5f03, KEY_HOME },
	{ 0x5e5f61, KEY_BACK },
	{ 0x5e5f77, KEY_CONFIG }, // mouse
	{ 0x5e5f83, KEY_EPG },
	{ 0x5e5f84, KEY_SCREEN }, // square
	{ 0x5e5f48, KEY_MENU },

	// inner ring
	{ 0x5e5f50, KEY_UP },
	{ 0x5e5f4b, KEY_DOWN },
	{ 0x5e5f4c, KEY_LEFT },
	{ 0x5e5f4d, KEY_RIGHT },
	{ 0x5e5f47, KEY_OK },

	{ 0x5e5f44, KEY_VOLUMEUP },
	{ 0x5e5f43, KEY_VOLUMEDOWN },
	{ 0x5e5f4f, KEY_FAVORITES },
	{ 0x5e5f82, KEY_SUBTITLE }, // txt
	{ 0x5e5f41, KEY_PAGEUP },
	{ 0x5e5f42, KEY_PAGEDOWN },

	{ 0x5e5f73, KEY_RED },
	{ 0x5e5f74, KEY_GREEN },
	{ 0x5e5f75, KEY_YELLOW },
	{ 0x5e5f76, KEY_BLUE },

	{ 0x5e5f67, KEY_PREVIOUSSONG },
	{ 0x5e5f79, KEY_REWIND },
	{ 0x5e5f80, KEY_FASTFORWARD },
	{ 0x5e5f81, KEY_NEXTSONG },

	{ 0x5e5f04, KEY_RECORD },
	{ 0x5e5f2c, KEY_PLAYPAUSE },
	{ 0x5e5f2b, KEY_STOP },
};

static struct rc_map_list wetek_play2_map = {
	.map = {
		.scan     = wetek_play2,
		.size     = ARRAY_SIZE(wetek_play2),
		.rc_proto = RC_PROTO_NECX,
		.name     = RC_MAP_WETEK_PLAY2,
	}
};

static int __init init_rc_map_wetek_play2(void)
{
	return rc_map_register(&wetek_play2_map);
}

static void __exit exit_rc_map_wetek_play2(void)
{
	rc_map_unregister(&wetek_play2_map);
}

module_init(init_rc_map_wetek_play2)
module_exit(exit_rc_map_wetek_play2)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Hewitt <christianshewitt@gmail.com");
