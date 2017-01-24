/*
 * Hardware exception handling
 *
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 * Copyright (C) 2001 Vic Phillips
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/ptrace.h>

#include <asm/traps.h>
#include <asm/sections.h>
#include <linux/uaccess.h>

static DEFINE_SPINLOCK(die_lock);

static void _send_sig(int signo, int code, unsigned long addr)
{
	siginfo_t info;

	info.si_signo = signo;
	info.si_errno = 0;
	info.si_code = code;
	info.si_addr = (void __user *) addr;
	force_sig_info(signo, &info, current);
}

void die(const char *str, struct pt_regs *regs, long err)
{
	console_verbose();
	spin_lock_irq(&die_lock);
	pr_warn("Oops: %s, sig: %ld\n", str, err);
	show_regs(regs);
	spin_unlock_irq(&die_lock);
	/*
	 * do_exit() should take care of panic'ing from an interrupt
	 * context so we don't handle it here
	 */
	do_exit(err);
}

void _exception(int signo, struct pt_regs *regs, int code, unsigned long addr)
{
	if (!user_mode(regs))
		die("Exception in kernel mode", regs, signo);

	_send_sig(signo, code, addr);
}

/*
 * The show_stack is an external API which we do not use ourselves.
 */

int kstack_depth_to_print = 48;

void show_stack(struct task_struct *task, unsigned long *stack)
{
	unsigned long *endstack, addr;
	int i;

	if (!stack) {
		if (task)
			stack = (unsigned long *)task->thread.ksp;
		else
			stack = (unsigned long *)&stack;
	}

	addr = (unsigned long) stack;
	endstack = (unsigned long *) PAGE_ALIGN(addr);

	pr_emerg("Stack from %08lx:", (unsigned long)stack);
	for (i = 0; i < kstack_depth_to_print; i++) {
		if (stack + 1 > endstack)
			break;
		if (i % 8 == 0)
			pr_emerg("\n       ");
		pr_emerg(" %08lx", *stack++);
	}

	pr_emerg("\nCall Trace:");
	i = 0;
	while (stack + 1 <= endstack) {
		addr = *stack++;
		/*
		 * If the address is either in the text segment of the
		 * kernel, or in the region which contains vmalloc'ed
		 * memory, it *may* be the address of a calling
		 * routine; if so, print it so that someone tracing
		 * down the cause of the crash will be able to figure
		 * out the call path that was taken.
		 */
		if (((addr >= (unsigned long) _stext) &&
		     (addr <= (unsigned long) _etext))) {
			if (i % 4 == 0)
				pr_emerg("\n       ");
			pr_emerg(" [<%08lx>]", addr);
			i++;
		}
	}
	pr_emerg("\n");
}

void __init trap_init(void)
{
	/* Nothing to do here */
}

/* Breakpoint handler */
asmlinkage void breakpoint_c(struct pt_regs *fp)
{
	/*
	 * The breakpoint entry code has moved the PC on by 4 bytes, so we must
	 * move it back. This could be done on the host but we do it here
	 * because monitor.S of JTAG gdbserver does it too.
	 */
	fp->ea -= 4;
	_exception(SIGTRAP, fp, TRAP_BRKPT, fp->ea);
}

#ifndef CONFIG_NIOS2_ALIGNMENT_TRAP
/* Alignment exception handler */
asmlinkage void handle_unaligned_c(struct pt_regs *fp, int cause)
{
	unsigned long addr = RDCTL(CTL_BADADDR);

	cause >>= 2;
	fp->ea -= 4;

	if (fixup_exception(fp))
		return;

	if (!user_mode(fp)) {
		pr_alert("Unaligned access from kernel mode, this might be a hardware\n");
		pr_alert("problem, dump registers and restart the instruction\n");
		pr_alert("  BADADDR 0x%08lx\n", addr);
		pr_alert("  cause   %d\n", cause);
		pr_alert("  op-code 0x%08lx\n", *(unsigned long *)(fp->ea));
		show_regs(fp);
		return;
	}

	_exception(SIGBUS, fp, BUS_ADRALN, addr);
}
#endif /* CONFIG_NIOS2_ALIGNMENT_TRAP */

/* Illegal instruction handler */
asmlinkage void handle_illegal_c(struct pt_regs *fp)
{
	fp->ea -= 4;
	_exception(SIGILL, fp, ILL_ILLOPC, fp->ea);
}

/* Supervisor instruction handler */
asmlinkage void handle_supervisor_instr(struct pt_regs *fp)
{
	fp->ea -= 4;
	_exception(SIGILL, fp, ILL_PRVOPC, fp->ea);
}

/* Division error handler */
asmlinkage void handle_diverror_c(struct pt_regs *fp)
{
	fp->ea -= 4;
	_exception(SIGFPE, fp, FPE_INTDIV, fp->ea);
}

/* Unhandled exception handler */
asmlinkage void unhandled_exception(struct pt_regs *regs, int cause)
{
	unsigned long addr = RDCTL(CTL_BADADDR);

	cause /= 4;

	pr_emerg("Unhandled exception #%d in %s mode (badaddr=0x%08lx)\n",
			cause, user_mode(regs) ? "user" : "kernel", addr);

	regs->ea -= 4;
	show_regs(regs);

	pr_emerg("opcode: 0x%08lx\n", *(unsigned long *)(regs->ea));
}

asmlinkage void handle_trap_1_c(struct pt_regs *fp)
{
	_send_sig(SIGUSR1, 0, fp->ea);
}

asmlinkage void handle_trap_2_c(struct pt_regs *fp)
{
	_send_sig(SIGUSR2, 0, fp->ea);
}

asmlinkage void handle_trap_3_c(struct pt_regs *fp)
{
	_send_sig(SIGILL, ILL_ILLTRP, fp->ea);
}
