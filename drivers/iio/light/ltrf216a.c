// SPDX-License-Identifier: GPL-2.0-only
/*
 * LTRF216A Ambient Light Sensor
 *
 * Copyright (C) 2022 Collabora, Ltd.
 * Author: Shreeya Patel <shreeya.patel@collabora.com>
 *
 * Copyright (C) 2021 Lite-On Technology Corp (Singapore)
 * Author: Shi Zhigang <Zhigang.Shi@liteon.com>
 *
 * IIO driver for LTRF216A (7-bit I2C slave address 0x53).
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <linux/iio/iio.h>

#include <linux/unaligned.h>

#define LTRF216A_ALS_RESET_MASK		BIT(4)
#define LTRF216A_ALS_DATA_STATUS	BIT(3)
#define LTRF216A_ALS_ENABLE_MASK	BIT(1)
#define LTRF216A_MAIN_CTRL		0x00
#define LTRF216A_ALS_MEAS_RES		0x04
#define LTRF216A_ALS_GAIN		0x05
#define LTRF216A_PART_ID		0x06
#define LTRF216A_MAIN_STATUS		0x07
#define LTRF216A_ALS_CLEAR_DATA_0	0x0a
#define LTRF216A_ALS_CLEAR_DATA_1	0x0b
#define LTRF216A_ALS_CLEAR_DATA_2	0x0c
#define LTRF216A_ALS_DATA_0		0x0d
#define LTRF216A_ALS_DATA_1		0x0e
#define LTRF216A_ALS_DATA_2		0x0f
#define LTRF216A_INT_CFG		0x19
#define LTRF216A_INT_PST		0x1a
#define LTRF216A_ALS_THRES_UP_0		0x21
#define LTRF216A_ALS_THRES_UP_1		0x22
#define LTRF216A_ALS_THRES_UP_2		0x23
#define LTRF216A_ALS_THRES_LOW_0	0x24
#define LTRF216A_ALS_THRES_LOW_1	0x25
#define LTRF216A_ALS_THRES_LOW_2	0x26
#define LTRF216A_ALS_READ_DATA_DELAY_US	20000

static const int ltrf216a_int_time_available[][2] = {
	{ 0, 400000 },
	{ 0, 200000 },
	{ 0, 100000 },
	{ 0,  50000 },
	{ 0,  25000 },
};

static const int ltrf216a_int_time_reg[][2] = {
	{ 400, 0x03 },
	{ 200, 0x13 },
	{ 100, 0x22 },
	{  50, 0x31 },
	{  25, 0x40 },
};

struct ltr_chip_info {
	/* Chip contains CLEAR_DATA_0/1/2 registers at offset 0xa..0xc */
	bool		has_clear_data;
	/* Lux calculation multiplier for ALS data */
	int		lux_multiplier;
};

/*
 * Window Factor is needed when the device is under Window glass
 * with coated tinted ink. This is to compensate for the light loss
 * due to the lower transmission rate of the window glass and helps
 * in calculating lux.
 */
#define LTRF216A_WIN_FAC	1

struct ltrf216a_data {
	struct regmap *regmap;
	struct i2c_client *client;
	const struct ltr_chip_info *info;
	u32 int_time;
	u16 int_time_fac;
	u8 als_gain_fac;
	/*
	 * Protects regmap accesses and makes sure integration time
	 * remains constant during the measurement of lux.
	 */
	struct mutex lock;
};

static const struct iio_chan_spec ltrf216a_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_PROCESSED) |
			BIT(IIO_CHAN_INFO_INT_TIME),
		.info_mask_separate_available =
			BIT(IIO_CHAN_INFO_INT_TIME),
	},
};

static void ltrf216a_reset(struct iio_dev *indio_dev)
{
	struct ltrf216a_data *data = iio_priv(indio_dev);

	/* reset sensor, chip fails to respond to this, so ignore any errors */
	regmap_write(data->regmap, LTRF216A_MAIN_CTRL, LTRF216A_ALS_RESET_MASK);

	/* reset time */
	usleep_range(1000, 2000);
}

static int ltrf216a_enable(struct iio_dev *indio_dev)
{
	struct ltrf216a_data *data = iio_priv(indio_dev);
	struct device *dev = &data->client->dev;
	int ret;

	/* enable sensor */
	ret = regmap_set_bits(data->regmap,
			      LTRF216A_MAIN_CTRL, LTRF216A_ALS_ENABLE_MASK);
	if (ret) {
		dev_err(dev, "failed to enable sensor: %d\n", ret);
		return ret;
	}

	/* sleep for one integration cycle after enabling the device */
	msleep(ltrf216a_int_time_reg[0][0]);

	return 0;
}

