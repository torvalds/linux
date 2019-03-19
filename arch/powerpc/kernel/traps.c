/*
 *  Copyright (C) 1995-1996  Gary Thomas (gdt@linuxppc.org)
 *  Copyright 2007-2010 Freescale Semiconductor, Inc.
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
#include <linux/sched/debug.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pkeys.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/extable.h>
#include <linux/module.h>	/* print_modules */
#include <linux/prctl.h>
#include <linux/delay.h>
#include <linux/kprobes.h>
#include <linux/kexec.h>
#include <linux/backlight.h>
#include <linux/bug.h>
#include <linux/kdebug.h>
#include <linux/ratelimit.h>
#include <linux/context_tracking.h>
#include <linux/smp.h>
#include <linux/console.h>
#include <linux/kmsg_dump.h>

#include <asm/emulated_ops.h>
#include <asm/pgtable.h>
#include <linux/uaccess.h>
#include <asm/debugfs.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/rtas.h>
#include <asm/pmc.h>
#include <asm/reg.h>
#ifdef CONFIG_PMAC_BACKLIGHT
#include <asm/backlight.h>
#endif
#ifdef CONFIG_PPC64
#include <asm/firmware.h>
#include <asm/processor.h>
#include <asm/tm.h>
#endif
#include <asm/kexec.h>
#include <asm/ppc-opcode.h>
#include <asm/rio.h>
#include <asm/fadump.h>
#include <asm/switch_to.h>
#include <asm/tm.h>
#include <asm/debug.h>
#include <asm/asm-prototypes.h>
#include <asm/hmi.h>
#include <sysdev/fsl_pci.h>
#include <asm/kprobes.h>
#include <asm/stacktrace.h>

#if defined(CONFIG_DEBUGGER) || defined(CONFIG_KEXEC_CORE)
int (*__debugger)(struct pt_regs *regs) __read_mostly;
int (*__debugger_ipi)(struct pt_regs *regs) __read_mostly;
int (*__debugger_bpt)(struct pt_regs *regs) __read_mostly;
int (*__debugger_sstep)(struct pt_regs *regs) __read_mostly;
int (*__debugger_iabr_match)(struct pt_regs *regs) __read_mostly;
int (*__debugger_break_match)(struct pt_regs *regs) __read_mostly;
int (*__debugger_fault_handler)(struct pt_regs *regs) __read_mostly;

EXPORT_SYMBOL(__debugger);
EXPORT_SYMBOL(__debugger_ipi);
EXPORT_SYMBOL(__debugger_bpt);
EXPORT_SYMBOL(__debugger_sstep);
EXPORT_SYMBOL(__debugger_iabr_match);
EXPORT_SYMBOL(__debugger_break_match);
EXPORT_SYMBOL(__debugger_fault_handler);
#endif

/* Transactional Memory trap debug */
#ifdef TM_DEBUG_SW
#define TM_DEBUG(x...) printk(KERN_INFO x)
#else
#define TM_DEBUG(x...) do { } while(0)
#endif

static const char *signame(int signr)
{
	switch (signr) {
	case SIGBUS:	return "bus error";
	case SIGFPE:	return "floating point exception";
	case SIGILL:	return "illegal instruction";
	case SIGSEGV:	return "segfault";
	case SIGTRAP:	return "unhandled trap";
	}

	return "unknown signal";
}

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

/*
 * If oops/die is expected to crash the machine, return true here.
 *
 * This should not be expected to be 100% accurate, there may be
 * notifiers registered or other unexpected conditions that may bring
 * down the kernel. Or if the current process in the kernel is holding
 * locks or has other critical state, the kernel may become effectively
 * unusable anyway.
 */
bool die_will_crash(void)
{
	if (should_fadump_crash())
		return true;
	if (kexec_should_crash(current))
		return true;
	if (in_interrupt() || panic_on_oops ||
			!current->pid || is_global_init(current))
		return true;

	return false;
}

static arch_spinlock_t die_lock = __ARCH_SPIN_LOCK_UNLOCKED;
static int die_owner = -1;
static unsigned int die_nest_count;
static int die_counter;

extern void panic_flush_kmsg_start(void)
{
	/*
	 * These are mostly taken from kernel/panic.c, but tries to do
	 * relatively minimal work. Don't use delay functions (TB may
	 * be broken), don't crash dump (need to set a firmware log),
	 * don't run notifiers. We do want to get some information to
	 * Linux console.
	 */
	console_verbose();
	bust_spinlocks(1);
}

extern void panic_flush_kmsg_end(void)
{
	printk_safe_flush_on_panic();
	kmsg_dump(KMSG_DUMP_PANIC);
	bust_spinlocks(0);
	debug_locks_off();
	console_flush_on_panic();
}

static unsigned long oops_begin(struct pt_regs *regs)
{
	int cpu;
	unsigned long flags;

	oops_enter();

	/* racy, but better than risking deadlock. */
	raw_local_irq_save(flags);
	cpu = smp_processor_id();
	if (!arch_spin_trylock(&die_lock)) {
		if (cpu == die_owner)
			/* nested oops. should stop eventually */;
		else
			arch_spin_lock(&die_lock);
	}
	die_nest_count++;
	die_owner = cpu;
	console_verbose();
	bust_spinlocks(1);
	if (machine_is(powermac))
		pmac_backlight_unblank();
	return flags;
}
NOKPROBE_SYMBOL(oops_begin);

static void oops_end(unsigned long flags, struct pt_regs *regs,
			       int signr)
{
	bust_spinlocks(0);
	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);
	die_nest_count--;
	oops_exit();
	printk("\n");
	if (!die_nest_count) {
		/* Nest count reaches zero, release the lock. */
		die_owner = -1;
		arch_spin_unlock(&die_lock);
	}
	raw_local_irq_restore(flags);

	/*
	 * system_reset_excption handles debugger, crash dump, panic, for 0x100
	 */
	if (TRAP(regs) == 0x100)
		return;

	crash_fadump(regs, "die oops");

	if (kexec_should_crash(current))
		crash_kexec(regs);

	if (!signr)
		return;

	/*
	 * While our oops output is serialised by a spinlock, output
	 * from panic() called below can race and corrupt it. If we
	 * know we are going to panic, delay for 1 second so we have a
	 * chance to get clean backtraces from all CPUs that are oopsing.
	 */
	if (in_interrupt() || panic_on_oops || !current->pid ||
	    is_global_init(current)) {
		mdelay(MSEC_PER_SEC);
	}

	if (panic_on_oops)
		panic("Fatal exception");
	do_exit(signr);
}
NOKPROBE_SYMBOL(oops_end);

static int __die(const char *str, struct pt_regs *regs, long err)
{
	printk("Oops: %s, sig: %ld [#%d]\n", str, err, ++die_counter);

	printk("%s PAGE_SIZE=%luK%s%s%s%s%s%s%s %s\n",
	       IS_ENABLED(CONFIG_CPU_LITTLE_ENDIAN) ? "LE" : "BE",
	       PAGE_SIZE / 1024,
	       early_radix_enabled() ? " MMU=Radix" : "",
	       early_mmu_has_feature(MMU_FTR_HPTE_TABLE) ? " MMU=Hash" : "",
	       IS_ENABLED(CONFIG_PREEMPT) ? " PREEMPT" : "",
	       IS_ENABLED(CONFIG_SMP) ? " SMP" : "",
	       IS_ENABLED(CONFIG_SMP) ? (" NR_CPUS=" __stringify(NR_CPUS)) : "",
	       debug_pagealloc_enabled() ? " DEBUG_PAGEALLOC" : "",
	       IS_ENABLED(CONFIG_NUMA) ? " NUMA" : "",
	       ppc_md.name ? ppc_md.name : "");

	if (notify_die(DIE_OOPS, str, regs, err, 255, SIGSEGV) == NOTIFY_STOP)
		return 1;

	print_modules();
	show_regs(regs);

	return 0;
}
NOKPROBE_SYMBOL(__die);

