// SPDX-License-Identifier: GPL-2.0-or-later
/* rc-dvbsky.c - Keytable for DVBSky Remote Controllers
 *
 * keymap imported from ir-keymaps.c
 *
 * Copyright (c) 2010-2012 by Nibble Max <nibble.max@gmail.com>
 */

#include <media/rc-map.h>
#include <linux/module.h>
/*
 * This table contains the complete RC5 code, instead of just the data part
 */

static struct rc_map_table rc5_dvbsky[] = {
	{ 0x0000, KEY_NUMERIC_0 },
	{ 0x0001, KEY_NUMERIC_1 },
	{ 0x0002, KEY_NUMERIC_2 },
	{ 0x0003, KEY_NUMERIC_3 },
	{ 0x0004, KEY_NUMERIC_4 },
	{ 0x0005, KEY_NUMERIC_5 },
	{ 0x0006, KEY_NUMERIC_6 },
	{ 0x0007, KEY_NUMERIC_7 },
	{ 0x0008, KEY_NUMERIC_8 },
	{ 0x0009, KEY_NUMERIC_9 },
	{ 0x000a, KEY_MUTE },
	{ 0x000d, KEY_OK },
	{ 0x000b, KEY_STOP },
	{ 0x000c, KEY_EXIT },
	{ 0x000e, KEY_CAMERA }, /*Snap shot*/
	{ 0x000f, KEY_SUBTITLE }, /*PIP*/
	{ 0x0010, KEY_VOLUMEUP },
	{ 0x0011, KEY_VOLUMEDOWN },
	{ 0x0012, KEY_FAVORITES },
	{ 0x0013, KEY_LIST }, /*Info*/
	{ 0x0016, KEY_PAUSE },
	{ 0x0017, KEY_PLAY },
	{ 0x001f, KEY_RECORD },
	{ 0x0020, KEY_CHANNELDOWN },
	{ 0x0021, KEY_CHANNELUP },
	{ 0x0025, KEY_POWER2 },
	{ 0x0026, KEY_REWIND },
	{ 0x0027, KEY_FASTFORWARD },
	{ 0x0029, KEY_LAST },
	{ 0x002b, KEY_MENU },
	{ 0x002c, KEY_EPG },
	{ 0x002d, KEY_ZOOM },
};

static struct rc_map_list rc5_dvbsky_map = {
	.map = {
		.scan     = rc5_dvbsky,
		.size     = ARRAY_SIZE(rc5_dvbsky),
		.rc_proto = RC_PROTO_RC5,
		.name     = RC_MAP_DVBSKY,
	}
};

static int __init init_rc_map_rc5_dvbsky(void)
{
	return rc_map_register(&rc5_dvbsky_map);
}

static void __exit exit_rc_map_rc5_dvbsky(void)
{
	rc_map_unregister(&rc5_dvbsky_map);
}

module_init(init_rc_map_rc5_dvbsky)
module_exit(exit_rc_map_rc5_dvbsky)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nibble Max <nibble.max@gmail.com>");
