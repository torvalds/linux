// SPDX-License-Identifier: GPL-2.0-only
/* DVB USB compliant Linux driver for the
 *  - TwinhanDTV Alpha/MagicBoxII USB2.0 DVB-T receiver
 *  - DigitalNow TinyUSB2 DVB-t receiver
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@posteo.de)
 *
 * Thanks to Twinhan who kindly provided hardware and information.
 *
 * see Documentation/driver-api/media/drivers/dvb-usb.rst for more information
 */
#include "vp7045.h"

/* debug */
static int dvb_usb_vp7045_debug;
module_param_named(debug,dvb_usb_vp7045_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,xfer=2,rc=4 (or-able))." DVB_USB_DEBUG_STATUS);

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

#define deb_info(args...) dprintk(dvb_usb_vp7045_debug,0x01,args)
#define deb_xfer(args...) dprintk(dvb_usb_vp7045_debug,0x02,args)
#define deb_rc(args...)   dprintk(dvb_usb_vp7045_debug,0x04,args)

int vp7045_usb_op(struct dvb_usb_device *d, u8 cmd, u8 *out, int outlen, u8 *in, int inlen, int msec)
{
	int ret = 0;
	u8 *buf = d->priv;

	buf[0] = cmd;

	if (outlen > 19)
		outlen = 19;

	if (inlen > 11)
		inlen = 11;

	ret = mutex_lock_interruptible(&d->usb_mutex);
	if (ret)
		return ret;

	if (out != NULL && outlen > 0)
		memcpy(&buf[1], out, outlen);

	deb_xfer("out buffer: ");
	debug_dump(buf, outlen+1, deb_xfer);


	if (usb_control_msg(d->udev,
			usb_sndctrlpipe(d->udev,0),
			TH_COMMAND_OUT, USB_TYPE_VENDOR | USB_DIR_OUT, 0, 0,
			buf, 20, 2000) != 20) {
		err("USB control message 'out' went wrong.");
		ret = -EIO;
		goto unlock;
	}

	msleep(msec);

	if (usb_control_msg(d->udev,
			usb_rcvctrlpipe(d->udev,0),
			TH_COMMAND_IN, USB_TYPE_VENDOR | USB_DIR_IN, 0, 0,
			buf, 12, 2000) != 12) {
		err("USB control message 'in' went wrong.");
		ret = -EIO;
		goto unlock;
	}

	deb_xfer("in buffer: ");
	debug_dump(buf, 12, deb_xfer);

	if (in != NULL && inlen > 0)
		memcpy(in, &buf[1], inlen);

unlock:
	mutex_unlock(&d->usb_mutex);

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

static int vp7045_rc_query(struct dvb_usb_device *d)
{
	int ret;
	u8 key;

	ret = vp7045_usb_op(d, RC_VAL_READ, NULL, 0, &key, 1, 20);
	if (ret)
		return ret;

	deb_rc("remote query key: %x\n", key);

	if (key != 0x44) {
		/*
		 * The 8 bit address isn't available, but since the remote uses
		 * address 0 we'll use that. nec repeats are ignored too, even
		 * though the remote sends them.
		 */
		rc_keydown(d->rc_dev, RC_PROTO_NEC, RC_SCANCODE_NEC(0, key), 0);
	}

	return 0;
}

static int vp7045_read_eeprom(struct dvb_usb_device *d,u8 *buf, int len, int offset)
{
	int i, ret;
	u8 v, br[2];
	for (i=0; i < len; i++) {
		v = offset + i;
		ret = vp7045_usb_op(d, GET_EE_VALUE, &v, 1, br, 2, 5);
		if (ret)
			return ret;

		buf[i] = br[1];
	}
	deb_info("VP7045 EEPROM read (offs: %d, len: %d) : ", offset, i);
	debug_dump(buf, i, deb_info);
	return 0;
}

static int vp7045_read_mac_addr(struct dvb_usb_device *d,u8 mac[6])
{
	return vp7045_read_eeprom(d,mac, 6, MAC_0_ADDR);
}

static int vp7045_frontend_attach(struct dvb_usb_adapter *adap)
{
	u8 buf[255] = { 0 };

	vp7045_usb_op(adap->dev,VENDOR_STRING_READ,NULL,0,buf,20,0);
	buf[10] = '\0';
	deb_info("firmware says: %s ",buf);

	vp7045_usb_op(adap->dev,PRODUCT_STRING_READ,NULL,0,buf,20,0);
	buf[10] = '\0';
	deb_info("%s ",buf);

	vp7045_usb_op(adap->dev,FW_VERSION_READ,NULL,0,buf,20,0);
	buf[10] = '\0';
	deb_info("v%s\n",buf);

/*	Dump the EEPROM */
/*	vp7045_read_eeprom(d,buf, 255, FX2_ID_ADDR); */

	adap->fe_adap[0].fe = vp7045_fe_attach(adap->dev);

	return 0;
}

static struct dvb_usb_device_properties vp7045_properties;

static int vp7045_usb_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	return dvb_usb_device_init(intf, &vp7045_properties,
				   THIS_MODULE, NULL, adapter_nr);
}

