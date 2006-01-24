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
#include <linux/utsname.h>
#include <linux/kprobes.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#ifdef CONFIG_PPC64
#include <asm/firmware.h>
#include <asm/time.h>
#endif

extern unsigned long _get_SP(void);

#ifndef CONFIG_SMP
struct task_struct *last_task_used_math = NULL;
struct task_struct *last_task_used_altivec = NULL;
struct task_struct *last_task_used_spe = NULL;
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

#ifndef CONFIG_SMP
/*
 * If we are doing lazy switching of CPU state (FP, altivec or SPE),
 * and the current task has some state, discard it.
 */
void discard_lazy_cpu_state(void)
{
	preempt_disable();
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
	preempt_enable();
}
#endif /* CONFIG_SMP */

#ifdef CONFIG_PPC_MERGE		/* XXX for now */
int set_dabr(unsigned long dabr)
{
	if (ppc_md.set_dabr)
		return ppc_md.set_dabr(dabr);

	mtspr(SPRN_DABR, dabr);
	return 0;
}
#endif

#ifdef CONFIG_PPC64
DEFINE_PER_CPU(struct cpu_usage, cpu_usage_array);
static DEFINE_PER_CPU(unsigned long, current_dabr);
#endif

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
#endif /* CONFIG_SPE */

#else  /* CONFIG_SMP */
#ifdef CONFIG_ALTIVEC
	/* Avoid the trap.  On smp this this never happens since
	 * we don't set last_task_used_altivec -- Cort
	 */
	if (new->thread.regs && last_task_used_altivec == new)
		new->thread.regs->msr |= MSR_VEC;
#endif /* CONFIG_ALTIVEC */
#ifdef CONFIG_SPE
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

	flush_tlb_pending();
#endif

	new_thread = &new->thread;
	old_thread = &current->thread;

#ifdef CONFIG_PPC64
	/*
	 * Collect processor utilization data per process
	 */
	if (firmware_has_feature(FW_FEATURE_SPLPAR)) {
		struct cpu_usage *cu = &__get_cpu_var(cpu_usage_array);
		long unsigned start_tb, current_tb;
		start_tb = old_thread->start_tb;
		cu->current_tb = current_tb = mfspr(SPRN_PURR);
		old_thread->accum_tb += (current_tb - start_tb);
		new_thread->start_tb = current_tb;
	}
#endif

	local_irq_save(flags);
	last = _switch(old_thread, new_thread);

	local_irq_restore(flags);

	return last;
}

static int instructions_to_print = 16;

#ifdef CONFIG_PPC64
#define BAD_PC(pc)	((REGION_ID(pc) != KERNEL_REGION_ID) && \
		         (REGION_ID(pc) != VMALLOC_REGION_ID))
