/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_INTERRUPT_H
#define _ASM_POWERPC_INTERRUPT_H

#include <linux/context_tracking.h>
#include <linux/hardirq.h>
#include <asm/cputime.h>
#include <asm/ftrace.h>
#include <asm/kprobes.h>
#include <asm/runlatch.h>

struct interrupt_state {
#ifdef CONFIG_PPC_BOOK3E_64
	enum ctx_state ctx_state;
#endif
};

static inline void booke_restore_dbcr0(void)
{
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
	unsigned long dbcr0 = current->thread.debug.dbcr0;

	if (IS_ENABLED(CONFIG_PPC32) && unlikely(dbcr0 & DBCR0_IDM)) {
		mtspr(SPRN_DBSR, -1);
		mtspr(SPRN_DBCR0, global_dbcr0[smp_processor_id()]);
	}
#endif
}

static inline void interrupt_enter_prepare(struct pt_regs *regs, struct interrupt_state *state)
{
	/*
	 * Book3E reconciles irq soft mask in asm
	 */
#ifdef CONFIG_PPC_BOOK3S_64
	if (irq_soft_mask_set_return(IRQS_ALL_DISABLED) == IRQS_ENABLED)
		trace_hardirqs_off();
	local_paca->irq_happened |= PACA_IRQ_HARD_DIS;

	if (user_mode(regs)) {
		CT_WARN_ON(ct_state() != CONTEXT_USER);
		user_exit_irqoff();

		account_cpu_user_entry();
		account_stolen_time();
	} else {
		/*
		 * CT_WARN_ON comes here via program_check_exception,
		 * so avoid recursion.
		 */
		if (TRAP(regs) != 0x700)
			CT_WARN_ON(ct_state() != CONTEXT_KERNEL);
	}
#endif

#ifdef CONFIG_PPC_BOOK3E_64
	state->ctx_state = exception_enter();
	if (user_mode(regs))
		account_cpu_user_entry();
#endif
}

/*
 * Care should be taken to note that interrupt_exit_prepare and
 * interrupt_async_exit_prepare do not necessarily return immediately to
 * regs context (e.g., if regs is usermode, we don't necessarily return to
 * user mode). Other interrupts might be taken between here and return,
 * context switch / preemption may occur in the exit path after this, or a
 * signal may be delivered, etc.
 *
 * The real interrupt exit code is platform specific, e.g.,
 * interrupt_exit_user_prepare / interrupt_exit_kernel_prepare for 64s.
 *
 * However interrupt_nmi_exit_prepare does return directly to regs, because
 * NMIs do not do "exit work" or replay soft-masked interrupts.
 */
static inline void interrupt_exit_prepare(struct pt_regs *regs, struct interrupt_state *state)
{
#ifdef CONFIG_PPC_BOOK3E_64
	exception_exit(state->ctx_state);
#endif

	/*
	 * Book3S exits to user via interrupt_exit_user_prepare(), which does
	 * context tracking, which is a cleaner way to handle PREEMPT=y
	 * and avoid context entry/exit in e.g., preempt_schedule_irq()),
	 * which is likely to be where the core code wants to end up.
	 *
	 * The above comment explains why we can't do the
	 *
	 *     if (user_mode(regs))
	 *         user_exit_irqoff();
	 *
	 * sequence here.
	 */
}

static inline void interrupt_async_enter_prepare(struct pt_regs *regs, struct interrupt_state *state)
{
#ifdef CONFIG_PPC_BOOK3S_64
	if (cpu_has_feature(CPU_FTR_CTRL) &&
	    !test_thread_local_flags(_TLF_RUNLATCH))
		__ppc64_runlatch_on();
#endif

	interrupt_enter_prepare(regs, state);
	irq_enter();
}

static inline void interrupt_async_exit_prepare(struct pt_regs *regs, struct interrupt_state *state)
{
	irq_exit();
	interrupt_exit_prepare(regs, state);
}

struct interrupt_nmi_state {
#ifdef CONFIG_PPC64
#ifdef CONFIG_PPC_BOOK3S_64
	u8 irq_soft_mask;
	u8 irq_happened;
#endif
	u8 ftrace_enabled;
#endif
};

