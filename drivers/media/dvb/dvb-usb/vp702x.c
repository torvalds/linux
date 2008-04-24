/* DVB USB compliant Linux driver for the TwinhanDTV StarBox USB2.0 DVB-S
 * receiver.
 *
 * Copyright (C) 2005 Ralph Metzler <rjkm@metzlerbros.de>
 *                    Metzler Brothers Systementwicklung GbR
 *
 * Copyright (C) 2005 Patrick Boettcher <patrick.boettcher@desy.de>
 *
 * Thanks to Twinhan who kindly provided hardware and information.
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#include "vp702x.h"

/* debug */
int dvb_usb_vp702x_debug;
module_param_named(debug,dvb_usb_vp702x_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,xfer=2,rc=4 (or-able))." DVB_USB_DEBUG_STATUS);

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

struct vp702x_state {
	int pid_filter_count;
	int pid_filter_can_bypass;
	u8  pid_filter_state;
};

struct vp702x_device_state {
	u8 power_state;
};

/* check for mutex FIXME */
int vp702x_usb_in_op(struct dvb_usb_device *d, u8 req, u16 value, u16 index, u8 *b, int blen)
{
	int ret = -1;

		ret = usb_control_msg(d->udev,
			usb_rcvctrlpipe(d->udev,0),
			req,
			USB_TYPE_VENDOR | USB_DIR_IN,
			value,index,b,blen,
			2000);

	if (ret < 0) {
		warn("usb in operation failed. (%d)", ret);
		ret = -EIO;
	} else
		ret = 0;


	deb_xfer("in: req. %02x, val: %04x, ind: %04x, buffer: ",req,value,index);
	debug_dump(b,blen,deb_xfer);

	return ret;
}

static int vp702x_usb_out_op(struct dvb_usb_device *d, u8 req, u16 value,
			     u16 index, u8 *b, int blen)
{
	int ret;
	deb_xfer("out: req. %02x, val: %04x, ind: %04x, buffer: ",req,value,index);
	debug_dump(b,blen,deb_xfer);

	if ((ret = usb_control_msg(d->udev,
			usb_sndctrlpipe(d->udev,0),
			req,
			USB_TYPE_VENDOR | USB_DIR_OUT,
			value,index,b,blen,
			2000)) != blen) {
		warn("usb out operation failed. (%d)",ret);
		return -EIO;
	} else
		return 0;
}

int vp702x_usb_inout_op(struct dvb_usb_device *d, u8 *o, int olen, u8 *i, int ilen, int msec)
{
	int ret;

	if ((ret = mutex_lock_interruptible(&d->usb_mutex)))
		return ret;

	ret = vp702x_usb_out_op(d,REQUEST_OUT,0,0,o,olen);
	msleep(msec);
	ret = vp702x_usb_in_op(d,REQUEST_IN,0,0,i,ilen);

	mutex_unlock(&d->usb_mutex);

	return ret;
}

static int vp702x_usb_inout_cmd(struct dvb_usb_device *d, u8 cmd, u8 *o,
				int olen, u8 *i, int ilen, int msec)
{
	u8 bout[olen+2];
	u8 bin[ilen+1];
	int ret = 0;

	bout[0] = 0x00;
	bout[1] = cmd;
	memcpy(&bout[2],o,olen);

	ret = vp702x_usb_inout_op(d, bout, olen+2, bin, ilen+1,msec);

	if (ret == 0)
		memcpy(i,&bin[1],ilen);

	return ret;
}

static int vp702x_set_pld_mode(struct dvb_usb_adapter *adap, u8 bypass)
{
	u8 buf[16] = { 0 };
	return vp702x_usb_in_op(adap->dev, 0xe0, (bypass << 8) | 0x0e, 0, buf, 16);
}

static int vp702x_set_pld_state(struct dvb_usb_adapter *adap, u8 state)
{
	u8 buf[16] = { 0 };
	return vp702x_usb_in_op(adap->dev, 0xe0, (state << 8) | 0x0f, 0, buf, 16);
}

static int vp702x_set_pid(struct dvb_usb_adapter *adap, u16 pid, u8 id, int onoff)
{
	struct vp702x_state *st = adap->priv;
	u8 buf[16] = { 0 };

	if (onoff)
		st->pid_filter_state |=  (1 << id);
	else {
		st->pid_filter_state &= ~(1 << id);
		pid = 0xffff;
	}

	id = 0x10 + id*2;

	vp702x_set_pld_state(adap, st->pid_filter_state);
	vp702x_usb_in_op(adap->dev, 0xe0, (((pid >> 8) & 0xff) << 8) | (id), 0, buf, 16);
	vp702x_usb_in_op(adap->dev, 0xe0, (((pid     ) & 0xff) << 8) | (id+1), 0, buf, 16);
	return 0;
}


static int vp702x_init_pid_filter(struct dvb_usb_adapter *adap)
{
	struct vp702x_state *st = adap->priv;
	int i;
	u8 b[10] = { 0 };

	st->pid_filter_count = 8;
	st->pid_filter_can_bypass = 1;
	st->pid_filter_state = 0x00;

	vp702x_set_pld_mode(adap, 1); // bypass

	for (i = 0; i < st->pid_filter_count; i++)
		vp702x_set_pid(adap, 0xffff, i, 1);

	vp702x_usb_in_op(adap->dev, 0xb5, 3, 0, b, 10);
	vp702x_usb_in_op(adap->dev, 0xb5, 0, 0, b, 10);
	vp702x_usb_in_op(adap->dev, 0xb5, 1, 0, b, 10);

	//vp702x_set_pld_mode(d, 0); // filter
	return 0;
}

