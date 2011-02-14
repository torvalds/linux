/*
 * A-Link DTU(m) remote controller keytable
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

/* A-Link DTU(m) slim remote, 6 rows, 3 columns. */
static struct rc_map_table alink_dtu_m[] = {
	{ 0x0800, KEY_VOLUMEUP },
	{ 0x0801, KEY_1 },
	{ 0x0802, KEY_3 },
	{ 0x0803, KEY_7 },
	{ 0x0804, KEY_9 },
	{ 0x0805, KEY_NEW },             /* symbol: PIP */
	{ 0x0806, KEY_0 },
	{ 0x0807, KEY_CHANNEL },         /* JUMP */
	{ 0x080d, KEY_5 },
	{ 0x080f, KEY_2 },
	{ 0x0812, KEY_POWER2 },
	{ 0x0814, KEY_CHANNELUP },
	{ 0x0816, KEY_VOLUMEDOWN },
	{ 0x0818, KEY_6 },
	{ 0x081a, KEY_MUTE },
	{ 0x081b, KEY_8 },
	{ 0x081c, KEY_4 },
	{ 0x081d, KEY_CHANNELDOWN },
};

static struct rc_map_list alink_dtu_m_map = {
	.map = {
		.scan    = alink_dtu_m,
		.size    = ARRAY_SIZE(alink_dtu_m),
		.rc_type = RC_TYPE_NEC,
		.name    = RC_MAP_ALINK_DTU_M,
	}
};

static int __init init_rc_map_alink_dtu_m(void)
{
	return rc_map_register(&alink_dtu_m_map);
}

static void __exit exit_rc_map_alink_dtu_m(void)
{
	rc_map_unregister(&alink_dtu_m_map);
}

module_init(init_rc_map_alink_dtu_m)
module_exit(exit_rc_map_alink_dtu_m)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
