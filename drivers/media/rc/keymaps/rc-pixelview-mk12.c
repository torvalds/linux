// SPDX-License-Identifier: GPL-2.0+
// rc-pixelview-mk12.h - Keytable for pixelview Remote Controller
//
// keymap imported from ir-keymaps.c
//
// Copyright (c) 2010 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

/*
 * Keytable for MK-F12 IR remote provided together with Pixelview
 * Ultra Pro Remote Controller. Uses NEC extended format.
 */
static struct rc_map_table pixelview_mk12[] = {
	{ 0x866b03, KEY_TUNER },	/* Timeshift */
	{ 0x866b1e, KEY_POWER2 },	/* power */

	{ 0x866b01, KEY_NUMERIC_1 },
	{ 0x866b0b, KEY_NUMERIC_2 },
	{ 0x866b1b, KEY_NUMERIC_3 },
	{ 0x866b05, KEY_NUMERIC_4 },
	{ 0x866b09, KEY_NUMERIC_5 },
	{ 0x866b15, KEY_NUMERIC_6 },
	{ 0x866b06, KEY_NUMERIC_7 },
	{ 0x866b0a, KEY_NUMERIC_8 },
	{ 0x866b12, KEY_NUMERIC_9 },
	{ 0x866b02, KEY_NUMERIC_0 },

	{ 0x866b13, KEY_AGAIN },	/* loop */
	{ 0x866b10, KEY_DIGITS },	/* +100 */

	{ 0x866b00, KEY_VIDEO },		/* source */
	{ 0x866b18, KEY_MUTE },		/* mute */
	{ 0x866b19, KEY_CAMERA },	/* snapshot */
	{ 0x866b1a, KEY_SEARCH },	/* scan */

	{ 0x866b16, KEY_CHANNELUP },	/* chn + */
	{ 0x866b14, KEY_CHANNELDOWN },	/* chn - */
	{ 0x866b1f, KEY_VOLUMEUP },	/* vol + */
	{ 0x866b17, KEY_VOLUMEDOWN },	/* vol - */
	{ 0x866b1c, KEY_ZOOM },		/* zoom */

	{ 0x866b04, KEY_REWIND },
	{ 0x866b0e, KEY_RECORD },
	{ 0x866b0c, KEY_FORWARD },

	{ 0x866b1d, KEY_STOP },
	{ 0x866b08, KEY_PLAY },
	{ 0x866b0f, KEY_PAUSE },

	{ 0x866b0d, KEY_TV },
	{ 0x866b07, KEY_RADIO },	/* FM */
};

static struct rc_map_list pixelview_map = {
	.map = {
		.scan     = pixelview_mk12,
		.size     = ARRAY_SIZE(pixelview_mk12),
		.rc_proto = RC_PROTO_NECX,
		.name     = RC_MAP_PIXELVIEW_MK12,
	}
};

static int __init init_rc_map_pixelview(void)
{
	return rc_map_register(&pixelview_map);
}

static void __exit exit_rc_map_pixelview(void)
{
	rc_map_unregister(&pixelview_map);
}

module_init(init_rc_map_pixelview)
module_exit(exit_rc_map_pixelview)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_DESCRIPTION("MK-F12 IR remote controller keytable");
