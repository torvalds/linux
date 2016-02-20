/* DVB USB library compliant Linux driver for the WideView/ Yakumo/ Hama/
 * Typhoon/ Yuan/ Miglia DVB-T USB2.0 receiver.
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@posteo.de)
 *
 * Thanks to Steve Chang from WideView for providing support for the WT-220U.
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#include "dtt200u.h"

/* debug */
int dvb_usb_dtt200u_debug;
module_param_named(debug,dvb_usb_dtt200u_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,xfer=2 (or-able))." DVB_USB_DEBUG_STATUS);

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static int dtt200u_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	u8 b = SET_INIT;

	if (onoff)
		dvb_usb_generic_write(d,&b,2);

	return 0;
}

static int dtt200u_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	u8 b_streaming[2] = { SET_STREAMING, onoff };
	u8 b_rst_pid = RESET_PID_FILTER;

	dvb_usb_generic_write(adap->dev, b_streaming, 2);

	if (onoff == 0)
		dvb_usb_generic_write(adap->dev, &b_rst_pid, 1);
	return 0;
}

static int dtt200u_pid_filter(struct dvb_usb_adapter *adap, int index, u16 pid, int onoff)
{
	u8 b_pid[4];
	pid = onoff ? pid : 0;

	b_pid[0] = SET_PID_FILTER;
	b_pid[1] = index;
	b_pid[2] = pid & 0xff;
	b_pid[3] = (pid >> 8) & 0x1f;

	return dvb_usb_generic_write(adap->dev, b_pid, 4);
}

/* remote control */
/* key list for the tiny remote control (Yakumo, don't know about the others) */
static struct rc_map_table rc_map_dtt200u_table[] = {
	{ 0x8001, KEY_MUTE },
	{ 0x8002, KEY_CHANNELDOWN },
	{ 0x8003, KEY_VOLUMEDOWN },
	{ 0x8004, KEY_1 },
	{ 0x8005, KEY_2 },
	{ 0x8006, KEY_3 },
	{ 0x8007, KEY_4 },
	{ 0x8008, KEY_5 },
	{ 0x8009, KEY_6 },
	{ 0x800a, KEY_7 },
	{ 0x800c, KEY_ZOOM },
	{ 0x800d, KEY_0 },
	{ 0x800e, KEY_SELECT },
	{ 0x8012, KEY_POWER },
	{ 0x801a, KEY_CHANNELUP },
	{ 0x801b, KEY_8 },
	{ 0x801e, KEY_VOLUMEUP },
	{ 0x801f, KEY_9 },
};

static int dtt200u_rc_query(struct dvb_usb_device *d, u32 *event, int *state)
{
	u8 key[5],cmd = GET_RC_CODE;
	dvb_usb_generic_rw(d,&cmd,1,key,5,0);
	dvb_usb_nec_rc_key_to_event(d,key,event,state);
	if (key[0] != 0)
		deb_info("key: %*ph\n", 5, key);
	return 0;
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

	.rc.legacy = {
		.rc_interval     = 300,
		.rc_map_table    = rc_map_dtt200u_table,
		.rc_map_size     = ARRAY_SIZE(rc_map_dtt200u_table),
		.rc_query        = dtt200u_rc_query,
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

	.rc.legacy = {
		.rc_interval     = 300,
		.rc_map_table      = rc_map_dtt200u_table,
		.rc_map_size = ARRAY_SIZE(rc_map_dtt200u_table),
		.rc_query        = dtt200u_rc_query,
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

	.rc.legacy = {
		.rc_interval     = 300,
		.rc_map_table    = rc_map_dtt200u_table,
		.rc_map_size     = ARRAY_SIZE(rc_map_dtt200u_table),
		.rc_query        = dtt200u_rc_query,
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

	.rc.legacy = {
		.rc_interval     = 300,
		.rc_map_table    = rc_map_dtt200u_table,
		.rc_map_size     = ARRAY_SIZE(rc_map_dtt200u_table),
		.rc_query        = dtt200u_rc_query,
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
