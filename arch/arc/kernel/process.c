/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Amit Bhor, Kanika Nema: Codito Technologies 2004
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/elf.h>
#include <linux/tick.h>

SYSCALL_DEFINE1(arc_settls, void *, user_tls_data_ptr)
{
	task_thread_info(current)->thr_ptr = (unsigned int)user_tls_data_ptr;
	return 0;
}

/*
 * We return the user space TLS data ptr as sys-call return code
 * Ideally it should be copy to user.
 * However we can cheat by the fact that some sys-calls do return
 * absurdly high values
 * Since the tls dat aptr is not going to be in range of 0xFFFF_xxxx
 * it won't be considered a sys-call error
 * and it will be loads better than copy-to-user, which is a definite
 * D-TLB Miss
 */
SYSCALL_DEFINE0(arc_gettls)
{
	return task_thread_info(current)->thr_ptr;
}

void arch_cpu_idle(void)
{
	/* sleep, but enable all interrupts before committing */
	__asm__ __volatile__(
		"sleep %0	\n"
		:
		:"I"(ISA_SLEEP_ARG)); /* can't be "r" has to be embedded const */
}

asmlinkage void ret_from_fork(void);

/*
 * Copy architecture-specific thread state
 *
 * Layout of Child kernel mode stack as setup at the end of this function is
 *
 * |     ...        |
 * |     ...        |
 * |    unused      |
 * |                |
 * ------------------
 * |     r25        |   <==== top of Stack (thread.ksp)
 * ~                ~
 * |    --to--      |   (CALLEE Regs of kernel mode)
 * |     r13        |
 * ------------------
 * |     fp         |
 * |    blink       |   @ret_from_fork
 * ------------------
 * |                |
 * ~                ~
 * ~                ~
 * |                |
 * ------------------
 * |     r12        |
 * ~                ~
 * |    --to--      |   (scratch Regs of user mode)
 * |     r0         |
 * ------------------
 * |      SP        |
 * |    orig_r0     |
 * |    event/ECR   |
 * |    user_r25    |
 * ------------------  <===== END of PAGE
 */
int copy_thread(unsigned long clone_flags,
		unsigned long usp, unsigned long kthread_arg,
		struct task_struct *p)
{
	struct pt_regs *c_regs;        /* child's pt_regs */
	unsigned long *childksp;       /* to unwind out of __switch_to() */
	struct callee_regs *c_callee;  /* child's callee regs */
	struct callee_regs *parent_callee;  /* paren't callee */
	struct pt_regs *regs = current_pt_regs();

	/* Mark the specific anchors to begin with (see pic above) */
	c_regs = task_pt_regs(p);
	childksp = (unsigned long *)c_regs - 2;  /* 2 words for FP/BLINK */
	c_callee = ((struct callee_regs *)childksp) - 1;

	/*
	 * __switch_to() uses thread.ksp to start unwinding stack
	 * For kernel threads we don't need to create callee regs, the
	 * stack layout nevertheless needs to remain the same.
	 * Also, since __switch_to anyways unwinds callee regs, we use
	 * this to populate kernel thread entry-pt/args into callee regs,
	 * so that ret_from_kernel_thread() becomes simpler.
	 */
	p->thread.ksp = (unsigned long)c_callee;	/* THREAD_KSP */

	/* __switch_to expects FP(0), BLINK(return addr) at top */
	childksp[0] = 0;			/* fp */
	childksp[1] = (unsigned long)ret_from_fork; /* blink */

	if (unlikely(p->flags & PF_KTHREAD)) {
		memset(c_regs, 0, sizeof(struct pt_regs));

		c_callee->r13 = kthread_arg;
		c_callee->r14 = usp;  /* function */

		return 0;
	}

	/*--------- User Task Only --------------*/

	/* __switch_to expects FP(0), BLINK(return addr) at top of stack */
	childksp[0] = 0;				/* for POP fp */
	childksp[1] = (unsigned long)ret_from_fork;	/* for POP blink */

	/* Copy parents pt regs on child's kernel mode stack */
	*c_regs = *regs;

	if (usp)
		c_regs->sp = usp;

	c_regs->r0 = 0;		/* fork returns 0 in child */

	parent_callee = ((struct callee_regs *)regs) - 1;
	*c_callee = *parent_callee;

	if (unlikely(clone_flags & CLONE_SETTLS)) {
		/*
		 * set task's userland tls data ptr from 4th arg
		 * clone C-lib call is difft from clone sys-call
		 */
		task_thread_info(p)->thr_ptr = regs->r3;
	} else {
		/* Normal fork case: set parent's TLS ptr in child */
		task_thread_info(p)->thr_ptr =
		task_thread_info(current)->thr_ptr;
	}

	return 0;
}

/*
 * Do necessary setup to start up a new user task
 */
void start_thread(struct pt_regs * regs, unsigned long pc, unsigned long usp)
{
	regs->sp = usp;
	regs->ret = pc;

	/*
	 * [U]ser Mode bit set
	 * [L] ZOL loop inhibited to begin with - cleared by a LP insn
	 * Interrupts enabled
	 */
	regs->status32 = STATUS_U_MASK | STATUS_L_MASK | ISA_INIT_STATUS_BITS;

	/* bogus seed values for debugging */
	regs->lp_start = 0x10;
	regs->lp_end = 0x80;
}

/*
 * Some archs flush debug and FPU info here
 */
void flush_thread(void)
{
}

int dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpu)
{
	return 0;
}

int elf_check_arch(const struct elf32_hdr *x)
{
	unsigned int eflags;

	if (x->e_machine != EM_ARC_INUSE) {
		pr_err("ELF not built for %s ISA\n",
			is_isa_arcompact() ? "ARCompact":"ARCv2");
		return 0;
	}

	eflags = x->e_flags;
	if ((eflags & EF_ARC_OSABI_MSK) != EF_ARC_OSABI_CURRENT) {
		pr_err("ABI mismatch - you need newer toolchain\n");
		force_sigsegv(SIGSEGV, current);
		return 0;
	}

	return 1;
}
EXPORT_SYMBOL(elf_check_arch);
