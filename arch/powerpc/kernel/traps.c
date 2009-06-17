/*
 *  Copyright (C) 1995-1996  Gary Thomas (gdt@linuxppc.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *  and Paul Mackerras (paulus@samba.org)
 */

/*
 * This file handles the architecture-dependent parts of hardware exceptions
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/prctl.h>
#include <linux/delay.h>
#include <linux/kprobes.h>
#include <linux/kexec.h>
#include <linux/backlight.h>
#include <linux/bug.h>
#include <linux/kdebug.h>
#include <linux/debugfs.h>

#include <asm/emulated_ops.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/rtas.h>
#include <asm/pmc.h>
#ifdef CONFIG_PPC32
#include <asm/reg.h>
#endif
#ifdef CONFIG_PMAC_BACKLIGHT
#include <asm/backlight.h>
#endif
#ifdef CONFIG_PPC64
#include <asm/firmware.h>
#include <asm/processor.h>
#endif
#include <asm/kexec.h>
#include <asm/ppc-opcode.h>
#ifdef CONFIG_FSL_BOOKE
#include <asm/dbell.h>
#endif

#if defined(CONFIG_DEBUGGER) || defined(CONFIG_KEXEC)
int (*__debugger)(struct pt_regs *regs);
int (*__debugger_ipi)(struct pt_regs *regs);
int (*__debugger_bpt)(struct pt_regs *regs);
int (*__debugger_sstep)(struct pt_regs *regs);
int (*__debugger_iabr_match)(struct pt_regs *regs);
int (*__debugger_dabr_match)(struct pt_regs *regs);
int (*__debugger_fault_handler)(struct pt_regs *regs);

EXPORT_SYMBOL(__debugger);
EXPORT_SYMBOL(__debugger_ipi);
EXPORT_SYMBOL(__debugger_bpt);
EXPORT_SYMBOL(__debugger_sstep);
EXPORT_SYMBOL(__debugger_iabr_match);
EXPORT_SYMBOL(__debugger_dabr_match);
EXPORT_SYMBOL(__debugger_fault_handler);
#endif

/*
 * Trap & Exception support
 */

#ifdef CONFIG_PMAC_BACKLIGHT
static void pmac_backlight_unblank(void)
{
	mutex_lock(&pmac_backlight_mutex);
	if (pmac_backlight) {
		struct backlight_properties *props;

		props = &pmac_backlight->props;
		props->brightness = props->max_brightness;
		props->power = FB_BLANK_UNBLANK;
		backlight_update_status(pmac_backlight);
	}
	mutex_unlock(&pmac_backlight_mutex);
}
#else
static inline void pmac_backlight_unblank(void) { }
#endif

int die(const char *str, struct pt_regs *regs, long err)
{
	static struct {
		spinlock_t lock;
		u32 lock_owner;
		int lock_owner_depth;
	} die = {
		.lock =			__SPIN_LOCK_UNLOCKED(die.lock),
		.lock_owner =		-1,
		.lock_owner_depth =	0
	};
	static int die_counter;
	unsigned long flags;

	if (debugger(regs))
		return 1;

	oops_enter();

	if (die.lock_owner != raw_smp_processor_id()) {
		console_verbose();
		spin_lock_irqsave(&die.lock, flags);
		die.lock_owner = smp_processor_id();
		die.lock_owner_depth = 0;
		bust_spinlocks(1);
		if (machine_is(powermac))
			pmac_backlight_unblank();
	} else {
		local_save_flags(flags);
	}

	if (++die.lock_owner_depth < 3) {
		printk("Oops: %s, sig: %ld [#%d]\n", str, err, ++die_counter);
#ifdef CONFIG_PREEMPT
		printk("PREEMPT ");
#endif
#ifdef CONFIG_SMP
		printk("SMP NR_CPUS=%d ", NR_CPUS);
#endif
#ifdef CONFIG_DEBUG_PAGEALLOC
		printk("DEBUG_PAGEALLOC ");
#endif
#ifdef CONFIG_NUMA
		printk("NUMA ");
#endif
		printk("%s\n", ppc_md.name ? ppc_md.name : "");

		print_modules();
		show_regs(regs);
	} else {
		printk("Recursive die() failure, output suppressed\n");
	}

	bust_spinlocks(0);
	die.lock_owner = -1;
	add_taint(TAINT_DIE);
	spin_unlock_irqrestore(&die.lock, flags);

	if (kexec_should_crash(current) ||
		kexec_sr_activated(smp_processor_id()))
		crash_kexec(regs);
	crash_kexec_secondary(regs);

	if (in_interrupt())
		panic("Fatal exception in interrupt");

	if (panic_on_oops)
		panic("Fatal exception");

	oops_exit();
	do_exit(err);

	return 0;
}

void _exception(int signr, struct pt_regs *regs, int code, unsigned long addr)
{
	siginfo_t info;
	const char fmt32[] = KERN_INFO "%s[%d]: unhandled signal %d " \
			"at %08lx nip %08lx lr %08lx code %x\n";
	const char fmt64[] = KERN_INFO "%s[%d]: unhandled signal %d " \
			"at %016lx nip %016lx lr %016lx code %x\n";

	if (!user_mode(regs)) {
		if (die("Exception in kernel mode", regs, signr))
			return;
	} else if (show_unhandled_signals &&
		    unhandled_signal(current, signr) &&
		    printk_ratelimit()) {
			printk(regs->msr & MSR_SF ? fmt64 : fmt32,
				current->comm, current->pid, signr,
				addr, regs->nip, regs->link, code);
		}

	memset(&info, 0, sizeof(info));
	info.si_signo = signr;
	info.si_code = code;
	info.si_addr = (void __user *) addr;
	force_sig_info(signr, &info, current);

	/*
	 * Init gets no signals that it doesn't have a handler for.
	 * That's all very well, but if it has caused a synchronous
	 * exception and we ignore the resulting signal, it will just
	 * generate the same exception over and over again and we get
	 * nowhere.  Better to kill it and let the kernel panic.
	 */
	if (is_global_init(current)) {
		__sighandler_t handler;

		spin_lock_irq(&current->sighand->siglock);
		handler = current->sighand->action[signr-1].sa.sa_handler;
		spin_unlock_irq(&current->sighand->siglock);
		if (handler == SIG_DFL) {
			/* init has generated a synchronous exception
			   and it doesn't have a handler for the signal */
			printk(KERN_CRIT "init has generated signal %d "
			       "but has no handler for it\n", signr);
			do_exit(signr);
		}
	}
}

