/*
 *  arch/ppc/kernel/process.c
 *
 *  Derived from "arch/i386/kernel/process.c"
 *    Copyright (C) 1995  Linus Torvalds
 *
 *  Updated and modified by Cort Dougan (cort@cs.nmt.edu) and
 *  Paul Mackerras (paulus@cs.anu.edu.au)
 *
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/elf.h>
#include <linux/init.h>
#include <linux/prctl.h>
#include <linux/init_task.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/mqueue.h>
#include <linux/hardirq.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/prom.h>

extern unsigned long _get_SP(void);

#ifndef CONFIG_SMP
struct task_struct *last_task_used_math = NULL;
struct task_struct *last_task_used_altivec = NULL;
struct task_struct *last_task_used_spe = NULL;
#endif

static struct fs_struct init_fs = INIT_FS;
static struct files_struct init_files = INIT_FILES;
static struct signal_struct init_signals = INIT_SIGNALS(init_signals);
static struct sighand_struct init_sighand = INIT_SIGHAND(init_sighand);
struct mm_struct init_mm = INIT_MM(init_mm);
EXPORT_SYMBOL(init_mm);

/* this is 8kB-aligned so we can get to the thread_info struct
   at the base of it from the stack pointer with 1 integer instruction. */
union thread_union init_thread_union
	__attribute__((__section__(".data.init_task"))) =
{ INIT_THREAD_INFO(init_task) };

/* initial task structure */
struct task_struct init_task = INIT_TASK(init_task);
EXPORT_SYMBOL(init_task);

/* only used to get secondary processor up */
struct task_struct *current_set[NR_CPUS] = {&init_task, };

/*
 * Make sure the floating-point register state in the
 * the thread_struct is up to date for task tsk.
 */
void flush_fp_to_thread(struct task_struct *tsk)
{
	if (tsk->thread.regs) {
		/*
		 * We need to disable preemption here because if we didn't,
		 * another process could get scheduled after the regs->msr
		 * test but before we have finished saving the FP registers
		 * to the thread_struct.  That process could take over the
		 * FPU, and then when we get scheduled again we would store
		 * bogus values for the remaining FP registers.
		 */
		preempt_disable();
		if (tsk->thread.regs->msr & MSR_FP) {
#ifdef CONFIG_SMP
			/*
			 * This should only ever be called for current or
			 * for a stopped child process.  Since we save away
			 * the FP register state on context switch on SMP,
			 * there is something wrong if a stopped child appears
			 * to still have its FP state in the CPU registers.
			 */
			BUG_ON(tsk != current);
#endif
			giveup_fpu(current);
		}
		preempt_enable();
	}
}

void enable_kernel_fp(void)
{
	WARN_ON(preemptible());

#ifdef CONFIG_SMP
	if (current->thread.regs && (current->thread.regs->msr & MSR_FP))
		giveup_fpu(current);
	else
		giveup_fpu(NULL);	/* just enables FP for kernel */
#else
	giveup_fpu(last_task_used_math);
#endif /* CONFIG_SMP */
}
EXPORT_SYMBOL(enable_kernel_fp);

int dump_task_fpu(struct task_struct *tsk, elf_fpregset_t *fpregs)
{
	if (!tsk->thread.regs)
		return 0;
	flush_fp_to_thread(current);

	memcpy(fpregs, &tsk->thread.fpr[0], sizeof(*fpregs));

	return 1;
}

#ifdef CONFIG_ALTIVEC
void enable_kernel_altivec(void)
{
	WARN_ON(preemptible());

#ifdef CONFIG_SMP
	if (current->thread.regs && (current->thread.regs->msr & MSR_VEC))
		giveup_altivec(current);
	else
		giveup_altivec(NULL);	/* just enable AltiVec for kernel - force */
#else
	giveup_altivec(last_task_used_altivec);
#endif /* CONFIG_SMP */
}
EXPORT_SYMBOL(enable_kernel_altivec);

