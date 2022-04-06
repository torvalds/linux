// SPDX-License-Identifier: GPL-2.0-only
/*
 * AD5755, AD5755-1, AD5757, AD5735, AD5737 Digital to analog converters driver
 *
 * Copyright 2012 Analog Devices Inc.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/property.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define AD5755_NUM_CHANNELS 4

#define AD5755_ADDR(x)			((x) << 16)

#define AD5755_WRITE_REG_DATA(chan)	(chan)
#define AD5755_WRITE_REG_GAIN(chan)	(0x08 | (chan))
#define AD5755_WRITE_REG_OFFSET(chan)	(0x10 | (chan))
#define AD5755_WRITE_REG_CTRL(chan)	(0x1c | (chan))

#define AD5755_READ_REG_DATA(chan)	(chan)
#define AD5755_READ_REG_CTRL(chan)	(0x4 | (chan))
#define AD5755_READ_REG_GAIN(chan)	(0x8 | (chan))
#define AD5755_READ_REG_OFFSET(chan)	(0xc | (chan))
#define AD5755_READ_REG_CLEAR(chan)	(0x10 | (chan))
#define AD5755_READ_REG_SLEW(chan)	(0x14 | (chan))
#define AD5755_READ_REG_STATUS		0x18
#define AD5755_READ_REG_MAIN		0x19
#define AD5755_READ_REG_DC_DC		0x1a

#define AD5755_CTRL_REG_SLEW	0x0
#define AD5755_CTRL_REG_MAIN	0x1
#define AD5755_CTRL_REG_DAC	0x2
#define AD5755_CTRL_REG_DC_DC	0x3
#define AD5755_CTRL_REG_SW	0x4

#define AD5755_READ_FLAG 0x800000

#define AD5755_NOOP 0x1CE000

#define AD5755_DAC_INT_EN			BIT(8)
#define AD5755_DAC_CLR_EN			BIT(7)
#define AD5755_DAC_OUT_EN			BIT(6)
#define AD5755_DAC_INT_CURRENT_SENSE_RESISTOR	BIT(5)
#define AD5755_DAC_DC_DC_EN			BIT(4)
#define AD5755_DAC_VOLTAGE_OVERRANGE_EN		BIT(3)

#define AD5755_DC_DC_MAXV			0
#define AD5755_DC_DC_FREQ_SHIFT			2
#define AD5755_DC_DC_PHASE_SHIFT		4
#define AD5755_EXT_DC_DC_COMP_RES		BIT(6)

#define AD5755_SLEW_STEP_SIZE_SHIFT		0
#define AD5755_SLEW_RATE_SHIFT			3
#define AD5755_SLEW_ENABLE			BIT(12)

enum ad5755_mode {
	AD5755_MODE_VOLTAGE_0V_5V		= 0,
	AD5755_MODE_VOLTAGE_0V_10V		= 1,
	AD5755_MODE_VOLTAGE_PLUSMINUS_5V	= 2,
	AD5755_MODE_VOLTAGE_PLUSMINUS_10V	= 3,
	AD5755_MODE_CURRENT_4mA_20mA		= 4,
	AD5755_MODE_CURRENT_0mA_20mA		= 5,
	AD5755_MODE_CURRENT_0mA_24mA		= 6,
};

enum ad5755_dc_dc_phase {
	AD5755_DC_DC_PHASE_ALL_SAME_EDGE		= 0,
	AD5755_DC_DC_PHASE_A_B_SAME_EDGE_C_D_OPP_EDGE	= 1,
	AD5755_DC_DC_PHASE_A_C_SAME_EDGE_B_D_OPP_EDGE	= 2,
	AD5755_DC_DC_PHASE_90_DEGREE			= 3,
};

enum ad5755_dc_dc_freq {
	AD5755_DC_DC_FREQ_250kHZ = 0,
	AD5755_DC_DC_FREQ_410kHZ = 1,
	AD5755_DC_DC_FREQ_650kHZ = 2,
};

enum ad5755_dc_dc_maxv {
	AD5755_DC_DC_MAXV_23V	= 0,
	AD5755_DC_DC_MAXV_24V5	= 1,
	AD5755_DC_DC_MAXV_27V	= 2,
	AD5755_DC_DC_MAXV_29V5	= 3,
};

enum ad5755_slew_rate {
	AD5755_SLEW_RATE_64k	= 0,
	AD5755_SLEW_RATE_32k	= 1,
	AD5755_SLEW_RATE_16k	= 2,
	AD5755_SLEW_RATE_8k	= 3,
	AD5755_SLEW_RATE_4k	= 4,
	AD5755_SLEW_RATE_2k	= 5,
	AD5755_SLEW_RATE_1k	= 6,
	AD5755_SLEW_RATE_500	= 7,
	AD5755_SLEW_RATE_250	= 8,
	AD5755_SLEW_RATE_125	= 9,
	AD5755_SLEW_RATE_64	= 10,
	AD5755_SLEW_RATE_32	= 11,
	AD5755_SLEW_RATE_16	= 12,
	AD5755_SLEW_RATE_8	= 13,
	AD5755_SLEW_RATE_4	= 14,
	AD5755_SLEW_RATE_0_5	= 15,
};

enum ad5755_slew_step_size {
	AD5755_SLEW_STEP_SIZE_1 = 0,
	AD5755_SLEW_STEP_SIZE_2 = 1,
	AD5755_SLEW_STEP_SIZE_4 = 2,
	AD5755_SLEW_STEP_SIZE_8 = 3,
	AD5755_SLEW_STEP_SIZE_16 = 4,
	AD5755_SLEW_STEP_SIZE_32 = 5,
	AD5755_SLEW_STEP_SIZE_64 = 6,
	AD5755_SLEW_STEP_SIZE_128 = 7,
	AD5755_SLEW_STEP_SIZE_256 = 8,
};

/**
 * struct ad5755_platform_data - AD5755 DAC driver platform data
 * @ext_dc_dc_compenstation_resistor: Whether an external DC-DC converter
 * compensation register is used.
 * @dc_dc_phase: DC-DC converter phase.
 * @dc_dc_freq: DC-DC converter frequency.
 * @dc_dc_maxv: DC-DC maximum allowed boost voltage.
 * @dac: Per DAC instance parameters.
 * @dac.mode: The mode to be used for the DAC output.
 * @dac.ext_current_sense_resistor: Whether an external current sense resistor
 * is used.
 * @dac.enable_voltage_overrange: Whether to enable 20% voltage output overrange.
 * @dac.slew.enable: Whether to enable digital slew.
 * @dac.slew.rate: Slew rate of the digital slew.
 * @dac.slew.step_size: Slew step size of the digital slew.
 **/
