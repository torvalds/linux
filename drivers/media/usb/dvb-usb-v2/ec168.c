// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * E3C EC168 DVB USB driver
 *
 * Copyright (C) 2009 Antti Palosaari <crope@iki.fi>
 */

#include "ec168.h"
#include "ec100.h"
#include "mxl5005s.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static int ec168_ctrl_msg(struct dvb_usb_device *d, struct ec168_req *req)
{
	int ret;
	unsigned int pipe;
	u8 request, requesttype;
	u8 *buf;

	switch (req->cmd) {
	case DOWNLOAD_FIRMWARE:
	case GPIO:
	case WRITE_I2C:
	case STREAMING_CTRL:
		requesttype = (USB_TYPE_VENDOR | USB_DIR_OUT);
		request = req->cmd;
		break;
	case READ_I2C:
		requesttype = (USB_TYPE_VENDOR | USB_DIR_IN);
		request = req->cmd;
		break;
	case GET_CONFIG:
		requesttype = (USB_TYPE_VENDOR | USB_DIR_IN);
		request = CONFIG;
		break;
	case SET_CONFIG:
		requesttype = (USB_TYPE_VENDOR | USB_DIR_OUT);
		request = CONFIG;
		break;
	case WRITE_DEMOD:
		requesttype = (USB_TYPE_VENDOR | USB_DIR_OUT);
		request = DEMOD_RW;
		break;
	case READ_DEMOD:
		requesttype = (USB_TYPE_VENDOR | USB_DIR_IN);
		request = DEMOD_RW;
		break;
	default:
		dev_err(&d->udev->dev, "%s: unknown command=%02x\n",
				KBUILD_MODNAME, req->cmd);
		ret = -EINVAL;
		goto error;
	}

	buf = kmalloc(req->size, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto error;
	}

	if (requesttype == (USB_TYPE_VENDOR | USB_DIR_OUT)) {
		/* write */
		memcpy(buf, req->data, req->size);
		pipe = usb_sndctrlpipe(d->udev, 0);
	} else {
		/* read */
		pipe = usb_rcvctrlpipe(d->udev, 0);
	}

	msleep(1); /* avoid I2C errors */

	ret = usb_control_msg(d->udev, pipe, request, requesttype, req->value,
		req->index, buf, req->size, EC168_USB_TIMEOUT);

	dvb_usb_dbg_usb_control_msg(d->udev, request, requesttype, req->value,
			req->index, buf, req->size);

	if (ret < 0)
		goto err_dealloc;
	else
		ret = 0;

	/* read request, copy returned data to return buf */
	if (!ret && requesttype == (USB_TYPE_VENDOR | USB_DIR_IN))
		memcpy(req->data, buf, req->size);

	kfree(buf);
	return ret;

err_dealloc:
	kfree(buf);
error:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

/* I2C */
static struct ec100_config ec168_ec100_config;

static int ec168_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msg[],
	int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	struct ec168_req req;
	int i = 0;
	int ret;

	if (num > 2)
		return -EINVAL;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	while (i < num) {
		if (num > i + 1 && (msg[i+1].flags & I2C_M_RD)) {
			if (msg[i].addr == ec168_ec100_config.demod_address) {
				if (msg[i].len < 1) {
					i = -EOPNOTSUPP;
					break;
				}
				req.cmd = READ_DEMOD;
				req.value = 0;
				req.index = 0xff00 + msg[i].buf[0]; /* reg */
				req.size = msg[i+1].len; /* bytes to read */
				req.data = &msg[i+1].buf[0];
				ret = ec168_ctrl_msg(d, &req);
				i += 2;
			} else {
				dev_err(&d->udev->dev, "%s: I2C read not " \
						"implemented\n",
						KBUILD_MODNAME);
				ret = -EOPNOTSUPP;
				i += 2;
			}
		} else {
			if (msg[i].addr == ec168_ec100_config.demod_address) {
				if (msg[i].len < 1) {
					i = -EOPNOTSUPP;
					break;
				}
				req.cmd = WRITE_DEMOD;
				req.value = msg[i].buf[1]; /* val */
				req.index = 0xff00 + msg[i].buf[0]; /* reg */
				req.size = 0;
				req.data = NULL;
				ret = ec168_ctrl_msg(d, &req);
				i += 1;
			} else {
				if (msg[i].len < 1) {
					i = -EOPNOTSUPP;
					break;
				}
				req.cmd = WRITE_I2C;
				req.value = msg[i].buf[0]; /* val */
				req.index = 0x0100 + msg[i].addr; /* I2C addr */
				req.size = msg[i].len-1;
				req.data = &msg[i].buf[1];
				ret = ec168_ctrl_msg(d, &req);
				i += 1;
			}
		}
		if (ret)
			goto error;

	}
	ret = i;

error:
	mutex_unlock(&d->i2c_mutex);
	return ret;
}

static u32 ec168_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static const struct i2c_algorithm ec168_i2c_algo = {
	.master_xfer   = ec168_i2c_xfer,
	.functionality = ec168_i2c_func,
};

