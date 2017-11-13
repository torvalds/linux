/* videomate-s350.h - Keytable for videomate_s350 Remote Controller
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

static struct rc_map_table videomate_s350[] = {
	{ 0x00, KEY_TV},
	{ 0x01, KEY_DVD},
	{ 0x04, KEY_RECORD},
	{ 0x05, KEY_VIDEO},	/* TV/Video */
	{ 0x07, KEY_STOP},
	{ 0x08, KEY_PLAYPAUSE},
	{ 0x0a, KEY_REWIND},
	{ 0x0f, KEY_FASTFORWARD},
	{ 0x10, KEY_CHANNELUP},
	{ 0x12, KEY_VOLUMEUP},
	{ 0x13, KEY_CHANNELDOWN},
	{ 0x14, KEY_MUTE},
	{ 0x15, KEY_VOLUMEDOWN},
	{ 0x16, KEY_1},
	{ 0x17, KEY_2},
	{ 0x18, KEY_3},
	{ 0x19, KEY_4},
	{ 0x1a, KEY_5},
	{ 0x1b, KEY_6},
	{ 0x1c, KEY_7},
	{ 0x1d, KEY_8},
	{ 0x1e, KEY_9},
	{ 0x1f, KEY_0},
	{ 0x21, KEY_SLEEP},
	{ 0x24, KEY_ZOOM},
	{ 0x25, KEY_LAST},	/* Recall */
	{ 0x26, KEY_SUBTITLE},	/* CC */
	{ 0x27, KEY_LANGUAGE},	/* MTS */
	{ 0x29, KEY_CHANNEL},	/* SURF */
	{ 0x2b, KEY_A},
	{ 0x2c, KEY_B},
	{ 0x2f, KEY_CAMERA},	/* Snapshot */
	{ 0x23, KEY_RADIO},
	{ 0x02, KEY_PREVIOUSSONG},
	{ 0x06, KEY_NEXTSONG},
	{ 0x03, KEY_EPG},
	{ 0x09, KEY_SETUP},
	{ 0x22, KEY_BACKSPACE},
	{ 0x0c, KEY_UP},
	{ 0x0e, KEY_DOWN},
	{ 0x0b, KEY_LEFT},
	{ 0x0d, KEY_RIGHT},
	{ 0x11, KEY_ENTER},
	{ 0x20, KEY_TEXT},
};

static struct rc_map_list videomate_s350_map = {
	.map = {
		.scan     = videomate_s350,
		.size     = ARRAY_SIZE(videomate_s350),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_VIDEOMATE_S350,
	}
};

static int __init init_rc_map_videomate_s350(void)
{
	return rc_map_register(&videomate_s350_map);
}

static void __exit exit_rc_map_videomate_s350(void)
{
	rc_map_unregister(&videomate_s350_map);
}

module_init(init_rc_map_videomate_s350)
module_exit(exit_rc_map_videomate_s350)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
