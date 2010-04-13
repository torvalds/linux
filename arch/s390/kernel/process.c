/*
 * This file handles the architecture dependent parts of process handling.
 *
 *    Copyright IBM Corp. 1999,2009
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *		 Hartmut Penner <hp@de.ibm.com>,
 *		 Denis Joseph Barrow,
 */

#include <linux/compiler.h>
#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/vmalloc.h>
#include <linux/user.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/tick.h>
#include <linux/elfcore.h>
#include <linux/kernel_stat.h>
#include <linux/syscalls.h>
#include <linux/compat.h>
#include <asm/compat.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/timer.h>
#include <asm/nmi.h>
#include "entry.h"

asmlinkage void ret_from_fork(void) asm ("ret_from_fork");

/*
 * Return saved PC of a blocked thread. used in kernel/sched.
 * resume in entry.S does not create a new stack frame, it
 * just stores the registers %r6-%r15 to the frame given by
 * schedule. We want to return the address of the caller of
 * schedule, so we have to walk the backchain one time to
 * find the frame schedule() store its return address.
 */
unsigned long thread_saved_pc(struct task_struct *tsk)
{
	struct stack_frame *sf, *low, *high;

	if (!tsk || !task_stack_page(tsk))
		return 0;
	low = task_stack_page(tsk);
	high = (struct stack_frame *) task_pt_regs(tsk);
	sf = (struct stack_frame *) (tsk->thread.ksp & PSW_ADDR_INSN);
	if (sf <= low || sf > high)
		return 0;
	sf = (struct stack_frame *) (sf->back_chain & PSW_ADDR_INSN);
	if (sf <= low || sf > high)
		return 0;
	return sf->gprs[8];
}

/*
 * The idle loop on a S390...
 */
static void default_idle(void)
{
	/* CPU is going idle. */
	local_irq_disable();
	if (need_resched()) {
		local_irq_enable();
		return;
	}
#ifdef CONFIG_HOTPLUG_CPU
	if (cpu_is_offline(smp_processor_id())) {
		preempt_enable_no_resched();
		cpu_die();
	}
#endif
	local_mcck_disable();
	if (test_thread_flag(TIF_MCCK_PENDING)) {
		local_mcck_enable();
		local_irq_enable();
		s390_handle_mcck();
		return;
	}
	trace_hardirqs_on();
	/* Don't trace preempt off for idle. */
	stop_critical_timings();
	/* Stop virtual timer and halt the cpu. */
	vtime_stop_cpu();
	/* Reenable preemption tracer. */
	start_critical_timings();
}

void cpu_idle(void)
{
	for (;;) {
		tick_nohz_stop_sched_tick(1);
		while (!need_resched())
			default_idle();
		tick_nohz_restart_sched_tick();
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

extern void kernel_thread_starter(void);

asm(
	".align 4\n"
	"kernel_thread_starter:\n"
	"    la    2,0(10)\n"
	"    basr  14,9\n"
	"    la    2,0\n"
	"    br    11\n");

int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));
	regs.psw.mask = psw_kernel_bits | PSW_MASK_IO | PSW_MASK_EXT;
	regs.psw.addr = (unsigned long) kernel_thread_starter | PSW_ADDR_AMODE;
	regs.gprs[9] = (unsigned long) fn;
	regs.gprs[10] = (unsigned long) arg;
	regs.gprs[11] = (unsigned long) do_exit;
	regs.orig_gpr2 = -1;

	/* Ok, create the new process.. */
	return do_fork(flags | CLONE_VM | CLONE_UNTRACED,
		       0, &regs, 0, NULL, NULL);
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
}

void release_thread(struct task_struct *dead_task)
{
}

int copy_thread(unsigned long clone_flags, unsigned long new_stackp,
		unsigned long unused,
		struct task_struct *p, struct pt_regs *regs)
{
	struct thread_info *ti;
	struct fake_frame
	{
		struct stack_frame sf;
		struct pt_regs childregs;
	} *frame;

	frame = container_of(task_pt_regs(p), struct fake_frame, childregs);
	p->thread.ksp = (unsigned long) frame;
	/* Store access registers to kernel stack of new process. */
	frame->childregs = *regs;
	frame->childregs.gprs[2] = 0;	/* child returns 0 on fork. */
	frame->childregs.gprs[15] = new_stackp;
	frame->sf.back_chain = 0;

	/* new return point is ret_from_fork */
	frame->sf.gprs[8] = (unsigned long) ret_from_fork;

	/* fake return stack for resume(), don't go back to schedule */
	frame->sf.gprs[9] = (unsigned long) frame;

