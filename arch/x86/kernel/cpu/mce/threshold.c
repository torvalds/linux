// SPDX-License-Identifier: GPL-2.0
/*
 * Common corrected MCE threshold handler code:
 */
#include <linux/interrupt.h>
#include <linux/kernel.h>

#include <asm/irq_vectors.h>
#include <asm/traps.h>
#include <asm/apic.h>
#include <asm/mce.h>
#include <asm/trace/irq_vectors.h>

#include "internal.h"

static void default_threshold_interrupt(void)
{
	pr_err("Unexpected threshold interrupt at vector %x\n",
		THRESHOLD_APIC_VECTOR);
}

void (*mce_threshold_vector)(void) = default_threshold_interrupt;

asmlinkage __visible void __irq_entry smp_threshold_interrupt(struct pt_regs *regs)
{
	entering_irq();
	trace_threshold_apic_entry(THRESHOLD_APIC_VECTOR);
	inc_irq_stat(irq_threshold_count);
	mce_threshold_vector();
	trace_threshold_apic_exit(THRESHOLD_APIC_VECTOR);
	exiting_ack_irq();
}
