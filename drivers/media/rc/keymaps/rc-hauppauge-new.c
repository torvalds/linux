/* hauppauge-new.h - Keytable for hauppauge_new Remote Controller
 *
 * keymap imported from ir-keymaps.c
 *
 * Copyright (c) 2010 by Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>

/* Hauppauge: the newer, gray remotes (seems there are multiple
 * slightly different versions), shipped with cx88+ivtv cards.
 * almost rc5 coding, but some non-standard keys */

static struct rc_map_table hauppauge_new[] = {
	/* Keys 0 to 9 */
	{ 0x00, KEY_0 },
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },

	{ 0x0a, KEY_TEXT },		/* keypad asterisk as well */
	{ 0x0b, KEY_RED },		/* red button */
	{ 0x0c, KEY_RADIO },
	{ 0x0d, KEY_MENU },
	{ 0x0e, KEY_SUBTITLE },		/* also the # key */
	{ 0x0f, KEY_MUTE },
	{ 0x10, KEY_VOLUMEUP },
	{ 0x11, KEY_VOLUMEDOWN },
	{ 0x12, KEY_PREVIOUS },		/* previous channel */
	{ 0x14, KEY_UP },
	{ 0x15, KEY_DOWN },
	{ 0x16, KEY_LEFT },
	{ 0x17, KEY_RIGHT },
	{ 0x18, KEY_VIDEO },		/* Videos */
	{ 0x19, KEY_AUDIO },		/* Music */
	/* 0x1a: Pictures - presume this means
	   "Multimedia Home Platform" -
	   no "PICTURES" key in input.h
	 */
	{ 0x1a, KEY_MHP },

	{ 0x1b, KEY_EPG },		/* Guide */
	{ 0x1c, KEY_TV },
	{ 0x1e, KEY_NEXTSONG },		/* skip >| */
	{ 0x1f, KEY_EXIT },		/* back/exit */
	{ 0x20, KEY_CHANNELUP },	/* channel / program + */
	{ 0x21, KEY_CHANNELDOWN },	/* channel / program - */
	{ 0x22, KEY_CHANNEL },		/* source (old black remote) */
	{ 0x24, KEY_PREVIOUSSONG },	/* replay |< */
	{ 0x25, KEY_ENTER },		/* OK */
	{ 0x26, KEY_SLEEP },		/* minimize (old black remote) */
	{ 0x29, KEY_BLUE },		/* blue key */
	{ 0x2e, KEY_GREEN },		/* green button */
	{ 0x30, KEY_PAUSE },		/* pause */
	{ 0x32, KEY_REWIND },		/* backward << */
	{ 0x34, KEY_FASTFORWARD },	/* forward >> */
	{ 0x35, KEY_PLAY },
	{ 0x36, KEY_STOP },
	{ 0x37, KEY_RECORD },		/* recording */
	{ 0x38, KEY_YELLOW },		/* yellow key */
	{ 0x3b, KEY_SELECT },		/* top right button */
	{ 0x3c, KEY_ZOOM },		/* full */
	{ 0x3d, KEY_POWER },		/* system power (green button) */
};

static struct rc_map_list hauppauge_new_map = {
	.map = {
		.scan    = hauppauge_new,
		.size    = ARRAY_SIZE(hauppauge_new),
		.rc_type = RC_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_HAUPPAUGE_NEW,
	}
};

static int __init init_rc_map_hauppauge_new(void)
{
	return rc_map_register(&hauppauge_new_map);
}

static void __exit exit_rc_map_hauppauge_new(void)
{
	rc_map_unregister(&hauppauge_new_map);
}

module_init(init_rc_map_hauppauge_new)
module_exit(exit_rc_map_hauppauge_new)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
