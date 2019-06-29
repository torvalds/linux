// SPDX-License-Identifier: GPL-2.0-only
/* DVB USB library compliant Linux driver for the WideView/ Yakumo/ Hama/
 * Typhoon/ Yuan/ Miglia DVB-T USB2.0 receiver.
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@posteo.de)
 *
 * Thanks to Steve Chang from WideView for providing support for the WT-220U.
 *
 * see Documentation/media/dvb-drivers/dvb-usb.rst for more information
 */
#include "dtt200u.h"

/* debug */
int dvb_usb_dtt200u_debug;
module_param_named(debug,dvb_usb_dtt200u_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,xfer=2 (or-able))." DVB_USB_DEBUG_STATUS);

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

struct dtt200u_state {
	unsigned char data[80];
};

static int dtt200u_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	struct dtt200u_state *st = d->priv;
	int ret = 0;

	mutex_lock(&d->data_mutex);

	st->data[0] = SET_INIT;

	if (onoff)
		ret = dvb_usb_generic_write(d, st->data, 2);

	mutex_unlock(&d->data_mutex);
	return ret;
}

static int dtt200u_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	struct dvb_usb_device *d = adap->dev;
	struct dtt200u_state *st = d->priv;
	int ret;

	mutex_lock(&d->data_mutex);
	st->data[0] = SET_STREAMING;
	st->data[1] = onoff;

	ret = dvb_usb_generic_write(adap->dev, st->data, 2);
	if (ret < 0)
		goto ret;

	if (onoff)
		goto ret;

	st->data[0] = RESET_PID_FILTER;
	ret = dvb_usb_generic_write(adap->dev, st->data, 1);

ret:
	mutex_unlock(&d->data_mutex);

	return ret;
}

static int dtt200u_pid_filter(struct dvb_usb_adapter *adap, int index, u16 pid, int onoff)
{
	struct dvb_usb_device *d = adap->dev;
	struct dtt200u_state *st = d->priv;
	int ret;

	pid = onoff ? pid : 0;

	mutex_lock(&d->data_mutex);
	st->data[0] = SET_PID_FILTER;
	st->data[1] = index;
	st->data[2] = pid & 0xff;
	st->data[3] = (pid >> 8) & 0x1f;

	ret = dvb_usb_generic_write(adap->dev, st->data, 4);
	mutex_unlock(&d->data_mutex);

	return ret;
}

static int dtt200u_rc_query(struct dvb_usb_device *d)
{
	struct dtt200u_state *st = d->priv;
	u32 scancode;
	int ret;

	mutex_lock(&d->data_mutex);
	st->data[0] = GET_RC_CODE;

	ret = dvb_usb_generic_rw(d, st->data, 1, st->data, 5, 0);
	if (ret < 0)
		goto ret;

	if (st->data[0] == 1) {
		enum rc_proto proto = RC_PROTO_NEC;

		scancode = st->data[1];
		if ((u8) ~st->data[1] != st->data[2]) {
			/* Extended NEC */
			scancode = scancode << 8;
			scancode |= st->data[2];
			proto = RC_PROTO_NECX;
		}
		scancode = scancode << 8;
		scancode |= st->data[3];

		/* Check command checksum is ok */
		if ((u8) ~st->data[3] == st->data[4])
			rc_keydown(d->rc_dev, proto, scancode, 0);
		else
			rc_keyup(d->rc_dev);
	} else if (st->data[0] == 2) {
		rc_repeat(d->rc_dev);
	} else {
		rc_keyup(d->rc_dev);
	}

	if (st->data[0] != 0)
		deb_info("st->data: %*ph\n", 5, st->data);

ret:
	mutex_unlock(&d->data_mutex);
	return ret;
}

static int dtt200u_frontend_attach(struct dvb_usb_adapter *adap)
{
	adap->fe_adap[0].fe = dtt200u_fe_attach(adap->dev);
	return 0;
}

