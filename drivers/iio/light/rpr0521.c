/*
 * RPR-0521 ROHM Ambient Light and Proximity Sensor
 *
 * Copyright (c) 2015, Intel Corporation.
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * IIO driver for RPR-0521RS (7-bit I2C slave address 0x38).
 *
 * TODO: illuminance channel, buffer
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/acpi.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/pm_runtime.h>

#define RPR0521_REG_SYSTEM_CTRL		0x40
#define RPR0521_REG_MODE_CTRL		0x41
#define RPR0521_REG_ALS_CTRL		0x42
#define RPR0521_REG_PXS_CTRL		0x43
#define RPR0521_REG_PXS_DATA		0x44 /* 16-bit, little endian */
#define RPR0521_REG_ALS_DATA0		0x46 /* 16-bit, little endian */
#define RPR0521_REG_ALS_DATA1		0x48 /* 16-bit, little endian */
#define RPR0521_REG_PS_OFFSET_LSB	0x53
#define RPR0521_REG_ID			0x92

#define RPR0521_MODE_ALS_MASK		BIT(7)
#define RPR0521_MODE_PXS_MASK		BIT(6)
#define RPR0521_MODE_MEAS_TIME_MASK	GENMASK(3, 0)
#define RPR0521_ALS_DATA0_GAIN_MASK	GENMASK(5, 4)
#define RPR0521_ALS_DATA0_GAIN_SHIFT	4
#define RPR0521_ALS_DATA1_GAIN_MASK	GENMASK(3, 2)
#define RPR0521_ALS_DATA1_GAIN_SHIFT	2
#define RPR0521_PXS_GAIN_MASK		GENMASK(5, 4)
#define RPR0521_PXS_GAIN_SHIFT		4

#define RPR0521_MODE_ALS_ENABLE		BIT(7)
#define RPR0521_MODE_ALS_DISABLE	0x00
#define RPR0521_MODE_PXS_ENABLE		BIT(6)
#define RPR0521_MODE_PXS_DISABLE	0x00

#define RPR0521_MANUFACT_ID		0xE0
#define RPR0521_DEFAULT_MEAS_TIME	0x06 /* ALS - 100ms, PXS - 100ms */

#define RPR0521_DRV_NAME		"RPR0521"
#define RPR0521_REGMAP_NAME		"rpr0521_regmap"

#define RPR0521_SLEEP_DELAY_MS	2000

#define RPR0521_ALS_SCALE_AVAIL "0.007812 0.015625 0.5 1"
#define RPR0521_PXS_SCALE_AVAIL "0.125 0.5 1"

struct rpr0521_gain {
	int scale;
	int uscale;
};

static const struct rpr0521_gain rpr0521_als_gain[4] = {
	{1, 0},		/* x1 */
	{0, 500000},	/* x2 */
	{0, 15625},	/* x64 */
	{0, 7812},	/* x128 */
};

static const struct rpr0521_gain rpr0521_pxs_gain[3] = {
	{1, 0},		/* x1 */
	{0, 500000},	/* x2 */
	{0, 125000},	/* x4 */
};

enum rpr0521_channel {
	RPR0521_CHAN_PXS,
	RPR0521_CHAN_ALS_DATA0,
	RPR0521_CHAN_ALS_DATA1,
};

struct rpr0521_reg_desc {
	u8 address;
	u8 device_mask;
};

static const struct rpr0521_reg_desc rpr0521_data_reg[] = {
	[RPR0521_CHAN_PXS]	= {
		.address	= RPR0521_REG_PXS_DATA,
		.device_mask	= RPR0521_MODE_PXS_MASK,
	},
	[RPR0521_CHAN_ALS_DATA0] = {
		.address	= RPR0521_REG_ALS_DATA0,
		.device_mask	= RPR0521_MODE_ALS_MASK,
	},
	[RPR0521_CHAN_ALS_DATA1] = {
		.address	= RPR0521_REG_ALS_DATA1,
		.device_mask	= RPR0521_MODE_ALS_MASK,
	},
};

