/*
 *  arch/s390/kernel/process.c
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Hartmut Penner (hp@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *
 *  Derived from "arch/i386/kernel/process.c"
 *    Copyright (C) 1995, Linus Torvalds
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <linux/compiler.h>
#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/notifier.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/timer.h>

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
 * Need to know about CPUs going idle?
 */
static ATOMIC_NOTIFIER_HEAD(idle_chain);

int register_idle_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&idle_chain, nb);
}
EXPORT_SYMBOL(register_idle_notifier);

int unregister_idle_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&idle_chain, nb);
}
EXPORT_SYMBOL(unregister_idle_notifier);

void do_monitor_call(struct pt_regs *regs, long interruption_code)
{
	/* disable monitor call class 0 */
	__ctl_clear_bit(8, 15);

	atomic_notifier_call_chain(&idle_chain, CPU_NOT_IDLE,
			    (void *)(long) smp_processor_id());
}

extern void s390_handle_mcck(void);
/*
 * The idle loop on a S390...
 */
static void default_idle(void)
{
	int cpu, rc;

	/* CPU is going idle. */
	cpu = smp_processor_id();

	local_irq_disable();
	if (need_resched()) {
		local_irq_enable();
		return;
	}

	rc = atomic_notifier_call_chain(&idle_chain,
			CPU_IDLE, (void *)(long) cpu);
	if (rc != NOTIFY_OK && rc != NOTIFY_DONE)
		BUG();
	if (rc != NOTIFY_OK) {
		local_irq_enable();
		return;
	}

	/* enable monitor call class 0 */
	__ctl_set_bit(8, 15);

#ifdef CONFIG_HOTPLUG_CPU
	if (cpu_is_offline(cpu)) {
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
	/* Wait for external, I/O or machine check interrupt. */
	__load_psw_mask(psw_kernel_bits | PSW_MASK_WAIT |
			PSW_MASK_IO | PSW_MASK_EXT);
}

void cpu_idle(void)
{
	for (;;) {
		while (!need_resched())
			default_idle();

		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

void show_regs(struct pt_regs *regs)
{
	struct task_struct *tsk = current;

        printk("CPU:    %d    %s\n", task_thread_info(tsk)->cpu, print_tainted());
        printk("Process %s (pid: %d, task: %p, ksp: %p)\n",
	       current->comm, current->pid, (void *) tsk,
	       (void *) tsk->thread.ksp);

	show_registers(regs);
	/* Show stack backtrace if pt_regs is from kernel mode */
	if (!(regs->psw.mask & PSW_MASK_PSTATE))
		show_trace(NULL, (unsigned long *) regs->gprs[15]);
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

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
}

void flush_thread(void)
{
	clear_used_math();
	clear_tsk_thread_flag(current, TIF_USEDFPU);
}

void release_thread(struct task_struct *dead_task)
{
}

int copy_thread(int nr, unsigned long clone_flags, unsigned long new_stackp,
	unsigned long unused,
        struct task_struct * p, struct pt_regs * regs)
{
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
        p->thread.user_seg = __pa((unsigned long) p->mm->pgd) | _SEGMENT_TABLE;
	/* Set a new TLS ?  */
	if (clone_flags & CLONE_SETTLS)
		p->thread.acrs[0] = regs->gprs[6];
#else /* CONFIG_64BIT */
	/* Save the fpu registers to new thread structure. */
	save_fp_regs(&p->thread.fp_regs);
        p->thread.user_seg = __pa((unsigned long) p->mm->pgd) | _REGION_TABLE;
	/* Set a new TLS ?  */
	if (clone_flags & CLONE_SETTLS) {
		if (test_thread_flag(TIF_31BIT)) {
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
        memset(&p->thread.per_info,0,sizeof(p->thread.per_info));

        return 0;
}

asmlinkage long sys_fork(void)
{
	struct pt_regs *regs = task_pt_regs(current);
	return do_fork(SIGCHLD, regs->gprs[15], regs, 0, NULL, NULL);
}

asmlinkage long sys_clone(void)
{
	struct pt_regs *regs = task_pt_regs(current);
	unsigned long clone_flags;
	unsigned long newsp;
	int __user *parent_tidptr, *child_tidptr;

	clone_flags = regs->gprs[3];
	newsp = regs->orig_gpr2;
	parent_tidptr = (int __user *) regs->gprs[4];
	child_tidptr = (int __user *) regs->gprs[5];
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
asmlinkage long sys_vfork(void)
{
	struct pt_regs *regs = task_pt_regs(current);
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD,
		       regs->gprs[15], regs, 0, NULL, NULL);
}

asmlinkage void execve_tail(void)
{
	task_lock(current);
	current->ptrace &= ~PT_DTRACE;
	task_unlock(current);
	current->thread.fp_regs.fpc = 0;
	if (MACHINE_HAS_IEEE)
		asm volatile("sfpc %0,%0" : : "d" (0));
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage long sys_execve(void)
{
	struct pt_regs *regs = task_pt_regs(current);
	char *filename;
	unsigned long result;
	int rc;

	filename = getname((char __user *) regs->orig_gpr2);
	if (IS_ERR(filename)) {
		result = PTR_ERR(filename);
		goto out;
	}
	rc = do_execve(filename, (char __user * __user *) regs->gprs[3],
		       (char __user * __user *) regs->gprs[4], regs);
	if (rc) {
		result = rc;
		goto out_putname;
	}
	execve_tail();
	result = regs->gprs[2];
out_putname:
	putname(filename);
out:
	return result;
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

