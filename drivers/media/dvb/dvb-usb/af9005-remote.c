/* DVB USB compliant Linux driver for the Afatech 9005
 * USB1.1 DVB-T receiver.
 *
 * Standard remote decode function
 *
 * Copyright (C) 2007 Luca Olivetti (luca@ventoso.org)
 *
 * Thanks to Afatech who kindly provided information.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * see Documentation/dvb/REDME.dvb-usb for more information
 */
#include "af9005.h"
/* debug */
static int dvb_usb_af9005_remote_debug;
module_param_named(debug, dvb_usb_af9005_remote_debug, int, 0644);
MODULE_PARM_DESC(debug,
		 "enable (1) or disable (0) debug messages."
		 DVB_USB_DEBUG_STATUS);

#define deb_decode(args...)   dprintk(dvb_usb_af9005_remote_debug,0x01,args)

struct dvb_usb_rc_key af9005_rc_keys[] = {

	{0x01, 0xb7, KEY_POWER},
	{0x01, 0xa7, KEY_VOLUMEUP},
	{0x01, 0x87, KEY_CHANNELUP},
	{0x01, 0x7f, KEY_MUTE},
	{0x01, 0xbf, KEY_VOLUMEDOWN},
	{0x01, 0x3f, KEY_CHANNELDOWN},
	{0x01, 0xdf, KEY_1},
	{0x01, 0x5f, KEY_2},
	{0x01, 0x9f, KEY_3},
	{0x01, 0x1f, KEY_4},
	{0x01, 0xef, KEY_5},
	{0x01, 0x6f, KEY_6},
	{0x01, 0xaf, KEY_7},
	{0x01, 0x27, KEY_8},
	{0x01, 0x07, KEY_9},
	{0x01, 0xcf, KEY_ZOOM},
	{0x01, 0x4f, KEY_0},
	{0x01, 0x8f, KEY_GOTO},	/* marked jump on the remote */

	{0x00, 0xbd, KEY_POWER},
	{0x00, 0x7d, KEY_VOLUMEUP},
	{0x00, 0xfd, KEY_CHANNELUP},
	{0x00, 0x9d, KEY_MUTE},
	{0x00, 0x5d, KEY_VOLUMEDOWN},
	{0x00, 0xdd, KEY_CHANNELDOWN},
	{0x00, 0xad, KEY_1},
	{0x00, 0x6d, KEY_2},
	{0x00, 0xed, KEY_3},
	{0x00, 0x8d, KEY_4},
	{0x00, 0x4d, KEY_5},
	{0x00, 0xcd, KEY_6},
	{0x00, 0xb5, KEY_7},
	{0x00, 0x75, KEY_8},
	{0x00, 0xf5, KEY_9},
	{0x00, 0x95, KEY_ZOOM},
	{0x00, 0x55, KEY_0},
	{0x00, 0xd5, KEY_GOTO},	/* marked jump on the remote */
};

int af9005_rc_keys_size = ARRAY_SIZE(af9005_rc_keys);

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
			for (i = 0; i < af9005_rc_keys_size; i++) {
				if (af9005_rc_keys[i].custom == cust
				    && af9005_rc_keys[i].data == dat) {
					*event = af9005_rc_keys[i].event;
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

EXPORT_SYMBOL(af9005_rc_keys);
EXPORT_SYMBOL(af9005_rc_keys_size);
EXPORT_SYMBOL(af9005_rc_decode);

MODULE_AUTHOR("Luca Olivetti <luca@ventoso.org>");
MODULE_DESCRIPTION
    ("Standard remote control decoder for Afatech 9005 DVB-T USB1.1 stick");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
