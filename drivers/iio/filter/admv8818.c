// SPDX-License-Identifier: GPL-2.0-only
/*
 * ADMV8818 driver
 *
 * Copyright 2021 Analog Devices Inc.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/units.h>

/* ADMV8818 Register Map */
#define ADMV8818_REG_SPI_CONFIG_A		0x0
#define ADMV8818_REG_SPI_CONFIG_B		0x1
#define ADMV8818_REG_CHIPTYPE			0x3
#define ADMV8818_REG_PRODUCT_ID_L		0x4
#define ADMV8818_REG_PRODUCT_ID_H		0x5
#define ADMV8818_REG_FAST_LATCH_POINTER		0x10
#define ADMV8818_REG_FAST_LATCH_STOP		0x11
#define ADMV8818_REG_FAST_LATCH_START		0x12
#define ADMV8818_REG_FAST_LATCH_DIRECTION	0x13
#define ADMV8818_REG_FAST_LATCH_STATE		0x14
#define ADMV8818_REG_WR0_SW			0x20
#define ADMV8818_REG_WR0_FILTER			0x21
#define ADMV8818_REG_WR1_SW			0x22
#define ADMV8818_REG_WR1_FILTER			0x23
#define ADMV8818_REG_WR2_SW			0x24
#define ADMV8818_REG_WR2_FILTER			0x25
#define ADMV8818_REG_WR3_SW			0x26
#define ADMV8818_REG_WR3_FILTER			0x27
#define ADMV8818_REG_WR4_SW			0x28
#define ADMV8818_REG_WR4_FILTER			0x29
#define ADMV8818_REG_LUT0_SW			0x100
#define ADMV8818_REG_LUT0_FILTER		0x101
#define ADMV8818_REG_LUT127_SW			0x1FE
#define ADMV8818_REG_LUT127_FILTER		0x1FF

/* ADMV8818_REG_SPI_CONFIG_A Map */
#define ADMV8818_SOFTRESET_N_MSK		BIT(7)
#define ADMV8818_LSB_FIRST_N_MSK		BIT(6)
#define ADMV8818_ENDIAN_N_MSK			BIT(5)
#define ADMV8818_SDOACTIVE_N_MSK		BIT(4)
#define ADMV8818_SDOACTIVE_MSK			BIT(3)
#define ADMV8818_ENDIAN_MSK			BIT(2)
#define ADMV8818_LSBFIRST_MSK			BIT(1)
#define ADMV8818_SOFTRESET_MSK			BIT(0)

/* ADMV8818_REG_SPI_CONFIG_B Map */
#define ADMV8818_SINGLE_INSTRUCTION_MSK		BIT(7)
#define ADMV8818_CSB_STALL_MSK			BIT(6)
#define ADMV8818_MASTER_SLAVE_RB_MSK		BIT(5)
#define ADMV8818_MASTER_SLAVE_TRANSFER_MSK	BIT(0)

/* ADMV8818_REG_WR0_SW Map */
#define ADMV8818_SW_IN_SET_WR0_MSK		BIT(7)
#define ADMV8818_SW_OUT_SET_WR0_MSK		BIT(6)
#define ADMV8818_SW_IN_WR0_MSK			GENMASK(5, 3)
#define ADMV8818_SW_OUT_WR0_MSK			GENMASK(2, 0)

/* ADMV8818_REG_WR0_FILTER Map */
#define ADMV8818_HPF_WR0_MSK			GENMASK(7, 4)
#define ADMV8818_LPF_WR0_MSK			GENMASK(3, 0)

#define ADMV8818_BAND_BYPASS       0
#define ADMV8818_BAND_MIN          1
#define ADMV8818_BAND_MAX          4
#define ADMV8818_BAND_CORNER_LOW   0
#define ADMV8818_BAND_CORNER_HIGH  1

#define ADMV8818_STATE_MIN   0
#define ADMV8818_STATE_MAX   15
#define ADMV8818_NUM_STATES  16

