/* cinergy.h - Keytable for cinergy Remote Controller
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

static struct rc_map_table cinergy[] = {
	{ 0x00, KEY_0 },
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },

	{ 0x0a, KEY_POWER },
	{ 0x0b, KEY_MEDIA },		/* app */
	{ 0x0c, KEY_ZOOM },		/* zoom/fullscreen */
	{ 0x0d, KEY_CHANNELUP },	/* channel */
	{ 0x0e, KEY_CHANNELDOWN },	/* channel- */
	{ 0x0f, KEY_VOLUMEUP },
	{ 0x10, KEY_VOLUMEDOWN },
	{ 0x11, KEY_TUNER },		/* AV */
	{ 0x12, KEY_NUMLOCK },		/* -/-- */
	{ 0x13, KEY_AUDIO },		/* audio */
	{ 0x14, KEY_MUTE },
	{ 0x15, KEY_UP },
	{ 0x16, KEY_DOWN },
	{ 0x17, KEY_LEFT },
	{ 0x18, KEY_RIGHT },
	{ 0x19, BTN_LEFT, },
	{ 0x1a, BTN_RIGHT, },
	{ 0x1b, KEY_WWW },		/* text */
	{ 0x1c, KEY_REWIND },
	{ 0x1d, KEY_FORWARD },
	{ 0x1e, KEY_RECORD },
	{ 0x1f, KEY_PLAY },
	{ 0x20, KEY_PREVIOUSSONG },
	{ 0x21, KEY_NEXTSONG },
	{ 0x22, KEY_PAUSE },
	{ 0x23, KEY_STOP },
};

static struct rc_map_list cinergy_map = {
	.map = {
		.scan     = cinergy,
		.size     = ARRAY_SIZE(cinergy),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_CINERGY,
	}
};

static int __init init_rc_map_cinergy(void)
{
	return rc_map_register(&cinergy_map);
}

static void __exit exit_rc_map_cinergy(void)
{
	rc_map_unregister(&cinergy_map);
}

module_init(init_rc_map_cinergy)
module_exit(exit_rc_map_cinergy)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
