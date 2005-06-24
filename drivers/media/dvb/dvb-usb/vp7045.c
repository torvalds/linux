/* DVB USB compliant Linux driver for the
 *  - TwinhanDTV Alpha/MagicBoxII USB2.0 DVB-T receiver
 *  - DigitalNow TinyUSB2 DVB-t receiver
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 * Thanks to Twinhan who kindly provided hardware and information.
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#include "vp7045.h"

/* debug */
int dvb_usb_vp7045_debug;
module_param_named(debug,dvb_usb_vp7045_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,xfer=2,rc=4 (or-able))." DVB_USB_DEBUG_STATUS);

int vp7045_usb_op(struct dvb_usb_device *d, u8 cmd, u8 *out, int outlen, u8 *in, int inlen, int msec)
{
	int ret = 0;
	u8 inbuf[12] = { 0 }, outbuf[20] = { 0 };

	outbuf[0] = cmd;

	if (outlen > 19)
		outlen = 19;

	if (inlen > 11)
		inlen = 11;

	if (out != NULL && outlen > 0)
		memcpy(&outbuf[1], out, outlen);

	deb_xfer("out buffer: ");
	debug_dump(outbuf,outlen+1,deb_xfer);

	if ((ret = down_interruptible(&d->usb_sem)))
		return ret;

	if (usb_control_msg(d->udev,
			usb_sndctrlpipe(d->udev,0),
			TH_COMMAND_OUT, USB_TYPE_VENDOR | USB_DIR_OUT, 0, 0,
			outbuf, 20, 2*HZ) != 20) {
		err("USB control message 'out' went wrong.");
		ret = -EIO;
		goto unlock;
	}

	msleep(msec);

	if (usb_control_msg(d->udev,
			usb_rcvctrlpipe(d->udev,0),
			TH_COMMAND_IN, USB_TYPE_VENDOR | USB_DIR_IN, 0, 0,
			inbuf, 12, 2*HZ) != 12) {
		err("USB control message 'in' went wrong.");
		ret = -EIO;
		goto unlock;
	}

	deb_xfer("in buffer: ");
	debug_dump(inbuf,12,deb_xfer);

	if (in != NULL && inlen > 0)
		memcpy(in,&inbuf[1],inlen);

unlock:
	up(&d->usb_sem);

	return ret;
}

u8 vp7045_read_reg(struct dvb_usb_device *d, u8 reg)
{
	u8 obuf[2] = { 0 },v;
	obuf[1] = reg;

	vp7045_usb_op(d,TUNER_REG_READ,obuf,2,&v,1,30);

	return v;
}

static int vp7045_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	u8 v = onoff;
	return vp7045_usb_op(d,SET_TUNER_POWER,&v,1,NULL,0,150);
}

/* remote control stuff */

/* The keymapping struct. Somehow this should be loaded to the driver, but
 * currently it is hardcoded. */
static struct dvb_usb_rc_key vp7045_rc_keys[] = {
	/* insert the keys like this. to make the raw keys visible, enable
	 * debug=0x04 when loading dvb-usb-vp7045. */

	/* these keys are probably wrong. I don't have a working IR-receiver on my
	 * vp7045, so I can't test it.  Patches are welcome. */
	{ 0x00, 0x01, KEY_1 },
	{ 0x00, 0x02, KEY_2 },
};

static int vp7045_rc_query(struct dvb_usb_device *d, u32 *key_buf, int *state)
{
	u8 key;
	int i;
	vp7045_usb_op(d,RC_VAL_READ,NULL,0,&key,1,20);

	deb_rc("remote query key: %x %d\n",key,key);

	if (key == 0x44) {
		*state = REMOTE_NO_KEY_PRESSED;
		return 0;
	}

	for (i = 0; i < sizeof(vp7045_rc_keys)/sizeof(struct dvb_usb_rc_key); i++)
		if (vp7045_rc_keys[i].data == key) {
			*state = REMOTE_KEY_PRESSED;
			*key_buf = vp7045_rc_keys[i].event;
			break;
		}
	return 0;
}

