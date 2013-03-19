/*
 * Total Media In Hand_02 remote controller keytable for Mygica X8507
 *
 * Copyright (C) 2012 Alfredo J. Delaiti <alfredodelaiti@netscape.net>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License along
 *    with this program; if not, write to the Free Software Foundation, Inc.,
 *    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <media/rc-map.h>
#include <linux/module.h>


static struct rc_map_table total_media_in_hand_02[] = {
	{ 0x0000, KEY_0 },
	{ 0x0001, KEY_1 },
	{ 0x0002, KEY_2 },
	{ 0x0003, KEY_3 },
	{ 0x0004, KEY_4 },
	{ 0x0005, KEY_5 },
	{ 0x0006, KEY_6 },
	{ 0x0007, KEY_7 },
	{ 0x0008, KEY_8 },
	{ 0x0009, KEY_9 },
	{ 0x000a, KEY_MUTE },
	{ 0x000b, KEY_STOP },                   /* Stop */
	{ 0x000c, KEY_POWER2 },                 /* Turn on/off application */
	{ 0x000d, KEY_OK },                     /* OK */
	{ 0x000e, KEY_CAMERA },                 /* Snapshot */
	{ 0x000f, KEY_ZOOM },                   /* Full Screen/Restore */
	{ 0x0010, KEY_RIGHT },                  /* Right arrow */
	{ 0x0011, KEY_LEFT },                   /* Left arrow */
	{ 0x0012, KEY_CHANNELUP },
	{ 0x0013, KEY_CHANNELDOWN },
	{ 0x0014, KEY_SHUFFLE },
	{ 0x0016, KEY_PAUSE },
	{ 0x0017, KEY_PLAY },                   /* Play */
	{ 0x001e, KEY_TIME },                   /* Time Shift */
	{ 0x001f, KEY_RECORD },
	{ 0x0020, KEY_UP },
	{ 0x0021, KEY_DOWN },
	{ 0x0025, KEY_POWER },                  /* Turn off computer */
	{ 0x0026, KEY_REWIND },                 /* FR << */
	{ 0x0027, KEY_FASTFORWARD },            /* FF >> */
	{ 0x0029, KEY_ESC },
	{ 0x002b, KEY_VOLUMEUP },
	{ 0x002c, KEY_VOLUMEDOWN },
	{ 0x002d, KEY_CHANNEL },                /* CH Surfing */
	{ 0x0038, KEY_VIDEO },                  /* TV/AV/S-Video/YPbPr */
};

static struct rc_map_list total_media_in_hand_02_map = {
	.map = {
		.scan    = total_media_in_hand_02,
		.size    = ARRAY_SIZE(total_media_in_hand_02),
		.rc_type = RC_TYPE_RC5,
		.name    = RC_MAP_TOTAL_MEDIA_IN_HAND_02,
	}
};

static int __init init_rc_map_total_media_in_hand_02(void)
{
	return rc_map_register(&total_media_in_hand_02_map);
}

static void __exit exit_rc_map_total_media_in_hand_02(void)
{
	rc_map_unregister(&total_media_in_hand_02_map);
}

module_init(init_rc_map_total_media_in_hand_02)
module_exit(exit_rc_map_total_media_in_hand_02)

MODULE_LICENSE("GPL");
MODULE_AUTHOR(" Alfredo J. Delaiti <alfredodelaiti@netscape.net>");
