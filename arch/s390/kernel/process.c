// SPDX-License-Identifier: GPL-2.0
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
#include <linux/sched/debug.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
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
#include <linux/export.h>
#include <linux/init_task.h>
#include <linux/entry-common.h>
#include <asm/cpu_mf.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/vtimer.h>
#include <asm/exec.h>
#include <asm/irq.h>
#include <asm/nmi.h>
#include <asm/smp.h>
#include <asm/stacktrace.h>
#include <asm/switch_to.h>
#include <asm/runtime_instr.h>
#include <asm/unwind.h>
#include "entry.h"

void ret_from_fork(void) asm("ret_from_fork");

void __ret_from_fork(struct task_struct *prev, struct pt_regs *regs)
{
	void (*func)(void *arg);

	schedule_tail(prev);

	if (!user_mode(regs)) {
		/* Kernel thread */
		func = (void *)regs->gprs[9];
		func((void *)regs->gprs[10]);
	}
	clear_pt_regs_flag(regs, PIF_SYSCALL);
	syscall_exit_to_user_mode(regs);
}

void flush_thread(void)
{
}

void arch_setup_new_exec(void)
{
	if (S390_lowcore.current_pid != current->pid) {
		S390_lowcore.current_pid = current->pid;
		if (test_facility(40))
			lpp(&S390_lowcore.lpp);
	}
}

void arch_release_task_struct(struct task_struct *tsk)
{
	runtime_instr_release(tsk);
	guarded_storage_release(tsk);
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
		unsigned long arg, struct task_struct *p, unsigned long tls)
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
	/* Don't copy debug registers */
	memset(&p->thread.per_user, 0, sizeof(p->thread.per_user));
	memset(&p->thread.per_event, 0, sizeof(p->thread.per_event));
	clear_tsk_thread_flag(p, TIF_SINGLE_STEP);
	p->thread.per_flags = 0;
	/* Initialize per thread user and system timer values */
	p->thread.user_timer = 0;
	p->thread.guest_timer = 0;
	p->thread.system_timer = 0;
	p->thread.hardirq_timer = 0;
	p->thread.softirq_timer = 0;
	p->thread.last_break = 1;

	frame->sf.back_chain = 0;
	frame->sf.gprs[5] = (unsigned long)frame + sizeof(struct stack_frame);
	frame->sf.gprs[6] = (unsigned long)p;
	/* new return point is ret_from_fork */
	frame->sf.gprs[8] = (unsigned long)ret_from_fork;
	/* fake return stack for resume(), don't go back to schedule */
	frame->sf.gprs[9] = (unsigned long)frame;

	/* Store access registers to kernel stack of new process. */
	if (unlikely(p->flags & (PF_KTHREAD | PF_IO_WORKER))) {
		/* kernel thread */
		memset(&frame->childregs, 0, sizeof(struct pt_regs));
		frame->childregs.psw.mask = PSW_KERNEL_BITS | PSW_MASK_DAT |
				PSW_MASK_IO | PSW_MASK_EXT | PSW_MASK_MCHECK;
		frame->childregs.psw.addr =
				(unsigned long)__ret_from_fork;
		frame->childregs.gprs[9] = new_stackp; /* function */
		frame->childregs.gprs[10] = arg;
		frame->childregs.gprs[11] = (unsigned long)do_exit;
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
	/* Don't copy guarded storage control block */
	p->thread.gs_cb = NULL;
	p->thread.gs_bc_cb = NULL;

	/* Set a new TLS ?  */
	if (clone_flags & CLONE_SETTLS) {
		if (is_compat_task()) {
			p->thread.acrs[0] = (unsigned int)tls;
		} else {
			p->thread.acrs[0] = (unsigned int)(tls >> 32);
			p->thread.acrs[1] = (unsigned int)tls;
		}
	}
	/*
	 * s390 stores the svc return address in arch_data when calling
	 * sigreturn()/restart_syscall() via vdso. 1 means no valid address
	 * stored.
	 */
	p->restart_block.arch_data = 1;
	return 0;
}

void execve_tail(void)
{
	current->thread.fpu.fpc = 0;
	asm volatile("sfpc %0" : : "d" (0));
}

unsigned long get_wchan(struct task_struct *p)
{
	struct unwind_state state;
	unsigned long ip = 0;

	if (!p || p == current || task_is_running(p) || !task_stack_page(p))
		return 0;

	if (!try_get_task_stack(p))
		return 0;

	unwind_for_each_frame(&state, p, NULL, 0) {
		if (state.stack_info.type != STACK_TYPE_TASK) {
			ip = 0;
			break;
		}

		ip = unwind_get_return_address(&state);
		if (!ip)
			break;

		if (!in_sched_functions(ip))
			break;
	}

	put_task_stack(p);
	return ip;
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
