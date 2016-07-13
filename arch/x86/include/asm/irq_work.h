#ifndef _ASM_IRQ_WORK_H
#define _ASM_IRQ_WORK_H

#include <asm/cpufeature.h>

static inline bool arch_irq_work_has_interrupt(void)
{
	return boot_cpu_has(X86_FEATURE_APIC);
}

#endif /* _ASM_IRQ_WORK_H */
