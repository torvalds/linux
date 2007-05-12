/*  $Id: process.c,v 1.161 2002/01/23 11:27:32 davem Exp $
 *  linux/arch/sparc/kernel/process.c
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
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
#include <linux/kallsyms.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/smp.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/init.h>

#include <asm/auxio.h>
#include <asm/oplib.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/delay.h>
#include <asm/processor.h>
#include <asm/psr.h>
#include <asm/elf.h>
#include <asm/unistd.h>

/* 
 * Power management idle function 
 * Set in pm platform drivers (apc.c and pmc.c)
 */
void (*pm_idle)(void);

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

#ifndef CONFIG_SMP

#define SUN4C_FAULT_HIGH 100

/*
 * the idle loop on a Sparc... ;)
 */
void cpu_idle(void)
{
	/* endless idle loop with no priority at all */
	for (;;) {
		if (ARCH_SUN4C_SUN4) {
			static int count = HZ;
			static unsigned long last_jiffies;
			static unsigned long last_faults;
			static unsigned long fps;
			unsigned long now;
			unsigned long faults;

			extern unsigned long sun4c_kernel_faults;
			extern void sun4c_grow_kernel_ring(void);

			local_irq_disable();
			now = jiffies;
			count -= (now - last_jiffies);
			last_jiffies = now;
			if (count < 0) {
				count += HZ;
				faults = sun4c_kernel_faults;
				fps = (fps + (faults - last_faults)) >> 1;
				last_faults = faults;
#if 0
				printk("kernel faults / second = %ld\n", fps);
#endif
				if (fps >= SUN4C_FAULT_HIGH) {
					sun4c_grow_kernel_ring();
				}
			}
			local_irq_enable();
		}

		if (pm_idle) {
			while (!need_resched())
				(*pm_idle)();
		} else {
			while (!need_resched())
				cpu_relax();
		}
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
		check_pgt_cache();
	}
}

#else

/* This is being executed in task 0 'user space'. */
void cpu_idle(void)
{
        set_thread_flag(TIF_POLLING_NRFLAG);
	/* endless idle loop with no priority at all */
	while(1) {
		while (!need_resched())
			cpu_relax();
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
		check_pgt_cache();
	}
}

#endif

extern char reboot_command [];

extern void (*prom_palette)(int);

/* XXX cli/sti -> local_irq_xxx here, check this works once SMP is fixed. */
void machine_halt(void)
{
	local_irq_enable();
	mdelay(8);
	local_irq_disable();
	if (!serial_console && prom_palette)
		prom_palette (1);
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
	if (!serial_console && prom_palette)
		prom_palette (1);
	if (cmd)
		prom_reboot(cmd);
	if (*reboot_command)
		prom_reboot(reboot_command);
	prom_feval ("reset");
	panic("Reboot failed!");
}

void machine_power_off(void)
{
#ifdef CONFIG_SUN_AUXIO
	if (auxio_power_register && (!serial_console || scons_pwroff))
		*auxio_power_register |= AUXIO_POWER_OFF;
#endif
	machine_halt();
}

static DEFINE_SPINLOCK(sparc_backtrace_lock);

void __show_backtrace(unsigned long fp)
{
	struct reg_window *rw;
	unsigned long flags;
	int cpu = smp_processor_id();

	spin_lock_irqsave(&sparc_backtrace_lock, flags);

	rw = (struct reg_window *)fp;
        while(rw && (((unsigned long) rw) >= PAGE_OFFSET) &&
            !(((unsigned long) rw) & 0x7)) {
		printk("CPU[%d]: ARGS[%08lx,%08lx,%08lx,%08lx,%08lx,%08lx] "
		       "FP[%08lx] CALLER[%08lx]: ", cpu,
		       rw->ins[0], rw->ins[1], rw->ins[2], rw->ins[3],
		       rw->ins[4], rw->ins[5],
		       rw->ins[6],
		       rw->ins[7]);
		print_symbol("%s\n", rw->ins[7]);
		rw = (struct reg_window *) rw->ins[6];
	}
	spin_unlock_irqrestore(&sparc_backtrace_lock, flags);
}

