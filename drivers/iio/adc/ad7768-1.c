// SPDX-License-Identifier: GPL-2.0
/*
 * Analog Devices AD7768-1 SPI ADC driver
 *
 * Copyright 2017 Analog Devices Inc.
 */
#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/unaligned.h>
#include <linux/units.h>
#include <linux/util_macros.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#include <dt-bindings/iio/adc/adi,ad7768-1.h>

/* AD7768 registers definition */
#define AD7768_REG_CHIP_TYPE		0x3
#define AD7768_REG_PROD_ID_L		0x4
#define AD7768_REG_PROD_ID_H		0x5
#define AD7768_REG_CHIP_GRADE		0x6
#define AD7768_REG_SCRATCH_PAD		0x0A
#define AD7768_REG_VENDOR_L		0x0C
#define AD7768_REG_VENDOR_H		0x0D
#define AD7768_REG_INTERFACE_FORMAT	0x14
#define AD7768_REG_POWER_CLOCK		0x15
#define AD7768_REG_ANALOG		0x16
#define AD7768_REG_ANALOG2		0x17
#define AD7768_REG_CONVERSION		0x18
#define AD7768_REG_DIGITAL_FILTER	0x19
#define AD7768_REG_SINC3_DEC_RATE_MSB	0x1A
#define AD7768_REG_SINC3_DEC_RATE_LSB	0x1B
#define AD7768_REG_DUTY_CYCLE_RATIO	0x1C
#define AD7768_REG_SYNC_RESET		0x1D
#define AD7768_REG_GPIO_CONTROL		0x1E
#define AD7768_REG_GPIO_WRITE		0x1F
#define AD7768_REG_GPIO_READ		0x20
#define AD7768_REG_OFFSET_HI		0x21
#define AD7768_REG_OFFSET_MID		0x22
#define AD7768_REG_OFFSET_LO		0x23
#define AD7768_REG_GAIN_HI		0x24
#define AD7768_REG_GAIN_MID		0x25
#define AD7768_REG_GAIN_LO		0x26
#define AD7768_REG_SPI_DIAG_ENABLE	0x28
#define AD7768_REG_ADC_DIAG_ENABLE	0x29
#define AD7768_REG_DIG_DIAG_ENABLE	0x2A
#define AD7768_REG24_ADC_DATA		0x2C
#define AD7768_REG_MASTER_STATUS	0x2D
#define AD7768_REG_SPI_DIAG_STATUS	0x2E
#define AD7768_REG_ADC_DIAG_STATUS	0x2F
#define AD7768_REG_DIG_DIAG_STATUS	0x30
#define AD7768_REG_MCLK_COUNTER		0x31
#define AD7768_REG_COEFF_CONTROL	0x32
#define AD7768_REG24_COEFF_DATA		0x33
#define AD7768_REG_ACCESS_KEY		0x34

/* AD7768_REG_POWER_CLOCK */
#define AD7768_PWR_MCLK_DIV_MSK		GENMASK(5, 4)
#define AD7768_PWR_MCLK_DIV(x)		FIELD_PREP(AD7768_PWR_MCLK_DIV_MSK, x)
#define AD7768_PWR_PWRMODE_MSK		GENMASK(1, 0)
#define AD7768_PWR_PWRMODE(x)		FIELD_PREP(AD7768_PWR_PWRMODE_MSK, x)

/* AD7768_REG_DIGITAL_FILTER */
#define AD7768_DIG_FIL_EN_60HZ_REJ	BIT(7)
#define AD7768_DIG_FIL_FIL_MSK		GENMASK(6, 4)
#define AD7768_DIG_FIL_FIL(x)		FIELD_PREP(AD7768_DIG_FIL_FIL_MSK, x)
#define AD7768_DIG_FIL_DEC_MSK		GENMASK(2, 0)
#define AD7768_DIG_FIL_DEC_RATE(x)	FIELD_PREP(AD7768_DIG_FIL_DEC_MSK, x)

/* AD7768_REG_CONVERSION */
#define AD7768_CONV_MODE_MSK		GENMASK(2, 0)
#define AD7768_CONV_MODE(x)		FIELD_PREP(AD7768_CONV_MODE_MSK, x)

/* AD7768_REG_ANALOG2 */
#define AD7768_REG_ANALOG2_VCM_MSK	GENMASK(2, 0)
#define AD7768_REG_ANALOG2_VCM(x)	FIELD_PREP(AD7768_REG_ANALOG2_VCM_MSK, (x))

/* AD7768_REG_GPIO_CONTROL */
#define AD7768_GPIO_UNIVERSAL_EN	BIT(7)
#define AD7768_GPIO_CONTROL_MSK		GENMASK(3, 0)

/* AD7768_REG_GPIO_WRITE */
#define AD7768_GPIO_WRITE_MSK		GENMASK(3, 0)

/* AD7768_REG_GPIO_READ */
#define AD7768_GPIO_READ_MSK		GENMASK(3, 0)

#define AD7768_VCM_OFF			0x07

#define AD7768_TRIGGER_SOURCE_SYNC_IDX 0

#define AD7768_MAX_CHANNELS 1

enum ad7768_conv_mode {
	AD7768_CONTINUOUS,
	AD7768_ONE_SHOT,
	AD7768_SINGLE,
	AD7768_PERIODIC,
	AD7768_STANDBY
};

enum ad7768_pwrmode {
	AD7768_ECO_MODE = 0,
	AD7768_MED_MODE = 2,
	AD7768_FAST_MODE = 3
};

enum ad7768_mclk_div {
	AD7768_MCLK_DIV_16,
	AD7768_MCLK_DIV_8,
	AD7768_MCLK_DIV_4,
	AD7768_MCLK_DIV_2
};

enum ad7768_filter_type {
	AD7768_FILTER_SINC5,
	AD7768_FILTER_SINC3,
	AD7768_FILTER_WIDEBAND,
	AD7768_FILTER_SINC3_REJ60,
};

enum ad7768_filter_regval {
	AD7768_FILTER_REGVAL_SINC5 = 0,
	AD7768_FILTER_REGVAL_SINC5_X8 = 1,
	AD7768_FILTER_REGVAL_SINC5_X16 = 2,
	AD7768_FILTER_REGVAL_SINC3 = 3,
	AD7768_FILTER_REGVAL_WIDEBAND = 4,
	AD7768_FILTER_REGVAL_SINC3_REJ60 = 11,
};

