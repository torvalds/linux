/*
 *  bcm4752.c: Machine specific driver for GPS module
 *
 *  Copyright (c) 2011 Samsung Electronics
 *  Minho Ban <mhban@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/rfkill.h>
#include <linux/regulator/machine.h>

#include <mach/gpio.h>
#include <mach/bcm4752.h>

#include <plat/gpio-cfg.h>

struct bcm4752_data {
	struct bcm4752_platform_data	*pdata;
	struct rfkill			*rfk;
	bool				in_use;
	bool				regulator_state;
	struct regulator		*regulator;
};

static void bcm4752_enable(void *data)
{
	struct bcm4752_data *bd = data;
	struct bcm4752_platform_data *pdata = bd->pdata;

	if (bd->regulator && !bd->regulator_state) {
		regulator_enable(bd->regulator);
		bd->regulator_state = true;
	}

	gpio_set_value(pdata->regpu, 1);

	if (gpio_is_valid(pdata->gps_cntl))
		gpio_set_value(pdata->gps_cntl, 1);

	bd->in_use = true;
}

static void bcm4752_disable(void *data)
{
	struct bcm4752_data *bd = data;
	struct bcm4752_platform_data *pdata = bd->pdata;

	gpio_set_value(pdata->regpu, 0);

	if (gpio_is_valid(pdata->gps_cntl))
		gpio_set_value(pdata->gps_cntl, 0);

	if (bd->regulator && bd->regulator_state) {
		regulator_disable(bd->regulator);
		bd->regulator_state = false;
	}

	bd->in_use = false;
}

static int bcm4752_set_block(void *data, bool blocked)
{
	if (!blocked) {
		printk(KERN_INFO "%s : Enable GPS chip\n", __func__);
		bcm4752_enable(data);
	} else {
		printk(KERN_INFO "%s : Disable GPS chip\n", __func__);
		bcm4752_disable(data);
	}
	return 0;
}

static const struct rfkill_ops bcm4752_rfkill_ops = {
	.set_block = bcm4752_set_block,
};

static int __devinit bcm4752_probe(struct platform_device *dev)
{
	struct bcm4752_platform_data *pdata;
	struct bcm4752_data *bd;
	int gpio;
	int ret = 0;

	pdata = dev->dev.platform_data;
	if (!pdata) {
		dev_err(&dev->dev, "No plat data.\n");
		return -EINVAL;
	}

	if (!pdata->reg32khz) {
		dev_err(&dev->dev, "No 32KHz clock id.\n");
		return -EINVAL;
	}

	bd = kzalloc(sizeof(struct bcm4752_data), GFP_KERNEL);
	if (!bd)
		return -ENOMEM;

	bd->pdata = pdata;

	if (gpio_is_valid(pdata->regpu)) {
		gpio = pdata->regpu;

		/* GPS_EN is low */
		gpio_request(gpio, "GPS_EN");
		gpio_direction_output(gpio, 0);
	}


	/* GPS_UART_RXD */
	if (gpio_is_valid(pdata->uart_rxd))
		s3c_gpio_setpull(pdata->uart_rxd, S3C_GPIO_PULL_UP);

	if (gpio_is_valid(pdata->gps_cntl)) {
		gpio = pdata->gps_cntl;
		gpio_request(gpio, "GPS_CNTL");
		gpio_direction_output(gpio, 0);
	}

	/* Register bcm4752 to RFKILL class */
	bd->rfk = rfkill_alloc("bcm4752", &dev->dev, RFKILL_TYPE_GPS,
			&bcm4752_rfkill_ops, bd);
	if (!bd->rfk) {
		ret = -ENOMEM;
		goto err_rfk_alloc;
	}

	bd->regulator = regulator_get(&dev->dev, pdata->reg32khz);
	if (IS_ERR_OR_NULL(bd->regulator)) {
		dev_err(&dev->dev, "regulator_get error (%ld)\n",
				PTR_ERR(bd->regulator));
		goto err_regulator;
	}

	bd->regulator_state = false;

	/*
	 * described by the comment in rfkill.h
	 * true : turn off
	 * false : turn on
	 */
	rfkill_init_sw_state(bd->rfk, true);
	bd->in_use = false;

	ret = rfkill_register(bd->rfk);
	if (ret)
		goto err_rfkill;

	platform_set_drvdata(dev, bd);

	dev_info(&dev->dev, "ready\n");

	return 0;

err_rfkill:
	rfkill_destroy(bd->rfk);
err_regulator:
err_rfk_alloc:
	kfree(bd);
	return ret;
}

static int __devexit bcm4752_remove(struct platform_device *dev)
{
	struct bcm4752_data *bd = platform_get_drvdata(dev);
	struct bcm4752_platform_data *pdata = bd->pdata;

	rfkill_unregister(bd->rfk);
	rfkill_destroy(bd->rfk);
	gpio_free(pdata->regpu);
	kfree(bd);
	platform_set_drvdata(dev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int bcm4752_suspend(struct platform_device *dev, pm_message_t stata)
{
	struct bcm4752_data *bd = platform_get_drvdata(dev);
	struct bcm4752_platform_data *pdata = bd->pdata;

	if (bd->in_use) {
		s5p_gpio_set_pd_cfg(pdata->regpu, S5P_GPIO_PD_OUTPUT1);
		if (gpio_is_valid(pdata->gps_cntl))
			s5p_gpio_set_pd_cfg(pdata->gps_cntl,
			S5P_GPIO_PD_OUTPUT1);
	} else {
		s5p_gpio_set_pd_cfg(pdata->regpu, S5P_GPIO_PD_OUTPUT0);
		if (gpio_is_valid(pdata->gps_cntl))
			s5p_gpio_set_pd_cfg(pdata->gps_cntl,
			S5P_GPIO_PD_OUTPUT0);
	}

	return 0;
}

static int bcm4752_resume(struct platform_device *dev)
{
	return 0;
}
#else
#define bcm4752_suspend	NULL
#define bcm4752_resume		NULL
#endif

static struct platform_driver bcm4752_driver = {
	.probe		= bcm4752_probe,
	.remove		= __devexit_p(bcm4752_remove),
	.suspend	= bcm4752_suspend,
	.resume		= bcm4752_resume,
	.driver	= {
		.name	= "bcm4752",
	},
};

static int __init bcm4752_init(void)
{
	return platform_driver_register(&bcm4752_driver);
}

static void __exit bcm4752_exit(void)
{
	platform_driver_unregister(&bcm4752_driver);
}

module_init(bcm4752_init);
module_exit(bcm4752_exit);

MODULE_AUTHOR("Minho Ban <mhban@samsung.com>");
MODULE_DESCRIPTION("BCM4752 GPS module driver");
MODULE_LICENSE("GPL");