static const struct rpr0521_gain_info {
	u8 reg;
	u8 mask;
	u8 shift;
	const struct rpr0521_gain *gain;
	int size;
} rpr0521_gain[] = {
	[RPR0521_CHAN_PXS] = {
		.reg	= RPR0521_REG_PXS_CTRL,
		.mask	= RPR0521_PXS_GAIN_MASK,
		.shift	= RPR0521_PXS_GAIN_SHIFT,
		.gain	= rpr0521_pxs_gain,
		.size	= ARRAY_SIZE(rpr0521_pxs_gain),
	},
	[RPR0521_CHAN_ALS_DATA0] = {
		.reg	= RPR0521_REG_ALS_CTRL,
		.mask	= RPR0521_ALS_DATA0_GAIN_MASK,
		.shift	= RPR0521_ALS_DATA0_GAIN_SHIFT,
		.gain	= rpr0521_als_gain,
		.size	= ARRAY_SIZE(rpr0521_als_gain),
	},
	[RPR0521_CHAN_ALS_DATA1] = {
		.reg	= RPR0521_REG_ALS_CTRL,
		.mask	= RPR0521_ALS_DATA1_GAIN_MASK,
		.shift	= RPR0521_ALS_DATA1_GAIN_SHIFT,
		.gain	= rpr0521_als_gain,
		.size	= ARRAY_SIZE(rpr0521_als_gain),
	},
};

struct rpr0521_samp_freq {
	int	als_hz;
	int	als_uhz;
	int	pxs_hz;
	int	pxs_uhz;
};

static const struct rpr0521_samp_freq rpr0521_samp_freq_i[13] = {
/*	{ALS, PXS},		   W==currently writable option */
	{0, 0, 0, 0},		/* W0000, 0=standby */
	{0, 0, 100, 0},		/*  0001 */
	{0, 0, 25, 0},		/*  0010 */
	{0, 0, 10, 0},		/*  0011 */
	{0, 0, 2, 500000},	/*  0100 */
	{10, 0, 20, 0},		/*  0101 */
	{10, 0, 10, 0},		/* W0110 */
	{10, 0, 2, 500000},	/*  0111 */
	{2, 500000, 20, 0},	/*  1000, measurement 100ms, sleep 300ms */
	{2, 500000, 10, 0},	/*  1001, measurement 100ms, sleep 300ms */
	{2, 500000, 0, 0},	/*  1010, high sensitivity mode */
	{2, 500000, 2, 500000},	/* W1011, high sensitivity mode */
	{20, 0, 20, 0}	/* 1100, ALS_data x 0.5, see specification P.18 */
};

struct rpr0521_data {
	struct i2c_client *client;

	/* protect device params updates (e.g state, gain) */
	struct mutex lock;

	/* device active status */
	bool als_dev_en;
	bool pxs_dev_en;

	/* optimize runtime pm ops - enable/disable device only if needed */
	bool als_ps_need_en;
	bool pxs_ps_need_en;
	bool als_need_dis;
	bool pxs_need_dis;

	struct regmap *regmap;
};

static IIO_CONST_ATTR(in_intensity_scale_available, RPR0521_ALS_SCALE_AVAIL);
static IIO_CONST_ATTR(in_proximity_scale_available, RPR0521_PXS_SCALE_AVAIL);

/*
 * Start with easy freq first, whole table of freq combinations is more
 * complicated.
 */
static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("2.5 10");

static struct attribute *rpr0521_attributes[] = {
	&iio_const_attr_in_intensity_scale_available.dev_attr.attr,
	&iio_const_attr_in_proximity_scale_available.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group rpr0521_attribute_group = {
	.attrs = rpr0521_attributes,
};

static const struct iio_chan_spec rpr0521_channels[] = {
	{
		.type = IIO_PROXIMITY,
		.address = RPR0521_CHAN_PXS,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_OFFSET) |
			BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	},
	{
		.type = IIO_INTENSITY,
		.modified = 1,
		.address = RPR0521_CHAN_ALS_DATA0,
		.channel2 = IIO_MOD_LIGHT_BOTH,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	},
	{
		.type = IIO_INTENSITY,
		.modified = 1,
		.address = RPR0521_CHAN_ALS_DATA1,
		.channel2 = IIO_MOD_LIGHT_IR,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	},
};

