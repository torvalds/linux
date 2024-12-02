// SPDX-License-Identifier: GPL-2.0-only
/*
 * MAX44000 Ambient and Infrared Proximity Sensor
 *
 * Copyright (c) 2016, Intel Corporation.
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
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
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

/* REG_RX bits */
#define MAX44000_CFG_RX_ALSTIM_MASK	0x0c
#define MAX44000_CFG_RX_ALSTIM_SHIFT	2
#define MAX44000_CFG_RX_ALSPGA_MASK	0x03
#define MAX44000_CFG_RX_ALSPGA_SHIFT	0

/* REG_TX bits */
#define MAX44000_LED_CURRENT_MASK	0xf
#define MAX44000_LED_CURRENT_MAX	11
#define MAX44000_LED_CURRENT_DEFAULT	6

#define MAX44000_ALSDATA_OVERFLOW	0x4000

struct max44000_data {
	struct mutex lock;
	struct regmap *regmap;
	/* Ensure naturally aligned timestamp */
	struct {
		u16 channels[2];
		s64 ts __aligned(8);
	} scan;
};

/* Default scale is set to the minimum of 0.03125 or 1 / (1 << 5) lux */
#define MAX44000_ALS_TO_LUX_DEFAULT_FRACTION_LOG2 5

/* Scale can be multiplied by up to 128x via ALSPGA for measurement gain */
static const int max44000_alspga_shift[] = {0, 2, 4, 7};
#define MAX44000_ALSPGA_MAX_SHIFT 7

/*
 * Scale can be multiplied by up to 64x via ALSTIM because of lost resolution
 *
 * This scaling factor is hidden from userspace and instead accounted for when
 * reading raw values from the device.
 *
 * This makes it possible to cleanly expose ALSPGA as IIO_CHAN_INFO_SCALE and
 * ALSTIM as IIO_CHAN_INFO_INT_TIME without the values affecting each other.
 *
 * Handling this internally is also required for buffer support because the
 * channel's scan_type can't be modified dynamically.
 */
#define MAX44000_ALSTIM_SHIFT(alstim) (2 * (alstim))

/* Available integration times with pretty manual alignment: */
static const int max44000_int_time_avail_ns_array[] = {
	   100000000,
	    25000000,
	     6250000,
	     1562500,
};
static const char max44000_int_time_avail_str[] =
	"0.100 "
	"0.025 "
	"0.00625 "
	"0.0015625";

/* Available scales (internal to ulux) with pretty manual alignment: */
static const int max44000_scale_avail_ulux_array[] = {
	    31250,
	   125000,
	   500000,
	  4000000,
};
static const char max44000_scale_avail_str[] =
	"0.03125 "
	"0.125 "
	"0.5 "
	 "4";

#define MAX44000_SCAN_INDEX_ALS 0
#define MAX44000_SCAN_INDEX_PRX 1

static const struct iio_chan_spec max44000_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |
					    BIT(IIO_CHAN_INFO_INT_TIME),
		.scan_index = MAX44000_SCAN_INDEX_ALS,
		.scan_type = {
			.sign		= 'u',
			.realbits	= 14,
			.storagebits	= 16,
		}
	},
	{
		.type = IIO_PROXIMITY,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.scan_index = MAX44000_SCAN_INDEX_PRX,
		.scan_type = {
			.sign		= 'u',
			.realbits	= 8,
			.storagebits	= 16,
		}
	},
	IIO_CHAN_SOFT_TIMESTAMP(2),
	{
		.type = IIO_CURRENT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.extend_name = "led",
		.output = 1,
		.scan_index = -1,
	},
};

static int max44000_read_alstim(struct max44000_data *data)
{
	unsigned int val;
	int ret;

	ret = regmap_read(data->regmap, MAX44000_REG_CFG_RX, &val);
	if (ret < 0)
		return ret;
	return (val & MAX44000_CFG_RX_ALSTIM_MASK) >> MAX44000_CFG_RX_ALSTIM_SHIFT;
}

