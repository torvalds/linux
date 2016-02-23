/* rc-rc6-mce.c - Keytable for Windows Media Center RC-6 remotes for use
 * with the Media Center Edition eHome Infrared Transceiver.
 *
 * Copyright (c) 2010 by Jarod Wilson <jarod@redhat.com>
 *
 * See http://mediacenterguides.com/book/export/html/31 for details on
 * key mappings.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table rc6_mce[] = {

	{ 0x800f0400, KEY_NUMERIC_0 },
	{ 0x800f0401, KEY_NUMERIC_1 },
	{ 0x800f0402, KEY_NUMERIC_2 },
	{ 0x800f0403, KEY_NUMERIC_3 },
	{ 0x800f0404, KEY_NUMERIC_4 },
	{ 0x800f0405, KEY_NUMERIC_5 },
	{ 0x800f0406, KEY_NUMERIC_6 },
	{ 0x800f0407, KEY_NUMERIC_7 },
	{ 0x800f0408, KEY_NUMERIC_8 },
	{ 0x800f0409, KEY_NUMERIC_9 },

	{ 0x800f040a, KEY_DELETE },
	{ 0x800f040b, KEY_ENTER },
	{ 0x800f040c, KEY_SLEEP },		/* Formerly PC Power */
	{ 0x800f040d, KEY_MEDIA },		/* Windows MCE button */
	{ 0x800f040e, KEY_MUTE },
	{ 0x800f040f, KEY_INFO },

	{ 0x800f0410, KEY_VOLUMEUP },
	{ 0x800f0411, KEY_VOLUMEDOWN },
	{ 0x800f0412, KEY_CHANNELUP },
	{ 0x800f0413, KEY_CHANNELDOWN },

	{ 0x800f0414, KEY_FASTFORWARD },
	{ 0x800f0415, KEY_REWIND },
	{ 0x800f0416, KEY_PLAY },
	{ 0x800f0417, KEY_RECORD },
	{ 0x800f0418, KEY_PAUSE },
	{ 0x800f0419, KEY_STOP },
	{ 0x800f041a, KEY_NEXT },
	{ 0x800f041b, KEY_PREVIOUS },
	{ 0x800f041c, KEY_NUMERIC_POUND },
	{ 0x800f041d, KEY_NUMERIC_STAR },

	{ 0x800f041e, KEY_UP },
	{ 0x800f041f, KEY_DOWN },
	{ 0x800f0420, KEY_LEFT },
	{ 0x800f0421, KEY_RIGHT },

	{ 0x800f0422, KEY_OK },
	{ 0x800f0423, KEY_EXIT },
	{ 0x800f0424, KEY_DVD },
	{ 0x800f0425, KEY_TUNER },		/* LiveTV */
	{ 0x800f0426, KEY_EPG },		/* Guide */
	{ 0x800f0427, KEY_ZOOM },		/* Aspect */

	{ 0x800f0432, KEY_MODE },		/* Visualization */
	{ 0x800f0433, KEY_PRESENTATION },	/* Slide Show */
	{ 0x800f0434, KEY_EJECTCD },
	{ 0x800f043a, KEY_BRIGHTNESSUP },

	{ 0x800f0446, KEY_TV },
	{ 0x800f0447, KEY_AUDIO },		/* My Music */
	{ 0x800f0448, KEY_PVR },		/* RecordedTV */
	{ 0x800f0449, KEY_CAMERA },
	{ 0x800f044a, KEY_VIDEO },
	{ 0x800f044c, KEY_LANGUAGE },
	{ 0x800f044d, KEY_TITLE },
	{ 0x800f044e, KEY_PRINT },	/* Print - HP OEM version of remote */

	{ 0x800f0450, KEY_RADIO },

	{ 0x800f045a, KEY_SUBTITLE },		/* Caption/Teletext */
	{ 0x800f045b, KEY_RED },
	{ 0x800f045c, KEY_GREEN },
	{ 0x800f045d, KEY_YELLOW },
	{ 0x800f045e, KEY_BLUE },

	{ 0x800f0465, KEY_POWER2 },	/* TV Power */
	{ 0x800f046e, KEY_PLAYPAUSE },
	{ 0x800f046f, KEY_PLAYER },	/* Start media application (NEW) */

	{ 0x800f0480, KEY_BRIGHTNESSDOWN },
	{ 0x800f0481, KEY_PLAYPAUSE },
};

static struct rc_map_list rc6_mce_map = {
	.map = {
		.scan    = rc6_mce,
		.size    = ARRAY_SIZE(rc6_mce),
		.rc_type = RC_TYPE_RC6_MCE,
		.name    = RC_MAP_RC6_MCE,
	}
};

static int __init init_rc_map_rc6_mce(void)
{
	return rc_map_register(&rc6_mce_map);
}

static void __exit exit_rc_map_rc6_mce(void)
{
	rc_map_unregister(&rc6_mce_map);
}

module_init(init_rc_map_rc6_mce)
module_exit(exit_rc_map_rc6_mce)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jarod Wilson <jarod@redhat.com>");
