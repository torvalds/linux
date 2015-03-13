/* rc5-imon-mce.c - Keytable for Windows Media Center RC-6 remotes for use
 * with the SoundGraph iMON/Antec Veris hardware IR decoder
 *
 * Copyright (c) 2010 by Jarod Wilson <jarod@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>
#include <linux/module.h>

/* mce-mode imon mce remote key table */
static struct rc_map_table imon_mce[] = {
	/* keys sorted mostly by frequency of use to optimize lookups */
	{ 0x800ff415, KEY_REWIND },
	{ 0x800ff414, KEY_FASTFORWARD },
	{ 0x800ff41b, KEY_PREVIOUS },
	{ 0x800ff41a, KEY_NEXT },

	{ 0x800ff416, KEY_PLAY },
	{ 0x800ff418, KEY_PAUSE },
	{ 0x800ff419, KEY_STOP },
	{ 0x800ff417, KEY_RECORD },

	{ 0x02000052, KEY_UP },
	{ 0x02000051, KEY_DOWN },
	{ 0x02000050, KEY_LEFT },
	{ 0x0200004f, KEY_RIGHT },

	{ 0x800ff41e, KEY_UP },
	{ 0x800ff41f, KEY_DOWN },
	{ 0x800ff420, KEY_LEFT },
	{ 0x800ff421, KEY_RIGHT },

	/* 0x800ff40b also KEY_NUMERIC_POUND on some receivers */
	{ 0x800ff40b, KEY_ENTER },
	{ 0x02000028, KEY_ENTER },
/* the OK and Enter buttons decode to the same value on some remotes
	{ 0x02000028, KEY_OK }, */
	{ 0x800ff422, KEY_OK },
	{ 0x0200002a, KEY_EXIT },
	{ 0x800ff423, KEY_EXIT },
	{ 0x02000029, KEY_DELETE },
	/* 0x800ff40a also KEY_NUMERIC_STAR on some receivers */
	{ 0x800ff40a, KEY_DELETE },

	{ 0x800ff40e, KEY_MUTE },
	{ 0x800ff410, KEY_VOLUMEUP },
	{ 0x800ff411, KEY_VOLUMEDOWN },
	{ 0x800ff412, KEY_CHANNELUP },
	{ 0x800ff413, KEY_CHANNELDOWN },

	{ 0x0200001e, KEY_NUMERIC_1 },
	{ 0x0200001f, KEY_NUMERIC_2 },
	{ 0x02000020, KEY_NUMERIC_3 },
	{ 0x02000021, KEY_NUMERIC_4 },
	{ 0x02000022, KEY_NUMERIC_5 },
	{ 0x02000023, KEY_NUMERIC_6 },
	{ 0x02000024, KEY_NUMERIC_7 },
	{ 0x02000025, KEY_NUMERIC_8 },
	{ 0x02000026, KEY_NUMERIC_9 },
	{ 0x02000027, KEY_NUMERIC_0 },

	{ 0x800ff401, KEY_NUMERIC_1 },
	{ 0x800ff402, KEY_NUMERIC_2 },
	{ 0x800ff403, KEY_NUMERIC_3 },
	{ 0x800ff404, KEY_NUMERIC_4 },
	{ 0x800ff405, KEY_NUMERIC_5 },
	{ 0x800ff406, KEY_NUMERIC_6 },
	{ 0x800ff407, KEY_NUMERIC_7 },
	{ 0x800ff408, KEY_NUMERIC_8 },
	{ 0x800ff409, KEY_NUMERIC_9 },
	{ 0x800ff400, KEY_NUMERIC_0 },

	{ 0x02200025, KEY_NUMERIC_STAR },
	{ 0x02200020, KEY_NUMERIC_POUND },
	/* 0x800ff41d also KEY_BLUE on some receivers */
	{ 0x800ff41d, KEY_NUMERIC_STAR },
	/* 0x800ff41c also KEY_PREVIOUS on some receivers */
	{ 0x800ff41c, KEY_NUMERIC_POUND },

	{ 0x800ff446, KEY_TV },
	{ 0x800ff447, KEY_AUDIO }, /* My Music */
	{ 0x800ff448, KEY_PVR }, /* RecordedTV */
	{ 0x800ff449, KEY_CAMERA },
	{ 0x800ff44a, KEY_VIDEO },
	/* 0x800ff424 also KEY_MENU on some receivers */
	{ 0x800ff424, KEY_DVD },
	/* 0x800ff425 also KEY_GREEN on some receivers */
	{ 0x800ff425, KEY_TUNER }, /* LiveTV */
	{ 0x800ff450, KEY_RADIO },

	{ 0x800ff44c, KEY_LANGUAGE },
	{ 0x800ff427, KEY_ZOOM }, /* Aspect */

	{ 0x800ff45b, KEY_RED },
	{ 0x800ff45c, KEY_GREEN },
	{ 0x800ff45d, KEY_YELLOW },
	{ 0x800ff45e, KEY_BLUE },

	{ 0x800ff466, KEY_RED },
	/* { 0x800ff425, KEY_GREEN }, */
	{ 0x800ff468, KEY_YELLOW },
	/* { 0x800ff41d, KEY_BLUE }, */

	{ 0x800ff40f, KEY_INFO },
	{ 0x800ff426, KEY_EPG }, /* Guide */
	{ 0x800ff45a, KEY_SUBTITLE }, /* Caption/Teletext */
	{ 0x800ff44d, KEY_TITLE },

	{ 0x800ff40c, KEY_POWER },
	{ 0x800ff40d, KEY_MEDIA }, /* Windows MCE button */

};

static struct rc_map_list imon_mce_map = {
	.map = {
		.scan    = imon_mce,
		.size    = ARRAY_SIZE(imon_mce),
		/* its RC6, but w/a hardware decoder */
		.rc_type = RC_TYPE_RC6_MCE,
		.name    = RC_MAP_IMON_MCE,
	}
};

static int __init init_rc_map_imon_mce(void)
{
	return rc_map_register(&imon_mce_map);
}

static void __exit exit_rc_map_imon_mce(void)
{
	rc_map_unregister(&imon_mce_map);
}

module_init(init_rc_map_imon_mce)
module_exit(exit_rc_map_imon_mce)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jarod Wilson <jarod@redhat.com>");
