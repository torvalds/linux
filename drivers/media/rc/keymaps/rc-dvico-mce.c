// SPDX-License-Identifier: GPL-2.0-only
/*
 * keymap imported from cxusb.c
 *
 * Copyright (C) 2016 Sean Young
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table rc_map_dvico_mce_table[] = {
	{ 0x0102, KEY_TV },
	{ 0x010e, KEY_MP3 },
	{ 0x011a, KEY_DVD },
	{ 0x011e, KEY_FAVORITES },
	{ 0x0116, KEY_SETUP },
	{ 0x0146, KEY_POWER2 },
	{ 0x010a, KEY_EPG },
	{ 0x0149, KEY_BACK },
	{ 0x014d, KEY_MENU },
	{ 0x0151, KEY_UP },
	{ 0x015b, KEY_LEFT },
	{ 0x015f, KEY_RIGHT },
	{ 0x0153, KEY_DOWN },
	{ 0x015e, KEY_OK },
	{ 0x0159, KEY_INFO },
	{ 0x0155, KEY_TAB },
	{ 0x010f, KEY_PREVIOUSSONG },/* Replay */
	{ 0x0112, KEY_NEXTSONG },	/* Skip */
	{ 0x0142, KEY_ENTER	 },	/* Windows/Start */
	{ 0x0115, KEY_VOLUMEUP },
	{ 0x0105, KEY_VOLUMEDOWN },
	{ 0x0111, KEY_CHANNELUP },
	{ 0x0109, KEY_CHANNELDOWN },
	{ 0x0152, KEY_CAMERA },
	{ 0x015a, KEY_TUNER },	/* Live */
	{ 0x0119, KEY_OPEN },
	{ 0x010b, KEY_NUMERIC_1 },
	{ 0x0117, KEY_NUMERIC_2 },
	{ 0x011b, KEY_NUMERIC_3 },
	{ 0x0107, KEY_NUMERIC_4 },
	{ 0x0150, KEY_NUMERIC_5 },
	{ 0x0154, KEY_NUMERIC_6 },
	{ 0x0148, KEY_NUMERIC_7 },
	{ 0x014c, KEY_NUMERIC_8 },
	{ 0x0158, KEY_NUMERIC_9 },
	{ 0x0113, KEY_ANGLE },	/* Aspect */
	{ 0x0103, KEY_NUMERIC_0 },
	{ 0x011f, KEY_ZOOM },
	{ 0x0143, KEY_REWIND },
	{ 0x0147, KEY_PLAYPAUSE },
	{ 0x014f, KEY_FASTFORWARD },
	{ 0x0157, KEY_MUTE },
	{ 0x010d, KEY_STOP },
	{ 0x0101, KEY_RECORD },
	{ 0x014e, KEY_POWER },
};

static struct rc_map_list dvico_mce_map = {
	.map = {
		.scan     = rc_map_dvico_mce_table,
		.size     = ARRAY_SIZE(rc_map_dvico_mce_table),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_DVICO_MCE,
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