/*
 * Make sure the VMX/Altivec register state in the
 * the thread_struct is up to date for task tsk.
 */
void flush_altivec_to_thread(struct task_struct *tsk)
{
	if (tsk->thread.regs) {
		preempt_disable();
		if (tsk->thread.regs->msr & MSR_VEC) {
#ifdef CONFIG_SMP
			BUG_ON(tsk != current);
#endif
			giveup_altivec(current);
		}
		preempt_enable();
	}
}

int dump_task_altivec(struct pt_regs *regs, elf_vrregset_t *vrregs)
{
	flush_altivec_to_thread(current);
	memcpy(vrregs, &current->thread.vr[0], sizeof(*vrregs));
	return 1;
}
#endif /* CONFIG_ALTIVEC */

#ifdef CONFIG_SPE

void enable_kernel_spe(void)
{
	WARN_ON(preemptible());

#ifdef CONFIG_SMP
	if (current->thread.regs && (current->thread.regs->msr & MSR_SPE))
		giveup_spe(current);
	else
		giveup_spe(NULL);	/* just enable SPE for kernel - force */
#else
	giveup_spe(last_task_used_spe);
#endif /* __SMP __ */
}
EXPORT_SYMBOL(enable_kernel_spe);

void flush_spe_to_thread(struct task_struct *tsk)
{
	if (tsk->thread.regs) {
		preempt_disable();
		if (tsk->thread.regs->msr & MSR_SPE) {
#ifdef CONFIG_SMP
			BUG_ON(tsk != current);
#endif
			giveup_spe(current);
		}
		preempt_enable();
	}
}

int dump_spe(struct pt_regs *regs, elf_vrregset_t *evrregs)
{
	flush_spe_to_thread(current);
	/* We copy u32 evr[32] + u64 acc + u32 spefscr -> 35 */
	memcpy(evrregs, &current->thread.evr[0], sizeof(u32) * 35);
	return 1;
}
#endif /* CONFIG_SPE */

static void set_dabr_spr(unsigned long val)
{
	mtspr(SPRN_DABR, val);
}

int set_dabr(unsigned long dabr)
{
	int ret = 0;

#ifdef CONFIG_PPC64
	if (firmware_has_feature(FW_FEATURE_XDABR)) {
		/* We want to catch accesses from kernel and userspace */
		unsigned long flags = H_DABRX_KERNEL|H_DABRX_USER;
		ret = plpar_set_xdabr(dabr, flags);
	} else if (firmware_has_feature(FW_FEATURE_DABR)) {
		ret = plpar_set_dabr(dabr);
	} else
#endif
		set_dabr_spr(dabr);

	return ret;
}

static DEFINE_PER_CPU(unsigned long, current_dabr);

