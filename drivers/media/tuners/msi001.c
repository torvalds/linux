/*
 * Mirics MSi001 silicon tuner driver
 *
 * Copyright (C) 2013 Antti Palosaari <crope@iki.fi>
 * Copyright (C) 2014 Antti Palosaari <crope@iki.fi>
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
 */

#include <linux/module.h>
#include <linux/gcd.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

static const struct v4l2_frequency_band bands[] = {
	{
		.type = V4L2_TUNER_RF,
		.index = 0,
		.capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   =   49000000,
		.rangehigh  =  263000000,
	}, {
		.type = V4L2_TUNER_RF,
		.index = 1,
		.capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   =  390000000,
		.rangehigh  =  960000000,
	},
};

struct msi001 {
	struct spi_device *spi;
	struct v4l2_subdev sd;

	/* Controls */
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *bandwidth_auto;
	struct v4l2_ctrl *bandwidth;
	struct v4l2_ctrl *lna_gain;
	struct v4l2_ctrl *mixer_gain;
	struct v4l2_ctrl *if_gain;

	unsigned int f_tuner;
};

static inline struct msi001 *sd_to_msi001(struct v4l2_subdev *sd)
{
	return container_of(sd, struct msi001, sd);
}

static int msi001_wreg(struct msi001 *s, u32 data)
{
	/* Register format: 4 bits addr + 20 bits value */
	return spi_write(s->spi, &data, 3);
};

static int msi001_set_gain(struct msi001 *s, int lna_gain, int mixer_gain,
		int if_gain)
{
	int ret;
	u32 reg;

	dev_dbg(&s->spi->dev, "lna=%d mixer=%d if=%d\n",
			lna_gain, mixer_gain, if_gain);

	reg = 1 << 0;
	reg |= (59 - if_gain) << 4;
	reg |= 0 << 10;
	reg |= (1 - mixer_gain) << 12;
	reg |= (1 - lna_gain) << 13;
	reg |= 4 << 14;
	reg |= 0 << 17;
	ret = msi001_wreg(s, reg);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&s->spi->dev, "failed %d\n", ret);
	return ret;
};