enum {
	ADMV8818_BW_FREQ,
	ADMV8818_CENTER_FREQ
};

enum {
	ADMV8818_AUTO_MODE,
	ADMV8818_MANUAL_MODE,
	ADMV8818_BYPASS_MODE,
};

struct admv8818_state {
	struct spi_device	*spi;
	struct regmap		*regmap;
	struct clk		*clkin;
	struct notifier_block	nb;
	/* Protect against concurrent accesses to the device and data content*/
	struct mutex		lock;
	unsigned int		filter_mode;
	u64			cf_hz;
	u64			lpf_margin_hz;
	u64			hpf_margin_hz;
};

static const unsigned long long freq_range_hpf[5][2] = {
	{0ULL, 0ULL}, /* bypass */
	{1750000000ULL, 3550000000ULL},
	{3400000000ULL, 7250000000ULL},
	{6600000000, 12000000000},
	{12500000000, 19900000000}
};

static const unsigned long long freq_range_lpf[5][2] = {
	{U64_MAX, U64_MAX}, /* bypass */
	{2050000000ULL, 3850000000ULL},
	{3350000000ULL, 7250000000ULL},
	{7000000000, 13000000000},
	{12550000000, 18850000000}
};

static const struct regmap_config admv8818_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.read_flag_mask = 0x80,
	.max_register = 0x1FF,
};

static const char * const admv8818_modes[] = {
	[0] = "auto",
	[1] = "manual",
	[2] = "bypass"
};

static int __admv8818_hpf_select(struct admv8818_state *st, u64 freq)
{
	int band, state, ret;
	unsigned int hpf_state = ADMV8818_STATE_MIN, hpf_band = ADMV8818_BAND_BYPASS;
	u64 freq_error, min_freq_error, freq_corner, freq_step;

	if (freq < freq_range_hpf[ADMV8818_BAND_MIN][ADMV8818_BAND_CORNER_LOW])
		goto hpf_write;

	if (freq >= freq_range_hpf[ADMV8818_BAND_MAX][ADMV8818_BAND_CORNER_HIGH]) {
		hpf_state = ADMV8818_STATE_MAX;
		hpf_band = ADMV8818_BAND_MAX;
		goto hpf_write;
	}

	/* Close HPF frequency gap between 12 and 12.5 GHz */
	if (freq >= 12000ULL * HZ_PER_MHZ && freq < 12500ULL * HZ_PER_MHZ) {
		hpf_state = ADMV8818_STATE_MAX;
		hpf_band = 3;
		goto hpf_write;
	}

	min_freq_error = U64_MAX;
	for (band = ADMV8818_BAND_MIN; band <= ADMV8818_BAND_MAX; band++) {
		/*
		 * This (and therefore all other ranges) have a corner
		 * frequency higher than the target frequency.
		 */
		if (freq_range_hpf[band][ADMV8818_BAND_CORNER_LOW] > freq)
			break;

		freq_step = freq_range_hpf[band][ADMV8818_BAND_CORNER_HIGH] -
			    freq_range_hpf[band][ADMV8818_BAND_CORNER_LOW];
		freq_step = div_u64(freq_step, ADMV8818_NUM_STATES - 1);

		for (state = ADMV8818_STATE_MIN; state <= ADMV8818_STATE_MAX; state++) {
			freq_corner = freq_range_hpf[band][ADMV8818_BAND_CORNER_LOW] +
				      freq_step * state;

			/*
			 * This (and therefore all other states) have a corner
			 * frequency higher than the target frequency.
			 */
			if (freq_corner > freq)
				break;

			freq_error = freq - freq_corner;
			if (freq_error < min_freq_error) {
				min_freq_error = freq_error;
				hpf_state = state;
				hpf_band = band;
			}
		}
	}

hpf_write:
	ret = regmap_update_bits(st->regmap, ADMV8818_REG_WR0_SW,
				 ADMV8818_SW_IN_SET_WR0_MSK |
				 ADMV8818_SW_IN_WR0_MSK,
				 FIELD_PREP(ADMV8818_SW_IN_SET_WR0_MSK, 1) |
				 FIELD_PREP(ADMV8818_SW_IN_WR0_MSK, hpf_band));
	if (ret)
		return ret;

	return regmap_update_bits(st->regmap, ADMV8818_REG_WR0_FILTER,
				  ADMV8818_HPF_WR0_MSK,
				  FIELD_PREP(ADMV8818_HPF_WR0_MSK, hpf_state));
}

