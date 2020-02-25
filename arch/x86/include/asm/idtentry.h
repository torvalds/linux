/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_IDTENTRY_H
#define _ASM_X86_IDTENTRY_H

/* Interrupts/Exceptions */
#include <asm/trapnr.h>

#ifndef __ASSEMBLY__

void idtentry_enter(struct pt_regs *regs);
void idtentry_exit(struct pt_regs *regs);

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
	idtentry_enter(regs);						\
	instrumentation_begin();					\
	__##func (regs);						\
	instrumentation_end();						\
	idtentry_exit(regs);						\
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
	idtentry_enter(regs);						\
	instrumentation_begin();					\
	__##func (regs, error_code);					\
	instrumentation_end();						\
	idtentry_exit(regs);						\
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

#endif /* __ASSEMBLY__ */

/*
 * The actual entry points. Note that DECLARE_IDTENTRY*() serves two
 * purposes:
 *  - provide the function declarations when included from C-Code
 *  - emit the ASM stubs when included from entry_32/64.S
 *
 * This avoids duplicate defines and ensures that everything is consistent.
 */

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

#endif
