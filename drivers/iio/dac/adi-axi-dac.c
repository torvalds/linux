// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices Generic AXI DAC IP core
 * Link: https://wiki.analog.com/resources/fpga/docs/axi_dac_ip
 *
 * Copyright 2016-2024 Analog Devices Inc.
 */
#include <linux/adi-axi-common.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/limits.h>
#include <linux/kstrtox.h>
#include <linux/math.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/units.h>

#include <linux/iio/backend.h>
#include <linux/iio/buffer-dmaengine.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>

#include "ad3552r-hs.h"

/*
 * Register definitions:
 *   https://wiki.analog.com/resources/fpga/docs/axi_dac_ip#register_map
 */

/* Base controls */
#define AXI_DAC_CONFIG_REG			0x0c
#define   AXI_DAC_CONFIG_DDS_DISABLE		BIT(6)

 /* DAC controls */
#define AXI_DAC_RSTN_REG			0x0040
#define   AXI_DAC_RSTN_CE_N			BIT(2)
#define   AXI_DAC_RSTN_MMCM_RSTN		BIT(1)
#define   AXI_DAC_RSTN_RSTN			BIT(0)
#define AXI_DAC_CNTRL_1_REG			0x0044
#define   AXI_DAC_CNTRL_1_SYNC			BIT(0)
#define AXI_DAC_CNTRL_2_REG			0x0048
#define   AXI_DAC_CNTRL_2_SDR_DDR_N		BIT(16)
#define   AXI_DAC_CNTRL_2_SYMB_8B		BIT(14)
#define   ADI_DAC_CNTRL_2_R1_MODE		BIT(5)
#define   AXI_DAC_CNTRL_2_UNSIGNED_DATA		BIT(4)
#define AXI_DAC_STATUS_1_REG			0x0054
#define AXI_DAC_STATUS_2_REG			0x0058
#define AXI_DAC_DRP_STATUS_REG			0x0074
#define   AXI_DAC_DRP_STATUS_DRP_LOCKED		BIT(17)
#define AXI_DAC_CUSTOM_RD_REG			0x0080
#define AXI_DAC_CUSTOM_WR_REG			0x0084
#define   AXI_DAC_CUSTOM_WR_DATA_8		GENMASK(23, 16)
#define   AXI_DAC_CUSTOM_WR_DATA_16		GENMASK(23, 8)
#define AXI_DAC_UI_STATUS_REG			0x0088
#define   AXI_DAC_UI_STATUS_IF_BUSY		BIT(4)
#define AXI_DAC_CUSTOM_CTRL_REG			0x008C
#define   AXI_DAC_CUSTOM_CTRL_ADDRESS		GENMASK(31, 24)
#define   AXI_DAC_CUSTOM_CTRL_MULTI_IO_MODE	GENMASK(3, 2)
#define   AXI_DAC_CUSTOM_CTRL_STREAM		BIT(1)
#define   AXI_DAC_CUSTOM_CTRL_TRANSFER_DATA	BIT(0)

#define AXI_DAC_CUSTOM_CTRL_STREAM_ENABLE	(AXI_DAC_CUSTOM_CTRL_TRANSFER_DATA | \
						 AXI_DAC_CUSTOM_CTRL_STREAM)

/* DAC Channel controls */
#define AXI_DAC_CHAN_CNTRL_1_REG(c)		(0x0400 + (c) * 0x40)
#define AXI_DAC_CHAN_CNTRL_3_REG(c)		(0x0408 + (c) * 0x40)
#define   AXI_DAC_CHAN_CNTRL_3_SCALE_SIGN	BIT(15)
#define   AXI_DAC_CHAN_CNTRL_3_SCALE_INT	BIT(14)
#define   AXI_DAC_CHAN_CNTRL_3_SCALE		GENMASK(14, 0)
#define AXI_DAC_CHAN_CNTRL_2_REG(c)		(0x0404 + (c) * 0x40)
#define   AXI_DAC_CHAN_CNTRL_2_PHASE		GENMASK(31, 16)
#define   AXI_DAC_CHAN_CNTRL_2_FREQUENCY	GENMASK(15, 0)
#define AXI_DAC_CHAN_CNTRL_4_REG(c)		(0x040c + (c) * 0x40)
#define AXI_DAC_CHAN_CNTRL_7_REG(c)		(0x0418 + (c) * 0x40)
#define   AXI_DAC_CHAN_CNTRL_7_DATA_SEL		GENMASK(3, 0)

#define AXI_DAC_CHAN_CNTRL_MAX			15
#define AXI_DAC_RD_ADDR(x)			(BIT(7) | (x))

/* 360 degrees in rad */
#define AXI_DAC_2_PI_MEGA			6283190

enum {
	AXI_DAC_DATA_INTERNAL_TONE,
	AXI_DAC_DATA_DMA = 2,
	AXI_DAC_DATA_INTERNAL_RAMP_16BIT = 11,
};

