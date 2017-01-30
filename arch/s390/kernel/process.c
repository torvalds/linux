/*
 * This file handles the architecture dependent parts of process handling.
 *
 *    Copyright IBM Corp. 1999, 2009
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *		 Hartmut Penner <hp@de.ibm.com>,
 *		 Denis Joseph Barrow,
 */

#include <linux/elf-randomize.h>
#include <linux/compiler.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/elfcore.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/tick.h>
#include <linux/personality.h>
#include <linux/syscalls.h>
#include <linux/compat.h>
#include <linux/kprobes.h>
#include <linux/random.h>
#include <linux/module.h>
#include <linux/init_task.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/vtimer.h>
#include <asm/exec.h>
#include <asm/irq.h>
#include <asm/nmi.h>
#include <asm/smp.h>
#include <asm/switch_to.h>
#include <asm/runtime_instr.h>
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
	sf = (struct stack_frame *) tsk->thread.ksp;
	if (sf <= low || sf > high)
		return 0;
	sf = (struct stack_frame *) sf->back_chain;
	if (sf <= low || sf > high)
		return 0;
	return sf->gprs[8];
}

extern void kernel_thread_starter(void);

/*
 * Free current thread data structures etc..
 */
void exit_thread(struct task_struct *tsk)
{
	if (tsk == current)
		exit_thread_runtime_instr();
}

void flush_thread(void)
{
}

void release_thread(struct task_struct *dead_task)
{
}

void arch_release_task_struct(struct task_struct *tsk)
{
}

int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src)
{
	/*
	 * Save the floating-point or vector register state of the current
	 * task and set the CIF_FPU flag to lazy restore the FPU register
	 * state when returning to user space.
	 */
	save_fpu_regs();

	memcpy(dst, src, arch_task_struct_size);
	dst->thread.fpu.regs = dst->thread.fpu.fprs;
	return 0;
}

int copy_thread(unsigned long clone_flags, unsigned long new_stackp,
		unsigned long arg, struct task_struct *p)
{
	struct fake_frame
	{
		struct stack_frame sf;
		struct pt_regs childregs;
	} *frame;

	frame = container_of(task_pt_regs(p), struct fake_frame, childregs);
	p->thread.ksp = (unsigned long) frame;
	/* Save access registers to new thread structure. */
	save_access_regs(&p->thread.acrs[0]);
	/* start new process with ar4 pointing to the correct address space */
	p->thread.mm_segment = get_fs();
	/* Don't copy debug registers */
	memset(&p->thread.per_user, 0, sizeof(p->thread.per_user));
	memset(&p->thread.per_event, 0, sizeof(p->thread.per_event));
	clear_tsk_thread_flag(p, TIF_SINGLE_STEP);
	/* Initialize per thread user and system timer values */
	p->thread.user_timer = 0;
	p->thread.system_timer = 0;

	frame->sf.back_chain = 0;
	/* new return point is ret_from_fork */
	frame->sf.gprs[8] = (unsigned long) ret_from_fork;
	/* fake return stack for resume(), don't go back to schedule */
	frame->sf.gprs[9] = (unsigned long) frame;

	/* Store access registers to kernel stack of new process. */
	if (unlikely(p->flags & PF_KTHREAD)) {
		/* kernel thread */
		memset(&frame->childregs, 0, sizeof(struct pt_regs));
		frame->childregs.psw.mask = PSW_KERNEL_BITS | PSW_MASK_DAT |
				PSW_MASK_IO | PSW_MASK_EXT | PSW_MASK_MCHECK;
		frame->childregs.psw.addr =
				(unsigned long) kernel_thread_starter;
		frame->childregs.gprs[9] = new_stackp; /* function */
		frame->childregs.gprs[10] = arg;
		frame->childregs.gprs[11] = (unsigned long) do_exit;
		frame->childregs.orig_gpr2 = -1;

		return 0;
	}
	frame->childregs = *current_pt_regs();
	frame->childregs.gprs[2] = 0;	/* child returns 0 on fork. */
	frame->childregs.flags = 0;
	if (new_stackp)
		frame->childregs.gprs[15] = new_stackp;

	/* Don't copy runtime instrumentation info */
	p->thread.ri_cb = NULL;
	frame->childregs.psw.mask &= ~PSW_MASK_RI;

	/* Set a new TLS ?  */
	if (clone_flags & CLONE_SETTLS) {
		unsigned long tls = frame->childregs.gprs[6];
		if (is_compat_task()) {
			p->thread.acrs[0] = (unsigned int)tls;
		} else {
			p->thread.acrs[0] = (unsigned int)(tls >> 32);
			p->thread.acrs[1] = (unsigned int)tls;
		}
	}
	return 0;
}

asmlinkage void execve_tail(void)
{
	current->thread.fpu.fpc = 0;
	asm volatile("sfpc %0" : : "d" (0));
}

/*
 * fill in the FPU structure for a core dump.
 */
int dump_fpu (struct pt_regs * regs, s390_fp_regs *fpregs)
{
	save_fpu_regs();
	fpregs->fpc = current->thread.fpu.fpc;
	fpregs->pad = 0;
	if (MACHINE_HAS_VX)
		convert_vx_to_fp((freg_t *)&fpregs->fprs,
				 current->thread.fpu.vxrs);
	else
		memcpy(&fpregs->fprs, current->thread.fpu.fprs,
		       sizeof(fpregs->fprs));
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
	sf = (struct stack_frame *) p->thread.ksp;
	if (sf <= low || sf > high)
		return 0;
	for (count = 0; count < 16; count++) {
		sf = (struct stack_frame *) sf->back_chain;
		if (sf <= low || sf > high)
			return 0;
		return_address = sf->gprs[8];
		if (!in_sched_functions(return_address))
			return return_address;
	}
	return 0;
}

unsigned long arch_align_stack(unsigned long sp)
{
	if (!(current->personality & ADDR_NO_RANDOMIZE) && randomize_va_space)
		sp -= get_random_int() & ~PAGE_MASK;
	return sp & ~0xf;
}

static inline unsigned long brk_rnd(void)
{
	return (get_random_int() & BRK_RND_MASK) << PAGE_SHIFT;
}

unsigned long arch_randomize_brk(struct mm_struct *mm)
{
	unsigned long ret;

	ret = PAGE_ALIGN(mm->brk + brk_rnd());
	return (ret > mm->brk) ? ret : mm->brk;
}
