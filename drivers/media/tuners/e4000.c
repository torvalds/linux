// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Elonics E4000 silicon tuner driver
 *
 * Copyright (C) 2012 Antti Palosaari <crope@iki.fi>
 */

#include "e4000_priv.h"

static int e4000_init(struct e4000_dev *dev)
{
	struct i2c_client *client = dev->client;
	int ret;

	dev_dbg(&client->dev, "\n");

	/* reset */
	ret = regmap_write(dev->regmap, 0x00, 0x01);
	if (ret)
		goto err;

	/* disable output clock */
	ret = regmap_write(dev->regmap, 0x06, 0x00);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x7a, 0x96);
	if (ret)
		goto err;

	/* configure gains */
	ret = regmap_bulk_write(dev->regmap, 0x7e, "\x01\xfe", 2);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x82, 0x00);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x24, 0x05);
	if (ret)
		goto err;

	ret = regmap_bulk_write(dev->regmap, 0x87, "\x20\x01", 2);
	if (ret)
		goto err;

	ret = regmap_bulk_write(dev->regmap, 0x9f, "\x7f\x07", 2);
	if (ret)
		goto err;

	/* DC offset control */
	ret = regmap_write(dev->regmap, 0x2d, 0x1f);
	if (ret)
		goto err;

	ret = regmap_bulk_write(dev->regmap, 0x70, "\x01\x01", 2);
	if (ret)
		goto err;

	/* gain control */
	ret = regmap_write(dev->regmap, 0x1a, 0x17);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x1f, 0x1a);
	if (ret)
		goto err;

	dev->active = true;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int e4000_sleep(struct e4000_dev *dev)
{
	struct i2c_client *client = dev->client;
	int ret;

	dev_dbg(&client->dev, "\n");

	dev->active = false;

	ret = regmap_write(dev->regmap, 0x00, 0x00);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int e4000_set_params(struct e4000_dev *dev)
{
	struct i2c_client *client = dev->client;
	int ret, i;
	unsigned int div_n, k, k_cw, div_out;
	u64 f_vco;
	u8 buf[5], i_data[4], q_data[4];

	if (!dev->active) {
		dev_dbg(&client->dev, "tuner is sleeping\n");
		return 0;
	}

	/* gain control manual */
	ret = regmap_write(dev->regmap, 0x1a, 0x00);
	if (ret)
		goto err;

	/*
	 * Fractional-N synthesizer
	 *
	 *           +----------------------------+
	 *           v                            |
	 *  Fref   +----+     +-------+         +------+     +---+
	 * ------> | PD | --> |  VCO  | ------> | /N.F | <-- | K |
	 *         +----+     +-------+         +------+     +---+
	 *                      |
	 *                      |
	 *                      v
	 *                    +-------+  Fout
	 *                    | /Rout | ------>
	 *                    +-------+
	 */
	for (i = 0; i < ARRAY_SIZE(e4000_pll_lut); i++) {
		if (dev->f_frequency <= e4000_pll_lut[i].freq)
			break;
	}
	if (i == ARRAY_SIZE(e4000_pll_lut)) {
		ret = -EINVAL;
		goto err;
	}

	#define F_REF dev->clk
	div_out = e4000_pll_lut[i].div_out;
	f_vco = (u64) dev->f_frequency * div_out;
	/* calculate PLL integer and fractional control word */
	div_n = div_u64_rem(f_vco, F_REF, &k);
	k_cw = div_u64((u64) k * 0x10000, F_REF);

	dev_dbg(&client->dev,
		"frequency=%u bandwidth=%u f_vco=%llu F_REF=%u div_n=%u k=%u k_cw=%04x div_out=%u\n",
		dev->f_frequency, dev->f_bandwidth, f_vco, F_REF, div_n, k,
		k_cw, div_out);

	buf[0] = div_n;
	buf[1] = (k_cw >> 0) & 0xff;
	buf[2] = (k_cw >> 8) & 0xff;
	buf[3] = 0x00;
	buf[4] = e4000_pll_lut[i].div_out_reg;
	ret = regmap_bulk_write(dev->regmap, 0x09, buf, 5);
	if (ret)
		goto err;

	/* LNA filter (RF filter) */
	for (i = 0; i < ARRAY_SIZE(e400_lna_filter_lut); i++) {
		if (dev->f_frequency <= e400_lna_filter_lut[i].freq)
			break;
	}
	if (i == ARRAY_SIZE(e400_lna_filter_lut)) {
		ret = -EINVAL;
		goto err;
	}

	ret = regmap_write(dev->regmap, 0x10, e400_lna_filter_lut[i].val);
	if (ret)
		goto err;

	/* IF filters */
	for (i = 0; i < ARRAY_SIZE(e4000_if_filter_lut); i++) {
		if (dev->f_bandwidth <= e4000_if_filter_lut[i].freq)
			break;
	}
	if (i == ARRAY_SIZE(e4000_if_filter_lut)) {
		ret = -EINVAL;
		goto err;
	}

	buf[0] = e4000_if_filter_lut[i].reg11_val;
	buf[1] = e4000_if_filter_lut[i].reg12_val;

	ret = regmap_bulk_write(dev->regmap, 0x11, buf, 2);
	if (ret)
		goto err;

	/* frequency band */
	for (i = 0; i < ARRAY_SIZE(e4000_band_lut); i++) {
		if (dev->f_frequency <= e4000_band_lut[i].freq)
			break;
	}
	if (i == ARRAY_SIZE(e4000_band_lut)) {
		ret = -EINVAL;
		goto err;
	}

	ret = regmap_write(dev->regmap, 0x07, e4000_band_lut[i].reg07_val);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x78, e4000_band_lut[i].reg78_val);
	if (ret)
		goto err;

	/* DC offset */
	for (i = 0; i < 4; i++) {
		if (i == 0)
			ret = regmap_bulk_write(dev->regmap, 0x15, "\x00\x7e\x24", 3);
		else if (i == 1)
			ret = regmap_bulk_write(dev->regmap, 0x15, "\x00\x7f", 2);
		else if (i == 2)
			ret = regmap_bulk_write(dev->regmap, 0x15, "\x01", 1);
		else
			ret = regmap_bulk_write(dev->regmap, 0x16, "\x7e", 1);

		if (ret)
			goto err;

		ret = regmap_write(dev->regmap, 0x29, 0x01);
		if (ret)
			goto err;

		ret = regmap_bulk_read(dev->regmap, 0x2a, buf, 3);
		if (ret)
			goto err;

		i_data[i] = (((buf[2] >> 0) & 0x3) << 6) | (buf[0] & 0x3f);
		q_data[i] = (((buf[2] >> 4) & 0x3) << 6) | (buf[1] & 0x3f);
	}

	swap(q_data[2], q_data[3]);
	swap(i_data[2], i_data[3]);

	ret = regmap_bulk_write(dev->regmap, 0x50, q_data, 4);
	if (ret)
		goto err;

	ret = regmap_bulk_write(dev->regmap, 0x60, i_data, 4);
	if (ret)
		goto err;

	/* gain control auto */
	ret = regmap_write(dev->regmap, 0x1a, 0x17);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

/*
 * V4L2 API
 */
#if IS_ENABLED(CONFIG_VIDEO_DEV)
static const struct v4l2_frequency_band bands[] = {
	{
		.type = V4L2_TUNER_RF,
		.index = 0,
		.capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   =    59000000,
		.rangehigh  =  1105000000,
	},
	{
		.type = V4L2_TUNER_RF,
		.index = 1,
		.capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   =  1249000000,
		.rangehigh  =  2208000000UL,
	},
};

static inline struct e4000_dev *e4000_subdev_to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct e4000_dev, sd);
}