struct ad5755_platform_data {
	bool ext_dc_dc_compenstation_resistor;
	enum ad5755_dc_dc_phase dc_dc_phase;
	enum ad5755_dc_dc_freq dc_dc_freq;
	enum ad5755_dc_dc_maxv dc_dc_maxv;

	struct {
		enum ad5755_mode mode;
		bool ext_current_sense_resistor;
		bool enable_voltage_overrange;
		struct {
			bool enable;
			enum ad5755_slew_rate rate;
			enum ad5755_slew_step_size step_size;
		} slew;
	} dac[4];
};

/**
 * struct ad5755_chip_info - chip specific information
 * @channel_template:	channel specification
 * @calib_shift:	shift for the calibration data registers
 * @has_voltage_out:	whether the chip has voltage outputs
 */
struct ad5755_chip_info {
	const struct iio_chan_spec channel_template;
	unsigned int calib_shift;
	bool has_voltage_out;
};

/**
 * struct ad5755_state - driver instance specific data
 * @spi:	spi device the driver is attached to
 * @chip_info:	chip model specific constants, available modes etc
 * @pwr_down:	bitmask which contains  hether a channel is powered down or not
 * @ctrl:	software shadow of the channel ctrl registers
 * @channels:	iio channel spec for the device
 * @lock:	lock to protect the data buffer during SPI ops
 * @data:	spi transfer buffers
 */
