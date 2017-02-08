/*
 *  Meta exception handling.
 *
 *  Copyright (C) 2005,2006,2007,2008,2009,2012 Imagination Technologies Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/export.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/signal.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/preempt.h>
#include <linux/ptrace.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/kdebug.h>
#include <linux/kexec.h>
#include <linux/unistd.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <linux/syscalls.h>

#include <asm/bug.h>
#include <asm/core_reg.h>
#include <asm/irqflags.h>
#include <asm/siginfo.h>
#include <asm/traps.h>
#include <asm/hwthread.h>
#include <asm/setup.h>
#include <asm/switch.h>
#include <asm/user_gateway.h>
#include <asm/syscall.h>
#include <asm/syscalls.h>

/* Passing syscall arguments as long long is quicker. */
typedef unsigned int (*LPSYSCALL) (unsigned long long,
				   unsigned long long,
				   unsigned long long);

/*
 * Users of LNKSET should compare the bus error bits obtained from DEFR
 * against TXDEFR_LNKSET_SUCCESS only as the failure code will vary between
 * different cores revisions.
 */
#define TXDEFR_LNKSET_SUCCESS 0x02000000
#define TXDEFR_LNKSET_FAILURE 0x04000000

/*
 * Our global TBI handle.  Initialised from setup.c/setup_arch.
 */
DECLARE_PER_CPU(PTBI, pTBI);

#ifdef CONFIG_SMP
static DEFINE_PER_CPU(unsigned int, trigger_mask);
#else
unsigned int global_trigger_mask;
EXPORT_SYMBOL(global_trigger_mask);
#endif

unsigned long per_cpu__stack_save[NR_CPUS];

static const char * const trap_names[] = {
	[TBIXXF_SIGNUM_IIF] = "Illegal instruction fault",
	[TBIXXF_SIGNUM_PGF] = "Privilege violation",
	[TBIXXF_SIGNUM_DHF] = "Unaligned data access fault",
	[TBIXXF_SIGNUM_IGF] = "Code fetch general read failure",
	[TBIXXF_SIGNUM_DGF] = "Data access general read/write fault",
	[TBIXXF_SIGNUM_IPF] = "Code fetch page fault",
	[TBIXXF_SIGNUM_DPF] = "Data access page fault",
	[TBIXXF_SIGNUM_IHF] = "Instruction breakpoint",
	[TBIXXF_SIGNUM_DWF] = "Read-only data access fault",
};

const char *trap_name(int trapno)
{
	if (trapno >= 0 && trapno < ARRAY_SIZE(trap_names)
			&& trap_names[trapno])
		return trap_names[trapno];
	return "Unknown fault";
}

static DEFINE_SPINLOCK(die_lock);

void __noreturn die(const char *str, struct pt_regs *regs,
		    long err, unsigned long addr)
{
	static int die_counter;

	oops_enter();

	spin_lock_irq(&die_lock);
	console_verbose();
	bust_spinlocks(1);
	pr_err("%s: err %04lx (%s) addr %08lx [#%d]\n", str, err & 0xffff,
	       trap_name(err & 0xffff), addr, ++die_counter);

	print_modules();
	show_regs(regs);

	pr_err("Process: %s (pid: %d, stack limit = %p)\n", current->comm,
	       task_pid_nr(current), task_stack_page(current) + THREAD_SIZE);

	bust_spinlocks(0);
	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);
	if (kexec_should_crash(current))
		crash_kexec(regs);

	if (in_interrupt())
		panic("Fatal exception in interrupt");

	if (panic_on_oops)
		panic("Fatal exception");

	spin_unlock_irq(&die_lock);
	oops_exit();
	do_exit(SIGSEGV);
}

#ifdef CONFIG_METAG_DSP
/*
 * The ECH encoding specifies the size of a DSPRAM as,
 *
 *		"slots" / 4
 *
 * A "slot" is the size of two DSPRAM bank entries; an entry from
 * DSPRAM bank A and an entry from DSPRAM bank B. One DSPRAM bank
 * entry is 4 bytes.
 */
#define SLOT_SZ	8
static inline unsigned int decode_dspram_size(unsigned int size)
{
	unsigned int _sz = size & 0x7f;

	return _sz * SLOT_SZ * 4;
}

