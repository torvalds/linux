// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices AD4851 DAS driver
 *
 * Copyright 2024 Analog Devices Inc.
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/minmax.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <linux/unaligned.h>
#include <linux/units.h>

#include <linux/iio/backend.h>
#include <linux/iio/iio.h>

#define AD4851_REG_INTERFACE_CONFIG_A	0x00
#define AD4851_REG_INTERFACE_CONFIG_B	0x01
#define AD4851_REG_PRODUCT_ID_L		0x04
#define AD4851_REG_PRODUCT_ID_H		0x05
#define AD4851_REG_DEVICE_CTRL		0x25
#define AD4851_REG_PACKET		0x26
#define AD4851_REG_OVERSAMPLE		0x27

#define AD4851_REG_CH_CONFIG_BASE	0x2A
#define AD4851_REG_CHX_SOFTSPAN(ch)	((0x12 * (ch)) + AD4851_REG_CH_CONFIG_BASE)
#define AD4851_REG_CHX_OFFSET(ch)	(AD4851_REG_CHX_SOFTSPAN(ch) + 0x01)
#define AD4851_REG_CHX_OFFSET_LSB(ch)	AD4851_REG_CHX_OFFSET(ch)
#define AD4851_REG_CHX_OFFSET_MID(ch)	(AD4851_REG_CHX_OFFSET_LSB(ch) + 0x01)
#define AD4851_REG_CHX_OFFSET_MSB(ch)	(AD4851_REG_CHX_OFFSET_MID(ch) + 0x01)
#define AD4851_REG_CHX_GAIN(ch)		(AD4851_REG_CHX_OFFSET(ch) + 0x03)
#define AD4851_REG_CHX_GAIN_LSB(ch)	AD4851_REG_CHX_GAIN(ch)
#define AD4851_REG_CHX_GAIN_MSB(ch)	(AD4851_REG_CHX_GAIN(ch) + 0x01)
#define AD4851_REG_CHX_PHASE(ch)	(AD4851_REG_CHX_GAIN(ch) + 0x02)
#define AD4851_REG_CHX_PHASE_LSB(ch)	AD4851_REG_CHX_PHASE(ch)
#define AD4851_REG_CHX_PHASE_MSB(ch)	(AD4851_REG_CHX_PHASE_LSB(ch) + 0x01)

#define AD4851_REG_TESTPAT_0(c)		(0x38 + (c) * 0x12)
#define AD4851_REG_TESTPAT_1(c)		(0x39 + (c) * 0x12)
#define AD4851_REG_TESTPAT_2(c)		(0x3A + (c) * 0x12)
#define AD4851_REG_TESTPAT_3(c)		(0x3B + (c) * 0x12)

#define AD4851_SW_RESET			(BIT(7) | BIT(0))
#define AD4851_SDO_ENABLE		BIT(4)
#define AD4851_SINGLE_INSTRUCTION	BIT(7)
#define AD4851_REFBUF			BIT(2)
#define AD4851_REFSEL			BIT(1)
#define AD4851_ECHO_CLOCK_MODE		BIT(0)

#define AD4851_PACKET_FORMAT_0		0
#define AD4851_PACKET_FORMAT_1		1
#define AD4851_PACKET_FORMAT_MASK	GENMASK(1, 0)

#define AD4851_OS_EN_MSK		BIT(7)
#define AD4851_OS_RATIO_MSK		GENMASK(3, 0)

#define AD4851_TEST_PAT			BIT(2)

#define AD4858_PACKET_SIZE_20		0
#define AD4858_PACKET_SIZE_24		1
#define AD4858_PACKET_SIZE_32		2

#define AD4857_PACKET_SIZE_16		0
#define AD4857_PACKET_SIZE_24		1

#define AD4851_TESTPAT_0_DEFAULT	0x2A
#define AD4851_TESTPAT_1_DEFAULT	0x3C
#define AD4851_TESTPAT_2_DEFAULT	0xCE
#define AD4851_TESTPAT_3_DEFAULT(c)	(0x0A + (0x10 * (c)))

#define AD4851_SOFTSPAN_0V_2V5		0
#define AD4851_SOFTSPAN_N2V5_2V5	1
#define AD4851_SOFTSPAN_0V_5V		2
#define AD4851_SOFTSPAN_N5V_5V		3
#define AD4851_SOFTSPAN_0V_6V25		4
#define AD4851_SOFTSPAN_N6V25_6V25	5
#define AD4851_SOFTSPAN_0V_10V		6
#define AD4851_SOFTSPAN_N10V_10V	7
#define AD4851_SOFTSPAN_0V_12V5		8
#define AD4851_SOFTSPAN_N12V5_12V5	9
#define AD4851_SOFTSPAN_0V_20V		10
#define AD4851_SOFTSPAN_N20V_20V	11
#define AD4851_SOFTSPAN_0V_25V		12
#define AD4851_SOFTSPAN_N25V_25V	13
#define AD4851_SOFTSPAN_0V_40V		14
#define AD4851_SOFTSPAN_N40V_40V	15

#define AD4851_MAX_LANES		8
#define AD4851_MAX_IODELAY		32

#define AD4851_T_CNVH_NS		40
#define AD4851_T_CNVH_NS_MARGIN		10

#define AD4841_MAX_SCALE_AVAIL		8

#define AD4851_MAX_CH_NR		8
#define AD4851_CH_START			0

