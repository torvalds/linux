/*
 * MSI DIGIVOX mini II remote controller keytable
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

static struct rc_map_table msi_digivox_ii[] = {
	{ 0x0302, KEY_2 },
	{ 0x0303, KEY_UP },              /* up */
	{ 0x0304, KEY_3 },
	{ 0x0305, KEY_CHANNELDOWN },
	{ 0x0308, KEY_5 },
	{ 0x0309, KEY_0 },
	{ 0x030b, KEY_8 },
	{ 0x030d, KEY_DOWN },            /* down */
	{ 0x0310, KEY_9 },
	{ 0x0311, KEY_7 },
	{ 0x0314, KEY_VOLUMEUP },
	{ 0x0315, KEY_CHANNELUP },
	{ 0x0316, KEY_OK },
	{ 0x0317, KEY_POWER2 },
	{ 0x031a, KEY_1 },
	{ 0x031c, KEY_4 },
	{ 0x031d, KEY_6 },
	{ 0x031f, KEY_VOLUMEDOWN },
};

static struct rc_map_list msi_digivox_ii_map = {
	.map = {
		.scan     = msi_digivox_ii,
		.size     = ARRAY_SIZE(msi_digivox_ii),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_MSI_DIGIVOX_II,
	}
};

static int __init init_rc_map_msi_digivox_ii(void)
{
	return rc_map_register(&msi_digivox_ii_map);
}

static void __exit exit_rc_map_msi_digivox_ii(void)
{
	rc_map_unregister(&msi_digivox_ii_map);
}

module_init(init_rc_map_msi_digivox_ii)
module_exit(exit_rc_map_msi_digivox_ii)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