static void dspram_save(struct meta_ext_context *dsp_ctx,
			unsigned int ramA_sz, unsigned int ramB_sz)
{
	unsigned int ram_sz[2];
	int i;

	ram_sz[0] = ramA_sz;
	ram_sz[1] = ramB_sz;

	for (i = 0; i < 2; i++) {
		if (ram_sz[i] != 0) {
			unsigned int sz;

			if (i == 0)
				sz = decode_dspram_size(ram_sz[i] >> 8);
			else
				sz = decode_dspram_size(ram_sz[i]);

			if (dsp_ctx->ram[i] == NULL) {
				dsp_ctx->ram[i] = kmalloc(sz, GFP_KERNEL);

				if (dsp_ctx->ram[i] == NULL)
					panic("couldn't save DSP context");
			} else {
				if (ram_sz[i] > dsp_ctx->ram_sz[i]) {
					kfree(dsp_ctx->ram[i]);

					dsp_ctx->ram[i] = kmalloc(sz,
								  GFP_KERNEL);

					if (dsp_ctx->ram[i] == NULL)
						panic("couldn't save DSP context");
				}
			}

			if (i == 0)
				__TBIDspramSaveA(ram_sz[i], dsp_ctx->ram[i]);
			else
				__TBIDspramSaveB(ram_sz[i], dsp_ctx->ram[i]);

			dsp_ctx->ram_sz[i] = ram_sz[i];
		}
	}
}
#endif /* CONFIG_METAG_DSP */

/*
 * Allow interrupts to be nested and save any "extended" register
 * context state, e.g. DSP regs and RAMs.
 */
static void nest_interrupts(TBIRES State, unsigned long mask)
{
#ifdef CONFIG_METAG_DSP
	struct meta_ext_context *dsp_ctx;
	unsigned int D0_8;

	/*
	 * D0.8 may contain an ECH encoding. The upper 16 bits
	 * tell us what DSP resources the current process is
	 * using. OR the bits into the SaveMask so that
	 * __TBINestInts() knows what resources to save as
	 * part of this context.
	 *
	 * Don't save the context if we're nesting interrupts in the
	 * kernel because the kernel doesn't use DSP hardware.
	 */
	D0_8 = __core_reg_get(D0.8);

	if (D0_8 && (State.Sig.SaveMask & TBICTX_PRIV_BIT)) {
		State.Sig.SaveMask |= (D0_8 >> 16);

		dsp_ctx = current->thread.dsp_context;
		if (dsp_ctx == NULL) {
			dsp_ctx = kzalloc(sizeof(*dsp_ctx), GFP_KERNEL);
			if (dsp_ctx == NULL)
				panic("couldn't save DSP context: ENOMEM");

			current->thread.dsp_context = dsp_ctx;
		}

		current->thread.user_flags |= (D0_8 & 0xffff0000);
		__TBINestInts(State, &dsp_ctx->regs, mask);
		dspram_save(dsp_ctx, D0_8 & 0x7f00, D0_8 & 0x007f);
	} else
		__TBINestInts(State, NULL, mask);
#else
	__TBINestInts(State, NULL, mask);
#endif
}

