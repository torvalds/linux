/*
 * Bluetooth built-in chip control
 *
 * Copyright (c) 2008 Dmitry Baryshkov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/rfkill.h>

#include <mach/tosa_bt.h>

static void tosa_bt_on(struct tosa_bt_data *data)
{
	gpio_set_value(data->gpio_reset, 0);
	gpio_set_value(data->gpio_pwr, 1);
	gpio_set_value(data->gpio_reset, 1);
	mdelay(20);
	gpio_set_value(data->gpio_reset, 0);
}

static void tosa_bt_off(struct tosa_bt_data *data)
{
	gpio_set_value(data->gpio_reset, 1);
	mdelay(10);
	gpio_set_value(data->gpio_pwr, 0);
	gpio_set_value(data->gpio_reset, 0);
}

static int tosa_bt_toggle_radio(void *data, enum rfkill_state state)
{
	pr_info("BT_RADIO going: %s\n",
			state == RFKILL_STATE_ON ? "on" : "off");

	if (state == RFKILL_STATE_ON) {
		pr_info("TOSA_BT: going ON\n");
		tosa_bt_on(data);
	} else {
		pr_info("TOSA_BT: going OFF\n");
		tosa_bt_off(data);
	}
	return 0;
}

static int tosa_bt_probe(struct platform_device *dev)
{
	int rc;
	struct rfkill *rfk;

	struct tosa_bt_data *data = dev->dev.platform_data;

	rc = gpio_request(data->gpio_reset, "Bluetooth reset");
	if (rc)
		goto err_reset;
	rc = gpio_direction_output(data->gpio_reset, 0);
	if (rc)
		goto err_reset_dir;
	rc = gpio_request(data->gpio_pwr, "Bluetooth power");
	if (rc)
		goto err_pwr;
	rc = gpio_direction_output(data->gpio_pwr, 0);
	if (rc)
		goto err_pwr_dir;

	rfk = rfkill_allocate(&dev->dev, RFKILL_TYPE_BLUETOOTH);
	if (!rfk) {
		rc = -ENOMEM;
		goto err_rfk_alloc;
	}

	rfk->name = "tosa-bt";
	rfk->toggle_radio = tosa_bt_toggle_radio;
	rfk->data = data;
#ifdef CONFIG_RFKILL_LEDS
	rfk->led_trigger.name = "tosa-bt";
#endif

	rc = rfkill_register(rfk);
	if (rc)
		goto err_rfkill;

	platform_set_drvdata(dev, rfk);

	return 0;

err_rfkill:
	if (rfk)
		rfkill_free(rfk);
	rfk = NULL;
err_rfk_alloc:
	tosa_bt_off(data);
err_pwr_dir:
	gpio_free(data->gpio_pwr);
err_pwr:
err_reset_dir:
	gpio_free(data->gpio_reset);
err_reset:
	return rc;
}

static int __devexit tosa_bt_remove(struct platform_device *dev)
{
	struct tosa_bt_data *data = dev->dev.platform_data;
	struct rfkill *rfk = platform_get_drvdata(dev);

	platform_set_drvdata(dev, NULL);

	if (rfk)
		rfkill_unregister(rfk);
	rfk = NULL;

	tosa_bt_off(data);

	gpio_free(data->gpio_pwr);
	gpio_free(data->gpio_reset);

	return 0;
}

static struct platform_driver tosa_bt_driver = {
	.probe = tosa_bt_probe,
	.remove = __devexit_p(tosa_bt_remove),

	.driver = {
		.name = "tosa-bt",
		.owner = THIS_MODULE,
	},
};


static int __init tosa_bt_init(void)
{
	return platform_driver_register(&tosa_bt_driver);
}

static void __exit tosa_bt_exit(void)
{
	platform_driver_unregister(&tosa_bt_driver);
}

module_init(tosa_bt_init);
module_exit(tosa_bt_exit);
