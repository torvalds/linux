// SPDX-License-Identifier: GPL-2.0+
/*
 * VEML3235 Ambient Light Sensor
 *
 * Copyright (c) 2024, Javier Carrasco <javier.carrasco.cruz@gmail.com>
 *
 * Datasheet: https://www.vishay.com/docs/80131/veml3235.pdf
 * Appnote-80222: https://www.vishay.com/docs/80222/designingveml3235.pdf
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/iio-gts-helper.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#define VEML3235_REG_CONF       0x00
#define VEML3235_REG_WH_DATA    0x04
#define VEML3235_REG_ALS_DATA   0x05
#define VEML3235_REG_ID         0x09

#define VEML3235_CONF_SD        BIT(0)
#define VEML3235_CONF_SD0       BIT(15)

struct veml3235_rf {
	struct regmap_field *it;
	struct regmap_field *gain;
	struct regmap_field *id;
};

struct veml3235_data {
	struct i2c_client *client;
	struct device *dev;
	struct regmap *regmap;
	struct veml3235_rf rf;
	struct iio_gts gts;
};

static const struct iio_itime_sel_mul veml3235_it_sel[] = {
	GAIN_SCALE_ITIME_US(50000, 0, 1),
	GAIN_SCALE_ITIME_US(100000, 1, 2),
	GAIN_SCALE_ITIME_US(200000, 2, 4),
	GAIN_SCALE_ITIME_US(400000, 3, 8),
	GAIN_SCALE_ITIME_US(800000, 4, 16),
};

/*
 * The MSB (DG) doubles the value of the rest of the field, which leads to
 * two possible combinations to obtain gain = 2 and gain = 4. The gain
 * handling can be simplified by restricting DG = 1 to the only gain that
 * really requires it, gain = 8. Note that "X10" is a reserved value.
 */
#define VEML3235_SEL_GAIN_X1 0
#define VEML3235_SEL_GAIN_X2 1
#define VEML3235_SEL_GAIN_X4 3
#define VEML3235_SEL_GAIN_X8 7
static const struct iio_gain_sel_pair veml3235_gain_sel[] = {
	GAIN_SCALE_GAIN(1, VEML3235_SEL_GAIN_X1),
	GAIN_SCALE_GAIN(2, VEML3235_SEL_GAIN_X2),
	GAIN_SCALE_GAIN(4, VEML3235_SEL_GAIN_X4),
	GAIN_SCALE_GAIN(8, VEML3235_SEL_GAIN_X8),
};

static int veml3235_power_on(struct veml3235_data *data)
{
	int ret;

	ret = regmap_clear_bits(data->regmap, VEML3235_REG_CONF,
				VEML3235_CONF_SD | VEML3235_CONF_SD0);
	if (ret)
		return ret;

	/* Wait 4 ms to let processor & oscillator start correctly */
	fsleep(4000);

	return 0;
}

static int veml3235_shut_down(struct veml3235_data *data)
{
	return regmap_set_bits(data->regmap, VEML3235_REG_CONF,
			       VEML3235_CONF_SD | VEML3235_CONF_SD0);
}

static void veml3235_shut_down_action(void *data)
{
	veml3235_shut_down(data);
}

enum veml3235_chan {
	CH_ALS,
	CH_WHITE,
};

static const struct iio_chan_spec veml3235_channels[] = {
	{
		.type = IIO_LIGHT,
		.channel = CH_ALS,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME) |
					   BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME) |
						     BIT(IIO_CHAN_INFO_SCALE),
	},
	{
		.type = IIO_INTENSITY,
		.channel = CH_WHITE,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_BOTH,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME) |
					   BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME) |
						     BIT(IIO_CHAN_INFO_SCALE),
	},
};

static const struct regmap_range veml3235_readable_ranges[] = {
	regmap_reg_range(VEML3235_REG_CONF, VEML3235_REG_ID),
};

static const struct regmap_access_table veml3235_readable_table = {
	.yes_ranges = veml3235_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(veml3235_readable_ranges),
};

static const struct regmap_range veml3235_writable_ranges[] = {
	regmap_reg_range(VEML3235_REG_CONF, VEML3235_REG_CONF),
};

static const struct regmap_access_table veml3235_writable_table = {
	.yes_ranges = veml3235_writable_ranges,
	.n_yes_ranges = ARRAY_SIZE(veml3235_writable_ranges),
};

static const struct regmap_range veml3235_volatile_ranges[] = {
	regmap_reg_range(VEML3235_REG_WH_DATA, VEML3235_REG_ALS_DATA),
};

static const struct regmap_access_table veml3235_volatile_table = {
	.yes_ranges = veml3235_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(veml3235_volatile_ranges),
};

static const struct regmap_config veml3235_regmap_config = {
	.name = "veml3235_regmap",
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = VEML3235_REG_ID,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.rd_table = &veml3235_readable_table,
	.wr_table = &veml3235_writable_table,
	.volatile_table = &veml3235_volatile_table,
	.cache_type = REGCACHE_RBTREE,
};

