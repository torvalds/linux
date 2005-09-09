/* DVB USB compliant linux driver for mobile DVB-T USB devices based on
 * reference designs made by DiBcom (http://www.dibcom.fr/) (DiB3000M-B)
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

static int dibusb_dib3000mb_frontend_attach(struct dvb_usb_device *d)
{
	struct dib3000_config demod_cfg;
	struct dibusb_state *st = d->priv;

	demod_cfg.demod_address = 0x8;
	demod_cfg.pll_set = dvb_usb_pll_set_i2c;
	demod_cfg.pll_init = dvb_usb_pll_init_i2c;

	if ((d->fe = dib3000mb_attach(&demod_cfg,&d->i2c_adap,&st->ops)) == NULL)
		return -ENODEV;

	d->tuner_pass_ctrl = st->ops.tuner_pass_ctrl;

	return 0;
}

static int dibusb_thomson_tuner_attach(struct dvb_usb_device *d)
{
	d->pll_addr = 0x61;
	d->pll_desc = &dvb_pll_tua6010xs;
	return 0;
}

/* Some of the Artec 1.1 device aren't equipped with the default tuner
 * (Thomson Cable), but with a Panasonic ENV77H11D5.  This function figures
 * this out. */
static int dibusb_tuner_probe_and_attach(struct dvb_usb_device *d)
{
	u8 b[2] = { 0,0 }, b2[1];
	int ret = 0;
	struct i2c_msg msg[2] = {
		{ .flags = 0,        .buf = b,  .len = 2 },
		{ .flags = I2C_M_RD, .buf = b2, .len = 1 },
	};

	/* the Panasonic sits on I2C addrass 0x60, the Thomson on 0x61 */
	msg[0].addr = msg[1].addr = 0x60;

	if (d->tuner_pass_ctrl)
		d->tuner_pass_ctrl(d->fe,1,msg[0].addr);

	if (i2c_transfer (&d->i2c_adap, msg, 2) != 2) {
		err("tuner i2c write failed.");
		ret = -EREMOTEIO;
	}

	if (d->tuner_pass_ctrl)
		d->tuner_pass_ctrl(d->fe,0,msg[0].addr);

	if (b2[0] == 0xfe) {
		info("this device has the Thomson Cable onboard. Which is default.");
		dibusb_thomson_tuner_attach(d);
	} else {
		u8 bpll[4] = { 0x0b, 0xf5, 0x85, 0xab };
		info("this device has the Panasonic ENV77H11D5 onboard.");
		d->pll_addr = 0x60;
		memcpy(d->pll_init,bpll,4);
		d->pll_desc = &dvb_pll_tda665x;
	}

	return ret;
}

/* USB Driver stuff */
static struct dvb_usb_properties dibusb1_1_properties;
static struct dvb_usb_properties dibusb1_1_an2235_properties;
static struct dvb_usb_properties dibusb2_0b_properties;

static int dibusb_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	if (dvb_usb_device_init(intf,&dibusb1_1_properties,THIS_MODULE,NULL) == 0 ||
		dvb_usb_device_init(intf,&dibusb1_1_an2235_properties,THIS_MODULE,NULL) == 0 ||
		dvb_usb_device_init(intf,&dibusb2_0b_properties,THIS_MODULE,NULL) == 0)
		return 0;

	return -EINVAL;
}

