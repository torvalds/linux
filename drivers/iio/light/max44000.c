/*
 * MAX44000 Ambient and Infrared Proximity Sensor
 *
 * Copyright (c) 2016, Intel Corporation.
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * Data sheet: https://datasheets.maximintegrated.com/en/ds/MAX44000.pdf
 *
 * 7-bit I2C slave address 0x4a
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/util_macros.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/acpi.h>

#define MAX44000_DRV_NAME		"max44000"

/* Registers in datasheet order */
#define MAX44000_REG_STATUS		0x00
#define MAX44000_REG_CFG_MAIN		0x01
#define MAX44000_REG_CFG_RX		0x02
#define MAX44000_REG_CFG_TX		0x03
#define MAX44000_REG_ALS_DATA_HI	0x04
#define MAX44000_REG_ALS_DATA_LO	0x05
#define MAX44000_REG_PRX_DATA		0x16
#define MAX44000_REG_ALS_UPTHR_HI	0x06
#define MAX44000_REG_ALS_UPTHR_LO	0x07
#define MAX44000_REG_ALS_LOTHR_HI	0x08
#define MAX44000_REG_ALS_LOTHR_LO	0x09
#define MAX44000_REG_PST		0x0a
#define MAX44000_REG_PRX_IND		0x0b
#define MAX44000_REG_PRX_THR		0x0c
#define MAX44000_REG_TRIM_GAIN_GREEN	0x0f
#define MAX44000_REG_TRIM_GAIN_IR	0x10

/* REG_CFG bits */
#define MAX44000_CFG_ALSINTE            0x01
#define MAX44000_CFG_PRXINTE            0x02
#define MAX44000_CFG_MASK               0x1c
#define MAX44000_CFG_MODE_SHUTDOWN      0x00
#define MAX44000_CFG_MODE_ALS_GIR       0x04
#define MAX44000_CFG_MODE_ALS_G         0x08
#define MAX44000_CFG_MODE_ALS_IR        0x0c
#define MAX44000_CFG_MODE_ALS_PRX       0x10
#define MAX44000_CFG_MODE_PRX           0x14
#define MAX44000_CFG_TRIM               0x20

/*
 * Upper 4 bits are not documented but start as 1 on powerup
 * Setting them to 0 causes proximity to misbehave so set them to 1
 */
#define MAX44000_REG_CFG_RX_DEFAULT 0xf0

/* REG_TX bits */
#define MAX44000_LED_CURRENT_MASK	0xf
#define MAX44000_LED_CURRENT_MAX	11
#define MAX44000_LED_CURRENT_DEFAULT	6

#define MAX44000_ALSDATA_OVERFLOW	0x4000

struct max44000_data {
	struct mutex lock;
	struct regmap *regmap;
};

/* Default scale is set to the minimum of 0.03125 or 1 / (1 << 5) lux */
#define MAX44000_ALS_TO_LUX_DEFAULT_FRACTION_LOG2 5

static const struct iio_chan_spec max44000_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
	},
	{
		.type = IIO_PROXIMITY,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
	},
};

static int max44000_read_alsval(struct max44000_data *data)
{
	u16 regval;
	int ret;

	ret = regmap_bulk_read(data->regmap, MAX44000_REG_ALS_DATA_HI,
			       &regval, sizeof(regval));
	if (ret < 0)
		return ret;

	regval = be16_to_cpu(regval);

	/*
	 * Overflow is explained on datasheet page 17.
	 *
	 * It's a warning that either the G or IR channel has become saturated
	 * and that the value in the register is likely incorrect.
	 *
	 * The recommendation is to change the scale (ALSPGA).
	 * The driver just returns the max representable value.
	 */
	if (regval & MAX44000_ALSDATA_OVERFLOW)
		return 0x3FFF;

	return regval;
}

static int max44000_write_led_current_raw(struct max44000_data *data, int val)
{
	/* Maybe we should clamp the value instead? */
	if (val < 0 || val > MAX44000_LED_CURRENT_MAX)
		return -ERANGE;
	if (val >= 8)
		val += 4;
	return regmap_write_bits(data->regmap, MAX44000_REG_CFG_TX,
				 MAX44000_LED_CURRENT_MASK, val);
}

static int max44000_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct max44000_data *data = iio_priv(indio_dev);
	unsigned int regval;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_LIGHT:
			mutex_lock(&data->lock);
			ret = max44000_read_alsval(data);
			mutex_unlock(&data->lock);
			if (ret < 0)
				return ret;
			*val = ret;
			return IIO_VAL_INT;

		case IIO_PROXIMITY:
			mutex_lock(&data->lock);
			ret = regmap_read(data->regmap, MAX44000_REG_PRX_DATA, &regval);
			mutex_unlock(&data->lock);
			if (ret < 0)
				return ret;
			*val = regval;
			return IIO_VAL_INT;

		default:
			return -EINVAL;
		}

	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_LIGHT:
			*val = 1;
			*val2 = MAX44000_ALS_TO_LUX_DEFAULT_FRACTION_LOG2;
			return IIO_VAL_FRACTIONAL_LOG2;

		default:
			return -EINVAL;
		}

	default:
		return -EINVAL;
	}
}

static const struct iio_info max44000_info = {
	.driver_module		= THIS_MODULE,
	.read_raw		= max44000_read_raw,
};

