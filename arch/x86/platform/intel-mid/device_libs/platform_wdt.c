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

#define TANGIER_EXT_TIMER0_MSI 15

static struct platform_device wdt_dev = {
	.name = "intel_mid_wdt",
	.id = -1,
};

static int tangier_probe(struct platform_device *pdev)
{
	int ioapic;
	int irq;
	struct intel_mid_wdt_pdata *pdata = pdev->dev.platform_data;
	struct io_apic_irq_attr irq_attr = { 0 };

	if (!pdata)
		return -EINVAL;

	irq = pdata->irq;
	ioapic = mp_find_ioapic(irq);
	if (ioapic >= 0) {
		int ret;
		irq_attr.ioapic = ioapic;
		irq_attr.ioapic_pin = irq;
		irq_attr.trigger = 1;
		/* irq_attr.polarity = 0; -> Active high */
		ret = io_apic_set_pci_routing(NULL, irq, &irq_attr);
		if (ret)
			return ret;
	} else {
		dev_warn(&pdev->dev, "cannot find interrupt %d in ioapic\n",
			 irq);
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