static int msi001_set_tuner(struct msi001 *s)
{
	int ret, i;
	unsigned int n, m, thresh, frac, vco_step, tmp, f_if1;
	u32 reg;
	u64 f_vco, tmp64;
	u8 mode, filter_mode, lo_div;

	static const struct {
		u32 rf;
		u8 mode;
		u8 lo_div;
	} band_lut[] = {
		{ 50000000, 0xe1, 16}, /* AM_MODE2, antenna 2 */
		{108000000, 0x42, 32}, /* VHF_MODE */
		{330000000, 0x44, 16}, /* B3_MODE */
		{960000000, 0x48,  4}, /* B45_MODE */
		{      ~0U, 0x50,  2}, /* BL_MODE */
	};
	static const struct {
		u32 freq;
		u8 filter_mode;
	} if_freq_lut[] = {
		{      0, 0x03}, /* Zero IF */
		{ 450000, 0x02}, /* 450 kHz IF */
		{1620000, 0x01}, /* 1.62 MHz IF */
		{2048000, 0x00}, /* 2.048 MHz IF */
	};
	static const struct {
		u32 freq;
		u8 val;
	} bandwidth_lut[] = {
		{ 200000, 0x00}, /* 200 kHz */
		{ 300000, 0x01}, /* 300 kHz */
		{ 600000, 0x02}, /* 600 kHz */
		{1536000, 0x03}, /* 1.536 MHz */
		{5000000, 0x04}, /* 5 MHz */
		{6000000, 0x05}, /* 6 MHz */
		{7000000, 0x06}, /* 7 MHz */
		{8000000, 0x07}, /* 8 MHz */
	};

	unsigned int f_rf = s->f_tuner;

	/*
	 * bandwidth (Hz)
	 * 200000, 300000, 600000, 1536000, 5000000, 6000000, 7000000, 8000000
	 */
	unsigned int bandwidth;

	/*
	 * intermediate frequency (Hz)
	 * 0, 450000, 1620000, 2048000
	 */
	unsigned int f_if = 0;
	#define F_REF 24000000
	#define R_REF 4
	#define F_OUT_STEP 1

	dev_dbg(&s->spi->dev, "f_rf=%d f_if=%d\n", f_rf, f_if);

	for (i = 0; i < ARRAY_SIZE(band_lut); i++) {
		if (f_rf <= band_lut[i].rf) {
			mode = band_lut[i].mode;
			lo_div = band_lut[i].lo_div;
			break;
		}
	}

	if (i == ARRAY_SIZE(band_lut)) {
		ret = -EINVAL;
		goto err;
	}

	/* AM_MODE is upconverted */
	if ((mode >> 0) & 0x1)
		f_if1 =  5 * F_REF;
	else
		f_if1 =  0;

	for (i = 0; i < ARRAY_SIZE(if_freq_lut); i++) {
		if (f_if == if_freq_lut[i].freq) {
			filter_mode = if_freq_lut[i].filter_mode;
			break;
		}
	}

	if (i == ARRAY_SIZE(if_freq_lut)) {
		ret = -EINVAL;
		goto err;
	}

	/* filters */
	bandwidth = s->bandwidth->val;
	bandwidth = clamp(bandwidth, 200000U, 8000000U);

	for (i = 0; i < ARRAY_SIZE(bandwidth_lut); i++) {
		if (bandwidth <= bandwidth_lut[i].freq) {
			bandwidth = bandwidth_lut[i].val;
			break;
		}
	}

	if (i == ARRAY_SIZE(bandwidth_lut)) {
		ret = -EINVAL;
		goto err;
	}

	s->bandwidth->val = bandwidth_lut[i].freq;

	dev_dbg(&s->spi->dev, "bandwidth selected=%d\n", bandwidth_lut[i].freq);

	f_vco = (u64) (f_rf + f_if + f_if1) * lo_div;
	tmp64 = f_vco;
	m = do_div(tmp64, F_REF * R_REF);
	n = (unsigned int) tmp64;

	vco_step = F_OUT_STEP * lo_div;
	thresh = (F_REF * R_REF) / vco_step;
	frac = 1ul * thresh * m / (F_REF * R_REF);

	/* Find out greatest common divisor and divide to smaller. */
	tmp = gcd(thresh, frac);
	thresh /= tmp;
	frac /= tmp;

	/* Force divide to reg max. Resolution will be reduced. */
	tmp = DIV_ROUND_UP(thresh, 4095);
	thresh = DIV_ROUND_CLOSEST(thresh, tmp);
	frac = DIV_ROUND_CLOSEST(frac, tmp);

	/* calc real RF set */
	tmp = 1ul * F_REF * R_REF * n;
	tmp += 1ul * F_REF * R_REF * frac / thresh;
	tmp /= lo_div;

	dev_dbg(&s->spi->dev, "rf=%u:%u n=%d thresh=%d frac=%d\n",
				f_rf, tmp, n, thresh, frac);

	ret = msi001_wreg(s, 0x00000e);
	if (ret)
		goto err;

	ret = msi001_wreg(s, 0x000003);
	if (ret)
		goto err;

	reg = 0 << 0;
	reg |= mode << 4;
	reg |= filter_mode << 12;
	reg |= bandwidth << 14;
	reg |= 0x02 << 17;
	reg |= 0x00 << 20;
	ret = msi001_wreg(s, reg);
	if (ret)
		goto err;

	reg = 5 << 0;
	reg |= thresh << 4;
	reg |= 1 << 19;
	reg |= 1 << 21;
	ret = msi001_wreg(s, reg);
	if (ret)
		goto err;

	reg = 2 << 0;
	reg |= frac << 4;
	reg |= n << 16;
	ret = msi001_wreg(s, reg);
	if (ret)
		goto err;

	ret = msi001_set_gain(s, s->lna_gain->cur.val, s->mixer_gain->cur.val,
			s->if_gain->cur.val);
	if (ret)
		goto err;

	reg = 6 << 0;
	reg |= 63 << 4;
	reg |= 4095 << 10;
	ret = msi001_wreg(s, reg);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&s->spi->dev, "failed %d\n", ret);
	return ret;
};

