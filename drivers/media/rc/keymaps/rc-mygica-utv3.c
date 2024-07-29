// SPDX-License-Identifier: GPL-2.0-or-later
/* rc-mygica-utv3.c - Keytable for the MyGica UTV3 Analog USB2.0 TV Box
 *
 * Copyright (c) 2024 by Nils Rothaug
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table mygica_utv3[] = {
	{ 0x0d, KEY_MUTE },
	{ 0x38, KEY_VIDEO },  /* Source */
	{ 0x14, KEY_RADIO },  /* FM Radio */
	{ 0x0c, KEY_POWER2 },

	{ 0x01, KEY_NUMERIC_1},
	{ 0x02, KEY_NUMERIC_2},
	{ 0x03, KEY_NUMERIC_3},
	{ 0x04, KEY_NUMERIC_4},
	{ 0x05, KEY_NUMERIC_5},
	{ 0x06, KEY_NUMERIC_6},
	{ 0x07, KEY_NUMERIC_7},
	{ 0x08, KEY_NUMERIC_8},
	{ 0x09, KEY_NUMERIC_9},
	{ 0x00, KEY_NUMERIC_0},

	{ 0x0a, KEY_DIGITS }, /* Single/double/triple digit */
	{ 0x0e, KEY_CAMERA }, /* Snapshot */
	{ 0x0f, KEY_ZOOM },   /* Full Screen */
	{ 0x29, KEY_LAST },   /* Recall (return to previous channel) */

	{ 0x17, KEY_PLAY },
	{ 0x1f, KEY_RECORD },
	{ 0x0b, KEY_STOP },
	{ 0x16, KEY_PAUSE },

	{ 0x20, KEY_CHANNELUP },
	{ 0x21, KEY_CHANNELDOWN },
	{ 0x10, KEY_VOLUMEUP },
	{ 0x11, KEY_VOLUMEDOWN },
	{ 0x26, KEY_REWIND },
	{ 0x27, KEY_FASTFORWARD },
};

static struct rc_map_list mygica_utv3_map = {
	.map = {
		.scan     = mygica_utv3,
		.size     = ARRAY_SIZE(mygica_utv3),
		.rc_proto = RC_PROTO_RC5,
		.name     = RC_MAP_MYGICA_UTV3,
	}
};

static int __init init_rc_map_mygica_utv3(void)
{
	return rc_map_register(&mygica_utv3_map);
}

static void __exit exit_rc_map_mygica_utv3(void)
{
	rc_map_unregister(&mygica_utv3_map);
}

module_init(init_rc_map_mygica_utv3)
module_exit(exit_rc_map_mygica_utv3)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nils Rothaug");
MODULE_DESCRIPTION("MyGica UTV3 Analog USB2.0 TV Box remote keytable");
