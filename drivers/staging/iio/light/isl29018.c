/*
 * A iio driver for the light sensor ISL 29018/29023/29035.
 *
 * IIO driver for monitoring ambient light intensity in luxi, proximity
 * sensing and infrared sensing.
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/acpi.h>

#define ISL29018_CONV_TIME_MS		100

#define ISL29018_REG_ADD_COMMAND1	0x00
#define ISL29018_CMD1_OPMODE_SHIFT	5
#define ISL29018_CMD1_OPMODE_MASK	(7 << ISL29018_CMD1_OPMODE_SHIFT)
#define ISL29018_CMD1_OPMODE_POWER_DOWN	0
#define ISL29018_CMD1_OPMODE_ALS_ONCE	1
#define ISL29018_CMD1_OPMODE_IR_ONCE	2
#define ISL29018_CMD1_OPMODE_PROX_ONCE	3

#define ISL29018_REG_ADD_COMMAND2	0x01
#define ISL29018_CMD2_RESOLUTION_SHIFT	2
#define ISL29018_CMD2_RESOLUTION_MASK	(0x3 << ISL29018_CMD2_RESOLUTION_SHIFT)

#define ISL29018_CMD2_RANGE_SHIFT	0
#define ISL29018_CMD2_RANGE_MASK	(0x3 << ISL29018_CMD2_RANGE_SHIFT)

#define ISL29018_CMD2_SCHEME_SHIFT	7
#define ISL29018_CMD2_SCHEME_MASK	(0x1 << ISL29018_CMD2_SCHEME_SHIFT)

#define ISL29018_REG_ADD_DATA_LSB	0x02
#define ISL29018_REG_ADD_DATA_MSB	0x03

#define ISL29018_REG_TEST		0x08
#define ISL29018_TEST_SHIFT		0
#define ISL29018_TEST_MASK		(0xFF << ISL29018_TEST_SHIFT)

#define ISL29035_REG_DEVICE_ID		0x0F
#define ISL29035_DEVICE_ID_SHIFT	0x03
#define ISL29035_DEVICE_ID_MASK		(0x7 << ISL29035_DEVICE_ID_SHIFT)
#define ISL29035_DEVICE_ID		0x5
#define ISL29035_BOUT_SHIFT		0x07
#define ISL29035_BOUT_MASK		(0x01 << ISL29035_BOUT_SHIFT)

enum isl29018_int_time {
	ISL29018_INT_TIME_16,
	ISL29018_INT_TIME_12,
	ISL29018_INT_TIME_8,
	ISL29018_INT_TIME_4,
};

static const unsigned int isl29018_int_utimes[3][4] = {
	{90000, 5630, 351, 21},
	{90000, 5600, 352, 22},
	{105000, 6500, 410, 25},
};

static const struct isl29018_scale {
	unsigned int scale;
	unsigned int uscale;
} isl29018_scales[4][4] = {
	{ {0, 15258}, {0, 61035}, {0, 244140}, {0, 976562} },
	{ {0, 244140}, {0, 976562}, {3, 906250}, {15, 625000} },
	{ {3, 906250}, {15, 625000}, {62, 500000}, {250, 0} },
	{ {62, 500000}, {250, 0}, {1000, 0}, {4000, 0} }
};

struct isl29018_chip {
	struct regmap		*regmap;
	struct mutex		lock;
	int			type;
	unsigned int		calibscale;
	unsigned int		ucalibscale;
	unsigned int		int_time;
	struct isl29018_scale	scale;
	int			prox_scheme;
	bool			suspended;
};

static int isl29018_set_integration_time(struct isl29018_chip *chip,
					 unsigned int utime)
{
	unsigned int i;
	int ret;
	unsigned int int_time, new_int_time;

	for (i = 0; i < ARRAY_SIZE(isl29018_int_utimes[chip->type]); ++i) {
		if (utime == isl29018_int_utimes[chip->type][i]) {
			new_int_time = i;
			break;
		}
	}

	if (i >= ARRAY_SIZE(isl29018_int_utimes[chip->type]))
		return -EINVAL;

	ret = regmap_update_bits(chip->regmap, ISL29018_REG_ADD_COMMAND2,
				 ISL29018_CMD2_RESOLUTION_MASK,
				 i << ISL29018_CMD2_RESOLUTION_SHIFT);
	if (ret < 0)
		return ret;

	/* Keep the same range when integration time changes */
	int_time = chip->int_time;
	for (i = 0; i < ARRAY_SIZE(isl29018_scales[int_time]); ++i) {
		if (chip->scale.scale == isl29018_scales[int_time][i].scale &&
		    chip->scale.uscale == isl29018_scales[int_time][i].uscale) {
			chip->scale = isl29018_scales[new_int_time][i];
			break;
		}
	}
	chip->int_time = new_int_time;

	return 0;
}