static struct dvb_usb_device_properties dtt200u_properties;
static struct dvb_usb_device_properties wt220u_fc_properties;
static struct dvb_usb_device_properties wt220u_properties;
static struct dvb_usb_device_properties wt220u_zl0353_properties;
static struct dvb_usb_device_properties wt220u_miglia_properties;

static int dtt200u_usb_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	if (0 == dvb_usb_device_init(intf, &dtt200u_properties,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf, &wt220u_properties,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf, &wt220u_fc_properties,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf, &wt220u_zl0353_properties,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf, &wt220u_miglia_properties,
				     THIS_MODULE, NULL, adapter_nr))
		return 0;

	return -ENODEV;
}

static struct usb_device_id dtt200u_usb_table [] = {
	{ USB_DEVICE(USB_VID_WIDEVIEW, USB_PID_DTT200U_COLD) },
	{ USB_DEVICE(USB_VID_WIDEVIEW, USB_PID_DTT200U_WARM) },
	{ USB_DEVICE(USB_VID_WIDEVIEW, USB_PID_WT220U_COLD)  },
	{ USB_DEVICE(USB_VID_WIDEVIEW, USB_PID_WT220U_WARM)  },
	{ USB_DEVICE(USB_VID_WIDEVIEW, USB_PID_WT220U_ZL0353_COLD)  },
	{ USB_DEVICE(USB_VID_WIDEVIEW, USB_PID_WT220U_ZL0353_WARM)  },
	{ USB_DEVICE(USB_VID_WIDEVIEW, USB_PID_WT220U_FC_COLD)  },
	{ USB_DEVICE(USB_VID_WIDEVIEW, USB_PID_WT220U_FC_WARM)  },
	{ USB_DEVICE(USB_VID_WIDEVIEW, USB_PID_WT220U_ZAP250_COLD)  },
	{ USB_DEVICE(USB_VID_MIGLIA, USB_PID_WT220U_ZAP250_COLD)  },
	{ 0 },
};
MODULE_DEVICE_TABLE(usb, dtt200u_usb_table);

static struct dvb_usb_device_properties dtt200u_properties = {
	.usb_ctrl = CYPRESS_FX2,
	.firmware = "dvb-usb-dtt200u-01.fw",

	.size_of_priv     = sizeof(struct dtt200u_state),

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.caps = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_NEED_PID_FILTERING,
			.pid_filter_count = 15,

	.streaming_ctrl  = dtt200u_streaming_ctrl,
	.pid_filter      = dtt200u_pid_filter,
	.frontend_attach = dtt200u_frontend_attach,
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
		}
	},
	.power_ctrl      = dtt200u_power_ctrl,

	.rc.core = {
		.rc_interval     = 300,
		.rc_codes        = RC_MAP_DTT200U,
		.rc_query        = dtt200u_rc_query,
		.allowed_protos  = RC_PROTO_BIT_NEC,
	},

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 1,
	.devices = {
		{ .name = "WideView/Yuan/Yakumo/Hama/Typhoon DVB-T USB2.0 (WT-200U)",
		  .cold_ids = { &dtt200u_usb_table[0], NULL },
		  .warm_ids = { &dtt200u_usb_table[1], NULL },
		},
		{ NULL },
	}
};

static struct dvb_usb_device_properties wt220u_properties = {
	.usb_ctrl = CYPRESS_FX2,
	.firmware = "dvb-usb-wt220u-02.fw",

	.size_of_priv     = sizeof(struct dtt200u_state),

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.caps = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_NEED_PID_FILTERING,
			.pid_filter_count = 15,

