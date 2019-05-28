// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DigitalNow TinyTwin remote controller keytable
 *
 * Copyright (C) 2010 Antti Palosaari <crope@iki.fi>
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table digitalnow_tinytwin[] = {
	{ 0x0000, KEY_MUTE },            /* [symbol speaker] */
	{ 0x0001, KEY_VOLUMEUP },
	{ 0x0002, KEY_POWER2 },          /* TV [power button] */
	{ 0x0003, KEY_2 },
	{ 0x0004, KEY_3 },
	{ 0x0005, KEY_4 },
	{ 0x0006, KEY_6 },
	{ 0x0007, KEY_7 },
	{ 0x0008, KEY_8 },
	{ 0x0009, KEY_NUMERIC_STAR },    /* [*] */
	{ 0x000a, KEY_0 },
	{ 0x000b, KEY_NUMERIC_POUND },   /* [#] */
	{ 0x000c, KEY_RIGHT },           /* [right arrow] */
	{ 0x000d, KEY_HOMEPAGE },        /* [symbol home] Start */
	{ 0x000e, KEY_RED },             /* [red] Videos */
	{ 0x0010, KEY_POWER },           /* PC [power button] */
	{ 0x0011, KEY_YELLOW },          /* [yellow] Pictures */
	{ 0x0012, KEY_DOWN },            /* [down arrow] */
	{ 0x0013, KEY_GREEN },           /* [green] Music */
	{ 0x0014, KEY_CYCLEWINDOWS },    /* BACK */
	{ 0x0015, KEY_FAVORITES },       /* MORE */
	{ 0x0016, KEY_UP },              /* [up arrow] */
	{ 0x0017, KEY_LEFT },            /* [left arrow] */
	{ 0x0018, KEY_OK },              /* OK */
	{ 0x0019, KEY_BLUE },            /* [blue] MyTV */
	{ 0x001a, KEY_REWIND },          /* REW [<<] */
	{ 0x001b, KEY_PLAY },            /* PLAY */
	{ 0x001c, KEY_5 },
	{ 0x001d, KEY_9 },
	{ 0x001e, KEY_VOLUMEDOWN },
	{ 0x001f, KEY_1 },
	{ 0x0040, KEY_STOP },            /* STOP */
	{ 0x0042, KEY_PAUSE },           /* PAUSE */
	{ 0x0043, KEY_SCREEN },          /* Aspect */
	{ 0x0044, KEY_FORWARD },         /* FWD [>>] */
	{ 0x0045, KEY_NEXT },            /* SKIP */
	{ 0x0048, KEY_RECORD },          /* RECORD */
	{ 0x0049, KEY_VIDEO },           /* RTV */
	{ 0x004a, KEY_EPG },             /* Guide */
	{ 0x004b, KEY_CHANNELUP },
	{ 0x004c, KEY_HELP },            /* Help */
	{ 0x004d, KEY_RADIO },           /* Radio */
	{ 0x004f, KEY_CHANNELDOWN },
	{ 0x0050, KEY_DVD },             /* DVD */
	{ 0x0051, KEY_AUDIO },           /* Audio */
	{ 0x0052, KEY_TITLE },           /* Title */
	{ 0x0053, KEY_NEW },             /* [symbol PIP?] */
	{ 0x0057, KEY_MENU },            /* Mouse */
	{ 0x005a, KEY_PREVIOUS },        /* REPLAY */
};

static struct rc_map_list digitalnow_tinytwin_map = {
	.map = {
		.scan     = digitalnow_tinytwin,
		.size     = ARRAY_SIZE(digitalnow_tinytwin),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_DIGITALNOW_TINYTWIN,
	}
};

static int __init init_rc_map_digitalnow_tinytwin(void)
{
	return rc_map_register(&digitalnow_tinytwin_map);
}

static void __exit exit_rc_map_digitalnow_tinytwin(void)
{
	rc_map_unregister(&digitalnow_tinytwin_map);
}

module_init(init_rc_map_digitalnow_tinytwin)
module_exit(exit_rc_map_digitalnow_tinytwin)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