static int isl29018_set_scale(struct isl29018_chip *chip, int scale, int uscale)
{
	unsigned int i;
	int ret;
	struct isl29018_scale new_scale;

	for (i = 0; i < ARRAY_SIZE(isl29018_scales[chip->int_time]); ++i) {
		if (scale == isl29018_scales[chip->int_time][i].scale &&
		    uscale == isl29018_scales[chip->int_time][i].uscale) {
			new_scale = isl29018_scales[chip->int_time][i];
			break;
		}
	}

	if (i >= ARRAY_SIZE(isl29018_scales[chip->int_time]))
		return -EINVAL;

	ret = regmap_update_bits(chip->regmap, ISL29018_REG_ADD_COMMAND2,
				 ISL29018_CMD2_RANGE_MASK,
				 i << ISL29018_CMD2_RANGE_SHIFT);
	if (ret < 0)
		return ret;

	chip->scale = new_scale;

	return 0;
}

static int isl29018_read_sensor_input(struct isl29018_chip *chip, int mode)
{
	int status;
	unsigned int lsb;
	unsigned int msb;
	struct device *dev = regmap_get_device(chip->regmap);

	/* Set mode */
	status = regmap_write(chip->regmap, ISL29018_REG_ADD_COMMAND1,
			      mode << ISL29018_CMD1_OPMODE_SHIFT);
	if (status) {
		dev_err(dev,
			"Error in setting operating mode err %d\n", status);
		return status;
	}
	msleep(ISL29018_CONV_TIME_MS);
	status = regmap_read(chip->regmap, ISL29018_REG_ADD_DATA_LSB, &lsb);
	if (status < 0) {
		dev_err(dev,
			"Error in reading LSB DATA with err %d\n", status);
		return status;
	}

	status = regmap_read(chip->regmap, ISL29018_REG_ADD_DATA_MSB, &msb);
	if (status < 0) {
		dev_err(dev,
			"Error in reading MSB DATA with error %d\n", status);
		return status;
	}
	dev_vdbg(dev, "MSB 0x%x and LSB 0x%x\n", msb, lsb);

	return (msb << 8) | lsb;
}

static int isl29018_read_lux(struct isl29018_chip *chip, int *lux)
{
	int lux_data;
	unsigned int data_x_range;

	lux_data = isl29018_read_sensor_input(chip,
					      ISL29018_CMD1_OPMODE_ALS_ONCE);
	if (lux_data < 0)
		return lux_data;

	data_x_range = lux_data * chip->scale.scale +
		       lux_data * chip->scale.uscale / 1000000;
	*lux = data_x_range * chip->calibscale +
	       data_x_range * chip->ucalibscale / 1000000;

	return 0;
}

static int isl29018_read_ir(struct isl29018_chip *chip, int *ir)
{
	int ir_data;

	ir_data = isl29018_read_sensor_input(chip,
					     ISL29018_CMD1_OPMODE_IR_ONCE);
	if (ir_data < 0)
		return ir_data;

	*ir = ir_data;

	return 0;
}

static int isl29018_read_proximity_ir(struct isl29018_chip *chip, int scheme,
				      int *near_ir)
{
	int status;
	int prox_data = -1;
	int ir_data = -1;
	struct device *dev = regmap_get_device(chip->regmap);

	/* Do proximity sensing with required scheme */
	status = regmap_update_bits(chip->regmap, ISL29018_REG_ADD_COMMAND2,
				    ISL29018_CMD2_SCHEME_MASK,
				    scheme << ISL29018_CMD2_SCHEME_SHIFT);
	if (status) {
		dev_err(dev, "Error in setting operating mode\n");
		return status;
	}

	prox_data = isl29018_read_sensor_input(chip,
					       ISL29018_CMD1_OPMODE_PROX_ONCE);
	if (prox_data < 0)
		return prox_data;

	if (scheme == 1) {
		*near_ir = prox_data;
		return 0;
	}

	ir_data = isl29018_read_sensor_input(chip,
					     ISL29018_CMD1_OPMODE_IR_ONCE);
	if (ir_data < 0)
		return ir_data;

	if (prox_data >= ir_data)
		*near_ir = prox_data - ir_data;
	else
		*near_ir = 0;

	return 0;
}

