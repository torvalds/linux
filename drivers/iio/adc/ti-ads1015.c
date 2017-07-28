/*
 * ADS1015 - Texas Instruments Analog-to-Digital Converter
 *
 * Copyright (c) 2016, Intel Corporation.
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * IIO driver for ADS1015 ADC 7-bit I2C slave address:
 *	* 0x48 - ADDR connected to Ground
 *	* 0x49 - ADDR connected to Vdd
 *	* 0x4A - ADDR connected to SDA
 *	* 0x4B - ADDR connected to SCL
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>
#include <linux/mutex.h>
#include <linux/delay.h>

#include <linux/platform_data/ads1015.h>

#include <linux/iio/iio.h>
#include <linux/iio/types.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#define ADS1015_DRV_NAME "ads1015"

#define ADS1015_CONV_REG	0x00
#define ADS1015_CFG_REG		0x01

#define ADS1015_CFG_DR_SHIFT	5
#define ADS1015_CFG_MOD_SHIFT	8
#define ADS1015_CFG_PGA_SHIFT	9
#define ADS1015_CFG_MUX_SHIFT	12

#define ADS1015_CFG_DR_MASK	GENMASK(7, 5)
#define ADS1015_CFG_MOD_MASK	BIT(8)
#define ADS1015_CFG_PGA_MASK	GENMASK(11, 9)
#define ADS1015_CFG_MUX_MASK	GENMASK(14, 12)

/* device operating modes */
#define ADS1015_CONTINUOUS	0
#define ADS1015_SINGLESHOT	1

#define ADS1015_SLEEP_DELAY_MS		2000
#define ADS1015_DEFAULT_PGA		2
#define ADS1015_DEFAULT_DATA_RATE	4
#define ADS1015_DEFAULT_CHAN		0

enum chip_ids {
	ADS1015,
	ADS1115,
};

enum ads1015_channels {
	ADS1015_AIN0_AIN1 = 0,
	ADS1015_AIN0_AIN3,
	ADS1015_AIN1_AIN3,
	ADS1015_AIN2_AIN3,
	ADS1015_AIN0,
	ADS1015_AIN1,
	ADS1015_AIN2,
	ADS1015_AIN3,
	ADS1015_TIMESTAMP,
};

static const unsigned int ads1015_data_rate[] = {
	128, 250, 490, 920, 1600, 2400, 3300, 3300
};

static const unsigned int ads1115_data_rate[] = {
	8, 16, 32, 64, 128, 250, 475, 860
};

static const struct {
	int scale;
	int uscale;
} ads1015_scale[] = {
	{3, 0},
	{2, 0},
	{1, 0},
	{0, 500000},
	{0, 250000},
	{0, 125000},
	{0, 125000},
	{0, 125000},
};

#define ADS1015_V_CHAN(_chan, _addr) {				\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.address = _addr,					\
	.channel = _chan,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
				BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.scan_index = _addr,					\
	.scan_type = {						\
		.sign = 's',					\
		.realbits = 12,					\
		.storagebits = 16,				\
		.shift = 4,					\
		.endianness = IIO_CPU,				\
	},							\
	.datasheet_name = "AIN"#_chan,				\
}

#define ADS1015_V_DIFF_CHAN(_chan, _chan2, _addr) {		\
	.type = IIO_VOLTAGE,					\
	.differential = 1,					\
	.indexed = 1,						\
	.address = _addr,					\
	.channel = _chan,					\
	.channel2 = _chan2,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
				BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.scan_index = _addr,					\
	.scan_type = {						\
		.sign = 's',					\
		.realbits = 12,					\
		.storagebits = 16,				\
		.shift = 4,					\
		.endianness = IIO_CPU,				\
	},							\
	.datasheet_name = "AIN"#_chan"-AIN"#_chan2,		\
}

#define ADS1115_V_CHAN(_chan, _addr) {				\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.address = _addr,					\
	.channel = _chan,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
				BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.scan_index = _addr,					\
	.scan_type = {						\
		.sign = 's',					\
		.realbits = 16,					\
		.storagebits = 16,				\
		.endianness = IIO_CPU,				\
	},							\
	.datasheet_name = "AIN"#_chan,				\
}

