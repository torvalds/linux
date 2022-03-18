// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic return hook for arm64.
 * Most of the code is copied from arch/arm64/kernel/probes/kprobes.c
 */

#include <linux/kprobes.h>
#include <linux/rethook.h>

/* This is called from arch_rethook_trampoline() */
unsigned long __used arch_rethook_trampoline_callback(struct pt_regs *regs)
{
	return rethook_trampoline_handler(regs, regs->regs[29]);
}
NOKPROBE_SYMBOL(arch_rethook_trampoline_callback);

void arch_rethook_prepare(struct rethook_node *rhn, struct pt_regs *regs, bool mcount)
{
	rhn->ret_addr = regs->regs[30];
	rhn->frame = regs->regs[29];

	/* replace return addr (x30) with trampoline */
	regs->regs[30] = (u64)arch_rethook_trampoline;
}
NOKPROBE_SYMBOL(arch_rethook_prepare);
