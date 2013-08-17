/*
 * DVB USB Linux driver for Intel CE6230 DVB-T USB2.0 receiver
 *
 * Copyright (C) 2009 Antti Palosaari <crope@iki.fi>
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
 *
 */

#include "ce6230.h"
#include "zl10353.h"
#include "mxl5005s.h"

/* debug */
static int dvb_usb_ce6230_debug;
module_param_named(debug, dvb_usb_ce6230_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level" DVB_USB_DEBUG_STATUS);
DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static struct zl10353_config ce6230_zl10353_config;

static int ce6230_rw_udev(struct usb_device *udev, struct req_t *req)
{
	int ret;
	unsigned int pipe;
	u8 request;
	u8 requesttype;
	u16 value;
	u16 index;
	u8 *buf;

	request = req->cmd;
	value = req->value;
	index = req->index;

	switch (req->cmd) {
	case I2C_READ:
	case DEMOD_READ:
	case REG_READ:
		requesttype = (USB_TYPE_VENDOR | USB_DIR_IN);
		break;
	case I2C_WRITE:
	case DEMOD_WRITE:
	case REG_WRITE:
		requesttype = (USB_TYPE_VENDOR | USB_DIR_OUT);
		break;
	default:
		err("unknown command:%02x", req->cmd);
		ret = -EPERM;
		goto error;
	}

	buf = kmalloc(req->data_len, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto error;
	}

	if (requesttype == (USB_TYPE_VENDOR | USB_DIR_OUT)) {
		/* write */
		memcpy(buf, req->data, req->data_len);
		pipe = usb_sndctrlpipe(udev, 0);
	} else {
		/* read */
		pipe = usb_rcvctrlpipe(udev, 0);
	}

	msleep(1); /* avoid I2C errors */

	ret = usb_control_msg(udev, pipe, request, requesttype, value, index,
				buf, req->data_len, CE6230_USB_TIMEOUT);

	ce6230_debug_dump(request, requesttype, value, index, buf,
		req->data_len, deb_xfer);

	if (ret < 0)
		deb_info("%s: usb_control_msg failed:%d\n", __func__, ret);
	else
		ret = 0;

	/* read request, copy returned data to return buf */
	if (!ret && requesttype == (USB_TYPE_VENDOR | USB_DIR_IN))
		memcpy(req->data, buf, req->data_len);

	kfree(buf);
error:
	return ret;
}

static int ce6230_ctrl_msg(struct dvb_usb_device *d, struct req_t *req)
{
	return ce6230_rw_udev(d->udev, req);
}

/* I2C */
static int ce6230_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msg[],
			   int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int i = 0;
	struct req_t req;
	int ret = 0;
	memset(&req, 0, sizeof(req));

	if (num > 2)
		return -EINVAL;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	while (i < num) {
		if (num > i + 1 && (msg[i+1].flags & I2C_M_RD)) {
			if (msg[i].addr ==
				ce6230_zl10353_config.demod_address) {
				req.cmd = DEMOD_READ;
				req.value = msg[i].addr >> 1;
				req.index = msg[i].buf[0];
				req.data_len = msg[i+1].len;
				req.data = &msg[i+1].buf[0];
				ret = ce6230_ctrl_msg(d, &req);
			} else {
				err("i2c read not implemented");
				ret = -EPERM;
			}
			i += 2;
		} else {
			if (msg[i].addr ==
				ce6230_zl10353_config.demod_address) {
				req.cmd = DEMOD_WRITE;
				req.value = msg[i].addr >> 1;
				req.index = msg[i].buf[0];
				req.data_len = msg[i].len-1;
				req.data = &msg[i].buf[1];
				ret = ce6230_ctrl_msg(d, &req);
			} else {
				req.cmd = I2C_WRITE;
				req.value = 0x2000 + (msg[i].addr >> 1);
				req.index = 0x0000;
				req.data_len = msg[i].len;
				req.data = &msg[i].buf[0];
				ret = ce6230_ctrl_msg(d, &req);
			}
			i += 1;
		}
		if (ret)
			break;
	}

	mutex_unlock(&d->i2c_mutex);
	return ret ? ret : i;
}

static u32 ce6230_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm ce6230_i2c_algo = {
	.master_xfer   = ce6230_i2c_xfer,
	.functionality = ce6230_i2c_func,
};