#else
#define BAD_PC(pc)	((pc) < KERNELBASE)
#endif

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

		if (BAD_PC(pc) || __get_user(instr, (unsigned int *)pc)) {
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

static struct regbit {
	unsigned long bit;
	const char *name;
} msr_bits[] = {
	{MSR_EE,	"EE"},
	{MSR_PR,	"PR"},
	{MSR_FP,	"FP"},
	{MSR_ME,	"ME"},
	{MSR_IR,	"IR"},
	{MSR_DR,	"DR"},
	{0,		NULL}
};

static void printbits(unsigned long val, struct regbit *bits)
{
	const char *sep = "";

	printk("<");
	for (; bits->bit; ++bits)
		if (val & bits->bit) {
			printk("%s%s", sep, bits->name);
			sep = ",";
		}
	printk(">");
}

#ifdef CONFIG_PPC64
#define REG		"%016lX"
#define REGS_PER_LINE	4
#define LAST_VOLATILE	13
#else
#define REG		"%08lX"
#define REGS_PER_LINE	8
#define LAST_VOLATILE	12
#endif

void show_regs(struct pt_regs * regs)
{
	int i, trap;

	printk("NIP: "REG" LR: "REG" CTR: "REG"\n",
	       regs->nip, regs->link, regs->ctr);
	printk("REGS: %p TRAP: %04lx   %s  (%s)\n",
	       regs, regs->trap, print_tainted(), system_utsname.release);
	printk("MSR: "REG" ", regs->msr);
	printbits(regs->msr, msr_bits);
	printk("  CR: %08lX  XER: %08lX\n", regs->ccr, regs->xer);
	trap = TRAP(regs);
	if (trap == 0x300 || trap == 0x600)
		printk("DAR: "REG", DSISR: "REG"\n", regs->dar, regs->dsisr);
	printk("TASK = %p[%d] '%s' THREAD: %p",
	       current, current->pid, current->comm, task_thread_info(current));

#ifdef CONFIG_SMP
	printk(" CPU: %d", smp_processor_id());
#endif /* CONFIG_SMP */

	for (i = 0;  i < 32;  i++) {
		if ((i % REGS_PER_LINE) == 0)
			printk("\n" KERN_INFO "GPR%02d: ", i);
		printk(REG " ", regs->gpr[i]);
		if (i == LAST_VOLATILE && !FULL_REGS(regs))
			break;
	}
	printk("\n");
#ifdef CONFIG_KALLSYMS
	/*
	 * Lookup NIP late so we have the best change of getting the
	 * above info out without failing
	 */
	printk("NIP ["REG"] ", regs->nip);
	print_symbol("%s\n", regs->nip);
	printk("LR ["REG"] ", regs->link);
	print_symbol("%s\n", regs->link);
#endif
	show_stack(current, (unsigned long *) regs->gpr[1]);
	if (!user_mode(regs))
		show_instructions(regs);
}

void exit_thread(void)
{
	kprobe_flush_task(current);
	discard_lazy_cpu_state();
}

void flush_thread(void)
{
#ifdef CONFIG_PPC64
	struct thread_info *t = current_thread_info();

	if (t->flags & _TIF_ABI_PENDING)
		t->flags ^= (_TIF_ABI_PENDING | _TIF_32BIT);
#endif

	discard_lazy_cpu_state();

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
int copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
		unsigned long unused, struct task_struct *p,
		struct pt_regs *regs)
{
	struct pt_regs *childregs, *kregs;
	extern void ret_from_fork(void);
	unsigned long sp = (unsigned long)task_stack_page(p) + THREAD_SIZE;

	CHECK_FULL_REGS(regs);
	/* Copy registers */
	sp -= sizeof(struct pt_regs);
	childregs = (struct pt_regs *) sp;
	*childregs = *regs;
	if ((childregs->msr & MSR_PR) == 0) {
		/* for kernel thread, set `current' and stackptr in new task */
		childregs->gpr[1] = sp + sizeof(struct pt_regs);
#ifdef CONFIG_PPC32
		childregs->gpr[2] = (unsigned long) p;
#else
		clear_tsk_thread_flag(p, TIF_32BIT);
#endif
		p->thread.regs = NULL;	/* no user register state */
	} else {
		childregs->gpr[1] = usp;
		p->thread.regs = childregs;
		if (clone_flags & CLONE_SETTLS) {
#ifdef CONFIG_PPC64
			if (!test_thread_flag(TIF_32BIT))
				childregs->gpr[13] = childregs->gpr[6];
			else
#endif
				childregs->gpr[2] = childregs->gpr[6];
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

#ifdef CONFIG_PPC64
	if (cpu_has_feature(CPU_FTR_SLB)) {
		unsigned long sp_vsid = get_kernel_vsid(sp);
		unsigned long llp = mmu_psize_defs[mmu_linear_psize].sllp;

		sp_vsid <<= SLB_VSID_SHIFT;
		sp_vsid |= SLB_VSID_KERNEL | llp;
		p->thread.ksp_vsid = sp_vsid;
	}

	/*
	 * The PPC64 ABI makes use of a TOC to contain function 
	 * pointers.  The function (ret_from_except) is actually a pointer
	 * to the TOC entry.  The first entry is a pointer to the actual
	 * function.
 	 */
	kregs->nip = *((unsigned long *)ret_from_fork);
#else
	kregs->nip = (unsigned long)ret_from_fork;
	p->thread.last_syscall = -1;
#endif

	return 0;
}

/*
 * Set up a thread for executing a new program
 */
void start_thread(struct pt_regs *regs, unsigned long start, unsigned long sp)
{
#ifdef CONFIG_PPC64
	unsigned long load_addr = regs->gpr[2];	/* saved by ELF_PLAT_INIT */
#endif

	set_fs(USER_DS);

	/*
	 * If we exec out of a kernel thread then thread.regs will not be
	 * set.  Do it now.
	 */
	if (!current->thread.regs) {
		struct pt_regs *regs = task_stack_page(current) + THREAD_SIZE;
		current->thread.regs = regs - 1;
	}

	memset(regs->gpr, 0, sizeof(regs->gpr));
	regs->ctr = 0;
	regs->link = 0;
	regs->xer = 0;
	regs->ccr = 0;
	regs->gpr[1] = sp;

#ifdef CONFIG_PPC32
	regs->mq = 0;
	regs->nip = start;
	regs->msr = MSR_USER;
#else
	if (!test_thread_flag(TIF_32BIT)) {
		unsigned long entry, toc;

		/* start is a relocated pointer to the function descriptor for
		 * the elf _start routine.  The first entry in the function
		 * descriptor is the entry address of _start and the second
		 * entry is the TOC value we need to use.
		 */
		__get_user(entry, (unsigned long __user *)start);
		__get_user(toc, (unsigned long __user *)start+1);

		/* Check whether the e_entry function descriptor entries
		 * need to be relocated before we can use them.
		 */
		if (load_addr != 0) {
			entry += load_addr;
			toc   += load_addr;
		}
		regs->nip = entry;
		regs->gpr[2] = toc;
		regs->msr = MSR_USER64;
	} else {
		regs->nip = start;
		regs->gpr[2] = 0;
		regs->msr = MSR_USER32;
	}
#endif

	discard_lazy_cpu_state();
	memset(current->thread.fpr, 0, sizeof(current->thread.fpr));
	current->thread.fpscr.val = 0;
#ifdef CONFIG_ALTIVEC
	memset(current->thread.vr, 0, sizeof(current->thread.vr));
	memset(&current->thread.vscr, 0, sizeof(current->thread.vscr));
	current->thread.vscr.u[3] = 0x00010000; /* Java mode disabled */
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
		return 0;
#else
		return -EINVAL;
#endif
	}

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

#define TRUNC_PTR(x)	((typeof(x))(((unsigned long)(x)) & 0xffffffff))

int sys_clone(unsigned long clone_flags, unsigned long usp,
	      int __user *parent_tidp, void __user *child_threadptr,
	      int __user *child_tidp, int p6,
	      struct pt_regs *regs)
{
	CHECK_FULL_REGS(regs);
	if (usp == 0)
		usp = regs->gpr[1];	/* stack pointer for child */
#ifdef CONFIG_PPC64
	if (test_thread_flag(TIF_32BIT)) {
		parent_tidp = TRUNC_PTR(parent_tidp);
		child_tidp = TRUNC_PTR(child_tidp);
	}
#endif
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
	char *filename;

	filename = getname((char __user *) a0);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	flush_fp_to_thread(current);
	flush_altivec_to_thread(current);
	flush_spe_to_thread(current);
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

static int validate_sp(unsigned long sp, struct task_struct *p,
		       unsigned long nbytes)
{
	unsigned long stack_page = (unsigned long)task_stack_page(p);

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

#ifdef CONFIG_PPC64
#define MIN_STACK_FRAME	112	/* same as STACK_FRAME_OVERHEAD, in fact */
#define FRAME_LR_SAVE	2
#define INT_FRAME_SIZE	(sizeof(struct pt_regs) + STACK_FRAME_OVERHEAD + 288)
#define REGS_MARKER	0x7265677368657265ul
#define FRAME_MARKER	12
#else
#define MIN_STACK_FRAME	16
#define FRAME_LR_SAVE	1
#define INT_FRAME_SIZE	(sizeof(struct pt_regs) + STACK_FRAME_OVERHEAD)
#define REGS_MARKER	0x72656773ul
#define FRAME_MARKER	2
#endif

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long ip, sp;
	int count = 0;

	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	sp = p->thread.ksp;
	if (!validate_sp(sp, p, MIN_STACK_FRAME))
		return 0;

	do {
		sp = *(unsigned long *)sp;
		if (!validate_sp(sp, p, MIN_STACK_FRAME))
			return 0;
		if (count > 0) {
			ip = ((unsigned long *)sp)[FRAME_LR_SAVE];
			if (!in_sched_functions(ip))
				return ip;
		}
	} while (count++ < 16);
	return 0;
}
EXPORT_SYMBOL(get_wchan);

static int kstack_depth_to_print = 64;

void show_stack(struct task_struct *tsk, unsigned long *stack)
{
	unsigned long sp, ip, lr, newsp;
	int count = 0;
	int firstframe = 1;

	sp = (unsigned long) stack;
	if (tsk == NULL)
		tsk = current;
	if (sp == 0) {
		if (tsk == current)
			asm("mr %0,1" : "=r" (sp));
		else
			sp = tsk->thread.ksp;
	}

	lr = 0;
	printk("Call Trace:\n");
	do {
		if (!validate_sp(sp, tsk, MIN_STACK_FRAME))
			return;

		stack = (unsigned long *) sp;
		newsp = stack[0];
		ip = stack[FRAME_LR_SAVE];
		if (!firstframe || ip != lr) {
			printk("["REG"] ["REG"] ", sp, ip);
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
		if (validate_sp(sp, tsk, INT_FRAME_SIZE)
		    && stack[FRAME_MARKER] == REGS_MARKER) {
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
	show_stack(current, NULL);
}
EXPORT_SYMBOL(dump_stack);
