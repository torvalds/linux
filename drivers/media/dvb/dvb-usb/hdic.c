/*
 * DVB USB Linux driver for the HDIC receiver
 *
 * Copyright (C) 2011 Metropolia University of Applied Sciences, Electria R&D
 *
 * Author: Antti Palosaari <crope@iki.fi>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "hdic.h"
#include "hd29l2.h"
#include "mxl5007t.h"

/* debug */
static int dvb_usb_hdic_debug;
module_param_named(debug, dvb_usb_hdic_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level" DVB_USB_DEBUG_STATUS);
DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);
/*
 * I2C addresses (7bit) found by probing I2C bus:
 * 0x48 ??
 * 0x51 eeprom
 * 0x60 MaxLinear MXL5007T tuner
 * 0x73 HDIC HD29L2 demod
 *
 * Xtals:
 * 24.000 MHz Cypress CY7C68013A-56 (FX2)
 * 30.400 MHz HDIC HD29L2
 * 24.000 MHz MaxLinear MXL5007T
 *
 * I/Os:
 * RDY1 / SLWR == TS_CLK (USB_SLWR = !TS_CLK&TS_VALID)
 * PA1 / INT1  == 29L1_RESET RST_N
 */

/*
 * See Qanu DVB-T USB2.0 communication protocol specification for more
 * information used USB API.
 */

/* I2C */
static int hdic_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msg[],
	int num)
{
	int ret;
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	u8 buf[64];

	/*
	 * increase sleep when there is a lot of errors:
	 * dvb-usb: recv bulk message failed: -110
	 */
#define HDIC_I2C_SLEEP 1

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	if (num == 2 && !(msg[0].flags & I2C_M_RD) &&
		(msg[1].flags & I2C_M_RD)) {
		/* I2C write + read combination (typical register read) */
		buf[0] = HDIC_CMD_I2C;
		buf[1] = (msg[0].addr << 1); /* I2C write */
		buf[2] = msg[0].len;
		buf[3] = 1; /* no I2C stop => repeated start */
		memcpy(&buf[4], msg[0].buf, msg[0].len);
		ret = dvb_usb_generic_rw(d, buf, 4+msg[0].len, buf, 1,
			HDIC_I2C_SLEEP);
		if (ret)
			goto err;

		buf[0] = HDIC_CMD_I2C;
		buf[1] = (msg[1].addr << 1) | 0x01; /* I2C read */
		buf[2] = msg[1].len;
		buf[3] = 0; /* I2C stop */
		ret = dvb_usb_generic_rw(d, buf, 4, buf, 1+msg[1].len,
			HDIC_I2C_SLEEP);
		if (ret)
			goto err;

		memcpy(msg[1].buf, &buf[1], msg[1].len);

	} else if (num == 1 && !(msg[0].flags & I2C_M_RD)) {
		/* I2C write (typical register write) */
		buf[0] = HDIC_CMD_I2C;
		buf[1] = (msg[0].addr << 1); /* I2C write */
		buf[2] = msg[0].len;
		buf[3] = 0; /* I2C stop */
		memcpy(&buf[4], msg[0].buf, msg[0].len);
		ret = dvb_usb_generic_rw(d, buf, 4+msg[0].len, buf, 1,
			HDIC_I2C_SLEEP);
		if (ret)
			goto err;
	} else {
		ret = -EOPNOTSUPP;
		goto err;
	}

	usleep_range(100, 1000);

	mutex_unlock(&d->i2c_mutex);

	return num;

err:
	deb_info("%s: failed=%d\n", __func__, ret);
	mutex_unlock(&d->i2c_mutex);
	return ret;
}

static u32 hdic_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm hdic_i2c_algo = {
	.master_xfer   = hdic_i2c_xfer,
	.functionality = hdic_i2c_func,
};

/* Callbacks for DVB USB */
static int hdic_power_ctrl(struct dvb_usb_device *d, int enable)
{
	u8 sbuf[] = { HDIC_CMD_SLEEP_MODE, enable ? 0 : 1 };
	u8 rbuf[1];

	deb_info("%s: enable=%d\n", __func__, enable);

	return dvb_usb_generic_rw(d, sbuf, sizeof(sbuf), rbuf, sizeof(rbuf), 0);
}

static int hdic_streaming_ctrl(struct dvb_usb_adapter *adap, int enable)
{
	u8 sbuf[] = { HDIC_CMD_CONTROL_STREAM_TRANSFER, enable };
	u8 rbuf[1];

	deb_info("%s: enable=%d\n", __func__, enable);

	return dvb_usb_generic_rw(adap->dev, sbuf, sizeof(sbuf), rbuf,
		sizeof(rbuf), 0);
}

/* general callback */
static int hdic_frontend_callback(void *priv, int component, int cmd, int arg)
{
	int ret;
	struct dvb_frontend *fe = priv;
	struct dvb_usb_adapter *adap = fe->dvb->priv;
	u8 sbuf[2] = { HDIC_CMD_DEMOD_RESET };
	u8 rbuf[1];

	deb_info("%s:\n", __func__);

	/* enable demod reset */
	sbuf[1] = 1;
	ret = dvb_usb_generic_rw(adap->dev, sbuf, sizeof(sbuf),
		rbuf, sizeof(rbuf), 0);
	if (ret)
		deb_info("%s: failed enable demod reset\n", __func__);

	usleep_range(100, 10000);

	/* disable demod reset */
	sbuf[1] = 0;
	ret = dvb_usb_generic_rw(adap->dev, sbuf, sizeof(sbuf), rbuf,
		sizeof(rbuf), 0);
	if (ret)
		deb_info("%s: failed disable demod reset\n", __func__);

	return 0;
}

