/* traps.c: high-level exception handler for FR-V
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/user.h>
#include <linux/string.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/asm-offsets.h>
#include <asm/setup.h>
#include <asm/fpu.h>
#include <linux/uaccess.h>
#include <asm/pgtable.h>
#include <asm/siginfo.h>
#include <asm/unaligned.h>

void show_backtrace(struct pt_regs *, unsigned long);

extern asmlinkage void __break_hijack_kernel_event(void);

/*****************************************************************************/
/*
 * instruction access error
 */
asmlinkage void insn_access_error(unsigned long esfr1, unsigned long epcr0, unsigned long esr0)
{
	siginfo_t info;

	die_if_kernel("-- Insn Access Error --\n"
		      "EPCR0 : %08lx\n"
		      "ESR0  : %08lx\n",
		      epcr0, esr0);

	info.si_signo	= SIGSEGV;
	info.si_code	= SEGV_ACCERR;
	info.si_errno	= 0;
	info.si_addr	= (void __user *) ((epcr0 & EPCR0_V) ? (epcr0 & EPCR0_PC) : __frame->pc);

	force_sig_info(info.si_signo, &info, current);
} /* end insn_access_error() */

/*****************************************************************************/
/*
 * handler for:
 * - illegal instruction
 * - privileged instruction
 * - unsupported trap
 * - debug exceptions
 */
asmlinkage void illegal_instruction(unsigned long esfr1, unsigned long epcr0, unsigned long esr0)
{
	siginfo_t info;

	die_if_kernel("-- Illegal Instruction --\n"
		      "EPCR0 : %08lx\n"
		      "ESR0  : %08lx\n"
		      "ESFR1 : %08lx\n",
		      epcr0, esr0, esfr1);

	info.si_errno	= 0;
	info.si_addr	= (void __user *) ((epcr0 & EPCR0_V) ? (epcr0 & EPCR0_PC) : __frame->pc);

	switch (__frame->tbr & TBR_TT) {
	case TBR_TT_ILLEGAL_INSTR:
		info.si_signo	= SIGILL;
		info.si_code	= ILL_ILLOPC;
		break;
	case TBR_TT_PRIV_INSTR:
		info.si_signo	= SIGILL;
		info.si_code	= ILL_PRVOPC;
		break;
	case TBR_TT_TRAP2 ... TBR_TT_TRAP126:
		info.si_signo	= SIGILL;
		info.si_code	= ILL_ILLTRP;
		break;
	/* GDB uses "tira gr0, #1" as a breakpoint instruction.  */
	case TBR_TT_TRAP1:
	case TBR_TT_BREAK:
		info.si_signo	= SIGTRAP;
		info.si_code	=
			(__frame->__status & REG__STATUS_STEPPED) ? TRAP_TRACE : TRAP_BRKPT;
		break;
	}

	force_sig_info(info.si_signo, &info, current);
} /* end illegal_instruction() */

/*****************************************************************************/
/*
 * handle atomic operations with errors
 * - arguments in gr8, gr9, gr10
 * - original memory value placed in gr5
 * - replacement memory value placed in gr9
 */