void head_end(TBIRES State, unsigned long mask)
{
	unsigned int savemask = (unsigned short)State.Sig.SaveMask;
	unsigned int ctx_savemask = (unsigned short)State.Sig.pCtx->SaveMask;

	if (savemask & TBICTX_PRIV_BIT) {
		ctx_savemask |= TBICTX_PRIV_BIT;
		current->thread.user_flags = savemask;
	}

	/* Always undo the sleep bit */
	ctx_savemask &= ~TBICTX_WAIT_BIT;

	/* Always save the catch buffer and RD pipe if they are dirty */
	savemask |= TBICTX_XCBF_BIT;

	/* Only save the catch and RD if we have not already done so.
	 * Note - the RD bits are in the pCtx only, and not in the
	 * State.SaveMask.
	 */
	if ((savemask & TBICTX_CBUF_BIT) ||
	    (ctx_savemask & TBICTX_CBRP_BIT)) {
		/* Have we already saved the buffers though?
		 * - See TestTrack 5071 */
		if (ctx_savemask & TBICTX_XCBF_BIT) {
			/* Strip off the bits so the call to __TBINestInts
			 * won't save the buffers again. */
			savemask &= ~TBICTX_CBUF_BIT;
			ctx_savemask &= ~TBICTX_CBRP_BIT;
		}
	}

#ifdef CONFIG_METAG_META21
	{
		unsigned int depth, txdefr;

		/*
		 * Save TXDEFR state.
		 *
		 * The process may have been interrupted after a LNKSET, but
		 * before it could read the DEFR state, so we mustn't lose that
		 * state or it could end up retrying an atomic operation that
		 * succeeded.
		 *
		 * All interrupts are disabled at this point so we
		 * don't need to perform any locking. We must do this
		 * dance before we use LNKGET or LNKSET.
		 */
		BUG_ON(current->thread.int_depth > HARDIRQ_BITS);

		depth = current->thread.int_depth++;

		txdefr = __core_reg_get(TXDEFR);

		txdefr &= TXDEFR_BUS_STATE_BITS;
		if (txdefr & TXDEFR_LNKSET_SUCCESS)
			current->thread.txdefr_failure &= ~(1 << depth);
		else
			current->thread.txdefr_failure |= (1 << depth);
	}
#endif

	State.Sig.SaveMask = savemask;
	State.Sig.pCtx->SaveMask = ctx_savemask;

	nest_interrupts(State, mask);

#ifdef CONFIG_METAG_POISON_CATCH_BUFFERS
	/* Poison the catch registers.  This shows up any mistakes we have
	 * made in their handling MUCH quicker.
	 */
	__core_reg_set(TXCATCH0, 0x87650021);
	__core_reg_set(TXCATCH1, 0x87654322);
	__core_reg_set(TXCATCH2, 0x87654323);
	__core_reg_set(TXCATCH3, 0x87654324);
#endif /* CONFIG_METAG_POISON_CATCH_BUFFERS */
}

TBIRES tail_end_sys(TBIRES State, int syscall, int *restart)
{
	struct pt_regs *regs = (struct pt_regs *)State.Sig.pCtx;
	unsigned long flags;

	local_irq_disable();

	if (user_mode(regs)) {
		flags = current_thread_info()->flags;
		if (flags & _TIF_WORK_MASK &&
		    do_work_pending(regs, flags, syscall)) {
			*restart = 1;
			return State;
		}

#ifdef CONFIG_METAG_FPU
		if (current->thread.fpu_context &&
		    current->thread.fpu_context->needs_restore) {
			__TBICtxFPURestore(State, current->thread.fpu_context);
			/*
			 * Clearing this bit ensures the FP unit is not made
			 * active again unless it is used.
			 */
			State.Sig.SaveMask &= ~TBICTX_FPAC_BIT;
			current->thread.fpu_context->needs_restore = false;
		}
		State.Sig.TrigMask |= TBI_TRIG_BIT(TBID_SIGNUM_DFR);
#endif
	}

	/* TBI will turn interrupts back on at some point. */
	if (!irqs_disabled_flags((unsigned long)State.Sig.TrigMask))
		trace_hardirqs_on();

#ifdef CONFIG_METAG_DSP
	/*
	 * If we previously saved an extended context then restore it
	 * now. Otherwise, clear D0.8 because this process is not
	 * using DSP hardware.
	 */
	if (State.Sig.pCtx->SaveMask & TBICTX_XEXT_BIT) {
		unsigned int D0_8;
		struct meta_ext_context *dsp_ctx = current->thread.dsp_context;

		/* Make sure we're going to return to userland. */
		BUG_ON(current->thread.int_depth != 1);

		if (dsp_ctx->ram_sz[0] > 0)
			__TBIDspramRestoreA(dsp_ctx->ram_sz[0],
					    dsp_ctx->ram[0]);
		if (dsp_ctx->ram_sz[1] > 0)
			__TBIDspramRestoreB(dsp_ctx->ram_sz[1],
					    dsp_ctx->ram[1]);

		State.Sig.SaveMask |= State.Sig.pCtx->SaveMask;
		__TBICtxRestore(State, current->thread.dsp_context);
		D0_8 = __core_reg_get(D0.8);
		D0_8 |= current->thread.user_flags & 0xffff0000;
		D0_8 |= (dsp_ctx->ram_sz[1] | dsp_ctx->ram_sz[0]) & 0xffff;
		__core_reg_set(D0.8, D0_8);
	} else
		__core_reg_set(D0.8, 0);
#endif /* CONFIG_METAG_DSP */

#ifdef CONFIG_METAG_META21
	{
		unsigned int depth, txdefr;

		/*
		 * If there hasn't been a LNKSET since the last LNKGET then the
		 * link flag will be set, causing the next LNKSET to succeed if
		 * the addresses match. The two LNK operations may not be a pair
		 * (e.g. see atomic_read()), so the LNKSET should fail.
		 * We use a conditional-never LNKSET to clear the link flag
		 * without side effects.
		 */
		asm volatile("LNKSETDNV [D0Re0],D0Re0");

		depth = --current->thread.int_depth;

		BUG_ON(user_mode(regs) && depth);

		txdefr = __core_reg_get(TXDEFR);

		txdefr &= ~TXDEFR_BUS_STATE_BITS;

		/* Do we need to restore a failure code into TXDEFR? */
		if (current->thread.txdefr_failure & (1 << depth))
			txdefr |= (TXDEFR_LNKSET_FAILURE | TXDEFR_BUS_TRIG_BIT);
		else
			txdefr |= (TXDEFR_LNKSET_SUCCESS | TXDEFR_BUS_TRIG_BIT);

		__core_reg_set(TXDEFR, txdefr);
	}
#endif
	return State;
}