static int rpr0521_als_enable(struct rpr0521_data *data, u8 status)
{
	int ret;

	ret = regmap_update_bits(data->regmap, RPR0521_REG_MODE_CTRL,
				 RPR0521_MODE_ALS_MASK,
				 status);
	if (ret < 0)
		return ret;

	if (status & RPR0521_MODE_ALS_MASK)
		data->als_dev_en = true;
	else
		data->als_dev_en = false;

	return 0;
}

static int rpr0521_pxs_enable(struct rpr0521_data *data, u8 status)
{
	int ret;

	ret = regmap_update_bits(data->regmap, RPR0521_REG_MODE_CTRL,
				 RPR0521_MODE_PXS_MASK,
				 status);
	if (ret < 0)
		return ret;

	if (status & RPR0521_MODE_PXS_MASK)
		data->pxs_dev_en = true;
	else
		data->pxs_dev_en = false;

	return 0;
}

/**
 * rpr0521_set_power_state - handles runtime PM state and sensors enabled status
 *
 * @data: rpr0521 device private data
 * @on: state to be set for devices in @device_mask
 * @device_mask: bitmask specifying for which device we need to update @on state
 *
 * Calls for this function must be balanced so that each ON should have matching
 * OFF. Otherwise pm usage_count gets out of sync.
 */
static int rpr0521_set_power_state(struct rpr0521_data *data, bool on,
				   u8 device_mask)
{
#ifdef CONFIG_PM
	int ret;

	if (device_mask & RPR0521_MODE_ALS_MASK) {
		data->als_ps_need_en = on;
		data->als_need_dis = !on;
	}

	if (device_mask & RPR0521_MODE_PXS_MASK) {
		data->pxs_ps_need_en = on;
		data->pxs_need_dis = !on;
	}

	/*
	 * On: _resume() is called only when we are suspended
	 * Off: _suspend() is called after delay if _resume() is not
	 * called before that.
	 * Note: If either measurement is re-enabled before _suspend(),
	 * both stay enabled until _suspend().
	 */
	if (on) {
		ret = pm_runtime_get_sync(&data->client->dev);
	} else {
		pm_runtime_mark_last_busy(&data->client->dev);
		ret = pm_runtime_put_autosuspend(&data->client->dev);
	}
	if (ret < 0) {
		dev_err(&data->client->dev,
			"Failed: rpr0521_set_power_state for %d, ret %d\n",
			on, ret);
		if (on)
			pm_runtime_put_noidle(&data->client->dev);

		return ret;
	}

	if (on) {
		/* If _resume() was not called, enable measurement now. */
		if (data->als_ps_need_en) {
			ret = rpr0521_als_enable(data, RPR0521_MODE_ALS_ENABLE);
			if (ret)
				return ret;
			data->als_ps_need_en = false;
		}

		if (data->pxs_ps_need_en) {
			ret = rpr0521_pxs_enable(data, RPR0521_MODE_PXS_ENABLE);
			if (ret)
				return ret;
			data->pxs_ps_need_en = false;
		}
	}
#endif
	return 0;
}

static int rpr0521_get_gain(struct rpr0521_data *data, int chan,
			    int *val, int *val2)
{
	int ret, reg, idx;

	ret = regmap_read(data->regmap, rpr0521_gain[chan].reg, &reg);
	if (ret < 0)
		return ret;

	idx = (rpr0521_gain[chan].mask & reg) >> rpr0521_gain[chan].shift;
	*val = rpr0521_gain[chan].gain[idx].scale;
	*val2 = rpr0521_gain[chan].gain[idx].uscale;

	return 0;
}

