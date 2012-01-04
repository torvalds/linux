/*
 * ATI X10 RF remote keytable
 *
 * Copyright (C) 2011 Anssi Hannula <anssi.hannula@?ki.fi>
 *
 * This file is based on the static generic keytable previously found in
 * ati_remote.c, which is
 * Copyright (c) 2004 Torrey Hoffman <thoffman@arnor.net>
 * Copyright (c) 2002 Vladimir Dergachev
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

static struct rc_map_table ati_x10[] = {
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
	{ 0xc500, KEY_A },
	{ 0xc601, KEY_B },
	{ 0xde19, KEY_C },
	{ 0xe01b, KEY_D },
	{ 0xe621, KEY_E },
	{ 0xe823, KEY_F },

	{ 0xdd18, KEY_KPENTER },    /* "check" */
	{ 0xdb16, KEY_MENU },       /* "menu" */
	{ 0xc702, KEY_POWER },      /* Power */
	{ 0xc803, KEY_TV },         /* TV */
	{ 0xc904, KEY_DVD },        /* DVD */
	{ 0xca05, KEY_WWW },        /* WEB */
	{ 0xcb06, KEY_BOOKMARKS },  /* "book" */
	{ 0xcc07, KEY_EDIT },       /* "hand" */
	{ 0xe11c, KEY_COFFEE },     /* "timer" */
	{ 0xe520, KEY_FRONT },      /* "max" */
	{ 0xe21d, KEY_LEFT },       /* left */
	{ 0xe41f, KEY_RIGHT },      /* right */
	{ 0xe722, KEY_DOWN },       /* down */
	{ 0xdf1a, KEY_UP },         /* up */
	{ 0xe31e, KEY_OK },         /* "OK" */
	{ 0xce09, KEY_VOLUMEDOWN }, /* VOL + */
	{ 0xcd08, KEY_VOLUMEUP },   /* VOL - */
	{ 0xcf0a, KEY_MUTE },       /* MUTE  */
	{ 0xd00b, KEY_CHANNELUP },  /* CH + */
	{ 0xd10c, KEY_CHANNELDOWN },/* CH - */
	{ 0xec27, KEY_RECORD },     /* ( o) red */
	{ 0xea25, KEY_PLAY },       /* ( >) */
	{ 0xe924, KEY_REWIND },     /* (<<) */
	{ 0xeb26, KEY_FORWARD },    /* (>>) */
	{ 0xed28, KEY_STOP },       /* ([]) */
	{ 0xee29, KEY_PAUSE },      /* ('') */
	{ 0xf02b, KEY_PREVIOUS },   /* (<-) */
	{ 0xef2a, KEY_NEXT },       /* (>+) */
	{ 0xf22d, KEY_INFO },       /* PLAYING */
	{ 0xf32e, KEY_HOME },       /* TOP */
	{ 0xf42f, KEY_END },        /* END */
	{ 0xf530, KEY_SELECT },     /* SELECT */
};

static struct rc_map_list ati_x10_map = {
	.map = {
		.scan    = ati_x10,
		.size    = ARRAY_SIZE(ati_x10),
		.rc_type = RC_TYPE_OTHER,
		.name    = RC_MAP_ATI_X10,
	}
};

static int __init init_rc_map_ati_x10(void)
{
	return rc_map_register(&ati_x10_map);
}

static void __exit exit_rc_map_ati_x10(void)
{
	rc_map_unregister(&ati_x10_map);
}

module_init(init_rc_map_ati_x10)
module_exit(exit_rc_map_ati_x10)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anssi Hannula <anssi.hannula@iki.fi>");
