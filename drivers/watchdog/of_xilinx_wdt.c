/*
 * Watchdog Device Driver for Xilinx axi/xps_timebase_wdt
 *
 * (C) Copyright 2013 - 2014 Xilinx, Inc.
 * (C) Copyright 2011 (Alejandro Cabrera <aldaya@gmail.com>)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/watchdog.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>

/* Register offsets for the Wdt device */
#define XWT_TWCSR0_OFFSET   0x0 /* Control/Status Register0 */
#define XWT_TWCSR1_OFFSET   0x4 /* Control/Status Register1 */
#define XWT_TBR_OFFSET      0x8 /* Timebase Register Offset */

/* Control/Status Register Masks  */
#define XWT_CSR0_WRS_MASK   0x00000008 /* Reset status */
#define XWT_CSR0_WDS_MASK   0x00000004 /* Timer state  */
#define XWT_CSR0_EWDT1_MASK 0x00000002 /* Enable bit 1 */

/* Control/Status Register 0/1 bits  */
#define XWT_CSRX_EWDT2_MASK 0x00000001 /* Enable bit 2 */

/* SelfTest constants */
#define XWT_MAX_SELFTEST_LOOP_COUNT 0x00010000
#define XWT_TIMER_FAILED            0xFFFFFFFF

#define WATCHDOG_NAME     "Xilinx Watchdog"
#define PFX WATCHDOG_NAME ": "

struct xwdt_device {
	struct resource  res;
	void __iomem *base;
	u32 wdt_interval;
};

static struct xwdt_device xdev;

static  u32 timeout;
static  u32 control_status_reg;
static  u8  no_timeout;

static  DEFINE_SPINLOCK(spinlock);

static int xilinx_wdt_start(struct watchdog_device *wdd)
{
	spin_lock(&spinlock);

	/* Clean previous status and enable the watchdog timer */
	control_status_reg = ioread32(xdev.base + XWT_TWCSR0_OFFSET);
	control_status_reg |= (XWT_CSR0_WRS_MASK | XWT_CSR0_WDS_MASK);

	iowrite32((control_status_reg | XWT_CSR0_EWDT1_MASK),
				xdev.base + XWT_TWCSR0_OFFSET);

	iowrite32(XWT_CSRX_EWDT2_MASK, xdev.base + XWT_TWCSR1_OFFSET);

	spin_unlock(&spinlock);

	return 0;
}

static int xilinx_wdt_stop(struct watchdog_device *wdd)
{
	spin_lock(&spinlock);

	control_status_reg = ioread32(xdev.base + XWT_TWCSR0_OFFSET);

	iowrite32((control_status_reg & ~XWT_CSR0_EWDT1_MASK),
				xdev.base + XWT_TWCSR0_OFFSET);

	iowrite32(0, xdev.base + XWT_TWCSR1_OFFSET);

	spin_unlock(&spinlock);
	pr_info("Stopped!\n");

	return 0;
}

static int xilinx_wdt_keepalive(struct watchdog_device *wdd)
{
	spin_lock(&spinlock);

	control_status_reg = ioread32(xdev.base + XWT_TWCSR0_OFFSET);
	control_status_reg |= (XWT_CSR0_WRS_MASK | XWT_CSR0_WDS_MASK);
	iowrite32(control_status_reg, xdev.base + XWT_TWCSR0_OFFSET);

	spin_unlock(&spinlock);

	return 0;
}

static const struct watchdog_info xilinx_wdt_ident = {
	.options =  WDIOF_MAGICCLOSE |
		    WDIOF_KEEPALIVEPING,
	.firmware_version =	1,
	.identity =	WATCHDOG_NAME,
};

static const struct watchdog_ops xilinx_wdt_ops = {
	.owner = THIS_MODULE,
	.start = xilinx_wdt_start,
	.stop = xilinx_wdt_stop,
	.ping = xilinx_wdt_keepalive,
};

static struct watchdog_device xilinx_wdt_wdd = {
	.info = &xilinx_wdt_ident,
	.ops = &xilinx_wdt_ops,
};