asmlinkage void atomic_operation(unsigned long esfr1, unsigned long epcr0,
				 unsigned long esr0)
{
	static DEFINE_SPINLOCK(atomic_op_lock);
	unsigned long x, y, z;
	unsigned long __user *p;
	mm_segment_t oldfs;
	siginfo_t info;
	int ret;

	y = 0;
	z = 0;

	oldfs = get_fs();
	if (!user_mode(__frame))
		set_fs(KERNEL_DS);

	switch (__frame->tbr & TBR_TT) {
		/* TIRA gr0,#120
		 * u32 __atomic_user_cmpxchg32(u32 *ptr, u32 test, u32 new)
		 */
	case TBR_TT_ATOMIC_CMPXCHG32:
		p = (unsigned long __user *) __frame->gr8;
		x = __frame->gr9;
		y = __frame->gr10;

		for (;;) {
			ret = get_user(z, p);
			if (ret < 0)
				goto error;

			if (z != x)
				goto done;

			spin_lock_irq(&atomic_op_lock);

			if (__get_user(z, p) == 0) {
				if (z != x)
					goto done2;

				if (__put_user(y, p) == 0)
					goto done2;
				goto error2;
			}

			spin_unlock_irq(&atomic_op_lock);
		}

		/* TIRA gr0,#121
		 * u32 __atomic_kernel_xchg32(void *v, u32 new)
		 */
	case TBR_TT_ATOMIC_XCHG32:
		p = (unsigned long __user *) __frame->gr8;
		y = __frame->gr9;

		for (;;) {
			ret = get_user(z, p);
			if (ret < 0)
				goto error;

			spin_lock_irq(&atomic_op_lock);

			if (__get_user(z, p) == 0) {
				if (__put_user(y, p) == 0)
					goto done2;
				goto error2;
			}

			spin_unlock_irq(&atomic_op_lock);
		}

		/* TIRA gr0,#122
		 * ulong __atomic_kernel_XOR_return(ulong i, ulong *v)
		 */
	case TBR_TT_ATOMIC_XOR:
		p = (unsigned long __user *) __frame->gr8;
		x = __frame->gr9;

		for (;;) {
			ret = get_user(z, p);
			if (ret < 0)
				goto error;

			spin_lock_irq(&atomic_op_lock);

			if (__get_user(z, p) == 0) {
				y = x ^ z;
				if (__put_user(y, p) == 0)
					goto done2;
				goto error2;
			}

			spin_unlock_irq(&atomic_op_lock);
		}

		/* TIRA gr0,#123
		 * ulong __atomic_kernel_OR_return(ulong i, ulong *v)
		 */
	case TBR_TT_ATOMIC_OR:
		p = (unsigned long __user *) __frame->gr8;
		x = __frame->gr9;

		for (;;) {
			ret = get_user(z, p);
			if (ret < 0)
				goto error;

			spin_lock_irq(&atomic_op_lock);

			if (__get_user(z, p) == 0) {
				y = x ^ z;
				if (__put_user(y, p) == 0)
					goto done2;
				goto error2;
			}

			spin_unlock_irq(&atomic_op_lock);
		}

		/* TIRA gr0,#124
		 * ulong __atomic_kernel_AND_return(ulong i, ulong *v)
		 */
	case TBR_TT_ATOMIC_AND:
		p = (unsigned long __user *) __frame->gr8;
		x = __frame->gr9;

		for (;;) {
			ret = get_user(z, p);
			if (ret < 0)
				goto error;

			spin_lock_irq(&atomic_op_lock);

			if (__get_user(z, p) == 0) {
				y = x & z;
				if (__put_user(y, p) == 0)
					goto done2;
				goto error2;
			}

			spin_unlock_irq(&atomic_op_lock);
		}

		/* TIRA gr0,#125
		 * int __atomic_user_sub_return(atomic_t *v, int i)
		 */
	case TBR_TT_ATOMIC_SUB:
		p = (unsigned long __user *) __frame->gr8;
		x = __frame->gr9;

		for (;;) {
			ret = get_user(z, p);
			if (ret < 0)
				goto error;

			spin_lock_irq(&atomic_op_lock);

			if (__get_user(z, p) == 0) {
				y = z - x;
				if (__put_user(y, p) == 0)
					goto done2;
				goto error2;
			}

			spin_unlock_irq(&atomic_op_lock);
		}

		/* TIRA gr0,#126
		 * int __atomic_user_add_return(atomic_t *v, int i)
		 */
	case TBR_TT_ATOMIC_ADD:
		p = (unsigned long __user *) __frame->gr8;
		x = __frame->gr9;

		for (;;) {
			ret = get_user(z, p);
			if (ret < 0)
				goto error;

			spin_lock_irq(&atomic_op_lock);

			if (__get_user(z, p) == 0) {
				y = z + x;
				if (__put_user(y, p) == 0)
					goto done2;
				goto error2;
			}

			spin_unlock_irq(&atomic_op_lock);
		}

	default:
		BUG();
	}

done2:
	spin_unlock_irq(&atomic_op_lock);
done:
	if (!user_mode(__frame))
		set_fs(oldfs);
	__frame->gr5 = z;
	__frame->gr9 = y;
	return;

error2:
	spin_unlock_irq(&atomic_op_lock);
error:
	if (!user_mode(__frame))
		set_fs(oldfs);
	__frame->pc -= 4;

	die_if_kernel("-- Atomic Op Error --\n");

	info.si_signo	= SIGSEGV;
	info.si_code	= SEGV_ACCERR;
	info.si_errno	= 0;
	info.si_addr	= (void __user *) __frame->pc;

	force_sig_info(info.si_signo, &info, current);
}

/*****************************************************************************/
/*
 *
 */
