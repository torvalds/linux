// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 Intel Corporation
 *
 * Driver for TXC PA12203001 Proximity and Ambient Light Sensor.
 *
 * To do: Interrupt support.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#define PA12203001_DRIVER_NAME	"pa12203001"

#define PA12203001_REG_CFG0		0x00
#define PA12203001_REG_CFG1		0x01
#define PA12203001_REG_CFG2		0x02
#define PA12203001_REG_CFG3		0x03

#define PA12203001_REG_ADL		0x0b
#define PA12203001_REG_PDH		0x0e

#define PA12203001_REG_POFS		0x10
#define PA12203001_REG_PSET		0x11

#define PA12203001_ALS_EN_MASK		BIT(0)
#define PA12203001_PX_EN_MASK		BIT(1)
#define PA12203001_PX_NORMAL_MODE_MASK		GENMASK(7, 6)
#define PA12203001_AFSR_MASK		GENMASK(5, 4)
#define PA12203001_AFSR_SHIFT		4

#define PA12203001_PSCAN			0x03

/* als range 31000, ps, als disabled */
#define PA12203001_REG_CFG0_DEFAULT		0x30

/* led current: 100 mA */
#define PA12203001_REG_CFG1_DEFAULT		0x20

/* ps mode: normal, interrupts not active */
#define PA12203001_REG_CFG2_DEFAULT		0xcc

#define PA12203001_REG_CFG3_DEFAULT		0x00

#define PA12203001_SLEEP_DELAY_MS		3000

#define PA12203001_CHIP_ENABLE		0xff
#define PA12203001_CHIP_DISABLE		0x00

/* available scales: corresponding to [500, 4000, 7000, 31000]  lux */
static const int pa12203001_scales[] = { 7629, 61036, 106813, 473029};

struct pa12203001_data {
	struct i2c_client *client;

	/* protect device states */
	struct mutex lock;

	bool als_enabled;
	bool px_enabled;
	bool als_needs_enable;
	bool px_needs_enable;

	struct regmap *map;
};

static const struct {
	u8 reg;
	u8 val;
} regvals[] = {
	{PA12203001_REG_CFG0, PA12203001_REG_CFG0_DEFAULT},
	{PA12203001_REG_CFG1, PA12203001_REG_CFG1_DEFAULT},
	{PA12203001_REG_CFG2, PA12203001_REG_CFG2_DEFAULT},
	{PA12203001_REG_CFG3, PA12203001_REG_CFG3_DEFAULT},
	{PA12203001_REG_PSET, PA12203001_PSCAN},
};

static IIO_CONST_ATTR(in_illuminance_scale_available,
		      "0.007629 0.061036 0.106813 0.473029");

static struct attribute *pa12203001_attrs[] = {
	&iio_const_attr_in_illuminance_scale_available.dev_attr.attr,
	NULL
};

static const struct attribute_group pa12203001_attr_group = {
	.attrs = pa12203001_attrs,
};

static const struct iio_chan_spec pa12203001_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
	},
	{
		.type = IIO_PROXIMITY,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	}
};

static const struct regmap_range pa12203001_volatile_regs_ranges[] = {
	regmap_reg_range(PA12203001_REG_ADL, PA12203001_REG_ADL + 1),
	regmap_reg_range(PA12203001_REG_PDH, PA12203001_REG_PDH),
};

static const struct regmap_access_table pa12203001_volatile_regs = {
	.yes_ranges = pa12203001_volatile_regs_ranges,
	.n_yes_ranges = ARRAY_SIZE(pa12203001_volatile_regs_ranges),
};

static const struct regmap_config pa12203001_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = PA12203001_REG_PSET,
	.cache_type = REGCACHE_RBTREE,
	.volatile_table = &pa12203001_volatile_regs,
};

static inline int pa12203001_als_enable(struct pa12203001_data *data, u8 enable)
{
	int ret;

	ret = regmap_update_bits(data->map, PA12203001_REG_CFG0,
				 PA12203001_ALS_EN_MASK, enable);
	if (ret < 0)
		return ret;

	data->als_enabled = !!enable;

	return 0;
}

static inline int pa12203001_px_enable(struct pa12203001_data *data, u8 enable)
{
	int ret;

	ret = regmap_update_bits(data->map, PA12203001_REG_CFG0,
				 PA12203001_PX_EN_MASK, enable);
	if (ret < 0)
		return ret;

	data->px_enabled = !!enable;

	return 0;
}

