/* DVB USB library compliant Linux driver for the WideView/ Yakumo/ Hama/
 * Typhoon/ Yuan DVB-T USB2.0 receiver.
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
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

static int dtt200u_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	u8 b = SET_INIT;

	if (onoff)
		dvb_usb_generic_write(d,&b,2);

	return 0;
}

static int dtt200u_streaming_ctrl(struct dvb_usb_device *d, int onoff)
{
	u8 b_streaming[2] = { SET_STREAMING, onoff };
	u8 b_rst_pid = RESET_PID_FILTER;

	dvb_usb_generic_write(d,b_streaming,2);

	if (onoff == 0)
		dvb_usb_generic_write(d,&b_rst_pid,1);
	return 0;
}

static int dtt200u_pid_filter(struct dvb_usb_device *d, int index, u16 pid, int onoff)
{
	u8 b_pid[4];
	pid = onoff ? pid : 0;

	b_pid[0] = SET_PID_FILTER;
	b_pid[1] = index;
	b_pid[2] = pid & 0xff;
	b_pid[3] = (pid >> 8) & 0x1f;

	return dvb_usb_generic_write(d,b_pid,4);
}

/* remote control */
/* key list for the tiny remote control (Yakumo, don't know about the others) */
static struct dvb_usb_rc_key dtt200u_rc_keys[] = {
	{ 0x80, 0x01, KEY_MUTE },
	{ 0x80, 0x02, KEY_CHANNELDOWN },
	{ 0x80, 0x03, KEY_VOLUMEDOWN },
	{ 0x80, 0x04, KEY_1 },
	{ 0x80, 0x05, KEY_2 },
	{ 0x80, 0x06, KEY_3 },
	{ 0x80, 0x07, KEY_4 },
	{ 0x80, 0x08, KEY_5 },
	{ 0x80, 0x09, KEY_6 },
	{ 0x80, 0x0a, KEY_7 },
	{ 0x80, 0x0c, KEY_ZOOM },
	{ 0x80, 0x0d, KEY_0 },
	{ 0x80, 0x0e, KEY_SELECT },
	{ 0x80, 0x12, KEY_POWER },
	{ 0x80, 0x1a, KEY_CHANNELUP },
	{ 0x80, 0x1b, KEY_8 },
	{ 0x80, 0x1e, KEY_VOLUMEUP },
	{ 0x80, 0x1f, KEY_9 },
};

static int dtt200u_rc_query(struct dvb_usb_device *d, u32 *event, int *state)
{
	u8 key[5],cmd = GET_RC_CODE;
	dvb_usb_generic_rw(d,&cmd,1,key,5,0);
	dvb_usb_nec_rc_key_to_event(d,key,event,state);
	if (key[0] != 0)
		deb_info("key: %x %x %x %x %x\n",key[0],key[1],key[2],key[3],key[4]);
	return 0;
}

static int dtt200u_frontend_attach(struct dvb_usb_device *d)
{
	d->fe = dtt200u_fe_attach(d);
	return 0;
}

static struct dvb_usb_properties dtt200u_properties;
static struct dvb_usb_properties wt220u_properties;

static int dtt200u_usb_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	if (dvb_usb_device_init(intf,&dtt200u_properties,THIS_MODULE,NULL) == 0 ||
		dvb_usb_device_init(intf,&wt220u_properties,THIS_MODULE,NULL) == 0)
		return 0;

	return -ENODEV;
}

static struct usb_device_id dtt200u_usb_table [] = {
	{ USB_DEVICE(USB_VID_WIDEVIEW, USB_PID_DTT200U_COLD) },
	{ USB_DEVICE(USB_VID_WIDEVIEW, USB_PID_DTT200U_WARM) },
	{ USB_DEVICE(USB_VID_WIDEVIEW, USB_PID_WT220U_COLD)  },
	{ USB_DEVICE(USB_VID_WIDEVIEW, USB_PID_WT220U_WARM)  },
	{ 0 },
};
MODULE_DEVICE_TABLE(usb, dtt200u_usb_table);

static struct dvb_usb_properties dtt200u_properties = {
	.caps = DVB_USB_HAS_PID_FILTER | DVB_USB_NEED_PID_FILTERING,
	.pid_filter_count = 15,

	.usb_ctrl = CYPRESS_FX2,
	.firmware = "dvb-usb-dtt200u-01.fw",

	.power_ctrl      = dtt200u_power_ctrl,
	.streaming_ctrl  = dtt200u_streaming_ctrl,
	.pid_filter      = dtt200u_pid_filter,
	.frontend_attach = dtt200u_frontend_attach,

	.rc_interval     = 300,
	.rc_key_map      = dtt200u_rc_keys,
	.rc_key_map_size = ARRAY_SIZE(dtt200u_rc_keys),
	.rc_query        = dtt200u_rc_query,

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

	.num_device_descs = 1,
	.devices = {
		{ .name = "WideView/Yuan/Yakumo/Hama/Typhoon DVB-T USB2.0 (WT-200U)",
		  .cold_ids = { &dtt200u_usb_table[0], NULL },
		  .warm_ids = { &dtt200u_usb_table[1], NULL },
		},
		{ 0 },
	}
};

static struct dvb_usb_properties wt220u_properties = {
	.caps = DVB_USB_HAS_PID_FILTER | DVB_USB_NEED_PID_FILTERING,
	.pid_filter_count = 15,

	.usb_ctrl = CYPRESS_FX2,
	.firmware = "dvb-usb-wt220u-01.fw",

	.power_ctrl      = dtt200u_power_ctrl,
	.streaming_ctrl  = dtt200u_streaming_ctrl,
	.pid_filter      = dtt200u_pid_filter,
	.frontend_attach = dtt200u_frontend_attach,

	.rc_interval     = 300,
	.rc_key_map      = dtt200u_rc_keys,
	.rc_key_map_size = ARRAY_SIZE(dtt200u_rc_keys),
	.rc_query        = dtt200u_rc_query,

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

	.num_device_descs = 1,
	.devices = {
		{ .name = "WideView WT-220U PenType Receiver (Typhoon/Freecom)",
		  .cold_ids = { &dtt200u_usb_table[2], NULL },
		  .warm_ids = { &dtt200u_usb_table[3], NULL },
		},
		{ 0 },
	}
};

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver dtt200u_usb_driver = {
	.owner		= THIS_MODULE,
	.name		= "dvb_usb_dtt200u",
	.probe		= dtt200u_usb_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table	= dtt200u_usb_table,
};

/* module stuff */
static int __init dtt200u_usb_module_init(void)
{
	int result;
	if ((result = usb_register(&dtt200u_usb_driver))) {
		err("usb_register failed. (%d)",result);
		return result;
	}

	return 0;
}

static void __exit dtt200u_usb_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&dtt200u_usb_driver);
}

module_init(dtt200u_usb_module_init);
module_exit(dtt200u_usb_module_exit);

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@desy.de>");
MODULE_DESCRIPTION("Driver for the WideView/Yakumo/Hama/Typhoon DVB-T USB2.0 devices");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