#ifdef CONFIG_PPC64
void system_reset_exception(struct pt_regs *regs)
{
	/* See if any machine dependent calls */
	if (ppc_md.system_reset_exception) {
		if (ppc_md.system_reset_exception(regs))
			return;
	}

#ifdef CONFIG_KEXEC
	cpu_set(smp_processor_id(), cpus_in_sr);
#endif

	die("System Reset", regs, SIGABRT);

	/*
	 * Some CPUs when released from the debugger will execute this path.
	 * These CPUs entered the debugger via a soft-reset. If the CPU was
	 * hung before entering the debugger it will return to the hung
	 * state when exiting this function.  This causes a problem in
	 * kdump since the hung CPU(s) will not respond to the IPI sent
	 * from kdump. To prevent the problem we call crash_kexec_secondary()
	 * here. If a kdump had not been initiated or we exit the debugger
	 * with the "exit and recover" command (x) crash_kexec_secondary()
	 * will return after 5ms and the CPU returns to its previous state.
	 */
	crash_kexec_secondary(regs);

	/* Must die if the interrupt is not recoverable */
	if (!(regs->msr & MSR_RI))
		panic("Unrecoverable System Reset");

	/* What should we do here? We could issue a shutdown or hard reset. */
}
#endif

/*
 * I/O accesses can cause machine checks on powermacs.
 * Check if the NIP corresponds to the address of a sync
 * instruction for which there is an entry in the exception
 * table.
 * Note that the 601 only takes a machine check on TEA
 * (transfer error ack) signal assertion, and does not
 * set any of the top 16 bits of SRR1.
 *  -- paulus.
 */
static inline int check_io_access(struct pt_regs *regs)
{
#ifdef CONFIG_PPC32
	unsigned long msr = regs->msr;
	const struct exception_table_entry *entry;
	unsigned int *nip = (unsigned int *)regs->nip;

	if (((msr & 0xffff0000) == 0 || (msr & (0x80000 | 0x40000)))
	    && (entry = search_exception_tables(regs->nip)) != NULL) {
		/*
		 * Check that it's a sync instruction, or somewhere
		 * in the twi; isync; nop sequence that inb/inw/inl uses.
		 * As the address is in the exception table
		 * we should be able to read the instr there.
		 * For the debug message, we look at the preceding
		 * load or store.
		 */
		if (*nip == 0x60000000)		/* nop */
			nip -= 2;
		else if (*nip == 0x4c00012c)	/* isync */
			--nip;
		if (*nip == 0x7c0004ac || (*nip >> 26) == 3) {
			/* sync or twi */
			unsigned int rb;

			--nip;
			rb = (*nip >> 11) & 0x1f;
			printk(KERN_DEBUG "%s bad port %lx at %p\n",
			       (*nip & 0x100)? "OUT to": "IN from",
			       regs->gpr[rb] - _IO_BASE, nip);
			regs->msr |= MSR_RI;
			regs->nip = entry->fixup;
			return 1;
		}
	}
#endif /* CONFIG_PPC32 */
	return 0;
}

#if defined(CONFIG_4xx) || defined(CONFIG_BOOKE)
/* On 4xx, the reason for the machine check or program exception
   is in the ESR. */
#define get_reason(regs)	((regs)->dsisr)
#ifndef CONFIG_FSL_BOOKE
#define get_mc_reason(regs)	((regs)->dsisr)
#else
#define get_mc_reason(regs)	(mfspr(SPRN_MCSR) & MCSR_MASK)
#endif
#define REASON_FP		ESR_FP
#define REASON_ILLEGAL		(ESR_PIL | ESR_PUO)
#define REASON_PRIVILEGED	ESR_PPR
#define REASON_TRAP		ESR_PTR

/* single-step stuff */
#define single_stepping(regs)	(current->thread.dbcr0 & DBCR0_IC)
#define clear_single_step(regs)	(current->thread.dbcr0 &= ~DBCR0_IC)

#else
/* On non-4xx, the reason for the machine check or program
   exception is in the MSR. */
#define get_reason(regs)	((regs)->msr)
#define get_mc_reason(regs)	((regs)->msr)
#define REASON_FP		0x100000
#define REASON_ILLEGAL		0x80000
#define REASON_PRIVILEGED	0x40000
#define REASON_TRAP		0x20000

#define single_stepping(regs)	((regs)->msr & MSR_SE)
#define clear_single_step(regs)	((regs)->msr &= ~MSR_SE)
#endif

#if defined(CONFIG_4xx)
int machine_check_4xx(struct pt_regs *regs)
{
	unsigned long reason = get_mc_reason(regs);

	if (reason & ESR_IMCP) {
		printk("Instruction");
		mtspr(SPRN_ESR, reason & ~ESR_IMCP);
	} else
		printk("Data");
	printk(" machine check in kernel mode.\n");

	return 0;
}

