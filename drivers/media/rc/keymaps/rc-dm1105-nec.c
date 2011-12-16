/* dm1105-nec.h - Keytable for dm1105_nec Remote Controller
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
#include <linux/module.h>

/* DVBWorld remotes
   Igor M. Liplianin <liplianin@me.by>
 */

static struct rc_map_table dm1105_nec[] = {
	{ 0x0a, KEY_POWER2},		/* power */
	{ 0x0c, KEY_MUTE},		/* mute */
	{ 0x11, KEY_1},
	{ 0x12, KEY_2},
	{ 0x13, KEY_3},
	{ 0x14, KEY_4},
	{ 0x15, KEY_5},
	{ 0x16, KEY_6},
	{ 0x17, KEY_7},
	{ 0x18, KEY_8},
	{ 0x19, KEY_9},
	{ 0x10, KEY_0},
	{ 0x1c, KEY_CHANNELUP},		/* ch+ */
	{ 0x0f, KEY_CHANNELDOWN},	/* ch- */
	{ 0x1a, KEY_VOLUMEUP},		/* vol+ */
	{ 0x0e, KEY_VOLUMEDOWN},	/* vol- */
	{ 0x04, KEY_RECORD},		/* rec */
	{ 0x09, KEY_CHANNEL},		/* fav */
	{ 0x08, KEY_BACKSPACE},		/* rewind */
	{ 0x07, KEY_FASTFORWARD},	/* fast */
	{ 0x0b, KEY_PAUSE},		/* pause */
	{ 0x02, KEY_ESC},		/* cancel */
	{ 0x03, KEY_TAB},		/* tab */
	{ 0x00, KEY_UP},		/* up */
	{ 0x1f, KEY_ENTER},		/* ok */
	{ 0x01, KEY_DOWN},		/* down */
	{ 0x05, KEY_RECORD},		/* cap */
	{ 0x06, KEY_STOP},		/* stop */
	{ 0x40, KEY_ZOOM},		/* full */
	{ 0x1e, KEY_TV},		/* tvmode */
	{ 0x1b, KEY_B},			/* recall */
};

static struct rc_map_list dm1105_nec_map = {
	.map = {
		.scan    = dm1105_nec,
		.size    = ARRAY_SIZE(dm1105_nec),
		.rc_type = RC_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_DM1105_NEC,
	}
};

static int __init init_rc_map_dm1105_nec(void)
{
	return rc_map_register(&dm1105_nec_map);
}

static void __exit exit_rc_map_dm1105_nec(void)
{
	rc_map_unregister(&dm1105_nec_map);
}

module_init(init_rc_map_dm1105_nec)
module_exit(exit_rc_map_dm1105_nec)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
