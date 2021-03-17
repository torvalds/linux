/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SYNC_CORE_H
#define _ASM_X86_SYNC_CORE_H

#include <linux/preempt.h>
#include <asm/processor.h>
#include <asm/cpufeature.h>

/*
 * Ensure that a core serializing instruction is issued before returning
 * to user-mode. x86 implements return to user-space through sysexit,
 * sysrel, and sysretq, which are not core serializing.
 */
static inline void sync_core_before_usermode(void)
{
	/* With PTI, we unconditionally serialize before running user code. */
	if (static_cpu_has(X86_FEATURE_PTI))
		return;
	/*
	 * Return from interrupt and NMI is done through iret, which is core
	 * serializing.
	 */
	if (in_irq() || in_nmi())
		return;
	sync_core();
}

#endif /* _ASM_X86_SYNC_CORE_H */
