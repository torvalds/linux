/*
 *  linux/arch/m68knommu/kernel/traps.c
 *
 *  Copyright (C) 1993, 1994 by Hamish Macdonald
 *
 *  68040 fixes by Michael Rausch
 *  68040 fixes by Martin Apel
 *  68060 fixes by Roman Hodek
 *  68060 fixes by Jesper Skov
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/*
 * Sets up all exception vectors
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/a.out.h>
#include <linux/user.h>
#include <linux/string.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/ptrace.h>

#include <asm/setup.h>
#include <asm/fpu.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/traps.h>
#include <asm/pgtable.h>
#include <asm/machdep.h>
#include <asm/siginfo.h>

static char const * const vec_names[] = {
	"RESET SP", "RESET PC", "BUS ERROR", "ADDRESS ERROR",
	"ILLEGAL INSTRUCTION", "ZERO DIVIDE", "CHK", "TRAPcc",
	"PRIVILEGE VIOLATION", "TRACE", "LINE 1010", "LINE 1111",
	"UNASSIGNED RESERVED 12", "COPROCESSOR PROTOCOL VIOLATION",
	"FORMAT ERROR", "UNINITIALIZED INTERRUPT",
	"UNASSIGNED RESERVED 16", "UNASSIGNED RESERVED 17",
	"UNASSIGNED RESERVED 18", "UNASSIGNED RESERVED 19",
	"UNASSIGNED RESERVED 20", "UNASSIGNED RESERVED 21",
	"UNASSIGNED RESERVED 22", "UNASSIGNED RESERVED 23",
	"SPURIOUS INTERRUPT", "LEVEL 1 INT", "LEVEL 2 INT", "LEVEL 3 INT",
	"LEVEL 4 INT", "LEVEL 5 INT", "LEVEL 6 INT", "LEVEL 7 INT",
	"SYSCALL", "TRAP #1", "TRAP #2", "TRAP #3",
	"TRAP #4", "TRAP #5", "TRAP #6", "TRAP #7",
	"TRAP #8", "TRAP #9", "TRAP #10", "TRAP #11",
	"TRAP #12", "TRAP #13", "TRAP #14", "TRAP #15",
	"FPCP BSUN", "FPCP INEXACT", "FPCP DIV BY 0", "FPCP UNDERFLOW",
	"FPCP OPERAND ERROR", "FPCP OVERFLOW", "FPCP SNAN",
	"FPCP UNSUPPORTED OPERATION",
	"MMU CONFIGURATION ERROR"
};

void __init trap_init(void)
{
	if (mach_trap_init)
		mach_trap_init();
}

void die_if_kernel(char *str, struct pt_regs *fp, int nr)
{
	if (!(fp->sr & PS_S))
		return;

	console_verbose();
	printk(KERN_EMERG "%s: %08x\n",str,nr);
	printk(KERN_EMERG "PC: [<%08lx>]\nSR: %04x  SP: %p  a2: %08lx\n",
	       fp->pc, fp->sr, fp, fp->a2);
	printk(KERN_EMERG "d0: %08lx    d1: %08lx    d2: %08lx    d3: %08lx\n",
	       fp->d0, fp->d1, fp->d2, fp->d3);
	printk(KERN_EMERG "d4: %08lx    d5: %08lx    a0: %08lx    a1: %08lx\n",
	       fp->d4, fp->d5, fp->a0, fp->a1);

	printk(KERN_EMERG "Process %s (pid: %d, stackpage=%08lx)\n",
		current->comm, current->pid, PAGE_SIZE+(unsigned long)current);
	show_stack(NULL, (unsigned long *)fp);
	do_exit(SIGSEGV);
}

asmlinkage void buserr_c(struct frame *fp)
{
	/* Only set esp0 if coming from user mode */
	if (user_mode(&fp->ptregs))
		current->thread.esp0 = (unsigned long) fp;

#if DEBUG
	printk (KERN_DEBUG "*** Bus Error *** Format is %x\n", fp->ptregs.format);
#endif

	die_if_kernel("bad frame format",&fp->ptregs,0);
#if DEBUG
	printk(KERN_DEBUG "Unknown SIGSEGV - 4\n");
#endif
	force_sig(SIGSEGV, current);
}


int kstack_depth_to_print = 48;

void show_stack(struct task_struct *task, unsigned long *stack)
{
	unsigned long *endstack, addr;
	extern char _start, _etext;
	int i;

	if (!stack) {
		if (task)
			stack = (unsigned long *)task->thread.ksp;
		else
			stack = (unsigned long *)&stack;
	}

	addr = (unsigned long) stack;
	endstack = (unsigned long *) PAGE_ALIGN(addr);

	printk(KERN_EMERG "Stack from %08lx:", (unsigned long)stack);
	for (i = 0; i < kstack_depth_to_print; i++) {
		if (stack + 1 > endstack)
			break;
		if (i % 8 == 0)
			printk(KERN_EMERG "\n       ");
		printk(KERN_EMERG " %08lx", *stack++);
	}

	printk(KERN_EMERG "\nCall Trace:");
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
		if (((addr >= (unsigned long) &_start) &&
		     (addr <= (unsigned long) &_etext))) {
			if (i % 4 == 0)
				printk(KERN_EMERG "\n       ");
			printk(KERN_EMERG " [<%08lx>]", addr);
			i++;
		}
	}
	printk(KERN_EMERG "\n");
}

