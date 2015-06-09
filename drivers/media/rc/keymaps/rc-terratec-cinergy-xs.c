/* terratec-cinergy-xs.h - Keytable for terratec_cinergy_xs Remote Controller
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

/* Terratec Cinergy Hybrid T USB XS
   Devin Heitmueller <dheitmueller@linuxtv.org>
 */

static struct rc_map_table terratec_cinergy_xs[] = {
	{ 0x41, KEY_HOME},
	{ 0x01, KEY_POWER},
	{ 0x42, KEY_MENU},
	{ 0x02, KEY_1},
	{ 0x03, KEY_2},
	{ 0x04, KEY_3},
	{ 0x43, KEY_SUBTITLE},
	{ 0x05, KEY_4},
	{ 0x06, KEY_5},
	{ 0x07, KEY_6},
	{ 0x44, KEY_TEXT},
	{ 0x08, KEY_7},
	{ 0x09, KEY_8},
	{ 0x0a, KEY_9},
	{ 0x45, KEY_DELETE},
	{ 0x0b, KEY_TUNER},
	{ 0x0c, KEY_0},
	{ 0x0d, KEY_MODE},
	{ 0x46, KEY_TV},
	{ 0x47, KEY_DVD},
	{ 0x49, KEY_VIDEO},
	{ 0x4b, KEY_AUX},
	{ 0x10, KEY_UP},
	{ 0x11, KEY_LEFT},
	{ 0x12, KEY_OK},
	{ 0x13, KEY_RIGHT},
	{ 0x14, KEY_DOWN},
	{ 0x0f, KEY_EPG},
	{ 0x16, KEY_INFO},
	{ 0x4d, KEY_BACKSPACE},
	{ 0x1c, KEY_VOLUMEUP},
	{ 0x4c, KEY_PLAY},
	{ 0x1b, KEY_CHANNELUP},
	{ 0x1e, KEY_VOLUMEDOWN},
	{ 0x1d, KEY_MUTE},
	{ 0x1f, KEY_CHANNELDOWN},
	{ 0x17, KEY_RED},
	{ 0x18, KEY_GREEN},
	{ 0x19, KEY_YELLOW},
	{ 0x1a, KEY_BLUE},
	{ 0x58, KEY_RECORD},
	{ 0x48, KEY_STOP},
	{ 0x40, KEY_PAUSE},
	{ 0x54, KEY_LAST},
	{ 0x4e, KEY_REWIND},
	{ 0x4f, KEY_FASTFORWARD},
	{ 0x5c, KEY_NEXT},
};

static struct rc_map_list terratec_cinergy_xs_map = {
	.map = {
		.scan    = terratec_cinergy_xs,
		.size    = ARRAY_SIZE(terratec_cinergy_xs),
		.rc_type = RC_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_TERRATEC_CINERGY_XS,
	}
};

static int __init init_rc_map_terratec_cinergy_xs(void)
{
	return rc_map_register(&terratec_cinergy_xs_map);
}

static void __exit exit_rc_map_terratec_cinergy_xs(void)
{
	rc_map_unregister(&terratec_cinergy_xs_map);
}

module_init(init_rc_map_terratec_cinergy_xs)
module_exit(exit_rc_map_terratec_cinergy_xs)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
