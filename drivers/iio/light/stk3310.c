/**
 * Sensortek STK3310/STK3311 Ambient Light and Proximity Sensor
 *
 * Copyright (c) 2015, Intel Corporation.
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License. See the file COPYING in the main
 * directory of this archive for more details.
 *
 * IIO driver for STK3310/STK3311. 7-bit I2C address: 0x48.
 */

#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define STK3310_REG_STATE			0x00
#define STK3310_REG_PSCTRL			0x01
#define STK3310_REG_ALSCTRL			0x02
#define STK3310_REG_PS_DATA_MSB			0x11
#define STK3310_REG_PS_DATA_LSB			0x12
#define STK3310_REG_ALS_DATA_MSB		0x13
#define STK3310_REG_ALS_DATA_LSB		0x14
#define STK3310_REG_ID				0x3E
#define STK3310_MAX_REG				0x80

#define STK3310_STATE_EN_PS			0x01
#define STK3310_STATE_EN_ALS			0x02
#define STK3310_STATE_STANDBY			0x00

#define STK3310_CHIP_ID_VAL			0x13
#define STK3311_CHIP_ID_VAL			0x1D
#define STK3310_PS_MAX_VAL			0xFFFF

#define STK3310_DRIVER_NAME			"stk3310"
#define STK3310_REGMAP_NAME			"stk3310_regmap"

#define STK3310_SCALE_AVAILABLE			"6.4 1.6 0.4 0.1"

#define STK3310_IT_AVAILABLE \
	"0.000185 0.000370 0.000741 0.001480 0.002960 0.005920 0.011840 " \
	"0.023680 0.047360 0.094720 0.189440 0.378880 0.757760 1.515520 " \
	"3.031040 6.062080"

#define STK3310_REGFIELD(name)						    \
	do {								    \
		data->reg_##name =					    \
			devm_regmap_field_alloc(&client->dev, regmap,	    \
				stk3310_reg_field_##name);		    \
		if (IS_ERR(data->reg_##name)) {				    \
			dev_err(&client->dev, "reg field alloc failed.\n"); \
			return PTR_ERR(data->reg_##name);		    \
		}							    \
	} while (0)

static const struct reg_field stk3310_reg_field_state =
				REG_FIELD(STK3310_REG_STATE, 0, 2);
static const struct reg_field stk3310_reg_field_als_gain =
				REG_FIELD(STK3310_REG_ALSCTRL, 4, 5);
static const struct reg_field stk3310_reg_field_ps_gain =
				REG_FIELD(STK3310_REG_PSCTRL, 4, 5);
static const struct reg_field stk3310_reg_field_als_it =
				REG_FIELD(STK3310_REG_ALSCTRL, 0, 3);
static const struct reg_field stk3310_reg_field_ps_it =
				REG_FIELD(STK3310_REG_PSCTRL, 0, 3);

/*
 * Maximum PS values with regard to scale. Used to export the 'inverse'
 * PS value (high values for far objects, low values for near objects).
 */
static const int stk3310_ps_max[4] = {
	STK3310_PS_MAX_VAL / 64,
	STK3310_PS_MAX_VAL / 16,
	STK3310_PS_MAX_VAL /  4,
	STK3310_PS_MAX_VAL,
};

static const int stk3310_scale_table[][2] = {
	{6, 400000}, {1, 600000}, {0, 400000}, {0, 100000}
};

/* Integration time in seconds, microseconds */
static const int stk3310_it_table[][2] = {
	{0, 185},	{0, 370},	{0, 741},	{0, 1480},
	{0, 2960},	{0, 5920},	{0, 11840},	{0, 23680},
	{0, 47360},	{0, 94720},	{0, 189440},	{0, 378880},
	{0, 757760},	{1, 515520},	{3, 31040},	{6, 62080},
};

struct stk3310_data {
	struct i2c_client *client;
	struct mutex lock;
	bool als_enabled;
	bool ps_enabled;
	struct regmap *regmap;
	struct regmap_field *reg_state;
	struct regmap_field *reg_als_gain;
	struct regmap_field *reg_ps_gain;
	struct regmap_field *reg_als_it;
	struct regmap_field *reg_ps_it;
};

static const struct iio_chan_spec stk3310_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_INT_TIME),
	},
	{
		.type = IIO_PROXIMITY,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_INT_TIME),
	}
};

static IIO_CONST_ATTR(in_illuminance_scale_available, STK3310_SCALE_AVAILABLE);

static IIO_CONST_ATTR(in_proximity_scale_available, STK3310_SCALE_AVAILABLE);

static IIO_CONST_ATTR(in_illuminance_integration_time_available,
		      STK3310_IT_AVAILABLE);

