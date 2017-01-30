/*  arch/sparc64/kernel/process.c
 *
 *  Copyright (C) 1995, 1996, 2008 David S. Miller (davem@davemloft.net)
 *  Copyright (C) 1996       Eddie C. Dost   (ecd@skynet.be)
 *  Copyright (C) 1997, 1998 Jakub Jelinek   (jj@sunsite.mff.cuni.cz)
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <stdarg.h>

#include <linux/errno.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/delay.h>
#include <linux/compat.h>
#include <linux/tick.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/perf_event.h>
#include <linux/elfcore.h>
#include <linux/sysrq.h>
#include <linux/nmi.h>
#include <linux/context_tracking.h>

#include <linux/uaccess.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/pstate.h>
#include <asm/elf.h>
#include <asm/fpumacro.h>
#include <asm/head.h>
#include <asm/cpudata.h>
#include <asm/mmu_context.h>
#include <asm/unistd.h>
#include <asm/hypervisor.h>
#include <asm/syscalls.h>
#include <asm/irq_regs.h>
#include <asm/smp.h>
#include <asm/pcr.h>

#include "kstack.h"

/* Idle loop support on sparc64. */
void arch_cpu_idle(void)
{
	if (tlb_type != hypervisor) {
		touch_nmi_watchdog();
		local_irq_enable();
	} else {
		unsigned long pstate;

		local_irq_enable();

                /* The sun4v sleeping code requires that we have PSTATE.IE cleared over
                 * the cpu sleep hypervisor call.
                 */
		__asm__ __volatile__(
			"rdpr %%pstate, %0\n\t"
			"andn %0, %1, %0\n\t"
			"wrpr %0, %%g0, %%pstate"
			: "=&r" (pstate)
			: "i" (PSTATE_IE));

		if (!need_resched() && !cpu_is_offline(smp_processor_id()))
			sun4v_cpu_yield();

		/* Re-enable interrupts. */
		__asm__ __volatile__(
			"rdpr %%pstate, %0\n\t"
			"or %0, %1, %0\n\t"
			"wrpr %0, %%g0, %%pstate"
			: "=&r" (pstate)
			: "i" (PSTATE_IE));
	}
}

#ifdef CONFIG_HOTPLUG_CPU
void arch_cpu_idle_dead(void)
{
	sched_preempt_enable_no_resched();
	cpu_play_dead();
}
#endif

#ifdef CONFIG_COMPAT
static void show_regwindow32(struct pt_regs *regs)
{
	struct reg_window32 __user *rw;
	struct reg_window32 r_w;
	mm_segment_t old_fs;
	
	__asm__ __volatile__ ("flushw");
	rw = compat_ptr((unsigned int)regs->u_regs[14]);
	old_fs = get_fs();
	set_fs (USER_DS);
	if (copy_from_user (&r_w, rw, sizeof(r_w))) {
		set_fs (old_fs);
		return;
	}

	set_fs (old_fs);			
	printk("l0: %08x l1: %08x l2: %08x l3: %08x "
	       "l4: %08x l5: %08x l6: %08x l7: %08x\n",
	       r_w.locals[0], r_w.locals[1], r_w.locals[2], r_w.locals[3],
	       r_w.locals[4], r_w.locals[5], r_w.locals[6], r_w.locals[7]);
	printk("i0: %08x i1: %08x i2: %08x i3: %08x "
	       "i4: %08x i5: %08x i6: %08x i7: %08x\n",
	       r_w.ins[0], r_w.ins[1], r_w.ins[2], r_w.ins[3],
	       r_w.ins[4], r_w.ins[5], r_w.ins[6], r_w.ins[7]);
}
#else
#define show_regwindow32(regs)	do { } while (0)
#endif

