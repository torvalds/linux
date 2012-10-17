/*  linux/arch/sparc/kernel/process.c
 *
 *  Copyright (C) 1995, 2008 David S. Miller (davem@davemloft.net)
 *  Copyright (C) 1996 Eddie C. Dost   (ecd@skynet.be)
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <stdarg.h>

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/smp.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/init.h>
#include <linux/slab.h>

#include <asm/auxio.h>
#include <asm/oplib.h>
#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/delay.h>
#include <asm/processor.h>
#include <asm/psr.h>
#include <asm/elf.h>
#include <asm/prom.h>
#include <asm/unistd.h>
#include <asm/setup.h>

/* 
 * Power management idle function 
 * Set in pm platform drivers (apc.c and pmc.c)
 */
void (*pm_idle)(void);
EXPORT_SYMBOL(pm_idle);

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

/*
 * the idle loop on a Sparc... ;)
 */
void cpu_idle(void)
{
	set_thread_flag(TIF_POLLING_NRFLAG);

	/* endless idle loop with no priority at all */
	for (;;) {
		while (!need_resched()) {
			if (pm_idle)
				(*pm_idle)();
			else
				cpu_relax();
		}
		schedule_preempt_disabled();
	}
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
	    (strcmp(of_console_device->type, "serial") || scons_pwroff))
		*auxio_power_register |= AUXIO_POWER_OFF;
	machine_halt();
}