void bad_super_trap(struct frame *fp)
{
	console_verbose();
	if (fp->ptregs.vector < 4*sizeof(vec_names)/sizeof(vec_names[0]))
		printk (KERN_WARNING "*** %s ***   FORMAT=%X\n",
			vec_names[(fp->ptregs.vector) >> 2],
			fp->ptregs.format);
	else
		printk (KERN_WARNING "*** Exception %d ***   FORMAT=%X\n",
			(fp->ptregs.vector) >> 2, 
			fp->ptregs.format);
	printk (KERN_WARNING "Current process id is %d\n", current->pid);
	die_if_kernel("BAD KERNEL TRAP", &fp->ptregs, 0);
}

asmlinkage void trap_c(struct frame *fp)
{
	int sig;
	siginfo_t info;

	if (fp->ptregs.sr & PS_S) {
		if ((fp->ptregs.vector >> 2) == VEC_TRACE) {
			/* traced a trapping instruction */
			current->ptrace |= PT_DTRACE;
		} else
			bad_super_trap(fp);
		return;
	}

	/* send the appropriate signal to the user program */
	switch ((fp->ptregs.vector) >> 2) {
	    case VEC_ADDRERR:
		info.si_code = BUS_ADRALN;
		sig = SIGBUS;
		break;
	    case VEC_ILLEGAL:
	    case VEC_LINE10:
	    case VEC_LINE11:
		info.si_code = ILL_ILLOPC;
		sig = SIGILL;
		break;
	    case VEC_PRIV:
		info.si_code = ILL_PRVOPC;
		sig = SIGILL;
		break;
	    case VEC_COPROC:
		info.si_code = ILL_COPROC;
		sig = SIGILL;
		break;
	    case VEC_TRAP1: /* gdbserver breakpoint */
		fp->ptregs.pc -= 2;
		info.si_code = TRAP_TRACE;
		sig = SIGTRAP;
		break;
	    case VEC_TRAP2:
	    case VEC_TRAP3:
	    case VEC_TRAP4:
	    case VEC_TRAP5:
	    case VEC_TRAP6:
	    case VEC_TRAP7:
	    case VEC_TRAP8:
	    case VEC_TRAP9:
	    case VEC_TRAP10:
	    case VEC_TRAP11:
	    case VEC_TRAP12:
	    case VEC_TRAP13:
	    case VEC_TRAP14:
		info.si_code = ILL_ILLTRP;
		sig = SIGILL;
		break;
	    case VEC_FPBRUC:
	    case VEC_FPOE:
	    case VEC_FPNAN:
		info.si_code = FPE_FLTINV;
		sig = SIGFPE;
		break;
	    case VEC_FPIR:
		info.si_code = FPE_FLTRES;
		sig = SIGFPE;
		break;
	    case VEC_FPDIVZ:
		info.si_code = FPE_FLTDIV;
		sig = SIGFPE;
		break;
	    case VEC_FPUNDER:
		info.si_code = FPE_FLTUND;
		sig = SIGFPE;
		break;
	    case VEC_FPOVER:
		info.si_code = FPE_FLTOVF;
		sig = SIGFPE;
		break;
	    case VEC_ZERODIV:
		info.si_code = FPE_INTDIV;
		sig = SIGFPE;
		break;
	    case VEC_CHK:
	    case VEC_TRAP:
		info.si_code = FPE_INTOVF;
		sig = SIGFPE;
		break;
	    case VEC_TRACE:		/* ptrace single step */
		info.si_code = TRAP_TRACE;
		sig = SIGTRAP;
		break;
	    case VEC_TRAP15:		/* breakpoint */
		info.si_code = TRAP_BRKPT;
		sig = SIGTRAP;
		break;
	    default:
		info.si_code = ILL_ILLOPC;
		sig = SIGILL;
		break;
	}
	info.si_signo = sig;
	info.si_errno = 0;
	switch (fp->ptregs.format) {
	    default:
		info.si_addr = (void *) fp->ptregs.pc;
		break;
	    case 2:
		info.si_addr = (void *) fp->un.fmt2.iaddr;
		break;
	    case 7:
		info.si_addr = (void *) fp->un.fmt7.effaddr;
		break;
	    case 9:
		info.si_addr = (void *) fp->un.fmt9.iaddr;
		break;
	    case 10:
		info.si_addr = (void *) fp->un.fmta.daddr;
		break;
	    case 11:
		info.si_addr = (void *) fp->un.fmtb.daddr;
		break;
	}
	force_sig_info (sig, &info, current);
}

asmlinkage void set_esp0(unsigned long ssp)
{
	current->thread.esp0 = ssp;
}


/*
 * The architecture-independent backtrace generator
 */
void dump_stack(void)
{
	unsigned long stack;

	show_stack(current, &stack);
}

EXPORT_SYMBOL(dump_stack);

#ifdef CONFIG_M68KFPU_EMU
asmlinkage void fpemu_signal(int signal, int code, void *addr)
{
	siginfo_t info;

	info.si_signo = signal;
	info.si_errno = 0;
	info.si_code = code;
	info.si_addr = addr;
	force_sig_info(signal, &info, current);
}
#endif