static int veml3235_get_it(struct veml3235_data *data, int *val, int *val2)
{
	int ret, it_idx;

	ret = regmap_field_read(data->rf.it, &it_idx);
	if (ret)
		return ret;

	ret = iio_gts_find_int_time_by_sel(&data->gts, it_idx);
	if (ret < 0)
		return ret;

	*val2 = ret;
	*val = 0;

	return IIO_VAL_INT_PLUS_MICRO;
}

static int veml3235_set_it(struct iio_dev *indio_dev, int val, int val2)
{
	struct veml3235_data *data = iio_priv(indio_dev);
	int ret, gain_idx, it_idx, new_gain, prev_gain, prev_it;
	bool in_range;

	if (val || !iio_gts_valid_time(&data->gts, val2))
		return -EINVAL;

	ret = regmap_field_read(data->rf.it, &it_idx);
	if (ret)
		return ret;

	ret = regmap_field_read(data->rf.gain, &gain_idx);
	if (ret)
		return ret;

	prev_it = iio_gts_find_int_time_by_sel(&data->gts, it_idx);
	if (prev_it < 0)
		return prev_it;

	if (prev_it == val2)
		return 0;

	prev_gain = iio_gts_find_gain_by_sel(&data->gts, gain_idx);
	if (prev_gain < 0)
		return prev_gain;

	ret = iio_gts_find_new_gain_by_gain_time_min(&data->gts, prev_gain, prev_it,
						     val2, &new_gain, &in_range);
	if (ret)
		return ret;

	if (!in_range)
		dev_dbg(data->dev, "Optimal gain out of range\n");

	ret = iio_gts_find_sel_by_int_time(&data->gts, val2);
	if (ret < 0)
		return ret;

	ret = regmap_field_write(data->rf.it, ret);
	if (ret)
		return ret;

	ret = iio_gts_find_sel_by_gain(&data->gts, new_gain);
	if (ret < 0)
		return ret;

	return regmap_field_write(data->rf.gain, ret);
}

static int veml3235_set_scale(struct iio_dev *indio_dev, int val, int val2)
{
	struct veml3235_data *data = iio_priv(indio_dev);
	int ret, it_idx, gain_sel, time_sel;

	ret = regmap_field_read(data->rf.it, &it_idx);
	if (ret)
		return ret;

	ret = iio_gts_find_gain_time_sel_for_scale(&data->gts, val, val2,
						   &gain_sel, &time_sel);
	if (ret)
		return ret;

	ret = regmap_field_write(data->rf.it, time_sel);
	if (ret)
		return ret;

	return regmap_field_write(data->rf.gain, gain_sel);
}

static int veml3235_get_scale(struct veml3235_data *data, int *val, int *val2)
{
	int gain, it, reg, ret;

	ret = regmap_field_read(data->rf.gain, &reg);
	if (ret) {
		dev_err(data->dev, "failed to read gain %d\n", ret);
		return ret;
	}

	gain = iio_gts_find_gain_by_sel(&data->gts, reg);
	if (gain < 0)
		return gain;

	ret = regmap_field_read(data->rf.it, &reg);
	if (ret) {
		dev_err(data->dev, "failed to read integration time %d\n", ret);
		return ret;
	}

	it = iio_gts_find_int_time_by_sel(&data->gts, reg);
	if (it < 0)
		return it;

	ret = iio_gts_get_scale(&data->gts, gain, it, val, val2);
	if (ret)
		return ret;

	return IIO_VAL_INT_PLUS_NANO;
}

static int veml3235_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int *val,
			     int *val2, long mask)
{
	struct veml3235_data *data = iio_priv(indio_dev);
	struct regmap *regmap = data->regmap;
	int ret, reg;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_LIGHT:
			ret = regmap_read(regmap, VEML3235_REG_ALS_DATA, &reg);
			if (ret < 0)
				return ret;

			*val = reg;
			return IIO_VAL_INT;
		case IIO_INTENSITY:
			ret = regmap_read(regmap, VEML3235_REG_WH_DATA, &reg);
			if (ret < 0)
				return ret;

			*val = reg;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_INT_TIME:
		return veml3235_get_it(data, val, val2);
	case IIO_CHAN_INFO_SCALE:
		return veml3235_get_scale(data, val, val2);
	default:
		return -EINVAL;
	}
}

static int veml3235_read_avail(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       const int **vals, int *type, int *length,
			       long mask)
{
	struct veml3235_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		return iio_gts_avail_times(&data->gts, vals, type, length);
	case IIO_CHAN_INFO_SCALE:
		return iio_gts_all_avail_scales(&data->gts, vals, type, length);
	default:
		return -EINVAL;
	}
}

static int veml3235_write_raw_get_fmt(struct iio_dev *indio_dev,
				      struct iio_chan_spec const *chan,
				      long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_INT_TIME:
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int veml3235_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		return veml3235_set_it(indio_dev, val, val2);
	case IIO_CHAN_INFO_SCALE:
		return veml3235_set_scale(indio_dev, val, val2);
	}

	return -EINVAL;
}

static void veml3235_read_id(struct veml3235_data *data)
{
	int ret, reg;

	ret = regmap_field_read(data->rf.id, &reg);
	if (ret) {
		dev_info(data->dev, "failed to read ID\n");
		return;
	}

	if (reg != 0x35)
		dev_info(data->dev, "Unknown ID %d\n", reg);
}