static ssize_t in_illuminance_scale_available_show
			(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct isl29018_chip *chip = iio_priv(indio_dev);
	unsigned int i;
	int len = 0;

	mutex_lock(&chip->lock);
	for (i = 0; i < ARRAY_SIZE(isl29018_scales[chip->int_time]); ++i)
		len += sprintf(buf + len, "%d.%06d ",
			       isl29018_scales[chip->int_time][i].scale,
			       isl29018_scales[chip->int_time][i].uscale);
	mutex_unlock(&chip->lock);

	buf[len - 1] = '\n';

	return len;
}

static ssize_t in_illuminance_integration_time_available_show
			(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct isl29018_chip *chip = iio_priv(indio_dev);
	unsigned int i;
	int len = 0;

	for (i = 0; i < ARRAY_SIZE(isl29018_int_utimes[chip->type]); ++i)
		len += sprintf(buf + len, "0.%06d ",
			       isl29018_int_utimes[chip->type][i]);

	buf[len - 1] = '\n';

	return len;
}

static ssize_t proximity_on_chip_ambient_infrared_suppression_show
			(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct isl29018_chip *chip = iio_priv(indio_dev);

	/*
	 * Return the "proximity scheme" i.e. if the chip does on chip
	 * infrared suppression (1 means perform on chip suppression)
	 */
	return sprintf(buf, "%d\n", chip->prox_scheme);
}

static ssize_t proximity_on_chip_ambient_infrared_suppression_store
			(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct isl29018_chip *chip = iio_priv(indio_dev);
	int val;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;
	if (!(val == 0 || val == 1))
		return -EINVAL;

	/*
	 * Get the "proximity scheme" i.e. if the chip does on chip
	 * infrared suppression (1 means perform on chip suppression)
	 */
	mutex_lock(&chip->lock);
	chip->prox_scheme = val;
	mutex_unlock(&chip->lock);

	return count;
}

static int isl29018_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val,
			      int val2,
			      long mask)
{
	struct isl29018_chip *chip = iio_priv(indio_dev);
	int ret = -EINVAL;

	mutex_lock(&chip->lock);
	switch (mask) {
	case IIO_CHAN_INFO_CALIBSCALE:
		if (chan->type == IIO_LIGHT) {
			chip->calibscale = val;
			chip->ucalibscale = val2;
			ret = 0;
		}
		break;
	case IIO_CHAN_INFO_INT_TIME:
		if (chan->type == IIO_LIGHT && !val)
			ret = isl29018_set_integration_time(chip, val2);
		break;
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_LIGHT)
			ret = isl29018_set_scale(chip, val, val2);
		break;
	default:
		break;
	}
	mutex_unlock(&chip->lock);

	return ret;
}

static int isl29018_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val,
			     int *val2,
			     long mask)
{
	int ret = -EINVAL;
	struct isl29018_chip *chip = iio_priv(indio_dev);

	mutex_lock(&chip->lock);
	if (chip->suspended) {
		ret = -EBUSY;
		goto read_done;
	}
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
	case IIO_CHAN_INFO_PROCESSED:
		switch (chan->type) {
		case IIO_LIGHT:
			ret = isl29018_read_lux(chip, val);
			break;
		case IIO_INTENSITY:
			ret = isl29018_read_ir(chip, val);
			break;
		case IIO_PROXIMITY:
			ret = isl29018_read_proximity_ir(chip,
							 chip->prox_scheme,
							 val);
			break;
		default:
			break;
		}
		if (!ret)
			ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_INT_TIME:
		if (chan->type == IIO_LIGHT) {
			*val = 0;
			*val2 = isl29018_int_utimes[chip->type][chip->int_time];
			ret = IIO_VAL_INT_PLUS_MICRO;
		}
		break;
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_LIGHT) {
			*val = chip->scale.scale;
			*val2 = chip->scale.uscale;
			ret = IIO_VAL_INT_PLUS_MICRO;
		}
		break;
	case IIO_CHAN_INFO_CALIBSCALE:
		if (chan->type == IIO_LIGHT) {
			*val = chip->calibscale;
			*val2 = chip->ucalibscale;
			ret = IIO_VAL_INT_PLUS_MICRO;
		}
		break;
	default:
		break;
	}

