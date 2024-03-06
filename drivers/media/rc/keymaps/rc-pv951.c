// SPDX-License-Identifier: GPL-2.0+
// pv951.h - Keytable for pv951 Remote Controller
//
// keymap imported from ir-keymaps.c
//
// Copyright (c) 2010 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

/* Mark Phalan <phalanm@o2.ie> */

static struct rc_map_table pv951[] = {
	{ 0x00, KEY_NUMERIC_0 },
	{ 0x01, KEY_NUMERIC_1 },
	{ 0x02, KEY_NUMERIC_2 },
	{ 0x03, KEY_NUMERIC_3 },
	{ 0x04, KEY_NUMERIC_4 },
	{ 0x05, KEY_NUMERIC_5 },
	{ 0x06, KEY_NUMERIC_6 },
	{ 0x07, KEY_NUMERIC_7 },
	{ 0x08, KEY_NUMERIC_8 },
	{ 0x09, KEY_NUMERIC_9 },

	{ 0x12, KEY_POWER },
	{ 0x10, KEY_MUTE },
	{ 0x1f, KEY_VOLUMEDOWN },
	{ 0x1b, KEY_VOLUMEUP },
	{ 0x1a, KEY_CHANNELUP },
	{ 0x1e, KEY_CHANNELDOWN },
	{ 0x0e, KEY_PAGEUP },
	{ 0x1d, KEY_PAGEDOWN },
	{ 0x13, KEY_SOUND },

	{ 0x18, KEY_KPPLUSMINUS },	/* CH +/- */
	{ 0x16, KEY_SUBTITLE },		/* CC */
	{ 0x0d, KEY_TEXT },		/* TTX */
	{ 0x0b, KEY_TV },		/* AIR/CBL */
	{ 0x11, KEY_PC },		/* PC/TV */
	{ 0x17, KEY_OK },		/* CH RTN */
	{ 0x19, KEY_MODE },		/* FUNC */
	{ 0x0c, KEY_SEARCH },		/* AUTOSCAN */

	/* Not sure what to do with these ones! */
	{ 0x0f, KEY_VIDEO },		/* SOURCE */
	{ 0x0a, KEY_KPPLUS },		/* +100 */
	{ 0x14, KEY_EQUAL },		/* SYNC */
	{ 0x1c, KEY_TV },		/* PC/TV */
};

static struct rc_map_list pv951_map = {
	.map = {
		.scan     = pv951,
		.size     = ARRAY_SIZE(pv951),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_PV951,
	}
};

static int __init init_rc_map_pv951(void)
{
	return rc_map_register(&pv951_map);
}

static void __exit exit_rc_map_pv951(void)
{
	rc_map_unregister(&pv951_map);
}

module_init(init_rc_map_pv951)
module_exit(exit_rc_map_pv951)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_DESCRIPTION("pv951 remote controller keytable");