struct ad4851_scale {
	unsigned int scale_val;
	u8 reg_val;
};

static const struct ad4851_scale ad4851_scale_table_unipolar[] = {
	{ 2500, 0x0 },
	{ 5000, 0x2 },
	{ 6250, 0x4 },
	{ 10000, 0x6 },
	{ 12500, 0x8 },
	{ 20000, 0xA },
	{ 25000, 0xC },
	{ 40000, 0xE },
};

static const struct ad4851_scale ad4851_scale_table_bipolar[] = {
	{ 5000, 0x1 },
	{ 10000, 0x3 },
	{ 12500, 0x5 },
	{ 20000, 0x7 },
	{ 25000, 0x9 },
	{ 40000, 0xB },
	{ 50000, 0xD },
	{ 80000, 0xF },
};

static const unsigned int ad4851_scale_avail_unipolar[] = {
	2500,
	5000,
	6250,
	10000,
	12500,
	20000,
	25000,
	40000,
};

static const unsigned int ad4851_scale_avail_bipolar[] = {
	5000,
	10000,
	12500,
	20000,
	25000,
	40000,
	50000,
	80000,
};

struct ad4851_chip_info {
	const char *name;
	unsigned int product_id;
	int num_scales;
	unsigned long max_sample_rate_hz;
	unsigned int resolution;
	unsigned int max_channels;
	int (*parse_channels)(struct iio_dev *indio_dev);
};

enum {
	AD4851_SCAN_TYPE_NORMAL,
	AD4851_SCAN_TYPE_RESOLUTION_BOOST,
};

struct ad4851_state {
	struct spi_device *spi;
	struct pwm_device *cnv;
	struct iio_backend *back;
	/*
	 * Synchronize access to members the of driver state, and ensure
	 * atomicity of consecutive regmap operations.
	 */
	struct mutex lock;
	struct regmap *regmap;
	const struct ad4851_chip_info *info;
	struct gpio_desc *pd_gpio;
	bool resolution_boost_enabled;
	unsigned long cnv_trigger_rate_hz;
	unsigned int osr;
	bool vrefbuf_en;
	bool vrefio_en;
	bool bipolar_ch[AD4851_MAX_CH_NR];
	unsigned int scales_unipolar[AD4841_MAX_SCALE_AVAIL][2];
	unsigned int scales_bipolar[AD4841_MAX_SCALE_AVAIL][2];
};

static int ad4851_reg_access(struct iio_dev *indio_dev,
			     unsigned int reg,
			     unsigned int writeval,
			     unsigned int *readval)
{
	struct ad4851_state *st = iio_priv(indio_dev);

	if (readval)
		return regmap_read(st->regmap, reg, readval);

	return regmap_write(st->regmap, reg, writeval);
}

static int ad4851_set_sampling_freq(struct ad4851_state *st, unsigned int freq)
{
	struct pwm_state cnv_state = {
		.duty_cycle = AD4851_T_CNVH_NS + AD4851_T_CNVH_NS_MARGIN,
		.enabled = true,
	};
	int ret;

	freq = clamp(freq, 1, st->info->max_sample_rate_hz);

	cnv_state.period = DIV_ROUND_UP_ULL(NSEC_PER_SEC, freq);

	ret = pwm_apply_might_sleep(st->cnv, &cnv_state);
	if (ret)
		return ret;

	st->cnv_trigger_rate_hz = freq;

	return 0;
}

static const int ad4851_oversampling_ratios[] = {
	1, 2, 4, 8, 16,	32, 64, 128,
	256, 512, 1024, 2048, 4096, 8192, 16384, 32768,
	65536,
};

static int ad4851_osr_to_regval(unsigned int ratio)
{
	int i;

	for (i = 1; i < ARRAY_SIZE(ad4851_oversampling_ratios); i++)
		if (ratio == ad4851_oversampling_ratios[i])
			return i - 1;

	return -EINVAL;
}

static int __ad4851_get_scale(struct iio_dev *indio_dev, int scale_tbl,
			      unsigned int *val, unsigned int *val2)
{
	const struct iio_scan_type *scan_type;
	unsigned int tmp;

	scan_type = iio_get_current_scan_type(indio_dev, &indio_dev->channels[0]);
	if (IS_ERR(scan_type))
		return PTR_ERR(scan_type);

	tmp = ((u64)scale_tbl * MICRO) >> scan_type->realbits;
	*val = tmp / MICRO;
	*val2 = tmp % MICRO;

	return 0;
}

static int ad4851_scale_fill(struct iio_dev *indio_dev)
{
	struct ad4851_state *st = iio_priv(indio_dev);
	unsigned int i, val1, val2;
	int ret;

	for (i = 0; i < ARRAY_SIZE(ad4851_scale_avail_unipolar); i++) {
		ret = __ad4851_get_scale(indio_dev,
					 ad4851_scale_avail_unipolar[i],
					 &val1, &val2);
		if (ret)
			return ret;

		st->scales_unipolar[i][0] = val1;
		st->scales_unipolar[i][1] = val2;
	}

	for (i = 0; i < ARRAY_SIZE(ad4851_scale_avail_bipolar); i++) {
		ret = __ad4851_get_scale(indio_dev,
					 ad4851_scale_avail_bipolar[i],
					 &val1, &val2);
		if (ret)
			return ret;

		st->scales_bipolar[i][0] = val1;
		st->scales_bipolar[i][1] = val2;
	}

	return 0;
}