	/* Save access registers to new thread structure. */
	save_access_regs(&p->thread.acrs[0]);

#ifndef CONFIG_64BIT
	/*
	 * save fprs to current->thread.fp_regs to merge them with
	 * the emulated registers and then copy the result to the child.
	 */
	save_fp_regs(&current->thread.fp_regs);
	memcpy(&p->thread.fp_regs, &current->thread.fp_regs,
	       sizeof(s390_fp_regs));
	/* Set a new TLS ?  */
	if (clone_flags & CLONE_SETTLS)
		p->thread.acrs[0] = regs->gprs[6];
#else /* CONFIG_64BIT */
	/* Save the fpu registers to new thread structure. */
	save_fp_regs(&p->thread.fp_regs);
	/* Set a new TLS ?  */
	if (clone_flags & CLONE_SETTLS) {
		if (is_compat_task()) {
			p->thread.acrs[0] = (unsigned int) regs->gprs[6];
		} else {
			p->thread.acrs[0] = (unsigned int)(regs->gprs[6] >> 32);
			p->thread.acrs[1] = (unsigned int) regs->gprs[6];
		}
	}
#endif /* CONFIG_64BIT */
	/* start new process with ar4 pointing to the correct address space */
	p->thread.mm_segment = get_fs();
	/* Don't copy debug registers */
	memset(&p->thread.per_info, 0, sizeof(p->thread.per_info));
	clear_tsk_thread_flag(p, TIF_SINGLE_STEP);
	/* Initialize per thread user and system timer values */
	ti = task_thread_info(p);
	ti->user_timer = 0;
	ti->system_timer = 0;
	return 0;
}

SYSCALL_DEFINE0(fork)
{
	struct pt_regs *regs = task_pt_regs(current);
	return do_fork(SIGCHLD, regs->gprs[15], regs, 0, NULL, NULL);
}

SYSCALL_DEFINE4(clone, unsigned long, newsp, unsigned long, clone_flags,
		int __user *, parent_tidptr, int __user *, child_tidptr)
{
	struct pt_regs *regs = task_pt_regs(current);

	if (!newsp)
		newsp = regs->gprs[15];
	return do_fork(clone_flags, newsp, regs, 0,
		       parent_tidptr, child_tidptr);
}

/*
 * This is trivial, and on the face of it looks like it
 * could equally well be done in user mode.
 *
 * Not so, for quite unobvious reasons - register pressure.
 * In user mode vfork() cannot have a stack frame, and if
 * done by calling the "clone()" system call directly, you
 * do not have enough call-clobbered registers to hold all
 * the information you need.
 */
SYSCALL_DEFINE0(vfork)
{
	struct pt_regs *regs = task_pt_regs(current);
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD,
		       regs->gprs[15], regs, 0, NULL, NULL);
}

asmlinkage void execve_tail(void)
{
	current->thread.fp_regs.fpc = 0;
	if (MACHINE_HAS_IEEE)
		asm volatile("sfpc %0,%0" : : "d" (0));
}

/*
 * sys_execve() executes a new program.
 */
SYSCALL_DEFINE3(execve, char __user *, name, char __user * __user *, argv,
		char __user * __user *, envp)
{
	struct pt_regs *regs = task_pt_regs(current);
	char *filename;
	long rc;

	filename = getname(name);
	rc = PTR_ERR(filename);
	if (IS_ERR(filename))
		return rc;
	rc = do_execve(filename, argv, envp, regs);
	if (rc)
		goto out;
	execve_tail();
	rc = regs->gprs[2];
out:
	putname(filename);
	return rc;
}

/*
 * fill in the FPU structure for a core dump.
 */
int dump_fpu (struct pt_regs * regs, s390_fp_regs *fpregs)
{
#ifndef CONFIG_64BIT
	/*
	 * save fprs to current->thread.fp_regs to merge them with
	 * the emulated registers and then copy the result to the dump.
	 */
	save_fp_regs(&current->thread.fp_regs);
	memcpy(fpregs, &current->thread.fp_regs, sizeof(s390_fp_regs));
#else /* CONFIG_64BIT */
	save_fp_regs(fpregs);
#endif /* CONFIG_64BIT */
	return 1;
}
EXPORT_SYMBOL(dump_fpu);

unsigned long get_wchan(struct task_struct *p)
{
	struct stack_frame *sf, *low, *high;
	unsigned long return_address;
	int count;

	if (!p || p == current || p->state == TASK_RUNNING || !task_stack_page(p))
		return 0;
	low = task_stack_page(p);
	high = (struct stack_frame *) task_pt_regs(p);
	sf = (struct stack_frame *) (p->thread.ksp & PSW_ADDR_INSN);
	if (sf <= low || sf > high)
		return 0;
	for (count = 0; count < 16; count++) {
		sf = (struct stack_frame *) (sf->back_chain & PSW_ADDR_INSN);
		if (sf <= low || sf > high)
			return 0;
		return_address = sf->gprs[8] & PSW_ADDR_INSN;
		if (!in_sched_functions(return_address))
			return return_address;
	}
	return 0;
}
