/*
 * Touchscreen driver for Marvell 88PM860x
 *
 * Copyright (C) 2009 Marvell International Ltd.
 * 	Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/mfd/88pm860x.h>
#include <linux/slab.h>

#define MEAS_LEN		(8)
#define ACCURATE_BIT		(12)

/* touch register */
#define MEAS_EN3		(0x52)

#define MEAS_TSIX_1		(0x8D)
#define MEAS_TSIX_2		(0x8E)
#define MEAS_TSIY_1		(0x8F)
#define MEAS_TSIY_2		(0x90)
#define MEAS_TSIZ1_1		(0x91)
#define MEAS_TSIZ1_2		(0x92)
#define MEAS_TSIZ2_1		(0x93)
#define MEAS_TSIZ2_2		(0x94)

/* bit definitions of touch */
#define MEAS_PD_EN		(1 << 3)
#define MEAS_TSIX_EN		(1 << 4)
#define MEAS_TSIY_EN		(1 << 5)
#define MEAS_TSIZ1_EN		(1 << 6)
#define MEAS_TSIZ2_EN		(1 << 7)

struct pm860x_touch {
	struct input_dev *idev;
	struct i2c_client *i2c;
	struct pm860x_chip *chip;
	int irq;
	int res_x;		/* resistor of Xplate */
};

static irqreturn_t pm860x_touch_handler(int irq, void *data)
{
	struct pm860x_touch *touch = data;
	struct pm860x_chip *chip = touch->chip;
	unsigned char buf[MEAS_LEN];
	int x, y, pen_down;
	int z1, z2, rt = 0;
	int ret;

	ret = pm860x_bulk_read(touch->i2c, MEAS_TSIX_1, MEAS_LEN, buf);
	if (ret < 0)
		goto out;

	pen_down = buf[1] & (1 << 6);
	x = ((buf[0] & 0xFF) << 4) | (buf[1] & 0x0F);
	y = ((buf[2] & 0xFF) << 4) | (buf[3] & 0x0F);
	z1 = ((buf[4] & 0xFF) << 4) | (buf[5] & 0x0F);
	z2 = ((buf[6] & 0xFF) << 4) | (buf[7] & 0x0F);

	if (pen_down) {
		if ((x != 0) && (z1 != 0) && (touch->res_x != 0)) {
			rt = z2 / z1 - 1;
			rt = (rt * touch->res_x * x) >> ACCURATE_BIT;
			dev_dbg(chip->dev, "z1:%d, z2:%d, rt:%d\n",
				z1, z2, rt);
		}
		input_report_abs(touch->idev, ABS_X, x);
		input_report_abs(touch->idev, ABS_Y, y);
		input_report_abs(touch->idev, ABS_PRESSURE, rt);
		input_report_key(touch->idev, BTN_TOUCH, 1);
		dev_dbg(chip->dev, "pen down at [%d, %d].\n", x, y);
	} else {
		input_report_abs(touch->idev, ABS_PRESSURE, 0);
		input_report_key(touch->idev, BTN_TOUCH, 0);
		dev_dbg(chip->dev, "pen release\n");
	}
	input_sync(touch->idev);

out:
	return IRQ_HANDLED;
}

static int pm860x_touch_open(struct input_dev *dev)
{
	struct pm860x_touch *touch = input_get_drvdata(dev);
	int data, ret;

	data = MEAS_PD_EN | MEAS_TSIX_EN | MEAS_TSIY_EN
		| MEAS_TSIZ1_EN | MEAS_TSIZ2_EN;
	ret = pm860x_set_bits(touch->i2c, MEAS_EN3, data, data);
	if (ret < 0)
		goto out;
	return 0;
out:
	return ret;
}

static void pm860x_touch_close(struct input_dev *dev)
{
	struct pm860x_touch *touch = input_get_drvdata(dev);
	int data;

	data = MEAS_PD_EN | MEAS_TSIX_EN | MEAS_TSIY_EN
		| MEAS_TSIZ1_EN | MEAS_TSIZ2_EN;
	pm860x_set_bits(touch->i2c, MEAS_EN3, data, 0);
}

