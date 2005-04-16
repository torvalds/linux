/* 
 * Copyright (C) 2002 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include "linux/sys.h"
#include "linux/ptrace.h"
#include "asm/errno.h"
#include "asm/unistd.h"
#include "asm/ptrace.h"
#include "asm/current.h"
#include "sysdep/syscalls.h"
#include "kern_util.h"

extern syscall_handler_t *sys_call_table[];

long execute_syscall_skas(void *r)
{
	struct pt_regs *regs = r;
	long res;
	int syscall;

	current->thread.nsyscalls++;
	nsyscalls++;
	syscall = UPT_SYSCALL_NR(&regs->regs);

	if((syscall >= NR_syscalls) || (syscall < 0))
		res = -ENOSYS;
	else res = EXECUTE_SYSCALL(syscall, regs);

	return(res);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
