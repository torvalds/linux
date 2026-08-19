/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_INTERRUPT_H
#define _ASM_POWERPC_INTERRUPT_H

/* BookE/4xx */
#define INTERRUPT_CRITICAL_INPUT  0x100

/* BookE */
#define INTERRUPT_DEBUG           0xd00
#ifdef CONFIG_BOOKE
#define INTERRUPT_PERFMON         0x260
#define INTERRUPT_DOORBELL        0x280
#endif

/* BookS/4xx/8xx */
#define INTERRUPT_MACHINE_CHECK   0x200

/* BookS/8xx */
#define INTERRUPT_SYSTEM_RESET    0x100

/* BookS */
#define INTERRUPT_DATA_SEGMENT    0x380
#define INTERRUPT_INST_SEGMENT    0x480
#define INTERRUPT_TRACE           0xd00
#define INTERRUPT_H_DATA_STORAGE  0xe00
#define INTERRUPT_HMI			0xe60
#define INTERRUPT_H_FAC_UNAVAIL   0xf80
#ifdef CONFIG_PPC_BOOK3S
#define INTERRUPT_DOORBELL        0xa00
#define INTERRUPT_PERFMON         0xf00
#define INTERRUPT_ALTIVEC_UNAVAIL	0xf20
#endif

/* BookE/BookS/4xx/8xx */
#define INTERRUPT_DATA_STORAGE    0x300
#define INTERRUPT_INST_STORAGE    0x400
#define INTERRUPT_EXTERNAL		0x500
#define INTERRUPT_ALIGNMENT       0x600
#define INTERRUPT_PROGRAM         0x700
#define INTERRUPT_SYSCALL         0xc00
#define INTERRUPT_TRACE			0xd00

/* BookE/BookS/44x */
#define INTERRUPT_FP_UNAVAIL      0x800

/* BookE/BookS/44x/8xx */
#define INTERRUPT_DECREMENTER     0x900

#ifndef INTERRUPT_PERFMON
#define INTERRUPT_PERFMON         0x0
#endif

/* 8xx */
#define INTERRUPT_SOFT_EMU_8xx		0x1000
#define INTERRUPT_INST_TLB_MISS_8xx	0x1100
#define INTERRUPT_DATA_TLB_MISS_8xx	0x1200
#define INTERRUPT_INST_TLB_ERROR_8xx	0x1300
#define INTERRUPT_DATA_TLB_ERROR_8xx	0x1400
#define INTERRUPT_DATA_BREAKPOINT_8xx	0x1c00
#define INTERRUPT_INST_BREAKPOINT_8xx	0x1d00

/* 603 */
#define INTERRUPT_INST_TLB_MISS_603		0x1000
#define INTERRUPT_DATA_LOAD_TLB_MISS_603	0x1100
#define INTERRUPT_DATA_STORE_TLB_MISS_603	0x1200

#ifndef __ASSEMBLER__

#include <linux/sched/debug.h> /* for show_regs */
#include <linux/irq-entry-common.h>

#include <asm/kprobes.h>
#include <asm/runlatch.h>

#ifdef CONFIG_PPC_IRQ_SOFT_MASK_DEBUG
/*
 * WARN/BUG is handled with a program interrupt so minimise checks here to
 * avoid recursion and maximise the chance of getting the first oops handled.
 */
#define INT_SOFT_MASK_BUG_ON(regs, cond)				\
do {									\
	if ((user_mode(regs) || (TRAP(regs) != INTERRUPT_PROGRAM)))	\
		BUG_ON(cond);						\
} while (0)
#else
#define INT_SOFT_MASK_BUG_ON(regs, cond)
#endif

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
static __always_inline __no_sanitize_address __no_kcsan long		\
____##func(struct pt_regs *regs);					\
									\
interrupt_handler long func(struct pt_regs *regs)			\
{									\
	long ret;							\
									\
	__hard_RI_enable();						\
									\
	ret = ____##func (regs);					\
									\
	return ret;							\
}									\
NOKPROBE_SYMBOL(func);							\
									\
static __always_inline __no_sanitize_address __no_kcsan long		\
____##func(struct pt_regs *regs)

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
	irqentry_state_t state;						\
	arch_interrupt_enter_prepare(regs);				\
	state = irqentry_enter(regs);					\
	instrumentation_begin();					\
	____##func (regs);						\
	instrumentation_end();						\
	arch_interrupt_exit_prepare(regs);				\
	irqentry_exit(regs, state);					\
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
	long ret;							\
	irqentry_state_t state;						\
									\
	arch_interrupt_enter_prepare(regs);				\
	state = irqentry_enter(regs);					\
	instrumentation_begin();					\
	ret = ____##func (regs);					\
	instrumentation_end();						\
	arch_interrupt_exit_prepare(regs);				\
	irqentry_exit(regs, state);					\
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
	irqentry_state_t state;						\
	arch_interrupt_async_enter_prepare(regs);			\
	state = irqentry_enter(regs);					\
	instrumentation_begin();					\
	irq_enter_rcu();						\
	____##func (regs);						\
	irq_exit_rcu();							\
	instrumentation_end();						\
	arch_interrupt_async_exit_prepare(regs);			\
	irqentry_exit(regs, state);					\
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
static __always_inline __no_sanitize_address __no_kcsan long		\
____##func(struct pt_regs *regs);					\
									\
