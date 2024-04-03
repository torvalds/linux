// SPDX-License-Identifier: GPL-2.0+
// fusionhdtv-mce.h - Keytable for fusionhdtv_mce Remote Controller
//
// keymap imported from ir-keymaps.c
//
// Copyright (c) 2010 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

/* DViCO FUSION HDTV MCE remote */

static struct rc_map_table fusionhdtv_mce[] = {

	{ 0x0b, KEY_NUMERIC_1 },
	{ 0x17, KEY_NUMERIC_2 },
	{ 0x1b, KEY_NUMERIC_3 },
	{ 0x07, KEY_NUMERIC_4 },
	{ 0x50, KEY_NUMERIC_5 },
	{ 0x54, KEY_NUMERIC_6 },
	{ 0x48, KEY_NUMERIC_7 },
	{ 0x4c, KEY_NUMERIC_8 },
	{ 0x58, KEY_NUMERIC_9 },
	{ 0x03, KEY_NUMERIC_0 },

	{ 0x5e, KEY_OK },
	{ 0x51, KEY_UP },
	{ 0x53, KEY_DOWN },
	{ 0x5b, KEY_LEFT },
	{ 0x5f, KEY_RIGHT },

	{ 0x02, KEY_TV },		/* Labeled DTV on remote */
	{ 0x0e, KEY_MP3 },
	{ 0x1a, KEY_DVD },
	{ 0x1e, KEY_FAVORITES },	/* Labeled CPF on remote */
	{ 0x16, KEY_SETUP },
	{ 0x46, KEY_POWER2 },		/* TV On/Off button on remote */
	{ 0x0a, KEY_EPG },		/* Labeled Guide on remote */

	{ 0x49, KEY_BACK },
	{ 0x59, KEY_INFO },		/* Labeled MORE on remote */
	{ 0x4d, KEY_MENU },		/* Labeled DVDMENU on remote */
	{ 0x55, KEY_CYCLEWINDOWS },	/* Labeled ALT-TAB on remote */

	{ 0x0f, KEY_PREVIOUSSONG },	/* Labeled |<< REPLAY on remote */
	{ 0x12, KEY_NEXTSONG },		/* Labeled >>| SKIP on remote */
	{ 0x42, KEY_ENTER },		/* Labeled START with a green
					   MS windows logo on remote */

	{ 0x15, KEY_VOLUMEUP },
	{ 0x05, KEY_VOLUMEDOWN },
	{ 0x11, KEY_CHANNELUP },
	{ 0x09, KEY_CHANNELDOWN },

	{ 0x52, KEY_CAMERA },
	{ 0x5a, KEY_TUNER },
	{ 0x19, KEY_OPEN },

	{ 0x13, KEY_MODE },		/* 4:3 16:9 select */
	{ 0x1f, KEY_ZOOM },

	{ 0x43, KEY_REWIND },
	{ 0x47, KEY_PLAYPAUSE },
	{ 0x4f, KEY_FASTFORWARD },
	{ 0x57, KEY_MUTE },
	{ 0x0d, KEY_STOP },
	{ 0x01, KEY_RECORD },
	{ 0x4e, KEY_POWER },
};

static struct rc_map_list fusionhdtv_mce_map = {
	.map = {
		.scan     = fusionhdtv_mce,
		.size     = ARRAY_SIZE(fusionhdtv_mce),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_FUSIONHDTV_MCE,
	}
};

static int __init init_rc_map_fusionhdtv_mce(void)
{
	return rc_map_register(&fusionhdtv_mce_map);
}

static void __exit exit_rc_map_fusionhdtv_mce(void)
{
	rc_map_unregister(&fusionhdtv_mce_map);
}

module_init(init_rc_map_fusionhdtv_mce)
module_exit(exit_rc_map_fusionhdtv_mce)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_DESCRIPTION("DViCO FUSION HDTV MCE remote controller keytable");
