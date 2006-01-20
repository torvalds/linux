/**********************************************************************
sysdep.c

Copyright (C) 1999 Lars Brinkhoff.  See the file COPYING for licensing
terms and conditions.
**********************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <linux/unistd.h>
#include "ptrace_user.h"
#include "user_util.h"
#include "user.h"
#include "os.h"

int get_syscall(pid_t pid, long *arg1, long *arg2, long *arg3, long *arg4, 
		long *arg5)
{
	*arg1 = ptrace(PTRACE_PEEKUSR, pid, PT_SYSCALL_ARG1_OFFSET, 0);
	*arg2 = ptrace(PTRACE_PEEKUSR, pid, PT_SYSCALL_ARG2_OFFSET, 0);
	*arg3 = ptrace(PTRACE_PEEKUSR, pid, PT_SYSCALL_ARG3_OFFSET, 0);
	*arg4 = ptrace(PTRACE_PEEKUSR, pid, PT_SYSCALL_ARG4_OFFSET, 0);
	*arg5 = ptrace(PTRACE_PEEKUSR, pid, PT_SYSCALL_ARG5_OFFSET, 0);
	return(ptrace(PTRACE_PEEKUSR, pid, PT_SYSCALL_NR_OFFSET, 0));
}

void syscall_cancel(pid_t pid, int result)
{
	if((ptrace(PTRACE_POKEUSR, pid, PT_SYSCALL_NR_OFFSET,
		   __NR_getpid) < 0) ||
	   (ptrace(PTRACE_SYSCALL, pid, 0, 0) < 0) ||
	   (wait_for_stop(pid, SIGTRAP, PTRACE_SYSCALL, NULL) < 0) ||
	   (ptrace(PTRACE_POKEUSR, pid, PT_SYSCALL_RET_OFFSET, result) < 0) ||
	   (ptrace(PTRACE_SYSCALL, pid, 0, 0) < 0))
		printk("ptproxy: couldn't cancel syscall: errno = %d\n", 
		       errno);
}

void syscall_set_result(pid_t pid, long result)
{
	ptrace(PTRACE_POKEUSR, pid, PT_SYSCALL_RET_OFFSET, result);
}

void syscall_continue(pid_t pid)
{
	ptrace(PTRACE_SYSCALL, pid, 0, 0);
}

int syscall_pause(pid_t pid) 
{
	if(ptrace(PTRACE_POKEUSR, pid, PT_SYSCALL_NR_OFFSET, __NR_pause) < 0){
		printk("syscall_change - ptrace failed, errno = %d\n", errno);
		return(-1);
	}
	return(0);
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
