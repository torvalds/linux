/* DVB USB compliant linux driver for mobile DVB-T USB devices based on
 * reference designs made by DiBcom (http://www.dibcom.fr/) (DiB3000M-C/P)
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@posteo.de)
 *
 * based on GPL code from DiBcom, which has
 * Copyright (C) 2004 Amaury Demol for DiBcom
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/media/dvb-drivers/dvb-usb.rst for more information
 */
#include "dibusb.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

/* USB Driver stuff */
static struct dvb_usb_device_properties dibusb_mc_properties;

static int dibusb_mc_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	return dvb_usb_device_init(intf, &dibusb_mc_properties, THIS_MODULE,
				   NULL, adapter_nr);
}

/* do not change the order of the ID table */
static struct usb_device_id dibusb_dib3000mc_table [] = {
/* 00 */	{ USB_DEVICE(USB_VID_DIBCOM,		USB_PID_DIBCOM_MOD3001_COLD) },
/* 01 */	{ USB_DEVICE(USB_VID_DIBCOM,		USB_PID_DIBCOM_MOD3001_WARM) },
/* 02 */	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC,	USB_PID_ULTIMA_TVBOX_USB2_COLD) },
/* 03 */	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC,	USB_PID_ULTIMA_TVBOX_USB2_WARM) }, // ( ? )
/* 04 */	{ USB_DEVICE(USB_VID_LITEON,		USB_PID_LITEON_DVB_T_COLD) },
/* 05 */	{ USB_DEVICE(USB_VID_LITEON,		USB_PID_LITEON_DVB_T_WARM) },
/* 06 */	{ USB_DEVICE(USB_VID_EMPIA,		USB_PID_DIGIVOX_MINI_SL_COLD) },
/* 07 */	{ USB_DEVICE(USB_VID_EMPIA,		USB_PID_DIGIVOX_MINI_SL_WARM) },
/* 08 */	{ USB_DEVICE(USB_VID_GRANDTEC,          USB_PID_GRANDTEC_DVBT_USB2_COLD) },
/* 09 */	{ USB_DEVICE(USB_VID_GRANDTEC,          USB_PID_GRANDTEC_DVBT_USB2_WARM) },
/* 10 */	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC,	USB_PID_ARTEC_T14_COLD) },
/* 11 */	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC,	USB_PID_ARTEC_T14_WARM) },
/* 12 */	{ USB_DEVICE(USB_VID_LEADTEK,		USB_PID_WINFAST_DTV_DONGLE_COLD) },
/* 13 */	{ USB_DEVICE(USB_VID_LEADTEK,		USB_PID_WINFAST_DTV_DONGLE_WARM) },
/* 14 */	{ USB_DEVICE(USB_VID_HUMAX_COEX,	USB_PID_DVB_T_USB_STICK_HIGH_SPEED_COLD) },
/* 15 */	{ USB_DEVICE(USB_VID_HUMAX_COEX,	USB_PID_DVB_T_USB_STICK_HIGH_SPEED_WARM) },
			{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, dibusb_dib3000mc_table);

static struct dvb_usb_device_properties dibusb_mc_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = CYPRESS_FX2,
	.firmware = "dvb-usb-dibusb-6.0.0.8.fw",

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
				.count = 8,
				.endpoint = 0x06,
				.u = {
					.bulk = {
						.buffersize = 4096,
					}
				}
			},
		}},
			.size_of_priv     = sizeof(struct dibusb_state),
		}
	},
	.power_ctrl       = dibusb2_0_power_ctrl,

	.rc.legacy = {
		.rc_interval      = DEFAULT_RC_INTERVAL,
		.rc_map_table     = rc_map_dibusb_table,
		.rc_map_size      = 111, /* FIXME */
		.rc_query         = dibusb_rc_query,
	},

	.i2c_algo         = &dibusb_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 8,
	.devices = {
		{   "DiBcom USB2.0 DVB-T reference design (MOD3000P)",
			{ &dibusb_dib3000mc_table[0], NULL },
			{ &dibusb_dib3000mc_table[1], NULL },
		},
		{   "Artec T1 USB2.0 TVBOX (please check the warm ID)",
			{ &dibusb_dib3000mc_table[2], NULL },
			{ &dibusb_dib3000mc_table[3], NULL },
		},
		{   "LITE-ON USB2.0 DVB-T Tuner",
		    /* Also rebranded as Intuix S800, Toshiba */
			{ &dibusb_dib3000mc_table[4], NULL },
			{ &dibusb_dib3000mc_table[5], NULL },
		},
		{   "MSI Digivox Mini SL",
			{ &dibusb_dib3000mc_table[6], NULL },
			{ &dibusb_dib3000mc_table[7], NULL },
		},
		{   "GRAND - USB2.0 DVB-T adapter",
			{ &dibusb_dib3000mc_table[8], NULL },
			{ &dibusb_dib3000mc_table[9], NULL },
		},
		{   "Artec T14 - USB2.0 DVB-T",
			{ &dibusb_dib3000mc_table[10], NULL },
			{ &dibusb_dib3000mc_table[11], NULL },
		},
		{   "Leadtek - USB2.0 Winfast DTV dongle",
			{ &dibusb_dib3000mc_table[12], NULL },
			{ &dibusb_dib3000mc_table[13], NULL },
		},
		{   "Humax/Coex DVB-T USB Stick 2.0 High Speed",
			{ &dibusb_dib3000mc_table[14], NULL },
			{ &dibusb_dib3000mc_table[15], NULL },
		},
		{ NULL },
	}
};

static struct usb_driver dibusb_mc_driver = {
	.name		= "dvb_usb_dibusb_mc",
	.probe		= dibusb_mc_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table	= dibusb_dib3000mc_table,
};

module_usb_driver(dibusb_mc_driver);

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@posteo.de>");
MODULE_DESCRIPTION("Driver for DiBcom USB2.0 DVB-T (DiB3000M-C/P based) devices");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