#ifdef CONFIG_OF
static int pm860x_touch_dt_init(struct platform_device *pdev,
					  struct pm860x_chip *chip,
					  int *res_x)
{
	struct device_node *np = pdev->dev.parent->of_node;
	struct i2c_client *i2c = (chip->id == CHIP_PM8607) ? chip->client \
				 : chip->companion;
	int data, n, ret;
	if (!np)
		return -ENODEV;
	np = of_find_node_by_name(np, "touch");
	if (!np) {
		dev_err(&pdev->dev, "Can't find touch node\n");
		return -EINVAL;
	}
	/* set GPADC MISC1 register */
	data = 0;
	if (!of_property_read_u32(np, "marvell,88pm860x-gpadc-prebias", &n))
		data |= (n << 1) & PM8607_GPADC_PREBIAS_MASK;
	if (!of_property_read_u32(np, "marvell,88pm860x-gpadc-slot-cycle", &n))
		data |= (n << 3) & PM8607_GPADC_SLOT_CYCLE_MASK;
	if (!of_property_read_u32(np, "marvell,88pm860x-gpadc-off-scale", &n))
		data |= (n << 5) & PM8607_GPADC_OFF_SCALE_MASK;
	if (!of_property_read_u32(np, "marvell,88pm860x-gpadc-sw-cal", &n))
		data |= (n << 7) & PM8607_GPADC_SW_CAL_MASK;
	if (data) {
		ret = pm860x_reg_write(i2c, PM8607_GPADC_MISC1, data);
		if (ret < 0)
			return -EINVAL;
	}
	/* set tsi prebias time */
	if (!of_property_read_u32(np, "marvell,88pm860x-tsi-prebias", &data)) {
		ret = pm860x_reg_write(i2c, PM8607_TSI_PREBIAS, data);
		if (ret < 0)
			return -EINVAL;
	}
	/* set prebias & prechg time of pen detect */
	data = 0;
	if (!of_property_read_u32(np, "marvell,88pm860x-pen-prebias", &n))
		data |= n & PM8607_PD_PREBIAS_MASK;
	if (!of_property_read_u32(np, "marvell,88pm860x-pen-prechg", &n))
		data |= n & PM8607_PD_PRECHG_MASK;
	if (data) {
		ret = pm860x_reg_write(i2c, PM8607_PD_PREBIAS, data);
		if (ret < 0)
			return -EINVAL;
	}
	of_property_read_u32(np, "marvell,88pm860x-resistor-X", res_x);
	return 0;
}
#else
#define pm860x_touch_dt_init(x, y, z)	(-1)
#endif

