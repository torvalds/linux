/*
 * LEDs driver for Freescale MC13783/MC13892
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

#define MC13XXX_REG_LED_CONTROL(x)	(51 + (x))

struct mc13xxx_led_devtype {
	int	led_min;
	int	led_max;
	int	num_regs;
};

struct mc13xxx_led {
	struct led_classdev	cdev;
	struct work_struct	work;
	struct mc13xxx		*master;
	enum led_brightness	new_brightness;
	int			id;
};

struct mc13xxx_leds {
	struct mc13xxx_led_devtype	*devtype;
	int				num_leds;
	struct mc13xxx_led		led[0];
};

static void mc13xxx_led_work(struct work_struct *work)
{
	struct mc13xxx_led *led = container_of(work, struct mc13xxx_led, work);
	int reg, mask, value, bank, off, shift;

	switch (led->id) {
	case MC13783_LED_MD:
		reg = MC13XXX_REG_LED_CONTROL(2);
		shift = 9;
		mask = 0x0f;
		value = led->new_brightness >> 4;
		break;
	case MC13783_LED_AD:
		reg = MC13XXX_REG_LED_CONTROL(2);
		shift = 13;
		mask = 0x0f;
		value = led->new_brightness >> 4;
		break;
	case MC13783_LED_KP:
		reg = MC13XXX_REG_LED_CONTROL(2);
		shift = 17;
		mask = 0x0f;
		value = led->new_brightness >> 4;
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
		bank = off / 3;
		reg = MC13XXX_REG_LED_CONTROL(3) + bank;
		shift = (off - bank * 3) * 5 + 6;
		value = led->new_brightness >> 3;
		mask = 0x1f;
		break;
	case MC13892_LED_MD:
		reg = MC13XXX_REG_LED_CONTROL(0);
		shift = 3;
		mask = 0x3f;
		value = led->new_brightness >> 2;
		break;
	case MC13892_LED_AD:
		reg = MC13XXX_REG_LED_CONTROL(0);
		shift = 15;
		mask = 0x3f;
		value = led->new_brightness >> 2;
		break;
	case MC13892_LED_KP:
		reg = MC13XXX_REG_LED_CONTROL(1);
		shift = 3;
		mask = 0x3f;
		value = led->new_brightness >> 2;
		break;
	case MC13892_LED_R:
	case MC13892_LED_G:
	case MC13892_LED_B:
		off = led->id - MC13892_LED_R;
		bank = off / 2;
		reg = MC13XXX_REG_LED_CONTROL(2) + bank;
		shift = (off - bank * 2) * 12 + 3;
		value = led->new_brightness >> 2;
		mask = 0x3f;
		break;
	default:
		BUG();
	}

	mc13xxx_lock(led->master);
	mc13xxx_reg_rmw(led->master, reg, mask << shift, value << shift);
	mc13xxx_unlock(led->master);
}

static void mc13xxx_led_set(struct led_classdev *led_cdev,
			    enum led_brightness value)
{
	struct mc13xxx_led *led =
		container_of(led_cdev, struct mc13xxx_led, cdev);

	led->new_brightness = value;
	schedule_work(&led->work);
}

static int __init mc13xxx_led_setup(struct mc13xxx_led *led, int max_current)
{
	int shift, mask, reg, ret, bank;

	switch (led->id) {
	case MC13783_LED_MD:
		reg = MC13XXX_REG_LED_CONTROL(2);
		shift = 0;
		mask = 0x07;
		break;
	case MC13783_LED_AD:
		reg = MC13XXX_REG_LED_CONTROL(2);
		shift = 3;
		mask = 0x07;
		break;
	case MC13783_LED_KP:
		reg = MC13XXX_REG_LED_CONTROL(2);
		shift = 6;
		mask = 0x07;
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
		bank = (led->id - MC13783_LED_R1) / 3;
		reg = MC13XXX_REG_LED_CONTROL(3) + bank;
		shift = ((led->id - MC13783_LED_R1) - bank * 3) * 2;
		mask = 0x03;
		break;
	case MC13892_LED_MD:
		reg = MC13XXX_REG_LED_CONTROL(0);
		shift = 9;
		mask = 0x07;
		break;
	case MC13892_LED_AD:
		reg = MC13XXX_REG_LED_CONTROL(0);
		shift = 21;
		mask = 0x07;
		break;
	case MC13892_LED_KP:
		reg = MC13XXX_REG_LED_CONTROL(1);
		shift = 9;
		mask = 0x07;
		break;
	case MC13892_LED_R:
	case MC13892_LED_G:
	case MC13892_LED_B:
		bank = (led->id - MC13892_LED_R) / 2;
		reg = MC13XXX_REG_LED_CONTROL(2) + bank;
		shift = ((led->id - MC13892_LED_R) - bank * 2) * 12 + 9;
		mask = 0x07;
		break;
	default:
		BUG();
	}

	mc13xxx_lock(led->master);
	ret = mc13xxx_reg_rmw(led->master, reg, mask << shift,
			      max_current << shift);
	mc13xxx_unlock(led->master);

	return ret;
}

static int __init mc13xxx_led_probe(struct platform_device *pdev)
{
	struct mc13xxx_leds_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct mc13xxx *mcdev = dev_get_drvdata(pdev->dev.parent);
	struct mc13xxx_led_devtype *devtype =
		(struct mc13xxx_led_devtype *)pdev->id_entry->driver_data;
	struct mc13xxx_leds *leds;
	int i, id, num_leds, ret = -ENODATA;
	u32 reg, init_led = 0;

	if (!pdata) {
		dev_err(&pdev->dev, "Missing platform data\n");
		return -ENODEV;
	}

	num_leds = pdata->num_leds;

	if ((num_leds < 1) ||
	    (num_leds > (devtype->led_max - devtype->led_min + 1))) {
		dev_err(&pdev->dev, "Invalid LED count %d\n", num_leds);
		return -EINVAL;
	}

	leds = devm_kzalloc(&pdev->dev, num_leds * sizeof(struct mc13xxx_led) +
			    sizeof(struct mc13xxx_leds), GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	leds->devtype = devtype;
	leds->num_leds = num_leds;
	platform_set_drvdata(pdev, leds);

	mc13xxx_lock(mcdev);
	for (i = 0; i < devtype->num_regs; i++) {
		reg = pdata->led_control[i];
		WARN_ON(reg >= (1 << 24));
		ret = mc13xxx_reg_write(mcdev, MC13XXX_REG_LED_CONTROL(i), reg);
		if (ret)
			break;
	}
	mc13xxx_unlock(mcdev);

	if (ret) {
		dev_err(&pdev->dev, "Unable to init LED driver\n");
		return ret;
	}

	for (i = 0; i < num_leds; i++) {
		const char *name, *trig;
		char max_current;

		ret = -EINVAL;

		id = pdata->led[i].id;
		name = pdata->led[i].name;
		trig = pdata->led[i].default_trigger;
		max_current = pdata->led[i].max_current;

		if ((id > devtype->led_max) || (id < devtype->led_min)) {
			dev_err(&pdev->dev, "Invalid ID %i\n", id);
			break;
		}

		if (init_led & (1 << id)) {
			dev_warn(&pdev->dev,
				 "LED %i already initialized\n", id);
			break;
		}

		init_led |= 1 << id;
		leds->led[i].id = id;
		leds->led[i].master = mcdev;
		leds->led[i].cdev.name = name;
		leds->led[i].cdev.default_trigger = trig;
		leds->led[i].cdev.brightness_set = mc13xxx_led_set;
		leds->led[i].cdev.brightness = LED_OFF;

		INIT_WORK(&leds->led[i].work, mc13xxx_led_work);

		ret = mc13xxx_led_setup(&leds->led[i], max_current);
		if (ret) {
			dev_err(&pdev->dev, "Unable to setup LED %i\n", id);
			break;
		}
		ret = led_classdev_register(pdev->dev.parent,
					    &leds->led[i].cdev);
		if (ret) {
			dev_err(&pdev->dev, "Failed to register LED %i\n", id);
			break;
		}
	}

	if (ret)
		while (--i >= 0) {
			led_classdev_unregister(&leds->led[i].cdev);
			cancel_work_sync(&leds->led[i].work);
		}

	return ret;
}

static int mc13xxx_led_remove(struct platform_device *pdev)
{
	struct mc13xxx *mcdev = dev_get_drvdata(pdev->dev.parent);
	struct mc13xxx_leds *leds = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < leds->num_leds; i++) {
		led_classdev_unregister(&leds->led[i].cdev);
		cancel_work_sync(&leds->led[i].work);
	}

	mc13xxx_lock(mcdev);
	for (i = 0; i < leds->devtype->num_regs; i++)
		mc13xxx_reg_write(mcdev, MC13XXX_REG_LED_CONTROL(i), 0);
	mc13xxx_unlock(mcdev);

	return 0;
}

static const struct mc13xxx_led_devtype mc13783_led_devtype = {
	.led_min	= MC13783_LED_MD,
	.led_max	= MC13783_LED_B3,
	.num_regs	= 6,
};

static const struct mc13xxx_led_devtype mc13892_led_devtype = {
	.led_min	= MC13892_LED_MD,
	.led_max	= MC13892_LED_B,
	.num_regs	= 4,
};

static const struct platform_device_id mc13xxx_led_id_table[] = {
	{ "mc13783-led", (kernel_ulong_t)&mc13783_led_devtype, },
	{ "mc13892-led", (kernel_ulong_t)&mc13892_led_devtype, },
	{ }
};
MODULE_DEVICE_TABLE(platform, mc13xxx_led_id_table);

static struct platform_driver mc13xxx_led_driver = {
	.driver	= {
		.name	= "mc13xxx-led",
		.owner	= THIS_MODULE,
	},
	.remove		= mc13xxx_led_remove,
	.id_table	= mc13xxx_led_id_table,
};
module_platform_driver_probe(mc13xxx_led_driver, mc13xxx_led_probe);

MODULE_DESCRIPTION("LEDs driver for Freescale MC13XXX PMIC");
MODULE_AUTHOR("Philippe Retornaz <philippe.retornaz@epfl.ch>");
MODULE_LICENSE("GPL");