struct ad5755_state {
	struct spi_device		*spi;
	const struct ad5755_chip_info	*chip_info;
	unsigned int			pwr_down;
	unsigned int			ctrl[AD5755_NUM_CHANNELS];
	struct iio_chan_spec		channels[AD5755_NUM_CHANNELS];
	struct mutex			lock;

	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */

	union {
		__be32 d32;
		u8 d8[4];
	} data[2] ____cacheline_aligned;
};

enum ad5755_type {
	ID_AD5755,
	ID_AD5757,
	ID_AD5735,
	ID_AD5737,
};

static const int ad5755_dcdc_freq_table[][2] = {
	{ 250000, AD5755_DC_DC_FREQ_250kHZ },
	{ 410000, AD5755_DC_DC_FREQ_410kHZ },
	{ 650000, AD5755_DC_DC_FREQ_650kHZ }
};

static const int ad5755_dcdc_maxv_table[][2] = {
	{ 23000000, AD5755_DC_DC_MAXV_23V },
	{ 24500000, AD5755_DC_DC_MAXV_24V5 },
	{ 27000000, AD5755_DC_DC_MAXV_27V },
	{ 29500000, AD5755_DC_DC_MAXV_29V5 },
};

static const int ad5755_slew_rate_table[][2] = {
	{ 64000, AD5755_SLEW_RATE_64k },
	{ 32000, AD5755_SLEW_RATE_32k },
	{ 16000, AD5755_SLEW_RATE_16k },
	{ 8000, AD5755_SLEW_RATE_8k },
	{ 4000, AD5755_SLEW_RATE_4k },
	{ 2000, AD5755_SLEW_RATE_2k },
	{ 1000, AD5755_SLEW_RATE_1k },
	{ 500, AD5755_SLEW_RATE_500 },
	{ 250, AD5755_SLEW_RATE_250 },
	{ 125, AD5755_SLEW_RATE_125 },
	{ 64, AD5755_SLEW_RATE_64 },
	{ 32, AD5755_SLEW_RATE_32 },
	{ 16, AD5755_SLEW_RATE_16 },
	{ 8, AD5755_SLEW_RATE_8 },
	{ 4, AD5755_SLEW_RATE_4 },
	{ 0, AD5755_SLEW_RATE_0_5 },
};

static const int ad5755_slew_step_table[][2] = {
	{ 256, AD5755_SLEW_STEP_SIZE_256 },
	{ 128, AD5755_SLEW_STEP_SIZE_128 },
	{ 64, AD5755_SLEW_STEP_SIZE_64 },
	{ 32, AD5755_SLEW_STEP_SIZE_32 },
	{ 16, AD5755_SLEW_STEP_SIZE_16 },
	{ 4, AD5755_SLEW_STEP_SIZE_4 },
	{ 2, AD5755_SLEW_STEP_SIZE_2 },
	{ 1, AD5755_SLEW_STEP_SIZE_1 },
};

static int ad5755_write_unlocked(struct iio_dev *indio_dev,
	unsigned int reg, unsigned int val)
{
	struct ad5755_state *st = iio_priv(indio_dev);

	st->data[0].d32 = cpu_to_be32((reg << 16) | val);

	return spi_write(st->spi, &st->data[0].d8[1], 3);
}

static int ad5755_write_ctrl_unlocked(struct iio_dev *indio_dev,
	unsigned int channel, unsigned int reg, unsigned int val)
{
	return ad5755_write_unlocked(indio_dev,
		AD5755_WRITE_REG_CTRL(channel), (reg << 13) | val);
}

static int ad5755_write(struct iio_dev *indio_dev, unsigned int reg,
	unsigned int val)
{
	struct ad5755_state *st = iio_priv(indio_dev);
	int ret;

	mutex_lock(&st->lock);
	ret = ad5755_write_unlocked(indio_dev, reg, val);
	mutex_unlock(&st->lock);

	return ret;
}

static int ad5755_write_ctrl(struct iio_dev *indio_dev, unsigned int channel,
	unsigned int reg, unsigned int val)
{
	struct ad5755_state *st = iio_priv(indio_dev);
	int ret;

	mutex_lock(&st->lock);
	ret = ad5755_write_ctrl_unlocked(indio_dev, channel, reg, val);
	mutex_unlock(&st->lock);

	return ret;
}

