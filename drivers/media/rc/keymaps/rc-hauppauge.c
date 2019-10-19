// SPDX-License-Identifier: GPL-2.0+
// rc-hauppauge.c - Keytable for Hauppauge Remote Controllers
//
// keymap imported from ir-keymaps.c
//
// This map currently contains the code for four different RCs:
//	- New Hauppauge Gray;
//	- Old Hauppauge Gray (with a golden screen for media keys);
//	- Hauppauge Black;
//	- DSR-0112 remote bundled with Haupauge MiniStick.
//
// Copyright (c) 2010-2011 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

/*
 * Hauppauge:the newer, gray remotes (seems there are multiple
 * slightly different versions), shipped with cx88+ivtv cards.
 *
 * This table contains the complete RC5 code, instead of just the data part
 */

static struct rc_map_table rc5_hauppauge_new[] = {
	/*
	 * Remote Controller Hauppauge Gray found on modern devices
	 * Keycodes start with address = 0x1e
	 */

	{ 0x1e3b, KEY_SELECT },		/* GO / house symbol */
	{ 0x1e3d, KEY_POWER2 },		/* system power (green button) */

	{ 0x1e1c, KEY_TV },
	{ 0x1e18, KEY_VIDEO },		/* Videos */
	{ 0x1e19, KEY_AUDIO },		/* Music */
	{ 0x1e1a, KEY_CAMERA },		/* Pictures */

	{ 0x1e1b, KEY_EPG },		/* Guide */
	{ 0x1e0c, KEY_RADIO },

	{ 0x1e14, KEY_UP },
	{ 0x1e15, KEY_DOWN },
	{ 0x1e16, KEY_LEFT },
	{ 0x1e17, KEY_RIGHT },
	{ 0x1e25, KEY_OK },		/* OK */

	{ 0x1e1f, KEY_EXIT },		/* back/exit */
	{ 0x1e0d, KEY_MENU },

	{ 0x1e10, KEY_VOLUMEUP },
	{ 0x1e11, KEY_VOLUMEDOWN },

	{ 0x1e12, KEY_PREVIOUS },	/* previous channel */
	{ 0x1e0f, KEY_MUTE },

	{ 0x1e20, KEY_CHANNELUP },	/* channel / program + */
	{ 0x1e21, KEY_CHANNELDOWN },	/* channel / program - */

	{ 0x1e37, KEY_RECORD },		/* recording */
	{ 0x1e36, KEY_STOP },

	{ 0x1e32, KEY_REWIND },		/* backward << */
	{ 0x1e35, KEY_PLAY },
	{ 0x1e34, KEY_FASTFORWARD },	/* forward >> */

	{ 0x1e24, KEY_PREVIOUSSONG },	/* replay |< */
	{ 0x1e30, KEY_PAUSE },		/* pause */
	{ 0x1e1e, KEY_NEXTSONG },	/* skip >| */

	{ 0x1e01, KEY_NUMERIC_1 },
	{ 0x1e02, KEY_NUMERIC_2 },
	{ 0x1e03, KEY_NUMERIC_3 },

	{ 0x1e04, KEY_NUMERIC_4 },
	{ 0x1e05, KEY_NUMERIC_5 },
	{ 0x1e06, KEY_NUMERIC_6 },

	{ 0x1e07, KEY_NUMERIC_7 },
	{ 0x1e08, KEY_NUMERIC_8 },
	{ 0x1e09, KEY_NUMERIC_9 },

	{ 0x1e0a, KEY_TEXT },		/* keypad asterisk as well */
	{ 0x1e00, KEY_NUMERIC_0 },
	{ 0x1e0e, KEY_SUBTITLE },	/* also the Pound key (#) */

	{ 0x1e0b, KEY_RED },		/* red button */
	{ 0x1e2e, KEY_GREEN },		/* green button */
	{ 0x1e38, KEY_YELLOW },		/* yellow key */
	{ 0x1e29, KEY_BLUE },		/* blue key */

	/*
	 * Old Remote Controller Hauppauge Gray with a golden screen
	 * Keycodes start with address = 0x1f
	 */
	{ 0x1f3d, KEY_POWER2 },		/* system power (green button) */
	{ 0x1f3b, KEY_SELECT },		/* GO */

