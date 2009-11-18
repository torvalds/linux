/*
 * TerraTec Cinergy T2/qanu USB2 DVB-T adapter.
 *
 * Copyright (C) 2007 Tomi Orava (tomimo@ncircle.nullnet.fi)
 *
 * Based on the dvb-usb-framework code and the
 * original Terratec Cinergy T2 driver by:
 *
 * Copyright (C) 2004 Daniel Mack <daniel@qanu.de> and
 *		    Holger Waechtler <holger@qanu.de>
 *
 *  Protocol Spec published on http://qanu.de/specs/terratec_cinergyT2.pdf
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
 */

#include "cinergyT2.h"


/* debug */
int dvb_usb_cinergyt2_debug;

module_param_named(debug, dvb_usb_cinergyt2_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info, xfer=2, rc=4 "
		"(or-able)).");

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

struct cinergyt2_state {
	u8 rc_counter;
};

/* We are missing a release hook with usb_device data */
static struct dvb_usb_device *cinergyt2_usb_device;

static struct dvb_usb_device_properties cinergyt2_properties;

static int cinergyt2_streaming_ctrl(struct dvb_usb_adapter *adap, int enable)
{
	char buf[] = { CINERGYT2_EP1_CONTROL_STREAM_TRANSFER, enable ? 1 : 0 };
	char result[64];
	return dvb_usb_generic_rw(adap->dev, buf, sizeof(buf), result,
				sizeof(result), 0);
}

static int cinergyt2_power_ctrl(struct dvb_usb_device *d, int enable)
{
	char buf[] = { CINERGYT2_EP1_SLEEP_MODE, enable ? 0 : 1 };
	char state[3];
	return dvb_usb_generic_rw(d, buf, sizeof(buf), state, sizeof(state), 0);
}

static int cinergyt2_frontend_attach(struct dvb_usb_adapter *adap)
{
	char query[] = { CINERGYT2_EP1_GET_FIRMWARE_VERSION };
	char state[3];
	int ret;

	adap->fe = cinergyt2_fe_attach(adap->dev);

	ret = dvb_usb_generic_rw(adap->dev, query, sizeof(query), state,
				sizeof(state), 0);
	if (ret < 0) {
		deb_rc("cinergyt2_power_ctrl() Failed to retrieve sleep "
			"state info\n");
	}

	/* Copy this pointer as we are gonna need it in the release phase */
	cinergyt2_usb_device = adap->dev;

	return 0;
}

static struct dvb_usb_rc_key cinergyt2_rc_keys[] = {
	{ 0x0401, KEY_POWER },
	{ 0x0402, KEY_1 },
	{ 0x0403, KEY_2 },
	{ 0x0404, KEY_3 },
	{ 0x0405, KEY_4 },
	{ 0x0406, KEY_5 },
	{ 0x0407, KEY_6 },
	{ 0x0408, KEY_7 },
	{ 0x0409, KEY_8 },
	{ 0x040a, KEY_9 },
	{ 0x040c, KEY_0 },
	{ 0x040b, KEY_VIDEO },
	{ 0x040d, KEY_REFRESH },
	{ 0x040e, KEY_SELECT },
	{ 0x040f, KEY_EPG },
	{ 0x0410, KEY_UP },
	{ 0x0414, KEY_DOWN },
	{ 0x0411, KEY_LEFT },
	{ 0x0413, KEY_RIGHT },
	{ 0x0412, KEY_OK },
	{ 0x0415, KEY_TEXT },
	{ 0x0416, KEY_INFO },
	{ 0x0417, KEY_RED },
	{ 0x0418, KEY_GREEN },
	{ 0x0419, KEY_YELLOW },
	{ 0x041a, KEY_BLUE },
	{ 0x041c, KEY_VOLUMEUP },
	{ 0x041e, KEY_VOLUMEDOWN },
	{ 0x041d, KEY_MUTE },
	{ 0x041b, KEY_CHANNELUP },
	{ 0x041f, KEY_CHANNELDOWN },
	{ 0x0440, KEY_PAUSE },
	{ 0x044c, KEY_PLAY },
	{ 0x0458, KEY_RECORD },
	{ 0x0454, KEY_PREVIOUS },
	{ 0x0448, KEY_STOP },
	{ 0x045c, KEY_NEXT }
};

