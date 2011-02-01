/* rc-dvb0700-big.c - Keytable for devices in dvb0700
 *
 * Copyright (c) 2010 by Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * TODO: This table is a real mess, as it merges RC codes from several
 * devices into a big table. It also has both RC-5 and NEC codes inside.
 * It should be broken into small tables, and the protocols should properly
 * be indentificated.
 *
 * The table were imported from dib0700_devices.c.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>

static struct rc_map_table dib0700_rc5_table[] = {
	/* Key codes for the tiny Pinnacle remote*/
	{ 0x0700, KEY_MUTE },
	{ 0x0701, KEY_MENU }, /* Pinnacle logo */
	{ 0x0739, KEY_POWER },
	{ 0x0703, KEY_VOLUMEUP },
	{ 0x0709, KEY_VOLUMEDOWN },
	{ 0x0706, KEY_CHANNELUP },
	{ 0x070c, KEY_CHANNELDOWN },
	{ 0x070f, KEY_1 },
	{ 0x0715, KEY_2 },
	{ 0x0710, KEY_3 },
	{ 0x0718, KEY_4 },
	{ 0x071b, KEY_5 },
	{ 0x071e, KEY_6 },
	{ 0x0711, KEY_7 },
	{ 0x0721, KEY_8 },
	{ 0x0712, KEY_9 },
	{ 0x0727, KEY_0 },
	{ 0x0724, KEY_SCREEN }, /* 'Square' key */
	{ 0x072a, KEY_TEXT },   /* 'T' key */
	{ 0x072d, KEY_REWIND },
	{ 0x0730, KEY_PLAY },
	{ 0x0733, KEY_FASTFORWARD },
	{ 0x0736, KEY_RECORD },
	{ 0x073c, KEY_STOP },
	{ 0x073f, KEY_CANCEL }, /* '?' key */

	/* Key codes for the Terratec Cinergy DT XS Diversity, similar to cinergyT2.c */
	{ 0xeb01, KEY_POWER },
	{ 0xeb02, KEY_1 },
	{ 0xeb03, KEY_2 },
	{ 0xeb04, KEY_3 },
	{ 0xeb05, KEY_4 },
	{ 0xeb06, KEY_5 },
	{ 0xeb07, KEY_6 },
	{ 0xeb08, KEY_7 },
	{ 0xeb09, KEY_8 },
	{ 0xeb0a, KEY_9 },
	{ 0xeb0b, KEY_VIDEO },
	{ 0xeb0c, KEY_0 },
	{ 0xeb0d, KEY_REFRESH },
	{ 0xeb0f, KEY_EPG },
	{ 0xeb10, KEY_UP },
	{ 0xeb11, KEY_LEFT },
	{ 0xeb12, KEY_OK },
	{ 0xeb13, KEY_RIGHT },
	{ 0xeb14, KEY_DOWN },
	{ 0xeb16, KEY_INFO },
	{ 0xeb17, KEY_RED },
	{ 0xeb18, KEY_GREEN },
	{ 0xeb19, KEY_YELLOW },
	{ 0xeb1a, KEY_BLUE },
	{ 0xeb1b, KEY_CHANNELUP },
	{ 0xeb1c, KEY_VOLUMEUP },
	{ 0xeb1d, KEY_MUTE },
	{ 0xeb1e, KEY_VOLUMEDOWN },
	{ 0xeb1f, KEY_CHANNELDOWN },
	{ 0xeb40, KEY_PAUSE },
	{ 0xeb41, KEY_HOME },
	{ 0xeb42, KEY_MENU }, /* DVD Menu */
	{ 0xeb43, KEY_SUBTITLE },
	{ 0xeb44, KEY_TEXT }, /* Teletext */
	{ 0xeb45, KEY_DELETE },
	{ 0xeb46, KEY_TV },
	{ 0xeb47, KEY_DVD },
	{ 0xeb48, KEY_STOP },
	{ 0xeb49, KEY_VIDEO },
	{ 0xeb4a, KEY_AUDIO }, /* Music */
	{ 0xeb4b, KEY_SCREEN }, /* Pic */
	{ 0xeb4c, KEY_PLAY },
	{ 0xeb4d, KEY_BACK },
	{ 0xeb4e, KEY_REWIND },
	{ 0xeb4f, KEY_FASTFORWARD },
	{ 0xeb54, KEY_PREVIOUS },
	{ 0xeb58, KEY_RECORD },
	{ 0xeb5c, KEY_NEXT },

	/* Key codes for the Haupauge WinTV Nova-TD, copied from nova-t-usb2.c (Nova-T USB2) */
	{ 0x1e00, KEY_0 },
	{ 0x1e01, KEY_1 },
	{ 0x1e02, KEY_2 },
	{ 0x1e03, KEY_3 },
	{ 0x1e04, KEY_4 },
	{ 0x1e05, KEY_5 },
	{ 0x1e06, KEY_6 },
	{ 0x1e07, KEY_7 },
	{ 0x1e08, KEY_8 },
	{ 0x1e09, KEY_9 },
	{ 0x1e0a, KEY_KPASTERISK },
	{ 0x1e0b, KEY_RED },
	{ 0x1e0c, KEY_RADIO },
	{ 0x1e0d, KEY_MENU },
	{ 0x1e0e, KEY_GRAVE }, /* # */
	{ 0x1e0f, KEY_MUTE },
	{ 0x1e10, KEY_VOLUMEUP },
	{ 0x1e11, KEY_VOLUMEDOWN },
	{ 0x1e12, KEY_CHANNEL },
	{ 0x1e14, KEY_UP },
	{ 0x1e15, KEY_DOWN },
	{ 0x1e16, KEY_LEFT },
	{ 0x1e17, KEY_RIGHT },
	{ 0x1e18, KEY_VIDEO },
	{ 0x1e19, KEY_AUDIO },
	{ 0x1e1a, KEY_MEDIA },
	{ 0x1e1b, KEY_EPG },
	{ 0x1e1c, KEY_TV },
	{ 0x1e1e, KEY_NEXT },
	{ 0x1e1f, KEY_BACK },
	{ 0x1e20, KEY_CHANNELUP },
	{ 0x1e21, KEY_CHANNELDOWN },
	{ 0x1e24, KEY_LAST }, /* Skip backwards */
	{ 0x1e25, KEY_OK },
	{ 0x1e29, KEY_BLUE},
	{ 0x1e2e, KEY_GREEN },
	{ 0x1e30, KEY_PAUSE },
	{ 0x1e32, KEY_REWIND },
	{ 0x1e34, KEY_FASTFORWARD },
	{ 0x1e35, KEY_PLAY },
	{ 0x1e36, KEY_STOP },
	{ 0x1e37, KEY_RECORD },
	{ 0x1e38, KEY_YELLOW },
	{ 0x1e3b, KEY_GOTO },
	{ 0x1e3d, KEY_POWER },

	/* Key codes for the Leadtek Winfast DTV Dongle */
	{ 0x0042, KEY_POWER },
	{ 0x077c, KEY_TUNER },
	{ 0x0f4e, KEY_PRINT }, /* PREVIEW */
	{ 0x0840, KEY_SCREEN }, /* full screen toggle*/
	{ 0x0f71, KEY_DOT }, /* frequency */
	{ 0x0743, KEY_0 },
	{ 0x0c41, KEY_1 },
	{ 0x0443, KEY_2 },
	{ 0x0b7f, KEY_3 },
	{ 0x0e41, KEY_4 },
	{ 0x0643, KEY_5 },
	{ 0x097f, KEY_6 },
	{ 0x0d7e, KEY_7 },
	{ 0x057c, KEY_8 },
	{ 0x0a40, KEY_9 },
	{ 0x0e4e, KEY_CLEAR },
	{ 0x047c, KEY_CHANNEL }, /* show channel number */
	{ 0x0f41, KEY_LAST }, /* recall */
	{ 0x0342, KEY_MUTE },
	{ 0x064c, KEY_RESERVED }, /* PIP button*/
	{ 0x0172, KEY_SHUFFLE }, /* SNAPSHOT */
	{ 0x0c4e, KEY_PLAYPAUSE }, /* TIMESHIFT */
	{ 0x0b70, KEY_RECORD },
	{ 0x037d, KEY_VOLUMEUP },
	{ 0x017d, KEY_VOLUMEDOWN },
	{ 0x0242, KEY_CHANNELUP },
	{ 0x007d, KEY_CHANNELDOWN },

	/* Key codes for Nova-TD "credit card" remote control. */
	{ 0x1d00, KEY_0 },
	{ 0x1d01, KEY_1 },
	{ 0x1d02, KEY_2 },
	{ 0x1d03, KEY_3 },
	{ 0x1d04, KEY_4 },
	{ 0x1d05, KEY_5 },
	{ 0x1d06, KEY_6 },
	{ 0x1d07, KEY_7 },
	{ 0x1d08, KEY_8 },
	{ 0x1d09, KEY_9 },
	{ 0x1d0a, KEY_TEXT },
	{ 0x1d0d, KEY_MENU },
	{ 0x1d0f, KEY_MUTE },
	{ 0x1d10, KEY_VOLUMEUP },
	{ 0x1d11, KEY_VOLUMEDOWN },
	{ 0x1d12, KEY_CHANNEL },
	{ 0x1d14, KEY_UP },
	{ 0x1d15, KEY_DOWN },
	{ 0x1d16, KEY_LEFT },
	{ 0x1d17, KEY_RIGHT },
	{ 0x1d1c, KEY_TV },
	{ 0x1d1e, KEY_NEXT },
	{ 0x1d1f, KEY_BACK },
	{ 0x1d20, KEY_CHANNELUP },
	{ 0x1d21, KEY_CHANNELDOWN },
	{ 0x1d24, KEY_LAST },
	{ 0x1d25, KEY_OK },
	{ 0x1d30, KEY_PAUSE },
	{ 0x1d32, KEY_REWIND },
	{ 0x1d34, KEY_FASTFORWARD },
	{ 0x1d35, KEY_PLAY },
	{ 0x1d36, KEY_STOP },
	{ 0x1d37, KEY_RECORD },
	{ 0x1d3b, KEY_GOTO },
	{ 0x1d3d, KEY_POWER },
};

static struct rc_map_list dib0700_rc5_map = {
	.map = {
		.scan    = dib0700_rc5_table,
		.size    = ARRAY_SIZE(dib0700_rc5_table),
		.rc_type = RC_TYPE_RC5,
		.name    = RC_MAP_DIB0700_RC5_TABLE,
	}
};

static int __init init_rc_map(void)
{
	return rc_map_register(&dib0700_rc5_map);
}

static void __exit exit_rc_map(void)
{
	rc_map_unregister(&dib0700_rc5_map);
}

module_init(init_rc_map)
module_exit(exit_rc_map)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
