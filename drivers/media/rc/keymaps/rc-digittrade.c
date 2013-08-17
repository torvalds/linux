/*
 * Digittrade DVB-T USB Stick remote controller keytable
 *
 * Copyright (C) 2010 Antti Palosaari <crope@iki.fi>
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

/* Digittrade DVB-T USB Stick remote controller. */
/* Imported from af9015.h.
   Initial keytable was from Alain Kalker <miki@dds.nl> */

/* Digittrade DVB-T USB Stick */
static struct rc_map_table digittrade[] = {
	{ 0x0000, KEY_9 },
	{ 0x0001, KEY_EPG },             /* EPG */
	{ 0x0002, KEY_VOLUMEDOWN },      /* Vol Dn */
	{ 0x0003, KEY_TEXT },            /* TELETEXT */
	{ 0x0004, KEY_8 },
	{ 0x0005, KEY_MUTE },            /* MUTE */
	{ 0x0006, KEY_POWER2 },          /* POWER */
	{ 0x0009, KEY_ZOOM },            /* FULLSCREEN */
	{ 0x000a, KEY_RECORD },          /* RECORD */
	{ 0x000d, KEY_SUBTITLE },        /* SUBTITLE */
	{ 0x000e, KEY_STOP },            /* STOP */
	{ 0x0010, KEY_OK },              /* RETURN */
	{ 0x0011, KEY_2 },
	{ 0x0012, KEY_4 },
	{ 0x0015, KEY_3 },
	{ 0x0016, KEY_5 },
	{ 0x0017, KEY_CHANNELDOWN },     /* Ch Dn */
	{ 0x0019, KEY_CHANNELUP },       /* CH Up */
	{ 0x001a, KEY_PAUSE },           /* PAUSE */
	{ 0x001b, KEY_1 },
	{ 0x001d, KEY_AUDIO },           /* DUAL SOUND */
	{ 0x001e, KEY_PLAY },            /* PLAY */
	{ 0x001f, KEY_CAMERA },          /* SNAPSHOT */
	{ 0x0040, KEY_VOLUMEUP },        /* Vol Up */
	{ 0x0048, KEY_7 },
	{ 0x004c, KEY_6 },
	{ 0x004d, KEY_PLAYPAUSE },       /* TIMESHIFT */
	{ 0x0054, KEY_0 },
};

static struct rc_map_list digittrade_map = {
	.map = {
		.scan    = digittrade,
		.size    = ARRAY_SIZE(digittrade),
		.rc_type = RC_TYPE_NEC,
		.name    = RC_MAP_DIGITTRADE,
	}
};

static int __init init_rc_map_digittrade(void)
{
	return rc_map_register(&digittrade_map);
}

static void __exit exit_rc_map_digittrade(void)
{
	rc_map_unregister(&digittrade_map);
}

module_init(init_rc_map_digittrade)
module_exit(exit_rc_map_digittrade)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
