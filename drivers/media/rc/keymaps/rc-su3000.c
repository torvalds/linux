// SPDX-License-Identifier: GPL-2.0-or-later
/* rc-su3000.h - Keytable for Geniatech HDStar Remote Controller
 *
 * Copyright (c) 2013 by Evgeny Plehov <Evgeny Plehov@ukr.net>
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table su3000[] = {
	{ 0x25, KEY_POWER },	/* right-bottom Red */
	{ 0x0a, KEY_MUTE },	/* -/-- */
	{ 0x01, KEY_NUMERIC_1 },
	{ 0x02, KEY_NUMERIC_2 },
	{ 0x03, KEY_NUMERIC_3 },
	{ 0x04, KEY_NUMERIC_4 },
	{ 0x05, KEY_NUMERIC_5 },
	{ 0x06, KEY_NUMERIC_6 },
	{ 0x07, KEY_NUMERIC_7 },
	{ 0x08, KEY_NUMERIC_8 },
	{ 0x09, KEY_NUMERIC_9 },
	{ 0x00, KEY_NUMERIC_0 },
	{ 0x20, KEY_UP },	/* CH+ */
	{ 0x21, KEY_DOWN },	/* CH+ */
	{ 0x12, KEY_VOLUMEUP },	/* Brightness Up */
	{ 0x13, KEY_VOLUMEDOWN },/* Brightness Down */
	{ 0x1f, KEY_RECORD },
	{ 0x17, KEY_PLAY },
	{ 0x16, KEY_PAUSE },
	{ 0x0b, KEY_STOP },
	{ 0x27, KEY_FASTFORWARD },/* >> */
	{ 0x26, KEY_REWIND },	/* << */
	{ 0x0d, KEY_OK },	/* Mute */
	{ 0x11, KEY_LEFT },	/* VOL- */
	{ 0x10, KEY_RIGHT },	/* VOL+ */
	{ 0x29, KEY_BACK },	/* button under 9 */
	{ 0x2c, KEY_MENU },	/* TTX */
	{ 0x2b, KEY_EPG },	/* EPG */
	{ 0x1e, KEY_RED },	/* OSD */
	{ 0x0e, KEY_GREEN },	/* Window */
	{ 0x2d, KEY_YELLOW },	/* button under << */
	{ 0x0f, KEY_BLUE },	/* bottom yellow button */
	{ 0x14, KEY_AUDIO },	/* Snapshot */
	{ 0x38, KEY_TV },	/* TV/Radio */
	{ 0x0c, KEY_ESC }	/* upper Red button */
};

static struct rc_map_list su3000_map = {
	.map = {
		.scan     = su3000,
		.size     = ARRAY_SIZE(su3000),
		.rc_proto = RC_PROTO_RC5,
		.name     = RC_MAP_SU3000,
	}
};

static int __init init_rc_map_su3000(void)
{
	return rc_map_register(&su3000_map);
}

static void __exit exit_rc_map_su3000(void)
{
	rc_map_unregister(&su3000_map);
}

module_init(init_rc_map_su3000)
module_exit(exit_rc_map_su3000)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Evgeny Plehov <Evgeny Plehov@ukr.net>");
MODULE_DESCRIPTION("Geniatech HDStar remote controller keytable");