#ifdef CONFIG_SMP
/*
 * If we took an interrupt in the middle of __kuser_get_tls then we need
 * to rewind the PC to the start of the function in case the process
 * gets migrated to another thread (SMP only) and it reads the wrong tls
 * data.
 */
static inline void _restart_critical_section(TBIRES State)
{
	unsigned long get_tls_start;
	unsigned long get_tls_end;

	get_tls_start = (unsigned long)__kuser_get_tls -
		(unsigned long)&__user_gateway_start;

	get_tls_start += USER_GATEWAY_PAGE;

	get_tls_end = (unsigned long)__kuser_get_tls_end -
		(unsigned long)&__user_gateway_start;

	get_tls_end += USER_GATEWAY_PAGE;

	if ((State.Sig.pCtx->CurrPC >= get_tls_start) &&
	    (State.Sig.pCtx->CurrPC < get_tls_end))
		State.Sig.pCtx->CurrPC = get_tls_start;
}
#else
/*
 * If we took an interrupt in the middle of
 * __kuser_cmpxchg then we need to rewind the PC to the
 * start of the function.
 */
static inline void _restart_critical_section(TBIRES State)
{
	unsigned long cmpxchg_start;
	unsigned long cmpxchg_end;

	cmpxchg_start = (unsigned long)__kuser_cmpxchg -
		(unsigned long)&__user_gateway_start;

	cmpxchg_start += USER_GATEWAY_PAGE;

	cmpxchg_end = (unsigned long)__kuser_cmpxchg_end -
		(unsigned long)&__user_gateway_start;

	cmpxchg_end += USER_GATEWAY_PAGE;

	if ((State.Sig.pCtx->CurrPC >= cmpxchg_start) &&
	    (State.Sig.pCtx->CurrPC < cmpxchg_end))
		State.Sig.pCtx->CurrPC = cmpxchg_start;
}
#endif

/* Used by kick_handler() */
void restart_critical_section(TBIRES State)
{
	_restart_critical_section(State);
}

TBIRES trigger_handler(TBIRES State, int SigNum, int Triggers, int Inst,
		       PTBI pTBI)
{
	head_end(State, ~INTS_OFF_MASK);

	/* If we interrupted user code handle any critical sections. */
	if (State.Sig.SaveMask & TBICTX_PRIV_BIT)
		_restart_critical_section(State);

	trace_hardirqs_off();

	do_IRQ(SigNum, (struct pt_regs *)State.Sig.pCtx);

	return tail_end(State);
}

static unsigned int load_fault(PTBICTXEXTCB0 pbuf)
{
	return pbuf->CBFlags & TXCATCH0_READ_BIT;
}

