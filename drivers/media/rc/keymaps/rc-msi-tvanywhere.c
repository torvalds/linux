// SPDX-License-Identifier: GPL-2.0+
// msi-tvanywhere.h - Keytable for msi_tvanywhere Remote Controller
//
// keymap imported from ir-keymaps.c
//
// Copyright (c) 2010 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

/* MSI TV@nywhere MASTER remote */

static struct rc_map_table msi_tvanywhere[] = {
	/* Keys 0 to 9 */
	{ 0x00, KEY_NUMERIC_0 },
	{ 0x01, KEY_NUMERIC_1 },
	{ 0x02, KEY_NUMERIC_2 },
	{ 0x03, KEY_NUMERIC_3 },
	{ 0x04, KEY_NUMERIC_4 },
	{ 0x05, KEY_NUMERIC_5 },
	{ 0x06, KEY_NUMERIC_6 },
	{ 0x07, KEY_NUMERIC_7 },
	{ 0x08, KEY_NUMERIC_8 },
	{ 0x09, KEY_NUMERIC_9 },

	{ 0x0c, KEY_MUTE },
	{ 0x0f, KEY_SCREEN },		/* Full Screen */
	{ 0x10, KEY_FN },		/* Function */
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

static struct rc_map_list msi_tvanywhere_map = {
	.map = {
		.scan     = msi_tvanywhere,
		.size     = ARRAY_SIZE(msi_tvanywhere),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_MSI_TVANYWHERE,
	}
};

static int __init init_rc_map_msi_tvanywhere(void)
{
	return rc_map_register(&msi_tvanywhere_map);
}

static void __exit exit_rc_map_msi_tvanywhere(void)
{
	rc_map_unregister(&msi_tvanywhere_map);
}

module_init(init_rc_map_msi_tvanywhere)
module_exit(exit_rc_map_msi_tvanywhere)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_DESCRIPTION("MSI TV@nywhere MASTER remote controller keytable");
