// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the Allegro MicroSystems ALS31300 3-D Linear Hall Effect Sensor
 *
 * Copyright (c) 2024 Linaro Limited
 */

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/units.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

/*
 * The Allegro MicroSystems ALS31300 has an EEPROM space to configure how
 * the device works and how the interrupt line behaves.
 * Only the default setup with external trigger is supported.
 *
 * While the bindings supports declaring an interrupt line, those
 * events are not supported.
 *
 * It should be possible to adapt the driver to the current
 * device EEPROM setup at runtime.
 */

#define ALS31300_EEPROM_CONFIG		0x02
#define ALS31300_EEPROM_INTERRUPT	0x03
#define ALS31300_EEPROM_CUSTOMER_1	0x0d
#define ALS31300_EEPROM_CUSTOMER_2	0x0e
#define ALS31300_EEPROM_CUSTOMER_3	0x0f
#define ALS31300_VOL_MODE		0x27
#define ALS31300_VOL_MODE_LPDCM			GENMASK(6, 4)
#define   ALS31300_LPDCM_INACTIVE_0_5_MS	0
#define   ALS31300_LPDCM_INACTIVE_1_0_MS	1
#define   ALS31300_LPDCM_INACTIVE_5_0_MS	2
#define   ALS31300_LPDCM_INACTIVE_10_0_MS	3
#define   ALS31300_LPDCM_INACTIVE_50_0_MS	4
#define   ALS31300_LPDCM_INACTIVE_100_0_MS	5
#define   ALS31300_LPDCM_INACTIVE_500_0_MS	6
#define   ALS31300_LPDCM_INACTIVE_1000_0_MS	7
#define ALS31300_VOL_MODE_SLEEP			GENMASK(1, 0)
#define   ALS31300_VOL_MODE_ACTIVE_MODE		0
#define   ALS31300_VOL_MODE_SLEEP_MODE		1
#define   ALS31300_VOL_MODE_LPDCM_MODE		2
#define ALS31300_VOL_MSB		0x28
#define ALS31300_VOL_MSB_TEMPERATURE		GENMASK(5, 0)
#define ALS31300_VOL_MSB_INTERRUPT		BIT(6)
#define ALS31300_VOL_MSB_NEW_DATA		BIT(7)
#define ALS31300_VOL_MSB_Z_AXIS			GENMASK(15, 8)
#define ALS31300_VOL_MSB_Y_AXIS			GENMASK(23, 16)
#define ALS31300_VOL_MSB_X_AXIS			GENMASK(31, 24)
#define ALS31300_VOL_LSB		0x29
#define ALS31300_VOL_LSB_TEMPERATURE		GENMASK(5, 0)
#define ALS31300_VOL_LSB_HALL_STATUS		GENMASK(7, 7)
#define ALS31300_VOL_LSB_Z_AXIS			GENMASK(11, 8)
#define ALS31300_VOL_LSB_Y_AXIS			GENMASK(15, 12)
#define ALS31300_VOL_LSB_X_AXIS			GENMASK(19, 16)
#define ALS31300_VOL_LSB_INTERRUPT_WRITE	BIT(20)
#define ALS31300_CUSTOMER_ACCESS	0x35

#define ALS31300_DATA_X_GET(b)		\
		sign_extend32(FIELD_GET(ALS31300_VOL_MSB_X_AXIS, b[0]) << 4 | \
			      FIELD_GET(ALS31300_VOL_LSB_X_AXIS, b[1]), 11)
#define ALS31300_DATA_Y_GET(b)		\
		sign_extend32(FIELD_GET(ALS31300_VOL_MSB_Y_AXIS, b[0]) << 4 | \
			      FIELD_GET(ALS31300_VOL_LSB_Y_AXIS, b[1]), 11)
#define ALS31300_DATA_Z_GET(b)		\
		sign_extend32(FIELD_GET(ALS31300_VOL_MSB_Z_AXIS, b[0]) << 4 | \
			      FIELD_GET(ALS31300_VOL_LSB_Z_AXIS, b[1]), 11)
#define ALS31300_TEMPERATURE_GET(b)	\
		(FIELD_GET(ALS31300_VOL_MSB_TEMPERATURE, b[0]) << 6 | \
		 FIELD_GET(ALS31300_VOL_LSB_TEMPERATURE, b[1]))

enum als31300_channels {
	TEMPERATURE = 0,
	AXIS_X,
	AXIS_Y,
	AXIS_Z,
};

struct als31300_variant_info {
	u8 sensitivity;
};

struct als31300_data {
	struct device *dev;
	/* protects power on/off the device and access HW */
	struct mutex mutex;
	const struct als31300_variant_info *variant_info;
	struct regmap *map;
};

