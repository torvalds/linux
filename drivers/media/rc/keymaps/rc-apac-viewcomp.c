/* apac-viewcomp.h - Keytable for apac_viewcomp Remote Controller
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

/* Attila Kondoros <attila.kondoros@chello.hu> */

static struct rc_map_table apac_viewcomp[] = {

	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },
	{ 0x00, KEY_0 },
	{ 0x17, KEY_LAST },		/* +100 */
	{ 0x0a, KEY_LIST },		/* recall */


	{ 0x1c, KEY_TUNER },		/* TV/FM */
	{ 0x15, KEY_SEARCH },		/* scan */
	{ 0x12, KEY_POWER },		/* power */
	{ 0x1f, KEY_VOLUMEDOWN },	/* vol up */
	{ 0x1b, KEY_VOLUMEUP },		/* vol down */
	{ 0x1e, KEY_CHANNELDOWN },	/* chn up */
	{ 0x1a, KEY_CHANNELUP },	/* chn down */

	{ 0x11, KEY_VIDEO },		/* video */
	{ 0x0f, KEY_ZOOM },		/* full screen */
	{ 0x13, KEY_MUTE },		/* mute/unmute */
	{ 0x10, KEY_TEXT },		/* min */

	{ 0x0d, KEY_STOP },		/* freeze */
	{ 0x0e, KEY_RECORD },		/* record */
	{ 0x1d, KEY_PLAYPAUSE },	/* stop */
	{ 0x19, KEY_PLAY },		/* play */

	{ 0x16, KEY_GOTO },		/* osd */
	{ 0x14, KEY_REFRESH },		/* default */
	{ 0x0c, KEY_KPPLUS },		/* fine tune >>>> */
	{ 0x18, KEY_KPMINUS },		/* fine tune <<<< */
};

static struct rc_map_list apac_viewcomp_map = {
	.map = {
		.scan    = apac_viewcomp,
		.size    = ARRAY_SIZE(apac_viewcomp),
		.rc_type = RC_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_APAC_VIEWCOMP,
	}
};

static int __init init_rc_map_apac_viewcomp(void)
{
	return rc_map_register(&apac_viewcomp_map);
}

static void __exit exit_rc_map_apac_viewcomp(void)
{
	rc_map_unregister(&apac_viewcomp_map);
}

module_init(init_rc_map_apac_viewcomp)
module_exit(exit_rc_map_apac_viewcomp)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
