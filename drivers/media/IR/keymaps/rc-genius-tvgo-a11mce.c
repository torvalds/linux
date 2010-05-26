/* genius-tvgo-a11mce.h - Keytable for genius_tvgo_a11mce Remote Controller
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

/*
 * Remote control for the Genius TVGO A11MCE
 * Adrian Pardini <pardo.bsso@gmail.com>
 */

static struct ir_scancode genius_tvgo_a11mce[] = {
	/* Keys 0 to 9 */
	{ 0x48, KEY_0 },
	{ 0x09, KEY_1 },
	{ 0x1d, KEY_2 },
	{ 0x1f, KEY_3 },
	{ 0x19, KEY_4 },
	{ 0x1b, KEY_5 },
	{ 0x11, KEY_6 },
	{ 0x17, KEY_7 },
	{ 0x12, KEY_8 },
	{ 0x16, KEY_9 },

	{ 0x54, KEY_RECORD },		/* recording */
	{ 0x06, KEY_MUTE },		/* mute */
	{ 0x10, KEY_POWER },
	{ 0x40, KEY_LAST },		/* recall */
	{ 0x4c, KEY_CHANNELUP },	/* channel / program + */
	{ 0x00, KEY_CHANNELDOWN },	/* channel / program - */
	{ 0x0d, KEY_VOLUMEUP },
	{ 0x15, KEY_VOLUMEDOWN },
	{ 0x4d, KEY_OK },		/* also labeled as Pause */
	{ 0x1c, KEY_ZOOM },		/* full screen and Stop*/
	{ 0x02, KEY_MODE },		/* AV Source or Rewind*/
	{ 0x04, KEY_LIST },		/* -/-- */
	/* small arrows above numbers */
	{ 0x1a, KEY_NEXT },		/* also Fast Forward */
	{ 0x0e, KEY_PREVIOUS },		/* also Rewind */
	/* these are in a rather non standard layout and have
	an alternate name written */
	{ 0x1e, KEY_UP },		/* Video Setting */
	{ 0x0a, KEY_DOWN },		/* Video Default */
	{ 0x05, KEY_CAMERA },		/* Snapshot */
	{ 0x0c, KEY_RIGHT },		/* Hide Panel */
	/* Four buttons without label */
	{ 0x49, KEY_RED },
	{ 0x0b, KEY_GREEN },
	{ 0x13, KEY_YELLOW },
	{ 0x50, KEY_BLUE },
};

static struct rc_keymap genius_tvgo_a11mce_map = {
	.map = {
		.scan    = genius_tvgo_a11mce,
		.size    = ARRAY_SIZE(genius_tvgo_a11mce),
		.ir_type = IR_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_GENIUS_TVGO_A11MCE,
	}
};

static int __init init_rc_map_genius_tvgo_a11mce(void)
{
	return ir_register_map(&genius_tvgo_a11mce_map);
}

static void __exit exit_rc_map_genius_tvgo_a11mce(void)
{
	ir_unregister_map(&genius_tvgo_a11mce_map);
}

module_init(init_rc_map_genius_tvgo_a11mce)
module_exit(exit_rc_map_genius_tvgo_a11mce)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
