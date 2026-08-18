// SPDX-License-Identifier: GPL-2.0
/*
 * P5 specific Machine Check Exception Reporting
 * (C) Copyright 2002 Alan Cox <alan@lxorguk.ukuu.org.uk>
 */
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/smp.h>
#include <linux/hardirq.h>

#include <asm/processor.h>
#include <asm/traps.h>
#include <asm/tlbflush.h>
#include <asm/mce.h>
#include <asm/msr.h>

#include "internal.h"

/* By default disabled */
int mce_p5_enabled __read_mostly;

/* Machine check handler for Pentium class Intel CPUs: */
noinstr void pentium_machine_check(struct pt_regs *regs)
{
	u64 addr, type;

	instrumentation_begin();
	rdmsrq(MSR_IA32_P5_MC_ADDR, addr);
	rdmsrq(MSR_IA32_P5_MC_TYPE, type);

	pr_emerg("CPU#%d: Machine Check Exception:  0x%8X (type 0x%8X).\n",
		 smp_processor_id(), (u32)addr, (u32)type);

	if (type & (1<<5)) {
		pr_emerg("CPU#%d: Possible thermal failure (CPU on fire ?).\n",
			 smp_processor_id());
	}

	add_taint(TAINT_MACHINE_CHECK, LOCKDEP_NOW_UNRELIABLE);
	instrumentation_end();
}

/* Set up machine check reporting for processors with Intel style MCE: */
void intel_p5_mcheck_init(struct cpuinfo_x86 *c)
{
	u64 __maybe_unused q;

	/* Default P5 to off as its often misconnected: */
	if (!mce_p5_enabled)
		return;

	/* Check for MCE support: */
	if (!cpu_has(c, X86_FEATURE_MCE))
		return;

	/* Read registers before enabling: */
	rdmsrq(MSR_IA32_P5_MC_ADDR, q);
	rdmsrq(MSR_IA32_P5_MC_TYPE, q);
	pr_info("Intel old style machine check architecture supported.\n");

	/* Enable MCE: */
	cr4_set_bits(X86_CR4_MCE);
	pr_info("Intel old style machine check reporting enabled on CPU#%d.\n",
		smp_processor_id());
}
