// SPDX-License-Identifier: GPL-2.0-only
/*
 * w1-gpio - GPIO w1 bus master driver
 *
 * Copyright (C) 2007 Ville Syrjala <syrjala@sci.fi>
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/types.h>

#include <linux/w1.h>

struct w1_gpio_ddata {
	struct gpio_desc *gpiod;
	struct gpio_desc *pullup_gpiod;
	unsigned int pullup_duration;
};

static u8 w1_gpio_set_pullup(void *data, int delay)
{
	struct w1_gpio_ddata *ddata = data;

	if (delay) {
		ddata->pullup_duration = delay;
	} else {
		if (ddata->pullup_duration) {
			/*
			 * This will OVERRIDE open drain emulation and force-pull
			 * the line high for some time.
			 */
			gpiod_set_raw_value(ddata->gpiod, 1);
			msleep(ddata->pullup_duration);
			/*
			 * This will simply set the line as input since we are doing
			 * open drain emulation in the GPIO library.
			 */
			gpiod_set_value(ddata->gpiod, 1);
		}
		ddata->pullup_duration = 0;
	}

	return 0;
}

static void w1_gpio_write_bit(void *data, u8 bit)
{
	struct w1_gpio_ddata *ddata = data;

	gpiod_set_value(ddata->gpiod, bit);
}

static u8 w1_gpio_read_bit(void *data)
{
	struct w1_gpio_ddata *ddata = data;

	return gpiod_get_value(ddata->gpiod) ? 1 : 0;
}

static int w1_gpio_probe(struct platform_device *pdev)
{
	struct w1_bus_master *master;
	struct w1_gpio_ddata *ddata;
	struct device *dev = &pdev->dev;
	/* Enforce open drain mode by default */
	enum gpiod_flags gflags = GPIOD_OUT_LOW_OPEN_DRAIN;
	int err;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	/*
	 * This parameter means that something else than the gpiolib has
	 * already set the line into open drain mode, so we should just
	 * driver it high/low like we are in full control of the line and
	 * open drain will happen transparently.
	 */
	if (device_property_present(dev, "linux,open-drain"))
		gflags = GPIOD_OUT_LOW;

	master = devm_kzalloc(dev, sizeof(*master), GFP_KERNEL);
	if (!master)
		return -ENOMEM;

	ddata->gpiod = devm_gpiod_get_index(dev, NULL, 0, gflags);
	if (IS_ERR(ddata->gpiod))
		return dev_err_probe(dev, PTR_ERR(ddata->gpiod), "gpio_request (pin) failed\n");

	ddata->pullup_gpiod =
		devm_gpiod_get_index_optional(dev, NULL, 1, GPIOD_OUT_LOW);
	if (IS_ERR(ddata->pullup_gpiod))
		return dev_err_probe(dev, PTR_ERR(ddata->pullup_gpiod),
				     "gpio_request (ext_pullup_enable_pin) failed\n");

	master->data = ddata;
	master->read_bit = w1_gpio_read_bit;
	gpiod_direction_output(ddata->gpiod, 1);
	master->write_bit = w1_gpio_write_bit;

	/*
	 * If we are using open drain emulation from the GPIO library,
	 * we need to use this pullup function that hammers the line
	 * high using a raw accessor to provide pull-up for the w1
	 * line.
	 */
	if (gflags == GPIOD_OUT_LOW_OPEN_DRAIN)
		master->set_pullup = w1_gpio_set_pullup;

	err = w1_add_master_device(master);
	if (err)
		return dev_err_probe(dev, err, "w1_add_master device failed\n");

	gpiod_set_value(ddata->pullup_gpiod, 1);

	platform_set_drvdata(pdev, master);

	return 0;
}

static void w1_gpio_remove(struct platform_device *pdev)
{
	struct w1_bus_master *master = platform_get_drvdata(pdev);
	struct w1_gpio_ddata *ddata = master->data;

	gpiod_set_value(ddata->pullup_gpiod, 0);

	w1_remove_master_device(master);
}

static const struct of_device_id w1_gpio_dt_ids[] = {
	{ .compatible = "w1-gpio" },
	{}
};
MODULE_DEVICE_TABLE(of, w1_gpio_dt_ids);

static struct platform_driver w1_gpio_driver = {
	.driver = {
		.name	= "w1-gpio",
		.of_match_table = w1_gpio_dt_ids,
	},
	.probe = w1_gpio_probe,
	.remove_new = w1_gpio_remove,
};

module_platform_driver(w1_gpio_driver);

MODULE_DESCRIPTION("GPIO w1 bus master driver");
MODULE_AUTHOR("Ville Syrjala <syrjala@sci.fi>");
MODULE_LICENSE("GPL");
