// SPDX-License-Identifier: GPL-2.0
/*  linux/arch/sparc/kernel/process.c
 *
 *  Copyright (C) 1995, 2008 David S. Miller (davem@davemloft.net)
 *  Copyright (C) 1996 Eddie C. Dost   (ecd@skynet.be)
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */
#include <linux/elfcore.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/smp.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/cpu.h>

#include <asm/auxio.h>
#include <asm/oplib.h>
#include <linux/uaccess.h>
#include <asm/page.h>
#include <asm/delay.h>
#include <asm/processor.h>
#include <asm/psr.h>
#include <asm/elf.h>
#include <asm/prom.h>
#include <asm/unistd.h>
#include <asm/setup.h>

#include "kernel.h"

/* 
 * Power management idle function 
 * Set in pm platform drivers (apc.c and pmc.c)
 */
void (*sparc_idle)(void);

/* 
 * Power-off handler instantiation for pm.h compliance
 * This is done via auxio, but could be used as a fallback
 * handler when auxio is not present-- unused for now...
 */
void (*pm_power_off)(void) = machine_power_off;
EXPORT_SYMBOL(pm_power_off);

/*
 * sysctl - toggle power-off restriction for serial console 
 * systems in machine_power_off()
 */
int scons_pwroff = 1;

extern void fpsave(unsigned long *, unsigned long *, void *, unsigned long *);

struct task_struct *last_task_used_math = NULL;
struct thread_info *current_set[NR_CPUS];

/* Idle loop support. */
void arch_cpu_idle(void)
{
	if (sparc_idle)
		(*sparc_idle)();
}

/* XXX cli/sti -> local_irq_xxx here, check this works once SMP is fixed. */
void machine_halt(void)
{
	local_irq_enable();
	mdelay(8);
	local_irq_disable();
	prom_halt();
	panic("Halt failed!");
}

void machine_restart(char * cmd)
{
	char *p;
	
	local_irq_enable();
	mdelay(8);
	local_irq_disable();

	p = strchr (reboot_command, '\n');
	if (p) *p = 0;
	if (cmd)
		prom_reboot(cmd);
	if (*reboot_command)
		prom_reboot(reboot_command);
	prom_feval ("reset");
	panic("Reboot failed!");
}

void machine_power_off(void)
{
	if (auxio_power_register &&
	    (!of_node_is_type(of_console_device, "serial") || scons_pwroff)) {
		u8 power_register = sbus_readb(auxio_power_register);
		power_register |= AUXIO_POWER_OFF;
		sbus_writeb(power_register, auxio_power_register);
	}

	machine_halt();
}

void show_regs(struct pt_regs *r)
{
	struct reg_window32 *rw = (struct reg_window32 *) r->u_regs[14];

	show_regs_print_info(KERN_DEFAULT);

        printk("PSR: %08lx PC: %08lx NPC: %08lx Y: %08lx    %s\n",
	       r->psr, r->pc, r->npc, r->y, print_tainted());
	printk("PC: <%pS>\n", (void *) r->pc);
	printk("%%G: %08lx %08lx  %08lx %08lx  %08lx %08lx  %08lx %08lx\n",
	       r->u_regs[0], r->u_regs[1], r->u_regs[2], r->u_regs[3],
	       r->u_regs[4], r->u_regs[5], r->u_regs[6], r->u_regs[7]);
	printk("%%O: %08lx %08lx  %08lx %08lx  %08lx %08lx  %08lx %08lx\n",
	       r->u_regs[8], r->u_regs[9], r->u_regs[10], r->u_regs[11],
	       r->u_regs[12], r->u_regs[13], r->u_regs[14], r->u_regs[15]);
	printk("RPC: <%pS>\n", (void *) r->u_regs[15]);

	printk("%%L: %08lx %08lx  %08lx %08lx  %08lx %08lx  %08lx %08lx\n",
	       rw->locals[0], rw->locals[1], rw->locals[2], rw->locals[3],
	       rw->locals[4], rw->locals[5], rw->locals[6], rw->locals[7]);
	printk("%%I: %08lx %08lx  %08lx %08lx  %08lx %08lx  %08lx %08lx\n",
	       rw->ins[0], rw->ins[1], rw->ins[2], rw->ins[3],
	       rw->ins[4], rw->ins[5], rw->ins[6], rw->ins[7]);
}