enum ad7768_scan_type {
	AD7768_SCAN_TYPE_NORMAL,
	AD7768_SCAN_TYPE_HIGH_SPEED,
};

/* -3dB cutoff frequency multipliers (relative to ODR) for each filter type. */
static const int ad7768_filter_3db_odr_multiplier[] = {
	[AD7768_FILTER_SINC5] = 204,		/* 0.204 */
	[AD7768_FILTER_SINC3] = 262,		/* 0.2617 */
	[AD7768_FILTER_SINC3_REJ60] = 262,	/* 0.2617 */
	[AD7768_FILTER_WIDEBAND] = 433,		/* 0.433 */
};

static const int ad7768_mclk_div_rates[] = {
	16, 8, 4, 2,
};

static const int ad7768_dec_rate_values[8] = {
	8, 16, 32, 64, 128, 256, 512, 1024,
};

/* Decimation rate range for sinc3 filter */
static const int ad7768_sinc3_dec_rate_range[3] = {
	32, 32, 163840,
};

/*
 * The AD7768-1 supports three primary filter types:
 * Sinc5, Sinc3, and Wideband.
 * However, the filter register values can also encode additional parameters
 * such as decimation rates and 60Hz rejection. This utility array separates
 * the filter type from these parameters.
 */
static const int ad7768_filter_regval_to_type[] = {
	[AD7768_FILTER_REGVAL_SINC5] = AD7768_FILTER_SINC5,
	[AD7768_FILTER_REGVAL_SINC5_X8] = AD7768_FILTER_SINC5,
	[AD7768_FILTER_REGVAL_SINC5_X16] = AD7768_FILTER_SINC5,
	[AD7768_FILTER_REGVAL_SINC3] = AD7768_FILTER_SINC3,
	[AD7768_FILTER_REGVAL_WIDEBAND] = AD7768_FILTER_WIDEBAND,
	[AD7768_FILTER_REGVAL_SINC3_REJ60] = AD7768_FILTER_SINC3_REJ60,
};

static const char * const ad7768_filter_enum[] = {
	[AD7768_FILTER_SINC5] = "sinc5",
	[AD7768_FILTER_SINC3] = "sinc3",
	[AD7768_FILTER_WIDEBAND] = "wideband",
	[AD7768_FILTER_SINC3_REJ60] = "sinc3+rej60",
};

static const struct iio_scan_type ad7768_scan_type[] = {
	[AD7768_SCAN_TYPE_NORMAL] = {
		.sign = 's',
		.realbits = 24,
		.storagebits = 32,
		.shift = 8,
		.endianness = IIO_BE,
	},
	[AD7768_SCAN_TYPE_HIGH_SPEED] = {
		.sign = 's',
		.realbits = 16,
		.storagebits = 16,
		.endianness = IIO_BE,
	},
};

struct ad7768_state {
	struct spi_device *spi;
	struct regmap *regmap;
	struct regmap *regmap24;
	int vref_uv;
	struct regulator_dev *vcm_rdev;
	unsigned int vcm_output_sel;
	struct clk *mclk;
	unsigned int mclk_freq;
	unsigned int mclk_div;
	unsigned int oversampling_ratio;
	enum ad7768_filter_type filter_type;
	unsigned int samp_freq;
	unsigned int samp_freq_avail[ARRAY_SIZE(ad7768_mclk_div_rates)];
	unsigned int samp_freq_avail_len;
	struct completion completion;
	struct iio_trigger *trig;
	struct gpio_desc *gpio_sync_in;
	struct gpio_desc *gpio_reset;
	const char *labels[AD7768_MAX_CHANNELS];
	struct gpio_chip gpiochip;
	bool en_spi_sync;
	/*
	 * DMA (thus cache coherency maintenance) may require the
	 * transfer buffers to live in their own cache lines.
	 */
	union {
		struct {
			__be32 chan;
			aligned_s64 timestamp;
		} scan;
		__be32 d32;
		u8 d8[2];
	} data __aligned(IIO_DMA_MINALIGN);
};

static const struct regmap_range ad7768_regmap_rd_ranges[] = {
	regmap_reg_range(AD7768_REG_CHIP_TYPE, AD7768_REG_CHIP_GRADE),
	regmap_reg_range(AD7768_REG_SCRATCH_PAD, AD7768_REG_SCRATCH_PAD),
	regmap_reg_range(AD7768_REG_VENDOR_L, AD7768_REG_VENDOR_H),
	regmap_reg_range(AD7768_REG_INTERFACE_FORMAT, AD7768_REG_GAIN_LO),
	regmap_reg_range(AD7768_REG_SPI_DIAG_ENABLE, AD7768_REG_DIG_DIAG_ENABLE),
	regmap_reg_range(AD7768_REG_MASTER_STATUS, AD7768_REG_COEFF_CONTROL),
	regmap_reg_range(AD7768_REG_ACCESS_KEY, AD7768_REG_ACCESS_KEY),
};

static const struct regmap_access_table ad7768_regmap_rd_table = {
	.yes_ranges = ad7768_regmap_rd_ranges,
	.n_yes_ranges = ARRAY_SIZE(ad7768_regmap_rd_ranges),
};

static const struct regmap_range ad7768_regmap_wr_ranges[] = {
	regmap_reg_range(AD7768_REG_SCRATCH_PAD, AD7768_REG_SCRATCH_PAD),
	regmap_reg_range(AD7768_REG_INTERFACE_FORMAT, AD7768_REG_GPIO_WRITE),
	regmap_reg_range(AD7768_REG_OFFSET_HI, AD7768_REG_GAIN_LO),
	regmap_reg_range(AD7768_REG_SPI_DIAG_ENABLE, AD7768_REG_DIG_DIAG_ENABLE),
	regmap_reg_range(AD7768_REG_SPI_DIAG_STATUS, AD7768_REG_SPI_DIAG_STATUS),
	regmap_reg_range(AD7768_REG_COEFF_CONTROL, AD7768_REG_COEFF_CONTROL),
	regmap_reg_range(AD7768_REG_ACCESS_KEY, AD7768_REG_ACCESS_KEY),
};