int machine_check_440A(struct pt_regs *regs)
{
	unsigned long reason = get_mc_reason(regs);

	printk("Machine check in kernel mode.\n");
	if (reason & ESR_IMCP){
		printk("Instruction Synchronous Machine Check exception\n");
		mtspr(SPRN_ESR, reason & ~ESR_IMCP);
	}
	else {
		u32 mcsr = mfspr(SPRN_MCSR);
		if (mcsr & MCSR_IB)
			printk("Instruction Read PLB Error\n");
		if (mcsr & MCSR_DRB)
			printk("Data Read PLB Error\n");
		if (mcsr & MCSR_DWB)
			printk("Data Write PLB Error\n");
		if (mcsr & MCSR_TLBP)
			printk("TLB Parity Error\n");
		if (mcsr & MCSR_ICP){
			flush_instruction_cache();
			printk("I-Cache Parity Error\n");
		}
		if (mcsr & MCSR_DCSP)
			printk("D-Cache Search Parity Error\n");
		if (mcsr & MCSR_DCFP)
			printk("D-Cache Flush Parity Error\n");
		if (mcsr & MCSR_IMPE)
			printk("Machine Check exception is imprecise\n");

		/* Clear MCSR */
		mtspr(SPRN_MCSR, mcsr);
	}
	return 0;
}
#elif defined(CONFIG_E500)
int machine_check_e500(struct pt_regs *regs)
{
	unsigned long reason = get_mc_reason(regs);

	printk("Machine check in kernel mode.\n");
	printk("Caused by (from MCSR=%lx): ", reason);

	if (reason & MCSR_MCP)
		printk("Machine Check Signal\n");
	if (reason & MCSR_ICPERR)
		printk("Instruction Cache Parity Error\n");
	if (reason & MCSR_DCP_PERR)
		printk("Data Cache Push Parity Error\n");
	if (reason & MCSR_DCPERR)
		printk("Data Cache Parity Error\n");
	if (reason & MCSR_BUS_IAERR)
		printk("Bus - Instruction Address Error\n");
	if (reason & MCSR_BUS_RAERR)
		printk("Bus - Read Address Error\n");
	if (reason & MCSR_BUS_WAERR)
		printk("Bus - Write Address Error\n");
	if (reason & MCSR_BUS_IBERR)
		printk("Bus - Instruction Data Error\n");
	if (reason & MCSR_BUS_RBERR)
		printk("Bus - Read Data Bus Error\n");
	if (reason & MCSR_BUS_WBERR)
		printk("Bus - Read Data Bus Error\n");
	if (reason & MCSR_BUS_IPERR)
		printk("Bus - Instruction Parity Error\n");
	if (reason & MCSR_BUS_RPERR)
		printk("Bus - Read Parity Error\n");

	return 0;
}
#elif defined(CONFIG_E200)
int machine_check_e200(struct pt_regs *regs)
{
	unsigned long reason = get_mc_reason(regs);

	printk("Machine check in kernel mode.\n");
	printk("Caused by (from MCSR=%lx): ", reason);

	if (reason & MCSR_MCP)
		printk("Machine Check Signal\n");
	if (reason & MCSR_CP_PERR)
		printk("Cache Push Parity Error\n");
	if (reason & MCSR_CPERR)
		printk("Cache Parity Error\n");
	if (reason & MCSR_EXCP_ERR)
		printk("ISI, ITLB, or Bus Error on first instruction fetch for an exception handler\n");
	if (reason & MCSR_BUS_IRERR)
		printk("Bus - Read Bus Error on instruction fetch\n");
	if (reason & MCSR_BUS_DRERR)
		printk("Bus - Read Bus Error on data load\n");
	if (reason & MCSR_BUS_WRERR)
		printk("Bus - Write Bus Error on buffered store or cache line push\n");

	return 0;
}
#else
int machine_check_generic(struct pt_regs *regs)
{
	unsigned long reason = get_mc_reason(regs);

	printk("Machine check in kernel mode.\n");
	printk("Caused by (from SRR1=%lx): ", reason);
	switch (reason & 0x601F0000) {
	case 0x80000:
		printk("Machine check signal\n");
		break;
	case 0:		/* for 601 */
	case 0x40000:
	case 0x140000:	/* 7450 MSS error and TEA */
		printk("Transfer error ack signal\n");
		break;
	case 0x20000:
		printk("Data parity error signal\n");
		break;
	case 0x10000:
		printk("Address parity error signal\n");
		break;
	case 0x20000000:
		printk("L1 Data Cache error\n");
		break;
	case 0x40000000:
		printk("L1 Instruction Cache error\n");
		break;
	case 0x00100000:
		printk("L2 data cache parity error\n");
		break;
	default:
		printk("Unknown values in msr\n");
	}
	return 0;
}
#endif /* everything else */

void machine_check_exception(struct pt_regs *regs)
{
	int recover = 0;

	/* See if any machine dependent calls. In theory, we would want
	 * to call the CPU first, and call the ppc_md. one if the CPU
	 * one returns a positive number. However there is existing code
	 * that assumes the board gets a first chance, so let's keep it
	 * that way for now and fix things later. --BenH.
	 */
	if (ppc_md.machine_check_exception)
		recover = ppc_md.machine_check_exception(regs);
	else if (cur_cpu_spec->machine_check)
		recover = cur_cpu_spec->machine_check(regs);

	if (recover > 0)
		return;

	if (user_mode(regs)) {
		regs->msr |= MSR_RI;
		_exception(SIGBUS, regs, BUS_ADRERR, regs->nip);
		return;
	}

#if defined(CONFIG_8xx) && defined(CONFIG_PCI)
	/* the qspan pci read routines can cause machine checks -- Cort
	 *
	 * yuck !!! that totally needs to go away ! There are better ways
	 * to deal with that than having a wart in the mcheck handler.
	 * -- BenH
	 */
	bad_page_fault(regs, regs->dar, SIGBUS);
	return;
#endif

	if (debugger_fault_handler(regs)) {
		regs->msr |= MSR_RI;
		return;
	}

	if (check_io_access(regs))
		return;

	if (debugger_fault_handler(regs))
		return;
	die("Machine check", regs, SIGBUS);

	/* Must die if the interrupt is not recoverable */
	if (!(regs->msr & MSR_RI))
		panic("Unrecoverable Machine check");
}

