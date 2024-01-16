// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Merrifield watchdog platform device library file
 *
 * (C) Copyright 2014 Intel Corporation
 * Author: David Cohen <david.a.cohen@linux.intel.com>
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/platform_data/intel-mid_wdt.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/intel-mid.h>
#include <asm/io_apic.h>
#include <asm/hw_irq.h>

#define TANGIER_EXT_TIMER0_MSI 12

static struct platform_device wdt_dev = {
	.name = "intel_mid_wdt",
	.id = -1,
};

static int tangier_probe(struct platform_device *pdev)
{
	struct irq_alloc_info info;
	struct intel_mid_wdt_pdata *pdata = pdev->dev.platform_data;
	int gsi = TANGIER_EXT_TIMER0_MSI;
	int irq;

	if (!pdata)
		return -EINVAL;

	/* IOAPIC builds identity mapping between GSI and IRQ on MID */
	ioapic_set_alloc_attr(&info, cpu_to_node(0), 1, 0);
	irq = mp_map_gsi_to_irq(gsi, IOAPIC_MAP_ALLOC, &info);
	if (irq < 0) {
		dev_warn(&pdev->dev, "cannot find interrupt %d in ioapic\n", gsi);
		return irq;
	}

	pdata->irq = irq;
	return 0;
}

static struct intel_mid_wdt_pdata tangier_pdata = {
	.probe = tangier_probe,
};

static const struct x86_cpu_id intel_mid_cpu_ids[] = {
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_SILVERMONT_MID, &tangier_pdata),
	{}
};

static int __init register_mid_wdt(void)
{
	const struct x86_cpu_id *id;

	id = x86_match_cpu(intel_mid_cpu_ids);
	if (!id)
		return -ENODEV;

	wdt_dev.dev.platform_data = (struct intel_mid_wdt_pdata *)id->driver_data;
	return platform_device_register(&wdt_dev);
}
arch_initcall(register_mid_wdt);

static void __exit unregister_mid_wdt(void)
{
	platform_device_unregister(&wdt_dev);
}
__exitcall(unregister_mid_wdt);