static int ad4851_set_oversampling_ratio(struct iio_dev *indio_dev,
					 const struct iio_chan_spec *chan,
					 unsigned int osr)
{
	struct ad4851_state *st = iio_priv(indio_dev);
	int val, ret;

	guard(mutex)(&st->lock);

	if (osr == 1) {
		ret = regmap_clear_bits(st->regmap, AD4851_REG_OVERSAMPLE,
					AD4851_OS_EN_MSK);
		if (ret)
			return ret;
	} else {
		val = ad4851_osr_to_regval(osr);
		if (val < 0)
			return -EINVAL;

		ret = regmap_update_bits(st->regmap, AD4851_REG_OVERSAMPLE,
					 AD4851_OS_EN_MSK |
					 AD4851_OS_RATIO_MSK,
					 FIELD_PREP(AD4851_OS_EN_MSK, 1) |
					 FIELD_PREP(AD4851_OS_RATIO_MSK, val));
		if (ret)
			return ret;
	}

	ret = iio_backend_oversampling_ratio_set(st->back, osr);
	if (ret)
		return ret;

	switch (st->info->resolution) {
	case 20:
		switch (osr) {
		case 0:
			return -EINVAL;
		case 1:
			val = 20;
			break;
		default:
			val = 24;
			break;
		}
		break;
	case 16:
		val = 16;
		break;
	default:
		return -EINVAL;
	}

	ret = iio_backend_data_size_set(st->back, val);
	if (ret)
		return ret;

	if (osr == 1 || st->info->resolution == 16) {
		ret = regmap_clear_bits(st->regmap, AD4851_REG_PACKET,
					AD4851_PACKET_FORMAT_MASK);
		if (ret)
			return ret;

		st->resolution_boost_enabled = false;
	} else {
		ret = regmap_update_bits(st->regmap, AD4851_REG_PACKET,
					 AD4851_PACKET_FORMAT_MASK,
					 FIELD_PREP(AD4851_PACKET_FORMAT_MASK, 1));
		if (ret)
			return ret;

		st->resolution_boost_enabled = true;
	}

	if (st->osr != osr) {
		ret = ad4851_scale_fill(indio_dev);
		if (ret)
			return ret;

		st->osr = osr;
	}

	return 0;
}

static int ad4851_get_oversampling_ratio(struct ad4851_state *st, unsigned int *val)
{
	unsigned int osr;
	int ret;

	guard(mutex)(&st->lock);

	ret = regmap_read(st->regmap, AD4851_REG_OVERSAMPLE, &osr);
	if (ret)
		return ret;

	if (!FIELD_GET(AD4851_OS_EN_MSK, osr))
		*val = 1;
	else
		*val = ad4851_oversampling_ratios[FIELD_GET(AD4851_OS_RATIO_MSK, osr) + 1];

	st->osr = *val;

	return IIO_VAL_INT;
}

static void ad4851_pwm_disable(void *data)
{
	pwm_disable(data);
}

static int ad4851_setup(struct ad4851_state *st)
{
	unsigned int product_id;
	int ret;

	if (st->pd_gpio) {
		/* To initiate a global reset, bring the PD pin high twice */
		gpiod_set_value(st->pd_gpio, 1);
		fsleep(1);
		gpiod_set_value(st->pd_gpio, 0);
		fsleep(1);
		gpiod_set_value(st->pd_gpio, 1);
		fsleep(1);
		gpiod_set_value(st->pd_gpio, 0);
		fsleep(1000);
	} else {
		ret = regmap_set_bits(st->regmap, AD4851_REG_INTERFACE_CONFIG_A,
				      AD4851_SW_RESET);
		if (ret)
			return ret;
	}

	if (st->vrefbuf_en) {
		ret = regmap_set_bits(st->regmap, AD4851_REG_DEVICE_CTRL,
				      AD4851_REFBUF);
		if (ret)
			return ret;
	}

	if (st->vrefio_en) {
		ret = regmap_set_bits(st->regmap, AD4851_REG_DEVICE_CTRL,
				      AD4851_REFSEL);
		if (ret)
			return ret;
	}

	ret = regmap_write(st->regmap, AD4851_REG_INTERFACE_CONFIG_B,
			   AD4851_SINGLE_INSTRUCTION);
	if (ret)
		return ret;

	ret = regmap_write(st->regmap, AD4851_REG_INTERFACE_CONFIG_A,
			   AD4851_SDO_ENABLE);
	if (ret)
		return ret;

	ret = regmap_read(st->regmap, AD4851_REG_PRODUCT_ID_L, &product_id);
	if (ret)
		return ret;

	if (product_id != st->info->product_id)
		dev_info(&st->spi->dev, "Unknown product ID: 0x%02X\n",
			 product_id);

	ret = regmap_set_bits(st->regmap, AD4851_REG_DEVICE_CTRL,
			      AD4851_ECHO_CLOCK_MODE);
	if (ret)
		return ret;

	return regmap_write(st->regmap, AD4851_REG_PACKET, 0);
}

/*
 * Find the longest consecutive sequence of false values from field
 * and return starting index.
 */
