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

#else /* !__ASSEMBLY__ */

/*
 * The ASM variants for DECLARE_IDTENTRY*() which emit the ASM entry stubs.
 */
#define DECLARE_IDTENTRY(vector, func)					\
	idtentry vector asm_##func func has_error_code=0 sane=1

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

#endif