/* The whole measure is split into 2x32-bit registers, we need to read them both at once */
static int als31300_get_measure(struct als31300_data *data,
				u16 *t, s16 *x, s16 *y, s16 *z)
{
	u32 buf[2];
	int ret, err;

	guard(mutex)(&data->mutex);

	ret = pm_runtime_resume_and_get(data->dev);
	if (ret)
		return ret;

	/*
	 * Loop until data is valid, new data should have the
	 * ALS31300_VOL_MSB_NEW_DATA bit set to 1.
	 * Max update rate is 2KHz, wait up to 1ms.
	 */
	ret = read_poll_timeout(regmap_bulk_read, err,
				err || FIELD_GET(ALS31300_VOL_MSB_NEW_DATA, buf[0]),
				20, USEC_PER_MSEC, false,
				data->map, ALS31300_VOL_MSB, buf, ARRAY_SIZE(buf));
	/* Bail out on read_poll_timeout() error */
	if (ret)
		goto out;

	/* Bail out on regmap_bulk_read() error */
	if (err) {
		dev_err(data->dev, "read data failed, error %d\n", ret);
		ret = err;
		goto out;
	}

	*t = ALS31300_TEMPERATURE_GET(buf);
	*x = ALS31300_DATA_X_GET(buf);
	*y = ALS31300_DATA_Y_GET(buf);
	*z = ALS31300_DATA_Z_GET(buf);

out:
	pm_runtime_put_autosuspend(data->dev);

	return ret;
}

static int als31300_read_raw(struct iio_dev *indio_dev,
			     const struct iio_chan_spec *chan, int *val,
			     int *val2, long mask)
{
	struct als31300_data *data = iio_priv(indio_dev);
	s16 x, y, z;
	u16 t;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = als31300_get_measure(data, &t, &x, &y, &z);
		if (ret)
			return ret;

		switch (chan->address) {
		case TEMPERATURE:
			*val = t;
			return IIO_VAL_INT;
		case AXIS_X:
			*val = x;
			return IIO_VAL_INT;
		case AXIS_Y:
			*val = y;
			return IIO_VAL_INT;
		case AXIS_Z:
			*val = z;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_TEMP:
			/*
			 * Fractional part of:
			 *         1000 * 302 * (value - 1708)
			 * temp = ----------------------------
			 *             4096
			 * to convert temperature in millicelcius.
			 */
			*val = MILLI * 302;
			*val2 = 4096;
			return IIO_VAL_FRACTIONAL;
		case IIO_MAGN:
			/*
			 * Devices are configured in factory
			 * with different sensitivities:
			 * - 500 GAUSS <-> 4 LSB/Gauss
			 * - 1000 GAUSS <-> 2 LSB/Gauss
			 * - 2000 GAUSS <-> 1 LSB/Gauss
			 * with translates by a division of the returned
			 * value to get Gauss value.
			 * The sensitivity cannot be read at runtime
			 * so the value depends on the model compatible
			 * or device id.
			 */
			*val = 1;
			*val2 = data->variant_info->sensitivity;
			return IIO_VAL_FRACTIONAL;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		switch (chan->type) {
		case IIO_TEMP:
			*val = -1708;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static irqreturn_t als31300_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct als31300_data *data = iio_priv(indio_dev);
	struct {
		u16 temperature;
		s16 channels[3];
		aligned_s64 timestamp;
	} scan;
	s16 x, y, z;
	int ret;
	u16 t;

	ret = als31300_get_measure(data, &t, &x, &y, &z);
	if (ret)
		goto trigger_out;

	scan.temperature = t;
	scan.channels[0] = x;
	scan.channels[1] = y;
	scan.channels[2] = z;
	iio_push_to_buffers_with_ts(indio_dev, &scan, sizeof(scan), pf->timestamp);

trigger_out:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

#define ALS31300_AXIS_CHANNEL(axis, index)				     \
	{								     \
		.type = IIO_MAGN,					     \
		.modified = 1,						     \
		.channel2 = IIO_MOD_##axis,				     \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		     \
				      BIT(IIO_CHAN_INFO_SCALE),		     \
		.address = index,					     \
		.scan_index = index,					     \
		.scan_type = {						     \
			.sign = 's',					     \
			.realbits = 12,					     \
			.storagebits = 16,				     \
			.endianness = IIO_CPU,				     \
		},							     \
	}

static const struct iio_chan_spec als31300_channels[] = {
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_OFFSET),
		.address = TEMPERATURE,
		.scan_index = TEMPERATURE,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_CPU,
		},
	},
	ALS31300_AXIS_CHANNEL(X, AXIS_X),
	ALS31300_AXIS_CHANNEL(Y, AXIS_Y),
	ALS31300_AXIS_CHANNEL(Z, AXIS_Z),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static const struct iio_info als31300_info = {
	.read_raw = als31300_read_raw,
};

static int als31300_set_operating_mode(struct als31300_data *data,
				       unsigned int val)
{
	int ret;

	ret = regmap_update_bits(data->map, ALS31300_VOL_MODE,
				 ALS31300_VOL_MODE_SLEEP, val);
	if (ret) {
		dev_err(data->dev, "failed to set operating mode (%pe)\n", ERR_PTR(ret));
		return ret;
	}

	/* The time it takes to exit sleep mode is equivalent to Power-On Delay Time */
	if (val == ALS31300_VOL_MODE_ACTIVE_MODE)
		fsleep(600);

	return 0;
}

static void als31300_power_down(void *data)
{
	als31300_set_operating_mode(data, ALS31300_VOL_MODE_SLEEP_MODE);
}

static const struct iio_buffer_setup_ops als31300_setup_ops = {};

static const unsigned long als31300_scan_masks[] = { GENMASK(3, 0), 0 };

static bool als31300_volatile_reg(struct device *dev, unsigned int reg)
{
	return reg == ALS31300_VOL_MSB || reg == ALS31300_VOL_LSB;
}

static const struct regmap_config als31300_regmap_config = {
	.reg_bits = 8,
	.val_bits = 32,
	.max_register = ALS31300_CUSTOMER_ACCESS,
	.volatile_reg = als31300_volatile_reg,
};

static int als31300_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct als31300_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->dev = dev;
	i2c_set_clientdata(i2c, indio_dev);

	ret = devm_mutex_init(dev, &data->mutex);
	if (ret)
		return ret;

	data->variant_info = i2c_get_match_data(i2c);
	if (!data->variant_info)
		return -EINVAL;

	data->map = devm_regmap_init_i2c(i2c, &als31300_regmap_config);
	if (IS_ERR(data->map))
		return dev_err_probe(dev, PTR_ERR(data->map),
				     "failed to allocate register map\n");

	ret = devm_regulator_get_enable(dev, "vcc");
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable regulator\n");

	ret = als31300_set_operating_mode(data, ALS31300_VOL_MODE_ACTIVE_MODE);
	if (ret)
		return dev_err_probe(dev, ret, "failed to power on device\n");

	ret = devm_add_action_or_reset(dev, als31300_power_down, data);
	if (ret)
		return ret;

	indio_dev->info = &als31300_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->name = i2c->name;
	indio_dev->channels = als31300_channels;
	indio_dev->num_channels = ARRAY_SIZE(als31300_channels);
	indio_dev->available_scan_masks = als31300_scan_masks;

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
					      iio_pollfunc_store_time,
					      als31300_trigger_handler,
					      &als31300_setup_ops);
	if (ret < 0)
		return dev_err_probe(dev, ret, "iio triggered buffer setup failed\n");

	ret = pm_runtime_set_active(dev);
	if (ret < 0)
		return ret;

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	pm_runtime_get_noresume(dev);
	pm_runtime_set_autosuspend_delay(dev, 200);
	pm_runtime_use_autosuspend(dev);

	pm_runtime_put_autosuspend(dev);

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "device register failed\n");

	return 0;
}

