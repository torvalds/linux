/*
 * Main exception handling logic.
 *
 * Copyright 2004-2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#include <linux/bug.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <asm/traps.h>
#include <asm/cplb.h>
#include <asm/blackfin.h>
#include <asm/irq_handler.h>
#include <linux/irq.h>
#include <asm/trace.h>
#include <asm/fixed_code.h>
#include <asm/pseudo_instructions.h>
#include <asm/pda.h>
#include <asm/asm-offsets.h>

#ifdef CONFIG_KGDB
# include <linux/kgdb.h>

# define CHK_DEBUGGER_TRAP() \
	do { \
		kgdb_handle_exception(trapnr, sig, info.si_code, fp); \
	} while (0)
# define CHK_DEBUGGER_TRAP_MAYBE() \
	do { \
		if (kgdb_connected) \
			CHK_DEBUGGER_TRAP(); \
	} while (0)
#else
# define CHK_DEBUGGER_TRAP() do { } while (0)
# define CHK_DEBUGGER_TRAP_MAYBE() do { } while (0)
#endif


#ifdef CONFIG_DEBUG_VERBOSE
#define verbose_printk(fmt, arg...) \
	printk(fmt, ##arg)
#else
#define verbose_printk(fmt, arg...) \
	({ if (0) printk(fmt, ##arg); 0; })
#endif

#if defined(CONFIG_DEBUG_MMRS) || defined(CONFIG_DEBUG_MMRS_MODULE)
u32 last_seqstat;
#ifdef CONFIG_DEBUG_MMRS_MODULE
EXPORT_SYMBOL(last_seqstat);
#endif
#endif

/* Initiate the event table handler */
void __init trap_init(void)
{
	CSYNC();
	bfin_write_EVT3(trap);
	CSYNC();
}

static int kernel_mode_regs(struct pt_regs *regs)
{
	return regs->ipend & 0xffc0;
}

