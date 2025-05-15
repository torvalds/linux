// SPDX-License-Identifier: GPL-2.0-only
/* DVB USB compliant linux driver for mobile DVB-T USB devices based on
 * reference designs made by DiBcom (http://www.dibcom.fr/) (DiB3000M-C/P)
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@posteo.de)
 *
 * based on GPL code from DiBcom, which has
 * Copyright (C) 2004 Amaury Demol for DiBcom
 *
 * see Documentation/driver-api/media/drivers/dvb-usb.rst for more information
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
enum {
	DIBCOM_MOD3001_COLD,
	DIBCOM_MOD3001_WARM,
	ULTIMA_TVBOX_USB2_COLD,
	ULTIMA_TVBOX_USB2_WARM,
	LITEON_DVB_T_COLD,
	LITEON_DVB_T_WARM,
	EMPIA_DIGIVOX_MINI_SL_COLD,
	EMPIA_DIGIVOX_MINI_SL_WARM,
	GRANDTEC_DVBT_USB2_COLD,
	GRANDTEC_DVBT_USB2_WARM,
	ULTIMA_ARTEC_T14_COLD,
	ULTIMA_ARTEC_T14_WARM,
	LEADTEK_WINFAST_DTV_DONGLE_COLD,
	LEADTEK_WINFAST_DTV_DONGLE_WARM,
	HUMAX_DVB_T_STICK_HIGH_SPEED_COLD,
	HUMAX_DVB_T_STICK_HIGH_SPEED_WARM,
};

static const struct usb_device_id dibusb_dib3000mc_table[] = {
	DVB_USB_DEV(DIBCOM, DIBCOM_MOD3001_COLD),
	DVB_USB_DEV(DIBCOM, DIBCOM_MOD3001_WARM),
	DVB_USB_DEV(ULTIMA_ELECTRONIC, ULTIMA_TVBOX_USB2_COLD),
	DVB_USB_DEV(ULTIMA_ELECTRONIC, ULTIMA_TVBOX_USB2_WARM),
	DVB_USB_DEV(LITEON, LITEON_DVB_T_COLD),
	DVB_USB_DEV(LITEON, LITEON_DVB_T_WARM),
	DVB_USB_DEV(EMPIA, EMPIA_DIGIVOX_MINI_SL_COLD),
	DVB_USB_DEV(EMPIA, EMPIA_DIGIVOX_MINI_SL_WARM),
	DVB_USB_DEV(GRANDTEC, GRANDTEC_DVBT_USB2_COLD),
	DVB_USB_DEV(GRANDTEC, GRANDTEC_DVBT_USB2_WARM),
	DVB_USB_DEV(ULTIMA_ELECTRONIC, ULTIMA_ARTEC_T14_COLD),
	DVB_USB_DEV(ULTIMA_ELECTRONIC, ULTIMA_ARTEC_T14_WARM),
	DVB_USB_DEV(LEADTEK, LEADTEK_WINFAST_DTV_DONGLE_COLD),
	DVB_USB_DEV(LEADTEK, LEADTEK_WINFAST_DTV_DONGLE_WARM),
	DVB_USB_DEV(HUMAX_COEX, HUMAX_DVB_T_STICK_HIGH_SPEED_COLD),
	DVB_USB_DEV(HUMAX_COEX, HUMAX_DVB_T_STICK_HIGH_SPEED_WARM),
	{ }
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
			{ &dibusb_dib3000mc_table[DIBCOM_MOD3001_COLD], NULL },
			{ &dibusb_dib3000mc_table[DIBCOM_MOD3001_WARM], NULL },
		},
		{   "Artec T1 USB2.0 TVBOX (please check the warm ID)",
			{ &dibusb_dib3000mc_table[ULTIMA_TVBOX_USB2_COLD], NULL },
			{ &dibusb_dib3000mc_table[ULTIMA_TVBOX_USB2_WARM], NULL },
		},
		{   "LITE-ON USB2.0 DVB-T Tuner",
		    /* Also rebranded as Intuix S800, Toshiba */
			{ &dibusb_dib3000mc_table[LITEON_DVB_T_COLD], NULL },
			{ &dibusb_dib3000mc_table[LITEON_DVB_T_WARM], NULL },
		},
		{   "MSI Digivox Mini SL",
			{ &dibusb_dib3000mc_table[EMPIA_DIGIVOX_MINI_SL_COLD], NULL },
			{ &dibusb_dib3000mc_table[EMPIA_DIGIVOX_MINI_SL_WARM], NULL },
		},
		{   "GRAND - USB2.0 DVB-T adapter",
			{ &dibusb_dib3000mc_table[GRANDTEC_DVBT_USB2_COLD], NULL },
			{ &dibusb_dib3000mc_table[GRANDTEC_DVBT_USB2_WARM], NULL },
		},
		{   "Artec T14 - USB2.0 DVB-T",
			{ &dibusb_dib3000mc_table[ULTIMA_ARTEC_T14_COLD], NULL },
			{ &dibusb_dib3000mc_table[ULTIMA_ARTEC_T14_WARM], NULL },
		},
		{   "Leadtek - USB2.0 Winfast DTV dongle",
			{ &dibusb_dib3000mc_table[LEADTEK_WINFAST_DTV_DONGLE_COLD], NULL },
			{ &dibusb_dib3000mc_table[LEADTEK_WINFAST_DTV_DONGLE_WARM], NULL },
		},
		{   "Humax/Coex DVB-T USB Stick 2.0 High Speed",
			{ &dibusb_dib3000mc_table[HUMAX_DVB_T_STICK_HIGH_SPEED_COLD], NULL },
			{ &dibusb_dib3000mc_table[HUMAX_DVB_T_STICK_HIGH_SPEED_WARM], NULL },
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