static IIO_CONST_ATTR(in_proximity_integration_time_available,
		      STK3310_IT_AVAILABLE);

static struct attribute *stk3310_attributes[] = {
	&iio_const_attr_in_illuminance_scale_available.dev_attr.attr,
	&iio_const_attr_in_proximity_scale_available.dev_attr.attr,
	&iio_const_attr_in_illuminance_integration_time_available.dev_attr.attr,
	&iio_const_attr_in_proximity_integration_time_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group stk3310_attribute_group = {
	.attrs = stk3310_attributes
};

static int stk3310_get_index(const int table[][2], int table_size,
			     int val, int val2)
{
	int i;

	for (i = 0; i < table_size; i++) {
		if (val == table[i][0] && val2 == table[i][1])
			return i;
	}

	return -EINVAL;
}

static int stk3310_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	u8 reg;
	u16 buf;
	int ret;
	unsigned int index;
	struct stk3310_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type == IIO_LIGHT)
			reg = STK3310_REG_ALS_DATA_MSB;
		else if (chan->type == IIO_PROXIMITY)
			reg = STK3310_REG_PS_DATA_MSB;
		else
			return -EINVAL;
		mutex_lock(&data->lock);
		ret = regmap_bulk_read(data->regmap, reg, &buf, 2);
		if (ret < 0) {
			dev_err(&client->dev, "register read failed\n");
			mutex_unlock(&data->lock);
			return ret;
		}
		*val = swab16(buf);
		if (chan->type == IIO_PROXIMITY) {
			/*
			 * Invert the proximity data so we return low values
			 * for close objects and high values for far ones.
			 */
			regmap_field_read(data->reg_ps_gain, &index);
			*val = stk3310_ps_max[index] - *val;
		}
		mutex_unlock(&data->lock);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_INT_TIME:
		if (chan->type == IIO_LIGHT)
			regmap_field_read(data->reg_als_it, &index);
		else
			regmap_field_read(data->reg_ps_it, &index);
		*val = stk3310_it_table[index][0];
		*val2 = stk3310_it_table[index][1];
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_LIGHT)
			regmap_field_read(data->reg_als_gain, &index);
		else
			regmap_field_read(data->reg_ps_gain, &index);
		*val = stk3310_scale_table[index][0];
		*val2 = stk3310_scale_table[index][1];
		return IIO_VAL_INT_PLUS_MICRO;
	}

	return -EINVAL;
}

static int stk3310_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	int ret;
	unsigned int index;
	struct stk3310_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		index = stk3310_get_index(stk3310_it_table,
					  ARRAY_SIZE(stk3310_it_table),
					  val, val2);
		if (index < 0)
			return -EINVAL;
		mutex_lock(&data->lock);
		if (chan->type == IIO_LIGHT)
			ret = regmap_field_write(data->reg_als_it, index);
		else
			ret = regmap_field_write(data->reg_ps_it, index);
		if (ret < 0)
			dev_err(&data->client->dev,
					"sensor configuration failed\n");
		mutex_unlock(&data->lock);
		return ret;

	case IIO_CHAN_INFO_SCALE:
		index = stk3310_get_index(stk3310_scale_table,
					  ARRAY_SIZE(stk3310_scale_table),
					  val, val2);
		if (index < 0)
			return -EINVAL;
		mutex_lock(&data->lock);
		if (chan->type == IIO_LIGHT)
			ret = regmap_field_write(data->reg_als_gain, index);
		else
			ret = regmap_field_write(data->reg_ps_gain, index);
		if (ret < 0)
			dev_err(&data->client->dev,
					"sensor configuration failed\n");
		mutex_unlock(&data->lock);
		return ret;
	}

	return -EINVAL;
}

static const struct iio_info stk3310_info = {
	.driver_module		= THIS_MODULE,
	.read_raw		= stk3310_read_raw,
	.write_raw		= stk3310_write_raw,
	.attrs			= &stk3310_attribute_group,
};

static int stk3310_set_state(struct stk3310_data *data, u8 state)
{
	int ret;
	struct i2c_client *client = data->client;

	/* 3-bit state; 0b100 is not supported. */
	if (state > 7 || state == 4)
		return -EINVAL;

	mutex_lock(&data->lock);
	ret = regmap_field_write(data->reg_state, state);
	if (ret < 0) {
		dev_err(&client->dev, "failed to change sensor state\n");
	} else if (state != STK3310_STATE_STANDBY) {
		/* Don't reset the 'enabled' flags if we're going in standby */
		data->ps_enabled  = !!(state & 0x01);
		data->als_enabled = !!(state & 0x02);
	}
	mutex_unlock(&data->lock);

	return ret;
}