read_done:
	mutex_unlock(&chip->lock);
	return ret;
}

#define ISL29018_LIGHT_CHANNEL {					\
	.type = IIO_LIGHT,						\
	.indexed = 1,							\
	.channel = 0,							\
	.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |		\
	BIT(IIO_CHAN_INFO_CALIBSCALE) |					\
	BIT(IIO_CHAN_INFO_SCALE) |					\
	BIT(IIO_CHAN_INFO_INT_TIME),					\
}

#define ISL29018_IR_CHANNEL {						\
	.type = IIO_INTENSITY,						\
	.modified = 1,							\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.channel2 = IIO_MOD_LIGHT_IR,					\
}

#define ISL29018_PROXIMITY_CHANNEL {					\
	.type = IIO_PROXIMITY,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
}

static const struct iio_chan_spec isl29018_channels[] = {
	ISL29018_LIGHT_CHANNEL,
	ISL29018_IR_CHANNEL,
	ISL29018_PROXIMITY_CHANNEL,
};

static const struct iio_chan_spec isl29023_channels[] = {
	ISL29018_LIGHT_CHANNEL,
	ISL29018_IR_CHANNEL,
};

static IIO_DEVICE_ATTR_RO(in_illuminance_integration_time_available, 0);
static IIO_DEVICE_ATTR_RO(in_illuminance_scale_available, 0);
static IIO_DEVICE_ATTR_RW(proximity_on_chip_ambient_infrared_suppression, 0);

#define ISL29018_DEV_ATTR(name) (&iio_dev_attr_##name.dev_attr.attr)

static struct attribute *isl29018_attributes[] = {
	ISL29018_DEV_ATTR(in_illuminance_scale_available),
	ISL29018_DEV_ATTR(in_illuminance_integration_time_available),
	ISL29018_DEV_ATTR(proximity_on_chip_ambient_infrared_suppression),
	NULL
};

static struct attribute *isl29023_attributes[] = {
	ISL29018_DEV_ATTR(in_illuminance_scale_available),
	ISL29018_DEV_ATTR(in_illuminance_integration_time_available),
	NULL
};

static const struct attribute_group isl29018_group = {
	.attrs = isl29018_attributes,
};

static const struct attribute_group isl29023_group = {
	.attrs = isl29023_attributes,
};

static int isl29035_detect(struct isl29018_chip *chip)
{
	int status;
	unsigned int id;
	struct device *dev = regmap_get_device(chip->regmap);

	status = regmap_read(chip->regmap, ISL29035_REG_DEVICE_ID, &id);
	if (status < 0) {
		dev_err(dev,
			"Error reading ID register with error %d\n",
			status);
		return status;
	}

	id = (id & ISL29035_DEVICE_ID_MASK) >> ISL29035_DEVICE_ID_SHIFT;

	if (id != ISL29035_DEVICE_ID)
		return -ENODEV;

	/* Clear brownout bit */
	return regmap_update_bits(chip->regmap, ISL29035_REG_DEVICE_ID,
				  ISL29035_BOUT_MASK, 0);
}

enum {
	isl29018,
	isl29023,
	isl29035,
};