static const struct regmap_access_table ad7768_regmap_wr_table = {
	.yes_ranges = ad7768_regmap_wr_ranges,
	.n_yes_ranges = ARRAY_SIZE(ad7768_regmap_wr_ranges),
};

static const struct regmap_config ad7768_regmap_config = {
	.name = "ad7768-1-8",
	.reg_bits = 8,
	.val_bits = 8,
	.read_flag_mask = BIT(6),
	.rd_table = &ad7768_regmap_rd_table,
	.wr_table = &ad7768_regmap_wr_table,
	.max_register = AD7768_REG_ACCESS_KEY,
	.use_single_write = true,
	.use_single_read = true,
};

static const struct regmap_range ad7768_regmap24_rd_ranges[] = {
	regmap_reg_range(AD7768_REG24_ADC_DATA, AD7768_REG24_ADC_DATA),
	regmap_reg_range(AD7768_REG24_COEFF_DATA, AD7768_REG24_COEFF_DATA),
};

static const struct regmap_access_table ad7768_regmap24_rd_table = {
	.yes_ranges = ad7768_regmap24_rd_ranges,
	.n_yes_ranges = ARRAY_SIZE(ad7768_regmap24_rd_ranges),
};

static const struct regmap_range ad7768_regmap24_wr_ranges[] = {
	regmap_reg_range(AD7768_REG24_COEFF_DATA, AD7768_REG24_COEFF_DATA),
};

static const struct regmap_access_table ad7768_regmap24_wr_table = {
	.yes_ranges = ad7768_regmap24_wr_ranges,
	.n_yes_ranges = ARRAY_SIZE(ad7768_regmap24_wr_ranges),
};

static const struct regmap_config ad7768_regmap24_config = {
	.name = "ad7768-1-24",
	.reg_bits = 8,
	.val_bits = 24,
	.read_flag_mask = BIT(6),
	.rd_table = &ad7768_regmap24_rd_table,
	.wr_table = &ad7768_regmap24_wr_table,
	.max_register = AD7768_REG24_COEFF_DATA,
};

static int ad7768_send_sync_pulse(struct ad7768_state *st)
{
	if (st->en_spi_sync)
		return regmap_write(st->regmap, AD7768_REG_SYNC_RESET, 0x00);

	/*
	 * The datasheet specifies a minimum SYNC_IN pulse width of 1.5 × Tmclk,
	 * where Tmclk is the MCLK period. The supported MCLK frequencies range
	 * from 0.6 MHz to 17 MHz, which corresponds to a minimum SYNC_IN pulse
	 * width of approximately 2.5 µs in the worst-case scenario (0.6 MHz).
	 *
	 * Add a delay to ensure the pulse width is always sufficient to
	 * trigger synchronization.
	 */
	gpiod_set_value_cansleep(st->gpio_sync_in, 1);
	fsleep(3);
	gpiod_set_value_cansleep(st->gpio_sync_in, 0);

	return 0;
}

static void ad7768_fill_samp_freq_tbl(struct ad7768_state *st)
{
	unsigned int i, samp_freq_avail, freq_filtered;
	unsigned int len = 0;

	freq_filtered = DIV_ROUND_CLOSEST(st->mclk_freq, st->oversampling_ratio);
	for (i = 0; i < ARRAY_SIZE(ad7768_mclk_div_rates); i++) {
		samp_freq_avail = DIV_ROUND_CLOSEST(freq_filtered, ad7768_mclk_div_rates[i]);
		/* Sampling frequency cannot be lower than the minimum of 50 SPS */
		if (samp_freq_avail < 50)
			continue;

		st->samp_freq_avail[len++] = samp_freq_avail;
	}

	st->samp_freq_avail_len = len;
}

static int ad7768_set_mclk_div(struct ad7768_state *st, unsigned int mclk_div)
{
	unsigned int mclk_div_value;

	mclk_div_value = AD7768_PWR_MCLK_DIV(mclk_div);
	/*
	 * Set power mode based on mclk_div value.
	 * ECO_MODE is only recommended for MCLK_DIV = 16.
	 */
	mclk_div_value |= mclk_div > AD7768_MCLK_DIV_16 ?
			  AD7768_PWR_PWRMODE(AD7768_FAST_MODE) :
			  AD7768_PWR_PWRMODE(AD7768_ECO_MODE);

	return regmap_update_bits(st->regmap, AD7768_REG_POWER_CLOCK,
				  AD7768_PWR_MCLK_DIV_MSK | AD7768_PWR_PWRMODE_MSK,
				  mclk_div_value);
}

static int ad7768_set_mode(struct ad7768_state *st,
			   enum ad7768_conv_mode mode)
{
	return regmap_update_bits(st->regmap, AD7768_REG_CONVERSION,
				 AD7768_CONV_MODE_MSK, AD7768_CONV_MODE(mode));
}

static int ad7768_scan_direct(struct iio_dev *indio_dev)
{
	struct ad7768_state *st = iio_priv(indio_dev);
	int readval, ret;

	reinit_completion(&st->completion);

	ret = ad7768_set_mode(st, AD7768_ONE_SHOT);
	if (ret < 0)
		return ret;

	ret = wait_for_completion_timeout(&st->completion,
					  msecs_to_jiffies(1000));
	if (!ret)
		return -ETIMEDOUT;

	ret = regmap_read(st->regmap24, AD7768_REG24_ADC_DATA, &readval);
	if (ret)
		return ret;

	/*
	 * When the decimation rate is set to x8, the ADC data precision is
	 * reduced from 24 bits to 16 bits. Since the AD7768_REG_ADC_DATA
	 * register provides 24-bit data, the precision is reduced by
	 * right-shifting the read value by 8 bits.
	 */
	if (st->oversampling_ratio == 8)
		readval >>= 8;

	/*
	 * Any SPI configuration of the AD7768-1 can only be
	 * performed in continuous conversion mode.
	 */
	ret = ad7768_set_mode(st, AD7768_CONTINUOUS);
	if (ret < 0)
		return ret;

	return readval;
}