static int admv8818_hpf_select(struct admv8818_state *st, u64 freq)
{
	int ret;

	mutex_lock(&st->lock);
	ret = __admv8818_hpf_select(st, freq);
	mutex_unlock(&st->lock);

	return ret;
}

static int __admv8818_lpf_select(struct admv8818_state *st, u64 freq)
{
	int band, state, ret;
	unsigned int lpf_state = ADMV8818_STATE_MIN, lpf_band = ADMV8818_BAND_BYPASS;
	u64 freq_error, min_freq_error, freq_corner, freq_step;

	if (freq > freq_range_lpf[ADMV8818_BAND_MAX][ADMV8818_BAND_CORNER_HIGH])
		goto lpf_write;

	if (freq < freq_range_lpf[ADMV8818_BAND_MIN][ADMV8818_BAND_CORNER_LOW]) {
		lpf_state = ADMV8818_STATE_MIN;
		lpf_band = ADMV8818_BAND_MIN;
		goto lpf_write;
	}

	min_freq_error = U64_MAX;
	for (band = ADMV8818_BAND_MAX; band >= ADMV8818_BAND_MIN; --band) {
		/*
		 * At this point the highest corner frequency of
		 * all remaining ranges is below the target.
		 * LPF corner should be >= the target.
		 */
		if (freq > freq_range_lpf[band][ADMV8818_BAND_CORNER_HIGH])
			break;

		freq_step = freq_range_lpf[band][ADMV8818_BAND_CORNER_HIGH] -
			    freq_range_lpf[band][ADMV8818_BAND_CORNER_LOW];
		freq_step = div_u64(freq_step, ADMV8818_NUM_STATES - 1);

		for (state = ADMV8818_STATE_MAX; state >= ADMV8818_STATE_MIN; --state) {

			freq_corner = freq_range_lpf[band][ADMV8818_BAND_CORNER_LOW] +
				      state * freq_step;

			/*
			 * At this point all other states in range will
			 * place the corner frequency below the target
			 * LPF corner should >= the target.
			 */
			if (freq > freq_corner)
				break;

			freq_error = freq_corner - freq;
			if (freq_error < min_freq_error) {
				min_freq_error = freq_error;
				lpf_state = state;
				lpf_band = band;
			}
		}
	}

lpf_write:
	ret = regmap_update_bits(st->regmap, ADMV8818_REG_WR0_SW,
				 ADMV8818_SW_OUT_SET_WR0_MSK |
				 ADMV8818_SW_OUT_WR0_MSK,
				 FIELD_PREP(ADMV8818_SW_OUT_SET_WR0_MSK, 1) |
				 FIELD_PREP(ADMV8818_SW_OUT_WR0_MSK, lpf_band));
	if (ret)
		return ret;

	return regmap_update_bits(st->regmap, ADMV8818_REG_WR0_FILTER,
				  ADMV8818_LPF_WR0_MSK,
				  FIELD_PREP(ADMV8818_LPF_WR0_MSK, lpf_state));
}

static int admv8818_lpf_select(struct admv8818_state *st, u64 freq)
{
	int ret;

	mutex_lock(&st->lock);
	ret = __admv8818_lpf_select(st, freq);
	mutex_unlock(&st->lock);

	return ret;
}

