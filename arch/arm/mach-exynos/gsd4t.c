/*
 *  gsd4t.c (SiRFstarIV)
 *  GPS driver for SiRFstar based chip.
 *
 *  Copyright (C) 2011 Samsung Electronics
 *  Minho Ban <mhban@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/rfkill.h>
#include <linux/regulator/machine.h>
#include <asm/mach-types.h>
#include <mach/gpio.h>
#include <plat/gpio-cfg.h>

#include <mach/gsd4t.h>

struct gsd4t_data {
	struct gsd4t_platform_data	*pdata;
	struct rfkill			*rfk;
	bool				in_use;
	/* No need below if constraints is always_on */
	struct regulator		*vdd_18;
	struct regulator		*rtc_xi;
};

static int gsd4t_set_block(void *data, bool blocked)
{
	struct gsd4t_data *bd = data;
	struct gsd4t_platform_data *pdata = bd->pdata;

	if (!blocked) {
		if (bd->in_use)
			return 0;

		pr_info("gsd4t on\n");

		regulator_enable(bd->vdd_18);
		regulator_enable(bd->rtc_xi);

		/*
		 * CSR(SiRF) recommends,
		 *
		 *  _|^^^|_100ms_|^^^^^^^^^^^^^^^ nrst
		 *  _____________,___200ms___|^^^ onoff
		 *
		 */

		gpio_set_value(pdata->nrst, 1);
		gpio_set_value(pdata->onoff, 0);
		/*
		 * But real boot sequence should be handled by user layer
		 * (including download firmware) so we don't need control pins
		 * here actually.
		 *
		msleep(50);
		gpio_set_value(pdata->nrst, 0);
		msleep(100);
		gpio_set_value(pdata->nrst, 1);
		msleep(200);
		gpio_set_value(pdata->onoff, 1);
		 */

		bd->in_use = true;
	} else {
		if (!bd->in_use)
			return 0;

		pr_info("gsd4t off\n");

		gpio_set_value(pdata->nrst, 0);

		regulator_disable(bd->vdd_18);
		regulator_disable(bd->rtc_xi);

		bd->in_use = false;
	}
	return 0;
}

static const struct rfkill_ops gsd4t_rfkill_ops = {
	.set_block = gsd4t_set_block,
};

