/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/linkage.h>

asmlinkage void do_notify_resume(struct pt_regs *regs);
asmlinkage void *do_sigreturn(struct pt_regs *regs, struct switch_stack *sw);
asmlinkage void *do_rt_sigreturn(struct pt_regs *regs, struct switch_stack *sw);
