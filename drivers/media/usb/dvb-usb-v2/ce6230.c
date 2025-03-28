// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Intel CE6230 DVB USB driver
 *
 * Copyright (C) 2009 Antti Palosaari <crope@iki.fi>
 */

#include "ce6230.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static int ce6230_ctrl_msg(struct dvb_usb_device *d, struct usb_req *req)
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
		dev_err(&d->udev->dev, "%s: unknown command=%02x\n",
				KBUILD_MODNAME, req->cmd);
		ret = -EINVAL;
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
		pipe = usb_sndctrlpipe(d->udev, 0);
	} else {
		/* read */
		pipe = usb_rcvctrlpipe(d->udev, 0);
	}

	msleep(1); /* avoid I2C errors */

	ret = usb_control_msg(d->udev, pipe, request, requesttype, value, index,
			buf, req->data_len, CE6230_USB_TIMEOUT);

	dvb_usb_dbg_usb_control_msg(d->udev, request, requesttype, value, index,
			buf, req->data_len);

	if (ret < 0)
		dev_err(&d->udev->dev, "%s: usb_control_msg() failed=%d\n",
				KBUILD_MODNAME, ret);
	else
		ret = 0;

	/* read request, copy returned data to return buf */
	if (!ret && requesttype == (USB_TYPE_VENDOR | USB_DIR_IN))
		memcpy(req->data, buf, req->data_len);

	kfree(buf);
error:
	return ret;
}

/* I2C */
static struct zl10353_config ce6230_zl10353_config;

static int ce6230_i2c_master_xfer(struct i2c_adapter *adap,
		struct i2c_msg msg[], int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int ret = 0, i = 0;
	struct usb_req req;

	if (num > 2)
		return -EOPNOTSUPP;

	memset(&req, 0, sizeof(req));

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	while (i < num) {
		if (num > i + 1 && (msg[i+1].flags & I2C_M_RD)) {
			if (msg[i].addr ==
				ce6230_zl10353_config.demod_address) {
				if (msg[i].len < 1) {
					i = -EOPNOTSUPP;
					break;
				}
				req.cmd = DEMOD_READ;
				req.value = msg[i].addr >> 1;
				req.index = msg[i].buf[0];
				req.data_len = msg[i+1].len;
				req.data = &msg[i+1].buf[0];
				ret = ce6230_ctrl_msg(d, &req);
			} else {
				dev_err(&d->udev->dev, "%s: I2C read not " \
						"implemented\n",
						KBUILD_MODNAME);
				ret = -EOPNOTSUPP;
			}
			i += 2;
		} else {
			if (msg[i].addr ==
				ce6230_zl10353_config.demod_address) {
				if (msg[i].len < 1) {
					i = -EOPNOTSUPP;
					break;
				}
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

static u32 ce6230_i2c_functionality(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static const struct i2c_algorithm ce6230_i2c_algorithm = {
	.master_xfer   = ce6230_i2c_master_xfer,
	.functionality = ce6230_i2c_functionality,
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
	struct dvb_usb_device *d = adap_to_d(adap);

	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	adap->fe[0] = dvb_attach(zl10353_attach, &ce6230_zl10353_config,
			&d->i2c_adap);
	if (adap->fe[0] == NULL)
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
	struct dvb_usb_device *d = adap_to_d(adap);
	int ret;

	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	ret = dvb_attach(mxl5005s_attach, adap->fe[0], &d->i2c_adap,
			&ce6230_mxl5003s_config) == NULL ? -ENODEV : 0;
	return ret;
}

static int ce6230_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	int ret;

	dev_dbg(&d->udev->dev, "%s: onoff=%d\n", __func__, onoff);

	/* InterfaceNumber 1 / AlternateSetting 0     idle
	   InterfaceNumber 1 / AlternateSetting 1     streaming */
	ret = usb_set_interface(d->udev, 1, onoff);
	if (ret)
		dev_err(&d->udev->dev, "%s: usb_set_interface() failed=%d\n",
				KBUILD_MODNAME, ret);

	return ret;
}

/* DVB USB Driver stuff */
static struct dvb_usb_device_properties ce6230_props = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,
	.bInterfaceNumber = 1,

	.i2c_algo = &ce6230_i2c_algorithm,
	.power_ctrl = ce6230_power_ctrl,
	.frontend_attach = ce6230_zl10353_frontend_attach,
	.tuner_attach = ce6230_mxl5003s_tuner_attach,

	.num_adapters = 1,
	.adapter = {
		{
			.stream = {
				.type = USB_BULK,
				.count = 6,
				.endpoint = 0x82,
				.u = {
					.bulk = {
						.buffersize = (16 * 512),
					}
				}
			},
		}
	},
};

static const struct usb_device_id ce6230_id_table[] = {
	{ DVB_USB_DEVICE(USB_VID_INTEL, USB_PID_INTEL_CE9500,
		&ce6230_props, "Intel CE9500 reference design", NULL) },
	{ DVB_USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_A310,
		&ce6230_props, "AVerMedia A310 USB 2.0 DVB-T tuner", NULL) },
	{ }
};
MODULE_DEVICE_TABLE(usb, ce6230_id_table);

static struct usb_driver ce6230_usb_driver = {
	.name = KBUILD_MODNAME,
	.id_table = ce6230_id_table,
	.probe = dvb_usbv2_probe,
	.disconnect = dvb_usbv2_disconnect,
	.suspend = dvb_usbv2_suspend,
	.resume = dvb_usbv2_resume,
	.reset_resume = dvb_usbv2_reset_resume,
	.no_dynamic_id = 1,
	.soft_unbind = 1,
};

module_usb_driver(ce6230_usb_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Intel CE6230 driver");
MODULE_LICENSE("GPL");
