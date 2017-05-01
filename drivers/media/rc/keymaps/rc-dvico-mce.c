/*
 * keymap imported from cxusb.c
 *
 * Copyright (C) 2016 Sean Young
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table rc_map_dvico_mce_table[] = {
	{ 0xfe02, KEY_TV },
	{ 0xfe0e, KEY_MP3 },
	{ 0xfe1a, KEY_DVD },
	{ 0xfe1e, KEY_FAVORITES },
	{ 0xfe16, KEY_SETUP },
	{ 0xfe46, KEY_POWER2 },
	{ 0xfe0a, KEY_EPG },
	{ 0xfe49, KEY_BACK },
	{ 0xfe4d, KEY_MENU },
	{ 0xfe51, KEY_UP },
	{ 0xfe5b, KEY_LEFT },
	{ 0xfe5f, KEY_RIGHT },
	{ 0xfe53, KEY_DOWN },
	{ 0xfe5e, KEY_OK },
	{ 0xfe59, KEY_INFO },
	{ 0xfe55, KEY_TAB },
	{ 0xfe0f, KEY_PREVIOUSSONG },/* Replay */
	{ 0xfe12, KEY_NEXTSONG },	/* Skip */
	{ 0xfe42, KEY_ENTER	 },	/* Windows/Start */
	{ 0xfe15, KEY_VOLUMEUP },
	{ 0xfe05, KEY_VOLUMEDOWN },
	{ 0xfe11, KEY_CHANNELUP },
	{ 0xfe09, KEY_CHANNELDOWN },
	{ 0xfe52, KEY_CAMERA },
	{ 0xfe5a, KEY_TUNER },	/* Live */
	{ 0xfe19, KEY_OPEN },
	{ 0xfe0b, KEY_1 },
	{ 0xfe17, KEY_2 },
	{ 0xfe1b, KEY_3 },
	{ 0xfe07, KEY_4 },
	{ 0xfe50, KEY_5 },
	{ 0xfe54, KEY_6 },
	{ 0xfe48, KEY_7 },
	{ 0xfe4c, KEY_8 },
	{ 0xfe58, KEY_9 },
	{ 0xfe13, KEY_ANGLE },	/* Aspect */
	{ 0xfe03, KEY_0 },
	{ 0xfe1f, KEY_ZOOM },
	{ 0xfe43, KEY_REWIND },
	{ 0xfe47, KEY_PLAYPAUSE },
	{ 0xfe4f, KEY_FASTFORWARD },
	{ 0xfe57, KEY_MUTE },
	{ 0xfe0d, KEY_STOP },
	{ 0xfe01, KEY_RECORD },
	{ 0xfe4e, KEY_POWER },
};

static struct rc_map_list dvico_mce_map = {
	.map = {
		.scan    = rc_map_dvico_mce_table,
		.size    = ARRAY_SIZE(rc_map_dvico_mce_table),
		.rc_type = RC_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_DVICO_MCE,
	}
};

static int __init init_rc_map_dvico_mce(void)
{
	return rc_map_register(&dvico_mce_map);
}

static void __exit exit_rc_map_dvico_mce(void)
{
	rc_map_unregister(&dvico_mce_map);
}

module_init(init_rc_map_dvico_mce)
module_exit(exit_rc_map_dvico_mce)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
