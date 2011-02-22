/* pixelview-new.h - Keytable for pixelview_new Remote Controller
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

/*
   Mauro Carvalho Chehab <mchehab@infradead.org>
   present on PV MPEG 8000GT
 */

static struct rc_map_table pixelview_new[] = {
	{ 0x3c, KEY_TIME },		/* Timeshift */
	{ 0x12, KEY_POWER },

	{ 0x3d, KEY_1 },
	{ 0x38, KEY_2 },
	{ 0x18, KEY_3 },
	{ 0x35, KEY_4 },
	{ 0x39, KEY_5 },
	{ 0x15, KEY_6 },
	{ 0x36, KEY_7 },
	{ 0x3a, KEY_8 },
	{ 0x1e, KEY_9 },
	{ 0x3e, KEY_0 },

	{ 0x1c, KEY_AGAIN },		/* LOOP	*/
	{ 0x3f, KEY_MEDIA },		/* Source */
	{ 0x1f, KEY_LAST },		/* +100 */
	{ 0x1b, KEY_MUTE },

	{ 0x17, KEY_CHANNELDOWN },
	{ 0x16, KEY_CHANNELUP },
	{ 0x10, KEY_VOLUMEUP },
	{ 0x14, KEY_VOLUMEDOWN },
	{ 0x13, KEY_ZOOM },

	{ 0x19, KEY_CAMERA },		/* SNAPSHOT */
	{ 0x1a, KEY_SEARCH },		/* scan */

	{ 0x37, KEY_REWIND },		/* << */
	{ 0x32, KEY_RECORD },		/* o (red) */
	{ 0x33, KEY_FORWARD },		/* >> */
	{ 0x11, KEY_STOP },		/* square */
	{ 0x3b, KEY_PLAY },		/* > */
	{ 0x30, KEY_PLAYPAUSE },	/* || */

	{ 0x31, KEY_TV },
	{ 0x34, KEY_RADIO },
};

static struct rc_map_list pixelview_new_map = {
	.map = {
		.scan    = pixelview_new,
		.size    = ARRAY_SIZE(pixelview_new),
		.rc_type = RC_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_PIXELVIEW_NEW,
	}
};

static int __init init_rc_map_pixelview_new(void)
{
	return rc_map_register(&pixelview_new_map);
}

static void __exit exit_rc_map_pixelview_new(void)
{
	rc_map_unregister(&pixelview_new_map);
}

module_init(init_rc_map_pixelview_new)
module_exit(exit_rc_map_pixelview_new)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
