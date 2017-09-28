/* real-audio-220-32-keys.h - Keytable for real_audio_220_32_keys Remote Controller
 *
 * keymap imported from ir-keymaps.c
 *
 * Copyright (c) 2010 by Mauro Carvalho Chehab
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>
#include <linux/module.h>

/* Zogis Real Audio 220 - 32 keys IR */

static struct rc_map_table real_audio_220_32_keys[] = {
	{ 0x1c, KEY_RADIO},
	{ 0x12, KEY_POWER2},

	{ 0x01, KEY_1},
	{ 0x02, KEY_2},
	{ 0x03, KEY_3},
	{ 0x04, KEY_4},
	{ 0x05, KEY_5},
	{ 0x06, KEY_6},
	{ 0x07, KEY_7},
	{ 0x08, KEY_8},
	{ 0x09, KEY_9},
	{ 0x00, KEY_0},

	{ 0x0c, KEY_VOLUMEUP},
	{ 0x18, KEY_VOLUMEDOWN},
	{ 0x0b, KEY_CHANNELUP},
	{ 0x15, KEY_CHANNELDOWN},
	{ 0x16, KEY_ENTER},

	{ 0x11, KEY_VIDEO},		/* Source */
	{ 0x0d, KEY_AUDIO},		/* stereo */

	{ 0x0f, KEY_PREVIOUS},		/* Prev */
	{ 0x1b, KEY_TIME},		/* Timeshift */
	{ 0x1a, KEY_NEXT},		/* Next */

	{ 0x0e, KEY_STOP},
	{ 0x1f, KEY_PLAY},
	{ 0x1e, KEY_PLAYPAUSE},		/* Pause */

	{ 0x1d, KEY_RECORD},
	{ 0x13, KEY_MUTE},
	{ 0x19, KEY_CAMERA},		/* Snapshot */

};

static struct rc_map_list real_audio_220_32_keys_map = {
	.map = {
		.scan     = real_audio_220_32_keys,
		.size     = ARRAY_SIZE(real_audio_220_32_keys),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_REAL_AUDIO_220_32_KEYS,
	}
};

static int __init init_rc_map_real_audio_220_32_keys(void)
{
	return rc_map_register(&real_audio_220_32_keys_map);
}

static void __exit exit_rc_map_real_audio_220_32_keys(void)
{
	rc_map_unregister(&real_audio_220_32_keys_map);
}

module_init(init_rc_map_real_audio_220_32_keys)
module_exit(exit_rc_map_real_audio_220_32_keys)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
