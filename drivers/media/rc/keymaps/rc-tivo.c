/* rc-tivo.c - Keytable for TiVo remotes
 *
 * Copyright (c) 2011 by Jarod Wilson <jarod@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>
#include <linux/module.h>

/*
 * Initial mapping is for the TiVo remote included in the Nero LiquidTV bundle,
 * which also ships with a TiVo-branded IR transceiver, supported by the mceusb
 * driver. Note that the remote uses an NEC-ish protocol, but instead of having
 * a command/not_command pair, it has a vendor ID of 0x3085, but some keys, the
 * NEC extended checksums do pass, so the table presently has the intended
 * values and the checksum-passed versions for those keys.
 */
static struct rc_map_table tivo[] = {
	{ 0x3085f009, KEY_MEDIA },	/* TiVo Button */
	{ 0x3085e010, KEY_POWER2 },	/* TV Power */
	{ 0x3085e011, KEY_TV },		/* Live TV/Swap */
	{ 0x3085c034, KEY_VIDEO_NEXT },	/* TV Input */
	{ 0x3085e013, KEY_INFO },
	{ 0x3085a05f, KEY_CYCLEWINDOWS }, /* Window */
	{ 0x0085305f, KEY_CYCLEWINDOWS },
	{ 0x3085c036, KEY_EPG },	/* Guide */

	{ 0x3085e014, KEY_UP },
	{ 0x3085e016, KEY_DOWN },
	{ 0x3085e017, KEY_LEFT },
	{ 0x3085e015, KEY_RIGHT },

	{ 0x3085e018, KEY_SCROLLDOWN },	/* Red Thumbs Down */
	{ 0x3085e019, KEY_SELECT },
	{ 0x3085e01a, KEY_SCROLLUP },	/* Green Thumbs Up */

	{ 0x3085e01c, KEY_VOLUMEUP },
	{ 0x3085e01d, KEY_VOLUMEDOWN },
	{ 0x3085e01b, KEY_MUTE },
	{ 0x3085d020, KEY_RECORD },
	{ 0x3085e01e, KEY_CHANNELUP },
	{ 0x3085e01f, KEY_CHANNELDOWN },
	{ 0x0085301f, KEY_CHANNELDOWN },

	{ 0x3085d021, KEY_PLAY },
	{ 0x3085d023, KEY_PAUSE },
	{ 0x3085d025, KEY_SLOW },
	{ 0x3085d022, KEY_REWIND },
	{ 0x3085d024, KEY_FASTFORWARD },
	{ 0x3085d026, KEY_PREVIOUS },
	{ 0x3085d027, KEY_NEXT },	/* ->| */

	{ 0x3085b044, KEY_ZOOM },	/* Aspect */
	{ 0x3085b048, KEY_STOP },
	{ 0x3085b04a, KEY_DVD },	/* DVD Menu */

	{ 0x3085d028, KEY_NUMERIC_1 },
	{ 0x3085d029, KEY_NUMERIC_2 },
	{ 0x3085d02a, KEY_NUMERIC_3 },
	{ 0x3085d02b, KEY_NUMERIC_4 },
	{ 0x3085d02c, KEY_NUMERIC_5 },
	{ 0x3085d02d, KEY_NUMERIC_6 },
	{ 0x3085d02e, KEY_NUMERIC_7 },
	{ 0x3085d02f, KEY_NUMERIC_8 },
	{ 0x0085302f, KEY_NUMERIC_8 },
	{ 0x3085c030, KEY_NUMERIC_9 },
	{ 0x3085c031, KEY_NUMERIC_0 },
	{ 0x3085c033, KEY_ENTER },
	{ 0x3085c032, KEY_CLEAR },
};

static struct rc_map_list tivo_map = {
	.map = {
		.scan     = tivo,
		.size     = ARRAY_SIZE(tivo),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_TIVO,
	}
};

static int __init init_rc_map_tivo(void)
{
	return rc_map_register(&tivo_map);
}

static void __exit exit_rc_map_tivo(void)
{
	rc_map_unregister(&tivo_map);
}

module_init(init_rc_map_tivo)
module_exit(exit_rc_map_tivo)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jarod Wilson <jarod@redhat.com>");