static int pm860x_touch_probe(struct platform_device *pdev)
{
	struct pm860x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct pm860x_touch_pdata *pdata = pdev->dev.platform_data;
	struct pm860x_touch *touch;
	struct i2c_client *i2c = (chip->id == CHIP_PM8607) ? chip->client \
				 : chip->companion;
	int irq, ret, res_x = 0, data = 0;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "No IRQ resource!\n");
		return -EINVAL;
	}

	if (pm860x_touch_dt_init(pdev, chip, &res_x)) {
		if (pdata) {
			/* set GPADC MISC1 register */
			data = 0;
			data |= (pdata->gpadc_prebias << 1)
				& PM8607_GPADC_PREBIAS_MASK;
			data |= (pdata->slot_cycle << 3)
				& PM8607_GPADC_SLOT_CYCLE_MASK;
			data |= (pdata->off_scale << 5)
				& PM8607_GPADC_OFF_SCALE_MASK;
			data |= (pdata->sw_cal << 7)
				& PM8607_GPADC_SW_CAL_MASK;
			if (data) {
				ret = pm860x_reg_write(i2c,
					PM8607_GPADC_MISC1, data);
				if (ret < 0)
					return -EINVAL;
			}
			/* set tsi prebias time */
			if (pdata->tsi_prebias) {
				data = pdata->tsi_prebias;
				ret = pm860x_reg_write(i2c,
					PM8607_TSI_PREBIAS, data);
				if (ret < 0)
					return -EINVAL;
			}
			/* set prebias & prechg time of pen detect */
			data = 0;
			data |= pdata->pen_prebias
				& PM8607_PD_PREBIAS_MASK;
			data |= (pdata->pen_prechg << 5)
				& PM8607_PD_PRECHG_MASK;
			if (data) {
				ret = pm860x_reg_write(i2c,
					PM8607_PD_PREBIAS, data);
				if (ret < 0)
					return -EINVAL;
			}
			res_x = pdata->res_x;
		} else {
			dev_err(&pdev->dev, "failed to get platform data\n");
			return -EINVAL;
		}
	}
	/* enable GPADC */
	ret = pm860x_set_bits(i2c, PM8607_GPADC_MISC1, PM8607_GPADC_EN,
			      PM8607_GPADC_EN);
	if (ret)
		return ret;

	touch = kzalloc(sizeof(struct pm860x_touch), GFP_KERNEL);
	if (touch == NULL)
		return -ENOMEM;
	dev_set_drvdata(&pdev->dev, touch);

	touch->idev = input_allocate_device();
	if (touch->idev == NULL) {
		dev_err(&pdev->dev, "Failed to allocate input device!\n");
		ret = -ENOMEM;
		goto out;
	}

	touch->idev->name = "88pm860x-touch";
	touch->idev->phys = "88pm860x/input0";
	touch->idev->id.bustype = BUS_I2C;
	touch->idev->dev.parent = &pdev->dev;
	touch->idev->open = pm860x_touch_open;
	touch->idev->close = pm860x_touch_close;
	touch->chip = chip;
	touch->i2c = i2c;
	touch->irq = irq;
	touch->res_x = res_x;
	input_set_drvdata(touch->idev, touch);

	ret = request_threaded_irq(touch->irq, NULL, pm860x_touch_handler,
				   IRQF_ONESHOT, "touch", touch);
	if (ret < 0)
		goto out_irq;

	__set_bit(EV_ABS, touch->idev->evbit);
	__set_bit(ABS_X, touch->idev->absbit);
	__set_bit(ABS_Y, touch->idev->absbit);
	__set_bit(ABS_PRESSURE, touch->idev->absbit);
	__set_bit(EV_SYN, touch->idev->evbit);
	__set_bit(EV_KEY, touch->idev->evbit);
	__set_bit(BTN_TOUCH, touch->idev->keybit);

	input_set_abs_params(touch->idev, ABS_X, 0, 1 << ACCURATE_BIT, 0, 0);
	input_set_abs_params(touch->idev, ABS_Y, 0, 1 << ACCURATE_BIT, 0, 0);
	input_set_abs_params(touch->idev, ABS_PRESSURE, 0, 1 << ACCURATE_BIT,
				0, 0);

	ret = input_register_device(touch->idev);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to register touch!\n");
		goto out_rg;
	}

	platform_set_drvdata(pdev, touch);
	return 0;
out_rg:
	free_irq(touch->irq, touch);
out_irq:
	input_free_device(touch->idev);
out:
	kfree(touch);
	return ret;
}

static int __devexit pm860x_touch_remove(struct platform_device *pdev)
{
	struct pm860x_touch *touch = platform_get_drvdata(pdev);

	input_unregister_device(touch->idev);
	free_irq(touch->irq, touch);
	platform_set_drvdata(pdev, NULL);
	kfree(touch);
	return 0;
}

static struct platform_driver pm860x_touch_driver = {
	.driver	= {
		.name	= "88pm860x-touch",
		.owner	= THIS_MODULE,
	},
	.probe	= pm860x_touch_probe,
	.remove	= pm860x_touch_remove,
};
module_platform_driver(pm860x_touch_driver);

MODULE_DESCRIPTION("Touchscreen driver for Marvell Semiconductor 88PM860x");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:88pm860x-touch");

