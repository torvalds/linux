// SPDX-License-Identifier: GPL-2.0
/*
 * VCNL4035 Ambient Light and Proximity Sensor - 7-bit I2C slave address 0x60
 *
 * Copyright (c) 2018, DENX Software Engineering GmbH
 * Author: Parthiban Nallathambi <pn@denx.de>
 *
 * TODO: Proximity
 */
#include <linux/bitops.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define VCNL4035_DRV_NAME	"vcnl4035"
#define VCNL4035_IRQ_NAME	"vcnl4035_event"
#define VCNL4035_REGMAP_NAME	"vcnl4035_regmap"

/* Device registers */
#define VCNL4035_ALS_CONF	0x00
#define VCNL4035_ALS_THDH	0x01
#define VCNL4035_ALS_THDL	0x02
#define VCNL4035_ALS_DATA	0x0B
#define VCNL4035_WHITE_DATA	0x0C
#define VCNL4035_INT_FLAG	0x0D
#define VCNL4035_DEV_ID		0x0E

/* Register masks */
#define VCNL4035_MODE_ALS_MASK		BIT(0)
#define VCNL4035_MODE_ALS_WHITE_CHAN	BIT(8)
#define VCNL4035_MODE_ALS_INT_MASK	BIT(1)
#define VCNL4035_ALS_IT_MASK		GENMASK(7, 5)
#define VCNL4035_ALS_PERS_MASK		GENMASK(3, 2)
#define VCNL4035_INT_ALS_IF_H_MASK	BIT(12)
#define VCNL4035_INT_ALS_IF_L_MASK	BIT(13)

/* Default values */
#define VCNL4035_MODE_ALS_ENABLE	BIT(0)
#define VCNL4035_MODE_ALS_DISABLE	0x00
#define VCNL4035_MODE_ALS_INT_ENABLE	BIT(1)
#define VCNL4035_MODE_ALS_INT_DISABLE	0
#define VCNL4035_DEV_ID_VAL		0x80
#define VCNL4035_ALS_IT_DEFAULT		0x01
#define VCNL4035_ALS_PERS_DEFAULT	0x00
#define VCNL4035_ALS_THDH_DEFAULT	5000
#define VCNL4035_ALS_THDL_DEFAULT	100
#define VCNL4035_SLEEP_DELAY_MS		2000

struct vcnl4035_data {
	struct i2c_client *client;
	struct regmap *regmap;
	unsigned int als_it_val;
	unsigned int als_persistence;
	unsigned int als_thresh_low;
	unsigned int als_thresh_high;
	struct iio_trigger *drdy_trigger0;
};

static inline bool vcnl4035_is_triggered(struct vcnl4035_data *data)
{
	int ret;
	int reg;

	ret = regmap_read(data->regmap, VCNL4035_INT_FLAG, &reg);
	if (ret < 0)
		return false;

	return !!(reg &
		(VCNL4035_INT_ALS_IF_H_MASK | VCNL4035_INT_ALS_IF_L_MASK));
}

static irqreturn_t vcnl4035_drdy_irq_thread(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct vcnl4035_data *data = iio_priv(indio_dev);

	if (vcnl4035_is_triggered(data)) {
		iio_push_event(indio_dev, IIO_UNMOD_EVENT_CODE(IIO_LIGHT,
							0,
							IIO_EV_TYPE_THRESH,
							IIO_EV_DIR_EITHER),
				iio_get_time_ns(indio_dev));
		iio_trigger_poll_chained(data->drdy_trigger0);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

/* Triggered buffer */
static irqreturn_t vcnl4035_trigger_consumer_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct vcnl4035_data *data = iio_priv(indio_dev);
	u8 buffer[ALIGN(sizeof(u16), sizeof(s64)) + sizeof(s64)];
	int ret;

	ret = regmap_read(data->regmap, VCNL4035_ALS_DATA, (int *)buffer);
	if (ret < 0) {
		dev_err(&data->client->dev,
			"Trigger consumer can't read from sensor.\n");
		goto fail_read;
	}
	iio_push_to_buffers_with_timestamp(indio_dev, buffer,
					iio_get_time_ns(indio_dev));

fail_read:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int vcnl4035_als_drdy_set_state(struct iio_trigger *trigger,
					bool enable_drdy)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trigger);
	struct vcnl4035_data *data = iio_priv(indio_dev);
	int val = enable_drdy ? VCNL4035_MODE_ALS_INT_ENABLE :
					VCNL4035_MODE_ALS_INT_DISABLE;

	return regmap_update_bits(data->regmap, VCNL4035_ALS_CONF,
				 VCNL4035_MODE_ALS_INT_MASK,
				 val);
}