static int rpr0521_set_gain(struct rpr0521_data *data, int chan,
			    int val, int val2)
{
	int i, idx = -EINVAL;

	/* get gain index */
	for (i = 0; i < rpr0521_gain[chan].size; i++)
		if (val == rpr0521_gain[chan].gain[i].scale &&
		    val2 == rpr0521_gain[chan].gain[i].uscale) {
			idx = i;
			break;
		}

	if (idx < 0)
		return idx;

	return regmap_update_bits(data->regmap, rpr0521_gain[chan].reg,
				  rpr0521_gain[chan].mask,
				  idx << rpr0521_gain[chan].shift);
}

static int rpr0521_read_samp_freq(struct rpr0521_data *data,
				enum iio_chan_type chan_type,
			    int *val, int *val2)
{
	int reg, ret;

	ret = regmap_read(data->regmap, RPR0521_REG_MODE_CTRL, &reg);
	if (ret < 0)
		return ret;

	reg &= RPR0521_MODE_MEAS_TIME_MASK;
	if (reg >= ARRAY_SIZE(rpr0521_samp_freq_i))
		return -EINVAL;

	switch (chan_type) {
	case IIO_INTENSITY:
		*val = rpr0521_samp_freq_i[reg].als_hz;
		*val2 = rpr0521_samp_freq_i[reg].als_uhz;
		return 0;

	case IIO_PROXIMITY:
		*val = rpr0521_samp_freq_i[reg].pxs_hz;
		*val2 = rpr0521_samp_freq_i[reg].pxs_uhz;
		return 0;

	default:
		return -EINVAL;
	}
}

static int rpr0521_write_samp_freq_common(struct rpr0521_data *data,
				enum iio_chan_type chan_type,
				int val, int val2)
{
	int i;

	/*
	 * Ignore channel
	 * both pxs and als are setup only to same freq because of simplicity
	 */
	switch (val) {
	case 0:
		i = 0;
		break;

	case 2:
		if (val2 != 500000)
			return -EINVAL;

		i = 11;
		break;

	case 10:
		i = 6;
		break;

	default:
		return -EINVAL;
	}

	return regmap_update_bits(data->regmap,
		RPR0521_REG_MODE_CTRL,
		RPR0521_MODE_MEAS_TIME_MASK,
		i);
}

static int rpr0521_read_ps_offset(struct rpr0521_data *data, int *offset)
{
	int ret;
	__le16 buffer;

	ret = regmap_bulk_read(data->regmap,
		RPR0521_REG_PS_OFFSET_LSB, &buffer, sizeof(buffer));

	if (ret < 0) {
		dev_err(&data->client->dev, "Failed to read PS OFFSET register\n");
		return ret;
	}
	*offset = le16_to_cpu(buffer);

	return ret;
}

static int rpr0521_write_ps_offset(struct rpr0521_data *data, int offset)
{
	int ret;
	__le16 buffer;

	buffer = cpu_to_le16(offset & 0x3ff);
	ret = regmap_raw_write(data->regmap,
		RPR0521_REG_PS_OFFSET_LSB, &buffer, sizeof(buffer));

	if (ret < 0) {
		dev_err(&data->client->dev, "Failed to write PS OFFSET register\n");
		return ret;
	}

	return ret;
}

