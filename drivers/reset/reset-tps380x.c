// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * TI TPS380x Supply Voltage Supervisor and Reset Controller Driver
 *
 * Copyright (C) 2022 Pengutronix, Marco Felsch <kernel@pengutronix.de>
 *
 * Based on Simple Reset Controller Driver
 *
 * Copyright (C) 2017 Pengutronix, Philipp Zabel <kernel@pengutronix.de>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/reset-controller.h>

struct tps380x_reset {
	struct reset_controller_dev	rcdev;
	struct gpio_desc		*reset_gpio;
	unsigned int			reset_ms;
};

struct tps380x_reset_devdata {
	unsigned int min_reset_ms;
	unsigned int typ_reset_ms;
	unsigned int max_reset_ms;
};

static inline
struct tps380x_reset *to_tps380x_reset(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct tps380x_reset, rcdev);
}

static int
tps380x_reset_assert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct tps380x_reset *tps380x = to_tps380x_reset(rcdev);

	gpiod_set_value_cansleep(tps380x->reset_gpio, 1);

	return 0;
}

static int
tps380x_reset_deassert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct tps380x_reset *tps380x = to_tps380x_reset(rcdev);

	gpiod_set_value_cansleep(tps380x->reset_gpio, 0);
	msleep(tps380x->reset_ms);

	return 0;
}

static const struct reset_control_ops reset_tps380x_ops = {
	.assert		= tps380x_reset_assert,
	.deassert	= tps380x_reset_deassert,
};

static int tps380x_reset_of_xlate(struct reset_controller_dev *rcdev,
				  const struct of_phandle_args *reset_spec)
{
	/* No special handling needed, we have only one reset line per device */
	return 0;
}

static int tps380x_reset_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct tps380x_reset_devdata *devdata;
	struct tps380x_reset *tps380x;

	devdata = device_get_match_data(dev);
	if (!devdata)
		return -EINVAL;

	tps380x = devm_kzalloc(dev, sizeof(*tps380x), GFP_KERNEL);
	if (!tps380x)
		return -ENOMEM;

	tps380x->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(tps380x->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(tps380x->reset_gpio),
				     "Failed to get GPIO\n");

	tps380x->reset_ms = devdata->max_reset_ms;

	tps380x->rcdev.ops = &reset_tps380x_ops;
	tps380x->rcdev.owner = THIS_MODULE;
	tps380x->rcdev.dev = dev;
	tps380x->rcdev.of_node = dev->of_node;
	tps380x->rcdev.of_reset_n_cells = 0;
	tps380x->rcdev.of_xlate = tps380x_reset_of_xlate;
	tps380x->rcdev.nr_resets = 1;

	return devm_reset_controller_register(dev, &tps380x->rcdev);
}

static const struct tps380x_reset_devdata tps3801_reset_data = {
	.min_reset_ms = 120,
	.typ_reset_ms = 200,
	.max_reset_ms = 280,
};

static const struct of_device_id tps380x_reset_dt_ids[] = {
	{ .compatible = "ti,tps3801", .data = &tps3801_reset_data },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, tps380x_reset_dt_ids);

static struct platform_driver tps380x_reset_driver = {
	.probe	= tps380x_reset_probe,
	.driver = {
		.name		= "tps380x-reset",
		.of_match_table	= tps380x_reset_dt_ids,
	},
};
module_platform_driver(tps380x_reset_driver);

MODULE_AUTHOR("Marco Felsch <kernel@pengutronix.de>");
MODULE_DESCRIPTION("TI TPS380x Supply Voltage Supervisor and Reset Driver");
MODULE_LICENSE("GPL v2");