static int admv8818_rfin_band_select(struct admv8818_state *st)
{
	int ret;
	u64 hpf_corner_target, lpf_corner_target;

	st->cf_hz = clk_get_rate(st->clkin);

	/* Check for underflow */
	if (st->cf_hz > st->hpf_margin_hz)
		hpf_corner_target = st->cf_hz - st->hpf_margin_hz;
	else
		hpf_corner_target = 0;

	/* Check for overflow */
	lpf_corner_target = st->cf_hz + st->lpf_margin_hz;
	if (lpf_corner_target < st->cf_hz)
		lpf_corner_target = U64_MAX;

	mutex_lock(&st->lock);

	ret = __admv8818_hpf_select(st, hpf_corner_target);
	if (ret)
		goto exit;

	ret = __admv8818_lpf_select(st, lpf_corner_target);
exit:
	mutex_unlock(&st->lock);
	return ret;
}

static int __admv8818_read_hpf_freq(struct admv8818_state *st, u64 *hpf_freq)
{
	unsigned int data, hpf_band, hpf_state;
	int ret;

	ret = regmap_read(st->regmap, ADMV8818_REG_WR0_SW, &data);
	if (ret)
		return ret;

	hpf_band = FIELD_GET(ADMV8818_SW_IN_WR0_MSK, data);
	if (!hpf_band || hpf_band > 4) {
		*hpf_freq = 0;
		return ret;
	}

	ret = regmap_read(st->regmap, ADMV8818_REG_WR0_FILTER, &data);
	if (ret)
		return ret;

	hpf_state = FIELD_GET(ADMV8818_HPF_WR0_MSK, data);

	*hpf_freq = freq_range_hpf[hpf_band][ADMV8818_BAND_CORNER_HIGH] -
		    freq_range_hpf[hpf_band][ADMV8818_BAND_CORNER_LOW];
	*hpf_freq = div_u64(*hpf_freq, ADMV8818_NUM_STATES - 1);
	*hpf_freq = freq_range_hpf[hpf_band][ADMV8818_BAND_CORNER_LOW] +
		    (*hpf_freq * hpf_state);

	return ret;
}

static int admv8818_read_hpf_freq(struct admv8818_state *st, u64 *hpf_freq)
{
	int ret;

	mutex_lock(&st->lock);
	ret = __admv8818_read_hpf_freq(st, hpf_freq);
	mutex_unlock(&st->lock);

	return ret;
}

static int __admv8818_read_lpf_freq(struct admv8818_state *st, u64 *lpf_freq)
{
	unsigned int data, lpf_band, lpf_state;
	int ret;

	ret = regmap_read(st->regmap, ADMV8818_REG_WR0_SW, &data);
	if (ret)
		return ret;

	lpf_band = FIELD_GET(ADMV8818_SW_OUT_WR0_MSK, data);
	if (!lpf_band || lpf_band > 4) {
		*lpf_freq = 0;
		return ret;
	}

	ret = regmap_read(st->regmap, ADMV8818_REG_WR0_FILTER, &data);
	if (ret)
		return ret;

	lpf_state = FIELD_GET(ADMV8818_LPF_WR0_MSK, data);

	*lpf_freq = freq_range_lpf[lpf_band][ADMV8818_BAND_CORNER_HIGH] -
		    freq_range_lpf[lpf_band][ADMV8818_BAND_CORNER_LOW];
	*lpf_freq = div_u64(*lpf_freq, ADMV8818_NUM_STATES - 1);
	*lpf_freq = freq_range_lpf[lpf_band][ADMV8818_BAND_CORNER_LOW] +
		    (*lpf_freq * lpf_state);

	return ret;
}

static int admv8818_read_lpf_freq(struct admv8818_state *st, u64 *lpf_freq)
{
	int ret;

	mutex_lock(&st->lock);
	ret = __admv8818_read_lpf_freq(st, lpf_freq);
	mutex_unlock(&st->lock);

	return ret;
}

static int admv8818_write_raw_get_fmt(struct iio_dev *indio_dev,
								struct iio_chan_spec const *chan,
								long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
	case IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY:
		return IIO_VAL_INT_64;
	default:
		return -EINVAL;
	}
}