static int ad5755_read(struct iio_dev *indio_dev, unsigned int addr)
{
	struct ad5755_state *st = iio_priv(indio_dev);
	int ret;
	struct spi_transfer t[] = {
		{
			.tx_buf = &st->data[0].d8[1],
			.len = 3,
			.cs_change = 1,
		}, {
			.tx_buf = &st->data[1].d8[1],
			.rx_buf = &st->data[1].d8[1],
			.len = 3,
		},
	};

	mutex_lock(&st->lock);

	st->data[0].d32 = cpu_to_be32(AD5755_READ_FLAG | (addr << 16));
	st->data[1].d32 = cpu_to_be32(AD5755_NOOP);

	ret = spi_sync_transfer(st->spi, t, ARRAY_SIZE(t));
	if (ret >= 0)
		ret = be32_to_cpu(st->data[1].d32) & 0xffff;

	mutex_unlock(&st->lock);

	return ret;
}

static int ad5755_update_dac_ctrl(struct iio_dev *indio_dev,
	unsigned int channel, unsigned int set, unsigned int clr)
{
	struct ad5755_state *st = iio_priv(indio_dev);
	int ret;

	st->ctrl[channel] |= set;
	st->ctrl[channel] &= ~clr;

	ret = ad5755_write_ctrl_unlocked(indio_dev, channel,
		AD5755_CTRL_REG_DAC, st->ctrl[channel]);

	return ret;
}

static int ad5755_set_channel_pwr_down(struct iio_dev *indio_dev,
	unsigned int channel, bool pwr_down)
{
	struct ad5755_state *st = iio_priv(indio_dev);
	unsigned int mask = BIT(channel);

	mutex_lock(&st->lock);

	if ((bool)(st->pwr_down & mask) == pwr_down)
		goto out_unlock;

	if (!pwr_down) {
		st->pwr_down &= ~mask;
		ad5755_update_dac_ctrl(indio_dev, channel,
			AD5755_DAC_INT_EN | AD5755_DAC_DC_DC_EN, 0);
		udelay(200);
		ad5755_update_dac_ctrl(indio_dev, channel,
			AD5755_DAC_OUT_EN, 0);
	} else {
		st->pwr_down |= mask;
		ad5755_update_dac_ctrl(indio_dev, channel,
			0, AD5755_DAC_INT_EN | AD5755_DAC_OUT_EN |
				AD5755_DAC_DC_DC_EN);
	}

out_unlock:
	mutex_unlock(&st->lock);

	return 0;
}

static const int ad5755_min_max_table[][2] = {
	[AD5755_MODE_VOLTAGE_0V_5V] = { 0, 5000 },
	[AD5755_MODE_VOLTAGE_0V_10V] = { 0, 10000 },
	[AD5755_MODE_VOLTAGE_PLUSMINUS_5V] = { -5000, 5000 },
	[AD5755_MODE_VOLTAGE_PLUSMINUS_10V] = { -10000, 10000 },
	[AD5755_MODE_CURRENT_4mA_20mA] = { 4, 20 },
	[AD5755_MODE_CURRENT_0mA_20mA] = { 0, 20 },
	[AD5755_MODE_CURRENT_0mA_24mA] = { 0, 24 },
};

static void ad5755_get_min_max(struct ad5755_state *st,
	struct iio_chan_spec const *chan, int *min, int *max)
{
	enum ad5755_mode mode = st->ctrl[chan->channel] & 7;
	*min = ad5755_min_max_table[mode][0];
	*max = ad5755_min_max_table[mode][1];
}

static inline int ad5755_get_offset(struct ad5755_state *st,
	struct iio_chan_spec const *chan)
{
	int min, max;

	ad5755_get_min_max(st, chan, &min, &max);
	return (min * (1 << chan->scan_type.realbits)) / (max - min);
}

