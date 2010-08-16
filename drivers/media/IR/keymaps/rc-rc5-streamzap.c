/* rc-rc5-streamzap.c - Keytable for Streamzap PC Remote, for use
 * with the Streamzap PC Remote IR Receiver.
 *
 * Copyright (c) 2010 by Jarod Wilson <jarod@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>

static struct ir_scancode rc5_streamzap[] = {
/*
 * FIXME: The Streamzap remote isn't actually true RC-5, it has an extra
 * bit in it, which presently throws the in-kernel RC-5 decoder for a loop.
 * We either have to enhance the decoder to support it, add a new decoder,
 * or just rely on lirc userspace decoding.
 */
	{ 0x00, KEY_NUMERIC_0 },
	{ 0x01, KEY_NUMERIC_1 },
	{ 0x02, KEY_NUMERIC_2 },
	{ 0x03, KEY_NUMERIC_3 },
	{ 0x04, KEY_NUMERIC_4 },
	{ 0x05, KEY_NUMERIC_5 },
	{ 0x06, KEY_NUMERIC_6 },
	{ 0x07, KEY_NUMERIC_7 },
	{ 0x08, KEY_NUMERIC_8 },
	{ 0x0a, KEY_POWER },
	{ 0x0b, KEY_MUTE },
	{ 0x0c, KEY_CHANNELUP },
	{ 0x0d, KEY_VOLUMEUP },
	{ 0x0e, KEY_CHANNELDOWN },
	{ 0x0f, KEY_VOLUMEDOWN },
	{ 0x10, KEY_UP },
	{ 0x11, KEY_LEFT },
	{ 0x12, KEY_OK },
	{ 0x13, KEY_RIGHT },
	{ 0x14, KEY_DOWN },
	{ 0x15, KEY_MENU },
	{ 0x16, KEY_EXIT },
	{ 0x17, KEY_PLAY },
	{ 0x18, KEY_PAUSE },
	{ 0x19, KEY_STOP },
	{ 0x1a, KEY_BACK },
	{ 0x1b, KEY_FORWARD },
	{ 0x1c, KEY_RECORD },
	{ 0x1d, KEY_REWIND },
	{ 0x1e, KEY_FASTFORWARD },
	{ 0x20, KEY_RED },
	{ 0x21, KEY_GREEN },
	{ 0x22, KEY_YELLOW },
	{ 0x23, KEY_BLUE },

};

static struct rc_keymap rc5_streamzap_map = {
	.map = {
		.scan    = rc5_streamzap,
		.size    = ARRAY_SIZE(rc5_streamzap),
		.ir_type = IR_TYPE_RC5,
		.name    = RC_MAP_RC5_STREAMZAP,
	}
};

static int __init init_rc_map_rc5_streamzap(void)
{
	return ir_register_map(&rc5_streamzap_map);
}

static void __exit exit_rc_map_rc5_streamzap(void)
{
	ir_unregister_map(&rc5_streamzap_map);
}

module_init(init_rc_map_rc5_streamzap)
module_exit(exit_rc_map_rc5_streamzap)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jarod Wilson <jarod@redhat.com>");
