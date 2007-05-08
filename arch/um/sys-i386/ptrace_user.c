/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include "ptrace_user.h"
/* Grr, asm/user.h includes asm/ptrace.h, so has to follow ptrace_user.h */
#include <asm/user.h>
#include "kern_util.h"
#include "sysdep/thread.h"
#include "user.h"
#include "os.h"
#include "uml-config.h"

int ptrace_getregs(long pid, unsigned long *regs_out)
{
	if (ptrace(PTRACE_GETREGS, pid, 0, regs_out) < 0)
		return -errno;
	return 0;
}

int ptrace_setregs(long pid, unsigned long *regs)
{
	if (ptrace(PTRACE_SETREGS, pid, 0, regs) < 0)
		return -errno;
	return 0;
}

int ptrace_getfpregs(long pid, unsigned long *regs)
{
	if (ptrace(PTRACE_GETFPREGS, pid, 0, regs) < 0)
		return -errno;
	return 0;
}

int ptrace_setfpregs(long pid, unsigned long *regs)
{
	if (ptrace(PTRACE_SETFPREGS, pid, 0, regs) < 0)
		return -errno;
	return 0;
}

#ifdef UML_CONFIG_MODE_TT

static void write_debugregs(int pid, unsigned long *regs)
{
	struct user *dummy;
	int nregs, i;

	dummy = NULL;
	nregs = ARRAY_SIZE(dummy->u_debugreg);
	for(i = 0; i < nregs; i++){
		if((i == 4) || (i == 5)) continue;
		if(ptrace(PTRACE_POKEUSR, pid, &dummy->u_debugreg[i],
			  regs[i]) < 0)
			printk("write_debugregs - ptrace failed on "
			       "register %d, value = 0x%lx, errno = %d\n", i,
			       regs[i], errno);
	}
}

static void read_debugregs(int pid, unsigned long *regs)
{
	struct user *dummy;
	int nregs, i;

	dummy = NULL;
	nregs = ARRAY_SIZE(dummy->u_debugreg);
	for(i = 0; i < nregs; i++){
		regs[i] = ptrace(PTRACE_PEEKUSR, pid,
				 &dummy->u_debugreg[i], 0);
	}
}

/* Accessed only by the tracing thread */
static unsigned long kernel_debugregs[8] = { [ 0 ... 7 ] = 0 };

void arch_enter_kernel(void *task, int pid)
{
	read_debugregs(pid, TASK_DEBUGREGS(task));
	write_debugregs(pid, kernel_debugregs);
}

void arch_leave_kernel(void *task, int pid)
{
	read_debugregs(pid, kernel_debugregs);
	write_debugregs(pid, TASK_DEBUGREGS(task));
}

#ifdef UML_CONFIG_PT_PROXY
/* Accessed only by the tracing thread */
static int debugregs_seq;

/* Only called by the ptrace proxy */
void ptrace_pokeuser(unsigned long addr, unsigned long data)
{
	if((addr < offsetof(struct user, u_debugreg[0])) ||
	   (addr > offsetof(struct user, u_debugreg[7])))
		return;
	addr -= offsetof(struct user, u_debugreg[0]);
	addr = addr >> 2;
	if(kernel_debugregs[addr] == data) return;

	kernel_debugregs[addr] = data;
	debugregs_seq++;
}

static void update_debugregs_cb(void *arg)
{
	int pid = *((int *) arg);

	write_debugregs(pid, kernel_debugregs);
}

/* Optimized out in its header when not defined */
void update_debugregs(int seq)
{
	int me;

	if(seq == debugregs_seq) return;

	me = os_getpid();
	initial_thread_cb(update_debugregs_cb, &me);
}
#endif

#endif
