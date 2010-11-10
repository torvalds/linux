/* rc5-imon-pad.c - Keytable for SoundGraph iMON PAD and Antec Veris
 * RM-200 Remote Control
 *
 * Copyright (c) 2010 by Jarod Wilson <jarod@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>

/*
 * standard imon remote key table, which isn't really entirely
 * "standard", as different receivers decode the same key on the
 * same remote to different hex codes, and the silkscreened names
 * vary a bit between the SoundGraph and Antec remotes... ugh.
 */
static struct ir_scancode imon_pad[] = {
	/* keys sorted mostly by frequency of use to optimize lookups */
	{ 0x2a8195b7, KEY_REWIND },
	{ 0x298315b7, KEY_REWIND },
	{ 0x2b8115b7, KEY_FASTFORWARD },
	{ 0x2b8315b7, KEY_FASTFORWARD },
	{ 0x2b9115b7, KEY_PREVIOUS },
	{ 0x298195b7, KEY_NEXT },

	{ 0x2a8115b7, KEY_PLAY },
	{ 0x2a8315b7, KEY_PLAY },
	{ 0x2a9115b7, KEY_PAUSE },
	{ 0x2b9715b7, KEY_STOP },
	{ 0x298115b7, KEY_RECORD },

	{ 0x01008000, KEY_UP },
	{ 0x01007f00, KEY_DOWN },
	{ 0x01000080, KEY_LEFT },
	{ 0x0100007f, KEY_RIGHT },

	{ 0x2aa515b7, KEY_UP },
	{ 0x289515b7, KEY_DOWN },
	{ 0x29a515b7, KEY_LEFT },
	{ 0x2ba515b7, KEY_RIGHT },

	{ 0x0200002c, KEY_SPACE }, /* Select/Space */
	{ 0x2a9315b7, KEY_SPACE }, /* Select/Space */
	{ 0x02000028, KEY_ENTER },
	{ 0x28a195b7, KEY_ENTER },
	{ 0x288195b7, KEY_EXIT },
	{ 0x02000029, KEY_ESC },
	{ 0x2bb715b7, KEY_ESC },
	{ 0x0200002a, KEY_BACKSPACE },
	{ 0x28a115b7, KEY_BACKSPACE },

	{ 0x2b9595b7, KEY_MUTE },
	{ 0x28a395b7, KEY_VOLUMEUP },
	{ 0x28a595b7, KEY_VOLUMEDOWN },
	{ 0x289395b7, KEY_CHANNELUP },
	{ 0x288795b7, KEY_CHANNELDOWN },

	{ 0x0200001e, KEY_NUMERIC_1 },
	{ 0x0200001f, KEY_NUMERIC_2 },
	{ 0x02000020, KEY_NUMERIC_3 },
	{ 0x02000021, KEY_NUMERIC_4 },
	{ 0x02000022, KEY_NUMERIC_5 },
	{ 0x02000023, KEY_NUMERIC_6 },
	{ 0x02000024, KEY_NUMERIC_7 },
	{ 0x02000025, KEY_NUMERIC_8 },
	{ 0x02000026, KEY_NUMERIC_9 },
	{ 0x02000027, KEY_NUMERIC_0 },

	{ 0x28b595b7, KEY_NUMERIC_1 },
	{ 0x2bb195b7, KEY_NUMERIC_2 },
	{ 0x28b195b7, KEY_NUMERIC_3 },
	{ 0x2a8595b7, KEY_NUMERIC_4 },
	{ 0x299595b7, KEY_NUMERIC_5 },
	{ 0x2aa595b7, KEY_NUMERIC_6 },
	{ 0x2b9395b7, KEY_NUMERIC_7 },
	{ 0x2a8515b7, KEY_NUMERIC_8 },
	{ 0x2aa115b7, KEY_NUMERIC_9 },
	{ 0x2ba595b7, KEY_NUMERIC_0 },

	{ 0x02200025, KEY_NUMERIC_STAR },
	{ 0x28b515b7, KEY_NUMERIC_STAR },
	{ 0x02200020, KEY_NUMERIC_POUND },
	{ 0x29a115b7, KEY_NUMERIC_POUND },

	{ 0x2b8515b7, KEY_VIDEO },
	{ 0x299195b7, KEY_AUDIO },
	{ 0x2ba115b7, KEY_CAMERA },
	{ 0x28a515b7, KEY_TV },
	{ 0x29a395b7, KEY_DVD },
	{ 0x29a295b7, KEY_DVD },

	/* the Menu key between DVD and Subtitle on the RM-200... */
	{ 0x2ba385b7, KEY_MENU },
	{ 0x2ba395b7, KEY_MENU },

	{ 0x288515b7, KEY_BOOKMARKS },
	{ 0x2ab715b7, KEY_MEDIA }, /* Thumbnail */
	{ 0x298595b7, KEY_SUBTITLE },
	{ 0x2b8595b7, KEY_LANGUAGE },

	{ 0x29a595b7, KEY_ZOOM },
	{ 0x2aa395b7, KEY_SCREEN }, /* FullScreen */

	{ 0x299115b7, KEY_KEYBOARD },
	{ 0x299135b7, KEY_KEYBOARD },

	{ 0x01010000, BTN_LEFT },
	{ 0x01020000, BTN_RIGHT },
	{ 0x01010080, BTN_LEFT },
	{ 0x01020080, BTN_RIGHT },
	{ 0x688301b7, BTN_LEFT },
	{ 0x688481b7, BTN_RIGHT },

	{ 0x2a9395b7, KEY_CYCLEWINDOWS }, /* TaskSwitcher */
	{ 0x2b8395b7, KEY_TIME }, /* Timer */

	{ 0x289115b7, KEY_POWER },
	{ 0x29b195b7, KEY_EJECTCD }, /* the one next to play */
	{ 0x299395b7, KEY_EJECTCLOSECD }, /* eject (by TaskSw) */

	{ 0x02800000, KEY_CONTEXT_MENU }, /* Left Menu */
	{ 0x2b8195b7, KEY_CONTEXT_MENU }, /* Left Menu*/
	{ 0x02000065, KEY_COMPOSE }, /* RightMenu */
	{ 0x28b715b7, KEY_COMPOSE }, /* RightMenu */
	{ 0x2ab195b7, KEY_PROG1 }, /* Go or MultiMon */
	{ 0x29b715b7, KEY_DASHBOARD }, /* AppLauncher */
};

static struct rc_keymap imon_pad_map = {
	.map = {
		.scan    = imon_pad,
		.size    = ARRAY_SIZE(imon_pad),
		/* actual protocol details unknown, hardware decoder */
		.ir_type = IR_TYPE_OTHER,
		.name    = RC_MAP_IMON_PAD,
	}
};

static int __init init_rc_map_imon_pad(void)
{
	return ir_register_map(&imon_pad_map);
}

static void __exit exit_rc_map_imon_pad(void)
{
	ir_unregister_map(&imon_pad_map);
}

module_init(init_rc_map_imon_pad)
module_exit(exit_rc_map_imon_pad)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jarod Wilson <jarod@redhat.com>");