asmlinkage notrace void trap_c(struct pt_regs *fp)
{
#ifdef CONFIG_DEBUG_BFIN_HWTRACE_ON
	int j;
#endif
#ifdef CONFIG_BFIN_PSEUDODBG_INSNS
	int opcode;
#endif
	unsigned int cpu = raw_smp_processor_id();
	const char *strerror = NULL;
	int sig = 0;
	siginfo_t info;
	unsigned long trapnr = fp->seqstat & SEQSTAT_EXCAUSE;

	trace_buffer_save(j);
#if defined(CONFIG_DEBUG_MMRS) || defined(CONFIG_DEBUG_MMRS_MODULE)
	last_seqstat = (u32)fp->seqstat;
#endif

	/* Important - be very careful dereferncing pointers - will lead to
	 * double faults if the stack has become corrupt
	 */

	/* trap_c() will be called for exceptions. During exceptions
	 * processing, the pc value should be set with retx value.
	 * With this change we can cleanup some code in signal.c- TODO
	 */
	fp->orig_pc = fp->retx;
	/* printk("exception: 0x%x, ipend=%x, reti=%x, retx=%x\n",
		trapnr, fp->ipend, fp->pc, fp->retx); */

	/* send the appropriate signal to the user program */
	switch (trapnr) {

	/* This table works in conjunction with the one in ./mach-common/entry.S
	 * Some exceptions are handled there (in assembly, in exception space)
	 * Some are handled here, (in C, in interrupt space)
	 * Some, like CPLB, are handled in both, where the normal path is
	 * handled in assembly/exception space, and the error path is handled
	 * here
	 */

	/* 0x00 - Linux Syscall, getting here is an error */
	/* 0x01 - userspace gdb breakpoint, handled here */
	case VEC_EXCPT01:
		info.si_code = TRAP_ILLTRAP;
		sig = SIGTRAP;
		CHK_DEBUGGER_TRAP_MAYBE();
		/* Check if this is a breakpoint in kernel space */
		if (kernel_mode_regs(fp))
			goto traps_done;
		else
			break;
	/* 0x03 - User Defined, userspace stack overflow */
	case VEC_EXCPT03:
		info.si_code = SEGV_STACKFLOW;
		sig = SIGSEGV;
		strerror = KERN_NOTICE EXC_0x03(KERN_NOTICE);
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x02 - KGDB initial connection and break signal trap */
	case VEC_EXCPT02:
#ifdef CONFIG_KGDB
		info.si_code = TRAP_ILLTRAP;
		sig = SIGTRAP;
		CHK_DEBUGGER_TRAP();
		goto traps_done;
#endif
	/* 0x04 - User Defined */
	/* 0x05 - User Defined */
	/* 0x06 - User Defined */
	/* 0x07 - User Defined */
	/* 0x08 - User Defined */
	/* 0x09 - User Defined */
	/* 0x0A - User Defined */
	/* 0x0B - User Defined */
	/* 0x0C - User Defined */
	/* 0x0D - User Defined */
	/* 0x0E - User Defined */
	/* 0x0F - User Defined */
	/* If we got here, it is most likely that someone was trying to use a
	 * custom exception handler, and it is not actually installed properly
	 */
	case VEC_EXCPT04 ... VEC_EXCPT15:
		info.si_code = ILL_ILLPARAOP;
		sig = SIGILL;
		strerror = KERN_NOTICE EXC_0x04(KERN_NOTICE);
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x10 HW Single step, handled here */
	case VEC_STEP:
		info.si_code = TRAP_STEP;
		sig = SIGTRAP;
		CHK_DEBUGGER_TRAP_MAYBE();
		/* Check if this is a single step in kernel space */
		if (kernel_mode_regs(fp))
			goto traps_done;
		else
			break;
	/* 0x11 - Trace Buffer Full, handled here */
	case VEC_OVFLOW:
		info.si_code = TRAP_TRACEFLOW;
		sig = SIGTRAP;
		strerror = KERN_NOTICE EXC_0x11(KERN_NOTICE);
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x12 - Reserved, Caught by default */
	/* 0x13 - Reserved, Caught by default */
	/* 0x14 - Reserved, Caught by default */
	/* 0x15 - Reserved, Caught by default */
	/* 0x16 - Reserved, Caught by default */
	/* 0x17 - Reserved, Caught by default */
	/* 0x18 - Reserved, Caught by default */
	/* 0x19 - Reserved, Caught by default */
	/* 0x1A - Reserved, Caught by default */
	/* 0x1B - Reserved, Caught by default */
	/* 0x1C - Reserved, Caught by default */
	/* 0x1D - Reserved, Caught by default */
	/* 0x1E - Reserved, Caught by default */
	/* 0x1F - Reserved, Caught by default */
	/* 0x20 - Reserved, Caught by default */
	/* 0x21 - Undefined Instruction, handled here */
	case VEC_UNDEF_I:
#ifdef CONFIG_BUG
		if (kernel_mode_regs(fp)) {
			switch (report_bug(fp->pc, fp)) {
			case BUG_TRAP_TYPE_NONE:
				break;
			case BUG_TRAP_TYPE_WARN:
				dump_bfin_trace_buffer();
				fp->pc += 2;
				goto traps_done;
			case BUG_TRAP_TYPE_BUG:
				/* call to panic() will dump trace, and it is
				 * off at this point, so it won't be clobbered
				 */
				panic("BUG()");
			}
		}
#endif
#ifdef CONFIG_BFIN_PSEUDODBG_INSNS
		/*
		 * Support for the fake instructions, if the instruction fails,
		 * then just execute a illegal opcode failure (like normal).
		 * Don't support these instructions inside the kernel
		 */
		if (!kernel_mode_regs(fp) && get_instruction(&opcode, (unsigned short *)fp->pc)) {
			if (execute_pseudodbg_assert(fp, opcode))
				goto traps_done;
			if (execute_pseudodbg(fp, opcode))
				goto traps_done;
		}
#endif
		info.si_code = ILL_ILLOPC;
		sig = SIGILL;
		strerror = KERN_NOTICE EXC_0x21(KERN_NOTICE);
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x22 - Illegal Instruction Combination, handled here */
	case VEC_ILGAL_I:
		info.si_code = ILL_ILLPARAOP;
		sig = SIGILL;
		strerror = KERN_NOTICE EXC_0x22(KERN_NOTICE);
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x23 - Data CPLB protection violation, handled here */
	case VEC_CPLB_VL:
		info.si_code = ILL_CPLB_VI;
		sig = SIGSEGV;
		strerror = KERN_NOTICE EXC_0x23(KERN_NOTICE);
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x24 - Data access misaligned, handled here */
	case VEC_MISALI_D:
		info.si_code = BUS_ADRALN;
		sig = SIGBUS;
		strerror = KERN_NOTICE EXC_0x24(KERN_NOTICE);
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x25 - Unrecoverable Event, handled here */
	case VEC_UNCOV:
		info.si_code = ILL_ILLEXCPT;
		sig = SIGILL;
		strerror = KERN_NOTICE EXC_0x25(KERN_NOTICE);
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x26 - Data CPLB Miss, normal case is handled in _cplb_hdr,
		error case is handled here */
	case VEC_CPLB_M:
		info.si_code = BUS_ADRALN;
		sig = SIGBUS;
		strerror = KERN_NOTICE EXC_0x26(KERN_NOTICE);
		break;
	/* 0x27 - Data CPLB Multiple Hits - Linux Trap Zero, handled here */
	case VEC_CPLB_MHIT:
		info.si_code = ILL_CPLB_MULHIT;
		sig = SIGSEGV;
#ifdef CONFIG_DEBUG_HUNT_FOR_ZERO
		if (cpu_pda[cpu].dcplb_fault_addr < FIXED_CODE_START)
			strerror = KERN_NOTICE "NULL pointer access\n";
		else
#endif
			strerror = KERN_NOTICE EXC_0x27(KERN_NOTICE);
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x28 - Emulation Watchpoint, handled here */
	case VEC_WATCH:
		info.si_code = TRAP_WATCHPT;
		sig = SIGTRAP;
		pr_debug(EXC_0x28(KERN_DEBUG));
		CHK_DEBUGGER_TRAP_MAYBE();
		/* Check if this is a watchpoint in kernel space */
		if (kernel_mode_regs(fp))
			goto traps_done;
		else
			break;
#ifdef CONFIG_BF535
	/* 0x29 - Instruction fetch access error (535 only) */
	case VEC_ISTRU_VL:      /* ADSP-BF535 only (MH) */
		info.si_code = BUS_OPFETCH;
		sig = SIGBUS;
		strerror = KERN_NOTICE "BF535: VEC_ISTRU_VL\n";
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
#else
	/* 0x29 - Reserved, Caught by default */
#endif
	/* 0x2A - Instruction fetch misaligned, handled here */
	case VEC_MISALI_I:
		info.si_code = BUS_ADRALN;
		sig = SIGBUS;
		strerror = KERN_NOTICE EXC_0x2A(KERN_NOTICE);
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x2B - Instruction CPLB protection violation, handled here */
	case VEC_CPLB_I_VL:
		info.si_code = ILL_CPLB_VI;
		sig = SIGBUS;
		strerror = KERN_NOTICE EXC_0x2B(KERN_NOTICE);
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x2C - Instruction CPLB miss, handled in _cplb_hdr */
	case VEC_CPLB_I_M:
		info.si_code = ILL_CPLB_MISS;
		sig = SIGBUS;
		strerror = KERN_NOTICE EXC_0x2C(KERN_NOTICE);
		break;
	/* 0x2D - Instruction CPLB Multiple Hits, handled here */
	case VEC_CPLB_I_MHIT:
		info.si_code = ILL_CPLB_MULHIT;
		sig = SIGSEGV;
#ifdef CONFIG_DEBUG_HUNT_FOR_ZERO
		if (cpu_pda[cpu].icplb_fault_addr < FIXED_CODE_START)
			strerror = KERN_NOTICE "Jump to NULL address\n";
		else
#endif
			strerror = KERN_NOTICE EXC_0x2D(KERN_NOTICE);
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x2E - Illegal use of Supervisor Resource, handled here */
	case VEC_ILL_RES:
		info.si_code = ILL_PRVOPC;
		sig = SIGILL;
		strerror = KERN_NOTICE EXC_0x2E(KERN_NOTICE);
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/* 0x2F - Reserved, Caught by default */
	/* 0x30 - Reserved, Caught by default */
	/* 0x31 - Reserved, Caught by default */
	/* 0x32 - Reserved, Caught by default */
	/* 0x33 - Reserved, Caught by default */
	/* 0x34 - Reserved, Caught by default */
	/* 0x35 - Reserved, Caught by default */
	/* 0x36 - Reserved, Caught by default */
	/* 0x37 - Reserved, Caught by default */
	/* 0x38 - Reserved, Caught by default */
	/* 0x39 - Reserved, Caught by default */
	/* 0x3A - Reserved, Caught by default */
	/* 0x3B - Reserved, Caught by default */
	/* 0x3C - Reserved, Caught by default */
	/* 0x3D - Reserved, Caught by default */
	/* 0x3E - Reserved, Caught by default */
	/* 0x3F - Reserved, Caught by default */
	case VEC_HWERR:
		info.si_code = BUS_ADRALN;
		sig = SIGBUS;
		switch (fp->seqstat & SEQSTAT_HWERRCAUSE) {
		/* System MMR Error */
		case (SEQSTAT_HWERRCAUSE_SYSTEM_MMR):
			info.si_code = BUS_ADRALN;
			sig = SIGBUS;
			strerror = KERN_NOTICE HWC_x2(KERN_NOTICE);
			break;
		/* External Memory Addressing Error */
		case (SEQSTAT_HWERRCAUSE_EXTERN_ADDR):
			if (ANOMALY_05000310) {
				static unsigned long anomaly_rets;

				if ((fp->pc >= (L1_CODE_START + L1_CODE_LENGTH - 512)) &&
				    (fp->pc < (L1_CODE_START + L1_CODE_LENGTH))) {
					/*
					 * A false hardware error will happen while fetching at
					 * the L1 instruction SRAM boundary.  Ignore it.
					 */
					anomaly_rets = fp->rets;
					goto traps_done;
				} else if (fp->rets == anomaly_rets) {
					/*
					 * While boundary code returns to a function, at the ret
					 * point, a new false hardware error might occur too based
					 * on tests.  Ignore it too.
					 */
					goto traps_done;
				} else if ((fp->rets >= (L1_CODE_START + L1_CODE_LENGTH - 512)) &&
				           (fp->rets < (L1_CODE_START + L1_CODE_LENGTH))) {
					/*
					 * If boundary code calls a function, at the entry point,
					 * a new false hardware error maybe happen based on tests.
					 * Ignore it too.
					 */
					goto traps_done;
				} else
					anomaly_rets = 0;
			}

			info.si_code = BUS_ADRERR;
			sig = SIGBUS;
			strerror = KERN_NOTICE HWC_x3(KERN_NOTICE);
			break;
		/* Performance Monitor Overflow */
		case (SEQSTAT_HWERRCAUSE_PERF_FLOW):
			strerror = KERN_NOTICE HWC_x12(KERN_NOTICE);
			break;
		/* RAISE 5 instruction */
		case (SEQSTAT_HWERRCAUSE_RAISE_5):
			printk(KERN_NOTICE HWC_x18(KERN_NOTICE));
			break;
		default:        /* Reserved */
			printk(KERN_NOTICE HWC_default(KERN_NOTICE));
			break;
		}
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	/*
	 * We should be handling all known exception types above,
	 * if we get here we hit a reserved one, so panic
	 */
	default:
		info.si_code = ILL_ILLPARAOP;
		sig = SIGILL;
		verbose_printk(KERN_EMERG "Caught Unhandled Exception, code = %08lx\n",
			(fp->seqstat & SEQSTAT_EXCAUSE));
		CHK_DEBUGGER_TRAP_MAYBE();
		break;
	}

	BUG_ON(sig == 0);

	/* If the fault was caused by a kernel thread, or interrupt handler
	 * we will kernel panic, so the system reboots.
	 */
	if (kernel_mode_regs(fp) || (current && !current->mm)) {
		console_verbose();
		oops_in_progress = 1;
	}

	if (sig != SIGTRAP) {
		if (strerror)
			verbose_printk(strerror);

		dump_bfin_process(fp);
		dump_bfin_mem(fp);
		show_regs(fp);

		/* Print out the trace buffer if it makes sense */
#ifndef CONFIG_DEBUG_BFIN_NO_KERN_HWTRACE
		if (trapnr == VEC_CPLB_I_M || trapnr == VEC_CPLB_M)
			verbose_printk(KERN_NOTICE "No trace since you do not have "
			       "CONFIG_DEBUG_BFIN_NO_KERN_HWTRACE enabled\n\n");
		else
#endif
			dump_bfin_trace_buffer();

		if (oops_in_progress) {
			/* Dump the current kernel stack */
			verbose_printk(KERN_NOTICE "Kernel Stack\n");
			show_stack(current, NULL);
			print_modules();
#ifndef CONFIG_ACCESS_CHECK
			verbose_printk(KERN_EMERG "Please turn on "
			       "CONFIG_ACCESS_CHECK\n");
#endif
			panic("Kernel exception");
		} else {
#ifdef CONFIG_DEBUG_VERBOSE
			unsigned long *stack;
			/* Dump the user space stack */
			stack = (unsigned long *)rdusp();
			verbose_printk(KERN_NOTICE "Userspace Stack\n");
			show_stack(NULL, stack);
#endif
		}
	}

#ifdef CONFIG_IPIPE
	if (!ipipe_trap_notify(fp->seqstat & 0x3f, fp))
#endif
	{
		info.si_signo = sig;
		info.si_errno = 0;
		switch (trapnr) {
		case VEC_CPLB_VL:
		case VEC_MISALI_D:
		case VEC_CPLB_M:
		case VEC_CPLB_MHIT:
			info.si_addr = (void __user *)cpu_pda[cpu].dcplb_fault_addr;
			break;
		default:
			info.si_addr = (void __user *)fp->pc;
			break;
		}
		force_sig_info(sig, &info, current);
	}

	if ((ANOMALY_05000461 && trapnr == VEC_HWERR && !access_ok(VERIFY_READ, fp->pc, 8)) ||
	    (ANOMALY_05000281 && trapnr == VEC_HWERR) ||
	    (ANOMALY_05000189 && (trapnr == VEC_CPLB_I_VL || trapnr == VEC_CPLB_VL)))
		fp->pc = SAFE_USER_INSTRUCTION;

 traps_done:
	trace_buffer_restore(j);
}