struct task_struct *__switch_to(struct task_struct *prev,
	struct task_struct *new)
{
	struct thread_struct *new_thread, *old_thread;
	unsigned long flags;
	struct task_struct *last;

#ifdef CONFIG_SMP
	/* avoid complexity of lazy save/restore of fpu
	 * by just saving it every time we switch out if
	 * this task used the fpu during the last quantum.
	 *
	 * If it tries to use the fpu again, it'll trap and
	 * reload its fp regs.  So we don't have to do a restore
	 * every switch, just a save.
	 *  -- Cort
	 */
	if (prev->thread.regs && (prev->thread.regs->msr & MSR_FP))
		giveup_fpu(prev);
#ifdef CONFIG_ALTIVEC
	/*
	 * If the previous thread used altivec in the last quantum
	 * (thus changing altivec regs) then save them.
	 * We used to check the VRSAVE register but not all apps
	 * set it, so we don't rely on it now (and in fact we need
	 * to save & restore VSCR even if VRSAVE == 0).  -- paulus
	 *
	 * On SMP we always save/restore altivec regs just to avoid the
	 * complexity of changing processors.
	 *  -- Cort
	 */
	if (prev->thread.regs && (prev->thread.regs->msr & MSR_VEC))
		giveup_altivec(prev);
	/* Avoid the trap.  On smp this this never happens since
	 * we don't set last_task_used_altivec -- Cort
	 */
	if (new->thread.regs && last_task_used_altivec == new)
		new->thread.regs->msr |= MSR_VEC;
#endif /* CONFIG_ALTIVEC */
#ifdef CONFIG_SPE
	/*
	 * If the previous thread used spe in the last quantum
	 * (thus changing spe regs) then save them.
	 *
	 * On SMP we always save/restore spe regs just to avoid the
	 * complexity of changing processors.
	 */
	if ((prev->thread.regs && (prev->thread.regs->msr & MSR_SPE)))
		giveup_spe(prev);
	/* Avoid the trap.  On smp this this never happens since
	 * we don't set last_task_used_spe
	 */
	if (new->thread.regs && last_task_used_spe == new)
		new->thread.regs->msr |= MSR_SPE;
#endif /* CONFIG_SPE */
#endif /* CONFIG_SMP */

#ifdef CONFIG_PPC64	/* for now */
	if (unlikely(__get_cpu_var(current_dabr) != new->thread.dabr)) {
		set_dabr(new->thread.dabr);
		__get_cpu_var(current_dabr) = new->thread.dabr;
	}
#endif

	new_thread = &new->thread;
	old_thread = &current->thread;
	local_irq_save(flags);
	last = _switch(old_thread, new_thread);

	local_irq_restore(flags);

	return last;
}

void show_regs(struct pt_regs * regs)
{
	int i, trap;

	printk("NIP: %08lX LR: %08lX SP: %08lX REGS: %p TRAP: %04lx    %s\n",
	       regs->nip, regs->link, regs->gpr[1], regs, regs->trap,
	       print_tainted());
	printk("MSR: %08lx EE: %01x PR: %01x FP: %01x ME: %01x IR/DR: %01x%01x\n",
	       regs->msr, regs->msr&MSR_EE ? 1 : 0, regs->msr&MSR_PR ? 1 : 0,
	       regs->msr & MSR_FP ? 1 : 0,regs->msr&MSR_ME ? 1 : 0,
	       regs->msr&MSR_IR ? 1 : 0,
	       regs->msr&MSR_DR ? 1 : 0);
	trap = TRAP(regs);
	if (trap == 0x300 || trap == 0x600)
		printk("DAR: %08lX, DSISR: %08lX\n", regs->dar, regs->dsisr);
	printk("TASK = %p[%d] '%s' THREAD: %p\n",
	       current, current->pid, current->comm, current->thread_info);
	printk("Last syscall: %ld ", current->thread.last_syscall);

#ifdef CONFIG_SMP
	printk(" CPU: %d", smp_processor_id());
#endif /* CONFIG_SMP */

	for (i = 0;  i < 32;  i++) {
		long r;
		if ((i % 8) == 0)
			printk("\n" KERN_INFO "GPR%02d: ", i);
		if (__get_user(r, &regs->gpr[i]))
			break;
		printk("%08lX ", r);
		if (i == 12 && !FULL_REGS(regs))
			break;
	}
	printk("\n");
#ifdef CONFIG_KALLSYMS
	/*
	 * Lookup NIP late so we have the best change of getting the
	 * above info out without failing
	 */
	printk("NIP [%08lx] ", regs->nip);
	print_symbol("%s\n", regs->nip);
	printk("LR [%08lx] ", regs->link);
	print_symbol("%s\n", regs->link);
#endif
	show_stack(current, (unsigned long *) regs->gpr[1]);
}

