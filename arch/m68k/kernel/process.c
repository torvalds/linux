// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/arch/m68k/kernel/process.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 *
 *  68060 fixes by Jesper Skov
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/reboot.h>
#include <linux/init_task.h>
#include <linux/mqueue.h>
#include <linux/rcupdate.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/setup.h>


asmlinkage void ret_from_fork(void);
asmlinkage void ret_from_kernel_thread(void);

void arch_cpu_idle(void)
{
#if defined(MACH_ATARI_ONLY)
	/* block out HSYNC on the atari (falcon) */
	__asm__("stop #0x2200" : : : "cc");
#else
	__asm__("stop #0x2000" : : : "cc");
#endif
}

void machine_restart(char * __unused)
{
	if (mach_reset)
		mach_reset();
	for (;;);
}

void machine_halt(void)
{
	if (mach_halt)
		mach_halt();
	for (;;);
}

void machine_power_off(void)
{
	do_kernel_power_off();
	for (;;);
}

void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

void show_regs(struct pt_regs * regs)
{
	pr_info("Format %02x  Vector: %04x  PC: %08lx  Status: %04x    %s\n",
		regs->format, regs->vector, regs->pc, regs->sr,
		print_tainted());
	pr_info("ORIG_D0: %08lx  D0: %08lx  A2: %08lx  A1: %08lx\n",
		regs->orig_d0, regs->d0, regs->a2, regs->a1);
	pr_info("A0: %08lx  D5: %08lx  D4: %08lx\n", regs->a0, regs->d5,
		regs->d4);
	pr_info("D3: %08lx  D2: %08lx  D1: %08lx\n", regs->d3, regs->d2,
		regs->d1);
	if (!(regs->sr & PS_S))
		pr_info("USP: %08lx\n", rdusp());
}

void flush_thread(void)
{
	current->thread.fc = USER_DATA;
#ifdef CONFIG_FPU
	if (!FPU_IS_EMU) {
		unsigned long zero = 0;
		asm volatile("frestore %0": :"m" (zero));
	}
#endif
}

/*
 * Why not generic sys_clone, you ask?  m68k passes all arguments on stack.
 * And we need all registers saved, which means a bunch of stuff pushed
 * on top of pt_regs, which means that sys_clone() arguments would be
 * buried.  We could, of course, copy them, but it's too costly for no
 * good reason - generic clone() would have to copy them *again* for
 * kernel_clone() anyway.  So in this case it's actually better to pass pt_regs *
 * and extract arguments for kernel_clone() from there.  Eventually we might
 * go for calling kernel_clone() directly from the wrapper, but only after we
 * are finished with kernel_clone() prototype conversion.
 */
asmlinkage int m68k_clone(struct pt_regs *regs)
{
	/* regs will be equal to current_pt_regs() */
	struct kernel_clone_args args = {
		.flags		= (u32)(regs->d1) & ~CSIGNAL,
		.pidfd		= (int __user *)regs->d3,
		.child_tid	= (int __user *)regs->d4,
		.parent_tid	= (int __user *)regs->d3,
		.exit_signal	= regs->d1 & CSIGNAL,
		.stack		= regs->d2,
		.tls		= regs->d5,
	};

	return kernel_clone(&args);
}

/*
 * Because extra registers are saved on the stack after the sys_clone3()
 * arguments, this C wrapper extracts them from pt_regs * and then calls the
 * generic sys_clone3() implementation.
 */
asmlinkage int m68k_clone3(struct pt_regs *regs)
{
	return sys_clone3((struct clone_args __user *)regs->d1, regs->d2);
}