/* Number of keypresses to ignore before detect repeating */
#define RC_REPEAT_DELAY 3

static int repeatable_keys[] = {
	KEY_UP,
	KEY_DOWN,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_VOLUMEUP,
	KEY_VOLUMEDOWN,
	KEY_CHANNELUP,
	KEY_CHANNELDOWN
};

static int cinergyt2_rc_query(struct dvb_usb_device *d, u32 *event, int *state)
{
	struct cinergyt2_state *st = d->priv;
	u8 key[5] = {0, 0, 0, 0, 0}, cmd = CINERGYT2_EP1_GET_RC_EVENTS;
	int i;

	*state = REMOTE_NO_KEY_PRESSED;

	dvb_usb_generic_rw(d, &cmd, 1, key, sizeof(key), 0);
	if (key[4] == 0xff) {
		/* key repeat */
		st->rc_counter++;
		if (st->rc_counter > RC_REPEAT_DELAY) {
			for (i = 0; i < ARRAY_SIZE(repeatable_keys); i++) {
				if (d->last_event == repeatable_keys[i]) {
					*state = REMOTE_KEY_REPEAT;
					*event = d->last_event;
					deb_rc("repeat key, event %x\n",
						   *event);
					return 0;
				}
			}
			deb_rc("repeated key (non repeatable)\n");
		}
		return 0;
	}

	/* hack to pass checksum on the custom field */
	key[2] = ~key[1];
	dvb_usb_nec_rc_key_to_event(d, key, event, state);
	if (key[0] != 0) {
		if (*event != d->last_event)
			st->rc_counter = 0;

		deb_rc("key: %x %x %x %x %x\n",
		       key[0], key[1], key[2], key[3], key[4]);
	}
	return 0;
}

static int cinergyt2_usb_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	return dvb_usb_device_init(intf, &cinergyt2_properties,
					THIS_MODULE, NULL, adapter_nr);
}


static struct usb_device_id cinergyt2_usb_table[] = {
	{ USB_DEVICE(USB_VID_TERRATEC, 0x0038) },
	{ 0 }
};

MODULE_DEVICE_TABLE(usb, cinergyt2_usb_table);

static struct dvb_usb_device_properties cinergyt2_properties = {
	.size_of_priv = sizeof(struct cinergyt2_state),
	.num_adapters = 1,
	.adapter = {
		{
			.streaming_ctrl   = cinergyt2_streaming_ctrl,
			.frontend_attach  = cinergyt2_frontend_attach,

			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 5,
				.endpoint = 0x02,
				.u = {
					.bulk = {
						.buffersize = 512,
					}
				}
			},
		}
	},

	.power_ctrl       = cinergyt2_power_ctrl,

	.rc_interval      = 50,
	.rc_key_map       = cinergyt2_rc_keys,
	.rc_key_map_size  = ARRAY_SIZE(cinergyt2_rc_keys),
	.rc_query         = cinergyt2_rc_query,

	.generic_bulk_ctrl_endpoint = 1,

	.num_device_descs = 1,
	.devices = {
		{ .name = "TerraTec/qanu USB2.0 Highspeed DVB-T Receiver",
		  .cold_ids = {NULL},
		  .warm_ids = { &cinergyt2_usb_table[0], NULL },
		},
		{ NULL },
	}
};


static struct usb_driver cinergyt2_driver = {
	.name		= "cinergyT2",
	.probe		= cinergyt2_usb_probe,
	.disconnect	= dvb_usb_device_exit,
	.id_table	= cinergyt2_usb_table
};

static int __init cinergyt2_usb_init(void)
{
	int err;

	err = usb_register(&cinergyt2_driver);
	if (err) {
		err("usb_register() failed! (err %i)\n", err);
		return err;
	}
	return 0;
}

static void __exit cinergyt2_usb_exit(void)
{
	usb_deregister(&cinergyt2_driver);
}

module_init(cinergyt2_usb_init);
module_exit(cinergyt2_usb_exit);

MODULE_DESCRIPTION("Terratec Cinergy T2 DVB-T driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tomi Orava");
