// SPDX-License-Identifier: GPL-2.0
/*
 * AD3530R/AD3530 8-channel, 16-bit Voltage Output DAC Driver
 * AD3531R/AD3531 4-channel, 16-bit Voltage Output DAC Driver
 *
 * Copyright 2025 Analog Devices Inc.
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/iio.h>
#include <linux/kstrtox.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/units.h>

#define AD3530R_INTERFACE_CONFIG_A		0x00
#define AD3530R_OUTPUT_OPERATING_MODE_0		0x20
#define AD3530R_OUTPUT_OPERATING_MODE_1		0x21
#define AD3530R_OUTPUT_CONTROL_0		0x2A
#define AD3530R_REFERENCE_CONTROL_0		0x3C
#define AD3530R_SW_LDAC_TRIG_A			0xE5
#define AD3530R_INPUT_CH			0xEB
#define AD3530R_MAX_REG_ADDR			0xF9

#define AD3531R_SW_LDAC_TRIG_A			0xDD
#define AD3531R_INPUT_CH			0xE3

#define AD3530R_SLD_TRIG_A			BIT(7)
#define AD3530R_OUTPUT_CONTROL_RANGE		BIT(2)
#define AD3530R_REFERENCE_CONTROL_SEL		BIT(0)
#define AD3530R_REG_VAL_MASK			GENMASK(15, 0)
#define AD3530R_OP_MODE_CHAN_MSK(chan)		(GENMASK(1, 0) << 2 * (chan))

#define AD3530R_SW_RESET			(BIT(7) | BIT(0))
#define AD3530R_INTERNAL_VREF_mV		2500
#define AD3530R_LDAC_PULSE_US			100

#define AD3530R_DAC_MAX_VAL			GENMASK(15, 0)
#define AD3530R_MAX_CHANNELS			8
#define AD3531R_MAX_CHANNELS			4

/* Non-constant mask variant of FIELD_PREP() */
#define field_prep(_mask, _val)	(((_val) << (ffs(_mask) - 1)) & (_mask))

enum ad3530r_mode {
	AD3530R_NORMAL_OP,
	AD3530R_POWERDOWN_1K,
	AD3530R_POWERDOWN_7K7,
	AD3530R_POWERDOWN_32K,
};

struct ad3530r_chan {
	enum ad3530r_mode powerdown_mode;
	bool powerdown;
};

struct ad3530r_chip_info {
	const char *name;
	const struct iio_chan_spec *channels;
	int (*input_ch_reg)(unsigned int channel);
	unsigned int num_channels;
	unsigned int sw_ldac_trig_reg;
	bool internal_ref_support;
};

struct ad3530r_state {
	struct regmap *regmap;
	/* lock to protect against multiple access to the device and shared data */
	struct mutex lock;
	struct ad3530r_chan chan[AD3530R_MAX_CHANNELS];
	const struct ad3530r_chip_info *chip_info;
	struct gpio_desc *ldac_gpio;
	int vref_mV;
	/*
	 * DMA (thus cache coherency maintenance) may require the transfer
	 * buffers to live in their own cache lines.
	 */
	__be16 buf __aligned(IIO_DMA_MINALIGN);
};

static int ad3530r_input_ch_reg(unsigned int channel)
{
	return 2 * channel + AD3530R_INPUT_CH;
}

static int ad3531r_input_ch_reg(unsigned int channel)
{
	return 2 * channel + AD3531R_INPUT_CH;
}

static const char * const ad3530r_powerdown_modes[] = {
	"1kohm_to_gnd",
	"7.7kohm_to_gnd",
	"32kohm_to_gnd",
};

static int ad3530r_get_powerdown_mode(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan)
{
	struct ad3530r_state *st = iio_priv(indio_dev);

	guard(mutex)(&st->lock);
	return st->chan[chan->channel].powerdown_mode - 1;
}

static int ad3530r_set_powerdown_mode(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      unsigned int mode)
{
	struct ad3530r_state *st = iio_priv(indio_dev);

	guard(mutex)(&st->lock);
	st->chan[chan->channel].powerdown_mode = mode + 1;

	return 0;
}

static const struct iio_enum ad3530r_powerdown_mode_enum = {
	.items = ad3530r_powerdown_modes,
	.num_items = ARRAY_SIZE(ad3530r_powerdown_modes),
	.get = ad3530r_get_powerdown_mode,
	.set = ad3530r_set_powerdown_mode,
};