static int rpr0521_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	struct rpr0521_data *data = iio_priv(indio_dev);
	int ret;
	u8 device_mask;
	__le16 raw_data;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type != IIO_INTENSITY && chan->type != IIO_PROXIMITY)
			return -EINVAL;

		device_mask = rpr0521_data_reg[chan->address].device_mask;

		mutex_lock(&data->lock);
		ret = rpr0521_set_power_state(data, true, device_mask);
		if (ret < 0) {
			mutex_unlock(&data->lock);
			return ret;
		}

		ret = regmap_bulk_read(data->regmap,
				       rpr0521_data_reg[chan->address].address,
				       &raw_data, sizeof(raw_data));
		if (ret < 0) {
			rpr0521_set_power_state(data, false, device_mask);
			mutex_unlock(&data->lock);
			return ret;
		}

		ret = rpr0521_set_power_state(data, false, device_mask);
		mutex_unlock(&data->lock);
		if (ret < 0)
			return ret;

		*val = le16_to_cpu(raw_data);

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		mutex_lock(&data->lock);
		ret = rpr0521_get_gain(data, chan->address, val, val2);
		mutex_unlock(&data->lock);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT_PLUS_MICRO;

	case IIO_CHAN_INFO_SAMP_FREQ:
		mutex_lock(&data->lock);
		ret = rpr0521_read_samp_freq(data, chan->type, val, val2);
		mutex_unlock(&data->lock);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT_PLUS_MICRO;

	case IIO_CHAN_INFO_OFFSET:
		mutex_lock(&data->lock);
		ret = rpr0521_read_ps_offset(data, val);
		mutex_unlock(&data->lock);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int rpr0521_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int val,
			     int val2, long mask)
{
	struct rpr0521_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		mutex_lock(&data->lock);
		ret = rpr0521_set_gain(data, chan->address, val, val2);
		mutex_unlock(&data->lock);

		return ret;

	case IIO_CHAN_INFO_SAMP_FREQ:
		mutex_lock(&data->lock);
		ret = rpr0521_write_samp_freq_common(data, chan->type,
						     val, val2);
		mutex_unlock(&data->lock);

		return ret;

	case IIO_CHAN_INFO_OFFSET:
		mutex_lock(&data->lock);
		ret = rpr0521_write_ps_offset(data, val);
		mutex_unlock(&data->lock);

		return ret;

	default:
		return -EINVAL;
	}
}

static const struct iio_info rpr0521_info = {
	.driver_module	= THIS_MODULE,
	.read_raw	= rpr0521_read_raw,
	.write_raw	= rpr0521_write_raw,
	.attrs		= &rpr0521_attribute_group,
};

static int rpr0521_init(struct rpr0521_data *data)
{
	int ret;
	int id;

	ret = regmap_read(data->regmap, RPR0521_REG_ID, &id);
	if (ret < 0) {
		dev_err(&data->client->dev, "Failed to read REG_ID register\n");
		return ret;
	}

	if (id != RPR0521_MANUFACT_ID) {
		dev_err(&data->client->dev, "Wrong id, got %x, expected %x\n",
			id, RPR0521_MANUFACT_ID);
		return -ENODEV;
	}

	/* set default measurement time - 100 ms for both ALS and PS */
	ret = regmap_update_bits(data->regmap, RPR0521_REG_MODE_CTRL,
				 RPR0521_MODE_MEAS_TIME_MASK,
				 RPR0521_DEFAULT_MEAS_TIME);
	if (ret) {
		pr_err("regmap_update_bits returned %d\n", ret);
		return ret;
	}

#ifndef CONFIG_PM
	ret = rpr0521_als_enable(data, RPR0521_MODE_ALS_ENABLE);
	if (ret < 0)
		return ret;
	ret = rpr0521_pxs_enable(data, RPR0521_MODE_PXS_ENABLE);
	if (ret < 0)
		return ret;
#endif

	return 0;
}

static int rpr0521_poweroff(struct rpr0521_data *data)
{
	int ret;

	ret = regmap_update_bits(data->regmap, RPR0521_REG_MODE_CTRL,
				 RPR0521_MODE_ALS_MASK |
				 RPR0521_MODE_PXS_MASK,
				 RPR0521_MODE_ALS_DISABLE |
				 RPR0521_MODE_PXS_DISABLE);
	if (ret < 0)
		return ret;

	data->als_dev_en = false;
	data->pxs_dev_en = false;

	return 0;
}

static bool rpr0521_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RPR0521_REG_MODE_CTRL:
	case RPR0521_REG_ALS_CTRL:
	case RPR0521_REG_PXS_CTRL:
		return false;
	default:
		return true;
	}
}

static const struct regmap_config rpr0521_regmap_config = {
	.name		= RPR0521_REGMAP_NAME,

	.reg_bits	= 8,
	.val_bits	= 8,

	.max_register	= RPR0521_REG_ID,
	.cache_type	= REGCACHE_RBTREE,
	.volatile_reg	= rpr0521_is_volatile_reg,
};

