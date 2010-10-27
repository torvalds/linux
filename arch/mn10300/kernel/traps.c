/* MN10300 Exception handling
 *
 * Copyright (C) 2007 Matsushita Electric Industrial Co., Ltd.
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Modified by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
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
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/kallsyms.h>
#include <linux/pci.h>
#include <linux/kdebug.h>
#include <linux/bug.h>
#include <linux/irq.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <asm/smp.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>
#include <asm/cpu-regs.h>
#include <asm/busctl-regs.h>
#include <unit/leds.h>
#include <asm/fpu.h>
#include <asm/gdb-stub.h>
#include <asm/sections.h>

#if (CONFIG_INTERRUPT_VECTOR_BASE & 0xffffff)
#error "INTERRUPT_VECTOR_BASE not aligned to 16MiB boundary!"
#endif

int kstack_depth_to_print = 24;

spinlock_t die_lock = __SPIN_LOCK_UNLOCKED(die_lock);

ATOMIC_NOTIFIER_HEAD(mn10300_die_chain);

/*
 * These constants are for searching for possible module text
 * segments. MODULE_RANGE is a guess of how much space is likely
 * to be vmalloced.
 */
#define MODULE_RANGE (8 * 1024 * 1024)

#define DO_ERROR(signr, prologue, str, name)			\
asmlinkage void name(struct pt_regs *regs, u32 intcode)		\
{								\
	prologue;						\
	if (die_if_no_fixup(str, regs, intcode))		\
		return;						\
	force_sig(signr, current);				\
}

#define DO_EINFO(signr, prologue, str, name, sicode)			\
asmlinkage void name(struct pt_regs *regs, u32 intcode)			\
{									\
	siginfo_t info;							\
	prologue;							\
	if (die_if_no_fixup(str, regs, intcode))			\
		return;							\
	info.si_signo = signr;						\
	if (signr == SIGILL && sicode == ILL_ILLOPC) {			\
		uint8_t opcode;						\
		if (get_user(opcode, (uint8_t __user *)regs->pc) == 0)	\
			if (opcode == 0xff)				\
				info.si_signo = SIGTRAP;		\
	}								\
	info.si_errno = 0;						\
	info.si_code = sicode;						\
	info.si_addr = (void *) regs->pc;				\
	force_sig_info(info.si_signo, &info, current);			\
}

DO_ERROR(SIGTRAP, {}, "trap",			trap);
DO_ERROR(SIGSEGV, {}, "ibreak",			ibreak);
DO_ERROR(SIGSEGV, {}, "obreak",			obreak);
DO_EINFO(SIGSEGV, {}, "access error",		access_error,	SEGV_ACCERR);
DO_EINFO(SIGSEGV, {}, "insn access error",	insn_acc_error,	SEGV_ACCERR);
DO_EINFO(SIGSEGV, {}, "data access error",	data_acc_error,	SEGV_ACCERR);
DO_EINFO(SIGILL,  {}, "privileged opcode",	priv_op,	ILL_PRVOPC);
DO_EINFO(SIGILL,  {}, "invalid opcode",		invalid_op,	ILL_ILLOPC);
DO_EINFO(SIGILL,  {}, "invalid ex opcode",	invalid_exop,	ILL_ILLOPC);
DO_EINFO(SIGBUS,  {}, "invalid address",	mem_error,	BUS_ADRERR);
DO_EINFO(SIGBUS,  {}, "bus error",		bus_error,	BUS_ADRERR);

DO_ERROR(SIGTRAP,
#ifndef CONFIG_MN10300_USING_JTAG
	 DCR &= ~0x0001,
#else
	 {},
#endif
	 "single step", istep);

/*
 * handle NMI
 */
asmlinkage void nmi(struct pt_regs *regs, enum exception_code code)
{
	/* see if gdbstub wants to deal with it */
#ifdef CONFIG_GDBSTUB
	if (gdbstub_intercept(regs, code))
		return;
#endif

	printk(KERN_WARNING "--- Register Dump ---\n");
	show_registers(regs);
	printk(KERN_WARNING "---------------------\n");
}

/*
 * show a stack trace from the specified stack pointer
 */
