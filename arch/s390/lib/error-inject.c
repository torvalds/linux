// SPDX-License-Identifier: GPL-2.0+
#include <asm/ptrace.h>
#include <linux/error-injection.h>
#include <linux/kprobes.h>

void override_function_with_return(struct pt_regs *regs)
{
	/*
	 * Emulate 'br 14'. 'regs' is captured by kprobes on entry to some
	 * kernel function.
	 */
	regs->psw.addr = regs->gprs[14];
}
NOKPROBE_SYMBOL(override_function_with_return);