#define __SAVE __asm__ __volatile__("save %sp, -0x40, %sp\n\t")
#define __RESTORE __asm__ __volatile__("restore %g0, %g0, %g0\n\t")
#define __GET_FP(fp) __asm__ __volatile__("mov %%i6, %0" : "=r" (fp))

void show_backtrace(void)
{
	unsigned long fp;

	__SAVE; __SAVE; __SAVE; __SAVE;
	__SAVE; __SAVE; __SAVE; __SAVE;
	__RESTORE; __RESTORE; __RESTORE; __RESTORE;
	__RESTORE; __RESTORE; __RESTORE; __RESTORE;

	__GET_FP(fp);

	__show_backtrace(fp);
}

#ifdef CONFIG_SMP
void smp_show_backtrace_all_cpus(void)
{
	xc0((smpfunc_t) show_backtrace);
	show_backtrace();
}
#endif

#if 0
void show_stackframe(struct sparc_stackf *sf)
{
	unsigned long size;
	unsigned long *stk;
	int i;

	printk("l0: %08lx l1: %08lx l2: %08lx l3: %08lx "
	       "l4: %08lx l5: %08lx l6: %08lx l7: %08lx\n",
	       sf->locals[0], sf->locals[1], sf->locals[2], sf->locals[3],
	       sf->locals[4], sf->locals[5], sf->locals[6], sf->locals[7]);
	printk("i0: %08lx i1: %08lx i2: %08lx i3: %08lx "
	       "i4: %08lx i5: %08lx fp: %08lx i7: %08lx\n",
	       sf->ins[0], sf->ins[1], sf->ins[2], sf->ins[3],
	       sf->ins[4], sf->ins[5], (unsigned long)sf->fp, sf->callers_pc);
	printk("sp: %08lx x0: %08lx x1: %08lx x2: %08lx "
	       "x3: %08lx x4: %08lx x5: %08lx xx: %08lx\n",
	       (unsigned long)sf->structptr, sf->xargs[0], sf->xargs[1],
	       sf->xargs[2], sf->xargs[3], sf->xargs[4], sf->xargs[5],
	       sf->xxargs[0]);
	size = ((unsigned long)sf->fp) - ((unsigned long)sf);
	size -= STACKFRAME_SZ;
	stk = (unsigned long *)((unsigned long)sf + STACKFRAME_SZ);
	i = 0;
	do {
		printk("s%d: %08lx\n", i++, *stk++);
	} while ((size -= sizeof(unsigned long)));
}
#endif

void show_regs(struct pt_regs *r)
{
	struct reg_window *rw = (struct reg_window *) r->u_regs[14];

        printk("PSR: %08lx PC: %08lx NPC: %08lx Y: %08lx    %s\n",
	       r->psr, r->pc, r->npc, r->y, print_tainted());
	print_symbol("PC: <%s>\n", r->pc);
	printk("%%G: %08lx %08lx  %08lx %08lx  %08lx %08lx  %08lx %08lx\n",
	       r->u_regs[0], r->u_regs[1], r->u_regs[2], r->u_regs[3],
	       r->u_regs[4], r->u_regs[5], r->u_regs[6], r->u_regs[7]);
	printk("%%O: %08lx %08lx  %08lx %08lx  %08lx %08lx  %08lx %08lx\n",
	       r->u_regs[8], r->u_regs[9], r->u_regs[10], r->u_regs[11],
	       r->u_regs[12], r->u_regs[13], r->u_regs[14], r->u_regs[15]);
	print_symbol("RPC: <%s>\n", r->u_regs[15]);

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
	struct reg_window *rw;
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
		rw = (struct reg_window *) fp;
		pc = rw->ins[7];
		printk("[%08lx : ", pc);
		print_symbol("%s ] ", pc);
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

	/* No new signal delivery by default */
	current->thread.new_signal = 0;
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

	/* Now, this task is no longer a kernel thread. */
	current->thread.current_ds = USER_DS;
	if (current->thread.flags & SPARC_FLAG_KTHREAD) {
		current->thread.flags &= ~SPARC_FLAG_KTHREAD;

		/* We must fixup kregs as well. */
		/* XXX This was not fixed for ti for a while, worked. Unused? */
		current->thread.kregs = (struct pt_regs *)
		    (task_stack_page(current) + (THREAD_SIZE - TRACEREG_SZ));
	}
}

