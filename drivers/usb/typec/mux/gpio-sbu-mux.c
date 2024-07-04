// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Linaro Ltd.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/usb/typec_dp.h>
#include <linux/usb/typec_mux.h>

struct gpio_sbu_mux {
	struct gpio_desc *enable_gpio;
	struct gpio_desc *select_gpio;

	struct typec_switch_dev *sw;
	struct typec_mux_dev *mux;

	struct mutex lock; /* protect enabled and swapped */
	bool enabled;
	bool swapped;
};

static int gpio_sbu_switch_set(struct typec_switch_dev *sw,
			       enum typec_orientation orientation)
{
	struct gpio_sbu_mux *sbu_mux = typec_switch_get_drvdata(sw);
	bool enabled;
	bool swapped;

	mutex_lock(&sbu_mux->lock);

	enabled = sbu_mux->enabled;
	swapped = sbu_mux->swapped;

	switch (orientation) {
	case TYPEC_ORIENTATION_NONE:
		enabled = false;
		break;
	case TYPEC_ORIENTATION_NORMAL:
		swapped = false;
		break;
	case TYPEC_ORIENTATION_REVERSE:
		swapped = true;
		break;
	}

	if (enabled != sbu_mux->enabled)
		gpiod_set_value_cansleep(sbu_mux->enable_gpio, enabled);

	if (swapped != sbu_mux->swapped)
		gpiod_set_value_cansleep(sbu_mux->select_gpio, swapped);

	sbu_mux->enabled = enabled;
	sbu_mux->swapped = swapped;

	mutex_unlock(&sbu_mux->lock);

	return 0;
}

static int gpio_sbu_mux_set(struct typec_mux_dev *mux,
			    struct typec_mux_state *state)
{
	struct gpio_sbu_mux *sbu_mux = typec_mux_get_drvdata(mux);

	mutex_lock(&sbu_mux->lock);

	switch (state->mode) {
	case TYPEC_STATE_SAFE:
	case TYPEC_STATE_USB:
		sbu_mux->enabled = false;
		break;
	case TYPEC_DP_STATE_C:
	case TYPEC_DP_STATE_D:
	case TYPEC_DP_STATE_E:
		sbu_mux->enabled = true;
		break;
	default:
		break;
	}

	gpiod_set_value_cansleep(sbu_mux->enable_gpio, sbu_mux->enabled);

	mutex_unlock(&sbu_mux->lock);

	return 0;
}

static int gpio_sbu_mux_probe(struct platform_device *pdev)
{
	struct typec_switch_desc sw_desc = { };
	struct typec_mux_desc mux_desc = { };
	struct device *dev = &pdev->dev;
	struct gpio_sbu_mux *sbu_mux;

	sbu_mux = devm_kzalloc(dev, sizeof(*sbu_mux), GFP_KERNEL);
	if (!sbu_mux)
		return -ENOMEM;

	mutex_init(&sbu_mux->lock);

	sbu_mux->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(sbu_mux->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(sbu_mux->enable_gpio),
				     "unable to acquire enable gpio\n");

	sbu_mux->select_gpio = devm_gpiod_get(dev, "select", GPIOD_OUT_LOW);
	if (IS_ERR(sbu_mux->select_gpio))
		return dev_err_probe(dev, PTR_ERR(sbu_mux->select_gpio),
				     "unable to acquire select gpio\n");

	sw_desc.drvdata = sbu_mux;
	sw_desc.fwnode = dev_fwnode(dev);
	sw_desc.set = gpio_sbu_switch_set;

	sbu_mux->sw = typec_switch_register(dev, &sw_desc);
	if (IS_ERR(sbu_mux->sw))
		return dev_err_probe(dev, PTR_ERR(sbu_mux->sw),
				     "failed to register typec switch\n");

	mux_desc.drvdata = sbu_mux;
	mux_desc.fwnode = dev_fwnode(dev);
	mux_desc.set = gpio_sbu_mux_set;

	sbu_mux->mux = typec_mux_register(dev, &mux_desc);
	if (IS_ERR(sbu_mux->mux)) {
		typec_switch_unregister(sbu_mux->sw);
		return dev_err_probe(dev, PTR_ERR(sbu_mux->mux),
				     "failed to register typec mux\n");
	}

	platform_set_drvdata(pdev, sbu_mux);

	return 0;
}

static void gpio_sbu_mux_remove(struct platform_device *pdev)
{
	struct gpio_sbu_mux *sbu_mux = platform_get_drvdata(pdev);

	gpiod_set_value_cansleep(sbu_mux->enable_gpio, 0);

	typec_mux_unregister(sbu_mux->mux);
	typec_switch_unregister(sbu_mux->sw);
}

static const struct of_device_id gpio_sbu_mux_match[] = {
	{ .compatible = "gpio-sbu-mux", },
	{}
};
MODULE_DEVICE_TABLE(of, gpio_sbu_mux_match);

static struct platform_driver gpio_sbu_mux_driver = {
	.probe = gpio_sbu_mux_probe,
	.remove_new = gpio_sbu_mux_remove,
	.driver = {
		.name = "gpio_sbu_mux",
		.of_match_table = gpio_sbu_mux_match,
	},
};
module_platform_driver(gpio_sbu_mux_driver);

MODULE_DESCRIPTION("GPIO based SBU mux driver");
MODULE_LICENSE("GPL");
