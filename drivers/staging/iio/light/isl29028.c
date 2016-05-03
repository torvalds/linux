/*
 * IIO driver for the light sensor ISL29028.
 * ISL29028 is Concurrent Ambient Light and Proximity Sensor
 *
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define CONVERSION_TIME_MS		100

#define ISL29028_REG_CONFIGURE		0x01

#define CONFIGURE_ALS_IR_MODE_ALS	0
#define CONFIGURE_ALS_IR_MODE_IR	BIT(0)
#define CONFIGURE_ALS_IR_MODE_MASK	BIT(0)

#define CONFIGURE_ALS_RANGE_LOW_LUX	0
#define CONFIGURE_ALS_RANGE_HIGH_LUX	BIT(1)
#define CONFIGURE_ALS_RANGE_MASK	BIT(1)

#define CONFIGURE_ALS_DIS		0
#define CONFIGURE_ALS_EN		BIT(2)
#define CONFIGURE_ALS_EN_MASK		BIT(2)

#define CONFIGURE_PROX_DRIVE		BIT(3)

#define CONFIGURE_PROX_SLP_SH		4
#define CONFIGURE_PROX_SLP_MASK		(7 << CONFIGURE_PROX_SLP_SH)

#define CONFIGURE_PROX_EN		BIT(7)
#define CONFIGURE_PROX_EN_MASK		BIT(7)

#define ISL29028_REG_INTERRUPT		0x02

#define ISL29028_REG_PROX_DATA		0x08
#define ISL29028_REG_ALSIR_L		0x09
#define ISL29028_REG_ALSIR_U		0x0A

#define ISL29028_REG_TEST1_MODE		0x0E
#define ISL29028_REG_TEST2_MODE		0x0F

#define ISL29028_NUM_REGS		(ISL29028_REG_TEST2_MODE + 1)

enum als_ir_mode {
	MODE_NONE = 0,
	MODE_ALS,
	MODE_IR
};

struct isl29028_chip {
	struct device		*dev;
	struct mutex		lock;
	struct regmap		*regmap;

	unsigned int		prox_sampling;
	bool			enable_prox;

	int			lux_scale;
	int			als_ir_mode;
};

static int isl29028_set_proxim_sampling(struct isl29028_chip *chip,
					unsigned int sampling)
{
	static unsigned int prox_period[] = {800, 400, 200, 100, 75, 50, 12, 0};
	int sel;
	unsigned int period = DIV_ROUND_UP(1000, sampling);

	for (sel = 0; sel < ARRAY_SIZE(prox_period); ++sel) {
		if (period >= prox_period[sel])
			break;
	}
	return regmap_update_bits(chip->regmap, ISL29028_REG_CONFIGURE,
			CONFIGURE_PROX_SLP_MASK, sel << CONFIGURE_PROX_SLP_SH);
}

static int isl29028_enable_proximity(struct isl29028_chip *chip, bool enable)
{
	int ret;
	int val = 0;

	if (enable)
		val = CONFIGURE_PROX_EN;
	ret = regmap_update_bits(chip->regmap, ISL29028_REG_CONFIGURE,
				 CONFIGURE_PROX_EN_MASK, val);
	if (ret < 0)
		return ret;

	/* Wait for conversion to be complete for first sample */
	mdelay(DIV_ROUND_UP(1000, chip->prox_sampling));
	return 0;
}

static int isl29028_set_als_scale(struct isl29028_chip *chip, int lux_scale)
{
	int val = (lux_scale == 2000) ? CONFIGURE_ALS_RANGE_HIGH_LUX :
					CONFIGURE_ALS_RANGE_LOW_LUX;

	return regmap_update_bits(chip->regmap, ISL29028_REG_CONFIGURE,
		CONFIGURE_ALS_RANGE_MASK, val);
}

static int isl29028_set_als_ir_mode(struct isl29028_chip *chip,
				    enum als_ir_mode mode)
{
	int ret = 0;