#define ADS1115_V_DIFF_CHAN(_chan, _chan2, _addr) {		\
	.type = IIO_VOLTAGE,					\
	.differential = 1,					\
	.indexed = 1,						\
	.address = _addr,					\
	.channel = _chan,					\
	.channel2 = _chan2,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
				BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.scan_index = _addr,					\
	.scan_type = {						\
		.sign = 's',					\
		.realbits = 16,					\
		.storagebits = 16,				\
		.endianness = IIO_CPU,				\
	},							\
	.datasheet_name = "AIN"#_chan"-AIN"#_chan2,		\
}

struct ads1015_data {
	struct regmap *regmap;
	/*
	 * Protects ADC ops, e.g: concurrent sysfs/buffered
	 * data reads, configuration updates
	 */
	struct mutex lock;
	struct ads1015_channel_data channel_data[ADS1015_CHANNELS];

	unsigned int *data_rate;
};

static bool ads1015_is_writeable_reg(struct device *dev, unsigned int reg)
{
	return (reg == ADS1015_CFG_REG);
}

static const struct regmap_config ads1015_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = ADS1015_CFG_REG,
	.writeable_reg = ads1015_is_writeable_reg,
};

static const struct iio_chan_spec ads1015_channels[] = {
	ADS1015_V_DIFF_CHAN(0, 1, ADS1015_AIN0_AIN1),
	ADS1015_V_DIFF_CHAN(0, 3, ADS1015_AIN0_AIN3),
	ADS1015_V_DIFF_CHAN(1, 3, ADS1015_AIN1_AIN3),
	ADS1015_V_DIFF_CHAN(2, 3, ADS1015_AIN2_AIN3),
	ADS1015_V_CHAN(0, ADS1015_AIN0),
	ADS1015_V_CHAN(1, ADS1015_AIN1),
	ADS1015_V_CHAN(2, ADS1015_AIN2),
	ADS1015_V_CHAN(3, ADS1015_AIN3),
	IIO_CHAN_SOFT_TIMESTAMP(ADS1015_TIMESTAMP),
};

static const struct iio_chan_spec ads1115_channels[] = {
	ADS1115_V_DIFF_CHAN(0, 1, ADS1015_AIN0_AIN1),
	ADS1115_V_DIFF_CHAN(0, 3, ADS1015_AIN0_AIN3),
	ADS1115_V_DIFF_CHAN(1, 3, ADS1015_AIN1_AIN3),
	ADS1115_V_DIFF_CHAN(2, 3, ADS1015_AIN2_AIN3),
	ADS1115_V_CHAN(0, ADS1015_AIN0),
	ADS1115_V_CHAN(1, ADS1015_AIN1),
	ADS1115_V_CHAN(2, ADS1015_AIN2),
	ADS1115_V_CHAN(3, ADS1015_AIN3),
	IIO_CHAN_SOFT_TIMESTAMP(ADS1015_TIMESTAMP),
};

static int ads1015_set_power_state(struct ads1015_data *data, bool on)
{
	int ret;
	struct device *dev = regmap_get_device(data->regmap);

	if (on) {
		ret = pm_runtime_get_sync(dev);
		if (ret < 0)
			pm_runtime_put_noidle(dev);
	} else {
		pm_runtime_mark_last_busy(dev);
		ret = pm_runtime_put_autosuspend(dev);
	}

	return ret;
}

static
int ads1015_get_adc_result(struct ads1015_data *data, int chan, int *val)
{
	int ret, pga, dr, conv_time;
	bool change;

	if (chan < 0 || chan >= ADS1015_CHANNELS)
		return -EINVAL;

	pga = data->channel_data[chan].pga;
	dr = data->channel_data[chan].data_rate;

	ret = regmap_update_bits_check(data->regmap, ADS1015_CFG_REG,
				       ADS1015_CFG_MUX_MASK |
				       ADS1015_CFG_PGA_MASK,
				       chan << ADS1015_CFG_MUX_SHIFT |
				       pga << ADS1015_CFG_PGA_SHIFT,
				       &change);
	if (ret < 0)
		return ret;

	if (change) {
		conv_time = DIV_ROUND_UP(USEC_PER_SEC, data->data_rate[dr]);
		usleep_range(conv_time, conv_time + 1);
	}

	return regmap_read(data->regmap, ADS1015_CONV_REG, val);
}

