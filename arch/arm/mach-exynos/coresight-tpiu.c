/* Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/err.h>

#include "coresight.h"
#include <mach/sec_debug.h>

#define tpiu_writel(tpiu, val, off)	__raw_writel((val), tpiu.base + off)
#define tpiu_readl(tpiu, off)		__raw_readl(tpiu.base + off)

#define TPIU_SUPP_PORTSZ				(0x000)
#define TPIU_CURR_PORTSZ				(0x004)
#define TPIU_SUPP_TRIGMODES				(0x100)
#define TPIU_TRIG_CNTRVAL				(0x104)
#define TPIU_TRIG_MULT					(0x108)
#define TPIU_SUPP_TESTPATM				(0x200)
#define TPIU_CURR_TESTPATM				(0x204)
#define TPIU_TEST_PATREPCNTR				(0x208)
#define TPIU_FFSR					(0x300)
#define TPIU_FFCR					(0x304)
#define TPIU_FSYNC_CNTR					(0x308)
#define TPIU_EXTCTL_INPORT				(0x400)
#define TPIU_EXTCTL_OUTPORT				(0x404)
#define TPIU_ITTRFLINACK				(0xEE4)
#define TPIU_ITTRFLIN					(0xEE8)
#define TPIU_ITATBDATA0					(0xEEC)
#define TPIU_ITATBCTR2					(0xEF0)
#define TPIU_ITATBCTR1					(0xEF4)
#define TPIU_ITATBCTR0					(0xEF8)


#define TPIU_LOCK()							\
do {									\
	mb();								\
	tpiu_writel(tpiu, 0x0, CS_LAR);					\
} while (0)
#define TPIU_UNLOCK()							\
do {									\
	tpiu_writel(tpiu, CS_UNLOCK_MAGIC, CS_LAR);			\
	mb();								\
} while (0)

struct tpiu_ctx {
	void __iomem	*base;
	bool		enabled;
	struct device	*dev;
};

static struct tpiu_ctx tpiu;

static void __tpiu_disable(void)
{
	TPIU_UNLOCK();

	tpiu_writel(tpiu, 0x3000, TPIU_FFCR);
	tpiu_writel(tpiu, 0x3040, TPIU_FFCR);

	TPIU_LOCK();
}

void tpiu_disable(void)
{
	__tpiu_disable();
	tpiu.enabled = false;
}

static int __devinit tpiu_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res;

	if (!sec_debug_level.en.kernel_fault) {
		pr_info("%s: debug level is low\n",__func__);
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -EINVAL;
		goto err_res;
	}

	tpiu.base = ioremap_nocache(res->start, resource_size(res));
	if (!tpiu.base) {
		ret = -EINVAL;
		goto err_ioremap;
	}

	tpiu.dev = &pdev->dev;

	dev_info(tpiu.dev, "TPIU initialized\n");
	return 0;

err_ioremap:
err_res:
	dev_err(tpiu.dev, "TPIU init failed\n");
	return ret;
}

static int tpiu_remove(struct platform_device *pdev)
{
	if (tpiu.enabled)
		tpiu_disable();
	iounmap(tpiu.base);

	return 0;
}

static struct platform_driver tpiu_driver = {
	.probe          = tpiu_probe,
	.remove         = tpiu_remove,
	.driver         = {
		.name   = "coresight_tpiu",
	},
};

int __init tpiu_init(void)
{
	return platform_driver_register(&tpiu_driver);
}

void tpiu_exit(void)
{
	platform_driver_unregister(&tpiu_driver);
}
