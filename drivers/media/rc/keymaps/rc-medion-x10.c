/*
 * Medion X10 RF remote keytable
 *
 * Copyright (C) 2011 Anssi Hannula <anssi.hannula@?ki.fi>
 *
 * This file is based on a keytable provided by
 * Jan Losinski <losinski@wh2.tu-dresden.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/module.h>
#include <media/rc-map.h>

static struct rc_map_table medion_x10[] = {
	{ 0xf12c, KEY_TV },    /* TV */
	{ 0xf22d, KEY_VCR },   /* VCR */
	{ 0xc904, KEY_DVD },   /* DVD */
	{ 0xcb06, KEY_AUDIO }, /* MUSIC */

	{ 0xf32e, KEY_RADIO },     /* RADIO */
	{ 0xca05, KEY_DIRECTORY }, /* PHOTO */
	{ 0xf42f, KEY_INFO },      /* TV-PREVIEW */
	{ 0xf530, KEY_LIST },      /* CHANNEL-LST */

	{ 0xe01b, KEY_SETUP }, /* SETUP */
	{ 0xf631, KEY_VIDEO }, /* VIDEO DESKTOP */

	{ 0xcd08, KEY_VOLUMEDOWN },  /* VOL - */
	{ 0xce09, KEY_VOLUMEUP },    /* VOL + */
	{ 0xd00b, KEY_CHANNELUP },   /* CHAN + */
	{ 0xd10c, KEY_CHANNELDOWN }, /* CHAN - */
	{ 0xc500, KEY_MUTE },        /* MUTE */

	{ 0xf732, KEY_RED }, /* red */
	{ 0xf833, KEY_GREEN }, /* green */
	{ 0xf934, KEY_YELLOW }, /* yellow */
	{ 0xfa35, KEY_BLUE }, /* blue */
	{ 0xdb16, KEY_TEXT }, /* TXT */

	{ 0xd20d, KEY_1 },
	{ 0xd30e, KEY_2 },
	{ 0xd40f, KEY_3 },
	{ 0xd510, KEY_4 },
	{ 0xd611, KEY_5 },
	{ 0xd712, KEY_6 },
	{ 0xd813, KEY_7 },
	{ 0xd914, KEY_8 },
	{ 0xda15, KEY_9 },
	{ 0xdc17, KEY_0 },
	{ 0xe11c, KEY_SEARCH }, /* TV/RAD, CH SRC */
	{ 0xe520, KEY_DELETE }, /* DELETE */

	{ 0xfb36, KEY_KEYBOARD }, /* RENAME */
	{ 0xdd18, KEY_SCREEN },   /* SNAPSHOT */

	{ 0xdf1a, KEY_UP },    /* up */
	{ 0xe722, KEY_DOWN },  /* down */
	{ 0xe21d, KEY_LEFT },  /* left */
	{ 0xe41f, KEY_RIGHT }, /* right */
	{ 0xe31e, KEY_OK },    /* OK */

	{ 0xfc37, KEY_SELECT }, /* ACQUIRE IMAGE */
	{ 0xfd38, KEY_EDIT },   /* EDIT IMAGE */

	{ 0xe924, KEY_REWIND },   /* rewind  (<<) */
	{ 0xea25, KEY_PLAY },     /* play    ( >) */
	{ 0xeb26, KEY_FORWARD },  /* forward (>>) */
	{ 0xec27, KEY_RECORD },   /* record  ( o) */
	{ 0xed28, KEY_STOP },     /* stop    ([]) */
	{ 0xee29, KEY_PAUSE },    /* pause   ('') */

	{ 0xe621, KEY_PREVIOUS },        /* prev */
	{ 0xfe39, KEY_SWITCHVIDEOMODE }, /* F SCR */
	{ 0xe823, KEY_NEXT },            /* next */
	{ 0xde19, KEY_MENU },            /* MENU */
	{ 0xff3a, KEY_LANGUAGE },        /* AUDIO */

	{ 0xc702, KEY_POWER }, /* POWER */
};

static struct rc_map_list medion_x10_map = {
	.map = {
		.scan    = medion_x10,
		.size    = ARRAY_SIZE(medion_x10),
		.rc_type = RC_TYPE_OTHER,
		.name    = RC_MAP_MEDION_X10,
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
