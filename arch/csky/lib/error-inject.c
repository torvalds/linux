// SPDX-License-Identifier: GPL-2.0

#include <linux/error-injection.h>
#include <linux/kprobes.h>

void override_function_with_return(struct pt_regs *regs)
{
	instruction_pointer_set(regs, regs->lr);
}
NOKPROBE_SYMBOL(override_function_with_return);