void die(const char *str, struct pt_regs *regs, long err)
{
	unsigned long flags;

	/*
	 * system_reset_excption handles debugger, crash dump, panic, for 0x100
	 */
	if (TRAP(regs) != 0x100) {
		if (debugger(regs))
			return;
	}

	flags = oops_begin(regs);
	if (__die(str, regs, err))
		err = 0;
	oops_end(flags, regs, err);
}
NOKPROBE_SYMBOL(die);

void user_single_step_report(struct pt_regs *regs)
{
	force_sig_fault(SIGTRAP, TRAP_TRACE, (void __user *)regs->nip, current);
}

static void show_signal_msg(int signr, struct pt_regs *regs, int code,
			    unsigned long addr)
{
	static DEFINE_RATELIMIT_STATE(rs, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);

	if (!show_unhandled_signals)
		return;

	if (!unhandled_signal(current, signr))
		return;

	if (!__ratelimit(&rs))
		return;

	pr_info("%s[%d]: %s (%d) at %lx nip %lx lr %lx code %x",
		current->comm, current->pid, signame(signr), signr,
		addr, regs->nip, regs->link, code);

	print_vma_addr(KERN_CONT " in ", regs->nip);

	pr_cont("\n");

	show_user_instructions(regs);
}

static bool exception_common(int signr, struct pt_regs *regs, int code,
			      unsigned long addr)
{
	if (!user_mode(regs)) {
		die("Exception in kernel mode", regs, signr);
		return false;
	}

	show_signal_msg(signr, regs, code, addr);

	if (arch_irqs_disabled() && !arch_irq_disabled_regs(regs))
		local_irq_enable();

	current->thread.trap_nr = code;

	/*
	 * Save all the pkey registers AMR/IAMR/UAMOR. Eg: Core dumps need
	 * to capture the content, if the task gets killed.
	 */
	thread_pkey_regs_save(&current->thread);

	return true;
}

void _exception_pkey(struct pt_regs *regs, unsigned long addr, int key)
{
	if (!exception_common(SIGSEGV, regs, SEGV_PKUERR, addr))
		return;

	force_sig_pkuerr((void __user *) addr, key);
}

void _exception(int signr, struct pt_regs *regs, int code, unsigned long addr)
{
	if (!exception_common(signr, regs, code, addr))
		return;

	force_sig_fault(signr, code, (void __user *)addr, current);
}

/*
 * The interrupt architecture has a quirk in that the HV interrupts excluding
 * the NMIs (0x100 and 0x200) do not clear MSR[RI] at entry. The first thing
 * that an interrupt handler must do is save off a GPR into a scratch register,
 * and all interrupts on POWERNV (HV=1) use the HSPRG1 register as scratch.
 * Therefore an NMI can clobber an HV interrupt's live HSPRG1 without noticing
 * that it is non-reentrant, which leads to random data corruption.
 *
 * The solution is for NMI interrupts in HV mode to check if they originated
 * from these critical HV interrupt regions. If so, then mark them not
 * recoverable.
 *
 * An alternative would be for HV NMIs to use SPRG for scratch to avoid the
 * HSPRG1 clobber, however this would cause guest SPRG to be clobbered. Linux
 * guests should always have MSR[RI]=0 when its scratch SPRG is in use, so
 * that would work. However any other guest OS that may have the SPRG live
 * and MSR[RI]=1 could encounter silent corruption.
 *
 * Builds that do not support KVM could take this second option to increase
 * the recoverability of NMIs.
 */
void hv_nmi_check_nonrecoverable(struct pt_regs *regs)
{
#ifdef CONFIG_PPC_POWERNV
	unsigned long kbase = (unsigned long)_stext;
	unsigned long nip = regs->nip;

	if (!(regs->msr & MSR_RI))
		return;
	if (!(regs->msr & MSR_HV))
		return;
	if (regs->msr & MSR_PR)
		return;

	/*
	 * Now test if the interrupt has hit a range that may be using
	 * HSPRG1 without having RI=0 (i.e., an HSRR interrupt). The
	 * problem ranges all run un-relocated. Test real and virt modes
	 * at the same time by droping the high bit of the nip (virt mode
	 * entry points still have the +0x4000 offset).
	 */
	nip &= ~0xc000000000000000ULL;
	if ((nip >= 0x500 && nip < 0x600) || (nip >= 0x4500 && nip < 0x4600))
		goto nonrecoverable;
	if ((nip >= 0x980 && nip < 0xa00) || (nip >= 0x4980 && nip < 0x4a00))
		goto nonrecoverable;
	if ((nip >= 0xe00 && nip < 0xec0) || (nip >= 0x4e00 && nip < 0x4ec0))
		goto nonrecoverable;
	if ((nip >= 0xf80 && nip < 0xfa0) || (nip >= 0x4f80 && nip < 0x4fa0))
		goto nonrecoverable;

	/* Trampoline code runs un-relocated so subtract kbase. */
	if (nip >= (unsigned long)(start_real_trampolines - kbase) &&
			nip < (unsigned long)(end_real_trampolines - kbase))
		goto nonrecoverable;
	if (nip >= (unsigned long)(start_virt_trampolines - kbase) &&
			nip < (unsigned long)(end_virt_trampolines - kbase))
		goto nonrecoverable;
	return;

nonrecoverable:
	regs->msr &= ~MSR_RI;
#endif
}

void system_reset_exception(struct pt_regs *regs)
{
	unsigned long hsrr0, hsrr1;
	bool nested = in_nmi();
	bool saved_hsrrs = false;

	/*
	 * Avoid crashes in case of nested NMI exceptions. Recoverability
	 * is determined by RI and in_nmi
	 */
	if (!nested)
		nmi_enter();

	/*
	 * System reset can interrupt code where HSRRs are live and MSR[RI]=1.
	 * The system reset interrupt itself may clobber HSRRs (e.g., to call
	 * OPAL), so save them here and restore them before returning.
	 *
	 * Machine checks don't need to save HSRRs, as the real mode handler
	 * is careful to avoid them, and the regular handler is not delivered
	 * as an NMI.
	 */
	if (cpu_has_feature(CPU_FTR_HVMODE)) {
		hsrr0 = mfspr(SPRN_HSRR0);
		hsrr1 = mfspr(SPRN_HSRR1);
		saved_hsrrs = true;
	}

	hv_nmi_check_nonrecoverable(regs);

	__this_cpu_inc(irq_stat.sreset_irqs);

	/* See if any machine dependent calls */
	if (ppc_md.system_reset_exception) {
		if (ppc_md.system_reset_exception(regs))
			goto out;
	}

	if (debugger(regs))
		goto out;

	/*
	 * A system reset is a request to dump, so we always send
	 * it through the crashdump code (if fadump or kdump are
	 * registered).
	 */
	crash_fadump(regs, "System Reset");

	crash_kexec(regs);

	/*
	 * We aren't the primary crash CPU. We need to send it
	 * to a holding pattern to avoid it ending up in the panic
	 * code.
	 */
	crash_kexec_secondary(regs);

	/*
	 * No debugger or crash dump registered, print logs then
	 * panic.
	 */
	die("System Reset", regs, SIGABRT);

	mdelay(2*MSEC_PER_SEC); /* Wait a little while for others to print */
	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);
	nmi_panic(regs, "System Reset");

out:
#ifdef CONFIG_PPC_BOOK3S_64
	BUG_ON(get_paca()->in_nmi == 0);
	if (get_paca()->in_nmi > 1)
		nmi_panic(regs, "Unrecoverable nested System Reset");
