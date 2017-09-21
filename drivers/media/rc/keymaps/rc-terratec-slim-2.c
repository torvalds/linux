/*
 * TerraTec remote controller keytable
 *
 * Copyright (C) 2011 Martin Groszhauser <mgroszhauser@gmail.com>
 * Copyright (C) 2011 Antti Palosaari <crope@iki.fi>
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

/*
 * TerraTec slim remote, 6 rows, 3 columns.
 * Keytable from Martin Groszhauser <mgroszhauser@gmail.com>
 */
static struct rc_map_table terratec_slim_2[] = {
	{ 0x8001, KEY_MUTE },            /* MUTE */
	{ 0x8002, KEY_VOLUMEDOWN },
	{ 0x8003, KEY_CHANNELDOWN },
	{ 0x8004, KEY_1 },
	{ 0x8005, KEY_2 },
	{ 0x8006, KEY_3 },
	{ 0x8007, KEY_4 },
	{ 0x8008, KEY_5 },
	{ 0x8009, KEY_6 },
	{ 0x800a, KEY_7 },
	{ 0x800c, KEY_ZOOM },            /* [fullscreen] */
	{ 0x800d, KEY_0 },
	{ 0x800e, KEY_AGAIN },           /* [two arrows forming a circle] */
	{ 0x8012, KEY_POWER2 },          /* [red power button] */
	{ 0x801a, KEY_VOLUMEUP },
	{ 0x801b, KEY_8 },
	{ 0x801e, KEY_CHANNELUP },
	{ 0x801f, KEY_9 },
};

static struct rc_map_list terratec_slim_2_map = {
	.map = {
		.scan     = terratec_slim_2,
		.size     = ARRAY_SIZE(terratec_slim_2),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_TERRATEC_SLIM_2,
	}
};

static int __init init_rc_map_terratec_slim_2(void)
{
	return rc_map_register(&terratec_slim_2_map);
}

static void __exit exit_rc_map_terratec_slim_2(void)
{
	rc_map_unregister(&terratec_slim_2_map);
}

module_init(init_rc_map_terratec_slim_2)
module_exit(exit_rc_map_terratec_slim_2)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