asmlinkage void media_exception(unsigned long msr0, unsigned long msr1)
{
	siginfo_t info;

	die_if_kernel("-- Media Exception --\n"
		      "MSR0 : %08lx\n"
		      "MSR1 : %08lx\n",
		      msr0, msr1);

	info.si_signo	= SIGFPE;
	info.si_code	= FPE_MDAOVF;
	info.si_errno	= 0;
	info.si_addr	= (void __user *) __frame->pc;

	force_sig_info(info.si_signo, &info, current);
} /* end media_exception() */

/*****************************************************************************/
/*
 * instruction or data access exception
 */
asmlinkage void memory_access_exception(unsigned long esr0,
					unsigned long ear0,
					unsigned long epcr0)
{
	siginfo_t info;

#ifdef CONFIG_MMU
	unsigned long fixup;

	fixup = search_exception_table(__frame->pc);
	if (fixup) {
		__frame->pc = fixup;
		return;
	}
#endif

	die_if_kernel("-- Memory Access Exception --\n"
		      "ESR0  : %08lx\n"
		      "EAR0  : %08lx\n"
		      "EPCR0 : %08lx\n",
		      esr0, ear0, epcr0);

	info.si_signo	= SIGSEGV;
	info.si_code	= SEGV_ACCERR;
	info.si_errno	= 0;
	info.si_addr	= NULL;

	if ((esr0 & (ESRx_VALID | ESR0_EAV)) == (ESRx_VALID | ESR0_EAV))
		info.si_addr = (void __user *) ear0;

	force_sig_info(info.si_signo, &info, current);

} /* end memory_access_exception() */

/*****************************************************************************/
/*
 * data access error
 * - double-word data load from CPU control area (0xFExxxxxx)
 * - read performed on inactive or self-refreshing SDRAM
 * - error notification from slave device
 * - misaligned address
 * - access to out of bounds memory region
 * - user mode accessing privileged memory region
 * - write to R/O memory region
 */
asmlinkage void data_access_error(unsigned long esfr1, unsigned long esr15, unsigned long ear15)
{
	siginfo_t info;

	die_if_kernel("-- Data Access Error --\n"
		      "ESR15 : %08lx\n"
		      "EAR15 : %08lx\n",
		      esr15, ear15);

	info.si_signo	= SIGSEGV;
	info.si_code	= SEGV_ACCERR;
	info.si_errno	= 0;
	info.si_addr	= (void __user *)
		(((esr15 & (ESRx_VALID|ESR15_EAV)) == (ESRx_VALID|ESR15_EAV)) ? ear15 : 0);

	force_sig_info(info.si_signo, &info, current);
} /* end data_access_error() */

/*****************************************************************************/
/*
 * data store error - should only happen if accessing inactive or self-refreshing SDRAM
 */
asmlinkage void data_store_error(unsigned long esfr1, unsigned long esr15)
{
	die_if_kernel("-- Data Store Error --\n"
		      "ESR15 : %08lx\n",
		      esr15);
	BUG();
} /* end data_store_error() */

/*****************************************************************************/
/*
 *
 */
asmlinkage void division_exception(unsigned long esfr1, unsigned long esr0, unsigned long isr)
{
	siginfo_t info;

	die_if_kernel("-- Division Exception --\n"
		      "ESR0 : %08lx\n"
		      "ISR  : %08lx\n",
		      esr0, isr);

	info.si_signo	= SIGFPE;
	info.si_code	= FPE_INTDIV;
	info.si_errno	= 0;
	info.si_addr	= (void __user *) __frame->pc;

	force_sig_info(info.si_signo, &info, current);
} /* end division_exception() */

/*****************************************************************************/
/*
 *
 */
asmlinkage void compound_exception(unsigned long esfr1,
				   unsigned long esr0, unsigned long esr14, unsigned long esr15,
				   unsigned long msr0, unsigned long msr1)
{
	die_if_kernel("-- Compound Exception --\n"
		      "ESR0  : %08lx\n"
		      "ESR15 : %08lx\n"
		      "ESR15 : %08lx\n"
		      "MSR0  : %08lx\n"
		      "MSR1  : %08lx\n",
		      esr0, esr14, esr15, msr0, msr1);
	BUG();
} /* end compound_exception() */

void show_stack(struct task_struct *task, unsigned long *sp)
{
}

void show_trace_task(struct task_struct *tsk)
{
	printk("CONTEXT: stack=0x%lx frame=0x%p LR=0x%lx RET=0x%lx\n",
	       tsk->thread.sp, tsk->thread.frame, tsk->thread.lr, tsk->thread.sched_lr);
}

