// SPDX-License-Identifier: GPL-2.0+
// pinnacle-pctv-hd.h - Keytable for pinnacle_pctv_hd Remote Controller
//
// keymap imported from ir-keymaps.c
//
// Copyright (c) 2010 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

/* Pinnacle PCTV HD 800i mini remote */

static struct rc_map_table pinnacle_pctv_hd[] = {
	/* Key codes for the tiny Pinnacle remote*/
	{ 0x0700, KEY_MUTE },
	{ 0x0701, KEY_MENU }, /* Pinnacle logo */
	{ 0x0739, KEY_POWER },
	{ 0x0703, KEY_VOLUMEUP },
	{ 0x0705, KEY_OK },
	{ 0x0709, KEY_VOLUMEDOWN },
	{ 0x0706, KEY_CHANNELUP },
	{ 0x070c, KEY_CHANNELDOWN },
	{ 0x070f, KEY_1 },
	{ 0x0715, KEY_2 },
	{ 0x0710, KEY_3 },
	{ 0x0718, KEY_4 },
	{ 0x071b, KEY_5 },
	{ 0x071e, KEY_6 },
	{ 0x0711, KEY_7 },
	{ 0x0721, KEY_8 },
	{ 0x0712, KEY_9 },
	{ 0x0727, KEY_0 },
	{ 0x0724, KEY_ZOOM }, /* 'Square' key */
	{ 0x072a, KEY_SUBTITLE },   /* 'T' key */
	{ 0x072d, KEY_REWIND },
	{ 0x0730, KEY_PLAYPAUSE },
	{ 0x0733, KEY_FASTFORWARD },
	{ 0x0736, KEY_RECORD },
	{ 0x073c, KEY_STOP },
	{ 0x073f, KEY_HELP }, /* '?' key */
};

static struct rc_map_list pinnacle_pctv_hd_map = {
	.map = {
		.scan     = pinnacle_pctv_hd,
		.size     = ARRAY_SIZE(pinnacle_pctv_hd),
		.rc_proto = RC_PROTO_RC5,
		.name     = RC_MAP_PINNACLE_PCTV_HD,
	}
};

static int __init init_rc_map_pinnacle_pctv_hd(void)
{
	return rc_map_register(&pinnacle_pctv_hd_map);
}

static void __exit exit_rc_map_pinnacle_pctv_hd(void)
{
	rc_map_unregister(&pinnacle_pctv_hd_map);
}

module_init(init_rc_map_pinnacle_pctv_hd)
module_exit(exit_rc_map_pinnacle_pctv_hd)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
