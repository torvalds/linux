/* rc-delock-61959.c - Keytable for Delock
 *
 * Copyright (c) 2013 by Jakob Haufe <sur5r@sur5r.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>
#include <linux/module.h>

/*
 * Keytable for remote provided with Delock 61959
 */
static struct rc_map_table delock_61959[] = {
	{ 0x866b16, KEY_POWER2 },	/* Power */
	{ 0x866b0c, KEY_POWER },	/* Shut Down */

	{ 0x866b00, KEY_1},
	{ 0x866b01, KEY_2},
	{ 0x866b02, KEY_3},
	{ 0x866b03, KEY_4},
	{ 0x866b04, KEY_5},
	{ 0x866b05, KEY_6},
	{ 0x866b06, KEY_7},
	{ 0x866b07, KEY_8},
	{ 0x866b08, KEY_9},
	{ 0x866b14, KEY_0},

	{ 0x866b0a, KEY_ZOOM},		/* Full Screen */
	{ 0x866b10, KEY_CAMERA},	/* Photo */
	{ 0x866b0e, KEY_CHANNEL},	/* circular arrow / Recall */
	{ 0x866b13, KEY_ESC},           /* Back */

	{ 0x866b20, KEY_UP},
	{ 0x866b21, KEY_DOWN},
	{ 0x866b42, KEY_LEFT},
	{ 0x866b43, KEY_RIGHT},
	{ 0x866b0b, KEY_OK},

	{ 0x866b11, KEY_CHANNELUP},
	{ 0x866b1b, KEY_CHANNELDOWN},

	{ 0x866b12, KEY_VOLUMEUP},
	{ 0x866b48, KEY_VOLUMEDOWN},
	{ 0x866b44, KEY_MUTE},

	{ 0x866b1a, KEY_RECORD},
	{ 0x866b41, KEY_PLAY},
	{ 0x866b40, KEY_STOP},
	{ 0x866b19, KEY_PAUSE},
	{ 0x866b1c, KEY_FASTFORWARD},	/* >> / FWD */
	{ 0x866b1e, KEY_REWIND},	/* << / REW */

};

static struct rc_map_list delock_61959_map = {
	.map = {
		.scan     = delock_61959,
		.size     = ARRAY_SIZE(delock_61959),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_DELOCK_61959,
	}
};

static int __init init_rc_map_delock_61959(void)
{
	return rc_map_register(&delock_61959_map);
}

static void __exit exit_rc_map_delock_61959(void)
{
	rc_map_unregister(&delock_61959_map);
}

module_init(init_rc_map_delock_61959)
module_exit(exit_rc_map_delock_61959)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jakob Haufe <sur5r@sur5r.net>");
MODULE_DESCRIPTION("Delock 61959 remote keytable");
