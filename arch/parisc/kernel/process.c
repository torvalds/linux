/*
 *    PARISC Architecture-dependent parts of process handling
 *    based on the work for i386
 *
 *    Copyright (C) 1999-2003 Matthew Wilcox <willy at parisc-linux.org>
 *    Copyright (C) 2000 Martin K Petersen <mkp at mkp.net>
 *    Copyright (C) 2000 John Marvin <jsm at parisc-linux.org>
 *    Copyright (C) 2000 David Huggins-Daines <dhd with pobox.org>
 *    Copyright (C) 2000-2003 Paul Bame <bame at parisc-linux.org>
 *    Copyright (C) 2000 Philipp Rumpf <prumpf with tux.org>
 *    Copyright (C) 2000 David Kennedy <dkennedy with linuxcare.com>
 *    Copyright (C) 2000 Richard Hirst <rhirst with parisc-linux.org>
 *    Copyright (C) 2000 Grant Grundler <grundler with parisc-linux.org>
 *    Copyright (C) 2001 Alan Modra <amodra at parisc-linux.org>
 *    Copyright (C) 2001-2002 Ryan Bradetich <rbrad at parisc-linux.org>
 *    Copyright (C) 2001-2007 Helge Deller <deller at parisc-linux.org>
 *    Copyright (C) 2002 Randolph Chung <tausq with parisc-linux.org>
 *
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdarg.h>

#include <linux/elf.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/personality.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/kallsyms.h>

#include <asm/io.h>
#include <asm/asm-offsets.h>
#include <asm/pdc.h>
#include <asm/pdc_chassis.h>
#include <asm/pgalloc.h>
#include <asm/uaccess.h>
#include <asm/unwind.h>

/*
 * The idle thread. There's no useful work to be
 * done, so just try to conserve power and have a
 * low exit latency (ie sit in a loop waiting for
 * somebody to say that they'd like to reschedule)
 */
void cpu_idle(void)
{
	set_thread_flag(TIF_POLLING_NRFLAG);

	/* endless idle loop with no priority at all */
	while (1) {
		while (!need_resched())
			barrier();
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
		check_pgt_cache();
	}
}


#define COMMAND_GLOBAL  F_EXTEND(0xfffe0030)
#define CMD_RESET       5       /* reset any module */

/*
** The Wright Brothers and Gecko systems have a H/W problem
** (Lasi...'nuf said) may cause a broadcast reset to lockup
** the system. An HVERSION dependent PDC call was developed
** to perform a "safe", platform specific broadcast reset instead
** of kludging up all the code.
**
** Older machines which do not implement PDC_BROADCAST_RESET will
** return (with an error) and the regular broadcast reset can be
** issued. Obviously, if the PDC does implement PDC_BROADCAST_RESET
** the PDC call will not return (the system will be reset).
*/
void machine_restart(char *cmd)
{
#ifdef FASTBOOT_SELFTEST_SUPPORT
	/*
	 ** If user has modified the Firmware Selftest Bitmap,
	 ** run the tests specified in the bitmap after the
	 ** system is rebooted w/PDC_DO_RESET.
	 **
	 ** ftc_bitmap = 0x1AUL "Skip destructive memory tests"
	 **
	 ** Using "directed resets" at each processor with the MEM_TOC
	 ** vector cleared will also avoid running destructive
	 ** memory self tests. (Not implemented yet)
	 */
	if (ftc_bitmap) {
		pdc_do_firm_test_reset(ftc_bitmap);
	}
#endif
	/* set up a new led state on systems shipped with a LED State panel */
	pdc_chassis_send_status(PDC_CHASSIS_DIRECT_SHUTDOWN);
	
	/* "Normal" system reset */
	pdc_do_reset();

	/* Nope...box should reset with just CMD_RESET now */
	gsc_writel(CMD_RESET, COMMAND_GLOBAL);

	/* Wait for RESET to lay us to rest. */
	while (1) ;

}

void machine_halt(void)
{
	/*
	** The LED/ChassisCodes are updated by the led_halt()
	** function, called by the reboot notifier chain.
	*/
}

void (*chassis_power_off)(void);

/*
 * This routine is called from sys_reboot to actually turn off the
 * machine 
 */
void machine_power_off(void)
{
	/* If there is a registered power off handler, call it. */
	if (chassis_power_off)
		chassis_power_off();

	/* Put the soft power button back under hardware control.
	 * If the user had already pressed the power button, the
	 * following call will immediately power off. */
	pdc_soft_power_button(0);
	
	pdc_chassis_send_status(PDC_CHASSIS_DIRECT_SHUTDOWN);
		
	/* It seems we have no way to power the system off via
	 * software. The user has to press the button himself. */

	printk(KERN_EMERG "System shut down completed.\n"
	       KERN_EMERG "Please power this system off now.");
}

void (*pm_power_off)(void) = machine_power_off;
EXPORT_SYMBOL(pm_power_off);

/*
 * Create a kernel thread
 */

extern pid_t __kernel_thread(int (*fn)(void *), void *arg, unsigned long flags);
pid_t kernel_thread(int (*fn)(void *), void *arg, unsigned long flags)
{

	/*
	 * FIXME: Once we are sure we don't need any debug here,
	 *	  kernel_thread can become a #define.
	 */

	return __kernel_thread(fn, arg, flags);
}
EXPORT_SYMBOL(kernel_thread);

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
}

void flush_thread(void)
{
	/* Only needs to handle fpu stuff or perf monitors.
	** REVISIT: several arches implement a "lazy fpu state".
	*/
	set_fs(USER_DS);
}

void release_thread(struct task_struct *dead_task)
{
}

