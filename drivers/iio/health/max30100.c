/*
 * max30100.c - Support for MAX30100 heart rate and pulse oximeter sensor
 *
 * Copyright (C) 2015 Matt Ranostay <mranostay@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * TODO: enable pulse length controls via device tree properties
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h>

#define MAX30100_REGMAP_NAME	"max30100_regmap"
#define MAX30100_DRV_NAME	"max30100"

#define MAX30100_REG_INT_STATUS			0x00
#define MAX30100_REG_INT_STATUS_PWR_RDY		BIT(0)
#define MAX30100_REG_INT_STATUS_SPO2_RDY	BIT(4)
#define MAX30100_REG_INT_STATUS_HR_RDY		BIT(5)
#define MAX30100_REG_INT_STATUS_FIFO_RDY	BIT(7)

#define MAX30100_REG_INT_ENABLE			0x01
#define MAX30100_REG_INT_ENABLE_SPO2_EN		BIT(0)
#define MAX30100_REG_INT_ENABLE_HR_EN		BIT(1)
#define MAX30100_REG_INT_ENABLE_FIFO_EN		BIT(3)
#define MAX30100_REG_INT_ENABLE_MASK		0xf0
#define MAX30100_REG_INT_ENABLE_MASK_SHIFT	4

#define MAX30100_REG_FIFO_WR_PTR		0x02
#define MAX30100_REG_FIFO_OVR_CTR		0x03
#define MAX30100_REG_FIFO_RD_PTR		0x04
#define MAX30100_REG_FIFO_DATA			0x05
#define MAX30100_REG_FIFO_DATA_ENTRY_COUNT	16
#define MAX30100_REG_FIFO_DATA_ENTRY_LEN	4

#define MAX30100_REG_MODE_CONFIG		0x06
#define MAX30100_REG_MODE_CONFIG_MODE_SPO2_EN	BIT(0)
#define MAX30100_REG_MODE_CONFIG_MODE_HR_EN	BIT(1)
#define MAX30100_REG_MODE_CONFIG_MODE_MASK	0x03
#define MAX30100_REG_MODE_CONFIG_TEMP_EN	BIT(3)
#define MAX30100_REG_MODE_CONFIG_PWR		BIT(7)

#define MAX30100_REG_SPO2_CONFIG		0x07
#define MAX30100_REG_SPO2_CONFIG_100HZ		BIT(2)
#define MAX30100_REG_SPO2_CONFIG_HI_RES_EN	BIT(6)
#define MAX30100_REG_SPO2_CONFIG_1600US		0x3

#define MAX30100_REG_LED_CONFIG			0x09
#define MAX30100_REG_LED_CONFIG_LED_MASK	0x0f
#define MAX30100_REG_LED_CONFIG_RED_LED_SHIFT	4

#define MAX30100_REG_LED_CONFIG_24MA		0x07
#define MAX30100_REG_LED_CONFIG_50MA		0x0f

#define MAX30100_REG_TEMP_INTEGER		0x16
#define MAX30100_REG_TEMP_FRACTION		0x17

struct max30100_data {
	struct i2c_client *client;
	struct iio_dev *indio_dev;
	struct mutex lock;
	struct regmap *regmap;

	__be16 buffer[2]; /* 2 16-bit channels */
};

static bool max30100_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX30100_REG_INT_STATUS:
	case MAX30100_REG_MODE_CONFIG:
	case MAX30100_REG_FIFO_WR_PTR:
	case MAX30100_REG_FIFO_OVR_CTR:
	case MAX30100_REG_FIFO_RD_PTR:
	case MAX30100_REG_FIFO_DATA:
	case MAX30100_REG_TEMP_INTEGER:
	case MAX30100_REG_TEMP_FRACTION:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config max30100_regmap_config = {
	.name = MAX30100_REGMAP_NAME,

	.reg_bits = 8,
	.val_bits = 8,

	.max_register = MAX30100_REG_TEMP_FRACTION,
	.cache_type = REGCACHE_FLAT,

	.volatile_reg = max30100_is_volatile_reg,
};

