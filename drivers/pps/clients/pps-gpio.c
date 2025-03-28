// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * pps-gpio.c -- PPS client driver using GPIO
 *
 * Copyright (C) 2010 Ricardo Martins <rasm@fe.up.pt>
 * Copyright (C) 2011 James Nuss <jamesnuss@nanometrics.ca>
 */

#define PPS_GPIO_NAME "pps-gpio"
#define pr_fmt(fmt) PPS_GPIO_NAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/pps_kernel.h>
#include <linux/gpio/consumer.h>
#include <linux/list.h>
#include <linux/property.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

/* Info for each registered platform device */
struct pps_gpio_device_data {
	int irq;			/* IRQ used as PPS source */
	struct pps_device *pps;		/* PPS source device */
	struct pps_source_info info;	/* PPS source information */
	struct gpio_desc *gpio_pin;	/* GPIO port descriptors */
	struct gpio_desc *echo_pin;
	struct timer_list echo_timer;	/* timer to reset echo active state */
	bool assert_falling_edge;
	bool capture_clear;
	unsigned int echo_active_ms;	/* PPS echo active duration */
	unsigned long echo_timeout;	/* timer timeout value in jiffies */
};

/*
 * Report the PPS event
 */

static irqreturn_t pps_gpio_irq_handler(int irq, void *data)
{
	const struct pps_gpio_device_data *info;
	struct pps_event_time ts;
	int rising_edge;

	/* Get the time stamp first */
	pps_get_ts(&ts);

	info = data;

	/* Small trick to bypass the check on edge's direction when capture_clear is unset */
	rising_edge = info->capture_clear ?
		      gpiod_get_value(info->gpio_pin) : !info->assert_falling_edge;
	if ((rising_edge && !info->assert_falling_edge) ||
			(!rising_edge && info->assert_falling_edge))
		pps_event(info->pps, &ts, PPS_CAPTUREASSERT, data);
	else if (info->capture_clear &&
			((rising_edge && info->assert_falling_edge) ||
			(!rising_edge && !info->assert_falling_edge)))
		pps_event(info->pps, &ts, PPS_CAPTURECLEAR, data);
	else
		dev_warn_ratelimited(&info->pps->dev, "IRQ did not trigger any PPS event\n");

	return IRQ_HANDLED;
}

/* This function will only be called when an ECHO GPIO is defined */
static void pps_gpio_echo(struct pps_device *pps, int event, void *data)
{
	/* add_timer() needs to write into info->echo_timer */
	struct pps_gpio_device_data *info = data;

	switch (event) {
	case PPS_CAPTUREASSERT:
		if (pps->params.mode & PPS_ECHOASSERT)
			gpiod_set_value(info->echo_pin, 1);
		break;

	case PPS_CAPTURECLEAR:
		if (pps->params.mode & PPS_ECHOCLEAR)
			gpiod_set_value(info->echo_pin, 1);
		break;
	}

	/* fire the timer */
	if (info->pps->params.mode & (PPS_ECHOASSERT | PPS_ECHOCLEAR)) {
		info->echo_timer.expires = jiffies + info->echo_timeout;
		add_timer(&info->echo_timer);
	}
}

/* Timer callback to reset the echo pin to the inactive state */
static void pps_gpio_echo_timer_callback(struct timer_list *t)
{
	const struct pps_gpio_device_data *info;

	info = from_timer(info, t, echo_timer);

	gpiod_set_value(info->echo_pin, 0);
}

static int pps_gpio_setup(struct device *dev)
{
	struct pps_gpio_device_data *data = dev_get_drvdata(dev);
	int ret;
	u32 value;

	data->gpio_pin = devm_gpiod_get(dev, NULL, GPIOD_IN);
	if (IS_ERR(data->gpio_pin))
		return dev_err_probe(dev, PTR_ERR(data->gpio_pin),
				     "failed to request PPS GPIO\n");

	data->assert_falling_edge =
		device_property_read_bool(dev, "assert-falling-edge");

	data->echo_pin = devm_gpiod_get_optional(dev, "echo", GPIOD_OUT_LOW);
	if (IS_ERR(data->echo_pin))
		return dev_err_probe(dev, PTR_ERR(data->echo_pin),
				     "failed to request ECHO GPIO\n");

	if (!data->echo_pin)
		return 0;

	ret = device_property_read_u32(dev, "echo-active-ms", &value);
	if (ret) {
		dev_err(dev, "failed to get echo-active-ms from FW\n");
		return ret;
	}

	/* sanity check on echo_active_ms */
	if (!value || value > 999) {
		dev_err(dev, "echo-active-ms: %u - bad value from FW\n", value);
		return -EINVAL;
	}

	data->echo_active_ms = value;

	return 0;
}

