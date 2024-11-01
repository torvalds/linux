// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2020 Christian Hewitt

#include <media/rc-map.h>
#include <linux/module.h>

/*
 * KHAMSIN is an IR/Bluetooth RCU supplied with the SmartLabs
 * SML-5442TW DVB-S/VOD box. The RCU has separate IR (TV) and
 * BT (STB) modes. This keymap suppors the IR controls.
 */

static struct rc_map_table khamsin[] = {
	{ 0x70702, KEY_POWER},

	{ 0x70701, KEY_VIDEO}, // source

	{ 0x7076c, KEY_RED},
	{ 0x70714, KEY_GREEN},
	{ 0x70715, KEY_YELLOW},
	{ 0x70716, KEY_BLUE},

	{ 0x7071a, KEY_MENU},
	{ 0x7074f, KEY_EPG},

	{ 0x70760, KEY_UP },
	{ 0x70761, KEY_DOWN },
	{ 0x70765, KEY_LEFT },
	{ 0x70762, KEY_RIGHT },
	{ 0x70768, KEY_ENTER },

	{ 0x7072d, KEY_ESC }, // back

	{ 0x70707, KEY_VOLUMEUP },
	{ 0x7070b, KEY_VOLUMEDOWN },
	{ 0x7070f, KEY_MUTE },
	{ 0x70712, KEY_CHANNELUP },
	{ 0x70710, KEY_CHANNELDOWN },

	{ 0x70704, KEY_1 },
	{ 0x70705, KEY_2 },
	{ 0x70706, KEY_3 },
	{ 0x70708, KEY_4 },
	{ 0x70709, KEY_5 },
	{ 0x7070a, KEY_6 },
	{ 0x7070c, KEY_7 },
	{ 0x7070d, KEY_8 },
	{ 0x7070e, KEY_9 },
	{ 0x70711, KEY_0 },
};

static struct rc_map_list khamsin_map = {
	.map = {
		.scan     = khamsin,
		.size     = ARRAY_SIZE(khamsin),
		.rc_proto = RC_PROTO_NECX,
		.name     = RC_MAP_KHAMSIN,
	}
};

static int __init init_rc_map_khamsin(void)
{
	return rc_map_register(&khamsin_map);
}

static void __exit exit_rc_map_khamsin(void)
{
	rc_map_unregister(&khamsin_map);
}

module_init(init_rc_map_khamsin)
module_exit(exit_rc_map_khamsin)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Hewitt <christianshewitt@gmail.com>");
