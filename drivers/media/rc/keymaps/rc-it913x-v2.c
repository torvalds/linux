/* ITE Generic remotes Version 2
 *
 * Copyright (C) 2012 Malcolm Priestley (tvboxspy@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>
#include <linux/module.h>


static struct rc_map_table it913x_v2_rc[] = {
	/* Type 1 */
	/* 9005 remote */
	{ 0x807f12, KEY_POWER2 },	/* Power (RED POWER BUTTON)*/
	{ 0x807f1a, KEY_VIDEO },	/* Source */
	{ 0x807f1e, KEY_MUTE },		/* Mute */
	{ 0x807f01, KEY_RECORD },	/* Record */
	{ 0x807f02, KEY_CHANNELUP },	/* Channel+ */
	{ 0x807f03, KEY_TIME },		/* TimeShift */
	{ 0x807f04, KEY_VOLUMEUP },	/* Volume- */
	{ 0x807f05, KEY_SCREEN },	/* FullScreen */
	{ 0x807f06, KEY_VOLUMEDOWN },	/* Volume- */
	{ 0x807f07, KEY_0 },		/* 0 */
	{ 0x807f08, KEY_CHANNELDOWN },	/* Channel- */
	{ 0x807f09, KEY_PREVIOUS },	/* Recall */
	{ 0x807f0a, KEY_1 },		/* 1 */
	{ 0x807f1b, KEY_2 },		/* 2 */
	{ 0x807f1f, KEY_3 },		/* 3 */
	{ 0x807f0c, KEY_4 },		/* 4 */
	{ 0x807f0d, KEY_5 },		/* 5 */
	{ 0x807f0e, KEY_6 },		/* 6 */
	{ 0x807f00, KEY_7 },		/* 7 */
	{ 0x807f0f, KEY_8 },		/* 8 */
	{ 0x807f19, KEY_9 },		/* 9 */

	/* Type 2 */
	/* keys stereo, snapshot unassigned */
	{ 0x866b00, KEY_0 },
	{ 0x866b01, KEY_1 },
	{ 0x866b02, KEY_2 },
	{ 0x866b03, KEY_3 },
	{ 0x866b04, KEY_4 },
	{ 0x866b05, KEY_5 },
	{ 0x866b06, KEY_6 },
	{ 0x866b07, KEY_7 },
	{ 0x866b08, KEY_8 },
	{ 0x866b09, KEY_9 },
	{ 0x866b12, KEY_POWER },
	{ 0x866b13, KEY_MUTE },
	{ 0x866b0a, KEY_PREVIOUS }, /* Recall */
	{ 0x866b1e, KEY_PAUSE },
	{ 0x866b0c, KEY_VOLUMEUP },
	{ 0x866b18, KEY_VOLUMEDOWN },
	{ 0x866b0b, KEY_CHANNELUP },
	{ 0x866b18, KEY_CHANNELDOWN },
	{ 0x866b10, KEY_ZOOM },
	{ 0x866b1d, KEY_RECORD },
	{ 0x866b0e, KEY_STOP },
	{ 0x866b11, KEY_EPG},
	{ 0x866b1a, KEY_FASTFORWARD },
	{ 0x866b0f, KEY_REWIND },
	{ 0x866b1c, KEY_TV },
	{ 0x866b1b, KEY_TEXT },

};

static struct rc_map_list it913x_v2_map = {
	.map = {
		.scan     = it913x_v2_rc,
		.size     = ARRAY_SIZE(it913x_v2_rc),
		.rc_proto = RC_PROTO_NECX,
		.name     = RC_MAP_IT913X_V2,
	}
};

static int __init init_rc_it913x_v2_map(void)
{
	return rc_map_register(&it913x_v2_map);
}

static void __exit exit_rc_it913x_v2_map(void)
{
	rc_map_unregister(&it913x_v2_map);
}

module_init(init_rc_it913x_v2_map)
module_exit(exit_rc_it913x_v2_map)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Malcolm Priestley tvboxspy@gmail.com");
