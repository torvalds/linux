// SPDX-License-Identifier: GPL-2.0-only
/* DVB USB compliant linux driver for mobile DVB-T USB devices based on
 * reference designs made by DiBcom (http://www.dibcom.fr/) (DiB3000M-B)
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

static int dib3000mb_i2c_gate_ctrl(struct dvb_frontend* fe, int enable)
{
	struct dvb_usb_adapter *adap = fe->dvb->priv;
	struct dibusb_state *st = adap->priv;

	return st->ops.tuner_pass_ctrl(fe, enable, st->tuner_addr);
}

static int dibusb_dib3000mb_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct dib3000_config demod_cfg;
	struct dibusb_state *st = adap->priv;

	demod_cfg.demod_address = 0x8;

	adap->fe_adap[0].fe = dvb_attach(dib3000mb_attach, &demod_cfg,
					 &adap->dev->i2c_adap, &st->ops);
	if ((adap->fe_adap[0].fe) == NULL)
		return -ENODEV;

	adap->fe_adap[0].fe->ops.i2c_gate_ctrl = dib3000mb_i2c_gate_ctrl;

	return 0;
}

static int dibusb_thomson_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct dibusb_state *st = adap->priv;

	st->tuner_addr = 0x61;

	dvb_attach(dvb_pll_attach, adap->fe_adap[0].fe, 0x61, &adap->dev->i2c_adap,
		   DVB_PLL_TUA6010XS);
	return 0;
}

static int dibusb_panasonic_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct dibusb_state *st = adap->priv;

	st->tuner_addr = 0x60;

	dvb_attach(dvb_pll_attach, adap->fe_adap[0].fe, 0x60, &adap->dev->i2c_adap,
		   DVB_PLL_TDA665X);
	return 0;
}

/* Some of the Artec 1.1 device aren't equipped with the default tuner
 * (Thomson Cable), but with a Panasonic ENV77H11D5.  This function figures
 * this out. */
static int dibusb_tuner_probe_and_attach(struct dvb_usb_adapter *adap)
{
	u8 b[2] = { 0,0 }, b2[1];
	int ret = 0;
	struct i2c_msg msg[2] = {
		{ .flags = 0,        .buf = b,  .len = 2 },
		{ .flags = I2C_M_RD, .buf = b2, .len = 1 },
	};
	struct dibusb_state *st = adap->priv;

	/* the Panasonic sits on I2C addrass 0x60, the Thomson on 0x61 */
	msg[0].addr = msg[1].addr = st->tuner_addr = 0x60;

	if (adap->fe_adap[0].fe->ops.i2c_gate_ctrl)
		adap->fe_adap[0].fe->ops.i2c_gate_ctrl(adap->fe_adap[0].fe, 1);

	if (i2c_transfer(&adap->dev->i2c_adap, msg, 2) != 2) {
		err("tuner i2c write failed.");
		return -EREMOTEIO;
	}

	if (adap->fe_adap[0].fe->ops.i2c_gate_ctrl)
		adap->fe_adap[0].fe->ops.i2c_gate_ctrl(adap->fe_adap[0].fe, 0);

	if (b2[0] == 0xfe) {
		info("This device has the Thomson Cable onboard. Which is default.");
		ret = dibusb_thomson_tuner_attach(adap);
	} else {
		info("This device has the Panasonic ENV77H11D5 onboard.");
		ret = dibusb_panasonic_tuner_attach(adap);
	}

	return ret;
}

/* USB Driver stuff */
static struct dvb_usb_device_properties dibusb1_1_properties;
static struct dvb_usb_device_properties dibusb1_1_an2235_properties;
static struct dvb_usb_device_properties dibusb2_0b_properties;
static struct dvb_usb_device_properties artec_t1_usb2_properties;

static int dibusb_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	if (0 == dvb_usb_device_init(intf, &dibusb1_1_properties,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf, &dibusb1_1_an2235_properties,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf, &dibusb2_0b_properties,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf, &artec_t1_usb2_properties,
				     THIS_MODULE, NULL, adapter_nr))
		return 0;

	return -EINVAL;
}