static void show_regwindow(struct pt_regs *regs)
{
	struct reg_window __user *rw;
	struct reg_window *rwk;
	struct reg_window r_w;
	mm_segment_t old_fs;

	if ((regs->tstate & TSTATE_PRIV) || !(test_thread_flag(TIF_32BIT))) {
		__asm__ __volatile__ ("flushw");
		rw = (struct reg_window __user *)
			(regs->u_regs[14] + STACK_BIAS);
		rwk = (struct reg_window *)
			(regs->u_regs[14] + STACK_BIAS);
		if (!(regs->tstate & TSTATE_PRIV)) {
			old_fs = get_fs();
			set_fs (USER_DS);
			if (copy_from_user (&r_w, rw, sizeof(r_w))) {
				set_fs (old_fs);
				return;
			}
			rwk = &r_w;
			set_fs (old_fs);			
		}
	} else {
		show_regwindow32(regs);
		return;
	}
	printk("l0: %016lx l1: %016lx l2: %016lx l3: %016lx\n",
	       rwk->locals[0], rwk->locals[1], rwk->locals[2], rwk->locals[3]);
	printk("l4: %016lx l5: %016lx l6: %016lx l7: %016lx\n",
	       rwk->locals[4], rwk->locals[5], rwk->locals[6], rwk->locals[7]);
	printk("i0: %016lx i1: %016lx i2: %016lx i3: %016lx\n",
	       rwk->ins[0], rwk->ins[1], rwk->ins[2], rwk->ins[3]);
	printk("i4: %016lx i5: %016lx i6: %016lx i7: %016lx\n",
	       rwk->ins[4], rwk->ins[5], rwk->ins[6], rwk->ins[7]);
	if (regs->tstate & TSTATE_PRIV)
		printk("I7: <%pS>\n", (void *) rwk->ins[7]);
}

void show_regs(struct pt_regs *regs)
{
	show_regs_print_info(KERN_DEFAULT);

	printk("TSTATE: %016lx TPC: %016lx TNPC: %016lx Y: %08x    %s\n", regs->tstate,
	       regs->tpc, regs->tnpc, regs->y, print_tainted());
	printk("TPC: <%pS>\n", (void *) regs->tpc);
	printk("g0: %016lx g1: %016lx g2: %016lx g3: %016lx\n",
	       regs->u_regs[0], regs->u_regs[1], regs->u_regs[2],
	       regs->u_regs[3]);
	printk("g4: %016lx g5: %016lx g6: %016lx g7: %016lx\n",
	       regs->u_regs[4], regs->u_regs[5], regs->u_regs[6],
	       regs->u_regs[7]);
	printk("o0: %016lx o1: %016lx o2: %016lx o3: %016lx\n",
	       regs->u_regs[8], regs->u_regs[9], regs->u_regs[10],
	       regs->u_regs[11]);
	printk("o4: %016lx o5: %016lx sp: %016lx ret_pc: %016lx\n",
	       regs->u_regs[12], regs->u_regs[13], regs->u_regs[14],
	       regs->u_regs[15]);
	printk("RPC: <%pS>\n", (void *) regs->u_regs[15]);
	show_regwindow(regs);
	show_stack(current, (unsigned long *) regs->u_regs[UREG_FP]);
}

union global_cpu_snapshot global_cpu_snapshot[NR_CPUS];
static DEFINE_SPINLOCK(global_cpu_snapshot_lock);

static void __global_reg_self(struct thread_info *tp, struct pt_regs *regs,
			      int this_cpu)
{
	struct global_reg_snapshot *rp;

	flushw_all();

	rp = &global_cpu_snapshot[this_cpu].reg;

	rp->tstate = regs->tstate;
	rp->tpc = regs->tpc;
	rp->tnpc = regs->tnpc;
	rp->o7 = regs->u_regs[UREG_I7];

	if (regs->tstate & TSTATE_PRIV) {
		struct reg_window *rw;

		rw = (struct reg_window *)
			(regs->u_regs[UREG_FP] + STACK_BIAS);
		if (kstack_valid(tp, (unsigned long) rw)) {
			rp->i7 = rw->ins[7];
			rw = (struct reg_window *)
				(rw->ins[6] + STACK_BIAS);
			if (kstack_valid(tp, (unsigned long) rw))
				rp->rpc = rw->ins[7];
		}
	} else {
		rp->i7 = 0;
		rp->rpc = 0;
	}
	rp->thread = tp;
}

/* In order to avoid hangs we do not try to synchronize with the
 * global register dump client cpus.  The last store they make is to
 * the thread pointer, so do a short poll waiting for that to become
 * non-NULL.
 */
static void __global_reg_poll(struct global_reg_snapshot *gp)
{
	int limit = 0;

	while (!gp->thread && ++limit < 100) {
		barrier();
		udelay(1);
	}
}

