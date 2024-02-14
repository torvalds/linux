// SPDX-License-Identifier: GPL-2.0+
// Keytable for Xbox 360 Universal Media remote
// Copyright (c) 2021 Bastien Nocera <hadess@hadess.net>

#include <media/rc-map.h>
#include <linux/module.h>

/*
 * Manual for remote available at:
 * http://download.microsoft.com/download/b/c/e/bce76f3f-db51-4c98-b79d-b3d21e90ccc1/universalmediaremote_na_0609.pdf
 */
static struct rc_map_table xbox_360[] = {
	{KEY_EJECTCD, 0x800f7428},
	{KEY_HOMEPAGE, 0x800f7464},
	{KEY_POWER, 0x800f740c},
	{KEY_STOP, 0x800f7419},
	{KEY_PAUSE, 0x800f7418},
	{KEY_REWIND, 0x800f7415},
	{KEY_FASTFORWARD, 0x800f7414},
	{KEY_PREVIOUS, 0x800f741b},
	{KEY_NEXT, 0x800f741a},
	{KEY_PLAY, 0x800f7416},
	{KEY_PROPS, 0x800f744f}, /* "Display" */
	{KEY_BACK, 0x800f7423},
	{KEY_MEDIA_TOP_MENU, 0x800f7424}, /* "DVD Menu" */
	{KEY_ROOT_MENU, 0x800f7451}, /* "Title" */
	{KEY_INFO, 0x800f740f},
	{KEY_UP, 0x800f741e},
	{KEY_LEFT, 0x800f7420},
	{KEY_RIGHT, 0x800f7421},
	{KEY_DOWN, 0x800f741f},
	{KEY_OK, 0x800f7422},
	{KEY_YELLOW, 0x800f7426},
	{KEY_BLUE, 0x800f7468},
	{KEY_GREEN, 0x800f7466},
	{KEY_RED, 0x800f7425},
	{KEY_VOLUMEUP, 0x800f7410},
	{KEY_VOLUMEDOWN, 0x800f7411},
	/* TV key doesn't light the IR LED */
	{KEY_MUTE, 0x800f740e},
	{KEY_CHANNELUP, 0x800f746c},
	{KEY_CHANNELDOWN, 0x800f746d},
	{KEY_LEFTMETA, 0x800f740d},
	{KEY_ENTER, 0x800f740b},
	{KEY_RECORD, 0x800f7417},
	{KEY_CLEAR, 0x800f740a},
	{KEY_NUMERIC_1, 0x800f7401},
	{KEY_NUMERIC_2, 0x800f7402},
	{KEY_NUMERIC_3, 0x800f7403},
	{KEY_NUMERIC_4, 0x800f7404},
	{KEY_NUMERIC_5, 0x800f7405},
	{KEY_NUMERIC_6, 0x800f7406},
	{KEY_NUMERIC_7, 0x800f7407},
	{KEY_NUMERIC_8, 0x800f7408},
	{KEY_NUMERIC_9, 0x800f7409},
	{KEY_NUMERIC_0, 0x800f7400},
	{KEY_102ND, 0x800f741d}, /* "100" */
	{KEY_CANCEL, 0x800f741c},
};

static struct rc_map_list xbox_360_map = {
	.map = {
		.scan     = xbox_360,
		.size     = ARRAY_SIZE(xbox_360),
		.rc_proto = RC_PROTO_RC6_MCE,
		.name     = RC_MAP_XBOX_360,
	}
};

static int __init init_rc_map(void)
{
	return rc_map_register(&xbox_360_map);
}

static void __exit exit_rc_map(void)
{
	rc_map_unregister(&xbox_360_map);
}

module_init(init_rc_map)
module_exit(exit_rc_map)

MODULE_LICENSE("GPL");
