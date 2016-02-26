/*
 * w1-gpio - GPIO w1 bus master driver
 *
 * Copyright (C) 2007 Ville Syrjala <syrjala@sci.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/w1-gpio.h>
#include <linux/gpio.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/delay.h>

#include <linux/amlogic/aml_gpio_consumer.h>

#include "../w1.h"
#include "../w1_int.h"

#define MODULE_NAME		"amlw1"

static int w1_gpio_pin = 0;
static int w1_gpio_pullup = 0;

module_param(w1_gpio_pin,int,0644);
MODULE_PARM_DESC(w1_gpio_pin,"\n odroid gpio number for 1-wire\n");

module_param(w1_gpio_pullup,int,0644);
MODULE_PARM_DESC(w1_gpio_pullup,"\n odroid gpio number for 1-wire ext_pullup\n");

static u8 w1_gpio_set_pullup(void *data, int delay)
{
	struct w1_gpio_platform_data *pdata = data;

	if (delay) {
		pdata->pullup_duration = delay;
	} else {
		if (pdata->pullup_duration) {
			amlogic_gpio_direction_output(pdata->pin, 1, MODULE_NAME);

			msleep(pdata->pullup_duration);

			amlogic_gpio_direction_input(pdata->pin, MODULE_NAME);
		}
		pdata->pullup_duration = 0;
	}

	return 0;
}

static void w1_gpio_write_bit_dir(void *data, u8 bit)
{
	struct w1_gpio_platform_data *pdata = data;

	if (bit)
		amlogic_gpio_direction_input(pdata->pin, MODULE_NAME);
	else
		amlogic_gpio_direction_output(pdata->pin, 0, MODULE_NAME);
}

static void w1_gpio_write_bit_val(void *data, u8 bit)
{
	struct w1_gpio_platform_data *pdata = data;

	amlogic_set_value(pdata->pin, bit, MODULE_NAME);
}

static u8 w1_gpio_read_bit(void *data)
{
	struct w1_gpio_platform_data *pdata = data;

	return amlogic_get_value(pdata->pin, MODULE_NAME) ? 1 : 0;
}

#if defined(CONFIG_OF)
static struct of_device_id w1_gpio_dt_ids[] = {
	{ .compatible = "w1-gpio" },
	{}
};
MODULE_DEVICE_TABLE(of, w1_gpio_dt_ids);
#endif

static int w1_gpio_probe_dt(struct platform_device *pdev)
{
	struct w1_gpio_platform_data *pdata = pdev->dev.platform_data;
	struct device_node *np = pdev->dev.of_node;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	if (of_get_property(np, "linux,open-drain", NULL))
		pdata->is_open_drain = 1;

    if(w1_gpio_pin) 
        pdata->pin = w1_gpio_pin;
	else 
	    pdata->pin = of_get_gpio(np, 0);

	pdata->ext_pullup_enable_pin = w1_gpio_pullup;
	pdev->dev.platform_data = pdata;

	return 0;
}

static int w1_gpio_probe(struct platform_device *pdev)
{
	struct w1_bus_master *master;
	struct w1_gpio_platform_data *pdata;
	int err;

	if (of_have_populated_dt()) {
		err = w1_gpio_probe_dt(pdev);
		if (err < 0) {
			dev_err(&pdev->dev, "Failed to parse DT\n");
			return err;
		}
	}

	pdata = pdev->dev.platform_data;

	if (!pdata) {
		dev_err(&pdev->dev, "No configuration data\n");
		return -ENXIO;
	}

	master = kzalloc(sizeof(struct w1_bus_master), GFP_KERNEL);
	if (!master) {
		dev_err(&pdev->dev, "Out of memory\n");
		return -ENOMEM;
	}

	err = amlogic_gpio_request(pdata->pin, MODULE_NAME);
	if (err) {
		dev_err(&pdev->dev, "gpio_request (pin) failed\n");
		goto free_master;
	}

	if(pdata->ext_pullup_enable_pin) {
		err = amlogic_gpio_request_one(pdata->ext_pullup_enable_pin,
				       GPIOF_INIT_LOW, "w1 pullup");
		if (err < 0) {
			dev_err(&pdev->dev, "gpio_request_one "
					"(ext_pullup_enable_pin) failed\n");
			goto free_gpio;
		}
	}

	master->data = pdata;
	master->read_bit = w1_gpio_read_bit;

	if (pdata->is_open_drain) {
		amlogic_gpio_direction_output(pdata->pin, 1, MODULE_NAME);
		master->write_bit = w1_gpio_write_bit_val;
	} else {
		amlogic_gpio_direction_input(pdata->pin, MODULE_NAME);
		master->write_bit = w1_gpio_write_bit_dir;
		master->set_pullup = w1_gpio_set_pullup;
	}

	err = w1_add_master_device(master);
	if (err) {
		dev_err(&pdev->dev, "w1_add_master device failed\n");
		goto free_gpio_ext_pu;
	}

	if (pdata->enable_external_pullup)
		pdata->enable_external_pullup(1);

	if(pdata->ext_pullup_enable_pin)
		amlogic_set_value(pdata->ext_pullup_enable_pin, 1, MODULE_NAME);

	platform_set_drvdata(pdev, master);

	return 0;

 free_gpio_ext_pu:
	if(pdata->ext_pullup_enable_pin)
		amlogic_gpio_free(pdata->ext_pullup_enable_pin, MODULE_NAME);
 free_gpio:
    amlogic_gpio_free(pdata->pin, MODULE_NAME);
 free_master:
	kfree(master);

	return err;
}

static int w1_gpio_remove(struct platform_device *pdev)
{
	struct w1_bus_master *master = platform_get_drvdata(pdev);
	struct w1_gpio_platform_data *pdata = pdev->dev.platform_data;
    
	if (pdata->enable_external_pullup)
		pdata->enable_external_pullup(0);

	if(pdata->ext_pullup_enable_pin)
		amlogic_set_value(pdata->ext_pullup_enable_pin, 0, MODULE_NAME);

	w1_remove_master_device(master);
    amlogic_gpio_free(pdata->pin, MODULE_NAME);
	kfree(master);

	return 0;
}

#ifdef CONFIG_PM

static int w1_gpio_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct w1_gpio_platform_data *pdata = pdev->dev.platform_data;

	if (pdata->enable_external_pullup)
		pdata->enable_external_pullup(0);

	return 0;
}

static int w1_gpio_resume(struct platform_device *pdev)
{
	struct w1_gpio_platform_data *pdata = pdev->dev.platform_data;

	if (pdata->enable_external_pullup)
		pdata->enable_external_pullup(1);

	return 0;
}

#else
#define w1_gpio_suspend	NULL
#define w1_gpio_resume	NULL
#endif

static struct platform_driver w1_gpio_driver = {
	.driver = {
		.name	= "w1-gpio",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(w1_gpio_dt_ids),
	},
	.probe = w1_gpio_probe,
	.remove	= w1_gpio_remove,
	.suspend = w1_gpio_suspend,
	.resume = w1_gpio_resume,
};

module_platform_driver(w1_gpio_driver);

MODULE_DESCRIPTION("GPIO w1 bus master driver for amlogic");
MODULE_AUTHOR("Ville Syrjala <syrjala@sci.fi>");
MODULE_LICENSE("GPL");
