// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * TerraTec remote controller keytable
 *
 * Copyright (C) 2010 Antti Palosaari <crope@iki.fi>
 */

#include <media/rc-map.h>
#include <linux/module.h>

/* TerraTec slim remote, 7 rows, 4 columns. */
/* Uses NEC extended 0x02bd. */
static struct rc_map_table terratec_slim[] = {
	{ 0x02bd00, KEY_1 },
	{ 0x02bd01, KEY_2 },
	{ 0x02bd02, KEY_3 },
	{ 0x02bd03, KEY_4 },
	{ 0x02bd04, KEY_5 },
	{ 0x02bd05, KEY_6 },
	{ 0x02bd06, KEY_7 },
	{ 0x02bd07, KEY_8 },
	{ 0x02bd08, KEY_9 },
	{ 0x02bd09, KEY_0 },
	{ 0x02bd0a, KEY_MUTE },
	{ 0x02bd0b, KEY_NEW },             /* symbol: PIP */
	{ 0x02bd0e, KEY_VOLUMEDOWN },
	{ 0x02bd0f, KEY_PLAYPAUSE },
	{ 0x02bd10, KEY_RIGHT },
	{ 0x02bd11, KEY_LEFT },
	{ 0x02bd12, KEY_UP },
	{ 0x02bd13, KEY_DOWN },
	{ 0x02bd15, KEY_OK },
	{ 0x02bd16, KEY_STOP },
	{ 0x02bd17, KEY_CAMERA },          /* snapshot */
	{ 0x02bd18, KEY_CHANNELUP },
	{ 0x02bd19, KEY_RECORD },
	{ 0x02bd1a, KEY_CHANNELDOWN },
	{ 0x02bd1c, KEY_ESC },
	{ 0x02bd1f, KEY_VOLUMEUP },
	{ 0x02bd44, KEY_EPG },
	{ 0x02bd45, KEY_POWER2 },          /* [red power button] */
};

static struct rc_map_list terratec_slim_map = {
	.map = {
		.scan     = terratec_slim,
		.size     = ARRAY_SIZE(terratec_slim),
		.rc_proto = RC_PROTO_NECX,
		.name     = RC_MAP_TERRATEC_SLIM,
	}
};

static int __init init_rc_map_terratec_slim(void)
{
	return rc_map_register(&terratec_slim_map);
}

static void __exit exit_rc_map_terratec_slim(void)
{
	rc_map_unregister(&terratec_slim_map);
}

module_init(init_rc_map_terratec_slim)
module_exit(exit_rc_map_terratec_slim)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
