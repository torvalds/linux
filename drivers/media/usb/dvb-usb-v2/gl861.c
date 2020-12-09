// SPDX-License-Identifier: GPL-2.0-only
/* DVB USB compliant linux driver for GL861 USB2.0 devices.
 *
 * see Documentation/driver-api/media/drivers/dvb-usb.rst for more information
 */
#include <linux/string.h>

#include "dvb_usb.h"

#include "zl10353.h"
#include "qt1010.h"
#include "tc90522.h"
#include "dvb-pll.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

struct gl861 {
	/* USB control message buffer */
	u8 buf[16];

	struct i2c_adapter *demod_sub_i2c;
	struct i2c_client  *i2c_client_demod;
	struct i2c_client  *i2c_client_tuner;
};

#define CMD_WRITE_SHORT     0x01
#define CMD_READ            0x02
#define CMD_WRITE           0x03

static int gl861_ctrl_msg(struct dvb_usb_device *d, u8 request, u16 value,
			  u16 index, void *data, u16 size)
{
	struct gl861 *ctx = d_to_priv(d);
	struct usb_interface *intf = d->intf;
	int ret;
	unsigned int pipe;
	u8 requesttype;

	mutex_lock(&d->usb_mutex);

	switch (request) {
	case CMD_WRITE:
		memcpy(ctx->buf, data, size);
		fallthrough;
	case CMD_WRITE_SHORT:
		pipe = usb_sndctrlpipe(d->udev, 0);
		requesttype = USB_TYPE_VENDOR | USB_DIR_OUT;
		break;
	case CMD_READ:
		pipe = usb_rcvctrlpipe(d->udev, 0);
		requesttype = USB_TYPE_VENDOR | USB_DIR_IN;
		break;
	default:
		ret = -EINVAL;
		goto err_mutex_unlock;
	}

	ret = usb_control_msg(d->udev, pipe, request, requesttype, value,
			      index, ctx->buf, size, 200);
	dev_dbg(&intf->dev, "%d | %02x %02x %*ph %*ph %*ph %s %*ph\n",
		ret, requesttype, request, 2, &value, 2, &index, 2, &size,
		(requesttype & USB_DIR_IN) ? "<<<" : ">>>", size, ctx->buf);
	if (ret < 0)
		goto err_mutex_unlock;

	if (request == CMD_READ)
		memcpy(data, ctx->buf, size);

	usleep_range(1000, 2000); /* Avoid I2C errors */

	mutex_unlock(&d->usb_mutex);

	return 0;

err_mutex_unlock:
	mutex_unlock(&d->usb_mutex);
	dev_dbg(&intf->dev, "failed %d\n", ret);
	return ret;
}

static int gl861_short_write(struct dvb_usb_device *d, u8 addr, u8 reg, u8 val)
{
	return gl861_ctrl_msg(d, CMD_WRITE_SHORT,
			      (addr << 9) | val, reg, NULL, 0);
}

