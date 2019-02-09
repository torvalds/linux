/* keytable for Terratec Cinergy C PCI Remote Controller
 *
 * Copyright (c) 2010 by Igor M. Liplianin <liplianin@me.by>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table terratec_cinergy_c_pci[] = {
	{ 0x3e, KEY_POWER},
	{ 0x3d, KEY_1},
	{ 0x3c, KEY_2},
	{ 0x3b, KEY_3},
	{ 0x3a, KEY_4},
	{ 0x39, KEY_5},
	{ 0x38, KEY_6},
	{ 0x37, KEY_7},
	{ 0x36, KEY_8},
	{ 0x35, KEY_9},
	{ 0x34, KEY_VIDEO_NEXT}, /* AV */
	{ 0x33, KEY_0},
	{ 0x32, KEY_REFRESH},
	{ 0x30, KEY_EPG},
	{ 0x2f, KEY_UP},
	{ 0x2e, KEY_LEFT},
	{ 0x2d, KEY_OK},
	{ 0x2c, KEY_RIGHT},
	{ 0x2b, KEY_DOWN},
	{ 0x29, KEY_INFO},
	{ 0x28, KEY_RED},
	{ 0x27, KEY_GREEN},
	{ 0x26, KEY_YELLOW},
	{ 0x25, KEY_BLUE},
	{ 0x24, KEY_CHANNELUP},
	{ 0x23, KEY_VOLUMEUP},
	{ 0x22, KEY_MUTE},
	{ 0x21, KEY_VOLUMEDOWN},
	{ 0x20, KEY_CHANNELDOWN},
	{ 0x1f, KEY_PAUSE},
	{ 0x1e, KEY_HOME},
	{ 0x1d, KEY_MENU}, /* DVD Menu */
	{ 0x1c, KEY_SUBTITLE},
	{ 0x1b, KEY_TEXT}, /* Teletext */
	{ 0x1a, KEY_DELETE},
	{ 0x19, KEY_TV},
	{ 0x18, KEY_DVD},
	{ 0x17, KEY_STOP},
	{ 0x16, KEY_VIDEO},
	{ 0x15, KEY_AUDIO}, /* Music */
	{ 0x14, KEY_SCREEN}, /* Pic */
	{ 0x13, KEY_PLAY},
	{ 0x12, KEY_BACK},
	{ 0x11, KEY_REWIND},
	{ 0x10, KEY_FASTFORWARD},
	{ 0x0b, KEY_PREVIOUS},
	{ 0x07, KEY_RECORD},
	{ 0x03, KEY_NEXT},

};

static struct rc_map_list terratec_cinergy_c_pci_map = {
	.map = {
		.scan    = terratec_cinergy_c_pci,
		.size    = ARRAY_SIZE(terratec_cinergy_c_pci),
		.rc_type = RC_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_TERRATEC_CINERGY_C_PCI,
	}
};

static int __init init_rc_map_terratec_cinergy_c_pci(void)
{
	return rc_map_register(&terratec_cinergy_c_pci_map);
}

static void __exit exit_rc_map_terratec_cinergy_c_pci(void)
{
	rc_map_unregister(&terratec_cinergy_c_pci_map);
}

module_init(init_rc_map_terratec_cinergy_c_pci);
module_exit(exit_rc_map_terratec_cinergy_c_pci);

MODULE_LICENSE("GPL");