void exit_thread(void)
{
#ifndef CONFIG_SMP
	if (last_task_used_math == current)
		last_task_used_math = NULL;
#ifdef CONFIG_ALTIVEC
	if (last_task_used_altivec == current)
		last_task_used_altivec = NULL;
#endif /* CONFIG_ALTIVEC */
#ifdef CONFIG_SPE
	if (last_task_used_spe == current)
		last_task_used_spe = NULL;
#endif
#endif /* CONFIG_SMP */
}

void flush_thread(void)
{
#ifndef CONFIG_SMP
	if (last_task_used_math == current)
		last_task_used_math = NULL;
#ifdef CONFIG_ALTIVEC
	if (last_task_used_altivec == current)
		last_task_used_altivec = NULL;
#endif /* CONFIG_ALTIVEC */
#ifdef CONFIG_SPE
	if (last_task_used_spe == current)
		last_task_used_spe = NULL;
#endif
#endif /* CONFIG_SMP */

#ifdef CONFIG_PPC64	/* for now */
	if (current->thread.dabr) {
		current->thread.dabr = 0;
		set_dabr(0);
	}
#endif
}

void
release_thread(struct task_struct *t)
{
}

/*
 * This gets called before we allocate a new thread and copy
 * the current task into it.
 */
void prepare_to_copy(struct task_struct *tsk)
{
	flush_fp_to_thread(current);
	flush_altivec_to_thread(current);
	flush_spe_to_thread(current);
}

/*
 * Copy a thread..
 */
int
copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
	    unsigned long unused,
	    struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs *childregs, *kregs;
	extern void ret_from_fork(void);
	unsigned long sp = (unsigned long)p->thread_info + THREAD_SIZE;
	unsigned long childframe;

	CHECK_FULL_REGS(regs);
	/* Copy registers */
	sp -= sizeof(struct pt_regs);
	childregs = (struct pt_regs *) sp;
	*childregs = *regs;
	if ((childregs->msr & MSR_PR) == 0) {
		/* for kernel thread, set `current' and stackptr in new task */
		childregs->gpr[1] = sp + sizeof(struct pt_regs);
		childregs->gpr[2] = (unsigned long) p;
		p->thread.regs = NULL;	/* no user register state */
	} else {
		childregs->gpr[1] = usp;
		p->thread.regs = childregs;
		if (clone_flags & CLONE_SETTLS)
			childregs->gpr[2] = childregs->gpr[6];
	}
	childregs->gpr[3] = 0;  /* Result from fork() */
	sp -= STACK_FRAME_OVERHEAD;
	childframe = sp;

	/*
	 * The way this works is that at some point in the future
	 * some task will call _switch to switch to the new task.
	 * That will pop off the stack frame created below and start
	 * the new task running at ret_from_fork.  The new task will
	 * do some house keeping and then return from the fork or clone
	 * system call, using the stack frame created above.
	 */
	sp -= sizeof(struct pt_regs);
	kregs = (struct pt_regs *) sp;
	sp -= STACK_FRAME_OVERHEAD;
	p->thread.ksp = sp;
	kregs->nip = (unsigned long)ret_from_fork;

	p->thread.last_syscall = -1;

	return 0;
}

/*
 * Set up a thread for executing a new program
 */
void start_thread(struct pt_regs *regs, unsigned long nip, unsigned long sp)
{
	set_fs(USER_DS);
	memset(regs->gpr, 0, sizeof(regs->gpr));
	regs->ctr = 0;
	regs->link = 0;
	regs->xer = 0;
	regs->ccr = 0;
	regs->mq = 0;
	regs->nip = nip;
	regs->gpr[1] = sp;
	regs->msr = MSR_USER;
#ifndef CONFIG_SMP
	if (last_task_used_math == current)
		last_task_used_math = NULL;
#ifdef CONFIG_ALTIVEC
	if (last_task_used_altivec == current)
		last_task_used_altivec = NULL;
#endif
#ifdef CONFIG_SPE
	if (last_task_used_spe == current)
		last_task_used_spe = NULL;
#endif
#endif /* CONFIG_SMP */
	memset(current->thread.fpr, 0, sizeof(current->thread.fpr));
	current->thread.fpscr = 0;
#ifdef CONFIG_ALTIVEC
	memset(current->thread.vr, 0, sizeof(current->thread.vr));
	memset(&current->thread.vscr, 0, sizeof(current->thread.vscr));
	current->thread.vrsave = 0;
	current->thread.used_vr = 0;
#endif /* CONFIG_ALTIVEC */
#ifdef CONFIG_SPE
	memset(current->thread.evr, 0, sizeof(current->thread.evr));
	current->thread.acc = 0;
	current->thread.spefscr = 0;
	current->thread.used_spe = 0;
#endif /* CONFIG_SPE */
}

