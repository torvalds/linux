/* 
 * Copyright (C) 2000 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include "linux/types.h"
#include "linux/utime.h"
#include "linux/sys.h"
#include "linux/ptrace.h"
#include "asm/unistd.h"
#include "asm/ptrace.h"
#include "asm/uaccess.h"
#include "asm/stat.h"
#include "sysdep/syscalls.h"
#include "sysdep/sigcontext.h"
#include "kern_util.h"
#include "syscall.h"

void syscall_handler_tt(int sig, struct pt_regs *regs)
{
	void *sc;
	long result;
	int syscall;
#ifdef CONFIG_SYSCALL_DEBUG
	int index;
  	index = record_syscall_start(syscall);
#endif
	sc = UPT_SC(&regs->regs);
	SC_START_SYSCALL(sc);

	syscall_trace(&regs->regs, 0);

	current->thread.nsyscalls++;
	nsyscalls++;
	syscall = UPT_SYSCALL_NR(&regs->regs);

	if((syscall >= NR_syscalls) || (syscall < 0))
		result = -ENOSYS;
	else result = EXECUTE_SYSCALL(syscall, regs);

	/* regs->sc may have changed while the system call ran (there may
	 * have been an interrupt or segfault), so it needs to be refreshed.
	 */
	UPT_SC(&regs->regs) = sc;

	SC_SET_SYSCALL_RETURN(sc, result);

	syscall_trace(&regs->regs, 1);
#ifdef CONFIG_SYSCALL_DEBUG
  	record_syscall_end(index, result);
#endif
}