static inline void interrupt_nmi_enter_prepare(struct pt_regs *regs, struct interrupt_nmi_state *state)
{
#ifdef CONFIG_PPC64
#ifdef CONFIG_PPC_BOOK3S_64
	state->irq_soft_mask = local_paca->irq_soft_mask;
	state->irq_happened = local_paca->irq_happened;

	/*
	 * Set IRQS_ALL_DISABLED unconditionally so irqs_disabled() does
	 * the right thing, and set IRQ_HARD_DIS. We do not want to reconcile
	 * because that goes through irq tracing which we don't want in NMI.
	 */
	local_paca->irq_soft_mask = IRQS_ALL_DISABLED;
	local_paca->irq_happened |= PACA_IRQ_HARD_DIS;

	/* Don't do any per-CPU operations until interrupt state is fixed */
#endif
	/* Allow DEC and PMI to be traced when they are soft-NMI */
	if (TRAP(regs) != 0x900 && TRAP(regs) != 0xf00 && TRAP(regs) != 0x260) {
		state->ftrace_enabled = this_cpu_get_ftrace_enabled();
		this_cpu_set_ftrace_enabled(0);
	}
#endif

	/*
	 * Do not use nmi_enter() for pseries hash guest taking a real-mode
	 * NMI because not everything it touches is within the RMA limit.
	 */
	if (!IS_ENABLED(CONFIG_PPC_BOOK3S_64) ||
			!firmware_has_feature(FW_FEATURE_LPAR) ||
			radix_enabled() || (mfmsr() & MSR_DR))
		nmi_enter();
}

static inline void interrupt_nmi_exit_prepare(struct pt_regs *regs, struct interrupt_nmi_state *state)
{
	if (!IS_ENABLED(CONFIG_PPC_BOOK3S_64) ||
			!firmware_has_feature(FW_FEATURE_LPAR) ||
			radix_enabled() || (mfmsr() & MSR_DR))
		nmi_exit();

#ifdef CONFIG_PPC64
	if (TRAP(regs) != 0x900 && TRAP(regs) != 0xf00 && TRAP(regs) != 0x260)
		this_cpu_set_ftrace_enabled(state->ftrace_enabled);

#ifdef CONFIG_PPC_BOOK3S_64
	/* Check we didn't change the pending interrupt mask. */
	WARN_ON_ONCE((state->irq_happened | PACA_IRQ_HARD_DIS) != local_paca->irq_happened);
	local_paca->irq_happened = state->irq_happened;
	local_paca->irq_soft_mask = state->irq_soft_mask;
#endif
#endif
}

/*
 * Don't use noinstr here like x86, but rather add NOKPROBE_SYMBOL to each
 * function definition. The reason for this is the noinstr section is placed
 * after the main text section, i.e., very far away from the interrupt entry
 * asm. That creates problems with fitting linker stubs when building large
 * kernels.
 */
#define interrupt_handler __visible noinline notrace __no_kcsan __no_sanitize_address

/**
 * DECLARE_INTERRUPT_HANDLER_RAW - Declare raw interrupt handler function
 * @func:	Function name of the entry point
 * @returns:	Returns a value back to asm caller
 */
#define DECLARE_INTERRUPT_HANDLER_RAW(func)				\
	__visible long func(struct pt_regs *regs)

/**
 * DEFINE_INTERRUPT_HANDLER_RAW - Define raw interrupt handler function
 * @func:	Function name of the entry point
 * @returns:	Returns a value back to asm caller
 *
 * @func is called from ASM entry code.
 *
 * This is a plain function which does no tracing, reconciling, etc.
 * The macro is written so it acts as function definition. Append the
 * body with a pair of curly brackets.
 *
 * raw interrupt handlers must not enable or disable interrupts, or
 * schedule, tracing and instrumentation (ftrace, lockdep, etc) would
 * not be advisable either, although may be possible in a pinch, the
 * trace will look odd at least.
 *
 * A raw handler may call one of the other interrupt handler functions
 * to be converted into that interrupt context without these restrictions.
 *
 * On PPC64, _RAW handlers may return with fast_interrupt_return.
 *
 * Specific handlers may have additional restrictions.
 */
#define DEFINE_INTERRUPT_HANDLER_RAW(func)				\
static __always_inline long ____##func(struct pt_regs *regs);		\
									\
interrupt_handler long func(struct pt_regs *regs)			\
{									\
	long ret;							\
									\
	ret = ____##func (regs);					\
									\
	return ret;							\
}									\
NOKPROBE_SYMBOL(func);							\
									\
static __always_inline long ____##func(struct pt_regs *regs)