static int ad7768_reg_access(struct iio_dev *indio_dev,
			     unsigned int reg,
			     unsigned int writeval,
			     unsigned int *readval)
{
	struct ad7768_state *st = iio_priv(indio_dev);
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = -EINVAL;
	if (readval) {
		if (regmap_check_range_table(st->regmap, reg, &ad7768_regmap_rd_table))
			ret = regmap_read(st->regmap, reg, readval);

		if (regmap_check_range_table(st->regmap24, reg, &ad7768_regmap24_rd_table))
			ret = regmap_read(st->regmap24, reg, readval);

	} else {
		if (regmap_check_range_table(st->regmap, reg, &ad7768_regmap_wr_table))
			ret = regmap_write(st->regmap, reg, writeval);

		if (regmap_check_range_table(st->regmap24, reg, &ad7768_regmap24_wr_table))
			ret = regmap_write(st->regmap24, reg, writeval);

	}

	iio_device_release_direct(indio_dev);

	return ret;
}

static int ad7768_set_sinc3_dec_rate(struct ad7768_state *st,
				     unsigned int dec_rate)
{
	unsigned int max_dec_rate;
	u8 dec_rate_reg[2];
	u16 regval;
	int ret;

	/*
	 * Maximum dec_rate is limited by the MCLK_DIV value and by the ODR.
	 * The edge case is for MCLK_DIV = 2, ODR = 50 SPS.
	 * max_dec_rate <= MCLK / (2 * 50)
	 */
	max_dec_rate = st->mclk_freq / 100;
	dec_rate = clamp(dec_rate, 32, max_dec_rate);
	/*
	 * Calculate the equivalent value to sinc3 decimation ratio
	 * to be written on the SINC3_DEC_RATE register:
	 *  Value = (DEC_RATE / 32) - 1
	 */
	dec_rate = DIV_ROUND_UP(dec_rate, 32) - 1;

	/*
	 * The SINC3_DEC_RATE value is a 13-bit value split across two
	 * registers: MSB [12:8] and LSB [7:0]. Prepare the 13-bit value using
	 * FIELD_PREP() and store it with the right endianness in dec_rate_reg.
	 */
	regval = FIELD_PREP(GENMASK(12, 0), dec_rate);
	put_unaligned_be16(regval, dec_rate_reg);
	ret = regmap_bulk_write(st->regmap, AD7768_REG_SINC3_DEC_RATE_MSB,
				dec_rate_reg, 2);
	if (ret)
		return ret;

	st->oversampling_ratio = (dec_rate + 1) * 32;

	return 0;
}

static int ad7768_configure_dig_fil(struct iio_dev *dev,
				    enum ad7768_filter_type filter_type,
				    unsigned int dec_rate)
{
	struct ad7768_state *st = iio_priv(dev);
	unsigned int dec_rate_idx, dig_filter_regval;
	int ret;

	switch (filter_type) {
	case AD7768_FILTER_SINC3:
		dig_filter_regval = AD7768_DIG_FIL_FIL(AD7768_FILTER_REGVAL_SINC3);
		break;
	case AD7768_FILTER_SINC3_REJ60:
		dig_filter_regval = AD7768_DIG_FIL_FIL(AD7768_FILTER_REGVAL_SINC3) |
				    AD7768_DIG_FIL_EN_60HZ_REJ;
		break;
	case AD7768_FILTER_WIDEBAND:
		/* Skip decimations 8 and 16, not supported by the wideband filter */
		dec_rate_idx = find_closest(dec_rate, &ad7768_dec_rate_values[2],
					    ARRAY_SIZE(ad7768_dec_rate_values) - 2);
		dig_filter_regval = AD7768_DIG_FIL_FIL(AD7768_FILTER_REGVAL_WIDEBAND) |
				    AD7768_DIG_FIL_DEC_RATE(dec_rate_idx);
		/* Correct the index offset */
		dec_rate_idx += 2;
		break;
	case AD7768_FILTER_SINC5:
		dec_rate_idx = find_closest(dec_rate, ad7768_dec_rate_values,
					    ARRAY_SIZE(ad7768_dec_rate_values));

		/*
		 * Decimations 8 (idx 0) and 16 (idx 1) are set in the
		 * FILTER[6:4] field. The other decimations are set in the
		 * DEC_RATE[2:0] field, and the idx needs to be offsetted by two.
		 */
		if (dec_rate_idx == 0)
			dig_filter_regval = AD7768_DIG_FIL_FIL(AD7768_FILTER_REGVAL_SINC5_X8);
		else if (dec_rate_idx == 1)
			dig_filter_regval = AD7768_DIG_FIL_FIL(AD7768_FILTER_REGVAL_SINC5_X16);
		else
			dig_filter_regval = AD7768_DIG_FIL_FIL(AD7768_FILTER_REGVAL_SINC5) |
					    AD7768_DIG_FIL_DEC_RATE(dec_rate_idx - 2);
		break;
	}

	ret = regmap_write(st->regmap, AD7768_REG_DIGITAL_FILTER, dig_filter_regval);
	if (ret)
		return ret;

	st->filter_type = filter_type;
	/*
	 * The decimation for SINC3 filters are configured in different
	 * registers.
	 */
	if (filter_type == AD7768_FILTER_SINC3 ||
	    filter_type == AD7768_FILTER_SINC3_REJ60) {
		ret = ad7768_set_sinc3_dec_rate(st, dec_rate);
		if (ret)
			return ret;
	} else {
		st->oversampling_ratio = ad7768_dec_rate_values[dec_rate_idx];
	}

	ad7768_fill_samp_freq_tbl(st);

	/* A sync-in pulse is required after every configuration change */
	return ad7768_send_sync_pulse(st);
}

static int ad7768_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct iio_dev *indio_dev = gpiochip_get_data(chip);
	struct ad7768_state *st = iio_priv(indio_dev);
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = regmap_clear_bits(st->regmap, AD7768_REG_GPIO_CONTROL,
				BIT(offset));
	iio_device_release_direct(indio_dev);

	return ret;
}

static int ad7768_gpio_direction_output(struct gpio_chip *chip,
					unsigned int offset, int value)
{
	struct iio_dev *indio_dev = gpiochip_get_data(chip);
	struct ad7768_state *st = iio_priv(indio_dev);
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = regmap_set_bits(st->regmap, AD7768_REG_GPIO_CONTROL,
			      BIT(offset));
	iio_device_release_direct(indio_dev);

	return ret;
}

