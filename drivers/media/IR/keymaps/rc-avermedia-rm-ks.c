/*
 * AverMedia RM-KS remote controller keytable
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

/* Initial keytable is from Jose Alberto Reguero <jareguero@telefonica.net>
   and Felipe Morales Moreno <felipe.morales.moreno@gmail.com> */
/* FIXME: mappings are not 100% correct? */
static struct ir_scancode avermedia_rm_ks[] = {
	{ 0x0501, KEY_POWER2 },
	{ 0x0502, KEY_CHANNELUP },
	{ 0x0503, KEY_CHANNELDOWN },
	{ 0x0504, KEY_VOLUMEUP },
	{ 0x0505, KEY_VOLUMEDOWN },
	{ 0x0506, KEY_MUTE },
	{ 0x0507, KEY_RIGHT },
	{ 0x0508, KEY_PROG1 },
	{ 0x0509, KEY_1 },
	{ 0x050a, KEY_2 },
	{ 0x050b, KEY_3 },
	{ 0x050c, KEY_4 },
	{ 0x050d, KEY_5 },
	{ 0x050e, KEY_6 },
	{ 0x050f, KEY_7 },
	{ 0x0510, KEY_8 },
	{ 0x0511, KEY_9 },
	{ 0x0512, KEY_0 },
	{ 0x0513, KEY_AUDIO },
	{ 0x0515, KEY_EPG },
	{ 0x0516, KEY_PLAY },
	{ 0x0517, KEY_RECORD },
	{ 0x0518, KEY_STOP },
	{ 0x051c, KEY_BACK },
	{ 0x051d, KEY_FORWARD },
	{ 0x054d, KEY_LEFT },
	{ 0x0556, KEY_ZOOM },
};

static struct rc_keymap avermedia_rm_ks_map = {
	.map = {
		.scan    = avermedia_rm_ks,
		.size    = ARRAY_SIZE(avermedia_rm_ks),
		.ir_type = IR_TYPE_NEC,
		.name    = RC_MAP_AVERMEDIA_RM_KS,
	}
};

static int __init init_rc_map_avermedia_rm_ks(void)
{
	return ir_register_map(&avermedia_rm_ks_map);
}

static void __exit exit_rc_map_avermedia_rm_ks(void)
{
	ir_unregister_map(&avermedia_rm_ks_map);
}

module_init(init_rc_map_avermedia_rm_ks)
module_exit(exit_rc_map_avermedia_rm_ks)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
