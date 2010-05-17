/*
 * Regulators driver for Maxim max8649
 *
 * Copyright (C) 2009-2010 Marvell International Ltd.
 *      Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/slab.h>
#include <linux/regulator/max8649.h>

#define MAX8649_DCDC_VMIN	750000		/* uV */
#define MAX8649_DCDC_VMAX	1380000		/* uV */
#define MAX8649_DCDC_STEP	10000		/* uV */
#define MAX8649_VOL_MASK	0x3f

/* Registers */
#define MAX8649_MODE0		0x00
#define MAX8649_MODE1		0x01
#define MAX8649_MODE2		0x02
#define MAX8649_MODE3		0x03
#define MAX8649_CONTROL		0x04
#define MAX8649_SYNC		0x05
#define MAX8649_RAMP		0x06
#define MAX8649_CHIP_ID1	0x08
#define MAX8649_CHIP_ID2	0x09

/* Bits */
#define MAX8649_EN_PD		(1 << 7)
#define MAX8649_VID0_PD		(1 << 6)
#define MAX8649_VID1_PD		(1 << 5)
#define MAX8649_VID_MASK	(3 << 5)

#define MAX8649_FORCE_PWM	(1 << 7)
#define MAX8649_SYNC_EXTCLK	(1 << 6)

#define MAX8649_EXT_MASK	(3 << 6)

#define MAX8649_RAMP_MASK	(7 << 5)
#define MAX8649_RAMP_DOWN	(1 << 1)

struct max8649_regulator_info {
	struct regulator_dev	*regulator;
	struct i2c_client	*i2c;
	struct device		*dev;
	struct mutex		io_lock;

	int		vol_reg;
	unsigned	mode:2;	/* bit[1:0] = VID1, VID0 */
	unsigned	extclk_freq:2;
	unsigned	extclk:1;
	unsigned	ramp_timing:3;
	unsigned	ramp_down:1;
};

/* I2C operations */

static inline int max8649_read_device(struct i2c_client *i2c,
				      int reg, int bytes, void *dest)
{
	unsigned char data;
	int ret;

	data = (unsigned char)reg;
	ret = i2c_master_send(i2c, &data, 1);
	if (ret < 0)
		return ret;
	ret = i2c_master_recv(i2c, dest, bytes);
	if (ret < 0)
		return ret;
	return 0;
}

static inline int max8649_write_device(struct i2c_client *i2c,
				       int reg, int bytes, void *src)
{
	unsigned char buf[bytes + 1];
	int ret;

	buf[0] = (unsigned char)reg;
	memcpy(&buf[1], src, bytes);

	ret = i2c_master_send(i2c, buf, bytes + 1);
	if (ret < 0)
		return ret;
	return 0;
}

static int max8649_reg_read(struct i2c_client *i2c, int reg)
{
	struct max8649_regulator_info *info = i2c_get_clientdata(i2c);
	unsigned char data;
	int ret;

	mutex_lock(&info->io_lock);
	ret = max8649_read_device(i2c, reg, 1, &data);
	mutex_unlock(&info->io_lock);

	if (ret < 0)
		return ret;
	return (int)data;
}

static int max8649_set_bits(struct i2c_client *i2c, int reg,
			    unsigned char mask, unsigned char data)
{
	struct max8649_regulator_info *info = i2c_get_clientdata(i2c);
	unsigned char value;
	int ret;

	mutex_lock(&info->io_lock);
	ret = max8649_read_device(i2c, reg, 1, &value);
	if (ret < 0)
		goto out;
	value &= ~mask;
	value |= data;
	ret = max8649_write_device(i2c, reg, 1, &value);
out:
	mutex_unlock(&info->io_lock);
	return ret;
}

static inline int check_range(int min_uV, int max_uV)
{
	if ((min_uV < MAX8649_DCDC_VMIN) || (max_uV > MAX8649_DCDC_VMAX)
		|| (min_uV > max_uV))
		return -EINVAL;
	return 0;
}

static int max8649_list_voltage(struct regulator_dev *rdev, unsigned index)
{
	return (MAX8649_DCDC_VMIN + index * MAX8649_DCDC_STEP);
}