/* do not change the order of the ID table */
static struct usb_device_id dibusb_dib3000mb_table [] = {
/* 00 */	{ USB_DEVICE(USB_VID_WIDEVIEW,		USB_PID_AVERMEDIA_DVBT_USB_COLD)},
/* 01 */	{ USB_DEVICE(USB_VID_WIDEVIEW,		USB_PID_AVERMEDIA_DVBT_USB_WARM)},
/* 02 */	{ USB_DEVICE(USB_VID_COMPRO,		USB_PID_COMPRO_DVBU2000_COLD) },
/* 03 */	{ USB_DEVICE(USB_VID_COMPRO,		USB_PID_COMPRO_DVBU2000_WARM) },
/* 04 */	{ USB_DEVICE(USB_VID_COMPRO_UNK,	USB_PID_COMPRO_DVBU2000_UNK_COLD) },
/* 05 */	{ USB_DEVICE(USB_VID_DIBCOM,		USB_PID_DIBCOM_MOD3000_COLD) },
/* 06 */	{ USB_DEVICE(USB_VID_DIBCOM,		USB_PID_DIBCOM_MOD3000_WARM) },
/* 07 */	{ USB_DEVICE(USB_VID_EMPIA,			USB_PID_KWORLD_VSTREAM_COLD) },
/* 08 */	{ USB_DEVICE(USB_VID_EMPIA,			USB_PID_KWORLD_VSTREAM_WARM) },
/* 09 */	{ USB_DEVICE(USB_VID_GRANDTEC,		USB_PID_GRANDTEC_DVBT_USB_COLD) },
/* 10 */	{ USB_DEVICE(USB_VID_GRANDTEC,		USB_PID_GRANDTEC_DVBT_USB_WARM) },
/* 11 */	{ USB_DEVICE(USB_VID_GRANDTEC,		USB_PID_DIBCOM_MOD3000_COLD) },
/* 12 */	{ USB_DEVICE(USB_VID_GRANDTEC,		USB_PID_DIBCOM_MOD3000_WARM) },
/* 13 */	{ USB_DEVICE(USB_VID_HYPER_PALTEK,	USB_PID_UNK_HYPER_PALTEK_COLD) },
/* 14 */	{ USB_DEVICE(USB_VID_HYPER_PALTEK,	USB_PID_UNK_HYPER_PALTEK_WARM) },
/* 15 */	{ USB_DEVICE(USB_VID_VISIONPLUS,	USB_PID_TWINHAN_VP7041_COLD) },
/* 16 */	{ USB_DEVICE(USB_VID_VISIONPLUS,	USB_PID_TWINHAN_VP7041_WARM) },
/* 17 */	{ USB_DEVICE(USB_VID_TWINHAN,		USB_PID_TWINHAN_VP7041_COLD) },
/* 18 */	{ USB_DEVICE(USB_VID_TWINHAN,		USB_PID_TWINHAN_VP7041_WARM) },
/* 19 */	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC, USB_PID_ULTIMA_TVBOX_COLD) },
/* 20 */	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC, USB_PID_ULTIMA_TVBOX_WARM) },
/* 21 */	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC, USB_PID_ULTIMA_TVBOX_AN2235_COLD) },
/* 22 */	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC, USB_PID_ULTIMA_TVBOX_AN2235_WARM) },
/* 23 */	{ USB_DEVICE(USB_VID_ADSTECH,		USB_PID_ADSTECH_USB2_COLD) },

/* device ID with default DIBUSB2_0-firmware and with the hacked firmware */
/* 24 */	{ USB_DEVICE(USB_VID_ADSTECH,		USB_PID_ADSTECH_USB2_WARM) },
/* 25 */	{ USB_DEVICE(USB_VID_KYE,			USB_PID_KYE_DVB_T_COLD) },
/* 26 */	{ USB_DEVICE(USB_VID_KYE,			USB_PID_KYE_DVB_T_WARM) },

/* 27 */	{ USB_DEVICE(USB_VID_KWORLD,		USB_PID_KWORLD_VSTREAM_COLD) },

// #define DVB_USB_DIBUSB_MB_FAULTY_USB_IDs

#ifdef DVB_USB_DIBUSB_MB_FAULTY_USB_IDs
/* 28 */	{ USB_DEVICE(USB_VID_ANCHOR,		USB_PID_ULTIMA_TVBOX_ANCHOR_COLD) },
#endif
			{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, dibusb_dib3000mb_table);

static struct dvb_usb_properties dibusb1_1_properties = {
	.caps = DVB_USB_HAS_PID_FILTER | DVB_USB_PID_FILTER_CAN_BE_TURNED_OFF | DVB_USB_IS_AN_I2C_ADAPTER,
	.pid_filter_count = 16,

