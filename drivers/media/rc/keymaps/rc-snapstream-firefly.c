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
	{ 0x2c, KEY_ZOOM },       /* Maximize */
	{ 0x02, KEY_CLOSE },

	{ 0x0d, KEY_1 },
	{ 0x0e, KEY_2 },
	{ 0x0f, KEY_3 },
	{ 0x10, KEY_4 },
	{ 0x11, KEY_5 },
	{ 0x12, KEY_6 },
	{ 0x13, KEY_7 },
	{ 0x14, KEY_8 },
	{ 0x15, KEY_9 },
	{ 0x17, KEY_0 },
	{ 0x16, KEY_BACK },
	{ 0x18, KEY_KPENTER },    /* ent */

	{ 0x09, KEY_VOLUMEUP },
	{ 0x08, KEY_VOLUMEDOWN },
	{ 0x0a, KEY_MUTE },
	{ 0x0b, KEY_CHANNELUP },
	{ 0x0c, KEY_CHANNELDOWN },
	{ 0x00, KEY_VENDOR },     /* firefly */

	{ 0x2e, KEY_INFO },
	{ 0x2f, KEY_OPTION },

	{ 0x1d, KEY_LEFT },
	{ 0x1f, KEY_RIGHT },
	{ 0x22, KEY_DOWN },
	{ 0x1a, KEY_UP },
	{ 0x1e, KEY_OK },

	{ 0x1c, KEY_MENU },
	{ 0x20, KEY_EXIT },

	{ 0x27, KEY_RECORD },
	{ 0x25, KEY_PLAY },
	{ 0x28, KEY_STOP },
	{ 0x24, KEY_REWIND },
	{ 0x26, KEY_FORWARD },
	{ 0x29, KEY_PAUSE },
	{ 0x2b, KEY_PREVIOUS },
	{ 0x2a, KEY_NEXT },

	{ 0x06, KEY_AUDIO },      /* Music */
	{ 0x05, KEY_IMAGES },     /* Photos */
	{ 0x04, KEY_DVD },
	{ 0x03, KEY_TV },
	{ 0x07, KEY_VIDEO },

	{ 0x01, KEY_HELP },
	{ 0x2d, KEY_MODE },       /* Mouse */

	{ 0x19, KEY_A },
	{ 0x1b, KEY_B },
	{ 0x21, KEY_C },
	{ 0x23, KEY_D },
};

static struct rc_map_list snapstream_firefly_map = {
	.map = {
		.scan     = snapstream_firefly,
		.size     = ARRAY_SIZE(snapstream_firefly),
		.rc_proto = RC_PROTO_OTHER,
		.name     = RC_MAP_SNAPSTREAM_FIREFLY,
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