static bool max44000_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX44000_REG_STATUS:
	case MAX44000_REG_CFG_MAIN:
	case MAX44000_REG_CFG_RX:
	case MAX44000_REG_CFG_TX:
	case MAX44000_REG_ALS_DATA_HI:
	case MAX44000_REG_ALS_DATA_LO:
	case MAX44000_REG_PRX_DATA:
	case MAX44000_REG_ALS_UPTHR_HI:
	case MAX44000_REG_ALS_UPTHR_LO:
	case MAX44000_REG_ALS_LOTHR_HI:
	case MAX44000_REG_ALS_LOTHR_LO:
	case MAX44000_REG_PST:
	case MAX44000_REG_PRX_IND:
	case MAX44000_REG_PRX_THR:
	case MAX44000_REG_TRIM_GAIN_GREEN:
	case MAX44000_REG_TRIM_GAIN_IR:
		return true;
	default:
		return false;
	}
}

static bool max44000_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX44000_REG_CFG_MAIN:
	case MAX44000_REG_CFG_RX:
	case MAX44000_REG_CFG_TX:
	case MAX44000_REG_ALS_UPTHR_HI:
	case MAX44000_REG_ALS_UPTHR_LO:
	case MAX44000_REG_ALS_LOTHR_HI:
	case MAX44000_REG_ALS_LOTHR_LO:
	case MAX44000_REG_PST:
	case MAX44000_REG_PRX_IND:
	case MAX44000_REG_PRX_THR:
	case MAX44000_REG_TRIM_GAIN_GREEN:
	case MAX44000_REG_TRIM_GAIN_IR:
		return true;
	default:
		return false;
	}
}

static bool max44000_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX44000_REG_STATUS:
	case MAX44000_REG_ALS_DATA_HI:
	case MAX44000_REG_ALS_DATA_LO:
	case MAX44000_REG_PRX_DATA:
		return true;
	default:
		return false;
	}
}

static bool max44000_precious_reg(struct device *dev, unsigned int reg)
{
	return reg == MAX44000_REG_STATUS;
}

static const struct regmap_config max44000_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,

	.max_register	= MAX44000_REG_PRX_DATA,
	.readable_reg	= max44000_readable_reg,
	.writeable_reg	= max44000_writeable_reg,
	.volatile_reg	= max44000_volatile_reg,
	.precious_reg	= max44000_precious_reg,

	.use_single_rw	= 1,
	.cache_type	= REGCACHE_RBTREE,
};

static int max44000_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct max44000_data *data;
	struct iio_dev *indio_dev;
	int ret, reg;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;
	data = iio_priv(indio_dev);
	data->regmap = devm_regmap_init_i2c(client, &max44000_regmap_config);
	if (IS_ERR(data->regmap)) {
		dev_err(&client->dev, "regmap_init failed!\n");
		return PTR_ERR(data->regmap);
	}

	i2c_set_clientdata(client, indio_dev);
	mutex_init(&data->lock);
	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &max44000_info;
	indio_dev->name = MAX44000_DRV_NAME;
	indio_dev->channels = max44000_channels;
	indio_dev->num_channels = ARRAY_SIZE(max44000_channels);

	/*
	 * The device doesn't have a reset function so we just clear some
	 * important bits at probe time to ensure sane operation.
	 *
	 * Since we don't support interrupts/events the threshold values are
	 * not important. We also don't touch trim values.
	 */

	/* Reset ALS scaling bits */
	ret = regmap_write(data->regmap, MAX44000_REG_CFG_RX,
			   MAX44000_REG_CFG_RX_DEFAULT);
	if (ret < 0) {
		dev_err(&client->dev, "failed to write default CFG_RX: %d\n",
			ret);
		return ret;
	}

	/*
	 * By default the LED pulse used for the proximity sensor is disabled.
	 * Set a middle value so that we get some sort of valid data by default.
	 */
	ret = max44000_write_led_current_raw(data, MAX44000_LED_CURRENT_DEFAULT);
	if (ret < 0) {
		dev_err(&client->dev, "failed to write init config: %d\n", ret);
		return ret;
	}

	/* Reset CFG bits to ALS_PRX mode which allows easy reading of both values. */
	reg = MAX44000_CFG_TRIM | MAX44000_CFG_MODE_ALS_PRX;
	ret = regmap_write(data->regmap, MAX44000_REG_CFG_MAIN, reg);
	if (ret < 0) {
		dev_err(&client->dev, "failed to write init config: %d\n", ret);
		return ret;
	}

	/* Read status at least once to clear any stale interrupt bits. */
	ret = regmap_read(data->regmap, MAX44000_REG_STATUS, &reg);
	if (ret < 0) {
		dev_err(&client->dev, "failed to read init status: %d\n", ret);
		return ret;
	}

	return iio_device_register(indio_dev);
}

static int max44000_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);

	return 0;
}

static const struct i2c_device_id max44000_id[] = {
	{"max44000", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, max44000_id);

#ifdef CONFIG_ACPI
static const struct acpi_device_id max44000_acpi_match[] = {
	{"MAX44000", 0},
	{ }
};
MODULE_DEVICE_TABLE(acpi, max44000_acpi_match);
#endif

static struct i2c_driver max44000_driver = {
	.driver = {
		.name	= MAX44000_DRV_NAME,
		.acpi_match_table = ACPI_PTR(max44000_acpi_match),
	},
	.probe		= max44000_probe,
	.remove		= max44000_remove,
	.id_table	= max44000_id,
};

module_i2c_driver(max44000_driver);

MODULE_AUTHOR("Crestez Dan Leonard <leonard.crestez@intel.com>");
MODULE_DESCRIPTION("MAX44000 Ambient and Infrared Proximity Sensor");
MODULE_LICENSE("GPL v2");
