/*
 * Intel Merrifield watchdog platform device library file
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
#include <asm/intel_scu_ipc.h>
#include <asm/io_apic.h>

#define TANGIER_EXT_TIMER0_MSI 12

static struct platform_device wdt_dev = {
	.name = "intel_mid_wdt",
	.id = -1,
};

static int tangier_probe(struct platform_device *pdev)
{
	struct irq_alloc_info info;
	struct intel_mid_wdt_pdata *pdata = pdev->dev.platform_data;
	int gsi, irq;

	if (!pdata)
		return -EINVAL;

	/* IOAPIC builds identity mapping between GSI and IRQ on MID */
	gsi = pdata->irq;
	ioapic_set_alloc_attr(&info, cpu_to_node(0), 1, 0);
	irq = mp_map_gsi_to_irq(gsi, IOAPIC_MAP_ALLOC, &info);
	if (irq < 0) {
		dev_warn(&pdev->dev, "cannot find interrupt %d in ioapic\n", gsi);
		return irq;
	}

	return 0;
}

static struct intel_mid_wdt_pdata tangier_pdata = {
	.irq = TANGIER_EXT_TIMER0_MSI,
	.probe = tangier_probe,
};

static int wdt_scu_status_change(struct notifier_block *nb,
				 unsigned long code, void *data)
{
	if (code == SCU_DOWN) {
		platform_device_unregister(&wdt_dev);
		return 0;
	}

	return platform_device_register(&wdt_dev);
}

static struct notifier_block wdt_scu_notifier = {
	.notifier_call	= wdt_scu_status_change,
};

static int __init register_mid_wdt(void)
{
	if (intel_mid_identify_cpu() != INTEL_MID_CPU_CHIP_TANGIER)
		return -ENODEV;

	wdt_dev.dev.platform_data = &tangier_pdata;

	/*
	 * We need to be sure that the SCU IPC is ready before watchdog device
	 * can be registered:
	 */
	intel_scu_notifier_add(&wdt_scu_notifier);

	return 0;
}
arch_initcall(register_mid_wdt);
