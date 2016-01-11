/* keytable for Twinhan DTV CAB CI Remote Controller
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

static struct rc_map_table twinhan_dtv_cab_ci[] = {
	{ 0x29, KEY_POWER},
	{ 0x28, KEY_FAVORITES},
	{ 0x30, KEY_TEXT},
	{ 0x17, KEY_INFO},              /* Preview */
	{ 0x23, KEY_EPG},
	{ 0x3b, KEY_F22},               /* Record List */

	{ 0x3c, KEY_1},
	{ 0x3e, KEY_2},
	{ 0x39, KEY_3},
	{ 0x36, KEY_4},
	{ 0x22, KEY_5},
	{ 0x20, KEY_6},
	{ 0x32, KEY_7},
	{ 0x26, KEY_8},
	{ 0x24, KEY_9},
	{ 0x2a, KEY_0},

	{ 0x33, KEY_CANCEL},
	{ 0x2c, KEY_BACK},
	{ 0x15, KEY_CLEAR},
	{ 0x3f, KEY_TAB},
	{ 0x10, KEY_ENTER},
	{ 0x14, KEY_UP},
	{ 0x0d, KEY_RIGHT},
	{ 0x0e, KEY_DOWN},
	{ 0x11, KEY_LEFT},

	{ 0x21, KEY_VOLUMEUP},
	{ 0x35, KEY_VOLUMEDOWN},
	{ 0x3d, KEY_CHANNELDOWN},
	{ 0x3a, KEY_CHANNELUP},
	{ 0x2e, KEY_RECORD},
	{ 0x2b, KEY_PLAY},
	{ 0x13, KEY_PAUSE},
	{ 0x25, KEY_STOP},

	{ 0x1f, KEY_REWIND},
	{ 0x2d, KEY_FASTFORWARD},
	{ 0x1e, KEY_PREVIOUS},          /* Replay |< */
	{ 0x1d, KEY_NEXT},              /* Skip   >| */

	{ 0x0b, KEY_CAMERA},            /* Capture */
	{ 0x0f, KEY_LANGUAGE},          /* SAP */
	{ 0x18, KEY_MODE},              /* PIP */
	{ 0x12, KEY_ZOOM},              /* Full screen */
	{ 0x1c, KEY_SUBTITLE},
	{ 0x2f, KEY_MUTE},
	{ 0x16, KEY_F20},               /* L/R */
	{ 0x38, KEY_F21},               /* Hibernate */

	{ 0x37, KEY_SWITCHVIDEOMODE},   /* A/V */
	{ 0x31, KEY_AGAIN},             /* Recall */
	{ 0x1a, KEY_KPPLUS},            /* Zoom+ */
	{ 0x19, KEY_KPMINUS},           /* Zoom- */
	{ 0x27, KEY_RED},
	{ 0x0C, KEY_GREEN},
	{ 0x01, KEY_YELLOW},
	{ 0x00, KEY_BLUE},
};

static struct rc_map_list twinhan_dtv_cab_ci_map = {
	.map = {
		.scan    = twinhan_dtv_cab_ci,
		.size    = ARRAY_SIZE(twinhan_dtv_cab_ci),
		.rc_type = RC_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_TWINHAN_DTV_CAB_CI,
	}
};

static int __init init_rc_map_twinhan_dtv_cab_ci(void)
{
	return rc_map_register(&twinhan_dtv_cab_ci_map);
}

static void __exit exit_rc_map_twinhan_dtv_cab_ci(void)
{
	rc_map_unregister(&twinhan_dtv_cab_ci_map);
}

module_init(init_rc_map_twinhan_dtv_cab_ci);
module_exit(exit_rc_map_twinhan_dtv_cab_ci);

MODULE_LICENSE("GPL");