static int rpr0521_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct rpr0521_data *data;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &rpr0521_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "regmap_init failed!\n");
		return PTR_ERR(regmap);
	}

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	data->regmap = regmap;

	mutex_init(&data->lock);

	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &rpr0521_info;
	indio_dev->name = RPR0521_DRV_NAME;
	indio_dev->channels = rpr0521_channels;
	indio_dev->num_channels = ARRAY_SIZE(rpr0521_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = rpr0521_init(data);
	if (ret < 0) {
		dev_err(&client->dev, "rpr0521 chip init failed\n");
		return ret;
	}

	ret = pm_runtime_set_active(&client->dev);
	if (ret < 0)
		goto err_poweroff;

	pm_runtime_enable(&client->dev);
	pm_runtime_set_autosuspend_delay(&client->dev, RPR0521_SLEEP_DELAY_MS);
	pm_runtime_use_autosuspend(&client->dev);

	ret = iio_device_register(indio_dev);
	if (ret)
		goto err_pm_disable;

	return 0;

err_pm_disable:
	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	pm_runtime_put_noidle(&client->dev);
err_poweroff:
	rpr0521_poweroff(data);

	return ret;
}

static int rpr0521_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	pm_runtime_put_noidle(&client->dev);

	rpr0521_poweroff(iio_priv(indio_dev));

	return 0;
}

#ifdef CONFIG_PM
static int rpr0521_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct rpr0521_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->lock);
	/* If measurements are enabled, enable them on resume */
	if (!data->als_need_dis)
		data->als_ps_need_en = data->als_dev_en;
	if (!data->pxs_need_dis)
		data->pxs_ps_need_en = data->pxs_dev_en;

	/* disable channels and sets {als,pxs}_dev_en to false */
	ret = rpr0521_poweroff(data);
	regcache_mark_dirty(data->regmap);
	mutex_unlock(&data->lock);

	return ret;
}

static int rpr0521_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct rpr0521_data *data = iio_priv(indio_dev);
	int ret;

	regcache_sync(data->regmap);
	if (data->als_ps_need_en) {
		ret = rpr0521_als_enable(data, RPR0521_MODE_ALS_ENABLE);
		if (ret < 0)
			return ret;
		data->als_ps_need_en = false;
	}

	if (data->pxs_ps_need_en) {
		ret = rpr0521_pxs_enable(data, RPR0521_MODE_PXS_ENABLE);
		if (ret < 0)
			return ret;
		data->pxs_ps_need_en = false;
	}
	msleep(100);	//wait for first measurement result

	return 0;
}
#endif

static const struct dev_pm_ops rpr0521_pm_ops = {
	SET_RUNTIME_PM_OPS(rpr0521_runtime_suspend,
			   rpr0521_runtime_resume, NULL)
};

static const struct acpi_device_id rpr0521_acpi_match[] = {
	{"RPR0521", 0},
	{ }
};
MODULE_DEVICE_TABLE(acpi, rpr0521_acpi_match);

static const struct i2c_device_id rpr0521_id[] = {
	{"rpr0521", 0},
	{ }
};

MODULE_DEVICE_TABLE(i2c, rpr0521_id);

static struct i2c_driver rpr0521_driver = {
	.driver = {
		.name	= RPR0521_DRV_NAME,
		.pm	= &rpr0521_pm_ops,
		.acpi_match_table = ACPI_PTR(rpr0521_acpi_match),
	},
	.probe		= rpr0521_probe,
	.remove		= rpr0521_remove,
	.id_table	= rpr0521_id,
};

module_i2c_driver(rpr0521_driver);

MODULE_AUTHOR("Daniel Baluta <daniel.baluta@intel.com>");
MODULE_DESCRIPTION("RPR0521 ROHM Ambient Light and Proximity Sensor driver");
MODULE_LICENSE("GPL v2");