void arch_trigger_cpumask_backtrace(const cpumask_t *mask, bool exclude_self)
{
	struct thread_info *tp = current_thread_info();
	struct pt_regs *regs = get_irq_regs();
	unsigned long flags;
	int this_cpu, cpu;

	if (!regs)
		regs = tp->kregs;

	spin_lock_irqsave(&global_cpu_snapshot_lock, flags);

	this_cpu = raw_smp_processor_id();

	memset(global_cpu_snapshot, 0, sizeof(global_cpu_snapshot));

	if (cpumask_test_cpu(this_cpu, mask) && !exclude_self)
		__global_reg_self(tp, regs, this_cpu);

	smp_fetch_global_regs();

	for_each_cpu(cpu, mask) {
		struct global_reg_snapshot *gp;

		if (exclude_self && cpu == this_cpu)
			continue;

		gp = &global_cpu_snapshot[cpu].reg;

		__global_reg_poll(gp);

		tp = gp->thread;
		printk("%c CPU[%3d]: TSTATE[%016lx] TPC[%016lx] TNPC[%016lx] TASK[%s:%d]\n",
		       (cpu == this_cpu ? '*' : ' '), cpu,
		       gp->tstate, gp->tpc, gp->tnpc,
		       ((tp && tp->task) ? tp->task->comm : "NULL"),
		       ((tp && tp->task) ? tp->task->pid : -1));

		if (gp->tstate & TSTATE_PRIV) {
			printk("             TPC[%pS] O7[%pS] I7[%pS] RPC[%pS]\n",
			       (void *) gp->tpc,
			       (void *) gp->o7,
			       (void *) gp->i7,
			       (void *) gp->rpc);
		} else {
			printk("             TPC[%lx] O7[%lx] I7[%lx] RPC[%lx]\n",
			       gp->tpc, gp->o7, gp->i7, gp->rpc);
		}

		touch_nmi_watchdog();
	}

	memset(global_cpu_snapshot, 0, sizeof(global_cpu_snapshot));

	spin_unlock_irqrestore(&global_cpu_snapshot_lock, flags);
}

#ifdef CONFIG_MAGIC_SYSRQ

static void sysrq_handle_globreg(int key)
{
	trigger_all_cpu_backtrace();
}

static struct sysrq_key_op sparc_globalreg_op = {
	.handler	= sysrq_handle_globreg,
	.help_msg	= "global-regs(y)",
	.action_msg	= "Show Global CPU Regs",
};

static void __global_pmu_self(int this_cpu)
{
	struct global_pmu_snapshot *pp;
	int i, num;

	if (!pcr_ops)
		return;

	pp = &global_cpu_snapshot[this_cpu].pmu;

	num = 1;
	if (tlb_type == hypervisor &&
	    sun4v_chip_type >= SUN4V_CHIP_NIAGARA4)
		num = 4;

	for (i = 0; i < num; i++) {
		pp->pcr[i] = pcr_ops->read_pcr(i);
		pp->pic[i] = pcr_ops->read_pic(i);
	}
}

static void __global_pmu_poll(struct global_pmu_snapshot *pp)
{
	int limit = 0;

	while (!pp->pcr[0] && ++limit < 100) {
		barrier();
		udelay(1);
	}
}

static void pmu_snapshot_all_cpus(void)
{
	unsigned long flags;
	int this_cpu, cpu;

	spin_lock_irqsave(&global_cpu_snapshot_lock, flags);

	memset(global_cpu_snapshot, 0, sizeof(global_cpu_snapshot));

	this_cpu = raw_smp_processor_id();

	__global_pmu_self(this_cpu);

	smp_fetch_global_pmu();

	for_each_online_cpu(cpu) {
		struct global_pmu_snapshot *pp = &global_cpu_snapshot[cpu].pmu;

		__global_pmu_poll(pp);

		printk("%c CPU[%3d]: PCR[%08lx:%08lx:%08lx:%08lx] PIC[%08lx:%08lx:%08lx:%08lx]\n",
		       (cpu == this_cpu ? '*' : ' '), cpu,
		       pp->pcr[0], pp->pcr[1], pp->pcr[2], pp->pcr[3],
		       pp->pic[0], pp->pic[1], pp->pic[2], pp->pic[3]);

		touch_nmi_watchdog();
	}

	memset(global_cpu_snapshot, 0, sizeof(global_cpu_snapshot));

	spin_unlock_irqrestore(&global_cpu_snapshot_lock, flags);
}