struct axi_dac_info {
	unsigned int version;
	const struct iio_backend_info *backend_info;
	bool has_dac_clk;
	bool has_child_nodes;
};

struct axi_dac_state {
	struct regmap *regmap;
	struct device *dev;
	/*
	 * lock to protect multiple accesses to the device registers and global
	 * data/variables.
	 */
	struct mutex lock;
	const struct axi_dac_info *info;
	u64 dac_clk;
	u32 reg_config;
	bool int_tone;
	int dac_clk_rate;
};

static int axi_dac_enable(struct iio_backend *back)
{
	struct axi_dac_state *st = iio_backend_get_priv(back);
	unsigned int __val;
	int ret;

	guard(mutex)(&st->lock);
	ret = regmap_set_bits(st->regmap, AXI_DAC_RSTN_REG,
			      AXI_DAC_RSTN_MMCM_RSTN);
	if (ret)
		return ret;
	/*
	 * Make sure the DRP (Dynamic Reconfiguration Port) is locked. Not all
	 * designs really use it but if they don't we still get the lock bit
	 * set. So let's do it all the time so the code is generic.
	 */
	ret = regmap_read_poll_timeout(st->regmap, AXI_DAC_DRP_STATUS_REG,
				       __val,
				       __val & AXI_DAC_DRP_STATUS_DRP_LOCKED,
				       100, 1000);
	if (ret)
		return ret;

	return regmap_set_bits(st->regmap, AXI_DAC_RSTN_REG,
			       AXI_DAC_RSTN_RSTN | AXI_DAC_RSTN_MMCM_RSTN);
}

static void axi_dac_disable(struct iio_backend *back)
{
	struct axi_dac_state *st = iio_backend_get_priv(back);

	guard(mutex)(&st->lock);
	regmap_write(st->regmap, AXI_DAC_RSTN_REG, 0);
}

static struct iio_buffer *axi_dac_request_buffer(struct iio_backend *back,
						 struct iio_dev *indio_dev)
{
	struct axi_dac_state *st = iio_backend_get_priv(back);
	const char *dma_name;

	if (device_property_read_string(st->dev, "dma-names", &dma_name))
		dma_name = "tx";

	return iio_dmaengine_buffer_setup_ext(st->dev, indio_dev, dma_name,
					      IIO_BUFFER_DIRECTION_OUT);
}

static void axi_dac_free_buffer(struct iio_backend *back,
				struct iio_buffer *buffer)
{
	iio_dmaengine_buffer_teardown(buffer);
}

enum {
	AXI_DAC_FREQ_TONE_1,
	AXI_DAC_FREQ_TONE_2,
	AXI_DAC_SCALE_TONE_1,
	AXI_DAC_SCALE_TONE_2,
	AXI_DAC_PHASE_TONE_1,
	AXI_DAC_PHASE_TONE_2,
};

static int __axi_dac_frequency_get(struct axi_dac_state *st, unsigned int chan,
				   unsigned int tone_2, unsigned int *freq)
{
	u32 reg, raw;
	int ret;

	if (chan > AXI_DAC_CHAN_CNTRL_MAX)
		return -EINVAL;

	if (!st->dac_clk) {
		dev_err(st->dev, "Sampling rate is 0...\n");
		return -EINVAL;
	}

	if (tone_2)
		reg = AXI_DAC_CHAN_CNTRL_4_REG(chan);
	else
		reg = AXI_DAC_CHAN_CNTRL_2_REG(chan);

	ret = regmap_read(st->regmap, reg, &raw);
	if (ret)
		return ret;

	raw = FIELD_GET(AXI_DAC_CHAN_CNTRL_2_FREQUENCY, raw);
	*freq = DIV_ROUND_CLOSEST_ULL(raw * st->dac_clk, BIT(16));

	return 0;
}

static int axi_dac_frequency_get(struct axi_dac_state *st,
				 const struct iio_chan_spec *chan, char *buf,
				 unsigned int tone_2)
{
	unsigned int freq;
	int ret;

	scoped_guard(mutex, &st->lock) {
		ret = __axi_dac_frequency_get(st, chan->channel, tone_2, &freq);
		if (ret)
			return ret;
	}

	return sysfs_emit(buf, "%u\n", freq);
}

static int axi_dac_scale_get(struct axi_dac_state *st,
			     const struct iio_chan_spec *chan, char *buf,
			     unsigned int tone_2)
{
	unsigned int scale, sign;
	int ret, vals[2];
	u32 reg, raw;

	if (chan->channel > AXI_DAC_CHAN_CNTRL_MAX)
		return -EINVAL;

	if (tone_2)
		reg = AXI_DAC_CHAN_CNTRL_3_REG(chan->channel);
	else
		reg = AXI_DAC_CHAN_CNTRL_1_REG(chan->channel);

	ret = regmap_read(st->regmap, reg, &raw);
	if (ret)
		return ret;

	sign = FIELD_GET(AXI_DAC_CHAN_CNTRL_3_SCALE_SIGN, raw);
	raw = FIELD_GET(AXI_DAC_CHAN_CNTRL_3_SCALE, raw);
	scale = DIV_ROUND_CLOSEST_ULL((u64)raw * MEGA,
				      AXI_DAC_CHAN_CNTRL_3_SCALE_INT);

	vals[0] = scale / MEGA;
	vals[1] = scale % MEGA;

	if (sign) {
		vals[0] *= -1;
		if (!vals[0])
			vals[1] *= -1;
	}

	return iio_format_value(buf, IIO_VAL_INT_PLUS_MICRO, ARRAY_SIZE(vals),
				vals);
}

