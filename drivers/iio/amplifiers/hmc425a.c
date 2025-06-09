// SPDX-License-Identifier: GPL-2.0
/*
 * HMC425A and similar Gain Amplifiers
 *
 * Copyright 2020, 2024 Analog Devices Inc.
 */

#include <linux/bits.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/kernel.h>
#include <linux/math.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>

/*
 * The LTC6373 amplifier supports configuring gain using GPIO's with the following
 *  values (OUTPUT_V / INPUT_V): 0(shutdown), 0.25, 0.5, 1, 2, 4, 8, 16
 *
 * Except for the shutdown value, all can be converted to dB using 20 * log10(x)
 * From here, it is observed that all values are multiples of the '2' gain setting,
 *  with the correspondent of 6.020dB.
 */
#define LTC6373_CONVERSION_CONSTANT	6020
#define LTC6373_MIN_GAIN_CODE		0x6
#define LTC6373_CONVERSION_MASK		GENMASK(2, 0)
#define LTC6373_SHUTDOWN		GENMASK(2, 0)

enum hmc425a_type {
	ID_HMC425A,
	ID_HMC540S,
	ID_ADRF5740,
	ID_LTC6373,
};

struct hmc425a_chip_info {
	const char			*name;
	const struct iio_chan_spec	*channels;
	unsigned int			num_channels;
	unsigned int			num_gpios;
	int				gain_min;
	int				gain_max;
	int				default_gain;
	int				powerdown_val;
	bool				has_powerdown;

	int				(*gain_dB_to_code)(int gain, int *code);
	int				(*code_to_gain_dB)(int code, int *val, int *val2);
};

struct hmc425a_state {
	struct	mutex lock; /* protect sensor state */
	const struct	hmc425a_chip_info *chip_info;
	struct	gpio_descs *gpios;
	u32	gain;
	bool	powerdown;
};

static int gain_dB_to_code(struct hmc425a_state *st, int val, int val2, int *code)
{
	const struct hmc425a_chip_info *inf = st->chip_info;
	int gain;

	if (val < 0)
		gain = (val * 1000) - (val2 / 1000);
	else
		gain = (val * 1000) + (val2 / 1000);

	if (gain > inf->gain_max || gain < inf->gain_min)
		return -EINVAL;
	if (st->powerdown)
		return -EPERM;

	return st->chip_info->gain_dB_to_code(gain, code);
}

static int hmc425a_gain_dB_to_code(int gain, int *code)
{
	*code = ~((abs(gain) / 500) & 0x3F);
	return 0;
}

static int hmc540s_gain_dB_to_code(int gain, int *code)
{
	*code = ~((abs(gain) / 1000) & 0xF);
	return 0;
}

static int adrf5740_gain_dB_to_code(int gain, int *code)
{
	int temp = (abs(gain) / 2000) & 0xF;

	/* Bit [0-3]: 2dB 4dB 8dB 8dB */
	*code = temp & BIT(3) ? temp | BIT(2) : temp;
	return 0;
}

static int ltc6373_gain_dB_to_code(int gain, int *code)
{
	*code = ~(DIV_ROUND_CLOSEST(gain, LTC6373_CONVERSION_CONSTANT) + 3)
		& LTC6373_CONVERSION_MASK;
	return 0;
}

static int code_to_gain_dB(struct hmc425a_state *st, int *val, int *val2)
{
	if (st->powerdown)
		return -EPERM;
	return st->chip_info->code_to_gain_dB(st->gain, val, val2);
}

static int hmc425a_code_to_gain_dB(int code, int *val, int *val2)
{
	*val = (~code * -500) / 1000;
	*val2 = ((~code * -500) % 1000) * 1000;
	return 0;
}

static int hmc540s_code_to_gain_dB(int code, int *val, int *val2)
{
	*val = (~code * -1000) / 1000;
	*val2 = ((~code * -1000) % 1000) * 1000;
	return 0;
}

static int adrf5740_code_to_gain_dB(int code, int *val, int *val2)
{
	/*
	 * Bit [0-3]: 2dB 4dB 8dB 8dB
	 * When BIT(3) is set, unset BIT(2) and use 3 as double the place value
	 */
	code = code & BIT(3) ? code & ~BIT(2) : code;
	*val = (code * -2000) / 1000;
	*val2 = ((code * -2000) % 1000) * 1000;
	return 0;
}