static int ad7768_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct iio_dev *indio_dev = gpiochip_get_data(chip);
	struct ad7768_state *st = iio_priv(indio_dev);
	unsigned int val;
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = regmap_read(st->regmap, AD7768_REG_GPIO_CONTROL, &val);
	if (ret)
		goto err_release;

	/*
	 * If the GPIO is configured as an output, read the current value from
	 * AD7768_REG_GPIO_WRITE. Otherwise, read the input value from
	 * AD7768_REG_GPIO_READ.
	 */
	if (val & BIT(offset))
		ret = regmap_read(st->regmap, AD7768_REG_GPIO_WRITE, &val);
	else
		ret = regmap_read(st->regmap, AD7768_REG_GPIO_READ, &val);
	if (ret)
		goto err_release;

	ret = !!(val & BIT(offset));
err_release:
	iio_device_release_direct(indio_dev);

	return ret;
}

static int ad7768_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct iio_dev *indio_dev = gpiochip_get_data(chip);
	struct ad7768_state *st = iio_priv(indio_dev);
	unsigned int val;
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = regmap_read(st->regmap, AD7768_REG_GPIO_CONTROL, &val);
	if (ret)
		goto err_release;

	if (val & BIT(offset))
		ret = regmap_assign_bits(st->regmap, AD7768_REG_GPIO_WRITE,
					 BIT(offset), value);

err_release:
	iio_device_release_direct(indio_dev);

	return ret;
}

static int ad7768_gpio_init(struct iio_dev *indio_dev)
{
	struct ad7768_state *st = iio_priv(indio_dev);
	int ret;

	ret = regmap_write(st->regmap, AD7768_REG_GPIO_CONTROL,
			   AD7768_GPIO_UNIVERSAL_EN);
	if (ret)
		return ret;

	st->gpiochip = (struct gpio_chip) {
		.label = "ad7768_1_gpios",
		.base = -1,
		.ngpio = 4,
		.parent = &st->spi->dev,
		.can_sleep = true,
		.direction_input = ad7768_gpio_direction_input,
		.direction_output = ad7768_gpio_direction_output,
		.get = ad7768_gpio_get,
		.set = ad7768_gpio_set,
		.owner = THIS_MODULE,
	};

	return devm_gpiochip_add_data(&st->spi->dev, &st->gpiochip, indio_dev);
}

static int ad7768_set_freq(struct ad7768_state *st,
			   unsigned int freq)
{
	unsigned int idx, mclk_div;
	int ret;

	freq = clamp(freq, 50, 1024000);

	mclk_div = DIV_ROUND_CLOSEST(st->mclk_freq, freq * st->oversampling_ratio);
	/* Find the closest match for the desired sampling frequency */
	idx = find_closest_descending(mclk_div, ad7768_mclk_div_rates,
				      ARRAY_SIZE(ad7768_mclk_div_rates));
	/* Set both the mclk_div and pwrmode */
	ret = ad7768_set_mclk_div(st, idx);
	if (ret)
		return ret;

	st->samp_freq = DIV_ROUND_CLOSEST(st->mclk_freq,
					  ad7768_mclk_div_rates[idx] * st->oversampling_ratio);

	/* A sync-in pulse is required after every configuration change */
	return ad7768_send_sync_pulse(st);
}

static int ad7768_set_filter_type_attr(struct iio_dev *dev,
				       const struct iio_chan_spec *chan,
				       unsigned int filter)
{
	struct ad7768_state *st = iio_priv(dev);
	int ret;

	ret = ad7768_configure_dig_fil(dev, filter, st->oversampling_ratio);
	if (ret)
		return ret;

	/* Update sampling frequency */
	return ad7768_set_freq(st, st->samp_freq);
}

static int ad7768_get_filter_type_attr(struct iio_dev *dev,
				       const struct iio_chan_spec *chan)
{
	struct ad7768_state *st = iio_priv(dev);
	int ret;
	unsigned int mode, mask;

	ret = regmap_read(st->regmap, AD7768_REG_DIGITAL_FILTER, &mode);
	if (ret)
		return ret;

	mask = AD7768_DIG_FIL_EN_60HZ_REJ | AD7768_DIG_FIL_FIL_MSK;
	/* From the register value, get the corresponding filter type */
	return ad7768_filter_regval_to_type[FIELD_GET(mask, mode)];
}

static const struct iio_enum ad7768_filter_type_iio_enum = {
	.items = ad7768_filter_enum,
	.num_items = ARRAY_SIZE(ad7768_filter_enum),
	.set = ad7768_set_filter_type_attr,
	.get = ad7768_get_filter_type_attr,
};

static const struct iio_chan_spec_ext_info ad7768_ext_info[] = {
	IIO_ENUM("filter_type", IIO_SHARED_BY_ALL, &ad7768_filter_type_iio_enum),
	IIO_ENUM_AVAILABLE("filter_type", IIO_SHARED_BY_ALL, &ad7768_filter_type_iio_enum),
	{ }
};

static const struct iio_chan_spec ad7768_channels[] = {
	{
		.type = IIO_VOLTAGE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |
					    BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY) |
					    BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.ext_info = ad7768_ext_info,
		.indexed = 1,
		.channel = 0,
		.scan_index = 0,
		.has_ext_scan_type = 1,
		.ext_scan_type = ad7768_scan_type,
		.num_ext_scan_type = ARRAY_SIZE(ad7768_scan_type),
	},
};