#endif
	/* Must die if the interrupt is not recoverable */
	if (!(regs->msr & MSR_RI))
		nmi_panic(regs, "Unrecoverable System Reset");

	if (saved_hsrrs) {
		mtspr(SPRN_HSRR0, hsrr0);
		mtspr(SPRN_HSRR1, hsrr1);
	}

	if (!nested)
		nmi_exit();

	/* What should we do here? We could issue a shutdown or hard reset. */
}

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
		if (*nip == PPC_INST_NOP)
			nip -= 2;
		else if (*nip == PPC_INST_ISYNC)
			--nip;
		if (*nip == PPC_INST_SYNC || (*nip >> 26) == OP_TRAP) {
			unsigned int rb;

			--nip;
			rb = (*nip >> 11) & 0x1f;
			printk(KERN_DEBUG "%s bad port %lx at %p\n",
			       (*nip & 0x100)? "OUT to": "IN from",
			       regs->gpr[rb] - _IO_BASE, nip);
			regs->msr |= MSR_RI;
			regs->nip = extable_fixup(entry);
			return 1;
		}
	}
#endif /* CONFIG_PPC32 */
	return 0;
}

#ifdef CONFIG_PPC_ADV_DEBUG_REGS
/* On 4xx, the reason for the machine check or program exception
   is in the ESR. */
#define get_reason(regs)	((regs)->dsisr)
#define REASON_FP		ESR_FP
#define REASON_ILLEGAL		(ESR_PIL | ESR_PUO)
#define REASON_PRIVILEGED	ESR_PPR
#define REASON_TRAP		ESR_PTR

/* single-step stuff */
#define single_stepping(regs)	(current->thread.debug.dbcr0 & DBCR0_IC)
#define clear_single_step(regs)	(current->thread.debug.dbcr0 &= ~DBCR0_IC)
#define clear_br_trace(regs)	do {} while(0)
#else
/* On non-4xx, the reason for the machine check or program
   exception is in the MSR. */
#define get_reason(regs)	((regs)->msr)
#define REASON_TM		SRR1_PROGTM
#define REASON_FP		SRR1_PROGFPE
#define REASON_ILLEGAL		SRR1_PROGILL
#define REASON_PRIVILEGED	SRR1_PROGPRIV
#define REASON_TRAP		SRR1_PROGTRAP

#define single_stepping(regs)	((regs)->msr & MSR_SE)
#define clear_single_step(regs)	((regs)->msr &= ~MSR_SE)
#define clear_br_trace(regs)	((regs)->msr &= ~MSR_BE)
#endif

#if defined(CONFIG_E500)
int machine_check_e500mc(struct pt_regs *regs)
{
	unsigned long mcsr = mfspr(SPRN_MCSR);
	unsigned long pvr = mfspr(SPRN_PVR);
	unsigned long reason = mcsr;
	int recoverable = 1;

	if (reason & MCSR_LD) {
		recoverable = fsl_rio_mcheck_exception(regs);
		if (recoverable == 1)
			goto silent_out;
	}

	printk("Machine check in kernel mode.\n");
	printk("Caused by (from MCSR=%lx): ", reason);

	if (reason & MCSR_MCP)
		pr_cont("Machine Check Signal\n");

	if (reason & MCSR_ICPERR) {
		pr_cont("Instruction Cache Parity Error\n");

		/*
		 * This is recoverable by invalidating the i-cache.
		 */
		mtspr(SPRN_L1CSR1, mfspr(SPRN_L1CSR1) | L1CSR1_ICFI);
		while (mfspr(SPRN_L1CSR1) & L1CSR1_ICFI)
			;

		/*
		 * This will generally be accompanied by an instruction
		 * fetch error report -- only treat MCSR_IF as fatal
		 * if it wasn't due to an L1 parity error.
		 */
		reason &= ~MCSR_IF;
	}

	if (reason & MCSR_DCPERR_MC) {
		pr_cont("Data Cache Parity Error\n");

		/*
		 * In write shadow mode we auto-recover from the error, but it
		 * may still get logged and cause a machine check.  We should
		 * only treat the non-write shadow case as non-recoverable.
		 */
		/* On e6500 core, L1 DCWS (Data cache write shadow mode) bit
		 * is not implemented but L1 data cache always runs in write
		 * shadow mode. Hence on data cache parity errors HW will
		 * automatically invalidate the L1 Data Cache.
		 */
		if (PVR_VER(pvr) != PVR_VER_E6500) {
			if (!(mfspr(SPRN_L1CSR2) & L1CSR2_DCWS))
				recoverable = 0;
		}
	}

	if (reason & MCSR_L2MMU_MHIT) {
		pr_cont("Hit on multiple TLB entries\n");
		recoverable = 0;
	}

	if (reason & MCSR_NMI)
		pr_cont("Non-maskable interrupt\n");

	if (reason & MCSR_IF) {
		pr_cont("Instruction Fetch Error Report\n");
		recoverable = 0;
	}

	if (reason & MCSR_LD) {
		pr_cont("Load Error Report\n");
		recoverable = 0;
	}

	if (reason & MCSR_ST) {
		pr_cont("Store Error Report\n");
		recoverable = 0;
	}

	if (reason & MCSR_LDG) {
		pr_cont("Guarded Load Error Report\n");
		recoverable = 0;
	}

	if (reason & MCSR_TLBSYNC)
		pr_cont("Simultaneous tlbsync operations\n");

	if (reason & MCSR_BSL2_ERR) {
		pr_cont("Level 2 Cache Error\n");
		recoverable = 0;
	}

	if (reason & MCSR_MAV) {
		u64 addr;

		addr = mfspr(SPRN_MCAR);
		addr |= (u64)mfspr(SPRN_MCARU) << 32;

		pr_cont("Machine Check %s Address: %#llx\n",
		       reason & MCSR_MEA ? "Effective" : "Physical", addr);
	}

silent_out:
	mtspr(SPRN_MCSR, mcsr);
	return mfspr(SPRN_MCSR) == 0 && recoverable;
}

int machine_check_e500(struct pt_regs *regs)
{
	unsigned long reason = mfspr(SPRN_MCSR);

	if (reason & MCSR_BUS_RBERR) {
		if (fsl_rio_mcheck_exception(regs))
			return 1;
		if (fsl_pci_mcheck_exception(regs))
			return 1;
	}

	printk("Machine check in kernel mode.\n");
	printk("Caused by (from MCSR=%lx): ", reason);

	if (reason & MCSR_MCP)
		pr_cont("Machine Check Signal\n");
	if (reason & MCSR_ICPERR)
		pr_cont("Instruction Cache Parity Error\n");
	if (reason & MCSR_DCP_PERR)
		pr_cont("Data Cache Push Parity Error\n");
	if (reason & MCSR_DCPERR)
		pr_cont("Data Cache Parity Error\n");
	if (reason & MCSR_BUS_IAERR)
		pr_cont("Bus - Instruction Address Error\n");
	if (reason & MCSR_BUS_RAERR)
		pr_cont("Bus - Read Address Error\n");
	if (reason & MCSR_BUS_WAERR)
		pr_cont("Bus - Write Address Error\n");
	if (reason & MCSR_BUS_IBERR)
		pr_cont("Bus - Instruction Data Error\n");
	if (reason & MCSR_BUS_RBERR)
		pr_cont("Bus - Read Data Bus Error\n");
	if (reason & MCSR_BUS_WBERR)
		pr_cont("Bus - Write Data Bus Error\n");
	if (reason & MCSR_BUS_IPERR)
		pr_cont("Bus - Instruction Parity Error\n");
	if (reason & MCSR_BUS_RPERR)
		pr_cont("Bus - Read Parity Error\n");

	return 0;
}