static int axi_dac_phase_get(struct axi_dac_state *st,
			     const struct iio_chan_spec *chan, char *buf,
			     unsigned int tone_2)
{
	u32 reg, raw, phase;
	int ret, vals[2];

	if (chan->channel > AXI_DAC_CHAN_CNTRL_MAX)
		return -EINVAL;

	if (tone_2)
		reg = AXI_DAC_CHAN_CNTRL_4_REG(chan->channel);
	else
		reg = AXI_DAC_CHAN_CNTRL_2_REG(chan->channel);

	ret = regmap_read(st->regmap, reg, &raw);
	if (ret)
		return ret;

	raw = FIELD_GET(AXI_DAC_CHAN_CNTRL_2_PHASE, raw);
	phase = DIV_ROUND_CLOSEST_ULL((u64)raw * AXI_DAC_2_PI_MEGA, U16_MAX);

	vals[0] = phase / MEGA;
	vals[1] = phase % MEGA;

	return iio_format_value(buf, IIO_VAL_INT_PLUS_MICRO, ARRAY_SIZE(vals),
				vals);
}

static int __axi_dac_frequency_set(struct axi_dac_state *st, unsigned int chan,
				   u64 sample_rate, unsigned int freq,
				   unsigned int tone_2)
{
	u32 reg;
	u16 raw;
	int ret;

	if (chan > AXI_DAC_CHAN_CNTRL_MAX)
		return -EINVAL;

	if (!sample_rate || freq > sample_rate / 2) {
		dev_err(st->dev, "Invalid frequency(%u) dac_clk(%llu)\n",
			freq, sample_rate);
		return -EINVAL;
	}

	if (tone_2)
		reg = AXI_DAC_CHAN_CNTRL_4_REG(chan);
	else
		reg = AXI_DAC_CHAN_CNTRL_2_REG(chan);

	raw = DIV64_U64_ROUND_CLOSEST((u64)freq * BIT(16), sample_rate);

	ret = regmap_update_bits(st->regmap, reg,
				 AXI_DAC_CHAN_CNTRL_2_FREQUENCY, raw);
	if (ret)
		return ret;

	/* synchronize channels */
	return regmap_set_bits(st->regmap, AXI_DAC_CNTRL_1_REG,
			       AXI_DAC_CNTRL_1_SYNC);
}

static int axi_dac_frequency_set(struct axi_dac_state *st,
				 const struct iio_chan_spec *chan,
				 const char *buf, size_t len, unsigned int tone_2)
{
	unsigned int freq;
	int ret;

	ret = kstrtou32(buf, 10, &freq);
	if (ret)
		return ret;

	guard(mutex)(&st->lock);
	ret = __axi_dac_frequency_set(st, chan->channel, st->dac_clk, freq,
				      tone_2);
	if (ret)
		return ret;

	return len;
}

static int axi_dac_scale_set(struct axi_dac_state *st,
			     const struct iio_chan_spec *chan,
			     const char *buf, size_t len, unsigned int tone_2)
{
	int integer, frac, scale;
	u32 raw = 0, reg;
	int ret;

	if (chan->channel > AXI_DAC_CHAN_CNTRL_MAX)
		return -EINVAL;

	ret = iio_str_to_fixpoint(buf, 100000, &integer, &frac);
	if (ret)
		return ret;

	scale = integer * MEGA + frac;
	if (scale <= -2 * (int)MEGA || scale >= 2 * (int)MEGA)
		return -EINVAL;

	/*  format is 1.1.14 (sign, integer and fractional bits) */
	if (scale < 0) {
		raw = FIELD_PREP(AXI_DAC_CHAN_CNTRL_3_SCALE_SIGN, 1);
		scale *= -1;
	}

	raw |= div_u64((u64)scale * AXI_DAC_CHAN_CNTRL_3_SCALE_INT, MEGA);

	if (tone_2)
		reg = AXI_DAC_CHAN_CNTRL_3_REG(chan->channel);
	else
		reg = AXI_DAC_CHAN_CNTRL_1_REG(chan->channel);

	guard(mutex)(&st->lock);
	ret = regmap_write(st->regmap, reg, raw);
	if (ret)
		return ret;

	/* synchronize channels */
	ret = regmap_set_bits(st->regmap, AXI_DAC_CNTRL_1_REG,
			      AXI_DAC_CNTRL_1_SYNC);
	if (ret)
		return ret;

	return len;
}