static const struct iio_trigger_ops vcnl4035_trigger_ops = {
	.validate_device = iio_trigger_validate_own_device,
	.set_trigger_state = vcnl4035_als_drdy_set_state,
};

static int vcnl4035_set_pm_runtime_state(struct vcnl4035_data *data, bool on)
{
	int ret;
	struct device *dev = &data->client->dev;

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

/*
 *	Device IT	INT Time (ms)	Scale (lux/step)
 *	000		50		0.064
 *	001		100		0.032
 *	010		200		0.016
 *	100		400		0.008
 *	101 - 111	800		0.004
 * Values are proportional, so ALS INT is selected for input due to
 * simplicity reason. Integration time value and scaling is
 * calculated based on device INT value
 *
 * Raw value needs to be scaled using ALS steps
 */
static int vcnl4035_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	struct vcnl4035_data *data = iio_priv(indio_dev);
	int ret;
	int raw_data;
	unsigned int reg;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = vcnl4035_set_pm_runtime_state(data, true);
		if  (ret < 0)
			return ret;

		ret = iio_device_claim_direct_mode(indio_dev);
		if (!ret) {
			if (chan->channel)
				reg = VCNL4035_ALS_DATA;
			else
				reg = VCNL4035_WHITE_DATA;
			ret = regmap_read(data->regmap, reg, &raw_data);
			iio_device_release_direct_mode(indio_dev);
			if (!ret) {
				*val = raw_data;
				ret = IIO_VAL_INT;
			}
		}
		vcnl4035_set_pm_runtime_state(data, false);
		return ret;
	case IIO_CHAN_INFO_INT_TIME:
		*val = 50;
		if (data->als_it_val)
			*val = data->als_it_val * 100;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 64;
		if (!data->als_it_val)
			*val2 = 1000;
		else
			*val2 = data->als_it_val * 2 * 1000;
		return IIO_VAL_FRACTIONAL;
	default:
		return -EINVAL;
	}
}

static int vcnl4035_write_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int val, int val2, long mask)
{
	int ret;
	struct vcnl4035_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		if (val <= 0 || val > 800)
			return -EINVAL;

		ret = vcnl4035_set_pm_runtime_state(data, true);
		if  (ret < 0)
			return ret;

		ret = regmap_update_bits(data->regmap, VCNL4035_ALS_CONF,
					 VCNL4035_ALS_IT_MASK,
					 val / 100);
		if (!ret)
			data->als_it_val = val / 100;

		vcnl4035_set_pm_runtime_state(data, false);
		return ret;
	default:
		return -EINVAL;
	}
}

/* No direct ABI for persistence and threshold, so eventing */
static int vcnl4035_read_thresh(struct iio_dev *indio_dev,
		const struct iio_chan_spec *chan, enum iio_event_type type,
		enum iio_event_direction dir, enum iio_event_info info,
		int *val, int *val2)
{
	struct vcnl4035_data *data = iio_priv(indio_dev);

	switch (info) {
	case IIO_EV_INFO_VALUE:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			*val = data->als_thresh_high;
			return IIO_VAL_INT;
		case IIO_EV_DIR_FALLING:
			*val = data->als_thresh_low;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
		break;
	case IIO_EV_INFO_PERIOD:
		*val = data->als_persistence;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}

}

