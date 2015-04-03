/*
 * CM3232 Ambient Light Sensor
 *
 * Copyright (C) 2014-2015 Capella Microsystems Inc.
 * Author: Kevin Tsai <ktsai@capellamicro.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2, as published
 * by the Free Software Foundation.
 *
 * IIO driver for CM3232 (7-bit I2C slave address 0x10).
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/init.h>

/* Registers Address */
#define CM3232_REG_ADDR_CMD		0x00
#define CM3232_REG_ADDR_ALS		0x50
#define CM3232_REG_ADDR_ID		0x53

#define CM3232_CMD_ALS_DISABLE		BIT(0)

#define CM3232_CMD_ALS_IT_SHIFT		2
#define CM3232_CMD_ALS_IT_MASK		(BIT(2) | BIT(3) | BIT(4))
#define CM3232_CMD_ALS_IT_DEFAULT	(0x01 << CM3232_CMD_ALS_IT_SHIFT)

#define CM3232_CMD_ALS_RESET		BIT(6)

#define CM3232_CMD_DEFAULT		CM3232_CMD_ALS_IT_DEFAULT

#define CM3232_HW_ID			0x32
#define CM3232_CALIBSCALE_DEFAULT	100000
#define CM3232_CALIBSCALE_RESOLUTION	100000
#define CM3232_MLUX_PER_LUX		1000

#define CM3232_MLUX_PER_BIT_DEFAULT	64
#define CM3232_MLUX_PER_BIT_BASE_IT	100000

static const struct {
	int val;
	int val2;
	u8 it;
} cm3232_als_it_scales[] = {
	{0, 100000, 0},	/* 0.100000 */
	{0, 200000, 1},	/* 0.200000 */
	{0, 400000, 2},	/* 0.400000 */
	{0, 800000, 3},	/* 0.800000 */
	{1, 600000, 4},	/* 1.600000 */
	{3, 200000, 5},	/* 3.200000 */
};

struct cm3232_als_info {
	u8 regs_cmd_default;
	u8 hw_id;
	int calibscale;
	int mlux_per_bit;
	int mlux_per_bit_base_it;
};

static struct cm3232_als_info cm3232_als_info_default = {
	.regs_cmd_default = CM3232_CMD_DEFAULT,
	.hw_id = CM3232_HW_ID,
	.calibscale = CM3232_CALIBSCALE_DEFAULT,
	.mlux_per_bit = CM3232_MLUX_PER_BIT_DEFAULT,
	.mlux_per_bit_base_it = CM3232_MLUX_PER_BIT_BASE_IT,
};

struct cm3232_chip {
	struct i2c_client *client;
	struct cm3232_als_info *als_info;
	u8 regs_cmd;
	u16 regs_als;
};

/**
 * cm3232_reg_init() - Initialize CM3232
 * @chip:	pointer of struct cm3232_chip.
 *
 * Check and initialize CM3232 ambient light sensor.
 *
 * Return: 0 for success; otherwise for error code.
 */
static int cm3232_reg_init(struct cm3232_chip *chip)
{
	struct i2c_client *client = chip->client;
	s32 ret;

	chip->als_info = &cm3232_als_info_default;

	/* Identify device */
	ret = i2c_smbus_read_word_data(client, CM3232_REG_ADDR_ID);
	if (ret < 0) {
		dev_err(&chip->client->dev, "Error reading addr_id\n");
		return ret;
	}

	if ((ret & 0xFF) != chip->als_info->hw_id)
		return -ENODEV;

	/* Disable and reset device */
	chip->regs_cmd = CM3232_CMD_ALS_DISABLE | CM3232_CMD_ALS_RESET;
	ret = i2c_smbus_write_byte_data(client, CM3232_REG_ADDR_CMD,
					chip->regs_cmd);
	if (ret < 0) {
		dev_err(&chip->client->dev, "Error writing reg_cmd\n");
		return ret;
	}

	/* Register default value */
	chip->regs_cmd = chip->als_info->regs_cmd_default;

	/* Configure register */
	ret = i2c_smbus_write_byte_data(client, CM3232_REG_ADDR_CMD,
					chip->regs_cmd);
	if (ret < 0)
		dev_err(&chip->client->dev, "Error writing reg_cmd\n");

	return 0;
}

