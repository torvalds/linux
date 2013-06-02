/*
 * Industrial I/O - generic interrupt based trigger support
 *
 * Copyright (c) 2008-2013 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>

static LIST_HEAD(iio_interrupt_trigger_list);
static DEFINE_MUTEX(iio_interrupt_trigger_list_lock);

struct iio_interrupt_trigger_info {
	struct mutex in_use;
	unsigned int irq;
};

static irqreturn_t iio_interrupt_trigger_poll(int irq, void *private)
{
	/* Timestamp not currently provided */
	iio_trigger_poll(private, 0);
	return IRQ_HANDLED;
}

static const struct iio_trigger_ops iio_interrupt_trigger_ops = {
	.owner = THIS_MODULE,
};

static int iio_interrupt_trigger_probe(struct platform_device *pdev)
{
	struct iio_interrupt_trigger_info *trig_info;
	struct iio_trigger *trig, *trig2;
	unsigned long irqflags;
	struct resource *irq_res;
	int irq, ret = 0, irq_res_cnt = 0;

	do {
		irq_res = platform_get_resource(pdev,
				IORESOURCE_IRQ, irq_res_cnt);

		if (irq_res == NULL) {
			if (irq_res_cnt == 0)
				dev_err(&pdev->dev, "No IRQs specified");
			break;
		}
		irqflags = (irq_res->flags & IRQF_TRIGGER_MASK) | IRQF_SHARED;

		for (irq = irq_res->start; irq <= irq_res->end; irq++) {

			trig = iio_trigger_alloc("irqtrig%d", irq);
			if (!trig) {
				ret = -ENOMEM;
				goto error_free_completed_registrations;
			}

			trig_info = kzalloc(sizeof(*trig_info), GFP_KERNEL);
			if (!trig_info) {
				ret = -ENOMEM;
				goto error_put_trigger;
			}
			iio_trigger_set_drvdata(trig, trig_info);
			trig_info->irq = irq;
			trig->ops = &iio_interrupt_trigger_ops;
			ret = request_irq(irq, iio_interrupt_trigger_poll,
					  irqflags, trig->name, trig);
			if (ret) {
				dev_err(&pdev->dev,
					"request IRQ-%d failed", irq);
				goto error_free_trig_info;
			}

			ret = iio_trigger_register(trig);
			if (ret)
				goto error_release_irq;

			list_add_tail(&trig->alloc_list,
					&iio_interrupt_trigger_list);
		}

		irq_res_cnt++;
	} while (irq_res != NULL);


	return 0;

/* First clean up the partly allocated trigger */
error_release_irq:
	free_irq(irq, trig);
error_free_trig_info:
	kfree(trig_info);
error_put_trigger:
	iio_trigger_put(trig);
error_free_completed_registrations:
	/* The rest should have been added to the iio_interrupt_trigger_list */
	list_for_each_entry_safe(trig,
				 trig2,
				 &iio_interrupt_trigger_list,
				 alloc_list) {
		trig_info = iio_trigger_get_drvdata(trig);
		free_irq(trig_info->irq, trig);
		kfree(trig_info);
		iio_trigger_unregister(trig);
	}

	return ret;
}

static int iio_interrupt_trigger_remove(struct platform_device *pdev)
{
	struct iio_trigger *trig, *trig2;
	struct iio_interrupt_trigger_info *trig_info;

	mutex_lock(&iio_interrupt_trigger_list_lock);
	list_for_each_entry_safe(trig,
				 trig2,
				 &iio_interrupt_trigger_list,
				 alloc_list) {
		trig_info = iio_trigger_get_drvdata(trig);
		iio_trigger_unregister(trig);
		free_irq(trig_info->irq, trig);
		kfree(trig_info);
		iio_trigger_put(trig);
	}
	mutex_unlock(&iio_interrupt_trigger_list_lock);

	return 0;
}

static struct platform_driver iio_interrupt_trigger_driver = {
	.probe = iio_interrupt_trigger_probe,
	.remove = iio_interrupt_trigger_remove,
	.driver = {
		.name = "iio_interrupt_trigger",
		.owner = THIS_MODULE,
	},
};

module_platform_driver(iio_interrupt_trigger_driver);

MODULE_AUTHOR("Jonathan Cameron <jic23@kernel.org>");
MODULE_DESCRIPTION("Interrupt trigger for the iio subsystem");
MODULE_LICENSE("GPL v2");
