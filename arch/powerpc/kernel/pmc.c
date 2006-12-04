/*
 *  arch/powerpc/kernel/pmc.c
 *
 *  Copyright (C) 2004 David Gibson, IBM Corporation.
 *  Includes code formerly from arch/ppc/kernel/perfmon.c:
 *    Author: Andy Fleming
 *    Copyright (c) 2004 Freescale Semiconductor, Inc
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/module.h>

#include <asm/processor.h>
#include <asm/pmc.h>

#if defined(CONFIG_FSL_BOOKE) && !defined(CONFIG_E200)
static void dummy_perf(struct pt_regs *regs)
{
	unsigned int pmgc0 = mfpmr(PMRN_PMGC0);

	pmgc0 &= ~PMGC0_PMIE;
	mtpmr(PMRN_PMGC0, pmgc0);
}
#elif defined(CONFIG_PPC64) || defined(CONFIG_6xx)

#ifndef MMCR0_PMAO
#define MMCR0_PMAO	0
#endif

/* Ensure exceptions are disabled */
static void dummy_perf(struct pt_regs *regs)
{
	unsigned int mmcr0 = mfspr(SPRN_MMCR0);

	mmcr0 &= ~(MMCR0_PMXE|MMCR0_PMAO);
	mtspr(SPRN_MMCR0, mmcr0);
}
#else
/* Ensure exceptions are disabled */
static void dummy_perf(struct pt_regs *regs)
{
	unsigned int mmcr0 = mfspr(SPRN_MMCR0);

	mmcr0 &= ~(MMCR0_PMXE);
	mtspr(SPRN_MMCR0, mmcr0);
}
#endif

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
	perf_irq = new_perf_irq ? new_perf_irq : dummy_perf;

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

#ifdef CONFIG_PPC64
void power4_enable_pmcs(void)
{
	unsigned long hid0;

	hid0 = mfspr(SPRN_HID0);
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
		"isync" : "=&r" (hid0) : "i" (SPRN_HID0), "0" (hid0):
		"memory");
}
#endif /* CONFIG_PPC64 */
