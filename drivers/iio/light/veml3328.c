// SPDX-License-Identifier: GPL-2.0
/*
 * Vishay VEML3328 RGBCIR light sensor driver
 *
 * Copyright (c) 2026 Joshua Crofts <joshua.crofts1@gmail.com>
 *
 * Datasheet: https://www.vishay.com/docs/84968/veml3328.pdf
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/time.h>
#include <linux/types.h>

#include <linux/iio/iio.h>

#define VEML3328_REG_CONF		0x00
#define VEML3328_REG_ID			0x0c
#define VEML3328_REG_DATA_C		0x04
#define VEML3328_REG_DATA_R		0x05
#define VEML3328_REG_DATA_G		0x06
#define VEML3328_REG_DATA_B		0x07
#define VEML3328_REG_DATA_IR		0x08

#define VEML3328_CONF_IT_MASK		GENMASK(5, 4)
#define VEML3328_CONF_GAIN_MASK		GENMASK(11, 10)

#define VEML3328_MAX_IT_TIME		(400 * USEC_PER_MSEC)

#define VEML3328_ID_VAL			0x28

#define VEML3328_SHUTDOWN		(BIT(0) | BIT(15))

struct veml3328_data {
	struct regmap *regmap;
	/* Ensure read-modify-write sequences are not interrupted. */
	struct mutex lock;
};

static const struct regmap_config veml3328_regmap_config = {
	.name = "veml3328",
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = VEML3328_REG_ID,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};

#define VEML3328_CHAN_SPEC(_color, _addr) { \
	.type = IIO_INTENSITY, \
	.modified = 1, \
	.channel2 = IIO_MOD_LIGHT_##_color, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME), \
	.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME), \
	.address = _addr, \
}

static const struct iio_chan_spec veml3328_channels[] = {
	{
		.type = IIO_LIGHT,

		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME),
		.address = VEML3328_REG_DATA_G,
	},
	VEML3328_CHAN_SPEC(CLEAR, VEML3328_REG_DATA_C),
	VEML3328_CHAN_SPEC(RED, VEML3328_REG_DATA_R),
	VEML3328_CHAN_SPEC(GREEN, VEML3328_REG_DATA_G),
	VEML3328_CHAN_SPEC(BLUE, VEML3328_REG_DATA_B),
	VEML3328_CHAN_SPEC(IR, VEML3328_REG_DATA_IR),
};

/*
 * Precomputed scale values (micro units).
 * Formula for calculation: 0.384 * (50000 / IT_us) * (1 / Gain)
 * Gain indexes: 0 (x0.5), 1 (x1), 2 (x2), 3 (x4)
 * IT indexes: 0 (50ms), 1 (100ms), 2 (200ms), 3 (400ms)
 */
static const int veml3328_scale_vals[4][8] = {
	{ 0, 768000, 0, 384000, 0, 192000, 0, 96000 },
	{ 0, 384000, 0, 192000, 0, 96000,  0, 48000 },
	{ 0, 192000, 0, 96000,  0, 48000,  0, 24000 },
	{ 0, 96000,  0, 48000,  0, 24000,  0, 12000 },
};

/* integration times in microseconds */
static const int veml3328_it_times[][2] = {
	{ 0, 50 * USEC_PER_MSEC },
	{ 0, 100 * USEC_PER_MSEC },
	{ 0, 200 * USEC_PER_MSEC },
	{ 0, 400 * USEC_PER_MSEC },
};

static int veml3328_power_down(struct veml3328_data *data)
{
	return regmap_set_bits(data->regmap, VEML3328_REG_CONF,
			       VEML3328_SHUTDOWN);
}

static int veml3328_power_up(struct veml3328_data *data)
{
	int ret;

	ret = regmap_clear_bits(data->regmap, VEML3328_REG_CONF,
				VEML3328_SHUTDOWN);
	if (ret)
		return ret;

	/*
	 * Sleep for maximum integration time to ensure sensor is powered on
	 * correctly.
	 */
	fsleep(VEML3328_MAX_IT_TIME);

	return 0;
}

static void veml3328_power_down_action(void *data)
{
	veml3328_power_down(data);
}

