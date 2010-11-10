/*
 * TerraTec remote controller keytable
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

/* TerraTec slim remote, 7 rows, 4 columns. */
/* Uses NEC extended 0x02bd. */
static struct ir_scancode terratec_slim[] = {
	{ 0x02bd00, KEY_1 },
	{ 0x02bd01, KEY_2 },
	{ 0x02bd02, KEY_3 },
	{ 0x02bd03, KEY_4 },
	{ 0x02bd04, KEY_5 },
	{ 0x02bd05, KEY_6 },
	{ 0x02bd06, KEY_7 },
	{ 0x02bd07, KEY_8 },
	{ 0x02bd08, KEY_9 },
	{ 0x02bd09, KEY_0 },
	{ 0x02bd0a, KEY_MUTE },
	{ 0x02bd0b, KEY_NEW },             /* symbol: PIP */
	{ 0x02bd0e, KEY_VOLUMEDOWN },
	{ 0x02bd0f, KEY_PLAYPAUSE },
	{ 0x02bd10, KEY_RIGHT },
	{ 0x02bd11, KEY_LEFT },
	{ 0x02bd12, KEY_UP },
	{ 0x02bd13, KEY_DOWN },
	{ 0x02bd15, KEY_OK },
	{ 0x02bd16, KEY_STOP },
	{ 0x02bd17, KEY_CAMERA },          /* snapshot */
	{ 0x02bd18, KEY_CHANNELUP },
	{ 0x02bd19, KEY_RECORD },
	{ 0x02bd1a, KEY_CHANNELDOWN },
	{ 0x02bd1c, KEY_ESC },
	{ 0x02bd1f, KEY_VOLUMEUP },
	{ 0x02bd44, KEY_EPG },
	{ 0x02bd45, KEY_POWER2 },          /* [red power button] */
};

static struct rc_keymap terratec_slim_map = {
	.map = {
		.scan    = terratec_slim,
		.size    = ARRAY_SIZE(terratec_slim),
		.ir_type = IR_TYPE_NEC,
		.name    = RC_MAP_TERRATEC_SLIM,
	}
};

static int __init init_rc_map_terratec_slim(void)
{
	return ir_register_map(&terratec_slim_map);
}

static void __exit exit_rc_map_terratec_slim(void)
{
	ir_unregister_map(&terratec_slim_map);
}

module_init(init_rc_map_terratec_slim)
module_exit(exit_rc_map_terratec_slim)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
