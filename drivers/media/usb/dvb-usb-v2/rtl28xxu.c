/*
 * Realtek RTL28xxU DVB USB driver
 *
 * Copyright (C) 2009 Antti Palosaari <crope@iki.fi>
 * Copyright (C) 2011 Antti Palosaari <crope@iki.fi>
 * Copyright (C) 2012 Thomas Mair <thomas.mair86@googlemail.com>
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
 *    You should have received a copy of the GNU General Public License along
 *    with this program; if not, write to the Free Software Foundation, Inc.,
 *    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "rtl28xxu.h"

#include "rtl2830.h"
#include "rtl2832.h"

#include "qt1010.h"
#include "mt2060.h"
#include "mxl5005s.h"
#include "fc0012.h"
#include "fc0013.h"
#include "e4000.h"
#include "fc2580.h"
#include "tua9001.h"
#include "r820t.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static int rtl28xxu_ctrl_msg(struct dvb_usb_device *d, struct rtl28xxu_req *req)
{
	int ret;
	unsigned int pipe;
	u8 requesttype;
	u8 *buf;

	buf = kmalloc(req->size, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto err;
	}

	if (req->index & CMD_WR_FLAG) {
		/* write */
		memcpy(buf, req->data, req->size);
		requesttype = (USB_TYPE_VENDOR | USB_DIR_OUT);
		pipe = usb_sndctrlpipe(d->udev, 0);
	} else {
		/* read */
		requesttype = (USB_TYPE_VENDOR | USB_DIR_IN);
		pipe = usb_rcvctrlpipe(d->udev, 0);
	}

	ret = usb_control_msg(d->udev, pipe, 0, requesttype, req->value,
			req->index, buf, req->size, 1000);

	dvb_usb_dbg_usb_control_msg(d->udev, 0, requesttype, req->value,
			req->index, buf, req->size);

	if (ret > 0)
		ret = 0;

	/* read request, copy returned data to return buf */
	if (!ret && requesttype == (USB_TYPE_VENDOR | USB_DIR_IN))
		memcpy(req->data, buf, req->size);

	kfree(buf);

	if (ret)
		goto err;

	return ret;
err:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int rtl28xx_wr_regs(struct dvb_usb_device *d, u16 reg, u8 *val, int len)
{
	struct rtl28xxu_req req;

	if (reg < 0x3000)
		req.index = CMD_USB_WR;
	else if (reg < 0x4000)
		req.index = CMD_SYS_WR;
	else
		req.index = CMD_IR_WR;

	req.value = reg;
	req.size = len;
	req.data = val;

	return rtl28xxu_ctrl_msg(d, &req);
}

static int rtl2831_rd_regs(struct dvb_usb_device *d, u16 reg, u8 *val, int len)
{
	struct rtl28xxu_req req;

	if (reg < 0x3000)
		req.index = CMD_USB_RD;
	else if (reg < 0x4000)
		req.index = CMD_SYS_RD;
	else
		req.index = CMD_IR_RD;

	req.value = reg;
	req.size = len;
	req.data = val;

	return rtl28xxu_ctrl_msg(d, &req);
}

static int rtl28xx_wr_reg(struct dvb_usb_device *d, u16 reg, u8 val)
{
	return rtl28xx_wr_regs(d, reg, &val, 1);
}

static int rtl28xx_rd_reg(struct dvb_usb_device *d, u16 reg, u8 *val)
{
	return rtl2831_rd_regs(d, reg, val, 1);
}

static int rtl28xx_wr_reg_mask(struct dvb_usb_device *d, u16 reg, u8 val,
		u8 mask)
{
	int ret;
	u8 tmp;

	/* no need for read if whole reg is written */
	if (mask != 0xff) {
		ret = rtl28xx_rd_reg(d, reg, &tmp);
		if (ret)
			return ret;

		val &= mask;
		tmp &= ~mask;
		val |= tmp;
	}

	return rtl28xx_wr_reg(d, reg, val);
}

/* I2C */
static int rtl28xxu_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msg[],
	int num)
{
	int ret;
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	struct rtl28xxu_priv *priv = d->priv;
	struct rtl28xxu_req req;

	/*
	 * It is not known which are real I2C bus xfer limits, but testing
	 * with RTL2831U + MT2060 gives max RD 24 and max WR 22 bytes.
	 * TODO: find out RTL2832U lens
	 */

	/*
	 * I2C adapter logic looks rather complicated due to fact it handles
	 * three different access methods. Those methods are;
	 * 1) integrated demod access
	 * 2) old I2C access
	 * 3) new I2C access
	 *
	 * Used method is selected in order 1, 2, 3. Method 3 can handle all
	 * requests but there is two reasons why not use it always;
	 * 1) It is most expensive, usually two USB messages are needed
	 * 2) At least RTL2831U does not support it
	 *
	 * Method 3 is needed in case of I2C write+read (typical register read)
	 * where write is more than one byte.
	 */

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	if (num == 2 && !(msg[0].flags & I2C_M_RD) &&
		(msg[1].flags & I2C_M_RD)) {
		if (msg[0].len > 24 || msg[1].len > 24) {
			/* TODO: check msg[0].len max */
			ret = -EOPNOTSUPP;
			goto err_mutex_unlock;
		} else if (msg[0].addr == 0x10) {
			/* method 1 - integrated demod */
			req.value = (msg[0].buf[0] << 8) | (msg[0].addr << 1);
			req.index = CMD_DEMOD_RD | priv->page;
			req.size = msg[1].len;
			req.data = &msg[1].buf[0];
			ret = rtl28xxu_ctrl_msg(d, &req);
		} else if (msg[0].len < 2) {
			/* method 2 - old I2C */
			req.value = (msg[0].buf[0] << 8) | (msg[0].addr << 1);
			req.index = CMD_I2C_RD;
			req.size = msg[1].len;
			req.data = &msg[1].buf[0];
			ret = rtl28xxu_ctrl_msg(d, &req);
		} else {
			/* method 3 - new I2C */
			req.value = (msg[0].addr << 1);
			req.index = CMD_I2C_DA_WR;
			req.size = msg[0].len;
			req.data = msg[0].buf;
			ret = rtl28xxu_ctrl_msg(d, &req);
			if (ret)
				goto err_mutex_unlock;

			req.value = (msg[0].addr << 1);
			req.index = CMD_I2C_DA_RD;
			req.size = msg[1].len;
			req.data = msg[1].buf;
			ret = rtl28xxu_ctrl_msg(d, &req);
		}
	} else if (num == 1 && !(msg[0].flags & I2C_M_RD)) {
		if (msg[0].len > 22) {
			/* TODO: check msg[0].len max */
			ret = -EOPNOTSUPP;
			goto err_mutex_unlock;
		} else if (msg[0].addr == 0x10) {
			/* method 1 - integrated demod */
			if (msg[0].buf[0] == 0x00) {
				/* save demod page for later demod access */
				priv->page = msg[0].buf[1];
				ret = 0;
			} else {
				req.value = (msg[0].buf[0] << 8) |
					(msg[0].addr << 1);
				req.index = CMD_DEMOD_WR | priv->page;
				req.size = msg[0].len-1;
				req.data = &msg[0].buf[1];
				ret = rtl28xxu_ctrl_msg(d, &req);
			}
		} else if (msg[0].len < 23) {
			/* method 2 - old I2C */
			req.value = (msg[0].buf[0] << 8) | (msg[0].addr << 1);
			req.index = CMD_I2C_WR;
			req.size = msg[0].len-1;
			req.data = &msg[0].buf[1];
			ret = rtl28xxu_ctrl_msg(d, &req);
		} else {
			/* method 3 - new I2C */
			req.value = (msg[0].addr << 1);
			req.index = CMD_I2C_DA_WR;
			req.size = msg[0].len;
			req.data = msg[0].buf;
			ret = rtl28xxu_ctrl_msg(d, &req);
		}
	} else {
		ret = -EINVAL;
	}

err_mutex_unlock:
	mutex_unlock(&d->i2c_mutex);

	return ret ? ret : num;
}

