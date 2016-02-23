/* evga-indtube.h - Keytable for evga_indtube Remote Controller
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

/* EVGA inDtube
   Devin Heitmueller <devin.heitmueller@gmail.com>
 */

static struct rc_map_table evga_indtube[] = {
	{ 0x12, KEY_POWER},
	{ 0x02, KEY_MODE},	/* TV */
	{ 0x14, KEY_MUTE},
	{ 0x1a, KEY_CHANNELUP},
	{ 0x16, KEY_TV2},	/* PIP */
	{ 0x1d, KEY_VOLUMEUP},
	{ 0x05, KEY_CHANNELDOWN},
	{ 0x0f, KEY_PLAYPAUSE},
	{ 0x19, KEY_VOLUMEDOWN},
	{ 0x1c, KEY_REWIND},
	{ 0x0d, KEY_RECORD},
	{ 0x18, KEY_FORWARD},
	{ 0x1e, KEY_PREVIOUS},
	{ 0x1b, KEY_STOP},
	{ 0x1f, KEY_NEXT},
	{ 0x13, KEY_CAMERA},
};

static struct rc_map_list evga_indtube_map = {
	.map = {
		.scan    = evga_indtube,
		.size    = ARRAY_SIZE(evga_indtube),
		.rc_type = RC_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_EVGA_INDTUBE,
	}
};

static int __init init_rc_map_evga_indtube(void)
{
	return rc_map_register(&evga_indtube_map);
}

static void __exit exit_rc_map_evga_indtube(void)
{
	rc_map_unregister(&evga_indtube_map);
}

module_init(init_rc_map_evga_indtube)
module_exit(exit_rc_map_evga_indtube)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