static int veml3328_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct veml3328_data *data = iio_priv(indio_dev);
	struct regmap *regmap = data->regmap;
	struct device *dev = regmap_get_device(regmap);
	unsigned int reg_val;
	int it_inx, gain_inx;
	int ret;

	PM_RUNTIME_ACQUIRE_IF_ENABLED_AUTOSUSPEND(dev, pm);
	ret = PM_RUNTIME_ACQUIRE_ERR(&pm);
	if (ret)
		return ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = regmap_read(regmap, chan->address, &reg_val);
		if (ret)
			return ret;

		*val = reg_val;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_INT_TIME:
		ret = regmap_read(regmap, VEML3328_REG_CONF, &reg_val);
		if (ret)
			return ret;

		it_inx = FIELD_GET(VEML3328_CONF_IT_MASK, reg_val);
		if (it_inx >= ARRAY_SIZE(veml3328_it_times))
			return -EINVAL;

		*val = veml3328_it_times[it_inx][0];
		*val2 = veml3328_it_times[it_inx][1];
		return IIO_VAL_INT_PLUS_MICRO;

	case IIO_CHAN_INFO_SCALE:
		ret = regmap_read(regmap, VEML3328_REG_CONF, &reg_val);
		if (ret)
			return ret;

		it_inx = FIELD_GET(VEML3328_CONF_IT_MASK, reg_val);
		gain_inx = FIELD_GET(VEML3328_CONF_GAIN_MASK, reg_val);

		if (it_inx >= ARRAY_SIZE(veml3328_it_times) || gain_inx >= 4)
			return -EINVAL;

		/* Stride by 2 through the flattened array to match (val, val2) */
		*val = veml3328_scale_vals[it_inx][gain_inx * 2];
		*val2 = veml3328_scale_vals[it_inx][gain_inx * 2 + 1];

		return IIO_VAL_INT_PLUS_MICRO;

	default:
		return -EINVAL;
	}
}

static int veml3328_read_avail(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       const int **vals, int *type, int *length,
			       long mask)
{
	struct veml3328_data *data = iio_priv(indio_dev);
	struct regmap *regmap = data->regmap;
	struct device *dev = regmap_get_device(data->regmap);
	unsigned int reg_val;
	int ret, it_inx;

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		*length = ARRAY_SIZE(veml3328_it_times) * 2;
		*vals = (const int *)veml3328_it_times;
		*type = IIO_VAL_INT_PLUS_MICRO;
		return IIO_AVAIL_LIST;

	case IIO_CHAN_INFO_SCALE: {
		PM_RUNTIME_ACQUIRE_IF_ENABLED_AUTOSUSPEND(dev, pm);
		ret = PM_RUNTIME_ACQUIRE_ERR(&pm);
		if (ret)
			return ret;

		ret = regmap_read(regmap, VEML3328_REG_CONF, &reg_val);
		if (ret)
			return ret;

		it_inx = FIELD_GET(VEML3328_CONF_IT_MASK, reg_val);
		if (it_inx >= ARRAY_SIZE(veml3328_it_times))
			return -EINVAL;

		*length = 8;
		*vals = (const int *)veml3328_scale_vals[it_inx];
		*type = IIO_VAL_INT_PLUS_MICRO;
		return IIO_AVAIL_LIST;
	}
	default:
		return -EINVAL;
	}
}

