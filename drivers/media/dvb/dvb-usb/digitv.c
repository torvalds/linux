/* DVB USB compliant linux driver for Nebula Electronics uDigiTV DVB-T USB2.0
 * receiver
 *
 * Copyright (C) 2005 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 * partly based on the SDK published by Nebula Electronics
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#include "digitv.h"

#include "mt352.h"
#include "nxt6000.h"

/* debug */
static int dvb_usb_digitv_debug;
module_param_named(debug,dvb_usb_digitv_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=rc (or-able))." DVB_USB_DEBUG_STATUS);

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

#define deb_rc(args...)   dprintk(dvb_usb_digitv_debug,0x01,args)

static int digitv_ctrl_msg(struct dvb_usb_device *d,
		u8 cmd, u8 vv, u8 *wbuf, int wlen, u8 *rbuf, int rlen)
{
	int wo = (rbuf == NULL || rlen == 0); /* write-only */
	u8 sndbuf[7],rcvbuf[7];
	memset(sndbuf,0,7); memset(rcvbuf,0,7);

	sndbuf[0] = cmd;
	sndbuf[1] = vv;
	sndbuf[2] = wo ? wlen : rlen;

	if (wo) {
		memcpy(&sndbuf[3],wbuf,wlen);
		dvb_usb_generic_write(d,sndbuf,7);
	} else {
		dvb_usb_generic_rw(d,sndbuf,7,rcvbuf,7,10);
		memcpy(rbuf,&rcvbuf[3],rlen);
	}
	return 0;
}

/* I2C */
static int digitv_i2c_xfer(struct i2c_adapter *adap,struct i2c_msg msg[],int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int i;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	if (num > 2)
		warn("more than 2 i2c messages at a time is not handled yet. TODO.");

	for (i = 0; i < num; i++) {
		/* write/read request */
		if (i+1 < num && (msg[i+1].flags & I2C_M_RD)) {
			if (digitv_ctrl_msg(d, USB_READ_COFDM, msg[i].buf[0], NULL, 0,
						msg[i+1].buf,msg[i+1].len) < 0)
				break;
			i++;
		} else
			if (digitv_ctrl_msg(d,USB_WRITE_COFDM, msg[i].buf[0],
						&msg[i].buf[1],msg[i].len-1,NULL,0) < 0)
				break;
	}

	mutex_unlock(&d->i2c_mutex);
	return i;
}

static u32 digitv_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm digitv_i2c_algo = {
	.master_xfer   = digitv_i2c_xfer,
	.functionality = digitv_i2c_func,
};

/* Callbacks for DVB USB */
static int digitv_identify_state (struct usb_device *udev, struct
		dvb_usb_device_properties *props, struct dvb_usb_device_description **desc,
		int *cold)
{
	*cold = udev->descriptor.iManufacturer == 0 && udev->descriptor.iProduct == 0;
	return 0;
}

static int digitv_mt352_demod_init(struct dvb_frontend *fe)
{
	static u8 reset_buf[] = { 0x89, 0x38,  0x8a, 0x2d, 0x50, 0x80 };
	static u8 init_buf[] = { 0x68, 0xa0,  0x8e, 0x40,  0x53, 0x50,
			0x67, 0x20,  0x7d, 0x01,  0x7c, 0x00,  0x7a, 0x00,
			0x79, 0x20,  0x57, 0x05,  0x56, 0x31,  0x88, 0x0f,
			0x75, 0x32 };
	int i;

	for (i = 0; i < ARRAY_SIZE(reset_buf); i += 2)
		mt352_write(fe, &reset_buf[i], 2);

	msleep(1);

	for (i = 0; i < ARRAY_SIZE(init_buf); i += 2)
		mt352_write(fe, &init_buf[i], 2);

	return 0;
}

static struct mt352_config digitv_mt352_config = {
	.demod_init = digitv_mt352_demod_init,
};

