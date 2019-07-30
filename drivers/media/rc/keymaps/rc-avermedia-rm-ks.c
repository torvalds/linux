// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AverMedia RM-KS remote controller keytable
 *
 * Copyright (C) 2010 Antti Palosaari <crope@iki.fi>
 */

#include <media/rc-map.h>
#include <linux/module.h>

/* Initial keytable is from Jose Alberto Reguero <jareguero@telefonica.net>
   and Felipe Morales Moreno <felipe.morales.moreno@gmail.com> */
/* Keytable fixed by Philippe Valembois <lephilousophe@users.sourceforge.net> */
static struct rc_map_table avermedia_rm_ks[] = {
	{ 0x0501, KEY_POWER2 }, /* Power (RED POWER BUTTON) */
	{ 0x0502, KEY_CHANNELUP }, /* Channel+ */
	{ 0x0503, KEY_CHANNELDOWN }, /* Channel- */
	{ 0x0504, KEY_VOLUMEUP }, /* Volume+ */
	{ 0x0505, KEY_VOLUMEDOWN }, /* Volume- */
	{ 0x0506, KEY_MUTE }, /* Mute */
	{ 0x0507, KEY_AGAIN }, /* Recall */
	{ 0x0508, KEY_VIDEO }, /* Source */
	{ 0x0509, KEY_1 }, /* 1 */
	{ 0x050a, KEY_2 }, /* 2 */
	{ 0x050b, KEY_3 }, /* 3 */
	{ 0x050c, KEY_4 }, /* 4 */
	{ 0x050d, KEY_5 }, /* 5 */
	{ 0x050e, KEY_6 }, /* 6 */
	{ 0x050f, KEY_7 }, /* 7 */
	{ 0x0510, KEY_8 }, /* 8 */
	{ 0x0511, KEY_9 }, /* 9 */
	{ 0x0512, KEY_0 }, /* 0 */
	{ 0x0513, KEY_AUDIO }, /* Audio */
	{ 0x0515, KEY_EPG }, /* EPG */
	{ 0x0516, KEY_PLAYPAUSE }, /* Play/Pause */
	{ 0x0517, KEY_RECORD }, /* Record */
	{ 0x0518, KEY_STOP }, /* Stop */
	{ 0x051c, KEY_BACK }, /* << */
	{ 0x051d, KEY_FORWARD }, /* >> */
	{ 0x054d, KEY_INFO }, /* Display information */
	{ 0x0556, KEY_ZOOM }, /* Fullscreen */
};

static struct rc_map_list avermedia_rm_ks_map = {
	.map = {
		.scan     = avermedia_rm_ks,
		.size     = ARRAY_SIZE(avermedia_rm_ks),
		.rc_proto = RC_PROTO_NEC,
		.name     = RC_MAP_AVERMEDIA_RM_KS,
	}
};

static int __init init_rc_map_avermedia_rm_ks(void)
{
	return rc_map_register(&avermedia_rm_ks_map);
}

static void __exit exit_rc_map_avermedia_rm_ks(void)
{
	rc_map_unregister(&avermedia_rm_ks_map);
}

module_init(init_rc_map_avermedia_rm_ks)
module_exit(exit_rc_map_avermedia_rm_ks)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
