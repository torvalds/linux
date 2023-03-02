// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Capella Microsystems Inc.
 * Author: Kevin Tsai <ktsai@capellamicro.com>
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/interrupt.h>
#include <linux/regulator/consumer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/init.h>

/* Registers Address */
#define CM32181_REG_ADDR_CMD		0x00
#define CM32181_REG_ADDR_WH		0x01
#define CM32181_REG_ADDR_WL		0x02
#define CM32181_REG_ADDR_TEST		0x03
#define CM32181_REG_ADDR_ALS		0x04
#define CM32181_REG_ADDR_STATUS		0x06
#define CM32181_REG_ADDR_ID		0x07

/* Number of Configurable Registers */
#define CM32181_CONF_REG_NUM		4

/* CMD register */
#define CM32181_CMD_ALS_DISABLE		BIT(0)
#define CM32181_CMD_ALS_INT_EN		BIT(1)
#define CM32181_CMD_ALS_THRES_WINDOW	BIT(2)

#define CM32181_CMD_ALS_PERS_SHIFT	4
#define CM32181_CMD_ALS_PERS_MASK	(0x03 << CM32181_CMD_ALS_PERS_SHIFT)
#define CM32181_CMD_ALS_PERS_DEFAULT	(0x01 << CM32181_CMD_ALS_PERS_SHIFT)

#define CM32181_CMD_ALS_IT_SHIFT	6
#define CM32181_CMD_ALS_IT_MASK		(0x0F << CM32181_CMD_ALS_IT_SHIFT)
#define CM32181_CMD_ALS_IT_DEFAULT	(0x00 << CM32181_CMD_ALS_IT_SHIFT)

#define CM32181_CMD_ALS_SM_SHIFT	11
#define CM32181_CMD_ALS_SM_MASK		(0x03 << CM32181_CMD_ALS_SM_SHIFT)
#define CM32181_CMD_ALS_SM_DEFAULT	(0x01 << CM32181_CMD_ALS_SM_SHIFT)

#define CM32181_LUX_PER_BIT		500	/* ALS_SM=01 IT=800ms */
#define CM32181_LUX_PER_BIT_RESOLUTION	100000
#define CM32181_LUX_PER_BIT_BASE_IT	800000	/* Based on IT=800ms */
#define CM32181_CALIBSCALE_DEFAULT	100000
#define CM32181_CALIBSCALE_RESOLUTION	100000

#define SMBUS_ALERT_RESPONSE_ADDRESS	0x0c

/* CPM0 Index 0: device-id (3218 or 32181), 1: Unknown, 2: init_regs_bitmap */
#define CPM0_REGS_BITMAP		2
#define CPM0_HEADER_SIZE		3

/* CPM1 Index 0: lux_per_bit, 1: calibscale, 2: resolution (100000) */
#define CPM1_LUX_PER_BIT		0
#define CPM1_CALIBSCALE			1
#define CPM1_SIZE			3

/* CM3218 Family */
static const int cm3218_als_it_bits[] = { 0, 1, 2, 3 };
static const int cm3218_als_it_values[] = { 100000, 200000, 400000, 800000 };

/* CM32181 Family */
static const int cm32181_als_it_bits[] = { 12, 8, 0, 1, 2, 3 };
static const int cm32181_als_it_values[] = {
	25000, 50000, 100000, 200000, 400000, 800000
};

struct cm32181_chip {
	struct i2c_client *client;
	struct device *dev;
	struct mutex lock;
	u16 conf_regs[CM32181_CONF_REG_NUM];
	unsigned long init_regs_bitmap;
	int calibscale;
	int lux_per_bit;
	int lux_per_bit_base_it;
	int num_als_it;
	const int *als_it_bits;
	const int *als_it_values;
};

static int cm32181_read_als_it(struct cm32181_chip *cm32181, int *val2);

#ifdef CONFIG_ACPI
/**
 * cm32181_acpi_get_cpm() - Get CPM object from ACPI
 * @dev:	pointer of struct device.
 * @obj_name:	pointer of ACPI object name.
 * @values:	pointer of array for return elements.
 * @count:	maximum size of return array.
 *
 * Convert ACPI CPM table to array.
 *
 * Return: -ENODEV for fail.  Otherwise is number of elements.
 */