static void sysrq_handle_globpmu(int key)
{
	pmu_snapshot_all_cpus();
}

static struct sysrq_key_op sparc_globalpmu_op = {
	.handler	= sysrq_handle_globpmu,
	.help_msg	= "global-pmu(x)",
	.action_msg	= "Show Global PMU Regs",
};

static int __init sparc_sysrq_init(void)
{
	int ret = register_sysrq_key('y', &sparc_globalreg_op);

	if (!ret)
		ret = register_sysrq_key('x', &sparc_globalpmu_op);
	return ret;
}

core_initcall(sparc_sysrq_init);

#endif

unsigned long thread_saved_pc(struct task_struct *tsk)
{
	struct thread_info *ti = task_thread_info(tsk);
	unsigned long ret = 0xdeadbeefUL;
	
	if (ti && ti->ksp) {
		unsigned long *sp;
		sp = (unsigned long *)(ti->ksp + STACK_BIAS);
		if (((unsigned long)sp & (sizeof(long) - 1)) == 0UL &&
		    sp[14]) {
			unsigned long *fp;
			fp = (unsigned long *)(sp[14] + STACK_BIAS);
			if (((unsigned long)fp & (sizeof(long) - 1)) == 0UL)
				ret = fp[15];
		}
	}
	return ret;
}

/* Free current thread data structures etc.. */
void exit_thread(struct task_struct *tsk)
{
	struct thread_info *t = task_thread_info(tsk);

	if (t->utraps) {
		if (t->utraps[0] < 2)
			kfree (t->utraps);
		else
			t->utraps[0]--;
	}
}

void flush_thread(void)
{
	struct thread_info *t = current_thread_info();
	struct mm_struct *mm;

	mm = t->task->mm;
	if (mm)
		tsb_context_switch(mm);

	set_thread_wsaved(0);

	/* Clear FPU register state. */
	t->fpsaved[0] = 0;
}

/* It's a bit more tricky when 64-bit tasks are involved... */
static unsigned long clone_stackframe(unsigned long csp, unsigned long psp)
{
	bool stack_64bit = test_thread_64bit_stack(psp);
	unsigned long fp, distance, rval;

	if (stack_64bit) {
		csp += STACK_BIAS;
		psp += STACK_BIAS;
		__get_user(fp, &(((struct reg_window __user *)psp)->ins[6]));
		fp += STACK_BIAS;
		if (test_thread_flag(TIF_32BIT))
			fp &= 0xffffffff;
	} else
		__get_user(fp, &(((struct reg_window32 __user *)psp)->ins[6]));

	/* Now align the stack as this is mandatory in the Sparc ABI
	 * due to how register windows work.  This hides the
	 * restriction from thread libraries etc.
	 */
	csp &= ~15UL;

	distance = fp - psp;
	rval = (csp - distance);
	if (copy_in_user((void __user *) rval, (void __user *) psp, distance))
		rval = 0;
	else if (!stack_64bit) {
		if (put_user(((u32)csp),
			     &(((struct reg_window32 __user *)rval)->ins[6])))
			rval = 0;
	} else {
		if (put_user(((u64)csp - STACK_BIAS),
			     &(((struct reg_window __user *)rval)->ins[6])))
			rval = 0;
		else
			rval = rval - STACK_BIAS;
	}

	return rval;
}

/* Standard stuff. */
static inline void shift_window_buffer(int first_win, int last_win,
				       struct thread_info *t)
{
	int i;

	for (i = first_win; i < last_win; i++) {
		t->rwbuf_stkptrs[i] = t->rwbuf_stkptrs[i+1];
		memcpy(&t->reg_window[i], &t->reg_window[i+1],
		       sizeof(struct reg_window));
	}
}

