/*
 *  bcm47511.c: Machine specific driver for GPS module
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
#include <mach/bcm47511.h>

#include <plat/gpio-cfg.h>

struct bcm47511_data {
	struct bcm47511_platform_data	*pdata;
	struct rfkill			*rfk;
	bool				in_use;
	bool				regulator_state;
	struct regulator		*regulator;
};

static void bcm47511_enable(void *data)
{
	struct bcm47511_data *bd = data;
	struct bcm47511_platform_data *pdata = bd->pdata;

	if (bd->regulator && !bd->regulator_state) {
		regulator_enable(bd->regulator);
		bd->regulator_state = true;
	}

	gpio_set_value(pdata->regpu, 1);

	if (gpio_is_valid(pdata->nrst))
		gpio_set_value(pdata->nrst, 1);

	if (gpio_is_valid(pdata->gps_cntl))
		gpio_set_value(pdata->gps_cntl, 1);

	bd->in_use = true;
}

static void bcm47511_disable(void *data)
{
	struct bcm47511_data *bd = data;
	struct bcm47511_platform_data *pdata = bd->pdata;

	gpio_set_value(pdata->regpu, 0);

	if (gpio_is_valid(pdata->gps_cntl))
		gpio_set_value(pdata->gps_cntl, 0);

	if (bd->regulator && bd->regulator_state) {
		regulator_disable(bd->regulator);
		bd->regulator_state = false;
	}

	bd->in_use = false;
}

static int bcm47511_set_block(void *data, bool blocked)
{
	if (!blocked) {
		printk(KERN_INFO "%s : Enable GPS chip\n", __func__);
		bcm47511_enable(data);
	} else {
		printk(KERN_INFO "%s : Disable GPS chip\n", __func__);
		bcm47511_disable(data);
	}
	return 0;
}

static const struct rfkill_ops bcm47511_rfkill_ops = {
	.set_block = bcm47511_set_block,
};

static int __devinit bcm47511_probe(struct platform_device *dev)
{
	struct bcm47511_platform_data *pdata;
	struct bcm47511_data *bd;
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

	bd = kzalloc(sizeof(struct bcm47511_data), GFP_KERNEL);
	if (!bd)
		return -ENOMEM;

	bd->pdata = pdata;

	if (gpio_is_valid(pdata->regpu)) {
		gpio = pdata->regpu;

		/* GPS_EN is low */
		gpio_request(gpio, "GPS_EN");
		gpio_direction_output(gpio, 0);
	}

	if (gpio_is_valid(pdata->nrst)) {
		gpio = pdata->nrst;
		/* GPS_nRST is high */
		gpio_request(gpio, "GPS_nRST");
		gpio_direction_output(gpio, 1);
	}

	/* GPS_UART_RXD */
	if (gpio_is_valid(pdata->uart_rxd))
		s3c_gpio_setpull(pdata->uart_rxd, S3C_GPIO_PULL_UP);

	if (gpio_is_valid(pdata->gps_cntl)) {
		gpio = pdata->gps_cntl;
		gpio_request(gpio, "GPS_CNTL");
		gpio_direction_output(gpio, 0);
	}

	/* Register BCM47511 to RFKILL class */
	bd->rfk = rfkill_alloc("bcm47511", &dev->dev, RFKILL_TYPE_GPS,
			&bcm47511_rfkill_ops, bd);
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

static int __devexit bcm47511_remove(struct platform_device *dev)
{
	struct bcm47511_data *bd = platform_get_drvdata(dev);
	struct bcm47511_platform_data *pdata = bd->pdata;

	rfkill_unregister(bd->rfk);
	rfkill_destroy(bd->rfk);
	gpio_free(pdata->regpu);
	gpio_free(pdata->nrst);
	kfree(bd);
	platform_set_drvdata(dev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int bcm47511_suspend(struct platform_device *dev, pm_message_t stata)
{
	struct bcm47511_data *bd = platform_get_drvdata(dev);
	struct bcm47511_platform_data *pdata = bd->pdata;

	if (bd->in_use) {
		s5p_gpio_set_pd_cfg(pdata->regpu, S5P_GPIO_PD_OUTPUT1);
		s5p_gpio_set_pd_cfg(pdata->nrst, S5P_GPIO_PD_OUTPUT1);
		if (gpio_is_valid(pdata->gps_cntl))
			s5p_gpio_set_pd_cfg(pdata->gps_cntl, S5P_GPIO_PD_OUTPUT1);
	} else {
		s5p_gpio_set_pd_cfg(pdata->regpu, S5P_GPIO_PD_OUTPUT0);
		s5p_gpio_set_pd_cfg(pdata->nrst, S5P_GPIO_PD_OUTPUT0);
		if (gpio_is_valid(pdata->gps_cntl))
			s5p_gpio_set_pd_cfg(pdata->gps_cntl, S5P_GPIO_PD_OUTPUT0);
	}

	return 0;
}

static int bcm47511_resume(struct platform_device *dev)
{
	return 0;
}
#else
#define bcm47511_suspend	NULL
#define bcm47511_resume		NULL
#endif

static struct platform_driver bcm47511_driver = {
	.probe		= bcm47511_probe,
	.remove		= __devexit_p(bcm47511_remove),
	.suspend	= bcm47511_suspend,
	.resume		= bcm47511_resume,
	.driver	= {
		.name	= "bcm47511",
	},
};

static int __init bcm47511_init(void)
{
	return platform_driver_register(&bcm47511_driver);
}

static void __exit bcm47511_exit(void)
{
	platform_driver_unregister(&bcm47511_driver);
}

module_init(bcm47511_init);
module_exit(bcm47511_exit);

MODULE_AUTHOR("Minho Ban <mhban@samsung.com>");
MODULE_DESCRIPTION("BCM47511 GPS module driver");
MODULE_LICENSE("GPL");
