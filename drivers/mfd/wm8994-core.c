/*
 * wm8994-core.c  --  Device access for Wolfson WM8994
 *
 * Copyright 2009 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/mfd/core.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/machine.h>

#include <linux/mfd/wm8994/core.h>
#include <linux/mfd/wm8994/pdata.h>
#include <linux/mfd/wm8994/registers.h>

static int wm8994_read(struct wm8994 *wm8994, unsigned short reg,
		       int bytes, void *dest)
{
	int ret, i;
	u16 *buf = dest;

	BUG_ON(bytes % 2);
	BUG_ON(bytes <= 0);

	ret = wm8994->read_dev(wm8994, reg, bytes, dest);
	if (ret < 0)
		return ret;

	for (i = 0; i < bytes / 2; i++) {
		buf[i] = be16_to_cpu(buf[i]);

		dev_vdbg(wm8994->dev, "Read %04x from R%d(0x%x)\n",
			 buf[i], reg + i, reg + i);
	}

	return 0;
}

/**
 * wm8994_reg_read: Read a single WM8994 register.
 *
 * @wm8994: Device to read from.
 * @reg: Register to read.
 */
int wm8994_reg_read(struct wm8994 *wm8994, unsigned short reg)
{
	unsigned short val;
	int ret;

	mutex_lock(&wm8994->io_lock);

	ret = wm8994_read(wm8994, reg, 2, &val);

	mutex_unlock(&wm8994->io_lock);

	if (ret < 0)
		return ret;
	else
		return val;
}
EXPORT_SYMBOL_GPL(wm8994_reg_read);

/**
 * wm8994_bulk_read: Read multiple WM8994 registers
 *
 * @wm8994: Device to read from
 * @reg: First register
 * @count: Number of registers
 * @buf: Buffer to fill.
 */
