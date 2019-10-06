// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Merrifield legacy RTC initialization file
 *
 * (C) Copyright 2017 Intel Corporation
 *
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#include <linux/init.h>

#include <asm/hw_irq.h>
#include <asm/intel-mid.h>
#include <asm/io_apic.h>
#include <asm/time.h>
#include <asm/x86_init.h>

static int __init mrfld_legacy_rtc_alloc_irq(void)
{
	struct irq_alloc_info info;
	int ret;

	if (!x86_platform.legacy.rtc)
		return -ENODEV;

	ioapic_set_alloc_attr(&info, NUMA_NO_NODE, 1, 0);
	ret = mp_map_gsi_to_irq(RTC_IRQ, IOAPIC_MAP_ALLOC, &info);
	if (ret < 0) {
		pr_info("Failed to allocate RTC interrupt. Disabling RTC\n");
		x86_platform.legacy.rtc = 0;
		return ret;
	}

	return 0;
}

static int __init mrfld_legacy_rtc_init(void)
{
	if (intel_mid_identify_cpu() != INTEL_MID_CPU_CHIP_TANGIER)
		return -ENODEV;

	return mrfld_legacy_rtc_alloc_irq();
}
arch_initcall(mrfld_legacy_rtc_init);
