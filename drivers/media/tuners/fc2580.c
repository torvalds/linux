// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * FCI FC2580 silicon tuner driver
 *
 * Copyright (C) 2012 Antti Palosaari <crope@iki.fi>
 */

#include "fc2580_priv.h"

/*
 * TODO:
 * I2C write and read works only for one single register. Multiple registers
 * could not be accessed using normal register address auto-increment.
 * There could be (very likely) register to change that behavior....
 */

/* write single register conditionally only when value differs from 0xff
 * XXX: This is special routine meant only for writing fc2580_freq_regs_lut[]
 * values. Do not use for the other purposes. */
static int fc2580_wr_reg_ff(struct fc2580_dev *dev, u8 reg, u8 val)
{
	if (val == 0xff)
		return 0;
	else
		return regmap_write(dev->regmap, reg, val);
}

static int fc2580_set_params(struct fc2580_dev *dev)
{
	struct i2c_client *client = dev->client;
	int ret, i;
	unsigned int uitmp, div_ref, div_ref_val, div_n, k, k_cw, div_out;
	u64 f_vco;
	u8 synth_config;
	unsigned long timeout;

	if (!dev->active) {
		dev_dbg(&client->dev, "tuner is sleeping\n");
		return 0;
	}

	/*
	 * Fractional-N synthesizer
	 *
	 *                      +---------------------------------------+
	 *                      v                                       |
	 *  Fref   +----+     +----+     +-------+         +----+     +------+     +---+
	 * ------> | /R | --> | PD | --> |  VCO  | ------> | /2 | --> | /N.F | <-- | K |
	 *         +----+     +----+     +-------+         +----+     +------+     +---+
	 *                                 |
	 *                                 |
	 *                                 v
	 *                               +-------+  Fout
	 *                               | /Rout | ------>
	 *                               +-------+
	 */
	for (i = 0; i < ARRAY_SIZE(fc2580_pll_lut); i++) {
		if (dev->f_frequency <= fc2580_pll_lut[i].freq)
			break;
	}
	if (i == ARRAY_SIZE(fc2580_pll_lut)) {
		ret = -EINVAL;
		goto err;
	}

	#define DIV_PRE_N 2
	#define F_REF dev->clk
	div_out = fc2580_pll_lut[i].div_out;
	f_vco = (u64) dev->f_frequency * div_out;
	synth_config = fc2580_pll_lut[i].band;
	if (f_vco < 2600000000ULL)
		synth_config |= 0x06;
	else
		synth_config |= 0x0e;

	/* select reference divider R (keep PLL div N in valid range) */
	#define DIV_N_MIN 76
	if (f_vco >= div_u64((u64) DIV_PRE_N * DIV_N_MIN * F_REF, 1)) {
		div_ref = 1;
		div_ref_val = 0x00;
	} else if (f_vco >= div_u64((u64) DIV_PRE_N * DIV_N_MIN * F_REF, 2)) {
		div_ref = 2;
		div_ref_val = 0x10;
	} else {
		div_ref = 4;
		div_ref_val = 0x20;
	}

	/* calculate PLL integer and fractional control word */
	uitmp = DIV_PRE_N * F_REF / div_ref;
	div_n = div_u64_rem(f_vco, uitmp, &k);
	k_cw = div_u64((u64) k * 0x100000, uitmp);

	dev_dbg(&client->dev,
		"frequency=%u bandwidth=%u f_vco=%llu F_REF=%u div_ref=%u div_n=%u k=%u div_out=%u k_cw=%0x\n",
		dev->f_frequency, dev->f_bandwidth, f_vco, F_REF, div_ref,
		div_n, k, div_out, k_cw);

	ret = regmap_write(dev->regmap, 0x02, synth_config);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x18, div_ref_val << 0 | k_cw >> 16);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x1a, (k_cw >> 8) & 0xff);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x1b, (k_cw >> 0) & 0xff);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x1c, div_n);
	if (ret)
		goto err;

	/* registers */
	for (i = 0; i < ARRAY_SIZE(fc2580_freq_regs_lut); i++) {
		if (dev->f_frequency <= fc2580_freq_regs_lut[i].freq)
			break;
	}
	if (i == ARRAY_SIZE(fc2580_freq_regs_lut)) {
		ret = -EINVAL;
		goto err;
	}

	ret = fc2580_wr_reg_ff(dev, 0x25, fc2580_freq_regs_lut[i].r25_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x27, fc2580_freq_regs_lut[i].r27_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x28, fc2580_freq_regs_lut[i].r28_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x29, fc2580_freq_regs_lut[i].r29_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x2b, fc2580_freq_regs_lut[i].r2b_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x2c, fc2580_freq_regs_lut[i].r2c_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x2d, fc2580_freq_regs_lut[i].r2d_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x30, fc2580_freq_regs_lut[i].r30_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x44, fc2580_freq_regs_lut[i].r44_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x50, fc2580_freq_regs_lut[i].r50_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x53, fc2580_freq_regs_lut[i].r53_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x5f, fc2580_freq_regs_lut[i].r5f_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x61, fc2580_freq_regs_lut[i].r61_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x62, fc2580_freq_regs_lut[i].r62_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x63, fc2580_freq_regs_lut[i].r63_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x67, fc2580_freq_regs_lut[i].r67_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x68, fc2580_freq_regs_lut[i].r68_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x69, fc2580_freq_regs_lut[i].r69_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x6a, fc2580_freq_regs_lut[i].r6a_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x6b, fc2580_freq_regs_lut[i].r6b_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x6c, fc2580_freq_regs_lut[i].r6c_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x6d, fc2580_freq_regs_lut[i].r6d_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x6e, fc2580_freq_regs_lut[i].r6e_val);
	if (ret)
		goto err;

	ret = fc2580_wr_reg_ff(dev, 0x6f, fc2580_freq_regs_lut[i].r6f_val);
	if (ret)
		goto err;

	/* IF filters */
	for (i = 0; i < ARRAY_SIZE(fc2580_if_filter_lut); i++) {
		if (dev->f_bandwidth <= fc2580_if_filter_lut[i].freq)
			break;
	}
	if (i == ARRAY_SIZE(fc2580_if_filter_lut)) {
		ret = -EINVAL;
		goto err;
	}

	ret = regmap_write(dev->regmap, 0x36, fc2580_if_filter_lut[i].r36_val);
	if (ret)
		goto err;

	uitmp = (unsigned int) 8058000 - (dev->f_bandwidth * 122 / 100 / 2);
	uitmp = div64_u64((u64) dev->clk * uitmp, 1000000000000ULL);
	ret = regmap_write(dev->regmap, 0x37, uitmp);
	if (ret)
		goto err;

	ret = regmap_write(dev->regmap, 0x39, fc2580_if_filter_lut[i].r39_val);
	if (ret)
		goto err;

	timeout = jiffies + msecs_to_jiffies(30);
	for (uitmp = ~0xc0; !time_after(jiffies, timeout) && uitmp != 0xc0;) {
		/* trigger filter */
		ret = regmap_write(dev->regmap, 0x2e, 0x09);
		if (ret)
			goto err;

		/* locked when [7:6] are set (val: d7 6MHz, d5 7MHz, cd 8MHz) */
		ret = regmap_read(dev->regmap, 0x2f, &uitmp);
		if (ret)
			goto err;
		uitmp &= 0xc0;

		ret = regmap_write(dev->regmap, 0x2e, 0x01);
		if (ret)
			goto err;
	}
	if (uitmp != 0xc0)
		dev_dbg(&client->dev, "filter did not lock %02x\n", uitmp);

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int fc2580_init(struct fc2580_dev *dev)
{
	struct i2c_client *client = dev->client;
	int ret, i;

	dev_dbg(&client->dev, "\n");

	for (i = 0; i < ARRAY_SIZE(fc2580_init_reg_vals); i++) {
		ret = regmap_write(dev->regmap, fc2580_init_reg_vals[i].reg,
				fc2580_init_reg_vals[i].val);
		if (ret)
			goto err;
	}

	dev->active = true;
	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int fc2580_sleep(struct fc2580_dev *dev)
{
	struct i2c_client *client = dev->client;
	int ret;

	dev_dbg(&client->dev, "\n");

	dev->active = false;

	ret = regmap_write(dev->regmap, 0x02, 0x0a);
	if (ret)
		goto err;
	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

/*
 * DVB API
 */
static int fc2580_dvb_set_params(struct dvb_frontend *fe)
{
	struct fc2580_dev *dev = fe->tuner_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	dev->f_frequency = c->frequency;
	dev->f_bandwidth = c->bandwidth_hz;
	return fc2580_set_params(dev);
}

static int fc2580_dvb_init(struct dvb_frontend *fe)
{
	return fc2580_init(fe->tuner_priv);
}

static int fc2580_dvb_sleep(struct dvb_frontend *fe)
{
	return fc2580_sleep(fe->tuner_priv);
}

static int fc2580_dvb_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	*frequency = 0; /* Zero-IF */
	return 0;
}

static const struct dvb_tuner_ops fc2580_dvb_tuner_ops = {
	.info = {
		.name             = "FCI FC2580",
		.frequency_min_hz = 174 * MHz,
		.frequency_max_hz = 862 * MHz,
	},

	.init = fc2580_dvb_init,
	.sleep = fc2580_dvb_sleep,
	.set_params = fc2580_dvb_set_params,

	.get_if_frequency = fc2580_dvb_get_if_frequency,
};

/*
 * V4L2 API
 */
#if IS_ENABLED(CONFIG_VIDEO_DEV)
static const struct v4l2_frequency_band bands[] = {
	{
		.type = V4L2_TUNER_RF,
		.index = 0,
		.capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   =   130000000,
		.rangehigh  =  2000000000,
	},
};

static inline struct fc2580_dev *fc2580_subdev_to_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct fc2580_dev, subdev);
}