static int cm32181_acpi_get_cpm(struct device *dev, char *obj_name,
				u64 *values, int count)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *cpm, *elem;
	acpi_handle handle;
	acpi_status status;
	int i;

	handle = ACPI_HANDLE(dev);
	if (!handle)
		return -ENODEV;

	status = acpi_evaluate_object(handle, obj_name, NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "object %s not found\n", obj_name);
		return -ENODEV;
	}

	cpm = buffer.pointer;
	if (cpm->package.count > count)
		dev_warn(dev, "%s table contains %u values, only using first %d values\n",
			 obj_name, cpm->package.count, count);

	count = min_t(int, cpm->package.count, count);
	for (i = 0; i < count; i++) {
		elem = &(cpm->package.elements[i]);
		values[i] = elem->integer.value;
	}

	kfree(buffer.pointer);

	return count;
}

static void cm32181_acpi_parse_cpm_tables(struct cm32181_chip *cm32181)
{
	u64 vals[CPM0_HEADER_SIZE + CM32181_CONF_REG_NUM];
	struct device *dev = cm32181->dev;
	int i, count;

	count = cm32181_acpi_get_cpm(dev, "CPM0", vals, ARRAY_SIZE(vals));
	if (count <= CPM0_HEADER_SIZE)
		return;

	count -= CPM0_HEADER_SIZE;

	cm32181->init_regs_bitmap = vals[CPM0_REGS_BITMAP];
	cm32181->init_regs_bitmap &= GENMASK(count - 1, 0);
	for_each_set_bit(i, &cm32181->init_regs_bitmap, count)
		cm32181->conf_regs[i] =	vals[CPM0_HEADER_SIZE + i];

	count = cm32181_acpi_get_cpm(dev, "CPM1", vals, ARRAY_SIZE(vals));
	if (count != CPM1_SIZE)
		return;

	cm32181->lux_per_bit = vals[CPM1_LUX_PER_BIT];

	/* Check for uncalibrated devices */
	if (vals[CPM1_CALIBSCALE] == CM32181_CALIBSCALE_DEFAULT)
		return;

	cm32181->calibscale = vals[CPM1_CALIBSCALE];
	/* CPM1 lux_per_bit is for the current it value */
	cm32181_read_als_it(cm32181, &cm32181->lux_per_bit_base_it);
}
#else
static void cm32181_acpi_parse_cpm_tables(struct cm32181_chip *cm32181)
{
}
#endif /* CONFIG_ACPI */

/**
 * cm32181_reg_init() - Initialize CM32181 registers
 * @cm32181:	pointer of struct cm32181.
 *
 * Initialize CM32181 ambient light sensor register to default values.
 *
 * Return: 0 for success; otherwise for error code.
 */