static int vcnl4035_write_thresh(struct iio_dev *indio_dev,
		const struct iio_chan_spec *chan, enum iio_event_type type,
		enum iio_event_direction dir, enum iio_event_info info, int val,
		int val2)
{
	struct vcnl4035_data *data = iio_priv(indio_dev);
	int ret;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		/* 16 bit threshold range 0 - 65535 */
		if (val < 0 || val > 65535)
			return -EINVAL;
		if (dir == IIO_EV_DIR_RISING) {
			if (val < data->als_thresh_low)
				return -EINVAL;
			ret = regmap_write(data->regmap, VCNL4035_ALS_THDH,
					   val);
			if (ret)
				return ret;
			data->als_thresh_high = val;
		} else {
			if (val > data->als_thresh_high)
				return -EINVAL;
			ret = regmap_write(data->regmap, VCNL4035_ALS_THDL,
					   val);
			if (ret)
				return ret;
			data->als_thresh_low = val;
		}
		return ret;
	case IIO_EV_INFO_PERIOD:
		/* allow only 1 2 4 8 as persistence value */
		if (val < 0 || val > 8 || hweight8(val) != 1)
			return -EINVAL;
		ret = regmap_update_bits(data->regmap, VCNL4035_ALS_CONF,
					 VCNL4035_ALS_PERS_MASK, val);
		if (!ret)
			data->als_persistence = val;
		return ret;
	default:
		return -EINVAL;
	}
}

static IIO_CONST_ATTR_INT_TIME_AVAIL("50 100 200 400 800");

static struct attribute *vcnl4035_attributes[] = {
	&iio_const_attr_integration_time_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group vcnl4035_attribute_group = {
	.attrs = vcnl4035_attributes,
};

static const struct iio_info vcnl4035_info = {
	.read_raw		= vcnl4035_read_raw,
	.write_raw		= vcnl4035_write_raw,
	.read_event_value	= vcnl4035_read_thresh,
	.write_event_value	= vcnl4035_write_thresh,
	.attrs			= &vcnl4035_attribute_group,
};

static const struct iio_event_spec vcnl4035_event_spec[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_PERIOD),
	},
};

enum vcnl4035_scan_index_order {
	VCNL4035_CHAN_INDEX_LIGHT,
	VCNL4035_CHAN_INDEX_WHITE_LED,
};

static const struct iio_buffer_setup_ops iio_triggered_buffer_setup_ops = {
	.validate_scan_mask = &iio_validate_scan_mask_onehot,
};