int machine_check_generic(struct pt_regs *regs)
{
	return 0;
}
#elif defined(CONFIG_E200)
int machine_check_e200(struct pt_regs *regs)
{
	unsigned long reason = mfspr(SPRN_MCSR);

	printk("Machine check in kernel mode.\n");
	printk("Caused by (from MCSR=%lx): ", reason);

	if (reason & MCSR_MCP)
		pr_cont("Machine Check Signal\n");
	if (reason & MCSR_CP_PERR)
		pr_cont("Cache Push Parity Error\n");
	if (reason & MCSR_CPERR)
		pr_cont("Cache Parity Error\n");
	if (reason & MCSR_EXCP_ERR)
		pr_cont("ISI, ITLB, or Bus Error on first instruction fetch for an exception handler\n");
	if (reason & MCSR_BUS_IRERR)
		pr_cont("Bus - Read Bus Error on instruction fetch\n");
	if (reason & MCSR_BUS_DRERR)
		pr_cont("Bus - Read Bus Error on data load\n");
	if (reason & MCSR_BUS_WRERR)
		pr_cont("Bus - Write Bus Error on buffered store or cache line push\n");

	return 0;
}
#elif defined(CONFIG_PPC32)
int machine_check_generic(struct pt_regs *regs)
{
	unsigned long reason = regs->msr;

	printk("Machine check in kernel mode.\n");
	printk("Caused by (from SRR1=%lx): ", reason);
	switch (reason & 0x601F0000) {
	case 0x80000:
		pr_cont("Machine check signal\n");
		break;
	case 0:		/* for 601 */
	case 0x40000:
	case 0x140000:	/* 7450 MSS error and TEA */
		pr_cont("Transfer error ack signal\n");
		break;
	case 0x20000:
		pr_cont("Data parity error signal\n");
		break;
	case 0x10000:
		pr_cont("Address parity error signal\n");
		break;
	case 0x20000000:
		pr_cont("L1 Data Cache error\n");
		break;
	case 0x40000000:
		pr_cont("L1 Instruction Cache error\n");
		break;
	case 0x00100000:
		pr_cont("L2 data cache parity error\n");
		break;
	default:
		pr_cont("Unknown values in msr\n");
	}
	return 0;
}
#endif /* everything else */

void machine_check_exception(struct pt_regs *regs)
{
	int recover = 0;
	bool nested = in_nmi();
	if (!nested)
		nmi_enter();

	__this_cpu_inc(irq_stat.mce_exceptions);

	add_taint(TAINT_MACHINE_CHECK, LOCKDEP_NOW_UNRELIABLE);

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
		goto bail;

	if (debugger_fault_handler(regs))
		goto bail;

	if (check_io_access(regs))
		goto bail;

	if (!nested)
		nmi_exit();

	die("Machine check", regs, SIGBUS);

	/* Must die if the interrupt is not recoverable */
	if (!(regs->msr & MSR_RI))
		nmi_panic(regs, "Unrecoverable Machine check");

	return;

bail:
	if (!nested)
		nmi_exit();
}

void SMIException(struct pt_regs *regs)
{
	die("System Management Interrupt", regs, SIGABRT);
}

#ifdef CONFIG_VSX
static void p9_hmi_special_emu(struct pt_regs *regs)
{
	unsigned int ra, rb, t, i, sel, instr, rc;
	const void __user *addr;
	u8 vbuf[16], *vdst;
	unsigned long ea, msr, msr_mask;
	bool swap;

	if (__get_user_inatomic(instr, (unsigned int __user *)regs->nip))
		return;

	/*
	 * lxvb16x	opcode: 0x7c0006d8
	 * lxvd2x	opcode: 0x7c000698
	 * lxvh8x	opcode: 0x7c000658
	 * lxvw4x	opcode: 0x7c000618
	 */
	if ((instr & 0xfc00073e) != 0x7c000618) {
		pr_devel("HMI vec emu: not vector CI %i:%s[%d] nip=%016lx"
			 " instr=%08x\n",
			 smp_processor_id(), current->comm, current->pid,
			 regs->nip, instr);
		return;
	}

	/* Grab vector registers into the task struct */
	msr = regs->msr; /* Grab msr before we flush the bits */
	flush_vsx_to_thread(current);
	enable_kernel_altivec();

	/*
	 * Is userspace running with a different endian (this is rare but
	 * not impossible)
	 */
	swap = (msr & MSR_LE) != (MSR_KERNEL & MSR_LE);

	/* Decode the instruction */
	ra = (instr >> 16) & 0x1f;
	rb = (instr >> 11) & 0x1f;
	t = (instr >> 21) & 0x1f;
	if (instr & 1)
		vdst = (u8 *)&current->thread.vr_state.vr[t];
	else
		vdst = (u8 *)&current->thread.fp_state.fpr[t][0];

	/* Grab the vector address */
	ea = regs->gpr[rb] + (ra ? regs->gpr[ra] : 0);
	if (is_32bit_task())
		ea &= 0xfffffffful;
	addr = (__force const void __user *)ea;

	/* Check it */
	if (!access_ok(addr, 16)) {
		pr_devel("HMI vec emu: bad access %i:%s[%d] nip=%016lx"
			 " instr=%08x addr=%016lx\n",
			 smp_processor_id(), current->comm, current->pid,
			 regs->nip, instr, (unsigned long)addr);
		return;
	}

	/* Read the vector */
	rc = 0;
	if ((unsigned long)addr & 0xfUL)
		/* unaligned case */
		rc = __copy_from_user_inatomic(vbuf, addr, 16);
	else
		__get_user_atomic_128_aligned(vbuf, addr, rc);
	if (rc) {
		pr_devel("HMI vec emu: page fault %i:%s[%d] nip=%016lx"
			 " instr=%08x addr=%016lx\n",
			 smp_processor_id(), current->comm, current->pid,
			 regs->nip, instr, (unsigned long)addr);
		return;
	}

	pr_devel("HMI vec emu: emulated vector CI %i:%s[%d] nip=%016lx"
		 " instr=%08x addr=%016lx\n",
		 smp_processor_id(), current->comm, current->pid, regs->nip,
		 instr, (unsigned long) addr);

	/* Grab instruction "selector" */
	sel = (instr >> 6) & 3;

	/*
	 * Check to make sure the facility is actually enabled. This
	 * could happen if we get a false positive hit.
	 *
	 * lxvd2x/lxvw4x always check MSR VSX sel = 0,2
	 * lxvh8x/lxvb16x check MSR VSX or VEC depending on VSR used sel = 1,3
	 */
	msr_mask = MSR_VSX;
	if ((sel & 1) && (instr & 1)) /* lxvh8x & lxvb16x + VSR >= 32 */
		msr_mask = MSR_VEC;
	if (!(msr & msr_mask)) {
		pr_devel("HMI vec emu: MSR fac clear %i:%s[%d] nip=%016lx"
			 " instr=%08x msr:%016lx\n",
			 smp_processor_id(), current->comm, current->pid,
			 regs->nip, instr, msr);
		return;
	}

	/* Do logging here before we modify sel based on endian */
	switch (sel) {
	case 0:	/* lxvw4x */
		PPC_WARN_EMULATED(lxvw4x, regs);
		break;
	case 1: /* lxvh8x */
		PPC_WARN_EMULATED(lxvh8x, regs);
		break;
	case 2: /* lxvd2x */
		PPC_WARN_EMULATED(lxvd2x, regs);
		break;
	case 3: /* lxvb16x */
		PPC_WARN_EMULATED(lxvb16x, regs);
		break;
	}

#ifdef __LITTLE_ENDIAN__
	/*
	 * An LE kernel stores the vector in the task struct as an LE
	 * byte array (effectively swapping both the components and
	 * the content of the components). Those instructions expect
	 * the components to remain in ascending address order, so we
	 * swap them back.
	 *
	 * If we are running a BE user space, the expectation is that
	 * of a simple memcpy, so forcing the emulation to look like
	 * a lxvb16x should do the trick.
	 */
	if (swap)
		sel = 3;

	switch (sel) {
	case 0:	/* lxvw4x */
		for (i = 0; i < 4; i++)
			((u32 *)vdst)[i] = ((u32 *)vbuf)[3-i];
		break;
	case 1: /* lxvh8x */
		for (i = 0; i < 8; i++)
			((u16 *)vdst)[i] = ((u16 *)vbuf)[7-i];
		break;
	case 2: /* lxvd2x */
		for (i = 0; i < 2; i++)
			((u64 *)vdst)[i] = ((u64 *)vbuf)[1-i];
		break;
	case 3: /* lxvb16x */
		for (i = 0; i < 16; i++)
			vdst[i] = vbuf[15-i];
		break;
	}
#else /* __LITTLE_ENDIAN__ */
	/* On a big endian kernel, a BE userspace only needs a memcpy */
	if (!swap)
		sel = 3;

	/* Otherwise, we need to swap the content of the components */
	switch (sel) {
	case 0:	/* lxvw4x */
		for (i = 0; i < 4; i++)
			((u32 *)vdst)[i] = cpu_to_le32(((u32 *)vbuf)[i]);
		break;
	case 1: /* lxvh8x */
		for (i = 0; i < 8; i++)
			((u16 *)vdst)[i] = cpu_to_le16(((u16 *)vbuf)[i]);
		break;
	case 2: /* lxvd2x */
		for (i = 0; i < 2; i++)
			((u64 *)vdst)[i] = cpu_to_le64(((u64 *)vbuf)[i]);
		break;
	case 3: /* lxvb16x */
		memcpy(vdst, vbuf, 16);
		break;
	}
#endif /* !__LITTLE_ENDIAN__ */

	/* Go to next instruction */
	regs->nip += 4;
}
#endif /* CONFIG_VSX */