/*
 * The show_stack() is external API which we do not use ourselves.
 * The oops is printed in die_if_kernel.
 */
void show_stack(struct task_struct *tsk, unsigned long *_ksp, const char *loglvl)
{
	unsigned long pc, fp;
	unsigned long task_base;
	struct reg_window32 *rw;
	int count = 0;

	if (!tsk)
		tsk = current;

	if (tsk == current && !_ksp)
		__asm__ __volatile__("mov	%%fp, %0" : "=r" (_ksp));

	task_base = (unsigned long) task_stack_page(tsk);
	fp = (unsigned long) _ksp;
	do {
		/* Bogus frame pointer? */
		if (fp < (task_base + sizeof(struct thread_info)) ||
		    fp >= (task_base + (PAGE_SIZE << 1)))
			break;
		rw = (struct reg_window32 *) fp;
		pc = rw->ins[7];
		printk("%s[%08lx : ", loglvl, pc);
		printk("%s%pS ] ", loglvl, (void *) pc);
		fp = rw->ins[6];
	} while (++count < 16);
	printk("%s\n", loglvl);
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(struct task_struct *tsk)
{
#ifndef CONFIG_SMP
	if (last_task_used_math == tsk) {
#else
	if (test_tsk_thread_flag(tsk, TIF_USEDFPU)) {
#endif
		/* Keep process from leaving FPU in a bogon state. */
		put_psr(get_psr() | PSR_EF);
		fpsave(&tsk->thread.float_regs[0], &tsk->thread.fsr,
		       &tsk->thread.fpqueue[0], &tsk->thread.fpqdepth);
#ifndef CONFIG_SMP
		last_task_used_math = NULL;
#else
		clear_ti_thread_flag(task_thread_info(tsk), TIF_USEDFPU);
#endif
	}
}

void flush_thread(void)
{
	current_thread_info()->w_saved = 0;

#ifndef CONFIG_SMP
	if(last_task_used_math == current) {
#else
	if (test_thread_flag(TIF_USEDFPU)) {
#endif
		/* Clean the fpu. */
		put_psr(get_psr() | PSR_EF);
		fpsave(&current->thread.float_regs[0], &current->thread.fsr,
		       &current->thread.fpqueue[0], &current->thread.fpqdepth);
#ifndef CONFIG_SMP
		last_task_used_math = NULL;
#else
		clear_thread_flag(TIF_USEDFPU);
#endif
	}
}

static inline struct sparc_stackf __user *
clone_stackframe(struct sparc_stackf __user *dst,
		 struct sparc_stackf __user *src)
{
	unsigned long size, fp;
	struct sparc_stackf *tmp;
	struct sparc_stackf __user *sp;

	if (get_user(tmp, &src->fp))
		return NULL;

	fp = (unsigned long) tmp;
	size = (fp - ((unsigned long) src));
	fp = (unsigned long) dst;
	sp = (struct sparc_stackf __user *)(fp - size); 

	/* do_fork() grabs the parent semaphore, we must release it
	 * temporarily so we can build the child clone stack frame
	 * without deadlocking.
	 */
	if (__copy_user(sp, src, size))
		sp = NULL;
	else if (put_user(fp, &sp->fp))
		sp = NULL;

	return sp;
}

/* Copy a Sparc thread.  The fork() return value conventions
 * under SunOS are nothing short of bletcherous:
 * Parent -->  %o0 == childs  pid, %o1 == 0
 * Child  -->  %o0 == parents pid, %o1 == 1
 *
 * NOTE: We have a separate fork kpsr/kwim because
 *       the parent could change these values between
 *       sys_fork invocation and when we reach here
 *       if the parent should sleep while trying to
 *       allocate the task_struct and kernel stack in
 *       do_fork().
 * XXX See comment above sys_vfork in sparc64. todo.
 */
extern void ret_from_fork(void);
extern void ret_from_kernel_thread(void);

int copy_thread(struct task_struct *p, const struct kernel_clone_args *args)
{
	unsigned long clone_flags = args->flags;
	unsigned long sp = args->stack;
	unsigned long tls = args->tls;
	struct thread_info *ti = task_thread_info(p);
	struct pt_regs *childregs, *regs = current_pt_regs();
	char *new_stack;

#ifndef CONFIG_SMP
	if(last_task_used_math == current) {
#else
	if (test_thread_flag(TIF_USEDFPU)) {
#endif
		put_psr(get_psr() | PSR_EF);
		fpsave(&p->thread.float_regs[0], &p->thread.fsr,
		       &p->thread.fpqueue[0], &p->thread.fpqdepth);
	}

	/*
	 *  p->thread_info         new_stack   childregs stack bottom
	 *  !                      !           !             !
	 *  V                      V (stk.fr.) V  (pt_regs)  V
	 *  +----- - - - - - ------+===========+=============+
	 */
	new_stack = task_stack_page(p) + THREAD_SIZE;
	new_stack -= STACKFRAME_SZ + TRACEREG_SZ;
	childregs = (struct pt_regs *) (new_stack + STACKFRAME_SZ);

	/*
	 * A new process must start with interrupts disabled, see schedule_tail()
	 * and finish_task_switch(). (If we do not do it and if a timer interrupt
	 * hits before we unlock and attempts to take the rq->lock, we deadlock.)
	 *
	 * Thus, kpsr |= PSR_PIL.
	 */
	ti->ksp = (unsigned long) new_stack;
	p->thread.kregs = childregs;

	if (unlikely(args->fn)) {
		extern int nwindows;
		unsigned long psr;
		memset(new_stack, 0, STACKFRAME_SZ + TRACEREG_SZ);
		ti->kpc = (((unsigned long) ret_from_kernel_thread) - 0x8);
		childregs->u_regs[UREG_G1] = (unsigned long) args->fn;
		childregs->u_regs[UREG_G2] = (unsigned long) args->fn_arg;
		psr = childregs->psr = get_psr();
		ti->kpsr = psr | PSR_PIL;
		ti->kwim = 1 << (((psr & PSR_CWP) + 1) % nwindows);
		return 0;
	}
	memcpy(new_stack, (char *)regs - STACKFRAME_SZ, STACKFRAME_SZ + TRACEREG_SZ);
	childregs->u_regs[UREG_FP] = sp;
	ti->kpc = (((unsigned long) ret_from_fork) - 0x8);
	ti->kpsr = current->thread.fork_kpsr | PSR_PIL;
	ti->kwim = current->thread.fork_kwim;

	if (sp != regs->u_regs[UREG_FP]) {
		struct sparc_stackf __user *childstack;
		struct sparc_stackf __user *parentstack;

		/*
		 * This is a clone() call with supplied user stack.
		 * Set some valid stack frames to give to the child.
		 */
		childstack = (struct sparc_stackf __user *)
			(sp & ~0xfUL);
		parentstack = (struct sparc_stackf __user *)
			regs->u_regs[UREG_FP];

#if 0
		printk("clone: parent stack:\n");
		show_stackframe(parentstack);
#endif

		childstack = clone_stackframe(childstack, parentstack);
		if (!childstack)
			return -EFAULT;

#if 0
		printk("clone: child stack:\n");
		show_stackframe(childstack);
#endif

		childregs->u_regs[UREG_FP] = (unsigned long)childstack;
	}

#ifdef CONFIG_SMP
	/* FPU must be disabled on SMP. */
	childregs->psr &= ~PSR_EF;
	clear_tsk_thread_flag(p, TIF_USEDFPU);
#endif

	/* Set the return value for the child. */
	childregs->u_regs[UREG_I0] = current->pid;
	childregs->u_regs[UREG_I1] = 1;

	/* Set the return value for the parent. */
	regs->u_regs[UREG_I1] = 0;

	if (clone_flags & CLONE_SETTLS)
		childregs->u_regs[UREG_G7] = tls;

	return 0;
}

unsigned long __get_wchan(struct task_struct *task)
{
	unsigned long pc, fp, bias = 0;
	unsigned long task_base = (unsigned long) task;
        unsigned long ret = 0;
	struct reg_window32 *rw;
	int count = 0;

	fp = task_thread_info(task)->ksp + bias;
	do {
		/* Bogus frame pointer? */
		if (fp < (task_base + sizeof(struct thread_info)) ||
		    fp >= (task_base + (2 * PAGE_SIZE)))
			break;
		rw = (struct reg_window32 *) fp;
		pc = rw->ins[7];
		if (!in_sched_functions(pc)) {
			ret = pc;
			goto out;
		}
		fp = rw->ins[6] + bias;
	} while (++count < 16);

out:
	return ret;
}

