/*
 *  arch/s390/kernel/traps.c
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *
 *  Derived from "arch/i386/kernel/traps.c"
 *    Copyright (C) 1991, 1992 Linus Torvalds
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'.
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kdebug.h>
#include <linux/kallsyms.h>
#include <linux/reboot.h>
#include <linux/kprobes.h>
#include <linux/bug.h>
#include <linux/utsname.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/atomic.h>
#include <asm/mathemu.h>
#include <asm/cpcmd.h>
#include <asm/lowcore.h>
#include <asm/debug.h>
#include <asm/ipl.h>
#include "entry.h"

void (*pgm_check_table[128])(struct pt_regs *regs);

int show_unhandled_signals = 1;

#define stack_pointer ({ void **sp; asm("la %0,0(15)" : "=&d" (sp)); sp; })

#ifndef CONFIG_64BIT
#define LONG "%08lx "
#define FOURLONG "%08lx %08lx %08lx %08lx\n"
static int kstack_depth_to_print = 12;
#else /* CONFIG_64BIT */
#define LONG "%016lx "
#define FOURLONG "%016lx %016lx %016lx %016lx\n"
static int kstack_depth_to_print = 20;
#endif /* CONFIG_64BIT */

/*
 * For show_trace we have tree different stack to consider:
 *   - the panic stack which is used if the kernel stack has overflown
 *   - the asynchronous interrupt stack (cpu related)
 *   - the synchronous kernel stack (process related)
 * The stack trace can start at any of the three stack and can potentially
 * touch all of them. The order is: panic stack, async stack, sync stack.
 */
static unsigned long
__show_trace(unsigned long sp, unsigned long low, unsigned long high)
{
	struct stack_frame *sf;
	struct pt_regs *regs;

	while (1) {
		sp = sp & PSW_ADDR_INSN;
		if (sp < low || sp > high - sizeof(*sf))
			return sp;
		sf = (struct stack_frame *) sp;
		printk("([<%016lx>] ", sf->gprs[8] & PSW_ADDR_INSN);
		print_symbol("%s)\n", sf->gprs[8] & PSW_ADDR_INSN);
		/* Follow the backchain. */
		while (1) {
			low = sp;
			sp = sf->back_chain & PSW_ADDR_INSN;
			if (!sp)
				break;
			if (sp <= low || sp > high - sizeof(*sf))
				return sp;
			sf = (struct stack_frame *) sp;
			printk(" [<%016lx>] ", sf->gprs[8] & PSW_ADDR_INSN);
			print_symbol("%s\n", sf->gprs[8] & PSW_ADDR_INSN);
		}
		/* Zero backchain detected, check for interrupt frame. */
		sp = (unsigned long) (sf + 1);
		if (sp <= low || sp > high - sizeof(*regs))
			return sp;
		regs = (struct pt_regs *) sp;
		printk(" [<%016lx>] ", regs->psw.addr & PSW_ADDR_INSN);
		print_symbol("%s\n", regs->psw.addr & PSW_ADDR_INSN);
		low = sp;
		sp = regs->gprs[15];
	}
}

static void show_trace(struct task_struct *task, unsigned long *stack)
{
	register unsigned long __r15 asm ("15");
	unsigned long sp;

	sp = (unsigned long) stack;
	if (!sp)
		sp = task ? task->thread.ksp : __r15;
	printk("Call Trace:\n");
#ifdef CONFIG_CHECK_STACK
	sp = __show_trace(sp, S390_lowcore.panic_stack - 4096,
			  S390_lowcore.panic_stack);
#endif
	sp = __show_trace(sp, S390_lowcore.async_stack - ASYNC_SIZE,
			  S390_lowcore.async_stack);
	if (task)
		__show_trace(sp, (unsigned long) task_stack_page(task),
			     (unsigned long) task_stack_page(task) + THREAD_SIZE);
	else
		__show_trace(sp, S390_lowcore.thread_info,
			     S390_lowcore.thread_info + THREAD_SIZE);
	if (!task)
		task = current;
	debug_show_held_locks(task);
}

