// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/extcon/extcon-adc-jack.c
 *
 * Analog Jack extcon driver with ADC-based detection capability.
 *
 * Copyright (C) 2016 Samsung Electronics
 * Chanwoo Choi <cw00.choi@samsung.com>
 *
 * Copyright (C) 2012 Samsung Electronics
 * MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * Modified for calling to IIO to get adc by <anish.singh@samsung.com>
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
#include <linux/extcon-provider.h>

/**
 * struct adc_jack_data - internal data for adc_jack device driver
 * @edev:		extcon device.
 * @cable_names:	list of supported cables.
 * @adc_conditions:	list of adc value conditions.
 * @num_conditions:	size of adc_conditions.
 * @irq:		irq number of attach/detach event (0 if not exist).
 * @handling_delay:	interrupt handler will schedule extcon event
 *			handling at handling_delay jiffies.
 * @handler:		extcon event handler called by interrupt handler.
 * @chan:		iio channel being queried.
 */
struct adc_jack_data {
	struct device *dev;
	struct extcon_dev *edev;

	const unsigned int **cable_names;
	struct adc_jack_cond *adc_conditions;
	int num_conditions;

	int irq;
	unsigned long handling_delay; /* in jiffies */
	struct delayed_work handler;

	struct iio_channel *chan;
	bool wakeup_source;
};

static void adc_jack_handler(struct work_struct *work)
{
	struct adc_jack_data *data = container_of(to_delayed_work(work),
			struct adc_jack_data,
			handler);
	struct adc_jack_cond *def;
	int ret, adc_val;
	int i;

	ret = iio_read_channel_raw(data->chan, &adc_val);
	if (ret < 0) {
		dev_err(data->dev, "read channel() error: %d\n", ret);
		return;
	}

	/* Get state from adc value with adc_conditions */
	for (i = 0; i < data->num_conditions; i++) {
		def = &data->adc_conditions[i];
		if (def->min_adc <= adc_val && def->max_adc >= adc_val) {
			extcon_set_state_sync(data->edev, def->id, true);
			return;
		}
	}

	/* Set the detached state if adc value is not included in the range */
	for (i = 0; i < data->num_conditions; i++) {
		def = &data->adc_conditions[i];
		extcon_set_state_sync(data->edev, def->id, false);
	}
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
	struct adc_jack_pdata *pdata = dev_get_platdata(&pdev->dev);
	int i, err = 0;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (!pdata->cable_names) {
		dev_err(&pdev->dev, "error: cable_names not defined.\n");
		return -EINVAL;
	}

	data->dev = &pdev->dev;
	data->edev = devm_extcon_dev_allocate(&pdev->dev, pdata->cable_names);
	if (IS_ERR(data->edev)) {
		dev_err(&pdev->dev, "failed to allocate extcon device\n");
		return -ENOMEM;
	}

	if (!pdata->adc_conditions) {
		dev_err(&pdev->dev, "error: adc_conditions not defined.\n");
		return -EINVAL;
	}
	data->adc_conditions = pdata->adc_conditions;

	/* Check the length of array and set num_conditions */
	for (i = 0; data->adc_conditions[i].id != EXTCON_NONE; i++);
	data->num_conditions = i;

	data->chan = iio_channel_get(&pdev->dev, pdata->consumer_channel);
	if (IS_ERR(data->chan))
		return PTR_ERR(data->chan);

	data->handling_delay = msecs_to_jiffies(pdata->handling_delay_ms);
	data->wakeup_source = pdata->wakeup_source;

	INIT_DEFERRABLE_WORK(&data->handler, adc_jack_handler);

	platform_set_drvdata(pdev, data);

	err = devm_extcon_dev_register(&pdev->dev, data->edev);
	if (err)
		return err;

	data->irq = platform_get_irq(pdev, 0);
	if (data->irq < 0)
		return -ENODEV;

	err = request_any_context_irq(data->irq, adc_jack_irq_thread,
			pdata->irq_flags, pdata->name, data);

	if (err < 0) {
		dev_err(&pdev->dev, "error: irq %d\n", data->irq);
		return err;
	}

	if (data->wakeup_source)
		device_init_wakeup(&pdev->dev, 1);

	adc_jack_handler(&data->handler.work);
	return 0;
}

static int adc_jack_remove(struct platform_device *pdev)
{
	struct adc_jack_data *data = platform_get_drvdata(pdev);

	free_irq(data->irq, data);
	cancel_work_sync(&data->handler.work);
	iio_channel_release(data->chan);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int adc_jack_suspend(struct device *dev)
{
	struct adc_jack_data *data = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&data->handler);
	if (device_may_wakeup(data->dev))
		enable_irq_wake(data->irq);

	return 0;
}

static int adc_jack_resume(struct device *dev)
{
	struct adc_jack_data *data = dev_get_drvdata(dev);

	if (device_may_wakeup(data->dev))
		disable_irq_wake(data->irq);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(adc_jack_pm_ops,
		adc_jack_suspend, adc_jack_resume);

static struct platform_driver adc_jack_driver = {
	.probe          = adc_jack_probe,
	.remove         = adc_jack_remove,
	.driver         = {
		.name   = "adc-jack",
		.pm = &adc_jack_pm_ops,
	},
};

module_platform_driver(adc_jack_driver);

MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_DESCRIPTION("ADC Jack extcon driver");
MODULE_LICENSE("GPL v2");
