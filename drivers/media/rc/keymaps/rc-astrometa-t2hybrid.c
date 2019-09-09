// SPDX-License-Identifier: GPL-2.0-only
/*
 * Keytable for the Astrometa T2hybrid remote controller
 *
 * Copyright (C) 2017 Oleh Kravchenko <oleg@kaa.org.ua>
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table t2hybrid[] = {
	{ 0x4d, KEY_POWER2 },
	{ 0x54, KEY_VIDEO }, /* Source */
	{ 0x16, KEY_MUTE },

	{ 0x4c, KEY_RECORD },
	{ 0x05, KEY_CHANNELUP },
	{ 0x0c, KEY_TIME}, /* Timeshift */

	{ 0x0a, KEY_VOLUMEDOWN },
	{ 0x40, KEY_ZOOM }, /* Fullscreen */
	{ 0x1e, KEY_VOLUMEUP },

	{ 0x12, KEY_NUMERIC_0 },
	{ 0x02, KEY_CHANNELDOWN },
	{ 0x1c, KEY_AGAIN }, /* Recall */

	{ 0x09, KEY_NUMERIC_1 },
	{ 0x1d, KEY_NUMERIC_2 },
	{ 0x1f, KEY_NUMERIC_3 },

	{ 0x0d, KEY_NUMERIC_4 },
	{ 0x19, KEY_NUMERIC_5 },
	{ 0x1b, KEY_NUMERIC_6 },

	{ 0x11, KEY_NUMERIC_7 },
	{ 0x15, KEY_NUMERIC_8 },
	{ 0x17, KEY_NUMERIC_9 },
};

static struct rc_map_list t2hybrid_map = {
	.map = {
		.scan     = t2hybrid,
		.size     = ARRAY_SIZE(t2hybrid),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_ASTROMETA_T2HYBRID,
	}
};

static int __init init_rc_map_t2hybrid(void)
{
	return rc_map_register(&t2hybrid_map);
}

static void __exit exit_rc_map_t2hybrid(void)
{
	rc_map_unregister(&t2hybrid_map);
}

module_init(init_rc_map_t2hybrid)
module_exit(exit_rc_map_t2hybrid)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Oleh Kravchenko <oleg@kaa.org.ua>");
