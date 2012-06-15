/*
 * Realtek RTL28xxU DVB USB driver
 *
 * Copyright (C) 2009 Antti Palosaari <crope@iki.fi>
 * Copyright (C) 2011 Antti Palosaari <crope@iki.fi>
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

#include "qt1010.h"
#include "mt2060.h"
#include "mxl5005s.h"

/* debug */
static int dvb_usb_rtl28xxu_debug;
module_param_named(debug, dvb_usb_rtl28xxu_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level" DVB_USB_DEBUG_STATUS);
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
	if (ret > 0)
		ret = 0;

	deb_dump(0, requesttype, req->value, req->index, buf, req->size,
			deb_xfer);

	/* read request, copy returned data to return buf */
	if (!ret && requesttype == (USB_TYPE_VENDOR | USB_DIR_IN))
		memcpy(req->data, buf, req->size);

	kfree(buf);

	if (ret)
		goto err;

	return ret;
err:
	deb_info("%s: failed=%d\n", __func__, ret);
	return ret;
}

static int rtl2831_wr_regs(struct dvb_usb_device *d, u16 reg, u8 *val, int len)
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

static int rtl2831_wr_reg(struct dvb_usb_device *d, u16 reg, u8 val)
{
	return rtl2831_wr_regs(d, reg, &val, 1);
}

