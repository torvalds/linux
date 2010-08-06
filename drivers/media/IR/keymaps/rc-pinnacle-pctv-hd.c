/* pinnacle-pctv-hd.h - Keytable for pinnacle_pctv_hd Remote Controller
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

/* Pinnacle PCTV HD 800i mini remote */

static struct ir_scancode pinnacle_pctv_hd[] = {

	{ 0x0f, KEY_1 },
	{ 0x15, KEY_2 },
	{ 0x10, KEY_3 },
	{ 0x18, KEY_4 },
	{ 0x1b, KEY_5 },
	{ 0x1e, KEY_6 },
	{ 0x11, KEY_7 },
	{ 0x21, KEY_8 },
	{ 0x12, KEY_9 },
	{ 0x27, KEY_0 },

	{ 0x24, KEY_ZOOM },
	{ 0x2a, KEY_SUBTITLE },

	{ 0x00, KEY_MUTE },
	{ 0x01, KEY_ENTER },	/* Pinnacle Logo */
	{ 0x39, KEY_POWER },

	{ 0x03, KEY_VOLUMEUP },
	{ 0x09, KEY_VOLUMEDOWN },
	{ 0x06, KEY_CHANNELUP },
	{ 0x0c, KEY_CHANNELDOWN },

	{ 0x2d, KEY_REWIND },
	{ 0x30, KEY_PLAYPAUSE },
	{ 0x33, KEY_FASTFORWARD },
	{ 0x3c, KEY_STOP },
	{ 0x36, KEY_RECORD },
	{ 0x3f, KEY_EPG },	/* Labeled "?" */
};

static struct rc_keymap pinnacle_pctv_hd_map = {
	.map = {
		.scan    = pinnacle_pctv_hd,
		.size    = ARRAY_SIZE(pinnacle_pctv_hd),
		.ir_type = IR_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_PINNACLE_PCTV_HD,
	}
};

static int __init init_rc_map_pinnacle_pctv_hd(void)
{
	return ir_register_map(&pinnacle_pctv_hd_map);
}

static void __exit exit_rc_map_pinnacle_pctv_hd(void)
{
	ir_unregister_map(&pinnacle_pctv_hd_map);
}

module_init(init_rc_map_pinnacle_pctv_hd)
module_exit(exit_rc_map_pinnacle_pctv_hd)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