static int ltrf216a_disable(struct iio_dev *indio_dev)
{
	struct ltrf216a_data *data = iio_priv(indio_dev);
	struct device *dev = &data->client->dev;
	int ret;

	ret = regmap_write(data->regmap, LTRF216A_MAIN_CTRL, 0);
	if (ret)
		dev_err(dev, "failed to disable sensor: %d\n", ret);

	return ret;
}

static void ltrf216a_cleanup(void *data)
{
	struct iio_dev *indio_dev = data;

	ltrf216a_disable(indio_dev);
}

static int ltrf216a_set_int_time(struct ltrf216a_data *data, int itime)
{
	struct device *dev = &data->client->dev;
	unsigned int i;
	u8 reg_val;
	int ret;

	for (i = 0; i < ARRAY_SIZE(ltrf216a_int_time_available); i++) {
		if (ltrf216a_int_time_available[i][1] == itime)
			break;
	}
	if (i == ARRAY_SIZE(ltrf216a_int_time_available))
		return -EINVAL;

	reg_val = ltrf216a_int_time_reg[i][1];

	ret = regmap_write(data->regmap, LTRF216A_ALS_MEAS_RES, reg_val);
	if (ret) {
		dev_err(dev, "failed to set integration time: %d\n", ret);
		return ret;
	}

	data->int_time_fac = ltrf216a_int_time_reg[i][0];
	data->int_time = itime;

	return 0;
}

static int ltrf216a_get_int_time(struct ltrf216a_data *data,
				 int *val, int *val2)
{
	*val = 0;
	*val2 = data->int_time;
	return IIO_VAL_INT_PLUS_MICRO;
}

static int ltrf216a_set_power_state(struct ltrf216a_data *data, bool on)
{
	struct device *dev = &data->client->dev;
	int ret = 0;

	if (on) {
		ret = pm_runtime_resume_and_get(dev);
		if (ret) {
			dev_err(dev, "failed to resume runtime PM: %d\n", ret);
			return ret;
		}
	} else {
		pm_runtime_mark_last_busy(dev);
		pm_runtime_put_autosuspend(dev);
	}

	return ret;
}

static int ltrf216a_read_data(struct ltrf216a_data *data, u8 addr)
{
	struct device *dev = &data->client->dev;
	int ret, val;
	u8 buf[3];

	ret = regmap_read_poll_timeout(data->regmap, LTRF216A_MAIN_STATUS,
				       val, val & LTRF216A_ALS_DATA_STATUS,
				       LTRF216A_ALS_READ_DATA_DELAY_US,
				       LTRF216A_ALS_READ_DATA_DELAY_US * 50);
	if (ret) {
		dev_err(dev, "failed to wait for measurement data: %d\n", ret);
		return ret;
	}

	ret = regmap_bulk_read(data->regmap, addr, buf, sizeof(buf));
	if (ret) {
		dev_err(dev, "failed to read measurement data: %d\n", ret);
		return ret;
	}

	return get_unaligned_le24(&buf[0]);
}

static int ltrf216a_get_lux(struct ltrf216a_data *data)
{
	int ret, greendata;
	u64 lux;

	ret = ltrf216a_set_power_state(data, true);
	if (ret)
		return ret;

	greendata = ltrf216a_read_data(data, LTRF216A_ALS_DATA_0);
	if (greendata < 0)
		return greendata;

	ltrf216a_set_power_state(data, false);

	lux = greendata * data->info->lux_multiplier * LTRF216A_WIN_FAC;

	return lux;
}

static int ltrf216a_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int *val,
			     int *val2, long mask)
{
	struct ltrf216a_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = ltrf216a_set_power_state(data, true);
		if (ret)
			return ret;
		mutex_lock(&data->lock);
		ret = ltrf216a_read_data(data, LTRF216A_ALS_DATA_0);
		mutex_unlock(&data->lock);
		ltrf216a_set_power_state(data, false);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_PROCESSED:
		mutex_lock(&data->lock);
		ret = ltrf216a_get_lux(data);
		mutex_unlock(&data->lock);
		if (ret < 0)
			return ret;
		*val = ret;
		*val2 = data->als_gain_fac * data->int_time_fac;
		return IIO_VAL_FRACTIONAL;
	case IIO_CHAN_INFO_INT_TIME:
		mutex_lock(&data->lock);
		ret = ltrf216a_get_int_time(data, val, val2);
		mutex_unlock(&data->lock);
		return ret;
	default:
		return -EINVAL;
	}
}