static int max44000_write_alstim(struct max44000_data *data, int val)
{
	return regmap_write_bits(data->regmap, MAX44000_REG_CFG_RX,
				 MAX44000_CFG_RX_ALSTIM_MASK,
				 val << MAX44000_CFG_RX_ALSTIM_SHIFT);
}

static int max44000_read_alspga(struct max44000_data *data)
{
	unsigned int val;
	int ret;

	ret = regmap_read(data->regmap, MAX44000_REG_CFG_RX, &val);
	if (ret < 0)
		return ret;
	return (val & MAX44000_CFG_RX_ALSPGA_MASK) >> MAX44000_CFG_RX_ALSPGA_SHIFT;
}

static int max44000_write_alspga(struct max44000_data *data, int val)
{
	return regmap_write_bits(data->regmap, MAX44000_REG_CFG_RX,
				 MAX44000_CFG_RX_ALSPGA_MASK,
				 val << MAX44000_CFG_RX_ALSPGA_SHIFT);
}

static int max44000_read_alsval(struct max44000_data *data)
{
	u16 regval;
	__be16 val;
	int alstim, ret;

	ret = regmap_bulk_read(data->regmap, MAX44000_REG_ALS_DATA_HI,
			       &val, sizeof(val));
	if (ret < 0)
		return ret;
	alstim = ret = max44000_read_alstim(data);
	if (ret < 0)
		return ret;

	regval = be16_to_cpu(val);

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

	return regval << MAX44000_ALSTIM_SHIFT(alstim);
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

static int max44000_read_led_current_raw(struct max44000_data *data)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(data->regmap, MAX44000_REG_CFG_TX, &regval);
	if (ret < 0)
		return ret;
	regval &= MAX44000_LED_CURRENT_MASK;
	if (regval >= 8)
		regval -= 4;
	return regval;
}

static int max44000_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct max44000_data *data = iio_priv(indio_dev);
	int alstim, alspga;
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

		case IIO_CURRENT:
			mutex_lock(&data->lock);
			ret = max44000_read_led_current_raw(data);
			mutex_unlock(&data->lock);
			if (ret < 0)
				return ret;
			*val = ret;
			return IIO_VAL_INT;

		default:
			return -EINVAL;
		}

	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_CURRENT:
			/* Output register is in 10s of miliamps */
			*val = 10;
			return IIO_VAL_INT;

		case IIO_LIGHT:
			mutex_lock(&data->lock);
			alspga = ret = max44000_read_alspga(data);
			mutex_unlock(&data->lock);
			if (ret < 0)
				return ret;

			/* Avoid negative shifts */
			*val = (1 << MAX44000_ALSPGA_MAX_SHIFT);
			*val2 = MAX44000_ALS_TO_LUX_DEFAULT_FRACTION_LOG2
					+ MAX44000_ALSPGA_MAX_SHIFT
					- max44000_alspga_shift[alspga];
			return IIO_VAL_FRACTIONAL_LOG2;

		default:
			return -EINVAL;
		}

	case IIO_CHAN_INFO_INT_TIME:
		mutex_lock(&data->lock);
		alstim = ret = max44000_read_alstim(data);
		mutex_unlock(&data->lock);

		if (ret < 0)
			return ret;
		*val = 0;
		*val2 = max44000_int_time_avail_ns_array[alstim];
		return IIO_VAL_INT_PLUS_NANO;

	default:
		return -EINVAL;
	}
}

static int max44000_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct max44000_data *data = iio_priv(indio_dev);
	int ret;

	if (mask == IIO_CHAN_INFO_RAW && chan->type == IIO_CURRENT) {
		mutex_lock(&data->lock);
		ret = max44000_write_led_current_raw(data, val);
		mutex_unlock(&data->lock);
		return ret;
	} else if (mask == IIO_CHAN_INFO_INT_TIME && chan->type == IIO_LIGHT) {
		s64 valns = val * NSEC_PER_SEC + val2;
		int alstim = find_closest_descending(valns,
				max44000_int_time_avail_ns_array,
				ARRAY_SIZE(max44000_int_time_avail_ns_array));
		mutex_lock(&data->lock);
		ret = max44000_write_alstim(data, alstim);
		mutex_unlock(&data->lock);
		return ret;
	} else if (mask == IIO_CHAN_INFO_SCALE && chan->type == IIO_LIGHT) {
		s64 valus = val * USEC_PER_SEC + val2;
		int alspga = find_closest(valus,
				max44000_scale_avail_ulux_array,
				ARRAY_SIZE(max44000_scale_avail_ulux_array));
		mutex_lock(&data->lock);
		ret = max44000_write_alspga(data, alspga);
		mutex_unlock(&data->lock);
		return ret;
	}

	return -EINVAL;
}

