/*
 * Medion X10 RF remote keytable (Digitainer variant)
 *
 * Copyright (C) 2012 Anssi Hannula <anssi.hannula@iki.fi>
 *
 * This keymap is for a variant that has a distinctive scrollwheel instead of
 * up/down buttons (tested with P/N 40009936 / 20018268), reportedly
 * originally shipped with Medion Digitainer but now sold separately simply as
 * an "X10" remote.
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

static struct rc_map_table medion_x10_digitainer[] = {
	{ 0x02, KEY_POWER },

	{ 0x2c, KEY_TV },
	{ 0x2d, KEY_VIDEO },
	{ 0x04, KEY_DVD },    /* CD/DVD */
	{ 0x16, KEY_TEXT },   /* "teletext" icon, i.e. a screen with lines */
	{ 0x06, KEY_AUDIO },
	{ 0x2e, KEY_RADIO },
	{ 0x31, KEY_EPG },    /* a screen with an open book */
	{ 0x05, KEY_IMAGES }, /* Photo */
	{ 0x2f, KEY_INFO },

	{ 0x78, KEY_UP },     /* scrollwheel up 1 notch */
	/* 0x79..0x7f: 2-8 notches, driver repeats 0x78 entry */

	{ 0x70, KEY_DOWN },   /* scrollwheel down 1 notch */
	/* 0x71..0x77: 2-8 notches, driver repeats 0x70 entry */

	{ 0x19, KEY_MENU },
	{ 0x1d, KEY_LEFT },
	{ 0x1e, KEY_OK },     /* scrollwheel press */
	{ 0x1f, KEY_RIGHT },
	{ 0x20, KEY_BACK },

	{ 0x09, KEY_VOLUMEUP },
	{ 0x08, KEY_VOLUMEDOWN },
	{ 0x00, KEY_MUTE },

	{ 0x1b, KEY_SELECT }, /* also has "U" rotated 90 degrees CCW */

	{ 0x0b, KEY_CHANNELUP },
	{ 0x0c, KEY_CHANNELDOWN },
	{ 0x1c, KEY_LAST },

	{ 0x32, KEY_RED },    /* also Audio */
	{ 0x33, KEY_GREEN },  /* also Subtitle */
	{ 0x34, KEY_YELLOW }, /* also Angle */
	{ 0x35, KEY_BLUE },   /* also Title */

	{ 0x28, KEY_STOP },
	{ 0x29, KEY_PAUSE },
	{ 0x25, KEY_PLAY },
	{ 0x21, KEY_PREVIOUS },
	{ 0x18, KEY_CAMERA },
	{ 0x23, KEY_NEXT },
	{ 0x24, KEY_REWIND },
	{ 0x27, KEY_RECORD },
	{ 0x26, KEY_FORWARD },

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

	/* these do not actually exist on this remote, but these scancodes
	 * exist on all other Medion X10 remotes and adding them here allows
	 * such remotes to be adequately usable with this keymap in case
	 * this keymap is wrongly used with them (which is quite possible as
	 * there are lots of different Medion X10 remotes): */
	{ 0x1a, KEY_UP },
	{ 0x22, KEY_DOWN },
};

static struct rc_map_list medion_x10_digitainer_map = {
	.map = {
		.scan    = medion_x10_digitainer,
		.size    = ARRAY_SIZE(medion_x10_digitainer),
		.rc_type = RC_TYPE_OTHER,
		.name    = RC_MAP_MEDION_X10_DIGITAINER,
	}
};

static int __init init_rc_map_medion_x10_digitainer(void)
{
	return rc_map_register(&medion_x10_digitainer_map);
}

static void __exit exit_rc_map_medion_x10_digitainer(void)
{
	rc_map_unregister(&medion_x10_digitainer_map);
}

module_init(init_rc_map_medion_x10_digitainer)
module_exit(exit_rc_map_medion_x10_digitainer)

MODULE_DESCRIPTION("Medion X10 RF remote keytable (Digitainer variant)");
MODULE_AUTHOR("Anssi Hannula <anssi.hannula@iki.fi>");
MODULE_LICENSE("GPL");