static __inline__ struct sparc_stackf __user *
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

	parent_tid_ptr = regs->u_regs[UREG_I2];
	child_tid_ptr = regs->u_regs[UREG_I4];

	return do_fork(clone_flags, stack_start,
		       regs, stack_size,
		       (int __user *) parent_tid_ptr,
		       (int __user *) child_tid_ptr);
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

int copy_thread(int nr, unsigned long clone_flags, unsigned long sp,
		unsigned long unused,
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
#ifdef CONFIG_SMP
		clear_thread_flag(TIF_USEDFPU);
#endif
	}

	/*
	 *  p->thread_info         new_stack   childregs
	 *  !                      !           !             {if(PSR_PS) }
	 *  V                      V (stk.fr.) V  (pt_regs)  { (stk.fr.) }
	 *  +----- - - - - - ------+===========+============={+==========}+
	 */
	new_stack = task_stack_page(p) + THREAD_SIZE;
	if (regs->psr & PSR_PS)
		new_stack -= STACKFRAME_SZ;
	new_stack -= STACKFRAME_SZ + TRACEREG_SZ;
	memcpy(new_stack, (char *)regs - STACKFRAME_SZ, STACKFRAME_SZ + TRACEREG_SZ);
	childregs = (struct pt_regs *) (new_stack + STACKFRAME_SZ);

	/*
	 * A new process must start with interrupts closed in 2.5,
	 * because this is how Mingo's scheduler works (see schedule_tail
	 * and finish_arch_switch). If we do not do it, a timer interrupt hits
	 * before we unlock, attempts to re-take the rq->lock, and then we die.
	 * Thus, kpsr|=PSR_PIL.
	 */
	ti->ksp = (unsigned long) new_stack;
	ti->kpc = (((unsigned long) ret_from_fork) - 0x8);
	ti->kpsr = current->thread.fork_kpsr | PSR_PIL;
	ti->kwim = current->thread.fork_kwim;

	if(regs->psr & PSR_PS) {
		extern struct pt_regs fake_swapper_regs;

		p->thread.kregs = &fake_swapper_regs;
		new_stack += STACKFRAME_SZ + TRACEREG_SZ;
		childregs->u_regs[UREG_FP] = (unsigned long) new_stack;
		p->thread.flags |= SPARC_FLAG_KTHREAD;
		p->thread.current_ds = KERNEL_DS;
		memcpy(new_stack, (void *)regs->u_regs[UREG_FP], STACKFRAME_SZ);
		childregs->u_regs[UREG_G6] = (unsigned long) ti;
	} else {
		p->thread.kregs = childregs;
		childregs->u_regs[UREG_FP] = sp;
		p->thread.flags &= ~SPARC_FLAG_KTHREAD;
		p->thread.current_ds = USER_DS;

		if (sp != regs->u_regs[UREG_FP]) {
			struct sparc_stackf __user *childstack;
			struct sparc_stackf __user *parentstack;

			/*
			 * This is a clone() call with supplied user stack.
			 * Set some valid stack frames to give to the child.
			 */
			childstack = (struct sparc_stackf __user *)
				(sp & ~0x7UL);
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
	}

#ifdef CONFIG_SMP
	/* FPU must be disabled on SMP. */
	childregs->psr &= ~PSR_EF;
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
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * regs, struct user * dump)
{
	unsigned long first_stack_page;

	dump->magic = SUNOS_CORE_MAGIC;
	dump->len = sizeof(struct user);
	dump->regs.psr = regs->psr;
	dump->regs.pc = regs->pc;
	dump->regs.npc = regs->npc;
	dump->regs.y = regs->y;
	/* fuck me plenty */
	memcpy(&dump->regs.regs[0], &regs->u_regs[1], (sizeof(unsigned long) * 15));
	dump->uexec = current->thread.core_exec;
	dump->u_tsize = (((unsigned long) current->mm->end_code) -
		((unsigned long) current->mm->start_code)) & ~(PAGE_SIZE - 1);
	dump->u_dsize = ((unsigned long) (current->mm->brk + (PAGE_SIZE-1)));
	dump->u_dsize -= dump->u_tsize;
	dump->u_dsize &= ~(PAGE_SIZE - 1);
	first_stack_page = (regs->u_regs[UREG_FP] & ~(PAGE_SIZE - 1));
	dump->u_ssize = (TASK_SIZE - first_stack_page) & ~(PAGE_SIZE - 1);
	memcpy(&dump->fpu.fpstatus.fregs.regs[0], &current->thread.float_regs[0], (sizeof(unsigned long) * 32));
	dump->fpu.fpstatus.fsr = current->thread.fsr;
	dump->fpu.fpstatus.flags = dump->fpu.fpstatus.extra = 0;
	dump->fpu.fpstatus.fpq_count = current->thread.fpqdepth;
	memcpy(&dump->fpu.fpstatus.fpq[0], &current->thread.fpqueue[0],
	       ((sizeof(unsigned long) * 2) * 16));
	dump->sigcode = 0;
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

/*
 * sparc_execve() executes a new program after the asm stub has set
 * things up for us.  This should basically do what I want it to.
 */
asmlinkage int sparc_execve(struct pt_regs *regs)
{
	int error, base = 0;
	char *filename;

	/* Check for indirect call. */
	if(regs->u_regs[UREG_G1] == 0)
		base = 1;

	filename = getname((char __user *)regs->u_regs[base + UREG_I0]);
	error = PTR_ERR(filename);
	if(IS_ERR(filename))
		goto out;
	error = do_execve(filename,
			  (char __user * __user *)regs->u_regs[base + UREG_I1],
			  (char __user * __user *)regs->u_regs[base + UREG_I2],
			  regs);
	putname(filename);
	if (error == 0) {
		task_lock(current);
		current->ptrace &= ~PT_DTRACE;
		task_unlock(current);
	}
out:
	return error;
}

/*
 * This is the mechanism for creating a new kernel thread.
 *
 * NOTE! Only a kernel-only process(ie the swapper or direct descendants
 * who haven't done an "execve()") should use this: it will work within
 * a system call from a "real" process, but the process memory space will
 * not be freed until both the parent and the child have exited.
 */
pid_t kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	long retval;

	__asm__ __volatile__("mov %4, %%g2\n\t"    /* Set aside fn ptr... */
			     "mov %5, %%g3\n\t"    /* and arg. */
			     "mov %1, %%g1\n\t"
			     "mov %2, %%o0\n\t"    /* Clone flags. */
			     "mov 0, %%o1\n\t"     /* usp arg == 0 */
			     "t 0x10\n\t"          /* Linux/Sparc clone(). */
			     "cmp %%o1, 0\n\t"
			     "be 1f\n\t"           /* The parent, just return. */
			     " nop\n\t"            /* Delay slot. */
			     "jmpl %%g2, %%o7\n\t" /* Call the function. */
			     " mov %%g3, %%o0\n\t" /* Get back the arg in delay. */
			     "mov %3, %%g1\n\t"
			     "t 0x10\n\t"          /* Linux/Sparc exit(). */
			     /* Notreached by child. */
			     "1: mov %%o0, %0\n\t" :
			     "=r" (retval) :
			     "i" (__NR_clone), "r" (flags | CLONE_VM | CLONE_UNTRACED),
			     "i" (__NR_exit),  "r" (fn), "r" (arg) :
			     "g1", "g2", "g3", "o0", "o1", "memory", "cc");
	return retval;
}

unsigned long get_wchan(struct task_struct *task)
{
	unsigned long pc, fp, bias = 0;
	unsigned long task_base = (unsigned long) task;
        unsigned long ret = 0;
	struct reg_window *rw;
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
		rw = (struct reg_window *) fp;
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