void show_trace(unsigned long *sp)
{
	unsigned long *stack, addr, module_start, module_end;
	int i;

	printk(KERN_EMERG "\nCall Trace:");

	stack = sp;
	i = 0;
	module_start = VMALLOC_START;
	module_end = VMALLOC_END;

	while (((long) stack & (THREAD_SIZE - 1)) != 0) {
		addr = *stack++;
		if (__kernel_text_address(addr)) {
#if 1
			printk(" [<%08lx>]", addr);
			print_symbol(" %s", addr);
			printk("\n");
#else
			if ((i % 6) == 0)
				printk(KERN_EMERG "  ");
			printk("[<%08lx>] ", addr);
			i++;
#endif
		}
	}

	printk("\n");
}

/*
 * show the raw stack from the specified stack pointer
 */
void show_stack(struct task_struct *task, unsigned long *sp)
{
	unsigned long *stack;
	int i;

	if (!sp)
		sp = (unsigned long *) &sp;

	stack = sp;
	printk(KERN_EMERG "Stack:");
	for (i = 0; i < kstack_depth_to_print; i++) {
		if (((long) stack & (THREAD_SIZE - 1)) == 0)
			break;
		if ((i % 8) == 0)
			printk(KERN_EMERG "  ");
		printk("%08lx ", *stack++);
	}

	show_trace(sp);
}

/*
 * the architecture-independent dump_stack generator
 */
void dump_stack(void)
{
	unsigned long stack;

	show_stack(current, &stack);
}
EXPORT_SYMBOL(dump_stack);

/*
 * dump the register file in the specified exception frame
 */
void show_registers_only(struct pt_regs *regs)
{
	unsigned long ssp;

	ssp = (unsigned long) regs + sizeof(*regs);

	printk(KERN_EMERG "PC:  %08lx EPSW:  %08lx  SSP: %08lx mode: %s\n",
	       regs->pc, regs->epsw, ssp, user_mode(regs) ? "User" : "Super");
	printk(KERN_EMERG "d0:  %08lx   d1:  %08lx   d2: %08lx   d3: %08lx\n",
	       regs->d0, regs->d1, regs->d2, regs->d3);
	printk(KERN_EMERG "a0:  %08lx   a1:  %08lx   a2: %08lx   a3: %08lx\n",
	       regs->a0, regs->a1, regs->a2, regs->a3);
	printk(KERN_EMERG "e0:  %08lx   e1:  %08lx   e2: %08lx   e3: %08lx\n",
	       regs->e0, regs->e1, regs->e2, regs->e3);
	printk(KERN_EMERG "e4:  %08lx   e5:  %08lx   e6: %08lx   e7: %08lx\n",
	       regs->e4, regs->e5, regs->e6, regs->e7);
	printk(KERN_EMERG "lar: %08lx   lir: %08lx  mdr: %08lx  usp: %08lx\n",
	       regs->lar, regs->lir, regs->mdr, regs->sp);
	printk(KERN_EMERG "cvf: %08lx   crl: %08lx  crh: %08lx  drq: %08lx\n",
	       regs->mcvf, regs->mcrl, regs->mcrh, regs->mdrq);
	printk(KERN_EMERG "threadinfo=%p task=%p)\n",
	       current_thread_info(), current);

	if ((unsigned long) current >= PAGE_OFFSET &&
	    (unsigned long) current < (unsigned long)high_memory)
		printk(KERN_EMERG "Process %s (pid: %d)\n",
		       current->comm, current->pid);

#ifdef CONFIG_SMP
	printk(KERN_EMERG "CPUID:  %08x\n", CPUID);
#endif
	printk(KERN_EMERG "CPUP:   %04hx\n", CPUP);
	printk(KERN_EMERG "TBR:    %08x\n", TBR);
	printk(KERN_EMERG "DEAR:   %08x\n", DEAR);
	printk(KERN_EMERG "sISR:   %08x\n", sISR);
	printk(KERN_EMERG "NMICR:  %04hx\n", NMICR);
	printk(KERN_EMERG "BCBERR: %08x\n", BCBERR);
	printk(KERN_EMERG "BCBEAR: %08x\n", BCBEAR);
	printk(KERN_EMERG "MMUFCR: %08x\n", MMUFCR);
	printk(KERN_EMERG "IPTEU : %08x  IPTEL2: %08x\n", IPTEU, IPTEL2);
	printk(KERN_EMERG "DPTEU:  %08x  DPTEL2: %08x\n", DPTEU, DPTEL2);
}

/*
 * dump the registers and the stack
 */
