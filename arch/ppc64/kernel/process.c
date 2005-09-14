/*
 *  linux/arch/ppc64/kernel/process.c
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
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/elf.h>
#include <linux/init.h>
#include <linux/init_task.h>
#include <linux/prctl.h>
#include <linux/ptrace.h>
#include <linux/kallsyms.h>
#include <linux/interrupt.h>
#include <linux/utsname.h>
#include <linux/kprobes.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/prom.h>
#include <asm/ppcdebug.h>
#include <asm/machdep.h>
#include <asm/iSeries/HvCallHpt.h>
#include <asm/cputable.h>
#include <asm/firmware.h>
#include <asm/sections.h>
#include <asm/tlbflush.h>
#include <asm/time.h>
#include <asm/plpar_wrappers.h>

#ifndef CONFIG_SMP
struct task_struct *last_task_used_math = NULL;
struct task_struct *last_task_used_altivec = NULL;
#endif

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
		giveup_altivec(NULL);	/* just enables FP for kernel */
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

static void set_dabr_spr(unsigned long val)
{
	mtspr(SPRN_DABR, val);
}

int set_dabr(unsigned long dabr)
{
	int ret = 0;

	if (firmware_has_feature(FW_FEATURE_XDABR)) {
		/* We want to catch accesses from kernel and userspace */
		unsigned long flags = H_DABRX_KERNEL|H_DABRX_USER;
		ret = plpar_set_xdabr(dabr, flags);
	} else if (firmware_has_feature(FW_FEATURE_DABR)) {
		ret = plpar_set_dabr(dabr);
	} else {
		set_dabr_spr(dabr);
	}

	return ret;
}

DEFINE_PER_CPU(struct cpu_usage, cpu_usage_array);
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
	if (prev->thread.regs && (prev->thread.regs->msr & MSR_VEC))
		giveup_altivec(prev);
#endif /* CONFIG_ALTIVEC */
#endif /* CONFIG_SMP */

#if defined(CONFIG_ALTIVEC) && !defined(CONFIG_SMP)
	/* Avoid the trap.  On smp this this never happens since
	 * we don't set last_task_used_altivec -- Cort
	 */
	if (new->thread.regs && last_task_used_altivec == new)
		new->thread.regs->msr |= MSR_VEC;
#endif /* CONFIG_ALTIVEC */

	if (unlikely(__get_cpu_var(current_dabr) != new->thread.dabr)) {
		set_dabr(new->thread.dabr);
		__get_cpu_var(current_dabr) = new->thread.dabr;
	}

	flush_tlb_pending();

	new_thread = &new->thread;
	old_thread = &current->thread;

	/* Collect purr utilization data per process and per processor
	 * wise purr is nothing but processor time base
	 */
	if (firmware_has_feature(FW_FEATURE_SPLPAR)) {
		struct cpu_usage *cu = &__get_cpu_var(cpu_usage_array);
		long unsigned start_tb, current_tb;
		start_tb = old_thread->start_tb;
		cu->current_tb = current_tb = mfspr(SPRN_PURR);
		old_thread->accum_tb += (current_tb - start_tb);
		new_thread->start_tb = current_tb;
	}

	local_irq_save(flags);
	last = _switch(old_thread, new_thread);

	local_irq_restore(flags);

	return last;
}

static int instructions_to_print = 16;

static void show_instructions(struct pt_regs *regs)
{
	int i;
	unsigned long pc = regs->nip - (instructions_to_print * 3 / 4 *
			sizeof(int));

	printk("Instruction dump:");

	for (i = 0; i < instructions_to_print; i++) {
		int instr;

		if (!(i % 8))
			printk("\n");

		if (((REGION_ID(pc) != KERNEL_REGION_ID) &&
		     (REGION_ID(pc) != VMALLOC_REGION_ID)) ||
		     __get_user(instr, (unsigned int *)pc)) {
			printk("XXXXXXXX ");
		} else {
			if (regs->nip == pc)
				printk("<%08x> ", instr);
			else
				printk("%08x ", instr);
		}

		pc += sizeof(int);
	}

	printk("\n");
}