static int gl861_i2c_master_xfer(struct i2c_adapter *adap, struct i2c_msg msg[],
				 int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	struct usb_interface *intf = d->intf;
	struct gl861 *ctx = d_to_priv(d);
	int ret;
	u8 request, *data;
	u16 value, index, size;

	/* XXX: I2C adapter maximum data lengths are not tested */
	if (num == 1 && !(msg[0].flags & I2C_M_RD)) {
		/* I2C write */
		if (msg[0].len < 2 || msg[0].len > sizeof(ctx->buf)) {
			ret = -EOPNOTSUPP;
			goto err;
		}

		value = (msg[0].addr << 1) << 8;
		index = msg[0].buf[0];

		if (msg[0].len == 2) {
			request = CMD_WRITE_SHORT;
			value |= msg[0].buf[1];
			size = 0;
			data = NULL;
		} else {
			request = CMD_WRITE;
			size = msg[0].len - 1;
			data = &msg[0].buf[1];
		}

		ret = gl861_ctrl_msg(d, request, value, index, data, size);
	} else if (num == 2 && !(msg[0].flags & I2C_M_RD) &&
		   (msg[1].flags & I2C_M_RD)) {
		/* I2C write + read */
		if (msg[0].len > 1 || msg[1].len > sizeof(ctx->buf)) {
			ret = -EOPNOTSUPP;
			goto err;
		}

		value = (msg[0].addr << 1) << 8;
		index = msg[0].buf[0];
		request = CMD_READ;

		ret = gl861_ctrl_msg(d, request, value, index,
				     msg[1].buf, msg[1].len);
	} else if (num == 1 && (msg[0].flags & I2C_M_RD)) {
		/* I2C read */
		if (msg[0].len > sizeof(ctx->buf)) {
			ret = -EOPNOTSUPP;
			goto err;
		}
		value = (msg[0].addr << 1) << 8;
		index = 0x0100;
		request = CMD_READ;

		ret = gl861_ctrl_msg(d, request, value, index,
				     msg[0].buf, msg[0].len);
	} else {
		/* Unsupported I2C message */
		dev_dbg(&intf->dev, "unknown i2c msg, num %u\n", num);
		ret = -EOPNOTSUPP;
	}
	if (ret)
		goto err;

	return num;
err:
	dev_dbg(&intf->dev, "failed %d\n", ret);
	return ret;
}

static u32 gl861_i2c_functionality(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm gl861_i2c_algo = {
	.master_xfer   = gl861_i2c_master_xfer,
	.functionality = gl861_i2c_functionality,
};

/* Callbacks for DVB USB */
static struct zl10353_config gl861_zl10353_config = {
	.demod_address = 0x0f,
	.no_tuner = 1,
	.parallel_ts = 1,
};

static int gl861_frontend_attach(struct dvb_usb_adapter *adap)
{

	adap->fe[0] = dvb_attach(zl10353_attach, &gl861_zl10353_config,
		&adap_to_d(adap)->i2c_adap);
	if (adap->fe[0] == NULL)
		return -EIO;

	return 0;
}

static struct qt1010_config gl861_qt1010_config = {
	.i2c_address = 0x62
};

static int gl861_tuner_attach(struct dvb_usb_adapter *adap)
{
	return dvb_attach(qt1010_attach,
			  adap->fe[0], &adap_to_d(adap)->i2c_adap,
			  &gl861_qt1010_config) == NULL ? -ENODEV : 0;
}

static int gl861_init(struct dvb_usb_device *d)
{
	/*
	 * There is 2 interfaces. Interface 0 is for TV and interface 1 is
	 * for HID remote controller. Interface 0 has 2 alternate settings.
	 * For some reason we need to set interface explicitly, defaulted
	 * as alternate setting 1?
	 */
	return usb_set_interface(d->udev, 0, 0);
}

/* DVB USB Driver stuff */
static struct dvb_usb_device_properties gl861_props = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,

	.size_of_priv = sizeof(struct gl861),

	.i2c_algo = &gl861_i2c_algo,
	.frontend_attach = gl861_frontend_attach,
	.tuner_attach = gl861_tuner_attach,
	.init = gl861_init,

	.num_adapters = 1,
	.adapter = {
		{
			.stream = DVB_USB_STREAM_BULK(0x81, 7, 512),
		}
	}
};


/*
 * For Friio
 */
struct friio_config {
	struct i2c_board_info demod_info;
	struct tc90522_config demod_cfg;

	struct i2c_board_info tuner_info;
	struct dvb_pll_config tuner_cfg;
};

static const struct friio_config friio_config = {
	.demod_info = { I2C_BOARD_INFO(TC90522_I2C_DEV_TER, 0x18), },
	.demod_cfg = { .split_tuner_read_i2c = true, },
	.tuner_info = { I2C_BOARD_INFO("tua6034_friio", 0x60), },
};


/* GPIO control in Friio */

