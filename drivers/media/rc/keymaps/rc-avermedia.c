/* avermedia.h - Keytable for avermedia Remote Controller
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

/* Alex Hermann <gaaf@gmx.net> */

static struct rc_map_table avermedia[] = {
	{ 0x28, KEY_1 },
	{ 0x18, KEY_2 },
	{ 0x38, KEY_3 },
	{ 0x24, KEY_4 },
	{ 0x14, KEY_5 },
	{ 0x34, KEY_6 },
	{ 0x2c, KEY_7 },
	{ 0x1c, KEY_8 },
	{ 0x3c, KEY_9 },
	{ 0x22, KEY_0 },

	{ 0x20, KEY_TV },		/* TV/FM */
	{ 0x10, KEY_CD },		/* CD */
	{ 0x30, KEY_TEXT },		/* TELETEXT */
	{ 0x00, KEY_POWER },		/* POWER */

	{ 0x08, KEY_VIDEO },		/* VIDEO */
	{ 0x04, KEY_AUDIO },		/* AUDIO */
	{ 0x0c, KEY_ZOOM },		/* FULL SCREEN */

	{ 0x12, KEY_SUBTITLE },		/* DISPLAY */
	{ 0x32, KEY_REWIND },		/* LOOP	*/
	{ 0x02, KEY_PRINT },		/* PREVIEW */

	{ 0x2a, KEY_SEARCH },		/* AUTOSCAN */
	{ 0x1a, KEY_SLEEP },		/* FREEZE */
	{ 0x3a, KEY_CAMERA },		/* SNAPSHOT */
	{ 0x0a, KEY_MUTE },		/* MUTE */

	{ 0x26, KEY_RECORD },		/* RECORD */
	{ 0x16, KEY_PAUSE },		/* PAUSE */
	{ 0x36, KEY_STOP },		/* STOP */
	{ 0x06, KEY_PLAY },		/* PLAY */

	{ 0x2e, KEY_RED },		/* RED */
	{ 0x21, KEY_GREEN },		/* GREEN */
	{ 0x0e, KEY_YELLOW },		/* YELLOW */
	{ 0x01, KEY_BLUE },		/* BLUE */

	{ 0x1e, KEY_VOLUMEDOWN },	/* VOLUME- */
	{ 0x3e, KEY_VOLUMEUP },		/* VOLUME+ */
	{ 0x11, KEY_CHANNELDOWN },	/* CHANNEL/PAGE- */
	{ 0x31, KEY_CHANNELUP }		/* CHANNEL/PAGE+ */
};

static struct rc_map_list avermedia_map = {
	.map = {
		.scan    = avermedia,
		.size    = ARRAY_SIZE(avermedia),
		.rc_type = RC_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_AVERMEDIA,
	}
};

static int __init init_rc_map_avermedia(void)
{
	return rc_map_register(&avermedia_map);
}

static void __exit exit_rc_map_avermedia(void)
{
	rc_map_unregister(&avermedia_map);
}

module_init(init_rc_map_avermedia)
module_exit(exit_rc_map_avermedia)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