static int e4000_standby(struct v4l2_subdev *sd)
{
	struct e4000_dev *dev = e4000_subdev_to_dev(sd);
	int ret;

	ret = e4000_sleep(dev);
	if (ret)
		return ret;

	return e4000_set_params(dev);
}

static int e4000_g_tuner(struct v4l2_subdev *sd, struct v4l2_tuner *v)
{
	struct e4000_dev *dev = e4000_subdev_to_dev(sd);
	struct i2c_client *client = dev->client;

	dev_dbg(&client->dev, "index=%d\n", v->index);

	strscpy(v->name, "Elonics E4000", sizeof(v->name));
	v->type = V4L2_TUNER_RF;
	v->capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS;
	v->rangelow  = bands[0].rangelow;
	v->rangehigh = bands[1].rangehigh;
	return 0;
}

static int e4000_s_tuner(struct v4l2_subdev *sd, const struct v4l2_tuner *v)
{
	struct e4000_dev *dev = e4000_subdev_to_dev(sd);
	struct i2c_client *client = dev->client;

	dev_dbg(&client->dev, "index=%d\n", v->index);
	return 0;
}

static int e4000_g_frequency(struct v4l2_subdev *sd, struct v4l2_frequency *f)
{
	struct e4000_dev *dev = e4000_subdev_to_dev(sd);
	struct i2c_client *client = dev->client;

	dev_dbg(&client->dev, "tuner=%d\n", f->tuner);
	f->frequency = dev->f_frequency;
	return 0;
}

