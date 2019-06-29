// SPDX-License-Identifier: GPL-2.0-or-later
/* rc-streamzap.c - Keytable for Streamzap PC Remote, for use
 * with the Streamzap PC Remote IR Receiver.
 *
 * Copyright (c) 2010 by Jarod Wilson <jarod@redhat.com>
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table streamzap[] = {
/*
 * The Streamzap remote is almost, but not quite, RC-5, as it has an extra
 * bit in it.
 */
	{ 0x28c0, KEY_NUMERIC_0 },
	{ 0x28c1, KEY_NUMERIC_1 },
	{ 0x28c2, KEY_NUMERIC_2 },
	{ 0x28c3, KEY_NUMERIC_3 },
	{ 0x28c4, KEY_NUMERIC_4 },
	{ 0x28c5, KEY_NUMERIC_5 },
	{ 0x28c6, KEY_NUMERIC_6 },
	{ 0x28c7, KEY_NUMERIC_7 },
	{ 0x28c8, KEY_NUMERIC_8 },
	{ 0x28c9, KEY_NUMERIC_9 },
	{ 0x28ca, KEY_POWER },
	{ 0x28cb, KEY_MUTE },
	{ 0x28cc, KEY_CHANNELUP },
	{ 0x28cd, KEY_VOLUMEUP },
	{ 0x28ce, KEY_CHANNELDOWN },
	{ 0x28cf, KEY_VOLUMEDOWN },
	{ 0x28d0, KEY_UP },
	{ 0x28d1, KEY_LEFT },
	{ 0x28d2, KEY_OK },
	{ 0x28d3, KEY_RIGHT },
	{ 0x28d4, KEY_DOWN },
	{ 0x28d5, KEY_MENU },
	{ 0x28d6, KEY_EXIT },
	{ 0x28d7, KEY_PLAY },
	{ 0x28d8, KEY_PAUSE },
	{ 0x28d9, KEY_STOP },
	{ 0x28da, KEY_BACK },
	{ 0x28db, KEY_FORWARD },
	{ 0x28dc, KEY_RECORD },
	{ 0x28dd, KEY_REWIND },
	{ 0x28de, KEY_FASTFORWARD },
	{ 0x28e0, KEY_RED },
	{ 0x28e1, KEY_GREEN },
	{ 0x28e2, KEY_YELLOW },
	{ 0x28e3, KEY_BLUE },

};

static struct rc_map_list streamzap_map = {
	.map = {
		.scan     = streamzap,
		.size     = ARRAY_SIZE(streamzap),
		.rc_proto = RC_PROTO_RC5_SZ,
		.name     = RC_MAP_STREAMZAP,
	}
};

static int __init init_rc_map_streamzap(void)
{
	return rc_map_register(&streamzap_map);
}

static void __exit exit_rc_map_streamzap(void)
{
	rc_map_unregister(&streamzap_map);
}

module_init(init_rc_map_streamzap)
module_exit(exit_rc_map_streamzap)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jarod Wilson <jarod@redhat.com>");
