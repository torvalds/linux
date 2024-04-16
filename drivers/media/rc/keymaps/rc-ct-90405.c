// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Toshiba CT-90405 remote controller keytable
 *
 * Copyright (C) 2021 Alexander Voronov <avv.0@ya.ru>
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table ct_90405[] = {
	{ 0x4014, KEY_SWITCHVIDEOMODE },
	{ 0x4012, KEY_POWER },
	{ 0x4044, KEY_TV },
	{ 0x40be43, KEY_3D_MODE },
	{ 0x400c, KEY_SUBTITLE },
	{ 0x4001, KEY_NUMERIC_1 },
	{ 0x4002, KEY_NUMERIC_2 },
	{ 0x4003, KEY_NUMERIC_3 },
	{ 0x4004, KEY_NUMERIC_4 },
	{ 0x4005, KEY_NUMERIC_5 },
	{ 0x4006, KEY_NUMERIC_6 },
	{ 0x4007, KEY_NUMERIC_7 },
	{ 0x4008, KEY_NUMERIC_8 },
	{ 0x4009, KEY_NUMERIC_9 },
	{ 0x4062, KEY_AUDIO_DESC },
	{ 0x4000, KEY_NUMERIC_0 },
	{ 0x401a, KEY_VOLUMEUP },
	{ 0x401e, KEY_VOLUMEDOWN },
	{ 0x4016, KEY_INFO },
	{ 0x4010, KEY_MUTE },
	{ 0x401b, KEY_CHANNELUP },
	{ 0x401f, KEY_CHANNELDOWN },
	{ 0x40da, KEY_VENDOR },
	{ 0x4066, KEY_PLAYER },
	{ 0x4017, KEY_TEXT },
	{ 0x4047, KEY_LIST },
	{ 0x4073, KEY_PAGEUP },
	{ 0x4045, KEY_PROGRAM },
	{ 0x4043, KEY_EXIT },
	{ 0x4074, KEY_PAGEDOWN },
	{ 0x4064, KEY_BACK },
	{ 0x405b, KEY_MENU },
	{ 0x4019, KEY_UP },
	{ 0x4040, KEY_RIGHT },
	{ 0x401d, KEY_DOWN },
	{ 0x4042, KEY_LEFT },
	{ 0x4021, KEY_OK },
	{ 0x4053, KEY_REWIND },
	{ 0x4067, KEY_PLAY },
	{ 0x400d, KEY_FASTFORWARD },
	{ 0x4054, KEY_PREVIOUS },
	{ 0x4068, KEY_STOP },
	{ 0x406a, KEY_PAUSE },
	{ 0x4015, KEY_NEXT },
	{ 0x4048, KEY_RED },
	{ 0x4049, KEY_GREEN },
	{ 0x404a, KEY_YELLOW },
	{ 0x404b, KEY_BLUE },
	{ 0x406f, KEY_RECORD }
};

static struct rc_map_list ct_90405_map = {
	.map = {
		.scan     = ct_90405,
		.size     = ARRAY_SIZE(ct_90405),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_CT_90405,
	}
};

static int __init init_rc_map_ct_90405(void)
{
	return rc_map_register(&ct_90405_map);
}

static void __exit exit_rc_map_ct_90405(void)
{
	rc_map_unregister(&ct_90405_map);
}

module_init(init_rc_map_ct_90405)
module_exit(exit_rc_map_ct_90405)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Voronov <avv.0@ya.ru>");