static u32 rtl28xxu_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm rtl28xxu_i2c_algo = {
	.master_xfer   = rtl28xxu_i2c_xfer,
	.functionality = rtl28xxu_i2c_func,
};

static int rtl2831u_read_config(struct dvb_usb_device *d)
{
	struct rtl28xxu_priv *priv = d_to_priv(d);
	int ret;
	u8 buf[1];
	/* open RTL2831U/RTL2830 I2C gate */
	struct rtl28xxu_req req_gate_open = {0x0120, 0x0011, 0x0001, "\x08"};
	/* tuner probes */
	struct rtl28xxu_req req_mt2060 = {0x00c0, CMD_I2C_RD, 1, buf};
	struct rtl28xxu_req req_qt1010 = {0x0fc4, CMD_I2C_RD, 1, buf};

	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	/*
	 * RTL2831U GPIOs
	 * =========================================================
	 * GPIO0 | tuner#0 | 0 off | 1 on  | MXL5005S (?)
	 * GPIO2 | LED     | 0 off | 1 on  |
	 * GPIO4 | tuner#1 | 0 on  | 1 off | MT2060
	 */

	/* GPIO direction */
	ret = rtl28xx_wr_reg(d, SYS_GPIO_DIR, 0x0a);
	if (ret)
		goto err;

	/* enable as output GPIO0, GPIO2, GPIO4 */
	ret = rtl28xx_wr_reg(d, SYS_GPIO_OUT_EN, 0x15);
	if (ret)
		goto err;

	/*
	 * Probe used tuner. We need to know used tuner before demod attach
	 * since there is some demod params needed to set according to tuner.
	 */

	/* demod needs some time to wake up */
	msleep(20);

	priv->tuner_name = "NONE";

	/* open demod I2C gate */
	ret = rtl28xxu_ctrl_msg(d, &req_gate_open);
	if (ret)
		goto err;

	/* check QT1010 ID(?) register; reg=0f val=2c */
	ret = rtl28xxu_ctrl_msg(d, &req_qt1010);
	if (ret == 0 && buf[0] == 0x2c) {
		priv->tuner = TUNER_RTL2830_QT1010;
		priv->tuner_name = "QT1010";
		goto found;
	}

	/* open demod I2C gate */
	ret = rtl28xxu_ctrl_msg(d, &req_gate_open);
	if (ret)
		goto err;

	/* check MT2060 ID register; reg=00 val=63 */
	ret = rtl28xxu_ctrl_msg(d, &req_mt2060);
	if (ret == 0 && buf[0] == 0x63) {
		priv->tuner = TUNER_RTL2830_MT2060;
		priv->tuner_name = "MT2060";
		goto found;
	}

	/* assume MXL5005S */
	priv->tuner = TUNER_RTL2830_MXL5005S;
	priv->tuner_name = "MXL5005S";
	goto found;

found:
	dev_dbg(&d->udev->dev, "%s: tuner=%s\n", __func__, priv->tuner_name);

	return 0;
err:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int rtl2832u_read_config(struct dvb_usb_device *d)
{
	struct rtl28xxu_priv *priv = d_to_priv(d);
	int ret;
	u8 buf[2];
	/* open RTL2832U/RTL2832 I2C gate */
	struct rtl28xxu_req req_gate_open = {0x0120, 0x0011, 0x0001, "\x18"};
	/* close RTL2832U/RTL2832 I2C gate */
	struct rtl28xxu_req req_gate_close = {0x0120, 0x0011, 0x0001, "\x10"};
	/* tuner probes */
	struct rtl28xxu_req req_fc0012 = {0x00c6, CMD_I2C_RD, 1, buf};
	struct rtl28xxu_req req_fc0013 = {0x00c6, CMD_I2C_RD, 1, buf};
	struct rtl28xxu_req req_mt2266 = {0x00c0, CMD_I2C_RD, 1, buf};
	struct rtl28xxu_req req_fc2580 = {0x01ac, CMD_I2C_RD, 1, buf};
	struct rtl28xxu_req req_mt2063 = {0x00c0, CMD_I2C_RD, 1, buf};
	struct rtl28xxu_req req_max3543 = {0x00c0, CMD_I2C_RD, 1, buf};
	struct rtl28xxu_req req_tua9001 = {0x7ec0, CMD_I2C_RD, 2, buf};
	struct rtl28xxu_req req_mxl5007t = {0xd9c0, CMD_I2C_RD, 1, buf};
	struct rtl28xxu_req req_e4000 = {0x02c8, CMD_I2C_RD, 1, buf};
	struct rtl28xxu_req req_tda18272 = {0x00c0, CMD_I2C_RD, 2, buf};
	struct rtl28xxu_req req_r820t = {0x0034, CMD_I2C_RD, 5, buf};

	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	/* enable GPIO3 and GPIO6 as output */
	ret = rtl28xx_wr_reg_mask(d, SYS_GPIO_DIR, 0x00, 0x40);
	if (ret)
		goto err;

	ret = rtl28xx_wr_reg_mask(d, SYS_GPIO_OUT_EN, 0x48, 0x48);
	if (ret)
		goto err;

	/*
	 * Probe used tuner. We need to know used tuner before demod attach
	 * since there is some demod params needed to set according to tuner.
	 */

	/* open demod I2C gate */
	ret = rtl28xxu_ctrl_msg(d, &req_gate_open);
	if (ret)
		goto err;

	priv->tuner_name = "NONE";

	/* check FC0012 ID register; reg=00 val=a1 */
	ret = rtl28xxu_ctrl_msg(d, &req_fc0012);
	if (ret == 0 && buf[0] == 0xa1) {
		priv->tuner = TUNER_RTL2832_FC0012;
		priv->tuner_name = "FC0012";
		goto found;
	}

	/* check FC0013 ID register; reg=00 val=a3 */
	ret = rtl28xxu_ctrl_msg(d, &req_fc0013);
	if (ret == 0 && buf[0] == 0xa3) {
		priv->tuner = TUNER_RTL2832_FC0013;
		priv->tuner_name = "FC0013";
		goto found;
	}

	/* check MT2266 ID register; reg=00 val=85 */
	ret = rtl28xxu_ctrl_msg(d, &req_mt2266);
	if (ret == 0 && buf[0] == 0x85) {
		priv->tuner = TUNER_RTL2832_MT2266;
		priv->tuner_name = "MT2266";
		goto found;
	}

	/* check FC2580 ID register; reg=01 val=56 */
	ret = rtl28xxu_ctrl_msg(d, &req_fc2580);
	if (ret == 0 && buf[0] == 0x56) {
		priv->tuner = TUNER_RTL2832_FC2580;
		priv->tuner_name = "FC2580";
		goto found;
	}

	/* check MT2063 ID register; reg=00 val=9e || 9c */
	ret = rtl28xxu_ctrl_msg(d, &req_mt2063);
	if (ret == 0 && (buf[0] == 0x9e || buf[0] == 0x9c)) {
		priv->tuner = TUNER_RTL2832_MT2063;
		priv->tuner_name = "MT2063";
		goto found;
	}

	/* check MAX3543 ID register; reg=00 val=38 */
	ret = rtl28xxu_ctrl_msg(d, &req_max3543);
	if (ret == 0 && buf[0] == 0x38) {
		priv->tuner = TUNER_RTL2832_MAX3543;
		priv->tuner_name = "MAX3543";
		goto found;
	}

	/* check TUA9001 ID register; reg=7e val=2328 */
	ret = rtl28xxu_ctrl_msg(d, &req_tua9001);
	if (ret == 0 && buf[0] == 0x23 && buf[1] == 0x28) {
		priv->tuner = TUNER_RTL2832_TUA9001;
		priv->tuner_name = "TUA9001";
		goto found;
	}

	/* check MXL5007R ID register; reg=d9 val=14 */
	ret = rtl28xxu_ctrl_msg(d, &req_mxl5007t);
	if (ret == 0 && buf[0] == 0x14) {
		priv->tuner = TUNER_RTL2832_MXL5007T;
		priv->tuner_name = "MXL5007T";
		goto found;
	}

	/* check E4000 ID register; reg=02 val=40 */
	ret = rtl28xxu_ctrl_msg(d, &req_e4000);
	if (ret == 0 && buf[0] == 0x40) {
		priv->tuner = TUNER_RTL2832_E4000;
		priv->tuner_name = "E4000";
		goto found;
	}

	/* check TDA18272 ID register; reg=00 val=c760  */
	ret = rtl28xxu_ctrl_msg(d, &req_tda18272);
	if (ret == 0 && (buf[0] == 0xc7 || buf[1] == 0x60)) {
		priv->tuner = TUNER_RTL2832_TDA18272;
		priv->tuner_name = "TDA18272";
		goto found;
	}

	/* check R820T by reading tuner stats at I2C addr 0x1a */
	ret = rtl28xxu_ctrl_msg(d, &req_r820t);
	if (ret == 0) {
		priv->tuner = TUNER_RTL2832_R820T;
		priv->tuner_name = "R820T";
		goto found;
	}

found:
	dev_dbg(&d->udev->dev, "%s: tuner=%s\n", __func__, priv->tuner_name);

	/* close demod I2C gate */
	ret = rtl28xxu_ctrl_msg(d, &req_gate_close);
	if (ret < 0)
		goto err;

	return 0;
err:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static struct rtl2830_config rtl28xxu_rtl2830_mt2060_config = {
	.i2c_addr = 0x10, /* 0x20 */
	.xtal = 28800000,
	.ts_mode = 0,
	.spec_inv = 1,
	.vtop = 0x20,
	.krf = 0x04,
	.agc_targ_val = 0x2d,

};

static struct rtl2830_config rtl28xxu_rtl2830_qt1010_config = {
	.i2c_addr = 0x10, /* 0x20 */
	.xtal = 28800000,
	.ts_mode = 0,
	.spec_inv = 1,
	.vtop = 0x20,
	.krf = 0x04,
	.agc_targ_val = 0x2d,
};

static struct rtl2830_config rtl28xxu_rtl2830_mxl5005s_config = {
	.i2c_addr = 0x10, /* 0x20 */
	.xtal = 28800000,
	.ts_mode = 0,
	.spec_inv = 0,
	.vtop = 0x3f,
	.krf = 0x04,
	.agc_targ_val = 0x3e,
};

static int rtl2831u_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	struct rtl28xxu_priv *priv = d_to_priv(d);
	struct rtl2830_config *rtl2830_config;
	int ret;

	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	switch (priv->tuner) {
	case TUNER_RTL2830_QT1010:
		rtl2830_config = &rtl28xxu_rtl2830_qt1010_config;
		break;
	case TUNER_RTL2830_MT2060:
		rtl2830_config = &rtl28xxu_rtl2830_mt2060_config;
		break;
	case TUNER_RTL2830_MXL5005S:
		rtl2830_config = &rtl28xxu_rtl2830_mxl5005s_config;
		break;
	default:
		dev_err(&d->udev->dev, "%s: unknown tuner=%s\n",
				KBUILD_MODNAME, priv->tuner_name);
		ret = -ENODEV;
		goto err;
	}

	/* attach demodulator */
	adap->fe[0] = dvb_attach(rtl2830_attach, rtl2830_config, &d->i2c_adap);
	if (!adap->fe[0]) {
		ret = -ENODEV;
		goto err;
	}

	return 0;
err:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static struct rtl2832_config rtl28xxu_rtl2832_fc0012_config = {
	.i2c_addr = 0x10, /* 0x20 */
	.xtal = 28800000,
	.if_dvbt = 0,
	.tuner = TUNER_RTL2832_FC0012
};

static struct rtl2832_config rtl28xxu_rtl2832_fc0013_config = {
	.i2c_addr = 0x10, /* 0x20 */
	.xtal = 28800000,
	.if_dvbt = 0,
	.tuner = TUNER_RTL2832_FC0013
};

static struct rtl2832_config rtl28xxu_rtl2832_tua9001_config = {
	.i2c_addr = 0x10, /* 0x20 */
	.xtal = 28800000,
	.tuner = TUNER_RTL2832_TUA9001,
};

static struct rtl2832_config rtl28xxu_rtl2832_e4000_config = {
	.i2c_addr = 0x10, /* 0x20 */
	.xtal = 28800000,
	.tuner = TUNER_RTL2832_E4000,
};

static struct rtl2832_config rtl28xxu_rtl2832_r820t_config = {
	.i2c_addr = 0x10,
	.xtal = 28800000,
	.tuner = TUNER_RTL2832_R820T,
};

static int rtl2832u_fc0012_tuner_callback(struct dvb_usb_device *d,
		int cmd, int arg)
{
	int ret;
	u8 val;

	dev_dbg(&d->udev->dev, "%s: cmd=%d arg=%d\n", __func__, cmd, arg);

	switch (cmd) {
	case FC_FE_CALLBACK_VHF_ENABLE:
		/* set output values */
		ret = rtl28xx_rd_reg(d, SYS_GPIO_OUT_VAL, &val);
		if (ret)
			goto err;

		if (arg)
			val &= 0xbf; /* set GPIO6 low */
		else
			val |= 0x40; /* set GPIO6 high */


		ret = rtl28xx_wr_reg(d, SYS_GPIO_OUT_VAL, val);
		if (ret)
			goto err;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}
	return 0;
err:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int rtl2832u_tua9001_tuner_callback(struct dvb_usb_device *d,
		int cmd, int arg)
{
	int ret;
	u8 val;

	dev_dbg(&d->udev->dev, "%s: cmd=%d arg=%d\n", __func__, cmd, arg);

	/*
	 * CEN     always enabled by hardware wiring
	 * RESETN  GPIO4
	 * RXEN    GPIO1
	 */

	switch (cmd) {
	case TUA9001_CMD_RESETN:
		if (arg)
			val = (1 << 4);
		else
			val = (0 << 4);

		ret = rtl28xx_wr_reg_mask(d, SYS_GPIO_OUT_VAL, val, 0x10);
		if (ret)
			goto err;
		break;
	case TUA9001_CMD_RXEN:
		if (arg)
			val = (1 << 1);
		else
			val = (0 << 1);

		ret = rtl28xx_wr_reg_mask(d, SYS_GPIO_OUT_VAL, val, 0x02);
		if (ret)
			goto err;
		break;
	}

	return 0;
err:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int rtl2832u_tuner_callback(struct dvb_usb_device *d, int cmd, int arg)
{
	struct rtl28xxu_priv *priv = d->priv;

	switch (priv->tuner) {
	case TUNER_RTL2832_FC0012:
		return rtl2832u_fc0012_tuner_callback(d, cmd, arg);
	case TUNER_RTL2832_TUA9001:
		return rtl2832u_tua9001_tuner_callback(d, cmd, arg);
	default:
		break;
	}

	return 0;
}

static int rtl2832u_frontend_callback(void *adapter_priv, int component,
		int cmd, int arg)
{
	struct i2c_adapter *adap = adapter_priv;
	struct dvb_usb_device *d = i2c_get_adapdata(adap);

	dev_dbg(&d->udev->dev, "%s: component=%d cmd=%d arg=%d\n",
			__func__, component, cmd, arg);

	switch (component) {
	case DVB_FRONTEND_COMPONENT_TUNER:
		return rtl2832u_tuner_callback(d, cmd, arg);
	default:
		break;
	}

	return 0;
}

static int rtl2832u_frontend_attach(struct dvb_usb_adapter *adap)
{
	int ret;
	struct dvb_usb_device *d = adap_to_d(adap);
	struct rtl28xxu_priv *priv = d_to_priv(d);
	struct rtl2832_config *rtl2832_config;

	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	switch (priv->tuner) {
	case TUNER_RTL2832_FC0012:
		rtl2832_config = &rtl28xxu_rtl2832_fc0012_config;
		break;
	case TUNER_RTL2832_FC0013:
		rtl2832_config = &rtl28xxu_rtl2832_fc0013_config;
		break;
	case TUNER_RTL2832_FC2580:
		/* FIXME: do not abuse fc0012 settings */
		rtl2832_config = &rtl28xxu_rtl2832_fc0012_config;
		break;
	case TUNER_RTL2832_TUA9001:
		rtl2832_config = &rtl28xxu_rtl2832_tua9001_config;
		break;
	case TUNER_RTL2832_E4000:
		rtl2832_config = &rtl28xxu_rtl2832_e4000_config;
		break;
	case TUNER_RTL2832_R820T:
		rtl2832_config = &rtl28xxu_rtl2832_r820t_config;
		break;
	default:
		dev_err(&d->udev->dev, "%s: unknown tuner=%s\n",
				KBUILD_MODNAME, priv->tuner_name);
		ret = -ENODEV;
		goto err;
	}

	/* attach demodulator */
	adap->fe[0] = dvb_attach(rtl2832_attach, rtl2832_config, &d->i2c_adap);
	if (!adap->fe[0]) {
		ret = -ENODEV;
		goto err;
	}

	/* set fe callback */
	adap->fe[0]->callback = rtl2832u_frontend_callback;

	return 0;
err:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static struct qt1010_config rtl28xxu_qt1010_config = {
	.i2c_address = 0x62, /* 0xc4 */
};

static struct mt2060_config rtl28xxu_mt2060_config = {
	.i2c_address = 0x60, /* 0xc0 */
	.clock_out = 0,
};

static struct mxl5005s_config rtl28xxu_mxl5005s_config = {
	.i2c_address     = 0x63, /* 0xc6 */
	.if_freq         = IF_FREQ_4570000HZ,
	.xtal_freq       = CRYSTAL_FREQ_16000000HZ,
	.agc_mode        = MXL_SINGLE_AGC,
	.tracking_filter = MXL_TF_C_H,
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

static int rtl2831u_tuner_attach(struct dvb_usb_adapter *adap)
{
	int ret;
	struct dvb_usb_device *d = adap_to_d(adap);
	struct rtl28xxu_priv *priv = d_to_priv(d);
	struct i2c_adapter *rtl2830_tuner_i2c;
	struct dvb_frontend *fe;

	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	/* use rtl2830 driver I2C adapter, for more info see rtl2830 driver */
	rtl2830_tuner_i2c = rtl2830_get_tuner_i2c_adapter(adap->fe[0]);

	switch (priv->tuner) {
	case TUNER_RTL2830_QT1010:
		fe = dvb_attach(qt1010_attach, adap->fe[0],
				rtl2830_tuner_i2c, &rtl28xxu_qt1010_config);
		break;
	case TUNER_RTL2830_MT2060:
		fe = dvb_attach(mt2060_attach, adap->fe[0],
				rtl2830_tuner_i2c, &rtl28xxu_mt2060_config,
				1220);
		break;
	case TUNER_RTL2830_MXL5005S:
		fe = dvb_attach(mxl5005s_attach, adap->fe[0],
				rtl2830_tuner_i2c, &rtl28xxu_mxl5005s_config);
		break;
	default:
		fe = NULL;
		dev_err(&d->udev->dev, "%s: unknown tuner=%d\n", KBUILD_MODNAME,
				priv->tuner);
	}

	if (fe == NULL) {
		ret = -ENODEV;
		goto err;
	}

	return 0;
err:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static const struct e4000_config rtl2832u_e4000_config = {
	.i2c_addr = 0x64,
	.clock = 28800000,
};

static const struct fc2580_config rtl2832u_fc2580_config = {
	.i2c_addr = 0x56,
	.clock = 16384000,
};

static struct tua9001_config rtl2832u_tua9001_config = {
	.i2c_addr = 0x60,
};

static const struct fc0012_config rtl2832u_fc0012_config = {
	.i2c_address = 0x63, /* 0xc6 >> 1 */
	.xtal_freq = FC_XTAL_28_8_MHZ,
};

static const struct r820t_config rtl2832u_r820t_config = {
	.i2c_addr = 0x1a,
	.xtal = 28800000,
	.max_i2c_msg_len = 2,
	.rafael_chip = CHIP_R820T,
};

static int rtl2832u_tuner_attach(struct dvb_usb_adapter *adap)
{
	int ret;
	struct dvb_usb_device *d = adap_to_d(adap);
	struct rtl28xxu_priv *priv = d_to_priv(d);
	struct dvb_frontend *fe;

	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	switch (priv->tuner) {
	case TUNER_RTL2832_FC0012:
		fe = dvb_attach(fc0012_attach, adap->fe[0],
			&d->i2c_adap, &rtl2832u_fc0012_config);

		/* since fc0012 includs reading the signal strength delegate
		 * that to the tuner driver */
		adap->fe[0]->ops.read_signal_strength =
				adap->fe[0]->ops.tuner_ops.get_rf_strength;
		return 0;
		break;
	case TUNER_RTL2832_FC0013:
		fe = dvb_attach(fc0013_attach, adap->fe[0],
			&d->i2c_adap, 0xc6>>1, 0, FC_XTAL_28_8_MHZ);

		/* fc0013 also supports signal strength reading */
		adap->fe[0]->ops.read_signal_strength =
				adap->fe[0]->ops.tuner_ops.get_rf_strength;
		return 0;
	case TUNER_RTL2832_E4000:
		fe = dvb_attach(e4000_attach, adap->fe[0], &d->i2c_adap,
				&rtl2832u_e4000_config);
		break;
	case TUNER_RTL2832_FC2580:
		fe = dvb_attach(fc2580_attach, adap->fe[0], &d->i2c_adap,
				&rtl2832u_fc2580_config);
		break;
	case TUNER_RTL2832_TUA9001:
		/* enable GPIO1 and GPIO4 as output */
		ret = rtl28xx_wr_reg_mask(d, SYS_GPIO_DIR, 0x00, 0x12);
		if (ret)
			goto err;

		ret = rtl28xx_wr_reg_mask(d, SYS_GPIO_OUT_EN, 0x12, 0x12);
		if (ret)
			goto err;

		fe = dvb_attach(tua9001_attach, adap->fe[0], &d->i2c_adap,
				&rtl2832u_tua9001_config);
		break;
	case TUNER_RTL2832_R820T:
		fe = dvb_attach(r820t_attach, adap->fe[0], &d->i2c_adap,
				&rtl2832u_r820t_config);

		/* Use tuner to get the signal strength */
		adap->fe[0]->ops.read_signal_strength =
				adap->fe[0]->ops.tuner_ops.get_rf_strength;
		break;
	default:
		fe = NULL;
		dev_err(&d->udev->dev, "%s: unknown tuner=%d\n", KBUILD_MODNAME,
				priv->tuner);
	}

	if (fe == NULL) {
		ret = -ENODEV;
		goto err;
	}

	return 0;
err:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int rtl28xxu_init(struct dvb_usb_device *d)
{
	int ret;
	u8 val;

	dev_dbg(&d->udev->dev, "%s:\n", __func__);

	/* init USB endpoints */
	ret = rtl28xx_rd_reg(d, USB_SYSCTL_0, &val);
	if (ret)
		goto err;

	/* enable DMA and Full Packet Mode*/
	val |= 0x09;
	ret = rtl28xx_wr_reg(d, USB_SYSCTL_0, val);
	if (ret)
		goto err;

	/* set EPA maximum packet size to 0x0200 */
	ret = rtl28xx_wr_regs(d, USB_EPA_MAXPKT, "\x00\x02\x00\x00", 4);
	if (ret)
		goto err;

	/* change EPA FIFO length */
	ret = rtl28xx_wr_regs(d, USB_EPA_FIFO_CFG, "\x14\x00\x00\x00", 4);
	if (ret)
		goto err;

	return ret;
err:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int rtl2831u_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	int ret;
	u8 gpio, sys0, epa_ctl[2];

	dev_dbg(&d->udev->dev, "%s: onoff=%d\n", __func__, onoff);

	/* demod adc */
	ret = rtl28xx_rd_reg(d, SYS_SYS0, &sys0);
	if (ret)
		goto err;

	/* tuner power, read GPIOs */
	ret = rtl28xx_rd_reg(d, SYS_GPIO_OUT_VAL, &gpio);
	if (ret)
		goto err;

	dev_dbg(&d->udev->dev, "%s: RD SYS0=%02x GPIO_OUT_VAL=%02x\n", __func__,
			sys0, gpio);

	if (onoff) {
		gpio |= 0x01; /* GPIO0 = 1 */
		gpio &= (~0x10); /* GPIO4 = 0 */
		gpio |= 0x04; /* GPIO2 = 1, LED on */
		sys0 = sys0 & 0x0f;
		sys0 |= 0xe0;
		epa_ctl[0] = 0x00; /* clear stall */
		epa_ctl[1] = 0x00; /* clear reset */
	} else {
		gpio &= (~0x01); /* GPIO0 = 0 */
		gpio |= 0x10; /* GPIO4 = 1 */
		gpio &= (~0x04); /* GPIO2 = 1, LED off */
		sys0 = sys0 & (~0xc0);
		epa_ctl[0] = 0x10; /* set stall */
		epa_ctl[1] = 0x02; /* set reset */
	}

	dev_dbg(&d->udev->dev, "%s: WR SYS0=%02x GPIO_OUT_VAL=%02x\n", __func__,
			sys0, gpio);

	/* demod adc */
	ret = rtl28xx_wr_reg(d, SYS_SYS0, sys0);
	if (ret)
		goto err;

	/* tuner power, write GPIOs */
	ret = rtl28xx_wr_reg(d, SYS_GPIO_OUT_VAL, gpio);
	if (ret)
		goto err;

	/* streaming EP: stall & reset */
	ret = rtl28xx_wr_regs(d, USB_EPA_CTL, epa_ctl, 2);
	if (ret)
		goto err;

	if (onoff)
		usb_clear_halt(d->udev, usb_rcvbulkpipe(d->udev, 0x81));

	return ret;
err:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int rtl2832u_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	int ret;
	u8 val;

	dev_dbg(&d->udev->dev, "%s: onoff=%d\n", __func__, onoff);

	if (onoff) {
		/* set output values */
		ret = rtl28xx_rd_reg(d, SYS_GPIO_OUT_VAL, &val);
		if (ret)
			goto err;

		val |= 0x08;
		val &= 0xef;

		ret = rtl28xx_wr_reg(d, SYS_GPIO_OUT_VAL, val);
		if (ret)
			goto err;

		/* demod_ctl_1 */
		ret = rtl28xx_rd_reg(d, SYS_DEMOD_CTL1, &val);
		if (ret)
			goto err;

		val &= 0xef;

		ret = rtl28xx_wr_reg(d, SYS_DEMOD_CTL1, val);
		if (ret)
			goto err;

		/* demod control */
		/* PLL enable */
		ret = rtl28xx_rd_reg(d, SYS_DEMOD_CTL, &val);
		if (ret)
			goto err;

		/* bit 7 to 1 */
		val |= 0x80;

		ret = rtl28xx_wr_reg(d, SYS_DEMOD_CTL, val);
		if (ret)
			goto err;

		ret = rtl28xx_rd_reg(d, SYS_DEMOD_CTL, &val);
		if (ret)
			goto err;

		val |= 0x20;

		ret = rtl28xx_wr_reg(d, SYS_DEMOD_CTL, val);
		if (ret)
			goto err;

		mdelay(5);

		/*enable ADC_Q and ADC_I */
		ret = rtl28xx_rd_reg(d, SYS_DEMOD_CTL, &val);
		if (ret)
			goto err;

		val |= 0x48;

		ret = rtl28xx_wr_reg(d, SYS_DEMOD_CTL, val);
		if (ret)
			goto err;

		/* streaming EP: clear stall & reset */
		ret = rtl28xx_wr_regs(d, USB_EPA_CTL, "\x00\x00", 2);
		if (ret)
			goto err;

		ret = usb_clear_halt(d->udev, usb_rcvbulkpipe(d->udev, 0x81));
		if (ret)
			goto err;
	} else {
		/* demod_ctl_1 */
		ret = rtl28xx_rd_reg(d, SYS_DEMOD_CTL1, &val);
		if (ret)
			goto err;

		val |= 0x0c;

		ret = rtl28xx_wr_reg(d, SYS_DEMOD_CTL1, val);
		if (ret)
			goto err;

		/* set output values */
		ret = rtl28xx_rd_reg(d, SYS_GPIO_OUT_VAL, &val);
		if (ret)
				goto err;

		val |= 0x10;

		ret = rtl28xx_wr_reg(d, SYS_GPIO_OUT_VAL, val);
		if (ret)
			goto err;

		/* demod control */
		ret = rtl28xx_rd_reg(d, SYS_DEMOD_CTL, &val);
		if (ret)
			goto err;

		val &= 0x37;

		ret = rtl28xx_wr_reg(d, SYS_DEMOD_CTL, val);
		if (ret)
			goto err;

		/* streaming EP: set stall & reset */
		ret = rtl28xx_wr_regs(d, USB_EPA_CTL, "\x10\x02", 2);
		if (ret)
			goto err;
	}

	return ret;
err:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

#if IS_ENABLED(CONFIG_RC_CORE)
static int rtl2831u_rc_query(struct dvb_usb_device *d)
{
	int ret, i;
	struct rtl28xxu_priv *priv = d->priv;
	u8 buf[5];
	u32 rc_code;
	struct rtl28xxu_reg_val rc_nec_tab[] = {
		{ 0x3033, 0x80 },
		{ 0x3020, 0x43 },
		{ 0x3021, 0x16 },
		{ 0x3022, 0x16 },
		{ 0x3023, 0x5a },
		{ 0x3024, 0x2d },
		{ 0x3025, 0x16 },
		{ 0x3026, 0x01 },
		{ 0x3028, 0xb0 },
		{ 0x3029, 0x04 },
		{ 0x302c, 0x88 },
		{ 0x302e, 0x13 },
		{ 0x3030, 0xdf },
		{ 0x3031, 0x05 },
	};

	/* init remote controller */
	if (!priv->rc_active) {
		for (i = 0; i < ARRAY_SIZE(rc_nec_tab); i++) {
			ret = rtl28xx_wr_reg(d, rc_nec_tab[i].reg,
					rc_nec_tab[i].val);
			if (ret)
				goto err;
		}
		priv->rc_active = true;
	}

	ret = rtl2831_rd_regs(d, SYS_IRRC_RP, buf, 5);
	if (ret)
		goto err;

	if (buf[4] & 0x01) {
		if (buf[2] == (u8) ~buf[3]) {
			if (buf[0] == (u8) ~buf[1]) {
				/* NEC standard (16 bit) */
				rc_code = buf[0] << 8 | buf[2];
			} else {
				/* NEC extended (24 bit) */
				rc_code = buf[0] << 16 |
						buf[1] << 8 | buf[2];
			}
		} else {
			/* NEC full (32 bit) */
			rc_code = buf[0] << 24 | buf[1] << 16 |
					buf[2] << 8 | buf[3];
		}

		rc_keydown(d->rc_dev, rc_code, 0);

		ret = rtl28xx_wr_reg(d, SYS_IRRC_SR, 1);
		if (ret)
			goto err;

		/* repeated intentionally to avoid extra keypress */
		ret = rtl28xx_wr_reg(d, SYS_IRRC_SR, 1);
		if (ret)
			goto err;
	}

	return ret;
err:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int rtl2831u_get_rc_config(struct dvb_usb_device *d,
		struct dvb_usb_rc *rc)
{
	rc->map_name = RC_MAP_EMPTY;
	rc->allowed_protos = RC_BIT_NEC;
	rc->query = rtl2831u_rc_query;
	rc->interval = 400;

	return 0;
}
#else
	#define rtl2831u_get_rc_config NULL
#endif

#if IS_ENABLED(CONFIG_RC_CORE)
static int rtl2832u_rc_query(struct dvb_usb_device *d)
{
	int ret, i;
	struct rtl28xxu_priv *priv = d->priv;
	u8 buf[128];
	int len;
	struct rtl28xxu_reg_val rc_nec_tab[] = {
		{ IR_RX_CTRL,             0x20 },
		{ IR_RX_BUF_CTRL,         0x80 },
		{ IR_RX_IF,               0xff },
		{ IR_RX_IE,               0xff },
		{ IR_MAX_DURATION0,       0xd0 },
		{ IR_MAX_DURATION1,       0x07 },
		{ IR_IDLE_LEN0,           0xc0 },
		{ IR_IDLE_LEN1,           0x00 },
		{ IR_GLITCH_LEN,          0x03 },
		{ IR_RX_CLK,              0x09 },
		{ IR_RX_CFG,              0x1c },
		{ IR_MAX_H_TOL_LEN,       0x1e },
		{ IR_MAX_L_TOL_LEN,       0x1e },
		{ IR_RX_CTRL,             0x80 },
	};

	/* init remote controller */
	if (!priv->rc_active) {
		for (i = 0; i < ARRAY_SIZE(rc_nec_tab); i++) {
			ret = rtl28xx_wr_reg(d, rc_nec_tab[i].reg,
					rc_nec_tab[i].val);
			if (ret)
				goto err;
		}
		priv->rc_active = true;
	}

	ret = rtl28xx_rd_reg(d, IR_RX_IF, &buf[0]);
	if (ret)
		goto err;

	if (buf[0] != 0x83)
		goto exit;

	ret = rtl28xx_rd_reg(d, IR_RX_BC, &buf[0]);
	if (ret)
		goto err;

	len = buf[0];
	ret = rtl2831_rd_regs(d, IR_RX_BUF, buf, len);

	/* TODO: pass raw IR to Kernel IR decoder */

	ret = rtl28xx_wr_reg(d, IR_RX_IF, 0x03);
	ret = rtl28xx_wr_reg(d, IR_RX_BUF_CTRL, 0x80);
	ret = rtl28xx_wr_reg(d, IR_RX_CTRL, 0x80);

exit:
	return ret;
err:
	dev_dbg(&d->udev->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int rtl2832u_get_rc_config(struct dvb_usb_device *d,
		struct dvb_usb_rc *rc)
{
	rc->map_name = RC_MAP_EMPTY;
	rc->allowed_protos = RC_BIT_NEC;
	rc->query = rtl2832u_rc_query;
	rc->interval = 400;

	return 0;
}
#else
	#define rtl2832u_get_rc_config NULL
#endif

static const struct dvb_usb_device_properties rtl2831u_props = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,
	.size_of_priv = sizeof(struct rtl28xxu_priv),

	.power_ctrl = rtl2831u_power_ctrl,
	.i2c_algo = &rtl28xxu_i2c_algo,
	.read_config = rtl2831u_read_config,
	.frontend_attach = rtl2831u_frontend_attach,
	.tuner_attach = rtl2831u_tuner_attach,
	.init = rtl28xxu_init,
	.get_rc_config = rtl2831u_get_rc_config,

	.num_adapters = 1,
	.adapter = {
		{
			.stream = DVB_USB_STREAM_BULK(0x81, 6, 8 * 512),
		},
	},
};

static const struct dvb_usb_device_properties rtl2832u_props = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,
	.size_of_priv = sizeof(struct rtl28xxu_priv),

	.power_ctrl = rtl2832u_power_ctrl,
	.i2c_algo = &rtl28xxu_i2c_algo,
	.read_config = rtl2832u_read_config,
	.frontend_attach = rtl2832u_frontend_attach,
	.tuner_attach = rtl2832u_tuner_attach,
	.init = rtl28xxu_init,
	.get_rc_config = rtl2832u_get_rc_config,

	.num_adapters = 1,
	.adapter = {
		{
			.stream = DVB_USB_STREAM_BULK(0x81, 6, 8 * 512),
		},
	},
};

static const struct usb_device_id rtl28xxu_id_table[] = {
	{ DVB_USB_DEVICE(USB_VID_REALTEK, USB_PID_REALTEK_RTL2831U,
		&rtl2831u_props, "Realtek RTL2831U reference design", NULL) },
	{ DVB_USB_DEVICE(USB_VID_WIDEVIEW, USB_PID_FREECOM_DVBT,
		&rtl2831u_props, "Freecom USB2.0 DVB-T", NULL) },
	{ DVB_USB_DEVICE(USB_VID_WIDEVIEW, USB_PID_FREECOM_DVBT_2,
		&rtl2831u_props, "Freecom USB2.0 DVB-T", NULL) },

	{ DVB_USB_DEVICE(USB_VID_REALTEK, 0x2832,
		&rtl2832u_props, "Realtek RTL2832U reference design", NULL) },
	{ DVB_USB_DEVICE(USB_VID_REALTEK, 0x2838,
		&rtl2832u_props, "Realtek RTL2832U reference design", NULL) },
	{ DVB_USB_DEVICE(USB_VID_TERRATEC, USB_PID_TERRATEC_CINERGY_T_STICK_BLACK_REV1,
		&rtl2832u_props, "TerraTec Cinergy T Stick Black", NULL) },
	{ DVB_USB_DEVICE(USB_VID_GTEK, USB_PID_DELOCK_USB2_DVBT,
		&rtl2832u_props, "G-Tek Electronics Group Lifeview LV5TDLX DVB-T", NULL) },
	{ DVB_USB_DEVICE(USB_VID_TERRATEC, USB_PID_NOXON_DAB_STICK,
		&rtl2832u_props, "TerraTec NOXON DAB Stick", NULL) },
	{ DVB_USB_DEVICE(USB_VID_TERRATEC, USB_PID_NOXON_DAB_STICK_REV2,
		&rtl2832u_props, "TerraTec NOXON DAB Stick (rev 2)", NULL) },
	{ DVB_USB_DEVICE(USB_VID_GTEK, USB_PID_TREKSTOR_TERRES_2_0,
		&rtl2832u_props, "Trekstor DVB-T Stick Terres 2.0", NULL) },
	{ DVB_USB_DEVICE(USB_VID_DEXATEK, 0x1101,
		&rtl2832u_props, "Dexatek DK DVB-T Dongle", NULL) },
	{ DVB_USB_DEVICE(USB_VID_LEADTEK, 0x6680,
		&rtl2832u_props, "DigitalNow Quad DVB-T Receiver", NULL) },
	{ DVB_USB_DEVICE(USB_VID_TERRATEC, 0x00d3,
		&rtl2832u_props, "TerraTec Cinergy T Stick RC (Rev. 3)", NULL) },
	{ DVB_USB_DEVICE(USB_VID_DEXATEK, 0x1102,
		&rtl2832u_props, "Dexatek DK mini DVB-T Dongle", NULL) },
	{ DVB_USB_DEVICE(USB_VID_TERRATEC, 0x00d7,
		&rtl2832u_props, "TerraTec Cinergy T Stick+", NULL) },
	{ DVB_USB_DEVICE(USB_VID_KWORLD_2, 0xd3a8,
		&rtl2832u_props, "ASUS My Cinema-U3100Mini Plus V2", NULL) },
	{ DVB_USB_DEVICE(USB_VID_KWORLD_2, 0xd393,
		&rtl2832u_props, "GIGABYTE U7300", NULL) },
	{ DVB_USB_DEVICE(USB_VID_DEXATEK, 0x1104,
		&rtl2832u_props, "Digivox Micro Hd", NULL) },
	{ DVB_USB_DEVICE(USB_VID_COMPRO, 0x0620,
		&rtl2832u_props, "Compro VideoMate U620F", NULL) },
	{ DVB_USB_DEVICE(USB_VID_KWORLD_2, 0xd394,
		&rtl2832u_props, "MaxMedia HU394-T", NULL) },
	{ }
};
MODULE_DEVICE_TABLE(usb, rtl28xxu_id_table);

static struct usb_driver rtl28xxu_usb_driver = {
	.name = KBUILD_MODNAME,
	.id_table = rtl28xxu_id_table,
	.probe = dvb_usbv2_probe,
	.disconnect = dvb_usbv2_disconnect,
	.suspend = dvb_usbv2_suspend,
	.resume = dvb_usbv2_resume,
	.reset_resume = dvb_usbv2_reset_resume,
	.no_dynamic_id = 1,
	.soft_unbind = 1,
};

module_usb_driver(rtl28xxu_usb_driver);

MODULE_DESCRIPTION("Realtek RTL28xxU DVB USB driver");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_AUTHOR("Thomas Mair <thomas.mair86@googlemail.com>");
MODULE_LICENSE("GPL");
