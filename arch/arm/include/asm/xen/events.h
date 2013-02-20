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

/*
 * We cannot use xchg because it does not support 8-byte
 * values. However it is safe to use {ldr,dtd}exd directly because all
 * platforms which Xen can run on support those instructions.
 */
static inline xen_ulong_t xchg_xen_ulong(xen_ulong_t *ptr, xen_ulong_t val)
{
	xen_ulong_t oldval;
	unsigned int tmp;

	wmb();
	asm volatile("@ xchg_xen_ulong\n"
		"1:     ldrexd  %0, %H0, [%3]\n"
		"       strexd  %1, %2, %H2, [%3]\n"
		"       teq     %1, #0\n"
		"       bne     1b"
		: "=&r" (oldval), "=&r" (tmp)
		: "r" (val), "r" (ptr)
		: "memory", "cc");
	return oldval;
}

#endif /* _ASM_ARM_XEN_EVENTS_H */