static int rtl2831_rd_reg(struct dvb_usb_device *d, u16 reg, u8 *val)
{
	return rtl2831_rd_regs(d, reg, val, 1);
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

static struct rtl2830_config rtl28xxu_rtl2830_mt2060_config = {
	.i2c_addr = 0x10, /* 0x20 */
	.xtal = 28800000,
	.ts_mode = 0,
	.spec_inv = 1,
	.if_dvbt = 36150000,
	.vtop = 0x20,
	.krf = 0x04,
	.agc_targ_val = 0x2d,

};

static struct rtl2830_config rtl28xxu_rtl2830_qt1010_config = {
	.i2c_addr = 0x10, /* 0x20 */
	.xtal = 28800000,
	.ts_mode = 0,
	.spec_inv = 1,
	.if_dvbt = 36125000,
	.vtop = 0x20,
	.krf = 0x04,
	.agc_targ_val = 0x2d,
};

static struct rtl2830_config rtl28xxu_rtl2830_mxl5005s_config = {
	.i2c_addr = 0x10, /* 0x20 */
	.xtal = 28800000,
	.ts_mode = 0,
	.spec_inv = 0,
	.if_dvbt = 4570000,
	.vtop = 0x3f,
	.krf = 0x04,
	.agc_targ_val = 0x3e,
};

static int rtl2831u_frontend_attach(struct dvb_usb_adapter *adap)
{
	int ret;
	struct rtl28xxu_priv *priv = adap->dev->priv;
	u8 buf[1];
	struct rtl2830_config *rtl2830_config;
	/* open RTL2831U/RTL2830 I2C gate */
	struct rtl28xxu_req req_gate = { 0x0120, 0x0011, 0x0001, "\x08" };
	/* for MT2060 tuner probe */
	struct rtl28xxu_req req_mt2060 = { 0x00c0, CMD_I2C_RD, 1, buf };
	/* for QT1010 tuner probe */
	struct rtl28xxu_req req_qt1010 = { 0x0fc4, CMD_I2C_RD, 1, buf };

	deb_info("%s:\n", __func__);

	/*
	 * RTL2831U GPIOs
	 * =========================================================
	 * GPIO0 | tuner#0 | 0 off | 1 on  | MXL5005S (?)
	 * GPIO2 | LED     | 0 off | 1 on  |
	 * GPIO4 | tuner#1 | 0 on  | 1 off | MT2060
	 */

	/* GPIO direction */
	ret = rtl2831_wr_reg(adap->dev, SYS_GPIO_DIR, 0x0a);
	if (ret)
		goto err;

	/* enable as output GPIO0, GPIO2, GPIO4 */
	ret = rtl2831_wr_reg(adap->dev, SYS_GPIO_OUT_EN, 0x15);
	if (ret)
		goto err;

	/*
	 * Probe used tuner. We need to know used tuner before demod attach
	 * since there is some demod params needed to set according to tuner.
	 */

	/* demod needs some time to wake up */
	msleep(20);

	/* open demod I2C gate */
	ret = rtl28xxu_ctrl_msg(adap->dev, &req_gate);
	if (ret)
		goto err;

	/* check QT1010 ID(?) register; reg=0f val=2c */
	ret = rtl28xxu_ctrl_msg(adap->dev, &req_qt1010);
	if (ret == 0 && buf[0] == 0x2c) {
		priv->tuner = TUNER_RTL2830_QT1010;
		rtl2830_config = &rtl28xxu_rtl2830_qt1010_config;
		deb_info("%s: QT1010\n", __func__);
		goto found;
	} else {
		deb_info("%s: QT1010 probe failed=%d - %02x\n",
			__func__, ret, buf[0]);
	}

	/* open demod I2C gate */
	ret = rtl28xxu_ctrl_msg(adap->dev, &req_gate);
	if (ret)
		goto err;

	/* check MT2060 ID register; reg=00 val=63 */
	ret = rtl28xxu_ctrl_msg(adap->dev, &req_mt2060);
	if (ret == 0 && buf[0] == 0x63) {
		priv->tuner = TUNER_RTL2830_MT2060;
		rtl2830_config = &rtl28xxu_rtl2830_mt2060_config;
		deb_info("%s: MT2060\n", __func__);
		goto found;
	} else {
		deb_info("%s: MT2060 probe failed=%d - %02x\n",
			__func__, ret, buf[0]);
	}

	/* assume MXL5005S */
	ret = 0;
	priv->tuner = TUNER_RTL2830_MXL5005S;
	rtl2830_config = &rtl28xxu_rtl2830_mxl5005s_config;
	deb_info("%s: MXL5005S\n", __func__);
	goto found;

found:
	/* attach demodulator */
	adap->fe_adap[0].fe = dvb_attach(rtl2830_attach, rtl2830_config,
		&adap->dev->i2c_adap);
	if (adap->fe_adap[0].fe == NULL) {
		ret = -ENODEV;
		goto err;
	}

	return ret;
err:
	deb_info("%s: failed=%d\n", __func__, ret);
	return ret;
}

static int rtl2832u_frontend_attach(struct dvb_usb_adapter *adap)
{
	int ret;
	struct rtl28xxu_priv *priv = adap->dev->priv;
	u8 buf[1];
	/* open RTL2832U/RTL2832 I2C gate */
	struct rtl28xxu_req req_gate_open = {0x0120, 0x0011, 0x0001, "\x18"};
	/* close RTL2832U/RTL2832 I2C gate */
	struct rtl28xxu_req req_gate_close = {0x0120, 0x0011, 0x0001, "\x10"};
	/* for FC2580 tuner probe */
	struct rtl28xxu_req req_fc2580 = {0x01ac, CMD_I2C_RD, 1, buf};

	deb_info("%s:\n", __func__);

	/* GPIO direction */
	ret = rtl2831_wr_reg(adap->dev, SYS_GPIO_DIR, 0x0a);
	if (ret)
		goto err;

	/* enable as output GPIO0, GPIO2, GPIO4 */
	ret = rtl2831_wr_reg(adap->dev, SYS_GPIO_OUT_EN, 0x15);
	if (ret)
		goto err;

	ret = rtl2831_wr_reg(adap->dev, SYS_DEMOD_CTL, 0xe8);
	if (ret)
		goto err;

	/*
	 * Probe used tuner. We need to know used tuner before demod attach
	 * since there is some demod params needed to set according to tuner.
	 */

	/* open demod I2C gate */
	ret = rtl28xxu_ctrl_msg(adap->dev, &req_gate_open);
	if (ret)
		goto err;

	/* check FC2580 ID register; reg=01 val=56 */
	ret = rtl28xxu_ctrl_msg(adap->dev, &req_fc2580);
	if (ret == 0 && buf[0] == 0x56) {
		priv->tuner = TUNER_RTL2832_FC2580;
		deb_info("%s: FC2580\n", __func__);
		goto found;
	} else {
		deb_info("%s: FC2580 probe failed=%d - %02x\n",
			__func__, ret, buf[0]);
	}

	/* close demod I2C gate */
	ret = rtl28xxu_ctrl_msg(adap->dev, &req_gate_close);
	if (ret)
		goto err;

	/* tuner not found */
	ret = -ENODEV;
	goto err;

found:
	/* close demod I2C gate */
	ret = rtl28xxu_ctrl_msg(adap->dev, &req_gate_close);
	if (ret)
		goto err;

	/* attach demodulator */
	/* TODO: */

	return ret;
err:
	deb_info("%s: failed=%d\n", __func__, ret);
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
	struct rtl28xxu_priv *priv = adap->dev->priv;
	struct i2c_adapter *rtl2830_tuner_i2c;
	struct dvb_frontend *fe;

	deb_info("%s:\n", __func__);

	/* use rtl2830 driver I2C adapter, for more info see rtl2830 driver */
	rtl2830_tuner_i2c = rtl2830_get_tuner_i2c_adapter(adap->fe_adap[0].fe);

	switch (priv->tuner) {
	case TUNER_RTL2830_QT1010:
		fe = dvb_attach(qt1010_attach, adap->fe_adap[0].fe,
				rtl2830_tuner_i2c, &rtl28xxu_qt1010_config);
		break;
	case TUNER_RTL2830_MT2060:
		fe = dvb_attach(mt2060_attach, adap->fe_adap[0].fe,
				rtl2830_tuner_i2c, &rtl28xxu_mt2060_config,
				1220);
		break;
	case TUNER_RTL2830_MXL5005S:
		fe = dvb_attach(mxl5005s_attach, adap->fe_adap[0].fe,
				rtl2830_tuner_i2c, &rtl28xxu_mxl5005s_config);
		break;
	default:
		fe = NULL;
		err("unknown tuner=%d", priv->tuner);
	}

	if (fe == NULL) {
		ret = -ENODEV;
		goto err;
	}

	return 0;
err:
	deb_info("%s: failed=%d\n", __func__, ret);
	return ret;
}

static int rtl2832u_tuner_attach(struct dvb_usb_adapter *adap)
{
	int ret;
	struct rtl28xxu_priv *priv = adap->dev->priv;
	struct dvb_frontend *fe;

	deb_info("%s:\n", __func__);

	switch (priv->tuner) {
	case TUNER_RTL2832_FC2580:
		/* TODO: */
		fe = NULL;
		break;
	default:
		fe = NULL;
		err("unknown tuner=%d", priv->tuner);
	}

	if (fe == NULL) {
		ret = -ENODEV;
		goto err;
	}

	return 0;
err:
	deb_info("%s: failed=%d\n", __func__, ret);
	return ret;
}

static int rtl28xxu_streaming_ctrl(struct dvb_usb_adapter *adap , int onoff)
{
	int ret;
	u8 buf[2], gpio;

	deb_info("%s: onoff=%d\n", __func__, onoff);

	ret = rtl2831_rd_reg(adap->dev, SYS_GPIO_OUT_VAL, &gpio);
	if (ret)
		goto err;

	if (onoff) {
		buf[0] = 0x00;
		buf[1] = 0x00;
		gpio |= 0x04; /* LED on */
	} else {
		buf[0] = 0x10; /* stall EPA */
		buf[1] = 0x02; /* reset EPA */
		gpio &= (~0x04); /* LED off */
	}

	ret = rtl2831_wr_reg(adap->dev, SYS_GPIO_OUT_VAL, gpio);
	if (ret)
		goto err;

	ret = rtl2831_wr_regs(adap->dev, USB_EPA_CTL, buf, 2);
	if (ret)
		goto err;

	return ret;
err:
	deb_info("%s: failed=%d\n", __func__, ret);
	return ret;
}

static int rtl28xxu_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	int ret;
	u8 gpio, sys0;

	deb_info("%s: onoff=%d\n", __func__, onoff);

	/* demod adc */
	ret = rtl2831_rd_reg(d, SYS_SYS0, &sys0);
	if (ret)
		goto err;

	/* tuner power, read GPIOs */
	ret = rtl2831_rd_reg(d, SYS_GPIO_OUT_VAL, &gpio);
	if (ret)
		goto err;

	deb_info("%s: RD SYS0=%02x GPIO_OUT_VAL=%02x\n", __func__, sys0, gpio);

	if (onoff) {
		gpio |= 0x01; /* GPIO0 = 1 */
		gpio &= (~0x10); /* GPIO4 = 0 */
		sys0 = sys0 & 0x0f;
		sys0 |= 0xe0;
	} else {
		gpio &= (~0x01); /* GPIO0 = 0 */
		gpio |= 0x10; /* GPIO4 = 1 */
		sys0 = sys0 & (~0xc0);
	}

	deb_info("%s: WR SYS0=%02x GPIO_OUT_VAL=%02x\n", __func__, sys0, gpio);

	/* demod adc */
	ret = rtl2831_wr_reg(d, SYS_SYS0, sys0);
	if (ret)
		goto err;

	/* tuner power, write GPIOs */
	ret = rtl2831_wr_reg(d, SYS_GPIO_OUT_VAL, gpio);
	if (ret)
		goto err;

	return ret;
err:
	deb_info("%s: failed=%d\n", __func__, ret);
	return ret;
}

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
			ret = rtl2831_wr_reg(d, rc_nec_tab[i].reg,
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

		ret = rtl2831_wr_reg(d, SYS_IRRC_SR, 1);
		if (ret)
			goto err;

		/* repeated intentionally to avoid extra keypress */
		ret = rtl2831_wr_reg(d, SYS_IRRC_SR, 1);
		if (ret)
			goto err;
	}

	return ret;