	.usb_ctrl = CYPRESS_AN2135,

	.firmware = "dvb-usb-dibusb-5.0.0.11.fw",

	.size_of_priv     = sizeof(struct dibusb_state),

	.streaming_ctrl   = dibusb_streaming_ctrl,
	.pid_filter       = dibusb_pid_filter,
	.pid_filter_ctrl  = dibusb_pid_filter_ctrl,
	.power_ctrl       = dibusb_power_ctrl,
	.frontend_attach  = dibusb_dib3000mb_frontend_attach,
	.tuner_attach     = dibusb_tuner_probe_and_attach,

	.rc_interval      = DEFAULT_RC_INTERVAL,
	.rc_key_map       = dibusb_rc_keys,
	.rc_key_map_size  = 63, /* wow, that is ugly ... I want to load it to the driver dynamically */
	.rc_query         = dibusb_rc_query,

	.i2c_algo         = &dibusb_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,
	/* parameter for the MPEG2-data transfer */
	.urb = {
		.type = DVB_USB_BULK,
		.count = 7,
		.endpoint = 0x02,
		.u = {
			.bulk = {
				.buffersize = 4096,
			}
		}
	},

	.num_device_descs = 9,
	.devices = {
		{	"AVerMedia AverTV DVBT USB1.1",
			{ &dibusb_dib3000mb_table[0],  NULL },
			{ &dibusb_dib3000mb_table[1],  NULL },
		},
		{	"Compro Videomate DVB-U2000 - DVB-T USB1.1 (please confirm to linux-dvb)",
			{ &dibusb_dib3000mb_table[2], &dibusb_dib3000mb_table[4], NULL},
			{ &dibusb_dib3000mb_table[3], NULL },
		},
		{	"DiBcom USB1.1 DVB-T reference design (MOD3000)",
			{ &dibusb_dib3000mb_table[5],  NULL },
			{ &dibusb_dib3000mb_table[6],  NULL },
		},
		{	"KWorld V-Stream XPERT DTV - DVB-T USB1.1",
			{ &dibusb_dib3000mb_table[7], NULL },
			{ &dibusb_dib3000mb_table[8], NULL },
		},
		{	"Grandtec USB1.1 DVB-T",
			{ &dibusb_dib3000mb_table[9],  &dibusb_dib3000mb_table[11], NULL },
			{ &dibusb_dib3000mb_table[10], &dibusb_dib3000mb_table[12], NULL },
		},
		{	"Unkown USB1.1 DVB-T device ???? please report the name to the author",
			{ &dibusb_dib3000mb_table[13], NULL },
			{ &dibusb_dib3000mb_table[14], NULL },
		},
		{	"TwinhanDTV USB-Ter USB1.1 / Magic Box I / HAMA USB1.1 DVB-T device",
			{ &dibusb_dib3000mb_table[15], &dibusb_dib3000mb_table[17], NULL},
			{ &dibusb_dib3000mb_table[16], &dibusb_dib3000mb_table[18], NULL},
		},
		{	"Artec T1 USB1.1 TVBOX with AN2135",
			{ &dibusb_dib3000mb_table[19], NULL },
			{ &dibusb_dib3000mb_table[20], NULL },
		},
		{	"VideoWalker DVB-T USB",
			{ &dibusb_dib3000mb_table[25], NULL },
			{ &dibusb_dib3000mb_table[26], NULL },
		},
	}
};

static struct dvb_usb_properties dibusb1_1_an2235_properties = {
	.caps = DVB_USB_HAS_PID_FILTER | DVB_USB_PID_FILTER_CAN_BE_TURNED_OFF | DVB_USB_IS_AN_I2C_ADAPTER,
	.pid_filter_count = 16,

	.usb_ctrl = CYPRESS_AN2235,

	.firmware = "dvb-usb-dibusb-an2235-01.fw",