static int isl29018_chip_init(struct isl29018_chip *chip)
{
	int status;
	struct device *dev = regmap_get_device(chip->regmap);

	if (chip->type == isl29035) {
		status = isl29035_detect(chip);
		if (status < 0)
			return status;
	}

	/* Code added per Intersil Application Note 1534:
	 *     When VDD sinks to approximately 1.8V or below, some of
	 * the part's registers may change their state. When VDD
	 * recovers to 2.25V (or greater), the part may thus be in an
	 * unknown mode of operation. The user can return the part to
	 * a known mode of operation either by (a) setting VDD = 0V for
	 * 1 second or more and then powering back up with a slew rate
	 * of 0.5V/ms or greater, or (b) via I2C disable all ALS/PROX
	 * conversions, clear the test registers, and then rewrite all
	 * registers to the desired values.
	 * ...
	 * For ISL29011, ISL29018, ISL29021, ISL29023
	 * 1. Write 0x00 to register 0x08 (TEST)
	 * 2. Write 0x00 to register 0x00 (CMD1)
	 * 3. Rewrite all registers to the desired values
	 *
	 * ISL29018 Data Sheet (FN6619.1, Feb 11, 2010) essentially says
	 * the same thing EXCEPT the data sheet asks for a 1ms delay after
	 * writing the CMD1 register.
	 */
	status = regmap_write(chip->regmap, ISL29018_REG_TEST, 0x0);
	if (status < 0) {
		dev_err(dev, "Failed to clear isl29018 TEST reg.(%d)\n",
			status);
		return status;
	}

	/* See Intersil AN1534 comments above.
	 * "Operating Mode" (COMMAND1) register is reprogrammed when
	 * data is read from the device.
	 */
	status = regmap_write(chip->regmap, ISL29018_REG_ADD_COMMAND1, 0);
	if (status < 0) {
		dev_err(dev, "Failed to clear isl29018 CMD1 reg.(%d)\n",
			status);
		return status;
	}

	usleep_range(1000, 2000);	/* per data sheet, page 10 */

	/* Set defaults */
	status = isl29018_set_scale(chip, chip->scale.scale,
				    chip->scale.uscale);
	if (status < 0) {
		dev_err(dev, "Init of isl29018 fails\n");
		return status;
	}

	status = isl29018_set_integration_time(chip,
			isl29018_int_utimes[chip->type][chip->int_time]);
	if (status < 0) {
		dev_err(dev, "Init of isl29018 fails\n");
		return status;
	}

	return 0;
}

static const struct iio_info isl29018_info = {
	.attrs = &isl29018_group,
	.driver_module = THIS_MODULE,
	.read_raw = isl29018_read_raw,
	.write_raw = isl29018_write_raw,
};

static const struct iio_info isl29023_info = {
	.attrs = &isl29023_group,
	.driver_module = THIS_MODULE,
	.read_raw = isl29018_read_raw,
	.write_raw = isl29018_write_raw,
};

static bool isl29018_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ISL29018_REG_ADD_DATA_LSB:
	case ISL29018_REG_ADD_DATA_MSB:
	case ISL29018_REG_ADD_COMMAND1:
	case ISL29018_REG_TEST:
	case ISL29035_REG_DEVICE_ID:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config isl29018_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_reg = isl29018_is_volatile_reg,
	.max_register = ISL29018_REG_TEST,
	.num_reg_defaults_raw = ISL29018_REG_TEST + 1,
	.cache_type = REGCACHE_RBTREE,
};

static const struct regmap_config isl29035_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_reg = isl29018_is_volatile_reg,
	.max_register = ISL29035_REG_DEVICE_ID,
	.num_reg_defaults_raw = ISL29035_REG_DEVICE_ID + 1,
	.cache_type = REGCACHE_RBTREE,
};

struct isl29018_chip_info {
	const struct iio_chan_spec *channels;
	int num_channels;
	const struct iio_info *indio_info;
	const struct regmap_config *regmap_cfg;
};

static const struct isl29018_chip_info isl29018_chip_info_tbl[] = {
	[isl29018] = {
		.channels = isl29018_channels,
		.num_channels = ARRAY_SIZE(isl29018_channels),
		.indio_info = &isl29018_info,
		.regmap_cfg = &isl29018_regmap_config,
	},
	[isl29023] = {
		.channels = isl29023_channels,
		.num_channels = ARRAY_SIZE(isl29023_channels),
		.indio_info = &isl29023_info,
		.regmap_cfg = &isl29018_regmap_config,
	},
	[isl29035] = {
		.channels = isl29023_channels,
		.num_channels = ARRAY_SIZE(isl29023_channels),
		.indio_info = &isl29023_info,
		.regmap_cfg = &isl29035_regmap_config,
	},
};