void show_stack(struct task_struct *task, unsigned long *sp)
{
	register unsigned long * __r15 asm ("15");
	unsigned long *stack;
	int i;

	if (!sp)
		stack = task ? (unsigned long *) task->thread.ksp : __r15;
	else
		stack = sp;

	for (i = 0; i < kstack_depth_to_print; i++) {
		if (((addr_t) stack & (THREAD_SIZE-1)) == 0)
			break;
		if ((i * sizeof(long) % 32) == 0)
			printk("%s       ", i == 0 ? "" : "\n");
		printk(LONG, *stack++);
	}
	printk("\n");
	show_trace(task, sp);
}

static void show_last_breaking_event(struct pt_regs *regs)
{
#ifdef CONFIG_64BIT
	printk("Last Breaking-Event-Address:\n");
	printk(" [<%016lx>] ", regs->args[0] & PSW_ADDR_INSN);
	print_symbol("%s\n", regs->args[0] & PSW_ADDR_INSN);
#endif
}

/*
 * The architecture-independent dump_stack generator
 */
void dump_stack(void)
{
	printk("CPU: %d %s %s %.*s\n",
	       task_thread_info(current)->cpu, print_tainted(),
	       init_utsname()->release,
	       (int)strcspn(init_utsname()->version, " "),
	       init_utsname()->version);
	printk("Process %s (pid: %d, task: %p, ksp: %p)\n",
	       current->comm, current->pid, current,
	       (void *) current->thread.ksp);
	show_stack(NULL, NULL);
}
EXPORT_SYMBOL(dump_stack);

static inline int mask_bits(struct pt_regs *regs, unsigned long bits)
{
	return (regs->psw.mask & bits) / ((~bits + 1) & bits);
}

void show_registers(struct pt_regs *regs)
{
	char *mode;

	mode = (regs->psw.mask & PSW_MASK_PSTATE) ? "User" : "Krnl";
	printk("%s PSW : %p %p",
	       mode, (void *) regs->psw.mask,
	       (void *) regs->psw.addr);
	print_symbol(" (%s)\n", regs->psw.addr & PSW_ADDR_INSN);
	printk("           R:%x T:%x IO:%x EX:%x Key:%x M:%x W:%x "
	       "P:%x AS:%x CC:%x PM:%x", mask_bits(regs, PSW_MASK_PER),
	       mask_bits(regs, PSW_MASK_DAT), mask_bits(regs, PSW_MASK_IO),
	       mask_bits(regs, PSW_MASK_EXT), mask_bits(regs, PSW_MASK_KEY),
	       mask_bits(regs, PSW_MASK_MCHECK), mask_bits(regs, PSW_MASK_WAIT),
	       mask_bits(regs, PSW_MASK_PSTATE), mask_bits(regs, PSW_MASK_ASC),
	       mask_bits(regs, PSW_MASK_CC), mask_bits(regs, PSW_MASK_PM));
#ifdef CONFIG_64BIT
	printk(" EA:%x", mask_bits(regs, PSW_MASK_EA | PSW_MASK_BA));
#endif
	printk("\n%s GPRS: " FOURLONG, mode,
	       regs->gprs[0], regs->gprs[1], regs->gprs[2], regs->gprs[3]);
	printk("           " FOURLONG,
	       regs->gprs[4], regs->gprs[5], regs->gprs[6], regs->gprs[7]);
	printk("           " FOURLONG,
	       regs->gprs[8], regs->gprs[9], regs->gprs[10], regs->gprs[11]);
	printk("           " FOURLONG,
	       regs->gprs[12], regs->gprs[13], regs->gprs[14], regs->gprs[15]);

	show_code(regs);
}	