static int ad4851_find_opt(const unsigned long *field, unsigned int start,
			   unsigned int nbits, unsigned int *val)
{
	unsigned int bit = start, end, start_cnt, cnt = 0;

	for_each_clear_bitrange_from(bit, end, field, start + nbits) {
		if (end - bit > cnt) {
			cnt = end - bit;
			start_cnt = bit - start;
		}
	}

	if (!cnt)
		return -ENOENT;

	*val = start_cnt;

	return cnt;
}

static int ad4851_calibrate(struct iio_dev *indio_dev)
{
	struct ad4851_state *st = iio_priv(indio_dev);
	unsigned int opt_delay, num_lanes, delay, i, s;
	enum iio_backend_interface_type interface_type;
	DECLARE_BITMAP(pn_status, AD4851_MAX_LANES * AD4851_MAX_IODELAY);
	bool status;
	int c, ret;

	ret = iio_backend_interface_type_get(st->back, &interface_type);
	if (ret)
		return ret;

	switch (interface_type) {
	case IIO_BACKEND_INTERFACE_SERIAL_CMOS:
		num_lanes = indio_dev->num_channels;
		break;
	case IIO_BACKEND_INTERFACE_SERIAL_LVDS:
		num_lanes = 1;
		break;
	default:
		return -EINVAL;
	}

	if (st->info->resolution == 16) {
		ret = iio_backend_data_size_set(st->back, 24);
		if (ret)
			return ret;

		ret = regmap_write(st->regmap, AD4851_REG_PACKET,
				   AD4851_TEST_PAT | AD4857_PACKET_SIZE_24);
		if (ret)
			return ret;
	} else {
		ret = iio_backend_data_size_set(st->back, 32);
		if (ret)
			return ret;

		ret = regmap_write(st->regmap, AD4851_REG_PACKET,
				   AD4851_TEST_PAT | AD4858_PACKET_SIZE_32);
		if (ret)
			return ret;
	}

	for (i = 0; i < indio_dev->num_channels; i++) {
		ret = regmap_write(st->regmap, AD4851_REG_TESTPAT_0(i),
				   AD4851_TESTPAT_0_DEFAULT);
		if (ret)
			return ret;

		ret = regmap_write(st->regmap, AD4851_REG_TESTPAT_1(i),
				   AD4851_TESTPAT_1_DEFAULT);
		if (ret)
			return ret;

		ret = regmap_write(st->regmap, AD4851_REG_TESTPAT_2(i),
				   AD4851_TESTPAT_2_DEFAULT);
		if (ret)
			return ret;

		ret = regmap_write(st->regmap, AD4851_REG_TESTPAT_3(i),
				   AD4851_TESTPAT_3_DEFAULT(i));
		if (ret)
			return ret;

		ret = iio_backend_chan_enable(st->back,
					      indio_dev->channels[i].channel);
		if (ret)
			return ret;
	}

	for (i = 0; i < num_lanes; i++) {
		for (delay = 0; delay < AD4851_MAX_IODELAY; delay++) {
			ret = iio_backend_iodelay_set(st->back, i, delay);
			if (ret)
				return ret;

			ret = iio_backend_chan_status(st->back, i, &status);
			if (ret)
				return ret;

			__assign_bit(i * AD4851_MAX_IODELAY + delay, pn_status,
				     status);
		}
	}

	for (i = 0; i < num_lanes; i++) {
		c = ad4851_find_opt(pn_status, i * AD4851_MAX_IODELAY,
				    AD4851_MAX_IODELAY, &s);
		if (c < 0)
			return c;

		opt_delay = s + c / 2;
		ret = iio_backend_iodelay_set(st->back, i, opt_delay);
		if (ret)
			return ret;
	}

	for (i = 0; i < indio_dev->num_channels; i++) {
		ret = iio_backend_chan_disable(st->back, i);
		if (ret)
			return ret;
	}

	ret = iio_backend_data_size_set(st->back, 20);
	if (ret)
		return ret;

	return regmap_write(st->regmap, AD4851_REG_PACKET, 0);
}

static int ad4851_get_calibscale(struct ad4851_state *st, int ch, int *val, int *val2)
{
	unsigned int reg_val;
	int gain;
	int ret;

	guard(mutex)(&st->lock);

	ret = regmap_read(st->regmap, AD4851_REG_CHX_GAIN_MSB(ch), &reg_val);
	if (ret)
		return ret;

	gain = reg_val << 8;

	ret = regmap_read(st->regmap, AD4851_REG_CHX_GAIN_LSB(ch), &reg_val);
	if (ret)
		return ret;

	gain |= reg_val;

	*val = gain;
	*val2 = 15;

	return IIO_VAL_FRACTIONAL_LOG2;
}

static int ad4851_set_calibscale(struct ad4851_state *st, int ch, int val,
				 int val2)
{
	u64 gain;
	u8 buf[2];
	int ret;

	if (val < 0 || val2 < 0)
		return -EINVAL;

	gain = val * MICRO + val2;
	gain = DIV_U64_ROUND_CLOSEST(gain * 32768, MICRO);

	put_unaligned_be16(gain, buf);

	guard(mutex)(&st->lock);

	ret = regmap_write(st->regmap, AD4851_REG_CHX_GAIN_MSB(ch), buf[0]);
	if (ret)
		return ret;

	return regmap_write(st->regmap, AD4851_REG_CHX_GAIN_LSB(ch), buf[1]);
}