static int pa12203001_set_power_state(struct pa12203001_data *data, bool on,
				      u8 mask)
{
#ifdef CONFIG_PM
	int ret;

	if (on && (mask & PA12203001_ALS_EN_MASK)) {
		mutex_lock(&data->lock);
		if (data->px_enabled) {
			ret = pa12203001_als_enable(data,
						    PA12203001_ALS_EN_MASK);
			if (ret < 0)
				goto err;
		} else {
			data->als_needs_enable = true;
		}
		mutex_unlock(&data->lock);
	}

	if (on && (mask & PA12203001_PX_EN_MASK)) {
		mutex_lock(&data->lock);
		if (data->als_enabled) {
			ret = pa12203001_px_enable(data, PA12203001_PX_EN_MASK);
			if (ret < 0)
				goto err;
		} else {
			data->px_needs_enable = true;
		}
		mutex_unlock(&data->lock);
	}

	if (on) {
		ret = pm_runtime_resume_and_get(&data->client->dev);

	} else {
		pm_runtime_mark_last_busy(&data->client->dev);
		ret = pm_runtime_put_autosuspend(&data->client->dev);
	}

	return ret;

err:
	mutex_unlock(&data->lock);
	return ret;

#endif
	return 0;
}

static int pa12203001_read_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan, int *val,
			       int *val2, long mask)
{
	struct pa12203001_data *data = iio_priv(indio_dev);
	int ret;
	u8 dev_mask;
	unsigned int reg_byte;
	__le16 reg_word;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_LIGHT:
			dev_mask = PA12203001_ALS_EN_MASK;
			ret = pa12203001_set_power_state(data, true, dev_mask);
			if (ret < 0)
				return ret;
			/*
			 * ALS ADC value is stored in registers
			 * PA12203001_REG_ADL and in PA12203001_REG_ADL + 1.
			 */
			ret = regmap_bulk_read(data->map, PA12203001_REG_ADL,
					       &reg_word, 2);
			if (ret < 0)
				goto reg_err;

			*val = le16_to_cpu(reg_word);
			ret = pa12203001_set_power_state(data, false, dev_mask);
			if (ret < 0)
				return ret;
			break;
		case IIO_PROXIMITY:
			dev_mask = PA12203001_PX_EN_MASK;
			ret = pa12203001_set_power_state(data, true, dev_mask);
			if (ret < 0)
				return ret;
			ret = regmap_read(data->map, PA12203001_REG_PDH,
					  &reg_byte);
			if (ret < 0)
				goto reg_err;

			*val = reg_byte;
			ret = pa12203001_set_power_state(data, false, dev_mask);
			if (ret < 0)
				return ret;
			break;
		default:
			return -EINVAL;
		}
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		ret = regmap_read(data->map, PA12203001_REG_CFG0, &reg_byte);
		if (ret < 0)
			return ret;
		*val = 0;
		reg_byte = (reg_byte & PA12203001_AFSR_MASK);
		*val2 = pa12203001_scales[reg_byte >> 4];
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}

reg_err:
	pa12203001_set_power_state(data, false, dev_mask);
	return ret;
}

static int pa12203001_write_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan, int val,
				int val2, long mask)
{
	struct pa12203001_data *data = iio_priv(indio_dev);
	int i, ret, new_val;
	unsigned int reg_byte;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		ret = regmap_read(data->map, PA12203001_REG_CFG0, &reg_byte);
		if (val != 0 || ret < 0)
			return -EINVAL;
		for (i = 0; i < ARRAY_SIZE(pa12203001_scales); i++) {
			if (val2 == pa12203001_scales[i]) {
				new_val = i << PA12203001_AFSR_SHIFT;
				return regmap_update_bits(data->map,
							  PA12203001_REG_CFG0,
							  PA12203001_AFSR_MASK,
							  new_val);
			}
		}
		break;
	default:
		break;
	}

	return -EINVAL;
}

static const struct iio_info pa12203001_info = {
	.read_raw = pa12203001_read_raw,
	.write_raw = pa12203001_write_raw,
	.attrs = &pa12203001_attr_group,
};