void show_registers(struct pt_regs *regs)
{
	unsigned long sp;
	int i;

	show_registers_only(regs);

	if (!user_mode(regs))
		sp = (unsigned long) regs + sizeof(*regs);
	else
		sp = regs->sp;

	/* when in-kernel, we also print out the stack and code at the
	 * time of the fault..
	 */
	if (!user_mode(regs)) {
		printk(KERN_EMERG "\n");
		show_stack(current, (unsigned long *) sp);

#if 0
		printk(KERN_EMERG "\nCode: ");
		if (regs->pc < PAGE_OFFSET)
			goto bad;

		for (i = 0; i < 20; i++) {
			unsigned char c;
			if (__get_user(c, &((unsigned char *) regs->pc)[i]))
				goto bad;
			printk("%02x ", c);
		}
#else
		i = 0;
#endif
	}

	printk("\n");
	return;

#if 0
bad:
	printk(KERN_EMERG " Bad PC value.");
	break;
#endif
}

/*
 *
 */
void show_trace_task(struct task_struct *tsk)
{
	unsigned long sp = tsk->thread.sp;

	/* User space on another CPU? */
	if ((sp ^ (unsigned long) tsk) & (PAGE_MASK << 1))
		return;

	show_trace((unsigned long *) sp);
}

/*
 * note the untimely death of part of the kernel
 */
void die(const char *str, struct pt_regs *regs, enum exception_code code)
{
	console_verbose();
	spin_lock_irq(&die_lock);
	printk(KERN_EMERG "\n%s: %04x\n",
	       str, code & 0xffff);
	show_registers(regs);

	if (regs->pc >= 0x02000000 && regs->pc < 0x04000000 &&
	    (regs->epsw & (EPSW_IM | EPSW_IE)) != (EPSW_IM | EPSW_IE)) {
		printk(KERN_EMERG "Exception in usermode interrupt handler\n");
		printk(KERN_EMERG "\nPlease connect to kernel debugger !!\n");
		asm volatile ("0: bra 0b");
	}

	spin_unlock_irq(&die_lock);
	do_exit(SIGSEGV);
}

/*
 * see if there's a fixup handler we can force a jump to when an exception
 * happens due to something kernel code did
 */
int die_if_no_fixup(const char *str, struct pt_regs *regs,
		    enum exception_code code)
{
	if (user_mode(regs))
		return 0;

	peripheral_leds_display_exception(code);

	switch (code) {
		/* see if we can fixup the kernel accessing memory */
	case EXCEP_ITLBMISS:
	case EXCEP_DTLBMISS:
	case EXCEP_IAERROR:
	case EXCEP_DAERROR:
	case EXCEP_MEMERR:
	case EXCEP_MISALIGN:
	case EXCEP_BUSERROR:
	case EXCEP_ILLDATACC:
	case EXCEP_IOINSACC:
	case EXCEP_PRIVINSACC:
	case EXCEP_PRIVDATACC:
	case EXCEP_DATINSACC:
		if (fixup_exception(regs))
			return 1;
	case EXCEP_UNIMPINS:
		if (regs->pc && *(uint8_t *)regs->pc == 0xff)
			if (notify_die(DIE_BREAKPOINT, str, regs, code, 0, 0))
				return 1;
		break;
	default:
		break;
	}

	/* see if gdbstub wants to deal with it */
#ifdef CONFIG_GDBSTUB
	if (gdbstub_intercept(regs, code))
		return 1;
#endif

	if (notify_die(DIE_GPF, str, regs, code, 0, 0))
		return 1;

	/* make the process die as the last resort */
	die(str, regs, code);
}

/*
 * handle unsupported syscall instructions (syscall 1-15)
 */
static asmlinkage void unsupported_syscall(struct pt_regs *regs,
					   enum exception_code code)
{
	struct task_struct *tsk = current;
	siginfo_t info;

	/* catch a kernel BUG() */
	if (code == EXCEP_SYSCALL15 && !user_mode(regs)) {
		if (report_bug(regs->pc, regs) == BUG_TRAP_TYPE_BUG) {
#ifdef CONFIG_GDBSTUB
			gdbstub_intercept(regs, code);
#endif
		}
	}

	regs->pc -= 2; /* syscall return addr is _after_ the instruction */

	die_if_no_fixup("An unsupported syscall insn was used by the kernel\n",
			regs, code);

