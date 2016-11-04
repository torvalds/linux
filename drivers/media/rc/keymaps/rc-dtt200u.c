/* Keytable for Wideview WT-220U.
 *
 * Copyright (c) 2016 Jonathan McDowell <noodles@earth.li>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>
#include <linux/module.h>

/* key list for the tiny remote control (Yakumo, don't know about the others) */
static struct rc_map_table dtt200u_table[] = {
	{ 0x8001, KEY_MUTE },
	{ 0x8002, KEY_CHANNELDOWN },
	{ 0x8003, KEY_VOLUMEDOWN },
	{ 0x8004, KEY_1 },
	{ 0x8005, KEY_2 },
	{ 0x8006, KEY_3 },
	{ 0x8007, KEY_4 },
	{ 0x8008, KEY_5 },
	{ 0x8009, KEY_6 },
	{ 0x800a, KEY_7 },
	{ 0x800c, KEY_ZOOM },
	{ 0x800d, KEY_0 },
	{ 0x800e, KEY_SELECT },
	{ 0x8012, KEY_POWER },
	{ 0x801a, KEY_CHANNELUP },
	{ 0x801b, KEY_8 },
	{ 0x801e, KEY_VOLUMEUP },
	{ 0x801f, KEY_9 },
};

static struct rc_map_list dtt200u_map = {
	.map = {
		.scan    = dtt200u_table,
		.size    = ARRAY_SIZE(dtt200u_table),
		.rc_type = RC_TYPE_NEC,
		.name    = RC_MAP_DTT200U,
	}
};

static int __init init_rc_map_dtt200u(void)
{
	return rc_map_register(&dtt200u_map);
}

static void __exit exit_rc_map_dtt200u(void)
{
	rc_map_unregister(&dtt200u_map);
}

module_init(init_rc_map_dtt200u)
module_exit(exit_rc_map_dtt200u)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jonathan McDowell <noodles@earth.li>");
