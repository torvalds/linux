/*
 * arch/xtensa/kernel/syscalls.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Copyright (C) 1995 - 2000 by Ralf Baechle
 *
 * Joe Taylor <joe@tensilica.com, joetylr@yahoo.com>
 * Marc Gauthier <marc@tensilica.com, marc@alumni.uwaterloo.ca>
 * Chris Zankel <chris@zankel.net>
 * Kevin Chea
 *
 */

#define DEBUG	0

#include <linux/linkage.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/utsname.h>
#include <linux/unistd.h>
#include <linux/stringify.h>
#include <linux/syscalls.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/errno.h>
#include <asm/ptrace.h>
#include <asm/signal.h>
#include <asm/uaccess.h>
#include <asm/hardirq.h>
#include <asm/mman.h>
#include <asm/shmparam.h>
#include <asm/page.h>

extern void do_syscall_trace(void);
typedef int (*syscall_t)(void *a0,...);
extern syscall_t sys_call_table[];
extern unsigned char sys_narg_table[];

/*
 * sys_pipe() is the normal C calling standard for creating a pipe. It's not
 * the way unix traditional does this, though.
 */

int sys_pipe(int __user *userfds)
{
	int fd[2];
	int error;

	error = do_pipe(fd);
	if (!error) {
		if (copy_to_user(userfds, fd, 2 * sizeof(int)))
			error = -EFAULT;
	}
	return error;
}

/*
 * Common code for old and new mmaps.
 */
long sys_mmap(unsigned long addr, unsigned long len, unsigned long prot,
	      unsigned long flags, unsigned long fd, unsigned long pgoff)
{
	int error = -EBADF;
	struct file * file = NULL;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			goto out;
	}

	down_write(&current->mm->mmap_sem);
	error = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	up_write(&current->mm->mmap_sem);

	if (file)
		fput(file);
out:
	return error;
}

int sys_clone(struct pt_regs *regs)
{
	unsigned long clone_flags;
	unsigned long newsp;
	int __user *parent_tidptr, *child_tidptr;
	clone_flags = regs->areg[4];
	newsp = regs->areg[3];
	parent_tidptr = (int __user *)regs->areg[5];
	child_tidptr = (int __user *)regs->areg[6];
	if (!newsp)
		newsp = regs->areg[1];
	return do_fork(clone_flags,newsp,regs,0,parent_tidptr,child_tidptr);
}

/*
 * sys_execve() executes a new program.
 */

int sys_execve(struct pt_regs *regs)
{
	int error;
	char * filename;

	filename = getname((char *) (long)regs->areg[5]);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, (char **) (long)regs->areg[3],
	                  (char **) (long)regs->areg[4], regs);
	putname(filename);

out:
	return error;
}

int sys_uname(struct old_utsname * name)
{
	if (name && !copy_to_user(name, utsname(), sizeof (*name)))
		return 0;
	return -EFAULT;
}

/*
 * Build the string table for the builtin "poor man's strace".
 */

#if DEBUG
#define SYSCALL(fun, narg) #fun,
static char *sfnames[] = {
#include "syscalls.h"
};
#undef SYS
#endif

