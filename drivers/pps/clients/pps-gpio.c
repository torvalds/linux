/*
 * pps-gpio.c -- PPS client driver using GPIO
 *
 *
 * Copyright (C) 2010 Ricardo Martins <rasm@fe.up.pt>
 * Copyright (C) 2011 James Nuss <jamesnuss@nanometrics.ca>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define PPS_GPIO_NAME "pps-gpio"
#define pr_fmt(fmt) PPS_GPIO_NAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/pps_kernel.h>
#include <linux/pps-gpio.h>
#include <linux/gpio.h>
#include <linux/list.h>

/* Info for each registered platform device */
struct pps_gpio_device_data {
	int irq;			/* IRQ used as PPS source */
	struct pps_device *pps;		/* PPS source device */
	struct pps_source_info info;	/* PPS source information */
	const struct pps_gpio_platform_data *pdata;
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

	rising_edge = gpio_get_value(info->pdata->gpio_pin);
	if ((rising_edge && !info->pdata->assert_falling_edge) ||
			(!rising_edge && info->pdata->assert_falling_edge))
		pps_event(info->pps, &ts, PPS_CAPTUREASSERT, NULL);
	else if (info->pdata->capture_clear &&
			((rising_edge && info->pdata->assert_falling_edge) ||
			 (!rising_edge && !info->pdata->assert_falling_edge)))
		pps_event(info->pps, &ts, PPS_CAPTURECLEAR, NULL);

	return IRQ_HANDLED;
}

static int pps_gpio_setup(struct platform_device *pdev)
{
	int ret;
	const struct pps_gpio_platform_data *pdata = pdev->dev.platform_data;

	ret = gpio_request(pdata->gpio_pin, pdata->gpio_label);
	if (ret) {
		pr_warning("failed to request GPIO %u\n", pdata->gpio_pin);
		return -EINVAL;
	}

	ret = gpio_direction_input(pdata->gpio_pin);
	if (ret) {
		pr_warning("failed to set pin direction\n");
		gpio_free(pdata->gpio_pin);
		return -EINVAL;
	}

	return 0;
}

static unsigned long
get_irqf_trigger_flags(const struct pps_gpio_platform_data *pdata)
{
	unsigned long flags = pdata->assert_falling_edge ?
		IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;

	if (pdata->capture_clear) {
		flags |= ((flags & IRQF_TRIGGER_RISING) ?
				IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING);
	}

	return flags;
}

static int pps_gpio_probe(struct platform_device *pdev)
{
	struct pps_gpio_device_data *data;
	int irq;
	int ret;
	int err;
	int pps_default_params;
	const struct pps_gpio_platform_data *pdata = pdev->dev.platform_data;


	/* GPIO setup */
	ret = pps_gpio_setup(pdev);
	if (ret)
		return -EINVAL;

	/* IRQ setup */
	irq = gpio_to_irq(pdata->gpio_pin);
	if (irq < 0) {
		pr_err("failed to map GPIO to IRQ: %d\n", irq);
		err = -EINVAL;
		goto return_error;
	}

	/* allocate space for device info */
	data = devm_kzalloc(&pdev->dev, sizeof(struct pps_gpio_device_data),
			    GFP_KERNEL);
	if (data == NULL) {
		err = -ENOMEM;
		goto return_error;
	}

	/* initialize PPS specific parts of the bookkeeping data structure. */
	data->info.mode = PPS_CAPTUREASSERT | PPS_OFFSETASSERT |
		PPS_ECHOASSERT | PPS_CANWAIT | PPS_TSFMT_TSPEC;
	if (pdata->capture_clear)
		data->info.mode |= PPS_CAPTURECLEAR | PPS_OFFSETCLEAR |
			PPS_ECHOCLEAR;
	data->info.owner = THIS_MODULE;
	snprintf(data->info.name, PPS_MAX_NAME_LEN - 1, "%s.%d",
		 pdev->name, pdev->id);

	/* register PPS source */
	pps_default_params = PPS_CAPTUREASSERT | PPS_OFFSETASSERT;
	if (pdata->capture_clear)
		pps_default_params |= PPS_CAPTURECLEAR | PPS_OFFSETCLEAR;
	data->pps = pps_register_source(&data->info, pps_default_params);
	if (data->pps == NULL) {
		pr_err("failed to register IRQ %d as PPS source\n", irq);
		err = -EINVAL;
		goto return_error;
	}

	data->irq = irq;
	data->pdata = pdata;

	/* register IRQ interrupt handler */
	ret = request_irq(irq, pps_gpio_irq_handler,
			get_irqf_trigger_flags(pdata), data->info.name, data);
	if (ret) {
		pps_unregister_source(data->pps);
		pr_err("failed to acquire IRQ %d\n", irq);
		err = -EINVAL;
		goto return_error;
	}

	platform_set_drvdata(pdev, data);
	dev_info(data->pps->dev, "Registered IRQ %d as PPS source\n", irq);

	return 0;

return_error:
	gpio_free(pdata->gpio_pin);
	return err;
}

static int pps_gpio_remove(struct platform_device *pdev)
{
	struct pps_gpio_device_data *data = platform_get_drvdata(pdev);
	const struct pps_gpio_platform_data *pdata = data->pdata;

	platform_set_drvdata(pdev, NULL);
	free_irq(data->irq, data);
	gpio_free(pdata->gpio_pin);
	pps_unregister_source(data->pps);
	pr_info("removed IRQ %d as PPS source\n", data->irq);
	return 0;
}

static struct platform_driver pps_gpio_driver = {
	.probe		= pps_gpio_probe,
	.remove		= pps_gpio_remove,
	.driver		= {
		.name	= PPS_GPIO_NAME,
		.owner	= THIS_MODULE
	},
};

static int __init pps_gpio_init(void)
{
	int ret = platform_driver_register(&pps_gpio_driver);
	if (ret < 0)
		pr_err("failed to register platform driver\n");
	return ret;
}

static void __exit pps_gpio_exit(void)
{
	platform_driver_unregister(&pps_gpio_driver);
	pr_debug("unregistered platform driver\n");
}

module_init(pps_gpio_init);
module_exit(pps_gpio_exit);

MODULE_AUTHOR("Ricardo Martins <rasm@fe.up.pt>");
MODULE_AUTHOR("James Nuss <jamesnuss@nanometrics.ca>");
MODULE_DESCRIPTION("Use GPIO pin as PPS source");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");
