/*
 * platform_wdt.c: Watchdog platform library file
 *
 * (C) Copyright 2014 Intel Corporation
 * Author: David Cohen <david.a.cohen@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/platform_data/intel-mid_wdt.h>
#include <asm/intel-mid.h>
#include <asm/io_apic.h>

#define TANGIER_EXT_TIMER0_MSI 12

static struct platform_device wdt_dev = {
	.name = "intel_mid_wdt",
	.id = -1,
};

static int tangier_probe(struct platform_device *pdev)
{
	int gsi;
	struct irq_alloc_info info;
	struct intel_mid_wdt_pdata *pdata = pdev->dev.platform_data;

	if (!pdata)
		return -EINVAL;

	/* IOAPIC builds identity mapping between GSI and IRQ on MID */
	gsi = pdata->irq;
	ioapic_set_alloc_attr(&info, cpu_to_node(0), 1, 0);
	if (mp_map_gsi_to_irq(gsi, IOAPIC_MAP_ALLOC, &info) <= 0) {
		dev_warn(&pdev->dev, "cannot find interrupt %d in ioapic\n",
			 gsi);
		return -EINVAL;
	}

	return 0;
}

static struct intel_mid_wdt_pdata tangier_pdata = {
	.irq = TANGIER_EXT_TIMER0_MSI,
	.probe = tangier_probe,
};

static int __init register_mid_wdt(void)
{
	if (intel_mid_identify_cpu() == INTEL_MID_CPU_CHIP_TANGIER) {
		wdt_dev.dev.platform_data = &tangier_pdata;
		return platform_device_register(&wdt_dev);
	}

	return -ENODEV;
}

rootfs_initcall(register_mid_wdt);