static const struct reg_field veml3235_rf_it =
	REG_FIELD(VEML3235_REG_CONF, 4, 6);

static const struct reg_field veml3235_rf_gain =
	REG_FIELD(VEML3235_REG_CONF, 11, 13);

static const struct reg_field veml3235_rf_id =
	REG_FIELD(VEML3235_REG_ID, 0, 7);

static int veml3235_regfield_init(struct veml3235_data *data)
{
	struct regmap *regmap = data->regmap;
	struct device *dev = data->dev;
	struct regmap_field *rm_field;
	struct veml3235_rf *rf = &data->rf;

	rm_field = devm_regmap_field_alloc(dev, regmap, veml3235_rf_it);
	if (IS_ERR(rm_field))
		return PTR_ERR(rm_field);
	rf->it = rm_field;

	rm_field = devm_regmap_field_alloc(dev, regmap, veml3235_rf_gain);
	if (IS_ERR(rm_field))
		return PTR_ERR(rm_field);
	rf->gain = rm_field;

	rm_field = devm_regmap_field_alloc(dev, regmap, veml3235_rf_id);
	if (IS_ERR(rm_field))
		return PTR_ERR(rm_field);
	rf->id = rm_field;

	return 0;
}

static int veml3235_hw_init(struct iio_dev *indio_dev)
{
	struct veml3235_data *data = iio_priv(indio_dev);
	struct device *dev = data->dev;
	int ret;

	ret = devm_iio_init_iio_gts(data->dev, 0, 272640000,
				    veml3235_gain_sel, ARRAY_SIZE(veml3235_gain_sel),
				    veml3235_it_sel, ARRAY_SIZE(veml3235_it_sel),
				    &data->gts);
	if (ret)
		return dev_err_probe(data->dev, ret, "failed to init iio gts\n");

	/* Set gain to 1 and integration time to 100 ms */
	ret = regmap_field_write(data->rf.gain, 0x00);
	if (ret)
		return dev_err_probe(data->dev, ret, "failed to set gain\n");

	ret = regmap_field_write(data->rf.it, 0x01);
	if (ret)
		return dev_err_probe(data->dev, ret,
				     "failed to set integration time\n");

	ret = veml3235_power_on(data);
	if (ret)
		return dev_err_probe(dev, ret, "failed to power on\n");

	return devm_add_action_or_reset(dev, veml3235_shut_down_action, data);
}

static const struct iio_info veml3235_info = {
	.read_raw = veml3235_read_raw,
	.read_avail = veml3235_read_avail,
	.write_raw = veml3235_write_raw,
	.write_raw_get_fmt = veml3235_write_raw_get_fmt,
};

static int veml3235_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct veml3235_data *data;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	int ret;

	regmap = devm_regmap_init_i2c(client, &veml3235_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "failed to setup regmap\n");

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	data->dev = dev;
	data->regmap = regmap;

	ret = veml3235_regfield_init(data);
	if (ret)
		return dev_err_probe(dev, ret, "failed to init regfield\n");

	ret = devm_regulator_get_enable(dev, "vdd");
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable regulator\n");

	indio_dev->name = "veml3235";
	indio_dev->channels = veml3235_channels;
	indio_dev->num_channels = ARRAY_SIZE(veml3235_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &veml3235_info;

	veml3235_read_id(data);

	ret = veml3235_hw_init(indio_dev);
	if (ret < 0)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static int veml3235_runtime_suspend(struct device *dev)
{
	struct veml3235_data *data = iio_priv(dev_get_drvdata(dev));
	int ret;

	ret = veml3235_shut_down(data);
	if (ret < 0)
		dev_err(data->dev, "failed to suspend: %d\n", ret);

	return ret;
}

static int veml3235_runtime_resume(struct device *dev)
{
	struct veml3235_data *data = iio_priv(dev_get_drvdata(dev));
	int ret;

	ret = veml3235_power_on(data);
	if (ret < 0)
		dev_err(data->dev, "failed to resume: %d\n", ret);

	return ret;
}

static DEFINE_RUNTIME_DEV_PM_OPS(veml3235_pm_ops, veml3235_runtime_suspend,
				 veml3235_runtime_resume, NULL);

static const struct of_device_id veml3235_of_match[] = {
	{ .compatible = "vishay,veml3235" },
	{ }
};
MODULE_DEVICE_TABLE(of, veml3235_of_match);

static const struct i2c_device_id veml3235_id[] = {
	{ "veml3235" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, veml3235_id);

static struct i2c_driver veml3235_driver = {
	.driver = {
		.name = "veml3235",
		.of_match_table = veml3235_of_match,
		.pm = pm_ptr(&veml3235_pm_ops),
	},
	.probe = veml3235_probe,
	.id_table = veml3235_id,
};
module_i2c_driver(veml3235_driver);

MODULE_AUTHOR("Javier Carrasco <javier.carrasco.cruz@gmail.com>");
MODULE_DESCRIPTION("VEML3235 Ambient Light Sensor");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_GTS_HELPER");
