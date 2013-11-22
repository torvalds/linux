/*
 * drivers/extcon/extcon-adc-jack.c
 *
 * Analog Jack extcon driver with ADC-based detection capability.
 *
 * Copyright (C) 2012 Samsung Electronics
 * MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * Modified for calling to IIO to get adc by <anish.singh@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/iio/consumer.h>
#include <linux/extcon/extcon-adc-jack.h>
#include <linux/extcon.h>

/**
 * struct adc_jack_data - internal data for adc_jack device driver
 * @edev        - extcon device.
 * @cable_names - list of supported cables.
 * @num_cables  - size of cable_names.
 * @adc_conditions       - list of adc value conditions.
 * @num_conditions       - size of adc_conditions.
 * @irq         - irq number of attach/detach event (0 if not exist).
 * @handling_delay      - interrupt handler will schedule extcon event
 *                      handling at handling_delay jiffies.
 * @handler     - extcon event handler called by interrupt handler.
 * @chan       - iio channel being queried.
 */
struct adc_jack_data {
	struct extcon_dev edev;

	const char **cable_names;
	int num_cables;
	struct adc_jack_cond *adc_conditions;
	int num_conditions;

	int irq;
	unsigned long handling_delay; /* in jiffies */
	struct delayed_work handler;

	struct iio_channel *chan;
};

static void adc_jack_handler(struct work_struct *work)
{
	struct adc_jack_data *data = container_of(to_delayed_work(work),
			struct adc_jack_data,
			handler);
	u32 state = 0;
	int ret, adc_val;
	int i;

	ret = iio_read_channel_raw(data->chan, &adc_val);
	if (ret < 0) {
		dev_err(data->edev.dev, "read channel() error: %d\n", ret);
		return;
	}

	/* Get state from adc value with adc_conditions */
	for (i = 0; i < data->num_conditions; i++) {
		struct adc_jack_cond *def = &data->adc_conditions[i];
		if (!def->state)
			break;
		if (def->min_adc <= adc_val && def->max_adc >= adc_val) {
			state = def->state;
			break;
		}
	}
	/* if no def has met, it means state = 0 (no cables attached) */

	extcon_set_state(&data->edev, state);
}

static irqreturn_t adc_jack_irq_thread(int irq, void *_data)
{
	struct adc_jack_data *data = _data;

	queue_delayed_work(system_power_efficient_wq,
			   &data->handler, data->handling_delay);
	return IRQ_HANDLED;
}

static int adc_jack_probe(struct platform_device *pdev)
{
	struct adc_jack_data *data;
	struct adc_jack_pdata *pdata = pdev->dev.platform_data;
	int i, err = 0;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->edev.name = pdata->name;

	if (!pdata->cable_names) {
		err = -EINVAL;
		dev_err(&pdev->dev, "error: cable_names not defined.\n");
		goto out;
	}

	data->edev.supported_cable = pdata->cable_names;

	/* Check the length of array and set num_cables */
	for (i = 0; data->edev.supported_cable[i]; i++)
		;
	if (i == 0 || i > SUPPORTED_CABLE_MAX) {
		err = -EINVAL;
		dev_err(&pdev->dev, "error: pdata->cable_names size = %d\n",
				i - 1);
		goto out;
	}
	data->num_cables = i;

	if (!pdata->adc_conditions ||
			!pdata->adc_conditions[0].state) {
		err = -EINVAL;
		dev_err(&pdev->dev, "error: adc_conditions not defined.\n");
		goto out;
	}
	data->adc_conditions = pdata->adc_conditions;

	/* Check the length of array and set num_conditions */
	for (i = 0; data->adc_conditions[i].state; i++)
		;
	data->num_conditions = i;

	data->chan = iio_channel_get(&pdev->dev, pdata->consumer_channel);
	if (IS_ERR(data->chan)) {
		err = PTR_ERR(data->chan);
		goto out;
	}

	data->handling_delay = msecs_to_jiffies(pdata->handling_delay_ms);

	INIT_DEFERRABLE_WORK(&data->handler, adc_jack_handler);

	platform_set_drvdata(pdev, data);

	err = extcon_dev_register(&data->edev, &pdev->dev);
	if (err)
		goto out;

	data->irq = platform_get_irq(pdev, 0);
	if (!data->irq) {
		dev_err(&pdev->dev, "platform_get_irq failed\n");
		err = -ENODEV;
		goto err_irq;
	}

	err = request_any_context_irq(data->irq, adc_jack_irq_thread,
			pdata->irq_flags, pdata->name, data);

	if (err < 0) {
		dev_err(&pdev->dev, "error: irq %d\n", data->irq);
		goto err_irq;
	}

	return 0;

err_irq:
	extcon_dev_unregister(&data->edev);
out:
	return err;
}

static int adc_jack_remove(struct platform_device *pdev)
{
	struct adc_jack_data *data = platform_get_drvdata(pdev);

	free_irq(data->irq, data);
	cancel_work_sync(&data->handler.work);
	extcon_dev_unregister(&data->edev);

	return 0;
}

static struct platform_driver adc_jack_driver = {
	.probe          = adc_jack_probe,
	.remove         = adc_jack_remove,
	.driver         = {
		.name   = "adc-jack",
		.owner  = THIS_MODULE,
	},
};

module_platform_driver(adc_jack_driver);

MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_DESCRIPTION("ADC Jack extcon driver");
MODULE_LICENSE("GPL v2");