static struct hd29l2_config hdic_hd29l2_config = {
	.i2c_addr = 0x73,
	.tuner_i2c_addr = 0x60,
	.ts_mode = HD29L2_TS_PARALLEL,
};

static int hdic_frontend_attach(struct dvb_usb_adapter *adap)
{
	int ret;
	u8 sbuf[2];
	u8 rbuf[3];

	deb_info("%s:\n", __func__);

	/* wake-up device */
	sbuf[0] = HDIC_CMD_GET_FIRMWARE_VERSION;
	ret = dvb_usb_generic_rw(adap->dev, sbuf, sizeof(sbuf), rbuf,
		sizeof(rbuf), 0);
	if (ret)
		deb_info("%s: failed wake-up\n", __func__);

	/* disable demod reset */
	sbuf[0] = HDIC_CMD_DEMOD_RESET;
	sbuf[1] = 0;
	ret = dvb_usb_generic_rw(adap->dev, sbuf, sizeof(sbuf), rbuf,
		sizeof(rbuf), 0);
	if (ret)
		deb_info("%s: failed disable demod reset\n", __func__);

	/* attach demod */
	adap->fe_adap[0].fe = dvb_attach(hd29l2_attach, &hdic_hd29l2_config,
		&adap->dev->i2c_adap);
	if (adap->fe_adap[0].fe == NULL) {
		ret = -ENODEV;
		goto err;
	}

	/* setup the reset callback */
	adap->fe_adap[0].fe->callback = hdic_frontend_callback;

	return 0;
err:
	deb_info("%s: failed=%d\n", __func__, ret);
	return ret;
}

static struct mxl5007t_config hdic_mxl5007t_config = {
	.xtal_freq_hz = MxL_XTAL_24_MHZ,
	.if_freq_hz = MxL_IF_36_15_MHZ,
	.invert_if = 1,
};

static int hdic_tuner_attach(struct dvb_usb_adapter *adap)
{
	int ret;

	deb_info("%s:\n", __func__);

	if (dvb_attach(mxl5007t_attach, adap->fe_adap[0].fe,
		&adap->dev->i2c_adap, 0x60, &hdic_mxl5007t_config) == NULL) {
		ret = -ENODEV;
		goto err;
	}

	return 0;
err:
	deb_info("%s: failed=%d\n", __func__, ret);
	return ret;
}

/* DVB USB Driver stuff */
static struct dvb_usb_device_properties hdic_properties;

static int hdic_probe(struct usb_interface *intf,
	const struct usb_device_id *id)
{
	return dvb_usb_device_init(intf, &hdic_properties, THIS_MODULE, NULL,
		adapter_nr);
}

/*
 * 04b4:1004 HDIC development board firmware
 * 04b4:8613 CY7C68013 EZ-USB FX2 USB 2.0 Development Kit
 */
static struct usb_device_id hdic_id[] = {
#define HDIC_8613       0 /* CY7C68013 EZ-USB FX2 USB 2.0 Development Kit */
#define HDIC_1004       1 /* HDIC 04b4:1004 */
#define HDIC_LINUX      2 /* HDIC Linux custom firmware */

	[HDIC_8613] = {USB_DEVICE(USB_VID_CYPRESS, 0x8613)},
	[HDIC_1004] = {USB_DEVICE(USB_VID_CYPRESS, 0x1004)},
	[HDIC_LINUX] = {USB_DEVICE(USB_VID_CYPRESS, 0x1e04)},
	{} /* terminating entry */
};

MODULE_DEVICE_TABLE(usb, hdic_id);

static struct dvb_usb_device_properties hdic_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = CYPRESS_FX2,
	.firmware = "dvb-usb-hdic.fw",

	.num_adapters = 1,
	.adapter = {
		{
			.num_frontends = 1,
			.fe = {
				{
					.streaming_ctrl  = hdic_streaming_ctrl,
					.frontend_attach = hdic_frontend_attach,
					.tuner_attach    = hdic_tuner_attach,

					.stream = {
						.type = USB_BULK,
						.count = 5,
						.endpoint = 0x02,
						.u = {
							.bulk = {
								.buffersize =
									(4*512),
							}
						}
					},
				}
			},
		}
	},

	.power_ctrl = hdic_power_ctrl,

	.i2c_algo = &hdic_i2c_algo,

	.generic_bulk_ctrl_endpoint = 1,

	.num_device_descs = 1,
	.devices = {
		{
			.name = "HDIC reference design",
			.cold_ids = {&hdic_id[HDIC_8613],
				&hdic_id[HDIC_1004], NULL},
			.warm_ids = {&hdic_id[HDIC_LINUX], NULL},
		},
	}
};

static struct usb_driver hdic_driver = {
	.name       = "dvb_usb_hdic",
	.probe      = hdic_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table   = hdic_id,
};

/* module stuff */
static int __init hdic_module_init(void)
{
	int ret;

	deb_info("%s:\n", __func__);

	ret = usb_register(&hdic_driver);
	if (ret)
		err("module init failed=%d", ret);

	return ret;
}

static void __exit hdic_module_exit(void)
{
	deb_info("%s:\n", __func__);

	/* deregister this driver from the USB subsystem */
	usb_deregister(&hdic_driver);
}

module_init(hdic_module_init);
module_exit(hdic_module_exit);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("HDIC DMB-TH reference design USB2.0 driver (custom firmware)");
MODULE_LICENSE("GPL");