err:
	deb_info("%s: failed=%d\n", __func__, ret);
	return ret;
}

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
			ret = rtl2831_wr_reg(d, rc_nec_tab[i].reg,
					rc_nec_tab[i].val);
			if (ret)
				goto err;
		}
		priv->rc_active = true;
	}

	ret = rtl2831_rd_reg(d, IR_RX_IF, &buf[0]);
	if (ret)
		goto err;

	if (buf[0] != 0x83)
		goto exit;

	ret = rtl2831_rd_reg(d, IR_RX_BC, &buf[0]);
	if (ret)
		goto err;

	len = buf[0];
	ret = rtl2831_rd_regs(d, IR_RX_BUF, buf, len);

	/* TODO: pass raw IR to Kernel IR decoder */

	ret = rtl2831_wr_reg(d, IR_RX_IF, 0x03);
	ret = rtl2831_wr_reg(d, IR_RX_BUF_CTRL, 0x80);
	ret = rtl2831_wr_reg(d, IR_RX_CTRL, 0x80);

exit:
	return ret;
err:
	deb_info("%s: failed=%d\n", __func__, ret);
	return ret;
}

enum rtl28xxu_usb_table_entry {
	RTL2831U_0BDA_2831,
	RTL2831U_14AA_0160,
	RTL2831U_14AA_0161,
};

