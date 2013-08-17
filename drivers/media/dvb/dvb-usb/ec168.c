/*
 * E3C EC168 DVB USB driver
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

#include "ec168.h"
#include "ec100.h"
#include "mxl5005s.h"

/* debug */
static int dvb_usb_ec168_debug;
module_param_named(debug, dvb_usb_ec168_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level" DVB_USB_DEBUG_STATUS);
DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static struct ec100_config ec168_ec100_config;

static int ec168_rw_udev(struct usb_device *udev, struct ec168_req *req)
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
		err("unknown command:%02x", req->cmd);
		ret = -EPERM;
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
		pipe = usb_sndctrlpipe(udev, 0);
	} else {
		/* read */
		pipe = usb_rcvctrlpipe(udev, 0);
	}

	msleep(1); /* avoid I2C errors */

	ret = usb_control_msg(udev, pipe, request, requesttype, req->value,
		req->index, buf, req->size, EC168_USB_TIMEOUT);

	ec168_debug_dump(request, requesttype, req->value, req->index, buf,
		req->size, deb_xfer);

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
	deb_info("%s: failed:%d\n", __func__, ret);
	return ret;
}

static int ec168_ctrl_msg(struct dvb_usb_device *d, struct ec168_req *req)
{
	return ec168_rw_udev(d->udev, req);
}

/* I2C */
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
				req.cmd = READ_DEMOD;
				req.value = 0;
				req.index = 0xff00 + msg[i].buf[0]; /* reg */
				req.size = msg[i+1].len; /* bytes to read */
				req.data = &msg[i+1].buf[0];
				ret = ec168_ctrl_msg(d, &req);
				i += 2;
			} else {
				err("I2C read not implemented");
				ret = -ENOSYS;
				i += 2;
			}
		} else {
			if (msg[i].addr == ec168_ec100_config.demod_address) {
				req.cmd = WRITE_DEMOD;
				req.value = msg[i].buf[1]; /* val */
				req.index = 0xff00 + msg[i].buf[0]; /* reg */
				req.size = 0;
				req.data = NULL;
				ret = ec168_ctrl_msg(d, &req);
				i += 1;
			} else {
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
	return i;
}


static u32 ec168_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm ec168_i2c_algo = {
	.master_xfer   = ec168_i2c_xfer,
	.functionality = ec168_i2c_func,
};

/* Callbacks for DVB USB */
static struct ec100_config ec168_ec100_config = {
	.demod_address = 0xff, /* not real address, demod is integrated */
};

static int ec168_ec100_frontend_attach(struct dvb_usb_adapter *adap)
{
	deb_info("%s:\n", __func__);
	adap->fe_adap[0].fe = dvb_attach(ec100_attach, &ec168_ec100_config,
		&adap->dev->i2c_adap);
	if (adap->fe_adap[0].fe == NULL)
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
	deb_info("%s:\n", __func__);
	return dvb_attach(mxl5005s_attach, adap->fe_adap[0].fe, &adap->dev->i2c_adap,
		&ec168_mxl5003s_config) == NULL ? -ENODEV : 0;
}

static int ec168_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	struct ec168_req req = {STREAMING_CTRL, 0x7f01, 0x0202, 0, NULL};
	deb_info("%s: onoff:%d\n", __func__, onoff);
	if (onoff)
		req.index = 0x0102;
	return ec168_ctrl_msg(adap->dev, &req);
}

static int ec168_download_firmware(struct usb_device *udev,
	const struct firmware *fw)
{
	int i, len, packets, remainder, ret;
	u16 addr = 0x0000; /* firmware start address */
	struct ec168_req req = {DOWNLOAD_FIRMWARE, 0, 0, 0, NULL};
	deb_info("%s:\n", __func__);

	#define FW_PACKET_MAX_DATA  2048
	packets = fw->size / FW_PACKET_MAX_DATA;
	remainder = fw->size % FW_PACKET_MAX_DATA;
	len = FW_PACKET_MAX_DATA;
	for (i = 0; i <= packets; i++) {
		if (i == packets)  /* set size of the last packet */
			len = remainder;

		req.size = len;
		req.data = (u8 *)(fw->data + i * FW_PACKET_MAX_DATA);
		req.index = addr;
		addr += FW_PACKET_MAX_DATA;

		ret = ec168_rw_udev(udev, &req);
		if (ret) {
			err("firmware download failed:%d packet:%d", ret, i);
			goto error;
		}
	}
	req.size = 0;

	/* set "warm"? */
	req.cmd = SET_CONFIG;
	req.value = 0;
	req.index = 0x0001;
	ret = ec168_rw_udev(udev, &req);
	if (ret)
		goto error;

	/* really needed - no idea what does */
	req.cmd = GPIO;
	req.value = 0;
	req.index = 0x0206;
	ret = ec168_rw_udev(udev, &req);
	if (ret)
		goto error;

	/* activate tuner I2C? */
	req.cmd = WRITE_I2C;
	req.value = 0;
	req.index = 0x00c6;
	ret = ec168_rw_udev(udev, &req);
	if (ret)
		goto error;

	return ret;
error:
	deb_info("%s: failed:%d\n", __func__, ret);
	return ret;
}