static int ad4851_get_calibbias(struct ad4851_state *st, int ch, int *val)
{
	unsigned int lsb, mid, msb;
	int ret;

	guard(mutex)(&st->lock);
	/*
	 * After testing, the bulk_write operations doesn't work as expected
	 * here since the cs needs to be raised after each byte transaction.
	 */
	ret = regmap_read(st->regmap, AD4851_REG_CHX_OFFSET_MSB(ch), &msb);
	if (ret)
		return ret;

	ret = regmap_read(st->regmap, AD4851_REG_CHX_OFFSET_MID(ch), &mid);
	if (ret)
		return ret;

	ret = regmap_read(st->regmap, AD4851_REG_CHX_OFFSET_LSB(ch), &lsb);
	if (ret)
		return ret;

	if (st->info->resolution == 16) {
		*val = msb << 8;
		*val |= mid;
		*val = sign_extend32(*val, 15);
	} else {
		*val = msb << 12;
		*val |= mid << 4;
		*val |= lsb >> 4;
		*val = sign_extend32(*val, 19);
	}

	return IIO_VAL_INT;
}

static int ad4851_set_calibbias(struct ad4851_state *st, int ch, int val)
{
	u8 buf[3];
	int ret;

	if (val < 0)
		return -EINVAL;

	if (st->info->resolution == 16)
		put_unaligned_be16(val, buf);
	else
		put_unaligned_be24(val << 4, buf);

	guard(mutex)(&st->lock);
	/*
	 * After testing, the bulk_write operations doesn't work as expected
	 * here since the cs needs to be raised after each byte transaction.
	 */
	ret = regmap_write(st->regmap, AD4851_REG_CHX_OFFSET_LSB(ch), buf[2]);
	if (ret)
		return ret;

	ret = regmap_write(st->regmap, AD4851_REG_CHX_OFFSET_MID(ch), buf[1]);
	if (ret)
		return ret;

	return regmap_write(st->regmap, AD4851_REG_CHX_OFFSET_MSB(ch), buf[0]);
}

static int ad4851_set_scale(struct iio_dev *indio_dev,
			    const struct iio_chan_spec *chan, int val, int val2)
{
	struct ad4851_state *st = iio_priv(indio_dev);
	unsigned int scale_val[2];
	unsigned int i;
	const struct ad4851_scale *scale_table;
	size_t table_size;
	int ret;

	if (st->bipolar_ch[chan->channel]) {
		scale_table = ad4851_scale_table_bipolar;
		table_size = ARRAY_SIZE(ad4851_scale_table_bipolar);
	} else {
		scale_table = ad4851_scale_table_unipolar;
		table_size = ARRAY_SIZE(ad4851_scale_table_unipolar);
	}

	for (i = 0; i < table_size; i++) {
		ret = __ad4851_get_scale(indio_dev, scale_table[i].scale_val,
					 &scale_val[0], &scale_val[1]);
		if (ret)
			return ret;

		if (scale_val[0] != val || scale_val[1] != val2)
			continue;

		return regmap_write(st->regmap,
				    AD4851_REG_CHX_SOFTSPAN(chan->channel),
				    scale_table[i].reg_val);
	}

	return -EINVAL;
}

static int ad4851_get_scale(struct iio_dev *indio_dev,
			    const struct iio_chan_spec *chan, int *val,
			    int *val2)
{
	struct ad4851_state *st = iio_priv(indio_dev);
	const struct ad4851_scale *scale_table;
	size_t table_size;
	u32 softspan_val;
	int i, ret;

	if (st->bipolar_ch[chan->channel]) {
		scale_table = ad4851_scale_table_bipolar;
		table_size = ARRAY_SIZE(ad4851_scale_table_bipolar);
	} else {
		scale_table = ad4851_scale_table_unipolar;
		table_size = ARRAY_SIZE(ad4851_scale_table_unipolar);
	}

	ret = regmap_read(st->regmap, AD4851_REG_CHX_SOFTSPAN(chan->channel),
			  &softspan_val);
	if (ret)
		return ret;

	for (i = 0; i < table_size; i++) {
		if (softspan_val == scale_table[i].reg_val)
			break;
	}

	if (i == table_size)
		return -EIO;

	ret = __ad4851_get_scale(indio_dev, scale_table[i].scale_val, val,
				 val2);
	if (ret)
		return ret;

	return IIO_VAL_INT_PLUS_MICRO;
}

static int ad4851_read_raw(struct iio_dev *indio_dev,
			   const struct iio_chan_spec *chan,
			   int *val, int *val2, long info)
{
	struct ad4851_state *st = iio_priv(indio_dev);

	switch (info) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = st->cnv_trigger_rate_hz;
		*val2 = st->osr;
		return IIO_VAL_FRACTIONAL;
	case IIO_CHAN_INFO_CALIBSCALE:
		return ad4851_get_calibscale(st, chan->channel, val, val2);
	case IIO_CHAN_INFO_SCALE:
		return ad4851_get_scale(indio_dev, chan, val, val2);
	case IIO_CHAN_INFO_CALIBBIAS:
		return ad4851_get_calibbias(st, chan->channel, val);
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		return ad4851_get_oversampling_ratio(st, val);
	default:
		return -EINVAL;
	}
}

