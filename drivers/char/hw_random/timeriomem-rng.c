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
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/timeriomem-rng.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/completion.h>

static struct timeriomem_rng_data *timeriomem_rng_data;

static void timeriomem_rng_trigger(unsigned long);
static DEFINE_TIMER(timeriomem_rng_timer, timeriomem_rng_trigger, 0, 0);

/*
 * have data return 1, however return 0 if we have nothing
 */
static int timeriomem_rng_data_present(struct hwrng *rng, int wait)
{
	if (rng->priv == 0)
		return 1;

	if (!wait || timeriomem_rng_data->present)
		return timeriomem_rng_data->present;

	wait_for_completion(&timeriomem_rng_data->completion);

	return 1;
}

static int timeriomem_rng_data_read(struct hwrng *rng, u32 *data)
{
	unsigned long cur;
	s32 delay;

	*data = readl(timeriomem_rng_data->address);

	if (rng->priv != 0) {
		cur = jiffies;

		delay = cur - timeriomem_rng_timer.expires;
		delay = rng->priv - (delay % rng->priv);

		timeriomem_rng_timer.expires = cur + delay;
		timeriomem_rng_data->present = 0;

		init_completion(&timeriomem_rng_data->completion);
		add_timer(&timeriomem_rng_timer);
	}

	return 4;
}

static void timeriomem_rng_trigger(unsigned long dummy)
{
	timeriomem_rng_data->present = 1;
	complete(&timeriomem_rng_data->completion);
}

static struct hwrng timeriomem_rng_ops = {
	.name		= "timeriomem",
	.data_present	= timeriomem_rng_data_present,
	.data_read	= timeriomem_rng_data_read,
	.priv		= 0,
};

static int __devinit timeriomem_rng_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!res)
		return -ENOENT;

	timeriomem_rng_data = pdev->dev.platform_data;

	timeriomem_rng_data->address = ioremap(res->start, resource_size(res));
	if (!timeriomem_rng_data->address)
		return -EIO;

	if (timeriomem_rng_data->period != 0
		&& usecs_to_jiffies(timeriomem_rng_data->period) > 0) {
		timeriomem_rng_timer.expires = jiffies;

		timeriomem_rng_ops.priv = usecs_to_jiffies(
						timeriomem_rng_data->period);
	}
	timeriomem_rng_data->present = 1;

	ret = hwrng_register(&timeriomem_rng_ops);
	if (ret)
		goto failed;

	dev_info(&pdev->dev, "32bits from 0x%p @ %dus\n",
			timeriomem_rng_data->address,
			timeriomem_rng_data->period);

	return 0;

failed:
	dev_err(&pdev->dev, "problem registering\n");
	iounmap(timeriomem_rng_data->address);

	return ret;
}

static int __devexit timeriomem_rng_remove(struct platform_device *pdev)
{
	del_timer_sync(&timeriomem_rng_timer);
	hwrng_unregister(&timeriomem_rng_ops);

	iounmap(timeriomem_rng_data->address);

	return 0;
}

static struct platform_driver timeriomem_rng_driver = {
	.driver = {
		.name		= "timeriomem_rng",
		.owner		= THIS_MODULE,
	},
	.probe		= timeriomem_rng_probe,
	.remove		= __devexit_p(timeriomem_rng_remove),
};

static int __init timeriomem_rng_init(void)
{
	return platform_driver_register(&timeriomem_rng_driver);
}

static void __exit timeriomem_rng_exit(void)
{
	platform_driver_unregister(&timeriomem_rng_driver);
}

module_init(timeriomem_rng_init);
module_exit(timeriomem_rng_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Clouter <alex@digriz.org.uk>");
MODULE_DESCRIPTION("Timer IOMEM H/W RNG driver");