static int e4000_s_frequency(struct v4l2_subdev *sd,
			      const struct v4l2_frequency *f)
{
	struct e4000_dev *dev = e4000_subdev_to_dev(sd);
	struct i2c_client *client = dev->client;

	dev_dbg(&client->dev, "tuner=%d type=%d frequency=%u\n",
		f->tuner, f->type, f->frequency);

	dev->f_frequency = clamp_t(unsigned int, f->frequency,
				   bands[0].rangelow, bands[1].rangehigh);
	return e4000_set_params(dev);
}

static int e4000_enum_freq_bands(struct v4l2_subdev *sd,
				  struct v4l2_frequency_band *band)
{
	struct e4000_dev *dev = e4000_subdev_to_dev(sd);
	struct i2c_client *client = dev->client;

	dev_dbg(&client->dev, "tuner=%d type=%d index=%d\n",
		band->tuner, band->type, band->index);

	if (band->index >= ARRAY_SIZE(bands))
		return -EINVAL;

	band->capability = bands[band->index].capability;
	band->rangelow = bands[band->index].rangelow;
	band->rangehigh = bands[band->index].rangehigh;
	return 0;
}

static const struct v4l2_subdev_tuner_ops e4000_subdev_tuner_ops = {
	.standby                  = e4000_standby,
	.g_tuner                  = e4000_g_tuner,
	.s_tuner                  = e4000_s_tuner,
	.g_frequency              = e4000_g_frequency,
	.s_frequency              = e4000_s_frequency,
	.enum_freq_bands          = e4000_enum_freq_bands,
};

static const struct v4l2_subdev_ops e4000_subdev_ops = {
	.tuner                    = &e4000_subdev_tuner_ops,
};