#define FRIIO_CTL_LNB (1 << 0)
#define FRIIO_CTL_STROBE (1 << 1)
#define FRIIO_CTL_CLK (1 << 2)
#define FRIIO_CTL_LED (1 << 3)

#define FRIIO_LED_RUNNING 0x6400ff64
#define FRIIO_LED_STOPPED 0x96ff00ff

/* control PIC16F676 attached to Friio */
static int friio_ext_ctl(struct dvb_usb_device *d,
			    u32 sat_color, int power_on)
{
	int i, ret;
	struct i2c_msg msg;
	u8 *buf;
	u32 mask;
	u8 power = (power_on) ? FRIIO_CTL_LNB : 0;

	buf = kmalloc(2, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	msg.addr = 0x00;
	msg.flags = 0;
	msg.len = 2;
	msg.buf = buf;
	buf[0] = 0x00;

	/* send 2bit header (&B10) */
	buf[1] = power | FRIIO_CTL_LED | FRIIO_CTL_STROBE;
	ret = i2c_transfer(&d->i2c_adap, &msg, 1);
	buf[1] |= FRIIO_CTL_CLK;
	ret += i2c_transfer(&d->i2c_adap, &msg, 1);

	buf[1] = power | FRIIO_CTL_STROBE;
	ret += i2c_transfer(&d->i2c_adap, &msg, 1);
	buf[1] |= FRIIO_CTL_CLK;
	ret += i2c_transfer(&d->i2c_adap, &msg, 1);

	/* send 32bit(satur, R, G, B) data in serial */
	mask = 1UL << 31;
	for (i = 0; i < 32; i++) {
		buf[1] = power | FRIIO_CTL_STROBE;
		if (sat_color & mask)
			buf[1] |= FRIIO_CTL_LED;
		ret += i2c_transfer(&d->i2c_adap, &msg, 1);
		buf[1] |= FRIIO_CTL_CLK;
		ret += i2c_transfer(&d->i2c_adap, &msg, 1);
		mask >>= 1;
	}

	/* set the strobe off */
	buf[1] = power;
	ret += i2c_transfer(&d->i2c_adap, &msg, 1);
	buf[1] |= FRIIO_CTL_CLK;
	ret += i2c_transfer(&d->i2c_adap, &msg, 1);

	kfree(buf);
	return (ret == 70) ? 0 : -EREMOTEIO;
}

/* init/config of gl861 for Friio */
/* NOTE:
 * This function cannot be moved to friio_init()/dvb_usbv2_init(),
 * because the init defined here includes a whole device reset,
 * it must be run early before any activities like I2C,
 * but friio_init() is called by dvb-usbv2 after {_frontend, _tuner}_attach(),
 * where I2C communication is used.
 * In addition, this reset is required in reset_resume() as well.
 * Thus this function is set to be called from _power_ctl().
 *
 * Since it will be called on the early init stage
 * where the i2c adapter is not initialized yet,
 * we cannot use i2c_transfer() here.
 */
static int friio_reset(struct dvb_usb_device *d)
{
	int i, ret;
	u8 wbuf[1], rbuf[2];

	static const u8 friio_init_cmds[][2] = {
		{0x33, 0x08}, {0x37, 0x40}, {0x3a, 0x1f}, {0x3b, 0xff},
		{0x3c, 0x1f}, {0x3d, 0xff}, {0x38, 0x00}, {0x35, 0x00},
		{0x39, 0x00}, {0x36, 0x00},
	};

	ret = usb_set_interface(d->udev, 0, 0);
	if (ret < 0)
		return ret;

	ret = gl861_short_write(d, 0x00, 0x11, 0x02);
	if (ret < 0)
		return ret;
	usleep_range(2000, 3000);

	ret = gl861_short_write(d, 0x00, 0x11, 0x00);
	if (ret < 0)
		return ret;

	/*
	 * Check if the dev is really a Friio White, since it might be
	 * another device, Friio Black, with the same VID/PID.
	 */

	usleep_range(1000, 2000);
	wbuf[0] = 0x80;
	ret = gl861_ctrl_msg(d, CMD_WRITE, 0x09 << 9, 0x03, wbuf, 1);
	if (ret < 0)
		return ret;

	usleep_range(2000, 3000);
	ret = gl861_ctrl_msg(d, CMD_READ, 0x09 << 9, 0x0100, rbuf, 2);
	if (ret < 0)
		return ret;
	if (rbuf[0] != 0xff || rbuf[1] != 0xff)
		return -ENODEV;


	usleep_range(1000, 2000);
	wbuf[0] = 0x80;
	ret = gl861_ctrl_msg(d, CMD_WRITE, 0x48 << 9, 0x03, wbuf, 1);
	if (ret < 0)
		return ret;

	usleep_range(2000, 3000);
	ret = gl861_ctrl_msg(d, CMD_READ, 0x48 << 9, 0x0100, rbuf, 2);
	if (ret < 0)
		return ret;
	if (rbuf[0] != 0xff || rbuf[1] != 0xff)
		return -ENODEV;

	ret = gl861_short_write(d, 0x00, 0x30, 0x04);
	if (ret < 0)
		return ret;

	ret = gl861_short_write(d, 0x00, 0x00, 0x01);
	if (ret < 0)
		return ret;

	ret = gl861_short_write(d, 0x00, 0x06, 0x0f);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(friio_init_cmds); i++) {
		ret = gl861_short_write(d, 0x00, friio_init_cmds[i][0],
					friio_init_cmds[i][1]);
		if (ret < 0)
			return ret;
	}
	return 0;
}

