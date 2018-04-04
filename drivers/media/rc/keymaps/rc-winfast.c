// SPDX-License-Identifier: GPL-2.0+
// winfast.h - Keytable for winfast Remote Controller
//
// keymap imported from ir-keymaps.c
//
// Copyright (c) 2010 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

/* Table for Leadtek Winfast Remote Controls - used by both bttv and cx88 */

static struct rc_map_table winfast[] = {
	/* Keys 0 to 9 */
	{ 0x12, KEY_0 },
	{ 0x05, KEY_1 },
	{ 0x06, KEY_2 },
	{ 0x07, KEY_3 },
	{ 0x09, KEY_4 },
	{ 0x0a, KEY_5 },
	{ 0x0b, KEY_6 },
	{ 0x0d, KEY_7 },
	{ 0x0e, KEY_8 },
	{ 0x0f, KEY_9 },

	{ 0x00, KEY_POWER2 },
	{ 0x1b, KEY_AUDIO },		/* Audio Source */
	{ 0x02, KEY_TUNER },		/* TV/FM, not on Y0400052 */
	{ 0x1e, KEY_VIDEO },		/* Video Source */
	{ 0x16, KEY_INFO },		/* Display information */
	{ 0x04, KEY_RIGHT },
	{ 0x08, KEY_LEFT },
	{ 0x0c, KEY_UP },
	{ 0x10, KEY_DOWN },
	{ 0x03, KEY_ZOOM },		/* fullscreen */
	{ 0x1f, KEY_TEXT },		/* closed caption/teletext */
	{ 0x20, KEY_SLEEP },
	{ 0x29, KEY_CLEAR },		/* boss key */
	{ 0x14, KEY_MUTE },
	{ 0x2b, KEY_RED },
	{ 0x2c, KEY_GREEN },
	{ 0x2d, KEY_YELLOW },
	{ 0x2e, KEY_BLUE },
	{ 0x18, KEY_KPPLUS },		/* fine tune + , not on Y040052 */
	{ 0x19, KEY_KPMINUS },		/* fine tune - , not on Y040052 */
	{ 0x2a, KEY_TV2 },		/* PIP (Picture in picture */
	{ 0x21, KEY_DOT },
	{ 0x13, KEY_ENTER },
	{ 0x11, KEY_LAST },		/* Recall (last channel */
	{ 0x22, KEY_PREVIOUS },
	{ 0x23, KEY_PLAYPAUSE },
	{ 0x24, KEY_NEXT },
	{ 0x25, KEY_TIME },		/* Time Shifting */
	{ 0x26, KEY_STOP },
	{ 0x27, KEY_RECORD },
	{ 0x28, KEY_CAMERA },		/* Screenshot */
	{ 0x2f, KEY_MENU },
	{ 0x30, KEY_CANCEL },
	{ 0x31, KEY_CHANNEL },		/* Channel Surf */
	{ 0x32, KEY_SUBTITLE },
	{ 0x33, KEY_LANGUAGE },
	{ 0x34, KEY_REWIND },
	{ 0x35, KEY_FASTFORWARD },
	{ 0x36, KEY_TV },
	{ 0x37, KEY_RADIO },		/* FM */
	{ 0x38, KEY_DVD },

	{ 0x1a, KEY_MODE},		/* change to MCE mode on Y04G0051 */
	{ 0x3e, KEY_VOLUMEUP },		/* MCE +VOL, on Y04G0033 */
	{ 0x3a, KEY_VOLUMEDOWN },	/* MCE -VOL, on Y04G0033 */
	{ 0x3b, KEY_CHANNELUP },	/* MCE +CH,  on Y04G0033 */
	{ 0x3f, KEY_CHANNELDOWN }	/* MCE -CH,  on Y04G0033 */
};

static struct rc_map_list winfast_map = {
	.map = {
		.scan     = winfast,
		.size     = ARRAY_SIZE(winfast),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_WINFAST,
	}
};

static int __init init_rc_map_winfast(void)
{
	return rc_map_register(&winfast_map);
}

static void __exit exit_rc_map_winfast(void)
{
	rc_map_unregister(&winfast_map);
}

module_init(init_rc_map_winfast)
module_exit(exit_rc_map_winfast)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