static int e4000_set_lna_gain(struct dvb_frontend *fe)
{
	struct e4000_dev *dev = fe->tuner_priv;
	struct i2c_client *client = dev->client;
	int ret;
	u8 u8tmp;

	dev_dbg(&client->dev, "lna auto=%d->%d val=%d->%d\n",
		dev->lna_gain_auto->cur.val, dev->lna_gain_auto->val,
		dev->lna_gain->cur.val, dev->lna_gain->val);

	if (dev->lna_gain_auto->val && dev->if_gain_auto->cur.val)
		u8tmp = 0x17;
	else if (dev->lna_gain_auto->val)
		u8tmp = 0x19;
	else if (dev->if_gain_auto->cur.val)
		u8tmp = 0x16;
	else
		u8tmp = 0x10;

	ret = regmap_write(dev->regmap, 0x1a, u8tmp);
	if (ret)
		goto err;

	if (dev->lna_gain_auto->val == false) {
		ret = regmap_write(dev->regmap, 0x14, dev->lna_gain->val);
		if (ret)
			goto err;
	}

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int e4000_set_mixer_gain(struct dvb_frontend *fe)
{
	struct e4000_dev *dev = fe->tuner_priv;
	struct i2c_client *client = dev->client;
	int ret;
	u8 u8tmp;

	dev_dbg(&client->dev, "mixer auto=%d->%d val=%d->%d\n",
		dev->mixer_gain_auto->cur.val, dev->mixer_gain_auto->val,
		dev->mixer_gain->cur.val, dev->mixer_gain->val);

	if (dev->mixer_gain_auto->val)
		u8tmp = 0x15;
	else
		u8tmp = 0x14;

	ret = regmap_write(dev->regmap, 0x20, u8tmp);
	if (ret)
		goto err;

	if (dev->mixer_gain_auto->val == false) {
		ret = regmap_write(dev->regmap, 0x15, dev->mixer_gain->val);
		if (ret)
			goto err;
	}

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int e4000_set_if_gain(struct dvb_frontend *fe)
{
	struct e4000_dev *dev = fe->tuner_priv;
	struct i2c_client *client = dev->client;
	int ret;
	u8 buf[2];
	u8 u8tmp;

	dev_dbg(&client->dev, "if auto=%d->%d val=%d->%d\n",
		dev->if_gain_auto->cur.val, dev->if_gain_auto->val,
		dev->if_gain->cur.val, dev->if_gain->val);

	if (dev->if_gain_auto->val && dev->lna_gain_auto->cur.val)
		u8tmp = 0x17;
	else if (dev->lna_gain_auto->cur.val)
		u8tmp = 0x19;
	else if (dev->if_gain_auto->val)
		u8tmp = 0x16;
	else
		u8tmp = 0x10;

	ret = regmap_write(dev->regmap, 0x1a, u8tmp);
	if (ret)
		goto err;

	if (dev->if_gain_auto->val == false) {
		buf[0] = e4000_if_gain_lut[dev->if_gain->val].reg16_val;
		buf[1] = e4000_if_gain_lut[dev->if_gain->val].reg17_val;
		ret = regmap_bulk_write(dev->regmap, 0x16, buf, 2);
		if (ret)
			goto err;
	}

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int e4000_pll_lock(struct dvb_frontend *fe)
{
	struct e4000_dev *dev = fe->tuner_priv;
	struct i2c_client *client = dev->client;
	int ret;
	unsigned int uitmp;

	ret = regmap_read(dev->regmap, 0x07, &uitmp);
	if (ret)
		goto err;

	dev->pll_lock->val = (uitmp & 0x01);

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int e4000_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct e4000_dev *dev = container_of(ctrl->handler, struct e4000_dev, hdl);
	struct i2c_client *client = dev->client;
	int ret;

	if (!dev->active)
		return 0;

	switch (ctrl->id) {
	case  V4L2_CID_RF_TUNER_PLL_LOCK:
		ret = e4000_pll_lock(dev->fe);
		break;
	default:
		dev_dbg(&client->dev, "unknown ctrl: id=%d name=%s\n",
			ctrl->id, ctrl->name);
		ret = -EINVAL;
	}

	return ret;
}

static int e4000_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct e4000_dev *dev = container_of(ctrl->handler, struct e4000_dev, hdl);
	struct i2c_client *client = dev->client;
	int ret;

	if (!dev->active)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_RF_TUNER_BANDWIDTH_AUTO:
	case V4L2_CID_RF_TUNER_BANDWIDTH:
		/*
		 * TODO: Auto logic does not work 100% correctly as tuner driver
		 * do not have information to calculate maximum suitable
		 * bandwidth. Calculating it is responsible of master driver.
		 */
		dev->f_bandwidth = dev->bandwidth->val;
		ret = e4000_set_params(dev);
		break;
	case  V4L2_CID_RF_TUNER_LNA_GAIN_AUTO:
	case  V4L2_CID_RF_TUNER_LNA_GAIN:
		ret = e4000_set_lna_gain(dev->fe);
		break;
	case  V4L2_CID_RF_TUNER_MIXER_GAIN_AUTO:
	case  V4L2_CID_RF_TUNER_MIXER_GAIN:
		ret = e4000_set_mixer_gain(dev->fe);
		break;
	case  V4L2_CID_RF_TUNER_IF_GAIN_AUTO:
	case  V4L2_CID_RF_TUNER_IF_GAIN:
		ret = e4000_set_if_gain(dev->fe);
		break;
	default:
		dev_dbg(&client->dev, "unknown ctrl: id=%d name=%s\n",
			ctrl->id, ctrl->name);
		ret = -EINVAL;
	}

	return ret;
}

static const struct v4l2_ctrl_ops e4000_ctrl_ops = {
	.g_volatile_ctrl = e4000_g_volatile_ctrl,
	.s_ctrl = e4000_s_ctrl,
};
#endif

/*
 * DVB API
 */
static int e4000_dvb_set_params(struct dvb_frontend *fe)
{
	struct e4000_dev *dev = fe->tuner_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	dev->f_frequency = c->frequency;
	dev->f_bandwidth = c->bandwidth_hz;
	return e4000_set_params(dev);
}

static int e4000_dvb_init(struct dvb_frontend *fe)
{
	return e4000_init(fe->tuner_priv);
}

static int e4000_dvb_sleep(struct dvb_frontend *fe)
{
	return e4000_sleep(fe->tuner_priv);
}

static int e4000_dvb_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	*frequency = 0; /* Zero-IF */
	return 0;
}