	/* Keys 0 to 9 */
	{ 0x1f00, KEY_NUMERIC_0 },
	{ 0x1f01, KEY_NUMERIC_1 },
	{ 0x1f02, KEY_NUMERIC_2 },
	{ 0x1f03, KEY_NUMERIC_3 },
	{ 0x1f04, KEY_NUMERIC_4 },
	{ 0x1f05, KEY_NUMERIC_5 },
	{ 0x1f06, KEY_NUMERIC_6 },
	{ 0x1f07, KEY_NUMERIC_7 },
	{ 0x1f08, KEY_NUMERIC_8 },
	{ 0x1f09, KEY_NUMERIC_9 },

	{ 0x1f1f, KEY_EXIT },		/* back/exit */
	{ 0x1f0d, KEY_MENU },

	{ 0x1f10, KEY_VOLUMEUP },
	{ 0x1f11, KEY_VOLUMEDOWN },
	{ 0x1f20, KEY_CHANNELUP },	/* channel / program + */
	{ 0x1f21, KEY_CHANNELDOWN },	/* channel / program - */
	{ 0x1f25, KEY_ENTER },		/* OK */

	{ 0x1f0b, KEY_RED },		/* red button */
	{ 0x1f2e, KEY_GREEN },		/* green button */
	{ 0x1f38, KEY_YELLOW },		/* yellow key */
	{ 0x1f29, KEY_BLUE },		/* blue key */

	{ 0x1f0f, KEY_MUTE },
	{ 0x1f0c, KEY_RADIO },		/* There's no indicator on this key */
	{ 0x1f3c, KEY_ZOOM },		/* full */

	{ 0x1f32, KEY_REWIND },		/* backward << */
	{ 0x1f35, KEY_PLAY },
	{ 0x1f34, KEY_FASTFORWARD },	/* forward >> */

	{ 0x1f37, KEY_RECORD },		/* recording */
	{ 0x1f36, KEY_STOP },
	{ 0x1f30, KEY_PAUSE },		/* pause */

	{ 0x1f24, KEY_PREVIOUSSONG },	/* replay |< */
	{ 0x1f1e, KEY_NEXTSONG },	/* skip >| */

	/*
	 * Keycodes for DSR-0112 remote bundled with Haupauge MiniStick
	 * Keycodes start with address = 0x1d
	 */
	{ 0x1d00, KEY_NUMERIC_0 },
	{ 0x1d01, KEY_NUMERIC_1 },
	{ 0x1d02, KEY_NUMERIC_2 },
	{ 0x1d03, KEY_NUMERIC_3 },
	{ 0x1d04, KEY_NUMERIC_4 },
	{ 0x1d05, KEY_NUMERIC_5 },
	{ 0x1d06, KEY_NUMERIC_6 },
	{ 0x1d07, KEY_NUMERIC_7 },
	{ 0x1d08, KEY_NUMERIC_8 },
	{ 0x1d09, KEY_NUMERIC_9 },
	{ 0x1d0a, KEY_TEXT },
	{ 0x1d0d, KEY_MENU },
	{ 0x1d0f, KEY_MUTE },
	{ 0x1d10, KEY_VOLUMEUP },
	{ 0x1d11, KEY_VOLUMEDOWN },
	{ 0x1d12, KEY_PREVIOUS },        /* Prev.Ch .. ??? */
	{ 0x1d14, KEY_UP },
	{ 0x1d15, KEY_DOWN },
	{ 0x1d16, KEY_LEFT },
	{ 0x1d17, KEY_RIGHT },
	{ 0x1d1c, KEY_TV },
	{ 0x1d1e, KEY_NEXT },           /* >|             */
	{ 0x1d1f, KEY_EXIT },
	{ 0x1d20, KEY_CHANNELUP },
	{ 0x1d21, KEY_CHANNELDOWN },
	{ 0x1d24, KEY_LAST },           /* <|             */
	{ 0x1d25, KEY_OK },
	{ 0x1d30, KEY_PAUSE },
	{ 0x1d32, KEY_REWIND },
	{ 0x1d34, KEY_FASTFORWARD },
	{ 0x1d35, KEY_PLAY },
	{ 0x1d36, KEY_STOP },
	{ 0x1d37, KEY_RECORD },
	{ 0x1d3b, KEY_GOTO },
	{ 0x1d3d, KEY_POWER },
	{ 0x1d3f, KEY_HOME },