static irqreturn_t ads1015_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ads1015_data *data = iio_priv(indio_dev);
	s16 buf[8]; /* 1x s16 ADC val + 3x s16 padding +  4x s16 timestamp */
	int chan, ret, res;

	memset(buf, 0, sizeof(buf));

	mutex_lock(&data->lock);
	chan = find_first_bit(indio_dev->active_scan_mask,
			      indio_dev->masklength);
	ret = ads1015_get_adc_result(data, chan, &res);
	if (ret < 0) {
		mutex_unlock(&data->lock);
		goto err;
	}

	buf[0] = res;
	mutex_unlock(&data->lock);

	iio_push_to_buffers_with_timestamp(indio_dev, buf,
					   iio_get_time_ns(indio_dev));

err:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int ads1015_set_scale(struct ads1015_data *data, int chan,
			     int scale, int uscale)
{
	int i, ret, rindex = -1;

	for (i = 0; i < ARRAY_SIZE(ads1015_scale); i++)
		if (ads1015_scale[i].scale == scale &&
		    ads1015_scale[i].uscale == uscale) {
			rindex = i;
			break;
		}
	if (rindex < 0)
		return -EINVAL;

	ret = regmap_update_bits(data->regmap, ADS1015_CFG_REG,
				 ADS1015_CFG_PGA_MASK,
				 rindex << ADS1015_CFG_PGA_SHIFT);
	if (ret < 0)
		return ret;

	data->channel_data[chan].pga = rindex;

	return 0;
}

static int ads1015_set_data_rate(struct ads1015_data *data, int chan, int rate)
{
	int i, ret, rindex = -1;

	for (i = 0; i < ARRAY_SIZE(ads1015_data_rate); i++)
		if (data->data_rate[i] == rate) {
			rindex = i;
			break;
		}
	if (rindex < 0)
		return -EINVAL;

	ret = regmap_update_bits(data->regmap, ADS1015_CFG_REG,
				 ADS1015_CFG_DR_MASK,
				 rindex << ADS1015_CFG_DR_SHIFT);
	if (ret < 0)
		return ret;

	data->channel_data[chan].data_rate = rindex;

	return 0;
}

static int ads1015_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	int ret, idx;
	struct ads1015_data *data = iio_priv(indio_dev);

	mutex_lock(&indio_dev->mlock);
	mutex_lock(&data->lock);
	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
		int shift = chan->scan_type.shift;

		if (iio_buffer_enabled(indio_dev)) {
			ret = -EBUSY;
			break;
		}

		ret = ads1015_set_power_state(data, true);
		if (ret < 0)
			break;

		ret = ads1015_get_adc_result(data, chan->address, val);
		if (ret < 0) {
			ads1015_set_power_state(data, false);
			break;
		}

		*val = sign_extend32(*val >> shift, 15 - shift);

		ret = ads1015_set_power_state(data, false);
		if (ret < 0)
			break;

		ret = IIO_VAL_INT;
		break;
	}
	case IIO_CHAN_INFO_SCALE:
		idx = data->channel_data[chan->address].pga;
		*val = ads1015_scale[idx].scale;
		*val2 = ads1015_scale[idx].uscale;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		idx = data->channel_data[chan->address].data_rate;
		*val = data->data_rate[idx];
		ret = IIO_VAL_INT;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&data->lock);
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static int ads1015_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int val,
			     int val2, long mask)
{
	struct ads1015_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->lock);
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		ret = ads1015_set_scale(data, chan->address, val, val2);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = ads1015_set_data_rate(data, chan->address, val);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&data->lock);

	return ret;
}

static int ads1015_buffer_preenable(struct iio_dev *indio_dev)
{
	return ads1015_set_power_state(iio_priv(indio_dev), true);
}

static int ads1015_buffer_postdisable(struct iio_dev *indio_dev)
{
	return ads1015_set_power_state(iio_priv(indio_dev), false);
}

static const struct iio_buffer_setup_ops ads1015_buffer_setup_ops = {
	.preenable	= ads1015_buffer_preenable,
	.postenable	= iio_triggered_buffer_postenable,
	.predisable	= iio_triggered_buffer_predisable,
	.postdisable	= ads1015_buffer_postdisable,
	.validate_scan_mask = &iio_validate_scan_mask_onehot,
};