static const struct iio_chan_spec vcnl4035_channels[] = {
	{
		.type = IIO_LIGHT,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_INT_TIME) |
				BIT(IIO_CHAN_INFO_SCALE),
		.event_spec = vcnl4035_event_spec,
		.num_event_specs = ARRAY_SIZE(vcnl4035_event_spec),
		.scan_index = VCNL4035_CHAN_INDEX_LIGHT,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
	{
		.type = IIO_INTENSITY,
		.channel = 1,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_BOTH,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.scan_index = VCNL4035_CHAN_INDEX_WHITE_LED,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
};

static int vcnl4035_set_als_power_state(struct vcnl4035_data *data, u8 status)
{
	return regmap_update_bits(data->regmap, VCNL4035_ALS_CONF,
					VCNL4035_MODE_ALS_MASK,
					status);
}

static int vcnl4035_init(struct vcnl4035_data *data)
{
	int ret;
	int id;

	ret = regmap_read(data->regmap, VCNL4035_DEV_ID, &id);
	if (ret < 0) {
		dev_err(&data->client->dev, "Failed to read DEV_ID register\n");
		return ret;
	}

	if (id != VCNL4035_DEV_ID_VAL) {
		dev_err(&data->client->dev, "Wrong id, got %x, expected %x\n",
			id, VCNL4035_DEV_ID_VAL);
		return -ENODEV;
	}

	ret = vcnl4035_set_als_power_state(data, VCNL4035_MODE_ALS_ENABLE);
	if (ret < 0)
		return ret;

	/* ALS white channel enable */
	ret = regmap_update_bits(data->regmap, VCNL4035_ALS_CONF,
				 VCNL4035_MODE_ALS_WHITE_CHAN,
				 1);
	if (ret) {
		dev_err(&data->client->dev, "set white channel enable %d\n",
			ret);
		return ret;
	}

	/* set default integration time - 100 ms for ALS */
	ret = regmap_update_bits(data->regmap, VCNL4035_ALS_CONF,
				 VCNL4035_ALS_IT_MASK,
				 VCNL4035_ALS_IT_DEFAULT);
	if (ret) {
		dev_err(&data->client->dev, "set default ALS IT returned %d\n",
			ret);
		return ret;
	}
	data->als_it_val = VCNL4035_ALS_IT_DEFAULT;

	/* set default persistence time - 1 for ALS */
	ret = regmap_update_bits(data->regmap, VCNL4035_ALS_CONF,
				 VCNL4035_ALS_PERS_MASK,
				 VCNL4035_ALS_PERS_DEFAULT);
	if (ret) {
		dev_err(&data->client->dev, "set default PERS returned %d\n",
			ret);
		return ret;
	}
	data->als_persistence = VCNL4035_ALS_PERS_DEFAULT;

	/* set default HIGH threshold for ALS */
	ret = regmap_write(data->regmap, VCNL4035_ALS_THDH,
				VCNL4035_ALS_THDH_DEFAULT);
	if (ret) {
		dev_err(&data->client->dev, "set default THDH returned %d\n",
			ret);
		return ret;
	}
	data->als_thresh_high = VCNL4035_ALS_THDH_DEFAULT;

	/* set default LOW threshold for ALS */
	ret = regmap_write(data->regmap, VCNL4035_ALS_THDL,
				VCNL4035_ALS_THDL_DEFAULT);
	if (ret) {
		dev_err(&data->client->dev, "set default THDL returned %d\n",
			ret);
		return ret;
	}
	data->als_thresh_low = VCNL4035_ALS_THDL_DEFAULT;

	return 0;
}

static bool vcnl4035_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case VCNL4035_ALS_CONF:
	case VCNL4035_DEV_ID:
		return false;
	default:
		return true;
	}
}

static const struct regmap_config vcnl4035_regmap_config = {
	.name		= VCNL4035_REGMAP_NAME,
	.reg_bits	= 8,
	.val_bits	= 16,
	.max_register	= VCNL4035_DEV_ID,
	.cache_type	= REGCACHE_RBTREE,
	.volatile_reg	= vcnl4035_is_volatile_reg,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};

static int vcnl4035_probe_trigger(struct iio_dev *indio_dev)
{
	int ret;
	struct vcnl4035_data *data = iio_priv(indio_dev);

	data->drdy_trigger0 = devm_iio_trigger_alloc(
			indio_dev->dev.parent,
			"%s-dev%d", indio_dev->name, indio_dev->id);
	if (!data->drdy_trigger0)
		return -ENOMEM;

	data->drdy_trigger0->ops = &vcnl4035_trigger_ops;
	iio_trigger_set_drvdata(data->drdy_trigger0, indio_dev);
	ret = devm_iio_trigger_register(indio_dev->dev.parent,
					data->drdy_trigger0);
	if (ret) {
		dev_err(&data->client->dev, "iio trigger register failed\n");
		return ret;
	}

	/* Trigger setup */
	ret = devm_iio_triggered_buffer_setup(indio_dev->dev.parent, indio_dev,
					NULL, vcnl4035_trigger_consumer_handler,
					&iio_triggered_buffer_setup_ops);
	if (ret < 0) {
		dev_err(&data->client->dev, "iio triggered buffer setup failed\n");
		return ret;
	}

	/* IRQ to trigger mapping */
	ret = devm_request_threaded_irq(&data->client->dev, data->client->irq,
			NULL, vcnl4035_drdy_irq_thread,
			IRQF_TRIGGER_LOW | IRQF_ONESHOT,
			VCNL4035_IRQ_NAME, indio_dev);
	if (ret < 0)
		dev_err(&data->client->dev, "request irq %d for trigger0 failed\n",
				data->client->irq);
	return ret;
}