/* Callbacks for DVB USB */
static int ec168_identify_state(struct dvb_usb_device *d, const char **name)
{
	int ret;
	u8 reply;
	struct ec168_req req = {GET_CONFIG, 0, 1, sizeof(reply), &reply};
	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	ret = ec168_ctrl_msg(d, &req);
	if (ret)
		goto error;

	dev_dbg(&d->udev->dev, "%s: reply=%02x\n", __func__, reply);

	if (reply == 0x01)
		ret = WARM;
	else
		ret = COLD;

	return ret;
error:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int ec168_download_firmware(struct dvb_usb_device *d,
		const struct firmware *fw)
{
	int ret, len, remaining;
	struct ec168_req req = {DOWNLOAD_FIRMWARE, 0, 0, 0, NULL};
	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	#define LEN_MAX 2048 /* max packet size */
	for (remaining = fw->size; remaining > 0; remaining -= LEN_MAX) {
		len = remaining;
		if (len > LEN_MAX)
			len = LEN_MAX;

		req.size = len;
		req.data = (u8 *) &fw->data[fw->size - remaining];
		req.index = fw->size - remaining;

		ret = ec168_ctrl_msg(d, &req);
		if (ret) {
			dev_err(&d->udev->dev,
					"%s: firmware download failed=%d\n",
					KBUILD_MODNAME, ret);
			goto error;
		}
	}

	req.size = 0;

	/* set "warm"? */
	req.cmd = SET_CONFIG;
	req.value = 0;
	req.index = 0x0001;
	ret = ec168_ctrl_msg(d, &req);
	if (ret)
		goto error;

	/* really needed - no idea what does */
	req.cmd = GPIO;
	req.value = 0;
	req.index = 0x0206;
	ret = ec168_ctrl_msg(d, &req);
	if (ret)
		goto error;

	/* activate tuner I2C? */
	req.cmd = WRITE_I2C;
	req.value = 0;
	req.index = 0x00c6;
	ret = ec168_ctrl_msg(d, &req);
	if (ret)
		goto error;

	return ret;
error:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static struct ec100_config ec168_ec100_config = {
	.demod_address = 0xff, /* not real address, demod is integrated */
};

static int ec168_ec100_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	adap->fe[0] = dvb_attach(ec100_attach, &ec168_ec100_config,
			&d->i2c_adap);
	if (adap->fe[0] == NULL)
		return -ENODEV;

	return 0;
}

static struct mxl5005s_config ec168_mxl5003s_config = {
	.i2c_address     = 0xc6,
	.if_freq         = IF_FREQ_4570000HZ,
	.xtal_freq       = CRYSTAL_FREQ_16000000HZ,
	.agc_mode        = MXL_SINGLE_AGC,
	.tracking_filter = MXL_TF_OFF,
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

static int ec168_mxl5003s_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	return dvb_attach(mxl5005s_attach, adap->fe[0], &d->i2c_adap,
			&ec168_mxl5003s_config) == NULL ? -ENODEV : 0;
}

static int ec168_streaming_ctrl(struct dvb_frontend *fe, int onoff)
{
	struct dvb_usb_device *d = fe_to_d(fe);
	struct ec168_req req = {STREAMING_CTRL, 0x7f01, 0x0202, 0, NULL};
	dev_dbg(&d->udev->dev, "%s: onoff=%d\n", __func__, onoff);

	if (onoff)
		req.index = 0x0102;
	return ec168_ctrl_msg(d, &req);
}

/* DVB USB Driver stuff */
/* bInterfaceNumber 0 is HID
 * bInterfaceNumber 1 is DVB-T */
static const struct dvb_usb_device_properties ec168_props = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,
	.bInterfaceNumber = 1,

	.identify_state = ec168_identify_state,
	.firmware = EC168_FIRMWARE,
	.download_firmware = ec168_download_firmware,

	.i2c_algo = &ec168_i2c_algo,
	.frontend_attach = ec168_ec100_frontend_attach,
	.tuner_attach = ec168_mxl5003s_tuner_attach,
	.streaming_ctrl = ec168_streaming_ctrl,

	.num_adapters = 1,
	.adapter = {
		{
			.stream = DVB_USB_STREAM_BULK(0x82, 6, 32 * 512),
		}
	},
};

static const struct usb_device_id ec168_id[] = {
	{ DVB_USB_DEVICE(USB_VID_E3C, USB_PID_E3C_EC168,
		     &ec168_props, "E3C EC168 reference design", NULL)},
	{ DVB_USB_DEVICE(USB_VID_E3C, USB_PID_E3C_EC168_2,
		     &ec168_props, "E3C EC168 reference design", NULL)},
	{ DVB_USB_DEVICE(USB_VID_E3C, USB_PID_E3C_EC168_3,
		     &ec168_props, "E3C EC168 reference design", NULL)},
	{ DVB_USB_DEVICE(USB_VID_E3C, USB_PID_E3C_EC168_4,
		     &ec168_props, "E3C EC168 reference design", NULL)},
	{ DVB_USB_DEVICE(USB_VID_E3C, USB_PID_E3C_EC168_5,
		     &ec168_props, "E3C EC168 reference design", NULL)},
	{}
};
MODULE_DEVICE_TABLE(usb, ec168_id);

static struct usb_driver ec168_driver = {
	.name = KBUILD_MODNAME,
	.id_table = ec168_id,
	.probe = dvb_usbv2_probe,
	.disconnect = dvb_usbv2_disconnect,
	.suspend = dvb_usbv2_suspend,
	.resume = dvb_usbv2_resume,
	.no_dynamic_id = 1,
	.soft_unbind = 1,
};

module_usb_driver(ec168_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("E3C EC168 driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(EC168_FIRMWARE);
