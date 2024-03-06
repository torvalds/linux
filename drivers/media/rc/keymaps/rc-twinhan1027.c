// SPDX-License-Identifier: GPL-2.0-only
#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table twinhan_vp1027[] = {
	{ 0x16, KEY_POWER2 },
	{ 0x17, KEY_FAVORITES },
	{ 0x0f, KEY_TEXT },
	{ 0x48, KEY_INFO},
	{ 0x1c, KEY_EPG },
	{ 0x04, KEY_LIST },

	{ 0x03, KEY_NUMERIC_1 },
	{ 0x01, KEY_NUMERIC_2 },
	{ 0x06, KEY_NUMERIC_3 },
	{ 0x09, KEY_NUMERIC_4 },
	{ 0x1d, KEY_NUMERIC_5 },
	{ 0x1f, KEY_NUMERIC_6 },
	{ 0x0d, KEY_NUMERIC_7 },
	{ 0x19, KEY_NUMERIC_8 },
	{ 0x1b, KEY_NUMERIC_9 },
	{ 0x15, KEY_NUMERIC_0 },

	{ 0x0c, KEY_CANCEL },
	{ 0x4a, KEY_CLEAR },
	{ 0x13, KEY_BACKSPACE },
	{ 0x00, KEY_TAB },

	{ 0x4b, KEY_UP },
	{ 0x51, KEY_DOWN },
	{ 0x4e, KEY_LEFT },
	{ 0x52, KEY_RIGHT },
	{ 0x4f, KEY_ENTER },

	{ 0x1e, KEY_VOLUMEUP },
	{ 0x0a, KEY_VOLUMEDOWN },
	{ 0x02, KEY_CHANNELDOWN },
	{ 0x05, KEY_CHANNELUP },
	{ 0x11, KEY_RECORD },

	{ 0x14, KEY_PLAY },
	{ 0x4c, KEY_PAUSE },
	{ 0x1a, KEY_STOP },
	{ 0x40, KEY_REWIND },
	{ 0x12, KEY_FASTFORWARD },
	{ 0x41, KEY_PREVIOUSSONG },
	{ 0x42, KEY_NEXTSONG },
	{ 0x54, KEY_SAVE },
	{ 0x50, KEY_LANGUAGE },
	{ 0x47, KEY_MEDIA },
	{ 0x4d, KEY_SCREEN },
	{ 0x43, KEY_SUBTITLE },
	{ 0x10, KEY_MUTE },
	{ 0x49, KEY_AUDIO },
	{ 0x07, KEY_SLEEP },
	{ 0x08, KEY_VIDEO },
	{ 0x0e, KEY_AGAIN },
	{ 0x45, KEY_EQUAL },
	{ 0x46, KEY_MINUS },
	{ 0x18, KEY_RED },
	{ 0x53, KEY_GREEN },
	{ 0x5e, KEY_YELLOW },
	{ 0x5f, KEY_BLUE },
};

static struct rc_map_list twinhan_vp1027_map = {
	.map = {
		.scan     = twinhan_vp1027,
		.size     = ARRAY_SIZE(twinhan_vp1027),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_TWINHAN_VP1027_DVBS,
	}
};

static int __init init_rc_map_twinhan_vp1027(void)
{
	return rc_map_register(&twinhan_vp1027_map);
}

static void __exit exit_rc_map_twinhan_vp1027(void)
{
	rc_map_unregister(&twinhan_vp1027_map);
}

module_init(init_rc_map_twinhan_vp1027)
module_exit(exit_rc_map_twinhan_vp1027)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sergey Ivanov <123kash@gmail.com>");
MODULE_DESCRIPTION("twinhan1027 remote controller keytable");