/**
 * DECLARE_INTERRUPT_HANDLER - Declare synchronous interrupt handler function
 * @func:	Function name of the entry point
 */
#define DECLARE_INTERRUPT_HANDLER(func)					\
	__visible void func(struct pt_regs *regs)

/**
 * DEFINE_INTERRUPT_HANDLER - Define synchronous interrupt handler function
 * @func:	Function name of the entry point
 *
 * @func is called from ASM entry code.
 *
 * The macro is written so it acts as function definition. Append the
 * body with a pair of curly brackets.
 */
#define DEFINE_INTERRUPT_HANDLER(func)					\
static __always_inline void ____##func(struct pt_regs *regs);		\
									\
interrupt_handler void func(struct pt_regs *regs)			\
{									\
	struct interrupt_state state;					\
									\
	interrupt_enter_prepare(regs, &state);				\
									\
	____##func (regs);						\
									\
	interrupt_exit_prepare(regs, &state);				\
}									\
NOKPROBE_SYMBOL(func);							\
									\
static __always_inline void ____##func(struct pt_regs *regs)

/**
 * DECLARE_INTERRUPT_HANDLER_RET - Declare synchronous interrupt handler function
 * @func:	Function name of the entry point
 * @returns:	Returns a value back to asm caller
 */
#define DECLARE_INTERRUPT_HANDLER_RET(func)				\
	__visible long func(struct pt_regs *regs)

/**
 * DEFINE_INTERRUPT_HANDLER_RET - Define synchronous interrupt handler function
 * @func:	Function name of the entry point
 * @returns:	Returns a value back to asm caller
 *
 * @func is called from ASM entry code.
 *
 * The macro is written so it acts as function definition. Append the
 * body with a pair of curly brackets.
 */
#define DEFINE_INTERRUPT_HANDLER_RET(func)				\
static __always_inline long ____##func(struct pt_regs *regs);		\
									\
interrupt_handler long func(struct pt_regs *regs)			\
{									\
	struct interrupt_state state;					\
	long ret;							\
									\
	interrupt_enter_prepare(regs, &state);				\
									\
	ret = ____##func (regs);					\
									\
	interrupt_exit_prepare(regs, &state);				\
									\
	return ret;							\
}									\
NOKPROBE_SYMBOL(func);							\
									\
static __always_inline long ____##func(struct pt_regs *regs)

/**
 * DECLARE_INTERRUPT_HANDLER_ASYNC - Declare asynchronous interrupt handler function
 * @func:	Function name of the entry point
 */
#define DECLARE_INTERRUPT_HANDLER_ASYNC(func)				\
	__visible void func(struct pt_regs *regs)

/**
 * DEFINE_INTERRUPT_HANDLER_ASYNC - Define asynchronous interrupt handler function
 * @func:	Function name of the entry point
 *
 * @func is called from ASM entry code.
 *
 * The macro is written so it acts as function definition. Append the
 * body with a pair of curly brackets.
 */
#define DEFINE_INTERRUPT_HANDLER_ASYNC(func)				\
static __always_inline void ____##func(struct pt_regs *regs);		\
									\
interrupt_handler void func(struct pt_regs *regs)			\
{									\
	struct interrupt_state state;					\
									\
	interrupt_async_enter_prepare(regs, &state);			\
									\
	____##func (regs);						\
									\
	interrupt_async_exit_prepare(regs, &state);			\
}									\
NOKPROBE_SYMBOL(func);							\
									\
static __always_inline void ____##func(struct pt_regs *regs)

/**
 * DECLARE_INTERRUPT_HANDLER_NMI - Declare NMI interrupt handler function
 * @func:	Function name of the entry point
 * @returns:	Returns a value back to asm caller
 */
#define DECLARE_INTERRUPT_HANDLER_NMI(func)				\
	__visible long func(struct pt_regs *regs)

/**
 * DEFINE_INTERRUPT_HANDLER_NMI - Define NMI interrupt handler function
 * @func:	Function name of the entry point
 * @returns:	Returns a value back to asm caller
 *
 * @func is called from ASM entry code.
 *
 * The macro is written so it acts as function definition. Append the
 * body with a pair of curly brackets.
 */
#define DEFINE_INTERRUPT_HANDLER_NMI(func)				\
static __always_inline long ____##func(struct pt_regs *regs);		\
									\
