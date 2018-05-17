// SPDX-License-Identifier: GPL-2.0+
// budget-ci-old.h - Keytable for budget_ci_old Remote Controller
//
// keymap imported from ir-keymaps.c
//
// Copyright (c) 2010 by Mauro Carvalho Chehab

#include <media/rc-map.h>
#include <linux/module.h>

/*
 * From reading the following remotes:
 * Zenith Universal 7 / TV Mode 807 / VCR Mode 837
 * Hauppauge (from NOVA-CI-s box product)
 * This is a "middle of the road" approach, differences are noted
 */

static struct rc_map_table budget_ci_old[] = {
	{ 0x00, KEY_0 },
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },
	{ 0x0a, KEY_ENTER },
	{ 0x0b, KEY_RED },
	{ 0x0c, KEY_POWER },		/* RADIO on Hauppauge */
	{ 0x0d, KEY_MUTE },
	{ 0x0f, KEY_A },		/* TV on Hauppauge */
	{ 0x10, KEY_VOLUMEUP },
	{ 0x11, KEY_VOLUMEDOWN },
	{ 0x14, KEY_B },
	{ 0x1c, KEY_UP },
	{ 0x1d, KEY_DOWN },
	{ 0x1e, KEY_OPTION },		/* RESERVED on Hauppauge */
	{ 0x1f, KEY_BREAK },
	{ 0x20, KEY_CHANNELUP },
	{ 0x21, KEY_CHANNELDOWN },
	{ 0x22, KEY_PREVIOUS },		/* Prev Ch on Zenith, SOURCE on Hauppauge */
	{ 0x24, KEY_RESTART },
	{ 0x25, KEY_OK },
	{ 0x26, KEY_CYCLEWINDOWS },	/* MINIMIZE on Hauppauge */
	{ 0x28, KEY_ENTER },		/* VCR mode on Zenith */
	{ 0x29, KEY_PAUSE },
	{ 0x2b, KEY_RIGHT },
	{ 0x2c, KEY_LEFT },
	{ 0x2e, KEY_MENU },		/* FULL SCREEN on Hauppauge */
	{ 0x30, KEY_SLOW },
	{ 0x31, KEY_PREVIOUS },		/* VCR mode on Zenith */
	{ 0x32, KEY_REWIND },
	{ 0x34, KEY_FASTFORWARD },
	{ 0x35, KEY_PLAY },
	{ 0x36, KEY_STOP },
	{ 0x37, KEY_RECORD },
	{ 0x38, KEY_TUNER },		/* TV/VCR on Zenith */
	{ 0x3a, KEY_C },
	{ 0x3c, KEY_EXIT },
	{ 0x3d, KEY_POWER2 },
	{ 0x3e, KEY_TUNER },
};

static struct rc_map_list budget_ci_old_map = {
	.map = {
		.scan     = budget_ci_old,
		.size     = ARRAY_SIZE(budget_ci_old),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_BUDGET_CI_OLD,
	}
};

static int __init init_rc_map_budget_ci_old(void)
{
	return rc_map_register(&budget_ci_old_map);
}

static void __exit exit_rc_map_budget_ci_old(void)
{
	rc_map_unregister(&budget_ci_old_map);
}

module_init(init_rc_map_budget_ci_old)
module_exit(exit_rc_map_budget_ci_old)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