static int stk3310_init(struct iio_dev *indio_dev)
{
	int ret;
	int chipid;
	u8 state;
	struct stk3310_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;

	regmap_read(data->regmap, STK3310_REG_ID, &chipid);
	if (chipid != STK3310_CHIP_ID_VAL &&
	    chipid != STK3311_CHIP_ID_VAL) {
		dev_err(&client->dev, "invalid chip id: 0x%x\n", chipid);
		return -ENODEV;
	}

	state = STK3310_STATE_EN_ALS | STK3310_STATE_EN_PS;
	ret = stk3310_set_state(data, state);
	if (ret < 0)
		dev_err(&client->dev, "failed to enable sensor");

	return ret;
}

static bool stk3310_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case STK3310_REG_ALS_DATA_MSB:
	case STK3310_REG_ALS_DATA_LSB:
	case STK3310_REG_PS_DATA_LSB:
	case STK3310_REG_PS_DATA_MSB:
		return true;
	default:
		return false;
	}
}

static struct regmap_config stk3310_regmap_config = {
	.name = STK3310_REGMAP_NAME,
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = STK3310_MAX_REG,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = stk3310_is_volatile_reg,
};

static int stk3310_regmap_init(struct stk3310_data *data)
{
	struct regmap *regmap;
	struct i2c_client *client;

	client = data->client;
	regmap = devm_regmap_init_i2c(client, &stk3310_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "regmap initialization failed.\n");
		return PTR_ERR(regmap);
	}
	data->regmap = regmap;

	STK3310_REGFIELD(state);
	STK3310_REGFIELD(als_gain);
	STK3310_REGFIELD(ps_gain);
	STK3310_REGFIELD(als_it);
	STK3310_REGFIELD(ps_it);

	return 0;
}

static int stk3310_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int ret;
	struct iio_dev *indio_dev;
	struct stk3310_data *data;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev) {
		dev_err(&client->dev, "iio allocation failed!\n");
		return -ENOMEM;
	}

	data = iio_priv(indio_dev);
	data->client = client;
	i2c_set_clientdata(client, indio_dev);
	mutex_init(&data->lock);

	ret = stk3310_regmap_init(data);
	if (ret < 0)
		return ret;

	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &stk3310_info;
	indio_dev->name = STK3310_DRIVER_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = stk3310_channels;
	indio_dev->num_channels = ARRAY_SIZE(stk3310_channels);

	ret = stk3310_init(indio_dev);
	if (ret < 0)
		return ret;

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "device_register failed\n");
		stk3310_set_state(data, STK3310_STATE_STANDBY);
	}

	return ret;
}

static int stk3310_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);
	return stk3310_set_state(iio_priv(indio_dev), STK3310_STATE_STANDBY);
}

#ifdef CONFIG_PM_SLEEP
static int stk3310_suspend(struct device *dev)
{
	struct stk3310_data *data;

	data = iio_priv(i2c_get_clientdata(to_i2c_client(dev)));

	return stk3310_set_state(data, STK3310_STATE_STANDBY);
}

static int stk3310_resume(struct device *dev)
{
	int state = 0;
	struct stk3310_data *data;

	data = iio_priv(i2c_get_clientdata(to_i2c_client(dev)));
	if (data->ps_enabled)
		state |= STK3310_STATE_EN_PS;
	if (data->als_enabled)
		state |= STK3310_STATE_EN_ALS;

	return stk3310_set_state(data, state);
}

static SIMPLE_DEV_PM_OPS(stk3310_pm_ops, stk3310_suspend, stk3310_resume);

#define STK3310_PM_OPS (&stk3310_pm_ops)
#else
#define STK3310_PM_OPS NULL
#endif

static const struct i2c_device_id stk3310_i2c_id[] = {
	{"STK3310", 0},
	{"STK3311", 0},
	{}
};

static const struct acpi_device_id stk3310_acpi_id[] = {
	{"STK3310", 0},
	{"STK3311", 0},
	{}
};

MODULE_DEVICE_TABLE(acpi, stk3310_acpi_id);

static struct i2c_driver stk3310_driver = {
	.driver = {
		.name = "stk3310",
		.pm = STK3310_PM_OPS,
		.acpi_match_table = ACPI_PTR(stk3310_acpi_id),
	},
	.probe =            stk3310_probe,
	.remove =           stk3310_remove,
	.id_table =         stk3310_i2c_id,
};

module_i2c_driver(stk3310_driver);

MODULE_AUTHOR("Tiberiu Breana <tiberiu.a.breana@intel.com>");
MODULE_DESCRIPTION("STK3310 Ambient Light and Proximity Sensor driver");
MODULE_LICENSE("GPL v2");