static int ad5755_chan_reg_info(struct ad5755_state *st,
	struct iio_chan_spec const *chan, long info, bool write,
	unsigned int *reg, unsigned int *shift, unsigned int *offset)
{
	switch (info) {
	case IIO_CHAN_INFO_RAW:
		if (write)
			*reg = AD5755_WRITE_REG_DATA(chan->address);
		else
			*reg = AD5755_READ_REG_DATA(chan->address);
		*shift = chan->scan_type.shift;
		*offset = 0;
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		if (write)
			*reg = AD5755_WRITE_REG_OFFSET(chan->address);
		else
			*reg = AD5755_READ_REG_OFFSET(chan->address);
		*shift = st->chip_info->calib_shift;
		*offset = 32768;
		break;
	case IIO_CHAN_INFO_CALIBSCALE:
		if (write)
			*reg =  AD5755_WRITE_REG_GAIN(chan->address);
		else
			*reg =  AD5755_READ_REG_GAIN(chan->address);
		*shift = st->chip_info->calib_shift;
		*offset = 0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ad5755_read_raw(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, int *val, int *val2, long info)
{
	struct ad5755_state *st = iio_priv(indio_dev);
	unsigned int reg, shift, offset;
	int min, max;
	int ret;

	switch (info) {
	case IIO_CHAN_INFO_SCALE:
		ad5755_get_min_max(st, chan, &min, &max);
		*val = max - min;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_OFFSET:
		*val = ad5755_get_offset(st, chan);
		return IIO_VAL_INT;
	default:
		ret = ad5755_chan_reg_info(st, chan, info, false,
						&reg, &shift, &offset);
		if (ret)
			return ret;

		ret = ad5755_read(indio_dev, reg);
		if (ret < 0)
			return ret;

		*val = (ret - offset) >> shift;

		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static int ad5755_write_raw(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, int val, int val2, long info)
{
	struct ad5755_state *st = iio_priv(indio_dev);
	unsigned int shift, reg, offset;
	int ret;

	ret = ad5755_chan_reg_info(st, chan, info, true,
					&reg, &shift, &offset);
	if (ret)
		return ret;

	val <<= shift;
	val += offset;

	if (val < 0 || val > 0xffff)
		return -EINVAL;

	return ad5755_write(indio_dev, reg, val);
}

static ssize_t ad5755_read_powerdown(struct iio_dev *indio_dev, uintptr_t priv,
	const struct iio_chan_spec *chan, char *buf)
{
	struct ad5755_state *st = iio_priv(indio_dev);

	return sysfs_emit(buf, "%d\n",
			  (bool)(st->pwr_down & (1 << chan->channel)));
}

static ssize_t ad5755_write_powerdown(struct iio_dev *indio_dev, uintptr_t priv,
	struct iio_chan_spec const *chan, const char *buf, size_t len)
{
	bool pwr_down;
	int ret;

	ret = strtobool(buf, &pwr_down);
	if (ret)
		return ret;

	ret = ad5755_set_channel_pwr_down(indio_dev, chan->channel, pwr_down);
	return ret ? ret : len;
}

static const struct iio_info ad5755_info = {
	.read_raw = ad5755_read_raw,
	.write_raw = ad5755_write_raw,
};

static const struct iio_chan_spec_ext_info ad5755_ext_info[] = {
	{
		.name = "powerdown",
		.read = ad5755_read_powerdown,
		.write = ad5755_write_powerdown,
		.shared = IIO_SEPARATE,
	},
	{ },
};

#define AD5755_CHANNEL(_bits) {					\
	.indexed = 1,						\
	.output = 1,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
		BIT(IIO_CHAN_INFO_SCALE) |			\
		BIT(IIO_CHAN_INFO_OFFSET) |			\
		BIT(IIO_CHAN_INFO_CALIBSCALE) |			\
		BIT(IIO_CHAN_INFO_CALIBBIAS),			\
	.scan_type = {						\
		.sign = 'u',					\
		.realbits = (_bits),				\
		.storagebits = 16,				\
		.shift = 16 - (_bits),				\
	},							\
	.ext_info = ad5755_ext_info,				\
}

static const struct ad5755_chip_info ad5755_chip_info_tbl[] = {
	[ID_AD5735] = {
		.channel_template = AD5755_CHANNEL(14),
		.has_voltage_out = true,
		.calib_shift = 4,
	},
	[ID_AD5737] = {
		.channel_template = AD5755_CHANNEL(14),
		.has_voltage_out = false,
		.calib_shift = 4,
	},
	[ID_AD5755] = {
		.channel_template = AD5755_CHANNEL(16),
		.has_voltage_out = true,
		.calib_shift = 0,
	},
	[ID_AD5757] = {
		.channel_template = AD5755_CHANNEL(16),
		.has_voltage_out = false,
		.calib_shift = 0,
	},
};

static bool ad5755_is_valid_mode(struct ad5755_state *st, enum ad5755_mode mode)
{
	switch (mode) {
	case AD5755_MODE_VOLTAGE_0V_5V:
	case AD5755_MODE_VOLTAGE_0V_10V:
	case AD5755_MODE_VOLTAGE_PLUSMINUS_5V:
	case AD5755_MODE_VOLTAGE_PLUSMINUS_10V:
		return st->chip_info->has_voltage_out;
	case AD5755_MODE_CURRENT_4mA_20mA:
	case AD5755_MODE_CURRENT_0mA_20mA:
	case AD5755_MODE_CURRENT_0mA_24mA:
		return true;
	default:
		return false;
	}
}

static int ad5755_setup_pdata(struct iio_dev *indio_dev,
			      const struct ad5755_platform_data *pdata)
{
	struct ad5755_state *st = iio_priv(indio_dev);
	unsigned int val;
	unsigned int i;
	int ret;

	if (pdata->dc_dc_phase > AD5755_DC_DC_PHASE_90_DEGREE ||
		pdata->dc_dc_freq > AD5755_DC_DC_FREQ_650kHZ ||
		pdata->dc_dc_maxv > AD5755_DC_DC_MAXV_29V5)
		return -EINVAL;

	val = pdata->dc_dc_maxv << AD5755_DC_DC_MAXV;
	val |= pdata->dc_dc_freq << AD5755_DC_DC_FREQ_SHIFT;
	val |= pdata->dc_dc_phase << AD5755_DC_DC_PHASE_SHIFT;
	if (pdata->ext_dc_dc_compenstation_resistor)
		val |= AD5755_EXT_DC_DC_COMP_RES;

	ret = ad5755_write_ctrl(indio_dev, 0, AD5755_CTRL_REG_DC_DC, val);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(pdata->dac); ++i) {
		val = pdata->dac[i].slew.step_size <<
			AD5755_SLEW_STEP_SIZE_SHIFT;
		val |= pdata->dac[i].slew.rate <<
			AD5755_SLEW_RATE_SHIFT;
		if (pdata->dac[i].slew.enable)
			val |= AD5755_SLEW_ENABLE;

		ret = ad5755_write_ctrl(indio_dev, i,
					AD5755_CTRL_REG_SLEW, val);
		if (ret < 0)
			return ret;
	}

	for (i = 0; i < ARRAY_SIZE(pdata->dac); ++i) {
		if (!ad5755_is_valid_mode(st, pdata->dac[i].mode))
			return -EINVAL;

		val = 0;
		if (!pdata->dac[i].ext_current_sense_resistor)
			val |= AD5755_DAC_INT_CURRENT_SENSE_RESISTOR;
		if (pdata->dac[i].enable_voltage_overrange)
			val |= AD5755_DAC_VOLTAGE_OVERRANGE_EN;
		val |= pdata->dac[i].mode;

		ret = ad5755_update_dac_ctrl(indio_dev, i, val, 0);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static bool ad5755_is_voltage_mode(enum ad5755_mode mode)
{
	switch (mode) {
	case AD5755_MODE_VOLTAGE_0V_5V:
	case AD5755_MODE_VOLTAGE_0V_10V:
	case AD5755_MODE_VOLTAGE_PLUSMINUS_5V:
	case AD5755_MODE_VOLTAGE_PLUSMINUS_10V:
		return true;
	default:
		return false;
	}
}

static int ad5755_init_channels(struct iio_dev *indio_dev,
				const struct ad5755_platform_data *pdata)
{
	struct ad5755_state *st = iio_priv(indio_dev);
	struct iio_chan_spec *channels = st->channels;
	unsigned int i;

	for (i = 0; i < AD5755_NUM_CHANNELS; ++i) {
		channels[i] = st->chip_info->channel_template;
		channels[i].channel = i;
		channels[i].address = i;
		if (pdata && ad5755_is_voltage_mode(pdata->dac[i].mode))
			channels[i].type = IIO_VOLTAGE;
		else
			channels[i].type = IIO_CURRENT;
	}

	indio_dev->channels = channels;

	return 0;
}

#define AD5755_DEFAULT_DAC_PDATA { \
		.mode = AD5755_MODE_CURRENT_4mA_20mA, \
		.ext_current_sense_resistor = true, \
		.enable_voltage_overrange = false, \
		.slew = { \
			.enable = false, \
			.rate = AD5755_SLEW_RATE_64k, \
			.step_size = AD5755_SLEW_STEP_SIZE_1, \
		}, \
	}

static const struct ad5755_platform_data ad5755_default_pdata = {
	.ext_dc_dc_compenstation_resistor = false,
	.dc_dc_phase = AD5755_DC_DC_PHASE_ALL_SAME_EDGE,
	.dc_dc_freq = AD5755_DC_DC_FREQ_410kHZ,
	.dc_dc_maxv = AD5755_DC_DC_MAXV_23V,
	.dac = {
		[0] = AD5755_DEFAULT_DAC_PDATA,
		[1] = AD5755_DEFAULT_DAC_PDATA,
		[2] = AD5755_DEFAULT_DAC_PDATA,
		[3] = AD5755_DEFAULT_DAC_PDATA,
	},
};

static struct ad5755_platform_data *ad5755_parse_fw(struct device *dev)
{
	struct fwnode_handle *pp;
	struct ad5755_platform_data *pdata;
	unsigned int tmp;
	unsigned int tmparray[3];
	int devnr, i;

	if (!dev_fwnode(dev))
		return NULL;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	pdata->ext_dc_dc_compenstation_resistor =
	    device_property_read_bool(dev, "adi,ext-dc-dc-compenstation-resistor");

	pdata->dc_dc_phase = AD5755_DC_DC_PHASE_ALL_SAME_EDGE;
	device_property_read_u32(dev, "adi,dc-dc-phase", &pdata->dc_dc_phase);

	pdata->dc_dc_freq = AD5755_DC_DC_FREQ_410kHZ;
	if (!device_property_read_u32(dev, "adi,dc-dc-freq-hz", &tmp)) {
		for (i = 0; i < ARRAY_SIZE(ad5755_dcdc_freq_table); i++) {
			if (tmp == ad5755_dcdc_freq_table[i][0]) {
				pdata->dc_dc_freq = ad5755_dcdc_freq_table[i][1];
				break;
			}
		}

		if (i == ARRAY_SIZE(ad5755_dcdc_freq_table))
			dev_err(dev,
				"adi,dc-dc-freq out of range selecting 410kHz\n");
	}

	pdata->dc_dc_maxv = AD5755_DC_DC_MAXV_23V;
	if (!device_property_read_u32(dev, "adi,dc-dc-max-microvolt", &tmp)) {
		for (i = 0; i < ARRAY_SIZE(ad5755_dcdc_maxv_table); i++) {
			if (tmp == ad5755_dcdc_maxv_table[i][0]) {
				pdata->dc_dc_maxv = ad5755_dcdc_maxv_table[i][1];
				break;
			}
		}
		if (i == ARRAY_SIZE(ad5755_dcdc_maxv_table))
				dev_err(dev,
					"adi,dc-dc-maxv out of range selecting 23V\n");
	}

	devnr = 0;
	device_for_each_child_node(dev, pp) {
		if (devnr >= AD5755_NUM_CHANNELS) {
			dev_err(dev,
				"There are too many channels defined in DT\n");
			goto error_out;
		}

		pdata->dac[devnr].mode = AD5755_MODE_CURRENT_4mA_20mA;
		fwnode_property_read_u32(pp, "adi,mode", &pdata->dac[devnr].mode);

		pdata->dac[devnr].ext_current_sense_resistor =
		    fwnode_property_read_bool(pp, "adi,ext-current-sense-resistor");

		pdata->dac[devnr].enable_voltage_overrange =
		    fwnode_property_read_bool(pp, "adi,enable-voltage-overrange");

		if (!fwnode_property_read_u32_array(pp, "adi,slew", tmparray, 3)) {
			pdata->dac[devnr].slew.enable = tmparray[0];

			pdata->dac[devnr].slew.rate = AD5755_SLEW_RATE_64k;
			for (i = 0; i < ARRAY_SIZE(ad5755_slew_rate_table); i++) {
				if (tmparray[1] == ad5755_slew_rate_table[i][0]) {
					pdata->dac[devnr].slew.rate =
						ad5755_slew_rate_table[i][1];
					break;
				}
			}
			if (i == ARRAY_SIZE(ad5755_slew_rate_table))
				dev_err(dev,
					"channel %d slew rate out of range selecting 64kHz\n",
					devnr);

			pdata->dac[devnr].slew.step_size = AD5755_SLEW_STEP_SIZE_1;
			for (i = 0; i < ARRAY_SIZE(ad5755_slew_step_table); i++) {
				if (tmparray[2] == ad5755_slew_step_table[i][0]) {
					pdata->dac[devnr].slew.step_size =
						ad5755_slew_step_table[i][1];
					break;
				}
			}
			if (i == ARRAY_SIZE(ad5755_slew_step_table))
				dev_err(dev,
					"channel %d slew step size out of range selecting 1 LSB\n",
					devnr);
		} else {
			pdata->dac[devnr].slew.enable = false;
			pdata->dac[devnr].slew.rate = AD5755_SLEW_RATE_64k;
			pdata->dac[devnr].slew.step_size =
			    AD5755_SLEW_STEP_SIZE_1;
		}
		devnr++;
	}

	return pdata;

 error_out:
	devm_kfree(dev, pdata);
	return NULL;
}

static int ad5755_probe(struct spi_device *spi)
{
	enum ad5755_type type = spi_get_device_id(spi)->driver_data;
	const struct ad5755_platform_data *pdata;
	struct iio_dev *indio_dev;
	struct ad5755_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (indio_dev == NULL) {
		dev_err(&spi->dev, "Failed to allocate iio device\n");
		return  -ENOMEM;
	}

	st = iio_priv(indio_dev);
	spi_set_drvdata(spi, indio_dev);

	st->chip_info = &ad5755_chip_info_tbl[type];
	st->spi = spi;
	st->pwr_down = 0xf;

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->info = &ad5755_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->num_channels = AD5755_NUM_CHANNELS;

	mutex_init(&st->lock);


	pdata = ad5755_parse_fw(&spi->dev);
	if (!pdata) {
		dev_warn(&spi->dev, "no firmware provided parameters? using default\n");
		pdata = &ad5755_default_pdata;
	}

	ret = ad5755_init_channels(indio_dev, pdata);
	if (ret)
		return ret;

	ret = ad5755_setup_pdata(indio_dev, pdata);
	if (ret)
		return ret;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct spi_device_id ad5755_id[] = {
	{ "ad5755", ID_AD5755 },
	{ "ad5755-1", ID_AD5755 },
	{ "ad5757", ID_AD5757 },
	{ "ad5735", ID_AD5735 },
	{ "ad5737", ID_AD5737 },
	{}
};
MODULE_DEVICE_TABLE(spi, ad5755_id);

static const struct of_device_id ad5755_of_match[] = {
	{ .compatible = "adi,ad5755" },
	{ .compatible = "adi,ad5755-1" },
	{ .compatible = "adi,ad5757" },
	{ .compatible = "adi,ad5735" },
	{ .compatible = "adi,ad5737" },
	{ }
};
MODULE_DEVICE_TABLE(of, ad5755_of_match);

static struct spi_driver ad5755_driver = {
	.driver = {
		.name = "ad5755",
	},
	.probe = ad5755_probe,
	.id_table = ad5755_id,
};
module_spi_driver(ad5755_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Analog Devices AD5755/55-1/57/35/37 DAC");
MODULE_LICENSE("GPL v2");
