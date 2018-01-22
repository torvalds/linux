/*
 * Medion X10 OR22/OR24 RF remote keytable
 *
 * Copyright (C) 2012 Anssi Hannula <anssi.hannula@iki.fi>
 *
 * This keymap is for several Medion X10 remotes that have the Windows MCE
 * button. This has been tested with a "RF VISTA Remote Control", OR24V,
 * P/N 20035335, but should work with other variants that have the same
 * buttons, such as OR22V and OR24E.
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

static struct rc_map_table medion_x10_or2x[] = {
	{ 0x02, KEY_POWER },
	{ 0x16, KEY_TEXT },   /* "T" in a box, for teletext */

	{ 0x09, KEY_VOLUMEUP },
	{ 0x08, KEY_VOLUMEDOWN },
	{ 0x00, KEY_MUTE },
	{ 0x0b, KEY_CHANNELUP },
	{ 0x0c, KEY_CHANNELDOWN },

	{ 0x32, KEY_RED },
	{ 0x33, KEY_GREEN },
	{ 0x34, KEY_YELLOW },
	{ 0x35, KEY_BLUE },

	{ 0x18, KEY_PVR },    /* record symbol inside a tv symbol */
	{ 0x04, KEY_DVD },    /* disc symbol */
	{ 0x31, KEY_EPG },    /* a tv schedule symbol */
	{ 0x1c, KEY_TV },     /* play symbol inside a tv symbol */
	{ 0x20, KEY_BACK },
	{ 0x2f, KEY_INFO },

	{ 0x1a, KEY_UP },
	{ 0x22, KEY_DOWN },
	{ 0x1d, KEY_LEFT },
	{ 0x1f, KEY_RIGHT },
	{ 0x1e, KEY_OK },

	{ 0x1b, KEY_MEDIA },  /* Windows MCE button */

	{ 0x21, KEY_PREVIOUS },
	{ 0x23, KEY_NEXT },
	{ 0x24, KEY_REWIND },
	{ 0x26, KEY_FORWARD },
	{ 0x25, KEY_PLAY },
	{ 0x28, KEY_STOP },
	{ 0x29, KEY_PAUSE },
	{ 0x27, KEY_RECORD },

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
	{ 0x30, KEY_CLEAR },
	{ 0x36, KEY_ENTER },
	{ 0x37, KEY_NUMERIC_STAR },
	{ 0x38, KEY_NUMERIC_POUND },
};

static struct rc_map_list medion_x10_or2x_map = {
	.map = {
		.scan     = medion_x10_or2x,
		.size     = ARRAY_SIZE(medion_x10_or2x),
		.rc_proto = RC_PROTO_OTHER,
		.name     = RC_MAP_MEDION_X10_OR2X,
	}
};

static int __init init_rc_map_medion_x10_or2x(void)
{
	return rc_map_register(&medion_x10_or2x_map);
}

static void __exit exit_rc_map_medion_x10_or2x(void)
{
	rc_map_unregister(&medion_x10_or2x_map);
}

module_init(init_rc_map_medion_x10_or2x)
module_exit(exit_rc_map_medion_x10_or2x)

MODULE_DESCRIPTION("Medion X10 OR22/OR24 RF remote keytable");
MODULE_AUTHOR("Anssi Hannula <anssi.hannula@iki.fi>");
MODULE_LICENSE("GPL");