static struct usb_device_id rtl28xxu_table[] = {
	/* RTL2831U */
	[RTL2831U_0BDA_2831] = {
		USB_DEVICE(USB_VID_REALTEK, USB_PID_REALTEK_RTL2831U)},
	[RTL2831U_14AA_0160] = {
		USB_DEVICE(USB_VID_WIDEVIEW, USB_PID_FREECOM_DVBT)},
	[RTL2831U_14AA_0161] = {
		USB_DEVICE(USB_VID_WIDEVIEW, USB_PID_FREECOM_DVBT_2)},

	/* RTL2832U */
	{} /* terminating entry */
};

MODULE_DEVICE_TABLE(usb, rtl28xxu_table);

static struct dvb_usb_device_properties rtl28xxu_properties[] = {
	{
		.caps = DVB_USB_IS_AN_I2C_ADAPTER,

		.usb_ctrl = DEVICE_SPECIFIC,
		.no_reconnect = 1,

		.size_of_priv = sizeof(struct rtl28xxu_priv),

		.num_adapters = 1,
		.adapter = {
			{
				.num_frontends = 1,
				.fe = {
					{
						.frontend_attach = rtl2831u_frontend_attach,
						.tuner_attach    = rtl2831u_tuner_attach,
						.streaming_ctrl  = rtl28xxu_streaming_ctrl,
						.stream = {
							.type = USB_BULK,
							.count = 6,
							.endpoint = 0x81,
							.u = {
								.bulk = {
									.buffersize = 8*512,
								}
							}
						}
					}
				}
			}
		},

		.power_ctrl = rtl28xxu_power_ctrl,

		.rc.core = {
			.protocol       = RC_TYPE_NEC,
			.module_name    = "rtl28xxu",
			.rc_query       = rtl2831u_rc_query,
			.rc_interval    = 400,
			.allowed_protos = RC_TYPE_NEC,
			.rc_codes       = RC_MAP_EMPTY,
		},

		.i2c_algo = &rtl28xxu_i2c_algo,

