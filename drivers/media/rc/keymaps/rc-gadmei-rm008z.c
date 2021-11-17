// SPDX-License-Identifier: GPL-2.0+
// gadmei-rm008z.h - Keytable for gadmei_rm008z Remote Controller
//
// keymap imported from ir-keymaps.c
//
// Copyright (c) 2010 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

/* GADMEI UTV330+ RM008Z remote
   Shine Liu <shinel@foxmail.com>
 */

static struct rc_map_table gadmei_rm008z[] = {
	{ 0x14, KEY_POWER2},		/* POWER OFF */
	{ 0x0c, KEY_MUTE},		/* MUTE */

	{ 0x18, KEY_TV},		/* TV */
	{ 0x0e, KEY_VIDEO},		/* AV */
	{ 0x0b, KEY_AUDIO},		/* SV */
	{ 0x0f, KEY_RADIO},		/* FM */

	{ 0x00, KEY_NUMERIC_1},
	{ 0x01, KEY_NUMERIC_2},
	{ 0x02, KEY_NUMERIC_3},
	{ 0x03, KEY_NUMERIC_4},
	{ 0x04, KEY_NUMERIC_5},
	{ 0x05, KEY_NUMERIC_6},
	{ 0x06, KEY_NUMERIC_7},
	{ 0x07, KEY_NUMERIC_8},
	{ 0x08, KEY_NUMERIC_9},
	{ 0x09, KEY_NUMERIC_0},
	{ 0x0a, KEY_INFO},		/* OSD */
	{ 0x1c, KEY_BACKSPACE},		/* LAST */

	{ 0x0d, KEY_PLAY},		/* PLAY */
	{ 0x1e, KEY_CAMERA},		/* SNAPSHOT */
	{ 0x1a, KEY_RECORD},		/* RECORD */
	{ 0x17, KEY_STOP},		/* STOP */

	{ 0x1f, KEY_UP},		/* UP */
	{ 0x44, KEY_DOWN},		/* DOWN */
	{ 0x46, KEY_TAB},		/* BACK */
	{ 0x4a, KEY_ZOOM},		/* FULLSECREEN */

	{ 0x10, KEY_VOLUMEUP},		/* VOLUMEUP */
	{ 0x11, KEY_VOLUMEDOWN},	/* VOLUMEDOWN */
	{ 0x12, KEY_CHANNELUP},		/* CHANNELUP */
	{ 0x13, KEY_CHANNELDOWN},	/* CHANNELDOWN */
	{ 0x15, KEY_ENTER},		/* OK */
};

static struct rc_map_list gadmei_rm008z_map = {
	.map = {
		.scan     = gadmei_rm008z,
		.size     = ARRAY_SIZE(gadmei_rm008z),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_GADMEI_RM008Z,
	}
};

static int __init init_rc_map_gadmei_rm008z(void)
{
	return rc_map_register(&gadmei_rm008z_map);
}

static void __exit exit_rc_map_gadmei_rm008z(void)
{
	rc_map_unregister(&gadmei_rm008z_map);
}

module_init(init_rc_map_gadmei_rm008z)
module_exit(exit_rc_map_gadmei_rm008z)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