static int als31300_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct als31300_data *data = iio_priv(indio_dev);

	return als31300_set_operating_mode(data, ALS31300_VOL_MODE_SLEEP_MODE);
}

static int als31300_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct als31300_data *data = iio_priv(indio_dev);

	return als31300_set_operating_mode(data, ALS31300_VOL_MODE_ACTIVE_MODE);
}

static DEFINE_RUNTIME_DEV_PM_OPS(als31300_pm_ops,
				 als31300_runtime_suspend, als31300_runtime_resume,
				 NULL);

static const struct als31300_variant_info al31300_variant_500 = {
	.sensitivity = 4,
};

static const struct als31300_variant_info al31300_variant_1000 = {
	.sensitivity = 2,
};

static const struct als31300_variant_info al31300_variant_2000 = {
	.sensitivity = 1,
};

static const struct i2c_device_id als31300_id[] = {
	{
		.name = "als31300-500",
		.driver_data = (kernel_ulong_t)&al31300_variant_500,
	},
	{
		.name = "als31300-1000",
		.driver_data = (kernel_ulong_t)&al31300_variant_1000,
	},
	{
		.name = "als31300-2000",
		.driver_data = (kernel_ulong_t)&al31300_variant_2000,
	},
	{ }
};
MODULE_DEVICE_TABLE(i2c, als31300_id);

static const struct of_device_id als31300_of_match[] = {
	{
		.compatible = "allegromicro,als31300-500",
		.data = &al31300_variant_500,
	},
	{
		.compatible = "allegromicro,als31300-1000",
		.data = &al31300_variant_1000,
	},
	{
		.compatible = "allegromicro,als31300-2000",
		.data = &al31300_variant_2000,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, als31300_of_match);

static struct i2c_driver als31300_driver = {
	.driver	 = {
		.name = "als31300",
		.of_match_table = als31300_of_match,
		.pm = pm_ptr(&als31300_pm_ops),
	},
	.probe = als31300_probe,
	.id_table = als31300_id,
};
module_i2c_driver(als31300_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ALS31300 3-D Linear Hall Effect Driver");
MODULE_AUTHOR("Neil Armstrong <neil.armstrong@linaro.org>");