static int __devinit gsd4t_probe(struct platform_device *dev)
{
	struct gsd4t_platform_data *pdata;
	struct gsd4t_data *bd;
	unsigned int gpio;
	int ret = 0;

	pdata = dev->dev.platform_data;
	if (!pdata) {
		dev_err(&dev->dev, "No platform data.\n");
		return -EINVAL;
	}

	bd = kzalloc(sizeof(struct gsd4t_data), GFP_KERNEL);
	if (!bd)
		return -ENOMEM;

	bd->pdata = pdata;

	if (gpio_is_valid(pdata->nrst)) {
		gpio = pdata->nrst;
		/* GPS_nRST is high */
		gpio_request(gpio, "GPS_nRST");
		s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
		gpio_direction_output(gpio, 0);

		gpio_export(gpio, 1);
		gpio_export_link(&dev->dev, "reset", gpio);
	} else {
		dev_err(&dev->dev, "Invalid nRST pin\n");
		ret = -EINVAL;
		goto err_invalid_pin1;
	}


	if (gpio_is_valid(pdata->onoff)) {
		gpio = pdata->onoff;
		/* GPS_EN is low */
		gpio_request(gpio, "GPS_EN");
		s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
		gpio_direction_output(gpio, 0);

		gpio_export(gpio, 1);
		gpio_export_link(&dev->dev, "onoff", gpio);
	} else {
		dev_err(&dev->dev, "Invalid GPS_EN pin\n");
		ret = -EINVAL;
		goto err_invalid_pin2;
	}

	/* Optional aiding pin */
	if (gpio_is_valid(pdata->tsync)) {
		gpio = pdata->tsync;
		/* AP_AGPS_TSYNC is low */
		gpio_request(gpio, "AP_AGPS_TSYNC");
		s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
		gpio_direction_output(gpio, 0);

		gpio_export(gpio, 1);
		gpio_export_link(&dev->dev, "tsync", gpio);
	}

	/* input UART pin need to set pull-up to prevent floating */
	if (gpio_is_valid(pdata->uart_rxd))
		s3c_gpio_setpull(pdata->uart_rxd, S3C_GPIO_PULL_UP);

	/*
	 * Get regulators.
	 * If always_on power, can remove below.
	 */
	bd->rtc_xi = regulator_get(&dev->dev, "gps_clk");
	if (IS_ERR_OR_NULL(bd->rtc_xi)) {
		dev_err(&dev->dev, "gps_clk regulator_get error\n");
		ret = -EINVAL;
		goto err_regulator_1;
	}

	bd->vdd_18 = regulator_get(&dev->dev, "v_gps_1.8v");
	if (IS_ERR_OR_NULL(bd->vdd_18)) {
		dev_err(&dev->dev, "vdd_18 regulator_get error\n");
		ret = -EINVAL;
		goto err_regulator_2;
	}

	/*
	 * Actually, we don't need rfkill becasue most of power sequences to be
	 * done by user level. Just leave this to know user trggers onoff so we
	 * can set pins PDN mode at suspend/resume.
	 */
	bd->rfk = rfkill_alloc("gsd4t", &dev->dev, RFKILL_TYPE_GPS,
			&gsd4t_rfkill_ops, bd);
	if (!bd->rfk) {
		ret = -ENOMEM;
		goto err_rfk_alloc;
	}

	/*
	 * rfkill.h
	 * block true	: off
	 * block false	: on
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
err_rfk_alloc:
	regulator_put(bd->vdd_18);
err_regulator_2:
	regulator_put(bd->rtc_xi);
err_regulator_1:
	gpio_unexport(pdata->tsync);
	gpio_unexport(pdata->onoff);
err_invalid_pin2:
	gpio_unexport(pdata->nrst);
err_invalid_pin1:
	kfree(bd);
	return ret;
}

static int __devexit gsd4t_remove(struct platform_device *dev)
{
	struct gsd4t_data *bd = platform_get_drvdata(dev);
	struct gsd4t_platform_data *pdata = bd->pdata;

	if (bd->in_use)
		rfkill_init_sw_state(bd->rfk, true);

	rfkill_unregister(bd->rfk);
	rfkill_destroy(bd->rfk);
	gpio_unexport(pdata->onoff);
	gpio_unexport(pdata->nrst);
	gpio_unexport(pdata->tsync);
	gpio_free(pdata->onoff);
	gpio_free(pdata->nrst);

	regulator_put(bd->vdd_18);
	regulator_put(bd->rtc_xi);

	kfree(bd);
	platform_set_drvdata(dev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int gsd4t_suspend(struct platform_device *dev, pm_message_t stata)
{
	struct gsd4t_data *bd = platform_get_drvdata(dev);
	struct gsd4t_platform_data *pdata = bd->pdata;

	if (bd->in_use)
		s5p_gpio_set_pd_cfg(pdata->nrst, S5P_GPIO_PD_OUTPUT1);
	else
		s5p_gpio_set_pd_cfg(pdata->nrst, S5P_GPIO_PD_OUTPUT0);

	return 0;
}

static int gsd4t_resume(struct platform_device *dev)
{
	return 0;
}
#else
#define gsd4t_suspend		NULL
#define gsd4t_resume		NULL
#endif

static struct platform_driver gsd4t_driver = {
	.probe		= gsd4t_probe,
	.remove		= __devexit_p(gsd4t_remove),
	.suspend	= gsd4t_suspend,
	.resume		= gsd4t_resume,
	.driver	= {
		.name	= "gsd4t",
	},
};

static int __init gsd4t_init(void)
{
	return platform_driver_register(&gsd4t_driver);
}

static void __exit gsd4t_exit(void)
{
	platform_driver_unregister(&gsd4t_driver);
}

module_init(gsd4t_init);
module_exit(gsd4t_exit);

MODULE_AUTHOR("Minho Ban <mhban@samsung.com>");
MODULE_DESCRIPTION("GSD4T GPS driver");
MODULE_LICENSE("GPL");
