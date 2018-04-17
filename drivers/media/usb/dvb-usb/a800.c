/* DVB USB framework compliant Linux driver for the AVerMedia AverTV DVB-T
 * USB2.0 (A800) DVB-T receiver.
 *
 * Copyright (C) 2005 Patrick Boettcher (patrick.boettcher@posteo.de)
 *
 * Thanks to
 *   - AVerMedia who kindly provided information and
 *   - Glen Harris who suffered from my mistakes during development.
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#include "dibusb.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (rc=1 (or-able))." DVB_USB_DEBUG_STATUS);

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

#define deb_rc(args...)   dprintk(debug,0x01,args)

static int a800_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	/* do nothing for the AVerMedia */
	return 0;
}

/* assure to put cold to 0 for iManufacturer == 1 */
static int a800_identify_state(struct usb_device *udev, struct dvb_usb_device_properties *props,
	struct dvb_usb_device_description **desc, int *cold)
{
	*cold = udev->descriptor.iManufacturer != 1;
	return 0;
}

static int a800_rc_query(struct dvb_usb_device *d)
{
	int ret = 0;
	u8 *key = kmalloc(5, GFP_KERNEL);
	if (!key)
		return -ENOMEM;

	if (usb_control_msg(d->udev,usb_rcvctrlpipe(d->udev,0),
				0x04, USB_TYPE_VENDOR | USB_DIR_IN, 0, 0, key, 5,
				2000) != 5) {
		ret = -ENODEV;
		goto out;
	}

	/* Note that extended nec and nec32 are dropped */
	if (key[0] == 1)
		rc_keydown(d->rc_dev, RC_PROTO_NEC,
			   RC_SCANCODE_NEC(key[1], key[3]), 0);
	else if (key[0] == 2)
		rc_repeat(d->rc_dev);
out:
	kfree(key);
	return ret;
}

/* USB Driver stuff */
static struct dvb_usb_device_properties a800_properties;

static int a800_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	return dvb_usb_device_init(intf, &a800_properties,
				   THIS_MODULE, NULL, adapter_nr);
}

/* do not change the order of the ID table */
static struct usb_device_id a800_table [] = {
/* 00 */	{ USB_DEVICE(USB_VID_AVERMEDIA,     USB_PID_AVERMEDIA_DVBT_USB2_COLD) },
/* 01 */	{ USB_DEVICE(USB_VID_AVERMEDIA,     USB_PID_AVERMEDIA_DVBT_USB2_WARM) },
			{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, a800_table);

static struct dvb_usb_device_properties a800_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = CYPRESS_FX2,
	.firmware = "dvb-usb-avertv-a800-02.fw",

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.caps = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
			.pid_filter_count = 32,
			.streaming_ctrl   = dibusb2_0_streaming_ctrl,
			.pid_filter       = dibusb_pid_filter,
			.pid_filter_ctrl  = dibusb_pid_filter_ctrl,

			.frontend_attach  = dibusb_dib3000mc_frontend_attach,
			.tuner_attach     = dibusb_dib3000mc_tuner_attach,

			/* parameter for the MPEG2-data transfer */
					.stream = {
						.type = USB_BULK,
				.count = 7,
				.endpoint = 0x06,
				.u = {
					.bulk = {
						.buffersize = 4096,
					}
				}
			},
		}},
			.size_of_priv     = sizeof(struct dibusb_state),
		},
	},

	.power_ctrl       = a800_power_ctrl,
	.identify_state   = a800_identify_state,

	.rc.core = {
		.rc_interval	= DEFAULT_RC_INTERVAL,
		.rc_codes	= RC_MAP_AVERMEDIA_M135A,
		.module_name	= KBUILD_MODNAME,
		.rc_query	= a800_rc_query,
		.allowed_protos = RC_PROTO_BIT_NEC,
	},

	.i2c_algo         = &dibusb_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,
	.num_device_descs = 1,
	.devices = {
		{   "AVerMedia AverTV DVB-T USB 2.0 (A800)",
			{ &a800_table[0], NULL },
			{ &a800_table[1], NULL },
		},
	}
};

static struct usb_driver a800_driver = {
	.name		= "dvb_usb_a800",
	.probe		= a800_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table	= a800_table,
};

module_usb_driver(a800_driver);

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@posteo.de>");
MODULE_DESCRIPTION("AVerMedia AverTV DVB-T USB 2.0 (A800)");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
