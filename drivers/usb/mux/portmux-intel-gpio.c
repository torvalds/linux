/*
 * USB Dual Role Port Mux driver controlled by gpios
 *
 * Copyright (c) 2016, Intel Corporation.
 * Author: David Cohen <david.a.cohen@linux.intel.com>
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/extcon.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/usb/portmux.h>
#include <linux/regulator/consumer.h>

struct vuport {
	struct extcon_dev *edev;
	struct notifier_block nb;
	struct portmux_desc desc;
	struct portmux_dev *pdev;
	struct regulator *regulator;
	struct gpio_desc *gpio_usb_mux;
};

/*
 * id == 0, HOST connected, USB port should be set to peripheral
 * id == 1, HOST disconnected, USB port should be set to host
 *
 * Peripheral: set USB mux to peripheral and disable VBUS
 * Host: set USB mux to host and enable VBUS
 */
static inline int vuport_set_port(struct device *dev, int id)
{
	struct vuport *vup;

	dev_dbg(dev, "USB PORT ID: %s\n", id ? "HOST" : "PERIPHERAL");

	vup = dev_get_drvdata(dev);

	gpiod_set_value_cansleep(vup->gpio_usb_mux, !id);

	if (!id ^ regulator_is_enabled(vup->regulator))
		return id ? regulator_disable(vup->regulator) :
				regulator_enable(vup->regulator);

	return 0;
}

static int vuport_cable_set(struct device *dev)
{
	return vuport_set_port(dev, 1);
}

static int vuport_cable_unset(struct device *dev)
{
	return vuport_set_port(dev, 0);
}

static const struct portmux_ops vuport_ops = {
	.set_host_cb = vuport_cable_set,
	.set_device_cb = vuport_cable_unset,
};

static int vuport_notifier(struct notifier_block *nb,
			   unsigned long event, void *ptr)
{
	struct vuport *vup;
	int state;

	vup = container_of(nb, struct vuport, nb);
	state = extcon_get_cable_state_(vup->edev, EXTCON_USB_HOST);
	if (state < 0)
		return state;

	return portmux_switch(vup->pdev, state ? PORTMUX_HOST : PORTMUX_DEVICE);
}

static int vuport_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vuport *vup;
	int ret;

	vup = devm_kzalloc(dev, sizeof(*vup), GFP_KERNEL);
	if (!vup)
		return -ENOMEM;

	vup->regulator = devm_regulator_get_exclusive(dev,
						      "regulator-usb-gpio");
	if (IS_ERR_OR_NULL(vup->regulator))
		return -EPROBE_DEFER;

	vup->edev = extcon_get_extcon_dev("extcon-usb-gpio");
	if (IS_ERR_OR_NULL(vup->edev))
		return -EPROBE_DEFER;

	vup->gpio_usb_mux = devm_gpiod_get_optional(dev,
						    "usb_mux", GPIOD_ASIS);
	if (IS_ERR(vup->gpio_usb_mux))
		return PTR_ERR(vup->gpio_usb_mux);

	vup->desc.dev = dev;
	vup->desc.name = "intel-mux-gpio";
	vup->desc.ops = &vuport_ops;
	dev_set_drvdata(dev, vup);
	vup->pdev = portmux_register(&vup->desc);
	if (IS_ERR(vup->pdev))
		return PTR_ERR(vup->pdev);

	vup->nb.notifier_call = vuport_notifier;
	ret = extcon_register_notifier(vup->edev, EXTCON_USB_HOST,
				       &vup->nb);
	if (ret < 0) {
		portmux_unregister(vup->pdev);
		return ret;
	}

	vuport_notifier(&vup->nb, 0, NULL);

	return 0;
}

static int vuport_remove(struct platform_device *pdev)
{
	struct vuport *vup;

	vup = platform_get_drvdata(pdev);
	extcon_unregister_notifier(vup->edev, EXTCON_USB_HOST, &vup->nb);
	portmux_unregister(vup->pdev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
/*
 * In case a micro A cable was plugged in while device was sleeping,
 * we missed the interrupt. We need to poll usb id gpio when waking the
 * driver to detect the missed event.
 * We use 'complete' callback to give time to all extcon listeners to
 * resume before we send new events.
 */
static void vuport_complete(struct device *dev)
{
	struct vuport *vup;

	vup = dev_get_drvdata(dev);
	vuport_notifier(&vup->nb, 0, NULL);
}

static const struct dev_pm_ops vuport_pm_ops = {
	.complete = vuport_complete,
};
#endif

static const struct platform_device_id vuport_platform_ids[] = {
	{ .name = "intel-mux-gpio", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, vuport_platform_ids);

static struct platform_driver vuport_driver = {
	.driver = {
		.name = "intel-mux-gpio",
#ifdef CONFIG_PM_SLEEP
		.pm = &vuport_pm_ops,
#endif
	},
	.probe = vuport_probe,
	.remove = vuport_remove,
	.id_table = vuport_platform_ids,
};

module_platform_driver(vuport_driver);

MODULE_AUTHOR("David Cohen <david.a.cohen@linux.intel.com>");
MODULE_AUTHOR("Lu Baolu <baolu.lu@linux.intel.com>");
MODULE_DESCRIPTION("Intel USB gpio mux driver");
MODULE_LICENSE("GPL v2");