static int vp7045_read_eeprom(struct dvb_usb_device *d,u8 *buf, int len, int offset)
{
	int i = 0;
	u8 v,br[2];
	for (i=0; i < len; i++) {
		v = offset + i;
		vp7045_usb_op(d,GET_EE_VALUE,&v,1,br,2,5);
		buf[i] = br[1];
	}
	deb_info("VP7045 EEPROM read (offs: %d, len: %d) : ",offset, i);
	debug_dump(buf,i,deb_info);
	return 0;
}


static int vp7045_read_mac_addr(struct dvb_usb_device *d,u8 mac[6])
{
	return vp7045_read_eeprom(d,mac, 6, MAC_0_ADDR);
}

static int vp7045_frontend_attach(struct dvb_usb_device *d)
{
	u8 buf[255] = { 0 };

	vp7045_usb_op(d,VENDOR_STRING_READ,NULL,0,buf,20,0);
	buf[10] = '\0';
	deb_info("firmware says: %s ",buf);

	vp7045_usb_op(d,PRODUCT_STRING_READ,NULL,0,buf,20,0);
	buf[10] = '\0';
	deb_info("%s ",buf);

	vp7045_usb_op(d,FW_VERSION_READ,NULL,0,buf,20,0);
	buf[10] = '\0';
	deb_info("v%s\n",buf);

/*	Dump the EEPROM */
/*	vp7045_read_eeprom(d,buf, 255, FX2_ID_ADDR); */

	d->fe = vp7045_fe_attach(d);

	return 0;
}

static struct dvb_usb_properties vp7045_properties;

static int vp7045_usb_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	return dvb_usb_device_init(intf,&vp7045_properties,THIS_MODULE);
}

static struct usb_device_id vp7045_usb_table [] = {
	    { USB_DEVICE(USB_VID_VISIONPLUS, USB_PID_TWINHAN_VP7045_COLD) },
	    { USB_DEVICE(USB_VID_VISIONPLUS, USB_PID_TWINHAN_VP7045_WARM) },
	    { USB_DEVICE(USB_VID_VISIONPLUS, USB_PID_DNTV_TINYUSB2_COLD) },
	    { USB_DEVICE(USB_VID_VISIONPLUS, USB_PID_DNTV_TINYUSB2_WARM) },
	    { 0 },
};
MODULE_DEVICE_TABLE(usb, vp7045_usb_table);

static struct dvb_usb_properties vp7045_properties = {
	.caps = 0,

	.usb_ctrl = CYPRESS_FX2,
	.firmware = "dvb-usb-vp7045-01.fw",

	.power_ctrl       = vp7045_power_ctrl,
	.frontend_attach  = vp7045_frontend_attach,
	.read_mac_address = vp7045_read_mac_addr,

	.rc_interval      = 400,
	.rc_key_map       = vp7045_rc_keys,
	.rc_key_map_size  = ARRAY_SIZE(vp7045_rc_keys),
	.rc_query         = vp7045_rc_query,

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
		{ .name = "Twinhan USB2.0 DVB-T receiver (TwinhanDTV Alpha/MagicBox II)",
		  .cold_ids = { &vp7045_usb_table[0], NULL },
		  .warm_ids = { &vp7045_usb_table[1], NULL },
		},
		{ .name = "DigitalNow TinyUSB 2 DVB-t Receiver",
		  .cold_ids = { &vp7045_usb_table[2], NULL },
		  .warm_ids = { &vp7045_usb_table[3], NULL },
		},
		{ 0 },
	}
};

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver vp7045_usb_driver = {
	.owner		= THIS_MODULE,
	.name		= "dvb-usb-vp7045",
	.probe 		= vp7045_usb_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table 	= vp7045_usb_table,
};

/* module stuff */
static int __init vp7045_usb_module_init(void)
{
	int result;
	if ((result = usb_register(&vp7045_usb_driver))) {
		err("usb_register failed. (%d)",result);
		return result;
	}

	return 0;
}

static void __exit vp7045_usb_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&vp7045_usb_driver);
}

module_init(vp7045_usb_module_init);
module_exit(vp7045_usb_module_exit);

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@desy.de>");
MODULE_DESCRIPTION("Driver for Twinhan MagicBox/Alpha and DNTV tinyUSB2 DVB-T USB2.0");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
