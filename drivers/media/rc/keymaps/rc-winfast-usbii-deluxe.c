// SPDX-License-Identifier: GPL-2.0+
// winfast-usbii-deluxe.h - Keytable for winfast_usbii_deluxe Remote Controller
//
// keymap imported from ir-keymaps.c
//
// Copyright (c) 2010 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

/* Leadtek Winfast TV USB II Deluxe remote
   Magnus Alm <magnus.alm@gmail.com>
 */

static struct rc_map_table winfast_usbii_deluxe[] = {
	{ 0x62, KEY_0},
	{ 0x75, KEY_1},
	{ 0x76, KEY_2},
	{ 0x77, KEY_3},
	{ 0x79, KEY_4},
	{ 0x7a, KEY_5},
	{ 0x7b, KEY_6},
	{ 0x7d, KEY_7},
	{ 0x7e, KEY_8},
	{ 0x7f, KEY_9},

	{ 0x38, KEY_CAMERA},		/* SNAPSHOT */
	{ 0x37, KEY_RECORD},		/* RECORD */
	{ 0x35, KEY_TIME},		/* TIMESHIFT */

	{ 0x74, KEY_VOLUMEUP},		/* VOLUMEUP */
	{ 0x78, KEY_VOLUMEDOWN},	/* VOLUMEDOWN */
	{ 0x64, KEY_MUTE},		/* MUTE */

	{ 0x21, KEY_CHANNEL},		/* SURF */
	{ 0x7c, KEY_CHANNELUP},		/* CHANNELUP */
	{ 0x60, KEY_CHANNELDOWN},	/* CHANNELDOWN */
	{ 0x61, KEY_LAST},		/* LAST CHANNEL (RECALL) */

	{ 0x72, KEY_VIDEO},		/* INPUT MODES (TV/FM) */

	{ 0x70, KEY_POWER2},		/* TV ON/OFF */

	{ 0x39, KEY_CYCLEWINDOWS},	/* MINIMIZE (BOSS) */
	{ 0x3a, KEY_NEW},		/* PIP */
	{ 0x73, KEY_ZOOM},		/* FULLSECREEN */

	{ 0x66, KEY_INFO},		/* OSD (DISPLAY) */

	{ 0x31, KEY_DOT},		/* '.' */
	{ 0x63, KEY_ENTER},		/* ENTER */

};

static struct rc_map_list winfast_usbii_deluxe_map = {
	.map = {
		.scan     = winfast_usbii_deluxe,
		.size     = ARRAY_SIZE(winfast_usbii_deluxe),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_WINFAST_USBII_DELUXE,
	}
};

static int __init init_rc_map_winfast_usbii_deluxe(void)
{
	return rc_map_register(&winfast_usbii_deluxe_map);
}

static void __exit exit_rc_map_winfast_usbii_deluxe(void)
{
	rc_map_unregister(&winfast_usbii_deluxe_map);
}

module_init(init_rc_map_winfast_usbii_deluxe)
module_exit(exit_rc_map_winfast_usbii_deluxe)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