static ssize_t ad3530r_get_dac_powerdown(struct iio_dev *indio_dev,
					 uintptr_t private,
					 const struct iio_chan_spec *chan,
					 char *buf)
{
	struct ad3530r_state *st = iio_priv(indio_dev);

	guard(mutex)(&st->lock);
	return sysfs_emit(buf, "%d\n", st->chan[chan->channel].powerdown);
}

static ssize_t ad3530r_set_dac_powerdown(struct iio_dev *indio_dev,
					 uintptr_t private,
					 const struct iio_chan_spec *chan,
					 const char *buf, size_t len)
{
	struct ad3530r_state *st = iio_priv(indio_dev);
	int ret;
	unsigned int reg, pdmode, mask, val;
	bool powerdown;

	ret = kstrtobool(buf, &powerdown);
	if (ret)
		return ret;

	guard(mutex)(&st->lock);
	reg = chan->channel < AD3531R_MAX_CHANNELS ?
	      AD3530R_OUTPUT_OPERATING_MODE_0 :
	      AD3530R_OUTPUT_OPERATING_MODE_1;
	pdmode = powerdown ? st->chan[chan->channel].powerdown_mode : 0;
	mask = chan->channel < AD3531R_MAX_CHANNELS ?
	       AD3530R_OP_MODE_CHAN_MSK(chan->channel) :
	       AD3530R_OP_MODE_CHAN_MSK(chan->channel - 4);
	val = field_prep(mask, pdmode);

	ret = regmap_update_bits(st->regmap, reg, mask, val);
	if (ret)
		return ret;

	st->chan[chan->channel].powerdown = powerdown;

	return len;
}

static int ad3530r_trigger_hw_ldac(struct gpio_desc *ldac_gpio)
{
	gpiod_set_value_cansleep(ldac_gpio, 1);
	fsleep(AD3530R_LDAC_PULSE_US);
	gpiod_set_value_cansleep(ldac_gpio, 0);

	return 0;
}

static int ad3530r_dac_write(struct ad3530r_state *st, unsigned int chan,
			     unsigned int val)
{
	int ret;

	guard(mutex)(&st->lock);
	st->buf = cpu_to_be16(val);

	ret = regmap_bulk_write(st->regmap, st->chip_info->input_ch_reg(chan),
				&st->buf, sizeof(st->buf));
	if (ret)
		return ret;

	if (st->ldac_gpio)
		return ad3530r_trigger_hw_ldac(st->ldac_gpio);

	return regmap_set_bits(st->regmap, st->chip_info->sw_ldac_trig_reg,
			       AD3530R_SLD_TRIG_A);
}

static int ad3530r_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long info)
{
	struct ad3530r_state *st = iio_priv(indio_dev);
	int ret;

	guard(mutex)(&st->lock);
	switch (info) {
	case IIO_CHAN_INFO_RAW:
		ret = regmap_bulk_read(st->regmap,
				       st->chip_info->input_ch_reg(chan->channel),
				       &st->buf, sizeof(st->buf));
		if (ret)
			return ret;

		*val = FIELD_GET(AD3530R_REG_VAL_MASK, be16_to_cpu(st->buf));

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = st->vref_mV;
		*val2 = 16;

		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static int ad3530r_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long info)
{
	struct ad3530r_state *st = iio_priv(indio_dev);

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		if (val < 0 || val > AD3530R_DAC_MAX_VAL)
			return -EINVAL;

		return ad3530r_dac_write(st, chan->channel, val);
	default:
		return -EINVAL;
	}
}

static int ad3530r_reg_access(struct iio_dev *indio_dev, unsigned int reg,
			      unsigned int writeval, unsigned int *readval)
{
	struct ad3530r_state *st = iio_priv(indio_dev);

	if (readval)
		return regmap_read(st->regmap, reg, readval);

	return regmap_write(st->regmap, reg, writeval);
}

static const struct iio_chan_spec_ext_info ad3530r_ext_info[] = {
	{
		.name = "powerdown",
		.shared = IIO_SEPARATE,
		.read = ad3530r_get_dac_powerdown,
		.write = ad3530r_set_dac_powerdown,
	},
	IIO_ENUM("powerdown_mode", IIO_SEPARATE, &ad3530r_powerdown_mode_enum),
	IIO_ENUM_AVAILABLE("powerdown_mode", IIO_SHARED_BY_TYPE,
			   &ad3530r_powerdown_mode_enum),
	{ }
};