		.num_device_descs = 2,
		.devices = {
			{
				.name = "Realtek RTL2831U reference design",
				.warm_ids = {
					&rtl28xxu_table[RTL2831U_0BDA_2831],
				},
			},
			{
				.name = "Freecom USB2.0 DVB-T",
				.warm_ids = {
					&rtl28xxu_table[RTL2831U_14AA_0160],
					&rtl28xxu_table[RTL2831U_14AA_0161],
				},
			},
		}
	},
	{
		.caps = DVB_USB_IS_AN_I2C_ADAPTER,

		.usb_ctrl = DEVICE_SPECIFIC,
		.no_reconnect = 1,

		.size_of_priv = sizeof(struct rtl28xxu_priv),

		.num_adapters = 1,
		.adapter = {
			{
				.num_frontends = 1,
				.fe = {
					{
						.frontend_attach = rtl2832u_frontend_attach,
						.tuner_attach    = rtl2832u_tuner_attach,
						.streaming_ctrl  = rtl28xxu_streaming_ctrl,
						.stream = {
							.type = USB_BULK,
							.count = 6,
							.endpoint = 0x81,
							.u = {
								.bulk = {
									.buffersize = 8*512,
								}
							}
						}
					}
				}
			}
		},

		.power_ctrl = rtl28xxu_power_ctrl,

		.rc.core = {
			.protocol       = RC_TYPE_NEC,
			.module_name    = "rtl28xxu",
			.rc_query       = rtl2832u_rc_query,
			.rc_interval    = 400,
			.allowed_protos = RC_TYPE_NEC,
			.rc_codes       = RC_MAP_EMPTY,
		},

		.i2c_algo = &rtl28xxu_i2c_algo,

		.num_device_descs = 0, /* disabled as no support for RTL2832 */
		.devices = {
			{
				.name = "Realtek RTL2832U reference design",
			},
		}
	},

};

static int rtl28xxu_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	int ret, i;
	int properties_count = ARRAY_SIZE(rtl28xxu_properties);
	struct dvb_usb_device *d;
	struct usb_device *udev;
	bool found;

	deb_info("%s: interface=%d\n", __func__,
		intf->cur_altsetting->desc.bInterfaceNumber);

	if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
		return 0;

	/* Dynamic USB ID support. Replaces first device ID with current one .*/
	udev = interface_to_usbdev(intf);

	for (i = 0, found = false; i < ARRAY_SIZE(rtl28xxu_table) - 1; i++) {
		if (rtl28xxu_table[i].idVendor ==
				le16_to_cpu(udev->descriptor.idVendor) &&
				rtl28xxu_table[i].idProduct ==
				le16_to_cpu(udev->descriptor.idProduct)) {
			found = true;
			break;
		}
	}

	if (!found) {
		deb_info("%s: using dynamic ID %04x:%04x\n", __func__,
				le16_to_cpu(udev->descriptor.idVendor),
				le16_to_cpu(udev->descriptor.idProduct));
		rtl28xxu_properties[0].devices[0].warm_ids[0]->idVendor =
				le16_to_cpu(udev->descriptor.idVendor);
		rtl28xxu_properties[0].devices[0].warm_ids[0]->idProduct =
				le16_to_cpu(udev->descriptor.idProduct);
	}

	for (i = 0; i < properties_count; i++) {
		ret = dvb_usb_device_init(intf, &rtl28xxu_properties[i],
				THIS_MODULE, &d, adapter_nr);
		if (ret == 0 || ret != -ENODEV)
			break;
	}

	if (ret)
		goto err;

	/* init USB endpoints */
	ret = rtl2831_wr_reg(d, USB_SYSCTL_0, 0x09);
	if (ret)
		goto err;

	ret = rtl2831_wr_regs(d, USB_EPA_MAXPKT, "\x00\x02\x00\x00", 4);
	if (ret)
		goto err;

	ret = rtl2831_wr_regs(d, USB_EPA_FIFO_CFG, "\x14\x00\x00\x00", 4);
	if (ret)
		goto err;

	return ret;
err:
	deb_info("%s: failed=%d\n", __func__, ret);
	return ret;
}

static struct usb_driver rtl28xxu_driver = {
	.name       = "dvb_usb_rtl28xxu",
	.probe      = rtl28xxu_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table   = rtl28xxu_table,
};

/* module stuff */
static int __init rtl28xxu_module_init(void)
{
	int ret;

	deb_info("%s:\n", __func__);

	ret = usb_register(&rtl28xxu_driver);
	if (ret)
		err("usb_register failed=%d", ret);

	return ret;
}

static void __exit rtl28xxu_module_exit(void)
{
	deb_info("%s:\n", __func__);

	/* deregister this driver from the USB subsystem */
	usb_deregister(&rtl28xxu_driver);
}

module_init(rtl28xxu_module_init);
module_exit(rtl28xxu_module_exit);

MODULE_DESCRIPTION("Realtek RTL28xxU DVB USB driver");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_LICENSE("GPL");