void handle_hmi_exception(struct pt_regs *regs)
{
	struct pt_regs *old_regs;

	old_regs = set_irq_regs(regs);
	irq_enter();

#ifdef CONFIG_VSX
	/* Real mode flagged P9 special emu is needed */
	if (local_paca->hmi_p9_special_emu) {
		local_paca->hmi_p9_special_emu = 0;

		/*
		 * We don't want to take page faults while doing the
		 * emulation, we just replay the instruction if necessary.
		 */
		pagefault_disable();
		p9_hmi_special_emu(regs);
		pagefault_enable();
	}
#endif /* CONFIG_VSX */

	if (ppc_md.handle_hmi_exception)
		ppc_md.handle_hmi_exception(regs);

	irq_exit();
	set_irq_regs(old_regs);
}

void unknown_exception(struct pt_regs *regs)
{
	enum ctx_state prev_state = exception_enter();

	printk("Bad trap at PC: %lx, SR: %lx, vector=%lx\n",
	       regs->nip, regs->msr, regs->trap);

	_exception(SIGTRAP, regs, TRAP_UNK, 0);

	exception_exit(prev_state);
}

void instruction_breakpoint_exception(struct pt_regs *regs)
{
	enum ctx_state prev_state = exception_enter();

	if (notify_die(DIE_IABR_MATCH, "iabr_match", regs, 5,
					5, SIGTRAP) == NOTIFY_STOP)
		goto bail;
	if (debugger_iabr_match(regs))
		goto bail;
	_exception(SIGTRAP, regs, TRAP_BRKPT, regs->nip);

bail:
	exception_exit(prev_state);
}

void RunModeException(struct pt_regs *regs)
{
	_exception(SIGTRAP, regs, TRAP_UNK, 0);
}

void single_step_exception(struct pt_regs *regs)
{
	enum ctx_state prev_state = exception_enter();

	clear_single_step(regs);
	clear_br_trace(regs);

	if (kprobe_post_handler(regs))
		return;

	if (notify_die(DIE_SSTEP, "single_step", regs, 5,
					5, SIGTRAP) == NOTIFY_STOP)
		goto bail;
	if (debugger_sstep(regs))
		goto bail;

	_exception(SIGTRAP, regs, TRAP_TRACE, regs->nip);

bail:
	exception_exit(prev_state);
}
NOKPROBE_SYMBOL(single_step_exception);

/*
 * After we have successfully emulated an instruction, we have to
 * check if the instruction was being single-stepped, and if so,
 * pretend we got a single-step exception.  This was pointed out
 * by Kumar Gala.  -- paulus
 */
static void emulate_single_step(struct pt_regs *regs)
{
	if (single_stepping(regs))
		single_step_exception(regs);
}