void SMIException(struct pt_regs *regs)
{
	die("System Management Interrupt", regs, SIGABRT);
}

void unknown_exception(struct pt_regs *regs)
{
	printk("Bad trap at PC: %lx, SR: %lx, vector=%lx\n",
	       regs->nip, regs->msr, regs->trap);

	_exception(SIGTRAP, regs, 0, 0);
}

void instruction_breakpoint_exception(struct pt_regs *regs)
{
	if (notify_die(DIE_IABR_MATCH, "iabr_match", regs, 5,
					5, SIGTRAP) == NOTIFY_STOP)
		return;
	if (debugger_iabr_match(regs))
		return;
	_exception(SIGTRAP, regs, TRAP_BRKPT, regs->nip);
}

void RunModeException(struct pt_regs *regs)
{
	_exception(SIGTRAP, regs, 0, 0);
}

void __kprobes single_step_exception(struct pt_regs *regs)
{
	regs->msr &= ~(MSR_SE | MSR_BE);  /* Turn off 'trace' bits */

	if (notify_die(DIE_SSTEP, "single_step", regs, 5,
					5, SIGTRAP) == NOTIFY_STOP)
		return;
	if (debugger_sstep(regs))
		return;

	_exception(SIGTRAP, regs, TRAP_TRACE, regs->nip);
}

/*
 * After we have successfully emulated an instruction, we have to
 * check if the instruction was being single-stepped, and if so,
 * pretend we got a single-step exception.  This was pointed out
 * by Kumar Gala.  -- paulus
 */
static void emulate_single_step(struct pt_regs *regs)
{
	if (single_stepping(regs)) {
		clear_single_step(regs);
		_exception(SIGTRAP, regs, TRAP_TRACE, 0);
	}
}

static inline int __parse_fpscr(unsigned long fpscr)
{
	int ret = 0;

	/* Invalid operation */
	if ((fpscr & FPSCR_VE) && (fpscr & FPSCR_VX))
		ret = FPE_FLTINV;

	/* Overflow */
	else if ((fpscr & FPSCR_OE) && (fpscr & FPSCR_OX))
		ret = FPE_FLTOVF;

	/* Underflow */
	else if ((fpscr & FPSCR_UE) && (fpscr & FPSCR_UX))
		ret = FPE_FLTUND;

	/* Divide by zero */
	else if ((fpscr & FPSCR_ZE) && (fpscr & FPSCR_ZX))
		ret = FPE_FLTDIV;

	/* Inexact result */
	else if ((fpscr & FPSCR_XE) && (fpscr & FPSCR_XX))
		ret = FPE_FLTRES;

	return ret;
}

static void parse_fpe(struct pt_regs *regs)
{
	int code = 0;

	flush_fp_to_thread(current);

	code = __parse_fpscr(current->thread.fpscr.val);

	_exception(SIGFPE, regs, code, regs->nip);
}

/*
 * Illegal instruction emulation support.  Originally written to
 * provide the PVR to user applications using the mfspr rd, PVR.
 * Return non-zero if we can't emulate, or -EFAULT if the associated
 * memory access caused an access fault.  Return zero on success.
 *
 * There are a couple of ways to do this, either "decode" the instruction
 * or directly match lots of bits.  In this case, matching lots of
 * bits is faster and easier.
 *
 */
static int emulate_string_inst(struct pt_regs *regs, u32 instword)
{
	u8 rT = (instword >> 21) & 0x1f;
	u8 rA = (instword >> 16) & 0x1f;
	u8 NB_RB = (instword >> 11) & 0x1f;
	u32 num_bytes;
	unsigned long EA;
	int pos = 0;

	/* Early out if we are an invalid form of lswx */
	if ((instword & PPC_INST_STRING_MASK) == PPC_INST_LSWX)
		if ((rT == rA) || (rT == NB_RB))
			return -EINVAL;

	EA = (rA == 0) ? 0 : regs->gpr[rA];

	switch (instword & PPC_INST_STRING_MASK) {
		case PPC_INST_LSWX:
		case PPC_INST_STSWX:
			EA += NB_RB;
			num_bytes = regs->xer & 0x7f;
			break;
		case PPC_INST_LSWI:
		case PPC_INST_STSWI:
			num_bytes = (NB_RB == 0) ? 32 : NB_RB;
			break;
		default:
			return -EINVAL;
	}

	while (num_bytes != 0)
	{
		u8 val;
		u32 shift = 8 * (3 - (pos & 0x3));

		switch ((instword & PPC_INST_STRING_MASK)) {
			case PPC_INST_LSWX:
			case PPC_INST_LSWI:
				if (get_user(val, (u8 __user *)EA))
					return -EFAULT;
				/* first time updating this reg,
				 * zero it out */
				if (pos == 0)
					regs->gpr[rT] = 0;
				regs->gpr[rT] |= val << shift;
				break;
			case PPC_INST_STSWI:
			case PPC_INST_STSWX:
				val = regs->gpr[rT] >> shift;
				if (put_user(val, (u8 __user *)EA))
					return -EFAULT;
				break;
		}
		/* move EA to next address */
		EA += 1;
		num_bytes--;

		/* manage our position within the register */
		if (++pos == 4) {
			pos = 0;
			if (++rT == 32)
				rT = 0;
		}
	}

	return 0;
}

static int emulate_popcntb_inst(struct pt_regs *regs, u32 instword)
{
	u32 ra,rs;
	unsigned long tmp;

	ra = (instword >> 16) & 0x1f;
	rs = (instword >> 21) & 0x1f;

	tmp = regs->gpr[rs];
	tmp = tmp - ((tmp >> 1) & 0x5555555555555555ULL);
	tmp = (tmp & 0x3333333333333333ULL) + ((tmp >> 2) & 0x3333333333333333ULL);
	tmp = (tmp + (tmp >> 4)) & 0x0f0f0f0f0f0f0f0fULL;
	regs->gpr[ra] = tmp;

	return 0;
}

