/*
 * si1132.c - Support for Silabs si1132 combined ambient light and
 * proximity sensor.
 *
 * Copyright (C) 2014 Hardkernel Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under  the terms of the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/err.h>

#include "si1132.h"

#define SI1132_ADDRESS		0x60
#define SI1132_NAME		"si1132"
#define SI1132_CHIP_ID		0x32

/* Registers */
#define SI1132_REG_PART_ID	0x00


static const unsigned short normal_i2c[] = { SI1132_ADDRESS,
						I2C_CLIENT_END };
struct si1132_data {
	struct device *dev;
	struct regmap *regmap;
	struct mutex lock;
	u32	raw_uv;
	u32	raw_ir;
	u32	raw_visible;
	u8	chip_id;
};

static int si1132_param_set(struct si1132_data *data, u8 param, u8 value)
{
	int ret;

	ret = regmap_write(data->regmap, SI1132_REG_PARAM_WR, value);
	if (ret < 0)
		return ret;
	ret = regmap_write(data->regmap, SI1132_REG_COMMAND,
				SI1132_COMMAND_PARAM_SET | (param & 0x1F));
	if (ret < 0)
		return ret;

	return 0;
}


static s32 si1132_read_calibration_data(s32 value)
{
	if (value > 256)
		value -= 256;
	else
		value = 0;
	value *= 7;
	value /= 10;
	return value;
}
static s32 si1132_update_raw_ir(struct si1132_data *data)
{
	u16 ir;
	s32 status;

	mutex_lock(&data->lock);
	status = regmap_bulk_read(data->regmap, SI1132_REG_ALSIR_DATA0,
					&ir, sizeof(ir));
	if (status < 0) {
		dev_err(data->dev,
			"Error while reading ir index measurement result\n");
		goto exit;
	}
	msleep(10);

	data->raw_ir = ir;
	status = 0;

exit:
	mutex_unlock(&data->lock);
	return status;
}

static s32 si1132_get_ir(struct si1132_data *data, int *ir)
{
	int status;
	status = si1132_update_raw_ir(data);
	if (status < 0)
		goto exit;
	*ir = si1132_read_calibration_data(data->raw_ir);

exit:
	return status;
}

static ssize_t show_ir(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ir;
	int status;
	struct si1132_data *data = dev_get_drvdata(dev);

	status = si1132_get_ir(data, &ir);
	if (status < 0)
		return status;
	else
		return sprintf(buf, "%d\n", ir);
}
static DEVICE_ATTR(ir_index, S_IRUGO, show_ir, NULL);

static s32 si1132_update_raw_visible(struct si1132_data *data)
{
	u16 visible;
	s32 status;

	mutex_lock(&data->lock);
	status = regmap_bulk_read(data->regmap, SI1132_REG_ALSVIS_DATA0,
					&visible, sizeof(visible));
	if (status < 0) {
		dev_err(data->dev,
			"Error while reading ir index measurement result\n");
		goto exit;
	}
	msleep(10);

	data->raw_visible = visible;
	status = 0;
exit:
	mutex_unlock(&data->lock);
	return status;
}

static s32 si1132_get_visible(struct si1132_data *data, int *visible)
{
	int status;
	status = si1132_update_raw_visible(data);
	if (status < 0)
		goto exit;
	*visible = si1132_read_calibration_data(data->raw_visible);
exit:
	return status;
}

static ssize_t show_visible(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int visible;
	int status;
	struct si1132_data *data = dev_get_drvdata(dev);

	status = si1132_get_visible(data, &visible);
	if (status < 0)
		return status;
	else
		return sprintf(buf, "%d\n", visible);
}
static DEVICE_ATTR(visible_index, S_IRUGO, show_visible, NULL);

static s32 si1132_update_raw_uv(struct si1132_data *data)
{
	u16 uv;
	s32 status;

	mutex_lock(&data->lock);
	status = regmap_bulk_read(data->regmap, SI1132_REG_AUX_DATA0,
					&uv, sizeof(uv));
	if (status < 0) {
		dev_err(data->dev,
			"Error while reading ir index measurement result\n");
		goto exit;
	}
	msleep(10);

	data->raw_uv = uv;
	status = 0;
exit:
	mutex_unlock(&data->lock);
	return status;
}

static s32 si1132_get_uv(struct si1132_data *data, int *uv)
{
	int status;
	status = si1132_update_raw_uv(data);
	if (status < 0)
		goto exit;
	*uv = data->raw_uv;
exit:
	return status;
}

static ssize_t show_uv(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int uv;
	int status;
	struct si1132_data *data = dev_get_drvdata(dev);

	status = si1132_get_uv(data, &uv);
	if (status < 0)
		return status;
	else
		return sprintf(buf, "%d\n", uv);
}
static DEVICE_ATTR(uv_index, S_IRUGO, show_uv, NULL);

