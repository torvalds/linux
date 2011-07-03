/* behold.h - Keytable for behold Remote Controller
 *
 * keymap imported from ir-keymaps.c
 *
 * Copyright (c) 2010 by Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

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
	{ 0x6b861c, KEY_TUNER },	/* XXX KEY_TV / KEY_RADIO */
	{ 0x6b8612, KEY_POWER },

	/*  0x01    0x02    0x03  *
	 *   1       2       3    *
	 *                        *
	 *  0x04    0x05    0x06  *
	 *   4       5       6    *
	 *                        *
	 *  0x07    0x08    0x09  *
	 *   7       8       9    *
	 *                        */
	{ 0x6b8601, KEY_1 },
	{ 0x6b8602, KEY_2 },
	{ 0x6b8603, KEY_3 },
	{ 0x6b8604, KEY_4 },
	{ 0x6b8605, KEY_5 },
	{ 0x6b8606, KEY_6 },
	{ 0x6b8607, KEY_7 },
	{ 0x6b8608, KEY_8 },
	{ 0x6b8609, KEY_9 },

	/*  0x0a    0x00    0x17  *
	 * RECALL    0      MODE  *
	 *                        */
	{ 0x6b860a, KEY_AGAIN },
	{ 0x6b8600, KEY_0 },
	{ 0x6b8617, KEY_MODE },

	/*  0x14          0x10    *
	 * ASPECT      FULLSCREEN *
	 *                        */
	{ 0x6b8614, KEY_SCREEN },
	{ 0x6b8610, KEY_ZOOM },

	/*          0x0b          *
	 *           Up           *
	 *                        *
	 *  0x18    0x16    0x0c  *
	 *  Left     Ok     Right *
	 *                        *
	 *         0x015          *
	 *         Down           *
	 *                        */
	{ 0x6b860b, KEY_CHANNELUP },
	{ 0x6b8618, KEY_VOLUMEDOWN },
	{ 0x6b8616, KEY_OK },		/* XXX KEY_ENTER */
	{ 0x6b860c, KEY_VOLUMEUP },
	{ 0x6b8615, KEY_CHANNELDOWN },

	/*  0x11            0x0d  *
	 *  MUTE            INFO  *
	 *                        */
	{ 0x6b8611, KEY_MUTE },
	{ 0x6b860d, KEY_INFO },

	/*  0x0f    0x1b    0x1a  *
	 * RECORD PLAY/PAUSE STOP *
	 *                        *
	 *  0x0e    0x1f    0x1e  *
	 *TELETEXT  AUDIO  SOURCE *
	 *           RED   YELLOW *
	 *                        */
	{ 0x6b860f, KEY_RECORD },
	{ 0x6b861b, KEY_PLAYPAUSE },
	{ 0x6b861a, KEY_STOP },
	{ 0x6b860e, KEY_TEXT },
	{ 0x6b861f, KEY_RED },	/*XXX KEY_AUDIO	*/
	{ 0x6b861e, KEY_VIDEO },

	/*  0x1d   0x13     0x19  *
	 * SLEEP  PREVIEW   DVB   *
	 *         GREEN    BLUE  *
	 *                        */
	{ 0x6b861d, KEY_SLEEP },
	{ 0x6b8613, KEY_GREEN },
	{ 0x6b8619, KEY_BLUE },	/* XXX KEY_SAT	*/

	/*  0x58           0x5c   *
	 * FREEZE        SNAPSHOT *
	 *                        */
	{ 0x6b8658, KEY_SLOW },
	{ 0x6b865c, KEY_CAMERA },

};

static struct rc_map_list behold_map = {
	.map = {
		.scan    = behold,
		.size    = ARRAY_SIZE(behold),
		.rc_type = RC_TYPE_NEC,
		.name    = RC_MAP_BEHOLD,
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
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