static int emulate_isel(struct pt_regs *regs, u32 instword)
{
	u8 rT = (instword >> 21) & 0x1f;
	u8 rA = (instword >> 16) & 0x1f;
	u8 rB = (instword >> 11) & 0x1f;
	u8 BC = (instword >> 6) & 0x1f;
	u8 bit;
	unsigned long tmp;

	tmp = (rA == 0) ? 0 : regs->gpr[rA];
	bit = (regs->ccr >> (31 - BC)) & 0x1;

	regs->gpr[rT] = bit ? tmp : regs->gpr[rB];

	return 0;
}

static int emulate_instruction(struct pt_regs *regs)
{
	u32 instword;
	u32 rd;

	if (!user_mode(regs) || (regs->msr & MSR_LE))
		return -EINVAL;
	CHECK_FULL_REGS(regs);

	if (get_user(instword, (u32 __user *)(regs->nip)))
		return -EFAULT;

	/* Emulate the mfspr rD, PVR. */
	if ((instword & PPC_INST_MFSPR_PVR_MASK) == PPC_INST_MFSPR_PVR) {
		PPC_WARN_EMULATED(mfpvr);
		rd = (instword >> 21) & 0x1f;
		regs->gpr[rd] = mfspr(SPRN_PVR);
		return 0;
	}

	/* Emulating the dcba insn is just a no-op.  */
	if ((instword & PPC_INST_DCBA_MASK) == PPC_INST_DCBA) {
		PPC_WARN_EMULATED(dcba);
		return 0;
	}

	/* Emulate the mcrxr insn.  */
	if ((instword & PPC_INST_MCRXR_MASK) == PPC_INST_MCRXR) {
		int shift = (instword >> 21) & 0x1c;
		unsigned long msk = 0xf0000000UL >> shift;

		PPC_WARN_EMULATED(mcrxr);
		regs->ccr = (regs->ccr & ~msk) | ((regs->xer >> shift) & msk);
		regs->xer &= ~0xf0000000UL;
		return 0;
	}

	/* Emulate load/store string insn. */
	if ((instword & PPC_INST_STRING_GEN_MASK) == PPC_INST_STRING) {
		PPC_WARN_EMULATED(string);
		return emulate_string_inst(regs, instword);
	}

	/* Emulate the popcntb (Population Count Bytes) instruction. */
	if ((instword & PPC_INST_POPCNTB_MASK) == PPC_INST_POPCNTB) {
		PPC_WARN_EMULATED(popcntb);
		return emulate_popcntb_inst(regs, instword);
	}

	/* Emulate isel (Integer Select) instruction */
	if ((instword & PPC_INST_ISEL_MASK) == PPC_INST_ISEL) {
		PPC_WARN_EMULATED(isel);
		return emulate_isel(regs, instword);
	}

	return -EINVAL;
}

int is_valid_bugaddr(unsigned long addr)
{
	return is_kernel_addr(addr);
}

void __kprobes program_check_exception(struct pt_regs *regs)
{
	unsigned int reason = get_reason(regs);
	extern int do_mathemu(struct pt_regs *regs);

	/* We can now get here via a FP Unavailable exception if the core
	 * has no FPU, in that case the reason flags will be 0 */

	if (reason & REASON_FP) {
		/* IEEE FP exception */
		parse_fpe(regs);
		return;
	}
	if (reason & REASON_TRAP) {
		/* trap exception */
		if (notify_die(DIE_BPT, "breakpoint", regs, 5, 5, SIGTRAP)
				== NOTIFY_STOP)
			return;
		if (debugger_bpt(regs))
			return;

		if (!(regs->msr & MSR_PR) &&  /* not user-mode */
		    report_bug(regs->nip, regs) == BUG_TRAP_TYPE_WARN) {
			regs->nip += 4;
			return;
		}
		_exception(SIGTRAP, regs, TRAP_BRKPT, regs->nip);
		return;
	}

	local_irq_enable();

#ifdef CONFIG_MATH_EMULATION
	/* (reason & REASON_ILLEGAL) would be the obvious thing here,
	 * but there seems to be a hardware bug on the 405GP (RevD)
	 * that means ESR is sometimes set incorrectly - either to
	 * ESR_DST (!?) or 0.  In the process of chasing this with the
	 * hardware people - not sure if it can happen on any illegal
	 * instruction or only on FP instructions, whether there is a
	 * pattern to occurences etc. -dgibson 31/Mar/2003 */
	switch (do_mathemu(regs)) {
	case 0:
		emulate_single_step(regs);
		return;
	case 1: {
			int code = 0;
			code = __parse_fpscr(current->thread.fpscr.val);
			_exception(SIGFPE, regs, code, regs->nip);
			return;
		}
	case -EFAULT:
		_exception(SIGSEGV, regs, SEGV_MAPERR, regs->nip);
		return;
	}
	/* fall through on any other errors */
#endif /* CONFIG_MATH_EMULATION */

	/* Try to emulate it if we should. */
	if (reason & (REASON_ILLEGAL | REASON_PRIVILEGED)) {
		switch (emulate_instruction(regs)) {
		case 0:
			regs->nip += 4;
			emulate_single_step(regs);
			return;
		case -EFAULT:
			_exception(SIGSEGV, regs, SEGV_MAPERR, regs->nip);
			return;
		}
	}

	if (reason & REASON_PRIVILEGED)
		_exception(SIGILL, regs, ILL_PRVOPC, regs->nip);
	else
		_exception(SIGILL, regs, ILL_ILLOPC, regs->nip);
}