	switch (mode) {
	case MODE_ALS:
		ret = regmap_update_bits(chip->regmap, ISL29028_REG_CONFIGURE,
					 CONFIGURE_ALS_IR_MODE_MASK,
					 CONFIGURE_ALS_IR_MODE_ALS);
		if (ret < 0)
			return ret;

		ret = regmap_update_bits(chip->regmap, ISL29028_REG_CONFIGURE,
					 CONFIGURE_ALS_RANGE_MASK,
					 CONFIGURE_ALS_RANGE_HIGH_LUX);
		break;

	case MODE_IR:
		ret = regmap_update_bits(chip->regmap, ISL29028_REG_CONFIGURE,
					 CONFIGURE_ALS_IR_MODE_MASK,
					 CONFIGURE_ALS_IR_MODE_IR);
		break;

	case MODE_NONE:
		return regmap_update_bits(chip->regmap, ISL29028_REG_CONFIGURE,
			CONFIGURE_ALS_EN_MASK, CONFIGURE_ALS_DIS);
	}

	if (ret < 0)
		return ret;

	/* Enable the ALS/IR */
	ret = regmap_update_bits(chip->regmap, ISL29028_REG_CONFIGURE,
				 CONFIGURE_ALS_EN_MASK, CONFIGURE_ALS_EN);
	if (ret < 0)
		return ret;

	/* Need to wait for conversion time if ALS/IR mode enabled */
	mdelay(CONVERSION_TIME_MS);
	return 0;
}

static int isl29028_read_als_ir(struct isl29028_chip *chip, int *als_ir)
{
	unsigned int lsb;
	unsigned int msb;
	int ret;

	ret = regmap_read(chip->regmap, ISL29028_REG_ALSIR_L, &lsb);
	if (ret < 0) {
		dev_err(chip->dev,
			"Error in reading register ALSIR_L err %d\n", ret);
		return ret;
	}

	ret = regmap_read(chip->regmap, ISL29028_REG_ALSIR_U, &msb);
	if (ret < 0) {
		dev_err(chip->dev,
			"Error in reading register ALSIR_U err %d\n", ret);
		return ret;
	}

	*als_ir = ((msb & 0xF) << 8) | (lsb & 0xFF);
	return 0;
}

static int isl29028_read_proxim(struct isl29028_chip *chip, int *prox)
{
	unsigned int data;
	int ret;

	ret = regmap_read(chip->regmap, ISL29028_REG_PROX_DATA, &data);
	if (ret < 0) {
		dev_err(chip->dev, "Error in reading register %d, error %d\n",
			ISL29028_REG_PROX_DATA, ret);
		return ret;
	}
	*prox = data;
	return 0;
}

static int isl29028_proxim_get(struct isl29028_chip *chip, int *prox_data)
{
	int ret;

	if (!chip->enable_prox) {
		ret = isl29028_enable_proximity(chip, true);
		if (ret < 0)
			return ret;
		chip->enable_prox = true;
	}
	return isl29028_read_proxim(chip, prox_data);
}

static int isl29028_als_get(struct isl29028_chip *chip, int *als_data)
{
	int ret;
	int als_ir_data;

	if (chip->als_ir_mode != MODE_ALS) {
		ret = isl29028_set_als_ir_mode(chip, MODE_ALS);
		if (ret < 0) {
			dev_err(chip->dev,
				"Error in enabling ALS mode err %d\n", ret);
			return ret;
		}
		chip->als_ir_mode = MODE_ALS;
	}

	ret = isl29028_read_als_ir(chip, &als_ir_data);
	if (ret < 0)
		return ret;

	/*
	 * convert als data count to lux.
	 * if lux_scale = 125,  lux = count * 0.031
	 * if lux_scale = 2000, lux = count * 0.49
	 */
	if (chip->lux_scale == 125)
		als_ir_data = (als_ir_data * 31) / 1000;
	else
		als_ir_data = (als_ir_data * 49) / 100;

	*als_data = als_ir_data;
	return 0;
}

static int isl29028_ir_get(struct isl29028_chip *chip, int *ir_data)
{
	int ret;

	if (chip->als_ir_mode != MODE_IR) {
		ret = isl29028_set_als_ir_mode(chip, MODE_IR);
		if (ret < 0) {
			dev_err(chip->dev,
				"Error in enabling IR mode err %d\n", ret);
			return ret;
		}
		chip->als_ir_mode = MODE_IR;
	}
	return isl29028_read_als_ir(chip, ir_data);
}

