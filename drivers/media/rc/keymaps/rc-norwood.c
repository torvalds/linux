/* norwood.h - Keytable for norwood Remote Controller
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

/* Norwood Micro (non-Pro) TV Tuner
   By Peter Naulls <peter@chocky.org>
   Key comments are the functions given in the manual */

static struct rc_map_table norwood[] = {
	/* Keys 0 to 9 */
	{ 0x20, KEY_0 },
	{ 0x21, KEY_1 },
	{ 0x22, KEY_2 },
	{ 0x23, KEY_3 },
	{ 0x24, KEY_4 },
	{ 0x25, KEY_5 },
	{ 0x26, KEY_6 },
	{ 0x27, KEY_7 },
	{ 0x28, KEY_8 },
	{ 0x29, KEY_9 },

	{ 0x78, KEY_VIDEO },		/* Video Source        */
	{ 0x2c, KEY_EXIT },		/* Open/Close software */
	{ 0x2a, KEY_SELECT },		/* 2 Digit Select      */
	{ 0x69, KEY_AGAIN },		/* Recall              */

	{ 0x32, KEY_BRIGHTNESSUP },	/* Brightness increase */
	{ 0x33, KEY_BRIGHTNESSDOWN },	/* Brightness decrease */
	{ 0x6b, KEY_KPPLUS },		/* (not named >>>>>)   */
	{ 0x6c, KEY_KPMINUS },		/* (not named <<<<<)   */

	{ 0x2d, KEY_MUTE },		/* Mute                */
	{ 0x30, KEY_VOLUMEUP },		/* Volume up           */
	{ 0x31, KEY_VOLUMEDOWN },	/* Volume down         */
	{ 0x60, KEY_CHANNELUP },	/* Channel up          */
	{ 0x61, KEY_CHANNELDOWN },	/* Channel down        */

	{ 0x3f, KEY_RECORD },		/* Record              */
	{ 0x37, KEY_PLAY },		/* Play                */
	{ 0x36, KEY_PAUSE },		/* Pause               */
	{ 0x2b, KEY_STOP },		/* Stop                */
	{ 0x67, KEY_FASTFORWARD },	/* Forward              */
	{ 0x66, KEY_REWIND },		/* Rewind              */
	{ 0x3e, KEY_SEARCH },		/* Auto Scan           */
	{ 0x2e, KEY_CAMERA },		/* Capture Video       */
	{ 0x6d, KEY_MENU },		/* Show/Hide Control   */
	{ 0x2f, KEY_ZOOM },		/* Full Screen         */
	{ 0x34, KEY_RADIO },		/* FM                  */
	{ 0x65, KEY_POWER },		/* Computer power      */
};

static struct rc_map_list norwood_map = {
	.map = {
		.scan     = norwood,
		.size     = ARRAY_SIZE(norwood),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_NORWOOD,
	}
};

static int __init init_rc_map_norwood(void)
{
	return rc_map_register(&norwood_map);
}

static void __exit exit_rc_map_norwood(void)
{
	rc_map_unregister(&norwood_map);
}

module_init(init_rc_map_norwood)
module_exit(exit_rc_map_norwood)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
