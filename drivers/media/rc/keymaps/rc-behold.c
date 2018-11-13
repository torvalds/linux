// SPDX-License-Identifier: GPL-2.0+
// behold.h - Keytable for behold Remote Controller
//
// keymap imported from ir-keymaps.c
//
// Copyright (c) 2010 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

/*
 * Igor Kuznetsov <igk72@ya.ru>
 * Andrey J. Melnikov <temnota@kmv.ru>
 *
 * Keytable is used by BeholdTV 60x series, M6 series at
 * least, and probably other cards too.
 * The "ascii-art picture" below (in comments, first row
 * is the keycode in hex, and subsequent row(s) shows
 * the button labels (several variants when appropriate)
 * helps to descide which keycodes to assign to the buttons.
 */

static struct rc_map_table behold[] = {

	/*  0x1c            0x12  *
	 *  TV/FM          POWER  *
	 *                        */
	{ 0x866b1c, KEY_TUNER },	/* XXX KEY_TV / KEY_RADIO */
	{ 0x866b12, KEY_POWER },

	/*  0x01    0x02    0x03  *
	 *   1       2       3    *
	 *                        *
	 *  0x04    0x05    0x06  *
	 *   4       5       6    *
	 *                        *
	 *  0x07    0x08    0x09  *
	 *   7       8       9    *
	 *                        */
	{ 0x866b01, KEY_1 },
	{ 0x866b02, KEY_2 },
	{ 0x866b03, KEY_3 },
	{ 0x866b04, KEY_4 },
	{ 0x866b05, KEY_5 },
	{ 0x866b06, KEY_6 },
	{ 0x866b07, KEY_7 },
	{ 0x866b08, KEY_8 },
	{ 0x866b09, KEY_9 },

	/*  0x0a    0x00    0x17  *
	 * RECALL    0      MODE  *
	 *                        */
	{ 0x866b0a, KEY_AGAIN },
	{ 0x866b00, KEY_0 },
	{ 0x866b17, KEY_MODE },

	/*  0x14          0x10    *
	 * ASPECT      FULLSCREEN *
	 *                        */
	{ 0x866b14, KEY_SCREEN },
	{ 0x866b10, KEY_ZOOM },

	/*          0x0b          *
	 *           Up           *
	 *                        *
	 *  0x18    0x16    0x0c  *
	 *  Left     Ok     Right *
	 *                        *
	 *         0x015          *
	 *         Down           *
	 *                        */
	{ 0x866b0b, KEY_CHANNELUP },
	{ 0x866b18, KEY_VOLUMEDOWN },
	{ 0x866b16, KEY_OK },		/* XXX KEY_ENTER */
	{ 0x866b0c, KEY_VOLUMEUP },
	{ 0x866b15, KEY_CHANNELDOWN },

	/*  0x11            0x0d  *
	 *  MUTE            INFO  *
	 *                        */
	{ 0x866b11, KEY_MUTE },
	{ 0x866b0d, KEY_INFO },

	/*  0x0f    0x1b    0x1a  *
	 * RECORD PLAY/PAUSE STOP *
	 *                        *
	 *  0x0e    0x1f    0x1e  *
	 *TELETEXT  AUDIO  SOURCE *
	 *           RED   YELLOW *
	 *                        */
	{ 0x866b0f, KEY_RECORD },
	{ 0x866b1b, KEY_PLAYPAUSE },
	{ 0x866b1a, KEY_STOP },
	{ 0x866b0e, KEY_TEXT },
	{ 0x866b1f, KEY_RED },	/*XXX KEY_AUDIO	*/
	{ 0x866b1e, KEY_VIDEO },

	/*  0x1d   0x13     0x19  *
	 * SLEEP  PREVIEW   DVB   *
	 *         GREEN    BLUE  *
	 *                        */
	{ 0x866b1d, KEY_SLEEP },
	{ 0x866b13, KEY_GREEN },
	{ 0x866b19, KEY_BLUE },	/* XXX KEY_SAT	*/

	/*  0x58           0x5c   *
	 * FREEZE        SNAPSHOT *
	 *                        */
	{ 0x866b58, KEY_SLOW },
	{ 0x866b5c, KEY_CAMERA },

};

static struct rc_map_list behold_map = {
	.map = {
		.scan     = behold,
		.size     = ARRAY_SIZE(behold),
		.rc_proto = RC_PROTO_NECX,
		.name     = RC_MAP_BEHOLD,
	}
};

static int __init init_rc_map_behold(void)
{
	return rc_map_register(&behold_map);
}

static void __exit exit_rc_map_behold(void)
{
	rc_map_unregister(&behold_map);
}

module_init(init_rc_map_behold)
module_exit(exit_rc_map_behold)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