void system_call (struct pt_regs *regs)
{
	syscall_t syscall;
	unsigned long parm0, parm1, parm2, parm3, parm4, parm5;
	int nargs, res;
	unsigned int syscallnr;
	int ps;

#if DEBUG
	int i;
	unsigned long parms[6];
	char *sysname;
#endif

	regs->syscall = regs->areg[2];

	do_syscall_trace();

	/* Have to load after syscall_trace because strace
	 * sometimes changes regs->syscall.
	 */
	syscallnr = regs->syscall;

	parm0 = parm1 = parm2 = parm3 = parm4 = parm5 = 0;

	/* Restore interrupt level to syscall invoker's.
	 * If this were in assembly, we wouldn't disable
	 * interrupts in the first place:
	 */
	local_save_flags (ps);
	local_irq_restore((ps & ~PS_INTLEVEL_MASK) |
			  (regs->ps & PS_INTLEVEL_MASK) );

	if (syscallnr > __NR_Linux_syscalls) {
		regs->areg[2] = -ENOSYS;
		return;
	}

	syscall = sys_call_table[syscallnr];
	nargs = sys_narg_table[syscallnr];

	if (syscall == NULL) {
		regs->areg[2] = -ENOSYS;
		return;
	}

	/* There shouldn't be more than six arguments in the table! */

	if (nargs > 6)
		panic("Internal error - too many syscall arguments (%d)!\n",
		      nargs);

	/* Linux takes system-call arguments in registers.  The ABI
         * and Xtensa software conventions require the system-call
         * number in a2.  If an argument exists in a2, we move it to
         * the next available register.  Note that for improved
         * efficiency, we do NOT shift all parameters down one
         * register to maintain the original order.
	 *
         * At best case (zero arguments), we just write the syscall
         * number to a2.  At worst case (1 to 6 arguments), we move
         * the argument in a2 to the next available register, then
         * write the syscall number to a2.
	 *
         * For clarity, the following truth table enumerates all
         * possibilities.
	 *
         * arguments	syscall number	arg0, arg1, arg2, arg3, arg4, arg5
         * ---------	--------------	----------------------------------
	 *	0	      a2
	 *	1	      a2	a3
	 *	2	      a2	a4,   a3
	 *	3	      a2	a5,   a3,   a4
	 *	4	      a2	a6,   a3,   a4,   a5
	 *	5	      a2	a7,   a3,   a4,   a5,   a6
	 *	6	      a2	a8,   a3,   a4,   a5,   a6,   a7
	 */
	if (nargs) {
		parm0 = regs->areg[nargs+2];
		parm1 = regs->areg[3];
		parm2 = regs->areg[4];
		parm3 = regs->areg[5];
		parm4 = regs->areg[6];
		parm5 = regs->areg[7];
	} else /* nargs == 0 */
		parm0 = (unsigned long) regs;

#if DEBUG
	parms[0] = parm0;
	parms[1] = parm1;
	parms[2] = parm2;
	parms[3] = parm3;
	parms[4] = parm4;
	parms[5] = parm5;

	sysname = sfnames[syscallnr];
	if (strncmp(sysname, "sys_", 4) == 0)
		sysname = sysname + 4;

	printk("\017SYSCALL:I:%x:%d:%s  %s(", regs->pc, current->pid,
	       current->comm, sysname);
	for (i = 0; i < nargs; i++)
		printk((i>0) ? ", %#lx" : "%#lx", parms[i]);
	printk(")\n");
#endif

	res = syscall((void *)parm0, parm1, parm2, parm3, parm4, parm5);

#if DEBUG
	printk("\017SYSCALL:O:%d:%s  %s(",current->pid, current->comm, sysname);
	for (i = 0; i < nargs; i++)
		printk((i>0) ? ", %#lx" : "%#lx", parms[i]);
	if (res < 4096)
		printk(") = %d\n", res);
	else
		printk(") = %#x\n", res);
#endif /* DEBUG */

	regs->areg[2] = res;
	do_syscall_trace();
}

/*
 * Do a system call from kernel instead of calling sys_execve so we
 * end up with proper pt_regs.
 */
int kernel_execve(const char *filename, char *const argv[], char *const envp[])
{
	long __res;
	asm volatile (
		"  mov   a5, %2 \n"
		"  mov   a4, %4 \n"
		"  mov   a3, %3 \n"
		"  movi  a2, %1 \n"
		"  syscall      \n"
		"  mov   %0, a2 \n"
		: "=a" (__res)
		: "i" (__NR_execve), "a" (filename), "a" (argv), "a" (envp)
		: "a2", "a3", "a4", "a5");
	return __res;
}