interrupt_handler long func(struct pt_regs *regs)			\
{									\
	struct interrupt_nmi_state state;				\
	long ret;							\
									\
	interrupt_nmi_enter_prepare(regs, &state);			\
									\
	ret = ____##func (regs);					\
									\
	interrupt_nmi_exit_prepare(regs, &state);			\
									\
	return ret;							\
}									\
NOKPROBE_SYMBOL(func);							\
									\
static __always_inline long ____##func(struct pt_regs *regs)


/* Interrupt handlers */
/* kernel/traps.c */
DECLARE_INTERRUPT_HANDLER_NMI(system_reset_exception);
#ifdef CONFIG_PPC_BOOK3S_64
DECLARE_INTERRUPT_HANDLER_ASYNC(machine_check_exception);
#else
DECLARE_INTERRUPT_HANDLER_NMI(machine_check_exception);
#endif
DECLARE_INTERRUPT_HANDLER(SMIException);
DECLARE_INTERRUPT_HANDLER(handle_hmi_exception);
DECLARE_INTERRUPT_HANDLER(unknown_exception);
DECLARE_INTERRUPT_HANDLER_ASYNC(unknown_async_exception);
DECLARE_INTERRUPT_HANDLER(instruction_breakpoint_exception);
DECLARE_INTERRUPT_HANDLER(RunModeException);
DECLARE_INTERRUPT_HANDLER(single_step_exception);
DECLARE_INTERRUPT_HANDLER(program_check_exception);
DECLARE_INTERRUPT_HANDLER(emulation_assist_interrupt);
DECLARE_INTERRUPT_HANDLER(alignment_exception);
DECLARE_INTERRUPT_HANDLER(StackOverflow);
DECLARE_INTERRUPT_HANDLER(stack_overflow_exception);
DECLARE_INTERRUPT_HANDLER(kernel_fp_unavailable_exception);
DECLARE_INTERRUPT_HANDLER(altivec_unavailable_exception);
DECLARE_INTERRUPT_HANDLER(vsx_unavailable_exception);
DECLARE_INTERRUPT_HANDLER(facility_unavailable_exception);
DECLARE_INTERRUPT_HANDLER(fp_unavailable_tm);
DECLARE_INTERRUPT_HANDLER(altivec_unavailable_tm);
DECLARE_INTERRUPT_HANDLER(vsx_unavailable_tm);
DECLARE_INTERRUPT_HANDLER_NMI(performance_monitor_exception_nmi);
DECLARE_INTERRUPT_HANDLER_ASYNC(performance_monitor_exception_async);
DECLARE_INTERRUPT_HANDLER_RAW(performance_monitor_exception);
DECLARE_INTERRUPT_HANDLER(DebugException);
DECLARE_INTERRUPT_HANDLER(altivec_assist_exception);
DECLARE_INTERRUPT_HANDLER(CacheLockingException);
DECLARE_INTERRUPT_HANDLER(SPEFloatingPointException);
DECLARE_INTERRUPT_HANDLER(SPEFloatingPointRoundException);
DECLARE_INTERRUPT_HANDLER(WatchdogException);
DECLARE_INTERRUPT_HANDLER(kernel_bad_stack);

/* slb.c */
DECLARE_INTERRUPT_HANDLER_RAW(do_slb_fault);
DECLARE_INTERRUPT_HANDLER(do_bad_slb_fault);

/* hash_utils.c */
DECLARE_INTERRUPT_HANDLER_RAW(do_hash_fault);

/* fault.c */
DECLARE_INTERRUPT_HANDLER_RET(do_page_fault);
DECLARE_INTERRUPT_HANDLER(do_bad_page_fault_segv);

/* process.c */
DECLARE_INTERRUPT_HANDLER(do_break);

/* time.c */
DECLARE_INTERRUPT_HANDLER_ASYNC(timer_interrupt);

/* mce.c */
DECLARE_INTERRUPT_HANDLER_NMI(machine_check_early);
DECLARE_INTERRUPT_HANDLER_NMI(hmi_exception_realmode);

DECLARE_INTERRUPT_HANDLER_ASYNC(TAUException);

void unrecoverable_exception(struct pt_regs *regs);

void replay_system_reset(void);
void replay_soft_interrupts(void);

static inline void interrupt_cond_local_irq_enable(struct pt_regs *regs)
{
	if (!arch_irq_disabled_regs(regs))
		local_irq_enable();
}

#endif /* _ASM_POWERPC_INTERRUPT_H */
