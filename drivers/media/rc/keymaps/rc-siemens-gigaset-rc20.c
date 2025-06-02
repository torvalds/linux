// SPDX-License-Identifier: GPL-2.0-or-later
/* rc-siemens-gigaset-rc20.c - Keytable for the Siemens Gigaset RC 20 remote
 *
 * Copyright (c) 2025 by Michael Klein
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table siemens_gigaset_rc20[] = {
	{ 0x1501, KEY_POWER },
	{ 0x1502, KEY_MUTE },
	{ 0x1503, KEY_NUMERIC_1 },
	{ 0x1504, KEY_NUMERIC_2 },
	{ 0x1505, KEY_NUMERIC_3 },
	{ 0x1506, KEY_NUMERIC_4 },
	{ 0x1507, KEY_NUMERIC_5 },
	{ 0x1508, KEY_NUMERIC_6 },
	{ 0x1509, KEY_NUMERIC_7 },
	{ 0x150a, KEY_NUMERIC_8 },
	{ 0x150b, KEY_NUMERIC_9 },
	{ 0x150c, KEY_NUMERIC_0 },
	{ 0x150d, KEY_UP },
	{ 0x150e, KEY_LEFT },
	{ 0x150f, KEY_OK },
	{ 0x1510, KEY_RIGHT },
	{ 0x1511, KEY_DOWN },
	{ 0x1512, KEY_SHUFFLE },        /* double-arrow */
	{ 0x1513, KEY_EXIT },
	{ 0x1514, KEY_RED },
	{ 0x1515, KEY_GREEN },
	{ 0x1516, KEY_YELLOW },         /* OPT */
	{ 0x1517, KEY_BLUE },
	{ 0x1518, KEY_MENU },
	{ 0x1519, KEY_TEXT },
	{ 0x151a, KEY_MODE },           /* TV/Radio */

	{ 0x1521, KEY_EPG },
	{ 0x1522, KEY_FAVORITES },
	{ 0x1523, KEY_CHANNELUP },
	{ 0x1524, KEY_CHANNELDOWN },
	{ 0x1525, KEY_VOLUMEUP },
	{ 0x1526, KEY_VOLUMEDOWN },
	{ 0x1527, KEY_INFO },
};

static struct rc_map_list siemens_gigaset_rc20_map = {
	.map = {
		.scan     = siemens_gigaset_rc20,
		.size     = ARRAY_SIZE(siemens_gigaset_rc20),
		.rc_proto = RC_PROTO_RC5,
		.name     = RC_MAP_SIEMENS_GIGASET_RC20,
	}
};

static int __init init_rc_map_siemens_gigaset_rc20(void)
{
	return rc_map_register(&siemens_gigaset_rc20_map);
}

static void __exit exit_rc_map_siemens_gigaset_rc20(void)
{
	rc_map_unregister(&siemens_gigaset_rc20_map);
}

module_init(init_rc_map_siemens_gigaset_rc20)
module_exit(exit_rc_map_siemens_gigaset_rc20)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Klein");
MODULE_DESCRIPTION("Siemens Gigaset RC20 remote keytable");
