/*
 * drivers/char/hw_random/timeriomem-rng.c
 *
 * Copyright (C) 2009 Alexander Clouter <alex@digriz.org.uk>
 *
 * Derived from drivers/char/hw_random/omap-rng.c
 *   Copyright 2005 (c) MontaVista Software, Inc.
 *   Author: Deepak Saxena <dsaxena@plexity.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Overview:
 *   This driver is useful for platforms that have an IO range that provides
 *   periodic random data from a single IO memory address.  All the platform
 *   has to do is provide the address and 'wait time' that new data becomes
 *   available.
 *
 * TODO: add support for reading sizes other than 32bits and masking
 */

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/timeriomem-rng.h>

struct timeriomem_rng_private {
	void __iomem		*io_base;
	ktime_t			period;
	unsigned int		present:1;

	struct hrtimer		timer;
	struct completion	completion;

	struct hwrng		rng_ops;
};

static int timeriomem_rng_read(struct hwrng *hwrng, void *data,
				size_t max, bool wait)
{
	struct timeriomem_rng_private *priv =
		container_of(hwrng, struct timeriomem_rng_private, rng_ops);
	int retval = 0;
	int period_us = ktime_to_us(priv->period);

	/*
	 * The RNG provides 32-bits per read.  Ensure there is enough space for
	 * at minimum one read.
	 */
	if (max < sizeof(u32))
		return 0;

	/*
	 * There may not have been enough time for new data to be generated
	 * since the last request.  If the caller doesn't want to wait, let them
	 * bail out.  Otherwise, wait for the completion.  If the new data has
	 * already been generated, the completion should already be available.
	 */
	if (!wait && !priv->present)
		return 0;

	wait_for_completion(&priv->completion);

	do {
		/*
		 * After the first read, all additional reads will need to wait
		 * for the RNG to generate new data.  Since the period can have
		 * a wide range of values (1us to 1s have been observed), allow
		 * for 1% tolerance in the sleep time rather than a fixed value.
		 */
		if (retval > 0)
			usleep_range(period_us,
					period_us + min(1, period_us / 100));

		*(u32 *)data = readl(priv->io_base);
		retval += sizeof(u32);
		data += sizeof(u32);
		max -= sizeof(u32);
	} while (wait && max > sizeof(u32));

	/*
	 * Block any new callers until the RNG has had time to generate new
	 * data.
	 */
	priv->present = 0;
	reinit_completion(&priv->completion);
	hrtimer_forward_now(&priv->timer, priv->period);
	hrtimer_restart(&priv->timer);

	return retval;
}

static enum hrtimer_restart timeriomem_rng_trigger(struct hrtimer *timer)
{
	struct timeriomem_rng_private *priv
		= container_of(timer, struct timeriomem_rng_private, timer);

	priv->present = 1;
	complete(&priv->completion);

	return HRTIMER_NORESTART;
}

static int timeriomem_rng_probe(struct platform_device *pdev)
{
	struct timeriomem_rng_data *pdata = pdev->dev.platform_data;
	struct timeriomem_rng_private *priv;
	struct resource *res;
	int err = 0;
	int period;

	if (!pdev->dev.of_node && !pdata) {
		dev_err(&pdev->dev, "timeriomem_rng_data is missing\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENXIO;

	if (res->start % 4 != 0 || resource_size(res) != 4) {
		dev_err(&pdev->dev,
			"address must be four bytes wide and aligned\n");
		return -EINVAL;
	}

	/* Allocate memory for the device structure (and zero it) */
	priv = devm_kzalloc(&pdev->dev,
			sizeof(struct timeriomem_rng_private), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	if (pdev->dev.of_node) {
		int i;

		if (!of_property_read_u32(pdev->dev.of_node,
						"period", &i))
			period = i;
		else {
			dev_err(&pdev->dev, "missing period\n");
			return -EINVAL;
		}
	} else {
		period = pdata->period;
	}

	priv->period = ns_to_ktime(period * NSEC_PER_USEC);
	init_completion(&priv->completion);
	hrtimer_init(&priv->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	priv->timer.function = timeriomem_rng_trigger;

	priv->rng_ops.name = dev_name(&pdev->dev);
	priv->rng_ops.read = timeriomem_rng_read;

	priv->io_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->io_base)) {
		return PTR_ERR(priv->io_base);
	}

	/* Assume random data is already available. */
	priv->present = 1;
	complete(&priv->completion);

	err = hwrng_register(&priv->rng_ops);
	if (err) {
		dev_err(&pdev->dev, "problem registering\n");
		return err;
	}

	dev_info(&pdev->dev, "32bits from 0x%p @ %dus\n",
			priv->io_base, period);

	return 0;
}

static int timeriomem_rng_remove(struct platform_device *pdev)
{
	struct timeriomem_rng_private *priv = platform_get_drvdata(pdev);

	hwrng_unregister(&priv->rng_ops);
	hrtimer_cancel(&priv->timer);

	return 0;
}

static const struct of_device_id timeriomem_rng_match[] = {
	{ .compatible = "timeriomem_rng" },
	{},
};
MODULE_DEVICE_TABLE(of, timeriomem_rng_match);

static struct platform_driver timeriomem_rng_driver = {
	.driver = {
		.name		= "timeriomem_rng",
		.of_match_table	= timeriomem_rng_match,
	},
	.probe		= timeriomem_rng_probe,
	.remove		= timeriomem_rng_remove,
};

module_platform_driver(timeriomem_rng_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Clouter <alex@digriz.org.uk>");
MODULE_DESCRIPTION("Timer IOMEM H/W RNG driver");
