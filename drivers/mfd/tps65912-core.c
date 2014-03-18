/*
 * tps65912-core.c  --  TI TPS65912x
 *
 * Copyright 2011 Texas Instruments Inc.
 *
 * Author: Margarita Olaya Cabrera <magi@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  This driver is based on wm8350 implementation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tps65912.h>

static const struct mfd_cell tps65912s[] = {
	{
		.name = "tps65912-pmic",
	},
};

int tps65912_set_bits(struct tps65912 *tps65912, u8 reg, u8 mask)
{
	u8 data;
	int err;

	mutex_lock(&tps65912->io_mutex);

	err = tps65912->read(tps65912, reg, 1, &data);
	if (err) {
		dev_err(tps65912->dev, "Read from reg 0x%x failed\n", reg);
		goto out;
	}

	data |= mask;
	err = tps65912->write(tps65912, reg, 1, &data);
	if (err)
		dev_err(tps65912->dev, "Write to reg 0x%x failed\n", reg);

out:
	mutex_unlock(&tps65912->io_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(tps65912_set_bits);

int tps65912_clear_bits(struct tps65912 *tps65912, u8 reg, u8 mask)
{
	u8 data;
	int err;

	mutex_lock(&tps65912->io_mutex);
	err = tps65912->read(tps65912, reg, 1, &data);
	if (err) {
		dev_err(tps65912->dev, "Read from reg 0x%x failed\n", reg);
		goto out;
	}

	data &= ~mask;
	err = tps65912->write(tps65912, reg, 1, &data);
	if (err)
		dev_err(tps65912->dev, "Write to reg 0x%x failed\n", reg);

out:
	mutex_unlock(&tps65912->io_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(tps65912_clear_bits);

static inline int tps65912_read(struct tps65912 *tps65912, u8 reg)
{
	u8 val;
	int err;

	err = tps65912->read(tps65912, reg, 1, &val);
	if (err < 0)
		return err;

	return val;
}

static inline int tps65912_write(struct tps65912 *tps65912, u8 reg, u8 val)
{
	return tps65912->write(tps65912, reg, 1, &val);
}

int tps65912_reg_read(struct tps65912 *tps65912, u8 reg)
{
	int data;

	mutex_lock(&tps65912->io_mutex);

	data = tps65912_read(tps65912, reg);
	if (data < 0)
		dev_err(tps65912->dev, "Read from reg 0x%x failed\n", reg);

	mutex_unlock(&tps65912->io_mutex);
	return data;
}
EXPORT_SYMBOL_GPL(tps65912_reg_read);

int tps65912_reg_write(struct tps65912 *tps65912, u8 reg, u8 val)
{
	int err;

	mutex_lock(&tps65912->io_mutex);

	err = tps65912_write(tps65912, reg, val);
	if (err < 0)
		dev_err(tps65912->dev, "Write for reg 0x%x failed\n", reg);

	mutex_unlock(&tps65912->io_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(tps65912_reg_write);

int tps65912_device_init(struct tps65912 *tps65912)
{
	struct tps65912_board *pmic_plat_data = dev_get_platdata(tps65912->dev);
	struct tps65912_platform_data *init_data;
	int ret, dcdc_avs, value;

	init_data = kzalloc(sizeof(struct tps65912_platform_data), GFP_KERNEL);
	if (init_data == NULL)
		return -ENOMEM;

	mutex_init(&tps65912->io_mutex);
	dev_set_drvdata(tps65912->dev, tps65912);

	dcdc_avs = (pmic_plat_data->is_dcdc1_avs << 0 |
			pmic_plat_data->is_dcdc2_avs  << 1 |
				pmic_plat_data->is_dcdc3_avs << 2 |
					pmic_plat_data->is_dcdc4_avs << 3);
	if (dcdc_avs) {
		tps65912->read(tps65912, TPS65912_I2C_SPI_CFG, 1, &value);
		dcdc_avs |= value;
		tps65912->write(tps65912, TPS65912_I2C_SPI_CFG, 1, &dcdc_avs);
	}

	ret = mfd_add_devices(tps65912->dev, -1,
			      tps65912s, ARRAY_SIZE(tps65912s),
			      NULL, 0, NULL);
	if (ret < 0)
		goto err;

	init_data->irq = pmic_plat_data->irq;
	init_data->irq_base = pmic_plat_data->irq_base;
	ret = tps65912_irq_init(tps65912, init_data->irq, init_data);
	if (ret < 0)
		goto err;

	kfree(init_data);
	return ret;

err:
	kfree(init_data);
	mfd_remove_devices(tps65912->dev);
	return ret;
}

void tps65912_device_exit(struct tps65912 *tps65912)
{
	mfd_remove_devices(tps65912->dev);
	tps65912_irq_exit(tps65912);
}

MODULE_AUTHOR("Margarita Olaya	<magi@slimlogic.co.uk>");
MODULE_DESCRIPTION("TPS65912x chip family multi-function driver");
MODULE_LICENSE("GPL");
