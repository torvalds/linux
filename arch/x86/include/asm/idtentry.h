/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_IDTENTRY_H
#define _ASM_X86_IDTENTRY_H

/* Interrupts/Exceptions */
#include <asm/trapnr.h>

#ifndef __ASSEMBLY__

void idtentry_enter_user(struct pt_regs *regs);
void idtentry_exit_user(struct pt_regs *regs);

bool idtentry_enter_cond_rcu(struct pt_regs *regs);
void idtentry_exit_cond_rcu(struct pt_regs *regs, bool rcu_exit);

/**
 * DECLARE_IDTENTRY - Declare functions for simple IDT entry points
 *		      No error code pushed by hardware
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Declares three functions:
 * - The ASM entry point: asm_##func
 * - The XEN PV trap entry point: xen_##func (maybe unused)
 * - The C handler called from the ASM entry point
 *
 * Note: This is the C variant of DECLARE_IDTENTRY(). As the name says it
 * declares the entry points for usage in C code. There is an ASM variant
 * as well which is used to emit the entry stubs in entry_32/64.S.
 */
#define DECLARE_IDTENTRY(vector, func)					\
	asmlinkage void asm_##func(void);				\
	asmlinkage void xen_asm_##func(void);				\
	__visible void func(struct pt_regs *regs)

/**
 * DEFINE_IDTENTRY - Emit code for simple IDT entry points
 * @func:	Function name of the entry point
 *
 * @func is called from ASM entry code with interrupts disabled.
 *
 * The macro is written so it acts as function definition. Append the
 * body with a pair of curly brackets.
 *
 * idtentry_enter() contains common code which has to be invoked before
 * arbitrary code in the body. idtentry_exit() contains common code
 * which has to run before returning to the low level assembly code.
 */
#define DEFINE_IDTENTRY(func)						\
static __always_inline void __##func(struct pt_regs *regs);		\
									\
__visible noinstr void func(struct pt_regs *regs)			\
{									\
	bool rcu_exit = idtentry_enter_cond_rcu(regs);			\
									\
	instrumentation_begin();					\
	__##func (regs);						\
	instrumentation_end();						\
	idtentry_exit_cond_rcu(regs, rcu_exit);				\
}									\
									\
static __always_inline void __##func(struct pt_regs *regs)

/* Special case for 32bit IRET 'trap' */
#define DECLARE_IDTENTRY_SW	DECLARE_IDTENTRY
#define DEFINE_IDTENTRY_SW	DEFINE_IDTENTRY

/**
 * DECLARE_IDTENTRY_ERRORCODE - Declare functions for simple IDT entry points
 *				Error code pushed by hardware
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Declares three functions:
 * - The ASM entry point: asm_##func
 * - The XEN PV trap entry point: xen_##func (maybe unused)
 * - The C handler called from the ASM entry point
 *
 * Same as DECLARE_IDTENTRY, but has an extra error_code argument for the
 * C-handler.
 */
#define DECLARE_IDTENTRY_ERRORCODE(vector, func)			\
	asmlinkage void asm_##func(void);				\
	asmlinkage void xen_asm_##func(void);				\
	__visible void func(struct pt_regs *regs, unsigned long error_code)

/**
 * DEFINE_IDTENTRY_ERRORCODE - Emit code for simple IDT entry points
 *			       Error code pushed by hardware
 * @func:	Function name of the entry point
 *
 * Same as DEFINE_IDTENTRY, but has an extra error_code argument
 */
#define DEFINE_IDTENTRY_ERRORCODE(func)					\
static __always_inline void __##func(struct pt_regs *regs,		\
				     unsigned long error_code);		\
									\
__visible noinstr void func(struct pt_regs *regs,			\
			    unsigned long error_code)			\
{									\
	bool rcu_exit = idtentry_enter_cond_rcu(regs);			\
									\
	instrumentation_begin();					\
	__##func (regs, error_code);					\
	instrumentation_end();						\
	idtentry_exit_cond_rcu(regs, rcu_exit);				\
}									\
									\
static __always_inline void __##func(struct pt_regs *regs,		\
				     unsigned long error_code)