static int fc2580_standby(struct v4l2_subdev *sd)
{
	struct fc2580_dev *dev = fc2580_subdev_to_dev(sd);
	int ret;

	ret = fc2580_sleep(dev);
	if (ret)
		return ret;

	return fc2580_set_params(dev);
}

static int fc2580_g_tuner(struct v4l2_subdev *sd, struct v4l2_tuner *v)
{
	struct fc2580_dev *dev = fc2580_subdev_to_dev(sd);
	struct i2c_client *client = dev->client;

	dev_dbg(&client->dev, "index=%d\n", v->index);

	strscpy(v->name, "FCI FC2580", sizeof(v->name));
	v->type = V4L2_TUNER_RF;
	v->capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS;
	v->rangelow  = bands[0].rangelow;
	v->rangehigh = bands[0].rangehigh;
	return 0;
}

static int fc2580_s_tuner(struct v4l2_subdev *sd, const struct v4l2_tuner *v)
{
	struct fc2580_dev *dev = fc2580_subdev_to_dev(sd);
	struct i2c_client *client = dev->client;

	dev_dbg(&client->dev, "index=%d\n", v->index);
	return 0;
}

static int fc2580_g_frequency(struct v4l2_subdev *sd, struct v4l2_frequency *f)
{
	struct fc2580_dev *dev = fc2580_subdev_to_dev(sd);
	struct i2c_client *client = dev->client;

	dev_dbg(&client->dev, "tuner=%d\n", f->tuner);
	f->frequency = dev->f_frequency;
	return 0;
}