/* do not change the order of the ID table */
enum {
	WIDEVIEW_DVBT_USB_COLD,
	WIDEVIEW_DVBT_USB_WARM,
	COMPRO_DVBU2000_COLD,
	COMPRO_DVBU2000_WARM,
	COMPRO_DVBU2000_UNK_COLD,
	DIBCOM_MOD3000_COLD,
	DIBCOM_MOD3000_WARM,
	EMPIA_VSTREAM_COLD,
	EMPIA_VSTREAM_WARM,
	GRANDTEC_DVBT_USB_COLD,
	GRANDTEC_DVBT_USB_WARM,
	GRANDTEC_MOD3000_COLD,
	GRANDTEC_MOD3000_WARM,
	UNK_HYPER_PALTEK_COLD,
	UNK_HYPER_PALTEK_WARM,
	VISIONPLUS_VP7041_COLD,
	VISIONPLUS_VP7041_WARM,
	TWINHAN_VP7041_COLD,
	TWINHAN_VP7041_WARM,
	ULTIMA_TVBOX_COLD,
	ULTIMA_TVBOX_WARM,
	ULTIMA_TVBOX_AN2235_COLD,
	ULTIMA_TVBOX_AN2235_WARM,
	ADSTECH_USB2_COLD,
	ADSTECH_USB2_WARM,
	KYE_DVB_T_COLD,
	KYE_DVB_T_WARM,
	KWORLD_VSTREAM_COLD,
	ULTIMA_TVBOX_USB2_COLD,
	ULTIMA_TVBOX_USB2_WARM,
	ULTIMA_TVBOX_ANCHOR_COLD,
};

static const struct usb_device_id dibusb_dib3000mb_table[] = {
	DVB_USB_DEV(WIDEVIEW, WIDEVIEW_DVBT_USB_COLD),
	DVB_USB_DEV(WIDEVIEW, WIDEVIEW_DVBT_USB_WARM),
	DVB_USB_DEV(COMPRO, COMPRO_DVBU2000_COLD),
	DVB_USB_DEV(COMPRO, COMPRO_DVBU2000_WARM),
	DVB_USB_DEV(COMPRO_UNK, COMPRO_DVBU2000_UNK_COLD),
	DVB_USB_DEV(DIBCOM, DIBCOM_MOD3000_COLD),
	DVB_USB_DEV(DIBCOM, DIBCOM_MOD3000_WARM),
	DVB_USB_DEV(EMPIA, EMPIA_VSTREAM_COLD),
	DVB_USB_DEV(EMPIA, EMPIA_VSTREAM_WARM),
	DVB_USB_DEV(GRANDTEC, GRANDTEC_DVBT_USB_COLD),
	DVB_USB_DEV(GRANDTEC, GRANDTEC_DVBT_USB_WARM),
	DVB_USB_DEV(GRANDTEC, GRANDTEC_MOD3000_COLD),
	DVB_USB_DEV(GRANDTEC, GRANDTEC_MOD3000_WARM),
	DVB_USB_DEV(HYPER_PALTEK, UNK_HYPER_PALTEK_COLD),
	DVB_USB_DEV(HYPER_PALTEK, UNK_HYPER_PALTEK_WARM),
	DVB_USB_DEV(VISIONPLUS, VISIONPLUS_VP7041_COLD),
	DVB_USB_DEV(VISIONPLUS, VISIONPLUS_VP7041_WARM),
	DVB_USB_DEV(TWINHAN, TWINHAN_VP7041_COLD),
	DVB_USB_DEV(TWINHAN, TWINHAN_VP7041_WARM),
	DVB_USB_DEV(ULTIMA_ELECTRONIC, ULTIMA_TVBOX_COLD),
	DVB_USB_DEV(ULTIMA_ELECTRONIC, ULTIMA_TVBOX_WARM),
	DVB_USB_DEV(ULTIMA_ELECTRONIC, ULTIMA_TVBOX_AN2235_COLD),
	DVB_USB_DEV(ULTIMA_ELECTRONIC, ULTIMA_TVBOX_AN2235_WARM),
	DVB_USB_DEV(ADSTECH, ADSTECH_USB2_COLD),
	DVB_USB_DEV(ADSTECH, ADSTECH_USB2_WARM),
	DVB_USB_DEV(KYE, KYE_DVB_T_COLD),
	DVB_USB_DEV(KYE, KYE_DVB_T_WARM),
	DVB_USB_DEV(KWORLD, KWORLD_VSTREAM_COLD),
	DVB_USB_DEV(ULTIMA_ELECTRONIC, ULTIMA_TVBOX_USB2_COLD),
	DVB_USB_DEV(ULTIMA_ELECTRONIC, ULTIMA_TVBOX_USB2_WARM),
#ifdef CONFIG_DVB_USB_DIBUSB_MB_FAULTY
	DVB_USB_DEV(ANCHOR, ULTIMA_TVBOX_ANCHOR_COLD),
#endif
	{ }
};

MODULE_DEVICE_TABLE (usb, dibusb_dib3000mb_table);

static struct dvb_usb_device_properties dibusb1_1_properties = {
	.caps =  DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = CYPRESS_AN2135,