#define AD3530R_CHAN(_chan)					\
{								\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.channel = _chan,					\
	.output = 1,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
			      BIT(IIO_CHAN_INFO_SCALE),		\
	.ext_info = ad3530r_ext_info,				\
}

static const struct iio_chan_spec ad3530r_channels[] = {
	AD3530R_CHAN(0),
	AD3530R_CHAN(1),
	AD3530R_CHAN(2),
	AD3530R_CHAN(3),
	AD3530R_CHAN(4),
	AD3530R_CHAN(5),
	AD3530R_CHAN(6),
	AD3530R_CHAN(7),
};

static const struct iio_chan_spec ad3531r_channels[] = {
	AD3530R_CHAN(0),
	AD3530R_CHAN(1),
	AD3530R_CHAN(2),
	AD3530R_CHAN(3),
};

static const struct ad3530r_chip_info ad3530_chip = {
	.name = "ad3530",
	.channels = ad3530r_channels,
	.num_channels = ARRAY_SIZE(ad3530r_channels),
	.sw_ldac_trig_reg = AD3530R_SW_LDAC_TRIG_A,
	.input_ch_reg = ad3530r_input_ch_reg,
	.internal_ref_support = false,
};

static const struct ad3530r_chip_info ad3530r_chip = {
	.name = "ad3530r",
	.channels = ad3530r_channels,
	.num_channels = ARRAY_SIZE(ad3530r_channels),
	.sw_ldac_trig_reg = AD3530R_SW_LDAC_TRIG_A,
	.input_ch_reg = ad3530r_input_ch_reg,
	.internal_ref_support = true,
};

static const struct ad3530r_chip_info ad3531_chip = {
	.name = "ad3531",
	.channels = ad3531r_channels,
	.num_channels = ARRAY_SIZE(ad3531r_channels),
	.sw_ldac_trig_reg = AD3531R_SW_LDAC_TRIG_A,
	.input_ch_reg = ad3531r_input_ch_reg,
	.internal_ref_support = false,
};

static const struct ad3530r_chip_info ad3531r_chip = {
	.name = "ad3531r",
	.channels = ad3531r_channels,
	.num_channels = ARRAY_SIZE(ad3531r_channels),
	.sw_ldac_trig_reg = AD3531R_SW_LDAC_TRIG_A,
	.input_ch_reg = ad3531r_input_ch_reg,
	.internal_ref_support = true,
};

static int ad3530r_setup(struct ad3530r_state *st, int external_vref_uV)
{
	struct device *dev = regmap_get_device(st->regmap);
	struct gpio_desc *reset_gpio;
	int i, ret;
	u8 range_multiplier, val;

	reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(reset_gpio))
		return dev_err_probe(dev, PTR_ERR(reset_gpio),
				     "Failed to get reset GPIO\n");

	if (reset_gpio) {
		/* Perform hardware reset */
		fsleep(1 * USEC_PER_MSEC);
		gpiod_set_value_cansleep(reset_gpio, 0);
	} else {
		/* Perform software reset */
		ret = regmap_update_bits(st->regmap, AD3530R_INTERFACE_CONFIG_A,
					 AD3530R_SW_RESET, AD3530R_SW_RESET);
		if (ret)
			return ret;
	}

	fsleep(10 * USEC_PER_MSEC);

	range_multiplier = 1;
	if (device_property_read_bool(dev, "adi,range-double")) {
		ret = regmap_set_bits(st->regmap, AD3530R_OUTPUT_CONTROL_0,
				      AD3530R_OUTPUT_CONTROL_RANGE);
		if (ret)
			return ret;

		range_multiplier = 2;
	}

	if (external_vref_uV) {
		st->vref_mV = range_multiplier * external_vref_uV / MILLI;
	} else {
		ret = regmap_set_bits(st->regmap, AD3530R_REFERENCE_CONTROL_0,
				      AD3530R_REFERENCE_CONTROL_SEL);
		if (ret)
			return ret;

		st->vref_mV = range_multiplier * AD3530R_INTERNAL_VREF_mV;
	}

	/* Set normal operating mode for all channels */
	val = FIELD_PREP(AD3530R_OP_MODE_CHAN_MSK(0), AD3530R_NORMAL_OP) |
	      FIELD_PREP(AD3530R_OP_MODE_CHAN_MSK(1), AD3530R_NORMAL_OP) |
	      FIELD_PREP(AD3530R_OP_MODE_CHAN_MSK(2), AD3530R_NORMAL_OP) |
	      FIELD_PREP(AD3530R_OP_MODE_CHAN_MSK(3), AD3530R_NORMAL_OP);

	ret = regmap_write(st->regmap, AD3530R_OUTPUT_OPERATING_MODE_0, val);
	if (ret)
		return ret;

	if (st->chip_info->num_channels > 4) {
		ret = regmap_write(st->regmap, AD3530R_OUTPUT_OPERATING_MODE_1,
				   val);
		if (ret)
			return ret;
	}

	for (i = 0; i < st->chip_info->num_channels; i++)
		st->chan[i].powerdown_mode = AD3530R_POWERDOWN_32K;

	st->ldac_gpio = devm_gpiod_get_optional(dev, "ldac", GPIOD_OUT_LOW);
	if (IS_ERR(st->ldac_gpio))
		return dev_err_probe(dev, PTR_ERR(st->ldac_gpio),
				     "Failed to get ldac GPIO\n");

	return 0;
}