static const struct dvb_tuner_ops e4000_dvb_tuner_ops = {
	.info = {
		.name              = "Elonics E4000",
		.frequency_min_hz  = 174 * MHz,
		.frequency_max_hz  = 862 * MHz,
	},

	.init = e4000_dvb_init,
	.sleep = e4000_dvb_sleep,
	.set_params = e4000_dvb_set_params,

	.get_if_frequency = e4000_dvb_get_if_frequency,
};

static int e4000_probe(struct i2c_client *client)
{
	struct e4000_dev *dev;
	struct e4000_config *cfg = client->dev.platform_data;
	struct dvb_frontend *fe = cfg->fe;
	int ret;
	unsigned int uitmp;
	static const struct regmap_config regmap_config = {
		.reg_bits = 8,
		.val_bits = 8,
	};

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto err;
	}

	dev->clk = cfg->clock;
	dev->client = client;
	dev->fe = cfg->fe;
	dev->regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(dev->regmap)) {
		ret = PTR_ERR(dev->regmap);
		goto err_kfree;
	}

	/* check if the tuner is there */
	ret = regmap_read(dev->regmap, 0x02, &uitmp);
	if (ret)
		goto err_kfree;

	dev_dbg(&client->dev, "chip id=%02x\n", uitmp);

	if (uitmp != 0x40) {
		ret = -ENODEV;
		goto err_kfree;
	}

	/* put sleep as chip seems to be in normal mode by default */
	ret = regmap_write(dev->regmap, 0x00, 0x00);
	if (ret)
		goto err_kfree;