static IIO_CONST_ATTR(scale_available, "3 2 1 0.5 0.25 0.125");

static IIO_CONST_ATTR_NAMED(ads1015_sampling_frequency_available,
	sampling_frequency_available, "128 250 490 920 1600 2400 3300");
static IIO_CONST_ATTR_NAMED(ads1115_sampling_frequency_available,
	sampling_frequency_available, "8 16 32 64 128 250 475 860");

static struct attribute *ads1015_attributes[] = {
	&iio_const_attr_scale_available.dev_attr.attr,
	&iio_const_attr_ads1015_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ads1015_attribute_group = {
	.attrs = ads1015_attributes,
};

static struct attribute *ads1115_attributes[] = {
	&iio_const_attr_scale_available.dev_attr.attr,
	&iio_const_attr_ads1115_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ads1115_attribute_group = {
	.attrs = ads1115_attributes,
};

static const struct iio_info ads1015_info = {
	.driver_module	= THIS_MODULE,
	.read_raw	= ads1015_read_raw,
	.write_raw	= ads1015_write_raw,
	.attrs          = &ads1015_attribute_group,
};

static const struct iio_info ads1115_info = {
	.driver_module	= THIS_MODULE,
	.read_raw	= ads1015_read_raw,
	.write_raw	= ads1015_write_raw,
	.attrs          = &ads1115_attribute_group,
};

#ifdef CONFIG_OF
static int ads1015_get_channels_config_of(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ads1015_data *data = iio_priv(indio_dev);
	struct device_node *node;

	if (!client->dev.of_node ||
	    !of_get_next_child(client->dev.of_node, NULL))
		return -EINVAL;

	for_each_child_of_node(client->dev.of_node, node) {
		u32 pval;
		unsigned int channel;
		unsigned int pga = ADS1015_DEFAULT_PGA;
		unsigned int data_rate = ADS1015_DEFAULT_DATA_RATE;

		if (of_property_read_u32(node, "reg", &pval)) {
			dev_err(&client->dev, "invalid reg on %pOF\n",
				node);
			continue;
		}

		channel = pval;
		if (channel >= ADS1015_CHANNELS) {
			dev_err(&client->dev,
				"invalid channel index %d on %pOF\n",
				channel, node);
			continue;
		}

		if (!of_property_read_u32(node, "ti,gain", &pval)) {
			pga = pval;
			if (pga > 6) {
				dev_err(&client->dev, "invalid gain on %pOF\n",
					node);
				of_node_put(node);
				return -EINVAL;
			}
		}

		if (!of_property_read_u32(node, "ti,datarate", &pval)) {
			data_rate = pval;
			if (data_rate > 7) {
				dev_err(&client->dev,
					"invalid data_rate on %pOF\n",
					node);
				of_node_put(node);
				return -EINVAL;
			}
		}

		data->channel_data[channel].pga = pga;
		data->channel_data[channel].data_rate = data_rate;
	}

	return 0;
}
#endif

static void ads1015_get_channels_config(struct i2c_client *client)
{
	unsigned int k;

	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ads1015_data *data = iio_priv(indio_dev);
	struct ads1015_platform_data *pdata = dev_get_platdata(&client->dev);

	/* prefer platform data */
	if (pdata) {
		memcpy(data->channel_data, pdata->channel_data,
		       sizeof(data->channel_data));
		return;
	}

#ifdef CONFIG_OF
	if (!ads1015_get_channels_config_of(client))
		return;
#endif
	/* fallback on default configuration */
	for (k = 0; k < ADS1015_CHANNELS; ++k) {
		data->channel_data[k].pga = ADS1015_DEFAULT_PGA;
		data->channel_data[k].data_rate = ADS1015_DEFAULT_DATA_RATE;
	}
}

static int ads1015_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct ads1015_data *data;
	int ret;
	enum chip_ids chip;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);

	mutex_init(&data->lock);

	indio_dev->dev.parent = &client->dev;
	indio_dev->dev.of_node = client->dev.of_node;
	indio_dev->name = ADS1015_DRV_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;