static int axi_dac_phase_set(struct axi_dac_state *st,
			     const struct iio_chan_spec *chan,
			     const char *buf, size_t len, unsigned int tone_2)
{
	int integer, frac, phase;
	u32 raw, reg;
	int ret;

	if (chan->channel > AXI_DAC_CHAN_CNTRL_MAX)
		return -EINVAL;

	ret = iio_str_to_fixpoint(buf, 100000, &integer, &frac);
	if (ret)
		return ret;

	phase = integer * MEGA + frac;
	if (phase < 0 || phase > AXI_DAC_2_PI_MEGA)
		return -EINVAL;

	raw = DIV_ROUND_CLOSEST_ULL((u64)phase * U16_MAX, AXI_DAC_2_PI_MEGA);

	if (tone_2)
		reg = AXI_DAC_CHAN_CNTRL_4_REG(chan->channel);
	else
		reg = AXI_DAC_CHAN_CNTRL_2_REG(chan->channel);

	guard(mutex)(&st->lock);
	ret = regmap_update_bits(st->regmap, reg, AXI_DAC_CHAN_CNTRL_2_PHASE,
				 FIELD_PREP(AXI_DAC_CHAN_CNTRL_2_PHASE, raw));
	if (ret)
		return ret;

	/* synchronize channels */
	ret = regmap_set_bits(st->regmap, AXI_DAC_CNTRL_1_REG,
			      AXI_DAC_CNTRL_1_SYNC);
	if (ret)
		return ret;

	return len;
}

static int axi_dac_ext_info_set(struct iio_backend *back, uintptr_t private,
				const struct iio_chan_spec *chan,
				const char *buf, size_t len)
{
	struct axi_dac_state *st = iio_backend_get_priv(back);

	switch (private) {
	case AXI_DAC_FREQ_TONE_1:
	case AXI_DAC_FREQ_TONE_2:
		return axi_dac_frequency_set(st, chan, buf, len,
					     private == AXI_DAC_FREQ_TONE_2);
	case AXI_DAC_SCALE_TONE_1:
	case AXI_DAC_SCALE_TONE_2:
		return axi_dac_scale_set(st, chan, buf, len,
					 private == AXI_DAC_SCALE_TONE_2);
	case AXI_DAC_PHASE_TONE_1:
	case AXI_DAC_PHASE_TONE_2:
		return axi_dac_phase_set(st, chan, buf, len,
					 private == AXI_DAC_PHASE_TONE_2);
	default:
		return -EOPNOTSUPP;
	}
}

