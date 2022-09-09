// SPDX-License-Identifier: GPL-2.0-or-later
/* DVB USB compliant Linux driver for the Afatech 9005
 * USB1.1 DVB-T receiver.
 *
 * Standard remote decode function
 *
 * Copyright (C) 2007 Luca Olivetti (luca@ventoso.org)
 *
 * Thanks to Afatech who kindly provided information.
 *
 * see Documentation/driver-api/media/drivers/dvb-usb.rst for more information
 */
#include "af9005.h"
/* debug */
static int dvb_usb_af9005_remote_debug;
module_param_named(debug, dvb_usb_af9005_remote_debug, int, 0644);
MODULE_PARM_DESC(debug,
		 "enable (1) or disable (0) debug messages."
		 DVB_USB_DEBUG_STATUS);

#define deb_decode(args...)   dprintk(dvb_usb_af9005_remote_debug,0x01,args)

struct rc_map_table rc_map_af9005_table[] = {

	{0x01b7, KEY_POWER},
	{0x01a7, KEY_VOLUMEUP},
	{0x0187, KEY_CHANNELUP},
	{0x017f, KEY_MUTE},
	{0x01bf, KEY_VOLUMEDOWN},
	{0x013f, KEY_CHANNELDOWN},
	{0x01df, KEY_1},
	{0x015f, KEY_2},
	{0x019f, KEY_3},
	{0x011f, KEY_4},
	{0x01ef, KEY_5},
	{0x016f, KEY_6},
	{0x01af, KEY_7},
	{0x0127, KEY_8},
	{0x0107, KEY_9},
	{0x01cf, KEY_ZOOM},
	{0x014f, KEY_0},
	{0x018f, KEY_GOTO},	/* marked jump on the remote */

	{0x00bd, KEY_POWER},
	{0x007d, KEY_VOLUMEUP},
	{0x00fd, KEY_CHANNELUP},
	{0x009d, KEY_MUTE},
	{0x005d, KEY_VOLUMEDOWN},
	{0x00dd, KEY_CHANNELDOWN},
	{0x00ad, KEY_1},
	{0x006d, KEY_2},
	{0x00ed, KEY_3},
	{0x008d, KEY_4},
	{0x004d, KEY_5},
	{0x00cd, KEY_6},
	{0x00b5, KEY_7},
	{0x0075, KEY_8},
	{0x00f5, KEY_9},
	{0x0095, KEY_ZOOM},
	{0x0055, KEY_0},
	{0x00d5, KEY_GOTO},	/* marked jump on the remote */
};

int rc_map_af9005_table_size = ARRAY_SIZE(rc_map_af9005_table);

static int repeatable_keys[] = {
	KEY_VOLUMEUP,
	KEY_VOLUMEDOWN,
	KEY_CHANNELUP,
	KEY_CHANNELDOWN
};

int af9005_rc_decode(struct dvb_usb_device *d, u8 * data, int len, u32 * event,
		     int *state)
{
	u16 mark, space;
	u32 result;
	u8 cust, dat, invdat;
	int i;

	if (len >= 6) {
		mark = (u16) (data[0] << 8) + data[1];
		space = (u16) (data[2] << 8) + data[3];
		if (space * 3 < mark) {
			for (i = 0; i < ARRAY_SIZE(repeatable_keys); i++) {
				if (d->last_event == repeatable_keys[i]) {
					*state = REMOTE_KEY_REPEAT;
					*event = d->last_event;
					deb_decode("repeat key, event %x\n",
						   *event);
					return 0;
				}
			}
			deb_decode("repeated key ignored (non repeatable)\n");
			return 0;
		} else if (len >= 33 * 4) {	/*32 bits + start code */
			result = 0;
			for (i = 4; i < 4 + 32 * 4; i += 4) {
				result <<= 1;
				mark = (u16) (data[i] << 8) + data[i + 1];
				mark >>= 1;
				space = (u16) (data[i + 2] << 8) + data[i + 3];
				space >>= 1;
				if (mark * 2 > space)
					result += 1;
			}
			deb_decode("key pressed, raw value %x\n", result);
			if ((result & 0xff000000) != 0xfe000000) {
				deb_decode
				    ("doesn't start with 0xfe, ignored\n");
				return 0;
			}
			cust = (result >> 16) & 0xff;
			dat = (result >> 8) & 0xff;
			invdat = (~result) & 0xff;
			if (dat != invdat) {
				deb_decode("code != inverted code\n");
				return 0;
			}
			for (i = 0; i < rc_map_af9005_table_size; i++) {
				if (rc5_custom(&rc_map_af9005_table[i]) == cust
				    && rc5_data(&rc_map_af9005_table[i]) == dat) {
					*event = rc_map_af9005_table[i].keycode;
					*state = REMOTE_KEY_PRESSED;
					deb_decode
					    ("key pressed, event %x\n", *event);
					return 0;
				}
			}
			deb_decode("not found in table\n");
		}
	}
	return 0;
}

EXPORT_SYMBOL(rc_map_af9005_table);
EXPORT_SYMBOL(rc_map_af9005_table_size);
EXPORT_SYMBOL(af9005_rc_decode);

MODULE_AUTHOR("Luca Olivetti <luca@ventoso.org>");
MODULE_DESCRIPTION
    ("Standard remote control decoder for Afatech 9005 DVB-T USB1.1 stick");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
