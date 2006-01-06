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

struct vp702x_state {
	u8 pid_table[17]; /* [16] controls the pid_table state */
};

/* check for mutex FIXME */
int vp702x_usb_in_op(struct dvb_usb_device *d, u8 req, u16 value, u16 index, u8 *b, int blen)
{
	int ret = 0,try = 0;

	while (ret >= 0 && ret != blen && try < 3) {
		ret = usb_control_msg(d->udev,
			usb_rcvctrlpipe(d->udev,0),
			req,
			USB_TYPE_VENDOR | USB_DIR_IN,
			value,index,b,blen,
			2000);
		deb_info("reading number %d (ret: %d)\n",try,ret);
		try++;
	}

	if (ret < 0 || ret != blen) {
		warn("usb in operation failed.");
		ret = -EIO;
	} else
		ret = 0;

	deb_xfer("in: req. %x, val: %x, ind: %x, buffer: ",req,value,index);
	debug_dump(b,blen,deb_xfer);

	return ret;
}

int vp702x_usb_out_op(struct dvb_usb_device *d, u8 req, u16 value, u16 index, u8 *b, int blen)
{
	deb_xfer("out: req. %x, val: %x, ind: %x, buffer: ",req,value,index);
	debug_dump(b,blen,deb_xfer);

	if (usb_control_msg(d->udev,
			usb_sndctrlpipe(d->udev,0),
			req,
			USB_TYPE_VENDOR | USB_DIR_OUT,
			value,index,b,blen,
			2000) != blen) {
		warn("usb out operation failed.");
		return -EIO;
	} else
		return 0;
}

int vp702x_usb_inout_op(struct dvb_usb_device *d, u8 *o, int olen, u8 *i, int ilen, int msec)
{
	int ret;

	if ((ret = down_interruptible(&d->usb_sem)))
		return ret;

	if ((ret = vp702x_usb_out_op(d,REQUEST_OUT,0,0,o,olen)) < 0)
		goto unlock;
	msleep(msec);
	ret = vp702x_usb_in_op(d,REQUEST_IN,0,0,i,ilen);

unlock:
	up(&d->usb_sem);

	return ret;
}

int vp702x_usb_inout_cmd(struct dvb_usb_device *d, u8 cmd, u8 *o, int olen, u8 *i, int ilen, int msec)
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

static int vp702x_pid_filter(struct dvb_usb_device *d, int index, u16 pid, int onoff)
{
	struct vp702x_state *st = d->priv;
	u8 buf[9];

	if (onoff) {
		st->pid_table[16]   |=   1 << index;
		st->pid_table[index*2]   = (pid >> 8) & 0xff;
		st->pid_table[index*2+1] =  pid       & 0xff;
	} else {
		st->pid_table[16]   &= ~(1 << index);
		st->pid_table[index*2] = st->pid_table[index*2+1] = 0;
	}

	return vp702x_usb_inout_cmd(d,SET_PID_FILTER,st->pid_table,17,buf,9,10);
}

static int vp702x_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	vp702x_usb_in_op(d,RESET_TUNER,0,0,NULL,0);

	vp702x_usb_in_op(d,SET_TUNER_POWER_REQ,0,onoff,NULL,0);
	return vp702x_usb_in_op(d,SET_TUNER_POWER_REQ,0,onoff,NULL,0);
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
	u8 macb[9];
	if (vp702x_usb_inout_cmd(d, GET_MAC_ADDRESS, NULL, 0, macb, 9, 10))
		return -EIO;
	memcpy(mac,&macb[3],6);
	return 0;
}

static int vp702x_frontend_attach(struct dvb_usb_device *d)
{
	u8 buf[9] = { 0 };

	if (vp702x_usb_inout_cmd(d, GET_SYSTEM_STRING, NULL, 0, buf, 9, 10))
		return -EIO;

	buf[8] = '\0';
	info("system string: %s",&buf[1]);

	d->fe = vp702x_fe_attach(d);
	return 0;
}

static struct dvb_usb_properties vp702x_properties;

static int vp702x_usb_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);

	usb_clear_halt(udev,usb_sndctrlpipe(udev,0));
	usb_clear_halt(udev,usb_rcvctrlpipe(udev,0));

	return dvb_usb_device_init(intf,&vp702x_properties,THIS_MODULE,NULL);
}

static struct usb_device_id vp702x_usb_table [] = {
	    { USB_DEVICE(USB_VID_VISIONPLUS, USB_PID_TWINHAN_VP7021_COLD) },
	    { USB_DEVICE(USB_VID_VISIONPLUS, USB_PID_TWINHAN_VP7021_WARM) },
	    { USB_DEVICE(USB_VID_VISIONPLUS, USB_PID_TWINHAN_VP7020_COLD) },
	    { USB_DEVICE(USB_VID_VISIONPLUS, USB_PID_TWINHAN_VP7020_WARM) },
	    { 0 },
};
MODULE_DEVICE_TABLE(usb, vp702x_usb_table);

static struct dvb_usb_properties vp702x_properties = {
	.caps = DVB_USB_HAS_PID_FILTER | DVB_USB_NEED_PID_FILTERING,
	.pid_filter_count = 8, /* !!! */

	.usb_ctrl = CYPRESS_FX2,
	.firmware = "dvb-usb-vp702x-01.fw",

	.pid_filter       = vp702x_pid_filter,
	.power_ctrl       = vp702x_power_ctrl,
	.frontend_attach  = vp702x_frontend_attach,
	.read_mac_address = vp702x_read_mac_addr,

	.rc_key_map       = vp702x_rc_keys,
	.rc_key_map_size  = ARRAY_SIZE(vp702x_rc_keys),
	.rc_interval      = 400,
	.rc_query         = vp702x_rc_query,

	.size_of_priv     = sizeof(struct vp702x_state),

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

	.num_device_descs = 2,
	.devices = {
		{ .name = "TwinhanDTV StarBox DVB-S USB2.0 (VP7021)",
		  .cold_ids = { &vp702x_usb_table[0], NULL },
		  .warm_ids = { &vp702x_usb_table[1], NULL },
		},
		{ .name = "TwinhanDTV StarBox DVB-S USB2.0 (VP7020)",
		  .cold_ids = { &vp702x_usb_table[2], NULL },
		  .warm_ids = { &vp702x_usb_table[3], NULL },
		},
		{ 0 },
	}
};

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver vp702x_usb_driver = {
	.name		= "dvb-usb-vp702x",
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
MODULE_VERSION("1.0-alpha");
MODULE_LICENSE("GPL");
