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
	{ 0xba45, KEY_0 },
	{ 0xa05f, KEY_1 },
	{ 0xaf50, KEY_2 },
	{ 0xa25d, KEY_3 },
	{ 0xbe41, KEY_4 },
	{ 0xf50a, KEY_5 },
	{ 0xbd42, KEY_6 },
	{ 0xb847, KEY_7 },
	{ 0xb649, KEY_8 },
	{ 0xfa05, KEY_9 },
	{ 0xbc43, KEY_POWER },
	{ 0xb946, KEY_SUBTITLE },
	{ 0xf906, KEY_PAUSE },
	{ 0xfc03, KEY_MEDIA_REPEAT},
	{ 0xfd02, KEY_PAUSE },
	{ 0xa15e, KEY_VOLUMEUP },
	{ 0xa35c, KEY_VOLUMEDOWN },
	{ 0xf609, KEY_CHANNELUP },
	{ 0xe51a, KEY_CHANNELDOWN },
	{ 0xe11e, KEY_PLAY },
	{ 0xe41b, KEY_ZOOM },
	{ 0xa659, KEY_MUTE },
	{ 0xa55a, KEY_TV },
	{ 0xe718, KEY_RECORD },
	{ 0xf807, KEY_EPG },
	{ 0xfe01, KEY_STOP },

};

static struct rc_map_list lme2510_map = {
	.map = {
		.scan    = lme2510_rc,
		.size    = ARRAY_SIZE(lme2510_rc),
		.rc_type = RC_TYPE_UNKNOWN,
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
