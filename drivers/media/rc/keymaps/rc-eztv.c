// SPDX-License-Identifier: GPL-2.0+
// eztv.h - Keytable for eztv Remote Controller
//
// keymap imported from ir-keymaps.c
//
// Copyright (c) 2010 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

/* Alfons Geser <a.geser@cox.net>
 * updates from Job D. R. Borges <jobdrb@ig.com.br> */

static struct rc_map_table eztv[] = {
	{ 0x12, KEY_POWER },
	{ 0x01, KEY_TV },	/* DVR */
	{ 0x15, KEY_DVD },	/* DVD */
	{ 0x17, KEY_AUDIO },	/* music */
				/* DVR mode / DVD mode / music mode */

	{ 0x1b, KEY_MUTE },	/* mute */
	{ 0x02, KEY_LANGUAGE },	/* MTS/SAP / audio / autoseek */
	{ 0x1e, KEY_SUBTITLE },	/* closed captioning / subtitle / seek */
	{ 0x16, KEY_ZOOM },	/* full screen */
	{ 0x1c, KEY_VIDEO },	/* video source / eject / delall */
	{ 0x1d, KEY_RESTART },	/* playback / angle / del */
	{ 0x2f, KEY_SEARCH },	/* scan / menu / playlist */
	{ 0x30, KEY_CHANNEL },	/* CH surfing / bookmark / memo */

	{ 0x31, KEY_HELP },	/* help */
	{ 0x32, KEY_MODE },	/* num/memo */
	{ 0x33, KEY_ESC },	/* cancel */

	{ 0x0c, KEY_UP },	/* up */
	{ 0x10, KEY_DOWN },	/* down */
	{ 0x08, KEY_LEFT },	/* left */
	{ 0x04, KEY_RIGHT },	/* right */
	{ 0x03, KEY_SELECT },	/* select */

	{ 0x1f, KEY_REWIND },	/* rewind */
	{ 0x20, KEY_PLAYPAUSE },/* play/pause */
	{ 0x29, KEY_FORWARD },	/* forward */
	{ 0x14, KEY_AGAIN },	/* repeat */
	{ 0x2b, KEY_RECORD },	/* recording */
	{ 0x2c, KEY_STOP },	/* stop */
	{ 0x2d, KEY_PLAY },	/* play */
	{ 0x2e, KEY_CAMERA },	/* snapshot / shuffle */

	{ 0x00, KEY_0 },
	{ 0x05, KEY_1 },
	{ 0x06, KEY_2 },
	{ 0x07, KEY_3 },
	{ 0x09, KEY_4 },
	{ 0x0a, KEY_5 },
	{ 0x0b, KEY_6 },
	{ 0x0d, KEY_7 },
	{ 0x0e, KEY_8 },
	{ 0x0f, KEY_9 },

	{ 0x2a, KEY_VOLUMEUP },
	{ 0x11, KEY_VOLUMEDOWN },
	{ 0x18, KEY_CHANNELUP },/* CH.tracking up */
	{ 0x19, KEY_CHANNELDOWN },/* CH.tracking down */

	{ 0x13, KEY_ENTER },	/* enter */
	{ 0x21, KEY_DOT },	/* . (decimal dot) */
};

static struct rc_map_list eztv_map = {
	.map = {
		.scan     = eztv,
		.size     = ARRAY_SIZE(eztv),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_EZTV,
	}
};

static int __init init_rc_map_eztv(void)
{
	return rc_map_register(&eztv_map);
}

static void __exit exit_rc_map_eztv(void)
{
	rc_map_unregister(&eztv_map);
}

module_init(init_rc_map_eztv)
module_exit(exit_rc_map_eztv)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
