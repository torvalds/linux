// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/rethook.h>
#include <linux/kprobes.h>
#include "rethook.h"

void arch_rethook_prepare(struct rethook_node *rh, struct pt_regs *regs, bool mcount)
{
	rh->ret_addr = regs->gprs[14];
	rh->frame = regs->gprs[15];

	/* Replace the return addr with trampoline addr */
	regs->gprs[14] = (unsigned long)&arch_rethook_trampoline;
}
NOKPROBE_SYMBOL(arch_rethook_prepare);

void arch_rethook_fixup_return(struct pt_regs *regs,
			       unsigned long correct_ret_addr)
{
	/* Replace fake return address with real one. */
	regs->gprs[14] = correct_ret_addr;
}
NOKPROBE_SYMBOL(arch_rethook_fixup_return);

/*
 * Called from arch_rethook_trampoline
 */
unsigned long arch_rethook_trampoline_callback(struct pt_regs *regs)
{
	return rethook_trampoline_handler(regs, regs->gprs[15]);
}
NOKPROBE_SYMBOL(arch_rethook_trampoline_callback);

/* assembler function that handles the rethook must not be probed itself */
NOKPROBE_SYMBOL(arch_rethook_trampoline);