static int vp702x_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	return 0;
}

/* keys for the enclosed remote control */
static struct dvb_usb_rc_key vp702x_rc_keys[] = {
	{ 0x00, 0x01, KEY_1 },
	{ 0x00, 0x02, KEY_2 },
};

/* remote control stuff (does not work with my box) */
static int vp702x_rc_query(struct dvb_usb_device *d, u32 *event, int *state)
{
	u8 key[10];
	int i;

/* remove the following return to enabled remote querying */
	return 0;

	vp702x_usb_in_op(d,READ_REMOTE_REQ,0,0,key,10);

	deb_rc("remote query key: %x %d\n",key[1],key[1]);

	if (key[1] == 0x44) {
		*state = REMOTE_NO_KEY_PRESSED;
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(vp702x_rc_keys); i++)
		if (vp702x_rc_keys[i].custom == key[1]) {
			*state = REMOTE_KEY_PRESSED;
			*event = vp702x_rc_keys[i].event;
			break;
		}
	return 0;
}


static int vp702x_read_mac_addr(struct dvb_usb_device *d,u8 mac[6])
{
	u8 i;
	for (i = 6; i < 12; i++)
		vp702x_usb_in_op(d, READ_EEPROM_REQ, i, 1, &mac[i - 6], 1);
	return 0;
}

static int vp702x_frontend_attach(struct dvb_usb_adapter *adap)
{
	u8 buf[10] = { 0 };

	vp702x_usb_out_op(adap->dev, SET_TUNER_POWER_REQ, 0, 7, NULL, 0);

	if (vp702x_usb_inout_cmd(adap->dev, GET_SYSTEM_STRING, NULL, 0, buf, 10, 10))
		return -EIO;

	buf[9] = '\0';
	info("system string: %s",&buf[1]);

	vp702x_init_pid_filter(adap);

	adap->fe = vp702x_fe_attach(adap->dev);
	vp702x_usb_out_op(adap->dev, SET_TUNER_POWER_REQ, 1, 7, NULL, 0);

	return 0;
}

static struct dvb_usb_device_properties vp702x_properties;

static int vp702x_usb_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	return dvb_usb_device_init(intf, &vp702x_properties,
				   THIS_MODULE, NULL, adapter_nr);
}

static struct usb_device_id vp702x_usb_table [] = {
	    { USB_DEVICE(USB_VID_VISIONPLUS, USB_PID_TWINHAN_VP7021_COLD) },
//	    { USB_DEVICE(USB_VID_VISIONPLUS, USB_PID_TWINHAN_VP7020_COLD) },
//	    { USB_DEVICE(USB_VID_VISIONPLUS, USB_PID_TWINHAN_VP7020_WARM) },
	    { 0 },
};
MODULE_DEVICE_TABLE(usb, vp702x_usb_table);

static struct dvb_usb_device_properties vp702x_properties = {
	.usb_ctrl = CYPRESS_FX2,
	.firmware            = "dvb-usb-vp702x-02.fw",
	.no_reconnect        = 1,

	.size_of_priv     = sizeof(struct vp702x_device_state),

	.num_adapters = 1,
	.adapter = {
		{
			.caps             = DVB_USB_ADAP_RECEIVES_204_BYTE_TS,

			.streaming_ctrl   = vp702x_streaming_ctrl,
			.frontend_attach  = vp702x_frontend_attach,

			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 10,
				.endpoint = 0x02,
				.u = {
					.bulk = {
						.buffersize = 4096,
					}
				}
			},
			.size_of_priv     = sizeof(struct vp702x_state),
		}
	},
	.read_mac_address = vp702x_read_mac_addr,

	.rc_key_map       = vp702x_rc_keys,
	.rc_key_map_size  = ARRAY_SIZE(vp702x_rc_keys),
	.rc_interval      = 400,
	.rc_query         = vp702x_rc_query,

	.num_device_descs = 1,
	.devices = {
		{ .name = "TwinhanDTV StarBox DVB-S USB2.0 (VP7021)",
		  .cold_ids = { &vp702x_usb_table[0], NULL },
		  .warm_ids = { NULL },
		},
/*		{ .name = "TwinhanDTV StarBox DVB-S USB2.0 (VP7020)",
		  .cold_ids = { &vp702x_usb_table[2], NULL },
		  .warm_ids = { &vp702x_usb_table[3], NULL },
		},
*/		{ NULL },
	}
};

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver vp702x_usb_driver = {
	.name		= "dvb_usb_vp702x",
	.probe 		= vp702x_usb_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table 	= vp702x_usb_table,
};

/* module stuff */
static int __init vp702x_usb_module_init(void)
{
	int result;
	if ((result = usb_register(&vp702x_usb_driver))) {
		err("usb_register failed. (%d)",result);
		return result;
	}

	return 0;
}

static void __exit vp702x_usb_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&vp702x_usb_driver);
}

module_init(vp702x_usb_module_init);
module_exit(vp702x_usb_module_exit);

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@desy.de>");
MODULE_DESCRIPTION("Driver for Twinhan StarBox DVB-S USB2.0 and clones");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
