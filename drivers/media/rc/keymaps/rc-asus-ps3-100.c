/* asus-ps3-100.h - Keytable for asus_ps3_100 Remote Controller
 *
 * Copyright (c) 2012 by Mauro Carvalho Chehab
 *
 * Based on a previous patch from Remi Schwartz <remi.schwartz@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>
#include <linux/module.h>

static struct rc_map_table asus_ps3_100[] = {
	{ 0x081c, KEY_HOME },             /* home */
	{ 0x081e, KEY_TV },               /* tv */
	{ 0x0803, KEY_TEXT },             /* teletext */
	{ 0x0829, KEY_POWER },            /* close */

	{ 0x080b, KEY_RED },              /* red */
	{ 0x080d, KEY_YELLOW },           /* yellow */
	{ 0x0806, KEY_BLUE },             /* blue */
	{ 0x0807, KEY_GREEN },            /* green */

	/* Keys 0 to 9 */
	{ 0x082a, KEY_0 },
	{ 0x0816, KEY_1 },
	{ 0x0812, KEY_2 },
	{ 0x0814, KEY_3 },
	{ 0x0836, KEY_4 },
	{ 0x0832, KEY_5 },
	{ 0x0834, KEY_6 },
	{ 0x080e, KEY_7 },
	{ 0x080a, KEY_8 },
	{ 0x080c, KEY_9 },

	{ 0x0815, KEY_VOLUMEUP },
	{ 0x0826, KEY_VOLUMEDOWN },
	{ 0x0835, KEY_CHANNELUP },        /* channel / program + */
	{ 0x0824, KEY_CHANNELDOWN },      /* channel / program - */

	{ 0x0808, KEY_UP },
	{ 0x0804, KEY_DOWN },
	{ 0x0818, KEY_LEFT },
	{ 0x0810, KEY_RIGHT },
	{ 0x0825, KEY_ENTER },            /* enter */

	{ 0x0822, KEY_EXIT },             /* back */
	{ 0x082c, KEY_AB },               /* recall */

	{ 0x0820, KEY_AUDIO },            /* TV audio */
	{ 0x0837, KEY_SCREEN },           /* snapshot */
	{ 0x082e, KEY_ZOOM },             /* full screen */
	{ 0x0802, KEY_MUTE },             /* mute */

	{ 0x0831, KEY_REWIND },           /* backward << */
	{ 0x0811, KEY_RECORD },           /* recording */
	{ 0x0809, KEY_STOP },
	{ 0x0805, KEY_FASTFORWARD },      /* forward >> */
	{ 0x0821, KEY_PREVIOUS },         /* rew */
	{ 0x081a, KEY_PAUSE },            /* pause */
	{ 0x0839, KEY_PLAY },             /* play */
	{ 0x0819, KEY_NEXT },             /* forward */
};

static struct rc_map_list asus_ps3_100_map = {
.map = {
	.scan     = asus_ps3_100,
	.size     = ARRAY_SIZE(asus_ps3_100),
	.rc_proto = RC_PROTO_RC5,
	.name     = RC_MAP_ASUS_PS3_100,
}
};

static int __init init_rc_map_asus_ps3_100(void)
{
return rc_map_register(&asus_ps3_100_map);
}

static void __exit exit_rc_map_asus_ps3_100(void)
{
rc_map_unregister(&asus_ps3_100_map);
}

module_init(init_rc_map_asus_ps3_100)
module_exit(exit_rc_map_asus_ps3_100)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