static int ad7768_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long info)
{
	struct ad7768_state *st = iio_priv(indio_dev);
	const struct iio_scan_type *scan_type;
	int ret, temp;

	scan_type = iio_get_current_scan_type(indio_dev, chan);
	if (IS_ERR(scan_type))
		return PTR_ERR(scan_type);

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		ret = ad7768_scan_direct(indio_dev);

		iio_device_release_direct(indio_dev);
		if (ret < 0)
			return ret;
		*val = sign_extend32(ret, scan_type->realbits - 1);

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = (st->vref_uv * 2) / 1000;
		*val2 = scan_type->realbits;

		return IIO_VAL_FRACTIONAL_LOG2;

	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = st->samp_freq;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*val = st->oversampling_ratio;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		temp = st->samp_freq * ad7768_filter_3db_odr_multiplier[st->filter_type];
		*val = DIV_ROUND_CLOSEST(temp, MILLI);

		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static int ad7768_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long info)
{
	struct ad7768_state *st = iio_priv(indio_dev);
	unsigned int shift;

	switch (info) {
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		/*
		 * Sinc3 filter allows a wider range of OSR values, so show
		 * the available values in range format.
		 */
		if (st->filter_type == AD7768_FILTER_SINC3 ||
		    st->filter_type == AD7768_FILTER_SINC3_REJ60) {
			*vals = (int *)ad7768_sinc3_dec_rate_range;
			*type = IIO_VAL_INT;
			return IIO_AVAIL_RANGE;
		}

		shift = st->filter_type == AD7768_FILTER_SINC5 ? 0 : 2;
		*vals = (int *)&ad7768_dec_rate_values[shift];
		*length = ARRAY_SIZE(ad7768_dec_rate_values) - shift;
		*type = IIO_VAL_INT;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = (int *)st->samp_freq_avail;
		*length = st->samp_freq_avail_len;
		*type = IIO_VAL_INT;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int __ad7768_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long info)
{
	struct ad7768_state *st = iio_priv(indio_dev);
	int ret;

	switch (info) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return ad7768_set_freq(st, val);

	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		ret = ad7768_configure_dig_fil(indio_dev, st->filter_type, val);
		if (ret)
			return ret;

		/* Update sampling frequency */
		return ad7768_set_freq(st, st->samp_freq);
	default:
		return -EINVAL;
	}
}

static int ad7768_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long info)
{
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = __ad7768_write_raw(indio_dev, chan, val, val2, info);
	iio_device_release_direct(indio_dev);

	return ret;
}

static int ad7768_read_label(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, char *label)
{
	struct ad7768_state *st = iio_priv(indio_dev);

	return sprintf(label, "%s\n", st->labels[chan->channel]);
}

static int ad7768_get_current_scan_type(const struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan)
{
	struct ad7768_state *st = iio_priv(indio_dev);

	return st->oversampling_ratio == 8 ?
	       AD7768_SCAN_TYPE_HIGH_SPEED : AD7768_SCAN_TYPE_NORMAL;
}

static const struct iio_info ad7768_info = {
	.read_raw = &ad7768_read_raw,
	.read_avail = &ad7768_read_avail,
	.write_raw = &ad7768_write_raw,
	.read_label = ad7768_read_label,
	.get_current_scan_type = &ad7768_get_current_scan_type,
	.debugfs_reg_access = &ad7768_reg_access,
};

static struct fwnode_handle *
ad7768_fwnode_find_reference_args(const struct fwnode_handle *fwnode,
				  const char *name, const char *nargs_prop,
				  unsigned int nargs, unsigned int index,
				  struct fwnode_reference_args *args)
{
	int ret;

	ret = fwnode_property_get_reference_args(fwnode, name, nargs_prop,
						 nargs, index, args);
	return ret ? ERR_PTR(ret) : args->fwnode;
}

static int ad7768_trigger_sources_sync_setup(struct device *dev,
					     struct fwnode_handle *fwnode,
					     struct ad7768_state *st)
{
	struct fwnode_reference_args args;

	struct fwnode_handle *ref __free(fwnode_handle) =
		ad7768_fwnode_find_reference_args(fwnode, "trigger-sources",
						  "#trigger-source-cells", 0,
						  AD7768_TRIGGER_SOURCE_SYNC_IDX,
						  &args);
	if (IS_ERR(ref))
		return PTR_ERR(ref);

	ref = args.fwnode;
	/* First, try getting the GPIO trigger source */
	if (fwnode_device_is_compatible(ref, "gpio-trigger")) {
		st->gpio_sync_in = devm_fwnode_gpiod_get_index(dev, ref, NULL, 0,
							       GPIOD_OUT_LOW,
							       "sync-in");
		return PTR_ERR_OR_ZERO(st->gpio_sync_in);
	}

	/*
	 * TODO: Support the other cases when we have a trigger subsystem
	 * to reliably handle other types of devices as trigger sources.
	 *
	 * For now, return an error message. For self triggering, omit the
	 * trigger-sources property.
	 */
	return dev_err_probe(dev, -EOPNOTSUPP, "Invalid synchronization trigger source\n");
}

static int ad7768_trigger_sources_get_sync(struct device *dev,
					   struct ad7768_state *st)
{
	struct fwnode_handle *fwnode = dev_fwnode(dev);

	/*
	 * The AD7768-1 allows two primary methods for driving the SYNC_IN pin
	 * to synchronize one or more devices:
	 * 1. Using an external GPIO.
	 * 2. Using a SPI command, where the SYNC_OUT pin generates a
	 *    synchronization pulse that drives the SYNC_IN pin.
	 */
	if (fwnode_property_present(fwnode, "trigger-sources"))
		return ad7768_trigger_sources_sync_setup(dev, fwnode, st);

	/*
	 * In the absence of trigger-sources property, enable self
	 * synchronization over SPI (SYNC_OUT).
	 */
	st->en_spi_sync = true;

	return 0;
}

static int ad7768_setup(struct iio_dev *indio_dev)
{
	struct ad7768_state *st = iio_priv(indio_dev);
	int ret;

	st->gpio_reset = devm_gpiod_get_optional(&st->spi->dev, "reset",
						 GPIOD_OUT_HIGH);
	if (IS_ERR(st->gpio_reset))
		return PTR_ERR(st->gpio_reset);

	if (st->gpio_reset) {
		fsleep(10);
		gpiod_set_value_cansleep(st->gpio_reset, 0);
		fsleep(200);
	} else {
		/*
		 * Two writes to the SPI_RESET[1:0] bits are required to initiate
		 * a software reset. The bits must first be set to 11, and then
		 * to 10. When the sequence is detected, the reset occurs.
		 * See the datasheet, page 70.
		 */
		ret = regmap_write(st->regmap, AD7768_REG_SYNC_RESET, 0x3);
		if (ret)
			return ret;

		ret = regmap_write(st->regmap, AD7768_REG_SYNC_RESET, 0x2);
		if (ret)
			return ret;
	}

	/* For backwards compatibility, try the adi,sync-in-gpios property */
	st->gpio_sync_in = devm_gpiod_get_optional(&st->spi->dev, "adi,sync-in",
						   GPIOD_OUT_LOW);
	if (IS_ERR(st->gpio_sync_in))
		return PTR_ERR(st->gpio_sync_in);

	/*
	 * If the synchronization is not defined by adi,sync-in-gpios, try the
	 * trigger-sources.
	 */
	if (!st->gpio_sync_in) {
		ret = ad7768_trigger_sources_get_sync(&st->spi->dev, st);
		if (ret)
			return ret;
	}

	/* Only create a Chip GPIO if flagged for it */
	if (device_property_read_bool(&st->spi->dev, "gpio-controller")) {
		ret = ad7768_gpio_init(indio_dev);
		if (ret)
			return ret;
	}

	/*
	 * Set Default Digital Filter configuration:
	 * SINC5 filter with x32 Decimation rate
	 */
	ret = ad7768_configure_dig_fil(indio_dev, AD7768_FILTER_SINC5, 32);
	if (ret)
		return ret;

	/* Set the default sampling frequency to 32000 kSPS */
	return ad7768_set_freq(st, 32000);
}