	info.si_signo	= SIGILL;
	info.si_errno	= ENOSYS;
	info.si_code	= ILL_ILLTRP;
	info.si_addr	= (void *) regs->pc;
	force_sig_info(SIGILL, &info, tsk);
}

/*
 * display the register file when the stack pointer gets clobbered
 */
asmlinkage void do_double_fault(struct pt_regs *regs)
{
	struct task_struct *tsk = current;

	strcpy(tsk->comm, "emergency tsk");
	tsk->pid = 0;
	console_verbose();
	printk(KERN_EMERG "--- double fault ---\n");
	show_registers(regs);
}

/*
 * asynchronous bus error (external, usually I/O DMA)
 */
asmlinkage void io_bus_error(u32 bcberr, u32 bcbear, struct pt_regs *regs)
{
	console_verbose();

	printk(KERN_EMERG "Asynchronous I/O Bus Error\n");
	printk(KERN_EMERG "==========================\n");

	if (bcberr & BCBERR_BEME)
		printk(KERN_EMERG "- Multiple recorded errors\n");

	printk(KERN_EMERG "- Faulting Buses:%s%s%s\n",
	       bcberr & BCBERR_BEMR_CI  ? " CPU-Ins-Fetch" : "",
	       bcberr & BCBERR_BEMR_CD  ? " CPU-Data" : "",
	       bcberr & BCBERR_BEMR_DMA ? " DMA" : "");

	printk(KERN_EMERG "- %s %s access made to %s at address %08x\n",
	       bcberr &	BCBERR_BEBST ? "Burst" : "Single",
	       bcberr &	BCBERR_BERW ? "Read" : "Write",
	       bcberr &	BCBERR_BESB_MON  ? "Monitor Space" :
	       bcberr &	BCBERR_BESB_IO   ? "Internal CPU I/O Space" :
	       bcberr &	BCBERR_BESB_EX   ? "External I/O Bus" :
	       bcberr &	BCBERR_BESB_OPEX ? "External Memory Bus" :
	       "On Chip Memory",
	       bcbear
	       );

	printk(KERN_EMERG "- Detected by the %s\n",
	       bcberr&BCBERR_BESD ? "Bus Control Unit" : "Slave Bus");

#ifdef CONFIG_PCI
#define BRIDGEREGB(X) (*(volatile __u8  *)(0xBE040000 + (X)))
#define BRIDGEREGW(X) (*(volatile __u16 *)(0xBE040000 + (X)))
#define BRIDGEREGL(X) (*(volatile __u32 *)(0xBE040000 + (X)))

	printk(KERN_EMERG "- PCI Memory Paging Reg:         %08x\n",
	       *(volatile __u32 *) (0xBFFFFFF4));
	printk(KERN_EMERG "- PCI Bridge Base Address 0:     %08x\n",
	       BRIDGEREGL(PCI_BASE_ADDRESS_0));
	printk(KERN_EMERG "- PCI Bridge AMPCI Base Address: %08x\n",
	       BRIDGEREGL(0x48));
	printk(KERN_EMERG "- PCI Bridge Command:                %04hx\n",
	       BRIDGEREGW(PCI_COMMAND));
	printk(KERN_EMERG "- PCI Bridge Status:                 %04hx\n",
	       BRIDGEREGW(PCI_STATUS));
	printk(KERN_EMERG "- PCI Bridge Int Status:         %08hx\n",
	       BRIDGEREGL(0x4c));
#endif

	printk(KERN_EMERG "\n");
	show_registers(regs);

	panic("Halted due to asynchronous I/O Bus Error\n");
}

/*
 * handle an exception for which a handler has not yet been installed
 */
asmlinkage void uninitialised_exception(struct pt_regs *regs,
					enum exception_code code)
{

	/* see if gdbstub wants to deal with it */
#ifdef CONFIG_GDBSTUB
	if (gdbstub_intercept(regs, code))
		return;
#endif

	peripheral_leds_display_exception(code);
	printk(KERN_EMERG "Uninitialised Exception 0x%04x\n", code & 0xFFFF);
	show_registers(regs);

	for (;;)
		continue;
}

/*
 * set an interrupt stub to jump to a handler
 * ! NOTE: this does *not* flush the caches
 */