void show_regs(struct pt_regs * regs)
{
	int i;
	unsigned long trap;

	printk("NIP: %016lX XER: %08X LR: %016lX CTR: %016lX\n",
	       regs->nip, (unsigned int)regs->xer, regs->link, regs->ctr);
	printk("REGS: %p TRAP: %04lx   %s  (%s)\n",
	       regs, regs->trap, print_tainted(), system_utsname.release);
	printk("MSR: %016lx EE: %01x PR: %01x FP: %01x ME: %01x "
	       "IR/DR: %01x%01x CR: %08X\n",
	       regs->msr, regs->msr&MSR_EE ? 1 : 0, regs->msr&MSR_PR ? 1 : 0,
	       regs->msr & MSR_FP ? 1 : 0,regs->msr&MSR_ME ? 1 : 0,
	       regs->msr&MSR_IR ? 1 : 0,
	       regs->msr&MSR_DR ? 1 : 0,
	       (unsigned int)regs->ccr);
	trap = TRAP(regs);
	printk("DAR: %016lx DSISR: %016lx\n", regs->dar, regs->dsisr);
	printk("TASK: %p[%d] '%s' THREAD: %p",
	       current, current->pid, current->comm, current->thread_info);

#ifdef CONFIG_SMP
	printk(" CPU: %d", smp_processor_id());
#endif /* CONFIG_SMP */

	for (i = 0; i < 32; i++) {
		if ((i % 4) == 0) {
			printk("\n" KERN_INFO "GPR%02d: ", i);
		}

		printk("%016lX ", regs->gpr[i]);
		if (i == 13 && !FULL_REGS(regs))
			break;
	}
	printk("\n");
	/*
	 * Lookup NIP late so we have the best change of getting the
	 * above info out without failing
	 */
	printk("NIP [%016lx] ", regs->nip);
	print_symbol("%s\n", regs->nip);
	printk("LR [%016lx] ", regs->link);
	print_symbol("%s\n", regs->link);
	show_stack(current, (unsigned long *)regs->gpr[1]);
	if (!user_mode(regs))
		show_instructions(regs);
}

void exit_thread(void)
{
	kprobe_flush_task(current);

#ifndef CONFIG_SMP
	if (last_task_used_math == current)
		last_task_used_math = NULL;
#ifdef CONFIG_ALTIVEC
	if (last_task_used_altivec == current)
		last_task_used_altivec = NULL;
#endif /* CONFIG_ALTIVEC */
#endif /* CONFIG_SMP */
}

void flush_thread(void)
{
	struct thread_info *t = current_thread_info();

	kprobe_flush_task(current);
	if (t->flags & _TIF_ABI_PENDING)
		t->flags ^= (_TIF_ABI_PENDING | _TIF_32BIT);

#ifndef CONFIG_SMP
	if (last_task_used_math == current)
		last_task_used_math = NULL;
#ifdef CONFIG_ALTIVEC
	if (last_task_used_altivec == current)
		last_task_used_altivec = NULL;
#endif /* CONFIG_ALTIVEC */
#endif /* CONFIG_SMP */

	if (current->thread.dabr) {
		current->thread.dabr = 0;
		set_dabr(0);
	}
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
}

/*
 * Copy a thread..
 */
