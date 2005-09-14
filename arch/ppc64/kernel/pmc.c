/*
 *  linux/arch/ppc64/kernel/pmc.c
 *
 *  Copyright (C) 2004 David Gibson, IBM Corporation.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/module.h>

#include <asm/processor.h>
#include <asm/pmc.h>

/* Ensure exceptions are disabled */
static void dummy_perf(struct pt_regs *regs)
{
	unsigned int mmcr0 = mfspr(SPRN_MMCR0);

	mmcr0 &= ~(MMCR0_PMXE|MMCR0_PMAO);
	mtspr(SPRN_MMCR0, mmcr0);
}

static DEFINE_SPINLOCK(pmc_owner_lock);
static void *pmc_owner_caller; /* mostly for debugging */
perf_irq_t perf_irq = dummy_perf;

int reserve_pmc_hardware(perf_irq_t new_perf_irq)
{
	int err = 0;

	spin_lock(&pmc_owner_lock);

	if (pmc_owner_caller) {
		printk(KERN_WARNING "reserve_pmc_hardware: "
		       "PMC hardware busy (reserved by caller %p)\n",
		       pmc_owner_caller);
		err = -EBUSY;
		goto out;
	}

	pmc_owner_caller = __builtin_return_address(0);
	perf_irq = new_perf_irq ? : dummy_perf;

 out:
	spin_unlock(&pmc_owner_lock);
	return err;
}
EXPORT_SYMBOL_GPL(reserve_pmc_hardware);

void release_pmc_hardware(void)
{
	spin_lock(&pmc_owner_lock);

	WARN_ON(! pmc_owner_caller);

	pmc_owner_caller = NULL;
	perf_irq = dummy_perf;

	spin_unlock(&pmc_owner_lock);
}
EXPORT_SYMBOL_GPL(release_pmc_hardware);

void power4_enable_pmcs(void)
{
	unsigned long hid0;

	hid0 = mfspr(HID0);
	hid0 |= 1UL << (63 - 20);

	/* POWER4 requires the following sequence */
	asm volatile(
		"sync\n"
		"mtspr     %1, %0\n"
		"mfspr     %0, %1\n"
		"mfspr     %0, %1\n"
		"mfspr     %0, %1\n"
		"mfspr     %0, %1\n"
		"mfspr     %0, %1\n"
		"mfspr     %0, %1\n"
		"isync" : "=&r" (hid0) : "i" (HID0), "0" (hid0):
		"memory");
}