static int veml3328_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct veml3328_data *data = iio_priv(indio_dev);
	struct regmap *regmap = data->regmap;
	struct device *dev = regmap_get_device(regmap);
	unsigned int reg_val;
	int i, it_inx;
	int ret;

	PM_RUNTIME_ACQUIRE_IF_ENABLED_AUTOSUSPEND(dev, pm);
	ret = PM_RUNTIME_ACQUIRE_ERR(&pm);
	if (ret)
		return ret;

	guard(mutex)(&data->lock);

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		if (val != 0)
			return -EINVAL;

		for (i = 0; i < ARRAY_SIZE(veml3328_it_times); i++) {
			if (veml3328_it_times[i][1] == val2)
				break;
		}

		if (i == ARRAY_SIZE(veml3328_it_times))
			return -EINVAL;

		return regmap_update_bits(regmap, VEML3328_REG_CONF,
					  VEML3328_CONF_IT_MASK,
					  FIELD_PREP(VEML3328_CONF_IT_MASK, i));

	case IIO_CHAN_INFO_SCALE:
		ret = regmap_read(regmap, VEML3328_REG_CONF, &reg_val);
		if (ret)
			return ret;

		it_inx = FIELD_GET(VEML3328_CONF_IT_MASK, reg_val);
		if (it_inx >= ARRAY_SIZE(veml3328_it_times))
			return -EINVAL;

		for (i = 0; i < 4; i++) {
			if (val == veml3328_scale_vals[it_inx][i * 2] &&
			    val2 == veml3328_scale_vals[it_inx][i * 2 + 1])
				break;
		}

		if (i == 4)
			return -EINVAL;

		return regmap_update_bits(regmap, VEML3328_REG_CONF,
					  VEML3328_CONF_GAIN_MASK,
					  FIELD_PREP(VEML3328_CONF_GAIN_MASK, i));

	default:
		return -EINVAL;
	}
}

static const struct iio_info veml3328_info = {
	.read_raw = veml3328_read_raw,
	.write_raw = veml3328_write_raw,
	.read_avail = veml3328_read_avail,
};

static int veml3328_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct veml3328_data *data;
	struct iio_dev *indio_dev;
	unsigned int reg_val;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);

	data->regmap = devm_regmap_init_i2c(client, &veml3328_regmap_config);
	if (IS_ERR(data->regmap))
		return dev_err_probe(dev, PTR_ERR(data->regmap),
				     "Failed to initialize regmap\n");

	ret = devm_mutex_init(dev, &data->lock);
	if (ret)
		return ret;

	ret = devm_regulator_get_enable(dev, "vdd");
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable regulator\n");

	ret = regmap_read(data->regmap, VEML3328_REG_ID, &reg_val);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read ID register\n");

	if ((reg_val & 0xff) != VEML3328_ID_VAL)
		dev_warn(dev, "Unknown device ID: 0x%02x\n", reg_val & 0xff);

	ret = veml3328_power_up(data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to power on sensor\n");

	ret = devm_add_action_or_reset(dev, veml3328_power_down_action, data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register teardown\n");

	indio_dev->name = "veml3328";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &veml3328_info;
	indio_dev->channels = veml3328_channels;
	indio_dev->num_channels = ARRAY_SIZE(veml3328_channels);

	pm_runtime_set_active(dev);
	pm_runtime_set_autosuspend_delay(dev, 2000);
	pm_runtime_use_autosuspend(dev);

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable runtime PM\n");

	return devm_iio_device_register(dev, indio_dev);
}

static int veml3328_runtime_suspend(struct device *dev)
{
	struct veml3328_data *data = iio_priv(dev_get_drvdata(dev));
	int ret;

	ret = veml3328_power_down(data);
	if (ret)
		dev_err(dev, "Failed to suspend: %d\n", ret);

	return ret;
}

static int veml3328_runtime_resume(struct device *dev)
{
	struct veml3328_data *data = iio_priv(dev_get_drvdata(dev));
	int ret;

	ret = veml3328_power_up(data);
	if (ret)
		dev_err(dev, "Failed to resume: %d\n", ret);

	return ret;
}

static DEFINE_RUNTIME_DEV_PM_OPS(veml3328_pm_ops,
				 veml3328_runtime_suspend,
				 veml3328_runtime_resume,
				 NULL);

static const struct of_device_id veml3328_of_match[] = {
	{ .compatible = "vishay,veml3328" },
	{ }
};
MODULE_DEVICE_TABLE(of, veml3328_of_match);

static const struct i2c_device_id veml3328_id[] = {
	{ .name = "veml3328" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, veml3328_id);

static struct i2c_driver veml3328_driver = {
	.driver = {
		.name = "veml3328",
		.of_match_table = veml3328_of_match,
		.pm = pm_ptr(&veml3328_pm_ops),
	},
	.probe = veml3328_probe,
	.id_table = veml3328_id,
};
module_i2c_driver(veml3328_driver);

MODULE_AUTHOR("Joshua Crofts <joshua.crofts1@gmail.com>");
MODULE_DESCRIPTION("VEML3328 RGBCIR Light Sensor");
MODULE_LICENSE("GPL");
