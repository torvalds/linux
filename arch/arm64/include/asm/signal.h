/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARM64_ASM_SIGNAL_H
#define __ARM64_ASM_SIGNAL_H

#include <asm/memory.h>
#include <uapi/asm/signal.h>
#include <uapi/asm/siginfo.h>

static inline void __user *arch_untagged_si_addr(void __user *addr,
						 unsigned long sig,
						 unsigned long si_code)
{
	/*
	 * For historical reasons, all bits of the fault address are exposed as
	 * address bits for watchpoint exceptions. New architectures should
	 * handle the tag bits consistently.
	 */
	if (sig == SIGTRAP && si_code == TRAP_BRKPT)
		return addr;

	return untagged_addr(addr);
}
#define arch_untagged_si_addr arch_untagged_si_addr

#endif
