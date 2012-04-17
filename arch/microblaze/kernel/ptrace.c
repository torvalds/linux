/*
 * `ptrace' system call
 *
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2004-2007 John Williams <john.williams@petalogix.com>
 *
 * derived from arch/v850/kernel/ptrace.c
 *
 * Copyright (C) 2002,03 NEC Electronics Corporation
 * Copyright (C) 2002,03 Miles Bader <miles@gnu.org>
 *
 * Derived from arch/mips/kernel/ptrace.c:
 *
 * Copyright (C) 1992 Ross Biro
 * Copyright (C) Linus Torvalds
 * Copyright (C) 1994, 95, 96, 97, 98, 2000 Ralf Baechle
 * Copyright (C) 1996 David S. Miller
 * Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999 MIPS Technologies, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/signal.h>
#include <linux/elf.h>
#include <linux/audit.h>
#include <linux/seccomp.h>
#include <linux/tracehook.h>

#include <linux/errno.h>
#include <asm/processor.h>
#include <linux/uaccess.h>
#include <asm/asm-offsets.h>
#include <asm/cacheflush.h>
#include <asm/syscall.h>
#include <asm/io.h>

/* Returns the address where the register at REG_OFFS in P is stashed away. */
static microblaze_reg_t *reg_save_addr(unsigned reg_offs,
					struct task_struct *t)
{
	struct pt_regs *regs;

	/*
	 * Three basic cases:
	 *
	 * (1)	A register normally saved before calling the scheduler, is
	 *	available in the kernel entry pt_regs structure at the top
	 *	of the kernel stack. The kernel trap/irq exit path takes
	 *	care to save/restore almost all registers for ptrace'd
	 *	processes.
	 *
	 * (2)	A call-clobbered register, where the process P entered the
	 *	kernel via [syscall] trap, is not stored anywhere; that's
	 *	OK, because such registers are not expected to be preserved
	 *	when the trap returns anyway (so we don't actually bother to
	 *	test for this case).
	 *
	 * (3)	A few registers not used at all by the kernel, and so
	 *	normally never saved except by context-switches, are in the
	 *	context switch state.
	 */

	/* Register saved during kernel entry (or not available). */
	regs = task_pt_regs(t);

	return (microblaze_reg_t *)((char *)regs + reg_offs);
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	int rval;
	unsigned long val = 0;

	switch (request) {
	/* Read/write the word at location ADDR in the registers. */
	case PTRACE_PEEKUSR:
	case PTRACE_POKEUSR:
		pr_debug("PEEKUSR/POKEUSR : 0x%08lx\n", addr);
		rval = 0;
		if (addr >= PT_SIZE && request == PTRACE_PEEKUSR) {
			/*
			 * Special requests that don't actually correspond
			 * to offsets in struct pt_regs.
			 */
			if (addr == PT_TEXT_ADDR) {
				val = child->mm->start_code;
			} else if (addr == PT_DATA_ADDR) {
				val = child->mm->start_data;
			} else if (addr == PT_TEXT_LEN) {
				val = child->mm->end_code
					- child->mm->start_code;
			} else {
				rval = -EIO;
			}
		} else if (addr < PT_SIZE && (addr & 0x3) == 0) {
			microblaze_reg_t *reg_addr = reg_save_addr(addr, child);
			if (request == PTRACE_PEEKUSR)
				val = *reg_addr;
			else {
#if 1
				*reg_addr = data;
#else
				/* MS potential problem on WB system
				 * Be aware that reg_addr is virtual address
				 * virt_to_phys conversion is necessary.
				 * This could be sensible solution.
				 */
				u32 paddr = virt_to_phys((u32)reg_addr);
				invalidate_icache_range(paddr, paddr + 4);
				*reg_addr = data;
				flush_dcache_range(paddr, paddr + 4);
#endif
			}
		} else
			rval = -EIO;

		if (rval == 0 && request == PTRACE_PEEKUSR)
			rval = put_user(val, (unsigned long __user *)data);
		break;
	default:
		rval = ptrace_request(child, request, addr, data);
	}
	return rval;
}

asmlinkage long do_syscall_trace_enter(struct pt_regs *regs)
{
	long ret = 0;

	secure_computing_strict(regs->r12);

	if (test_thread_flag(TIF_SYSCALL_TRACE) &&
	    tracehook_report_syscall_entry(regs))
		/*
		 * Tracing decided this syscall should not happen.
		 * We'll return a bogus call number to get an ENOSYS
		 * error, but leave the original number in regs->regs[0].
		 */
		ret = -1L;

	audit_syscall_entry(EM_MICROBLAZE, regs->r12, regs->r5, regs->r6,
			    regs->r7, regs->r8);

	return ret ?: regs->r12;
}

asmlinkage void do_syscall_trace_leave(struct pt_regs *regs)
{
	int step;

	audit_syscall_exit(regs);

	step = test_thread_flag(TIF_SINGLESTEP);
	if (step || test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, step);
}

#if 0
static asmlinkage void syscall_trace(void)
{
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;
	if (!(current->ptrace & PT_PTRACED))
		return;
	/* The 0x80 provides a way for the tracing parent to distinguish
	 between a syscall stop and SIGTRAP delivery */
	ptrace_notify(SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
				? 0x80 : 0));
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use. strace only continues with a signal if the
	 * stopping signal is not SIGTRAP. -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}
#endif

void ptrace_disable(struct task_struct *child)
{
	/* nothing to do */
}