void alignment_exception(struct pt_regs *regs)
{
	int sig, code, fixed = 0;

	/* we don't implement logging of alignment exceptions */
	if (!(current->thread.align_ctl & PR_UNALIGN_SIGBUS))
		fixed = fix_alignment(regs);

	if (fixed == 1) {
		regs->nip += 4;	/* skip over emulated instruction */
		emulate_single_step(regs);
		return;
	}

	/* Operand address was bad */
	if (fixed == -EFAULT) {
		sig = SIGSEGV;
		code = SEGV_ACCERR;
	} else {
		sig = SIGBUS;
		code = BUS_ADRALN;
	}
	if (user_mode(regs))
		_exception(sig, regs, code, regs->dar);
	else
		bad_page_fault(regs, regs->dar, sig);
}

void StackOverflow(struct pt_regs *regs)
{
	printk(KERN_CRIT "Kernel stack overflow in process %p, r1=%lx\n",
	       current, regs->gpr[1]);
	debugger(regs);
	show_regs(regs);
	panic("kernel stack overflow");
}

void nonrecoverable_exception(struct pt_regs *regs)
{
	printk(KERN_ERR "Non-recoverable exception at PC=%lx MSR=%lx\n",
	       regs->nip, regs->msr);
	debugger(regs);
	die("nonrecoverable exception", regs, SIGKILL);
}

void trace_syscall(struct pt_regs *regs)
{
	printk("Task: %p(%d), PC: %08lX/%08lX, Syscall: %3ld, Result: %s%ld    %s\n",
	       current, task_pid_nr(current), regs->nip, regs->link, regs->gpr[0],
	       regs->ccr&0x10000000?"Error=":"", regs->gpr[3], print_tainted());
}

void kernel_fp_unavailable_exception(struct pt_regs *regs)
{
	printk(KERN_EMERG "Unrecoverable FP Unavailable Exception "
			  "%lx at %lx\n", regs->trap, regs->nip);
	die("Unrecoverable FP Unavailable Exception", regs, SIGABRT);
}

void altivec_unavailable_exception(struct pt_regs *regs)
{
	if (user_mode(regs)) {
		/* A user program has executed an altivec instruction,
		   but this kernel doesn't support altivec. */
		_exception(SIGILL, regs, ILL_ILLOPC, regs->nip);
		return;
	}

	printk(KERN_EMERG "Unrecoverable VMX/Altivec Unavailable Exception "
			"%lx at %lx\n", regs->trap, regs->nip);
	die("Unrecoverable VMX/Altivec Unavailable Exception", regs, SIGABRT);
}

void vsx_unavailable_exception(struct pt_regs *regs)
{
	if (user_mode(regs)) {
		/* A user program has executed an vsx instruction,
		   but this kernel doesn't support vsx. */
		_exception(SIGILL, regs, ILL_ILLOPC, regs->nip);
		return;
	}

	printk(KERN_EMERG "Unrecoverable VSX Unavailable Exception "
			"%lx at %lx\n", regs->trap, regs->nip);
	die("Unrecoverable VSX Unavailable Exception", regs, SIGABRT);
}

void performance_monitor_exception(struct pt_regs *regs)
{
	perf_irq(regs);
}

#ifdef CONFIG_8xx
void SoftwareEmulation(struct pt_regs *regs)
{
	extern int do_mathemu(struct pt_regs *);
	extern int Soft_emulate_8xx(struct pt_regs *);
#if defined(CONFIG_MATH_EMULATION) || defined(CONFIG_8XX_MINIMAL_FPEMU)
	int errcode;
#endif

	CHECK_FULL_REGS(regs);

	if (!user_mode(regs)) {
		debugger(regs);
		die("Kernel Mode Software FPU Emulation", regs, SIGFPE);
	}

#ifdef CONFIG_MATH_EMULATION
	errcode = do_mathemu(regs);
	if (errcode >= 0)
		PPC_WARN_EMULATED(math);

	switch (errcode) {
	case 0:
		emulate_single_step(regs);
		return;
	case 1: {
			int code = 0;
			code = __parse_fpscr(current->thread.fpscr.val);
			_exception(SIGFPE, regs, code, regs->nip);
			return;
		}
	case -EFAULT:
		_exception(SIGSEGV, regs, SEGV_MAPERR, regs->nip);
		return;
	default:
		_exception(SIGILL, regs, ILL_ILLOPC, regs->nip);
		return;
	}

#elif defined(CONFIG_8XX_MINIMAL_FPEMU)
	errcode = Soft_emulate_8xx(regs);
	if (errcode >= 0)
		PPC_WARN_EMULATED(8xx);

	switch (errcode) {
	case 0:
		emulate_single_step(regs);
		return;
	case 1:
		_exception(SIGILL, regs, ILL_ILLOPC, regs->nip);
		return;
	case -EFAULT:
		_exception(SIGSEGV, regs, SEGV_MAPERR, regs->nip);
		return;
	}
#else
	_exception(SIGILL, regs, ILL_ILLOPC, regs->nip);
#endif
}
#endif /* CONFIG_8xx */

#if defined(CONFIG_40x) || defined(CONFIG_BOOKE)