enum {
	VISIONPLUS_VP7045_COLD,
	VISIONPLUS_VP7045_WARM,
	VISIONPLUS_TINYUSB2_COLD,
	VISIONPLUS_TINYUSB2_WARM,
};

static const struct usb_device_id vp7045_usb_table[] = {
	DVB_USB_DEV(VISIONPLUS, VISIONPLUS_VP7045_COLD),
	DVB_USB_DEV(VISIONPLUS, VISIONPLUS_VP7045_WARM),
	DVB_USB_DEV(VISIONPLUS, VISIONPLUS_TINYUSB2_COLD),
	DVB_USB_DEV(VISIONPLUS, VISIONPLUS_TINYUSB2_WARM),
	{ }
};

MODULE_DEVICE_TABLE(usb, vp7045_usb_table);

static struct dvb_usb_device_properties vp7045_properties = {
	.usb_ctrl = CYPRESS_FX2,
	.firmware = "dvb-usb-vp7045-01.fw",
	.size_of_priv = 20,

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.frontend_attach  = vp7045_frontend_attach,
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
	.power_ctrl       = vp7045_power_ctrl,
	.read_mac_address = vp7045_read_mac_addr,

	.rc.core = {
		.rc_interval	= 400,
		.rc_codes	= RC_MAP_TWINHAN_VP1027_DVBS,
		.module_name    = KBUILD_MODNAME,
		.rc_query	= vp7045_rc_query,
		.allowed_protos = RC_PROTO_BIT_NEC,
		.scancode_mask	= 0xff,
	},

	.num_device_descs = 2,
	.devices = {
		{ .name = "Twinhan USB2.0 DVB-T receiver (TwinhanDTV Alpha/MagicBox II)",
		  .cold_ids = { &vp7045_usb_table[VISIONPLUS_VP7045_COLD], NULL },
		  .warm_ids = { &vp7045_usb_table[VISIONPLUS_VP7045_WARM], NULL },
		},
		{ .name = "DigitalNow TinyUSB 2 DVB-t Receiver",
		  .cold_ids = { &vp7045_usb_table[VISIONPLUS_TINYUSB2_COLD], NULL },
		  .warm_ids = { &vp7045_usb_table[VISIONPLUS_TINYUSB2_WARM], NULL },
		},
		{ NULL },
	}
};

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver vp7045_usb_driver = {
	.name		= "dvb_usb_vp7045",
	.probe		= vp7045_usb_probe,
	.disconnect	= dvb_usb_device_exit,
	.id_table	= vp7045_usb_table,
};

module_usb_driver(vp7045_usb_driver);

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@posteo.de>");
MODULE_DESCRIPTION("Driver for Twinhan MagicBox/Alpha and DNTV tinyUSB2 DVB-T USB2.0");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