void show_regs(struct pt_regs *regs)
{
	print_modules();
	printk("CPU: %d %s %s %.*s\n",
	       task_thread_info(current)->cpu, print_tainted(),
	       init_utsname()->release,
	       (int)strcspn(init_utsname()->version, " "),
	       init_utsname()->version);
	printk("Process %s (pid: %d, task: %p, ksp: %p)\n",
	       current->comm, current->pid, current,
	       (void *) current->thread.ksp);
	show_registers(regs);
	/* Show stack backtrace if pt_regs is from kernel mode */
	if (!(regs->psw.mask & PSW_MASK_PSTATE))
		show_trace(NULL, (unsigned long *) regs->gprs[15]);
	show_last_breaking_event(regs);
}

static DEFINE_SPINLOCK(die_lock);

void die(struct pt_regs *regs, const char *str)
{
	static int die_counter;

	oops_enter();
	lgr_info_log();
	debug_stop_all();
	console_verbose();
	spin_lock_irq(&die_lock);
	bust_spinlocks(1);
	printk("%s: %04x [#%d] ", str, regs->int_code & 0xffff, ++die_counter);
#ifdef CONFIG_PREEMPT
	printk("PREEMPT ");
#endif
#ifdef CONFIG_SMP
	printk("SMP ");
#endif
#ifdef CONFIG_DEBUG_PAGEALLOC
	printk("DEBUG_PAGEALLOC");
#endif
	printk("\n");
	notify_die(DIE_OOPS, str, regs, 0, regs->int_code & 0xffff, SIGSEGV);
	show_regs(regs);
	bust_spinlocks(0);
	add_taint(TAINT_DIE);
	spin_unlock_irq(&die_lock);
	if (in_interrupt())
		panic("Fatal exception in interrupt");
	if (panic_on_oops)
		panic("Fatal exception: panic_on_oops");
	oops_exit();
	do_exit(SIGSEGV);
}

static inline void report_user_fault(struct pt_regs *regs, int signr)
{
	if ((task_pid_nr(current) > 1) && !show_unhandled_signals)
		return;
	if (!unhandled_signal(current, signr))
		return;
	if (!printk_ratelimit())
		return;
	printk("User process fault: interruption code 0x%X ", regs->int_code);
	print_vma_addr("in ", regs->psw.addr & PSW_ADDR_INSN);
	printk("\n");
	show_regs(regs);
}

int is_valid_bugaddr(unsigned long addr)
{
	return 1;
}

static inline void __user *get_psw_address(struct pt_regs *regs)
{
	return (void __user *)
		((regs->psw.addr - (regs->int_code >> 16)) & PSW_ADDR_INSN);
}

static void __kprobes do_trap(struct pt_regs *regs,
			      int si_signo, int si_code, char *str)
{
	siginfo_t info;

	if (notify_die(DIE_TRAP, str, regs, 0,
		       regs->int_code, si_signo) == NOTIFY_STOP)
		return;

        if (regs->psw.mask & PSW_MASK_PSTATE) {
		info.si_signo = si_signo;
		info.si_errno = 0;
		info.si_code = si_code;
		info.si_addr = get_psw_address(regs);
		force_sig_info(si_signo, &info, current);
		report_user_fault(regs, si_signo);
        } else {
                const struct exception_table_entry *fixup;
                fixup = search_exception_tables(regs->psw.addr & PSW_ADDR_INSN);
                if (fixup)
                        regs->psw.addr = fixup->fixup | PSW_ADDR_AMODE;
		else {
			enum bug_trap_type btt;

			btt = report_bug(regs->psw.addr & PSW_ADDR_INSN, regs);
			if (btt == BUG_TRAP_TYPE_WARN)
				return;
			die(regs, str);
		}
        }
}

void __kprobes do_per_trap(struct pt_regs *regs)
{
	siginfo_t info;

	if (notify_die(DIE_SSTEP, "sstep", regs, 0, 0, SIGTRAP) == NOTIFY_STOP)
		return;
	if (!current->ptrace)
		return;
	info.si_signo = SIGTRAP;
	info.si_errno = 0;
	info.si_code = TRAP_HWBKPT;
	info.si_addr =
		(void __force __user *) current->thread.per_event.address;
	force_sig_info(SIGTRAP, &info, current);
}