static int admv8818_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long info)
{
	struct admv8818_state *st = iio_priv(indio_dev);

	u64 freq = ((u64)val2 << 32 | (u32)val);

	if ((s64)freq < 0)
		return -EINVAL;

	switch (info) {
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		return admv8818_lpf_select(st, freq);
	case IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY:
		return admv8818_hpf_select(st, freq);
	default:
		return -EINVAL;
	}
}

static int admv8818_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long info)
{
	struct admv8818_state *st = iio_priv(indio_dev);
	int ret;
	u64 freq;

	switch (info) {
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		ret = admv8818_read_lpf_freq(st, &freq);
		if (ret)
			return ret;

		*val = (u32)freq;
		*val2 = (u32)(freq >> 32);

		return IIO_VAL_INT_64;
	case IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY:
		ret = admv8818_read_hpf_freq(st, &freq);
		if (ret)
			return ret;

		*val = (u32)freq;
		*val2 = (u32)(freq >> 32);

		return IIO_VAL_INT_64;
	default:
		return -EINVAL;
	}
}

static int admv8818_reg_access(struct iio_dev *indio_dev,
			       unsigned int reg,
			       unsigned int write_val,
			       unsigned int *read_val)
{
	struct admv8818_state *st = iio_priv(indio_dev);

	if (read_val)
		return regmap_read(st->regmap, reg, read_val);
	else
		return regmap_write(st->regmap, reg, write_val);
}

static int admv8818_filter_bypass(struct admv8818_state *st)
{
	int ret;

	mutex_lock(&st->lock);

	ret = regmap_update_bits(st->regmap, ADMV8818_REG_WR0_SW,
				 ADMV8818_SW_IN_SET_WR0_MSK |
				 ADMV8818_SW_IN_WR0_MSK |
				 ADMV8818_SW_OUT_SET_WR0_MSK |
				 ADMV8818_SW_OUT_WR0_MSK,
				 FIELD_PREP(ADMV8818_SW_IN_SET_WR0_MSK, 1) |
				 FIELD_PREP(ADMV8818_SW_IN_WR0_MSK, 0) |
				 FIELD_PREP(ADMV8818_SW_OUT_SET_WR0_MSK, 1) |
				 FIELD_PREP(ADMV8818_SW_OUT_WR0_MSK, 0));
	if (ret)
		goto exit;

	ret = regmap_update_bits(st->regmap, ADMV8818_REG_WR0_FILTER,
				 ADMV8818_HPF_WR0_MSK |
				 ADMV8818_LPF_WR0_MSK,
				 FIELD_PREP(ADMV8818_HPF_WR0_MSK, 0) |
				 FIELD_PREP(ADMV8818_LPF_WR0_MSK, 0));

exit:
	mutex_unlock(&st->lock);

	return ret;
}

static int admv8818_get_mode(struct iio_dev *indio_dev,
			     const struct iio_chan_spec *chan)
{
	struct admv8818_state *st = iio_priv(indio_dev);

	return st->filter_mode;
}

static int admv8818_set_mode(struct iio_dev *indio_dev,
			     const struct iio_chan_spec *chan,
			     unsigned int mode)
{
	struct admv8818_state *st = iio_priv(indio_dev);
	int ret = 0;

	if (!st->clkin) {
		if (mode == ADMV8818_MANUAL_MODE)
			goto set_mode;

		if (mode == ADMV8818_BYPASS_MODE) {
			ret = admv8818_filter_bypass(st);
			if (ret)
				return ret;

			goto set_mode;
		}

		return -EINVAL;
	}

	switch (mode) {
	case ADMV8818_AUTO_MODE:
		if (st->filter_mode == ADMV8818_AUTO_MODE)
			return 0;

		ret = clk_prepare_enable(st->clkin);
		if (ret)
			return ret;

		ret = clk_notifier_register(st->clkin, &st->nb);
		if (ret) {
			clk_disable_unprepare(st->clkin);

			return ret;
		}

		break;
	case ADMV8818_MANUAL_MODE:
	case ADMV8818_BYPASS_MODE:
		if (st->filter_mode == ADMV8818_AUTO_MODE) {
			clk_disable_unprepare(st->clkin);

			ret = clk_notifier_unregister(st->clkin, &st->nb);
			if (ret)
				return ret;
		}

		if (mode == ADMV8818_BYPASS_MODE) {
			ret = admv8818_filter_bypass(st);
			if (ret)
				return ret;
		}

		break;
	default:
		return -EINVAL;
	}

set_mode:
	st->filter_mode = mode;

	return ret;
}

