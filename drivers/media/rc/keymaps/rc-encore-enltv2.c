/* encore-enltv2.h - Keytable for encore_enltv2 Remote Controller
 *
 * keymap imported from ir-keymaps.c
 *
 * Copyright (c) 2010 by Mauro Carvalho Chehab
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>
#include <linux/module.h>

/* Encore ENLTV2-FM  - silver plastic - "Wand Media" written at the botton
    Mauro Carvalho Chehab <mchehab@infradead.org> */

static struct rc_map_table encore_enltv2[] = {
	{ 0x4c, KEY_POWER2 },
	{ 0x4a, KEY_TUNER },
	{ 0x40, KEY_1 },
	{ 0x60, KEY_2 },
	{ 0x50, KEY_3 },
	{ 0x70, KEY_4 },
	{ 0x48, KEY_5 },
	{ 0x68, KEY_6 },
	{ 0x58, KEY_7 },
	{ 0x78, KEY_8 },
	{ 0x44, KEY_9 },
	{ 0x54, KEY_0 },

	{ 0x64, KEY_LAST },		/* +100 */
	{ 0x4e, KEY_AGAIN },		/* Recall */

	{ 0x6c, KEY_VIDEO },		/* Video Source */
	{ 0x5e, KEY_MENU },
	{ 0x56, KEY_SCREEN },
	{ 0x7a, KEY_SETUP },

	{ 0x46, KEY_MUTE },
	{ 0x5c, KEY_MODE },		/* Stereo */
	{ 0x74, KEY_INFO },
	{ 0x7c, KEY_CLEAR },

	{ 0x55, KEY_UP },
	{ 0x49, KEY_DOWN },
	{ 0x7e, KEY_LEFT },
	{ 0x59, KEY_RIGHT },
	{ 0x6a, KEY_ENTER },

	{ 0x42, KEY_VOLUMEUP },
	{ 0x62, KEY_VOLUMEDOWN },
	{ 0x52, KEY_CHANNELUP },
	{ 0x72, KEY_CHANNELDOWN },

	{ 0x41, KEY_RECORD },
	{ 0x51, KEY_CAMERA },		/* Snapshot */
	{ 0x75, KEY_TIME },		/* Timeshift */
	{ 0x71, KEY_TV2 },		/* PIP */

	{ 0x45, KEY_REWIND },
	{ 0x6f, KEY_PAUSE },
	{ 0x7d, KEY_FORWARD },
	{ 0x79, KEY_STOP },
};

static struct rc_map_list encore_enltv2_map = {
	.map = {
		.scan     = encore_enltv2,
		.size     = ARRAY_SIZE(encore_enltv2),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_ENCORE_ENLTV2,
	}
};

static int __init init_rc_map_encore_enltv2(void)
{
	return rc_map_register(&encore_enltv2_map);
}

static void __exit exit_rc_map_encore_enltv2(void)
{
	rc_map_unregister(&encore_enltv2_map);
}

module_init(init_rc_map_encore_enltv2)
module_exit(exit_rc_map_encore_enltv2)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