asmlinkage void double_fault_c(struct pt_regs *fp)
{
#ifdef CONFIG_DEBUG_BFIN_HWTRACE_ON
	int j;
	trace_buffer_save(j);
#endif

	console_verbose();
	oops_in_progress = 1;
#ifdef CONFIG_DEBUG_VERBOSE
	printk(KERN_EMERG "Double Fault\n");
#ifdef CONFIG_DEBUG_DOUBLEFAULT_PRINT
	if (((long)fp->seqstat &  SEQSTAT_EXCAUSE) == VEC_UNCOV) {
		unsigned int cpu = raw_smp_processor_id();
		char buf[150];
		decode_address(buf, cpu_pda[cpu].retx_doublefault);
		printk(KERN_EMERG "While handling exception (EXCAUSE = 0x%x) at %s:\n",
			(unsigned int)cpu_pda[cpu].seqstat_doublefault & SEQSTAT_EXCAUSE, buf);
		decode_address(buf, cpu_pda[cpu].dcplb_doublefault_addr);
		printk(KERN_NOTICE "   DCPLB_FAULT_ADDR: %s\n", buf);
		decode_address(buf, cpu_pda[cpu].icplb_doublefault_addr);
		printk(KERN_NOTICE "   ICPLB_FAULT_ADDR: %s\n", buf);

		decode_address(buf, fp->retx);
		printk(KERN_NOTICE "The instruction at %s caused a double exception\n", buf);
	} else
#endif
	{
		dump_bfin_process(fp);
		dump_bfin_mem(fp);
		show_regs(fp);
		dump_bfin_trace_buffer();
	}
#endif
	panic("Double Fault - unrecoverable event");

}


void panic_cplb_error(int cplb_panic, struct pt_regs *fp)
{
	switch (cplb_panic) {
	case CPLB_NO_UNLOCKED:
		printk(KERN_EMERG "All CPLBs are locked\n");
		break;
	case CPLB_PROT_VIOL:
		return;
	case CPLB_NO_ADDR_MATCH:
		return;
	case CPLB_UNKNOWN_ERR:
		printk(KERN_EMERG "Unknown CPLB Exception\n");
		break;
	}

	oops_in_progress = 1;

	dump_bfin_process(fp);
	dump_bfin_mem(fp);
	show_regs(fp);
	dump_stack();
	panic("Unrecoverable event");
}

#ifdef CONFIG_BUG
int is_valid_bugaddr(unsigned long addr)
{
	unsigned int opcode;

	if (!get_instruction(&opcode, (unsigned short *)addr))
		return 0;

	return opcode == BFIN_BUG_OPCODE;
}
#endif

/* stub this out */
#ifndef CONFIG_DEBUG_VERBOSE
void show_regs(struct pt_regs *fp)
{

}
#endif