/* Channel IO */
static int isl29028_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct isl29028_chip *chip = iio_priv(indio_dev);
	int ret = -EINVAL;

	mutex_lock(&chip->lock);
	switch (chan->type) {
	case IIO_PROXIMITY:
		if (mask != IIO_CHAN_INFO_SAMP_FREQ) {
			dev_err(chip->dev,
				"proximity: mask value 0x%08lx not supported\n",
				mask);
			break;
		}
		if (val < 1 || val > 100) {
			dev_err(chip->dev,
				"Samp_freq %d is not in range[1:100]\n", val);
			break;
		}
		ret = isl29028_set_proxim_sampling(chip, val);
		if (ret < 0) {
			dev_err(chip->dev,
				"Setting proximity samp_freq fail, err %d\n",
				ret);
			break;
		}
		chip->prox_sampling = val;
		break;

	case IIO_LIGHT:
		if (mask != IIO_CHAN_INFO_SCALE) {
			dev_err(chip->dev,
				"light: mask value 0x%08lx not supported\n",
				mask);
			break;
		}
		if ((val != 125) && (val != 2000)) {
			dev_err(chip->dev,
				"lux scale %d is invalid [125, 2000]\n", val);
			break;
		}
		ret = isl29028_set_als_scale(chip, val);
		if (ret < 0) {
			dev_err(chip->dev,
				"Setting lux scale fail with error %d\n", ret);
			break;
		}
		chip->lux_scale = val;
		break;

	default:
		dev_err(chip->dev, "Unsupported channel type\n");
		break;
	}
	mutex_unlock(&chip->lock);
	return ret;
}

static int isl29028_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct isl29028_chip *chip = iio_priv(indio_dev);
	int ret = -EINVAL;

	mutex_lock(&chip->lock);
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
	case IIO_CHAN_INFO_PROCESSED:
		switch (chan->type) {
		case IIO_LIGHT:
			ret = isl29028_als_get(chip, val);
			break;
		case IIO_INTENSITY:
			ret = isl29028_ir_get(chip, val);
			break;
		case IIO_PROXIMITY:
			ret = isl29028_proxim_get(chip, val);
			break;
		default:
			break;
		}
		if (ret < 0)
			break;
		ret = IIO_VAL_INT;
		break;

	case IIO_CHAN_INFO_SAMP_FREQ:
		if (chan->type != IIO_PROXIMITY)
			break;
		*val = chip->prox_sampling;
		ret = IIO_VAL_INT;
		break;

	case IIO_CHAN_INFO_SCALE:
		if (chan->type != IIO_LIGHT)
			break;
		*val = chip->lux_scale;
		ret = IIO_VAL_INT;
		break;

	default:
		dev_err(chip->dev, "mask value 0x%08lx not supported\n", mask);
		break;
	}
	mutex_unlock(&chip->lock);
	return ret;
}

static IIO_CONST_ATTR(in_proximity_sampling_frequency_available,
				"1, 3, 5, 10, 13, 20, 83, 100");
static IIO_CONST_ATTR(in_illuminance_scale_available, "125, 2000");

#define ISL29028_DEV_ATTR(name) (&iio_dev_attr_##name.dev_attr.attr)
#define ISL29028_CONST_ATTR(name) (&iio_const_attr_##name.dev_attr.attr)
static struct attribute *isl29028_attributes[] = {
	ISL29028_CONST_ATTR(in_proximity_sampling_frequency_available),
	ISL29028_CONST_ATTR(in_illuminance_scale_available),
	NULL,
};

static const struct attribute_group isl29108_group = {
	.attrs = isl29028_attributes,
};

static const struct iio_chan_spec isl29028_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
		BIT(IIO_CHAN_INFO_SCALE),
	}, {
		.type = IIO_INTENSITY,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	}, {
		.type = IIO_PROXIMITY,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ),
	}
};

static const struct iio_info isl29028_info = {
	.attrs = &isl29108_group,
	.driver_module = THIS_MODULE,
	.read_raw = isl29028_read_raw,
	.write_raw = isl29028_write_raw,
};