void __kprobes DebugException(struct pt_regs *regs, unsigned long debug_status)
{
	/* Hack alert: On BookE, Branch Taken stops on the branch itself, while
	 * on server, it stops on the target of the branch. In order to simulate
	 * the server behaviour, we thus restart right away with a single step
	 * instead of stopping here when hitting a BT
	 */
	if (debug_status & DBSR_BT) {
		regs->msr &= ~MSR_DE;

		/* Disable BT */
		mtspr(SPRN_DBCR0, mfspr(SPRN_DBCR0) & ~DBCR0_BT);
		/* Clear the BT event */
		mtspr(SPRN_DBSR, DBSR_BT);

		/* Do the single step trick only when coming from userspace */
		if (user_mode(regs)) {
			current->thread.dbcr0 &= ~DBCR0_BT;
			current->thread.dbcr0 |= DBCR0_IDM | DBCR0_IC;
			regs->msr |= MSR_DE;
			return;
		}

		if (notify_die(DIE_SSTEP, "block_step", regs, 5,
			       5, SIGTRAP) == NOTIFY_STOP) {
			return;
		}
		if (debugger_sstep(regs))
			return;
	} else if (debug_status & DBSR_IC) { 	/* Instruction complete */
		regs->msr &= ~MSR_DE;

		/* Disable instruction completion */
		mtspr(SPRN_DBCR0, mfspr(SPRN_DBCR0) & ~DBCR0_IC);
		/* Clear the instruction completion event */
		mtspr(SPRN_DBSR, DBSR_IC);

		if (notify_die(DIE_SSTEP, "single_step", regs, 5,
			       5, SIGTRAP) == NOTIFY_STOP) {
			return;
		}

		if (debugger_sstep(regs))
			return;

		if (user_mode(regs))
			current->thread.dbcr0 &= ~(DBCR0_IC);

		_exception(SIGTRAP, regs, TRAP_TRACE, regs->nip);
	} else if (debug_status & (DBSR_DAC1R | DBSR_DAC1W)) {
		regs->msr &= ~MSR_DE;

		if (user_mode(regs)) {
			current->thread.dbcr0 &= ~(DBSR_DAC1R | DBSR_DAC1W |
								DBCR0_IDM);
		} else {
			/* Disable DAC interupts */
			mtspr(SPRN_DBCR0, mfspr(SPRN_DBCR0) & ~(DBSR_DAC1R |
						DBSR_DAC1W | DBCR0_IDM));

			/* Clear the DAC event */
			mtspr(SPRN_DBSR, (DBSR_DAC1R | DBSR_DAC1W));
		}
		/* Setup and send the trap to the handler */
		do_dabr(regs, mfspr(SPRN_DAC1), debug_status);
	}
}
#endif /* CONFIG_4xx || CONFIG_BOOKE */

#if !defined(CONFIG_TAU_INT)
void TAUException(struct pt_regs *regs)
{
	printk("TAU trap at PC: %lx, MSR: %lx, vector=%lx    %s\n",
	       regs->nip, regs->msr, regs->trap, print_tainted());
}
#endif /* CONFIG_INT_TAU */

#ifdef CONFIG_ALTIVEC
void altivec_assist_exception(struct pt_regs *regs)
{
	int err;

	if (!user_mode(regs)) {
		printk(KERN_EMERG "VMX/Altivec assist exception in kernel mode"
		       " at %lx\n", regs->nip);
		die("Kernel VMX/Altivec assist exception", regs, SIGILL);
	}

	flush_altivec_to_thread(current);

	PPC_WARN_EMULATED(altivec);
	err = emulate_altivec(regs);
	if (err == 0) {
		regs->nip += 4;		/* skip emulated instruction */
		emulate_single_step(regs);
		return;
	}

	if (err == -EFAULT) {
		/* got an error reading the instruction */
		_exception(SIGSEGV, regs, SEGV_ACCERR, regs->nip);
	} else {
		/* didn't recognize the instruction */
		/* XXX quick hack for now: set the non-Java bit in the VSCR */
		if (printk_ratelimit())
			printk(KERN_ERR "Unrecognized altivec instruction "
			       "in %s at %lx\n", current->comm, regs->nip);
		current->thread.vscr.u[3] |= 0x10000;
	}
}
#endif /* CONFIG_ALTIVEC */

#ifdef CONFIG_VSX
void vsx_assist_exception(struct pt_regs *regs)
{
	if (!user_mode(regs)) {
		printk(KERN_EMERG "VSX assist exception in kernel mode"
		       " at %lx\n", regs->nip);
		die("Kernel VSX assist exception", regs, SIGILL);
	}

	flush_vsx_to_thread(current);
	printk(KERN_INFO "VSX assist not supported at %lx\n", regs->nip);
	_exception(SIGILL, regs, ILL_ILLOPC, regs->nip);
}
#endif /* CONFIG_VSX */

#ifdef CONFIG_FSL_BOOKE

void doorbell_exception(struct pt_regs *regs)
{
#ifdef CONFIG_SMP
	int cpu = smp_processor_id();
	int msg;

	if (num_online_cpus() < 2)
		return;

	for (msg = 0; msg < 4; msg++)
		if (test_and_clear_bit(msg, &dbell_smp_message[cpu]))
			smp_message_recv(msg);
#else
	printk(KERN_WARNING "Received doorbell on non-smp system\n");
#endif
}

void CacheLockingException(struct pt_regs *regs, unsigned long address,
			   unsigned long error_code)
{
	/* We treat cache locking instructions from the user
	 * as priv ops, in the future we could try to do
	 * something smarter
	 */
	if (error_code & (ESR_DLK|ESR_ILK))
		_exception(SIGILL, regs, ILL_PRVOPC, regs->nip);
	return;
}
#endif /* CONFIG_FSL_BOOKE */

#ifdef CONFIG_SPE
void SPEFloatingPointException(struct pt_regs *regs)
{
	extern int do_spe_mathemu(struct pt_regs *regs);
	unsigned long spefscr;
	int fpexc_mode;
	int code = 0;
	int err;

	preempt_disable();
	if (regs->msr & MSR_SPE)
		giveup_spe(current);
	preempt_enable();

	spefscr = current->thread.spefscr;
	fpexc_mode = current->thread.fpexc_mode;

	if ((spefscr & SPEFSCR_FOVF) && (fpexc_mode & PR_FP_EXC_OVF)) {
		code = FPE_FLTOVF;
	}
	else if ((spefscr & SPEFSCR_FUNF) && (fpexc_mode & PR_FP_EXC_UND)) {
		code = FPE_FLTUND;
	}
	else if ((spefscr & SPEFSCR_FDBZ) && (fpexc_mode & PR_FP_EXC_DIV))
		code = FPE_FLTDIV;
	else if ((spefscr & SPEFSCR_FINV) && (fpexc_mode & PR_FP_EXC_INV)) {
		code = FPE_FLTINV;
	}
	else if ((spefscr & (SPEFSCR_FG | SPEFSCR_FX)) && (fpexc_mode & PR_FP_EXC_RES))
		code = FPE_FLTRES;

	err = do_spe_mathemu(regs);
	if (err == 0) {
		regs->nip += 4;		/* skip emulated instruction */
		emulate_single_step(regs);
		return;
	}

	if (err == -EFAULT) {
		/* got an error reading the instruction */
		_exception(SIGSEGV, regs, SEGV_ACCERR, regs->nip);
	} else if (err == -EINVAL) {
		/* didn't recognize the instruction */
		printk(KERN_ERR "unrecognized spe instruction "
		       "in %s at %lx\n", current->comm, regs->nip);
	} else {
		_exception(SIGFPE, regs, code, regs->nip);
	}

	return;
}