static unsigned long fault_address(PTBICTXEXTCB0 pbuf)
{
	return pbuf->CBAddr;
}

static void unhandled_fault(struct pt_regs *regs, unsigned long addr,
			    int signo, int code, int trapno)
{
	if (user_mode(regs)) {
		siginfo_t info;

		if (show_unhandled_signals && unhandled_signal(current, signo)
		    && printk_ratelimit()) {

			pr_info("pid %d unhandled fault: pc 0x%08x, addr 0x%08lx, trap %d (%s)\n",
				current->pid, regs->ctx.CurrPC, addr,
				trapno, trap_name(trapno));
			print_vma_addr(" in ", regs->ctx.CurrPC);
			print_vma_addr(" rtp in ", regs->ctx.DX[4].U1);
			printk("\n");
			show_regs(regs);
		}

		info.si_signo = signo;
		info.si_errno = 0;
		info.si_code = code;
		info.si_addr = (__force void __user *)addr;
		info.si_trapno = trapno;
		force_sig_info(signo, &info, current);
	} else {
		die("Oops", regs, trapno, addr);
	}
}

static int handle_data_fault(PTBICTXEXTCB0 pcbuf, struct pt_regs *regs,
			     unsigned int data_address, int trapno)
{
	int ret;

	ret = do_page_fault(regs, data_address, !load_fault(pcbuf), trapno);

	return ret;
}

static unsigned long get_inst_fault_address(struct pt_regs *regs)
{
	return regs->ctx.CurrPC;
}

TBIRES fault_handler(TBIRES State, int SigNum, int Triggers,
		     int Inst, PTBI pTBI)
{
	struct pt_regs *regs = (struct pt_regs *)State.Sig.pCtx;
	PTBICTXEXTCB0 pcbuf = (PTBICTXEXTCB0)&regs->extcb0;
	unsigned long data_address;

	head_end(State, ~INTS_OFF_MASK);

	/* Hardware breakpoint or data watch */
	if ((SigNum == TBIXXF_SIGNUM_IHF) ||
	    ((SigNum == TBIXXF_SIGNUM_DHF) &&
	     (pcbuf[0].CBFlags & (TXCATCH0_WATCH1_BIT |
				  TXCATCH0_WATCH0_BIT)))) {
		State = __TBIUnExpXXX(State, SigNum, Triggers, Inst,
				      pTBI);
		return tail_end(State);
	}

	local_irq_enable();

	data_address = fault_address(pcbuf);

	switch (SigNum) {
	case TBIXXF_SIGNUM_IGF:
		/* 1st-level entry invalid (instruction fetch) */
	case TBIXXF_SIGNUM_IPF: {
		/* 2nd-level entry invalid (instruction fetch) */
		unsigned long addr = get_inst_fault_address(regs);
		do_page_fault(regs, addr, 0, SigNum);
		break;
	}

	case TBIXXF_SIGNUM_DGF:
		/* 1st-level entry invalid (data access) */
	case TBIXXF_SIGNUM_DPF:
		/* 2nd-level entry invalid (data access) */
	case TBIXXF_SIGNUM_DWF:
		/* Write to read only page */
		handle_data_fault(pcbuf, regs, data_address, SigNum);
		break;

	case TBIXXF_SIGNUM_IIF:
		/* Illegal instruction */
		unhandled_fault(regs, regs->ctx.CurrPC, SIGILL, ILL_ILLOPC,
				SigNum);
		break;

	case TBIXXF_SIGNUM_DHF:
		/* Unaligned access */
		unhandled_fault(regs, data_address, SIGBUS, BUS_ADRALN,
				SigNum);
		break;
	case TBIXXF_SIGNUM_PGF:
		/* Privilege violation */
		unhandled_fault(regs, data_address, SIGSEGV, SEGV_ACCERR,
				SigNum);
		break;
	default:
		BUG();
		break;
	}

	return tail_end(State);
}

static bool switch_is_syscall(unsigned int inst)
{
	return inst == __METAG_SW_ENCODING(SYS);
}

static bool switch_is_legacy_syscall(unsigned int inst)
{
	return inst == __METAG_SW_ENCODING(SYS_LEGACY);
}