static int pa12203001_init(struct iio_dev *indio_dev)
{
	struct pa12203001_data *data = iio_priv(indio_dev);
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(regvals); i++) {
		ret = regmap_write(data->map, regvals[i].reg, regvals[i].val);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int pa12203001_power_chip(struct iio_dev *indio_dev, u8 state)
{
	struct pa12203001_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->lock);
	ret = pa12203001_als_enable(data, state);
	if (ret < 0)
		goto out;

	ret = pa12203001_px_enable(data, state);

out:
	mutex_unlock(&data->lock);
	return ret;
}

static int pa12203001_probe(struct i2c_client *client)
{
	struct pa12203001_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev,
					  sizeof(struct pa12203001_data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	data->map = devm_regmap_init_i2c(client, &pa12203001_regmap_config);
	if (IS_ERR(data->map))
		return PTR_ERR(data->map);

	mutex_init(&data->lock);

	indio_dev->info = &pa12203001_info;
	indio_dev->name = PA12203001_DRIVER_NAME;
	indio_dev->channels = pa12203001_channels;
	indio_dev->num_channels = ARRAY_SIZE(pa12203001_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = pa12203001_init(indio_dev);
	if (ret < 0)
		return ret;

	ret = pa12203001_power_chip(indio_dev, PA12203001_CHIP_ENABLE);
	if (ret < 0)
		return ret;

	ret = pm_runtime_set_active(&client->dev);
	if (ret < 0)
		goto out_err;

	pm_runtime_enable(&client->dev);
	pm_runtime_set_autosuspend_delay(&client->dev,
					 PA12203001_SLEEP_DELAY_MS);
	pm_runtime_use_autosuspend(&client->dev);

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto out_err;

	return 0;

out_err:
	pa12203001_power_chip(indio_dev, PA12203001_CHIP_DISABLE);
	return ret;
}

static void pa12203001_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	int ret;

	iio_device_unregister(indio_dev);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	ret = pa12203001_power_chip(indio_dev, PA12203001_CHIP_DISABLE);
	if (ret)
		dev_warn(&client->dev, "Failed to power down (%pe)\n",
			 ERR_PTR(ret));
}

#if defined(CONFIG_PM_SLEEP) || defined(CONFIG_PM)
static int pa12203001_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));

	return pa12203001_power_chip(indio_dev, PA12203001_CHIP_DISABLE);
}
#endif

#ifdef CONFIG_PM_SLEEP
static int pa12203001_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));

	return pa12203001_power_chip(indio_dev, PA12203001_CHIP_ENABLE);
}
#endif

#ifdef CONFIG_PM
static int pa12203001_runtime_resume(struct device *dev)
{
	struct pa12203001_data *data;

	data = iio_priv(i2c_get_clientdata(to_i2c_client(dev)));

	mutex_lock(&data->lock);
	if (data->als_needs_enable) {
		pa12203001_als_enable(data, PA12203001_ALS_EN_MASK);
		data->als_needs_enable = false;
	}
	if (data->px_needs_enable) {
		pa12203001_px_enable(data, PA12203001_PX_EN_MASK);
		data->px_needs_enable = false;
	}
	mutex_unlock(&data->lock);

	return 0;
}
#endif

static const struct dev_pm_ops pa12203001_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pa12203001_suspend, pa12203001_resume)
	SET_RUNTIME_PM_OPS(pa12203001_suspend, pa12203001_runtime_resume, NULL)
};

static const struct acpi_device_id pa12203001_acpi_match[] = {
	{ "TXCPA122", 0 },
	{}
};

MODULE_DEVICE_TABLE(acpi, pa12203001_acpi_match);

static const struct i2c_device_id pa12203001_id[] = {
		{ "txcpa122", 0 },
		{}
};

MODULE_DEVICE_TABLE(i2c, pa12203001_id);

static struct i2c_driver pa12203001_driver = {
	.driver = {
		.name = PA12203001_DRIVER_NAME,
		.pm = &pa12203001_pm_ops,
		.acpi_match_table = ACPI_PTR(pa12203001_acpi_match),
	},
	.probe_new = pa12203001_probe,
	.remove = pa12203001_remove,
	.id_table = pa12203001_id,

};
module_i2c_driver(pa12203001_driver);

MODULE_AUTHOR("Adriana Reus <adriana.reus@intel.com>");
MODULE_DESCRIPTION("Driver for TXC PA12203001 Proximity and Light Sensor");
MODULE_LICENSE("GPL v2");