/*
 * DVB callbacks for Friio
 */

static int friio_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	return onoff ? friio_reset(d) : 0;
}

static int friio_frontend_attach(struct dvb_usb_adapter *adap)
{
	const struct i2c_board_info *info;
	struct dvb_usb_device *d;
	struct tc90522_config cfg;
	struct i2c_client *cl;
	struct gl861 *priv;

	info = &friio_config.demod_info;
	cfg = friio_config.demod_cfg;
	d = adap_to_d(adap);
	cl = dvb_module_probe("tc90522", info->type,
			      &d->i2c_adap, info->addr, &cfg);
	if (!cl)
		return -ENODEV;
	adap->fe[0] = cfg.fe;

	priv = adap_to_priv(adap);
	priv->i2c_client_demod = cl;
	priv->demod_sub_i2c = cfg.tuner_i2c;
	return 0;
}

static int friio_frontend_detach(struct dvb_usb_adapter *adap)
{
	struct gl861 *priv;

	priv = adap_to_priv(adap);
	dvb_module_release(priv->i2c_client_demod);
	return 0;
}

static int friio_tuner_attach(struct dvb_usb_adapter *adap)
{
	const struct i2c_board_info *info;
	struct dvb_pll_config cfg;
	struct i2c_client *cl;
	struct gl861 *priv;

	priv = adap_to_priv(adap);
	info = &friio_config.tuner_info;
	cfg = friio_config.tuner_cfg;
	cfg.fe = adap->fe[0];

	cl = dvb_module_probe("dvb_pll", info->type,
			      priv->demod_sub_i2c, info->addr, &cfg);
	if (!cl)
		return -ENODEV;
	priv->i2c_client_tuner = cl;
	return 0;
}

static int friio_tuner_detach(struct dvb_usb_adapter *adap)
{
	struct gl861 *priv;

	priv = adap_to_priv(adap);
	dvb_module_release(priv->i2c_client_tuner);
	return 0;
}