static irqreturn_t ad7768_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad7768_state *st = iio_priv(indio_dev);
	const struct iio_scan_type *scan_type;
	int ret;

	scan_type = iio_get_current_scan_type(indio_dev, &indio_dev->channels[0]);
	if (IS_ERR(scan_type))
		goto out;

	ret = spi_read(st->spi, &st->data.scan.chan,
		       BITS_TO_BYTES(scan_type->realbits));
	if (ret < 0)
		goto out;

	iio_push_to_buffers_with_ts(indio_dev, &st->data.scan,
				    sizeof(st->data.scan),
				    iio_get_time_ns(indio_dev));

out:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static irqreturn_t ad7768_interrupt(int irq, void *dev_id)
{
	struct iio_dev *indio_dev = dev_id;
	struct ad7768_state *st = iio_priv(indio_dev);

	if (iio_buffer_enabled(indio_dev))
		iio_trigger_poll(st->trig);
	else
		complete(&st->completion);

	return IRQ_HANDLED;
};

static int ad7768_buffer_postenable(struct iio_dev *indio_dev)
{
	struct ad7768_state *st = iio_priv(indio_dev);

	/*
	 * Write a 1 to the LSB of the INTERFACE_FORMAT register to enter
	 * continuous read mode. Subsequent data reads do not require an
	 * initial 8-bit write to query the ADC_DATA register.
	 */
	return regmap_write(st->regmap, AD7768_REG_INTERFACE_FORMAT, 0x01);
}

static int ad7768_buffer_predisable(struct iio_dev *indio_dev)
{
	struct ad7768_state *st = iio_priv(indio_dev);
	unsigned int unused;

	/*
	 * To exit continuous read mode, perform a single read of the ADC_DATA
	 * reg (0x2C), which allows further configuration of the device.
	 */
	return regmap_read(st->regmap24, AD7768_REG24_ADC_DATA, &unused);
}

static const struct iio_buffer_setup_ops ad7768_buffer_ops = {
	.postenable = &ad7768_buffer_postenable,
	.predisable = &ad7768_buffer_predisable,
};

static const struct iio_trigger_ops ad7768_trigger_ops = {
	.validate_device = iio_trigger_validate_own_device,
};

static int ad7768_set_channel_label(struct iio_dev *indio_dev,
						int num_channels)
{
	struct ad7768_state *st = iio_priv(indio_dev);
	struct device *device = indio_dev->dev.parent;
	const char *label;
	int crt_ch = 0;

	device_for_each_child_node_scoped(device, child) {
		if (fwnode_property_read_u32(child, "reg", &crt_ch))
			continue;

		if (crt_ch >= num_channels)
			continue;

		if (fwnode_property_read_string(child, "label", &label))
			continue;

		st->labels[crt_ch] = label;
	}

	return 0;
}

static int ad7768_triggered_buffer_alloc(struct iio_dev *indio_dev)
{
	struct ad7768_state *st = iio_priv(indio_dev);
	int ret;

	st->trig = devm_iio_trigger_alloc(indio_dev->dev.parent, "%s-dev%d",
					  indio_dev->name,
					  iio_device_id(indio_dev));
	if (!st->trig)
		return -ENOMEM;

	st->trig->ops = &ad7768_trigger_ops;
	iio_trigger_set_drvdata(st->trig, indio_dev);
	ret = devm_iio_trigger_register(indio_dev->dev.parent, st->trig);
	if (ret)
		return ret;

	indio_dev->trig = iio_trigger_get(st->trig);

	return devm_iio_triggered_buffer_setup(indio_dev->dev.parent, indio_dev,
					       &iio_pollfunc_store_time,
					       &ad7768_trigger_handler,
					       &ad7768_buffer_ops);
}

static int ad7768_vcm_enable(struct regulator_dev *rdev)
{
	struct iio_dev *indio_dev = rdev_get_drvdata(rdev);
	struct ad7768_state *st = iio_priv(indio_dev);
	int ret, regval;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	/* To enable, set the last selected output */
	regval = AD7768_REG_ANALOG2_VCM(st->vcm_output_sel + 1);
	ret = regmap_update_bits(st->regmap, AD7768_REG_ANALOG2,
				 AD7768_REG_ANALOG2_VCM_MSK, regval);
	iio_device_release_direct(indio_dev);

	return ret;
}

static int ad7768_vcm_disable(struct regulator_dev *rdev)
{
	struct iio_dev *indio_dev = rdev_get_drvdata(rdev);
	struct ad7768_state *st = iio_priv(indio_dev);
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = regmap_update_bits(st->regmap, AD7768_REG_ANALOG2,
				 AD7768_REG_ANALOG2_VCM_MSK, AD7768_VCM_OFF);
	iio_device_release_direct(indio_dev);

	return ret;
}

static int ad7768_vcm_is_enabled(struct regulator_dev *rdev)
{
	struct iio_dev *indio_dev = rdev_get_drvdata(rdev);
	struct ad7768_state *st = iio_priv(indio_dev);
	int ret, val;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = regmap_read(st->regmap, AD7768_REG_ANALOG2, &val);
	iio_device_release_direct(indio_dev);
	if (ret)
		return ret;

	return FIELD_GET(AD7768_REG_ANALOG2_VCM_MSK, val) != AD7768_VCM_OFF;
}