	.streaming_ctrl  = dtt200u_streaming_ctrl,
	.pid_filter      = dtt200u_pid_filter,
	.frontend_attach = dtt200u_frontend_attach,
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
		}
	},
	.power_ctrl      = dtt200u_power_ctrl,

	.rc.core = {
		.rc_interval     = 300,
		.rc_codes        = RC_MAP_DTT200U,
		.rc_query        = dtt200u_rc_query,
		.allowed_protos  = RC_PROTO_BIT_NEC,
	},

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 1,
	.devices = {
		{ .name = "WideView WT-220U PenType Receiver (Typhoon/Freecom)",
		  .cold_ids = { &dtt200u_usb_table[2], &dtt200u_usb_table[8], NULL },
		  .warm_ids = { &dtt200u_usb_table[3], NULL },
		},
		{ NULL },
	}
};

static struct dvb_usb_device_properties wt220u_fc_properties = {
	.usb_ctrl = CYPRESS_FX2,
	.firmware = "dvb-usb-wt220u-fc03.fw",

	.size_of_priv     = sizeof(struct dtt200u_state),

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.caps = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_NEED_PID_FILTERING,
			.pid_filter_count = 15,

	.streaming_ctrl  = dtt200u_streaming_ctrl,
	.pid_filter      = dtt200u_pid_filter,
	.frontend_attach = dtt200u_frontend_attach,
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
		}
	},
	.power_ctrl      = dtt200u_power_ctrl,

	.rc.core = {
		.rc_interval     = 300,
		.rc_codes        = RC_MAP_DTT200U,
		.rc_query        = dtt200u_rc_query,
		.allowed_protos  = RC_PROTO_BIT_NEC,
	},

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 1,
	.devices = {
		{ .name = "WideView WT-220U PenType Receiver (Typhoon/Freecom)",
		  .cold_ids = { &dtt200u_usb_table[6], NULL },
		  .warm_ids = { &dtt200u_usb_table[7], NULL },
		},
		{ NULL },
	}
};

static struct dvb_usb_device_properties wt220u_zl0353_properties = {
	.usb_ctrl = CYPRESS_FX2,
	.firmware = "dvb-usb-wt220u-zl0353-01.fw",

	.size_of_priv     = sizeof(struct dtt200u_state),

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.caps = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_NEED_PID_FILTERING,
			.pid_filter_count = 15,

			.streaming_ctrl  = dtt200u_streaming_ctrl,
			.pid_filter      = dtt200u_pid_filter,
			.frontend_attach = dtt200u_frontend_attach,
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
		}
	},
	.power_ctrl      = dtt200u_power_ctrl,

	.rc.core = {
		.rc_interval     = 300,
		.rc_codes        = RC_MAP_DTT200U,
		.rc_query        = dtt200u_rc_query,
		.allowed_protos  = RC_PROTO_BIT_NEC,
	},

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 1,
	.devices = {
		{ .name = "WideView WT-220U PenType Receiver (based on ZL353)",
		  .cold_ids = { &dtt200u_usb_table[4], NULL },
		  .warm_ids = { &dtt200u_usb_table[5], NULL },
		},
		{ NULL },
	}
};

static struct dvb_usb_device_properties wt220u_miglia_properties = {
	.usb_ctrl = CYPRESS_FX2,
	.firmware = "dvb-usb-wt220u-miglia-01.fw",

	.size_of_priv     = sizeof(struct dtt200u_state),

	.num_adapters = 1,
	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 1,
	.devices = {
		{ .name = "WideView WT-220U PenType Receiver (Miglia)",
		  .cold_ids = { &dtt200u_usb_table[9], NULL },
		  /* This device turns into WT220U_ZL0353_WARM when fw
		     has been uploaded */
		  .warm_ids = { NULL },
		},
		{ NULL },
	}
};

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver dtt200u_usb_driver = {
	.name		= "dvb_usb_dtt200u",
	.probe		= dtt200u_usb_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table	= dtt200u_usb_table,
};

module_usb_driver(dtt200u_usb_driver);

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@posteo.de>");
MODULE_DESCRIPTION("Driver for the WideView/Yakumo/Hama/Typhoon/Club3D/Miglia DVB-T USB2.0 devices");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
