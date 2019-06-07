/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IRQ_WORK_H
#define _ASM_IRQ_WORK_H

#include <asm/cpufeature.h>

#ifdef CONFIG_X86_LOCAL_APIC
static inline bool arch_irq_work_has_interrupt(void)
{
	return boot_cpu_has(X86_FEATURE_APIC);
}
extern void arch_irq_work_raise(void);
extern __visible void smp_irq_work_interrupt(struct pt_regs *regs);
#else
static inline bool arch_irq_work_has_interrupt(void)
{
	return false;
}
#endif

#endif /* _ASM_IRQ_WORK_H */