static void default_trap_handler(struct pt_regs *regs)
{
        if (regs->psw.mask & PSW_MASK_PSTATE) {
		report_user_fault(regs, SIGSEGV);
		do_exit(SIGSEGV);
	} else
		die(regs, "Unknown program exception");
}

#define DO_ERROR_INFO(name, signr, sicode, str) \
static void name(struct pt_regs *regs) \
{ \
	do_trap(regs, signr, sicode, str); \
}

DO_ERROR_INFO(addressing_exception, SIGILL, ILL_ILLADR,
	      "addressing exception")
DO_ERROR_INFO(execute_exception, SIGILL, ILL_ILLOPN,
	      "execute exception")
DO_ERROR_INFO(divide_exception, SIGFPE, FPE_INTDIV,
	      "fixpoint divide exception")
DO_ERROR_INFO(overflow_exception, SIGFPE, FPE_INTOVF,
	      "fixpoint overflow exception")
DO_ERROR_INFO(hfp_overflow_exception, SIGFPE, FPE_FLTOVF,
	      "HFP overflow exception")
DO_ERROR_INFO(hfp_underflow_exception, SIGFPE, FPE_FLTUND,
	      "HFP underflow exception")
DO_ERROR_INFO(hfp_significance_exception, SIGFPE, FPE_FLTRES,
	      "HFP significance exception")
DO_ERROR_INFO(hfp_divide_exception, SIGFPE, FPE_FLTDIV,
	      "HFP divide exception")
DO_ERROR_INFO(hfp_sqrt_exception, SIGFPE, FPE_FLTINV,
	      "HFP square root exception")
DO_ERROR_INFO(operand_exception, SIGILL, ILL_ILLOPN,
	      "operand exception")
DO_ERROR_INFO(privileged_op, SIGILL, ILL_PRVOPC,
	      "privileged operation")
DO_ERROR_INFO(special_op_exception, SIGILL, ILL_ILLOPN,
	      "special operation exception")
DO_ERROR_INFO(translation_exception, SIGILL, ILL_ILLOPN,
	      "translation exception")

static inline void do_fp_trap(struct pt_regs *regs, int fpc)
{
	int si_code = 0;
	/* FPC[2] is Data Exception Code */
	if ((fpc & 0x00000300) == 0) {
		/* bits 6 and 7 of DXC are 0 iff IEEE exception */
		if (fpc & 0x8000) /* invalid fp operation */
			si_code = FPE_FLTINV;
		else if (fpc & 0x4000) /* div by 0 */
			si_code = FPE_FLTDIV;
		else if (fpc & 0x2000) /* overflow */
			si_code = FPE_FLTOVF;
		else if (fpc & 0x1000) /* underflow */
			si_code = FPE_FLTUND;
		else if (fpc & 0x0800) /* inexact */
			si_code = FPE_FLTRES;
	}
	do_trap(regs, SIGFPE, si_code, "floating point exception");
}

