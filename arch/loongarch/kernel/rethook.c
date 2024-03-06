// SPDX-License-Identifier: GPL-2.0
/*
 * Generic return hook for LoongArch.
 */

#include <linux/kprobes.h>
#include <linux/rethook.h>
#include "rethook.h"

/* This is called from arch_rethook_trampoline() */
unsigned long __used arch_rethook_trampoline_callback(struct pt_regs *regs)
{
	return rethook_trampoline_handler(regs, 0);
}
NOKPROBE_SYMBOL(arch_rethook_trampoline_callback);

void arch_rethook_prepare(struct rethook_node *rhn, struct pt_regs *regs, bool mcount)
{
	rhn->frame = 0;
	rhn->ret_addr = regs->regs[1];

	/* replace return addr with trampoline */
	regs->regs[1] = (unsigned long)arch_rethook_trampoline;
}
NOKPROBE_SYMBOL(arch_rethook_prepare);

/* ASM function that handles the rethook must not be probed itself */
NOKPROBE_SYMBOL(arch_rethook_trampoline);