/**
 *  cm3232_read_als_it() - Get sensor integration time
 *  @chip:	pointer of struct cm3232_chip
 *  @val:	pointer of int to load the integration (sec).
 *  @val2:	pointer of int to load the integration time (microsecond).
 *
 *  Report the current integration time.
 *
 *  Return: IIO_VAL_INT_PLUS_MICRO for success, otherwise -EINVAL.
 */
static int cm3232_read_als_it(struct cm3232_chip *chip, int *val, int *val2)
{
	u16 als_it;
	int i;

	als_it = chip->regs_cmd;
	als_it &= CM3232_CMD_ALS_IT_MASK;
	als_it >>= CM3232_CMD_ALS_IT_SHIFT;
	for (i = 0; i < ARRAY_SIZE(cm3232_als_it_scales); i++) {
		if (als_it == cm3232_als_it_scales[i].it) {
			*val = cm3232_als_it_scales[i].val;
			*val2 = cm3232_als_it_scales[i].val2;
			return IIO_VAL_INT_PLUS_MICRO;
		}
	}

	return -EINVAL;
}

/**
 * cm3232_write_als_it() - Write sensor integration time
 * @chip:	pointer of struct cm3232_chip.
 * @val:	integration time in second.
 * @val2:	integration time in microsecond.
 *
 * Convert integration time to sensor value.
 *
 * Return: i2c_smbus_write_byte_data command return value.
 */
static int cm3232_write_als_it(struct cm3232_chip *chip, int val, int val2)
{
	struct i2c_client *client = chip->client;
	u16 als_it, cmd;
	int i;
	s32 ret;

	for (i = 0; i < ARRAY_SIZE(cm3232_als_it_scales); i++) {
		if (val == cm3232_als_it_scales[i].val &&
			val2 == cm3232_als_it_scales[i].val2) {

			als_it = cm3232_als_it_scales[i].it;
			als_it <<= CM3232_CMD_ALS_IT_SHIFT;

			cmd = chip->regs_cmd & ~CM3232_CMD_ALS_IT_MASK;
			cmd |= als_it;
			ret = i2c_smbus_write_byte_data(client,
							CM3232_REG_ADDR_CMD,
							cmd);
			if (ret < 0)
				return ret;
			chip->regs_cmd = cmd;
			return 0;
		}
	}
	return -EINVAL;
}

/**
 * cm3232_get_lux() - report current lux value
 * @chip:	pointer of struct cm3232_chip.
 *
 * Convert sensor data to lux.  It depends on integration
 * time and calibscale variable.
 *
 * Return: Zero or positive value is lux, otherwise error code.
 */
static int cm3232_get_lux(struct cm3232_chip *chip)
{
	struct i2c_client *client = chip->client;
	struct cm3232_als_info *als_info = chip->als_info;
	int ret;
	int val, val2;
	int als_it;
	u64 lux;

	/* Calculate mlux per bit based on als_it */
	ret = cm3232_read_als_it(chip, &val, &val2);
	if (ret < 0)
		return -EINVAL;
	als_it = val * 1000000 + val2;
	lux = (__force u64)als_info->mlux_per_bit;
	lux *= als_info->mlux_per_bit_base_it;
	lux = div_u64(lux, als_it);

	ret = i2c_smbus_read_word_data(client, CM3232_REG_ADDR_ALS);
	if (ret < 0) {
		dev_err(&client->dev, "Error reading reg_addr_als\n");
		return ret;
	}

	chip->regs_als = (u16)ret;
	lux *= chip->regs_als;
	lux *= als_info->calibscale;
	lux = div_u64(lux, CM3232_CALIBSCALE_RESOLUTION);
	lux = div_u64(lux, CM3232_MLUX_PER_LUX);

	if (lux > 0xFFFF)
		lux = 0xFFFF;

	return (int)lux;
}

static int cm3232_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val, int *val2, long mask)
{
	struct cm3232_chip *chip = iio_priv(indio_dev);
	struct cm3232_als_info *als_info = chip->als_info;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		ret = cm3232_get_lux(chip);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBSCALE:
		*val = als_info->calibscale;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_INT_TIME:
		return cm3232_read_als_it(chip, val, val2);
	}

	return -EINVAL;
}

