// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2021 Emanuel Strobel <emanuel.strobel@yahoo.com>
 */

#include <media/rc-map.h>
#include <linux/module.h>

/*
 * Keytable for Dreambox RC10/RC0 and RC20/RC-BT remote controls
 *
 * Keys that are not IR addressable:
 *
 * // DREAM switches to STB control mode
 * // TV switches to TV control mode
 * // MODE toggles STB/TV/BT control modes
 *
 */

static struct rc_map_table dreambox[] = {
	/* Dreambox RC10/RC0/RCU-BT remote */
	{ 0x3200, KEY_POWER },

	// DREAM
	{ 0x3290, KEY_HELP },
	// TV

	{ 0x3201, KEY_1 },
	{ 0x3202, KEY_2 },
	{ 0x3203, KEY_3 },
	{ 0x3204, KEY_4 },
	{ 0x3205, KEY_5 },
	{ 0x3206, KEY_6 },
	{ 0x3207, KEY_7 },
	{ 0x3208, KEY_8 },
	{ 0x3209, KEY_9 },
	{ 0x320a, KEY_PREVIOUS },
	{ 0x320b, KEY_0 },
	{ 0x320c, KEY_NEXT },

	{ 0x321f, KEY_RED },
	{ 0x3220, KEY_GREEN },
	{ 0x3221, KEY_YELLOW },
	{ 0x3222, KEY_BLUE },

	{ 0x3210, KEY_INFO },
	{ 0x3212, KEY_MENU },
	{ 0x320e, KEY_AUDIO },
	{ 0x3218, KEY_PVR },

	{ 0x3213, KEY_LEFT },
	{ 0x3211, KEY_UP },
	{ 0x3215, KEY_RIGHT },
	{ 0x3217, KEY_DOWN },
	{ 0x3214, KEY_OK },

	{ 0x3219, KEY_VOLUMEUP },
	{ 0x321c, KEY_VOLUMEDOWN },

	{ 0x321d, KEY_ESC }, // EXIT
	{ 0x321a, KEY_MUTE },

	{ 0x321b, KEY_PAGEUP },
	{ 0x321e, KEY_PAGEDOWN },

	{ 0x3223, KEY_PREVIOUSSONG },
	{ 0x3224, KEY_PLAYPAUSE },
	{ 0x3225, KEY_STOP },
	{ 0x3226, KEY_NEXTSONG },

	{ 0x3227, KEY_TV },
	{ 0x3228, KEY_RADIO },
	{ 0x3229, KEY_TEXT },
	{ 0x322a, KEY_RECORD },

	/* Dreambox RC20/RC-BT */
	{ 0x3407, KEY_MUTE },
	// MODE
	{ 0x3401, KEY_POWER },

	{ 0x3432, KEY_PREVIOUSSONG },
	{ 0x3433, KEY_PLAYPAUSE },
	{ 0x3435, KEY_NEXTSONG },

	{ 0x3436, KEY_RECORD },
	{ 0x3434, KEY_STOP },
	{ 0x3425, KEY_TEXT },

	{ 0x341f, KEY_RED },
	{ 0x3420, KEY_GREEN },
	{ 0x3421, KEY_YELLOW },
	{ 0x3422, KEY_BLUE },

	{ 0x341b, KEY_INFO },
	{ 0x341c, KEY_MENU },
	{ 0x3430, KEY_AUDIO },
	{ 0x3431, KEY_PVR },

	{ 0x3414, KEY_LEFT },
	{ 0x3411, KEY_UP },
	{ 0x3416, KEY_RIGHT },
	{ 0x3419, KEY_DOWN },
	{ 0x3415, KEY_OK },

	{ 0x3413, KEY_VOLUMEUP },
	{ 0x3418, KEY_VOLUMEDOWN },

	{ 0x3412, KEY_ESC }, // EXIT
	{ 0x3426, KEY_HELP }, // MIC

	{ 0x3417, KEY_PAGEUP },
	{ 0x341a, KEY_PAGEDOWN },

	{ 0x3404, KEY_1 },
	{ 0x3405, KEY_2 },
	{ 0x3406, KEY_3 },
	{ 0x3408, KEY_4 },
	{ 0x3409, KEY_5 },
	{ 0x340a, KEY_6 },
	{ 0x340c, KEY_7 },
	{ 0x340d, KEY_8 },
	{ 0x340e, KEY_9 },
	{ 0x340b, KEY_PREVIOUS },
	{ 0x3410, KEY_0 },
	{ 0x340f, KEY_NEXT },
};

static struct rc_map_list dreambox_map = {
	.map = {
		.scan     = dreambox,
		.size     = ARRAY_SIZE(dreambox),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_DREAMBOX,
	}
};

static int __init init_rc_map_dreambox(void)
{
	return rc_map_register(&dreambox_map);
}

static void __exit exit_rc_map_dreambox(void)
{
	rc_map_unregister(&dreambox_map);
}

module_init(init_rc_map_dreambox)
module_exit(exit_rc_map_dreambox)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emanuel Strobel <emanuel.strobel@yahoo.com>");
MODULE_DESCRIPTION("Dreambox RC10/RC0 and RC20/RC-BT remote controller keytable");
