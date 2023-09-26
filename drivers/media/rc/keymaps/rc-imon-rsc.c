// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (C) 2018 Sean Young <sean@mess.org>

#include <media/rc-map.h>
#include <linux/module.h>

//
// Note that this remote has a stick which its own IR protocol,
// with 16 directions. This is supported by the imon_rsc BPF decoder
// in v4l-utils.
//
static struct rc_map_table imon_rsc[] = {
	{ 0x801010, KEY_EXIT },
	{ 0x80102f, KEY_POWER },
	{ 0x80104a, KEY_SCREENSAVER },	/* Screensaver */
	{ 0x801049, KEY_TIME },		/* Timer */
	{ 0x801054, KEY_NUMERIC_1 },
	{ 0x801055, KEY_NUMERIC_2 },
	{ 0x801056, KEY_NUMERIC_3 },
	{ 0x801057, KEY_NUMERIC_4 },
	{ 0x801058, KEY_NUMERIC_5 },
	{ 0x801059, KEY_NUMERIC_6 },
	{ 0x80105a, KEY_NUMERIC_7 },
	{ 0x80105b, KEY_NUMERIC_8 },
	{ 0x80105c, KEY_NUMERIC_9 },
	{ 0x801081, KEY_SCREEN },	/* Desktop */
	{ 0x80105d, KEY_NUMERIC_0 },
	{ 0x801082, KEY_ZOOM },		/* Maximise */
	{ 0x801048, KEY_ESC },
	{ 0x80104b, KEY_MEDIA },	/* Windows key */
	{ 0x801083, KEY_MENU },
	{ 0x801045, KEY_APPSELECT },	/* app launcher */
	{ 0x801084, KEY_STOP },
	{ 0x801046, KEY_CYCLEWINDOWS },
	{ 0x801085, KEY_BACKSPACE },
	{ 0x801086, KEY_KEYBOARD },
	{ 0x801087, KEY_SPACE },
	{ 0x80101e, KEY_RESERVED },	/* shift tab */
	{ 0x801098, BTN_0 },
	{ 0x80101f, KEY_TAB },
	{ 0x80101b, BTN_LEFT },
	{ 0x80101d, BTN_RIGHT },
	{ 0x801016, BTN_MIDDLE },	/* drag and drop */
	{ 0x801088, KEY_MUTE },
	{ 0x80105e, KEY_VOLUMEDOWN },
	{ 0x80105f, KEY_VOLUMEUP },
	{ 0x80104c, KEY_PLAY },
	{ 0x80104d, KEY_PAUSE },
	{ 0x80104f, KEY_EJECTCD },
	{ 0x801050, KEY_PREVIOUS },
	{ 0x801051, KEY_NEXT },
	{ 0x80104e, KEY_STOP },
	{ 0x801052, KEY_REWIND },
	{ 0x801053, KEY_FASTFORWARD },
	{ 0x801089, KEY_FULL_SCREEN }	/* full screen */
};

static struct rc_map_list imon_rsc_map = {
	.map = {
		.scan     = imon_rsc,
		.size     = ARRAY_SIZE(imon_rsc),
		.rc_proto = RC_PROTO_NECX,
		.name     = RC_MAP_IMON_RSC,
	}
};

static int __init init_rc_map_imon_rsc(void)
{
	return rc_map_register(&imon_rsc_map);
}

static void __exit exit_rc_map_imon_rsc(void)
{
	rc_map_unregister(&imon_rsc_map);
}

module_init(init_rc_map_imon_rsc)
module_exit(exit_rc_map_imon_rsc)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sean Young <sean@mess.org>");
MODULE_DESCRIPTION("iMON RSC remote controller keytable");