void __init __set_intr_stub(enum exception_code code, void *handler)
{
	unsigned long addr;
	u8 *vector = (u8 *)(CONFIG_INTERRUPT_VECTOR_BASE + code);

	addr = (unsigned long) handler - (unsigned long) vector;
	vector[0] = 0xdc;		/* JMP handler */
	vector[1] = addr;
	vector[2] = addr >> 8;
	vector[3] = addr >> 16;
	vector[4] = addr >> 24;
	vector[5] = 0xcb;
	vector[6] = 0xcb;
	vector[7] = 0xcb;
}

/*
 * set an interrupt stub to jump to a handler
 */
void __init set_intr_stub(enum exception_code code, void *handler)
{
	unsigned long addr;
	u8 *vector = (u8 *)(CONFIG_INTERRUPT_VECTOR_BASE + code);
	unsigned long flags;

	addr = (unsigned long) handler - (unsigned long) vector;

	flags = arch_local_cli_save();

	vector[0] = 0xdc;		/* JMP handler */
	vector[1] = addr;
	vector[2] = addr >> 8;
	vector[3] = addr >> 16;
	vector[4] = addr >> 24;
	vector[5] = 0xcb;
	vector[6] = 0xcb;
	vector[7] = 0xcb;

	arch_local_irq_restore(flags);

#ifndef CONFIG_MN10300_CACHE_SNOOP
	mn10300_dcache_flush_inv();
	mn10300_icache_inv();
#endif
}

/*
 * initialise the exception table
 */
void __init trap_init(void)
{
	set_excp_vector(EXCEP_TRAP,		trap);
	set_excp_vector(EXCEP_ISTEP,		istep);
	set_excp_vector(EXCEP_IBREAK,		ibreak);
	set_excp_vector(EXCEP_OBREAK,		obreak);

	set_excp_vector(EXCEP_PRIVINS,		priv_op);
	set_excp_vector(EXCEP_UNIMPINS,		invalid_op);
	set_excp_vector(EXCEP_UNIMPEXINS,	invalid_exop);
	set_excp_vector(EXCEP_MEMERR,		mem_error);
	set_excp_vector(EXCEP_MISALIGN,		misalignment);
	set_excp_vector(EXCEP_BUSERROR,		bus_error);
	set_excp_vector(EXCEP_ILLINSACC,	insn_acc_error);
	set_excp_vector(EXCEP_ILLDATACC,	data_acc_error);
	set_excp_vector(EXCEP_IOINSACC,		insn_acc_error);
	set_excp_vector(EXCEP_PRIVINSACC,	insn_acc_error);
	set_excp_vector(EXCEP_PRIVDATACC,	data_acc_error);
	set_excp_vector(EXCEP_DATINSACC,	insn_acc_error);
	set_excp_vector(EXCEP_FPU_UNIMPINS,	fpu_invalid_op);
	set_excp_vector(EXCEP_FPU_OPERATION,	fpu_exception);

	set_excp_vector(EXCEP_NMI,		nmi);

	set_excp_vector(EXCEP_SYSCALL1,		unsupported_syscall);
	set_excp_vector(EXCEP_SYSCALL2,		unsupported_syscall);
	set_excp_vector(EXCEP_SYSCALL3,		unsupported_syscall);
	set_excp_vector(EXCEP_SYSCALL4,		unsupported_syscall);
	set_excp_vector(EXCEP_SYSCALL5,		unsupported_syscall);
	set_excp_vector(EXCEP_SYSCALL6,		unsupported_syscall);
	set_excp_vector(EXCEP_SYSCALL7,		unsupported_syscall);
	set_excp_vector(EXCEP_SYSCALL8,		unsupported_syscall);
	set_excp_vector(EXCEP_SYSCALL9,		unsupported_syscall);
	set_excp_vector(EXCEP_SYSCALL10,	unsupported_syscall);
	set_excp_vector(EXCEP_SYSCALL11,	unsupported_syscall);
	set_excp_vector(EXCEP_SYSCALL12,	unsupported_syscall);
	set_excp_vector(EXCEP_SYSCALL13,	unsupported_syscall);
	set_excp_vector(EXCEP_SYSCALL14,	unsupported_syscall);
	set_excp_vector(EXCEP_SYSCALL15,	unsupported_syscall);
}

/*
 * determine if a program counter value is a valid bug address
 */
int is_valid_bugaddr(unsigned long pc)
{
	return pc >= PAGE_OFFSET;
}