int wm8994_bulk_read(struct wm8994 *wm8994, unsigned short reg,
		     int count, u16 *buf)
{
	int ret;

	mutex_lock(&wm8994->io_lock);

	ret = wm8994_read(wm8994, reg, count * 2, buf);

	mutex_unlock(&wm8994->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(wm8994_bulk_read);

static int wm8994_write(struct wm8994 *wm8994, unsigned short reg,
			int bytes, void *src)
{
	u16 *buf = src;
	int i;

	BUG_ON(bytes % 2);
	BUG_ON(bytes <= 0);

	for (i = 0; i < bytes / 2; i++) {
		dev_vdbg(wm8994->dev, "Write %04x to R%d(0x%x)\n",
			 buf[i], reg + i, reg + i);

		buf[i] = cpu_to_be16(buf[i]);
	}

	return wm8994->write_dev(wm8994, reg, bytes, src);
}

/**
 * wm8994_reg_write: Write a single WM8994 register.
 *
 * @wm8994: Device to write to.
 * @reg: Register to write to.
 * @val: Value to write.
 */
int wm8994_reg_write(struct wm8994 *wm8994, unsigned short reg,
		     unsigned short val)
{
	int ret;

	mutex_lock(&wm8994->io_lock);

	ret = wm8994_write(wm8994, reg, 2, &val);

	mutex_unlock(&wm8994->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(wm8994_reg_write);

/**
 * wm8994_set_bits: Set the value of a bitfield in a WM8994 register
 *
 * @wm8994: Device to write to.
 * @reg: Register to write to.
 * @mask: Mask of bits to set.
 * @val: Value to set (unshifted)
 */
int wm8994_set_bits(struct wm8994 *wm8994, unsigned short reg,
		    unsigned short mask, unsigned short val)
{
	int ret;
	u16 r;

	mutex_lock(&wm8994->io_lock);

	ret = wm8994_read(wm8994, reg, 2, &r);
	if (ret < 0)
		goto out;

	r &= ~mask;
	r |= val;

	ret = wm8994_write(wm8994, reg, 2, &r);

out:
	mutex_unlock(&wm8994->io_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(wm8994_set_bits);

static struct mfd_cell wm8994_regulator_devs[] = {
	{ .name = "wm8994-ldo", .id = 1 },
	{ .name = "wm8994-ldo", .id = 2 },
};

static struct mfd_cell wm8994_devs[] = {
	{ .name = "wm8994-codec" },
	{ .name = "wm8994-gpio" },
};

/*
 * Supplies for the main bulk of CODEC; the LDO supplies are ignored
 * and should be handled via the standard regulator API supply
 * management.
 */
static const char *wm8994_main_supplies[] = {
	"DBVDD",
	"DCVDD",
	"AVDD1",
	"AVDD2",
	"CPVDD",
	"SPKVDD1",
	"SPKVDD2",
};

#ifdef CONFIG_PM
static int wm8994_device_suspend(struct device *dev)
{
	struct wm8994 *wm8994 = dev_get_drvdata(dev);
	int ret;

	/* GPIO configuration state is saved here since we may be configuring
	 * the GPIO alternate functions even if we're not using the gpiolib
	 * driver for them.
	 */
	ret = wm8994_read(wm8994, WM8994_GPIO_1, WM8994_NUM_GPIO_REGS * 2,
			  &wm8994->gpio_regs);
	if (ret < 0)
		dev_err(dev, "Failed to save GPIO registers: %d\n", ret);

	/* For similar reasons we also stash the regulator states */
	ret = wm8994_read(wm8994, WM8994_LDO_1, WM8994_NUM_LDO_REGS * 2,
			  &wm8994->ldo_regs);
	if (ret < 0)
		dev_err(dev, "Failed to save LDO registers: %d\n", ret);

	ret = regulator_bulk_disable(ARRAY_SIZE(wm8994_main_supplies),
				     wm8994->supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to disable supplies: %d\n", ret);
		return ret;
	}

	return 0;
}

static int wm8994_device_resume(struct device *dev)
{
	struct wm8994 *wm8994 = dev_get_drvdata(dev);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8994_main_supplies),
				    wm8994->supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	ret = wm8994_write(wm8994, WM8994_LDO_1, WM8994_NUM_LDO_REGS * 2,
			   &wm8994->ldo_regs);
	if (ret < 0)
		dev_err(dev, "Failed to restore LDO registers: %d\n", ret);

	ret = wm8994_write(wm8994, WM8994_GPIO_1, WM8994_NUM_GPIO_REGS * 2,
			   &wm8994->gpio_regs);
	if (ret < 0)
		dev_err(dev, "Failed to restore GPIO registers: %d\n", ret);

	return 0;
}
#endif

#ifdef CONFIG_REGULATOR
static int wm8994_ldo_in_use(struct wm8994_pdata *pdata, int ldo)
{
	struct wm8994_ldo_pdata *ldo_pdata;

	if (!pdata)
		return 0;

	ldo_pdata = &pdata->ldo[ldo];

	if (!ldo_pdata->init_data)
		return 0;

	return ldo_pdata->init_data->num_consumer_supplies != 0;
}
#else
static int wm8994_ldo_in_use(struct wm8994_pdata *pdata, int ldo)
{
	return 0;
}
#endif

/*
 * Instantiate the generic non-control parts of the device.
 */
static int wm8994_device_init(struct wm8994 *wm8994, unsigned long id, int irq)
{
	struct wm8994_pdata *pdata = wm8994->dev->platform_data;
	int ret, i;

	mutex_init(&wm8994->io_lock);
	dev_set_drvdata(wm8994->dev, wm8994);

	/* Add the on-chip regulators first for bootstrapping */
	ret = mfd_add_devices(wm8994->dev, -1,
			      wm8994_regulator_devs,
			      ARRAY_SIZE(wm8994_regulator_devs),
			      NULL, 0);
	if (ret != 0) {
		dev_err(wm8994->dev, "Failed to add children: %d\n", ret);
		goto err;
	}

	wm8994->supplies = kzalloc(sizeof(struct regulator_bulk_data) *
				   ARRAY_SIZE(wm8994_main_supplies),
				   GFP_KERNEL);
	if (!wm8994->supplies)
		goto err;

	for (i = 0; i < ARRAY_SIZE(wm8994_main_supplies); i++)
		wm8994->supplies[i].supply = wm8994_main_supplies[i];

	ret = regulator_bulk_get(wm8994->dev, ARRAY_SIZE(wm8994_main_supplies),
				 wm8994->supplies);
	if (ret != 0) {
		dev_err(wm8994->dev, "Failed to get supplies: %d\n", ret);
		goto err_supplies;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8994_main_supplies),
				    wm8994->supplies);
	if (ret != 0) {
		dev_err(wm8994->dev, "Failed to enable supplies: %d\n", ret);
		goto err_get;
	}

	ret = wm8994_reg_read(wm8994, WM8994_SOFTWARE_RESET);
	if (ret < 0) {
		dev_err(wm8994->dev, "Failed to read ID register\n");
		goto err_enable;
	}
	if (ret != 0x8994) {
		dev_err(wm8994->dev, "Device is not a WM8994, ID is %x\n",
			ret);
		ret = -EINVAL;
		goto err_enable;
	}

	ret = wm8994_reg_read(wm8994, WM8994_CHIP_REVISION);
	if (ret < 0) {
		dev_err(wm8994->dev, "Failed to read revision register: %d\n",
			ret);
		goto err_enable;
	}

	switch (ret) {
	case 0:
	case 1:
		dev_warn(wm8994->dev, "revision %c not fully supported\n",
			'A' + ret);
		break;
	default:
		dev_info(wm8994->dev, "revision %c\n", 'A' + ret);
		break;
	}


	if (pdata) {
		wm8994->gpio_base = pdata->gpio_base;

		/* GPIO configuration is only applied if it's non-zero */
		for (i = 0; i < ARRAY_SIZE(pdata->gpio_defaults); i++) {
			if (pdata->gpio_defaults[i]) {
				wm8994_set_bits(wm8994, WM8994_GPIO_1 + i,
						0xffff,
						pdata->gpio_defaults[i]);
			}
		}
	}

	/* In some system designs where the regulators are not in use,
	 * we can achieve a small reduction in leakage currents by
	 * floating LDO outputs.  This bit makes no difference if the
	 * LDOs are enabled, it only affects cases where the LDOs were
	 * in operation and are then disabled.
	 */
	for (i = 0; i < WM8994_NUM_LDO_REGS; i++) {
		if (wm8994_ldo_in_use(pdata, i))
			wm8994_set_bits(wm8994, WM8994_LDO_1 + i,
					WM8994_LDO1_DISCH, WM8994_LDO1_DISCH);
		else
			wm8994_set_bits(wm8994, WM8994_LDO_1 + i,
					WM8994_LDO1_DISCH, 0);
	}

	ret = mfd_add_devices(wm8994->dev, -1,
			      wm8994_devs, ARRAY_SIZE(wm8994_devs),
			      NULL, 0);
	if (ret != 0) {
		dev_err(wm8994->dev, "Failed to add children: %d\n", ret);
		goto err_enable;
	}

	return 0;

err_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm8994_main_supplies),
			       wm8994->supplies);
err_get:
	regulator_bulk_free(ARRAY_SIZE(wm8994_main_supplies), wm8994->supplies);
err_supplies:
	kfree(wm8994->supplies);
err:
	mfd_remove_devices(wm8994->dev);
	kfree(wm8994);
	return ret;
}

static void wm8994_device_exit(struct wm8994 *wm8994)
{
	mfd_remove_devices(wm8994->dev);
	regulator_bulk_disable(ARRAY_SIZE(wm8994_main_supplies),
			       wm8994->supplies);
	regulator_bulk_free(ARRAY_SIZE(wm8994_main_supplies), wm8994->supplies);
	kfree(wm8994->supplies);
	kfree(wm8994);
}

static int wm8994_i2c_read_device(struct wm8994 *wm8994, unsigned short reg,
				  int bytes, void *dest)
{
	struct i2c_client *i2c = wm8994->control_data;
	int ret;
	u16 r = cpu_to_be16(reg);

	ret = i2c_master_send(i2c, (unsigned char *)&r, 2);
	if (ret < 0)
		return ret;
	if (ret != 2)
		return -EIO;

	ret = i2c_master_recv(i2c, dest, bytes);
	if (ret < 0)
		return ret;
	if (ret != bytes)
		return -EIO;
	return 0;
}

/* Currently we allocate the write buffer on the stack; this is OK for
 * small writes - if we need to do large writes this will need to be
 * revised.
 */
static int wm8994_i2c_write_device(struct wm8994 *wm8994, unsigned short reg,
				   int bytes, void *src)
{
	struct i2c_client *i2c = wm8994->control_data;
	unsigned char msg[bytes + 2];
	int ret;

	reg = cpu_to_be16(reg);
	memcpy(&msg[0], &reg, 2);
	memcpy(&msg[2], src, bytes);

	ret = i2c_master_send(i2c, msg, bytes + 2);
	if (ret < 0)
		return ret;
	if (ret < bytes + 2)
		return -EIO;

	return 0;
}

static int wm8994_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct wm8994 *wm8994;

	wm8994 = kzalloc(sizeof(struct wm8994), GFP_KERNEL);
	if (wm8994 == NULL) {
		kfree(i2c);
		return -ENOMEM;
	}

	i2c_set_clientdata(i2c, wm8994);
	wm8994->dev = &i2c->dev;
	wm8994->control_data = i2c;
	wm8994->read_dev = wm8994_i2c_read_device;
	wm8994->write_dev = wm8994_i2c_write_device;

	return wm8994_device_init(wm8994, id->driver_data, i2c->irq);
}

static int wm8994_i2c_remove(struct i2c_client *i2c)
{
	struct wm8994 *wm8994 = i2c_get_clientdata(i2c);

	wm8994_device_exit(wm8994);

	return 0;
}

#ifdef CONFIG_PM
static int wm8994_i2c_suspend(struct i2c_client *i2c, pm_message_t state)
{
	return wm8994_device_suspend(&i2c->dev);
}

static int wm8994_i2c_resume(struct i2c_client *i2c)
{
	return wm8994_device_resume(&i2c->dev);
}
#else
#define wm8994_i2c_suspend NULL
#define wm8994_i2c_resume NULL
#endif

static const struct i2c_device_id wm8994_i2c_id[] = {
	{ "wm8994", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8994_i2c_id);

static struct i2c_driver wm8994_i2c_driver = {
	.driver = {
		   .name = "wm8994",
		   .owner = THIS_MODULE,
	},
	.probe = wm8994_i2c_probe,
	.remove = wm8994_i2c_remove,
	.suspend = wm8994_i2c_suspend,
	.resume = wm8994_i2c_resume,
	.id_table = wm8994_i2c_id,
};

static int __init wm8994_i2c_init(void)
{
	int ret;

	ret = i2c_add_driver(&wm8994_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register wm8994 I2C driver: %d\n", ret);

	return ret;
}
module_init(wm8994_i2c_init);

static void __exit wm8994_i2c_exit(void)
{
	i2c_del_driver(&wm8994_i2c_driver);
}
module_exit(wm8994_i2c_exit);

MODULE_DESCRIPTION("Core support for the WM8994 audio CODEC");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