void synchronize_user_stack(void)
{
	struct thread_info *t = current_thread_info();
	unsigned long window;

	flush_user_windows();
	if ((window = get_thread_wsaved()) != 0) {
		window -= 1;
		do {
			struct reg_window *rwin = &t->reg_window[window];
			int winsize = sizeof(struct reg_window);
			unsigned long sp;

			sp = t->rwbuf_stkptrs[window];

			if (test_thread_64bit_stack(sp))
				sp += STACK_BIAS;
			else
				winsize = sizeof(struct reg_window32);

			if (!copy_to_user((char __user *)sp, rwin, winsize)) {
				shift_window_buffer(window, get_thread_wsaved() - 1, t);
				set_thread_wsaved(get_thread_wsaved() - 1);
			}
		} while (window--);
	}
}

static void stack_unaligned(unsigned long sp)
{
	siginfo_t info;

	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRALN;
	info.si_addr = (void __user *) sp;
	info.si_trapno = 0;
	force_sig_info(SIGBUS, &info, current);
}

void fault_in_user_windows(void)
{
	struct thread_info *t = current_thread_info();
	unsigned long window;

	flush_user_windows();
	window = get_thread_wsaved();

	if (likely(window != 0)) {
		window -= 1;
		do {
			struct reg_window *rwin = &t->reg_window[window];
			int winsize = sizeof(struct reg_window);
			unsigned long sp;

			sp = t->rwbuf_stkptrs[window];

			if (test_thread_64bit_stack(sp))
				sp += STACK_BIAS;
			else
				winsize = sizeof(struct reg_window32);

			if (unlikely(sp & 0x7UL))
				stack_unaligned(sp);

			if (unlikely(copy_to_user((char __user *)sp,
						  rwin, winsize)))
				goto barf;
		} while (window--);
	}
	set_thread_wsaved(0);
	return;

barf:
	set_thread_wsaved(window + 1);
	user_exit();
	do_exit(SIGILL);
}

