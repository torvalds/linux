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

enum ad7768_dec_rate {
	AD7768_DEC_RATE_32 = 0,
	AD7768_DEC_RATE_64 = 1,
	AD7768_DEC_RATE_128 = 2,
	AD7768_DEC_RATE_256 = 3,
	AD7768_DEC_RATE_512 = 4,
	AD7768_DEC_RATE_1024 = 5,
	AD7768_DEC_RATE_8 = 9,
	AD7768_DEC_RATE_16 = 10
};

struct ad7768_clk_configuration {
	enum ad7768_mclk_div mclk_div;
	enum ad7768_dec_rate dec_rate;
	unsigned int clk_div;
	enum ad7768_pwrmode pwrmode;
};

enum ad7768_scan_type {
	AD7768_SCAN_TYPE_NORMAL,
	AD7768_SCAN_TYPE_HIGH_SPEED,
};

static const int ad7768_mclk_div_rates[] = {
	16, 8, 4, 2,
};

static const struct ad7768_clk_configuration ad7768_clk_config[] = {
	{ AD7768_MCLK_DIV_2, AD7768_DEC_RATE_8, 16,  AD7768_FAST_MODE },
	{ AD7768_MCLK_DIV_2, AD7768_DEC_RATE_16, 32,  AD7768_FAST_MODE },
	{ AD7768_MCLK_DIV_2, AD7768_DEC_RATE_32, 64, AD7768_FAST_MODE },
	{ AD7768_MCLK_DIV_2, AD7768_DEC_RATE_64, 128, AD7768_FAST_MODE },
	{ AD7768_MCLK_DIV_2, AD7768_DEC_RATE_128, 256, AD7768_FAST_MODE },
	{ AD7768_MCLK_DIV_4, AD7768_DEC_RATE_128, 512, AD7768_MED_MODE },
	{ AD7768_MCLK_DIV_4, AD7768_DEC_RATE_256, 1024, AD7768_MED_MODE },
	{ AD7768_MCLK_DIV_4, AD7768_DEC_RATE_512, 2048, AD7768_MED_MODE },
	{ AD7768_MCLK_DIV_4, AD7768_DEC_RATE_1024, 4096, AD7768_MED_MODE },
	{ AD7768_MCLK_DIV_8, AD7768_DEC_RATE_1024, 8192, AD7768_MED_MODE },
	{ AD7768_MCLK_DIV_16, AD7768_DEC_RATE_1024, 16384, AD7768_ECO_MODE },
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

static const struct iio_chan_spec ad7768_channels[] = {
	{
		.type = IIO_VOLTAGE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.indexed = 1,
		.channel = 0,
		.scan_index = 0,
		.has_ext_scan_type = 1,
		.ext_scan_type = ad7768_scan_type,
		.num_ext_scan_type = ARRAY_SIZE(ad7768_scan_type),
	},
};

struct ad7768_state {
	struct spi_device *spi;
	struct regmap *regmap;
	struct regmap *regmap24;
	struct regulator *vref;
	struct regulator_dev *vcm_rdev;
	unsigned int vcm_output_sel;
	struct clk *mclk;
	unsigned int mclk_freq;
	unsigned int dec_rate;
	unsigned int samp_freq;
	struct completion completion;
	struct iio_trigger *trig;
	struct gpio_desc *gpio_sync_in;
	struct gpio_desc *gpio_reset;
	const char *labels[ARRAY_SIZE(ad7768_channels)];
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
	if (st->dec_rate == 8)
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

static int ad7768_set_dig_fil(struct ad7768_state *st,
			      enum ad7768_dec_rate dec_rate)
{
	unsigned int mode;
	int ret;

	if (dec_rate == AD7768_DEC_RATE_8 || dec_rate == AD7768_DEC_RATE_16)
		mode = AD7768_DIG_FIL_FIL(dec_rate);
	else
		mode = AD7768_DIG_FIL_DEC_RATE(dec_rate);

	ret = regmap_write(st->regmap, AD7768_REG_DIGITAL_FILTER, mode);
	if (ret < 0)
		return ret;

	/* A sync-in pulse is required every time the filter dec rate changes */
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
		.set_rv = ad7768_gpio_set,
		.owner = THIS_MODULE,
	};

	return devm_gpiochip_add_data(&st->spi->dev, &st->gpiochip, indio_dev);
}

static int ad7768_set_freq(struct ad7768_state *st,
			   unsigned int freq)
{
	unsigned int diff_new, diff_old, pwr_mode, i, idx;
	int res, ret;

	diff_old = U32_MAX;
	idx = 0;

	res = DIV_ROUND_CLOSEST(st->mclk_freq, freq);

	/* Find the closest match for the desired sampling frequency */
	for (i = 0; i < ARRAY_SIZE(ad7768_clk_config); i++) {
		diff_new = abs(res - ad7768_clk_config[i].clk_div);
		if (diff_new < diff_old) {
			diff_old = diff_new;
			idx = i;
		}
	}

	/*
	 * Set both the mclk_div and pwrmode with a single write to the
	 * POWER_CLOCK register
	 */
	pwr_mode = AD7768_PWR_MCLK_DIV(ad7768_clk_config[idx].mclk_div) |
		   AD7768_PWR_PWRMODE(ad7768_clk_config[idx].pwrmode);
	ret = regmap_write(st->regmap, AD7768_REG_POWER_CLOCK, pwr_mode);
	if (ret < 0)
		return ret;

	ret =  ad7768_set_dig_fil(st, ad7768_clk_config[idx].dec_rate);
	if (ret < 0)
		return ret;

	st->dec_rate = ad7768_clk_config[idx].clk_div /
		       ad7768_mclk_div_rates[ad7768_clk_config[idx].mclk_div];
	st->samp_freq = DIV_ROUND_CLOSEST(st->mclk_freq,
					  ad7768_clk_config[idx].clk_div);

	return 0;
}

static ssize_t ad7768_sampling_freq_avail(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7768_state *st = iio_priv(indio_dev);
	unsigned int freq;
	int i, len = 0;

	for (i = 0; i < ARRAY_SIZE(ad7768_clk_config); i++) {
		freq = DIV_ROUND_CLOSEST(st->mclk_freq,
					 ad7768_clk_config[i].clk_div);
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d ", freq);
	}

	buf[len - 1] = '\n';

	return len;
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(ad7768_sampling_freq_avail);

static int ad7768_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long info)
{
	struct ad7768_state *st = iio_priv(indio_dev);
	const struct iio_scan_type *scan_type;
	int scale_uv, ret;

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
		scale_uv = regulator_get_voltage(st->vref);
		if (scale_uv < 0)
			return scale_uv;

		*val = (scale_uv * 2) / 1000;
		*val2 = scan_type->realbits;

		return IIO_VAL_FRACTIONAL_LOG2;

	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = st->samp_freq;

		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static int ad7768_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long info)
{
	struct ad7768_state *st = iio_priv(indio_dev);

	switch (info) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return ad7768_set_freq(st, val);
	default:
		return -EINVAL;
	}
}

