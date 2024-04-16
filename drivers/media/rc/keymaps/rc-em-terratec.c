// SPDX-License-Identifier: GPL-2.0+
// em-terratec.h - Keytable for em_terratec Remote Controller
//
// keymap imported from ir-keymaps.c
//
// Copyright (c) 2010 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table em_terratec[] = {
	{ 0x01, KEY_CHANNEL },
	{ 0x02, KEY_SELECT },
	{ 0x03, KEY_MUTE },
	{ 0x04, KEY_POWER },
	{ 0x05, KEY_NUMERIC_1 },
	{ 0x06, KEY_NUMERIC_2 },
	{ 0x07, KEY_NUMERIC_3 },
	{ 0x08, KEY_CHANNELUP },
	{ 0x09, KEY_NUMERIC_4 },
	{ 0x0a, KEY_NUMERIC_5 },
	{ 0x0b, KEY_NUMERIC_6 },
	{ 0x0c, KEY_CHANNELDOWN },
	{ 0x0d, KEY_NUMERIC_7 },
	{ 0x0e, KEY_NUMERIC_8 },
	{ 0x0f, KEY_NUMERIC_9 },
	{ 0x10, KEY_VOLUMEUP },
	{ 0x11, KEY_NUMERIC_0 },
	{ 0x12, KEY_MENU },
	{ 0x13, KEY_PRINT },
	{ 0x14, KEY_VOLUMEDOWN },
	{ 0x16, KEY_PAUSE },
	{ 0x18, KEY_RECORD },
	{ 0x19, KEY_REWIND },
	{ 0x1a, KEY_PLAY },
	{ 0x1b, KEY_FORWARD },
	{ 0x1c, KEY_BACKSPACE },
	{ 0x1e, KEY_STOP },
	{ 0x40, KEY_ZOOM },
};

static struct rc_map_list em_terratec_map = {
	.map = {
		.scan     = em_terratec,
		.size     = ARRAY_SIZE(em_terratec),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_EM_TERRATEC,
	}
};

static int __init init_rc_map_em_terratec(void)
{
	return rc_map_register(&em_terratec_map);
}

static void __exit exit_rc_map_em_terratec(void)
{
	rc_map_unregister(&em_terratec_map);
}

module_init(init_rc_map_em_terratec)
module_exit(exit_rc_map_em_terratec)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
