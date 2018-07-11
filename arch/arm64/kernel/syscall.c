// SPDX-License-Identifier: GPL-2.0

#include <linux/errno.h>
#include <linux/nospec.h>
#include <linux/ptrace.h>
#include <linux/syscalls.h>

#include <asm/syscall.h>

long compat_arm_syscall(struct pt_regs *regs);

asmlinkage long do_ni_syscall(struct pt_regs *regs)
{
#ifdef CONFIG_COMPAT
	long ret;
	if (is_compat_task()) {
		ret = compat_arm_syscall(regs);
		if (ret != -ENOSYS)
			return ret;
	}
#endif

	return sys_ni_syscall();
}

static long __invoke_syscall(struct pt_regs *regs, syscall_fn_t syscall_fn)
{
	return syscall_fn(regs->regs[0], regs->regs[1], regs->regs[2],
			  regs->regs[3], regs->regs[4], regs->regs[5]);
}

asmlinkage void invoke_syscall(struct pt_regs *regs, unsigned int scno,
			       unsigned int sc_nr,
			       const syscall_fn_t syscall_table[])
{
	long ret;

	if (scno < sc_nr) {
		syscall_fn_t syscall_fn;
		syscall_fn = syscall_table[array_index_nospec(scno, sc_nr)];
		ret = __invoke_syscall(regs, syscall_fn);
	} else {
		ret = do_ni_syscall(regs);
	}

	regs->regs[0] = ret;
}
