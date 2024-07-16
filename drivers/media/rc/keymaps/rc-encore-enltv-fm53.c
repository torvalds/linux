// SPDX-License-Identifier: GPL-2.0+
// encore-enltv-fm53.h - Keytable for encore_enltv_fm53 Remote Controller
//
// keymap imported from ir-keymaps.c
//
// Copyright (c) 2010 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

/* Encore ENLTV-FM v5.3
   Mauro Carvalho Chehab <mchehab@kernel.org>
 */

static struct rc_map_table encore_enltv_fm53[] = {
	{ 0x10, KEY_POWER2},
	{ 0x06, KEY_MUTE},

	{ 0x09, KEY_NUMERIC_1},
	{ 0x1d, KEY_NUMERIC_2},
	{ 0x1f, KEY_NUMERIC_3},
	{ 0x19, KEY_NUMERIC_4},
	{ 0x1b, KEY_NUMERIC_5},
	{ 0x11, KEY_NUMERIC_6},
	{ 0x17, KEY_NUMERIC_7},
	{ 0x12, KEY_NUMERIC_8},
	{ 0x16, KEY_NUMERIC_9},
	{ 0x48, KEY_NUMERIC_0},

	{ 0x04, KEY_LIST},		/* -/-- */
	{ 0x40, KEY_LAST},		/* recall */

	{ 0x02, KEY_MODE},		/* TV/AV */
	{ 0x05, KEY_CAMERA},		/* SNAPSHOT */

	{ 0x4c, KEY_CHANNELUP},		/* UP */
	{ 0x00, KEY_CHANNELDOWN},	/* DOWN */
	{ 0x0d, KEY_VOLUMEUP},		/* RIGHT */
	{ 0x15, KEY_VOLUMEDOWN},	/* LEFT */
	{ 0x49, KEY_ENTER},		/* OK */

	{ 0x54, KEY_RECORD},
	{ 0x4d, KEY_PLAY},		/* pause */

	{ 0x1e, KEY_MENU},		/* video setting */
	{ 0x0e, KEY_RIGHT},		/* <- */
	{ 0x1a, KEY_LEFT},		/* -> */

	{ 0x0a, KEY_CLEAR},		/* video default */
	{ 0x0c, KEY_ZOOM},		/* hide pannel */
	{ 0x47, KEY_SLEEP},		/* shutdown */
};

static struct rc_map_list encore_enltv_fm53_map = {
	.map = {
		.scan     = encore_enltv_fm53,
		.size     = ARRAY_SIZE(encore_enltv_fm53),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_ENCORE_ENLTV_FM53,
	}
};

static int __init init_rc_map_encore_enltv_fm53(void)
{
	return rc_map_register(&encore_enltv_fm53_map);
}

static void __exit exit_rc_map_encore_enltv_fm53(void)
{
	rc_map_unregister(&encore_enltv_fm53_map);
}

module_init(init_rc_map_encore_enltv_fm53)
module_exit(exit_rc_map_encore_enltv_fm53)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