static u32 xwdt_selftest(void)
{
	int i;
	u32 timer_value1;
	u32 timer_value2;

	spin_lock(&spinlock);

	timer_value1 = ioread32(xdev.base + XWT_TBR_OFFSET);
	timer_value2 = ioread32(xdev.base + XWT_TBR_OFFSET);

	for (i = 0;
		((i <= XWT_MAX_SELFTEST_LOOP_COUNT) &&
			(timer_value2 == timer_value1)); i++) {
		timer_value2 = ioread32(xdev.base + XWT_TBR_OFFSET);
	}

	spin_unlock(&spinlock);

	if (timer_value2 != timer_value1)
		return ~XWT_TIMER_FAILED;
	else
		return XWT_TIMER_FAILED;
}

static int xwdt_probe(struct platform_device *pdev)
{
	int rc;
	u32 *tmptr;
	u32 *pfreq;

	no_timeout = 0;

	pfreq = (u32 *)of_get_property(pdev->dev.of_node,
					"clock-frequency", NULL);

	if (pfreq == NULL) {
		pr_warn("The watchdog clock frequency cannot be obtained!\n");
		no_timeout = 1;
	}

	rc = of_address_to_resource(pdev->dev.of_node, 0, &xdev.res);
	if (rc) {
		pr_warn("invalid address!\n");
		return rc;
	}

	tmptr = (u32 *)of_get_property(pdev->dev.of_node,
					"xlnx,wdt-interval", NULL);
	if (tmptr == NULL) {
		pr_warn("Parameter \"xlnx,wdt-interval\" not found in device tree!\n");
		no_timeout = 1;
	} else {
		xdev.wdt_interval = *tmptr;
	}

	tmptr = (u32 *)of_get_property(pdev->dev.of_node,
					"xlnx,wdt-enable-once", NULL);
	if (tmptr == NULL) {
		pr_warn("Parameter \"xlnx,wdt-enable-once\" not found in device tree!\n");
		watchdog_set_nowayout(&xilinx_wdt_wdd, true);
	}

/*
 *  Twice of the 2^wdt_interval / freq  because the first wdt overflow is
 *  ignored (interrupt), reset is only generated at second wdt overflow
 */
	if (!no_timeout)
		timeout = 2 * ((1<<xdev.wdt_interval) / *pfreq);

	if (!request_mem_region(xdev.res.start,
			xdev.res.end - xdev.res.start + 1, WATCHDOG_NAME)) {
		rc = -ENXIO;
		pr_err("memory request failure!\n");
		goto err_out;
	}

	xdev.base = ioremap(xdev.res.start, xdev.res.end - xdev.res.start + 1);
	if (xdev.base == NULL) {
		rc = -ENOMEM;
		pr_err("ioremap failure!\n");
		goto release_mem;
	}

	rc = xwdt_selftest();
	if (rc == XWT_TIMER_FAILED) {
		pr_err("SelfTest routine error!\n");
		goto unmap_io;
	}

	rc = watchdog_register_device(&xilinx_wdt_wdd);
	if (rc) {
		pr_err("cannot register watchdog (err=%d)\n", rc);
		goto unmap_io;
	}

	dev_info(&pdev->dev, "Xilinx Watchdog Timer at %p with timeout %ds\n",
		 xdev.base, timeout);

	return 0;

unmap_io:
	iounmap(xdev.base);
release_mem:
	release_mem_region(xdev.res.start, resource_size(&xdev.res));
err_out:
	return rc;
}

static int xwdt_remove(struct platform_device *dev)
{
	watchdog_unregister_device(&xilinx_wdt_wdd);
	iounmap(xdev.base);
	release_mem_region(xdev.res.start, resource_size(&xdev.res));

	return 0;
}

/* Match table for of_platform binding */
static struct of_device_id xwdt_of_match[] = {
	{ .compatible = "xlnx,xps-timebase-wdt-1.00.a", },
	{ .compatible = "xlnx,xps-timebase-wdt-1.01.a", },
	{},
};
MODULE_DEVICE_TABLE(of, xwdt_of_match);

static struct platform_driver xwdt_driver = {
	.probe       = xwdt_probe,
	.remove      = xwdt_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name  = WATCHDOG_NAME,
		.of_match_table = xwdt_of_match,
	},
};

module_platform_driver(xwdt_driver);

MODULE_AUTHOR("Alejandro Cabrera <aldaya@gmail.com>");
MODULE_DESCRIPTION("Xilinx Watchdog driver");
MODULE_LICENSE("GPL v2");
