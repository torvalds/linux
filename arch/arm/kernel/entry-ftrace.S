/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/assembler.h>
#include <asm/ftrace.h>
#include <asm/unwind.h>

#include "entry-header.S"

/*
 * When compiling with -pg, gcc inserts a call to the mcount routine at the
 * start of every function.  In mcount, apart from the function's address (in
 * lr), we need to get hold of the function's caller's address.
 *
 * Older GCCs (pre-4.4) inserted a call to a routine called mcount like this:
 *
 *	bl	mcount
 *
 * These versions have the limitation that in order for the mcount routine to
 * be able to determine the function's caller's address, an APCS-style frame
 * pointer (which is set up with something like the code below) is required.
 *
 *	mov     ip, sp
 *	push    {fp, ip, lr, pc}
 *	sub     fp, ip, #4
 *
 * With EABI, these frame pointers are not available unless -mapcs-frame is
 * specified, and if building as Thumb-2, not even then.
 *
 * Newer GCCs (4.4+) solve this problem by introducing a new version of mcount,
 * with call sites like:
 *
 *	push	{lr}
 *	bl	__gnu_mcount_nc
 *
 * With these compilers, frame pointers are not necessary.
 *
 * mcount can be thought of as a function called in the middle of a subroutine
 * call.  As such, it needs to be transparent for both the caller and the
 * callee: the original lr needs to be restored when leaving mcount, and no
 * registers should be clobbered.  (In the __gnu_mcount_nc implementation, we
 * clobber the ip register.  This is OK because the ARM calling convention
 * allows it to be clobbered in subroutines and doesn't use it to hold
 * parameters.)
 *
 * When using dynamic ftrace, we patch out the mcount call by a "mov r0, r0"
 * for the mcount case, and a "pop {lr}" for the __gnu_mcount_nc case (see
 * arch/arm/kernel/ftrace.c).
 */

#ifndef CONFIG_OLD_MCOUNT
#if (__GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 4))
#error Ftrace requires CONFIG_FRAME_POINTER=y with GCC older than 4.4.0.
#endif
#endif

.macro mcount_adjust_addr rd, rn
	bic	\rd, \rn, #1		@ clear the Thumb bit if present
	sub	\rd, \rd, #MCOUNT_INSN_SIZE
.endm

.macro __mcount suffix
	mcount_enter
	ldr	r0, =ftrace_trace_function
	ldr	r2, [r0]
	adr	r0, .Lftrace_stub
	cmp	r0, r2
	bne	1f

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	ldr     r1, =ftrace_graph_return
	ldr     r2, [r1]
	cmp     r0, r2
	bne     ftrace_graph_caller\suffix

	ldr     r1, =ftrace_graph_entry
	ldr     r2, [r1]
	ldr     r0, =ftrace_graph_entry_stub
	cmp     r0, r2
	bne     ftrace_graph_caller\suffix
#endif

	mcount_exit

1: 	mcount_get_lr	r1			@ lr of instrumented func
	mcount_adjust_addr	r0, lr		@ instrumented function
	badr	lr, 2f
	mov	pc, r2
2:	mcount_exit
.endm

.macro __ftrace_caller suffix
	mcount_enter

	mcount_get_lr	r1			@ lr of instrumented func
	mcount_adjust_addr	r0, lr		@ instrumented function

	.globl ftrace_call\suffix
ftrace_call\suffix:
	bl	ftrace_stub

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	.globl ftrace_graph_call\suffix
ftrace_graph_call\suffix:
	mov	r0, r0
#endif

	mcount_exit
.endm

.macro __ftrace_graph_caller
	sub	r0, fp, #4		@ &lr of instrumented routine (&parent)
#ifdef CONFIG_DYNAMIC_FTRACE
	@ called from __ftrace_caller, saved in mcount_enter
	ldr	r1, [sp, #16]		@ instrumented routine (func)
	mcount_adjust_addr	r1, r1
#else
	@ called from __mcount, untouched in lr
	mcount_adjust_addr	r1, lr	@ instrumented routine (func)
#endif
	mov	r2, fp			@ frame pointer
	bl	prepare_ftrace_return
	mcount_exit
.endm

#ifdef CONFIG_OLD_MCOUNT
/*
 * mcount
 */

.macro mcount_enter
	stmdb	sp!, {r0-r3, lr}
.endm

.macro mcount_get_lr reg
	ldr	\reg, [fp, #-4]
.endm

.macro mcount_exit
	ldr	lr, [fp, #-4]
	ldmia	sp!, {r0-r3, pc}
.endm

ENTRY(mcount)
#ifdef CONFIG_DYNAMIC_FTRACE
	stmdb	sp!, {lr}
	ldr	lr, [fp, #-4]
	ldmia	sp!, {pc}
#else
	__mcount _old
#endif
ENDPROC(mcount)

#ifdef CONFIG_DYNAMIC_FTRACE
ENTRY(ftrace_caller_old)
	__ftrace_caller _old
ENDPROC(ftrace_caller_old)
#endif

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
ENTRY(ftrace_graph_caller_old)
	__ftrace_graph_caller
ENDPROC(ftrace_graph_caller_old)
#endif

.purgem mcount_enter
.purgem mcount_get_lr
.purgem mcount_exit
#endif

/*
 * __gnu_mcount_nc
 */

.macro mcount_enter
/*
 * This pad compensates for the push {lr} at the call site.  Note that we are
 * unable to unwind through a function which does not otherwise save its lr.
 */
 UNWIND(.pad	#4)
	stmdb	sp!, {r0-r3, lr}
 UNWIND(.save	{r0-r3, lr})
.endm

.macro mcount_get_lr reg
	ldr	\reg, [sp, #20]
.endm

.macro mcount_exit
	ldmia	sp!, {r0-r3, ip, lr}
	ret	ip
.endm

ENTRY(__gnu_mcount_nc)
UNWIND(.fnstart)
#ifdef CONFIG_DYNAMIC_FTRACE
	mov	ip, lr
	ldmia	sp!, {lr}
	ret	ip
#else
	__mcount
#endif
UNWIND(.fnend)
ENDPROC(__gnu_mcount_nc)

#ifdef CONFIG_DYNAMIC_FTRACE
ENTRY(ftrace_caller)
UNWIND(.fnstart)
	__ftrace_caller
UNWIND(.fnend)
ENDPROC(ftrace_caller)
#endif

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
ENTRY(ftrace_graph_caller)
UNWIND(.fnstart)
	__ftrace_graph_caller
UNWIND(.fnend)
ENDPROC(ftrace_graph_caller)
#endif

.purgem mcount_enter
.purgem mcount_get_lr
.purgem mcount_exit

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	.globl return_to_handler
return_to_handler:
	stmdb	sp!, {r0-r3}
	mov	r0, fp			@ frame pointer
	bl	ftrace_return_to_handler
	mov	lr, r0			@ r0 has real ret addr
	ldmia	sp!, {r0-r3}
	ret	lr
#endif

ENTRY(ftrace_stub)
.Lftrace_stub:
	ret	lr
ENDPROC(ftrace_stub)