static int ad7768_set_voltage_sel(struct regulator_dev *rdev,
				  unsigned int selector)
{
	unsigned int regval = AD7768_REG_ANALOG2_VCM(selector + 1);
	struct iio_dev *indio_dev = rdev_get_drvdata(rdev);
	struct ad7768_state *st = iio_priv(indio_dev);
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = regmap_update_bits(st->regmap, AD7768_REG_ANALOG2,
				 AD7768_REG_ANALOG2_VCM_MSK, regval);
	iio_device_release_direct(indio_dev);
	if (ret)
		return ret;

	st->vcm_output_sel = selector;

	return 0;
}

static int ad7768_get_voltage_sel(struct regulator_dev *rdev)
{
	struct iio_dev *indio_dev = rdev_get_drvdata(rdev);
	struct ad7768_state *st = iio_priv(indio_dev);
	int ret, val;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = regmap_read(st->regmap, AD7768_REG_ANALOG2, &val);
	iio_device_release_direct(indio_dev);
	if (ret)
		return ret;

	val = FIELD_GET(AD7768_REG_ANALOG2_VCM_MSK, val);

	return clamp(val, 1, rdev->desc->n_voltages) - 1;
}

static const struct regulator_ops vcm_regulator_ops = {
	.enable = ad7768_vcm_enable,
	.disable = ad7768_vcm_disable,
	.is_enabled = ad7768_vcm_is_enabled,
	.list_voltage = regulator_list_voltage_table,
	.set_voltage_sel = ad7768_set_voltage_sel,
	.get_voltage_sel = ad7768_get_voltage_sel,
};

static const unsigned int vcm_voltage_table[] = {
	2500000,
	2050000,
	1650000,
	1900000,
	1100000,
	900000,
};

static const struct regulator_desc vcm_desc = {
	.name = "ad7768-1-vcm",
	.of_match = "vcm-output",
	.regulators_node = "regulators",
	.n_voltages = ARRAY_SIZE(vcm_voltage_table),
	.volt_table = vcm_voltage_table,
	.ops = &vcm_regulator_ops,
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
};

static int ad7768_register_regulators(struct device *dev, struct ad7768_state *st,
				      struct iio_dev *indio_dev)
{
	struct regulator_config config = {
		.dev = dev,
		.driver_data = indio_dev,
	};
	int ret;

	/* Disable the regulator before registering it */
	ret = regmap_update_bits(st->regmap, AD7768_REG_ANALOG2,
				 AD7768_REG_ANALOG2_VCM_MSK, AD7768_VCM_OFF);
	if (ret)
		return ret;

	st->vcm_rdev = devm_regulator_register(dev, &vcm_desc, &config);
	if (IS_ERR(st->vcm_rdev))
		return dev_err_probe(dev, PTR_ERR(st->vcm_rdev),
				     "failed to register VCM regulator\n");

	return 0;
}

static int ad7768_probe(struct spi_device *spi)
{
	struct ad7768_state *st;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	/*
	 * Datasheet recommends SDI line to be kept high when data is not being
	 * clocked out of the controller and the spi clock is free running,
	 * to prevent accidental reset.
	 * Since many controllers do not support the SPI_MOSI_IDLE_HIGH flag
	 * yet, only request the MOSI idle state to enable if the controller
	 * supports it.
	 */
	if (spi->controller->mode_bits & SPI_MOSI_IDLE_HIGH) {
		spi->mode |= SPI_MOSI_IDLE_HIGH;
		ret = spi_setup(spi);
		if (ret < 0)
			return ret;
	}

	st->spi = spi;

	st->regmap = devm_regmap_init_spi(spi, &ad7768_regmap_config);
	if (IS_ERR(st->regmap))
		return dev_err_probe(&spi->dev, PTR_ERR(st->regmap),
				     "Failed to initialize regmap");

	st->regmap24 = devm_regmap_init_spi(spi, &ad7768_regmap24_config);
	if (IS_ERR(st->regmap24))
		return dev_err_probe(&spi->dev, PTR_ERR(st->regmap24),
				     "Failed to initialize regmap24");

	ret = devm_regulator_get_enable_read_voltage(&spi->dev, "vref");
	if (ret < 0)
		return dev_err_probe(&spi->dev, ret,
				     "Failed to get VREF voltage\n");
	st->vref_uv = ret;

	st->mclk = devm_clk_get_enabled(&spi->dev, "mclk");
	if (IS_ERR(st->mclk))
		return PTR_ERR(st->mclk);

	st->mclk_freq = clk_get_rate(st->mclk);

	indio_dev->channels = ad7768_channels;
	indio_dev->num_channels = ARRAY_SIZE(ad7768_channels);
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->info = &ad7768_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	/* Register VCM output regulator */
	ret = ad7768_register_regulators(&spi->dev, st, indio_dev);
	if (ret)
		return ret;

	ret = ad7768_setup(indio_dev);
	if (ret < 0) {
		dev_err(&spi->dev, "AD7768 setup failed\n");
		return ret;
	}

	init_completion(&st->completion);

	ret = ad7768_set_channel_label(indio_dev, ARRAY_SIZE(ad7768_channels));
	if (ret)
		return ret;

	ret = devm_request_irq(&spi->dev, spi->irq,
			       &ad7768_interrupt,
			       IRQF_TRIGGER_RISING | IRQF_ONESHOT,
			       indio_dev->name, indio_dev);
	if (ret)
		return ret;

	ret = ad7768_triggered_buffer_alloc(indio_dev);
	if (ret)
		return ret;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct spi_device_id ad7768_id_table[] = {
	{ "ad7768-1", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad7768_id_table);

static const struct of_device_id ad7768_of_match[] = {
	{ .compatible = "adi,ad7768-1" },
	{ }
};
MODULE_DEVICE_TABLE(of, ad7768_of_match);

static struct spi_driver ad7768_driver = {
	.driver = {
		.name = "ad7768-1",
		.of_match_table = ad7768_of_match,
	},
	.probe = ad7768_probe,
	.id_table = ad7768_id_table,
};
module_spi_driver(ad7768_driver);

MODULE_AUTHOR("Stefan Popa <stefan.popa@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7768-1 ADC driver");
MODULE_LICENSE("GPL v2");