static int fc2580_s_frequency(struct v4l2_subdev *sd,
			      const struct v4l2_frequency *f)
{
	struct fc2580_dev *dev = fc2580_subdev_to_dev(sd);
	struct i2c_client *client = dev->client;

	dev_dbg(&client->dev, "tuner=%d type=%d frequency=%u\n",
		f->tuner, f->type, f->frequency);

	dev->f_frequency = clamp_t(unsigned int, f->frequency,
				   bands[0].rangelow, bands[0].rangehigh);
	return fc2580_set_params(dev);
}

static int fc2580_enum_freq_bands(struct v4l2_subdev *sd,
				  struct v4l2_frequency_band *band)
{
	struct fc2580_dev *dev = fc2580_subdev_to_dev(sd);
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

static const struct v4l2_subdev_tuner_ops fc2580_subdev_tuner_ops = {
	.standby                  = fc2580_standby,
	.g_tuner                  = fc2580_g_tuner,
	.s_tuner                  = fc2580_s_tuner,
	.g_frequency              = fc2580_g_frequency,
	.s_frequency              = fc2580_s_frequency,
	.enum_freq_bands          = fc2580_enum_freq_bands,
};

static const struct v4l2_subdev_ops fc2580_subdev_ops = {
	.tuner                    = &fc2580_subdev_tuner_ops,
};

static int fc2580_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct fc2580_dev *dev = container_of(ctrl->handler, struct fc2580_dev, hdl);
	struct i2c_client *client = dev->client;
	int ret;

	dev_dbg(&client->dev, "ctrl: id=%d name=%s cur.val=%d val=%d\n",
		ctrl->id, ctrl->name, ctrl->cur.val, ctrl->val);