static int cm32181_reg_init(struct cm32181_chip *cm32181)
{
	struct i2c_client *client = cm32181->client;
	int i;
	s32 ret;

	ret = i2c_smbus_read_word_data(client, CM32181_REG_ADDR_ID);
	if (ret < 0)
		return ret;

	/* check device ID */
	switch (ret & 0xFF) {
	case 0x18: /* CM3218 */
		cm32181->num_als_it = ARRAY_SIZE(cm3218_als_it_bits);
		cm32181->als_it_bits = cm3218_als_it_bits;
		cm32181->als_it_values = cm3218_als_it_values;
		break;
	case 0x81: /* CM32181 */
	case 0x82: /* CM32182, fully compat. with CM32181 */
		cm32181->num_als_it = ARRAY_SIZE(cm32181_als_it_bits);
		cm32181->als_it_bits = cm32181_als_it_bits;
		cm32181->als_it_values = cm32181_als_it_values;
		break;
	default:
		return -ENODEV;
	}

	/* Default Values */
	cm32181->conf_regs[CM32181_REG_ADDR_CMD] =
			CM32181_CMD_ALS_IT_DEFAULT | CM32181_CMD_ALS_SM_DEFAULT;
	cm32181->init_regs_bitmap = BIT(CM32181_REG_ADDR_CMD);
	cm32181->calibscale = CM32181_CALIBSCALE_DEFAULT;
	cm32181->lux_per_bit = CM32181_LUX_PER_BIT;
	cm32181->lux_per_bit_base_it = CM32181_LUX_PER_BIT_BASE_IT;

	if (ACPI_HANDLE(cm32181->dev))
		cm32181_acpi_parse_cpm_tables(cm32181);

	/* Initialize registers*/
	for_each_set_bit(i, &cm32181->init_regs_bitmap, CM32181_CONF_REG_NUM) {
		ret = i2c_smbus_write_word_data(client, i,
						cm32181->conf_regs[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/**
 *  cm32181_read_als_it() - Get sensor integration time (ms)
 *  @cm32181:	pointer of struct cm32181
 *  @val2:	pointer of int to load the als_it value.
 *
 *  Report the current integration time in milliseconds.
 *
 *  Return: IIO_VAL_INT_PLUS_MICRO for success, otherwise -EINVAL.
 */
static int cm32181_read_als_it(struct cm32181_chip *cm32181, int *val2)
{
	u16 als_it;
	int i;

	als_it = cm32181->conf_regs[CM32181_REG_ADDR_CMD];
	als_it &= CM32181_CMD_ALS_IT_MASK;
	als_it >>= CM32181_CMD_ALS_IT_SHIFT;
	for (i = 0; i < cm32181->num_als_it; i++) {
		if (als_it == cm32181->als_it_bits[i]) {
			*val2 = cm32181->als_it_values[i];
			return IIO_VAL_INT_PLUS_MICRO;
		}
	}

	return -EINVAL;
}

/**
 * cm32181_write_als_it() - Write sensor integration time
 * @cm32181:	pointer of struct cm32181.
 * @val:	integration time by millisecond.
 *
 * Convert integration time (ms) to sensor value.
 *
 * Return: i2c_smbus_write_word_data command return value.
 */
static int cm32181_write_als_it(struct cm32181_chip *cm32181, int val)
{
	struct i2c_client *client = cm32181->client;
	u16 als_it;
	int ret, i, n;

	n = cm32181->num_als_it;
	for (i = 0; i < n; i++)
		if (val <= cm32181->als_it_values[i])
			break;
	if (i >= n)
		i = n - 1;

	als_it = cm32181->als_it_bits[i];
	als_it <<= CM32181_CMD_ALS_IT_SHIFT;

	mutex_lock(&cm32181->lock);
	cm32181->conf_regs[CM32181_REG_ADDR_CMD] &=
		~CM32181_CMD_ALS_IT_MASK;
	cm32181->conf_regs[CM32181_REG_ADDR_CMD] |=
		als_it;
	ret = i2c_smbus_write_word_data(client, CM32181_REG_ADDR_CMD,
			cm32181->conf_regs[CM32181_REG_ADDR_CMD]);
	mutex_unlock(&cm32181->lock);

	return ret;
}

/**
 * cm32181_get_lux() - report current lux value
 * @cm32181:	pointer of struct cm32181.
 *
 * Convert sensor raw data to lux.  It depends on integration
 * time and calibscale variable.
 *
 * Return: Positive value is lux, otherwise is error code.
 */
static int cm32181_get_lux(struct cm32181_chip *cm32181)
{
	struct i2c_client *client = cm32181->client;
	int ret;
	int als_it;
	u64 lux;

	ret = cm32181_read_als_it(cm32181, &als_it);
	if (ret < 0)
		return -EINVAL;

	lux = cm32181->lux_per_bit;
	lux *= cm32181->lux_per_bit_base_it;
	lux = div_u64(lux, als_it);

	ret = i2c_smbus_read_word_data(client, CM32181_REG_ADDR_ALS);
	if (ret < 0)
		return ret;

	lux *= ret;
	lux *= cm32181->calibscale;
	lux = div_u64(lux, CM32181_CALIBSCALE_RESOLUTION);
	lux = div_u64(lux, CM32181_LUX_PER_BIT_RESOLUTION);

	if (lux > 0xFFFF)
		lux = 0xFFFF;

	return lux;
}

static int cm32181_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct cm32181_chip *cm32181 = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		ret = cm32181_get_lux(cm32181);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBSCALE:
		*val = cm32181->calibscale;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_INT_TIME:
		*val = 0;
		ret = cm32181_read_als_it(cm32181, val2);
		return ret;
	}

	return -EINVAL;
}

static int cm32181_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct cm32181_chip *cm32181 = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_CALIBSCALE:
		cm32181->calibscale = val;
		return val;
	case IIO_CHAN_INFO_INT_TIME:
		ret = cm32181_write_als_it(cm32181, val2);
		return ret;
	}

	return -EINVAL;
}

/**
 * cm32181_get_it_available() - Get available ALS IT value
 * @dev:	pointer of struct device.
 * @attr:	pointer of struct device_attribute.
 * @buf:	pointer of return string buffer.
 *
 * Display the available integration time values by millisecond.
 *
 * Return: string length.
 */
