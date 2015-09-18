/* encore-enltv.h - Keytable for encore_enltv Remote Controller
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

/* Encore ENLTV-FM  - black plastic, white front cover with white glowing buttons
    Juan Pablo Sormani <sorman@gmail.com> */

static struct rc_map_table encore_enltv[] = {

	/* Power button does nothing, neither in Windows app,
	 although it sends data (used for BIOS wakeup?) */
	{ 0x0d, KEY_MUTE },

	{ 0x1e, KEY_TV },
	{ 0x00, KEY_VIDEO },
	{ 0x01, KEY_AUDIO },		/* music */
	{ 0x02, KEY_CAMERA },		/* picture */

	{ 0x1f, KEY_1 },
	{ 0x03, KEY_2 },
	{ 0x04, KEY_3 },
	{ 0x05, KEY_4 },
	{ 0x1c, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x1d, KEY_9 },
	{ 0x0a, KEY_0 },

	{ 0x09, KEY_LIST },		/* -/-- */
	{ 0x0b, KEY_LAST },		/* recall */

	{ 0x14, KEY_HOME },		/* win start menu */
	{ 0x15, KEY_EXIT },		/* exit */
	{ 0x16, KEY_CHANNELUP },	/* UP */
	{ 0x12, KEY_CHANNELDOWN },	/* DOWN */
	{ 0x0c, KEY_VOLUMEUP },		/* RIGHT */
	{ 0x17, KEY_VOLUMEDOWN },	/* LEFT */

	{ 0x18, KEY_ENTER },		/* OK */

	{ 0x0e, KEY_ESC },
	{ 0x13, KEY_CYCLEWINDOWS },	/* desktop */
	{ 0x11, KEY_TAB },
	{ 0x19, KEY_SWITCHVIDEOMODE },	/* switch */

	{ 0x1a, KEY_MENU },
	{ 0x1b, KEY_ZOOM },		/* fullscreen */
	{ 0x44, KEY_TIME },		/* time shift */
	{ 0x40, KEY_MODE },		/* source */

	{ 0x5a, KEY_RECORD },
	{ 0x42, KEY_PLAY },		/* play/pause */
	{ 0x45, KEY_STOP },
	{ 0x43, KEY_CAMERA },		/* camera icon */

	{ 0x48, KEY_REWIND },
	{ 0x4a, KEY_FASTFORWARD },
	{ 0x49, KEY_PREVIOUS },
	{ 0x4b, KEY_NEXT },

	{ 0x4c, KEY_FAVORITES },	/* tv wall */
	{ 0x4d, KEY_SOUND },		/* DVD sound */
	{ 0x4e, KEY_LANGUAGE },		/* DVD lang */
	{ 0x4f, KEY_TEXT },		/* DVD text */

	{ 0x50, KEY_SLEEP },		/* shutdown */
	{ 0x51, KEY_MODE },		/* stereo > main */
	{ 0x52, KEY_SELECT },		/* stereo > sap */
	{ 0x53, KEY_TEXT },		/* teletext */


	{ 0x59, KEY_RED },		/* AP1 */
	{ 0x41, KEY_GREEN },		/* AP2 */
	{ 0x47, KEY_YELLOW },		/* AP3 */
	{ 0x57, KEY_BLUE },		/* AP4 */
};

static struct rc_map_list encore_enltv_map = {
	.map = {
		.scan    = encore_enltv,
		.size    = ARRAY_SIZE(encore_enltv),
		.rc_type = RC_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_ENCORE_ENLTV,
	}
};

static int __init init_rc_map_encore_enltv(void)
{
	return rc_map_register(&encore_enltv_map);
}

static void __exit exit_rc_map_encore_enltv(void)
{
	rc_map_unregister(&encore_enltv_map);
}

module_init(init_rc_map_encore_enltv)
module_exit(exit_rc_map_encore_enltv)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