static inline void step_over_switch(struct pt_regs *regs, unsigned int inst)
{
	regs->ctx.CurrPC += 4;
}

static inline int test_syscall_work(void)
{
	return current_thread_info()->flags & _TIF_WORK_SYSCALL_MASK;
}

TBIRES switch1_handler(TBIRES State, int SigNum, int Triggers,
		       int Inst, PTBI pTBI)
{
	struct pt_regs *regs = (struct pt_regs *)State.Sig.pCtx;
	unsigned int sysnumber;
	unsigned long long a1_a2, a3_a4, a5_a6;
	LPSYSCALL syscall_entry;
	int restart;

	head_end(State, ~INTS_OFF_MASK);

	/*
	 * If this is not a syscall SWITCH it could be a breakpoint.
	 */
	if (!switch_is_syscall(Inst)) {
		/*
		 * Alert the user if they're trying to use legacy system
		 * calls. This suggests they need to update their C
		 * library and build against up to date kernel headers.
		 */
		if (switch_is_legacy_syscall(Inst))
			pr_warn_once("WARNING: A legacy syscall was made. Your userland needs updating.\n");
		/*
		 * We don't know how to handle the SWITCH and cannot
		 * safely ignore it, so treat all unknown switches
		 * (including breakpoints) as traps.
		 */
		force_sig(SIGTRAP, current);
		return tail_end(State);
	}

	local_irq_enable();

restart_syscall:
	restart = 0;
	sysnumber = regs->ctx.DX[0].U1;

	if (test_syscall_work())
		sysnumber = syscall_trace_enter(regs);

	/* Skip over the SWITCH instruction - or you just get 'stuck' on it! */
	step_over_switch(regs, Inst);

	if (sysnumber >= __NR_syscalls) {
		pr_debug("unknown syscall number: %d\n", sysnumber);
		syscall_entry = (LPSYSCALL) sys_ni_syscall;
	} else {
		syscall_entry = (LPSYSCALL) sys_call_table[sysnumber];
	}

	/* Use 64bit loads for speed. */
	a5_a6 = *(unsigned long long *)&regs->ctx.DX[1];
	a3_a4 = *(unsigned long long *)&regs->ctx.DX[2];
	a1_a2 = *(unsigned long long *)&regs->ctx.DX[3];

	/* here is the actual call to the syscall handler functions */
	regs->ctx.DX[0].U0 = syscall_entry(a1_a2, a3_a4, a5_a6);

	if (test_syscall_work())
		syscall_trace_leave(regs);

	State = tail_end_sys(State, sysnumber, &restart);
	/* Handlerless restarts shouldn't go via userland */
	if (restart)
		goto restart_syscall;
	return State;
}

TBIRES switchx_handler(TBIRES State, int SigNum, int Triggers,
		       int Inst, PTBI pTBI)
{
	struct pt_regs *regs = (struct pt_regs *)State.Sig.pCtx;

	/*
	 * This can be caused by any user process simply executing an unusual
	 * SWITCH instruction. If there's no DA, __TBIUnExpXXX will cause the
	 * thread to stop, so signal a SIGTRAP instead.
	 */
	head_end(State, ~INTS_OFF_MASK);
	if (user_mode(regs))
		force_sig(SIGTRAP, current);
	else
		State = __TBIUnExpXXX(State, SigNum, Triggers, Inst, pTBI);
	return tail_end(State);
}

#ifdef CONFIG_METAG_META21
TBIRES fpe_handler(TBIRES State, int SigNum, int Triggers, int Inst, PTBI pTBI)
{
	struct pt_regs *regs = (struct pt_regs *)State.Sig.pCtx;
	unsigned int error_state = Triggers;
	siginfo_t info;

	head_end(State, ~INTS_OFF_MASK);

	local_irq_enable();

	info.si_signo = SIGFPE;

	if (error_state & TXSTAT_FPE_INVALID_BIT)
		info.si_code = FPE_FLTINV;
	else if (error_state & TXSTAT_FPE_DIVBYZERO_BIT)
		info.si_code = FPE_FLTDIV;
	else if (error_state & TXSTAT_FPE_OVERFLOW_BIT)
		info.si_code = FPE_FLTOVF;
	else if (error_state & TXSTAT_FPE_UNDERFLOW_BIT)
		info.si_code = FPE_FLTUND;
	else if (error_state & TXSTAT_FPE_INEXACT_BIT)
		info.si_code = FPE_FLTRES;
	else
		info.si_code = 0;
	info.si_errno = 0;
	info.si_addr = (__force void __user *)regs->ctx.CurrPC;
	force_sig_info(SIGFPE, &info, current);

	return tail_end(State);
}
#endif