#if IS_ENABLED(CONFIG_VIDEO_DEV)
	/* Register controls */
	v4l2_ctrl_handler_init(&dev->hdl, 9);
	dev->bandwidth_auto = v4l2_ctrl_new_std(&dev->hdl, &e4000_ctrl_ops,
			V4L2_CID_RF_TUNER_BANDWIDTH_AUTO, 0, 1, 1, 1);
	dev->bandwidth = v4l2_ctrl_new_std(&dev->hdl, &e4000_ctrl_ops,
			V4L2_CID_RF_TUNER_BANDWIDTH, 4300000, 11000000, 100000, 4300000);
	v4l2_ctrl_auto_cluster(2, &dev->bandwidth_auto, 0, false);
	dev->lna_gain_auto = v4l2_ctrl_new_std(&dev->hdl, &e4000_ctrl_ops,
			V4L2_CID_RF_TUNER_LNA_GAIN_AUTO, 0, 1, 1, 1);
	dev->lna_gain = v4l2_ctrl_new_std(&dev->hdl, &e4000_ctrl_ops,
			V4L2_CID_RF_TUNER_LNA_GAIN, 0, 15, 1, 10);
	v4l2_ctrl_auto_cluster(2, &dev->lna_gain_auto, 0, false);
	dev->mixer_gain_auto = v4l2_ctrl_new_std(&dev->hdl, &e4000_ctrl_ops,
			V4L2_CID_RF_TUNER_MIXER_GAIN_AUTO, 0, 1, 1, 1);
	dev->mixer_gain = v4l2_ctrl_new_std(&dev->hdl, &e4000_ctrl_ops,
			V4L2_CID_RF_TUNER_MIXER_GAIN, 0, 1, 1, 1);
	v4l2_ctrl_auto_cluster(2, &dev->mixer_gain_auto, 0, false);
	dev->if_gain_auto = v4l2_ctrl_new_std(&dev->hdl, &e4000_ctrl_ops,
			V4L2_CID_RF_TUNER_IF_GAIN_AUTO, 0, 1, 1, 1);
	dev->if_gain = v4l2_ctrl_new_std(&dev->hdl, &e4000_ctrl_ops,
			V4L2_CID_RF_TUNER_IF_GAIN, 0, 54, 1, 0);
	v4l2_ctrl_auto_cluster(2, &dev->if_gain_auto, 0, false);
	dev->pll_lock = v4l2_ctrl_new_std(&dev->hdl, &e4000_ctrl_ops,
			V4L2_CID_RF_TUNER_PLL_LOCK,  0, 1, 1, 0);
	if (dev->hdl.error) {
		ret = dev->hdl.error;
		dev_err(&client->dev, "Could not initialize controls\n");
		v4l2_ctrl_handler_free(&dev->hdl);
		goto err_kfree;
	}

	dev->sd.ctrl_handler = &dev->hdl;
	dev->f_frequency = bands[0].rangelow;
	dev->f_bandwidth = dev->bandwidth->val;
	v4l2_i2c_subdev_init(&dev->sd, client, &e4000_subdev_ops);
#endif
	fe->tuner_priv = dev;
	memcpy(&fe->ops.tuner_ops, &e4000_dvb_tuner_ops,
	       sizeof(fe->ops.tuner_ops));
	v4l2_set_subdevdata(&dev->sd, client);
	i2c_set_clientdata(client, &dev->sd);

	dev_info(&client->dev, "Elonics E4000 successfully identified\n");
	return 0;
err_kfree:
	kfree(dev);
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static void e4000_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct e4000_dev *dev = container_of(sd, struct e4000_dev, sd);

	dev_dbg(&client->dev, "\n");

#if IS_ENABLED(CONFIG_VIDEO_DEV)
	v4l2_ctrl_handler_free(&dev->hdl);
#endif
	kfree(dev);
}

static const struct i2c_device_id e4000_id_table[] = {
	{"e4000", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, e4000_id_table);

static struct i2c_driver e4000_driver = {
	.driver = {
		.name	= "e4000",
		.suppress_bind_attrs = true,
	},
	.probe		= e4000_probe,
	.remove		= e4000_remove,
	.id_table	= e4000_id_table,
};

module_i2c_driver(e4000_driver);

MODULE_DESCRIPTION("Elonics E4000 silicon tuner driver");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_LICENSE("GPL");