static const unsigned int max30100_led_current_mapping[] = {
	4400, 7600, 11000, 14200, 17400,
	20800, 24000, 27100, 30600, 33800,
	37000, 40200, 43600, 46800, 50000
};

static const unsigned long max30100_scan_masks[] = {0x3, 0};

static const struct iio_chan_spec max30100_channels[] = {
	{
		.type = IIO_INTENSITY,
		.channel2 = IIO_MOD_LIGHT_IR,
		.modified = 1,

		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
	},
	{
		.type = IIO_INTENSITY,
		.channel2 = IIO_MOD_LIGHT_RED,
		.modified = 1,

		.scan_index = 1,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
	},
	{
		.type = IIO_TEMP,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = -1,
	},
};

static int max30100_set_powermode(struct max30100_data *data, bool state)
{
	return regmap_update_bits(data->regmap, MAX30100_REG_MODE_CONFIG,
				  MAX30100_REG_MODE_CONFIG_PWR,
				  state ? 0 : MAX30100_REG_MODE_CONFIG_PWR);
}

static int max30100_clear_fifo(struct max30100_data *data)
{
	int ret;

	ret = regmap_write(data->regmap, MAX30100_REG_FIFO_WR_PTR, 0);
	if (ret)
		return ret;

	ret = regmap_write(data->regmap, MAX30100_REG_FIFO_OVR_CTR, 0);
	if (ret)
		return ret;

	return regmap_write(data->regmap, MAX30100_REG_FIFO_RD_PTR, 0);
}

static int max30100_buffer_postenable(struct iio_dev *indio_dev)
{
	struct max30100_data *data = iio_priv(indio_dev);
	int ret;

	ret = max30100_set_powermode(data, true);
	if (ret)
		return ret;

	return max30100_clear_fifo(data);
}

static int max30100_buffer_predisable(struct iio_dev *indio_dev)
{
	struct max30100_data *data = iio_priv(indio_dev);

	return max30100_set_powermode(data, false);
}

static const struct iio_buffer_setup_ops max30100_buffer_setup_ops = {
	.postenable = max30100_buffer_postenable,
	.predisable = max30100_buffer_predisable,
};

static inline int max30100_fifo_count(struct max30100_data *data)
{
	unsigned int val;
	int ret;

	ret = regmap_read(data->regmap, MAX30100_REG_INT_STATUS, &val);
	if (ret)
		return ret;

	/* FIFO is almost full */
	if (val & MAX30100_REG_INT_STATUS_FIFO_RDY)
		return MAX30100_REG_FIFO_DATA_ENTRY_COUNT - 1;

	return 0;
}

static int max30100_read_measurement(struct max30100_data *data)
{
	int ret;

	ret = i2c_smbus_read_i2c_block_data(data->client,
					    MAX30100_REG_FIFO_DATA,
					    MAX30100_REG_FIFO_DATA_ENTRY_LEN,
					    (u8 *) &data->buffer);

	return (ret == MAX30100_REG_FIFO_DATA_ENTRY_LEN) ? 0 : ret;
}

static irqreturn_t max30100_interrupt_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct max30100_data *data = iio_priv(indio_dev);
	int ret, cnt = 0;

	mutex_lock(&data->lock);

	while (cnt || (cnt = max30100_fifo_count(data) > 0)) {
		ret = max30100_read_measurement(data);
		if (ret)
			break;

		iio_push_to_buffers(data->indio_dev, data->buffer);
		cnt--;
	}

	mutex_unlock(&data->lock);

	return IRQ_HANDLED;
}

