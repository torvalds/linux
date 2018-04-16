// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2017 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <media/cec-pin.h>

struct cec_gpio {
	struct cec_adapter	*adap;
	struct device		*dev;

	struct gpio_desc	*cec_gpio;
	int			cec_irq;
	bool			cec_is_low;
	bool			cec_have_irq;

	struct gpio_desc	*hpd_gpio;
	int			hpd_irq;
	bool			hpd_is_high;
	ktime_t			hpd_ts;
};

static bool cec_gpio_read(struct cec_adapter *adap)
{
	struct cec_gpio *cec = cec_get_drvdata(adap);

	if (cec->cec_is_low)
		return false;
	return gpiod_get_value(cec->cec_gpio);
}

static void cec_gpio_high(struct cec_adapter *adap)
{
	struct cec_gpio *cec = cec_get_drvdata(adap);

	if (!cec->cec_is_low)
		return;
	cec->cec_is_low = false;
	gpiod_set_value(cec->cec_gpio, 1);
}

static void cec_gpio_low(struct cec_adapter *adap)
{
	struct cec_gpio *cec = cec_get_drvdata(adap);

	if (cec->cec_is_low)
		return;
	if (WARN_ON_ONCE(cec->cec_have_irq))
		free_irq(cec->cec_irq, cec);
	cec->cec_have_irq = false;
	cec->cec_is_low = true;
	gpiod_set_value(cec->cec_gpio, 0);
}

static irqreturn_t cec_hpd_gpio_irq_handler_thread(int irq, void *priv)
{
	struct cec_gpio *cec = priv;

	cec_queue_pin_hpd_event(cec->adap, cec->hpd_is_high, cec->hpd_ts);
	return IRQ_HANDLED;
}

static irqreturn_t cec_hpd_gpio_irq_handler(int irq, void *priv)
{
	struct cec_gpio *cec = priv;
	bool is_high = gpiod_get_value(cec->hpd_gpio);

	if (is_high == cec->hpd_is_high)
		return IRQ_HANDLED;
	cec->hpd_ts = ktime_get();
	cec->hpd_is_high = is_high;
	return IRQ_WAKE_THREAD;
}

static irqreturn_t cec_gpio_irq_handler(int irq, void *priv)
{
	struct cec_gpio *cec = priv;

	cec_pin_changed(cec->adap, gpiod_get_value(cec->cec_gpio));
	return IRQ_HANDLED;
}

static bool cec_gpio_enable_irq(struct cec_adapter *adap)
{
	struct cec_gpio *cec = cec_get_drvdata(adap);

	if (cec->cec_have_irq)
		return true;

	if (request_irq(cec->cec_irq, cec_gpio_irq_handler,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			adap->name, cec))
		return false;
	cec->cec_have_irq = true;
	return true;
}

static void cec_gpio_disable_irq(struct cec_adapter *adap)
{
	struct cec_gpio *cec = cec_get_drvdata(adap);

	if (cec->cec_have_irq)
		free_irq(cec->cec_irq, cec);
	cec->cec_have_irq = false;
}

static void cec_gpio_status(struct cec_adapter *adap, struct seq_file *file)
{
	struct cec_gpio *cec = cec_get_drvdata(adap);

	seq_printf(file, "mode: %s\n", cec->cec_is_low ? "low-drive" : "read");
	if (cec->cec_have_irq)
		seq_printf(file, "using irq: %d\n", cec->cec_irq);
	if (cec->hpd_gpio)
		seq_printf(file, "hpd: %s\n",
			   cec->hpd_is_high ? "high" : "low");
}

static int cec_gpio_read_hpd(struct cec_adapter *adap)
{
	struct cec_gpio *cec = cec_get_drvdata(adap);

	if (!cec->hpd_gpio)
		return -ENOTTY;
	return gpiod_get_value(cec->hpd_gpio);
}

static void cec_gpio_free(struct cec_adapter *adap)
{
	cec_gpio_disable_irq(adap);
}

static const struct cec_pin_ops cec_gpio_pin_ops = {
	.read = cec_gpio_read,
	.low = cec_gpio_low,
	.high = cec_gpio_high,
	.enable_irq = cec_gpio_enable_irq,
	.disable_irq = cec_gpio_disable_irq,
	.status = cec_gpio_status,
	.free = cec_gpio_free,
	.read_hpd = cec_gpio_read_hpd,
};

static int cec_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cec_gpio *cec;
	int ret;

	cec = devm_kzalloc(dev, sizeof(*cec), GFP_KERNEL);
	if (!cec)
		return -ENOMEM;

	cec->dev = dev;

	cec->cec_gpio = devm_gpiod_get(dev, "cec", GPIOD_IN);
	if (IS_ERR(cec->cec_gpio))
		return PTR_ERR(cec->cec_gpio);
	cec->cec_irq = gpiod_to_irq(cec->cec_gpio);

	cec->hpd_gpio = devm_gpiod_get_optional(dev, "hpd", GPIOD_IN);
	if (IS_ERR(cec->hpd_gpio))
		return PTR_ERR(cec->hpd_gpio);

	cec->adap = cec_pin_allocate_adapter(&cec_gpio_pin_ops,
		cec, pdev->name, CEC_CAP_DEFAULTS | CEC_CAP_PHYS_ADDR |
				 CEC_CAP_MONITOR_ALL | CEC_CAP_MONITOR_PIN);
	if (IS_ERR(cec->adap))
		return PTR_ERR(cec->adap);

	if (cec->hpd_gpio) {
		cec->hpd_irq = gpiod_to_irq(cec->hpd_gpio);
		ret = devm_request_threaded_irq(dev, cec->hpd_irq,
			cec_hpd_gpio_irq_handler,
			cec_hpd_gpio_irq_handler_thread,
			IRQF_ONESHOT |
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
			"hpd-gpio", cec);
		if (ret)
			return ret;
	}

	ret = cec_register_adapter(cec->adap, &pdev->dev);
	if (ret) {
		cec_delete_adapter(cec->adap);
		return ret;
	}

	platform_set_drvdata(pdev, cec);
	return 0;
}

static int cec_gpio_remove(struct platform_device *pdev)
{
	struct cec_gpio *cec = platform_get_drvdata(pdev);

	cec_unregister_adapter(cec->adap);
	return 0;
}

static const struct of_device_id cec_gpio_match[] = {
	{
		.compatible	= "cec-gpio",
	},
	{},
};
MODULE_DEVICE_TABLE(of, cec_gpio_match);

static struct platform_driver cec_gpio_pdrv = {
	.probe	= cec_gpio_probe,
	.remove = cec_gpio_remove,
	.driver = {
		.name		= "cec-gpio",
		.of_match_table	= cec_gpio_match,
	},
};

module_platform_driver(cec_gpio_pdrv);

MODULE_AUTHOR("Hans Verkuil <hans.verkuil@cisco.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CEC GPIO driver");
