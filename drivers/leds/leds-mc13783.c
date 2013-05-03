/*
 * LEDs driver for Freescale MC13783
 *
 * Copyright (C) 2010 Philippe Rétornaz
 *
 * Based on leds-da903x:
 * Copyright (C) 2008 Compulab, Ltd.
 *      Mike Rapoport <mike@compulab.co.il>
 *
 * Copyright (C) 2006-2008 Marvell International Ltd.
 *      Eric Miao <eric.miao@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/workqueue.h>
#include <linux/mfd/mc13xxx.h>
#include <linux/slab.h>

struct mc13783_led {
	struct led_classdev	cdev;
	struct work_struct	work;
	struct mc13xxx		*master;
	enum led_brightness	new_brightness;
	int			id;
};

#define MC13783_REG_LED_CONTROL_0	51
#define MC13783_LED_C0_ENABLE_BIT	(1 << 0)
#define MC13783_LED_C0_TRIODE_MD_BIT	(1 << 7)
#define MC13783_LED_C0_TRIODE_AD_BIT	(1 << 8)
#define MC13783_LED_C0_TRIODE_KP_BIT	(1 << 9)
#define MC13783_LED_C0_BOOST_BIT	(1 << 10)
#define MC13783_LED_C0_ABMODE_MASK	0x7
#define MC13783_LED_C0_ABMODE		11
#define MC13783_LED_C0_ABREF_MASK	0x3
#define MC13783_LED_C0_ABREF		14

#define MC13783_REG_LED_CONTROL_1	52
#define MC13783_LED_C1_TC1HALF_BIT	(1 << 18)

#define MC13783_REG_LED_CONTROL_2	53
#define MC13783_LED_C2_BL_P_MASK	0xf
#define MC13783_LED_C2_MD_P		9
#define MC13783_LED_C2_AD_P		13
#define MC13783_LED_C2_KP_P		17
#define MC13783_LED_C2_BL_C_MASK	0x7
#define MC13783_LED_C2_MD_C		0
#define MC13783_LED_C2_AD_C		3
#define MC13783_LED_C2_KP_C		6

#define MC13783_REG_LED_CONTROL_3	54
#define MC13783_LED_C3_TC_P		6
#define MC13783_LED_C3_TC_P_MASK	0x1f

#define MC13783_REG_LED_CONTROL_4	55
#define MC13783_REG_LED_CONTROL_5	56

#define MC13783_LED_Cx_PERIOD		21
#define MC13783_LED_Cx_PERIOD_MASK	0x3
#define MC13783_LED_Cx_SLEWLIM_BIT      (1 << 23)
#define MC13783_LED_Cx_TRIODE_TC_BIT	(1 << 23)
#define MC13783_LED_Cx_TC_C_MASK	0x3

static void mc13783_led_work(struct work_struct *work)
{
	struct mc13783_led *led = container_of(work, struct mc13783_led, work);
	int reg = 0;
	int mask = 0;
	int value = 0;
	int bank, off, shift;

	switch (led->id) {
	case MC13783_LED_MD:
		reg = MC13783_REG_LED_CONTROL_2;
		mask = MC13783_LED_C2_BL_P_MASK << MC13783_LED_C2_MD_P;
		value = (led->new_brightness >> 4) << MC13783_LED_C2_MD_P;
		break;
	case MC13783_LED_AD:
		reg = MC13783_REG_LED_CONTROL_2;
		mask = MC13783_LED_C2_BL_P_MASK << MC13783_LED_C2_AD_P;
		value = (led->new_brightness >> 4) << MC13783_LED_C2_AD_P;
		break;
	case MC13783_LED_KP:
		reg = MC13783_REG_LED_CONTROL_2;
		mask = MC13783_LED_C2_BL_P_MASK << MC13783_LED_C2_KP_P;
		value = (led->new_brightness >> 4) << MC13783_LED_C2_KP_P;
		break;
	case MC13783_LED_R1:
	case MC13783_LED_G1:
	case MC13783_LED_B1:
	case MC13783_LED_R2:
	case MC13783_LED_G2:
	case MC13783_LED_B2:
	case MC13783_LED_R3:
	case MC13783_LED_G3:
	case MC13783_LED_B3:
		off = led->id - MC13783_LED_R1;
		bank = off/3;
		reg = MC13783_REG_LED_CONTROL_3 + off/3;
		shift = (off - bank * 3) * 5 + MC13783_LED_C3_TC_P;
		value = (led->new_brightness >> 3) << shift;
		mask = MC13783_LED_C3_TC_P_MASK << shift;
		break;
	}

	mc13xxx_lock(led->master);

	mc13xxx_reg_rmw(led->master, reg, mask, value);

	mc13xxx_unlock(led->master);
}

static void mc13783_led_set(struct led_classdev *led_cdev,
			   enum led_brightness value)
{
	struct mc13783_led *led;

	led = container_of(led_cdev, struct mc13783_led, cdev);
	led->new_brightness = value;
	schedule_work(&led->work);
}

static int mc13783_led_setup(struct mc13783_led *led, int max_current)
{
	int shift = 0;
	int mask = 0;
	int value = 0;
	int reg = 0;
	int ret, bank;

	switch (led->id) {
	case MC13783_LED_MD:
		shift = MC13783_LED_C2_MD_C;
		mask = MC13783_LED_C2_BL_C_MASK;
		value = max_current & MC13783_LED_C2_BL_C_MASK;
		reg = MC13783_REG_LED_CONTROL_2;
		break;
	case MC13783_LED_AD:
		shift = MC13783_LED_C2_AD_C;
		mask = MC13783_LED_C2_BL_C_MASK;
		value = max_current & MC13783_LED_C2_BL_C_MASK;
		reg = MC13783_REG_LED_CONTROL_2;
		break;
	case MC13783_LED_KP:
		shift = MC13783_LED_C2_KP_C;
		mask = MC13783_LED_C2_BL_C_MASK;
		value = max_current & MC13783_LED_C2_BL_C_MASK;
		reg = MC13783_REG_LED_CONTROL_2;
		break;
	case MC13783_LED_R1:
	case MC13783_LED_G1:
	case MC13783_LED_B1:
	case MC13783_LED_R2:
	case MC13783_LED_G2:
	case MC13783_LED_B2:
	case MC13783_LED_R3:
	case MC13783_LED_G3:
	case MC13783_LED_B3:
		bank = (led->id - MC13783_LED_R1)/3;
		reg = MC13783_REG_LED_CONTROL_3 + bank;
		shift = ((led->id - MC13783_LED_R1) - bank * 3) * 2;
		mask = MC13783_LED_Cx_TC_C_MASK;
		value = max_current & MC13783_LED_Cx_TC_C_MASK;
		break;
	}

	mc13xxx_lock(led->master);

	ret = mc13xxx_reg_rmw(led->master, reg, mask << shift,
						value << shift);

	mc13xxx_unlock(led->master);
	return ret;
}

static int mc13783_leds_prepare(struct platform_device *pdev)
{
	struct mc13xxx_leds_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct mc13xxx *dev = dev_get_drvdata(pdev->dev.parent);
	int ret = 0;
	int reg = 0;

	mc13xxx_lock(dev);

	if (pdata->flags & MC13783_LED_TC1HALF)
		reg |= MC13783_LED_C1_TC1HALF_BIT;

	if (pdata->flags & MC13783_LED_SLEWLIMTC)
		reg |= MC13783_LED_Cx_SLEWLIM_BIT;

	ret = mc13xxx_reg_write(dev, MC13783_REG_LED_CONTROL_1, reg);
	if (ret)
		goto out;

	reg = (pdata->bl_period & MC13783_LED_Cx_PERIOD_MASK) <<
							MC13783_LED_Cx_PERIOD;

	if (pdata->flags & MC13783_LED_SLEWLIMBL)
		reg |= MC13783_LED_Cx_SLEWLIM_BIT;

	ret = mc13xxx_reg_write(dev, MC13783_REG_LED_CONTROL_2, reg);
	if (ret)
		goto out;

	reg = (pdata->tc1_period & MC13783_LED_Cx_PERIOD_MASK) <<
							MC13783_LED_Cx_PERIOD;

	if (pdata->flags & MC13783_LED_TRIODE_TC1)
		reg |= MC13783_LED_Cx_TRIODE_TC_BIT;

	ret = mc13xxx_reg_write(dev, MC13783_REG_LED_CONTROL_3, reg);
	if (ret)
		goto out;

	reg = (pdata->tc2_period & MC13783_LED_Cx_PERIOD_MASK) <<
							MC13783_LED_Cx_PERIOD;

	if (pdata->flags & MC13783_LED_TRIODE_TC2)
		reg |= MC13783_LED_Cx_TRIODE_TC_BIT;

	ret = mc13xxx_reg_write(dev, MC13783_REG_LED_CONTROL_4, reg);
	if (ret)
		goto out;

	reg = (pdata->tc3_period & MC13783_LED_Cx_PERIOD_MASK) <<
							MC13783_LED_Cx_PERIOD;

	if (pdata->flags & MC13783_LED_TRIODE_TC3)
		reg |= MC13783_LED_Cx_TRIODE_TC_BIT;

	ret = mc13xxx_reg_write(dev, MC13783_REG_LED_CONTROL_5, reg);
	if (ret)
		goto out;

	reg = MC13783_LED_C0_ENABLE_BIT;
	if (pdata->flags & MC13783_LED_TRIODE_MD)
		reg |= MC13783_LED_C0_TRIODE_MD_BIT;
	if (pdata->flags & MC13783_LED_TRIODE_AD)
		reg |= MC13783_LED_C0_TRIODE_AD_BIT;
	if (pdata->flags & MC13783_LED_TRIODE_KP)
		reg |= MC13783_LED_C0_TRIODE_KP_BIT;
	if (pdata->flags & MC13783_LED_BOOST_EN)
		reg |= MC13783_LED_C0_BOOST_BIT;

	reg |= (pdata->abmode & MC13783_LED_C0_ABMODE_MASK) <<
							MC13783_LED_C0_ABMODE;
	reg |= (pdata->abref & MC13783_LED_C0_ABREF_MASK) <<
							MC13783_LED_C0_ABREF;

	ret = mc13xxx_reg_write(dev, MC13783_REG_LED_CONTROL_0, reg);

out:
	mc13xxx_unlock(dev);
	return ret;
}

static int mc13783_led_probe(struct platform_device *pdev)
{
	struct mc13xxx_leds_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct mc13xxx_led_platform_data *led_cur;
	struct mc13783_led *led, *led_dat;
	int ret, i;
	int init_led = 0;

	if (pdata == NULL) {
		dev_err(&pdev->dev, "missing platform data\n");
		return -ENODEV;
	}

	if (pdata->num_leds < 1 || pdata->num_leds > (MC13783_LED_MAX + 1)) {
		dev_err(&pdev->dev, "Invalid led count %d\n", pdata->num_leds);
		return -EINVAL;
	}

	led = devm_kzalloc(&pdev->dev, pdata->num_leds * sizeof(*led),
				GFP_KERNEL);
	if (led == NULL) {
		dev_err(&pdev->dev, "failed to alloc memory\n");
		return -ENOMEM;
	}

	ret = mc13783_leds_prepare(pdev);
	if (ret) {
		dev_err(&pdev->dev, "unable to init led driver\n");
		return ret;
	}

	for (i = 0; i < pdata->num_leds; i++) {
		led_dat = &led[i];
		led_cur = &pdata->led[i];

		if (led_cur->id > MC13783_LED_MAX || led_cur->id < 0) {
			dev_err(&pdev->dev, "invalid id %d\n", led_cur->id);
			ret = -EINVAL;
			goto err_register;
		}

		if (init_led & (1 << led_cur->id)) {
			dev_err(&pdev->dev, "led %d already initialized\n",
					led_cur->id);
			ret = -EINVAL;
			goto err_register;
		}

		init_led |= 1 << led_cur->id;
		led_dat->cdev.name = led_cur->name;
		led_dat->cdev.default_trigger = led_cur->default_trigger;
		led_dat->cdev.brightness_set = mc13783_led_set;
		led_dat->cdev.brightness = LED_OFF;
		led_dat->id = led_cur->id;
		led_dat->master = dev_get_drvdata(pdev->dev.parent);

		INIT_WORK(&led_dat->work, mc13783_led_work);

		ret = led_classdev_register(pdev->dev.parent, &led_dat->cdev);
		if (ret) {
			dev_err(&pdev->dev, "failed to register led %d\n",
					led_dat->id);
			goto err_register;
		}

		ret = mc13783_led_setup(led_dat, led_cur->max_current);
		if (ret) {
			dev_err(&pdev->dev, "unable to init led %d\n",
					led_dat->id);
			i++;
			goto err_register;
		}
	}

	platform_set_drvdata(pdev, led);
	return 0;

err_register:
	for (i = i - 1; i >= 0; i--) {
		led_classdev_unregister(&led[i].cdev);
		cancel_work_sync(&led[i].work);
	}

	return ret;
}

static int mc13783_led_remove(struct platform_device *pdev)
{
	struct mc13xxx_leds_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct mc13783_led *led = platform_get_drvdata(pdev);
	struct mc13xxx *dev = dev_get_drvdata(pdev->dev.parent);
	int i;

	for (i = 0; i < pdata->num_leds; i++) {
		led_classdev_unregister(&led[i].cdev);
		cancel_work_sync(&led[i].work);
	}

	mc13xxx_lock(dev);

	mc13xxx_reg_write(dev, MC13783_REG_LED_CONTROL_0, 0);
	mc13xxx_reg_write(dev, MC13783_REG_LED_CONTROL_1, 0);
	mc13xxx_reg_write(dev, MC13783_REG_LED_CONTROL_2, 0);
	mc13xxx_reg_write(dev, MC13783_REG_LED_CONTROL_3, 0);
	mc13xxx_reg_write(dev, MC13783_REG_LED_CONTROL_4, 0);
	mc13xxx_reg_write(dev, MC13783_REG_LED_CONTROL_5, 0);

	mc13xxx_unlock(dev);

	return 0;
}

static struct platform_driver mc13783_led_driver = {
	.driver	= {
		.name	= "mc13783-led",
		.owner	= THIS_MODULE,
	},
	.probe		= mc13783_led_probe,
	.remove		= mc13783_led_remove,
};

module_platform_driver(mc13783_led_driver);

MODULE_DESCRIPTION("LEDs driver for Freescale MC13783 PMIC");
MODULE_AUTHOR("Philippe Retornaz <philippe.retornaz@epfl.ch>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mc13783-led");