static int max30100_get_current_idx(unsigned int val, int *reg)
{
	int idx;

	/* LED turned off */
	if (val == 0) {
		*reg = 0;
		return 0;
	}

	for (idx = 0; idx < ARRAY_SIZE(max30100_led_current_mapping); idx++) {
		if (max30100_led_current_mapping[idx] == val) {
			*reg = idx + 1;
			return 0;
		}
	}

	return -EINVAL;
}

static int max30100_led_init(struct max30100_data *data)
{
	struct device *dev = &data->client->dev;
	struct device_node *np = dev->of_node;
	unsigned int val[2];
	int reg, ret;

	ret = of_property_read_u32_array(np, "maxim,led-current-microamp",
					(unsigned int *) &val, 2);
	if (ret) {
		/* Default to 24 mA RED LED, 50 mA IR LED */
		reg = (MAX30100_REG_LED_CONFIG_24MA <<
			MAX30100_REG_LED_CONFIG_RED_LED_SHIFT) |
			MAX30100_REG_LED_CONFIG_50MA;
		dev_warn(dev, "no led-current-microamp set");

		return regmap_write(data->regmap, MAX30100_REG_LED_CONFIG, reg);
	}

	/* RED LED current */
	ret = max30100_get_current_idx(val[0], &reg);
	if (ret) {
		dev_err(dev, "invalid RED current setting %d", val[0]);
		return ret;
	}

	ret = regmap_update_bits(data->regmap, MAX30100_REG_LED_CONFIG,
		MAX30100_REG_LED_CONFIG_LED_MASK <<
		MAX30100_REG_LED_CONFIG_RED_LED_SHIFT,
		reg << MAX30100_REG_LED_CONFIG_RED_LED_SHIFT);
	if (ret)
		return ret;

	/* IR LED current */
	ret = max30100_get_current_idx(val[1], &reg);
	if (ret) {
		dev_err(dev, "invalid IR current setting %d", val[1]);
		return ret;
	}

	return regmap_update_bits(data->regmap, MAX30100_REG_LED_CONFIG,
		MAX30100_REG_LED_CONFIG_LED_MASK, reg);
}

static int max30100_chip_init(struct max30100_data *data)
{
	int ret;

	/* setup LED current settings */
	ret = max30100_led_init(data);
	if (ret)
		return ret;

	/* enable hi-res SPO2 readings at 100Hz */
	ret = regmap_write(data->regmap, MAX30100_REG_SPO2_CONFIG,
				 MAX30100_REG_SPO2_CONFIG_HI_RES_EN |
				 MAX30100_REG_SPO2_CONFIG_100HZ);
	if (ret)
		return ret;

	/* enable SPO2 mode */
	ret = regmap_update_bits(data->regmap, MAX30100_REG_MODE_CONFIG,
				 MAX30100_REG_MODE_CONFIG_MODE_MASK,
				 MAX30100_REG_MODE_CONFIG_MODE_HR_EN |
				 MAX30100_REG_MODE_CONFIG_MODE_SPO2_EN);
	if (ret)
		return ret;

	/* enable FIFO interrupt */
	return regmap_update_bits(data->regmap, MAX30100_REG_INT_ENABLE,
				 MAX30100_REG_INT_ENABLE_MASK,
				 MAX30100_REG_INT_ENABLE_FIFO_EN
				 << MAX30100_REG_INT_ENABLE_MASK_SHIFT);
}

static int max30100_read_temp(struct max30100_data *data, int *val)
{
	int ret;
	unsigned int reg;

	ret = regmap_read(data->regmap, MAX30100_REG_TEMP_INTEGER, &reg);
	if (ret < 0)
		return ret;
	*val = reg << 4;

	ret = regmap_read(data->regmap, MAX30100_REG_TEMP_FRACTION, &reg);
	if (ret < 0)
		return ret;

	*val |= reg & 0xf;
	*val = sign_extend32(*val, 11);

	return 0;
}

