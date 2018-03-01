// SPDX-License-Identifier: GPL-2.0

#include <linux/error-injection.h>
#include <linux/kprobes.h>

asmlinkage void just_return_func(void);

asm(
	".type just_return_func, @function\n"
	"just_return_func:\n"
	"	ret\n"
	".size just_return_func, .-just_return_func\n"
);

void override_function_with_return(struct pt_regs *regs)
{
	regs->ip = (unsigned long)&just_return_func;
}
NOKPROBE_SYMBOL(override_function_with_return);