int
copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
	    unsigned long unused, struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs *childregs, *kregs;
	extern void ret_from_fork(void);
	unsigned long sp = (unsigned long)p->thread_info + THREAD_SIZE;

	/* Copy registers */
	sp -= sizeof(struct pt_regs);
	childregs = (struct pt_regs *) sp;
	*childregs = *regs;
	if ((childregs->msr & MSR_PR) == 0) {
		/* for kernel thread, set stackptr in new task */
		childregs->gpr[1] = sp + sizeof(struct pt_regs);
		p->thread.regs = NULL;	/* no user register state */
		clear_ti_thread_flag(p->thread_info, TIF_32BIT);
	} else {
		childregs->gpr[1] = usp;
		p->thread.regs = childregs;
		if (clone_flags & CLONE_SETTLS) {
			if (test_thread_flag(TIF_32BIT))
				childregs->gpr[2] = childregs->gpr[6];
			else
				childregs->gpr[13] = childregs->gpr[6];
		}
	}
	childregs->gpr[3] = 0;  /* Result from fork() */
	sp -= STACK_FRAME_OVERHEAD;

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
	if (cpu_has_feature(CPU_FTR_SLB)) {
		unsigned long sp_vsid = get_kernel_vsid(sp);

		sp_vsid <<= SLB_VSID_SHIFT;
		sp_vsid |= SLB_VSID_KERNEL;
		if (cpu_has_feature(CPU_FTR_16M_PAGE))
			sp_vsid |= SLB_VSID_L;

		p->thread.ksp_vsid = sp_vsid;
	}

	/*
	 * The PPC64 ABI makes use of a TOC to contain function 
	 * pointers.  The function (ret_from_except) is actually a pointer
	 * to the TOC entry.  The first entry is a pointer to the actual
	 * function.
 	 */
	kregs->nip = *((unsigned long *)ret_from_fork);

	return 0;
}

/*
 * Set up a thread for executing a new program
 */
void start_thread(struct pt_regs *regs, unsigned long fdptr, unsigned long sp)
{
	unsigned long entry, toc, load_addr = regs->gpr[2];

	/* fdptr is a relocated pointer to the function descriptor for
         * the elf _start routine.  The first entry in the function
         * descriptor is the entry address of _start and the second
         * entry is the TOC value we need to use.
         */
	set_fs(USER_DS);
	__get_user(entry, (unsigned long __user *)fdptr);
	__get_user(toc, (unsigned long __user *)fdptr+1);

	/* Check whether the e_entry function descriptor entries
	 * need to be relocated before we can use them.
	 */
	if (load_addr != 0) {
		entry += load_addr;
		toc   += load_addr;
	}

	/*
	 * If we exec out of a kernel thread then thread.regs will not be
	 * set. Do it now.
	 */
	if (!current->thread.regs) {
		unsigned long childregs = (unsigned long)current->thread_info +
						THREAD_SIZE;
		childregs -= sizeof(struct pt_regs);
		current->thread.regs = (struct pt_regs *)childregs;
	}

	regs->nip = entry;
	regs->gpr[1] = sp;
	regs->gpr[2] = toc;
	regs->msr = MSR_USER64;
#ifndef CONFIG_SMP
	if (last_task_used_math == current)
		last_task_used_math = 0;
#endif /* CONFIG_SMP */
	memset(current->thread.fpr, 0, sizeof(current->thread.fpr));
	current->thread.fpscr = 0;
#ifdef CONFIG_ALTIVEC
#ifndef CONFIG_SMP
	if (last_task_used_altivec == current)
		last_task_used_altivec = 0;
#endif /* CONFIG_SMP */
	memset(current->thread.vr, 0, sizeof(current->thread.vr));
	current->thread.vscr.u[0] = 0;
	current->thread.vscr.u[1] = 0;
	current->thread.vscr.u[2] = 0;
	current->thread.vscr.u[3] = 0x00010000; /* Java mode disabled */
	current->thread.vrsave = 0;
	current->thread.used_vr = 0;
#endif /* CONFIG_ALTIVEC */
}
EXPORT_SYMBOL(start_thread);

int set_fpexc_mode(struct task_struct *tsk, unsigned int val)
{
	struct pt_regs *regs = tsk->thread.regs;

	if (val > PR_FP_EXC_PRECISE)
		return -EINVAL;
	tsk->thread.fpexc_mode = __pack_fe01(val);
	if (regs != NULL && (regs->msr & MSR_FP) != 0)
		regs->msr = (regs->msr & ~(MSR_FE0|MSR_FE1))
			| tsk->thread.fpexc_mode;
	return 0;
}

int get_fpexc_mode(struct task_struct *tsk, unsigned long adr)
{
	unsigned int val;

	val = __unpack_fe01(tsk->thread.fpexc_mode);
	return put_user(val, (unsigned int __user *) adr);
}