void show_regs(struct pt_regs *r)
{
	struct reg_window32 *rw = (struct reg_window32 *) r->u_regs[14];

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
 * The show_stack is an external API which we do not use ourselves.
 * The oops is printed in die_if_kernel.
 */
void show_stack(struct task_struct *tsk, unsigned long *_ksp)
{
	unsigned long pc, fp;
	unsigned long task_base;
	struct reg_window32 *rw;
	int count = 0;

	if (tsk != NULL)
		task_base = (unsigned long) task_stack_page(tsk);
	else
		task_base = (unsigned long) current_thread_info();

	fp = (unsigned long) _ksp;
	do {
		/* Bogus frame pointer? */
		if (fp < (task_base + sizeof(struct thread_info)) ||
		    fp >= (task_base + (PAGE_SIZE << 1)))
			break;
		rw = (struct reg_window32 *) fp;
		pc = rw->ins[7];
		printk("[%08lx : ", pc);
		printk("%pS ] ", (void *) pc);
		fp = rw->ins[6];
	} while (++count < 16);
	printk("\n");
}

void dump_stack(void)
{
	unsigned long *ksp;

	__asm__ __volatile__("mov	%%fp, %0"
			     : "=r" (ksp));
	show_stack(current, ksp);
}

EXPORT_SYMBOL(dump_stack);

/*
 * Note: sparc64 has a pretty intricated thread_saved_pc, check it out.
 */
unsigned long thread_saved_pc(struct task_struct *tsk)
{
	return task_thread_info(tsk)->kpc;
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
#ifndef CONFIG_SMP
	if(last_task_used_math == current) {
#else
	if (test_thread_flag(TIF_USEDFPU)) {
#endif
		/* Keep process from leaving FPU in a bogon state. */
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

	/* This task is no longer a kernel thread. */
	if (current->thread.flags & SPARC_FLAG_KTHREAD) {
		current->thread.flags &= ~SPARC_FLAG_KTHREAD;

		/* We must fixup kregs as well. */
		/* XXX This was not fixed for ti for a while, worked. Unused? */
		current->thread.kregs = (struct pt_regs *)
		    (task_stack_page(current) + (THREAD_SIZE - TRACEREG_SZ));
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

asmlinkage int sparc_do_fork(unsigned long clone_flags,
                             unsigned long stack_start,
                             struct pt_regs *regs,
                             unsigned long stack_size)
{
	unsigned long parent_tid_ptr, child_tid_ptr;
	unsigned long orig_i1 = regs->u_regs[UREG_I1];
	long ret;

	parent_tid_ptr = regs->u_regs[UREG_I2];
	child_tid_ptr = regs->u_regs[UREG_I4];

	ret = do_fork(clone_flags, stack_start,
		      regs, stack_size,
		      (int __user *) parent_tid_ptr,
		      (int __user *) child_tid_ptr);

	/* If we get an error and potentially restart the system
	 * call, we're screwed because copy_thread() clobbered
	 * the parent's %o1.  So detect that case and restore it
	 * here.
	 */
	if ((unsigned long)ret >= -ERESTART_RESTARTBLOCK)
		regs->u_regs[UREG_I1] = orig_i1;

	return ret;
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

int copy_thread(unsigned long clone_flags, unsigned long sp,
		unsigned long arg,
		struct task_struct *p, struct pt_regs *regs)
{
	struct thread_info *ti = task_thread_info(p);
	struct pt_regs *childregs;
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
	 * A new process must start with interrupts closed in 2.5,
	 * because this is how Mingo's scheduler works (see schedule_tail
	 * and finish_arch_switch). If we do not do it, a timer interrupt hits
	 * before we unlock, attempts to re-take the rq->lock, and then we die.
	 * Thus, kpsr|=PSR_PIL.
	 */
	ti->ksp = (unsigned long) new_stack;
	p->thread.kregs = childregs;

	if (unlikely(p->flags & PF_KTHREAD)) {
		extern int nwindows;
		unsigned long psr;
		memset(new_stack, 0, STACKFRAME_SZ + TRACEREG_SZ);
		p->thread.flags |= SPARC_FLAG_KTHREAD;
		p->thread.current_ds = KERNEL_DS;
		ti->kpc = (((unsigned long) ret_from_kernel_thread) - 0x8);
		childregs->u_regs[UREG_G1] = sp; /* function */
		childregs->u_regs[UREG_G2] = arg;
		psr = childregs->psr = get_psr();
		ti->kpsr = psr | PSR_PIL;
		ti->kwim = 1 << (((psr & PSR_CWP) + 1) % nwindows);
		return 0;
	}
	memcpy(new_stack, (char *)regs - STACKFRAME_SZ, STACKFRAME_SZ + TRACEREG_SZ);
	childregs->u_regs[UREG_FP] = sp;
	p->thread.flags &= ~SPARC_FLAG_KTHREAD;
	p->thread.current_ds = USER_DS;
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
		childregs->u_regs[UREG_G7] = regs->u_regs[UREG_I3];

	return 0;
}

/*
 * fill in the fpu structure for a core dump.
 */
int dump_fpu (struct pt_regs * regs, elf_fpregset_t * fpregs)
{
	if (used_math()) {
		memset(fpregs, 0, sizeof(*fpregs));
		fpregs->pr_q_entrysize = 8;
		return 1;
	}
#ifdef CONFIG_SMP
	if (test_thread_flag(TIF_USEDFPU)) {
		put_psr(get_psr() | PSR_EF);
		fpsave(&current->thread.float_regs[0], &current->thread.fsr,
		       &current->thread.fpqueue[0], &current->thread.fpqdepth);
		if (regs != NULL) {
			regs->psr &= ~(PSR_EF);
			clear_thread_flag(TIF_USEDFPU);
		}
	}
#else
	if (current == last_task_used_math) {
		put_psr(get_psr() | PSR_EF);
		fpsave(&current->thread.float_regs[0], &current->thread.fsr,
		       &current->thread.fpqueue[0], &current->thread.fpqdepth);
		if (regs != NULL) {
			regs->psr &= ~(PSR_EF);
			last_task_used_math = NULL;
		}
	}
#endif
	memcpy(&fpregs->pr_fr.pr_regs[0],
	       &current->thread.float_regs[0],
	       (sizeof(unsigned long) * 32));
	fpregs->pr_fsr = current->thread.fsr;
	fpregs->pr_qcnt = current->thread.fpqdepth;
	fpregs->pr_q_entrysize = 8;
	fpregs->pr_en = 1;
	if(fpregs->pr_qcnt != 0) {
		memcpy(&fpregs->pr_q[0],
		       &current->thread.fpqueue[0],
		       sizeof(struct fpq) * fpregs->pr_qcnt);
	}
	/* Zero out the rest. */
	memset(&fpregs->pr_q[fpregs->pr_qcnt], 0,
	       sizeof(struct fpq) * (32 - fpregs->pr_qcnt));
	return 1;
}

unsigned long get_wchan(struct task_struct *task)
{
	unsigned long pc, fp, bias = 0;
	unsigned long task_base = (unsigned long) task;
        unsigned long ret = 0;
	struct reg_window32 *rw;
	int count = 0;

	if (!task || task == current ||
            task->state == TASK_RUNNING)
		goto out;

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