static int ltrf216a_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan, int val,
			      int val2, long mask)
{
	struct ltrf216a_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		if (val != 0)
			return -EINVAL;
		mutex_lock(&data->lock);
		ret = ltrf216a_set_int_time(data, val2);
		mutex_unlock(&data->lock);
		return ret;
	default:
		return -EINVAL;
	}
}

static int ltrf216a_read_available(struct iio_dev *indio_dev,
				   struct iio_chan_spec const *chan,
				   const int **vals, int *type, int *length,
				   long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		*length = ARRAY_SIZE(ltrf216a_int_time_available) * 2;
		*vals = (const int *)ltrf216a_int_time_available;
		*type = IIO_VAL_INT_PLUS_MICRO;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static const struct iio_info ltrf216a_info = {
	.read_raw = ltrf216a_read_raw,
	.write_raw = ltrf216a_write_raw,
	.read_avail = ltrf216a_read_available,
};

static bool ltrf216a_readable_reg(struct device *dev, unsigned int reg)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct ltrf216a_data *data = iio_priv(indio_dev);

	switch (reg) {
	case LTRF216A_MAIN_CTRL:
	case LTRF216A_ALS_MEAS_RES:
	case LTRF216A_ALS_GAIN:
	case LTRF216A_PART_ID:
	case LTRF216A_MAIN_STATUS:
	case LTRF216A_ALS_DATA_0:
	case LTRF216A_ALS_DATA_1:
	case LTRF216A_ALS_DATA_2:
	case LTRF216A_INT_CFG:
	case LTRF216A_INT_PST:
	case LTRF216A_ALS_THRES_UP_0:
	case LTRF216A_ALS_THRES_UP_1:
	case LTRF216A_ALS_THRES_UP_2:
	case LTRF216A_ALS_THRES_LOW_0:
	case LTRF216A_ALS_THRES_LOW_1:
	case LTRF216A_ALS_THRES_LOW_2:
		return true;
	case LTRF216A_ALS_CLEAR_DATA_0:
	case LTRF216A_ALS_CLEAR_DATA_1:
	case LTRF216A_ALS_CLEAR_DATA_2:
		return data->info->has_clear_data;
	default:
		return false;
	}
}

static bool ltrf216a_writable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case LTRF216A_MAIN_CTRL:
	case LTRF216A_ALS_MEAS_RES:
	case LTRF216A_ALS_GAIN:
	case LTRF216A_INT_CFG:
	case LTRF216A_INT_PST:
	case LTRF216A_ALS_THRES_UP_0:
	case LTRF216A_ALS_THRES_UP_1:
	case LTRF216A_ALS_THRES_UP_2:
	case LTRF216A_ALS_THRES_LOW_0:
	case LTRF216A_ALS_THRES_LOW_1:
	case LTRF216A_ALS_THRES_LOW_2:
		return true;
	default:
		return false;
	}
}

static bool ltrf216a_volatile_reg(struct device *dev, unsigned int reg)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct ltrf216a_data *data = iio_priv(indio_dev);

	switch (reg) {
	case LTRF216A_MAIN_STATUS:
	case LTRF216A_ALS_DATA_0:
	case LTRF216A_ALS_DATA_1:
	case LTRF216A_ALS_DATA_2:
		return true;
	/*
	 * If these registers are not present on a chip (like LTR-308),
	 * the missing registers are not considered volatile.
	 */
	case LTRF216A_ALS_CLEAR_DATA_0:
	case LTRF216A_ALS_CLEAR_DATA_1:
	case LTRF216A_ALS_CLEAR_DATA_2:
		return data->info->has_clear_data;
	default:
		return false;
	}
}

static bool ltrf216a_precious_reg(struct device *dev, unsigned int reg)
{
	return reg == LTRF216A_MAIN_STATUS;
}

static const struct regmap_config ltrf216a_regmap_config = {
	.name = "ltrf216a",
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.max_register = LTRF216A_ALS_THRES_LOW_2,
	.readable_reg = ltrf216a_readable_reg,
	.writeable_reg = ltrf216a_writable_reg,
	.volatile_reg = ltrf216a_volatile_reg,
	.precious_reg = ltrf216a_precious_reg,
	.disable_locking = true,
};