/**
 * DECLARE_IDTENTRY_RAW - Declare functions for raw IDT entry points
 *		      No error code pushed by hardware
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Maps to DECLARE_IDTENTRY().
 */
#define DECLARE_IDTENTRY_RAW(vector, func)				\
	DECLARE_IDTENTRY(vector, func)

/**
 * DEFINE_IDTENTRY_RAW - Emit code for raw IDT entry points
 * @func:	Function name of the entry point
 *
 * @func is called from ASM entry code with interrupts disabled.
 *
 * The macro is written so it acts as function definition. Append the
 * body with a pair of curly brackets.
 *
 * Contrary to DEFINE_IDTENTRY() this does not invoke the
 * idtentry_enter/exit() helpers before and after the body invocation. This
 * needs to be done in the body itself if applicable. Use if extra work
 * is required before the enter/exit() helpers are invoked.
 */
#define DEFINE_IDTENTRY_RAW(func)					\
__visible noinstr void func(struct pt_regs *regs)

/**
 * DECLARE_IDTENTRY_RAW_ERRORCODE - Declare functions for raw IDT entry points
 *				    Error code pushed by hardware
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Maps to DECLARE_IDTENTRY_ERRORCODE()
 */
#define DECLARE_IDTENTRY_RAW_ERRORCODE(vector, func)			\
	DECLARE_IDTENTRY_ERRORCODE(vector, func)

/**
 * DEFINE_IDTENTRY_RAW_ERRORCODE - Emit code for raw IDT entry points
 * @func:	Function name of the entry point
 *
 * @func is called from ASM entry code with interrupts disabled.
 *
 * The macro is written so it acts as function definition. Append the
 * body with a pair of curly brackets.
 *
 * Contrary to DEFINE_IDTENTRY_ERRORCODE() this does not invoke the
 * idtentry_enter/exit() helpers before and after the body invocation. This
 * needs to be done in the body itself if applicable. Use if extra work
 * is required before the enter/exit() helpers are invoked.
 */
#define DEFINE_IDTENTRY_RAW_ERRORCODE(func)				\
__visible noinstr void func(struct pt_regs *regs, unsigned long error_code)

/**
 * DECLARE_IDTENTRY_XENCB - Declare functions for XEN HV callback entry point
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Declares three functions:
 * - The ASM entry point: asm_##func
 * - The XEN PV trap entry point: xen_##func (maybe unused)
 * - The C handler called from the ASM entry point
 *
 * Maps to DECLARE_IDTENTRY(). Distinct entry point to handle the 32/64-bit
 * difference
 */
#define DECLARE_IDTENTRY_XENCB(vector, func)				\
	DECLARE_IDTENTRY(vector, func)

#ifdef CONFIG_X86_64
/**
 * DECLARE_IDTENTRY_IST - Declare functions for IST handling IDT entry points
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Maps to DECLARE_IDTENTRY_RAW, but declares also the NOIST C handler
 * which is called from the ASM entry point on user mode entry
 */
#define DECLARE_IDTENTRY_IST(vector, func)				\
	DECLARE_IDTENTRY_RAW(vector, func);				\
	__visible void noist_##func(struct pt_regs *regs)

/**
 * DEFINE_IDTENTRY_IST - Emit code for IST entry points
 * @func:	Function name of the entry point
 *
 * Maps to DEFINE_IDTENTRY_RAW
 */
#define DEFINE_IDTENTRY_IST(func)					\
	DEFINE_IDTENTRY_RAW(func)

/**
 * DEFINE_IDTENTRY_NOIST - Emit code for NOIST entry points which
 *			   belong to a IST entry point (MCE, DB)
 * @func:	Function name of the entry point. Must be the same as
 *		the function name of the corresponding IST variant
 *
 * Maps to DEFINE_IDTENTRY_RAW().
 */