static int isl29028_chip_init(struct isl29028_chip *chip)
{
	int ret;

	chip->enable_prox  = false;
	chip->prox_sampling = 20;
	chip->lux_scale = 2000;
	chip->als_ir_mode = MODE_NONE;

	ret = regmap_write(chip->regmap, ISL29028_REG_TEST1_MODE, 0x0);
	if (ret < 0) {
		dev_err(chip->dev, "%s(): write to reg %d failed, err = %d\n",
			__func__, ISL29028_REG_TEST1_MODE, ret);
		return ret;
	}
	ret = regmap_write(chip->regmap, ISL29028_REG_TEST2_MODE, 0x0);
	if (ret < 0) {
		dev_err(chip->dev, "%s(): write to reg %d failed, err = %d\n",
			__func__, ISL29028_REG_TEST2_MODE, ret);
		return ret;
	}

	ret = regmap_write(chip->regmap, ISL29028_REG_CONFIGURE, 0x0);
	if (ret < 0) {
		dev_err(chip->dev, "%s(): write to reg %d failed, err = %d\n",
			__func__, ISL29028_REG_CONFIGURE, ret);
		return ret;
	}

	ret = isl29028_set_proxim_sampling(chip, chip->prox_sampling);
	if (ret < 0) {
		dev_err(chip->dev, "setting the proximity, err = %d\n",
			ret);
		return ret;
	}

	ret = isl29028_set_als_scale(chip, chip->lux_scale);
	if (ret < 0)
		dev_err(chip->dev,
			"setting als scale failed, err = %d\n", ret);
	return ret;
}

static bool is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ISL29028_REG_INTERRUPT:
	case ISL29028_REG_PROX_DATA:
	case ISL29028_REG_ALSIR_L:
	case ISL29028_REG_ALSIR_U:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config isl29028_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_reg = is_volatile_reg,
	.max_register = ISL29028_NUM_REGS - 1,
	.num_reg_defaults_raw = ISL29028_NUM_REGS,
	.cache_type = REGCACHE_RBTREE,
};

static int isl29028_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct isl29028_chip *chip;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*chip));
	if (!indio_dev) {
		dev_err(&client->dev, "iio allocation fails\n");
		return -ENOMEM;
	}

	chip = iio_priv(indio_dev);

	i2c_set_clientdata(client, indio_dev);
	chip->dev = &client->dev;
	mutex_init(&chip->lock);

	chip->regmap = devm_regmap_init_i2c(client, &isl29028_regmap_config);
	if (IS_ERR(chip->regmap)) {
		ret = PTR_ERR(chip->regmap);
		dev_err(chip->dev, "regmap initialization failed: %d\n", ret);
		return ret;
	}

	ret = isl29028_chip_init(chip);
	if (ret < 0) {
		dev_err(chip->dev, "chip initialization failed: %d\n", ret);
		return ret;
	}

	indio_dev->info = &isl29028_info;
	indio_dev->channels = isl29028_channels;
	indio_dev->num_channels = ARRAY_SIZE(isl29028_channels);
	indio_dev->name = id->name;
	indio_dev->dev.parent = &client->dev;
	indio_dev->modes = INDIO_DIRECT_MODE;
	ret = devm_iio_device_register(indio_dev->dev.parent, indio_dev);
	if (ret < 0) {
		dev_err(chip->dev, "iio registration fails with error %d\n",
			ret);
		return ret;
	}
	return 0;
}

static const struct i2c_device_id isl29028_id[] = {
	{"isl29028", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, isl29028_id);

static const struct of_device_id isl29028_of_match[] = {
	{ .compatible = "isl,isl29028", }, /* for backward compat., don't use */
	{ .compatible = "isil,isl29028", },
	{ },
};
MODULE_DEVICE_TABLE(of, isl29028_of_match);

static struct i2c_driver isl29028_driver = {
	.class	= I2C_CLASS_HWMON,
	.driver  = {
		.name = "isl29028",
		.of_match_table = isl29028_of_match,
	},
	.probe	 = isl29028_probe,
	.id_table = isl29028_id,
};

module_i2c_driver(isl29028_driver);

MODULE_DESCRIPTION("ISL29028 Ambient Light and Proximity Sensor driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