static int ltc6373_code_to_gain_dB(int code, int *val, int *val2)
{
	int gain = ((~code & LTC6373_CONVERSION_MASK) - 3) *
		   LTC6373_CONVERSION_CONSTANT;

	*val = gain / 1000;
	*val2 = (gain % 1000) * 1000;
	return 0;
}

static int hmc425a_write(struct iio_dev *indio_dev, u32 value)
{
	struct hmc425a_state *st = iio_priv(indio_dev);
	DECLARE_BITMAP(values, BITS_PER_TYPE(value));

	values[0] = value;

	gpiod_multi_set_value_cansleep(st->gpios, values);
	return 0;
}

static int hmc425a_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long m)
{
	struct hmc425a_state *st = iio_priv(indio_dev);
	int ret;

	mutex_lock(&st->lock);
	switch (m) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		ret = code_to_gain_dB(st, val, val2);
		if (ret)
			break;
		ret = IIO_VAL_INT_PLUS_MICRO_DB;
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&st->lock);

	return ret;
};

static int hmc425a_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int val,
			     int val2, long mask)
{
	struct hmc425a_state *st = iio_priv(indio_dev);
	int code = 0, ret;

	mutex_lock(&st->lock);
	switch (mask) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		ret = gain_dB_to_code(st, val, val2, &code);
		if (ret)
			break;
		st->gain = code;

		ret = hmc425a_write(indio_dev, st->gain);
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&st->lock);

	return ret;
}

static int hmc425a_write_raw_get_fmt(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan,
				     long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		return IIO_VAL_INT_PLUS_MICRO_DB;
	default:
		return -EINVAL;
	}
}

static const struct iio_info hmc425a_info = {
	.read_raw = &hmc425a_read_raw,
	.write_raw = &hmc425a_write_raw,
	.write_raw_get_fmt = &hmc425a_write_raw_get_fmt,
};

static ssize_t ltc6373_read_powerdown(struct iio_dev *indio_dev,
				      uintptr_t private,
				      const struct iio_chan_spec *chan,
				      char *buf)
{
	struct hmc425a_state *st = iio_priv(indio_dev);

	return sysfs_emit(buf, "%d\n", st->powerdown);
}

static ssize_t ltc6373_write_powerdown(struct iio_dev *indio_dev,
				       uintptr_t private,
				       const struct iio_chan_spec *chan,
				       const char *buf,
				       size_t len)
{
	struct hmc425a_state *st = iio_priv(indio_dev);
	bool powerdown;
	int code, ret;

	ret = kstrtobool(buf, &powerdown);
	if (ret)
		return ret;

	mutex_lock(&st->lock);
	st->powerdown = powerdown;
	code = (powerdown) ? LTC6373_SHUTDOWN : st->gain;
	hmc425a_write(indio_dev, code);
	mutex_unlock(&st->lock);
	return len;
}

static const struct iio_chan_spec_ext_info ltc6373_ext_info[] = {
	{
		.name = "powerdown",
		.read = ltc6373_read_powerdown,
		.write = ltc6373_write_powerdown,
		.shared = IIO_SEPARATE,
	},
	{}
};

#define HMC425A_CHAN(_channel)						\
{									\
	.type = IIO_VOLTAGE,						\
	.output = 1,							\
	.indexed = 1,							\
	.channel = _channel,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_HARDWAREGAIN),		\
}

#define LTC6373_CHAN(_channel)						\
{									\
	.type = IIO_VOLTAGE,						\
	.output = 1,							\
	.indexed = 1,							\
	.channel = _channel,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_HARDWAREGAIN),		\
	.ext_info = ltc6373_ext_info,					\
}

static const struct iio_chan_spec hmc425a_channels[] = {
	HMC425A_CHAN(0),
};

static const struct iio_chan_spec ltc6373_channels[] = {
	LTC6373_CHAN(0),
};

