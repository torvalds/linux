// SPDX-License-Identifier: GPL-2.0-only
/*
 * af8133j.c - Voltafield AF8133J magnetometer driver
 *
 * Copyright 2021 Icenowy Zheng <icenowy@aosc.io>
 * Copyright 2024 Ondřej Jirman <megi@xff.cz>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <linux/iio/iio.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define AF8133J_REG_OUT		0x03
#define AF8133J_REG_PCODE	0x00
#define AF8133J_REG_PCODE_VAL	0x5e
#define AF8133J_REG_STATUS	0x02
#define AF8133J_REG_STATUS_ACQ	BIT(0)
#define AF8133J_REG_STATE	0x0a
#define AF8133J_REG_STATE_STBY	0x00
#define AF8133J_REG_STATE_WORK	0x01
#define AF8133J_REG_RANGE	0x0b
#define AF8133J_REG_RANGE_22G	0x12
#define AF8133J_REG_RANGE_12G	0x34
#define AF8133J_REG_SWR		0x11
#define AF8133J_REG_SWR_PERFORM	0x81

static const char * const af8133j_supply_names[] = {
	"avdd",
	"dvdd",
};

struct af8133j_data {
	struct i2c_client *client;
	struct regmap *regmap;
	/*
	 * Protect device internal state between starting a measurement
	 * and reading the result.
	 */
	struct mutex mutex;
	struct iio_mount_matrix orientation;

	struct gpio_desc *reset_gpiod;
	struct regulator_bulk_data supplies[ARRAY_SIZE(af8133j_supply_names)];

	u8 range;
};

enum af8133j_axis {
	AXIS_X = 0,
	AXIS_Y,
	AXIS_Z,
};

static struct iio_mount_matrix *
af8133j_get_mount_matrix(struct iio_dev *indio_dev,
			 const struct iio_chan_spec *chan)
{
	struct af8133j_data *data = iio_priv(indio_dev);

	return &data->orientation;
}

static const struct iio_chan_spec_ext_info af8133j_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_DIR, af8133j_get_mount_matrix),
	{ }
};

#define AF8133J_CHANNEL(_si, _axis) { \
	.type = IIO_MAGN, \
	.modified = 1, \
	.channel2 = IIO_MOD_ ## _axis, \
	.address = AXIS_ ## _axis, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SCALE), \
	.ext_info = af8133j_ext_info, \
	.scan_index = _si, \
	.scan_type = { \
		.sign = 's', \
		.realbits = 16, \
		.storagebits = 16, \
		.endianness = IIO_LE, \
	}, \
}

static const struct iio_chan_spec af8133j_channels[] = {
	AF8133J_CHANNEL(0, X),
	AF8133J_CHANNEL(1, Y),
	AF8133J_CHANNEL(2, Z),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static int af8133j_product_check(struct af8133j_data *data)
{
	struct device *dev = &data->client->dev;
	unsigned int val;
	int ret;

	ret = regmap_read(data->regmap, AF8133J_REG_PCODE, &val);
	if (ret) {
		dev_err(dev, "Error reading product code (%d)\n", ret);
		return ret;
	}

	if (val != AF8133J_REG_PCODE_VAL) {
		dev_warn(dev, "Invalid product code (0x%02x)\n", val);
		return 0; /* Allow unknown ID so fallback compatibles work */
	}

	return 0;
}

static int af8133j_reset(struct af8133j_data *data)
{
	struct device *dev = &data->client->dev;
	int ret;

	if (data->reset_gpiod) {
		/* If we have GPIO reset line, use it */
		gpiod_set_value_cansleep(data->reset_gpiod, 1);
		udelay(10);
		gpiod_set_value_cansleep(data->reset_gpiod, 0);
	} else {
		/* Otherwise use software reset */
		ret = regmap_write(data->regmap, AF8133J_REG_SWR,
				   AF8133J_REG_SWR_PERFORM);
		if (ret) {
			dev_err(dev, "Failed to reset the chip\n");
			return ret;
		}
	}

	/* Wait for reset to finish */
	usleep_range(1000, 1100);

	/* Restore range setting */
	if (data->range == AF8133J_REG_RANGE_22G) {
		ret = regmap_write(data->regmap, AF8133J_REG_RANGE, data->range);
		if (ret)
			return ret;
	}

	return 0;
}

static void af8133j_power_down(struct af8133j_data *data)
{
	gpiod_set_value_cansleep(data->reset_gpiod, 1);
	regulator_bulk_disable(ARRAY_SIZE(data->supplies), data->supplies);
}

static int af8133j_power_up(struct af8133j_data *data)
{
	struct device *dev = &data->client->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(data->supplies), data->supplies);
	if (ret) {
		dev_err(dev, "Could not enable regulators\n");
		return ret;
	}

	gpiod_set_value_cansleep(data->reset_gpiod, 0);

	/* Wait for power on reset */
	usleep_range(15000, 16000);

	ret = af8133j_reset(data);
	if (ret) {
		af8133j_power_down(data);
		return ret;
	}

	return 0;
}