static int max8649_get_voltage(struct regulator_dev *rdev)
{
	struct max8649_regulator_info *info = rdev_get_drvdata(rdev);
	unsigned char data;
	int ret;

	ret = max8649_reg_read(info->i2c, info->vol_reg);
	if (ret < 0)
		return ret;
	data = (unsigned char)ret & MAX8649_VOL_MASK;
	return max8649_list_voltage(rdev, data);
}

static int max8649_set_voltage(struct regulator_dev *rdev,
			       int min_uV, int max_uV)
{
	struct max8649_regulator_info *info = rdev_get_drvdata(rdev);
	unsigned char data, mask;

	if (check_range(min_uV, max_uV)) {
		dev_err(info->dev, "invalid voltage range (%d, %d) uV\n",
			min_uV, max_uV);
		return -EINVAL;
	}
	data = (min_uV - MAX8649_DCDC_VMIN + MAX8649_DCDC_STEP - 1)
		/ MAX8649_DCDC_STEP;
	mask = MAX8649_VOL_MASK;

	return max8649_set_bits(info->i2c, info->vol_reg, mask, data);
}

/* EN_PD means pulldown on EN input */
static int max8649_enable(struct regulator_dev *rdev)
{
	struct max8649_regulator_info *info = rdev_get_drvdata(rdev);
	return max8649_set_bits(info->i2c, MAX8649_CONTROL, MAX8649_EN_PD, 0);
}

/*
 * Applied internal pulldown resistor on EN input pin.
 * If pulldown EN pin outside, it would be better.
 */
static int max8649_disable(struct regulator_dev *rdev)
{
	struct max8649_regulator_info *info = rdev_get_drvdata(rdev);
	return max8649_set_bits(info->i2c, MAX8649_CONTROL, MAX8649_EN_PD,
				MAX8649_EN_PD);
}

static int max8649_is_enabled(struct regulator_dev *rdev)
{
	struct max8649_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;

	ret = max8649_reg_read(info->i2c, MAX8649_CONTROL);
	if (ret < 0)
		return ret;
	return !((unsigned char)ret & MAX8649_EN_PD);
}

static int max8649_enable_time(struct regulator_dev *rdev)
{
	struct max8649_regulator_info *info = rdev_get_drvdata(rdev);
	int voltage, rate, ret;

	/* get voltage */
	ret = max8649_reg_read(info->i2c, info->vol_reg);
	if (ret < 0)
		return ret;
	ret &= MAX8649_VOL_MASK;
	voltage = max8649_list_voltage(rdev, (unsigned char)ret); /* uV */

	/* get rate */
	ret = max8649_reg_read(info->i2c, MAX8649_RAMP);
	if (ret < 0)
		return ret;
	ret = (ret & MAX8649_RAMP_MASK) >> 5;
	rate = (32 * 1000) >> ret;	/* uV/uS */

	return (voltage / rate);
}

