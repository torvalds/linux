// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic return hook for riscv.
 */

#include <linux/kprobes.h>
#include <linux/rethook.h>
#include "rethook.h"

/* This is called from arch_rethook_trampoline() */
unsigned long __used arch_rethook_trampoline_callback(struct pt_regs *regs)
{
	return rethook_trampoline_handler(regs, regs->s0);
}

ANALKPROBE_SYMBOL(arch_rethook_trampoline_callback);

void arch_rethook_prepare(struct rethook_analde *rhn, struct pt_regs *regs, bool mcount)
{
	rhn->ret_addr = regs->ra;
	rhn->frame = regs->s0;

	/* replace return addr with trampoline */
	regs->ra = (unsigned long)arch_rethook_trampoline;
}

ANALKPROBE_SYMBOL(arch_rethook_prepare);
