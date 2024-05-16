// SPDX-License-Identifier: GPL-2.0-only
/*
 * keymap imported from cxusb.c
 *
 * Copyright (C) 2016 Sean Young
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table rc_map_d680_dmb_table[] = {
	{ 0x0038, KEY_SWITCHVIDEOMODE },	/* TV/AV */
	{ 0x080c, KEY_ZOOM },
	{ 0x0800, KEY_NUMERIC_0 },
	{ 0x0001, KEY_NUMERIC_1 },
	{ 0x0802, KEY_NUMERIC_2 },
	{ 0x0003, KEY_NUMERIC_3 },
	{ 0x0804, KEY_NUMERIC_4 },
	{ 0x0005, KEY_NUMERIC_5 },
	{ 0x0806, KEY_NUMERIC_6 },
	{ 0x0007, KEY_NUMERIC_7 },
	{ 0x0808, KEY_NUMERIC_8 },
	{ 0x0009, KEY_NUMERIC_9 },
	{ 0x000a, KEY_MUTE },
	{ 0x0829, KEY_BACK },
	{ 0x0012, KEY_CHANNELUP },
	{ 0x0813, KEY_CHANNELDOWN },
	{ 0x002b, KEY_VOLUMEUP },
	{ 0x082c, KEY_VOLUMEDOWN },
	{ 0x0020, KEY_UP },
	{ 0x0821, KEY_DOWN },
	{ 0x0011, KEY_LEFT },
	{ 0x0810, KEY_RIGHT },
	{ 0x000d, KEY_OK },
	{ 0x081f, KEY_RECORD },
	{ 0x0017, KEY_PLAYPAUSE },
	{ 0x0816, KEY_PLAYPAUSE },
	{ 0x000b, KEY_STOP },
	{ 0x0827, KEY_FASTFORWARD },
	{ 0x0026, KEY_REWIND },
	{ 0x081e, KEY_UNKNOWN },    /* Time Shift */
	{ 0x000e, KEY_UNKNOWN },    /* Snapshot */
	{ 0x082d, KEY_UNKNOWN },    /* Mouse Cursor */
	{ 0x000f, KEY_UNKNOWN },    /* Minimize/Maximize */
	{ 0x0814, KEY_SHUFFLE },    /* Shuffle */
	{ 0x0025, KEY_POWER },
};

static struct rc_map_list d680_dmb_map = {
	.map = {
		.scan     = rc_map_d680_dmb_table,
		.size     = ARRAY_SIZE(rc_map_d680_dmb_table),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_D680_DMB,
	}
};

static int __init init_rc_map_d680_dmb(void)
{
	return rc_map_register(&d680_dmb_map);
}

static void __exit exit_rc_map_d680_dmb(void)
{
	rc_map_unregister(&d680_dmb_map);
}

module_init(init_rc_map_d680_dmb)
module_exit(exit_rc_map_d680_dmb)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_DESCRIPTION("d680-dmb remote controller keytable");
