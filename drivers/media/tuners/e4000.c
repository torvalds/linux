/*
 * Elonics E4000 silicon tuner driver
 *
 * Copyright (C) 2012 Antti Palosaari <crope@iki.fi>
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

#include "e4000_priv.h"
#include <linux/math64.h>

/* Max transfer size done by I2C transfer functions */
#define MAX_XFER_SIZE  64

/* write multiple registers */
static int e4000_wr_regs(struct e4000_priv *priv, u8 reg, u8 *val, int len)
{
	int ret;
	u8 buf[MAX_XFER_SIZE];
	struct i2c_msg msg[1] = {
		{
			.addr = priv->client->addr,
			.flags = 0,
			.len = 1 + len,
			.buf = buf,
		}
	};

	if (1 + len > sizeof(buf)) {
		dev_warn(&priv->client->dev,
			 "%s: i2c wr reg=%04x: len=%d is too big!\n",
			 KBUILD_MODNAME, reg, len);
		return -EINVAL;
	}

	buf[0] = reg;
	memcpy(&buf[1], val, len);

	ret = i2c_transfer(priv->client->adapter, msg, 1);
	if (ret == 1) {
		ret = 0;
	} else {
		dev_warn(&priv->client->dev,
				"%s: i2c wr failed=%d reg=%02x len=%d\n",
				KBUILD_MODNAME, ret, reg, len);
		ret = -EREMOTEIO;
	}
	return ret;
}

/* read multiple registers */
static int e4000_rd_regs(struct e4000_priv *priv, u8 reg, u8 *val, int len)
{
	int ret;
	u8 buf[MAX_XFER_SIZE];
	struct i2c_msg msg[2] = {
		{
			.addr = priv->client->addr,
			.flags = 0,
			.len = 1,
			.buf = &reg,
		}, {
			.addr = priv->client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = buf,
		}
	};

	if (len > sizeof(buf)) {
		dev_warn(&priv->client->dev,
			 "%s: i2c rd reg=%04x: len=%d is too big!\n",
			 KBUILD_MODNAME, reg, len);
		return -EINVAL;
	}

	ret = i2c_transfer(priv->client->adapter, msg, 2);
	if (ret == 2) {
		memcpy(val, buf, len);
		ret = 0;
	} else {
		dev_warn(&priv->client->dev,
				"%s: i2c rd failed=%d reg=%02x len=%d\n",
				KBUILD_MODNAME, ret, reg, len);
		ret = -EREMOTEIO;
	}

	return ret;
}

/* write single register */
static int e4000_wr_reg(struct e4000_priv *priv, u8 reg, u8 val)
{
	return e4000_wr_regs(priv, reg, &val, 1);
}

/* read single register */
static int e4000_rd_reg(struct e4000_priv *priv, u8 reg, u8 *val)
{
	return e4000_rd_regs(priv, reg, val, 1);
}

