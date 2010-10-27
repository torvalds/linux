/* DVB USB compliant linux driver for Technotrend DVB USB boxes and clones
 * (e.g. Pinnacle 400e DVB-S USB2.0).
 *
 * The Pinnacle 400e uses the same protocol as the Technotrend USB1.1 boxes.
 *
 * TDA8263 + TDA10086
 *
 * I2C addresses:
 * 0x08 - LNBP21PD   - LNB power supply
 * 0x0e - TDA10086   - Demodulator
 * 0x50 - FX2 eeprom
 * 0x60 - TDA8263    - Tuner
 * 0x78 ???
 *
 * Copyright (c) 2002 Holger Waechtler <holger@convergence.de>
 * Copyright (c) 2003 Felix Domke <tmbinc@elitedvb.net>
 * Copyright (C) 2005-6 Patrick Boettcher <pb@linuxtv.org>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#define DVB_USB_LOG_PREFIX "ttusb2"
#include "dvb-usb.h"

#include "ttusb2.h"

#include "tda826x.h"
#include "tda10086.h"
#include "tda1002x.h"
#include "tda827x.h"
#include "lnbp21.h"

/* debug */
static int dvb_usb_ttusb2_debug;
#define deb_info(args...)   dprintk(dvb_usb_ttusb2_debug,0x01,args)
module_param_named(debug,dvb_usb_ttusb2_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info (or-able))." DVB_USB_DEBUG_STATUS);

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

struct ttusb2_state {
	u8 id;
};

static int ttusb2_msg(struct dvb_usb_device *d, u8 cmd,
		u8 *wbuf, int wlen, u8 *rbuf, int rlen)
{
	struct ttusb2_state *st = d->priv;
	u8 s[wlen+4],r[64] = { 0 };
	int ret = 0;

	memset(s,0,wlen+4);

	s[0] = 0xaa;
	s[1] = ++st->id;
	s[2] = cmd;
	s[3] = wlen;
	memcpy(&s[4],wbuf,wlen);

	ret = dvb_usb_generic_rw(d, s, wlen+4, r, 64, 0);

	if (ret  != 0 ||
		r[0] != 0x55 ||
		r[1] != s[1] ||
		r[2] != cmd ||
		(rlen > 0 && r[3] != rlen)) {
		warn("there might have been an error during control message transfer. (rlen = %d, was %d)",rlen,r[3]);
		return -EIO;
	}

	if (rlen > 0)
		memcpy(rbuf, &r[4], rlen);

	return 0;
}

static int ttusb2_i2c_xfer(struct i2c_adapter *adap,struct i2c_msg msg[],int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	static u8 obuf[60], ibuf[60];
	int i,read;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	if (num > 2)
		warn("more than 2 i2c messages at a time is not handled yet. TODO.");

	for (i = 0; i < num; i++) {
		read = i+1 < num && (msg[i+1].flags & I2C_M_RD);

		obuf[0] = (msg[i].addr << 1) | read;
		obuf[1] = msg[i].len;

		/* read request */
		if (read)
			obuf[2] = msg[i+1].len;
		else
			obuf[2] = 0;

		memcpy(&obuf[3],msg[i].buf,msg[i].len);

		if (ttusb2_msg(d, CMD_I2C_XFER, obuf, msg[i].len+3, ibuf, obuf[2] + 3) < 0) {
			err("i2c transfer failed.");
			break;
		}

		if (read) {
			memcpy(msg[i+1].buf,&ibuf[3],msg[i+1].len);
			i++;
		}
	}

	mutex_unlock(&d->i2c_mutex);
	return i;
}

static u32 ttusb2_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm ttusb2_i2c_algo = {
	.master_xfer   = ttusb2_i2c_xfer,
	.functionality = ttusb2_i2c_func,
};