static unsigned long
get_irqf_trigger_flags(const struct pps_gpio_device_data *data)
{
	unsigned long flags = data->assert_falling_edge ?
		IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;

	if (data->capture_clear) {
		flags |= ((flags & IRQF_TRIGGER_RISING) ?
				IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING);
	}

	return flags;
}

static int pps_gpio_probe(struct platform_device *pdev)
{
	struct pps_gpio_device_data *data;
	struct device *dev = &pdev->dev;
	int ret;
	int pps_default_params;

	/* allocate space for device info */
	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	dev_set_drvdata(dev, data);

	/* GPIO setup */
	ret = pps_gpio_setup(dev);
	if (ret)
		return ret;

	/* IRQ setup */
	ret = gpiod_to_irq(data->gpio_pin);
	if (ret < 0) {
		dev_err(dev, "failed to map GPIO to IRQ: %d\n", ret);
		return -EINVAL;
	}
	data->irq = ret;

	/* initialize PPS specific parts of the bookkeeping data structure. */
	data->info.mode = PPS_CAPTUREASSERT | PPS_OFFSETASSERT |
		PPS_ECHOASSERT | PPS_CANWAIT | PPS_TSFMT_TSPEC;
	if (data->capture_clear)
		data->info.mode |= PPS_CAPTURECLEAR | PPS_OFFSETCLEAR |
			PPS_ECHOCLEAR;
	data->info.owner = THIS_MODULE;
	snprintf(data->info.name, PPS_MAX_NAME_LEN - 1, "%s.%d",
		 pdev->name, pdev->id);
	if (data->echo_pin) {
		data->info.echo = pps_gpio_echo;
		data->echo_timeout = msecs_to_jiffies(data->echo_active_ms);
		timer_setup(&data->echo_timer, pps_gpio_echo_timer_callback, 0);
	}

	/* register PPS source */
	pps_default_params = PPS_CAPTUREASSERT | PPS_OFFSETASSERT;
	if (data->capture_clear)
		pps_default_params |= PPS_CAPTURECLEAR | PPS_OFFSETCLEAR;
	data->pps = pps_register_source(&data->info, pps_default_params);
	if (IS_ERR(data->pps)) {
		dev_err(dev, "failed to register IRQ %d as PPS source\n",
			data->irq);
		return PTR_ERR(data->pps);
	}

	/* register IRQ interrupt handler */
	ret = devm_request_irq(dev, data->irq, pps_gpio_irq_handler,
			get_irqf_trigger_flags(data), data->info.name, data);
	if (ret) {
		pps_unregister_source(data->pps);
		dev_err(dev, "failed to acquire IRQ %d\n", data->irq);
		return -EINVAL;
	}

	dev_dbg(&data->pps->dev, "Registered IRQ %d as PPS source\n",
		data->irq);

	return 0;
}

static void pps_gpio_remove(struct platform_device *pdev)
{
	struct pps_gpio_device_data *data = platform_get_drvdata(pdev);

	pps_unregister_source(data->pps);
	del_timer_sync(&data->echo_timer);
	/* reset echo pin in any case */
	gpiod_set_value(data->echo_pin, 0);
	dev_info(&pdev->dev, "removed IRQ %d as PPS source\n", data->irq);
}

static const struct of_device_id pps_gpio_dt_ids[] = {
	{ .compatible = "pps-gpio", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pps_gpio_dt_ids);

static struct platform_driver pps_gpio_driver = {
	.probe		= pps_gpio_probe,
	.remove		= pps_gpio_remove,
	.driver		= {
		.name	= PPS_GPIO_NAME,
		.of_match_table	= pps_gpio_dt_ids,
	},
};

module_platform_driver(pps_gpio_driver);
MODULE_AUTHOR("Ricardo Martins <rasm@fe.up.pt>");
MODULE_AUTHOR("James Nuss <jamesnuss@nanometrics.ca>");
MODULE_DESCRIPTION("Use GPIO pin as PPS source");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.2.0");