/* Callbacks for DVB USB */
static struct zl10353_config ce6230_zl10353_config = {
	.demod_address = 0x1e,
	.adc_clock = 450000,
	.if2 = 45700,
	.no_tuner = 1,
	.parallel_ts = 1,
	.clock_ctl_1 = 0x34,
	.pll_0 = 0x0e,
};

static int ce6230_zl10353_frontend_attach(struct dvb_usb_adapter *adap)
{
	deb_info("%s:\n", __func__);
	adap->fe_adap[0].fe = dvb_attach(zl10353_attach, &ce6230_zl10353_config,
		&adap->dev->i2c_adap);
	if (adap->fe_adap[0].fe == NULL)
		return -ENODEV;
	return 0;
}

static struct mxl5005s_config ce6230_mxl5003s_config = {
	.i2c_address     = 0xc6,
	.if_freq         = IF_FREQ_4570000HZ,
	.xtal_freq       = CRYSTAL_FREQ_16000000HZ,
	.agc_mode        = MXL_SINGLE_AGC,
	.tracking_filter = MXL_TF_DEFAULT,
	.rssi_enable     = MXL_RSSI_ENABLE,
	.cap_select      = MXL_CAP_SEL_ENABLE,
	.div_out         = MXL_DIV_OUT_4,
	.clock_out       = MXL_CLOCK_OUT_DISABLE,
	.output_load     = MXL5005S_IF_OUTPUT_LOAD_200_OHM,
	.top		 = MXL5005S_TOP_25P2,
	.mod_mode        = MXL_DIGITAL_MODE,
	.if_mode         = MXL_ZERO_IF,
	.AgcMasterByte   = 0x00,
};

static int ce6230_mxl5003s_tuner_attach(struct dvb_usb_adapter *adap)
{
	int ret;
	deb_info("%s:\n", __func__);
	ret = dvb_attach(mxl5005s_attach, adap->fe_adap[0].fe, &adap->dev->i2c_adap,
			&ce6230_mxl5003s_config) == NULL ? -ENODEV : 0;
	return ret;
}

static int ce6230_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	int ret;
	deb_info("%s: onoff:%d\n", __func__, onoff);

	/* InterfaceNumber 1 / AlternateSetting 0     idle
	   InterfaceNumber 1 / AlternateSetting 1     streaming */
	ret = usb_set_interface(d->udev, 1, onoff);
	if (ret)
		err("usb_set_interface failed with error:%d", ret);

	return ret;
}

/* DVB USB Driver stuff */
static struct dvb_usb_device_properties ce6230_properties;

static int ce6230_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	int ret = 0;
	struct dvb_usb_device *d = NULL;

	deb_info("%s: interface:%d\n", __func__,
		intf->cur_altsetting->desc.bInterfaceNumber);

	if (intf->cur_altsetting->desc.bInterfaceNumber == 1) {
		ret = dvb_usb_device_init(intf, &ce6230_properties, THIS_MODULE,
			&d, adapter_nr);
		if (ret)
			err("init failed with error:%d\n", ret);
	}

	return ret;
}

static struct usb_device_id ce6230_table[] = {
	{ USB_DEVICE(USB_VID_INTEL, USB_PID_INTEL_CE9500) },
	{ USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_A310) },
	{ } /* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, ce6230_table);

static struct dvb_usb_device_properties ce6230_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = DEVICE_SPECIFIC,
	.no_reconnect = 1,

	.size_of_priv = 0,

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.frontend_attach  = ce6230_zl10353_frontend_attach,
			.tuner_attach     = ce6230_mxl5003s_tuner_attach,
			.stream = {
				.type = USB_BULK,
				.count = 6,
				.endpoint = 0x82,
				.u = {
					.bulk = {
						.buffersize = (16*512),
					}
				}
			},
		}},
		}
	},

	.power_ctrl = ce6230_power_ctrl,

	.i2c_algo = &ce6230_i2c_algo,

	.num_device_descs = 2,
	.devices = {
		{
			.name = "Intel CE9500 reference design",
			.cold_ids = {NULL},
			.warm_ids = {&ce6230_table[0], NULL},
		},
		{
			.name = "AVerMedia A310 USB 2.0 DVB-T tuner",
			.cold_ids = {NULL},
			.warm_ids = {&ce6230_table[1], NULL},
		},
	}
};

static struct usb_driver ce6230_driver = {
	.name       = "dvb_usb_ce6230",
	.probe      = ce6230_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table   = ce6230_table,
};

module_usb_driver(ce6230_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Driver for Intel CE6230 DVB-T USB2.0");
MODULE_LICENSE("GPL");