/* Callbacks for DVB USB */
static int ttusb2_identify_state (struct usb_device *udev, struct
		dvb_usb_device_properties *props, struct dvb_usb_device_description **desc,
		int *cold)
{
	*cold = udev->descriptor.iManufacturer == 0 && udev->descriptor.iProduct == 0;
	return 0;
}

static int ttusb2_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	u8 b = onoff;
	ttusb2_msg(d, CMD_POWER, &b, 0, NULL, 0);
	return ttusb2_msg(d, CMD_POWER, &b, 1, NULL, 0);
}


static struct tda10086_config tda10086_config = {
	.demod_address = 0x0e,
	.invert = 0,
	.diseqc_tone = 1,
	.xtal_freq = TDA10086_XTAL_16M,
};

static struct tda10023_config tda10023_config = {
	.demod_address = 0x0c,
	.invert = 0,
	.xtal = 16000000,
	.pll_m = 11,
	.pll_p = 3,
	.pll_n = 1,
	.deltaf = 0xa511,
};

static int ttusb2_frontend_tda10086_attach(struct dvb_usb_adapter *adap)
{
	if (usb_set_interface(adap->dev->udev,0,3) < 0)
		err("set interface to alts=3 failed");

	if ((adap->fe = dvb_attach(tda10086_attach, &tda10086_config, &adap->dev->i2c_adap)) == NULL) {
		deb_info("TDA10086 attach failed\n");
		return -ENODEV;
	}

	return 0;
}

static int ttusb2_frontend_tda10023_attach(struct dvb_usb_adapter *adap)
{
	if (usb_set_interface(adap->dev->udev, 0, 3) < 0)
		err("set interface to alts=3 failed");
	if ((adap->fe = dvb_attach(tda10023_attach, &tda10023_config, &adap->dev->i2c_adap, 0x48)) == NULL) {
		deb_info("TDA10023 attach failed\n");
		return -ENODEV;
	}
	return 0;
}

static int ttusb2_tuner_tda827x_attach(struct dvb_usb_adapter *adap)
{
	if (dvb_attach(tda827x_attach, adap->fe, 0x61, &adap->dev->i2c_adap, NULL) == NULL) {
		printk(KERN_ERR "%s: No tda827x found!\n", __func__);
		return -ENODEV;
	}
	return 0;
}

static int ttusb2_tuner_tda826x_attach(struct dvb_usb_adapter *adap)
{
	if (dvb_attach(tda826x_attach, adap->fe, 0x60, &adap->dev->i2c_adap, 0) == NULL) {
		deb_info("TDA8263 attach failed\n");
		return -ENODEV;
	}

	if (dvb_attach(lnbp21_attach, adap->fe, &adap->dev->i2c_adap, 0, 0) == NULL) {
		deb_info("LNBP21 attach failed\n");
		return -ENODEV;
	}
	return 0;
}

/* DVB USB Driver stuff */
static struct dvb_usb_device_properties ttusb2_properties;
static struct dvb_usb_device_properties ttusb2_properties_s2400;
static struct dvb_usb_device_properties ttusb2_properties_ct3650;

static int ttusb2_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	if (0 == dvb_usb_device_init(intf, &ttusb2_properties,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf, &ttusb2_properties_s2400,
				     THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf, &ttusb2_properties_ct3650,
				     THIS_MODULE, NULL, adapter_nr))
		return 0;
	return -ENODEV;
}

static struct usb_device_id ttusb2_table [] = {
	{ USB_DEVICE(USB_VID_PINNACLE, USB_PID_PCTV_400E) },
	{ USB_DEVICE(USB_VID_PINNACLE, USB_PID_PCTV_450E) },
	{ USB_DEVICE(USB_VID_TECHNOTREND,
		USB_PID_TECHNOTREND_CONNECT_S2400) },
	{ USB_DEVICE(USB_VID_TECHNOTREND,
		USB_PID_TECHNOTREND_CONNECT_CT3650) },
	{}		/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, ttusb2_table);