static void __kprobes illegal_op(struct pt_regs *regs)
{
	siginfo_t info;
        __u8 opcode[6];
	__u16 __user *location;
	int signal = 0;

	location = get_psw_address(regs);

	if (regs->psw.mask & PSW_MASK_PSTATE) {
		if (get_user(*((__u16 *) opcode), (__u16 __user *) location))
			return;
		if (*((__u16 *) opcode) == S390_BREAKPOINT_U16) {
			if (current->ptrace) {
				info.si_signo = SIGTRAP;
				info.si_errno = 0;
				info.si_code = TRAP_BRKPT;
				info.si_addr = location;
				force_sig_info(SIGTRAP, &info, current);
			} else
				signal = SIGILL;
#ifdef CONFIG_MATHEMU
		} else if (opcode[0] == 0xb3) {
			if (get_user(*((__u16 *) (opcode+2)), location+1))
				return;
			signal = math_emu_b3(opcode, regs);
                } else if (opcode[0] == 0xed) {
			if (get_user(*((__u32 *) (opcode+2)),
				     (__u32 __user *)(location+1)))
				return;
			signal = math_emu_ed(opcode, regs);
		} else if (*((__u16 *) opcode) == 0xb299) {
			if (get_user(*((__u16 *) (opcode+2)), location+1))
				return;
			signal = math_emu_srnm(opcode, regs);
		} else if (*((__u16 *) opcode) == 0xb29c) {
			if (get_user(*((__u16 *) (opcode+2)), location+1))
				return;
			signal = math_emu_stfpc(opcode, regs);
		} else if (*((__u16 *) opcode) == 0xb29d) {
			if (get_user(*((__u16 *) (opcode+2)), location+1))
				return;
			signal = math_emu_lfpc(opcode, regs);
#endif
		} else
			signal = SIGILL;
	} else {
		/*
		 * If we get an illegal op in kernel mode, send it through the
		 * kprobes notifier. If kprobes doesn't pick it up, SIGILL
		 */
		if (notify_die(DIE_BPT, "bpt", regs, 0,
			       3, SIGTRAP) != NOTIFY_STOP)
			signal = SIGILL;
	}

#ifdef CONFIG_MATHEMU
        if (signal == SIGFPE)
		do_fp_trap(regs, current->thread.fp_regs.fpc);
	else if (signal == SIGSEGV)
		do_trap(regs, signal, SEGV_MAPERR, "user address fault");
	else
#endif
	if (signal)
		do_trap(regs, signal, ILL_ILLOPC, "illegal operation");
}


