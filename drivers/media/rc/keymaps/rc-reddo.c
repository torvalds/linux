// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MSI DIGIVOX mini III remote controller keytable
 *
 * Copyright (C) 2013 Antti Palosaari <crope@iki.fi>
 */

#include <media/rc-map.h>
#include <linux/module.h>

/*
 * Derived from MSI DIGIVOX mini III remote (rc-msi-digivox-iii.c)
 *
 * Differences between these remotes are:
 *
 * 1) scancode 0x61d601 is mapped to different button:
 *    MSI DIGIVOX mini III   "Source" = KEY_VIDEO
 *    Reddo                     "EPG" = KEY_EPG
 *
 * 2) Reddo remote has less buttons. Missing buttons are: colored buttons,
 *    navigation buttons and main power button.
 */

static struct rc_map_table reddo[] = {
	{ 0x61d601, KEY_EPG },             /* EPG */
	{ 0x61d602, KEY_NUMERIC_3 },
	{ 0x61d604, KEY_NUMERIC_1 },
	{ 0x61d605, KEY_NUMERIC_5 },
	{ 0x61d606, KEY_NUMERIC_6 },
	{ 0x61d607, KEY_CHANNELDOWN },     /* CH- */
	{ 0x61d608, KEY_NUMERIC_2 },
	{ 0x61d609, KEY_CHANNELUP },       /* CH+ */
	{ 0x61d60a, KEY_NUMERIC_9 },
	{ 0x61d60b, KEY_ZOOM },            /* Zoom */
	{ 0x61d60c, KEY_NUMERIC_7 },
	{ 0x61d60d, KEY_NUMERIC_8 },
	{ 0x61d60e, KEY_VOLUMEUP },        /* Vol+ */
	{ 0x61d60f, KEY_NUMERIC_4 },
	{ 0x61d610, KEY_ESC },             /* [back up arrow] */
	{ 0x61d611, KEY_NUMERIC_0 },
	{ 0x61d612, KEY_OK },              /* [enter arrow] */
	{ 0x61d613, KEY_VOLUMEDOWN },      /* Vol- */
	{ 0x61d614, KEY_RECORD },          /* Rec */
	{ 0x61d615, KEY_STOP },            /* Stop */
	{ 0x61d616, KEY_PLAY },            /* Play */
	{ 0x61d617, KEY_MUTE },            /* Mute */
	{ 0x61d643, KEY_POWER2 },          /* [red power button] */
};

static struct rc_map_list reddo_map = {
	.map = {
		.scan     = reddo,
		.size     = ARRAY_SIZE(reddo),
		.rc_proto = RC_PROTO_NECX,
		.name     = RC_MAP_REDDO,
	}
};

static int __init init_rc_map_reddo(void)
{
	return rc_map_register(&reddo_map);
}

static void __exit exit_rc_map_reddo(void)
{
	rc_map_unregister(&reddo_map);
}

module_init(init_rc_map_reddo)
module_exit(exit_rc_map_reddo)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
