// SPDX-License-Identifier: GPL-2.0+
// videomate-tv-pvr.h - Keytable for videomate_tv_pvr Remote Controller
//
// keymap imported from ir-keymaps.c
//
// Copyright (c) 2010 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table videomate_tv_pvr[] = {
	{ 0x14, KEY_MUTE },
	{ 0x24, KEY_ZOOM },

	{ 0x01, KEY_DVD },
	{ 0x23, KEY_RADIO },
	{ 0x00, KEY_TV },

	{ 0x0a, KEY_REWIND },
	{ 0x08, KEY_PLAYPAUSE },
	{ 0x0f, KEY_FORWARD },

	{ 0x02, KEY_PREVIOUS },
	{ 0x07, KEY_STOP },
	{ 0x06, KEY_NEXT },

	{ 0x0c, KEY_UP },
	{ 0x0e, KEY_DOWN },
	{ 0x0b, KEY_LEFT },
	{ 0x0d, KEY_RIGHT },
	{ 0x11, KEY_OK },

	{ 0x03, KEY_MENU },
	{ 0x09, KEY_SETUP },
	{ 0x05, KEY_VIDEO },
	{ 0x22, KEY_CHANNEL },

	{ 0x12, KEY_VOLUMEUP },
	{ 0x15, KEY_VOLUMEDOWN },
	{ 0x10, KEY_CHANNELUP },
	{ 0x13, KEY_CHANNELDOWN },

	{ 0x04, KEY_RECORD },

	{ 0x16, KEY_NUMERIC_1 },
	{ 0x17, KEY_NUMERIC_2 },
	{ 0x18, KEY_NUMERIC_3 },
	{ 0x19, KEY_NUMERIC_4 },
	{ 0x1a, KEY_NUMERIC_5 },
	{ 0x1b, KEY_NUMERIC_6 },
	{ 0x1c, KEY_NUMERIC_7 },
	{ 0x1d, KEY_NUMERIC_8 },
	{ 0x1e, KEY_NUMERIC_9 },
	{ 0x1f, KEY_NUMERIC_0 },

	{ 0x20, KEY_LANGUAGE },
	{ 0x21, KEY_SLEEP },
};

static struct rc_map_list videomate_tv_pvr_map = {
	.map = {
		.scan     = videomate_tv_pvr,
		.size     = ARRAY_SIZE(videomate_tv_pvr),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_VIDEOMATE_TV_PVR,
	}
};

static int __init init_rc_map_videomate_tv_pvr(void)
{
	return rc_map_register(&videomate_tv_pvr_map);
}

static void __exit exit_rc_map_videomate_tv_pvr(void)
{
	rc_map_unregister(&videomate_tv_pvr_map);
}

module_init(init_rc_map_videomate_tv_pvr)
module_exit(exit_rc_map_videomate_tv_pvr)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_DESCRIPTION("videomate-tv-pvr remote controller keytable");
