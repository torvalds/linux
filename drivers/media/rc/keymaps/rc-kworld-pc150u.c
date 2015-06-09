/* kworld-pc150u.c - Keytable for kworld_pc150u Remote Controller
 *
 * keymap imported from ir-keymaps.c
 *
 * Copyright (c) 2010 by Kyle Strickland
 *   (based on kworld-plus-tv-analog.c by
 *    Mauro Carvalho Chehab)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>
#include <linux/module.h>

/* Kworld PC150-U
   Kyle Strickland <kyle@kyle.strickland.name>
 */

static struct rc_map_table kworld_pc150u[] = {
	{ 0x0c, KEY_MEDIA },		/* Kworld key */
	{ 0x16, KEY_EJECTCLOSECD },	/* -> ) */
	{ 0x1d, KEY_POWER2 },

	{ 0x00, KEY_1 },
	{ 0x01, KEY_2 },
	{ 0x02, KEY_3 },
	{ 0x03, KEY_4 },
	{ 0x04, KEY_5 },
	{ 0x05, KEY_6 },
	{ 0x06, KEY_7 },
	{ 0x07, KEY_8 },
	{ 0x08, KEY_9 },
	{ 0x0a, KEY_0 },

	{ 0x09, KEY_AGAIN },
	{ 0x14, KEY_MUTE },

	{ 0x1e, KEY_LAST },
	{ 0x17, KEY_ZOOM },
	{ 0x1f, KEY_HOMEPAGE },
	{ 0x0e, KEY_ESC },

	{ 0x20, KEY_UP },
	{ 0x21, KEY_DOWN },
	{ 0x42, KEY_LEFT },
	{ 0x43, KEY_RIGHT },
	{ 0x0b, KEY_ENTER },

	{ 0x10, KEY_CHANNELUP },
	{ 0x11, KEY_CHANNELDOWN },

	{ 0x13, KEY_VOLUMEUP },
	{ 0x12, KEY_VOLUMEDOWN },

	{ 0x19, KEY_TIME},		/* Timeshift */
	{ 0x1a, KEY_STOP},
	{ 0x1b, KEY_RECORD},
	{ 0x4b, KEY_EMAIL},

	{ 0x40, KEY_REWIND},
	{ 0x44, KEY_PLAYPAUSE},
	{ 0x41, KEY_FORWARD},
	{ 0x22, KEY_TEXT},

	{ 0x15, KEY_AUDIO},		/* ((*)) */
	{ 0x0f, KEY_MODE},		/* display ratio */
	{ 0x1c, KEY_SYSRQ},		/* snapshot */
	{ 0x4a, KEY_SLEEP},		/* sleep timer */

	{ 0x48, KEY_SOUND},		/* switch theater mode */
	{ 0x49, KEY_BLUE},		/* A */
	{ 0x18, KEY_RED},		/* B */
	{ 0x23, KEY_GREEN},		/* C */
};

static struct rc_map_list kworld_pc150u_map = {
	.map = {
		.scan    = kworld_pc150u,
		.size    = ARRAY_SIZE(kworld_pc150u),
		.rc_type = RC_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_KWORLD_PC150U,
	}
};

static int __init init_rc_map_kworld_pc150u(void)
{
	return rc_map_register(&kworld_pc150u_map);
}

static void __exit exit_rc_map_kworld_pc150u(void)
{
	rc_map_unregister(&kworld_pc150u_map);
}

module_init(init_rc_map_kworld_pc150u)
module_exit(exit_rc_map_kworld_pc150u)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kyle Strickland <kyle@kyle.strickland.name>");