#ifdef CONFIG_MATHEMU
void specification_exception(struct pt_regs *regs)
{
        __u8 opcode[6];
	__u16 __user *location = NULL;
	int signal = 0;

	location = (__u16 __user *) get_psw_address(regs);

        if (regs->psw.mask & PSW_MASK_PSTATE) {
		get_user(*((__u16 *) opcode), location);
		switch (opcode[0]) {
		case 0x28: /* LDR Rx,Ry   */
			signal = math_emu_ldr(opcode);
			break;
		case 0x38: /* LER Rx,Ry   */
			signal = math_emu_ler(opcode);
			break;
		case 0x60: /* STD R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_std(opcode, regs);
			break;
		case 0x68: /* LD R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_ld(opcode, regs);
			break;
		case 0x70: /* STE R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_ste(opcode, regs);
			break;
		case 0x78: /* LE R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_le(opcode, regs);
			break;
		default:
			signal = SIGILL;
			break;
                }
        } else
		signal = SIGILL;

        if (signal == SIGFPE)
		do_fp_trap(regs, current->thread.fp_regs.fpc);
	else if (signal)
		do_trap(regs, signal, ILL_ILLOPN, "specification exception");
}
#else
DO_ERROR_INFO(specification_exception, SIGILL, ILL_ILLOPN,
	      "specification exception");
#endif

static void data_exception(struct pt_regs *regs)
{
	__u16 __user *location;
	int signal = 0;

	location = get_psw_address(regs);

	if (MACHINE_HAS_IEEE)
		asm volatile("stfpc %0" : "=m" (current->thread.fp_regs.fpc));

#ifdef CONFIG_MATHEMU
        else if (regs->psw.mask & PSW_MASK_PSTATE) {
        	__u8 opcode[6];
		get_user(*((__u16 *) opcode), location);
		switch (opcode[0]) {
		case 0x28: /* LDR Rx,Ry   */
			signal = math_emu_ldr(opcode);
			break;
		case 0x38: /* LER Rx,Ry   */
			signal = math_emu_ler(opcode);
			break;
		case 0x60: /* STD R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_std(opcode, regs);
			break;
		case 0x68: /* LD R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_ld(opcode, regs);
			break;
		case 0x70: /* STE R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_ste(opcode, regs);
			break;
		case 0x78: /* LE R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_le(opcode, regs);
			break;
		case 0xb3:
			get_user(*((__u16 *) (opcode+2)), location+1);
			signal = math_emu_b3(opcode, regs);
			break;
                case 0xed:
			get_user(*((__u32 *) (opcode+2)),
				 (__u32 __user *)(location+1));
			signal = math_emu_ed(opcode, regs);
			break;
	        case 0xb2:
			if (opcode[1] == 0x99) {
				get_user(*((__u16 *) (opcode+2)), location+1);
				signal = math_emu_srnm(opcode, regs);
			} else if (opcode[1] == 0x9c) {
				get_user(*((__u16 *) (opcode+2)), location+1);
				signal = math_emu_stfpc(opcode, regs);
			} else if (opcode[1] == 0x9d) {
				get_user(*((__u16 *) (opcode+2)), location+1);
				signal = math_emu_lfpc(opcode, regs);
			} else
				signal = SIGILL;
			break;
		default:
			signal = SIGILL;
			break;
                }
        }
#endif 
	if (current->thread.fp_regs.fpc & FPC_DXC_MASK)
		signal = SIGFPE;
	else
		signal = SIGILL;
        if (signal == SIGFPE)
		do_fp_trap(regs, current->thread.fp_regs.fpc);
	else if (signal)
		do_trap(regs, signal, ILL_ILLOPN, "data exception");
}

static void space_switch_exception(struct pt_regs *regs)
{
	/* Set user psw back to home space mode. */
	if (regs->psw.mask & PSW_MASK_PSTATE)
		regs->psw.mask |= PSW_ASC_HOME;
	/* Send SIGILL. */
	do_trap(regs, SIGILL, ILL_PRVOPC, "space switch event");
}

void __kprobes kernel_stack_overflow(struct pt_regs * regs)
{
	bust_spinlocks(1);
	printk("Kernel stack overflow.\n");
	show_regs(regs);
	bust_spinlocks(0);
	panic("Corrupt kernel stack, can't continue.");
}

/* init is done in lowcore.S and head.S */

void __init trap_init(void)
{
        int i;

        for (i = 0; i < 128; i++)
          pgm_check_table[i] = &default_trap_handler;
        pgm_check_table[1] = &illegal_op;
        pgm_check_table[2] = &privileged_op;
        pgm_check_table[3] = &execute_exception;
        pgm_check_table[4] = &do_protection_exception;
        pgm_check_table[5] = &addressing_exception;
        pgm_check_table[6] = &specification_exception;
        pgm_check_table[7] = &data_exception;
        pgm_check_table[8] = &overflow_exception;
        pgm_check_table[9] = &divide_exception;
        pgm_check_table[0x0A] = &overflow_exception;
        pgm_check_table[0x0B] = &divide_exception;
        pgm_check_table[0x0C] = &hfp_overflow_exception;
        pgm_check_table[0x0D] = &hfp_underflow_exception;
        pgm_check_table[0x0E] = &hfp_significance_exception;
        pgm_check_table[0x0F] = &hfp_divide_exception;
        pgm_check_table[0x10] = &do_dat_exception;
        pgm_check_table[0x11] = &do_dat_exception;
        pgm_check_table[0x12] = &translation_exception;
        pgm_check_table[0x13] = &special_op_exception;
#ifdef CONFIG_64BIT
	pgm_check_table[0x38] = &do_asce_exception;
	pgm_check_table[0x39] = &do_dat_exception;
	pgm_check_table[0x3A] = &do_dat_exception;
        pgm_check_table[0x3B] = &do_dat_exception;
#endif /* CONFIG_64BIT */
        pgm_check_table[0x15] = &operand_exception;
        pgm_check_table[0x1C] = &space_switch_exception;
        pgm_check_table[0x1D] = &hfp_sqrt_exception;
	/* Enable machine checks early. */
	local_mcck_enable();
}
