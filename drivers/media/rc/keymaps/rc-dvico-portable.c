// SPDX-License-Identifier: GPL-2.0-only
/*
 * keymap imported from cxusb.c
 *
 * Copyright (C) 2016 Sean Young
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table rc_map_dvico_portable_table[] = {
	{ 0x0302, KEY_SETUP },       /* Profile */
	{ 0x0343, KEY_POWER2 },
	{ 0x0306, KEY_EPG },
	{ 0x035a, KEY_BACK },
	{ 0x0305, KEY_MENU },
	{ 0x0347, KEY_INFO },
	{ 0x0301, KEY_TAB },
	{ 0x0342, KEY_PREVIOUSSONG },/* Replay */
	{ 0x0349, KEY_VOLUMEUP },
	{ 0x0309, KEY_VOLUMEDOWN },
	{ 0x0354, KEY_CHANNELUP },
	{ 0x030b, KEY_CHANNELDOWN },
	{ 0x0316, KEY_CAMERA },
	{ 0x0340, KEY_TUNER },	/* ATV/DTV */
	{ 0x0345, KEY_OPEN },
	{ 0x0319, KEY_NUMERIC_1 },
	{ 0x0318, KEY_NUMERIC_2 },
	{ 0x031b, KEY_NUMERIC_3 },
	{ 0x031a, KEY_NUMERIC_4 },
	{ 0x0358, KEY_NUMERIC_5 },
	{ 0x0359, KEY_NUMERIC_6 },
	{ 0x0315, KEY_NUMERIC_7 },
	{ 0x0314, KEY_NUMERIC_8 },
	{ 0x0317, KEY_NUMERIC_9 },
	{ 0x0344, KEY_ANGLE },	/* Aspect */
	{ 0x0355, KEY_NUMERIC_0 },
	{ 0x0307, KEY_ZOOM },
	{ 0x030a, KEY_REWIND },
	{ 0x0308, KEY_PLAYPAUSE },
	{ 0x034b, KEY_FASTFORWARD },
	{ 0x035b, KEY_MUTE },
	{ 0x0304, KEY_STOP },
	{ 0x0356, KEY_RECORD },
	{ 0x0357, KEY_POWER },
	{ 0x0341, KEY_UNKNOWN },    /* INPUT */
	{ 0x0300, KEY_UNKNOWN },    /* HD */
};

static struct rc_map_list dvico_portable_map = {
	.map = {
		.scan     = rc_map_dvico_portable_table,
		.size     = ARRAY_SIZE(rc_map_dvico_portable_table),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_DVICO_PORTABLE,
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
