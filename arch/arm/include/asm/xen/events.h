#ifndef _ASM_ARM_XEN_EVENTS_H
#define _ASM_ARM_XEN_EVENTS_H

#include <asm/ptrace.h>

enum ipi_vector {
	XEN_PLACEHOLDER_VECTOR,

	/* Xen IPIs go here */
	XEN_NR_IPIS,
};

static inline int xen_irqs_disabled(struct pt_regs *regs)
{
	return raw_irqs_disabled_flags(regs->ARM_cpsr);
}

#endif /* _ASM_ARM_XEN_EVENTS_H */