#ifdef CONFIG_METAG_SUSPEND_MEM
struct traps_context {
	PTBIAPIFN fnSigs[TBID_SIGNUM_MAX + 1];
};

static struct traps_context *metag_traps_context;

int traps_save_context(void)
{
	unsigned long cpu = smp_processor_id();
	PTBI _pTBI = per_cpu(pTBI, cpu);
	struct traps_context *context;

	context = kzalloc(sizeof(*context), GFP_ATOMIC);
	if (!context)
		return -ENOMEM;

	memcpy(context->fnSigs, (void *)_pTBI->fnSigs, sizeof(context->fnSigs));

	metag_traps_context = context;
	return 0;
}

int traps_restore_context(void)
{
	unsigned long cpu = smp_processor_id();
	PTBI _pTBI = per_cpu(pTBI, cpu);
	struct traps_context *context = metag_traps_context;

	metag_traps_context = NULL;

	memcpy((void *)_pTBI->fnSigs, context->fnSigs, sizeof(context->fnSigs));

	kfree(context);
	return 0;
}
#endif

#ifdef CONFIG_SMP
static inline unsigned int _get_trigger_mask(void)
{
	unsigned long cpu = smp_processor_id();
	return per_cpu(trigger_mask, cpu);
}

unsigned int get_trigger_mask(void)
{
	return _get_trigger_mask();
}
EXPORT_SYMBOL(get_trigger_mask);

static void set_trigger_mask(unsigned int mask)
{
	unsigned long cpu = smp_processor_id();
	per_cpu(trigger_mask, cpu) = mask;
}

void arch_local_irq_enable(void)
{
	preempt_disable();
	arch_local_irq_restore(_get_trigger_mask());
	preempt_enable_no_resched();
}
EXPORT_SYMBOL(arch_local_irq_enable);
#else
static void set_trigger_mask(unsigned int mask)
{
	global_trigger_mask = mask;
}
#endif

void per_cpu_trap_init(unsigned long cpu)
{
	TBIRES int_context;
	unsigned int thread = cpu_2_hwthread_id[cpu];

	set_trigger_mask(TBI_INTS_INIT(thread) | /* interrupts */
			 TBI_TRIG_BIT(TBID_SIGNUM_LWK) | /* low level kick */
			 TBI_TRIG_BIT(TBID_SIGNUM_SW1));

	/* non-priv - use current stack */
	int_context.Sig.pCtx = NULL;
	/* Start with interrupts off */
	int_context.Sig.TrigMask = INTS_OFF_MASK;
	int_context.Sig.SaveMask = 0;

	/* And call __TBIASyncTrigger() */
	__TBIASyncTrigger(int_context);
}

void __init trap_init(void)
{
	unsigned long cpu = smp_processor_id();
	PTBI _pTBI = per_cpu(pTBI, cpu);

	_pTBI->fnSigs[TBID_SIGNUM_XXF] = fault_handler;
	_pTBI->fnSigs[TBID_SIGNUM_SW0] = switchx_handler;
	_pTBI->fnSigs[TBID_SIGNUM_SW1] = switch1_handler;
	_pTBI->fnSigs[TBID_SIGNUM_SW2] = switchx_handler;
	_pTBI->fnSigs[TBID_SIGNUM_SW3] = switchx_handler;
	_pTBI->fnSigs[TBID_SIGNUM_LWK] = kick_handler;

#ifdef CONFIG_METAG_META21
	_pTBI->fnSigs[TBID_SIGNUM_DFR] = __TBIHandleDFR;
	_pTBI->fnSigs[TBID_SIGNUM_FPE] = fpe_handler;
#endif

	per_cpu_trap_init(cpu);
}