static const struct iio_info admv8818_info = {
	.write_raw = admv8818_write_raw,
	.write_raw_get_fmt = admv8818_write_raw_get_fmt,
	.read_raw = admv8818_read_raw,
	.debugfs_reg_access = &admv8818_reg_access,
};

static const struct iio_enum admv8818_mode_enum = {
	.items = admv8818_modes,
	.num_items = ARRAY_SIZE(admv8818_modes),
	.get = admv8818_get_mode,
	.set = admv8818_set_mode,
};

static const struct iio_chan_spec_ext_info admv8818_ext_info[] = {
	IIO_ENUM("filter_mode", IIO_SHARED_BY_ALL, &admv8818_mode_enum),
	IIO_ENUM_AVAILABLE("filter_mode", IIO_SHARED_BY_ALL, &admv8818_mode_enum),
	{ }
};

#define ADMV8818_CHAN(_channel) {				\
	.type = IIO_ALTVOLTAGE,					\
	.output = 1,						\
	.indexed = 1,						\
	.channel = _channel,					\
	.info_mask_separate =					\
		BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY) | \
		BIT(IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY) \
}

#define ADMV8818_CHAN_BW_CF(_channel, _admv8818_ext_info) {	\
	.type = IIO_ALTVOLTAGE,					\
	.output = 1,						\
	.indexed = 1,						\
	.channel = _channel,					\
	.ext_info = _admv8818_ext_info,				\
}

static const struct iio_chan_spec admv8818_channels[] = {
	ADMV8818_CHAN(0),
	ADMV8818_CHAN_BW_CF(0, admv8818_ext_info),
};

static int admv8818_freq_change(struct notifier_block *nb, unsigned long action, void *data)
{
	struct admv8818_state *st = container_of(nb, struct admv8818_state, nb);

	if (action == POST_RATE_CHANGE)
		return notifier_from_errno(admv8818_rfin_band_select(st));

	return NOTIFY_OK;
}

static void admv8818_clk_notifier_unreg(void *data)
{
	struct admv8818_state *st = data;

	if (st->filter_mode == 0)
		clk_notifier_unregister(st->clkin, &st->nb);
}

static void admv8818_clk_disable(void *data)
{
	struct admv8818_state *st = data;

	if (st->filter_mode == 0)
		clk_disable_unprepare(st->clkin);
}

static int admv8818_init(struct admv8818_state *st)
{
	int ret;
	struct spi_device *spi = st->spi;
	unsigned int chip_id;

	ret = regmap_write(st->regmap, ADMV8818_REG_SPI_CONFIG_A,
			   ADMV8818_SOFTRESET_N_MSK | ADMV8818_SOFTRESET_MSK);
	if (ret) {
		dev_err(&spi->dev, "ADMV8818 Soft Reset failed.\n");
		return ret;
	}

	ret = regmap_write(st->regmap, ADMV8818_REG_SPI_CONFIG_A,
			   ADMV8818_SDOACTIVE_N_MSK | ADMV8818_SDOACTIVE_MSK);
	if (ret) {
		dev_err(&spi->dev, "ADMV8818 SDO Enable failed.\n");
		return ret;
	}

	ret = regmap_read(st->regmap, ADMV8818_REG_CHIPTYPE, &chip_id);
	if (ret) {
		dev_err(&spi->dev, "ADMV8818 Chip ID read failed.\n");
		return ret;
	}

	if (chip_id != 0x1) {
		dev_err(&spi->dev, "ADMV8818 Invalid Chip ID.\n");
		return -EINVAL;
	}

	ret = regmap_update_bits(st->regmap, ADMV8818_REG_SPI_CONFIG_B,
				 ADMV8818_SINGLE_INSTRUCTION_MSK,
				 FIELD_PREP(ADMV8818_SINGLE_INSTRUCTION_MSK, 1));
	if (ret) {
		dev_err(&spi->dev, "ADMV8818 Single Instruction failed.\n");
		return ret;
	}

	if (st->clkin)
		return admv8818_rfin_band_select(st);
	else
		return 0;
}

