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

static struct rc_map_table tbs_nec[] = {
	{ 0x84, KEY_POWER2},		/* power */
	{ 0x94, KEY_MUTE},		/* mute */
	{ 0x87, KEY_1},
	{ 0x86, KEY_2},
	{ 0x85, KEY_3},
	{ 0x8b, KEY_4},
	{ 0x8a, KEY_5},
	{ 0x89, KEY_6},
	{ 0x8f, KEY_7},
	{ 0x8e, KEY_8},
	{ 0x8d, KEY_9},
	{ 0x92, KEY_0},
	{ 0xc0, KEY_10CHANNELSUP},	/* 10+ */
	{ 0xd0, KEY_10CHANNELSDOWN},	/* 10- */
	{ 0x96, KEY_CHANNELUP},		/* ch+ */
	{ 0x91, KEY_CHANNELDOWN},	/* ch- */
	{ 0x93, KEY_VOLUMEUP},		/* vol+ */
	{ 0x8c, KEY_VOLUMEDOWN},	/* vol- */
	{ 0x83, KEY_RECORD},		/* rec */
	{ 0x98, KEY_PAUSE},		/* pause, yellow */
	{ 0x99, KEY_OK},		/* ok */
	{ 0x9a, KEY_CAMERA},		/* snapshot */
	{ 0x81, KEY_UP},
	{ 0x90, KEY_LEFT},
	{ 0x82, KEY_RIGHT},
	{ 0x88, KEY_DOWN},
	{ 0x95, KEY_FAVORITES},		/* blue */
	{ 0x97, KEY_SUBTITLE},		/* green */
	{ 0x9d, KEY_ZOOM},
	{ 0x9f, KEY_EXIT},
	{ 0x9e, KEY_MENU},
	{ 0x9c, KEY_EPG},
	{ 0x80, KEY_PREVIOUS},		/* red */
	{ 0x9b, KEY_MODE},
};

static struct rc_map_list tbs_nec_map = {
	.map = {
		.scan    = tbs_nec,
		.size    = ARRAY_SIZE(tbs_nec),
		.rc_type = RC_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_TBS_NEC,
	}
};

static int __init init_rc_map_tbs_nec(void)
{
	return rc_map_register(&tbs_nec_map);
}

static void __exit exit_rc_map_tbs_nec(void)
{
	rc_map_unregister(&tbs_nec_map);
}

module_init(init_rc_map_tbs_nec)
module_exit(exit_rc_map_tbs_nec)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