asmlinkage long sparc_do_fork(unsigned long clone_flags,
			      unsigned long stack_start,
			      struct pt_regs *regs,
			      unsigned long stack_size)
{
	int __user *parent_tid_ptr, *child_tid_ptr;
	unsigned long orig_i1 = regs->u_regs[UREG_I1];
	long ret;

#ifdef CONFIG_COMPAT
	if (test_thread_flag(TIF_32BIT)) {
		parent_tid_ptr = compat_ptr(regs->u_regs[UREG_I2]);
		child_tid_ptr = compat_ptr(regs->u_regs[UREG_I4]);
	} else
#endif
	{
		parent_tid_ptr = (int __user *) regs->u_regs[UREG_I2];
		child_tid_ptr = (int __user *) regs->u_regs[UREG_I4];
	}

	ret = do_fork(clone_flags, stack_start, stack_size,
		      parent_tid_ptr, child_tid_ptr);

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
 */
int copy_thread(unsigned long clone_flags, unsigned long sp,
		unsigned long arg, struct task_struct *p)
{
	struct thread_info *t = task_thread_info(p);
	struct pt_regs *regs = current_pt_regs();
	struct sparc_stackf *parent_sf;
	unsigned long child_stack_sz;
	char *child_trap_frame;

	/* Calculate offset to stack_frame & pt_regs */
	child_stack_sz = (STACKFRAME_SZ + TRACEREG_SZ);
	child_trap_frame = (task_stack_page(p) +
			    (THREAD_SIZE - child_stack_sz));

	t->new_child = 1;
	t->ksp = ((unsigned long) child_trap_frame) - STACK_BIAS;
	t->kregs = (struct pt_regs *) (child_trap_frame +
				       sizeof(struct sparc_stackf));
	t->fpsaved[0] = 0;

	if (unlikely(p->flags & PF_KTHREAD)) {
		memset(child_trap_frame, 0, child_stack_sz);
		__thread_flag_byte_ptr(t)[TI_FLAG_BYTE_CWP] = 
			(current_pt_regs()->tstate + 1) & TSTATE_CWP;
		t->current_ds = ASI_P;
		t->kregs->u_regs[UREG_G1] = sp; /* function */
		t->kregs->u_regs[UREG_G2] = arg;
		return 0;
	}

	parent_sf = ((struct sparc_stackf *) regs) - 1;
	memcpy(child_trap_frame, parent_sf, child_stack_sz);
	if (t->flags & _TIF_32BIT) {
		sp &= 0x00000000ffffffffUL;
		regs->u_regs[UREG_FP] &= 0x00000000ffffffffUL;
	}
	t->kregs->u_regs[UREG_FP] = sp;
	__thread_flag_byte_ptr(t)[TI_FLAG_BYTE_CWP] = 
		(regs->tstate + 1) & TSTATE_CWP;
	t->current_ds = ASI_AIUS;
	if (sp != regs->u_regs[UREG_FP]) {
		unsigned long csp;

		csp = clone_stackframe(sp, regs->u_regs[UREG_FP]);
		if (!csp)
			return -EFAULT;
		t->kregs->u_regs[UREG_FP] = csp;
	}
	if (t->utraps)
		t->utraps[0]++;

	/* Set the return value for the child. */
	t->kregs->u_regs[UREG_I0] = current->pid;
	t->kregs->u_regs[UREG_I1] = 1;

	/* Set the second return value for the parent. */
	regs->u_regs[UREG_I1] = 0;

	if (clone_flags & CLONE_SETTLS)
		t->kregs->u_regs[UREG_G7] = regs->u_regs[UREG_I3];

	return 0;
}

typedef struct {
	union {
		unsigned int	pr_regs[32];
		unsigned long	pr_dregs[16];
	} pr_fr;
	unsigned int __unused;
	unsigned int	pr_fsr;
	unsigned char	pr_qcnt;
	unsigned char	pr_q_entrysize;
	unsigned char	pr_en;
	unsigned int	pr_q[64];
} elf_fpregset_t32;

/*
 * fill in the fpu structure for a core dump.
 */
int dump_fpu (struct pt_regs * regs, elf_fpregset_t * fpregs)
{
	unsigned long *kfpregs = current_thread_info()->fpregs;
	unsigned long fprs = current_thread_info()->fpsaved[0];

	if (test_thread_flag(TIF_32BIT)) {
		elf_fpregset_t32 *fpregs32 = (elf_fpregset_t32 *)fpregs;

		if (fprs & FPRS_DL)
			memcpy(&fpregs32->pr_fr.pr_regs[0], kfpregs,
			       sizeof(unsigned int) * 32);
		else
			memset(&fpregs32->pr_fr.pr_regs[0], 0,
			       sizeof(unsigned int) * 32);
		fpregs32->pr_qcnt = 0;
		fpregs32->pr_q_entrysize = 8;
		memset(&fpregs32->pr_q[0], 0,
		       (sizeof(unsigned int) * 64));
		if (fprs & FPRS_FEF) {
			fpregs32->pr_fsr = (unsigned int) current_thread_info()->xfsr[0];
			fpregs32->pr_en = 1;
		} else {
			fpregs32->pr_fsr = 0;
			fpregs32->pr_en = 0;
		}
	} else {
		if(fprs & FPRS_DL)
			memcpy(&fpregs->pr_regs[0], kfpregs,
			       sizeof(unsigned int) * 32);
		else
			memset(&fpregs->pr_regs[0], 0,
			       sizeof(unsigned int) * 32);
		if(fprs & FPRS_DU)
			memcpy(&fpregs->pr_regs[16], kfpregs+16,
			       sizeof(unsigned int) * 32);
		else
			memset(&fpregs->pr_regs[16], 0,
			       sizeof(unsigned int) * 32);
		if(fprs & FPRS_FEF) {
			fpregs->pr_fsr = current_thread_info()->xfsr[0];
			fpregs->pr_gsr = current_thread_info()->gsr[0];
		} else {
			fpregs->pr_fsr = fpregs->pr_gsr = 0;
		}
		fpregs->pr_fprs = fprs;
	}
	return 1;
}
EXPORT_SYMBOL(dump_fpu);

unsigned long get_wchan(struct task_struct *task)
{
	unsigned long pc, fp, bias = 0;
	struct thread_info *tp;
	struct reg_window *rw;
        unsigned long ret = 0;
	int count = 0; 

	if (!task || task == current ||
            task->state == TASK_RUNNING)
		goto out;

	tp = task_thread_info(task);
	bias = STACK_BIAS;
	fp = task_thread_info(task)->ksp + bias;

	do {
		if (!kstack_valid(tp, fp))
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
