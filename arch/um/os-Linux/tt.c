/*
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sched.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>
#include <sys/time.h>
#include <sys/ptrace.h>
#include <linux/ptrace.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <asm/ptrace.h>
#include <asm/unistd.h>
#include <asm/page.h>
#include "user_util.h"
#include "kern_util.h"
#include "user.h"
#include "signal_kern.h"
#include "sysdep/ptrace.h"
#include "sysdep/sigcontext.h"
#include "irq_user.h"
#include "ptrace_user.h"
#include "time_user.h"
#include "init.h"
#include "os.h"
#include "uml-config.h"
#include "choose-mode.h"
#include "mode.h"
#include "tempfile.h"

int protect_memory(unsigned long addr, unsigned long len, int r, int w, int x,
		   int must_succeed)
{
	int err;

	err = os_protect_memory((void *) addr, len, r, w, x);
	if(err < 0){
                if(must_succeed)
			panic("protect failed, err = %d", -err);
		else return(err);
	}
	return(0);
}

void kill_child_dead(int pid)
{
	kill(pid, SIGKILL);
	kill(pid, SIGCONT);
	do {
		int n;
		CATCH_EINTR(n = waitpid(pid, NULL, 0));
		if (n > 0)
			kill(pid, SIGCONT);
		else
			break;
	} while(1);
}

/*
 *-------------------------
 * only for tt mode (will be deleted in future...)
 *-------------------------
 */

struct tramp {
	int (*tramp)(void *);
	void *tramp_data;
	unsigned long temp_stack;
	int flags;
	int pid;
};

/* See above for why sigkill is here */

int sigkill = SIGKILL;

int outer_tramp(void *arg)
{
	struct tramp *t;
	int sig = sigkill;

	t = arg;
	t->pid = clone(t->tramp, (void *) t->temp_stack + page_size()/2,
		       t->flags, t->tramp_data);
	if(t->pid > 0) wait_for_stop(t->pid, SIGSTOP, PTRACE_CONT, NULL);
	kill(os_getpid(), sig);
	_exit(0);
}

int start_fork_tramp(void *thread_arg, unsigned long temp_stack,
		     int clone_flags, int (*tramp)(void *))
{
	struct tramp arg;
	unsigned long sp;
	int new_pid, status, err;

	/* The trampoline will run on the temporary stack */
	sp = stack_sp(temp_stack);

	clone_flags |= CLONE_FILES | SIGCHLD;

	arg.tramp = tramp;
	arg.tramp_data = thread_arg;
	arg.temp_stack = temp_stack;
	arg.flags = clone_flags;

	/* Start the process and wait for it to kill itself */
	new_pid = clone(outer_tramp, (void *) sp, clone_flags, &arg);
	if(new_pid < 0)
		return(new_pid);

	CATCH_EINTR(err = waitpid(new_pid, &status, 0));
	if(err < 0)
		panic("Waiting for outer trampoline failed - errno = %d",
		      errno);

	if(!WIFSIGNALED(status) || (WTERMSIG(status) != SIGKILL))
		panic("outer trampoline didn't exit with SIGKILL, "
		      "status = %d", status);

	return(arg.pid);
}

void forward_pending_sigio(int target)
{
	sigset_t sigs;

	if(sigpending(&sigs))
		panic("forward_pending_sigio : sigpending failed");
	if(sigismember(&sigs, SIGIO))
		kill(target, SIGIO);
}

