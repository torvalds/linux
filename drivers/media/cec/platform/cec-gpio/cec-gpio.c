// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2017 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <media/cec-notifier.h>
#include <media/cec-pin.h>

struct cec_gpio {
	struct cec_adapter	*adap;
	struct cec_notifier	*notifier;
	struct device		*dev;

	struct gpio_desc	*cec_gpio;
	int			cec_irq;
	bool			cec_is_low;

	struct gpio_desc	*hpd_gpio;
	int			hpd_irq;
	bool			hpd_is_high;
	ktime_t			hpd_ts;

	struct gpio_desc	*v5_gpio;
	int			v5_irq;
	bool			v5_is_high;
	ktime_t			v5_ts;
};

static int cec_gpio_read(struct cec_adapter *adap)
{
	struct cec_gpio *cec = cec_get_drvdata(adap);

	if (cec->cec_is_low)
		return 0;
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
	cec->cec_is_low = true;
	gpiod_set_value(cec->cec_gpio, 0);
}

static irqreturn_t cec_hpd_gpio_irq_handler_thread(int irq, void *priv)
{
	struct cec_gpio *cec = priv;

	cec_queue_pin_hpd_event(cec->adap, cec->hpd_is_high, cec->hpd_ts);
	return IRQ_HANDLED;
}

static irqreturn_t cec_5v_gpio_irq_handler(int irq, void *priv)
{
	struct cec_gpio *cec = priv;
	int val = gpiod_get_value(cec->v5_gpio);
	bool is_high = val > 0;

	if (val < 0 || is_high == cec->v5_is_high)
		return IRQ_HANDLED;
	cec->v5_ts = ktime_get();
	cec->v5_is_high = is_high;
	return IRQ_WAKE_THREAD;
}

static irqreturn_t cec_5v_gpio_irq_handler_thread(int irq, void *priv)
{
	struct cec_gpio *cec = priv;

	cec_queue_pin_5v_event(cec->adap, cec->v5_is_high, cec->v5_ts);
	return IRQ_HANDLED;
}

static irqreturn_t cec_hpd_gpio_irq_handler(int irq, void *priv)
{
	struct cec_gpio *cec = priv;
	int val = gpiod_get_value(cec->hpd_gpio);
	bool is_high = val > 0;

	if (val < 0 || is_high == cec->hpd_is_high)
		return IRQ_HANDLED;
	cec->hpd_ts = ktime_get();
	cec->hpd_is_high = is_high;
	return IRQ_WAKE_THREAD;
}

static irqreturn_t cec_gpio_irq_handler(int irq, void *priv)
{
	struct cec_gpio *cec = priv;
	int val = gpiod_get_value(cec->cec_gpio);

	if (val >= 0)
		cec_pin_changed(cec->adap, val > 0);
	return IRQ_HANDLED;
}

static bool cec_gpio_enable_irq(struct cec_adapter *adap)
{
	struct cec_gpio *cec = cec_get_drvdata(adap);

	enable_irq(cec->cec_irq);
	return true;
}

static void cec_gpio_disable_irq(struct cec_adapter *adap)
{
	struct cec_gpio *cec = cec_get_drvdata(adap);

	disable_irq(cec->cec_irq);
}

static void cec_gpio_status(struct cec_adapter *adap, struct seq_file *file)
{
	struct cec_gpio *cec = cec_get_drvdata(adap);

	seq_printf(file, "mode: %s\n", cec->cec_is_low ? "low-drive" : "read");
	seq_printf(file, "using irq: %d\n", cec->cec_irq);
	if (cec->hpd_gpio)
		seq_printf(file, "hpd: %s\n",
			   cec->hpd_is_high ? "high" : "low");
	if (cec->v5_gpio)
		seq_printf(file, "5V: %s\n",
			   cec->v5_is_high ? "high" : "low");
}

static int cec_gpio_read_hpd(struct cec_adapter *adap)
{
	struct cec_gpio *cec = cec_get_drvdata(adap);

	if (!cec->hpd_gpio)
		return -ENOTTY;
	return gpiod_get_value(cec->hpd_gpio);
}

static int cec_gpio_read_5v(struct cec_adapter *adap)
{
	struct cec_gpio *cec = cec_get_drvdata(adap);

	if (!cec->v5_gpio)
		return -ENOTTY;
	return gpiod_get_value(cec->v5_gpio);
}