int sys_clone(unsigned long clone_flags, unsigned long p2, unsigned long p3,
	      unsigned long p4, unsigned long p5, unsigned long p6,
	      struct pt_regs *regs)
{
	unsigned long parent_tidptr = 0;
	unsigned long child_tidptr = 0;

	if (p2 == 0)
		p2 = regs->gpr[1];	/* stack pointer for child */

	if (clone_flags & (CLONE_PARENT_SETTID | CLONE_CHILD_SETTID |
			   CLONE_CHILD_CLEARTID)) {
		parent_tidptr = p3;
		child_tidptr = p5;
		if (test_thread_flag(TIF_32BIT)) {
			parent_tidptr &= 0xffffffff;
			child_tidptr &= 0xffffffff;
		}
	}

	return do_fork(clone_flags, p2, regs, 0,
		    (int __user *)parent_tidptr, (int __user *)child_tidptr);
}

int sys_fork(unsigned long p1, unsigned long p2, unsigned long p3,
	     unsigned long p4, unsigned long p5, unsigned long p6,
	     struct pt_regs *regs)
{
	return do_fork(SIGCHLD, regs->gpr[1], regs, 0, NULL, NULL);
}

int sys_vfork(unsigned long p1, unsigned long p2, unsigned long p3,
	      unsigned long p4, unsigned long p5, unsigned long p6,
	      struct pt_regs *regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs->gpr[1], regs, 0,
	            NULL, NULL);
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
	error = do_execve(filename, (char __user * __user *) a1,
				    (char __user * __user *) a2, regs);
  
	if (error == 0) {
		task_lock(current);
		current->ptrace &= ~PT_DTRACE;
		task_unlock(current);
	}
	putname(filename);

out:
	return error;
}

static int kstack_depth_to_print = 64;

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

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long ip, sp;
	int count = 0;

	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	sp = p->thread.ksp;
	if (!validate_sp(sp, p, 112))
		return 0;

	do {
		sp = *(unsigned long *)sp;
		if (!validate_sp(sp, p, 112))
			return 0;
		if (count > 0) {
			ip = *(unsigned long *)(sp + 16);
			if (!in_sched_functions(ip))
				return ip;
		}
	} while (count++ < 16);
	return 0;
}
EXPORT_SYMBOL(get_wchan);

void show_stack(struct task_struct *p, unsigned long *_sp)
{
	unsigned long ip, newsp, lr;
	int count = 0;
	unsigned long sp = (unsigned long)_sp;
	int firstframe = 1;

	if (sp == 0) {
		if (p) {
			sp = p->thread.ksp;
		} else {
			sp = __get_SP();
			p = current;
		}
	}

	lr = 0;
	printk("Call Trace:\n");
	do {
		if (!validate_sp(sp, p, 112))
			return;

		_sp = (unsigned long *) sp;
		newsp = _sp[0];
		ip = _sp[2];
		if (!firstframe || ip != lr) {
			printk("[%016lx] [%016lx] ", sp, ip);
			print_symbol("%s", ip);
			if (firstframe)
				printk(" (unreliable)");
			printk("\n");
		}
		firstframe = 0;

		/*
		 * See if this is an exception frame.
		 * We look for the "regshere" marker in the current frame.
		 */
		if (validate_sp(sp, p, sizeof(struct pt_regs) + 400)
		    && _sp[12] == 0x7265677368657265ul) {
			struct pt_regs *regs = (struct pt_regs *)
				(sp + STACK_FRAME_OVERHEAD);
			printk("--- Exception: %lx", regs->trap);
			print_symbol(" at %s\n", regs->nip);
			lr = regs->link;
			print_symbol("    LR = %s\n", lr);
			firstframe = 1;
		}

		sp = newsp;
	} while (count++ < kstack_depth_to_print);
}

void dump_stack(void)
{
	show_stack(current, (unsigned long *)__get_SP());
}
EXPORT_SYMBOL(dump_stack);