static const struct hmc425a_chip_info hmc425a_chip_info_tbl[] = {
	[ID_HMC425A] = {
		.name = "hmc425a",
		.channels = hmc425a_channels,
		.num_channels = ARRAY_SIZE(hmc425a_channels),
		.num_gpios = 6,
		.gain_min = -31500,
		.gain_max = 0,
		.default_gain = -0x40, /* set default gain -31.5db*/
		.gain_dB_to_code = hmc425a_gain_dB_to_code,
		.code_to_gain_dB = hmc425a_code_to_gain_dB,
	},
	[ID_HMC540S] = {
		.name = "hmc540s",
		.channels = hmc425a_channels,
		.num_channels = ARRAY_SIZE(hmc425a_channels),
		.num_gpios = 4,
		.gain_min = -15000,
		.gain_max = 0,
		.default_gain = -0x10, /* set default gain -15.0db*/
		.gain_dB_to_code = hmc540s_gain_dB_to_code,
		.code_to_gain_dB = hmc540s_code_to_gain_dB,
	},
	[ID_ADRF5740] = {
		.name = "adrf5740",
		.channels = hmc425a_channels,
		.num_channels = ARRAY_SIZE(hmc425a_channels),
		.num_gpios = 4,
		.gain_min = -22000,
		.gain_max = 0,
		.default_gain = 0xF, /* set default gain -22.0db*/
		.gain_dB_to_code = adrf5740_gain_dB_to_code,
		.code_to_gain_dB = adrf5740_code_to_gain_dB,
	},
	[ID_LTC6373] = {
		.name = "ltc6373",
		.channels = ltc6373_channels,
		.num_channels = ARRAY_SIZE(ltc6373_channels),
		.num_gpios = 3,
		.gain_min = -12041, /* gain setting x0.25*/
		.gain_max = 24082,  /* gain setting x16  */
		.default_gain = LTC6373_MIN_GAIN_CODE,
		.powerdown_val = LTC6373_SHUTDOWN,
		.has_powerdown = true,
		.gain_dB_to_code = ltc6373_gain_dB_to_code,
		.code_to_gain_dB = ltc6373_code_to_gain_dB,
	},
};

static int hmc425a_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	struct hmc425a_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->chip_info = device_get_match_data(&pdev->dev);
	indio_dev->num_channels = st->chip_info->num_channels;
	indio_dev->channels = st->chip_info->channels;
	indio_dev->name = st->chip_info->name;
	st->gain = st->chip_info->default_gain;

	st->gpios = devm_gpiod_get_array(&pdev->dev, "ctrl", GPIOD_OUT_LOW);
	if (IS_ERR(st->gpios))
		return dev_err_probe(&pdev->dev, PTR_ERR(st->gpios),
				     "failed to get gpios\n");

	if (st->gpios->ndescs != st->chip_info->num_gpios) {
		dev_err(&pdev->dev, "%d GPIOs needed to operate\n",
			st->chip_info->num_gpios);
		return -ENODEV;
	}

	ret = devm_regulator_get_enable(&pdev->dev, "vcc-supply");
	if (ret)
		return ret;

	mutex_init(&st->lock);

	indio_dev->info = &hmc425a_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	if (st->chip_info->has_powerdown) {
		st->powerdown = true;
		hmc425a_write(indio_dev, st->chip_info->powerdown_val);
	} else {
		/* Set default gain */
		hmc425a_write(indio_dev, st->gain);
	}

	return devm_iio_device_register(&pdev->dev, indio_dev);
}

/* Match table for of_platform binding */
static const struct of_device_id hmc425a_of_match[] = {
	{ .compatible = "adi,hmc425a",
	  .data = &hmc425a_chip_info_tbl[ID_HMC425A]},
	{ .compatible = "adi,hmc540s",
	  .data = &hmc425a_chip_info_tbl[ID_HMC540S]},
	{ .compatible = "adi,adrf5740",
	  .data = &hmc425a_chip_info_tbl[ID_ADRF5740]},
	{ .compatible = "adi,ltc6373",
	  .data = &hmc425a_chip_info_tbl[ID_LTC6373]},
	{}
};
MODULE_DEVICE_TABLE(of, hmc425a_of_match);

static struct platform_driver hmc425a_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = hmc425a_of_match,
	},
	.probe = hmc425a_probe,
};
module_platform_driver(hmc425a_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices HMC425A and similar GPIO control Gain Amplifiers");
MODULE_LICENSE("GPL v2");