#define DEFINE_IDTENTRY_NOIST(func)					\
	DEFINE_IDTENTRY_RAW(noist_##func)

/**
 * DECLARE_IDTENTRY_DF - Declare functions for double fault
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Maps to DECLARE_IDTENTRY_RAW_ERRORCODE
 */
#define DECLARE_IDTENTRY_DF(vector, func)				\
	DECLARE_IDTENTRY_RAW_ERRORCODE(vector, func)

/**
 * DEFINE_IDTENTRY_DF - Emit code for double fault
 * @func:	Function name of the entry point
 *
 * Maps to DEFINE_IDTENTRY_RAW_ERRORCODE
 */
#define DEFINE_IDTENTRY_DF(func)					\
	DEFINE_IDTENTRY_RAW_ERRORCODE(func)

#else	/* CONFIG_X86_64 */

/* Maps to a regular IDTENTRY on 32bit for now */
# define DECLARE_IDTENTRY_IST		DECLARE_IDTENTRY
# define DEFINE_IDTENTRY_IST		DEFINE_IDTENTRY

/**
 * DECLARE_IDTENTRY_DF - Declare functions for double fault 32bit variant
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Declares two functions:
 * - The ASM entry point: asm_##func
 * - The C handler called from the C shim
 */
#define DECLARE_IDTENTRY_DF(vector, func)				\
	asmlinkage void asm_##func(void);				\
	__visible void func(struct pt_regs *regs,			\
			    unsigned long error_code,			\
			    unsigned long address)

/**
 * DEFINE_IDTENTRY_DF - Emit code for double fault on 32bit
 * @func:	Function name of the entry point
 *
 * This is called through the doublefault shim which already provides
 * cr2 in the address argument.
 */
#define DEFINE_IDTENTRY_DF(func)					\
__visible noinstr void func(struct pt_regs *regs,			\
			    unsigned long error_code,			\
			    unsigned long address)

#endif	/* !CONFIG_X86_64 */

/* C-Code mapping */
#define DECLARE_IDTENTRY_MCE		DECLARE_IDTENTRY_IST
#define DEFINE_IDTENTRY_MCE		DEFINE_IDTENTRY_IST
#define DEFINE_IDTENTRY_MCE_USER	DEFINE_IDTENTRY_NOIST

#define DECLARE_IDTENTRY_NMI		DECLARE_IDTENTRY_IST
#define DEFINE_IDTENTRY_NMI		DEFINE_IDTENTRY_IST

#define DECLARE_IDTENTRY_DEBUG		DECLARE_IDTENTRY_IST
#define DEFINE_IDTENTRY_DEBUG		DEFINE_IDTENTRY_IST
#define DEFINE_IDTENTRY_DEBUG_USER	DEFINE_IDTENTRY_NOIST

/**
 * DECLARE_IDTENTRY_XEN - Declare functions for XEN redirect IDT entry points
 * @vector:	Vector number (ignored for C)
 * @func:	Function name of the entry point
 *
 * Used for xennmi and xendebug redirections. No DEFINE as this is all ASM
 * indirection magic.
 */
#define DECLARE_IDTENTRY_XEN(vector, func)				\
	asmlinkage void xen_asm_exc_xen##func(void);			\
	asmlinkage void asm_exc_xen##func(void)

#else /* !__ASSEMBLY__ */

/*
 * The ASM variants for DECLARE_IDTENTRY*() which emit the ASM entry stubs.
 */
#define DECLARE_IDTENTRY(vector, func)					\
	idtentry vector asm_##func func has_error_code=0 sane=1

#define DECLARE_IDTENTRY_ERRORCODE(vector, func)			\
	idtentry vector asm_##func func has_error_code=1 sane=1

/* Special case for 32bit IRET 'trap'. Do not emit ASM code */
#define DECLARE_IDTENTRY_SW(vector, func)

#define DECLARE_IDTENTRY_RAW(vector, func)				\
	DECLARE_IDTENTRY(vector, func)

#define DECLARE_IDTENTRY_RAW_ERRORCODE(vector, func)			\
	DECLARE_IDTENTRY_ERRORCODE(vector, func)

#ifdef CONFIG_X86_64
# define DECLARE_IDTENTRY_MCE(vector, func)				\
	idtentry_mce_db vector asm_##func func

# define DECLARE_IDTENTRY_DEBUG(vector, func)				\
	idtentry_mce_db vector asm_##func func

# define DECLARE_IDTENTRY_DF(vector, func)				\
	idtentry_df vector asm_##func func

# define DECLARE_IDTENTRY_XENCB(vector, func)				\
	DECLARE_IDTENTRY(vector, func)

#else
# define DECLARE_IDTENTRY_MCE(vector, func)				\
	DECLARE_IDTENTRY(vector, func)

# define DECLARE_IDTENTRY_DEBUG(vector, func)				\
	DECLARE_IDTENTRY(vector, func)

/* No ASM emitted for DF as this goes through a C shim */
# define DECLARE_IDTENTRY_DF(vector, func)

/* No ASM emitted for XEN hypervisor callback */
# define DECLARE_IDTENTRY_XENCB(vector, func)

#endif

/* No ASM code emitted for NMI */
#define DECLARE_IDTENTRY_NMI(vector, func)

/* XEN NMI and DB wrapper */
#define DECLARE_IDTENTRY_XEN(vector, func)				\
	idtentry vector asm_exc_xen##func exc_##func has_error_code=0 sane=1

#endif /* __ASSEMBLY__ */

/*
 * The actual entry points. Note that DECLARE_IDTENTRY*() serves two
 * purposes:
 *  - provide the function declarations when included from C-Code
 *  - emit the ASM stubs when included from entry_32/64.S
 *
 * This avoids duplicate defines and ensures that everything is consistent.
 */

/*
 * Dummy trap number so the low level ASM macro vector number checks do not
 * match which results in emitting plain IDTENTRY stubs without bells and
 * whistels.
 */
#define X86_TRAP_OTHER		0xFFFF

/* Simple exception entry points. No hardware error code */
DECLARE_IDTENTRY(X86_TRAP_DE,		exc_divide_error);
DECLARE_IDTENTRY(X86_TRAP_OF,		exc_overflow);
DECLARE_IDTENTRY(X86_TRAP_BR,		exc_bounds);
DECLARE_IDTENTRY(X86_TRAP_UD,		exc_invalid_op);
DECLARE_IDTENTRY(X86_TRAP_NM,		exc_device_not_available);
DECLARE_IDTENTRY(X86_TRAP_OLD_MF,	exc_coproc_segment_overrun);
DECLARE_IDTENTRY(X86_TRAP_SPURIOUS,	exc_spurious_interrupt_bug);
DECLARE_IDTENTRY(X86_TRAP_MF,		exc_coprocessor_error);
DECLARE_IDTENTRY(X86_TRAP_XF,		exc_simd_coprocessor_error);

/* 32bit software IRET trap. Do not emit ASM code */
DECLARE_IDTENTRY_SW(X86_TRAP_IRET,	iret_error);

/* Simple exception entries with error code pushed by hardware */
DECLARE_IDTENTRY_ERRORCODE(X86_TRAP_TS,	exc_invalid_tss);
DECLARE_IDTENTRY_ERRORCODE(X86_TRAP_NP,	exc_segment_not_present);
DECLARE_IDTENTRY_ERRORCODE(X86_TRAP_SS,	exc_stack_segment);
DECLARE_IDTENTRY_ERRORCODE(X86_TRAP_GP,	exc_general_protection);
DECLARE_IDTENTRY_ERRORCODE(X86_TRAP_AC,	exc_alignment_check);

/* Raw exception entries which need extra work */
DECLARE_IDTENTRY_RAW(X86_TRAP_BP,	exc_int3);

#ifdef CONFIG_X86_MCE
DECLARE_IDTENTRY_MCE(X86_TRAP_MC,	exc_machine_check);
#endif

/* NMI */
DECLARE_IDTENTRY_NMI(X86_TRAP_NMI,	exc_nmi);
DECLARE_IDTENTRY_XEN(X86_TRAP_NMI,	nmi);

/* #DB */
DECLARE_IDTENTRY_DEBUG(X86_TRAP_DB,	exc_debug);
DECLARE_IDTENTRY_XEN(X86_TRAP_DB,	debug);

/* #DF */
DECLARE_IDTENTRY_DF(X86_TRAP_DF,	exc_double_fault);

#ifdef CONFIG_XEN_PV
DECLARE_IDTENTRY_XENCB(X86_TRAP_OTHER,	exc_xen_hypervisor_callback);
#endif

#undef X86_TRAP_OTHER

#endif
