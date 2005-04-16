/**********************************************************************
wait.c

Copyright (C) 1999 Lars Brinkhoff.  See the file COPYING for licensing
terms and conditions.

**********************************************************************/

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include "ptproxy.h"
#include "sysdep.h"
#include "wait.h"
#include "user_util.h"
#include "ptrace_user.h"
#include "sysdep/ptrace.h"
#include "sysdep/sigcontext.h"

int proxy_wait_return(struct debugger *debugger, pid_t unused)
{
	debugger->waiting = 0;

	if(debugger->debugee->died || (debugger->wait_options & __WCLONE)){
		debugger_cancelled_return(debugger, -ECHILD);
		return(0);
	}

	if(debugger->debugee->zombie && debugger->debugee->event)
		debugger->debugee->died = 1;

	if(debugger->debugee->event){
		debugger->debugee->event = 0;
		ptrace(PTRACE_POKEDATA, debugger->pid,
		       debugger->wait_status_ptr, 
		       debugger->debugee->wait_status);
		/* if (wait4)
		   ptrace (PTRACE_POKEDATA, pid, rusage_ptr, ...); */
		debugger_cancelled_return(debugger, debugger->debugee->pid);
		return(0);
	}

	/* pause will return -EINTR, which happens to be right for wait */
	debugger_normal_return(debugger, -1);
	return(0);
}

int parent_wait_return(struct debugger *debugger, pid_t unused)
{
	return(debugger_normal_return(debugger, -1));
}

int real_wait_return(struct debugger *debugger)
{
	unsigned long ip;
	int pid;

	pid = debugger->pid;

	ip = ptrace(PTRACE_PEEKUSR, pid, PT_IP_OFFSET, 0);
	IP_RESTART_SYSCALL(ip);

	if(ptrace(PTRACE_POKEUSR, pid, PT_IP_OFFSET, ip) < 0)
		tracer_panic("real_wait_return : Failed to restart system "
			     "call, errno = %d\n", errno);

	if((ptrace(PTRACE_SYSCALL, debugger->pid, 0, SIGCHLD) < 0) ||
	   (ptrace(PTRACE_SYSCALL, debugger->pid, 0, 0) < 0) ||
	   (ptrace(PTRACE_SYSCALL, debugger->pid, 0, 0) < 0) ||
	   debugger_normal_return(debugger, -1))
		tracer_panic("real_wait_return : gdb failed to wait, "
			     "errno = %d\n", errno);
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
