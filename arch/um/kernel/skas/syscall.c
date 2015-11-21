/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <kern_util.h>
#include <sysdep/ptrace.h>
#include <sysdep/syscalls.h>
#include <os.h>

void handle_syscall(struct uml_pt_regs *r)
{
	struct pt_regs *regs = container_of(r, struct pt_regs, regs);
	long result;
	int syscall;

	if (syscall_trace_enter(regs)) {
		result = -ENOSYS;
		goto out;
	}

	syscall = get_syscall(r);

	if ((syscall > __NR_syscall_max) || syscall < 0)
		result = -ENOSYS;
	else
		result = EXECUTE_SYSCALL(syscall, regs);

out:
	PT_REGS_SET_SYSCALL_RETURN(regs, result);

	syscall_trace_leave(regs);
}