	.firmware = "dvb-usb-dibusb-5.0.0.11.fw",

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.caps = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
			.pid_filter_count = 16,

			.streaming_ctrl   = dibusb_streaming_ctrl,
			.pid_filter       = dibusb_pid_filter,
			.pid_filter_ctrl  = dibusb_pid_filter_ctrl,
			.frontend_attach  = dibusb_dib3000mb_frontend_attach,
			.tuner_attach     = dibusb_tuner_probe_and_attach,

			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 7,
				.endpoint = 0x02,
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

	.power_ctrl       = dibusb_power_ctrl,

	.rc.legacy = {
		.rc_interval      = DEFAULT_RC_INTERVAL,
		.rc_map_table     = rc_map_dibusb_table,
		.rc_map_size      = 111, /* wow, that is ugly ... I want to load it to the driver dynamically */
		.rc_query         = dibusb_rc_query,
	},

	.i2c_algo         = &dibusb_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 9,
	.devices = {
		{	"AVerMedia AverTV DVBT USB1.1",
			{ &dibusb_dib3000mb_table[WIDEVIEW_DVBT_USB_COLD],  NULL },
			{ &dibusb_dib3000mb_table[WIDEVIEW_DVBT_USB_WARM],  NULL },
		},
		{	"Compro Videomate DVB-U2000 - DVB-T USB1.1 (please confirm to linux-dvb)",
			{ &dibusb_dib3000mb_table[COMPRO_DVBU2000_COLD], &dibusb_dib3000mb_table[COMPRO_DVBU2000_UNK_COLD], NULL},
			{ &dibusb_dib3000mb_table[COMPRO_DVBU2000_WARM], NULL },
		},
		{	"DiBcom USB1.1 DVB-T reference design (MOD3000)",
			{ &dibusb_dib3000mb_table[DIBCOM_MOD3000_COLD],  NULL },
			{ &dibusb_dib3000mb_table[DIBCOM_MOD3000_WARM],  NULL },
		},
		{	"KWorld V-Stream XPERT DTV - DVB-T USB1.1",
			{ &dibusb_dib3000mb_table[EMPIA_VSTREAM_COLD], NULL },
			{ &dibusb_dib3000mb_table[EMPIA_VSTREAM_WARM], NULL },
		},
		{	"Grandtec USB1.1 DVB-T",
			{ &dibusb_dib3000mb_table[GRANDTEC_DVBT_USB_COLD],  &dibusb_dib3000mb_table[GRANDTEC_MOD3000_COLD], NULL },
			{ &dibusb_dib3000mb_table[GRANDTEC_DVBT_USB_WARM], &dibusb_dib3000mb_table[GRANDTEC_MOD3000_WARM], NULL },
		},
		{	"Unknown USB1.1 DVB-T device ???? please report the name to the author",
			{ &dibusb_dib3000mb_table[UNK_HYPER_PALTEK_COLD], NULL },
			{ &dibusb_dib3000mb_table[UNK_HYPER_PALTEK_WARM], NULL },
		},
		{	"TwinhanDTV USB-Ter USB1.1 / Magic Box I / HAMA USB1.1 DVB-T device",
			{ &dibusb_dib3000mb_table[VISIONPLUS_VP7041_COLD], &dibusb_dib3000mb_table[TWINHAN_VP7041_COLD], NULL},
			{ &dibusb_dib3000mb_table[VISIONPLUS_VP7041_WARM], &dibusb_dib3000mb_table[TWINHAN_VP7041_WARM], NULL},
		},
		{	"Artec T1 USB1.1 TVBOX with AN2135",
			{ &dibusb_dib3000mb_table[ULTIMA_TVBOX_COLD], NULL },
			{ &dibusb_dib3000mb_table[ULTIMA_TVBOX_WARM], NULL },
		},
		{	"VideoWalker DVB-T USB",
			{ &dibusb_dib3000mb_table[KYE_DVB_T_COLD], NULL },
			{ &dibusb_dib3000mb_table[KYE_DVB_T_WARM], NULL },
		},
	}
};

static struct dvb_usb_device_properties dibusb1_1_an2235_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,
	.usb_ctrl = CYPRESS_AN2235,

	.firmware = "dvb-usb-dibusb-an2235-01.fw",

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.caps = DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF | DVB_USB_ADAP_HAS_PID_FILTER,
			.pid_filter_count = 16,

			.streaming_ctrl   = dibusb_streaming_ctrl,
			.pid_filter       = dibusb_pid_filter,
			.pid_filter_ctrl  = dibusb_pid_filter_ctrl,
			.frontend_attach  = dibusb_dib3000mb_frontend_attach,
			.tuner_attach     = dibusb_tuner_probe_and_attach,

			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 7,
				.endpoint = 0x02,
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
	.power_ctrl       = dibusb_power_ctrl,

	.rc.legacy = {
		.rc_interval      = DEFAULT_RC_INTERVAL,
		.rc_map_table     = rc_map_dibusb_table,
		.rc_map_size      = 111, /* wow, that is ugly ... I want to load it to the driver dynamically */
		.rc_query         = dibusb_rc_query,
	},

	.i2c_algo         = &dibusb_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,

