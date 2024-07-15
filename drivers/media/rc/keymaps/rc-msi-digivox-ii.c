// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MSI DIGIVOX mini II remote controller keytable
 *
 * Copyright (C) 2010 Antti Palosaari <crope@iki.fi>
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table msi_digivox_ii[] = {
	{ 0x0302, KEY_NUMERIC_2 },
	{ 0x0303, KEY_UP },              /* up */
	{ 0x0304, KEY_NUMERIC_3 },
	{ 0x0305, KEY_CHANNELDOWN },
	{ 0x0308, KEY_NUMERIC_5 },
	{ 0x0309, KEY_NUMERIC_0 },
	{ 0x030b, KEY_NUMERIC_8 },
	{ 0x030d, KEY_DOWN },            /* down */
	{ 0x0310, KEY_NUMERIC_9 },
	{ 0x0311, KEY_NUMERIC_7 },
	{ 0x0314, KEY_VOLUMEUP },
	{ 0x0315, KEY_CHANNELUP },
	{ 0x0316, KEY_OK },
	{ 0x0317, KEY_POWER2 },
	{ 0x031a, KEY_NUMERIC_1 },
	{ 0x031c, KEY_NUMERIC_4 },
	{ 0x031d, KEY_NUMERIC_6 },
	{ 0x031f, KEY_VOLUMEDOWN },
};

static struct rc_map_list msi_digivox_ii_map = {
	.map = {
		.scan     = msi_digivox_ii,
		.size     = ARRAY_SIZE(msi_digivox_ii),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_MSI_DIGIVOX_II,
	}
};

static int __init init_rc_map_msi_digivox_ii(void)
{
	return rc_map_register(&msi_digivox_ii_map);
}

static void __exit exit_rc_map_msi_digivox_ii(void)
{
	rc_map_unregister(&msi_digivox_ii_map);
}

module_init(init_rc_map_msi_digivox_ii)
module_exit(exit_rc_map_msi_digivox_ii)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("MSI DIGIVOX mini II remote controller keytable");
