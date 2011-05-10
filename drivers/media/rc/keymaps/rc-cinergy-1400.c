/* cinergy-1400.h - Keytable for cinergy_1400 Remote Controller
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

/* Cinergy 1400 DVB-T */

static struct rc_map_table cinergy_1400[] = {
	{ 0x01, KEY_POWER },
	{ 0x02, KEY_1 },
	{ 0x03, KEY_2 },
	{ 0x04, KEY_3 },
	{ 0x05, KEY_4 },
	{ 0x06, KEY_5 },
	{ 0x07, KEY_6 },
	{ 0x08, KEY_7 },
	{ 0x09, KEY_8 },
	{ 0x0a, KEY_9 },
	{ 0x0c, KEY_0 },

	{ 0x0b, KEY_VIDEO },
	{ 0x0d, KEY_REFRESH },
	{ 0x0e, KEY_SELECT },
	{ 0x0f, KEY_EPG },
	{ 0x10, KEY_UP },
	{ 0x11, KEY_LEFT },
	{ 0x12, KEY_OK },
	{ 0x13, KEY_RIGHT },
	{ 0x14, KEY_DOWN },
	{ 0x15, KEY_TEXT },
	{ 0x16, KEY_INFO },

	{ 0x17, KEY_RED },
	{ 0x18, KEY_GREEN },
	{ 0x19, KEY_YELLOW },
	{ 0x1a, KEY_BLUE },

	{ 0x1b, KEY_CHANNELUP },
	{ 0x1c, KEY_VOLUMEUP },
	{ 0x1d, KEY_MUTE },
	{ 0x1e, KEY_VOLUMEDOWN },
	{ 0x1f, KEY_CHANNELDOWN },

	{ 0x40, KEY_PAUSE },
	{ 0x4c, KEY_PLAY },
	{ 0x58, KEY_RECORD },
	{ 0x54, KEY_PREVIOUS },
	{ 0x48, KEY_STOP },
	{ 0x5c, KEY_NEXT },
};

static struct rc_map_list cinergy_1400_map = {
	.map = {
		.scan    = cinergy_1400,
		.size    = ARRAY_SIZE(cinergy_1400),
		.rc_type = RC_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_CINERGY_1400,
	}
};

static int __init init_rc_map_cinergy_1400(void)
{
	return rc_map_register(&cinergy_1400_map);
}

static void __exit exit_rc_map_cinergy_1400(void)
{
	rc_map_unregister(&cinergy_1400_map);
}

module_init(init_rc_map_cinergy_1400)
module_exit(exit_rc_map_cinergy_1400)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
