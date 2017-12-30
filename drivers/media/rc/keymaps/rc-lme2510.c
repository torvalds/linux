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
#include <linux/module.h>


static struct rc_map_table lme2510_rc[] = {
	/* Type 1 - 26 buttons */
	{ 0xef12ba45, KEY_0 },
	{ 0xef12a05f, KEY_1 },
	{ 0xef12af50, KEY_2 },
	{ 0xef12a25d, KEY_3 },
	{ 0xef12be41, KEY_4 },
	{ 0xef12f50a, KEY_5 },
	{ 0xef12bd42, KEY_6 },
	{ 0xef12b847, KEY_7 },
	{ 0xef12b649, KEY_8 },
	{ 0xef12fa05, KEY_9 },
	{ 0xef12bc43, KEY_POWER },
	{ 0xef12b946, KEY_SUBTITLE },
	{ 0xef12f906, KEY_PAUSE },
	{ 0xef12fc03, KEY_MEDIA_REPEAT},
	{ 0xef12fd02, KEY_PAUSE },
	{ 0xef12a15e, KEY_VOLUMEUP },
	{ 0xef12a35c, KEY_VOLUMEDOWN },
	{ 0xef12f609, KEY_CHANNELUP },
	{ 0xef12e51a, KEY_CHANNELDOWN },
	{ 0xef12e11e, KEY_PLAY },
	{ 0xef12e41b, KEY_ZOOM },
	{ 0xef12a659, KEY_MUTE },
	{ 0xef12a55a, KEY_TV },
	{ 0xef12e718, KEY_RECORD },
	{ 0xef12f807, KEY_EPG },
	{ 0xef12fe01, KEY_STOP },
	/* Type 2 - 20 buttons */
	{ 0xff40ea15, KEY_0 },
	{ 0xff40f708, KEY_1 },
	{ 0xff40f609, KEY_2 },
	{ 0xff40f50a, KEY_3 },
	{ 0xff40f30c, KEY_4 },
	{ 0xff40f20d, KEY_5 },
	{ 0xff40f10e, KEY_6 },
	{ 0xff40ef10, KEY_7 },
	{ 0xff40ee11, KEY_8 },
	{ 0xff40ed12, KEY_9 },
	{ 0xff40ff00, KEY_POWER },
	{ 0xff40fb04, KEY_MEDIA_REPEAT}, /* Recall */
	{ 0xff40e51a, KEY_PAUSE }, /* Timeshift */
	{ 0xff40fd02, KEY_VOLUMEUP }, /* 2 x -/+ Keys not marked */
	{ 0xff40f906, KEY_VOLUMEDOWN }, /* Volume defined as right hand*/
	{ 0xff40fe01, KEY_CHANNELUP },
	{ 0xff40fa05, KEY_CHANNELDOWN },
	{ 0xff40eb14, KEY_ZOOM },
	{ 0xff40e718, KEY_RECORD },
	{ 0xff40e916, KEY_STOP },
	/* Type 3 - 20 buttons */
	{ 0xff00e31c, KEY_0 },
	{ 0xff00f807, KEY_1 },
	{ 0xff00ea15, KEY_2 },
	{ 0xff00f609, KEY_3 },
	{ 0xff00e916, KEY_4 },
	{ 0xff00e619, KEY_5 },
	{ 0xff00f20d, KEY_6 },
	{ 0xff00f30c, KEY_7 },
	{ 0xff00e718, KEY_8 },
	{ 0xff00a15e, KEY_9 },
	{ 0xff00ba45, KEY_POWER },
	{ 0xff00bb44, KEY_MEDIA_REPEAT}, /* Recall */
	{ 0xff00b54a, KEY_PAUSE }, /* Timeshift */
	{ 0xff00b847, KEY_VOLUMEUP }, /* 2 x -/+ Keys not marked */
	{ 0xff00bc43, KEY_VOLUMEDOWN }, /* Volume defined as right hand*/
	{ 0xff00b946, KEY_CHANNELUP },
	{ 0xff00bf40, KEY_CHANNELDOWN },
	{ 0xff00f708, KEY_ZOOM },
	{ 0xff00bd42, KEY_RECORD },
	{ 0xff00a55a, KEY_STOP },
};

static struct rc_map_list lme2510_map = {
	.map = {
		.scan     = lme2510_rc,
		.size     = ARRAY_SIZE(lme2510_rc),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_LME2510,
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