interrupt_handler long func(struct pt_regs *regs)			\
{									\
	irqentry_state_t state;						\
	struct interrupt_nmi_state nmi_state;				\
	long ret;							\
									\
	arch_interrupt_nmi_enter_prepare(regs, &nmi_state);		\
	if (mfmsr() & MSR_DR) {						\
		/* nmi_entry if relocations are on */			\
		state = irqentry_nmi_enter(regs);			\
	} else if (IS_ENABLED(CONFIG_PPC_BOOK3S_64) &&			\
		   firmware_has_feature(FW_FEATURE_LPAR) &&		\
		   !radix_enabled()) {					\
		/* no nmi_entry for a pseries hash guest		\
		 * taking a real mode exception */			\
	} else if (IS_ENABLED(CONFIG_KASAN)) {				\
		/* no nmi_entry for KASAN in real mode */		\
	} else if (percpu_first_chunk_is_paged) {			\
		/* no nmi_entry if percpu first chunk is not embedded */\
	} else {							\
		state = irqentry_nmi_enter(regs);			\
	}								\
	ret = ____##func (regs);					\
	arch_interrupt_nmi_exit_prepare(regs, &nmi_state);		\
	if (mfmsr() & MSR_DR) {						\
		/* nmi_exit if relocations are on */			\
		irqentry_nmi_exit(regs, state);				\
	} else if (IS_ENABLED(CONFIG_PPC_BOOK3S_64) &&			\
		   firmware_has_feature(FW_FEATURE_LPAR) &&		\
		   !radix_enabled()) {					\
		/* no nmi_exit for a pseries hash guest			\
		 * taking a real mode exception */			\
	} else if (IS_ENABLED(CONFIG_KASAN)) {				\
		/* no nmi_exit for KASAN in real mode */		\
	} else if (percpu_first_chunk_is_paged) {			\
		/* no nmi_exit if percpu first chunk is not embedded */	\
	} else {							\
		irqentry_nmi_exit(regs, state);				\
	}								\
									\
	return ret;							\
}									\
NOKPROBE_SYMBOL(func);							\
									\
static __always_inline  __no_sanitize_address __no_kcsan long		\
____##func(struct pt_regs *regs)


/* Interrupt handlers */
/* kernel/traps.c */
DECLARE_INTERRUPT_HANDLER_NMI(system_reset_exception);
#ifdef CONFIG_PPC_BOOK3S_64
DECLARE_INTERRUPT_HANDLER_RAW(machine_check_early_boot);
DECLARE_INTERRUPT_HANDLER_ASYNC(machine_check_exception_async);
#endif
DECLARE_INTERRUPT_HANDLER_NMI(machine_check_exception);
DECLARE_INTERRUPT_HANDLER(SMIException);
DECLARE_INTERRUPT_HANDLER(handle_hmi_exception);
DECLARE_INTERRUPT_HANDLER(unknown_exception);
DECLARE_INTERRUPT_HANDLER_ASYNC(unknown_async_exception);
DECLARE_INTERRUPT_HANDLER_NMI(unknown_nmi_exception);
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
DECLARE_INTERRUPT_HANDLER_NMI(WatchdogException);
DECLARE_INTERRUPT_HANDLER(kernel_bad_stack);

/* slb.c */
DECLARE_INTERRUPT_HANDLER_RAW(do_slb_fault);
DECLARE_INTERRUPT_HANDLER(do_bad_segment_interrupt);

/* hash_utils.c */
DECLARE_INTERRUPT_HANDLER(do_hash_fault);

/* fault.c */
DECLARE_INTERRUPT_HANDLER(do_page_fault);
DECLARE_INTERRUPT_HANDLER(do_bad_page_fault_segv);

/* process.c */
DECLARE_INTERRUPT_HANDLER(do_break);

/* time.c */
DECLARE_INTERRUPT_HANDLER_ASYNC(timer_interrupt);

/* mce.c */
DECLARE_INTERRUPT_HANDLER_NMI(machine_check_early);
DECLARE_INTERRUPT_HANDLER_NMI(hmi_exception_realmode);

DECLARE_INTERRUPT_HANDLER_ASYNC(TAUException);

/* irq.c */
DECLARE_INTERRUPT_HANDLER_ASYNC(do_IRQ);

void __noreturn unrecoverable_exception(struct pt_regs *regs);

void replay_system_reset(void);
void replay_soft_interrupts(void);

static inline void interrupt_cond_local_irq_enable(struct pt_regs *regs)
{
	if (!regs_irqs_disabled(regs))
		local_irq_enable();
}

long system_call_exception(struct pt_regs *regs, unsigned long r0);
notrace unsigned long syscall_exit_prepare(unsigned long r3, struct pt_regs *regs, long scv);
notrace unsigned long interrupt_exit_user_prepare(struct pt_regs *regs);
notrace unsigned long interrupt_exit_kernel_prepare(struct pt_regs *regs);
#ifdef CONFIG_PPC64
unsigned long syscall_exit_restart(unsigned long r3, struct pt_regs *regs);
unsigned long interrupt_exit_user_restart(struct pt_regs *regs);
unsigned long interrupt_exit_kernel_restart(struct pt_regs *regs);
#endif

#endif /* __ASSEMBLER__ */

#endif /* _ASM_POWERPC_INTERRUPT_H */