static int msi001_s_power(struct v4l2_subdev *sd, int on)
{
	struct msi001 *s = sd_to_msi001(sd);
	int ret;

	dev_dbg(&s->spi->dev, "on=%d\n", on);

	if (on)
		ret = 0;
	else
		ret = msi001_wreg(s, 0x000000);

	return ret;
}

static const struct v4l2_subdev_core_ops msi001_core_ops = {
	.s_power                  = msi001_s_power,
};

static int msi001_g_tuner(struct v4l2_subdev *sd, struct v4l2_tuner *v)
{
	struct msi001 *s = sd_to_msi001(sd);

	dev_dbg(&s->spi->dev, "index=%d\n", v->index);

	strlcpy(v->name, "Mirics MSi001", sizeof(v->name));
	v->type = V4L2_TUNER_RF;
	v->capability = V4L2_TUNER_CAP_1HZ | V4L2_TUNER_CAP_FREQ_BANDS;
	v->rangelow =    49000000;
	v->rangehigh =  960000000;

	return 0;
}

static int msi001_s_tuner(struct v4l2_subdev *sd, const struct v4l2_tuner *v)
{
	struct msi001 *s = sd_to_msi001(sd);

	dev_dbg(&s->spi->dev, "index=%d\n", v->index);
	return 0;
}

static int msi001_g_frequency(struct v4l2_subdev *sd, struct v4l2_frequency *f)
{
	struct msi001 *s = sd_to_msi001(sd);

	dev_dbg(&s->spi->dev, "tuner=%d\n", f->tuner);
	f->frequency = s->f_tuner;
	return 0;
}

static int msi001_s_frequency(struct v4l2_subdev *sd,
		const struct v4l2_frequency *f)
{
	struct msi001 *s = sd_to_msi001(sd);
	unsigned int band;

	dev_dbg(&s->spi->dev, "tuner=%d type=%d frequency=%u\n",
			f->tuner, f->type, f->frequency);

	if (f->frequency < ((bands[0].rangehigh + bands[1].rangelow) / 2))
		band = 0;
	else
		band = 1;
	s->f_tuner = clamp_t(unsigned int, f->frequency,
			bands[band].rangelow, bands[band].rangehigh);

	return msi001_set_tuner(s);
}

static int msi001_enum_freq_bands(struct v4l2_subdev *sd,
		struct v4l2_frequency_band *band)
{
	struct msi001 *s = sd_to_msi001(sd);

	dev_dbg(&s->spi->dev, "tuner=%d type=%d index=%d\n",
			band->tuner, band->type, band->index);

	if (band->index >= ARRAY_SIZE(bands))
		return -EINVAL;

	band->capability = bands[band->index].capability;
	band->rangelow = bands[band->index].rangelow;
	band->rangehigh = bands[band->index].rangehigh;

	return 0;
}

static const struct v4l2_subdev_tuner_ops msi001_tuner_ops = {
	.g_tuner                  = msi001_g_tuner,
	.s_tuner                  = msi001_s_tuner,
	.g_frequency              = msi001_g_frequency,
	.s_frequency              = msi001_s_frequency,
	.enum_freq_bands          = msi001_enum_freq_bands,
};

static const struct v4l2_subdev_ops msi001_ops = {
	.core                     = &msi001_core_ops,
	.tuner                    = &msi001_tuner_ops,
};

static int msi001_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct msi001 *s = container_of(ctrl->handler, struct msi001, hdl);

	int ret;

	dev_dbg(&s->spi->dev,
			"id=%d name=%s val=%d min=%lld max=%lld step=%lld\n",
			ctrl->id, ctrl->name, ctrl->val,
			ctrl->minimum, ctrl->maximum, ctrl->step);

	switch (ctrl->id) {
	case V4L2_CID_RF_TUNER_BANDWIDTH_AUTO:
	case V4L2_CID_RF_TUNER_BANDWIDTH:
		ret = msi001_set_tuner(s);
		break;
	case  V4L2_CID_RF_TUNER_LNA_GAIN:
		ret = msi001_set_gain(s, s->lna_gain->val,
				s->mixer_gain->cur.val, s->if_gain->cur.val);
		break;
	case  V4L2_CID_RF_TUNER_MIXER_GAIN:
		ret = msi001_set_gain(s, s->lna_gain->cur.val,
				s->mixer_gain->val, s->if_gain->cur.val);
		break;
	case  V4L2_CID_RF_TUNER_IF_GAIN:
		ret = msi001_set_gain(s, s->lna_gain->cur.val,
				s->mixer_gain->cur.val, s->if_gain->val);
		break;
	default:
		dev_dbg(&s->spi->dev, "unkown control %d\n", ctrl->id);
		ret = -EINVAL;
	}

	return ret;
}