static int vcnl4035_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct vcnl4035_data *data;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &vcnl4035_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "regmap_init failed!\n");
		return PTR_ERR(regmap);
	}

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	data->regmap = regmap;

	indio_dev->info = &vcnl4035_info;
	indio_dev->name = VCNL4035_DRV_NAME;
	indio_dev->channels = vcnl4035_channels;
	indio_dev->num_channels = ARRAY_SIZE(vcnl4035_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = vcnl4035_init(data);
	if (ret < 0) {
		dev_err(&client->dev, "vcnl4035 chip init failed\n");
		return ret;
	}

	if (client->irq > 0) {
		ret = vcnl4035_probe_trigger(indio_dev);
		if (ret < 0) {
			dev_err(&client->dev, "vcnl4035 unable init trigger\n");
			goto fail_poweroff;
		}
	}

	ret = pm_runtime_set_active(&client->dev);
	if (ret < 0)
		goto fail_poweroff;

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto fail_poweroff;

	pm_runtime_enable(&client->dev);
	pm_runtime_set_autosuspend_delay(&client->dev, VCNL4035_SLEEP_DELAY_MS);
	pm_runtime_use_autosuspend(&client->dev);

	return 0;

fail_poweroff:
	vcnl4035_set_als_power_state(data, VCNL4035_MODE_ALS_DISABLE);
	return ret;
}

static int vcnl4035_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	pm_runtime_dont_use_autosuspend(&client->dev);
	pm_runtime_disable(&client->dev);
	iio_device_unregister(indio_dev);
	pm_runtime_set_suspended(&client->dev);

	return vcnl4035_set_als_power_state(iio_priv(indio_dev),
					VCNL4035_MODE_ALS_DISABLE);
}

static int __maybe_unused vcnl4035_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct vcnl4035_data *data = iio_priv(indio_dev);
	int ret;

	ret = vcnl4035_set_als_power_state(data, VCNL4035_MODE_ALS_DISABLE);
	regcache_mark_dirty(data->regmap);

	return ret;
}

static int __maybe_unused vcnl4035_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct vcnl4035_data *data = iio_priv(indio_dev);
	int ret;

	regcache_sync(data->regmap);
	ret = vcnl4035_set_als_power_state(data, VCNL4035_MODE_ALS_ENABLE);
	if (ret < 0)
		return ret;

	/* wait for 1 ALS integration cycle */
	msleep(data->als_it_val * 100);

	return 0;
}

static const struct dev_pm_ops vcnl4035_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(vcnl4035_runtime_suspend,
			   vcnl4035_runtime_resume, NULL)
};

static const struct i2c_device_id vcnl4035_id[] = {
	{ "vcnl4035", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, vcnl4035_id);

static const struct of_device_id vcnl4035_of_match[] = {
	{ .compatible = "vishay,vcnl4035", },
	{ }
};
MODULE_DEVICE_TABLE(of, vcnl4035_of_match);

static struct i2c_driver vcnl4035_driver = {
	.driver = {
		.name   = VCNL4035_DRV_NAME,
		.pm	= &vcnl4035_pm_ops,
		.of_match_table = vcnl4035_of_match,
	},
	.probe  = vcnl4035_probe,
	.remove	= vcnl4035_remove,
	.id_table = vcnl4035_id,
};

module_i2c_driver(vcnl4035_driver);

MODULE_AUTHOR("Parthiban Nallathambi <pn@denx.de>");
MODULE_DESCRIPTION("VCNL4035 Ambient Light Sensor driver");
MODULE_LICENSE("GPL v2");
