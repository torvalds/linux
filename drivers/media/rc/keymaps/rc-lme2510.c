/* LME2510 remote control
 *
 *
 * Copyright (C) 2010 Malcolm Priestley (tvboxspy@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>


static struct rc_map_table lme2510_rc[] = {
	/* Type 1 - 26 buttons */
	{ 0x10ed45, KEY_0 },
	{ 0x10ed5f, KEY_1 },
	{ 0x10ed50, KEY_2 },
	{ 0x10ed5d, KEY_3 },
	{ 0x10ed41, KEY_4 },
	{ 0x10ed0a, KEY_5 },
	{ 0x10ed42, KEY_6 },
	{ 0x10ed47, KEY_7 },
	{ 0x10ed49, KEY_8 },
	{ 0x10ed05, KEY_9 },
	{ 0x10ed43, KEY_POWER },
	{ 0x10ed46, KEY_SUBTITLE },
	{ 0x10ed06, KEY_PAUSE },
	{ 0x10ed03, KEY_MEDIA_REPEAT},
	{ 0x10ed02, KEY_PAUSE },
	{ 0x10ed5e, KEY_VOLUMEUP },
	{ 0x10ed5c, KEY_VOLUMEDOWN },
	{ 0x10ed09, KEY_CHANNELUP },
	{ 0x10ed1a, KEY_CHANNELDOWN },
	{ 0x10ed1e, KEY_PLAY },
	{ 0x10ed1b, KEY_ZOOM },
	{ 0x10ed59, KEY_MUTE },
	{ 0x10ed5a, KEY_TV },
	{ 0x10ed18, KEY_RECORD },
	{ 0x10ed07, KEY_EPG },
	{ 0x10ed01, KEY_STOP },
	/* Type 2 - 20 buttons */
	{ 0xbf15, KEY_0 },
	{ 0xbf08, KEY_1 },
	{ 0xbf09, KEY_2 },
	{ 0xbf0a, KEY_3 },
	{ 0xbf0c, KEY_4 },
	{ 0xbf0d, KEY_5 },
	{ 0xbf0e, KEY_6 },
	{ 0xbf10, KEY_7 },
	{ 0xbf11, KEY_8 },
	{ 0xbf12, KEY_9 },
	{ 0xbf00, KEY_POWER },
	{ 0xbf04, KEY_MEDIA_REPEAT}, /* Recall */
	{ 0xbf1a, KEY_PAUSE }, /* Timeshift */
	{ 0xbf02, KEY_VOLUMEUP }, /* 2 x -/+ Keys not marked */
	{ 0xbf06, KEY_VOLUMEDOWN }, /* Volume defined as right hand*/
	{ 0xbf01, KEY_CHANNELUP },
	{ 0xbf05, KEY_CHANNELDOWN },
	{ 0xbf14, KEY_ZOOM },
	{ 0xbf18, KEY_RECORD },
	{ 0xbf16, KEY_STOP },
	/* Type 3 - 20 buttons */
	{ 0x1c, KEY_0 },
	{ 0x07, KEY_1 },
	{ 0x15, KEY_2 },
	{ 0x09, KEY_3 },
	{ 0x16, KEY_4 },
	{ 0x19, KEY_5 },
	{ 0x0d, KEY_6 },
	{ 0x0c, KEY_7 },
	{ 0x18, KEY_8 },
	{ 0x5e, KEY_9 },
	{ 0x45, KEY_POWER },
	{ 0x44, KEY_MEDIA_REPEAT}, /* Recall */
	{ 0x4a, KEY_PAUSE }, /* Timeshift */
	{ 0x47, KEY_VOLUMEUP }, /* 2 x -/+ Keys not marked */
	{ 0x43, KEY_VOLUMEDOWN }, /* Volume defined as right hand*/
	{ 0x46, KEY_CHANNELUP },
	{ 0x40, KEY_CHANNELDOWN },
	{ 0x08, KEY_ZOOM },
	{ 0x42, KEY_RECORD },
	{ 0x5a, KEY_STOP },
};

static struct rc_map_list lme2510_map = {
	.map = {
		.scan    = lme2510_rc,
		.size    = ARRAY_SIZE(lme2510_rc),
		.rc_type = RC_TYPE_NEC,
		.name    = RC_MAP_LME2510,
	}
};

static int __init init_rc_lme2510_map(void)
{
	return rc_map_register(&lme2510_map);
}

static void __exit exit_rc_lme2510_map(void)
{
	rc_map_unregister(&lme2510_map);
}

module_init(init_rc_lme2510_map)
module_exit(exit_rc_lme2510_map)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Malcolm Priestley tvboxspy@gmail.com");
