/*
 * SnapStream Firefly X10 RF remote keytable
 *
 * Copyright (C) 2011 Anssi Hannula <anssi.hannula@?ki.fi>
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

static struct rc_map_table snapstream_firefly[] = {
	{ 0xf12c, KEY_ZOOM },       /* Maximize */
	{ 0xc702, KEY_CLOSE },

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
	{ 0xdb16, KEY_BACK },
	{ 0xdd18, KEY_KPENTER },    /* ent */

	{ 0xce09, KEY_VOLUMEUP },
	{ 0xcd08, KEY_VOLUMEDOWN },
	{ 0xcf0a, KEY_MUTE },
	{ 0xd00b, KEY_CHANNELUP },
	{ 0xd10c, KEY_CHANNELDOWN },
	{ 0xc500, KEY_VENDOR },     /* firefly */

	{ 0xf32e, KEY_INFO },
	{ 0xf42f, KEY_OPTION },

	{ 0xe21d, KEY_LEFT },
	{ 0xe41f, KEY_RIGHT },
	{ 0xe722, KEY_DOWN },
	{ 0xdf1a, KEY_UP },
	{ 0xe31e, KEY_OK },

	{ 0xe11c, KEY_MENU },
	{ 0xe520, KEY_EXIT },

	{ 0xec27, KEY_RECORD },
	{ 0xea25, KEY_PLAY },
	{ 0xed28, KEY_STOP },
	{ 0xe924, KEY_REWIND },
	{ 0xeb26, KEY_FORWARD },
	{ 0xee29, KEY_PAUSE },
	{ 0xf02b, KEY_PREVIOUS },
	{ 0xef2a, KEY_NEXT },

	{ 0xcb06, KEY_AUDIO },      /* Music */
	{ 0xca05, KEY_IMAGES },     /* Photos */
	{ 0xc904, KEY_DVD },
	{ 0xc803, KEY_TV },
	{ 0xcc07, KEY_VIDEO },

	{ 0xc601, KEY_HELP },
	{ 0xf22d, KEY_MODE },       /* Mouse */

	{ 0xde19, KEY_A },
	{ 0xe01b, KEY_B },
	{ 0xe621, KEY_C },
	{ 0xe823, KEY_D },
};

static struct rc_map_list snapstream_firefly_map = {
	.map = {
		.scan    = snapstream_firefly,
		.size    = ARRAY_SIZE(snapstream_firefly),
		.rc_type = RC_TYPE_OTHER,
		.name    = RC_MAP_SNAPSTREAM_FIREFLY,
	}
};

static int __init init_rc_map_snapstream_firefly(void)
{
	return rc_map_register(&snapstream_firefly_map);
}

static void __exit exit_rc_map_snapstream_firefly(void)
{
	rc_map_unregister(&snapstream_firefly_map);
}

module_init(init_rc_map_snapstream_firefly)
module_exit(exit_rc_map_snapstream_firefly)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anssi Hannula <anssi.hannula@iki.fi>");
