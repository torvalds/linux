/* dntv-live-dvb-t.h - Keytable for dntv_live_dvb_t Remote Controller
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

/* DigitalNow DNTV Live DVB-T Remote */

static struct rc_map_table dntv_live_dvb_t[] = {
	{ 0x00, KEY_ESC },		/* 'go up a level?' */
	/* Keys 0 to 9 */
	{ 0x0a, KEY_0 },
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },

	{ 0x0b, KEY_TUNER },		/* tv/fm */
	{ 0x0c, KEY_SEARCH },		/* scan */
	{ 0x0d, KEY_STOP },
	{ 0x0e, KEY_PAUSE },
	{ 0x0f, KEY_VIDEO },		/* source */

	{ 0x10, KEY_MUTE },
	{ 0x11, KEY_REWIND },		/* backward << */
	{ 0x12, KEY_POWER },
	{ 0x13, KEY_CAMERA },		/* snap */
	{ 0x14, KEY_AUDIO },		/* stereo */
	{ 0x15, KEY_CLEAR },		/* reset */
	{ 0x16, KEY_PLAY },
	{ 0x17, KEY_ENTER },
	{ 0x18, KEY_ZOOM },		/* full screen */
	{ 0x19, KEY_FASTFORWARD },	/* forward >> */
	{ 0x1a, KEY_CHANNELUP },
	{ 0x1b, KEY_VOLUMEUP },
	{ 0x1c, KEY_INFO },		/* preview */
	{ 0x1d, KEY_RECORD },		/* record */
	{ 0x1e, KEY_CHANNELDOWN },
	{ 0x1f, KEY_VOLUMEDOWN },
};

static struct rc_map_list dntv_live_dvb_t_map = {
	.map = {
		.scan    = dntv_live_dvb_t,
		.size    = ARRAY_SIZE(dntv_live_dvb_t),
		.rc_type = RC_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_DNTV_LIVE_DVB_T,
	}
};

static int __init init_rc_map_dntv_live_dvb_t(void)
{
	return rc_map_register(&dntv_live_dvb_t_map);
}

static void __exit exit_rc_map_dntv_live_dvb_t(void)
{
	rc_map_unregister(&dntv_live_dvb_t_map);
}

module_init(init_rc_map_dntv_live_dvb_t)
module_exit(exit_rc_map_dntv_live_dvb_t)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
