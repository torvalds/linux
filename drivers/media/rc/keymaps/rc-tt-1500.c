/* tt-1500.h - Keytable for tt_1500 Remote Controller
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

/* for the Technotrend 1500 bundled remotes (grey and black): */

static struct rc_map_table tt_1500[] = {
	{ 0x1501, KEY_POWER },
	{ 0x1502, KEY_SHUFFLE },		/* ? double-arrow key */
	{ 0x1503, KEY_1 },
	{ 0x1504, KEY_2 },
	{ 0x1505, KEY_3 },
	{ 0x1506, KEY_4 },
	{ 0x1507, KEY_5 },
	{ 0x1508, KEY_6 },
	{ 0x1509, KEY_7 },
	{ 0x150a, KEY_8 },
	{ 0x150b, KEY_9 },
	{ 0x150c, KEY_0 },
	{ 0x150d, KEY_UP },
	{ 0x150e, KEY_LEFT },
	{ 0x150f, KEY_OK },
	{ 0x1510, KEY_RIGHT },
	{ 0x1511, KEY_DOWN },
	{ 0x1512, KEY_INFO },
	{ 0x1513, KEY_EXIT },
	{ 0x1514, KEY_RED },
	{ 0x1515, KEY_GREEN },
	{ 0x1516, KEY_YELLOW },
	{ 0x1517, KEY_BLUE },
	{ 0x1518, KEY_MUTE },
	{ 0x1519, KEY_TEXT },
	{ 0x151a, KEY_MODE },		/* ? TV/Radio */
	{ 0x1521, KEY_OPTION },
	{ 0x1522, KEY_EPG },
	{ 0x1523, KEY_CHANNELUP },
	{ 0x1524, KEY_CHANNELDOWN },
	{ 0x1525, KEY_VOLUMEUP },
	{ 0x1526, KEY_VOLUMEDOWN },
	{ 0x1527, KEY_SETUP },
	{ 0x153a, KEY_RECORD },		/* these keys are only in the black remote */
	{ 0x153b, KEY_PLAY },
	{ 0x153c, KEY_STOP },
	{ 0x153d, KEY_REWIND },
	{ 0x153e, KEY_PAUSE },
	{ 0x153f, KEY_FORWARD },
};

static struct rc_map_list tt_1500_map = {
	.map = {
		.scan     = tt_1500,
		.size     = ARRAY_SIZE(tt_1500),
		.rc_proto = RC_PROTO_RC5,
		.name     = RC_MAP_TT_1500,
	}
};

static int __init init_rc_map_tt_1500(void)
{
	return rc_map_register(&tt_1500_map);
}

static void __exit exit_rc_map_tt_1500(void)
{
	rc_map_unregister(&tt_1500_map);
}

module_init(init_rc_map_tt_1500)
module_exit(exit_rc_map_tt_1500)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