static int af8133j_take_measurement(struct af8133j_data *data)
{
	unsigned int val;
	int ret;

	ret = regmap_write(data->regmap,
			   AF8133J_REG_STATE, AF8133J_REG_STATE_WORK);
	if (ret)
		return ret;

	/* The datasheet says "Mesaure Time <1.5ms" */
	ret = regmap_read_poll_timeout(data->regmap, AF8133J_REG_STATUS, val,
				       val & AF8133J_REG_STATUS_ACQ,
				       500, 1500);
	if (ret)
		return ret;

	ret = regmap_write(data->regmap,
			   AF8133J_REG_STATE, AF8133J_REG_STATE_STBY);
	if (ret)
		return ret;

	return 0;
}

static int af8133j_read_measurement(struct af8133j_data *data, __le16 buf[3])
{
	struct device *dev = &data->client->dev;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret) {
		/*
		 * Ignore EACCES because that happens when RPM is disabled
		 * during system sleep, while userspace leave eg. hrtimer
		 * trigger attached and IIO core keeps trying to do measurements.
		 */
		if (ret != -EACCES)
			dev_err(dev, "Failed to power on (%d)\n", ret);
		return ret;
	}

	scoped_guard(mutex, &data->mutex) {
		ret = af8133j_take_measurement(data);
		if (ret)
			goto out_rpm_put;

		ret = regmap_bulk_read(data->regmap, AF8133J_REG_OUT,
				       buf, sizeof(__le16) * 3);
	}

out_rpm_put:
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
}

static const int af8133j_scales[][2] = {
	[0] = { 0, 366210 }, /* 12 gauss */
	[1] = { 0, 671386 }, /* 22 gauss */
};

static int af8133j_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	struct af8133j_data *data = iio_priv(indio_dev);
	__le16 buf[3];
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = af8133j_read_measurement(data, buf);
		if (ret)
			return ret;

		*val = sign_extend32(le16_to_cpu(buf[chan->address]),
				     chan->scan_type.realbits - 1);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;

		if (data->range == AF8133J_REG_RANGE_12G)
			*val2 = af8133j_scales[0][1];
		else
			*val2 = af8133j_scales[1][1];

		return IIO_VAL_INT_PLUS_NANO;
	default:
		return -EINVAL;
	}
}

static int af8133j_read_avail(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      const int **vals, int *type, int *length,
			      long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		*vals = (const int *)af8133j_scales;
		*length = ARRAY_SIZE(af8133j_scales) * 2;
		*type = IIO_VAL_INT_PLUS_NANO;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int af8133j_set_scale(struct af8133j_data *data,
			     unsigned int val, unsigned int val2)
{
	struct device *dev = &data->client->dev;
	u8 range;
	int ret = 0;

	if (af8133j_scales[0][0] == val && af8133j_scales[0][1] == val2)
		range = AF8133J_REG_RANGE_12G;
	else if (af8133j_scales[1][0] == val && af8133j_scales[1][1] == val2)
		range = AF8133J_REG_RANGE_22G;
	else
		return -EINVAL;

	pm_runtime_disable(dev);

	/*
	 * When suspended, just store the new range to data->range to be
	 * applied later during power up.
	 */
	if (!pm_runtime_status_suspended(dev)) {
		scoped_guard(mutex, &data->mutex)
			ret = regmap_write(data->regmap,
					   AF8133J_REG_RANGE, range);
	}

	pm_runtime_enable(dev);

	data->range = range;
	return ret;
}

static int af8133j_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct af8133j_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return af8133j_set_scale(data, val, val2);
	default:
		return -EINVAL;
	}
}