static int ad4851_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long info)
{
	struct ad4851_state *st = iio_priv(indio_dev);

	switch (info) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (val < 0 || val2 < 0)
			return -EINVAL;
		return ad4851_set_sampling_freq(st, val * st->osr + val2 * st->osr / MICRO);
	case IIO_CHAN_INFO_SCALE:
		return ad4851_set_scale(indio_dev, chan, val, val2);
	case IIO_CHAN_INFO_CALIBSCALE:
		return ad4851_set_calibscale(st, chan->channel, val, val2);
	case IIO_CHAN_INFO_CALIBBIAS:
		return ad4851_set_calibbias(st, chan->channel, val);
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		return ad4851_set_oversampling_ratio(indio_dev, chan, val);
	default:
		return -EINVAL;
	}
}

static int ad4851_update_scan_mode(struct iio_dev *indio_dev,
				   const unsigned long *scan_mask)
{
	struct ad4851_state *st = iio_priv(indio_dev);
	unsigned int c;
	int ret;

	for (c = 0; c < indio_dev->num_channels; c++) {
		if (test_bit(c, scan_mask))
			ret = iio_backend_chan_enable(st->back, c);
		else
			ret = iio_backend_chan_disable(st->back, c);
		if (ret)
			return ret;
	}

	return 0;
}

static int ad4851_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long mask)
{
	struct ad4851_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		if (st->bipolar_ch[chan->channel]) {
			*vals = (const int *)st->scales_bipolar;
			*type = IIO_VAL_INT_PLUS_MICRO;
			/* Values are stored in a 2D matrix */
			*length = ARRAY_SIZE(ad4851_scale_avail_bipolar) * 2;
		} else {
			*vals = (const int *)st->scales_unipolar;
			*type = IIO_VAL_INT_PLUS_MICRO;
			/* Values are stored in a 2D matrix */
			*length = ARRAY_SIZE(ad4851_scale_avail_unipolar) * 2;
		}
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*vals = ad4851_oversampling_ratios;
		*length = ARRAY_SIZE(ad4851_oversampling_ratios);
		*type = IIO_VAL_INT;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static const struct iio_scan_type ad4851_scan_type_20_u[] = {
	[AD4851_SCAN_TYPE_NORMAL] = {
		.sign = 'u',
		.realbits = 20,
		.storagebits = 32,
	},
	[AD4851_SCAN_TYPE_RESOLUTION_BOOST] = {
		.sign = 'u',
		.realbits = 24,
		.storagebits = 32,
	},
};

static const struct iio_scan_type ad4851_scan_type_20_b[] = {
	[AD4851_SCAN_TYPE_NORMAL] = {
		.sign = 's',
		.realbits = 20,
		.storagebits = 32,
	},
	[AD4851_SCAN_TYPE_RESOLUTION_BOOST] = {
		.sign = 's',
		.realbits = 24,
		.storagebits = 32,
	},
};

static int ad4851_get_current_scan_type(const struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan)
{
	struct ad4851_state *st = iio_priv(indio_dev);

	return st->resolution_boost_enabled ? AD4851_SCAN_TYPE_RESOLUTION_BOOST
					    : AD4851_SCAN_TYPE_NORMAL;
}

#define AD4851_IIO_CHANNEL							\
	.type = IIO_VOLTAGE,							\
	.info_mask_separate = BIT(IIO_CHAN_INFO_CALIBSCALE) |			\
		BIT(IIO_CHAN_INFO_CALIBBIAS) |					\
		BIT(IIO_CHAN_INFO_SCALE),					\
	.info_mask_separate_available = BIT(IIO_CHAN_INFO_SCALE),		\
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ) |		\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),				\
	.info_mask_shared_by_all_available =					\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),				\
	.indexed = 1

/*
 * In case of AD4858_IIO_CHANNEL the scan_type is handled dynamically during the
 * parse_channels function.
 */
#define AD4858_IIO_CHANNEL							\
{										\
	AD4851_IIO_CHANNEL							\
}

#define AD4857_IIO_CHANNEL							\
{										\
	AD4851_IIO_CHANNEL,							\
	.scan_type = {								\
		.sign = 'u',							\
		.realbits = 16,							\
		.storagebits = 16,						\
	},									\
}