static int digitv_nxt6000_tuner_set_params(struct dvb_frontend *fe)
{
	struct dvb_usb_adapter *adap = fe->dvb->priv;
	u8 b[5];

	fe->ops.tuner_ops.calc_regs(fe, b, sizeof(b));
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	return digitv_ctrl_msg(adap->dev, USB_WRITE_TUNER, 0, &b[1], 4, NULL, 0);
}

static struct nxt6000_config digitv_nxt6000_config = {
	.clock_inversion = 1,
};

static int digitv_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct digitv_state *st = adap->dev->priv;

	adap->fe_adap[0].fe = dvb_attach(mt352_attach, &digitv_mt352_config,
					 &adap->dev->i2c_adap);
	if ((adap->fe_adap[0].fe) != NULL) {
		st->is_nxt6000 = 0;
		return 0;
	}
	adap->fe_adap[0].fe = dvb_attach(nxt6000_attach,
					 &digitv_nxt6000_config,
					 &adap->dev->i2c_adap);
	if ((adap->fe_adap[0].fe) != NULL) {
		st->is_nxt6000 = 1;
		return 0;
	}
	return -EIO;
}

static int digitv_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct digitv_state *st = adap->dev->priv;

	if (!dvb_attach(dvb_pll_attach, adap->fe_adap[0].fe, 0x60, NULL, DVB_PLL_TDED4))
		return -ENODEV;

	if (st->is_nxt6000)
		adap->fe_adap[0].fe->ops.tuner_ops.set_params = digitv_nxt6000_tuner_set_params;

	return 0;
}

static struct rc_map_table rc_map_digitv_table[] = {
	{ 0x5f55, KEY_0 },
	{ 0x6f55, KEY_1 },
	{ 0x9f55, KEY_2 },
	{ 0xaf55, KEY_3 },
	{ 0x5f56, KEY_4 },
	{ 0x6f56, KEY_5 },
	{ 0x9f56, KEY_6 },
	{ 0xaf56, KEY_7 },
	{ 0x5f59, KEY_8 },
	{ 0x6f59, KEY_9 },
	{ 0x9f59, KEY_TV },
	{ 0xaf59, KEY_AUX },
	{ 0x5f5a, KEY_DVD },
	{ 0x6f5a, KEY_POWER },
	{ 0x9f5a, KEY_CAMERA },     /* labelled 'Picture' */
	{ 0xaf5a, KEY_AUDIO },
	{ 0x5f65, KEY_INFO },
	{ 0x6f65, KEY_F13 },     /* 16:9 */
	{ 0x9f65, KEY_F14 },     /* 14:9 */
	{ 0xaf65, KEY_EPG },
	{ 0x5f66, KEY_EXIT },
	{ 0x6f66, KEY_MENU },
	{ 0x9f66, KEY_UP },
	{ 0xaf66, KEY_DOWN },
	{ 0x5f69, KEY_LEFT },
	{ 0x6f69, KEY_RIGHT },
	{ 0x9f69, KEY_ENTER },
	{ 0xaf69, KEY_CHANNELUP },
	{ 0x5f6a, KEY_CHANNELDOWN },
	{ 0x6f6a, KEY_VOLUMEUP },
	{ 0x9f6a, KEY_VOLUMEDOWN },
	{ 0xaf6a, KEY_RED },
	{ 0x5f95, KEY_GREEN },
	{ 0x6f95, KEY_YELLOW },
	{ 0x9f95, KEY_BLUE },
	{ 0xaf95, KEY_SUBTITLE },
	{ 0x5f96, KEY_F15 },     /* AD */
	{ 0x6f96, KEY_TEXT },
	{ 0x9f96, KEY_MUTE },
	{ 0xaf96, KEY_REWIND },
	{ 0x5f99, KEY_STOP },
	{ 0x6f99, KEY_PLAY },
	{ 0x9f99, KEY_FASTFORWARD },
	{ 0xaf99, KEY_F16 },     /* chapter */
	{ 0x5f9a, KEY_PAUSE },
	{ 0x6f9a, KEY_PLAY },
	{ 0x9f9a, KEY_RECORD },
	{ 0xaf9a, KEY_F17 },     /* picture in picture */
	{ 0x5fa5, KEY_KPPLUS },  /* zoom in */
	{ 0x6fa5, KEY_KPMINUS }, /* zoom out */
	{ 0x9fa5, KEY_F18 },     /* capture */
	{ 0xafa5, KEY_F19 },     /* web */
	{ 0x5fa6, KEY_EMAIL },
	{ 0x6fa6, KEY_PHONE },
	{ 0x9fa6, KEY_PC },
};