static int friio_init(struct dvb_usb_device *d)
{
	int i;
	int ret;
	struct gl861 *priv;

	static const u8 demod_init[][2] = {
		{0x01, 0x40}, {0x04, 0x38}, {0x05, 0x40}, {0x07, 0x40},
		{0x0f, 0x4f}, {0x11, 0x21}, {0x12, 0x0b}, {0x13, 0x2f},
		{0x14, 0x31}, {0x16, 0x02}, {0x21, 0xc4}, {0x22, 0x20},
		{0x2c, 0x79}, {0x2d, 0x34}, {0x2f, 0x00}, {0x30, 0x28},
		{0x31, 0x31}, {0x32, 0xdf}, {0x38, 0x01}, {0x39, 0x78},
		{0x3b, 0x33}, {0x3c, 0x33}, {0x48, 0x90}, {0x51, 0x68},
		{0x5e, 0x38}, {0x71, 0x00}, {0x72, 0x08}, {0x77, 0x00},
		{0xc0, 0x21}, {0xc1, 0x10}, {0xe4, 0x1a}, {0xea, 0x1f},
		{0x77, 0x00}, {0x71, 0x00}, {0x71, 0x00}, {0x76, 0x0c},
	};

	/* power on LNA? */
	ret = friio_ext_ctl(d, FRIIO_LED_STOPPED, true);
	if (ret < 0)
		return ret;
	msleep(20);

	/* init/config demod */
	priv = d_to_priv(d);
	for (i = 0; i < ARRAY_SIZE(demod_init); i++) {
		int ret;

		ret = i2c_master_send(priv->i2c_client_demod, demod_init[i], 2);
		if (ret < 0)
			return ret;
	}
	msleep(100);
	return 0;
}

static void friio_exit(struct dvb_usb_device *d)
{
	friio_ext_ctl(d, FRIIO_LED_STOPPED, false);
}

static int friio_streaming_ctrl(struct dvb_frontend *fe, int onoff)
{
	u32 led_color;

	led_color = onoff ? FRIIO_LED_RUNNING : FRIIO_LED_STOPPED;
	return friio_ext_ctl(fe_to_d(fe), led_color, true);
}


static struct dvb_usb_device_properties friio_props = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,

	.size_of_priv = sizeof(struct gl861),

	.i2c_algo = &gl861_i2c_algo,
	.power_ctrl = friio_power_ctrl,
	.frontend_attach = friio_frontend_attach,
	.frontend_detach = friio_frontend_detach,
	.tuner_attach = friio_tuner_attach,
	.tuner_detach = friio_tuner_detach,
	.init = friio_init,
	.exit = friio_exit,
	.streaming_ctrl = friio_streaming_ctrl,

	.num_adapters = 1,
	.adapter = {
		{
			.stream = DVB_USB_STREAM_BULK(0x01, 8, 16384),
		}
	}
};

static const struct usb_device_id gl861_id_table[] = {
	{ DVB_USB_DEVICE(USB_VID_MSI, USB_PID_MSI_MEGASKY580_55801,
		&gl861_props, "MSI Mega Sky 55801 DVB-T USB2.0", NULL) },
	{ DVB_USB_DEVICE(USB_VID_ALINK, USB_PID_ALINK_DTU,
		&gl861_props, "A-LINK DTU DVB-T USB2.0", NULL) },
	{ DVB_USB_DEVICE(USB_VID_774, USB_PID_FRIIO_WHITE,
		&friio_props, "774 Friio White ISDB-T USB2.0", NULL) },
	{ }
};
MODULE_DEVICE_TABLE(usb, gl861_id_table);

static struct usb_driver gl861_usb_driver = {
	.name = KBUILD_MODNAME,
	.id_table = gl861_id_table,
	.probe = dvb_usbv2_probe,
	.disconnect = dvb_usbv2_disconnect,
	.suspend = dvb_usbv2_suspend,
	.resume = dvb_usbv2_resume,
	.reset_resume = dvb_usbv2_reset_resume,
	.no_dynamic_id = 1,
	.soft_unbind = 1,
};

module_usb_driver(gl861_usb_driver);

MODULE_AUTHOR("Carl Lundqvist <comabug@gmail.com>");
MODULE_DESCRIPTION("Driver MSI Mega Sky 580 DVB-T USB2.0 / GL861");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");
