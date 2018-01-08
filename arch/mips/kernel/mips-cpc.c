/*
 * Copyright (C) 2013 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/errno.h>
#include <linux/percpu.h>
#include <linux/spinlock.h>

#include <asm/mips-cps.h>

void __iomem *mips_cpc_base;

static DEFINE_PER_CPU_ALIGNED(spinlock_t, cpc_core_lock);

static DEFINE_PER_CPU_ALIGNED(unsigned long, cpc_core_lock_flags);

phys_addr_t __weak mips_cpc_default_phys_base(void)
{
	return 0;
}

/**
 * mips_cpc_phys_base - retrieve the physical base address of the CPC
 *
 * This function returns the physical base address of the Cluster Power
 * Controller memory mapped registers, or 0 if no Cluster Power Controller
 * is present.
 */
static phys_addr_t mips_cpc_phys_base(void)
{
	unsigned long cpc_base;

	if (!mips_cm_present())
		return 0;

	if (!(read_gcr_cpc_status() & CM_GCR_CPC_STATUS_EX))
		return 0;

	/* If the CPC is already enabled, leave it so */
	cpc_base = read_gcr_cpc_base();
	if (cpc_base & CM_GCR_CPC_BASE_CPCEN)
		return cpc_base & CM_GCR_CPC_BASE_CPCBASE;

	/* Otherwise, use the default address */
	cpc_base = mips_cpc_default_phys_base();
	if (!cpc_base)
		return cpc_base;

	/* Enable the CPC, mapped at the default address */
	write_gcr_cpc_base(cpc_base | CM_GCR_CPC_BASE_CPCEN);
	return cpc_base;
}

int mips_cpc_probe(void)
{
	phys_addr_t addr;
	unsigned int cpu;

	for_each_possible_cpu(cpu)
		spin_lock_init(&per_cpu(cpc_core_lock, cpu));

	addr = mips_cpc_phys_base();
	if (!addr)
		return -ENODEV;

	mips_cpc_base = ioremap_nocache(addr, 0x8000);
	if (!mips_cpc_base)
		return -ENXIO;

	return 0;
}

void mips_cpc_lock_other(unsigned int core)
{
	unsigned int curr_core;

	if (mips_cm_revision() >= CM_REV_CM3)
		/* Systems with CM >= 3 lock the CPC via mips_cm_lock_other */
		return;

	preempt_disable();
	curr_core = cpu_core(&current_cpu_data);
	spin_lock_irqsave(&per_cpu(cpc_core_lock, curr_core),
			  per_cpu(cpc_core_lock_flags, curr_core));
	write_cpc_cl_other(core << __ffs(CPC_Cx_OTHER_CORENUM));

	/*
	 * Ensure the core-other region reflects the appropriate core &
	 * VP before any accesses to it occur.
	 */
	mb();
}

void mips_cpc_unlock_other(void)
{
	unsigned int curr_core;

	if (mips_cm_revision() >= CM_REV_CM3)
		/* Systems with CM >= 3 lock the CPC via mips_cm_lock_other */
		return;

	curr_core = cpu_core(&current_cpu_data);
	spin_unlock_irqrestore(&per_cpu(cpc_core_lock, curr_core),
			       per_cpu(cpc_core_lock_flags, curr_core));
	preempt_enable();
}