static int digitv_rc_query(struct dvb_usb_device *d, u32 *event, int *state)
{
	int i;
	u8 key[5];
	u8 b[4] = { 0 };

	*event = 0;
	*state = REMOTE_NO_KEY_PRESSED;

	digitv_ctrl_msg(d,USB_READ_REMOTE,0,NULL,0,&key[1],4);

	/* Tell the device we've read the remote. Not sure how necessary
	   this is, but the Nebula SDK does it. */
	digitv_ctrl_msg(d,USB_WRITE_REMOTE,0,b,4,NULL,0);

	/* if something is inside the buffer, simulate key press */
	if (key[1] != 0)
	{
		  for (i = 0; i < d->props.rc.legacy.rc_map_size; i++) {
			if (rc5_custom(&d->props.rc.legacy.rc_map_table[i]) == key[1] &&
			    rc5_data(&d->props.rc.legacy.rc_map_table[i]) == key[2]) {
				*event = d->props.rc.legacy.rc_map_table[i].keycode;
				*state = REMOTE_KEY_PRESSED;
				return 0;
			}
		}
	}

	if (key[0] != 0)
		deb_rc("key: %x %x %x %x %x\n",key[0],key[1],key[2],key[3],key[4]);
	return 0;
}

/* DVB USB Driver stuff */
static struct dvb_usb_device_properties digitv_properties;

static int digitv_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	struct dvb_usb_device *d;
	int ret = dvb_usb_device_init(intf, &digitv_properties, THIS_MODULE, &d,
				      adapter_nr);
	if (ret == 0) {
		u8 b[4] = { 0 };

		if (d != NULL) { /* do that only when the firmware is loaded */
			b[0] = 1;
			digitv_ctrl_msg(d,USB_WRITE_REMOTE_TYPE,0,b,4,NULL,0);

			b[0] = 0;
			digitv_ctrl_msg(d,USB_WRITE_REMOTE,0,b,4,NULL,0);
		}
	}
	return ret;
}

static struct usb_device_id digitv_table [] = {
		{ USB_DEVICE(USB_VID_ANCHOR, USB_PID_NEBULA_DIGITV) },
		{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, digitv_table);

static struct dvb_usb_device_properties digitv_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = CYPRESS_FX2,
	.firmware = "dvb-usb-digitv-02.fw",

	.size_of_priv = sizeof(struct digitv_state),

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.frontend_attach  = digitv_frontend_attach,
			.tuner_attach     = digitv_tuner_attach,

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
	.identify_state   = digitv_identify_state,

	.rc.legacy = {
		.rc_interval      = 1000,
		.rc_map_table     = rc_map_digitv_table,
		.rc_map_size      = ARRAY_SIZE(rc_map_digitv_table),
		.rc_query         = digitv_rc_query,
	},

	.i2c_algo         = &digitv_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 1,
	.devices = {
		{   "Nebula Electronics uDigiTV DVB-T USB2.0)",
			{ &digitv_table[0], NULL },
			{ NULL },
		},
		{ NULL },
	}
};

static struct usb_driver digitv_driver = {
	.name		= "dvb_usb_digitv",
	.probe		= digitv_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table	= digitv_table,
};

module_usb_driver(digitv_driver);

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@desy.de>");
MODULE_DESCRIPTION("Driver for Nebula Electronics uDigiTV DVB-T USB2.0");
MODULE_VERSION("1.0-alpha");
MODULE_LICENSE("GPL");
