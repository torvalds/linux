/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM_XEN_EVENTS_H
#define _ASM_ARM_XEN_EVENTS_H

#include <asm/ptrace.h>
#include <asm/atomic.h>

enum ipi_vector {
	XEN_PLACEHOLDER_VECTOR,

	/* Xen IPIs go here */
	XEN_NR_IPIS,
};

static inline int xen_irqs_disabled(struct pt_regs *regs)
{
	return raw_irqs_disabled_flags(regs->ARM_cpsr);
}

#define xchg_xen_ulong(ptr, val) atomic64_xchg(container_of((long long*)(ptr),\
							    atomic64_t,	\
							    counter), (val))

/* Rebind event channel is supported by default */
static inline bool xen_support_evtchn_rebind(void)
{
	return true;
}

#endif /* _ASM_ARM_XEN_EVENTS_H */
