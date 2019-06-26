// SPDX-License-Identifier: GPL-2.0+
// behold-columbus.h - Keytable for behold_columbus Remote Controller
//
// keymap imported from ir-keymaps.c
//
// Copyright (c) 2010 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

/* Beholder Intl. Ltd. 2008
 * Dmitry Belimov d.belimov@google.com
 * Keytable is used by BeholdTV Columbus
 * The "ascii-art picture" below (in comments, first row
 * is the keycode in hex, and subsequent row(s) shows
 * the button labels (several variants when appropriate)
 * helps to decide which keycodes to assign to the buttons.
 */

static struct rc_map_table behold_columbus[] = {

	/*  0x13   0x11   0x1C   0x12  *
	 *  Mute  Source  TV/FM  Power *
	 *                             */

	{ 0x13, KEY_MUTE },
	{ 0x11, KEY_VIDEO },
	{ 0x1C, KEY_TUNER },	/* KEY_TV/KEY_RADIO	*/
	{ 0x12, KEY_POWER },

	/*  0x01    0x02    0x03  0x0D    *
	 *   1       2       3   Stereo   *
	 *				  *
	 *  0x04    0x05    0x06  0x19    *
	 *   4       5       6   Snapshot *
	 *				  *
	 *  0x07    0x08    0x09  0x10    *
	 *   7       8       9    Zoom	  *
	 *                                */
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x0D, KEY_SETUP },	  /* Setup key */
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x19, KEY_CAMERA },	/* Snapshot key */
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },
	{ 0x10, KEY_ZOOM },

	/*  0x0A    0x00    0x0B       0x0C   *
	 * RECALL    0    ChannelUp  VolumeUp *
	 *                                    */
	{ 0x0A, KEY_AGAIN },
	{ 0x00, KEY_0 },
	{ 0x0B, KEY_CHANNELUP },
	{ 0x0C, KEY_VOLUMEUP },

	/*   0x1B      0x1D      0x15        0x18     *
	 * Timeshift  Record  ChannelDown  VolumeDown *
	 *                                            */

	{ 0x1B, KEY_TIME },
	{ 0x1D, KEY_RECORD },
	{ 0x15, KEY_CHANNELDOWN },
	{ 0x18, KEY_VOLUMEDOWN },

	/*   0x0E   0x1E     0x0F     0x1A  *
	 *   Stop   Pause  Previous   Next  *
	 *                                  */

	{ 0x0E, KEY_STOP },
	{ 0x1E, KEY_PAUSE },
	{ 0x0F, KEY_PREVIOUS },
	{ 0x1A, KEY_NEXT },

};

static struct rc_map_list behold_columbus_map = {
	.map = {
		.scan     = behold_columbus,
		.size     = ARRAY_SIZE(behold_columbus),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_BEHOLD_COLUMBUS,
	}
};

static int __init init_rc_map_behold_columbus(void)
{
	return rc_map_register(&behold_columbus_map);
}

static void __exit exit_rc_map_behold_columbus(void)
{
	rc_map_unregister(&behold_columbus_map);
}

module_init(init_rc_map_behold_columbus)
module_exit(exit_rc_map_behold_columbus)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