static int max30100_get_temp(struct max30100_data *data, int *val)
{
	int ret;

	/* start acquisition */
	ret = regmap_update_bits(data->regmap, MAX30100_REG_MODE_CONFIG,
				 MAX30100_REG_MODE_CONFIG_TEMP_EN,
				 MAX30100_REG_MODE_CONFIG_TEMP_EN);
	if (ret)
		return ret;

	usleep_range(35000, 50000);

	return max30100_read_temp(data, val);
}

static int max30100_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct max30100_data *data = iio_priv(indio_dev);
	int ret = -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		/*
		 * Temperature reading can only be acquired while engine
		 * is running
		 */
		mutex_lock(&indio_dev->mlock);

		if (!iio_buffer_enabled(indio_dev))
			ret = -EAGAIN;
		else {
			ret = max30100_get_temp(data, val);
			if (!ret)
				ret = IIO_VAL_INT;

		}

		mutex_unlock(&indio_dev->mlock);
		break;
	case IIO_CHAN_INFO_SCALE:
		*val = 1;  /* 0.0625 */
		*val2 = 16;
		ret = IIO_VAL_FRACTIONAL;
		break;
	}

	return ret;
}

static const struct iio_info max30100_info = {
	.driver_module = THIS_MODULE,
	.read_raw = max30100_read_raw,
};

static int max30100_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct max30100_data *data;
	struct iio_buffer *buffer;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	buffer = devm_iio_kfifo_allocate(&client->dev);
	if (!buffer)
		return -ENOMEM;

	iio_device_attach_buffer(indio_dev, buffer);

	indio_dev->name = MAX30100_DRV_NAME;
	indio_dev->channels = max30100_channels;
	indio_dev->info = &max30100_info;
	indio_dev->num_channels = ARRAY_SIZE(max30100_channels);
	indio_dev->available_scan_masks = max30100_scan_masks;
	indio_dev->modes = (INDIO_BUFFER_SOFTWARE | INDIO_DIRECT_MODE);
	indio_dev->setup_ops = &max30100_buffer_setup_ops;

	data = iio_priv(indio_dev);
	data->indio_dev = indio_dev;
	data->client = client;

	mutex_init(&data->lock);
	i2c_set_clientdata(client, indio_dev);

	data->regmap = devm_regmap_init_i2c(client, &max30100_regmap_config);
	if (IS_ERR(data->regmap)) {
		dev_err(&client->dev, "regmap initialization failed.\n");
		return PTR_ERR(data->regmap);
	}
	max30100_set_powermode(data, false);

	ret = max30100_chip_init(data);
	if (ret)
		return ret;

	if (client->irq <= 0) {
		dev_err(&client->dev, "no valid irq defined\n");
		return -EINVAL;
	}
	ret = devm_request_threaded_irq(&client->dev, client->irq,
					NULL, max30100_interrupt_handler,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"max30100_irq", indio_dev);
	if (ret) {
		dev_err(&client->dev, "request irq (%d) failed\n", client->irq);
		return ret;
	}

	return iio_device_register(indio_dev);
}

static int max30100_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct max30100_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	max30100_set_powermode(data, false);

	return 0;
}

static const struct i2c_device_id max30100_id[] = {
	{ "max30100", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, max30100_id);

static const struct of_device_id max30100_dt_ids[] = {
	{ .compatible = "maxim,max30100" },
	{ }
};
MODULE_DEVICE_TABLE(of, max30100_dt_ids);

static struct i2c_driver max30100_driver = {
	.driver = {
		.name	= MAX30100_DRV_NAME,
		.of_match_table	= of_match_ptr(max30100_dt_ids),
	},
	.probe		= max30100_probe,
	.remove		= max30100_remove,
	.id_table	= max30100_id,
};
module_i2c_driver(max30100_driver);

MODULE_AUTHOR("Matt Ranostay <mranostay@gmail.com>");
MODULE_DESCRIPTION("MAX30100 heart rate and pulse oximeter sensor");
MODULE_LICENSE("GPL");