static int axi_dac_ext_info_get(struct iio_backend *back, uintptr_t private,
				const struct iio_chan_spec *chan, char *buf)
{
	struct axi_dac_state *st = iio_backend_get_priv(back);

	switch (private) {
	case AXI_DAC_FREQ_TONE_1:
	case AXI_DAC_FREQ_TONE_2:
		return axi_dac_frequency_get(st, chan, buf,
					     private - AXI_DAC_FREQ_TONE_1);
	case AXI_DAC_SCALE_TONE_1:
	case AXI_DAC_SCALE_TONE_2:
		return axi_dac_scale_get(st, chan, buf,
					 private - AXI_DAC_SCALE_TONE_1);
	case AXI_DAC_PHASE_TONE_1:
	case AXI_DAC_PHASE_TONE_2:
		return axi_dac_phase_get(st, chan, buf,
					 private - AXI_DAC_PHASE_TONE_1);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct iio_chan_spec_ext_info axi_dac_ext_info[] = {
	IIO_BACKEND_EX_INFO("frequency0", IIO_SEPARATE, AXI_DAC_FREQ_TONE_1),
	IIO_BACKEND_EX_INFO("frequency1", IIO_SEPARATE, AXI_DAC_FREQ_TONE_2),
	IIO_BACKEND_EX_INFO("scale0", IIO_SEPARATE, AXI_DAC_SCALE_TONE_1),
	IIO_BACKEND_EX_INFO("scale1", IIO_SEPARATE, AXI_DAC_SCALE_TONE_2),
	IIO_BACKEND_EX_INFO("phase0", IIO_SEPARATE, AXI_DAC_PHASE_TONE_1),
	IIO_BACKEND_EX_INFO("phase1", IIO_SEPARATE, AXI_DAC_PHASE_TONE_2),
	{ }
};

static int axi_dac_extend_chan(struct iio_backend *back,
			       struct iio_chan_spec *chan)
{
	struct axi_dac_state *st = iio_backend_get_priv(back);

	if (chan->type != IIO_ALTVOLTAGE)
		return -EINVAL;
	if (st->reg_config & AXI_DAC_CONFIG_DDS_DISABLE)
		/* nothing to extend */
		return 0;

	chan->ext_info = axi_dac_ext_info;

	return 0;
}

static int axi_dac_data_source_set(struct iio_backend *back, unsigned int chan,
				   enum iio_backend_data_source data)
{
	struct axi_dac_state *st = iio_backend_get_priv(back);

	if (chan > AXI_DAC_CHAN_CNTRL_MAX)
		return -EINVAL;

	switch (data) {
	case IIO_BACKEND_INTERNAL_CONTINUOUS_WAVE:
		return regmap_update_bits(st->regmap,
					  AXI_DAC_CHAN_CNTRL_7_REG(chan),
					  AXI_DAC_CHAN_CNTRL_7_DATA_SEL,
					  AXI_DAC_DATA_INTERNAL_TONE);
	case IIO_BACKEND_EXTERNAL:
		return regmap_update_bits(st->regmap,
					  AXI_DAC_CHAN_CNTRL_7_REG(chan),
					  AXI_DAC_CHAN_CNTRL_7_DATA_SEL,
					  AXI_DAC_DATA_DMA);
	case IIO_BACKEND_INTERNAL_RAMP_16BIT:
		return regmap_update_bits(st->regmap,
					  AXI_DAC_CHAN_CNTRL_7_REG(chan),
					  AXI_DAC_CHAN_CNTRL_7_DATA_SEL,
					  AXI_DAC_DATA_INTERNAL_RAMP_16BIT);
	default:
		return -EINVAL;
	}
}

static int axi_dac_data_source_get(struct iio_backend *back, unsigned int chan,
				   enum iio_backend_data_source *data)
{
	struct axi_dac_state *st = iio_backend_get_priv(back);
	int ret;
	u32 val;

	if (chan > AXI_DAC_CHAN_CNTRL_MAX)
		return -EINVAL;

	ret = regmap_read(st->regmap, AXI_DAC_CHAN_CNTRL_7_REG(chan), &val);
	if (ret)
		return ret;

	switch (val) {
	case AXI_DAC_DATA_INTERNAL_TONE:
		*data = IIO_BACKEND_INTERNAL_CONTINUOUS_WAVE;
		return 0;
	case AXI_DAC_DATA_DMA:
		*data = IIO_BACKEND_EXTERNAL;
		return 0;
	case AXI_DAC_DATA_INTERNAL_RAMP_16BIT:
		*data = IIO_BACKEND_INTERNAL_RAMP_16BIT;
		return 0;
	default:
		return -EIO;
	}
}

static int axi_dac_set_sample_rate(struct iio_backend *back, unsigned int chan,
				   u64 sample_rate)
{
	struct axi_dac_state *st = iio_backend_get_priv(back);
	unsigned int freq;
	int ret, tone;

	if (chan > AXI_DAC_CHAN_CNTRL_MAX)
		return -EINVAL;
	if (!sample_rate)
		return -EINVAL;
	if (st->reg_config & AXI_DAC_CONFIG_DDS_DISABLE)
		/* sample_rate has no meaning if DDS is disabled */
		return 0;

	guard(mutex)(&st->lock);
	/*
	 * If dac_clk is 0 then this must be the first time we're being notified
	 * about the interface sample rate. Hence, just update our internal
	 * variable and bail... If it's not 0, then we get the current DDS
	 * frequency (for the old rate) and update the registers for the new
	 * sample rate.
	 */
	if (!st->dac_clk) {
		st->dac_clk = sample_rate;
		return 0;
	}

	for (tone = 0; tone <= AXI_DAC_FREQ_TONE_2; tone++) {
		ret = __axi_dac_frequency_get(st, chan, tone, &freq);
		if (ret)
			return ret;

		ret = __axi_dac_frequency_set(st, chan, sample_rate, tone, freq);
		if (ret)
			return ret;
	}

	st->dac_clk = sample_rate;

	return 0;
}

static int axi_dac_reg_access(struct iio_backend *back, unsigned int reg,
			      unsigned int writeval, unsigned int *readval)
{
	struct axi_dac_state *st = iio_backend_get_priv(back);

	if (readval)
		return regmap_read(st->regmap, reg, readval);

	return regmap_write(st->regmap, reg, writeval);
}

static int axi_dac_ddr_enable(struct iio_backend *back)
{
	struct axi_dac_state *st = iio_backend_get_priv(back);

	return regmap_clear_bits(st->regmap, AXI_DAC_CNTRL_2_REG,
				 AXI_DAC_CNTRL_2_SDR_DDR_N);
}

static int axi_dac_ddr_disable(struct iio_backend *back)
{
	struct axi_dac_state *st = iio_backend_get_priv(back);

	return regmap_set_bits(st->regmap, AXI_DAC_CNTRL_2_REG,
			       AXI_DAC_CNTRL_2_SDR_DDR_N);
}

static int axi_dac_wait_bus_free(struct axi_dac_state *st)
{
	u32 val;
	int ret;

	ret = regmap_read_poll_timeout(st->regmap, AXI_DAC_UI_STATUS_REG, val,
		FIELD_GET(AXI_DAC_UI_STATUS_IF_BUSY, val) == 0, 10,
		100 * KILO);
	if (ret == -ETIMEDOUT)
		dev_err(st->dev, "AXI bus timeout\n");

	return ret;
}

static int axi_dac_data_stream_enable(struct iio_backend *back)
{
	struct axi_dac_state *st = iio_backend_get_priv(back);
	int ret;

	ret = axi_dac_wait_bus_free(st);
	if (ret)
		return ret;

	return regmap_set_bits(st->regmap, AXI_DAC_CUSTOM_CTRL_REG,
			       AXI_DAC_CUSTOM_CTRL_STREAM_ENABLE);
}

static int axi_dac_data_stream_disable(struct iio_backend *back)
{
	struct axi_dac_state *st = iio_backend_get_priv(back);

	return regmap_clear_bits(st->regmap, AXI_DAC_CUSTOM_CTRL_REG,
				 AXI_DAC_CUSTOM_CTRL_STREAM_ENABLE);
}

static int axi_dac_data_transfer_addr(struct iio_backend *back, u32 address)
{
	struct axi_dac_state *st = iio_backend_get_priv(back);

	if (address > FIELD_MAX(AXI_DAC_CUSTOM_CTRL_ADDRESS))
		return -EINVAL;

	/*
	 * Sample register address, when the DAC is configured, or stream
	 * start address when the FSM is in stream state.
	 */
	return regmap_update_bits(st->regmap, AXI_DAC_CUSTOM_CTRL_REG,
				  AXI_DAC_CUSTOM_CTRL_ADDRESS,
				  FIELD_PREP(AXI_DAC_CUSTOM_CTRL_ADDRESS,
				  address));
}

static int axi_dac_data_format_set(struct iio_backend *back, unsigned int ch,
				   const struct iio_backend_data_fmt *data)
{
	struct axi_dac_state *st = iio_backend_get_priv(back);

	switch (data->type) {
	case IIO_BACKEND_DATA_UNSIGNED:
		return regmap_clear_bits(st->regmap, AXI_DAC_CNTRL_2_REG,
					 AXI_DAC_CNTRL_2_UNSIGNED_DATA);
	default:
		return -EINVAL;
	}
}

static int __axi_dac_bus_reg_write(struct iio_backend *back, u32 reg,
				 u32 val, size_t data_size)
{
	struct axi_dac_state *st = iio_backend_get_priv(back);
	int ret;
	u32 ival;

	/*
	 * Both AXI_DAC_CNTRL_2_REG and AXI_DAC_CUSTOM_WR_REG need to know
	 * the data size. So keeping data size control here only,
	 * since data size is mandatory for the current transfer.
	 * DDR state handled separately by specific backend calls,
	 * generally all raw register writes are SDR.
	 */
	if (data_size == sizeof(u16))
		ival = FIELD_PREP(AXI_DAC_CUSTOM_WR_DATA_16, val);
	else
		ival = FIELD_PREP(AXI_DAC_CUSTOM_WR_DATA_8, val);

	ret = regmap_write(st->regmap, AXI_DAC_CUSTOM_WR_REG, ival);
	if (ret)
		return ret;

	if (data_size == sizeof(u8))
		ret = regmap_set_bits(st->regmap, AXI_DAC_CNTRL_2_REG,
				      AXI_DAC_CNTRL_2_SYMB_8B);
	else
		ret = regmap_clear_bits(st->regmap, AXI_DAC_CNTRL_2_REG,
					AXI_DAC_CNTRL_2_SYMB_8B);
	if (ret)
		return ret;

	ret = regmap_update_bits(st->regmap, AXI_DAC_CUSTOM_CTRL_REG,
				 AXI_DAC_CUSTOM_CTRL_ADDRESS,
				 FIELD_PREP(AXI_DAC_CUSTOM_CTRL_ADDRESS, reg));
	if (ret)
		return ret;

	ret = regmap_update_bits(st->regmap, AXI_DAC_CUSTOM_CTRL_REG,
				 AXI_DAC_CUSTOM_CTRL_TRANSFER_DATA,
				 AXI_DAC_CUSTOM_CTRL_TRANSFER_DATA);
	if (ret)
		return ret;

	ret = axi_dac_wait_bus_free(st);
	if (ret)
		return ret;

	/* Cleaning always AXI_DAC_CUSTOM_CTRL_TRANSFER_DATA */
	return regmap_clear_bits(st->regmap, AXI_DAC_CUSTOM_CTRL_REG,
				 AXI_DAC_CUSTOM_CTRL_TRANSFER_DATA);
}

static int axi_dac_bus_reg_write(struct iio_backend *back, u32 reg,
					u32 val, size_t data_size)
{
	struct axi_dac_state *st = iio_backend_get_priv(back);

	guard(mutex)(&st->lock);
	return __axi_dac_bus_reg_write(back, reg, val, data_size);
}

static int axi_dac_bus_reg_read(struct iio_backend *back, u32 reg, u32 *val,
				size_t data_size)
{
	struct axi_dac_state *st = iio_backend_get_priv(back);
	int ret;

	guard(mutex)(&st->lock);

	/*
	 * SPI, we write with read flag, then we read just at the AXI
	 * io address space to get data read.
	 */
	ret = __axi_dac_bus_reg_write(back, AXI_DAC_RD_ADDR(reg), 0,
				      data_size);
	if (ret)
		return ret;

	ret = axi_dac_wait_bus_free(st);
	if (ret)
		return ret;

	return regmap_read(st->regmap, AXI_DAC_CUSTOM_RD_REG, val);
}

static int axi_dac_bus_set_io_mode(struct iio_backend *back,
				   enum ad3552r_io_mode mode)
{
	struct axi_dac_state *st = iio_backend_get_priv(back);
	int ret;

	if (mode > AD3552R_IO_MODE_QSPI)
		return -EINVAL;

	guard(mutex)(&st->lock);

	ret = regmap_update_bits(st->regmap, AXI_DAC_CUSTOM_CTRL_REG,
			AXI_DAC_CUSTOM_CTRL_MULTI_IO_MODE,
			FIELD_PREP(AXI_DAC_CUSTOM_CTRL_MULTI_IO_MODE, mode));
	if (ret)
		return ret;

	return axi_dac_wait_bus_free(st);
}

static void axi_dac_child_remove(void *data)
{
	platform_device_unregister(data);
}

static int axi_dac_create_platform_device(struct axi_dac_state *st,
					  struct fwnode_handle *child)
{
	struct ad3552r_hs_platform_data pdata = {
		.bus_reg_read = axi_dac_bus_reg_read,
		.bus_reg_write = axi_dac_bus_reg_write,
		.bus_set_io_mode = axi_dac_bus_set_io_mode,
		.bus_sample_data_clock_hz = st->dac_clk_rate,
	};
	struct platform_device_info pi = {
		.parent = st->dev,
		.name = fwnode_get_name(child),
		.id = PLATFORM_DEVID_AUTO,
		.fwnode = child,
		.data = &pdata,
		.size_data = sizeof(pdata),
	};
	struct platform_device *pdev;

	pdev = platform_device_register_full(&pi);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	return devm_add_action_or_reset(st->dev, axi_dac_child_remove, pdev);
}

static const struct iio_backend_ops axi_dac_generic_ops = {
	.enable = axi_dac_enable,
	.disable = axi_dac_disable,
	.request_buffer = axi_dac_request_buffer,
	.free_buffer = axi_dac_free_buffer,
	.extend_chan_spec = axi_dac_extend_chan,
	.ext_info_set = axi_dac_ext_info_set,
	.ext_info_get = axi_dac_ext_info_get,
	.data_source_set = axi_dac_data_source_set,
	.set_sample_rate = axi_dac_set_sample_rate,
	.debugfs_reg_access = iio_backend_debugfs_ptr(axi_dac_reg_access),
};

static const struct iio_backend_ops axi_ad3552r_ops = {
	.enable = axi_dac_enable,
	.disable = axi_dac_disable,
	.request_buffer = axi_dac_request_buffer,
	.free_buffer = axi_dac_free_buffer,
	.data_source_set = axi_dac_data_source_set,
	.data_source_get = axi_dac_data_source_get,
	.ddr_enable = axi_dac_ddr_enable,
	.ddr_disable = axi_dac_ddr_disable,
	.data_stream_enable = axi_dac_data_stream_enable,
	.data_stream_disable = axi_dac_data_stream_disable,
	.data_format_set = axi_dac_data_format_set,
	.data_transfer_addr = axi_dac_data_transfer_addr,
};

static const struct iio_backend_info axi_dac_generic = {
	.name = "axi-dac",
	.ops = &axi_dac_generic_ops,
};

static const struct iio_backend_info axi_ad3552r = {
	.name = "axi-ad3552r",
	.ops = &axi_ad3552r_ops,
};

static const struct regmap_config axi_dac_regmap_config = {
	.val_bits = 32,
	.reg_bits = 32,
	.reg_stride = 4,
	.max_register = 0x0800,
};

static int axi_dac_probe(struct platform_device *pdev)
{
	struct axi_dac_state *st;
	void __iomem *base;
	unsigned int ver;
	struct clk *clk;
	int ret;

	st = devm_kzalloc(&pdev->dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	st->info = device_get_match_data(&pdev->dev);
	if (!st->info)
		return -ENODEV;
	clk = devm_clk_get_enabled(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(clk)) {
		/* Backward compat., old fdt versions without clock-names. */
		clk = devm_clk_get_enabled(&pdev->dev, NULL);
		if (IS_ERR(clk))
			return dev_err_probe(&pdev->dev, PTR_ERR(clk),
					"failed to get clock\n");
	}

	if (st->info->has_dac_clk) {
		struct clk *dac_clk;

		dac_clk = devm_clk_get_enabled(&pdev->dev, "dac_clk");
		if (IS_ERR(dac_clk))
			return dev_err_probe(&pdev->dev, PTR_ERR(dac_clk),
					     "failed to get dac_clk clock\n");

		/* We only care about the streaming mode rate */
		st->dac_clk_rate = clk_get_rate(dac_clk) / 2;
	}

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	st->dev = &pdev->dev;
	st->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					   &axi_dac_regmap_config);
	if (IS_ERR(st->regmap))
		return dev_err_probe(&pdev->dev, PTR_ERR(st->regmap),
				     "failed to init register map\n");

	/*
	 * Force disable the core. Up to the frontend to enable us. And we can
	 * still read/write registers...
	 */
	ret = regmap_write(st->regmap, AXI_DAC_RSTN_REG, 0);
	if (ret)
		return ret;

	ret = regmap_read(st->regmap, ADI_AXI_REG_VERSION, &ver);
	if (ret)
		return ret;

	if (ADI_AXI_PCORE_VER_MAJOR(ver) !=
		ADI_AXI_PCORE_VER_MAJOR(st->info->version)) {
		dev_err(&pdev->dev,
			"Major version mismatch. Expected %d.%.2d.%c, Reported %d.%.2d.%c\n",
			ADI_AXI_PCORE_VER_MAJOR(st->info->version),
			ADI_AXI_PCORE_VER_MINOR(st->info->version),
			ADI_AXI_PCORE_VER_PATCH(st->info->version),
			ADI_AXI_PCORE_VER_MAJOR(ver),
			ADI_AXI_PCORE_VER_MINOR(ver),
			ADI_AXI_PCORE_VER_PATCH(ver));
		return -ENODEV;
	}

	/* Let's get the core read only configuration */
	ret = regmap_read(st->regmap, AXI_DAC_CONFIG_REG, &st->reg_config);
	if (ret)
		return ret;

	/*
	 * In some designs, setting the R1_MODE bit to 0 (which is the default
	 * value) causes all channels of the frontend to be routed to the same
	 * DMA (so they are sampled together). This is for things like
	 * Multiple-Input and Multiple-Output (MIMO). As most of the times we
	 * want independent channels let's override the core's default value and
	 * set the R1_MODE bit.
	 */
	ret = regmap_set_bits(st->regmap, AXI_DAC_CNTRL_2_REG,
			      ADI_DAC_CNTRL_2_R1_MODE);
	if (ret)
		return ret;

	mutex_init(&st->lock);

	ret = devm_iio_backend_register(&pdev->dev, st->info->backend_info, st);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register iio backend\n");

	device_for_each_child_node_scoped(&pdev->dev, child) {
		int val;

		if (!st->info->has_child_nodes)
			return dev_err_probe(&pdev->dev, -EINVAL,
					     "invalid fdt axi-dac compatible.");

		/* Processing only reg 0 node */
		ret = fwnode_property_read_u32(child, "reg", &val);
		if (ret)
			return dev_err_probe(&pdev->dev, ret,
						"invalid reg property.");
		if (val != 0)
			return dev_err_probe(&pdev->dev, -EINVAL,
						"invalid node address.");

		ret = axi_dac_create_platform_device(st, child);
		if (ret)
			return dev_err_probe(&pdev->dev, -EINVAL,
						"cannot create device.");
	}

	dev_info(&pdev->dev, "AXI DAC IP core (%d.%.2d.%c) probed\n",
		 ADI_AXI_PCORE_VER_MAJOR(ver),
		 ADI_AXI_PCORE_VER_MINOR(ver),
		 ADI_AXI_PCORE_VER_PATCH(ver));

	return 0;
}

static const struct axi_dac_info dac_generic = {
	.version = ADI_AXI_PCORE_VER(9, 1, 'b'),
	.backend_info = &axi_dac_generic,
};

static const struct axi_dac_info dac_ad3552r = {
	.version = ADI_AXI_PCORE_VER(9, 1, 'b'),
	.backend_info = &axi_ad3552r,
	.has_dac_clk = true,
	.has_child_nodes = true,
};

static const struct of_device_id axi_dac_of_match[] = {
	{ .compatible = "adi,axi-dac-9.1.b", .data = &dac_generic },
	{ .compatible = "adi,axi-ad3552r", .data = &dac_ad3552r },
	{ }
};
MODULE_DEVICE_TABLE(of, axi_dac_of_match);

static struct platform_driver axi_dac_driver = {
	.driver = {
		.name = "adi-axi-dac",
		.of_match_table = axi_dac_of_match,
	},
	.probe = axi_dac_probe,
};
module_platform_driver(axi_dac_driver);

MODULE_AUTHOR("Nuno Sa <nuno.sa@analog.com>");
MODULE_DESCRIPTION("Analog Devices Generic AXI DAC IP core driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_DMAENGINE_BUFFER");
MODULE_IMPORT_NS("IIO_BACKEND");