static int ad7768_read_label(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, char *label)
{
	struct ad7768_state *st = iio_priv(indio_dev);

	return sprintf(label, "%s\n", st->labels[chan->channel]);
}

static struct attribute *ad7768_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	NULL
};

static const struct attribute_group ad7768_group = {
	.attrs = ad7768_attributes,
};

static int ad7768_get_current_scan_type(const struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan)
{
	struct ad7768_state *st = iio_priv(indio_dev);

	return st->dec_rate == 8 ?
	       AD7768_SCAN_TYPE_HIGH_SPEED : AD7768_SCAN_TYPE_NORMAL;
}

static const struct iio_info ad7768_info = {
	.attrs = &ad7768_group,
	.read_raw = &ad7768_read_raw,
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

static void ad7768_regulator_disable(void *data)
{
	struct ad7768_state *st = data;

	regulator_disable(st->vref);
}

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

	st->vref = devm_regulator_get(&spi->dev, "vref");
	if (IS_ERR(st->vref))
		return PTR_ERR(st->vref);

	ret = regulator_enable(st->vref);
	if (ret) {
		dev_err(&spi->dev, "Failed to enable specified vref supply\n");
		return ret;
	}

	ret = devm_add_action_or_reset(&spi->dev, ad7768_regulator_disable, st);
	if (ret)
		return ret;

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