static int cm3232_write_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int val, int val2, long mask)
{
	struct cm3232_chip *chip = iio_priv(indio_dev);
	struct cm3232_als_info *als_info = chip->als_info;

	switch (mask) {
	case IIO_CHAN_INFO_CALIBSCALE:
		als_info->calibscale = val;
		return 0;
	case IIO_CHAN_INFO_INT_TIME:
		return cm3232_write_als_it(chip, val, val2);
	}

	return -EINVAL;
}

/**
 * cm3232_get_it_available() - Get available ALS IT value
 * @dev:	pointer of struct device.
 * @attr:	pointer of struct device_attribute.
 * @buf:	pointer of return string buffer.
 *
 * Display the available integration time in second.
 *
 * Return: string length.
 */
static ssize_t cm3232_get_it_available(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int i, len;

	for (i = 0, len = 0; i < ARRAY_SIZE(cm3232_als_it_scales); i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%u.%06u ",
			cm3232_als_it_scales[i].val,
			cm3232_als_it_scales[i].val2);
	return len + scnprintf(buf + len, PAGE_SIZE - len, "\n");
}

static const struct iio_chan_spec cm3232_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_PROCESSED) |
			BIT(IIO_CHAN_INFO_CALIBSCALE) |
			BIT(IIO_CHAN_INFO_INT_TIME),
	}
};

static IIO_DEVICE_ATTR(in_illuminance_integration_time_available,
			S_IRUGO, cm3232_get_it_available, NULL, 0);

static struct attribute *cm3232_attributes[] = {
	&iio_dev_attr_in_illuminance_integration_time_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group cm3232_attribute_group = {
	.attrs = cm3232_attributes
};

static const struct iio_info cm3232_info = {
	.driver_module		= THIS_MODULE,
	.read_raw		= &cm3232_read_raw,
	.write_raw		= &cm3232_write_raw,
	.attrs			= &cm3232_attribute_group,
};

static int cm3232_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct cm3232_chip *chip;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;

	chip = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	chip->client = client;

	indio_dev->dev.parent = &client->dev;
	indio_dev->channels = cm3232_channels;
	indio_dev->num_channels = ARRAY_SIZE(cm3232_channels);
	indio_dev->info = &cm3232_info;
	indio_dev->name = id->name;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = cm3232_reg_init(chip);
	if (ret) {
		dev_err(&client->dev,
			"%s: register init failed\n",
			__func__);
		return ret;
	}

	return iio_device_register(indio_dev);
}

static int cm3232_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	i2c_smbus_write_byte_data(client, CM3232_REG_ADDR_CMD,
		CM3232_CMD_ALS_DISABLE);

	iio_device_unregister(indio_dev);

	return 0;
}

static const struct i2c_device_id cm3232_id[] = {
	{"cm3232", 0},
	{}
};

#ifdef CONFIG_PM_SLEEP
static int cm3232_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct cm3232_chip *chip = iio_priv(indio_dev);
	struct i2c_client *client = chip->client;
	int ret;

	chip->regs_cmd |= CM3232_CMD_ALS_DISABLE;
	ret = i2c_smbus_write_byte_data(client, CM3232_REG_ADDR_CMD,
					chip->regs_cmd);

	return ret;
}

static int cm3232_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct cm3232_chip *chip = iio_priv(indio_dev);
	struct i2c_client *client = chip->client;
	int ret;

	chip->regs_cmd &= ~CM3232_CMD_ALS_DISABLE;
	ret = i2c_smbus_write_byte_data(client, CM3232_REG_ADDR_CMD,
					chip->regs_cmd | CM3232_CMD_ALS_RESET);

	return ret;
}

static const struct dev_pm_ops cm3232_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cm3232_suspend, cm3232_resume)};
#endif

MODULE_DEVICE_TABLE(i2c, cm3232_id);

static const struct of_device_id cm3232_of_match[] = {
	{.compatible = "capella,cm3232"},
	{}
};

static struct i2c_driver cm3232_driver = {
	.driver = {
		.name	= "cm3232",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(cm3232_of_match),
#ifdef CONFIG_PM_SLEEP
		.pm	= &cm3232_pm_ops,
#endif
	},
	.id_table	= cm3232_id,
	.probe		= cm3232_probe,
	.remove		= cm3232_remove,
};

module_i2c_driver(cm3232_driver);

MODULE_AUTHOR("Kevin Tsai <ktsai@capellamicro.com>");
MODULE_DESCRIPTION("CM3232 ambient light sensor driver");
MODULE_LICENSE("GPL");