static int e4000_init(struct dvb_frontend *fe)
{
	struct e4000_priv *priv = fe->tuner_priv;
	int ret;

	dev_dbg(&priv->client->dev, "%s:\n", __func__);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	/* dummy I2C to ensure I2C wakes up */
	ret = e4000_wr_reg(priv, 0x02, 0x40);

	/* reset */
	ret = e4000_wr_reg(priv, 0x00, 0x01);
	if (ret < 0)
		goto err;

	/* disable output clock */
	ret = e4000_wr_reg(priv, 0x06, 0x00);
	if (ret < 0)
		goto err;

	ret = e4000_wr_reg(priv, 0x7a, 0x96);
	if (ret < 0)
		goto err;

	/* configure gains */
	ret = e4000_wr_regs(priv, 0x7e, "\x01\xfe", 2);
	if (ret < 0)
		goto err;

	ret = e4000_wr_reg(priv, 0x82, 0x00);
	if (ret < 0)
		goto err;

	ret = e4000_wr_reg(priv, 0x24, 0x05);
	if (ret < 0)
		goto err;

	ret = e4000_wr_regs(priv, 0x87, "\x20\x01", 2);
	if (ret < 0)
		goto err;

	ret = e4000_wr_regs(priv, 0x9f, "\x7f\x07", 2);
	if (ret < 0)
		goto err;

	/* DC offset control */
	ret = e4000_wr_reg(priv, 0x2d, 0x1f);
	if (ret < 0)
		goto err;

	ret = e4000_wr_regs(priv, 0x70, "\x01\x01", 2);
	if (ret < 0)
		goto err;

	/* gain control */
	ret = e4000_wr_reg(priv, 0x1a, 0x17);
	if (ret < 0)
		goto err;

	ret = e4000_wr_reg(priv, 0x1f, 0x1a);
	if (ret < 0)
		goto err;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return 0;
err:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	dev_dbg(&priv->client->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int e4000_sleep(struct dvb_frontend *fe)
{
	struct e4000_priv *priv = fe->tuner_priv;
	int ret;

	dev_dbg(&priv->client->dev, "%s:\n", __func__);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	ret = e4000_wr_reg(priv, 0x00, 0x00);
	if (ret < 0)
		goto err;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return 0;
err:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	dev_dbg(&priv->client->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int e4000_set_params(struct dvb_frontend *fe)
{
	struct e4000_priv *priv = fe->tuner_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i, sigma_delta;
	u64 f_vco;
	u8 buf[5], i_data[4], q_data[4];

	dev_dbg(&priv->client->dev,
			"%s: delivery_system=%d frequency=%u bandwidth_hz=%u\n",
			__func__, c->delivery_system, c->frequency,
			c->bandwidth_hz);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	/* gain control manual */
	ret = e4000_wr_reg(priv, 0x1a, 0x00);
	if (ret < 0)
		goto err;

	/* PLL */
	for (i = 0; i < ARRAY_SIZE(e4000_pll_lut); i++) {
		if (c->frequency <= e4000_pll_lut[i].freq)
			break;
	}

	if (i == ARRAY_SIZE(e4000_pll_lut)) {
		ret = -EINVAL;
		goto err;
	}

	f_vco = 1ull * c->frequency * e4000_pll_lut[i].mul;
	sigma_delta = div_u64(0x10000ULL * (f_vco % priv->clock), priv->clock);
	buf[0] = div_u64(f_vco, priv->clock);
	buf[1] = (sigma_delta >> 0) & 0xff;
	buf[2] = (sigma_delta >> 8) & 0xff;
	buf[3] = 0x00;
	buf[4] = e4000_pll_lut[i].div;

	dev_dbg(&priv->client->dev,
			"%s: f_vco=%llu pll div=%d sigma_delta=%04x\n",
			__func__, f_vco, buf[0], sigma_delta);

	ret = e4000_wr_regs(priv, 0x09, buf, 5);
	if (ret < 0)
		goto err;

	/* LNA filter (RF filter) */
	for (i = 0; i < ARRAY_SIZE(e400_lna_filter_lut); i++) {
		if (c->frequency <= e400_lna_filter_lut[i].freq)
			break;
	}

	if (i == ARRAY_SIZE(e400_lna_filter_lut)) {
		ret = -EINVAL;
		goto err;
	}

	ret = e4000_wr_reg(priv, 0x10, e400_lna_filter_lut[i].val);
	if (ret < 0)
		goto err;

	/* IF filters */
	for (i = 0; i < ARRAY_SIZE(e4000_if_filter_lut); i++) {
		if (c->bandwidth_hz <= e4000_if_filter_lut[i].freq)
			break;
	}

	if (i == ARRAY_SIZE(e4000_if_filter_lut)) {
		ret = -EINVAL;
		goto err;
	}

	buf[0] = e4000_if_filter_lut[i].reg11_val;
	buf[1] = e4000_if_filter_lut[i].reg12_val;

	ret = e4000_wr_regs(priv, 0x11, buf, 2);
	if (ret < 0)
		goto err;

	/* frequency band */
	for (i = 0; i < ARRAY_SIZE(e4000_band_lut); i++) {
		if (c->frequency <= e4000_band_lut[i].freq)
			break;
	}

	if (i == ARRAY_SIZE(e4000_band_lut)) {
		ret = -EINVAL;
		goto err;
	}

	ret = e4000_wr_reg(priv, 0x07, e4000_band_lut[i].reg07_val);
	if (ret < 0)
		goto err;

	ret = e4000_wr_reg(priv, 0x78, e4000_band_lut[i].reg78_val);
	if (ret < 0)
		goto err;

	/* DC offset */
	for (i = 0; i < 4; i++) {
		if (i == 0)
			ret = e4000_wr_regs(priv, 0x15, "\x00\x7e\x24", 3);
		else if (i == 1)
			ret = e4000_wr_regs(priv, 0x15, "\x00\x7f", 2);
		else if (i == 2)
			ret = e4000_wr_regs(priv, 0x15, "\x01", 1);
		else
			ret = e4000_wr_regs(priv, 0x16, "\x7e", 1);

		if (ret < 0)
			goto err;

		ret = e4000_wr_reg(priv, 0x29, 0x01);
		if (ret < 0)
			goto err;

		ret = e4000_rd_regs(priv, 0x2a, buf, 3);
		if (ret < 0)
			goto err;

		i_data[i] = (((buf[2] >> 0) & 0x3) << 6) | (buf[0] & 0x3f);
		q_data[i] = (((buf[2] >> 4) & 0x3) << 6) | (buf[1] & 0x3f);
	}

	swap(q_data[2], q_data[3]);
	swap(i_data[2], i_data[3]);

	ret = e4000_wr_regs(priv, 0x50, q_data, 4);
	if (ret < 0)
		goto err;

	ret = e4000_wr_regs(priv, 0x60, i_data, 4);
	if (ret < 0)
		goto err;

	/* gain control auto */
	ret = e4000_wr_reg(priv, 0x1a, 0x17);
	if (ret < 0)
		goto err;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return 0;
err:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	dev_dbg(&priv->client->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int e4000_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct e4000_priv *priv = fe->tuner_priv;

	dev_dbg(&priv->client->dev, "%s:\n", __func__);

	*frequency = 0; /* Zero-IF */

	return 0;
}

static int e4000_set_lna_gain(struct dvb_frontend *fe)
{
	struct e4000_priv *priv = fe->tuner_priv;
	int ret;
	u8 u8tmp;
	dev_dbg(&priv->client->dev, "%s: lna auto=%d->%d val=%d->%d\n",
			__func__, priv->lna_gain_auto->cur.val,
			priv->lna_gain_auto->val, priv->lna_gain->cur.val,
			priv->lna_gain->val);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	if (priv->lna_gain_auto->val && priv->if_gain_auto->cur.val)
		u8tmp = 0x17;
	else if (priv->lna_gain_auto->val)
		u8tmp = 0x19;
	else if (priv->if_gain_auto->cur.val)
		u8tmp = 0x16;
	else
		u8tmp = 0x10;

	ret = e4000_wr_reg(priv, 0x1a, u8tmp);
	if (ret)
		goto err;

	if (priv->lna_gain_auto->val == false) {
		ret = e4000_wr_reg(priv, 0x14, priv->lna_gain->val);
		if (ret)
			goto err;
	}

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return 0;
err:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	dev_dbg(&priv->client->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int e4000_set_mixer_gain(struct dvb_frontend *fe)
{
	struct e4000_priv *priv = fe->tuner_priv;
	int ret;
	u8 u8tmp;
	dev_dbg(&priv->client->dev, "%s: mixer auto=%d->%d val=%d->%d\n",
			__func__, priv->mixer_gain_auto->cur.val,
			priv->mixer_gain_auto->val, priv->mixer_gain->cur.val,
			priv->mixer_gain->val);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	if (priv->mixer_gain_auto->val)
		u8tmp = 0x15;
	else
		u8tmp = 0x14;

	ret = e4000_wr_reg(priv, 0x20, u8tmp);
	if (ret)
		goto err;

	if (priv->mixer_gain_auto->val == false) {
		ret = e4000_wr_reg(priv, 0x15, priv->mixer_gain->val);
		if (ret)
			goto err;
	}

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return 0;
err:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	dev_dbg(&priv->client->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int e4000_set_if_gain(struct dvb_frontend *fe)
{
	struct e4000_priv *priv = fe->tuner_priv;
	int ret;
	u8 buf[2];
	u8 u8tmp;
	dev_dbg(&priv->client->dev, "%s: if auto=%d->%d val=%d->%d\n",
			__func__, priv->if_gain_auto->cur.val,
			priv->if_gain_auto->val, priv->if_gain->cur.val,
			priv->if_gain->val);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	if (priv->if_gain_auto->val && priv->lna_gain_auto->cur.val)
		u8tmp = 0x17;
	else if (priv->lna_gain_auto->cur.val)
		u8tmp = 0x19;
	else if (priv->if_gain_auto->val)
		u8tmp = 0x16;
	else
		u8tmp = 0x10;

	ret = e4000_wr_reg(priv, 0x1a, u8tmp);
	if (ret)
		goto err;

	if (priv->if_gain_auto->val == false) {
		buf[0] = e4000_if_gain_lut[priv->if_gain->val].reg16_val;
		buf[1] = e4000_if_gain_lut[priv->if_gain->val].reg17_val;
		ret = e4000_wr_regs(priv, 0x16, buf, 2);
		if (ret)
			goto err;
	}

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return 0;
err:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	dev_dbg(&priv->client->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int e4000_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct e4000_priv *priv =
			container_of(ctrl->handler, struct e4000_priv, hdl);
	struct dvb_frontend *fe = priv->fe;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	dev_dbg(&priv->client->dev,
			"%s: id=%d name=%s val=%d min=%d max=%d step=%d\n",
			__func__, ctrl->id, ctrl->name, ctrl->val,
			ctrl->minimum, ctrl->maximum, ctrl->step);

	switch (ctrl->id) {
	case V4L2_CID_RF_TUNER_BANDWIDTH_AUTO:
	case V4L2_CID_RF_TUNER_BANDWIDTH:
		c->bandwidth_hz = priv->bandwidth->val;
		ret = e4000_set_params(priv->fe);
		break;
	case  V4L2_CID_RF_TUNER_LNA_GAIN_AUTO:
	case  V4L2_CID_RF_TUNER_LNA_GAIN:
		ret = e4000_set_lna_gain(priv->fe);
		break;
	case  V4L2_CID_RF_TUNER_MIXER_GAIN_AUTO:
	case  V4L2_CID_RF_TUNER_MIXER_GAIN:
		ret = e4000_set_mixer_gain(priv->fe);
		break;
	case  V4L2_CID_RF_TUNER_IF_GAIN_AUTO:
	case  V4L2_CID_RF_TUNER_IF_GAIN:
		ret = e4000_set_if_gain(priv->fe);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct v4l2_ctrl_ops e4000_ctrl_ops = {
	.s_ctrl = e4000_s_ctrl,
};

static const struct dvb_tuner_ops e4000_tuner_ops = {
	.info = {
		.name           = "Elonics E4000",
		.frequency_min  = 174000000,
		.frequency_max  = 862000000,
	},

	.init = e4000_init,
	.sleep = e4000_sleep,
	.set_params = e4000_set_params,

	.get_if_frequency = e4000_get_if_frequency,
};

/*
 * Use V4L2 subdev to carry V4L2 control handler, even we don't implement
 * subdev itself, just to avoid reinventing the wheel.
 */
static int e4000_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct e4000_config *cfg = client->dev.platform_data;
	struct dvb_frontend *fe = cfg->fe;
	struct e4000_priv *priv;
	int ret;
	u8 chip_id;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	priv = kzalloc(sizeof(struct e4000_priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		dev_err(&client->dev, "%s: kzalloc() failed\n", KBUILD_MODNAME);
		goto err;
	}

	priv->clock = cfg->clock;
	priv->client = client;
	priv->fe = cfg->fe;

	/* check if the tuner is there */
	ret = e4000_rd_reg(priv, 0x02, &chip_id);
	if (ret < 0)
		goto err;

	dev_dbg(&priv->client->dev,
			"%s: chip_id=%02x\n", __func__, chip_id);

	if (chip_id != 0x40) {
		ret = -ENODEV;
		goto err;
	}

	/* put sleep as chip seems to be in normal mode by default */
	ret = e4000_wr_reg(priv, 0x00, 0x00);
	if (ret < 0)
		goto err;

	/* Register controls */
	v4l2_ctrl_handler_init(&priv->hdl, 8);
	priv->bandwidth_auto = v4l2_ctrl_new_std(&priv->hdl, &e4000_ctrl_ops,
			V4L2_CID_RF_TUNER_BANDWIDTH_AUTO, 0, 1, 1, 1);
	priv->bandwidth = v4l2_ctrl_new_std(&priv->hdl, &e4000_ctrl_ops,
			V4L2_CID_RF_TUNER_BANDWIDTH, 4300000, 11000000, 100000, 4300000);
	v4l2_ctrl_auto_cluster(2, &priv->bandwidth_auto, 0, false);
	priv->lna_gain_auto = v4l2_ctrl_new_std(&priv->hdl, &e4000_ctrl_ops,
			V4L2_CID_RF_TUNER_LNA_GAIN_AUTO, 0, 1, 1, 1);
	priv->lna_gain = v4l2_ctrl_new_std(&priv->hdl, &e4000_ctrl_ops,
			V4L2_CID_RF_TUNER_LNA_GAIN, 0, 15, 1, 10);
	v4l2_ctrl_auto_cluster(2, &priv->lna_gain_auto, 0, false);
	priv->mixer_gain_auto = v4l2_ctrl_new_std(&priv->hdl, &e4000_ctrl_ops,
			V4L2_CID_RF_TUNER_MIXER_GAIN_AUTO, 0, 1, 1, 1);
	priv->mixer_gain = v4l2_ctrl_new_std(&priv->hdl, &e4000_ctrl_ops,
			V4L2_CID_RF_TUNER_MIXER_GAIN, 0, 1, 1, 1);
	v4l2_ctrl_auto_cluster(2, &priv->mixer_gain_auto, 0, false);
	priv->if_gain_auto = v4l2_ctrl_new_std(&priv->hdl, &e4000_ctrl_ops,
			V4L2_CID_RF_TUNER_IF_GAIN_AUTO, 0, 1, 1, 1);
	priv->if_gain = v4l2_ctrl_new_std(&priv->hdl, &e4000_ctrl_ops,
			V4L2_CID_RF_TUNER_IF_GAIN, 0, 54, 1, 0);
	v4l2_ctrl_auto_cluster(2, &priv->if_gain_auto, 0, false);
	if (priv->hdl.error) {
		ret = priv->hdl.error;
		dev_err(&priv->client->dev, "Could not initialize controls\n");
		v4l2_ctrl_handler_free(&priv->hdl);
		goto err;
	}

	priv->sd.ctrl_handler = &priv->hdl;

	dev_info(&priv->client->dev,
			"%s: Elonics E4000 successfully identified\n",
			KBUILD_MODNAME);

	fe->tuner_priv = priv;
	memcpy(&fe->ops.tuner_ops, &e4000_tuner_ops,
			sizeof(struct dvb_tuner_ops));

	v4l2_set_subdevdata(&priv->sd, client);
	i2c_set_clientdata(client, &priv->sd);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return 0;
err:
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	dev_dbg(&client->dev, "%s: failed=%d\n", __func__, ret);
	kfree(priv);
	return ret;
}

static int e4000_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct e4000_priv *priv = container_of(sd, struct e4000_priv, sd);
	struct dvb_frontend *fe = priv->fe;

	dev_dbg(&client->dev, "%s:\n", __func__);
	v4l2_ctrl_handler_free(&priv->hdl);
	memset(&fe->ops.tuner_ops, 0, sizeof(struct dvb_tuner_ops));
	fe->tuner_priv = NULL;
	kfree(priv);

	return 0;
}

static const struct i2c_device_id e4000_id[] = {
	{"e4000", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, e4000_id);

static struct i2c_driver e4000_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "e4000",
	},
	.probe		= e4000_probe,
	.remove		= e4000_remove,
	.id_table	= e4000_id,
};

module_i2c_driver(e4000_driver);

MODULE_DESCRIPTION("Elonics E4000 silicon tuner driver");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_LICENSE("GPL");