static int max44000_write_raw_get_fmt(struct iio_dev *indio_dev,
				      struct iio_chan_spec const *chan,
				      long mask)
{
	if (mask == IIO_CHAN_INFO_INT_TIME && chan->type == IIO_LIGHT)
		return IIO_VAL_INT_PLUS_NANO;
	else if (mask == IIO_CHAN_INFO_SCALE && chan->type == IIO_LIGHT)
		return IIO_VAL_INT_PLUS_MICRO;
	else
		return IIO_VAL_INT;
}

static IIO_CONST_ATTR(illuminance_integration_time_available, max44000_int_time_avail_str);
static IIO_CONST_ATTR(illuminance_scale_available, max44000_scale_avail_str);

static struct attribute *max44000_attributes[] = {
	&iio_const_attr_illuminance_integration_time_available.dev_attr.attr,
	&iio_const_attr_illuminance_scale_available.dev_attr.attr,
	NULL
};

static const struct attribute_group max44000_attribute_group = {
	.attrs = max44000_attributes,
};

static const struct iio_info max44000_info = {
	.read_raw		= max44000_read_raw,
	.write_raw		= max44000_write_raw,
	.write_raw_get_fmt	= max44000_write_raw_get_fmt,
	.attrs			= &max44000_attribute_group,
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
	.reg_bits		= 8,
	.val_bits		= 8,

	.max_register		= MAX44000_REG_PRX_DATA,
	.readable_reg		= max44000_readable_reg,
	.writeable_reg		= max44000_writeable_reg,
	.volatile_reg		= max44000_volatile_reg,
	.precious_reg		= max44000_precious_reg,

	.use_single_read	= true,
	.use_single_write	= true,
	.cache_type		= REGCACHE_RBTREE,
};

static irqreturn_t max44000_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct max44000_data *data = iio_priv(indio_dev);
	int index = 0;
	unsigned int regval;
	int ret;

	mutex_lock(&data->lock);
	if (test_bit(MAX44000_SCAN_INDEX_ALS, indio_dev->active_scan_mask)) {
		ret = max44000_read_alsval(data);
		if (ret < 0)
			goto out_unlock;
		data->scan.channels[index++] = ret;
	}
	if (test_bit(MAX44000_SCAN_INDEX_PRX, indio_dev->active_scan_mask)) {
		ret = regmap_read(data->regmap, MAX44000_REG_PRX_DATA, &regval);
		if (ret < 0)
			goto out_unlock;
		data->scan.channels[index] = regval;
	}
	mutex_unlock(&data->lock);

	iio_push_to_buffers_with_timestamp(indio_dev, &data->scan,
					   iio_get_time_ns(indio_dev));
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;

out_unlock:
	mutex_unlock(&data->lock);
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

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

	mutex_init(&data->lock);
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

	ret = devm_iio_triggered_buffer_setup(&client->dev, indio_dev, NULL,
					      max44000_trigger_handler, NULL);
	if (ret < 0) {
		dev_err(&client->dev, "iio triggered buffer setup failed\n");
		return ret;
	}

	return devm_iio_device_register(&client->dev, indio_dev);
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
	.id_table	= max44000_id,
};

module_i2c_driver(max44000_driver);

MODULE_AUTHOR("Crestez Dan Leonard <leonard.crestez@intel.com>");
MODULE_DESCRIPTION("MAX44000 Ambient and Infrared Proximity Sensor");
MODULE_LICENSE("GPL v2");
