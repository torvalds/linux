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
int dvb_usb_digitv_debug;
module_param_named(debug,dvb_usb_digitv_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=rc (or-able))." DVB_USB_DEBUG_STATUS);

static int digitv_ctrl_msg(struct dvb_usb_device *d,
		u8 cmd, u8 vv, u8 *wbuf, int wlen, u8 *rbuf, int rlen)
{
	int wo = (rbuf == NULL || rlen == 0); /* write-only */
	u8 sndbuf[7],rcvbuf[7];
	memset(sndbuf,0,7); memset(rcvbuf,0,7);

	sndbuf[0] = cmd;
	sndbuf[1] = vv;
	sndbuf[2] = wo ? wlen : rlen;

	if (!wo) {
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

	if (down_interruptible(&d->i2c_sem) < 0)
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

	up(&d->i2c_sem);
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
		dvb_usb_properties *props, struct dvb_usb_device_description **desc,
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
	.pll_set = dvb_usb_pll_set,
};

static int digitv_nxt6000_pll_set(struct dvb_frontend *fe, struct dvb_frontend_parameters *fep)
{
	struct dvb_usb_device *d = fe->dvb->priv;
	u8 b[5];
	dvb_usb_pll_set(fe,fep,b);
	return digitv_ctrl_msg(d,USB_WRITE_TUNER,0,&b[1],4,NULL,0);
}

static struct nxt6000_config digitv_nxt6000_config = {
	.clock_inversion = 1,
	.pll_set = digitv_nxt6000_pll_set,
};

static int digitv_frontend_attach(struct dvb_usb_device *d)
{
	if ((d->fe = mt352_attach(&digitv_mt352_config, &d->i2c_adap)) != NULL ||
		(d->fe = nxt6000_attach(&digitv_nxt6000_config, &d->i2c_adap)) != NULL)
		return 0;
	return -EIO;
}

static int digitv_tuner_attach(struct dvb_usb_device *d)
{
	d->pll_addr = 0x60;
	d->pll_desc = &dvb_pll_tded4;
	return 0;
}

static struct dvb_usb_rc_key digitv_rc_keys[] = {
	{ 0x00, 0x16, KEY_POWER }, /* dummy key */
};

/* TODO is it really the NEC protocol ? */
int digitv_rc_query(struct dvb_usb_device *d, u32 *event, int *state)
{
	u8 key[5];

	digitv_ctrl_msg(d,USB_READ_REMOTE,0,NULL,0,&key[1],4);
	/* TODO state, maybe it is VV ? */
	if (key[1] != 0)
		key[0] = 0x01; /* if something is inside the buffer, simulate key press */

	/* call the universal NEC remote processor, to find out the key's state and event */
	dvb_usb_nec_rc_key_to_event(d,key,event,state);
	if (key[0] != 0)
		deb_rc("key: %x %x %x %x %x\n",key[0],key[1],key[2],key[3],key[4]);
	return 0;
}

/* DVB USB Driver stuff */
static struct dvb_usb_properties digitv_properties;

static int digitv_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	struct dvb_usb_device *d;
	int ret;
	if ((ret = dvb_usb_device_init(intf,&digitv_properties,THIS_MODULE,&d)) == 0) {
		u8 b[4] = { 0 };

		b[0] = 1;
		digitv_ctrl_msg(d,USB_WRITE_REMOTE_TYPE,0,b,4,NULL,0);

		b[0] = 0;
		digitv_ctrl_msg(d,USB_WRITE_REMOTE,0,b,4,NULL,0);
	}
	return ret;
}

static struct usb_device_id digitv_table [] = {
		{ USB_DEVICE(USB_VID_ANCHOR, USB_PID_NEBULA_DIGITV) },
		{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, digitv_table);

static struct dvb_usb_properties digitv_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = CYPRESS_FX2,
	.firmware = "dvb-usb-digitv-01.fw",

	.size_of_priv     = 0,

	.frontend_attach  = digitv_frontend_attach,
	.tuner_attach     = digitv_tuner_attach,

	.rc_interval      = 1000,
	.rc_key_map       = digitv_rc_keys,
	.rc_key_map_size  = ARRAY_SIZE(digitv_rc_keys),
	.rc_query         = digitv_rc_query,

	.identify_state   = digitv_identify_state,

	.i2c_algo         = &digitv_i2c_algo,

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
		{   "Nebula Electronics uDigiTV DVB-T USB2.0)",
			{ &digitv_table[0], NULL },
			{ NULL },
		},
	}
};

static struct usb_driver digitv_driver = {
	.owner		= THIS_MODULE,
	.name		= "dvb_usb_digitv",
	.probe		= digitv_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table	= digitv_table,
};

/* module stuff */
static int __init digitv_module_init(void)
{
	int result;
	if ((result = usb_register(&digitv_driver))) {
		err("usb_register failed. Error number %d",result);
		return result;
	}

	return 0;
}

static void __exit digitv_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&digitv_driver);
}

module_init (digitv_module_init);
module_exit (digitv_module_exit);

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@desy.de>");
MODULE_DESCRIPTION("Driver for Nebula Electronics uDigiTV DVB-T USB2.0");
MODULE_VERSION("1.0-alpha");
MODULE_LICENSE("GPL");
