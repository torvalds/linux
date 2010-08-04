/* tbs-nec.h - Keytable for tbs_nec Remote Controller
 *
 * keymap imported from ir-keymaps.c
 *
 * Copyright (c) 2010 by Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>

static struct ir_scancode tbs_nec[] = {
	{ 0x04, KEY_POWER2},	/*power*/
	{ 0x14, KEY_MUTE},	/*mute*/
	{ 0x07, KEY_1},
	{ 0x06, KEY_2},
	{ 0x05, KEY_3},
	{ 0x0b, KEY_4},
	{ 0x0a, KEY_5},
	{ 0x09, KEY_6},
	{ 0x0f, KEY_7},
	{ 0x0e, KEY_8},
	{ 0x0d, KEY_9},
	{ 0x12, KEY_0},
	{ 0x16, KEY_CHANNELUP},	/*ch+*/
	{ 0x11, KEY_CHANNELDOWN},/*ch-*/
	{ 0x13, KEY_VOLUMEUP},	/*vol+*/
	{ 0x0c, KEY_VOLUMEDOWN},/*vol-*/
	{ 0x03, KEY_RECORD},	/*rec*/
	{ 0x18, KEY_PAUSE},	/*pause*/
	{ 0x19, KEY_OK},	/*ok*/
	{ 0x1a, KEY_CAMERA},	/* snapshot */
	{ 0x01, KEY_UP},
	{ 0x10, KEY_LEFT},
	{ 0x02, KEY_RIGHT},
	{ 0x08, KEY_DOWN},
	{ 0x15, KEY_FAVORITES},
	{ 0x17, KEY_SUBTITLE},
	{ 0x1d, KEY_ZOOM},
	{ 0x1f, KEY_EXIT},
	{ 0x1e, KEY_MENU},
	{ 0x1c, KEY_EPG},
	{ 0x00, KEY_PREVIOUS},
	{ 0x1b, KEY_MODE},
};

static struct rc_keymap tbs_nec_map = {
	.map = {
		.scan    = tbs_nec,
		.size    = ARRAY_SIZE(tbs_nec),
		.ir_type = IR_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_TBS_NEC,
	}
};

static int __init init_rc_map_tbs_nec(void)
{
	return ir_register_map(&tbs_nec_map);
}

static void __exit exit_rc_map_tbs_nec(void)
{
	ir_unregister_map(&tbs_nec_map);
}

module_init(init_rc_map_tbs_nec)
module_exit(exit_rc_map_tbs_nec)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
