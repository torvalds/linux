// SPDX-License-Identifier: GPL-2.0+
// pctv-sedna.h - Keytable for pctv_sedna Remote Controller
//
// keymap imported from ir-keymaps.c
//
// Copyright (c) 2010 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

/* Mapping for the 28 key remote control as seen at
   http://www.sednacomputer.com/photo/cardbus-tv.jpg
   Pavel Mihaylov <bin@bash.info>
   Also for the remote bundled with Kozumi KTV-01C card */

static struct rc_map_table pctv_sedna[] = {
	{ 0x00, KEY_NUMERIC_0 },
	{ 0x01, KEY_NUMERIC_1 },
	{ 0x02, KEY_NUMERIC_2 },
	{ 0x03, KEY_NUMERIC_3 },
	{ 0x04, KEY_NUMERIC_4 },
	{ 0x05, KEY_NUMERIC_5 },
	{ 0x06, KEY_NUMERIC_6 },
	{ 0x07, KEY_NUMERIC_7 },
	{ 0x08, KEY_NUMERIC_8 },
	{ 0x09, KEY_NUMERIC_9 },

	{ 0x0a, KEY_AGAIN },	/* Recall */
	{ 0x0b, KEY_CHANNELUP },
	{ 0x0c, KEY_VOLUMEUP },
	{ 0x0d, KEY_MODE },	/* Stereo */
	{ 0x0e, KEY_STOP },
	{ 0x0f, KEY_PREVIOUSSONG },
	{ 0x10, KEY_ZOOM },
	{ 0x11, KEY_VIDEO },	/* Source */
	{ 0x12, KEY_POWER },
	{ 0x13, KEY_MUTE },
	{ 0x15, KEY_CHANNELDOWN },
	{ 0x18, KEY_VOLUMEDOWN },
	{ 0x19, KEY_CAMERA },	/* Snapshot */
	{ 0x1a, KEY_NEXTSONG },
	{ 0x1b, KEY_TIME },	/* Time Shift */
	{ 0x1c, KEY_RADIO },	/* FM Radio */
	{ 0x1d, KEY_RECORD },
	{ 0x1e, KEY_PAUSE },
	/* additional codes for Kozumi's remote */
	{ 0x14, KEY_INFO },	/* OSD */
	{ 0x16, KEY_OK },	/* OK */
	{ 0x17, KEY_DIGITS },	/* Plus */
	{ 0x1f, KEY_PLAY },	/* Play */
};

static struct rc_map_list pctv_sedna_map = {
	.map = {
		.scan     = pctv_sedna,
		.size     = ARRAY_SIZE(pctv_sedna),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_PCTV_SEDNA,
	}
};

static int __init init_rc_map_pctv_sedna(void)
{
	return rc_map_register(&pctv_sedna_map);
}

static void __exit exit_rc_map_pctv_sedna(void)
{
	rc_map_unregister(&pctv_sedna_map);
}

module_init(init_rc_map_pctv_sedna)
module_exit(exit_rc_map_pctv_sedna)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_DESCRIPTION("pctv-sedna remote controller keytable");
