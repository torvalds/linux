/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/seccomp.h>
#include <kern_util.h>
#include <sysdep/ptrace.h>
#include <sysdep/ptrace_user.h>
#include <sysdep/syscalls.h>

void handle_syscall(struct uml_pt_regs *r)
{
	struct pt_regs *regs = container_of(r, struct pt_regs, regs);
	int syscall;

	/* Initialize the syscall number and default return value. */
	UPT_SYSCALL_NR(r) = PT_SYSCALL_NR(r->gp);
	PT_REGS_SET_SYSCALL_RETURN(regs, -ENOSYS);

	if (syscall_trace_enter(regs))
		return;

	/* Do the seccomp check after ptrace; failures should be fast. */
	if (secure_computing(NULL) == -1)
		return;

	/* Update the syscall number after orig_ax has potentially been updated
	 * with ptrace.
	 */
	UPT_SYSCALL_NR(r) = PT_SYSCALL_NR(r->gp);
	syscall = UPT_SYSCALL_NR(r);

	if (syscall >= 0 && syscall <= __NR_syscall_max)
		PT_REGS_SET_SYSCALL_RETURN(regs,
				EXECUTE_SYSCALL(syscall, regs));

	syscall_trace_leave(regs);
}
