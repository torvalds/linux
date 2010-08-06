/* msi-tvanywhere.h - Keytable for msi_tvanywhere Remote Controller
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

/* MSI TV@nywhere MASTER remote */

static struct ir_scancode msi_tvanywhere[] = {
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

	{ 0x0c, KEY_MUTE },
	{ 0x0f, KEY_SCREEN },		/* Full Screen */
	{ 0x10, KEY_FN },		/* Funtion */
	{ 0x11, KEY_TIME },		/* Time shift */
	{ 0x12, KEY_POWER },
	{ 0x13, KEY_MEDIA },		/* MTS */
	{ 0x14, KEY_SLOW },
	{ 0x16, KEY_REWIND },		/* backward << */
	{ 0x17, KEY_ENTER },		/* Return */
	{ 0x18, KEY_FASTFORWARD },	/* forward >> */
	{ 0x1a, KEY_CHANNELUP },
	{ 0x1b, KEY_VOLUMEUP },
	{ 0x1e, KEY_CHANNELDOWN },
	{ 0x1f, KEY_VOLUMEDOWN },
};

static struct rc_keymap msi_tvanywhere_map = {
	.map = {
		.scan    = msi_tvanywhere,
		.size    = ARRAY_SIZE(msi_tvanywhere),
		.ir_type = IR_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_MSI_TVANYWHERE,
	}
};

static int __init init_rc_map_msi_tvanywhere(void)
{
	return ir_register_map(&msi_tvanywhere_map);
}

static void __exit exit_rc_map_msi_tvanywhere(void)
{
	ir_unregister_map(&msi_tvanywhere_map);
}

module_init(init_rc_map_msi_tvanywhere)
module_exit(exit_rc_map_msi_tvanywhere)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