void tbi_startup_interrupt(int irq)
{
	unsigned long cpu = smp_processor_id();
	PTBI _pTBI = per_cpu(pTBI, cpu);

	BUG_ON(irq > TBID_SIGNUM_MAX);

	/* For TR1 and TR2, the thread id is encoded in the irq number */
	if (irq >= TBID_SIGNUM_T10 && irq < TBID_SIGNUM_TR3)
		cpu = hwthread_id_2_cpu[(irq - TBID_SIGNUM_T10) % 4];

	set_trigger_mask(get_trigger_mask() | TBI_TRIG_BIT(irq));

	_pTBI->fnSigs[irq] = trigger_handler;
}

void tbi_shutdown_interrupt(int irq)
{
	unsigned long cpu = smp_processor_id();
	PTBI _pTBI = per_cpu(pTBI, cpu);

	BUG_ON(irq > TBID_SIGNUM_MAX);

	set_trigger_mask(get_trigger_mask() & ~TBI_TRIG_BIT(irq));

	_pTBI->fnSigs[irq] = __TBIUnExpXXX;
}

int ret_from_fork(TBIRES arg)
{
	struct task_struct *prev = arg.Switch.pPara;
	struct task_struct *tsk = current;
	struct pt_regs *regs = task_pt_regs(tsk);
	int (*fn)(void *);
	TBIRES Next;

	schedule_tail(prev);

	if (tsk->flags & PF_KTHREAD) {
		fn = (void *)regs->ctx.DX[4].U1;
		BUG_ON(!fn);

		fn((void *)regs->ctx.DX[3].U1);
	}

	if (test_syscall_work())
		syscall_trace_leave(regs);

	preempt_disable();

	Next.Sig.TrigMask = get_trigger_mask();
	Next.Sig.SaveMask = 0;
	Next.Sig.pCtx = &regs->ctx;

	set_gateway_tls(current->thread.tls_ptr);

	preempt_enable_no_resched();

	/* And interrupts should come back on when we resume the real usermode
	 * code. Call __TBIASyncResume()
	 */
	__TBIASyncResume(tail_end(Next));
	/* ASyncResume should NEVER return */
	BUG();
	return 0;
}

void show_trace(struct task_struct *tsk, unsigned long *sp,
		struct pt_regs *regs)
{
	unsigned long addr;
#ifdef CONFIG_FRAME_POINTER
	unsigned long fp, fpnew;
	unsigned long stack;
#endif

	if (regs && user_mode(regs))
		return;

	printk("\nCall trace: ");
#ifdef CONFIG_KALLSYMS
	printk("\n");
#endif

	if (!tsk)
		tsk = current;

#ifdef CONFIG_FRAME_POINTER
	if (regs) {
		print_ip_sym(regs->ctx.CurrPC);
		fp = regs->ctx.AX[1].U0;
	} else {
		fp = __core_reg_get(A0FrP);
	}

	/* detect when the frame pointer has been used for other purposes and
	 * doesn't point to the stack (it may point completely elsewhere which
	 * kstack_end may not detect).
	 */
	stack = (unsigned long)task_stack_page(tsk);
	while (fp >= stack && fp + 8 <= stack + THREAD_SIZE) {
		addr = __raw_readl((unsigned long *)(fp + 4)) - 4;
		if (kernel_text_address(addr))
			print_ip_sym(addr);
		else
			break;
		/* stack grows up, so frame pointers must decrease */
		fpnew = __raw_readl((unsigned long *)(fp + 0));
		if (fpnew >= fp)
			break;
		fp = fpnew;
	}
#else
	while (!kstack_end(sp)) {
		addr = (*sp--) - 4;
		if (kernel_text_address(addr))
			print_ip_sym(addr);
	}
#endif

	printk("\n");

	debug_show_held_locks(tsk);
}

void show_stack(struct task_struct *tsk, unsigned long *sp)
{
	if (!tsk)
		tsk = current;
	if (tsk == current)
		sp = (unsigned long *)current_stack_pointer;
	else
		sp = (unsigned long *)tsk->thread.kernel_context->AX[0].U0;

	show_trace(tsk, sp, NULL);
}