void SPEFloatingPointRoundException(struct pt_regs *regs)
{
	extern int speround_handler(struct pt_regs *regs);
	int err;

	preempt_disable();
	if (regs->msr & MSR_SPE)
		giveup_spe(current);
	preempt_enable();

	regs->nip -= 4;
	err = speround_handler(regs);
	if (err == 0) {
		regs->nip += 4;		/* skip emulated instruction */
		emulate_single_step(regs);
		return;
	}

	if (err == -EFAULT) {
		/* got an error reading the instruction */
		_exception(SIGSEGV, regs, SEGV_ACCERR, regs->nip);
	} else if (err == -EINVAL) {
		/* didn't recognize the instruction */
		printk(KERN_ERR "unrecognized spe instruction "
		       "in %s at %lx\n", current->comm, regs->nip);
	} else {
		_exception(SIGFPE, regs, 0, regs->nip);
		return;
	}
}
#endif

/*
 * We enter here if we get an unrecoverable exception, that is, one
 * that happened at a point where the RI (recoverable interrupt) bit
 * in the MSR is 0.  This indicates that SRR0/1 are live, and that
 * we therefore lost state by taking this exception.
 */
void unrecoverable_exception(struct pt_regs *regs)
{
	printk(KERN_EMERG "Unrecoverable exception %lx at %lx\n",
	       regs->trap, regs->nip);
	die("Unrecoverable exception", regs, SIGABRT);
}

#ifdef CONFIG_BOOKE_WDT
/*
 * Default handler for a Watchdog exception,
 * spins until a reboot occurs
 */
void __attribute__ ((weak)) WatchdogHandler(struct pt_regs *regs)
{
	/* Generic WatchdogHandler, implement your own */
	mtspr(SPRN_TCR, mfspr(SPRN_TCR)&(~TCR_WIE));
	return;
}

void WatchdogException(struct pt_regs *regs)
{
	printk (KERN_EMERG "PowerPC Book-E Watchdog Exception\n");
	WatchdogHandler(regs);
}
#endif

/*
 * We enter here if we discover during exception entry that we are
 * running in supervisor mode with a userspace value in the stack pointer.
 */
void kernel_bad_stack(struct pt_regs *regs)
{
	printk(KERN_EMERG "Bad kernel stack pointer %lx at %lx\n",
	       regs->gpr[1], regs->nip);
	die("Bad kernel stack pointer", regs, SIGABRT);
}

void __init trap_init(void)
{
}


#ifdef CONFIG_PPC_EMULATED_STATS

#define WARN_EMULATED_SETUP(type)	.type = { .name = #type }

struct ppc_emulated ppc_emulated = {
#ifdef CONFIG_ALTIVEC
	WARN_EMULATED_SETUP(altivec),
#endif
	WARN_EMULATED_SETUP(dcba),
	WARN_EMULATED_SETUP(dcbz),
	WARN_EMULATED_SETUP(fp_pair),
	WARN_EMULATED_SETUP(isel),
	WARN_EMULATED_SETUP(mcrxr),
	WARN_EMULATED_SETUP(mfpvr),
	WARN_EMULATED_SETUP(multiple),
	WARN_EMULATED_SETUP(popcntb),
	WARN_EMULATED_SETUP(spe),
	WARN_EMULATED_SETUP(string),
	WARN_EMULATED_SETUP(unaligned),
#ifdef CONFIG_MATH_EMULATION
	WARN_EMULATED_SETUP(math),
#elif defined(CONFIG_8XX_MINIMAL_FPEMU)
	WARN_EMULATED_SETUP(8xx),
#endif
#ifdef CONFIG_VSX
	WARN_EMULATED_SETUP(vsx),
#endif
};

u32 ppc_warn_emulated;

void ppc_warn_emulated_print(const char *type)
{
	if (printk_ratelimit())
		pr_warning("%s used emulated %s instruction\n", current->comm,
			   type);
}

static int __init ppc_warn_emulated_init(void)
{
	struct dentry *dir, *d;
	unsigned int i;
	struct ppc_emulated_entry *entries = (void *)&ppc_emulated;

	if (!powerpc_debugfs_root)
		return -ENODEV;

	dir = debugfs_create_dir("emulated_instructions",
				 powerpc_debugfs_root);
	if (!dir)
		return -ENOMEM;

	d = debugfs_create_u32("do_warn", S_IRUGO | S_IWUSR, dir,
			       &ppc_warn_emulated);
	if (!d)
		goto fail;

	for (i = 0; i < sizeof(ppc_emulated)/sizeof(*entries); i++) {
		d = debugfs_create_u32(entries[i].name, S_IRUGO | S_IWUSR, dir,
				       (u32 *)&entries[i].val.counter);
		if (!d)
			goto fail;
	}

	return 0;

fail:
	debugfs_remove_recursive(dir);
	return -ENOMEM;
}

device_initcall(ppc_warn_emulated_init);

#endif /* CONFIG_PPC_EMULATED_STATS */