static inline int __parse_fpscr(unsigned long fpscr)
{
	int ret = FPE_FLTUNK;

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

	code = __parse_fpscr(current->thread.fp_state.fpscr);

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

		/* if process is 32-bit, clear upper 32 bits of EA */
		if ((regs->msr & MSR_64BIT) == 0)
			EA &= 0xFFFFFFFF;

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

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
static inline bool tm_abort_check(struct pt_regs *regs, int cause)
{
        /* If we're emulating a load/store in an active transaction, we cannot
         * emulate it as the kernel operates in transaction suspended context.
         * We need to abort the transaction.  This creates a persistent TM
         * abort so tell the user what caused it with a new code.
	 */
	if (MSR_TM_TRANSACTIONAL(regs->msr)) {
		tm_enable();
		tm_abort(cause);
		return true;
	}
	return false;
}
#else
static inline bool tm_abort_check(struct pt_regs *regs, int reason)
{
	return false;
}
#endif

static int emulate_instruction(struct pt_regs *regs)
{
	u32 instword;
	u32 rd;

	if (!user_mode(regs))
		return -EINVAL;
	CHECK_FULL_REGS(regs);

	if (get_user(instword, (u32 __user *)(regs->nip)))
		return -EFAULT;

	/* Emulate the mfspr rD, PVR. */
	if ((instword & PPC_INST_MFSPR_PVR_MASK) == PPC_INST_MFSPR_PVR) {
		PPC_WARN_EMULATED(mfpvr, regs);
		rd = (instword >> 21) & 0x1f;
		regs->gpr[rd] = mfspr(SPRN_PVR);
		return 0;
	}

	/* Emulating the dcba insn is just a no-op.  */
	if ((instword & PPC_INST_DCBA_MASK) == PPC_INST_DCBA) {
		PPC_WARN_EMULATED(dcba, regs);
		return 0;
	}

	/* Emulate the mcrxr insn.  */
	if ((instword & PPC_INST_MCRXR_MASK) == PPC_INST_MCRXR) {
		int shift = (instword >> 21) & 0x1c;
		unsigned long msk = 0xf0000000UL >> shift;

		PPC_WARN_EMULATED(mcrxr, regs);
		regs->ccr = (regs->ccr & ~msk) | ((regs->xer >> shift) & msk);
		regs->xer &= ~0xf0000000UL;
		return 0;
	}

	/* Emulate load/store string insn. */
	if ((instword & PPC_INST_STRING_GEN_MASK) == PPC_INST_STRING) {
		if (tm_abort_check(regs,
				   TM_CAUSE_EMULATE | TM_CAUSE_PERSISTENT))
			return -EINVAL;
		PPC_WARN_EMULATED(string, regs);
		return emulate_string_inst(regs, instword);
	}

	/* Emulate the popcntb (Population Count Bytes) instruction. */
	if ((instword & PPC_INST_POPCNTB_MASK) == PPC_INST_POPCNTB) {
		PPC_WARN_EMULATED(popcntb, regs);
		return emulate_popcntb_inst(regs, instword);
	}

	/* Emulate isel (Integer Select) instruction */
	if ((instword & PPC_INST_ISEL_MASK) == PPC_INST_ISEL) {
		PPC_WARN_EMULATED(isel, regs);
		return emulate_isel(regs, instword);
	}

	/* Emulate sync instruction variants */
	if ((instword & PPC_INST_SYNC_MASK) == PPC_INST_SYNC) {
		PPC_WARN_EMULATED(sync, regs);
		asm volatile("sync");
		return 0;
	}

#ifdef CONFIG_PPC64
	/* Emulate the mfspr rD, DSCR. */
	if ((((instword & PPC_INST_MFSPR_DSCR_USER_MASK) ==
		PPC_INST_MFSPR_DSCR_USER) ||
	     ((instword & PPC_INST_MFSPR_DSCR_MASK) ==
		PPC_INST_MFSPR_DSCR)) &&
			cpu_has_feature(CPU_FTR_DSCR)) {
		PPC_WARN_EMULATED(mfdscr, regs);
		rd = (instword >> 21) & 0x1f;
		regs->gpr[rd] = mfspr(SPRN_DSCR);
		return 0;
	}
	/* Emulate the mtspr DSCR, rD. */
	if ((((instword & PPC_INST_MTSPR_DSCR_USER_MASK) ==
		PPC_INST_MTSPR_DSCR_USER) ||
	     ((instword & PPC_INST_MTSPR_DSCR_MASK) ==
		PPC_INST_MTSPR_DSCR)) &&
			cpu_has_feature(CPU_FTR_DSCR)) {
		PPC_WARN_EMULATED(mtdscr, regs);
		rd = (instword >> 21) & 0x1f;
		current->thread.dscr = regs->gpr[rd];
		current->thread.dscr_inherit = 1;
		mtspr(SPRN_DSCR, current->thread.dscr);
		return 0;
	}
#endif

	return -EINVAL;
}

int is_valid_bugaddr(unsigned long addr)
{
	return is_kernel_addr(addr);
}

#ifdef CONFIG_MATH_EMULATION
static int emulate_math(struct pt_regs *regs)
{
	int ret;
	extern int do_mathemu(struct pt_regs *regs);

	ret = do_mathemu(regs);
	if (ret >= 0)
		PPC_WARN_EMULATED(math, regs);

	switch (ret) {
	case 0:
		emulate_single_step(regs);
		return 0;
	case 1: {
			int code = 0;
			code = __parse_fpscr(current->thread.fp_state.fpscr);
			_exception(SIGFPE, regs, code, regs->nip);
			return 0;
		}
	case -EFAULT:
		_exception(SIGSEGV, regs, SEGV_MAPERR, regs->nip);
		return 0;
	}

	return -1;
}
#else
static inline int emulate_math(struct pt_regs *regs) { return -1; }
#endif

void program_check_exception(struct pt_regs *regs)
{
	enum ctx_state prev_state = exception_enter();
	unsigned int reason = get_reason(regs);

	/* We can now get here via a FP Unavailable exception if the core
	 * has no FPU, in that case the reason flags will be 0 */

	if (reason & REASON_FP) {
		/* IEEE FP exception */
		parse_fpe(regs);
		goto bail;
	}
	if (reason & REASON_TRAP) {
		unsigned long bugaddr;
		/* Debugger is first in line to stop recursive faults in
		 * rcu_lock, notify_die, or atomic_notifier_call_chain */
		if (debugger_bpt(regs))
			goto bail;

		if (kprobe_handler(regs))
			goto bail;

		/* trap exception */
		if (notify_die(DIE_BPT, "breakpoint", regs, 5, 5, SIGTRAP)
				== NOTIFY_STOP)
			goto bail;

		bugaddr = regs->nip;
		/*
		 * Fixup bugaddr for BUG_ON() in real mode
		 */
		if (!is_kernel_addr(bugaddr) && !(regs->msr & MSR_IR))
			bugaddr += PAGE_OFFSET;

		if (!(regs->msr & MSR_PR) &&  /* not user-mode */
		    report_bug(bugaddr, regs) == BUG_TRAP_TYPE_WARN) {
			regs->nip += 4;
			goto bail;
		}
		_exception(SIGTRAP, regs, TRAP_BRKPT, regs->nip);
		goto bail;
	}
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	if (reason & REASON_TM) {
		/* This is a TM "Bad Thing Exception" program check.
		 * This occurs when:
		 * -  An rfid/hrfid/mtmsrd attempts to cause an illegal
		 *    transition in TM states.
		 * -  A trechkpt is attempted when transactional.
		 * -  A treclaim is attempted when non transactional.
		 * -  A tend is illegally attempted.
		 * -  writing a TM SPR when transactional.
		 *
		 * If usermode caused this, it's done something illegal and
		 * gets a SIGILL slap on the wrist.  We call it an illegal
		 * operand to distinguish from the instruction just being bad
		 * (e.g. executing a 'tend' on a CPU without TM!); it's an
		 * illegal /placement/ of a valid instruction.
		 */
		if (user_mode(regs)) {
			_exception(SIGILL, regs, ILL_ILLOPN, regs->nip);
			goto bail;
		} else {
			printk(KERN_EMERG "Unexpected TM Bad Thing exception "
			       "at %lx (msr 0x%lx) tm_scratch=%llx\n",
			       regs->nip, regs->msr, get_paca()->tm_scratch);
			die("Unrecoverable exception", regs, SIGABRT);
		}
	}
#endif

	/*
	 * If we took the program check in the kernel skip down to sending a
	 * SIGILL. The subsequent cases all relate to emulating instructions
	 * which we should only do for userspace. We also do not want to enable
	 * interrupts for kernel faults because that might lead to further
	 * faults, and loose the context of the original exception.
	 */
	if (!user_mode(regs))
		goto sigill;

	/* We restore the interrupt state now */
	if (!arch_irq_disabled_regs(regs))
		local_irq_enable();

	/* (reason & REASON_ILLEGAL) would be the obvious thing here,
	 * but there seems to be a hardware bug on the 405GP (RevD)
	 * that means ESR is sometimes set incorrectly - either to
	 * ESR_DST (!?) or 0.  In the process of chasing this with the
	 * hardware people - not sure if it can happen on any illegal
	 * instruction or only on FP instructions, whether there is a
	 * pattern to occurrences etc. -dgibson 31/Mar/2003
	 */
	if (!emulate_math(regs))
		goto bail;

	/* Try to emulate it if we should. */
	if (reason & (REASON_ILLEGAL | REASON_PRIVILEGED)) {
		switch (emulate_instruction(regs)) {
		case 0:
			regs->nip += 4;
			emulate_single_step(regs);
			goto bail;
		case -EFAULT:
			_exception(SIGSEGV, regs, SEGV_MAPERR, regs->nip);
			goto bail;
		}
	}

sigill:
	if (reason & REASON_PRIVILEGED)
		_exception(SIGILL, regs, ILL_PRVOPC, regs->nip);
	else
		_exception(SIGILL, regs, ILL_ILLOPC, regs->nip);

bail:
	exception_exit(prev_state);
}
NOKPROBE_SYMBOL(program_check_exception);

/*
 * This occurs when running in hypervisor mode on POWER6 or later
 * and an illegal instruction is encountered.
 */
void emulation_assist_interrupt(struct pt_regs *regs)
{
	regs->msr |= REASON_ILLEGAL;
	program_check_exception(regs);
}
NOKPROBE_SYMBOL(emulation_assist_interrupt);

void alignment_exception(struct pt_regs *regs)
{
	enum ctx_state prev_state = exception_enter();
	int sig, code, fixed = 0;

	/* We restore the interrupt state now */
	if (!arch_irq_disabled_regs(regs))
		local_irq_enable();

	if (tm_abort_check(regs, TM_CAUSE_ALIGNMENT | TM_CAUSE_PERSISTENT))
		goto bail;

	/* we don't implement logging of alignment exceptions */
	if (!(current->thread.align_ctl & PR_UNALIGN_SIGBUS))
		fixed = fix_alignment(regs);

	if (fixed == 1) {
		regs->nip += 4;	/* skip over emulated instruction */
		emulate_single_step(regs);
		goto bail;
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

bail:
	exception_exit(prev_state);
}

void StackOverflow(struct pt_regs *regs)
{
	pr_crit("Kernel stack overflow in process %s[%d], r1=%lx\n",
		current->comm, task_pid_nr(current), regs->gpr[1]);
	debugger(regs);
	show_regs(regs);
	panic("kernel stack overflow");
}

void kernel_fp_unavailable_exception(struct pt_regs *regs)
{
	enum ctx_state prev_state = exception_enter();

	printk(KERN_EMERG "Unrecoverable FP Unavailable Exception "
			  "%lx at %lx\n", regs->trap, regs->nip);
	die("Unrecoverable FP Unavailable Exception", regs, SIGABRT);

	exception_exit(prev_state);
}

void altivec_unavailable_exception(struct pt_regs *regs)
{
	enum ctx_state prev_state = exception_enter();

	if (user_mode(regs)) {
		/* A user program has executed an altivec instruction,
		   but this kernel doesn't support altivec. */
		_exception(SIGILL, regs, ILL_ILLOPC, regs->nip);
		goto bail;
	}

	printk(KERN_EMERG "Unrecoverable VMX/Altivec Unavailable Exception "
			"%lx at %lx\n", regs->trap, regs->nip);
	die("Unrecoverable VMX/Altivec Unavailable Exception", regs, SIGABRT);

bail:
	exception_exit(prev_state);
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

#ifdef CONFIG_PPC64
static void tm_unavailable(struct pt_regs *regs)
{
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	if (user_mode(regs)) {
		current->thread.load_tm++;
		regs->msr |= MSR_TM;
		tm_enable();
		tm_restore_sprs(&current->thread);
		return;
	}
#endif
	pr_emerg("Unrecoverable TM Unavailable Exception "
			"%lx at %lx\n", regs->trap, regs->nip);
	die("Unrecoverable TM Unavailable Exception", regs, SIGABRT);
}

void facility_unavailable_exception(struct pt_regs *regs)
{
	static char *facility_strings[] = {
		[FSCR_FP_LG] = "FPU",
		[FSCR_VECVSX_LG] = "VMX/VSX",
		[FSCR_DSCR_LG] = "DSCR",
		[FSCR_PM_LG] = "PMU SPRs",
		[FSCR_BHRB_LG] = "BHRB",
		[FSCR_TM_LG] = "TM",
		[FSCR_EBB_LG] = "EBB",
		[FSCR_TAR_LG] = "TAR",
		[FSCR_MSGP_LG] = "MSGP",
		[FSCR_SCV_LG] = "SCV",
	};
	char *facility = "unknown";
	u64 value;
	u32 instword, rd;
	u8 status;
	bool hv;

	hv = (TRAP(regs) == 0xf80);
	if (hv)
		value = mfspr(SPRN_HFSCR);
	else
		value = mfspr(SPRN_FSCR);

	status = value >> 56;
	if ((hv || status >= 2) &&
	    (status < ARRAY_SIZE(facility_strings)) &&
	    facility_strings[status])
		facility = facility_strings[status];

	/* We should not have taken this interrupt in kernel */
	if (!user_mode(regs)) {
		pr_emerg("Facility '%s' unavailable (%d) exception in kernel mode at %lx\n",
			 facility, status, regs->nip);
		die("Unexpected facility unavailable exception", regs, SIGABRT);
	}

	/* We restore the interrupt state now */
	if (!arch_irq_disabled_regs(regs))
		local_irq_enable();

	if (status == FSCR_DSCR_LG) {
		/*
		 * User is accessing the DSCR register using the problem
		 * state only SPR number (0x03) either through a mfspr or
		 * a mtspr instruction. If it is a write attempt through
		 * a mtspr, then we set the inherit bit. This also allows
		 * the user to write or read the register directly in the
		 * future by setting via the FSCR DSCR bit. But in case it
		 * is a read DSCR attempt through a mfspr instruction, we
		 * just emulate the instruction instead. This code path will
		 * always emulate all the mfspr instructions till the user
		 * has attempted at least one mtspr instruction. This way it
		 * preserves the same behaviour when the user is accessing
		 * the DSCR through privilege level only SPR number (0x11)
		 * which is emulated through illegal instruction exception.
		 * We always leave HFSCR DSCR set.
		 */
		if (get_user(instword, (u32 __user *)(regs->nip))) {
			pr_err("Failed to fetch the user instruction\n");
			return;
		}

		/* Write into DSCR (mtspr 0x03, RS) */
		if ((instword & PPC_INST_MTSPR_DSCR_USER_MASK)
				== PPC_INST_MTSPR_DSCR_USER) {
			rd = (instword >> 21) & 0x1f;
			current->thread.dscr = regs->gpr[rd];
			current->thread.dscr_inherit = 1;
			current->thread.fscr |= FSCR_DSCR;
			mtspr(SPRN_FSCR, current->thread.fscr);
		}

		/* Read from DSCR (mfspr RT, 0x03) */
		if ((instword & PPC_INST_MFSPR_DSCR_USER_MASK)
				== PPC_INST_MFSPR_DSCR_USER) {
			if (emulate_instruction(regs)) {
				pr_err("DSCR based mfspr emulation failed\n");
				return;
			}
			regs->nip += 4;
			emulate_single_step(regs);
		}
		return;
	}

	if (status == FSCR_TM_LG) {
		/*
		 * If we're here then the hardware is TM aware because it
		 * generated an exception with FSRM_TM set.
		 *
		 * If cpu_has_feature(CPU_FTR_TM) is false, then either firmware
		 * told us not to do TM, or the kernel is not built with TM
		 * support.
		 *
		 * If both of those things are true, then userspace can spam the
		 * console by triggering the printk() below just by continually
		 * doing tbegin (or any TM instruction). So in that case just
		 * send the process a SIGILL immediately.
		 */
		if (!cpu_has_feature(CPU_FTR_TM))
			goto out;

		tm_unavailable(regs);
		return;
	}

	pr_err_ratelimited("%sFacility '%s' unavailable (%d), exception at 0x%lx, MSR=%lx\n",
		hv ? "Hypervisor " : "", facility, status, regs->nip, regs->msr);

out:
	_exception(SIGILL, regs, ILL_ILLOPC, regs->nip);
}
#endif

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM

void fp_unavailable_tm(struct pt_regs *regs)
{
	/* Note:  This does not handle any kind of FP laziness. */

	TM_DEBUG("FP Unavailable trap whilst transactional at 0x%lx, MSR=%lx\n",
		 regs->nip, regs->msr);

        /* We can only have got here if the task started using FP after
         * beginning the transaction.  So, the transactional regs are just a
         * copy of the checkpointed ones.  But, we still need to recheckpoint
         * as we're enabling FP for the process; it will return, abort the
         * transaction, and probably retry but now with FP enabled.  So the
         * checkpointed FP registers need to be loaded.
	 */
	tm_reclaim_current(TM_CAUSE_FAC_UNAV);

	/*
	 * Reclaim initially saved out bogus (lazy) FPRs to ckfp_state, and
	 * then it was overwrite by the thr->fp_state by tm_reclaim_thread().
	 *
	 * At this point, ck{fp,vr}_state contains the exact values we want to
	 * recheckpoint.
	 */

	/* Enable FP for the task: */
	current->thread.load_fp = 1;

	/*
	 * Recheckpoint all the checkpointed ckpt, ck{fp, vr}_state registers.
	 */
	tm_recheckpoint(&current->thread);
}

void altivec_unavailable_tm(struct pt_regs *regs)
{
	/* See the comments in fp_unavailable_tm().  This function operates
	 * the same way.
	 */

	TM_DEBUG("Vector Unavailable trap whilst transactional at 0x%lx,"
		 "MSR=%lx\n",
		 regs->nip, regs->msr);
	tm_reclaim_current(TM_CAUSE_FAC_UNAV);
	current->thread.load_vec = 1;
	tm_recheckpoint(&current->thread);
	current->thread.used_vr = 1;
}

void vsx_unavailable_tm(struct pt_regs *regs)
{
	/* See the comments in fp_unavailable_tm().  This works similarly,
	 * though we're loading both FP and VEC registers in here.
	 *
	 * If FP isn't in use, load FP regs.  If VEC isn't in use, load VEC
	 * regs.  Either way, set MSR_VSX.
	 */

	TM_DEBUG("VSX Unavailable trap whilst transactional at 0x%lx,"
		 "MSR=%lx\n",
		 regs->nip, regs->msr);

	current->thread.used_vsr = 1;

	/* This reclaims FP and/or VR regs if they're already enabled */
	tm_reclaim_current(TM_CAUSE_FAC_UNAV);

	current->thread.load_vec = 1;
	current->thread.load_fp = 1;

	tm_recheckpoint(&current->thread);
}
#endif /* CONFIG_PPC_TRANSACTIONAL_MEM */

void performance_monitor_exception(struct pt_regs *regs)
{
	__this_cpu_inc(irq_stat.pmu_irqs);

	perf_irq(regs);
}

#ifdef CONFIG_PPC_ADV_DEBUG_REGS
static void handle_debug(struct pt_regs *regs, unsigned long debug_status)
{
	int changed = 0;
	/*
	 * Determine the cause of the debug event, clear the
	 * event flags and send a trap to the handler. Torez
	 */
	if (debug_status & (DBSR_DAC1R | DBSR_DAC1W)) {
		dbcr_dac(current) &= ~(DBCR_DAC1R | DBCR_DAC1W);
#ifdef CONFIG_PPC_ADV_DEBUG_DAC_RANGE
		current->thread.debug.dbcr2 &= ~DBCR2_DAC12MODE;
#endif
		do_send_trap(regs, mfspr(SPRN_DAC1), debug_status,
			     5);
		changed |= 0x01;
	}  else if (debug_status & (DBSR_DAC2R | DBSR_DAC2W)) {
		dbcr_dac(current) &= ~(DBCR_DAC2R | DBCR_DAC2W);
		do_send_trap(regs, mfspr(SPRN_DAC2), debug_status,
			     6);
		changed |= 0x01;
	}  else if (debug_status & DBSR_IAC1) {
		current->thread.debug.dbcr0 &= ~DBCR0_IAC1;
		dbcr_iac_range(current) &= ~DBCR_IAC12MODE;
		do_send_trap(regs, mfspr(SPRN_IAC1), debug_status,
			     1);
		changed |= 0x01;
	}  else if (debug_status & DBSR_IAC2) {
		current->thread.debug.dbcr0 &= ~DBCR0_IAC2;
		do_send_trap(regs, mfspr(SPRN_IAC2), debug_status,
			     2);
		changed |= 0x01;
	}  else if (debug_status & DBSR_IAC3) {
		current->thread.debug.dbcr0 &= ~DBCR0_IAC3;
		dbcr_iac_range(current) &= ~DBCR_IAC34MODE;
		do_send_trap(regs, mfspr(SPRN_IAC3), debug_status,
			     3);
		changed |= 0x01;
	}  else if (debug_status & DBSR_IAC4) {
		current->thread.debug.dbcr0 &= ~DBCR0_IAC4;
		do_send_trap(regs, mfspr(SPRN_IAC4), debug_status,
			     4);
		changed |= 0x01;
	}
	/*
	 * At the point this routine was called, the MSR(DE) was turned off.
	 * Check all other debug flags and see if that bit needs to be turned
	 * back on or not.
	 */
	if (DBCR_ACTIVE_EVENTS(current->thread.debug.dbcr0,
			       current->thread.debug.dbcr1))
		regs->msr |= MSR_DE;
	else
		/* Make sure the IDM flag is off */
		current->thread.debug.dbcr0 &= ~DBCR0_IDM;

	if (changed & 0x01)
		mtspr(SPRN_DBCR0, current->thread.debug.dbcr0);
}

void DebugException(struct pt_regs *regs, unsigned long debug_status)
{
	current->thread.debug.dbsr = debug_status;

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
			current->thread.debug.dbcr0 &= ~DBCR0_BT;
			current->thread.debug.dbcr0 |= DBCR0_IDM | DBCR0_IC;
			regs->msr |= MSR_DE;
			return;
		}

		if (kprobe_post_handler(regs))
			return;

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

		if (kprobe_post_handler(regs))
			return;

		if (notify_die(DIE_SSTEP, "single_step", regs, 5,
			       5, SIGTRAP) == NOTIFY_STOP) {
			return;
		}

		if (debugger_sstep(regs))
			return;

		if (user_mode(regs)) {
			current->thread.debug.dbcr0 &= ~DBCR0_IC;
			if (DBCR_ACTIVE_EVENTS(current->thread.debug.dbcr0,
					       current->thread.debug.dbcr1))
				regs->msr |= MSR_DE;
			else
				/* Make sure the IDM bit is off */
				current->thread.debug.dbcr0 &= ~DBCR0_IDM;
		}

		_exception(SIGTRAP, regs, TRAP_TRACE, regs->nip);
	} else
		handle_debug(regs, debug_status);
}
NOKPROBE_SYMBOL(DebugException);
#endif /* CONFIG_PPC_ADV_DEBUG_REGS */

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

	PPC_WARN_EMULATED(altivec, regs);
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
		printk_ratelimited(KERN_ERR "Unrecognized altivec instruction "
				   "in %s at %lx\n", current->comm, regs->nip);
		current->thread.vr_state.vscr.u[3] |= 0x10000;
	}
}
#endif /* CONFIG_ALTIVEC */