static const struct v4l2_ctrl_ops msi001_ctrl_ops = {
	.s_ctrl                   = msi001_s_ctrl,
};

static int msi001_probe(struct spi_device *spi)
{
	struct msi001 *s;
	int ret;

	dev_dbg(&spi->dev, "\n");

	s = kzalloc(sizeof(struct msi001), GFP_KERNEL);
	if (s == NULL) {
		ret = -ENOMEM;
		dev_dbg(&spi->dev, "Could not allocate memory for msi001\n");
		goto err_kfree;
	}

	s->spi = spi;
	s->f_tuner = bands[0].rangelow;
	v4l2_spi_subdev_init(&s->sd, spi, &msi001_ops);

	/* Register controls */
	v4l2_ctrl_handler_init(&s->hdl, 5);
	s->bandwidth_auto = v4l2_ctrl_new_std(&s->hdl, &msi001_ctrl_ops,
			V4L2_CID_RF_TUNER_BANDWIDTH_AUTO, 0, 1, 1, 1);
	s->bandwidth = v4l2_ctrl_new_std(&s->hdl, &msi001_ctrl_ops,
			V4L2_CID_RF_TUNER_BANDWIDTH, 200000, 8000000, 1, 200000);
	v4l2_ctrl_auto_cluster(2, &s->bandwidth_auto, 0, false);
	s->lna_gain = v4l2_ctrl_new_std(&s->hdl, &msi001_ctrl_ops,
			V4L2_CID_RF_TUNER_LNA_GAIN, 0, 1, 1, 1);
	s->mixer_gain = v4l2_ctrl_new_std(&s->hdl, &msi001_ctrl_ops,
			V4L2_CID_RF_TUNER_MIXER_GAIN, 0, 1, 1, 1);
	s->if_gain = v4l2_ctrl_new_std(&s->hdl, &msi001_ctrl_ops,
			V4L2_CID_RF_TUNER_IF_GAIN, 0, 59, 1, 0);
	if (s->hdl.error) {
		ret = s->hdl.error;
		dev_err(&s->spi->dev, "Could not initialize controls\n");
		/* control init failed, free handler */
		goto err_ctrl_handler_free;
	}

	s->sd.ctrl_handler = &s->hdl;
	return 0;

err_ctrl_handler_free:
	v4l2_ctrl_handler_free(&s->hdl);
err_kfree:
	kfree(s);
	return ret;
}

static int msi001_remove(struct spi_device *spi)
{
	struct v4l2_subdev *sd = spi_get_drvdata(spi);
	struct msi001 *s = sd_to_msi001(sd);

	dev_dbg(&spi->dev, "\n");

	/*
	 * Registered by v4l2_spi_new_subdev() from master driver, but we must
	 * unregister it from here. Weird.
	 */
	v4l2_device_unregister_subdev(&s->sd);
	v4l2_ctrl_handler_free(&s->hdl);
	kfree(s);
	return 0;
}

static const struct spi_device_id msi001_id[] = {
	{"msi001", 0},
	{}
};
MODULE_DEVICE_TABLE(spi, msi001_id);

static struct spi_driver msi001_driver = {
	.driver = {
		.name	= "msi001",
		.owner	= THIS_MODULE,
	},
	.probe		= msi001_probe,
	.remove		= msi001_remove,
	.id_table	= msi001_id,
};
module_spi_driver(msi001_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Mirics MSi001");
MODULE_LICENSE("GPL");