static struct attribute *si1132_attributes[] = {
	&dev_attr_ir_index.attr,
	&dev_attr_visible_index.attr,
	&dev_attr_uv_index.attr,
	NULL
};

static const struct attribute_group si1132_attr_group = {
	.attrs = si1132_attributes,
};

static int si1132_initialize(struct device *dev)
{
	struct si1132_data *data = dev_get_drvdata(dev);

	regmap_write(data->regmap, SI1132_REG_COMMAND, SI1132_COMMAND_RESET);
	msleep(10);
	regmap_write(data->regmap, SI1132_REG_HW_KEY, 0x17);
	msleep(10);

	regmap_write(data->regmap,SI1132_REG_UCOEF0, 0x29);
	regmap_write(data->regmap,SI1132_REG_UCOEF1, 0x89);
	regmap_write(data->regmap,SI1132_REG_UCOEF2, 0x02);
	regmap_write(data->regmap,SI1132_REG_UCOEF3, 0x00);

	si1132_param_set(data, SI1132_PARAM_CHLIST, SI1132_CHLIST_EN_UV |
				SI1132_CHLIST_EN_AUX | SI1132_CHLIST_EN_ALSIR |
						SI1132_CHLIST_EN_ALSVIS);

	regmap_write(data->regmap, SI1132_REG_INT_CFG, SI1132_INT_CFG_OE);
	regmap_write(data->regmap, SI1132_REG_IRQ_ENABLE, SI1132_ALS_INT0_IE);

	si1132_param_set(data, SI1132_PARAM_ALSIR_ADC_MUX, 0x00);
	si1132_param_set(data, SI1132_PARAM_ALSIR_ADC_GAIN, 1);
	si1132_param_set(data, SI1132_PARAM_ALSIR_ADC_COUNTER, 0x70);

	si1132_param_set(data, SI1132_PARAM_ALSVIS_ADC_GAIN, 3);
	si1132_param_set(data, SI1132_PARAM_ALSVIS_ADC_COUNTER, 0X70);

	regmap_write(data->regmap, 0x08, 0xff);
	regmap_write(data->regmap, SI1132_REG_COMMAND, SI1132_COMMAND_ALS_AUTO);

	return 0;
}

int si1132_detect(struct device *dev)
{
	struct si1132_data *data = dev_get_drvdata(dev);
	unsigned int id;
	int ret;

	pr_err("%s, %d", __func__, __LINE__);
	ret = regmap_read(data->regmap, SI1132_REG_PART_ID, &id);
	if (ret < 0)
		return ret;

	if (id != data->chip_id)
		return -ENODEV;

	return 0;
}

static int si1132_init_client(struct si1132_data *data)
{
	data->chip_id = SI1132_CHIP_ID;
	mutex_init(&data->lock);

	return 0;
}

struct regmap_config si1132_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8
};

static int si1132_probe(struct i2c_client *client,
				      const struct i2c_device_id *id)
{
	struct si1132_data *data;
	int err = 0;

	struct regmap *regmap = devm_regmap_init_i2c(client,
							&si1132_regmap_config);

	if (IS_ERR(regmap)) {
		err = PTR_ERR(regmap);
		dev_err(&client->dev, "Failed to init regmap: %d\n", err);
		return err;
	}

	data = kzalloc(sizeof(struct si1132_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	dev_set_drvdata(&client->dev, data);
	data->dev = &client->dev;
	data->regmap = regmap;

	err = si1132_init_client(data);
	if (err < 0)
		goto exit_free;

	err = si1132_detect(&client->dev);
	if (err < 0) {
		dev_err(&client->dev, "%s: chip_id failed!\n", SI1132_NAME);
		goto exit_free;
	}

	si1132_initialize(&client->dev);

	/* Register sysfs hooks */
	err = sysfs_create_group(&data->dev->kobj, &si1132_attr_group);
	if (err)
		goto exit_free;

	dev_info(&client->dev, "Successfully initialized %s!\n", SI1132_NAME);

	return 0;

exit_free:
	kfree(data);
exit:
	return err;
}

static int si1132_remove(struct i2c_client *client)
{
	struct si1132_data *data = dev_get_drvdata(&client->dev);

	sysfs_remove_group(&data->dev->kobj, &si1132_attr_group);
	kfree(data);

	return 0;
}

static const struct i2c_device_id si1132_id[] = {
	{ SI1132_NAME, 0 },
	{ "si1132", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, si1132_id);

static struct i2c_driver bmp085_i2c_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= SI1132_NAME,
	},
	.id_table	= si1132_id,
	.probe		= si1132_probe,
	.remove		= si1132_remove,
	.address_list	= normal_i2c
};

module_i2c_driver(bmp085_i2c_driver);

MODULE_AUTHOR("John Lee <john.lee@hardkernel.com>");
MODULE_DESCRIPTION("SI1132 I2C bus driver");
MODULE_LICENSE("GPL");