	.size_of_priv     = sizeof(struct dibusb_state),

	.streaming_ctrl   = dibusb_streaming_ctrl,
	.pid_filter       = dibusb_pid_filter,
	.pid_filter_ctrl  = dibusb_pid_filter_ctrl,
	.power_ctrl       = dibusb_power_ctrl,
	.frontend_attach  = dibusb_dib3000mb_frontend_attach,
	.tuner_attach     = dibusb_tuner_probe_and_attach,

	.rc_interval      = DEFAULT_RC_INTERVAL,
	.rc_key_map       = dibusb_rc_keys,
	.rc_key_map_size  = 63, /* wow, that is ugly ... I want to load it to the driver dynamically */
	.rc_query         = dibusb_rc_query,

	.i2c_algo         = &dibusb_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,
	/* parameter for the MPEG2-data transfer */
	.urb = {
		.type = DVB_USB_BULK,
		.count = 7,
		.endpoint = 0x02,
		.u = {
			.bulk = {
				.buffersize = 4096,
			}
		}
	},

#ifdef DVB_USB_DIBUSB_MB_FAULTY_USB_IDs
	.num_device_descs = 2,
#else
	.num_device_descs = 1,
#endif
	.devices = {
		{	"Artec T1 USB1.1 TVBOX with AN2235",
			{ &dibusb_dib3000mb_table[20], NULL },
			{ &dibusb_dib3000mb_table[21], NULL },
		},
#ifdef DVB_USB_DIBUSB_MB_FAULTY_USB_IDs
		{	"Artec T1 USB1.1 TVBOX with AN2235 (faulty USB IDs)",
			{ &dibusb_dib3000mb_table[28], NULL },
			{ NULL },
		},
#endif
	}
};

static struct dvb_usb_properties dibusb2_0b_properties = {
	.caps = DVB_USB_HAS_PID_FILTER | DVB_USB_PID_FILTER_CAN_BE_TURNED_OFF | DVB_USB_IS_AN_I2C_ADAPTER,
	.pid_filter_count = 32,

	.usb_ctrl = CYPRESS_FX2,

	.firmware = "dvb-usb-adstech-usb2-02.fw",

	.size_of_priv     = sizeof(struct dibusb_state),

	.streaming_ctrl   = dibusb2_0_streaming_ctrl,
	.pid_filter       = dibusb_pid_filter,
	.pid_filter_ctrl  = dibusb_pid_filter_ctrl,
	.power_ctrl       = dibusb2_0_power_ctrl,
	.frontend_attach  = dibusb_dib3000mb_frontend_attach,
	.tuner_attach     = dibusb_thomson_tuner_attach,

	.rc_interval      = DEFAULT_RC_INTERVAL,
	.rc_key_map       = dibusb_rc_keys,
	.rc_key_map_size  = 63, /* wow, that is ugly ... I want to load it to the driver dynamically */
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
		{	"KWorld/ADSTech Instant DVB-T USB2.0",
			{ &dibusb_dib3000mb_table[23], NULL },
			{ &dibusb_dib3000mb_table[24], NULL },
		},
		{	"KWorld Xpert DVB-T USB2.0",
			{ &dibusb_dib3000mb_table[27], NULL },
			{ NULL }
		},
	}
};

static struct usb_driver dibusb_driver = {
	.owner		= THIS_MODULE,
	.name		= "dvb_usb_dibusb_mb",
	.probe		= dibusb_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table	= dibusb_dib3000mb_table,
};

/* module stuff */
static int __init dibusb_module_init(void)
{
	int result;
	if ((result = usb_register(&dibusb_driver))) {
		err("usb_register failed. Error number %d",result);
		return result;
	}

	return 0;
}

static void __exit dibusb_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&dibusb_driver);
}

module_init (dibusb_module_init);
module_exit (dibusb_module_exit);

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@desy.de>");
MODULE_DESCRIPTION("Driver for DiBcom USB DVB-T devices (DiB3000M-B based)");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