static int ltrf216a_probe(struct i2c_client *client)
{
	struct ltrf216a_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);

	data->regmap = devm_regmap_init_i2c(client, &ltrf216a_regmap_config);
	if (IS_ERR(data->regmap))
		return dev_err_probe(&client->dev, PTR_ERR(data->regmap),
				     "regmap initialization failed\n");

	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	data->info = i2c_get_match_data(client);

	mutex_init(&data->lock);

	indio_dev->info = &ltrf216a_info;
	indio_dev->name = "ltrf216a";
	indio_dev->channels = ltrf216a_channels;
	indio_dev->num_channels = ARRAY_SIZE(ltrf216a_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = pm_runtime_set_active(&client->dev);
	if (ret)
		return ret;

	/* reset sensor, chip fails to respond to this, so ignore any errors */
	ltrf216a_reset(indio_dev);

	ret = regmap_reinit_cache(data->regmap, &ltrf216a_regmap_config);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to reinit regmap cache\n");

	ret = ltrf216a_enable(indio_dev);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(&client->dev, ltrf216a_cleanup,
				       indio_dev);
	if (ret)
		return ret;

	ret = devm_pm_runtime_enable(&client->dev);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to enable runtime PM\n");

	pm_runtime_set_autosuspend_delay(&client->dev, 1000);
	pm_runtime_use_autosuspend(&client->dev);

	data->int_time = 100000;
	data->int_time_fac = 100;
	data->als_gain_fac = 3;

	return devm_iio_device_register(&client->dev, indio_dev);
}

static int ltrf216a_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct ltrf216a_data *data = iio_priv(indio_dev);
	int ret;

	ret = ltrf216a_disable(indio_dev);
	if (ret)
		return ret;

	regcache_cache_only(data->regmap, true);

	return 0;
}

static int ltrf216a_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct ltrf216a_data *data = iio_priv(indio_dev);
	int ret;

	regcache_cache_only(data->regmap, false);
	regcache_mark_dirty(data->regmap);
	ret = regcache_sync(data->regmap);
	if (ret)
		goto cache_only;

	ret = ltrf216a_enable(indio_dev);
	if (ret)
		goto cache_only;

	return 0;

cache_only:
	regcache_cache_only(data->regmap, true);

	return ret;
}

static DEFINE_RUNTIME_DEV_PM_OPS(ltrf216a_pm_ops, ltrf216a_runtime_suspend,
				 ltrf216a_runtime_resume, NULL);

static const struct ltr_chip_info ltr308_chip_info = {
	.has_clear_data		= false,
	.lux_multiplier		= 60,
};

static const struct ltr_chip_info ltrf216a_chip_info = {
	.has_clear_data		= true,
	.lux_multiplier		= 45,
};

static const struct i2c_device_id ltrf216a_id[] = {
	{ "ltr308", .driver_data = (kernel_ulong_t)&ltr308_chip_info },
	{ "ltrf216a", .driver_data = (kernel_ulong_t)&ltrf216a_chip_info },
	{}
};
MODULE_DEVICE_TABLE(i2c, ltrf216a_id);

static const struct of_device_id ltrf216a_of_match[] = {
	{ .compatible = "liteon,ltr308", .data = &ltr308_chip_info },
	{ .compatible = "liteon,ltrf216a", .data = &ltrf216a_chip_info },
	{ .compatible = "ltr,ltrf216a", .data = &ltrf216a_chip_info },
	{}
};
MODULE_DEVICE_TABLE(of, ltrf216a_of_match);

static struct i2c_driver ltrf216a_driver = {
	.driver = {
		.name = "ltrf216a",
		.pm = pm_ptr(&ltrf216a_pm_ops),
		.of_match_table = ltrf216a_of_match,
	},
	.probe = ltrf216a_probe,
	.id_table = ltrf216a_id,
};
module_i2c_driver(ltrf216a_driver);

MODULE_AUTHOR("Shreeya Patel <shreeya.patel@collabora.com>");
MODULE_AUTHOR("Shi Zhigang <Zhigang.Shi@liteon.com>");
MODULE_DESCRIPTION("LTRF216A ambient light sensor driver");
MODULE_LICENSE("GPL");