int copy_thread(struct task_struct *p, const struct kernel_clone_args *args)
{
	unsigned long clone_flags = args->flags;
	unsigned long usp = args->stack;
	unsigned long tls = args->tls;
	struct fork_frame {
		struct switch_stack sw;
		struct pt_regs regs;
	} *frame;

	frame = (struct fork_frame *) (task_stack_page(p) + THREAD_SIZE) - 1;

	p->thread.ksp = (unsigned long)frame;
	p->thread.esp0 = (unsigned long)&frame->regs;

	/*
	 * Must save the current SFC/DFC value, NOT the value when
	 * the parent was last descheduled - RGH  10-08-96
	 */
	p->thread.fc = USER_DATA;

	if (unlikely(args->fn)) {
		/* kernel thread */
		memset(frame, 0, sizeof(struct fork_frame));
		frame->regs.sr = PS_S;
		frame->sw.a3 = (unsigned long)args->fn;
		frame->sw.d7 = (unsigned long)args->fn_arg;
		frame->sw.retpc = (unsigned long)ret_from_kernel_thread;
		p->thread.usp = 0;
		return 0;
	}
	memcpy(frame, container_of(current_pt_regs(), struct fork_frame, regs),
		sizeof(struct fork_frame));
	frame->regs.d0 = 0;
	frame->sw.retpc = (unsigned long)ret_from_fork;
	p->thread.usp = usp ?: rdusp();

	if (clone_flags & CLONE_SETTLS)
		task_thread_info(p)->tp_value = tls;

#ifdef CONFIG_FPU
	if (!FPU_IS_EMU) {
		/* Copy the current fpu state */
		asm volatile ("fsave %0" : : "m" (p->thread.fpstate[0]) : "memory");

		if (!CPU_IS_060 ? p->thread.fpstate[0] : p->thread.fpstate[2]) {
			if (CPU_IS_COLDFIRE) {
				asm volatile ("fmovemd %/fp0-%/fp7,%0\n\t"
					      "fmovel %/fpiar,%1\n\t"
					      "fmovel %/fpcr,%2\n\t"
					      "fmovel %/fpsr,%3"
					      :
					      : "m" (p->thread.fp[0]),
						"m" (p->thread.fpcntl[0]),
						"m" (p->thread.fpcntl[1]),
						"m" (p->thread.fpcntl[2])
					      : "memory");
			} else {
				asm volatile ("fmovemx %/fp0-%/fp7,%0\n\t"
					      "fmoveml %/fpiar/%/fpcr/%/fpsr,%1"
					      :
					      : "m" (p->thread.fp[0]),
						"m" (p->thread.fpcntl[0])
					      : "memory");
			}
		}

		/* Restore the state in case the fpu was busy */
		asm volatile ("frestore %0" : : "m" (p->thread.fpstate[0]));
	}
#endif /* CONFIG_FPU */

	return 0;
}

/* Fill in the fpu structure for a core dump.  */
int dump_fpu (struct pt_regs *regs, struct user_m68kfp_struct *fpu)
{
	if (FPU_IS_EMU) {
		int i;

		memcpy(fpu->fpcntl, current->thread.fpcntl, 12);
		memcpy(fpu->fpregs, current->thread.fp, 96);
		/* Convert internal fpu reg representation
		 * into long double format
		 */
		for (i = 0; i < 24; i += 3)
			fpu->fpregs[i] = ((fpu->fpregs[i] & 0xffff0000) << 15) |
			                 ((fpu->fpregs[i] & 0x0000ffff) << 16);
		return 1;
	}

	if (IS_ENABLED(CONFIG_FPU)) {
		char fpustate[216];

		/* First dump the fpu context to avoid protocol violation.  */
		asm volatile ("fsave %0" :: "m" (fpustate[0]) : "memory");
		if (!CPU_IS_060 ? !fpustate[0] : !fpustate[2])
			return 0;

		if (CPU_IS_COLDFIRE) {
			asm volatile ("fmovel %/fpiar,%0\n\t"
				      "fmovel %/fpcr,%1\n\t"
				      "fmovel %/fpsr,%2\n\t"
				      "fmovemd %/fp0-%/fp7,%3"
				      :
				      : "m" (fpu->fpcntl[0]),
					"m" (fpu->fpcntl[1]),
					"m" (fpu->fpcntl[2]),
					"m" (fpu->fpregs[0])
				      : "memory");
		} else {
			asm volatile ("fmovem %/fpiar/%/fpcr/%/fpsr,%0"
				      :
				      : "m" (fpu->fpcntl[0])
				      : "memory");
			asm volatile ("fmovemx %/fp0-%/fp7,%0"
				      :
				      : "m" (fpu->fpregs[0])
				      : "memory");
		}
	}

	return 1;
}
EXPORT_SYMBOL(dump_fpu);

unsigned long __get_wchan(struct task_struct *p)
{
	unsigned long fp, pc;
	unsigned long stack_page;
	int count = 0;

	stack_page = (unsigned long)task_stack_page(p);
	fp = ((struct switch_stack *)p->thread.ksp)->a6;
	do {
		if (fp < stack_page+sizeof(struct thread_info) ||
		    fp >= 8184+stack_page)
			return 0;
		pc = ((unsigned long *)fp)[1];
		if (!in_sched_functions(pc))
			return pc;
		fp = *(unsigned long *) fp;
	} while (count++ < 16);
	return 0;
}
