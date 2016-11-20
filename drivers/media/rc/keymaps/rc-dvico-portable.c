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

static struct rc_map_table rc_map_dvico_portable_table[] = {
	{ 0xfc02, KEY_SETUP },       /* Profile */
	{ 0xfc43, KEY_POWER2 },
	{ 0xfc06, KEY_EPG },
	{ 0xfc5a, KEY_BACK },
	{ 0xfc05, KEY_MENU },
	{ 0xfc47, KEY_INFO },
	{ 0xfc01, KEY_TAB },
	{ 0xfc42, KEY_PREVIOUSSONG },/* Replay */
	{ 0xfc49, KEY_VOLUMEUP },
	{ 0xfc09, KEY_VOLUMEDOWN },
	{ 0xfc54, KEY_CHANNELUP },
	{ 0xfc0b, KEY_CHANNELDOWN },
	{ 0xfc16, KEY_CAMERA },
	{ 0xfc40, KEY_TUNER },	/* ATV/DTV */
	{ 0xfc45, KEY_OPEN },
	{ 0xfc19, KEY_1 },
	{ 0xfc18, KEY_2 },
	{ 0xfc1b, KEY_3 },
	{ 0xfc1a, KEY_4 },
	{ 0xfc58, KEY_5 },
	{ 0xfc59, KEY_6 },
	{ 0xfc15, KEY_7 },
	{ 0xfc14, KEY_8 },
	{ 0xfc17, KEY_9 },
	{ 0xfc44, KEY_ANGLE },	/* Aspect */
	{ 0xfc55, KEY_0 },
	{ 0xfc07, KEY_ZOOM },
	{ 0xfc0a, KEY_REWIND },
	{ 0xfc08, KEY_PLAYPAUSE },
	{ 0xfc4b, KEY_FASTFORWARD },
	{ 0xfc5b, KEY_MUTE },
	{ 0xfc04, KEY_STOP },
	{ 0xfc56, KEY_RECORD },
	{ 0xfc57, KEY_POWER },
	{ 0xfc41, KEY_UNKNOWN },    /* INPUT */
	{ 0xfc00, KEY_UNKNOWN },    /* HD */
};

static struct rc_map_list dvico_portable_map = {
	.map = {
		.scan    = rc_map_dvico_portable_table,
		.size    = ARRAY_SIZE(rc_map_dvico_portable_table),
		.rc_type = RC_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_DVICO_PORTABLE,
	}
};

static int __init init_rc_map_dvico_portable(void)
{
	return rc_map_register(&dvico_portable_map);
}

static void __exit exit_rc_map_dvico_portable(void)
{
	rc_map_unregister(&dvico_portable_map);
}

module_init(init_rc_map_dvico_portable)
module_exit(exit_rc_map_dvico_portable)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