static int ad4851_parse_channels_common(struct iio_dev *indio_dev,
					struct iio_chan_spec **chans,
					const struct iio_chan_spec ad4851_chan)
{
	struct ad4851_state *st = iio_priv(indio_dev);
	struct device *dev = &st->spi->dev;
	struct iio_chan_spec *channels, *chan_start;
	unsigned int num_channels, reg;
	unsigned int index = 0;
	int ret;

	num_channels = device_get_child_node_count(dev);
	if (num_channels > AD4851_MAX_CH_NR)
		return dev_err_probe(dev, -EINVAL, "Too many channels: %u\n",
				     num_channels);

	channels = devm_kcalloc(dev, num_channels, sizeof(*channels), GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	chan_start = channels;

	device_for_each_child_node_scoped(dev, child) {
		ret = fwnode_property_read_u32(child, "reg", &reg);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Missing channel number\n");
		if (reg >= AD4851_MAX_CH_NR)
			return dev_err_probe(dev, -EINVAL,
					     "Invalid channel number\n");
		*channels = ad4851_chan;
		channels->scan_index = index++;
		channels->channel = reg;

		if (fwnode_property_present(child, "diff-channels")) {
			channels->channel2 = reg + st->info->max_channels;
			channels->differential = 1;
		}

		st->bipolar_ch[reg] = fwnode_property_read_bool(child, "bipolar");

		if (st->bipolar_ch[reg]) {
			channels->scan_type.sign = 's';
		} else {
			ret = regmap_write(st->regmap, AD4851_REG_CHX_SOFTSPAN(reg),
					   AD4851_SOFTSPAN_0V_40V);
			if (ret)
				return ret;
		}

		channels++;
	}

	*chans = chan_start;

	return num_channels;
}

static int ad4857_parse_channels(struct iio_dev *indio_dev)
{
	struct iio_chan_spec *ad4851_channels;
	const struct iio_chan_spec ad4851_chan = AD4857_IIO_CHANNEL;
	int ret;

	ret = ad4851_parse_channels_common(indio_dev, &ad4851_channels,
					   ad4851_chan);
	if (ret < 0)
		return ret;

	indio_dev->channels = ad4851_channels;
	indio_dev->num_channels = ret;

	return 0;
}

static int ad4858_parse_channels(struct iio_dev *indio_dev)
{
	struct ad4851_state *st = iio_priv(indio_dev);
	struct device *dev = &st->spi->dev;
	struct iio_chan_spec *ad4851_channels;
	const struct iio_chan_spec ad4851_chan = AD4858_IIO_CHANNEL;
	int ret;

	ret = ad4851_parse_channels_common(indio_dev, &ad4851_channels,
					   ad4851_chan);
	if (ret < 0)
		return ret;

	device_for_each_child_node_scoped(dev, child) {
		ad4851_channels->has_ext_scan_type = 1;
		if (fwnode_property_read_bool(child, "bipolar")) {
			ad4851_channels->ext_scan_type = ad4851_scan_type_20_b;
			ad4851_channels->num_ext_scan_type = ARRAY_SIZE(ad4851_scan_type_20_b);
		} else {
			ad4851_channels->ext_scan_type = ad4851_scan_type_20_u;
			ad4851_channels->num_ext_scan_type = ARRAY_SIZE(ad4851_scan_type_20_u);
		}
		ad4851_channels++;
	}

	indio_dev->channels = ad4851_channels;
	indio_dev->num_channels = ret;

	return 0;
}

/*
 * parse_channels() function handles the rest of the channel related attributes
 * that are usually are stored in the chip info structure.
 */
static const struct ad4851_chip_info ad4851_info = {
	.name = "ad4851",
	.product_id = 0x67,
	.max_sample_rate_hz = 250 * KILO,
	.resolution = 16,
	.max_channels = AD4851_MAX_CH_NR,
	.parse_channels = ad4857_parse_channels,
};

static const struct ad4851_chip_info ad4852_info = {
	.name = "ad4852",
	.product_id = 0x66,
	.max_sample_rate_hz = 250 * KILO,
	.resolution = 20,
	.max_channels = AD4851_MAX_CH_NR,
	.parse_channels = ad4858_parse_channels,
};

static const struct ad4851_chip_info ad4853_info = {
	.name = "ad4853",
	.product_id = 0x65,
	.max_sample_rate_hz = 1 * MEGA,
	.resolution = 16,
	.max_channels = AD4851_MAX_CH_NR,
	.parse_channels = ad4857_parse_channels,
};

static const struct ad4851_chip_info ad4854_info = {
	.name = "ad4854",
	.product_id = 0x64,
	.max_sample_rate_hz = 1 * MEGA,
	.resolution = 20,
	.max_channels = AD4851_MAX_CH_NR,
	.parse_channels = ad4858_parse_channels,
};

static const struct ad4851_chip_info ad4855_info = {
	.name = "ad4855",
	.product_id = 0x63,
	.max_sample_rate_hz = 250 * KILO,
	.resolution = 16,
	.max_channels = AD4851_MAX_CH_NR,
	.parse_channels = ad4857_parse_channels,
};

static const struct ad4851_chip_info ad4856_info = {
	.name = "ad4856",
	.product_id = 0x62,
	.max_sample_rate_hz = 250 * KILO,
	.resolution = 20,
	.max_channels = AD4851_MAX_CH_NR,
	.parse_channels = ad4858_parse_channels,
};

static const struct ad4851_chip_info ad4857_info = {
	.name = "ad4857",
	.product_id = 0x61,
	.max_sample_rate_hz = 1 * MEGA,
	.resolution = 16,
	.max_channels = AD4851_MAX_CH_NR,
	.parse_channels = ad4857_parse_channels,
};

static const struct ad4851_chip_info ad4858_info = {
	.name = "ad4858",
	.product_id = 0x60,
	.max_sample_rate_hz = 1 * MEGA,
	.resolution = 20,
	.max_channels = AD4851_MAX_CH_NR,
	.parse_channels = ad4858_parse_channels,
};

static const struct ad4851_chip_info ad4858i_info = {
	.name = "ad4858i",
	.product_id = 0x6F,
	.max_sample_rate_hz = 1 * MEGA,
	.resolution = 20,
	.max_channels = AD4851_MAX_CH_NR,
	.parse_channels = ad4858_parse_channels,
};

static const struct iio_info ad4851_iio_info = {
	.debugfs_reg_access = ad4851_reg_access,
	.read_raw = ad4851_read_raw,
	.write_raw = ad4851_write_raw,
	.update_scan_mode = ad4851_update_scan_mode,
	.get_current_scan_type = ad4851_get_current_scan_type,
	.read_avail = ad4851_read_avail,
};

static const struct regmap_config regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.read_flag_mask = BIT(7),
};