static struct dvb_usb_device_properties ttusb2_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = CYPRESS_FX2,
	.firmware = "dvb-usb-pctv-400e-01.fw",

	.size_of_priv = sizeof(struct ttusb2_state),

	.num_adapters = 1,
	.adapter = {
		{
			.streaming_ctrl   = NULL, // ttusb2_streaming_ctrl,

			.frontend_attach  = ttusb2_frontend_tda10086_attach,
			.tuner_attach     = ttusb2_tuner_tda826x_attach,

			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_ISOC,
				.count = 5,
				.endpoint = 0x02,
				.u = {
					.isoc = {
						.framesperurb = 4,
						.framesize = 940,
						.interval = 1,
					}
				}
			}
		}
	},

	.power_ctrl       = ttusb2_power_ctrl,
	.identify_state   = ttusb2_identify_state,

	.i2c_algo         = &ttusb2_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 2,
	.devices = {
		{   "Pinnacle 400e DVB-S USB2.0",
			{ &ttusb2_table[0], NULL },
			{ NULL },
		},
		{   "Pinnacle 450e DVB-S USB2.0",
			{ &ttusb2_table[1], NULL },
			{ NULL },
		},
	}
};

static struct dvb_usb_device_properties ttusb2_properties_s2400 = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = CYPRESS_FX2,
	.firmware = "dvb-usb-tt-s2400-01.fw",

	.size_of_priv = sizeof(struct ttusb2_state),

	.num_adapters = 1,
	.adapter = {
		{
			.streaming_ctrl   = NULL,

			.frontend_attach  = ttusb2_frontend_tda10086_attach,
			.tuner_attach     = ttusb2_tuner_tda826x_attach,

			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_ISOC,
				.count = 5,
				.endpoint = 0x02,
				.u = {
					.isoc = {
						.framesperurb = 4,
						.framesize = 940,
						.interval = 1,
					}
				}
			}
		}
	},

	.power_ctrl       = ttusb2_power_ctrl,
	.identify_state   = ttusb2_identify_state,

	.i2c_algo         = &ttusb2_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 1,
	.devices = {
		{   "Technotrend TT-connect S-2400",
			{ &ttusb2_table[2], NULL },
			{ NULL },
		},
	}
};

static struct dvb_usb_device_properties ttusb2_properties_ct3650 = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = CYPRESS_FX2,

	.size_of_priv = sizeof(struct ttusb2_state),

	.num_adapters = 1,
	.adapter = {
		{
			.streaming_ctrl   = NULL,

			.frontend_attach  = ttusb2_frontend_tda10023_attach,
			.tuner_attach = ttusb2_tuner_tda827x_attach,

			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_ISOC,
				.count = 5,
				.endpoint = 0x02,
				.u = {
					.isoc = {
						.framesperurb = 4,
						.framesize = 940,
						.interval = 1,
					}
				}
			}
		},
	},

	.power_ctrl       = ttusb2_power_ctrl,
	.identify_state   = ttusb2_identify_state,

	.i2c_algo         = &ttusb2_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 1,
	.devices = {
		{   "Technotrend TT-connect CT-3650",
			.warm_ids = { &ttusb2_table[3], NULL },
		},
	}
};

static struct usb_driver ttusb2_driver = {
	.name		= "dvb_usb_ttusb2",
	.probe		= ttusb2_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table	= ttusb2_table,
};

/* module stuff */
static int __init ttusb2_module_init(void)
{
	int result;
	if ((result = usb_register(&ttusb2_driver))) {
		err("usb_register failed. Error number %d",result);
		return result;
	}

	return 0;
}

static void __exit ttusb2_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&ttusb2_driver);
}

module_init (ttusb2_module_init);
module_exit (ttusb2_module_exit);

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@desy.de>");
MODULE_DESCRIPTION("Driver for Pinnacle PCTV 400e DVB-S USB2.0");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