static int admv8818_clk_setup(struct admv8818_state *st)
{
	struct spi_device *spi = st->spi;
	int ret;

	st->clkin = devm_clk_get_optional(&spi->dev, "rf_in");
	if (IS_ERR(st->clkin))
		return dev_err_probe(&spi->dev, PTR_ERR(st->clkin),
				     "failed to get the input clock\n");
	else if (!st->clkin)
		return 0;

	ret = clk_prepare_enable(st->clkin);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(&spi->dev, admv8818_clk_disable, st);
	if (ret)
		return ret;

	st->nb.notifier_call = admv8818_freq_change;
	ret = clk_notifier_register(st->clkin, &st->nb);
	if (ret < 0)
		return ret;

	return devm_add_action_or_reset(&spi->dev, admv8818_clk_notifier_unreg, st);
}

static int admv8818_read_properties(struct admv8818_state *st)
{
	struct spi_device *spi = st->spi;
	u32 mhz;
	int ret;

	ret = device_property_read_u32(&spi->dev, "adi,lpf-margin-mhz", &mhz);
	if (ret == 0)
		st->lpf_margin_hz = (u64)mhz * HZ_PER_MHZ;
	else if (ret == -EINVAL)
		st->lpf_margin_hz = 0;
	else
		return ret;


	ret = device_property_read_u32(&spi->dev, "adi,hpf-margin-mhz", &mhz);
	if (ret == 0)
		st->hpf_margin_hz = (u64)mhz * HZ_PER_MHZ;
	else if (ret == -EINVAL)
		st->hpf_margin_hz = 0;
	else if (ret < 0)
		return ret;

	return 0;
}

static int admv8818_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	struct admv8818_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	regmap = devm_regmap_init_spi(spi, &admv8818_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	st = iio_priv(indio_dev);
	st->regmap = regmap;

	indio_dev->info = &admv8818_info;
	indio_dev->name = "admv8818";
	indio_dev->channels = admv8818_channels;
	indio_dev->num_channels = ARRAY_SIZE(admv8818_channels);

	st->spi = spi;

	ret = admv8818_clk_setup(st);
	if (ret)
		return ret;

	mutex_init(&st->lock);

	ret = admv8818_read_properties(st);
	if (ret)
		return ret;

	ret = admv8818_init(st);
	if (ret)
		return ret;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct spi_device_id admv8818_id[] = {
	{ "admv8818", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, admv8818_id);

static const struct of_device_id admv8818_of_match[] = {
	{ .compatible = "adi,admv8818" },
	{ }
};
MODULE_DEVICE_TABLE(of, admv8818_of_match);

static struct spi_driver admv8818_driver = {
	.driver = {
		.name = "admv8818",
		.of_match_table = admv8818_of_match,
	},
	.probe = admv8818_probe,
	.id_table = admv8818_id,
};
module_spi_driver(admv8818_driver);

MODULE_AUTHOR("Antoniu Miclaus <antoniu.miclaus@analog.com");
MODULE_DESCRIPTION("Analog Devices ADMV8818");
MODULE_LICENSE("GPL v2");