static int max8649_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct max8649_regulator_info *info = rdev_get_drvdata(rdev);

	switch (mode) {
	case REGULATOR_MODE_FAST:
		max8649_set_bits(info->i2c, info->vol_reg, MAX8649_FORCE_PWM,
				 MAX8649_FORCE_PWM);
		break;
	case REGULATOR_MODE_NORMAL:
		max8649_set_bits(info->i2c, info->vol_reg,
				 MAX8649_FORCE_PWM, 0);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static unsigned int max8649_get_mode(struct regulator_dev *rdev)
{
	struct max8649_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;

	ret = max8649_reg_read(info->i2c, info->vol_reg);
	if (ret & MAX8649_FORCE_PWM)
		return REGULATOR_MODE_FAST;
	return REGULATOR_MODE_NORMAL;
}

static struct regulator_ops max8649_dcdc_ops = {
	.set_voltage	= max8649_set_voltage,
	.get_voltage	= max8649_get_voltage,
	.list_voltage	= max8649_list_voltage,
	.enable		= max8649_enable,
	.disable	= max8649_disable,
	.is_enabled	= max8649_is_enabled,
	.enable_time	= max8649_enable_time,
	.set_mode	= max8649_set_mode,
	.get_mode	= max8649_get_mode,

};

static struct regulator_desc dcdc_desc = {
	.name		= "max8649",
	.ops		= &max8649_dcdc_ops,
	.type		= REGULATOR_VOLTAGE,
	.n_voltages	= 1 << 6,
	.owner		= THIS_MODULE,
};

static int __devinit max8649_regulator_probe(struct i2c_client *client,
					     const struct i2c_device_id *id)
{
	struct max8649_platform_data *pdata = client->dev.platform_data;
	struct max8649_regulator_info *info = NULL;
	unsigned char data;
	int ret;

	info = kzalloc(sizeof(struct max8649_regulator_info), GFP_KERNEL);
	if (!info) {
		dev_err(&client->dev, "No enough memory\n");
		return -ENOMEM;
	}

	info->i2c = client;
	info->dev = &client->dev;
	mutex_init(&info->io_lock);
	i2c_set_clientdata(client, info);

	info->mode = pdata->mode;
	switch (info->mode) {
	case 0:
		info->vol_reg = MAX8649_MODE0;
		break;
	case 1:
		info->vol_reg = MAX8649_MODE1;
		break;
	case 2:
		info->vol_reg = MAX8649_MODE2;
		break;
	case 3:
		info->vol_reg = MAX8649_MODE3;
		break;
	default:
		break;
	}

	ret = max8649_reg_read(info->i2c, MAX8649_CHIP_ID1);
	if (ret < 0) {
		dev_err(info->dev, "Failed to detect ID of MAX8649:%d\n",
			ret);
		goto out;
	}
	dev_info(info->dev, "Detected MAX8649 (ID:%x)\n", ret);

	/* enable VID0 & VID1 */
	max8649_set_bits(info->i2c, MAX8649_CONTROL, MAX8649_VID_MASK, 0);

	/* enable/disable external clock synchronization */
	info->extclk = pdata->extclk;
	data = (info->extclk) ? MAX8649_SYNC_EXTCLK : 0;
	max8649_set_bits(info->i2c, info->vol_reg, MAX8649_SYNC_EXTCLK, data);
	if (info->extclk) {
		/* set external clock frequency */
		info->extclk_freq = pdata->extclk_freq;
		max8649_set_bits(info->i2c, MAX8649_SYNC, MAX8649_EXT_MASK,
				 info->extclk_freq);
	}

	if (pdata->ramp_timing) {
		info->ramp_timing = pdata->ramp_timing;
		max8649_set_bits(info->i2c, MAX8649_RAMP, MAX8649_RAMP_MASK,
				 info->ramp_timing << 5);
	}

	info->ramp_down = pdata->ramp_down;
	if (info->ramp_down) {
		max8649_set_bits(info->i2c, MAX8649_RAMP, MAX8649_RAMP_DOWN,
				 MAX8649_RAMP_DOWN);
	}

	info->regulator = regulator_register(&dcdc_desc, &client->dev,
					     pdata->regulator, info);
	if (IS_ERR(info->regulator)) {
		dev_err(info->dev, "failed to register regulator %s\n",
			dcdc_desc.name);
		ret = PTR_ERR(info->regulator);
		goto out;
	}

	dev_info(info->dev, "Max8649 regulator device is detected.\n");
	return 0;
out:
	i2c_set_clientdata(client, NULL);
	kfree(info);
	return ret;
}

static int __devexit max8649_regulator_remove(struct i2c_client *client)
{
	struct max8649_regulator_info *info = i2c_get_clientdata(client);

	if (info) {
		if (info->regulator)
			regulator_unregister(info->regulator);
		i2c_set_clientdata(client, NULL);
		kfree(info);
	}

	return 0;
}

static const struct i2c_device_id max8649_id[] = {
	{ "max8649", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max8649_id);

static struct i2c_driver max8649_driver = {
	.probe		= max8649_regulator_probe,
	.remove		= __devexit_p(max8649_regulator_remove),
	.driver		= {
		.name	= "max8649",
	},
	.id_table	= max8649_id,
};

static int __init max8649_init(void)
{
	return i2c_add_driver(&max8649_driver);
}
subsys_initcall(max8649_init);

static void __exit max8649_exit(void)
{
	i2c_del_driver(&max8649_driver);
}
module_exit(max8649_exit);

/* Module information */
MODULE_DESCRIPTION("MAXIM 8649 voltage regulator driver");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_LICENSE("GPL");

