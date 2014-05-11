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

/*
 * Intended usage comments below are from vendor-supplied
 * Source: ATI REMOTE WONDER™ Installation Guide
 * http://www2.ati.com/manuals/remctrl.pdf
 *
 * Scancodes were in strict left-right, top-bottom order on the
 * original ATI Remote Wonder, but were moved on later models.
 *
 * Keys A-F are intended to be user-programmable.
 */

static struct rc_map_table ati_x10[] = {
	/* keyboard - Above the cursor pad */
	{ 0x00, KEY_A },
	{ 0x01, KEY_B },
	{ 0x02, KEY_POWER },      /* Power */

	{ 0x03, KEY_TV },         /* TV */
	{ 0x04, KEY_DVD },        /* DVD */
	{ 0x05, KEY_WWW },        /* WEB */
	{ 0x06, KEY_BOOKMARKS },  /* "book": Open Media Library */
	{ 0x07, KEY_EDIT },       /* "hand": Toggle left mouse button (grab) */

	/* Mouse emulation pad goes here, handled by driver separately */

	{ 0x09, KEY_VOLUMEDOWN }, /* VOL + */
	{ 0x08, KEY_VOLUMEUP },   /* VOL - */
	{ 0x0a, KEY_MUTE },       /* MUTE  */
	{ 0x0b, KEY_CHANNELUP },  /* CH + */
	{ 0x0c, KEY_CHANNELDOWN },/* CH - */

	{ 0x0d, KEY_1 },
	{ 0x0e, KEY_2 },
	{ 0x0f, KEY_3 },
	{ 0x10, KEY_4 },
	{ 0x11, KEY_5 },
	{ 0x12, KEY_6 },
	{ 0x13, KEY_7 },
	{ 0x14, KEY_8 },
	{ 0x15, KEY_9 },
	{ 0x16, KEY_MENU },       /* "menu": DVD root menu */
	{ 0x17, KEY_0 },
	{ 0x18, KEY_KPENTER },    /* "check": DVD setup menu */

	/* DVD navigation buttons */
	{ 0x19, KEY_C },
	{ 0x1a, KEY_UP },         /* up */
	{ 0x1b, KEY_D },

	{ 0x1c, KEY_COFFEE },     /* "timer" */
	{ 0x1d, KEY_LEFT },       /* left */
	{ 0x1e, KEY_OK },         /* "OK" */
	{ 0x1f, KEY_RIGHT },      /* right */
	{ 0x20, KEY_FRONT },      /* "max" */

	{ 0x21, KEY_E },
	{ 0x22, KEY_DOWN },       /* down */
	{ 0x23, KEY_F },
	/* Play/stop/pause buttons */
	{ 0x24, KEY_REWIND },     /* (<<) Rewind */
	{ 0x25, KEY_PLAY },       /* ( >) Play */
	{ 0x26, KEY_FORWARD },    /* (>>) Fast forward */

	{ 0x27, KEY_RECORD },     /* ( o) red */
	{ 0x28, KEY_STOP },       /* ([]) Stop */
	{ 0x29, KEY_PAUSE },      /* ('') Pause */

	/* Extra keys, not on the original ATI remote */
	{ 0x2a, KEY_NEXT },       /* (>+) */
	{ 0x2b, KEY_PREVIOUS },   /* (<-) */
	{ 0x2d, KEY_INFO },       /* PLAYING */
	{ 0x2e, KEY_HOME },       /* TOP */
	{ 0x2f, KEY_END },        /* END */
	{ 0x30, KEY_SELECT },     /* SELECT */
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
