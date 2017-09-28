/* adstech-dvb-t-pci.h - Keytable for adstech_dvb_t_pci Remote Controller
 *
 * keymap imported from ir-keymaps.c
 *
 * Copyright (c) 2010 by Mauro Carvalho Chehab
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>
#include <linux/module.h>

/* ADS Tech Instant TV DVB-T PCI Remote */

static struct rc_map_table adstech_dvb_t_pci[] = {
	/* Keys 0 to 9 */
	{ 0x4d, KEY_0 },
	{ 0x57, KEY_1 },
	{ 0x4f, KEY_2 },
	{ 0x53, KEY_3 },
	{ 0x56, KEY_4 },
	{ 0x4e, KEY_5 },
	{ 0x5e, KEY_6 },
	{ 0x54, KEY_7 },
	{ 0x4c, KEY_8 },
	{ 0x5c, KEY_9 },

	{ 0x5b, KEY_POWER },
	{ 0x5f, KEY_MUTE },
	{ 0x55, KEY_GOTO },
	{ 0x5d, KEY_SEARCH },
	{ 0x17, KEY_EPG },		/* Guide */
	{ 0x1f, KEY_MENU },
	{ 0x0f, KEY_UP },
	{ 0x46, KEY_DOWN },
	{ 0x16, KEY_LEFT },
	{ 0x1e, KEY_RIGHT },
	{ 0x0e, KEY_SELECT },		/* Enter */
	{ 0x5a, KEY_INFO },
	{ 0x52, KEY_EXIT },
	{ 0x59, KEY_PREVIOUS },
	{ 0x51, KEY_NEXT },
	{ 0x58, KEY_REWIND },
	{ 0x50, KEY_FORWARD },
	{ 0x44, KEY_PLAYPAUSE },
	{ 0x07, KEY_STOP },
	{ 0x1b, KEY_RECORD },
	{ 0x13, KEY_TUNER },		/* Live */
	{ 0x0a, KEY_A },
	{ 0x12, KEY_B },
	{ 0x03, KEY_RED },		/* 1 */
	{ 0x01, KEY_GREEN },		/* 2 */
	{ 0x00, KEY_YELLOW },		/* 3 */
	{ 0x06, KEY_DVD },
	{ 0x48, KEY_AUX },		/* Photo */
	{ 0x40, KEY_VIDEO },
	{ 0x19, KEY_AUDIO },		/* Music */
	{ 0x0b, KEY_CHANNELUP },
	{ 0x08, KEY_CHANNELDOWN },
	{ 0x15, KEY_VOLUMEUP },
	{ 0x1c, KEY_VOLUMEDOWN },
};

static struct rc_map_list adstech_dvb_t_pci_map = {
	.map = {
		.scan     = adstech_dvb_t_pci,
		.size     = ARRAY_SIZE(adstech_dvb_t_pci),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_ADSTECH_DVB_T_PCI,
	}
};

static int __init init_rc_map_adstech_dvb_t_pci(void)
{
	return rc_map_register(&adstech_dvb_t_pci_map);
}

static void __exit exit_rc_map_adstech_dvb_t_pci(void)
{
	rc_map_unregister(&adstech_dvb_t_pci_map);
}

module_init(init_rc_map_adstech_dvb_t_pci)
module_exit(exit_rc_map_adstech_dvb_t_pci)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