static const char *isl29018_match_acpi_device(struct device *dev, int *data)
{
	const struct acpi_device_id *id;

	id = acpi_match_device(dev->driver->acpi_match_table, dev);

	if (!id)
		return NULL;

	*data = (int)id->driver_data;

	return dev_name(dev);
}

static int isl29018_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct isl29018_chip *chip;
	struct iio_dev *indio_dev;
	int err;
	const char *name = NULL;
	int dev_id = 0;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;
	chip = iio_priv(indio_dev);

	i2c_set_clientdata(client, indio_dev);

	if (id) {
		name = id->name;
		dev_id = id->driver_data;
	}

	if (ACPI_HANDLE(&client->dev))
		name = isl29018_match_acpi_device(&client->dev, &dev_id);

	mutex_init(&chip->lock);

	chip->type = dev_id;
	chip->calibscale = 1;
	chip->ucalibscale = 0;
	chip->int_time = ISL29018_INT_TIME_16;
	chip->scale = isl29018_scales[chip->int_time][0];
	chip->suspended = false;

	chip->regmap = devm_regmap_init_i2c(client,
				isl29018_chip_info_tbl[dev_id].regmap_cfg);
	if (IS_ERR(chip->regmap)) {
		err = PTR_ERR(chip->regmap);
		dev_err(&client->dev, "regmap initialization fails: %d\n", err);
		return err;
	}

	err = isl29018_chip_init(chip);
	if (err)
		return err;

	indio_dev->info = isl29018_chip_info_tbl[dev_id].indio_info;
	indio_dev->channels = isl29018_chip_info_tbl[dev_id].channels;
	indio_dev->num_channels = isl29018_chip_info_tbl[dev_id].num_channels;
	indio_dev->name = name;
	indio_dev->dev.parent = &client->dev;
	indio_dev->modes = INDIO_DIRECT_MODE;
	return devm_iio_device_register(&client->dev, indio_dev);
}

#ifdef CONFIG_PM_SLEEP
static int isl29018_suspend(struct device *dev)
{
	struct isl29018_chip *chip = iio_priv(dev_get_drvdata(dev));

	mutex_lock(&chip->lock);

	/* Since this driver uses only polling commands, we are by default in
	 * auto shutdown (ie, power-down) mode.
	 * So we do not have much to do here.
	 */
	chip->suspended = true;

	mutex_unlock(&chip->lock);
	return 0;
}

static int isl29018_resume(struct device *dev)
{
	struct isl29018_chip *chip = iio_priv(dev_get_drvdata(dev));
	int err;

	mutex_lock(&chip->lock);

	err = isl29018_chip_init(chip);
	if (!err)
		chip->suspended = false;

	mutex_unlock(&chip->lock);
	return err;
}

static SIMPLE_DEV_PM_OPS(isl29018_pm_ops, isl29018_suspend, isl29018_resume);
#define ISL29018_PM_OPS (&isl29018_pm_ops)
#else
#define ISL29018_PM_OPS NULL
#endif

static const struct acpi_device_id isl29018_acpi_match[] = {
	{"ISL29018", isl29018},
	{"ISL29023", isl29023},
	{"ISL29035", isl29035},
	{},
};
MODULE_DEVICE_TABLE(acpi, isl29018_acpi_match);

static const struct i2c_device_id isl29018_id[] = {
	{"isl29018", isl29018},
	{"isl29023", isl29023},
	{"isl29035", isl29035},
	{}
};

MODULE_DEVICE_TABLE(i2c, isl29018_id);

static const struct of_device_id isl29018_of_match[] = {
	{ .compatible = "isil,isl29018", },
	{ .compatible = "isil,isl29023", },
	{ .compatible = "isil,isl29035", },
	{ },
};
MODULE_DEVICE_TABLE(of, isl29018_of_match);

static struct i2c_driver isl29018_driver = {
	.driver	 = {
			.name = "isl29018",
			.acpi_match_table = ACPI_PTR(isl29018_acpi_match),
			.pm = ISL29018_PM_OPS,
			.of_match_table = isl29018_of_match,
		    },
	.probe	 = isl29018_probe,
	.id_table = isl29018_id,
};
module_i2c_driver(isl29018_driver);

MODULE_DESCRIPTION("ISL29018 Ambient Light Sensor driver");
MODULE_LICENSE("GPL");