#define PR_FP_ALL_EXCEPT (PR_FP_EXC_DIV | PR_FP_EXC_OVF | PR_FP_EXC_UND \
		| PR_FP_EXC_RES | PR_FP_EXC_INV)

int set_fpexc_mode(struct task_struct *tsk, unsigned int val)
{
	struct pt_regs *regs = tsk->thread.regs;

	/* This is a bit hairy.  If we are an SPE enabled  processor
	 * (have embedded fp) we store the IEEE exception enable flags in
	 * fpexc_mode.  fpexc_mode is also used for setting FP exception
	 * mode (asyn, precise, disabled) for 'Classic' FP. */
	if (val & PR_FP_EXC_SW_ENABLE) {
#ifdef CONFIG_SPE
		tsk->thread.fpexc_mode = val &
			(PR_FP_EXC_SW_ENABLE | PR_FP_ALL_EXCEPT);
#else
		return -EINVAL;
#endif
	} else {
		/* on a CONFIG_SPE this does not hurt us.  The bits that
		 * __pack_fe01 use do not overlap with bits used for
		 * PR_FP_EXC_SW_ENABLE.  Additionally, the MSR[FE0,FE1] bits
		 * on CONFIG_SPE implementations are reserved so writing to
		 * them does not change anything */
		if (val > PR_FP_EXC_PRECISE)
			return -EINVAL;
		tsk->thread.fpexc_mode = __pack_fe01(val);
		if (regs != NULL && (regs->msr & MSR_FP) != 0)
			regs->msr = (regs->msr & ~(MSR_FE0|MSR_FE1))
				| tsk->thread.fpexc_mode;
	}
	return 0;
}

int get_fpexc_mode(struct task_struct *tsk, unsigned long adr)
{
	unsigned int val;

	if (tsk->thread.fpexc_mode & PR_FP_EXC_SW_ENABLE)
#ifdef CONFIG_SPE
		val = tsk->thread.fpexc_mode;
#else
		return -EINVAL;
#endif
	else
		val = __unpack_fe01(tsk->thread.fpexc_mode);
	return put_user(val, (unsigned int __user *) adr);
}

int sys_clone(unsigned long clone_flags, unsigned long usp,
	      int __user *parent_tidp, void __user *child_threadptr,
	      int __user *child_tidp, int p6,
	      struct pt_regs *regs)
{
	CHECK_FULL_REGS(regs);
	if (usp == 0)
		usp = regs->gpr[1];	/* stack pointer for child */
 	return do_fork(clone_flags, usp, regs, 0, parent_tidp, child_tidp);
}

int sys_fork(unsigned long p1, unsigned long p2, unsigned long p3,
	     unsigned long p4, unsigned long p5, unsigned long p6,
	     struct pt_regs *regs)
{
	CHECK_FULL_REGS(regs);
	return do_fork(SIGCHLD, regs->gpr[1], regs, 0, NULL, NULL);
}

int sys_vfork(unsigned long p1, unsigned long p2, unsigned long p3,
	      unsigned long p4, unsigned long p5, unsigned long p6,
	      struct pt_regs *regs)
{
	CHECK_FULL_REGS(regs);
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs->gpr[1],
			regs, 0, NULL, NULL);
}