	if (client->dev.of_node)
		chip = (enum chip_ids)of_device_get_match_data(&client->dev);
	else
		chip = id->driver_data;
	switch (chip) {
	case ADS1015:
		indio_dev->channels = ads1015_channels;
		indio_dev->num_channels = ARRAY_SIZE(ads1015_channels);
		indio_dev->info = &ads1015_info;
		data->data_rate = (unsigned int *) &ads1015_data_rate;
		break;
	case ADS1115:
		indio_dev->channels = ads1115_channels;
		indio_dev->num_channels = ARRAY_SIZE(ads1115_channels);
		indio_dev->info = &ads1115_info;
		data->data_rate = (unsigned int *) &ads1115_data_rate;
		break;
	}

	/* we need to keep this ABI the same as used by hwmon ADS1015 driver */
	ads1015_get_channels_config(client);

	data->regmap = devm_regmap_init_i2c(client, &ads1015_regmap_config);
	if (IS_ERR(data->regmap)) {
		dev_err(&client->dev, "Failed to allocate register map\n");
		return PTR_ERR(data->regmap);
	}

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
					 ads1015_trigger_handler,
					 &ads1015_buffer_setup_ops);
	if (ret < 0) {
		dev_err(&client->dev, "iio triggered buffer setup failed\n");
		return ret;
	}
	ret = pm_runtime_set_active(&client->dev);
	if (ret)
		goto err_buffer_cleanup;
	pm_runtime_set_autosuspend_delay(&client->dev, ADS1015_SLEEP_DELAY_MS);
	pm_runtime_use_autosuspend(&client->dev);
	pm_runtime_enable(&client->dev);

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to register IIO device\n");
		goto err_buffer_cleanup;
	}

	return 0;

err_buffer_cleanup:
	iio_triggered_buffer_cleanup(indio_dev);

	return ret;
}

static int ads1015_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ads1015_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	pm_runtime_put_noidle(&client->dev);

	iio_triggered_buffer_cleanup(indio_dev);

	/* power down single shot mode */
	return regmap_update_bits(data->regmap, ADS1015_CFG_REG,
				  ADS1015_CFG_MOD_MASK,
				  ADS1015_SINGLESHOT << ADS1015_CFG_MOD_SHIFT);
}

#ifdef CONFIG_PM
static int ads1015_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct ads1015_data *data = iio_priv(indio_dev);

	return regmap_update_bits(data->regmap, ADS1015_CFG_REG,
				  ADS1015_CFG_MOD_MASK,
				  ADS1015_SINGLESHOT << ADS1015_CFG_MOD_SHIFT);
}

static int ads1015_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct ads1015_data *data = iio_priv(indio_dev);

	return regmap_update_bits(data->regmap, ADS1015_CFG_REG,
				  ADS1015_CFG_MOD_MASK,
				  ADS1015_CONTINUOUS << ADS1015_CFG_MOD_SHIFT);
}
#endif

static const struct dev_pm_ops ads1015_pm_ops = {
	SET_RUNTIME_PM_OPS(ads1015_runtime_suspend,
			   ads1015_runtime_resume, NULL)
};

static const struct i2c_device_id ads1015_id[] = {
	{"ads1015", ADS1015},
	{"ads1115", ADS1115},
	{}
};
MODULE_DEVICE_TABLE(i2c, ads1015_id);

static const struct of_device_id ads1015_of_match[] = {
	{
		.compatible = "ti,ads1015",
		.data = (void *)ADS1015
	},
	{
		.compatible = "ti,ads1115",
		.data = (void *)ADS1115
	},
	{}
};
MODULE_DEVICE_TABLE(of, ads1015_of_match);

static struct i2c_driver ads1015_driver = {
	.driver = {
		.name = ADS1015_DRV_NAME,
		.of_match_table = ads1015_of_match,
		.pm = &ads1015_pm_ops,
	},
	.probe		= ads1015_probe,
	.remove		= ads1015_remove,
	.id_table	= ads1015_id,
};

module_i2c_driver(ads1015_driver);

MODULE_AUTHOR("Daniel Baluta <daniel.baluta@intel.com>");
MODULE_DESCRIPTION("Texas Instruments ADS1015 ADC driver");
MODULE_LICENSE("GPL v2");
