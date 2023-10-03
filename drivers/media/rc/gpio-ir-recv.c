// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2012, Code Aurora Forum. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <linux/irq.h>
#include <media/rc-core.h>

#define GPIO_IR_DEVICE_NAME	"gpio_ir_recv"

struct gpio_rc_dev {
	struct rc_dev *rcdev;
	struct gpio_desc *gpiod;
	int irq;
	struct device *pmdev;
	struct pm_qos_request qos;
};

static irqreturn_t gpio_ir_recv_irq(int irq, void *dev_id)
{
	int val;
	struct gpio_rc_dev *gpio_dev = dev_id;
	struct device *pmdev = gpio_dev->pmdev;

	/*
	 * For some cpuidle systems, not all:
	 * Respond to interrupt taking more latency when cpu in idle.
	 * Invoke asynchronous pm runtime get from interrupt context,
	 * this may introduce a millisecond delay to call resume callback,
	 * where to disable cpuilde.
	 *
	 * Two issues lead to fail to decode first frame, one is latency to
	 * respond to interrupt, another is delay introduced by async api.
	 */
	if (pmdev)
		pm_runtime_get(pmdev);

	val = gpiod_get_value(gpio_dev->gpiod);
	if (val >= 0)
		ir_raw_event_store_edge(gpio_dev->rcdev, val == 1);

	if (pmdev) {
		pm_runtime_mark_last_busy(pmdev);
		pm_runtime_put_autosuspend(pmdev);
	}

	return IRQ_HANDLED;
}

static int gpio_ir_recv_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct gpio_rc_dev *gpio_dev;
	struct rc_dev *rcdev;
	u32 period = 0;
	int rc;

	if (!np)
		return -ENODEV;

	gpio_dev = devm_kzalloc(dev, sizeof(*gpio_dev), GFP_KERNEL);
	if (!gpio_dev)
		return -ENOMEM;

	gpio_dev->gpiod = devm_gpiod_get(dev, NULL, GPIOD_IN);
	if (IS_ERR(gpio_dev->gpiod))
		return dev_err_probe(dev, PTR_ERR(gpio_dev->gpiod),
				     "error getting gpio\n");
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
	if (of_property_read_bool(np, "wakeup-source"))
		device_init_wakeup(dev, true);

	rc = devm_rc_register_device(dev, rcdev);
	if (rc < 0) {
		dev_err(dev, "failed to register rc device (%d)\n", rc);
		return rc;
	}

	of_property_read_u32(np, "linux,autosuspend-period", &period);
	if (period) {
		gpio_dev->pmdev = dev;
		pm_runtime_set_autosuspend_delay(dev, period);
		pm_runtime_use_autosuspend(dev);
		pm_runtime_set_suspended(dev);
		pm_runtime_enable(dev);
	}

	platform_set_drvdata(pdev, gpio_dev);

	return devm_request_irq(dev, gpio_dev->irq, gpio_ir_recv_irq,
				IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
				"gpio-ir-recv-irq", gpio_dev);
}

static void gpio_ir_recv_remove(struct platform_device *pdev)
{
	struct gpio_rc_dev *gpio_dev = platform_get_drvdata(pdev);
	struct device *pmdev = gpio_dev->pmdev;

	if (pmdev) {
		pm_runtime_get_sync(pmdev);
		cpu_latency_qos_remove_request(&gpio_dev->qos);

		pm_runtime_disable(pmdev);
		pm_runtime_put_noidle(pmdev);
		pm_runtime_set_suspended(pmdev);
	}
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

static int gpio_ir_recv_runtime_suspend(struct device *dev)
{
	struct gpio_rc_dev *gpio_dev = dev_get_drvdata(dev);

	cpu_latency_qos_remove_request(&gpio_dev->qos);

	return 0;
}

static int gpio_ir_recv_runtime_resume(struct device *dev)
{
	struct gpio_rc_dev *gpio_dev = dev_get_drvdata(dev);

	cpu_latency_qos_add_request(&gpio_dev->qos, 0);

	return 0;
}

static const struct dev_pm_ops gpio_ir_recv_pm_ops = {
	.suspend        = gpio_ir_recv_suspend,
	.resume         = gpio_ir_recv_resume,
	.runtime_suspend = gpio_ir_recv_runtime_suspend,
	.runtime_resume  = gpio_ir_recv_runtime_resume,
};
#endif

static const struct of_device_id gpio_ir_recv_of_match[] = {
	{ .compatible = "gpio-ir-receiver", },
	{ },
};
MODULE_DEVICE_TABLE(of, gpio_ir_recv_of_match);

static struct platform_driver gpio_ir_recv_driver = {
	.probe  = gpio_ir_recv_probe,
	.remove_new = gpio_ir_recv_remove,
	.driver = {
		.name   = KBUILD_MODNAME,
		.of_match_table = gpio_ir_recv_of_match,
#ifdef CONFIG_PM
		.pm	= &gpio_ir_recv_pm_ops,
#endif
	},
};
module_platform_driver(gpio_ir_recv_driver);

MODULE_DESCRIPTION("GPIO IR Receiver driver");
MODULE_LICENSE("GPL v2");