int sys_execve(unsigned long a0, unsigned long a1, unsigned long a2,
	       unsigned long a3, unsigned long a4, unsigned long a5,
	       struct pt_regs *regs)
{
	int error;
	char * filename;

	filename = getname((char __user *) a0);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	flush_fp_to_thread(current);
	flush_altivec_to_thread(current);
	flush_spe_to_thread(current);
	if (error == 0) {
		task_lock(current);
		current->ptrace &= ~PT_DTRACE;
		task_unlock(current);
	}
	putname(filename);
out:
	return error;
}

static int validate_sp(unsigned long sp, struct task_struct *p,
		       unsigned long nbytes)
{
	unsigned long stack_page = (unsigned long)p->thread_info;

	if (sp >= stack_page + sizeof(struct thread_struct)
	    && sp <= stack_page + THREAD_SIZE - nbytes)
		return 1;

#ifdef CONFIG_IRQSTACKS
	stack_page = (unsigned long) hardirq_ctx[task_cpu(p)];
	if (sp >= stack_page + sizeof(struct thread_struct)
	    && sp <= stack_page + THREAD_SIZE - nbytes)
		return 1;

	stack_page = (unsigned long) softirq_ctx[task_cpu(p)];
	if (sp >= stack_page + sizeof(struct thread_struct)
	    && sp <= stack_page + THREAD_SIZE - nbytes)
		return 1;
#endif

	return 0;
}

void dump_stack(void)
{
	show_stack(current, NULL);
}

EXPORT_SYMBOL(dump_stack);

void show_stack(struct task_struct *tsk, unsigned long *stack)
{
	unsigned long sp, stack_top, prev_sp, ret;
	int count = 0;
	unsigned long next_exc = 0;
	struct pt_regs *regs;
	extern char ret_from_except, ret_from_except_full, ret_from_syscall;

	sp = (unsigned long) stack;
	if (tsk == NULL)
		tsk = current;
	if (sp == 0) {
		if (tsk == current)
			asm("mr %0,1" : "=r" (sp));
		else
			sp = tsk->thread.ksp;
	}

	prev_sp = (unsigned long) (tsk->thread_info + 1);
	stack_top = (unsigned long) tsk->thread_info + THREAD_SIZE;
	while (count < 16 && sp > prev_sp && sp < stack_top && (sp & 3) == 0) {
		if (count == 0) {
			printk("Call trace:");
#ifdef CONFIG_KALLSYMS
			printk("\n");
#endif
		} else {
			if (next_exc) {
				ret = next_exc;
				next_exc = 0;
			} else
				ret = *(unsigned long *)(sp + 4);
			printk(" [%08lx] ", ret);
#ifdef CONFIG_KALLSYMS
			print_symbol("%s", ret);
			printk("\n");
#endif
			if (ret == (unsigned long) &ret_from_except
			    || ret == (unsigned long) &ret_from_except_full
			    || ret == (unsigned long) &ret_from_syscall) {
				/* sp + 16 points to an exception frame */
				regs = (struct pt_regs *) (sp + 16);
				if (sp + 16 + sizeof(*regs) <= stack_top)
					next_exc = regs->nip;
			}
		}
		++count;
		sp = *(unsigned long *)sp;
	}
#ifndef CONFIG_KALLSYMS
	if (count > 0)
		printk("\n");
#endif
}

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long ip, sp;
	int count = 0;

	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	sp = p->thread.ksp;
	if (!validate_sp(sp, p, 16))
		return 0;

	do {
		sp = *(unsigned long *)sp;
		if (!validate_sp(sp, p, 16))
			return 0;
		if (count > 0) {
			ip = *(unsigned long *)(sp + 4);
			if (!in_sched_functions(ip))
				return ip;
		}
	} while (count++ < 16);
	return 0;
}
EXPORT_SYMBOL(get_wchan);