static const char *regnames[] = {
	"PSR ", "ISR ", "CCR ", "CCCR",
	"LR  ", "LCR ", "PC  ", "_stt",
	"sys ", "GR8*", "GNE0", "GNE1",
	"IACH", "IACL",
	"TBR ", "SP  ", "FP  ", "GR3 ",
	"GR4 ", "GR5 ", "GR6 ", "GR7 ",
	"GR8 ", "GR9 ", "GR10", "GR11",
	"GR12", "GR13", "GR14", "GR15",
	"GR16", "GR17", "GR18", "GR19",
	"GR20", "GR21", "GR22", "GR23",
	"GR24", "GR25", "GR26", "GR27",
	"EFRM", "CURR", "GR30", "BFRM"
};

void show_regs(struct pt_regs *regs)
{
	unsigned long *reg;
	int loop;

	printk("\n");
	show_regs_print_info(KERN_DEFAULT);

	printk("Frame: @%08lx [%s]\n",
	       (unsigned long) regs,
	       regs->psr & PSR_S ? "kernel" : "user");

	reg = (unsigned long *) regs;
	for (loop = 0; loop < NR_PT_REGS; loop++) {
		printk("%s %08lx", regnames[loop + 0], reg[loop + 0]);

		if (loop == NR_PT_REGS - 1 || loop % 5 == 4)
			printk("\n");
		else
			printk(" | ");
	}
}

void die_if_kernel(const char *str, ...)
{
	char buffer[256];
	va_list va;

	if (user_mode(__frame))
		return;

	va_start(va, str);
	vsnprintf(buffer, sizeof(buffer), str, va);
	va_end(va);

	console_verbose();
	printk("\n===================================\n");
	printk("%s\n", buffer);
	show_backtrace(__frame, 0);

	__break_hijack_kernel_event();
	do_exit(SIGSEGV);
}

/*****************************************************************************/
/*
 * dump the contents of an exception frame
 */
static void show_backtrace_regs(struct pt_regs *frame)
{
	unsigned long *reg;
	int loop;

	/* print the registers for this frame */
	printk("<-- %s Frame: @%p -->\n",
	       frame->psr & PSR_S ? "Kernel Mode" : "User Mode",
	       frame);

	reg = (unsigned long *) frame;
	for (loop = 0; loop < NR_PT_REGS; loop++) {
		printk("%s %08lx", regnames[loop + 0], reg[loop + 0]);

		if (loop == NR_PT_REGS - 1 || loop % 5 == 4)
			printk("\n");
		else
			printk(" | ");
	}

	printk("--------\n");
} /* end show_backtrace_regs() */

/*****************************************************************************/
/*
 * generate a backtrace of the kernel stack
 */
void show_backtrace(struct pt_regs *frame, unsigned long sp)
{
	struct pt_regs *frame0;
	unsigned long tos = 0, stop = 0, base;
	int format;

	base = ((((unsigned long) frame) + 8191) & ~8191) - sizeof(struct user_context);
	frame0 = (struct pt_regs *) base;

	if (sp) {
		tos = sp;
		stop = (unsigned long) frame;
	}

	printk("\nProcess %s (pid: %d)\n\n", current->comm, current->pid);

	for (;;) {
		/* dump stack segment between frames */
		//printk("%08lx -> %08lx\n", tos, stop);
		format = 0;
		while (tos < stop) {
			if (format == 0)
				printk(" %04lx :", tos & 0xffff);

			printk(" %08lx", *(unsigned long *) tos);

			tos += 4;
			format++;
			if (format == 8) {
				printk("\n");
				format = 0;
			}
		}

		if (format > 0)
			printk("\n");

		/* dump frame 0 outside of the loop */
		if (frame == frame0)
			break;

		tos = frame->sp;
		if (((unsigned long) frame) + sizeof(*frame) != tos) {
			printk("-- TOS %08lx does not follow frame %p --\n",
			       tos, frame);
			break;
		}

		show_backtrace_regs(frame);

		/* dump the stack between this frame and the next */
		stop = (unsigned long) frame->next_frame;
		if (stop != base &&
		    (stop < tos ||
		     stop > base ||
		     (stop < base && stop + sizeof(*frame) > base) ||
		     stop & 3)) {
			printk("-- next_frame %08lx is invalid (range %08lx-%08lx) --\n",
			       stop, tos, base);
			break;
		}

		/* move to next frame */
		frame = frame->next_frame;
	}

	/* we can always dump frame 0, even if the rest of the stack is corrupt */
	show_backtrace_regs(frame0);

} /* end show_backtrace() */

/*****************************************************************************/
/*
 * initialise traps
 */
void __init trap_init (void)
{
} /* end trap_init() */
