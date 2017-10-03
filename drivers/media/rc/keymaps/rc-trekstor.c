/*
 * TrekStor remote controller keytable
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

/* TrekStor DVB-T USB Stick remote controller. */
/* Imported from af9015.h.
   Initial keytable was from Marc Schneider <macke@macke.org> */
static struct rc_map_table trekstor[] = {
	{ 0x0084, KEY_0 },
	{ 0x0085, KEY_MUTE },            /* Mute */
	{ 0x0086, KEY_HOMEPAGE },        /* Home */
	{ 0x0087, KEY_UP },              /* Up */
	{ 0x0088, KEY_OK },              /* OK */
	{ 0x0089, KEY_RIGHT },           /* Right */
	{ 0x008a, KEY_FASTFORWARD },     /* Fast forward */
	{ 0x008b, KEY_VOLUMEUP },        /* Volume + */
	{ 0x008c, KEY_DOWN },            /* Down */
	{ 0x008d, KEY_PLAY },            /* Play/Pause */
	{ 0x008e, KEY_STOP },            /* Stop */
	{ 0x008f, KEY_EPG },             /* Info/EPG */
	{ 0x0090, KEY_7 },
	{ 0x0091, KEY_4 },
	{ 0x0092, KEY_1 },
	{ 0x0093, KEY_CHANNELDOWN },     /* Channel - */
	{ 0x0094, KEY_8 },
	{ 0x0095, KEY_5 },
	{ 0x0096, KEY_2 },
	{ 0x0097, KEY_CHANNELUP },       /* Channel + */
	{ 0x0098, KEY_9 },
	{ 0x0099, KEY_6 },
	{ 0x009a, KEY_3 },
	{ 0x009b, KEY_VOLUMEDOWN },      /* Volume - */
	{ 0x009c, KEY_TV },              /* TV */
	{ 0x009d, KEY_RECORD },          /* Record */
	{ 0x009e, KEY_REWIND },          /* Rewind */
	{ 0x009f, KEY_LEFT },            /* Left */
};

static struct rc_map_list trekstor_map = {
	.map = {
		.scan     = trekstor,
		.size     = ARRAY_SIZE(trekstor),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_TREKSTOR,
	}
};

static int __init init_rc_map_trekstor(void)
{
	return rc_map_register(&trekstor_map);
}

static void __exit exit_rc_map_trekstor(void)
{
	rc_map_unregister(&trekstor_map);
}

module_init(init_rc_map_trekstor)
module_exit(exit_rc_map_trekstor)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
