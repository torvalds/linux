/* kworld-plus-tv-analog.h - Keytable for kworld_plus_tv_analog Remote Controller
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

/* Kworld Plus TV Analog Lite PCI IR
   Mauro Carvalho Chehab <mchehab@infradead.org>
 */

static struct rc_map_table kworld_plus_tv_analog[] = {
	{ 0x0c, KEY_MEDIA },		/* Kworld key */
	{ 0x16, KEY_CLOSECD },		/* -> ) */
	{ 0x1d, KEY_POWER2 },

	{ 0x00, KEY_1 },
	{ 0x01, KEY_2 },
	{ 0x02, KEY_3 },		/* Two keys have the same code: 3 and left */
	{ 0x03, KEY_4 },		/* Two keys have the same code: 3 and right */
	{ 0x04, KEY_5 },
	{ 0x05, KEY_6 },
	{ 0x06, KEY_7 },
	{ 0x07, KEY_8 },
	{ 0x08, KEY_9 },
	{ 0x0a, KEY_0 },

	{ 0x09, KEY_AGAIN },
	{ 0x14, KEY_MUTE },

	{ 0x20, KEY_UP },
	{ 0x21, KEY_DOWN },
	{ 0x0b, KEY_ENTER },

	{ 0x10, KEY_CHANNELUP },
	{ 0x11, KEY_CHANNELDOWN },

	/* Couldn't map key left/key right since those
	   conflict with '3' and '4' scancodes
	   I dunno what the original driver does
	 */

	{ 0x13, KEY_VOLUMEUP },
	{ 0x12, KEY_VOLUMEDOWN },

	/* The lower part of the IR
	   There are several duplicated keycodes there.
	   Most of them conflict with digits.
	   Add mappings just to the unused scancodes.
	   Somehow, the original driver has a way to know,
	   but this doesn't seem to be on some GPIO.
	   Also, it is not related to the time between keyup
	   and keydown.
	 */
	{ 0x19, KEY_TIME},		/* Timeshift */
	{ 0x1a, KEY_STOP},
	{ 0x1b, KEY_RECORD},

	{ 0x22, KEY_TEXT},

	{ 0x15, KEY_AUDIO},		/* ((*)) */
	{ 0x0f, KEY_ZOOM},
	{ 0x1c, KEY_CAMERA},		/* snapshot */

	{ 0x18, KEY_RED},		/* B */
	{ 0x23, KEY_GREEN},		/* C */
};

static struct rc_map_list kworld_plus_tv_analog_map = {
	.map = {
		.scan    = kworld_plus_tv_analog,
		.size    = ARRAY_SIZE(kworld_plus_tv_analog),
		.rc_type = RC_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_KWORLD_PLUS_TV_ANALOG,
	}
};

static int __init init_rc_map_kworld_plus_tv_analog(void)
{
	return rc_map_register(&kworld_plus_tv_analog_map);
}

static void __exit exit_rc_map_kworld_plus_tv_analog(void)
{
	rc_map_unregister(&kworld_plus_tv_analog_map);
}

module_init(init_rc_map_kworld_plus_tv_analog)
module_exit(exit_rc_map_kworld_plus_tv_analog)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