static const struct regmap_config ad3530r_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = AD3530R_MAX_REG_ADDR,
};

static const struct iio_info ad3530r_info = {
	.read_raw = ad3530r_read_raw,
	.write_raw = ad3530r_write_raw,
	.debugfs_reg_access = ad3530r_reg_access,
};

static int ad3530r_probe(struct spi_device *spi)
{
	static const char * const regulators[] = { "vdd", "iovdd" };
	struct device *dev = &spi->dev;
	struct iio_dev *indio_dev;
	struct ad3530r_state *st;
	int ret, external_vref_uV;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->regmap = devm_regmap_init_spi(spi, &ad3530r_regmap_config);
	if (IS_ERR(st->regmap))
		return dev_err_probe(dev, PTR_ERR(st->regmap),
				     "Failed to init regmap");

	ret = devm_mutex_init(dev, &st->lock);
	if (ret)
		return ret;

	st->chip_info = spi_get_device_match_data(spi);
	if (!st->chip_info)
		return -ENODEV;

	ret = devm_regulator_bulk_get_enable(dev, ARRAY_SIZE(regulators),
					     regulators);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable regulators\n");

	external_vref_uV = devm_regulator_get_enable_read_voltage(dev, "ref");
	if (external_vref_uV < 0 && external_vref_uV != -ENODEV)
		return external_vref_uV;

	if (external_vref_uV == -ENODEV)
		external_vref_uV = 0;

	if (!st->chip_info->internal_ref_support && external_vref_uV == 0)
		return -ENODEV;

	ret = ad3530r_setup(st, external_vref_uV);
	if (ret)
		return ret;

	indio_dev->name = st->chip_info->name;
	indio_dev->info = &ad3530r_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = st->chip_info->channels;
	indio_dev->num_channels = st->chip_info->num_channels;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct spi_device_id ad3530r_id[] = {
	{ "ad3530", (kernel_ulong_t)&ad3530_chip },
	{ "ad3530r", (kernel_ulong_t)&ad3530r_chip },
	{ "ad3531", (kernel_ulong_t)&ad3531_chip },
	{ "ad3531r", (kernel_ulong_t)&ad3531r_chip },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad3530r_id);

static const struct of_device_id ad3530r_of_match[] = {
	{ .compatible = "adi,ad3530", .data = &ad3530_chip },
	{ .compatible = "adi,ad3530r", .data = &ad3530r_chip },
	{ .compatible = "adi,ad3531", .data = &ad3531_chip },
	{ .compatible = "adi,ad3531r", .data = &ad3531r_chip },
	{ }
};
MODULE_DEVICE_TABLE(of, ad3530r_of_match);

static struct spi_driver ad3530r_driver = {
	.driver = {
		.name = "ad3530r",
		.of_match_table = ad3530r_of_match,
	},
	.probe = ad3530r_probe,
	.id_table = ad3530r_id,
};
module_spi_driver(ad3530r_driver);

MODULE_AUTHOR("Kim Seer Paller <kimseer.paller@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD3530R and Similar DACs Driver");
MODULE_LICENSE("GPL");