static int af8133j_write_raw_get_fmt(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan,
				     long mask)
{
	return IIO_VAL_INT_PLUS_NANO;
}

static const struct iio_info af8133j_info = {
	.read_raw = af8133j_read_raw,
	.read_avail = af8133j_read_avail,
	.write_raw = af8133j_write_raw,
	.write_raw_get_fmt = af8133j_write_raw_get_fmt,
};

static irqreturn_t af8133j_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct af8133j_data *data = iio_priv(indio_dev);
	s64 timestamp = iio_get_time_ns(indio_dev);
	struct {
		__le16 values[3];
		aligned_s64 timestamp;
	} sample;
	int ret;

	memset(&sample, 0, sizeof(sample));

	ret = af8133j_read_measurement(data, sample.values);
	if (ret)
		goto out_done;

	iio_push_to_buffers_with_timestamp(indio_dev, &sample, timestamp);

out_done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const struct regmap_config af8133j_regmap_config = {
	.name = "af8133j_regmap",
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = AF8133J_REG_SWR,
	.cache_type = REGCACHE_NONE,
};

static void af8133j_power_down_action(void *ptr)
{
	struct af8133j_data *data = ptr;

	if (!pm_runtime_status_suspended(&data->client->dev))
		af8133j_power_down(data);
}

static int af8133j_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct af8133j_data *data;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	int ret, i;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &af8133j_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "regmap initialization failed\n");

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	data->regmap = regmap;
	data->range = AF8133J_REG_RANGE_12G;
	mutex_init(&data->mutex);

	data->reset_gpiod = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(data->reset_gpiod))
		return dev_err_probe(dev, PTR_ERR(data->reset_gpiod),
				     "Failed to get reset gpio\n");

	for (i = 0; i < ARRAY_SIZE(af8133j_supply_names); i++)
		data->supplies[i].supply = af8133j_supply_names[i];
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(data->supplies),
				      data->supplies);
	if (ret)
		return ret;

	ret = iio_read_mount_matrix(dev, &data->orientation);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read mount matrix\n");

	ret = af8133j_power_up(data);
	if (ret)
		return ret;

	pm_runtime_set_active(dev);

	ret = devm_add_action_or_reset(dev, af8133j_power_down_action, data);
	if (ret)
		return ret;

	ret = af8133j_product_check(data);
	if (ret)
		return ret;

	pm_runtime_get_noresume(dev);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, 500);
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	pm_runtime_put_autosuspend(dev);

	indio_dev->info = &af8133j_info;
	indio_dev->name = "af8133j";
	indio_dev->channels = af8133j_channels;
	indio_dev->num_channels = ARRAY_SIZE(af8133j_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL,
					      &af8133j_trigger_handler, NULL);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "Failed to setup iio triggered buffer\n");

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register iio device");

	return 0;
}

static int af8133j_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct af8133j_data *data = iio_priv(indio_dev);

	af8133j_power_down(data);

	return 0;
}

static int af8133j_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct af8133j_data *data = iio_priv(indio_dev);

	return af8133j_power_up(data);
}

static const struct dev_pm_ops af8133j_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
	RUNTIME_PM_OPS(af8133j_runtime_suspend, af8133j_runtime_resume, NULL)
};

static const struct of_device_id af8133j_of_match[] = {
	{ .compatible = "voltafield,af8133j", },
	{ }
};
MODULE_DEVICE_TABLE(of, af8133j_of_match);

static const struct i2c_device_id af8133j_id[] = {
	{ "af8133j" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, af8133j_id);

static struct i2c_driver af8133j_driver = {
	.driver = {
		.name = "af8133j",
		.of_match_table = af8133j_of_match,
		.pm = pm_ptr(&af8133j_pm_ops),
	},
	.probe = af8133j_probe,
	.id_table = af8133j_id,
};

module_i2c_driver(af8133j_driver);

MODULE_AUTHOR("Icenowy Zheng <icenowy@aosc.io>");
MODULE_AUTHOR("Ondřej Jirman <megi@xff.cz>");
MODULE_DESCRIPTION("Voltafield AF8133J magnetic sensor driver");
MODULE_LICENSE("GPL");