#ifdef CONFIG_DVB_USB_DIBUSB_MB_FAULTY
	.num_device_descs = 2,
#else
	.num_device_descs = 1,
#endif
	.devices = {
		{	"Artec T1 USB1.1 TVBOX with AN2235",
			{ &dibusb_dib3000mb_table[ULTIMA_TVBOX_AN2235_COLD], NULL },
			{ &dibusb_dib3000mb_table[ULTIMA_TVBOX_AN2235_WARM], NULL },
		},
#ifdef CONFIG_DVB_USB_DIBUSB_MB_FAULTY
		{	"Artec T1 USB1.1 TVBOX with AN2235 (faulty USB IDs)",
			{ &dibusb_dib3000mb_table[ULTIMA_TVBOX_ANCHOR_COLD], NULL },
			{ NULL },
		},
		{ NULL },
#endif
	}
};

static struct dvb_usb_device_properties dibusb2_0b_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = CYPRESS_FX2,

	.firmware = "dvb-usb-adstech-usb2-02.fw",

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.caps = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
			.pid_filter_count = 16,

			.streaming_ctrl   = dibusb2_0_streaming_ctrl,
			.pid_filter       = dibusb_pid_filter,
			.pid_filter_ctrl  = dibusb_pid_filter_ctrl,
			.frontend_attach  = dibusb_dib3000mb_frontend_attach,
			.tuner_attach     = dibusb_thomson_tuner_attach,

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
		}
	},
	.power_ctrl       = dibusb2_0_power_ctrl,

	.rc.legacy = {
		.rc_interval      = DEFAULT_RC_INTERVAL,
		.rc_map_table     = rc_map_dibusb_table,
		.rc_map_size      = 111, /* wow, that is ugly ... I want to load it to the driver dynamically */
		.rc_query         = dibusb_rc_query,
	},

	.i2c_algo         = &dibusb_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 2,
	.devices = {
		{	"KWorld/ADSTech Instant DVB-T USB2.0",
			{ &dibusb_dib3000mb_table[ADSTECH_USB2_COLD], NULL },
			{ &dibusb_dib3000mb_table[ADSTECH_USB2_WARM], NULL },
		},
		{	"KWorld Xpert DVB-T USB2.0",
			{ &dibusb_dib3000mb_table[KWORLD_VSTREAM_COLD], NULL },
			{ NULL }
		},
		{ NULL },
	}
};

static struct dvb_usb_device_properties artec_t1_usb2_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = CYPRESS_FX2,

	.firmware = "dvb-usb-dibusb-6.0.0.8.fw",

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.caps = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
			.pid_filter_count = 16,

			.streaming_ctrl   = dibusb2_0_streaming_ctrl,
			.pid_filter       = dibusb_pid_filter,
			.pid_filter_ctrl  = dibusb_pid_filter_ctrl,
			.frontend_attach  = dibusb_dib3000mb_frontend_attach,
			.tuner_attach     = dibusb_tuner_probe_and_attach,
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
		}
	},
	.power_ctrl       = dibusb2_0_power_ctrl,

	.rc.legacy = {
		.rc_interval      = DEFAULT_RC_INTERVAL,
		.rc_map_table     = rc_map_dibusb_table,
		.rc_map_size      = 111, /* wow, that is ugly ... I want to load it to the driver dynamically */
		.rc_query         = dibusb_rc_query,
	},

	.i2c_algo         = &dibusb_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 1,
	.devices = {
		{	"Artec T1 USB2.0",
			{ &dibusb_dib3000mb_table[ULTIMA_TVBOX_USB2_COLD], NULL },
			{ &dibusb_dib3000mb_table[ULTIMA_TVBOX_USB2_WARM], NULL },
		},
		{ NULL },
	}
};

static struct usb_driver dibusb_driver = {
	.name		= "dvb_usb_dibusb_mb",
	.probe		= dibusb_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table	= dibusb_dib3000mb_table,
};

module_usb_driver(dibusb_driver);

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@posteo.de>");
MODULE_DESCRIPTION("Driver for DiBcom USB DVB-T devices (DiB3000M-B based)");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
