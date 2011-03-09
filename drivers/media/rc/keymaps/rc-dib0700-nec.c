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

static struct rc_map_table dib0700_nec_table[] = {
	/* Key codes for the Pixelview SBTVD remote */
	{ 0x8613, KEY_MUTE },
	{ 0x8612, KEY_POWER },
	{ 0x8601, KEY_1 },
	{ 0x8602, KEY_2 },
	{ 0x8603, KEY_3 },
	{ 0x8604, KEY_4 },
	{ 0x8605, KEY_5 },
	{ 0x8606, KEY_6 },
	{ 0x8607, KEY_7 },
	{ 0x8608, KEY_8 },
	{ 0x8609, KEY_9 },
	{ 0x8600, KEY_0 },
	{ 0x860d, KEY_CHANNELUP },
	{ 0x8619, KEY_CHANNELDOWN },
	{ 0x8610, KEY_VOLUMEUP },
	{ 0x860c, KEY_VOLUMEDOWN },

	{ 0x860a, KEY_CAMERA },
	{ 0x860b, KEY_ZOOM },
	{ 0x861b, KEY_BACKSPACE },
	{ 0x8615, KEY_ENTER },

	{ 0x861d, KEY_UP },
	{ 0x861e, KEY_DOWN },
	{ 0x860e, KEY_LEFT },
	{ 0x860f, KEY_RIGHT },

	{ 0x8618, KEY_RECORD },
	{ 0x861a, KEY_STOP },

	/* Key codes for the EvolutePC TVWay+ remote */
	{ 0x7a00, KEY_MENU },
	{ 0x7a01, KEY_RECORD },
	{ 0x7a02, KEY_PLAY },
	{ 0x7a03, KEY_STOP },
	{ 0x7a10, KEY_CHANNELUP },
	{ 0x7a11, KEY_CHANNELDOWN },
	{ 0x7a12, KEY_VOLUMEUP },
	{ 0x7a13, KEY_VOLUMEDOWN },
	{ 0x7a40, KEY_POWER },
	{ 0x7a41, KEY_MUTE },

	/* Key codes for the Elgato EyeTV Diversity silver remote */
	{ 0x4501, KEY_POWER },
	{ 0x4502, KEY_MUTE },
	{ 0x4503, KEY_1 },
	{ 0x4504, KEY_2 },
	{ 0x4505, KEY_3 },
	{ 0x4506, KEY_4 },
	{ 0x4507, KEY_5 },
	{ 0x4508, KEY_6 },
	{ 0x4509, KEY_7 },
	{ 0x450a, KEY_8 },
	{ 0x450b, KEY_9 },
	{ 0x450c, KEY_LAST },
	{ 0x450d, KEY_0 },
	{ 0x450e, KEY_ENTER },
	{ 0x450f, KEY_RED },
	{ 0x4510, KEY_CHANNELUP },
	{ 0x4511, KEY_GREEN },
	{ 0x4512, KEY_VOLUMEDOWN },
	{ 0x4513, KEY_OK },
	{ 0x4514, KEY_VOLUMEUP },
	{ 0x4515, KEY_YELLOW },
	{ 0x4516, KEY_CHANNELDOWN },
	{ 0x4517, KEY_BLUE },
	{ 0x4518, KEY_LEFT }, /* Skip backwards */
	{ 0x4519, KEY_PLAYPAUSE },
	{ 0x451a, KEY_RIGHT }, /* Skip forward */
	{ 0x451b, KEY_REWIND },
	{ 0x451c, KEY_L }, /* Live */
	{ 0x451d, KEY_FASTFORWARD },
	{ 0x451e, KEY_STOP }, /* 'Reveal' for Teletext */
	{ 0x451f, KEY_MENU }, /* KEY_TEXT for Teletext */
	{ 0x4540, KEY_RECORD }, /* Font 'Size' for Teletext */
	{ 0x4541, KEY_SCREEN }, /*  Full screen toggle, 'Hold' for Teletext */
	{ 0x4542, KEY_SELECT }, /* Select video input, 'Select' for Teletext */
};

static struct rc_map_list dib0700_nec_map = {
	.map = {
		.scan    = dib0700_nec_table,
		.size    = ARRAY_SIZE(dib0700_nec_table),
		.rc_type = RC_TYPE_NEC,
		.name    = RC_MAP_DIB0700_NEC_TABLE,
	}
};

static int __init init_rc_map(void)
{
	return rc_map_register(&dib0700_nec_map);
}

static void __exit exit_rc_map(void)
{
	rc_map_unregister(&dib0700_nec_map);
}

module_init(init_rc_map)
module_exit(exit_rc_map)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