/*
 * Fill in the FPU structure for a core dump.
 */

int dump_fpu (struct pt_regs * regs, elf_fpregset_t *r)
{
	if (regs == NULL)
		return 0;

	memcpy(r, regs->fr, sizeof *r);
	return 1;
}

int dump_task_fpu (struct task_struct *tsk, elf_fpregset_t *r)
{
	memcpy(r, tsk->thread.regs.fr, sizeof(*r));
	return 1;
}

/* Note that "fork()" is implemented in terms of clone, with
   parameters (SIGCHLD, regs->gr[30], regs). */
int
sys_clone(unsigned long clone_flags, unsigned long usp,
	  struct pt_regs *regs)
{
  	/* Arugments from userspace are:
	   r26 = Clone flags.
	   r25 = Child stack.
	   r24 = parent_tidptr.
	   r23 = Is the TLS storage descriptor 
	   r22 = child_tidptr 
	   
	   However, these last 3 args are only examined
	   if the proper flags are set. */
	int __user *child_tidptr;
	int __user *parent_tidptr;

	/* usp must be word aligned.  This also prevents users from
	 * passing in the value 1 (which is the signal for a special
	 * return for a kernel thread) */
	usp = ALIGN(usp, 4);

	/* A zero value for usp means use the current stack */
	if (usp == 0)
	  usp = regs->gr[30];

	if (clone_flags & CLONE_PARENT_SETTID)
	  parent_tidptr = (int __user *)regs->gr[24];
	else
	  parent_tidptr = NULL;
	
	if (clone_flags & (CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID))
	  child_tidptr = (int __user *)regs->gr[22];
	else
	  child_tidptr = NULL;

	return do_fork(clone_flags, usp, regs, 0, parent_tidptr, child_tidptr);
}

int
sys_vfork(struct pt_regs *regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs->gr[30], regs, 0, NULL, NULL);
}

int
copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
	    unsigned long unused,	/* in ia64 this is "user_stack_size" */
	    struct task_struct * p, struct pt_regs * pregs)
{
	struct pt_regs * cregs = &(p->thread.regs);
	void *stack = task_stack_page(p);
	
	/* We have to use void * instead of a function pointer, because
	 * function pointers aren't a pointer to the function on 64-bit.
	 * Make them const so the compiler knows they live in .text */
	extern void * const ret_from_kernel_thread;
	extern void * const child_return;
#ifdef CONFIG_HPUX
	extern void * const hpux_child_return;
#endif

	*cregs = *pregs;

	/* Set the return value for the child.  Note that this is not
           actually restored by the syscall exit path, but we put it
           here for consistency in case of signals. */
	cregs->gr[28] = 0; /* child */

	/*
	 * We need to differentiate between a user fork and a
	 * kernel fork. We can't use user_mode, because the
	 * the syscall path doesn't save iaoq. Right now
	 * We rely on the fact that kernel_thread passes
	 * in zero for usp.
	 */
	if (usp == 1) {
		/* kernel thread */
		cregs->ksp = (unsigned long)stack + THREAD_SZ_ALGN;
		/* Must exit via ret_from_kernel_thread in order
		 * to call schedule_tail()
		 */
		cregs->kpc = (unsigned long) &ret_from_kernel_thread;
		/*
		 * Copy function and argument to be called from
		 * ret_from_kernel_thread.
		 */
#ifdef CONFIG_64BIT
		cregs->gr[27] = pregs->gr[27];
#endif
		cregs->gr[26] = pregs->gr[26];
		cregs->gr[25] = pregs->gr[25];
	} else {
		/* user thread */
		/*
		 * Note that the fork wrappers are responsible
		 * for setting gr[21].
		 */

		/* Use same stack depth as parent */
		cregs->ksp = (unsigned long)stack
			+ (pregs->gr[21] & (THREAD_SIZE - 1));
		cregs->gr[30] = usp;
		if (p->personality == PER_HPUX) {
#ifdef CONFIG_HPUX
			cregs->kpc = (unsigned long) &hpux_child_return;
#else
			BUG();
#endif
		} else {
			cregs->kpc = (unsigned long) &child_return;
		}
		/* Setup thread TLS area from the 4th parameter in clone */
		if (clone_flags & CLONE_SETTLS)
		  cregs->cr27 = pregs->gr[23];
	
	}

	return 0;
}

unsigned long thread_saved_pc(struct task_struct *t)
{
	return t->thread.regs.kpc;
}

/*
 * sys_execve() executes a new program.
 */

asmlinkage int sys_execve(struct pt_regs *regs)
{
	int error;
	char *filename;

	filename = getname((const char __user *) regs->gr[26]);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, (char __user * __user *) regs->gr[25],
		(char __user * __user *) regs->gr[24], regs);
	if (error == 0) {
		task_lock(current);
		current->ptrace &= ~PT_DTRACE;
		task_unlock(current);
	}
	putname(filename);
out:

	return error;
}

extern int __execve(const char *filename, char *const argv[],
		char *const envp[], struct task_struct *task);
int kernel_execve(const char *filename, char *const argv[], char *const envp[])
{
	return __execve(filename, argv, envp, current);
}

unsigned long
get_wchan(struct task_struct *p)
{
	struct unwind_frame_info info;
	unsigned long ip;
	int count = 0;

	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	/*
	 * These bracket the sleeping functions..
	 */

	unwind_frame_init_from_blocked_task(&info, p);
	do {
		if (unwind_once(&info) < 0)
			return 0;
		ip = info.ip;
		if (!in_sched_functions(ip))
			return ip;
	} while (count++ < 16);
	return 0;
}
