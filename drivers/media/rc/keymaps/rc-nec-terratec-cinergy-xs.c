/* nec-terratec-cinergy-xs.h - Keytable for nec_terratec_cinergy_xs Remote Controller
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

/* Terratec Cinergy Hybrid T USB XS FM
   Mauro Carvalho Chehab <mchehab@redhat.com>
 */

static struct rc_map_table nec_terratec_cinergy_xs[] = {
	{ 0x1441, KEY_HOME},
	{ 0x1401, KEY_POWER2},

	{ 0x1442, KEY_MENU},		/* DVD menu */
	{ 0x1443, KEY_SUBTITLE},
	{ 0x1444, KEY_TEXT},		/* Teletext */
	{ 0x1445, KEY_DELETE},

	{ 0x1402, KEY_1},
	{ 0x1403, KEY_2},
	{ 0x1404, KEY_3},
	{ 0x1405, KEY_4},
	{ 0x1406, KEY_5},
	{ 0x1407, KEY_6},
	{ 0x1408, KEY_7},
	{ 0x1409, KEY_8},
	{ 0x140a, KEY_9},
	{ 0x140c, KEY_0},

	{ 0x140b, KEY_TUNER},		/* AV */
	{ 0x140d, KEY_MODE},		/* A.B */

	{ 0x1446, KEY_TV},
	{ 0x1447, KEY_DVD},
	{ 0x1449, KEY_VIDEO},
	{ 0x144a, KEY_RADIO},		/* Music */
	{ 0x144b, KEY_CAMERA},		/* PIC */

	{ 0x1410, KEY_UP},
	{ 0x1411, KEY_LEFT},
	{ 0x1412, KEY_OK},
	{ 0x1413, KEY_RIGHT},
	{ 0x1414, KEY_DOWN},

	{ 0x140f, KEY_EPG},
	{ 0x1416, KEY_INFO},
	{ 0x144d, KEY_BACKSPACE},

	{ 0x141c, KEY_VOLUMEUP},
	{ 0x141e, KEY_VOLUMEDOWN},

	{ 0x144c, KEY_PLAY},
	{ 0x141d, KEY_MUTE},

	{ 0x141b, KEY_CHANNELUP},
	{ 0x141f, KEY_CHANNELDOWN},

	{ 0x1417, KEY_RED},
	{ 0x1418, KEY_GREEN},
	{ 0x1419, KEY_YELLOW},
	{ 0x141a, KEY_BLUE},

	{ 0x1458, KEY_RECORD},
	{ 0x1448, KEY_STOP},
	{ 0x1440, KEY_PAUSE},

	{ 0x1454, KEY_LAST},
	{ 0x144e, KEY_REWIND},
	{ 0x144f, KEY_FASTFORWARD},
	{ 0x145c, KEY_NEXT},
};

static struct rc_map_list nec_terratec_cinergy_xs_map = {
	.map = {
		.scan    = nec_terratec_cinergy_xs,
		.size    = ARRAY_SIZE(nec_terratec_cinergy_xs),
		.rc_type = RC_TYPE_NEC,
		.name    = RC_MAP_NEC_TERRATEC_CINERGY_XS,
	}
};

static int __init init_rc_map_nec_terratec_cinergy_xs(void)
{
	return rc_map_register(&nec_terratec_cinergy_xs_map);
}

static void __exit exit_rc_map_nec_terratec_cinergy_xs(void)
{
	rc_map_unregister(&nec_terratec_cinergy_xs_map);
}

module_init(init_rc_map_nec_terratec_cinergy_xs)
module_exit(exit_rc_map_nec_terratec_cinergy_xs)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
