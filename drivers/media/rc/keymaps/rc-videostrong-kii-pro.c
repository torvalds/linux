// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (C) 2019 Mohammad Rasim <mohammad.rasim96@gmail.com>

#include <media/rc-map.h>
#include <linux/module.h>

//
// Keytable for the Videostrong KII Pro STB remote control
//

static struct rc_map_table kii_pro[] = {
	{ 0x59, KEY_POWER },
	{ 0x19, KEY_MUTE },
	{ 0x42, KEY_RED },
	{ 0x40, KEY_GREEN },
	{ 0x00, KEY_YELLOW },
	{ 0x03, KEY_BLUE },
	{ 0x4a, KEY_BACK },
	{ 0x48, KEY_FORWARD },
	{ 0x08, KEY_PREVIOUSSONG},
	{ 0x0b, KEY_NEXTSONG},
	{ 0x46, KEY_PLAYPAUSE },
	{ 0x44, KEY_STOP },
	{ 0x1f, KEY_FAVORITES},	//KEY_F5?
	{ 0x04, KEY_PVR },
	{ 0x4d, KEY_EPG },
	{ 0x02, KEY_INFO },
	{ 0x09, KEY_SUBTITLE },
	{ 0x01, KEY_AUDIO },
	{ 0x0d, KEY_HOMEPAGE },
	{ 0x11, KEY_TV },	// DTV ?
	{ 0x06, KEY_UP },
	{ 0x5a, KEY_LEFT },
	{ 0x1a, KEY_ENTER },	// KEY_OK ?
	{ 0x1b, KEY_RIGHT },
	{ 0x16, KEY_DOWN },
	{ 0x45, KEY_MENU },
	{ 0x05, KEY_ESC },
	{ 0x13, KEY_VOLUMEUP },
	{ 0x17, KEY_VOLUMEDOWN },
	{ 0x58, KEY_APPSELECT },
	{ 0x12, KEY_VENDOR },	// mouse
	{ 0x55, KEY_PAGEUP },	// KEY_CHANNELUP ?
	{ 0x15, KEY_PAGEDOWN },	// KEY_CHANNELDOWN ?
	{ 0x52, KEY_1 },
	{ 0x50, KEY_2 },
	{ 0x10, KEY_3 },
	{ 0x56, KEY_4 },
	{ 0x54, KEY_5 },
	{ 0x14, KEY_6 },
	{ 0x4e, KEY_7 },
	{ 0x4c, KEY_8 },
	{ 0x0c, KEY_9 },
	{ 0x18, KEY_WWW },	// KEY_F7
	{ 0x0f, KEY_0 },
	{ 0x51, KEY_BACKSPACE },
};

static struct rc_map_list kii_pro_map = {
	.map = {
		.scan     = kii_pro,
		.size     = ARRAY_SIZE(kii_pro),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_KII_PRO,
	}
};

static int __init init_rc_map_kii_pro(void)
{
	return rc_map_register(&kii_pro_map);
}

static void __exit exit_rc_map_kii_pro(void)
{
	rc_map_unregister(&kii_pro_map);
}

module_init(init_rc_map_kii_pro)
module_exit(exit_rc_map_kii_pro)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mohammad Rasim <mohammad.rasim96@gmail.com>");
