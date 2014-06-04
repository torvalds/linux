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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/timeriomem-rng.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/completion.h>

struct timeriomem_rng_private_data {
	void __iomem		*io_base;
	unsigned int		expires;
	unsigned int		period;
	unsigned int		present:1;

	struct timer_list	timer;
	struct completion	completion;

	struct hwrng		timeriomem_rng_ops;
};

#define to_rng_priv(rng) \
		((struct timeriomem_rng_private_data *)rng->priv)

/*
 * have data return 1, however return 0 if we have nothing
 */
static int timeriomem_rng_data_present(struct hwrng *rng, int wait)
{
	struct timeriomem_rng_private_data *priv = to_rng_priv(rng);

	if (!wait || priv->present)
		return priv->present;

	wait_for_completion(&priv->completion);

	return 1;
}

static int timeriomem_rng_data_read(struct hwrng *rng, u32 *data)
{
	struct timeriomem_rng_private_data *priv = to_rng_priv(rng);
	unsigned long cur;
	s32 delay;

	*data = readl(priv->io_base);

	cur = jiffies;

	delay = cur - priv->expires;
	delay = priv->period - (delay % priv->period);

	priv->expires = cur + delay;
	priv->present = 0;

	reinit_completion(&priv->completion);
	mod_timer(&priv->timer, priv->expires);

	return 4;
}

static void timeriomem_rng_trigger(unsigned long data)
{
	struct timeriomem_rng_private_data *priv
			= (struct timeriomem_rng_private_data *)data;

	priv->present = 1;
	complete(&priv->completion);
}

static int timeriomem_rng_probe(struct platform_device *pdev)
{
	struct timeriomem_rng_data *pdata = pdev->dev.platform_data;
	struct timeriomem_rng_private_data *priv;
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
			sizeof(struct timeriomem_rng_private_data), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "failed to allocate device structure.\n");
		return -ENOMEM;
	}

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

	priv->period = usecs_to_jiffies(period);
	if (priv->period < 1) {
		dev_err(&pdev->dev, "period is less than one jiffy\n");
		return -EINVAL;
	}

	priv->expires	= jiffies;
	priv->present	= 1;

	init_completion(&priv->completion);
	complete(&priv->completion);

	setup_timer(&priv->timer, timeriomem_rng_trigger, (unsigned long)priv);

	priv->timeriomem_rng_ops.name		= dev_name(&pdev->dev);
	priv->timeriomem_rng_ops.data_present	= timeriomem_rng_data_present;
	priv->timeriomem_rng_ops.data_read	= timeriomem_rng_data_read;
	priv->timeriomem_rng_ops.priv		= (unsigned long)priv;

	priv->io_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->io_base)) {
		err = PTR_ERR(priv->io_base);
		goto out_timer;
	}

	err = hwrng_register(&priv->timeriomem_rng_ops);
	if (err) {
		dev_err(&pdev->dev, "problem registering\n");
		goto out_timer;
	}

	dev_info(&pdev->dev, "32bits from 0x%p @ %dus\n",
			priv->io_base, period);

	return 0;

out_timer:
	del_timer_sync(&priv->timer);
	return err;
}

static int timeriomem_rng_remove(struct platform_device *pdev)
{
	struct timeriomem_rng_private_data *priv = platform_get_drvdata(pdev);

	hwrng_unregister(&priv->timeriomem_rng_ops);

	del_timer_sync(&priv->timer);

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
		.owner		= THIS_MODULE,
		.of_match_table	= timeriomem_rng_match,
	},
	.probe		= timeriomem_rng_probe,
	.remove		= timeriomem_rng_remove,
};

module_platform_driver(timeriomem_rng_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Clouter <alex@digriz.org.uk>");
MODULE_DESCRIPTION("Timer IOMEM H/W RNG driver");