#ifdef CONFIG_FSL_BOOKE
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
	int code = FPE_FLTUNK;
	int err;

	flush_spe_to_thread(current);

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
		_exception(SIGFPE, regs, FPE_FLTUNK, regs->nip);
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
	pr_emerg("Unrecoverable exception %lx at %lx (msr=%lx)\n",
		 regs->trap, regs->nip, regs->msr);
	die("Unrecoverable exception", regs, SIGABRT);
}
NOKPROBE_SYMBOL(unrecoverable_exception);

#if defined(CONFIG_BOOKE_WDT) || defined(CONFIG_40x)
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
NOKPROBE_SYMBOL(kernel_bad_stack);

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
	WARN_EMULATED_SETUP(sync),
	WARN_EMULATED_SETUP(unaligned),
#ifdef CONFIG_MATH_EMULATION
	WARN_EMULATED_SETUP(math),
#endif
#ifdef CONFIG_VSX
	WARN_EMULATED_SETUP(vsx),
#endif
#ifdef CONFIG_PPC64
	WARN_EMULATED_SETUP(mfdscr),
	WARN_EMULATED_SETUP(mtdscr),
	WARN_EMULATED_SETUP(lq_stq),
	WARN_EMULATED_SETUP(lxvw4x),
	WARN_EMULATED_SETUP(lxvh8x),
	WARN_EMULATED_SETUP(lxvd2x),
	WARN_EMULATED_SETUP(lxvb16x),
#endif
};

u32 ppc_warn_emulated;

void ppc_warn_emulated_print(const char *type)
{
	pr_warn_ratelimited("%s used emulated %s instruction\n", current->comm,
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

	d = debugfs_create_u32("do_warn", 0644, dir,
			       &ppc_warn_emulated);
	if (!d)
		goto fail;

	for (i = 0; i < sizeof(ppc_emulated)/sizeof(*entries); i++) {
		d = debugfs_create_u32(entries[i].name, 0644, dir,
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
