/*
 * Support for features of the OLPC XO-1 laptop
 *
 * Copyright (C) 2010 Andres Salomon <dilinger@queued.net>
 * Copyright (C) 2010 One Laptop per Child
 * Copyright (C) 2006 Red Hat, Inc.
 * Copyright (C) 2006 Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>

#include <asm/io.h>
#include <asm/olpc.h>

#define DRV_NAME "olpc-xo1"

/* PMC registers (PMS block) */
#define PM_SCLK		0x10
#define PM_IN_SLPCTL	0x20
#define PM_WKXD		0x34
#define PM_WKD		0x30
#define PM_SSC		0x54

/* PM registers (ACPI block) */
#define PM1_CNT		0x08
#define PM_GPE0_STS	0x18

static unsigned long acpi_base;
static unsigned long pms_base;

static void xo1_power_off(void)
{
	printk(KERN_INFO "OLPC XO-1 power off sequence...\n");

	/* Enable all of these controls with 0 delay */
	outl(0x40000000, pms_base + PM_SCLK);
	outl(0x40000000, pms_base + PM_IN_SLPCTL);
	outl(0x40000000, pms_base + PM_WKXD);
	outl(0x40000000, pms_base + PM_WKD);

	/* Clear status bits (possibly unnecessary) */
	outl(0x0002ffff, pms_base  + PM_SSC);
	outl(0xffffffff, acpi_base + PM_GPE0_STS);

	/* Write SLP_EN bit to start the machinery */
	outl(0x00002000, acpi_base + PM1_CNT);
}

static int __devinit olpc_xo1_probe(struct platform_device *pdev)
{
	struct resource *res;

	/* don't run on non-XOs */
	if (!machine_is_olpc())
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res) {
		dev_err(&pdev->dev, "can't fetch device resource info\n");
		return -EIO;
	}

	if (!request_region(res->start, resource_size(res), DRV_NAME)) {
		dev_err(&pdev->dev, "can't request region\n");
		return -EIO;
	}

	if (strcmp(pdev->name, "cs5535-pms") == 0)
		pms_base = res->start;
	else if (strcmp(pdev->name, "cs5535-acpi") == 0)
		acpi_base = res->start;

	/* If we have both addresses, we can override the poweroff hook */
	if (pms_base && acpi_base) {
		pm_power_off = xo1_power_off;
		printk(KERN_INFO "OLPC XO-1 support registered\n");
	}

	return 0;
}

static int __devexit olpc_xo1_remove(struct platform_device *pdev)
{
	struct resource *r;

	r = platform_get_resource(pdev, IORESOURCE_IO, 0);
	release_region(r->start, resource_size(r));

	if (strcmp(pdev->name, "cs5535-pms") == 0)
		pms_base = 0;
	else if (strcmp(pdev->name, "cs5535-acpi") == 0)
		acpi_base = 0;

	pm_power_off = NULL;
	return 0;
}

static struct platform_driver cs5535_pms_drv = {
	.driver = {
		.name = "cs5535-pms",
		.owner = THIS_MODULE,
	},
	.probe = olpc_xo1_probe,
	.remove = __devexit_p(olpc_xo1_remove),
};

static struct platform_driver cs5535_acpi_drv = {
	.driver = {
		.name = "cs5535-acpi",
		.owner = THIS_MODULE,
	},
	.probe = olpc_xo1_probe,
	.remove = __devexit_p(olpc_xo1_remove),
};

static int __init olpc_xo1_init(void)
{
	int r;

	r = platform_driver_register(&cs5535_pms_drv);
	if (r)
		return r;

	r = platform_driver_register(&cs5535_acpi_drv);
	if (r)
		platform_driver_unregister(&cs5535_pms_drv);

	return r;
}

static void __exit olpc_xo1_exit(void)
{
	platform_driver_unregister(&cs5535_acpi_drv);
	platform_driver_unregister(&cs5535_pms_drv);
}

MODULE_AUTHOR("Daniel Drake <dsd@laptop.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:olpc-xo1");

module_init(olpc_xo1_init);
module_exit(olpc_xo1_exit);
