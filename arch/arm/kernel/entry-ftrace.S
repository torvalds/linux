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
 * Newer GCCs (4.4+) solve this problem by using a version of mcount with call
 * sites like:
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
 * When using dynamic ftrace, we patch out the mcount call by a "pop {lr}"
 * instead of the __gnu_mcount_nc call (see arch/arm/kernel/ftrace.c).
 */

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

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS

.macro __ftrace_regs_caller

	sub	sp, sp, #8	@ space for PC and CPSR OLD_R0,
				@ OLD_R0 will overwrite previous LR

	add 	ip, sp, #12	@ move in IP the value of SP as it was
				@ before the push {lr} of the mcount mechanism

	str     lr, [sp, #0]    @ store LR instead of PC

	ldr     lr, [sp, #8]    @ get previous LR

	str	r0, [sp, #8]	@ write r0 as OLD_R0 over previous LR

	stmdb   sp!, {ip, lr}
	stmdb   sp!, {r0-r11, lr}

	@ stack content at this point:
	@ 0  4          48   52       56            60   64    68       72
	@ R0 | R1 | ... | LR | SP + 4 | previous LR | LR | PSR | OLD_R0 |

	mov r3, sp				@ struct pt_regs*

	ldr r2, =function_trace_op
	ldr r2, [r2]				@ pointer to the current
						@ function tracing op

	ldr	r1, [sp, #S_LR]			@ lr of instrumented func

	ldr	lr, [sp, #S_PC]			@ get LR

	mcount_adjust_addr	r0, lr		@ instrumented function

	.globl ftrace_regs_call
ftrace_regs_call:
	bl	ftrace_stub

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	.globl ftrace_graph_regs_call
ftrace_graph_regs_call:
	mov	r0, r0
#endif

	@ pop saved regs
	ldmia   sp!, {r0-r12}			@ restore r0 through r12
	ldr	ip, [sp, #8]			@ restore PC
	ldr	lr, [sp, #4]			@ restore LR
	ldr	sp, [sp, #0]			@ restore SP
	mov	pc, ip				@ return
.endm

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
.macro __ftrace_graph_regs_caller

	sub     r0, fp, #4              @ lr of instrumented routine (parent)

	@ called from __ftrace_regs_caller
	ldr     r1, [sp, #S_PC]		@ instrumented routine (func)
	mcount_adjust_addr	r1, r1

	mov	r2, fp			@ frame pointer
	bl	prepare_ftrace_return

	@ pop registers saved in ftrace_regs_caller
	ldmia   sp!, {r0-r12}			@ restore r0 through r12
	ldr	ip, [sp, #8]			@ restore PC
	ldr	lr, [sp, #4]			@ restore LR
	ldr	sp, [sp, #0]			@ restore SP
	mov	pc, ip				@ return

.endm
#endif
#endif

.macro __ftrace_caller suffix
	mcount_enter

	mcount_get_lr	r1			@ lr of instrumented func
	mcount_adjust_addr	r0, lr		@ instrumented function

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
	ldr r2, =function_trace_op
	ldr r2, [r2]				@ pointer to the current
						@ function tracing op
	mov r3, #0				@ regs is NULL
#endif

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

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
ENTRY(ftrace_regs_caller)
UNWIND(.fnstart)
	__ftrace_regs_caller
UNWIND(.fnend)
ENDPROC(ftrace_regs_caller)
#endif

#endif

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
ENTRY(ftrace_graph_caller)
UNWIND(.fnstart)
	__ftrace_graph_caller
UNWIND(.fnend)
ENDPROC(ftrace_graph_caller)

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
ENTRY(ftrace_graph_regs_caller)
UNWIND(.fnstart)
	__ftrace_graph_regs_caller
UNWIND(.fnend)
ENDPROC(ftrace_graph_regs_caller)
#endif
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