static const struct cec_pin_ops cec_gpio_pin_ops = {
	.read = cec_gpio_read,
	.low = cec_gpio_low,
	.high = cec_gpio_high,
	.enable_irq = cec_gpio_enable_irq,
	.disable_irq = cec_gpio_disable_irq,
	.status = cec_gpio_status,
	.read_hpd = cec_gpio_read_hpd,
	.read_5v = cec_gpio_read_5v,
};

static int cec_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *hdmi_dev;
	struct cec_gpio *cec;
	u32 caps = CEC_CAP_DEFAULTS | CEC_CAP_MONITOR_ALL | CEC_CAP_MONITOR_PIN;
	int ret;

	hdmi_dev = cec_notifier_parse_hdmi_phandle(dev);
	if (PTR_ERR(hdmi_dev) == -EPROBE_DEFER)
		return PTR_ERR(hdmi_dev);
	if (IS_ERR(hdmi_dev))
		caps |= CEC_CAP_PHYS_ADDR;

	cec = devm_kzalloc(dev, sizeof(*cec), GFP_KERNEL);
	if (!cec)
		return -ENOMEM;

	cec->dev = dev;

	cec->cec_gpio = devm_gpiod_get(dev, "cec", GPIOD_OUT_HIGH_OPEN_DRAIN);
	if (IS_ERR(cec->cec_gpio))
		return PTR_ERR(cec->cec_gpio);
	cec->cec_irq = gpiod_to_irq(cec->cec_gpio);

	cec->hpd_gpio = devm_gpiod_get_optional(dev, "hpd", GPIOD_IN);
	if (IS_ERR(cec->hpd_gpio))
		return PTR_ERR(cec->hpd_gpio);

	cec->v5_gpio = devm_gpiod_get_optional(dev, "v5", GPIOD_IN);
	if (IS_ERR(cec->v5_gpio))
		return PTR_ERR(cec->v5_gpio);

	cec->adap = cec_pin_allocate_adapter(&cec_gpio_pin_ops,
					     cec, pdev->name, caps);
	if (IS_ERR(cec->adap))
		return PTR_ERR(cec->adap);

	ret = devm_request_irq(dev, cec->cec_irq, cec_gpio_irq_handler,
			       IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_NO_AUTOEN,
			       cec->adap->name, cec);
	if (ret)
		goto del_adap;

	if (cec->hpd_gpio) {
		cec->hpd_irq = gpiod_to_irq(cec->hpd_gpio);
		ret = devm_request_threaded_irq(dev, cec->hpd_irq,
			cec_hpd_gpio_irq_handler,
			cec_hpd_gpio_irq_handler_thread,
			IRQF_ONESHOT |
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
			"hpd-gpio", cec);
		if (ret)
			goto del_adap;
	}

	if (cec->v5_gpio) {
		cec->v5_irq = gpiod_to_irq(cec->v5_gpio);
		ret = devm_request_threaded_irq(dev, cec->v5_irq,
			cec_5v_gpio_irq_handler,
			cec_5v_gpio_irq_handler_thread,
			IRQF_ONESHOT |
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
			"v5-gpio", cec);
		if (ret)
			goto del_adap;
	}

	if (!IS_ERR(hdmi_dev)) {
		cec->notifier = cec_notifier_cec_adap_register(hdmi_dev, NULL,
							       cec->adap);
		if (!cec->notifier) {
			ret = -ENOMEM;
			goto del_adap;
		}
	}

	ret = cec_register_adapter(cec->adap, &pdev->dev);
	if (ret)
		goto unreg_notifier;

	platform_set_drvdata(pdev, cec);
	return 0;

unreg_notifier:
	cec_notifier_cec_adap_unregister(cec->notifier, cec->adap);
del_adap:
	cec_delete_adapter(cec->adap);
	return ret;
}

static void cec_gpio_remove(struct platform_device *pdev)
{
	struct cec_gpio *cec = platform_get_drvdata(pdev);

	cec_notifier_cec_adap_unregister(cec->notifier, cec->adap);
	cec_unregister_adapter(cec->adap);
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
	.remove_new = cec_gpio_remove,
	.driver = {
		.name		= "cec-gpio",
		.of_match_table	= cec_gpio_match,
	},
};

module_platform_driver(cec_gpio_pdrv);

MODULE_AUTHOR("Hans Verkuil <hans.verkuil@cisco.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CEC GPIO driver");
