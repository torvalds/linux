// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 Sigma Designs
 */

#include <linux/module.h>
#include <media/rc-map.h>

static struct rc_map_table tango_table[] = {
	{ 0x4cb4a, KEY_POWER },
	{ 0x4cb48, KEY_FILE },
	{ 0x4cb0f, KEY_SETUP },
	{ 0x4cb4d, KEY_SUSPEND },
	{ 0x4cb4e, KEY_VOLUMEUP },
	{ 0x4cb44, KEY_EJECTCD },
	{ 0x4cb13, KEY_TV },
	{ 0x4cb51, KEY_MUTE },
	{ 0x4cb52, KEY_VOLUMEDOWN },

	{ 0x4cb41, KEY_NUMERIC_1 },
	{ 0x4cb03, KEY_NUMERIC_2 },
	{ 0x4cb42, KEY_NUMERIC_3 },
	{ 0x4cb45, KEY_NUMERIC_4 },
	{ 0x4cb07, KEY_NUMERIC_5 },
	{ 0x4cb46, KEY_NUMERIC_6 },
	{ 0x4cb55, KEY_NUMERIC_7 },
	{ 0x4cb17, KEY_NUMERIC_8 },
	{ 0x4cb56, KEY_NUMERIC_9 },
	{ 0x4cb1b, KEY_NUMERIC_0 },
	{ 0x4cb59, KEY_DELETE },
	{ 0x4cb5a, KEY_CAPSLOCK },

	{ 0x4cb47, KEY_BACK },
	{ 0x4cb05, KEY_SWITCHVIDEOMODE },
	{ 0x4cb06, KEY_UP },
	{ 0x4cb43, KEY_LEFT },
	{ 0x4cb01, KEY_RIGHT },
	{ 0x4cb0a, KEY_DOWN },
	{ 0x4cb02, KEY_ENTER },
	{ 0x4cb4b, KEY_INFO },
	{ 0x4cb09, KEY_HOME },

	{ 0x4cb53, KEY_MENU },
	{ 0x4cb12, KEY_PREVIOUS },
	{ 0x4cb50, KEY_PLAY },
	{ 0x4cb11, KEY_NEXT },
	{ 0x4cb4f, KEY_TITLE },
	{ 0x4cb0e, KEY_REWIND },
	{ 0x4cb4c, KEY_STOP },
	{ 0x4cb0d, KEY_FORWARD },
	{ 0x4cb57, KEY_MEDIA_REPEAT },
	{ 0x4cb16, KEY_ANGLE },
	{ 0x4cb54, KEY_PAUSE },
	{ 0x4cb15, KEY_SLOW },
	{ 0x4cb5b, KEY_TIME },
	{ 0x4cb1a, KEY_AUDIO },
	{ 0x4cb58, KEY_SUBTITLE },
	{ 0x4cb19, KEY_ZOOM },

	{ 0x4cb5f, KEY_RED },
	{ 0x4cb1e, KEY_GREEN },
	{ 0x4cb5c, KEY_YELLOW },
	{ 0x4cb1d, KEY_BLUE },
};

static struct rc_map_list tango_map = {
	.map = {
		.scan = tango_table,
		.size = ARRAY_SIZE(tango_table),
		.rc_proto = RC_PROTO_NECX,
		.name = RC_MAP_TANGO,
	}
};

static int __init init_rc_map_tango(void)
{
	return rc_map_register(&tango_map);
}

static void __exit exit_rc_map_tango(void)
{
	rc_map_unregister(&tango_map);
}

module_init(init_rc_map_tango)
module_exit(exit_rc_map_tango)

MODULE_AUTHOR("Sigma Designs");
MODULE_LICENSE("GPL");