static int ec168_identify_state(struct usb_device *udev,
	struct dvb_usb_device_properties *props,
	struct dvb_usb_device_description **desc, int *cold)
{
	int ret;
	u8 reply;
	struct ec168_req req = {GET_CONFIG, 0, 1, sizeof(reply), &reply};
	deb_info("%s:\n", __func__);

	ret = ec168_rw_udev(udev, &req);
	if (ret)
		goto error;

	deb_info("%s: reply:%02x\n", __func__, reply);

	if (reply == 0x01)
		*cold = 0;
	else
		*cold = 1;

	return ret;
error:
	deb_info("%s: failed:%d\n", __func__, ret);
	return ret;
}

/* DVB USB Driver stuff */
static struct dvb_usb_device_properties ec168_properties;

static int ec168_probe(struct usb_interface *intf,
	const struct usb_device_id *id)
{
	int ret;
	deb_info("%s: interface:%d\n", __func__,
		intf->cur_altsetting->desc.bInterfaceNumber);

	ret = dvb_usb_device_init(intf, &ec168_properties, THIS_MODULE, NULL,
		adapter_nr);
	if (ret)
		goto error;

	return ret;
error:
	deb_info("%s: failed:%d\n", __func__, ret);
	return ret;
}

#define E3C_EC168_1689                          0
#define E3C_EC168_FFFA                          1
#define E3C_EC168_FFFB                          2
#define E3C_EC168_1001                          3
#define E3C_EC168_1002                          4

static struct usb_device_id ec168_id[] = {
	[E3C_EC168_1689] =
		{USB_DEVICE(USB_VID_E3C, USB_PID_E3C_EC168)},
	[E3C_EC168_FFFA] =
		{USB_DEVICE(USB_VID_E3C, USB_PID_E3C_EC168_2)},
	[E3C_EC168_FFFB] =
		{USB_DEVICE(USB_VID_E3C, USB_PID_E3C_EC168_3)},
	[E3C_EC168_1001] =
		{USB_DEVICE(USB_VID_E3C, USB_PID_E3C_EC168_4)},
	[E3C_EC168_1002] =
		{USB_DEVICE(USB_VID_E3C, USB_PID_E3C_EC168_5)},
	{} /* terminating entry */
};

MODULE_DEVICE_TABLE(usb, ec168_id);

static struct dvb_usb_device_properties ec168_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = DEVICE_SPECIFIC,
	.download_firmware = ec168_download_firmware,
	.firmware = "dvb-usb-ec168.fw",
	.no_reconnect = 1,

	.size_of_priv = 0,

	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.streaming_ctrl   = ec168_streaming_ctrl,
			.frontend_attach  = ec168_ec100_frontend_attach,
			.tuner_attach     = ec168_mxl5003s_tuner_attach,
			.stream = {
				.type = USB_BULK,
				.count = 6,
				.endpoint = 0x82,
				.u = {
					.bulk = {
						.buffersize = (32*512),
					}
				}
			},
		}},
		}
	},

	.identify_state = ec168_identify_state,

	.i2c_algo = &ec168_i2c_algo,

	.num_device_descs = 1,
	.devices = {
		{
			.name = "E3C EC168 DVB-T USB2.0 reference design",
			.cold_ids = {
				&ec168_id[E3C_EC168_1689],
				&ec168_id[E3C_EC168_FFFA],
				&ec168_id[E3C_EC168_FFFB],
				&ec168_id[E3C_EC168_1001],
				&ec168_id[E3C_EC168_1002],
				NULL},
			.warm_ids = {NULL},
		},
	}
};

static struct usb_driver ec168_driver = {
	.name       = "dvb_usb_ec168",
	.probe      = ec168_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table   = ec168_id,
};

module_usb_driver(ec168_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("E3C EC168 DVB-T USB2.0 driver");
MODULE_LICENSE("GPL");
