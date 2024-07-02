// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Medion X10 RF remote keytable
 *
 * Copyright (C) 2011 Anssi Hannula <anssi.hannula@?ki.fi>
 *
 * This file is based on a keytable provided by
 * Jan Losinski <losinski@wh2.tu-dresden.de>
 */

#include <linux/module.h>
#include <media/rc-map.h>

static struct rc_map_table medion_x10[] = {
	{ 0x2c, KEY_TV },    /* TV */
	{ 0x2d, KEY_VCR },   /* VCR */
	{ 0x04, KEY_DVD },   /* DVD */
	{ 0x06, KEY_AUDIO }, /* MUSIC */

	{ 0x2e, KEY_RADIO },     /* RADIO */
	{ 0x05, KEY_DIRECTORY }, /* PHOTO */
	{ 0x2f, KEY_INFO },      /* TV-PREVIEW */
	{ 0x30, KEY_LIST },      /* CHANNEL-LST */

	{ 0x1b, KEY_SETUP }, /* SETUP */
	{ 0x31, KEY_VIDEO }, /* VIDEO DESKTOP */

	{ 0x08, KEY_VOLUMEDOWN },  /* VOL - */
	{ 0x09, KEY_VOLUMEUP },    /* VOL + */
	{ 0x0b, KEY_CHANNELUP },   /* CHAN + */
	{ 0x0c, KEY_CHANNELDOWN }, /* CHAN - */
	{ 0x00, KEY_MUTE },        /* MUTE */

	{ 0x32, KEY_RED }, /* red */
	{ 0x33, KEY_GREEN }, /* green */
	{ 0x34, KEY_YELLOW }, /* yellow */
	{ 0x35, KEY_BLUE }, /* blue */
	{ 0x16, KEY_TEXT }, /* TXT */

	{ 0x0d, KEY_NUMERIC_1 },
	{ 0x0e, KEY_NUMERIC_2 },
	{ 0x0f, KEY_NUMERIC_3 },
	{ 0x10, KEY_NUMERIC_4 },
	{ 0x11, KEY_NUMERIC_5 },
	{ 0x12, KEY_NUMERIC_6 },
	{ 0x13, KEY_NUMERIC_7 },
	{ 0x14, KEY_NUMERIC_8 },
	{ 0x15, KEY_NUMERIC_9 },
	{ 0x17, KEY_NUMERIC_0 },
	{ 0x1c, KEY_SEARCH }, /* TV/RAD, CH SRC */
	{ 0x20, KEY_DELETE }, /* DELETE */

	{ 0x36, KEY_KEYBOARD }, /* RENAME */
	{ 0x18, KEY_SCREEN },   /* SNAPSHOT */

	{ 0x1a, KEY_UP },    /* up */
	{ 0x22, KEY_DOWN },  /* down */
	{ 0x1d, KEY_LEFT },  /* left */
	{ 0x1f, KEY_RIGHT }, /* right */
	{ 0x1e, KEY_OK },    /* OK */

	{ 0x37, KEY_SELECT }, /* ACQUIRE IMAGE */
	{ 0x38, KEY_EDIT },   /* EDIT IMAGE */

	{ 0x24, KEY_REWIND },   /* rewind  (<<) */
	{ 0x25, KEY_PLAY },     /* play    ( >) */
	{ 0x26, KEY_FORWARD },  /* forward (>>) */
	{ 0x27, KEY_RECORD },   /* record  ( o) */
	{ 0x28, KEY_STOP },     /* stop    ([]) */
	{ 0x29, KEY_PAUSE },    /* pause   ('') */

	{ 0x21, KEY_PREVIOUS },        /* prev */
	{ 0x39, KEY_SWITCHVIDEOMODE }, /* F SCR */
	{ 0x23, KEY_NEXT },            /* next */
	{ 0x19, KEY_MENU },            /* MENU */
	{ 0x3a, KEY_LANGUAGE },        /* AUDIO */

	{ 0x02, KEY_POWER }, /* POWER */
};

static struct rc_map_list medion_x10_map = {
	.map = {
		.scan     = medion_x10,
		.size     = ARRAY_SIZE(medion_x10),
		.rc_proto = RC_PROTO_OTHER,
		.name     = RC_MAP_MEDION_X10,
	}
};

static int __init init_rc_map_medion_x10(void)
{
	return rc_map_register(&medion_x10_map);
}

static void __exit exit_rc_map_medion_x10(void)
{
	rc_map_unregister(&medion_x10_map);
}

module_init(init_rc_map_medion_x10)
module_exit(exit_rc_map_medion_x10)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anssi Hannula <anssi.hannula@iki.fi>");
MODULE_DESCRIPTION("Medion X10 RF remote controller keytable");