static ssize_t cm32181_get_it_available(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct cm32181_chip *cm32181 = iio_priv(dev_to_iio_dev(dev));
	int i, n, len;

	n = cm32181->num_als_it;
	for (i = 0, len = 0; i < n; i++)
		len += sprintf(buf + len, "0.%06u ", cm32181->als_it_values[i]);
	return len + sprintf(buf + len, "\n");
}

static const struct iio_chan_spec cm32181_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_PROCESSED) |
			BIT(IIO_CHAN_INFO_CALIBSCALE) |
			BIT(IIO_CHAN_INFO_INT_TIME),
	}
};

static IIO_DEVICE_ATTR(in_illuminance_integration_time_available,
			S_IRUGO, cm32181_get_it_available, NULL, 0);

static struct attribute *cm32181_attributes[] = {
	&iio_dev_attr_in_illuminance_integration_time_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group cm32181_attribute_group = {
	.attrs = cm32181_attributes
};

static const struct iio_info cm32181_info = {
	.read_raw		= &cm32181_read_raw,
	.write_raw		= &cm32181_write_raw,
	.attrs			= &cm32181_attribute_group,
};

static int cm32181_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct cm32181_chip *cm32181;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*cm32181));
	if (!indio_dev)
		return -ENOMEM;

	i2c_set_clientdata(client, indio_dev);

	/*
	 * Some ACPI systems list 2 I2C resources for the CM3218 sensor, the
	 * SMBus Alert Response Address (ARA, 0x0c) and the actual I2C address.
	 * Detect this and take the following step to deal with it:
	 * 1. When a SMBus Alert capable sensor has an Alert asserted, it will
	 *    not respond on its actual I2C address. Read a byte from the ARA
	 *    to clear any pending Alerts.
	 * 2. Create a "dummy" client for the actual I2C address and
	 *    use that client to communicate with the sensor.
	 */
	if (ACPI_HANDLE(dev) && client->addr == SMBUS_ALERT_RESPONSE_ADDRESS) {
		struct i2c_board_info board_info = { .type = "dummy" };

		i2c_smbus_read_byte(client);

		client = i2c_acpi_new_device(dev, 1, &board_info);
		if (IS_ERR(client))
			return PTR_ERR(client);
	}

	cm32181 = iio_priv(indio_dev);
	cm32181->client = client;
	cm32181->dev = dev;

	mutex_init(&cm32181->lock);
	indio_dev->channels = cm32181_channels;
	indio_dev->num_channels = ARRAY_SIZE(cm32181_channels);
	indio_dev->info = &cm32181_info;
	indio_dev->name = dev_name(dev);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = cm32181_reg_init(cm32181);
	if (ret) {
		dev_err(dev, "%s: register init failed\n", __func__);
		return ret;
	}

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret) {
		dev_err(dev, "%s: regist device failed\n", __func__);
		return ret;
	}

	return 0;
}

static int cm32181_suspend(struct device *dev)
{
	struct cm32181_chip *cm32181 = iio_priv(dev_get_drvdata(dev));
	struct i2c_client *client = cm32181->client;

	return i2c_smbus_write_word_data(client, CM32181_REG_ADDR_CMD,
					 CM32181_CMD_ALS_DISABLE);
}

static int cm32181_resume(struct device *dev)
{
	struct cm32181_chip *cm32181 = iio_priv(dev_get_drvdata(dev));
	struct i2c_client *client = cm32181->client;

	return i2c_smbus_write_word_data(client, CM32181_REG_ADDR_CMD,
					 cm32181->conf_regs[CM32181_REG_ADDR_CMD]);
}

static DEFINE_SIMPLE_DEV_PM_OPS(cm32181_pm_ops, cm32181_suspend, cm32181_resume);

static const struct of_device_id cm32181_of_match[] = {
	{ .compatible = "capella,cm3218" },
	{ .compatible = "capella,cm32181" },
	{ }
};
MODULE_DEVICE_TABLE(of, cm32181_of_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id cm32181_acpi_match[] = {
	{ "CPLM3218", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, cm32181_acpi_match);
#endif

static struct i2c_driver cm32181_driver = {
	.driver = {
		.name	= "cm32181",
		.acpi_match_table = ACPI_PTR(cm32181_acpi_match),
		.of_match_table = cm32181_of_match,
		.pm = pm_sleep_ptr(&cm32181_pm_ops),
	},
	.probe_new	= cm32181_probe,
};

module_i2c_driver(cm32181_driver);

MODULE_AUTHOR("Kevin Tsai <ktsai@capellamicro.com>");
MODULE_DESCRIPTION("CM32181 ambient light sensor driver");
MODULE_LICENSE("GPL");