	switch (ctrl->id) {
	case V4L2_CID_RF_TUNER_BANDWIDTH_AUTO:
	case V4L2_CID_RF_TUNER_BANDWIDTH:
		/*
		 * TODO: Auto logic does not work 100% correctly as tuner driver
		 * do not have information to calculate maximum suitable
		 * bandwidth. Calculating it is responsible of master driver.
		 */
		dev->f_bandwidth = dev->bandwidth->val;
		ret = fc2580_set_params(dev);
		break;
	default:
		dev_dbg(&client->dev, "unknown ctrl");
		ret = -EINVAL;
	}
	return ret;
}

static const struct v4l2_ctrl_ops fc2580_ctrl_ops = {
	.s_ctrl = fc2580_s_ctrl,
};
#endif

static struct v4l2_subdev *fc2580_get_v4l2_subdev(struct i2c_client *client)
{
	struct fc2580_dev *dev = i2c_get_clientdata(client);

	if (dev->subdev.ops)
		return &dev->subdev;
	else
		return NULL;
}

static int fc2580_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct fc2580_dev *dev;
	struct fc2580_platform_data *pdata = client->dev.platform_data;
	struct dvb_frontend *fe = pdata->dvb_frontend;
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

	if (pdata->clk)
		dev->clk = pdata->clk;
	else
		dev->clk = 16384000; /* internal clock */
	dev->client = client;
	dev->regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(dev->regmap)) {
		ret = PTR_ERR(dev->regmap);
		goto err_kfree;
	}

	/* check if the tuner is there */
	ret = regmap_read(dev->regmap, 0x01, &uitmp);
	if (ret)
		goto err_kfree;

	dev_dbg(&client->dev, "chip_id=%02x\n", uitmp);

	switch (uitmp) {
	case 0x56:
	case 0x5a:
		break;
	default:
		ret = -ENODEV;
		goto err_kfree;
	}

#if IS_ENABLED(CONFIG_VIDEO_DEV)
	/* Register controls */
	v4l2_ctrl_handler_init(&dev->hdl, 2);
	dev->bandwidth_auto = v4l2_ctrl_new_std(&dev->hdl, &fc2580_ctrl_ops,
						V4L2_CID_RF_TUNER_BANDWIDTH_AUTO,
						0, 1, 1, 1);
	dev->bandwidth = v4l2_ctrl_new_std(&dev->hdl, &fc2580_ctrl_ops,
					   V4L2_CID_RF_TUNER_BANDWIDTH,
					   3000, 10000000, 1, 3000);
	v4l2_ctrl_auto_cluster(2, &dev->bandwidth_auto, 0, false);
	if (dev->hdl.error) {
		ret = dev->hdl.error;
		dev_err(&client->dev, "Could not initialize controls\n");
		v4l2_ctrl_handler_free(&dev->hdl);
		goto err_kfree;
	}
	dev->subdev.ctrl_handler = &dev->hdl;
	dev->f_frequency = bands[0].rangelow;
	dev->f_bandwidth = dev->bandwidth->val;
	v4l2_i2c_subdev_init(&dev->subdev, client, &fc2580_subdev_ops);
#endif
	fe->tuner_priv = dev;
	memcpy(&fe->ops.tuner_ops, &fc2580_dvb_tuner_ops,
	       sizeof(fe->ops.tuner_ops));
	pdata->get_v4l2_subdev = fc2580_get_v4l2_subdev;
	i2c_set_clientdata(client, dev);

	dev_info(&client->dev, "FCI FC2580 successfully identified\n");
	return 0;
err_kfree:
	kfree(dev);
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int fc2580_remove(struct i2c_client *client)
{
	struct fc2580_dev *dev = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

#if IS_ENABLED(CONFIG_VIDEO_DEV)
	v4l2_ctrl_handler_free(&dev->hdl);
#endif
	kfree(dev);
	return 0;
}

static const struct i2c_device_id fc2580_id_table[] = {
	{"fc2580", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, fc2580_id_table);

static struct i2c_driver fc2580_driver = {
	.driver = {
		.name	= "fc2580",
		.suppress_bind_attrs = true,
	},
	.probe		= fc2580_probe,
	.remove		= fc2580_remove,
	.id_table	= fc2580_id_table,
};

module_i2c_driver(fc2580_driver);

MODULE_DESCRIPTION("FCI FC2580 silicon tuner driver");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_LICENSE("GPL");
