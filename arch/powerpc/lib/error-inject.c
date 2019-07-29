// SPDX-License-Identifier: GPL-2.0+

#include <linux/error-injection.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>

void override_function_with_return(struct pt_regs *regs)
{
	/*
	 * Emulate 'blr'. 'regs' represents the state on entry of a predefined
	 * function in the kernel/module, captured on a kprobe. We don't need
	 * to worry about 32-bit userspace on a 64-bit kernel.
	 */
	regs->nip = regs->link;
}
NOKPROBE_SYMBOL(override_function_with_return);
