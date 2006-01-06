/* DVB USB compliant linux driver for mobile DVB-T USB devices based on
 * reference designs made by DiBcom (http://www.dibcom.fr/) (DiB3000M-C/P)
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 * based on GPL code from DiBcom, which has
 * Copyright (C) 2004 Amaury Demol for DiBcom (ademol@dibcom.fr)
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#include "dibusb.h"

/* USB Driver stuff */
static struct dvb_usb_properties dibusb_mc_properties;

static int dibusb_mc_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	return dvb_usb_device_init(intf,&dibusb_mc_properties,THIS_MODULE,NULL);
}

/* do not change the order of the ID table */
static struct usb_device_id dibusb_dib3000mc_table [] = {
/* 00 */	{ USB_DEVICE(USB_VID_DIBCOM,		USB_PID_DIBCOM_MOD3001_COLD) },
/* 01 */	{ USB_DEVICE(USB_VID_DIBCOM,		USB_PID_DIBCOM_MOD3001_WARM) },
/* 02 */	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC,	USB_PID_ULTIMA_TVBOX_USB2_COLD) },
			{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, dibusb_dib3000mc_table);

static struct dvb_usb_properties dibusb_mc_properties = {
	.caps = DVB_USB_HAS_PID_FILTER | DVB_USB_PID_FILTER_CAN_BE_TURNED_OFF | DVB_USB_IS_AN_I2C_ADAPTER,
	.pid_filter_count = 32,

	.usb_ctrl = CYPRESS_FX2,
	.firmware = "dvb-usb-dibusb-6.0.0.8.fw",

	.size_of_priv     = sizeof(struct dibusb_state),

	.streaming_ctrl   = dibusb2_0_streaming_ctrl,
	.pid_filter       = dibusb_pid_filter,
	.pid_filter_ctrl  = dibusb_pid_filter_ctrl,
	.power_ctrl       = dibusb2_0_power_ctrl,
	.frontend_attach  = dibusb_dib3000mc_frontend_attach,
	.tuner_attach     = dibusb_dib3000mc_tuner_attach,

	.rc_interval      = DEFAULT_RC_INTERVAL,
	.rc_key_map       = dibusb_rc_keys,
	.rc_key_map_size  = 63, /* FIXME */
	.rc_query         = dibusb_rc_query,

	.i2c_algo         = &dibusb_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,
	/* parameter for the MPEG2-data transfer */
	.urb = {
		.type = DVB_USB_BULK,
		.count = 7,
		.endpoint = 0x06,
		.u = {
			.bulk = {
				.buffersize = 4096,
			}
		}
	},

	.num_device_descs = 2,
	.devices = {
		{   "DiBcom USB2.0 DVB-T reference design (MOD3000P)",
			{ &dibusb_dib3000mc_table[0], NULL },
			{ &dibusb_dib3000mc_table[1], NULL },
		},
		{   "Artec T1 USB2.0 TVBOX (please report the warm ID)",
			{ &dibusb_dib3000mc_table[2], NULL },
			{ NULL },
		},
	}
};

static struct usb_driver dibusb_mc_driver = {
	.name		= "dvb_usb_dibusb_mc",
	.probe		= dibusb_mc_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table	= dibusb_dib3000mc_table,
};

/* module stuff */
static int __init dibusb_mc_module_init(void)
{
	int result;
	if ((result = usb_register(&dibusb_mc_driver))) {
		err("usb_register failed. Error number %d",result);
		return result;
	}

	return 0;
}

static void __exit dibusb_mc_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&dibusb_mc_driver);
}

module_init (dibusb_mc_module_init);
module_exit (dibusb_mc_module_exit);

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@desy.de>");
MODULE_DESCRIPTION("Driver for DiBcom USB2.0 DVB-T (DiB3000M-C/P based) devices");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
