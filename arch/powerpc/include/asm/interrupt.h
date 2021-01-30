/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_INTERRUPT_H
#define _ASM_POWERPC_INTERRUPT_H

#include <linux/context_tracking.h>
#include <linux/hardirq.h>
#include <asm/ftrace.h>

struct interrupt_state {
#ifdef CONFIG_PPC64
	enum ctx_state ctx_state;
#endif
};

static inline void interrupt_enter_prepare(struct pt_regs *regs, struct interrupt_state *state)
{
#ifdef CONFIG_PPC64
	state->ctx_state = exception_enter();
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
#ifdef CONFIG_PPC64
	exception_exit(state->ctx_state);
#endif
}

static inline void interrupt_async_enter_prepare(struct pt_regs *regs, struct interrupt_state *state)
{
	interrupt_enter_prepare(regs, state);
}

static inline void interrupt_async_exit_prepare(struct pt_regs *regs, struct interrupt_state *state)
{
	interrupt_exit_prepare(regs, state);
}

struct interrupt_nmi_state {
};

static inline void interrupt_nmi_enter_prepare(struct pt_regs *regs, struct interrupt_nmi_state *state)
{
}

static inline void interrupt_nmi_exit_prepare(struct pt_regs *regs, struct interrupt_nmi_state *state)
{
}

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
__visible noinstr long func(struct pt_regs *regs)			\
{									\
	long ret;							\
									\
	ret = ____##func (regs);					\
									\
	return ret;							\
}									\
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
__visible noinstr void func(struct pt_regs *regs)			\
{									\
	struct interrupt_state state;					\
									\
	interrupt_enter_prepare(regs, &state);				\
									\
	____##func (regs);						\
									\
	interrupt_exit_prepare(regs, &state);				\
}									\
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
__visible noinstr long func(struct pt_regs *regs)			\
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
__visible noinstr void func(struct pt_regs *regs)			\
{									\
	struct interrupt_state state;					\
									\
	interrupt_async_enter_prepare(regs, &state);			\
									\
	____##func (regs);						\
									\
	interrupt_async_exit_prepare(regs, &state);			\
}									\
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
__visible noinstr long func(struct pt_regs *regs)			\
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
DECLARE_INTERRUPT_HANDLER(unrecoverable_exception);
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

void replay_system_reset(void);
void replay_soft_interrupts(void);

static inline void interrupt_cond_local_irq_enable(struct pt_regs *regs)
{
	if (!arch_irq_disabled_regs(regs))
		local_irq_enable();
}

#endif /* _ASM_POWERPC_INTERRUPT_H */