static const char * const ad4851_power_supplies[] = {
	"vcc",	"vdd", "vee", "vio",
};

static int ad4851_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct device *dev = &spi->dev;
	struct ad4851_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->spi = spi;

	ret = devm_mutex_init(dev, &st->lock);
	if (ret)
		return ret;

	ret = devm_regulator_bulk_get_enable(dev,
					     ARRAY_SIZE(ad4851_power_supplies),
					     ad4851_power_supplies);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to get and enable supplies\n");

	ret = devm_regulator_get_enable_optional(dev, "vddh");
	if (ret < 0 && ret != -ENODEV)
		return dev_err_probe(dev, ret, "failed to enable vddh voltage\n");

	ret = devm_regulator_get_enable_optional(dev, "vddl");
	if (ret < 0 && ret != -ENODEV)
		return dev_err_probe(dev, ret, "failed to enable vddl voltage\n");

	ret = devm_regulator_get_enable_optional(dev, "vrefbuf");
	if (ret < 0 && ret != -ENODEV)
		return dev_err_probe(dev, ret, "failed to enable vrefbuf voltage\n");

	st->vrefbuf_en = ret != -ENODEV;

	ret = devm_regulator_get_enable_optional(dev, "vrefio");
	if (ret < 0 && ret != -ENODEV)
		return dev_err_probe(dev, ret, "failed to enable vrefio voltage\n");

	st->vrefio_en = ret != -ENODEV;

	st->pd_gpio = devm_gpiod_get_optional(dev, "pd", GPIOD_OUT_LOW);
	if (IS_ERR(st->pd_gpio))
		return dev_err_probe(dev, PTR_ERR(st->pd_gpio),
				     "Error on requesting pd GPIO\n");

	st->cnv = devm_pwm_get(dev, NULL);
	if (IS_ERR(st->cnv))
		return dev_err_probe(dev, PTR_ERR(st->cnv),
				     "Error on requesting pwm\n");

	st->info = spi_get_device_match_data(spi);
	if (!st->info)
		return -ENODEV;

	st->regmap = devm_regmap_init_spi(spi, &regmap_config);
	if (IS_ERR(st->regmap))
		return PTR_ERR(st->regmap);

	ret = ad4851_set_sampling_freq(st, HZ_PER_MHZ);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(&st->spi->dev, ad4851_pwm_disable,
				       st->cnv);
	if (ret)
		return ret;

	ret = ad4851_setup(st);
	if (ret)
		return ret;

	indio_dev->name = st->info->name;
	indio_dev->info = &ad4851_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = st->info->parse_channels(indio_dev);
	if (ret)
		return ret;

	ret = ad4851_scale_fill(indio_dev);
	if (ret)
		return ret;

	st->back = devm_iio_backend_get(dev, NULL);
	if (IS_ERR(st->back))
		return PTR_ERR(st->back);

	ret = devm_iio_backend_request_buffer(dev, st->back, indio_dev);
	if (ret)
		return ret;

	ret = devm_iio_backend_enable(dev, st->back);
	if (ret)
		return ret;

	ret = ad4851_calibrate(indio_dev);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id ad4851_of_match[] = {
	{ .compatible = "adi,ad4851", .data = &ad4851_info, },
	{ .compatible = "adi,ad4852", .data = &ad4852_info, },
	{ .compatible = "adi,ad4853", .data = &ad4853_info, },
	{ .compatible = "adi,ad4854", .data = &ad4854_info, },
	{ .compatible = "adi,ad4855", .data = &ad4855_info, },
	{ .compatible = "adi,ad4856", .data = &ad4856_info, },
	{ .compatible = "adi,ad4857", .data = &ad4857_info, },
	{ .compatible = "adi,ad4858", .data = &ad4858_info, },
	{ .compatible = "adi,ad4858i", .data = &ad4858i_info, },
	{ }
};

static const struct spi_device_id ad4851_spi_id[] = {
	{ "ad4851", (kernel_ulong_t)&ad4851_info },
	{ "ad4852", (kernel_ulong_t)&ad4852_info },
	{ "ad4853", (kernel_ulong_t)&ad4853_info },
	{ "ad4854", (kernel_ulong_t)&ad4854_info },
	{ "ad4855", (kernel_ulong_t)&ad4855_info },
	{ "ad4856", (kernel_ulong_t)&ad4856_info },
	{ "ad4857", (kernel_ulong_t)&ad4857_info },
	{ "ad4858", (kernel_ulong_t)&ad4858_info },
	{ "ad4858i", (kernel_ulong_t)&ad4858i_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad4851_spi_id);

static struct spi_driver ad4851_driver = {
	.probe = ad4851_probe,
	.driver = {
		.name = "ad4851",
		.of_match_table = ad4851_of_match,
	},
	.id_table = ad4851_spi_id,
};
module_spi_driver(ad4851_driver);

MODULE_AUTHOR("Sergiu Cuciurean <sergiu.cuciurean@analog.com>");
MODULE_AUTHOR("Dragos Bogdan <dragos.bogdan@analog.com>");
MODULE_AUTHOR("Antoniu Miclaus <antoniu.miclaus@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD4851 DAS driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_BACKEND");
