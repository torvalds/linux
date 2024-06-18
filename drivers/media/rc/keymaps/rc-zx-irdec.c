// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 Sanechips Technology Co., Ltd.
 * Copyright 2017 Linaro Ltd.
 */

#include <linux/module.h>
#include <media/rc-map.h>

static struct rc_map_table zx_irdec_table[] = {
	{ 0x01, KEY_NUMERIC_1 },
	{ 0x02, KEY_NUMERIC_2 },
	{ 0x03, KEY_NUMERIC_3 },
	{ 0x04, KEY_NUMERIC_4 },
	{ 0x05, KEY_NUMERIC_5 },
	{ 0x06, KEY_NUMERIC_6 },
	{ 0x07, KEY_NUMERIC_7 },
	{ 0x08, KEY_NUMERIC_8 },
	{ 0x09, KEY_NUMERIC_9 },
	{ 0x31, KEY_NUMERIC_0 },
	{ 0x16, KEY_DELETE },
	{ 0x0a, KEY_MODE },		/* Input method */
	{ 0x0c, KEY_VOLUMEUP },
	{ 0x18, KEY_VOLUMEDOWN },
	{ 0x0b, KEY_CHANNELUP },
	{ 0x15, KEY_CHANNELDOWN },
	{ 0x0d, KEY_PAGEUP },
	{ 0x13, KEY_PAGEDOWN },
	{ 0x46, KEY_FASTFORWARD },
	{ 0x43, KEY_REWIND },
	{ 0x44, KEY_PLAYPAUSE },
	{ 0x45, KEY_STOP },
	{ 0x49, KEY_OK },
	{ 0x47, KEY_UP },
	{ 0x4b, KEY_DOWN },
	{ 0x48, KEY_LEFT },
	{ 0x4a, KEY_RIGHT },
	{ 0x4d, KEY_MENU },
	{ 0x56, KEY_APPSELECT },	/* Application */
	{ 0x4c, KEY_BACK },
	{ 0x1e, KEY_INFO },
	{ 0x4e, KEY_F1 },
	{ 0x4f, KEY_F2 },
	{ 0x50, KEY_F3 },
	{ 0x51, KEY_F4 },
	{ 0x1c, KEY_AUDIO },
	{ 0x12, KEY_MUTE },
	{ 0x11, KEY_DOT },		/* Location */
	{ 0x1d, KEY_SETUP },
	{ 0x40, KEY_POWER },
};

static struct rc_map_list zx_irdec_map = {
	.map = {
		.scan = zx_irdec_table,
		.size = ARRAY_SIZE(zx_irdec_table),
		.rc_proto = RC_PROTO_NEC,
		.name = RC_MAP_ZX_IRDEC,
	}
};

static int __init init_rc_map_zx_irdec(void)
{
	return rc_map_register(&zx_irdec_map);
}

static void __exit exit_rc_map_zx_irdec(void)
{
	rc_map_unregister(&zx_irdec_map);
}

module_init(init_rc_map_zx_irdec)
module_exit(exit_rc_map_zx_irdec)

MODULE_AUTHOR("Shawn Guo <shawn.guo@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("zx-irdec remote controller keytable");
