/* Copyright (c) 2012, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <media/rc-core.h>

#define GPIO_IR_DEVICE_NAME	"gpio_ir_recv"

struct gpio_rc_dev {
	struct rc_dev *rcdev;
	struct gpio_desc *gpiod;
	int irq;
};

static irqreturn_t gpio_ir_recv_irq(int irq, void *dev_id)
{
	int val;
	struct gpio_rc_dev *gpio_dev = dev_id;

	val = gpiod_get_value(gpio_dev->gpiod);
	if (val >= 0)
		ir_raw_event_store_edge(gpio_dev->rcdev, val == 1);

	return IRQ_HANDLED;
}

static int gpio_ir_recv_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct gpio_rc_dev *gpio_dev;
	struct rc_dev *rcdev;
	int rc;

	if (!np)
		return -ENODEV;

	gpio_dev = devm_kzalloc(dev, sizeof(*gpio_dev), GFP_KERNEL);
	if (!gpio_dev)
		return -ENOMEM;

	gpio_dev->gpiod = devm_gpiod_get(dev, NULL, GPIOD_IN);
	if (IS_ERR(gpio_dev->gpiod)) {
		rc = PTR_ERR(gpio_dev->gpiod);
		/* Just try again if this happens */
		if (rc != -EPROBE_DEFER)
			dev_err(dev, "error getting gpio (%d)\n", rc);
		return rc;
	}
	gpio_dev->irq = gpiod_to_irq(gpio_dev->gpiod);
	if (gpio_dev->irq < 0)
		return gpio_dev->irq;

	rcdev = devm_rc_allocate_device(dev, RC_DRIVER_IR_RAW);
	if (!rcdev)
		return -ENOMEM;

	rcdev->priv = gpio_dev;
	rcdev->device_name = GPIO_IR_DEVICE_NAME;
	rcdev->input_phys = GPIO_IR_DEVICE_NAME "/input0";
	rcdev->input_id.bustype = BUS_HOST;
	rcdev->input_id.vendor = 0x0001;
	rcdev->input_id.product = 0x0001;
	rcdev->input_id.version = 0x0100;
	rcdev->dev.parent = dev;
	rcdev->driver_name = KBUILD_MODNAME;
	rcdev->min_timeout = 1;
	rcdev->timeout = IR_DEFAULT_TIMEOUT;
	rcdev->max_timeout = 10 * IR_DEFAULT_TIMEOUT;
	rcdev->allowed_protocols = RC_PROTO_BIT_ALL_IR_DECODER;
	rcdev->map_name = of_get_property(np, "linux,rc-map-name", NULL);
	if (!rcdev->map_name)
		rcdev->map_name = RC_MAP_EMPTY;

	gpio_dev->rcdev = rcdev;

	rc = devm_rc_register_device(dev, rcdev);
	if (rc < 0) {
		dev_err(dev, "failed to register rc device (%d)\n", rc);
		return rc;
	}

	platform_set_drvdata(pdev, gpio_dev);

	return devm_request_irq(dev, gpio_dev->irq, gpio_ir_recv_irq,
				IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
				"gpio-ir-recv-irq", gpio_dev);
}

#ifdef CONFIG_PM
static int gpio_ir_recv_suspend(struct device *dev)
{
	struct gpio_rc_dev *gpio_dev = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(gpio_dev->irq);
	else
		disable_irq(gpio_dev->irq);

	return 0;
}

static int gpio_ir_recv_resume(struct device *dev)
{
	struct gpio_rc_dev *gpio_dev = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(gpio_dev->irq);
	else
		enable_irq(gpio_dev->irq);

	return 0;
}

static const struct dev_pm_ops gpio_ir_recv_pm_ops = {
	.suspend        = gpio_ir_recv_suspend,
	.resume         = gpio_ir_recv_resume,
};
#endif

static const struct of_device_id gpio_ir_recv_of_match[] = {
	{ .compatible = "gpio-ir-receiver", },
	{ },
};
MODULE_DEVICE_TABLE(of, gpio_ir_recv_of_match);

static struct platform_driver gpio_ir_recv_driver = {
	.probe  = gpio_ir_recv_probe,
	.driver = {
		.name   = KBUILD_MODNAME,
		.of_match_table = of_match_ptr(gpio_ir_recv_of_match),
#ifdef CONFIG_PM
		.pm	= &gpio_ir_recv_pm_ops,
#endif
	},
};
module_platform_driver(gpio_ir_recv_driver);

MODULE_DESCRIPTION("GPIO IR Receiver driver");
MODULE_LICENSE("GPL v2");