	/*
	 * Keycodes for PT# R-005 remote bundled with Haupauge HVR-930C
	 * Keycodes start with address = 0x1c
	 */
	{ 0x1c3b, KEY_GOTO },
	{ 0x1c3d, KEY_POWER },

	{ 0x1c14, KEY_UP },
	{ 0x1c15, KEY_DOWN },
	{ 0x1c16, KEY_LEFT },
	{ 0x1c17, KEY_RIGHT },
	{ 0x1c25, KEY_OK },

	{ 0x1c00, KEY_NUMERIC_0 },
	{ 0x1c01, KEY_NUMERIC_1 },
	{ 0x1c02, KEY_NUMERIC_2 },
	{ 0x1c03, KEY_NUMERIC_3 },
	{ 0x1c04, KEY_NUMERIC_4 },
	{ 0x1c05, KEY_NUMERIC_5 },
	{ 0x1c06, KEY_NUMERIC_6 },
	{ 0x1c07, KEY_NUMERIC_7 },
	{ 0x1c08, KEY_NUMERIC_8 },
	{ 0x1c09, KEY_NUMERIC_9 },

	{ 0x1c1f, KEY_EXIT },	/* BACK */
	{ 0x1c0d, KEY_MENU },
	{ 0x1c1c, KEY_TV },

	{ 0x1c10, KEY_VOLUMEUP },
	{ 0x1c11, KEY_VOLUMEDOWN },

	{ 0x1c20, KEY_CHANNELUP },
	{ 0x1c21, KEY_CHANNELDOWN },

	{ 0x1c0f, KEY_MUTE },
	{ 0x1c12, KEY_PREVIOUS }, /* Prev */

	{ 0x1c36, KEY_STOP },
	{ 0x1c37, KEY_RECORD },

	{ 0x1c24, KEY_LAST },           /* <|             */
	{ 0x1c1e, KEY_NEXT },           /* >|             */

	{ 0x1c0a, KEY_TEXT },
	{ 0x1c0e, KEY_SUBTITLE },	/* CC */

	{ 0x1c32, KEY_REWIND },
	{ 0x1c30, KEY_PAUSE },
	{ 0x1c35, KEY_PLAY },
	{ 0x1c34, KEY_FASTFORWARD },

	/*
	 * Keycodes for the old Black Remote Controller
	 * This one also uses RC-5 protocol
	 * Keycodes start with address = 0x00
	 */
	{ 0x000f, KEY_TV },
	{ 0x001f, KEY_TV },
	{ 0x0020, KEY_CHANNELUP },
	{ 0x000c, KEY_RADIO },

	{ 0x0011, KEY_VOLUMEDOWN },
	{ 0x002e, KEY_ZOOM },		/* full screen */
	{ 0x0010, KEY_VOLUMEUP },

	{ 0x000d, KEY_MUTE },
	{ 0x0021, KEY_CHANNELDOWN },
	{ 0x0022, KEY_VIDEO },		/* source */

	{ 0x0001, KEY_NUMERIC_1 },
	{ 0x0002, KEY_NUMERIC_2 },
	{ 0x0003, KEY_NUMERIC_3 },

	{ 0x0004, KEY_NUMERIC_4 },
	{ 0x0005, KEY_NUMERIC_5 },
	{ 0x0006, KEY_NUMERIC_6 },

	{ 0x0007, KEY_NUMERIC_7 },
	{ 0x0008, KEY_NUMERIC_8 },
	{ 0x0009, KEY_NUMERIC_9 },

	{ 0x001e, KEY_RED },	/* Reserved */
	{ 0x0000, KEY_NUMERIC_0 },
	{ 0x0026, KEY_SLEEP },	/* Minimize */
};

static struct rc_map_list rc5_hauppauge_new_map = {
	.map = {
		.scan     = rc5_hauppauge_new,
		.size     = ARRAY_SIZE(rc5_hauppauge_new),
		.rc_proto = RC_PROTO_RC5,
		.name     = RC_MAP_HAUPPAUGE,
	}
};

static int __init init_rc_map_rc5_hauppauge_new(void)
{
	return rc_map_register(&rc5_hauppauge_new_map);
}

static void __exit exit_rc_map_rc5_hauppauge_new(void)
{
	rc_map_unregister(&rc5_hauppauge_new_map);
}

module_init(init_rc_map_rc5_hauppauge_new)
module_exit(exit_rc_map_rc5_hauppauge_new)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
