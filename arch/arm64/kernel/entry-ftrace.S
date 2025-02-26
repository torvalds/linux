/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm64/kernel/entry-ftrace.S
 *
 * Copyright (C) 2013 Linaro Limited
 * Author: AKASHI Takahiro <takahiro.akashi@linaro.org>
 */

#include <linux/linkage.h>
#include <linux/cfi_types.h>
#include <asm/asm-offsets.h>
#include <asm/assembler.h>
#include <asm/ftrace.h>
#include <asm/insn.h>

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_ARGS
/*
 * Due to -fpatchable-function-entry=2, the compiler has placed two NOPs before
 * the regular function prologue. For an enabled callsite, ftrace_init_nop() and
 * ftrace_make_call() have patched those NOPs to:
 *
 * 	MOV	X9, LR
 * 	BL	ftrace_caller
 *
 * Each instrumented function follows the AAPCS, so here x0-x8 and x18-x30 are
 * live (x18 holds the Shadow Call Stack pointer), and x9-x17 are safe to
 * clobber.
 *
 * We save the callsite's context into a struct ftrace_regs before invoking any
 * ftrace callbacks. So that we can get a sensible backtrace, we create frame
 * records for the callsite and the ftrace entry assembly. This is not
 * sufficient for reliable stacktrace: until we create the callsite stack
 * record, its caller is missing from the LR and existing chain of frame
 * records.
 */
SYM_CODE_START(ftrace_caller)
	bti	c

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_CALL_OPS
	/*
	 * The literal pointer to the ops is at an 8-byte aligned boundary
	 * which is either 12 or 16 bytes before the BL instruction in the call
	 * site. See ftrace_call_adjust() for details.
	 *
	 * Therefore here the LR points at `literal + 16` or `literal + 20`,
	 * and we can find the address of the literal in either case by
	 * aligning to an 8-byte boundary and subtracting 16. We do the
	 * alignment first as this allows us to fold the subtraction into the
	 * LDR.
	 */
	bic	x11, x30, 0x7
	ldr	x11, [x11, #-(4 * AARCH64_INSN_SIZE)]		// op

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
	/*
	 * If the op has a direct call, handle it immediately without
	 * saving/restoring registers.
	 */
	ldr	x17, [x11, #FTRACE_OPS_DIRECT_CALL]		// op->direct_call
	cbnz	x17, ftrace_caller_direct
#endif
#endif

	/* Save original SP */
	mov	x10, sp

	/* Make room for ftrace regs, plus two frame records */
	sub	sp, sp, #(FREGS_SIZE + 32)

	/* Save function arguments */
	stp	x0, x1, [sp, #FREGS_X0]
	stp	x2, x3, [sp, #FREGS_X2]
	stp	x4, x5, [sp, #FREGS_X4]
	stp	x6, x7, [sp, #FREGS_X6]
	str	x8,     [sp, #FREGS_X8]

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
	str	xzr, [sp, #FREGS_DIRECT_TRAMP]
#endif

	/* Save the callsite's FP, LR, SP */
	str	x29, [sp, #FREGS_FP]
	str	x9,  [sp, #FREGS_LR]
	str	x10, [sp, #FREGS_SP]

	/* Save the PC after the ftrace callsite */
	str	x30, [sp, #FREGS_PC]

	/* Create a frame record for the callsite above the ftrace regs */
	stp	x29, x9, [sp, #FREGS_SIZE + 16]
	add	x29, sp, #FREGS_SIZE + 16

	/* Create our frame record above the ftrace regs */
	stp	x29, x30, [sp, #FREGS_SIZE]
	add	x29, sp, #FREGS_SIZE

	/* Prepare arguments for the the tracer func */
	sub	x0, x30, #AARCH64_INSN_SIZE		// ip (callsite's BL insn)
	mov	x1, x9					// parent_ip (callsite's LR)
	mov	x3, sp					// regs

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_CALL_OPS
	mov	x2, x11					// op
	ldr	x4, [x2, #FTRACE_OPS_FUNC]		// op->func
	blr	x4					// op->func(ip, parent_ip, op, regs)

#else
	ldr_l   x2, function_trace_op			// op

SYM_INNER_LABEL(ftrace_call, SYM_L_GLOBAL)
	bl      ftrace_stub				// func(ip, parent_ip, op, regs)
#endif

/*
 * At the callsite x0-x8 and x19-x30 were live. Any C code will have preserved
 * x19-x29 per the AAPCS, and we created frame records upon entry, so we need
 * to restore x0-x8, x29, and x30.
 */
	/* Restore function arguments */
	ldp	x0, x1, [sp, #FREGS_X0]
	ldp	x2, x3, [sp, #FREGS_X2]
	ldp	x4, x5, [sp, #FREGS_X4]
	ldp	x6, x7, [sp, #FREGS_X6]
	ldr	x8,     [sp, #FREGS_X8]

	/* Restore the callsite's FP */
	ldr	x29, [sp, #FREGS_FP]

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
	ldr	x17, [sp, #FREGS_DIRECT_TRAMP]
	cbnz	x17, ftrace_caller_direct_late
#endif

	/* Restore the callsite's LR and PC */
	ldr	x30, [sp, #FREGS_LR]
	ldr	x9,  [sp, #FREGS_PC]

	/* Restore the callsite's SP */
	add	sp, sp, #FREGS_SIZE + 32

	ret	x9

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
SYM_INNER_LABEL(ftrace_caller_direct_late, SYM_L_LOCAL)
	/*
	 * Head to a direct trampoline in x17 after having run other tracers.
	 * The ftrace_regs are live, and x0-x8 and FP have been restored. The
	 * LR, PC, and SP have not been restored.
	 */

	/*
	 * Restore the callsite's LR and PC matching the trampoline calling
	 * convention.
	 */
	ldr	x9,  [sp, #FREGS_LR]
	ldr	x30, [sp, #FREGS_PC]

	/* Restore the callsite's SP */
	add	sp, sp, #FREGS_SIZE + 32

SYM_INNER_LABEL(ftrace_caller_direct, SYM_L_LOCAL)
	/*
	 * Head to a direct trampoline in x17.
	 *
	 * We use `BR X17` as this can safely land on a `BTI C` or `PACIASP` in
	 * the trampoline, and will not unbalance any return stack.
	 */
	br	x17
#endif /* CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS */
SYM_CODE_END(ftrace_caller)

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
SYM_CODE_START(ftrace_stub_direct_tramp)
	bti	c
	mov	x10, x30
	mov	x30, x9
	ret	x10
SYM_CODE_END(ftrace_stub_direct_tramp)
#endif /* CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS */

#else /* CONFIG_DYNAMIC_FTRACE_WITH_ARGS */

/*
 * Gcc with -pg will put the following code in the beginning of each function:
 *      mov x0, x30
 *      bl _mcount
 *	[function's body ...]
 * "bl _mcount" may be replaced to "bl ftrace_caller" or NOP if dynamic
 * ftrace is enabled.
 *
 * Please note that x0 as an argument will not be used here because we can
 * get lr(x30) of instrumented function at any time by winding up call stack
 * as long as the kernel is compiled without -fomit-frame-pointer.
 * (or CONFIG_FRAME_POINTER, this is forced on arm64)
 *
 * stack layout after mcount_enter in _mcount():
 *
 * current sp/fp =>  0:+-----+
 * in _mcount()        | x29 | -> instrumented function's fp
 *                     +-----+
 *                     | x30 | -> _mcount()'s lr (= instrumented function's pc)
 * old sp       => +16:+-----+
 * when instrumented   |     |
 * function calls      | ... |
 * _mcount()           |     |
 *                     |     |
 * instrumented => +xx:+-----+
 * function's fp       | x29 | -> parent's fp
 *                     +-----+
 *                     | x30 | -> instrumented function's lr (= parent's pc)
 *                     +-----+
 *                     | ... |
 */

	.macro mcount_enter
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp
	.endm

	.macro mcount_exit
	ldp	x29, x30, [sp], #16
	ret
	.endm

	.macro mcount_adjust_addr rd, rn
	sub	\rd, \rn, #AARCH64_INSN_SIZE
	.endm

	/* for instrumented function's parent */
	.macro mcount_get_parent_fp reg
	ldr	\reg, [x29]
	ldr	\reg, [\reg]
	.endm

	/* for instrumented function */
	.macro mcount_get_pc0 reg
	mcount_adjust_addr	\reg, x30
	.endm

	.macro mcount_get_pc reg
	ldr	\reg, [x29, #8]
	mcount_adjust_addr	\reg, \reg
	.endm

	.macro mcount_get_lr reg
	ldr	\reg, [x29]
	ldr	\reg, [\reg, #8]
	.endm

	.macro mcount_get_lr_addr reg
	ldr	\reg, [x29]
	add	\reg, \reg, #8
	.endm

/*
 * _mcount() is used to build the kernel with -pg option, but all the branch
 * instructions to _mcount() are replaced to NOP initially at kernel start up,
 * and later on, NOP to branch to ftrace_caller() when enabled or branch to
 * NOP when disabled per-function base.
 */
SYM_FUNC_START(_mcount)
	ret
SYM_FUNC_END(_mcount)
EXPORT_SYMBOL(_mcount)
NOKPROBE(_mcount)

/*
 * void ftrace_caller(unsigned long return_address)
 * @return_address: return address to instrumented function
 *
 * This function is a counterpart of _mcount() in 'static' ftrace, and
 * makes calls to:
 *     - tracer function to probe instrumented function's entry,
 *     - ftrace_graph_caller to set up an exit hook
 */
SYM_FUNC_START(ftrace_caller)
	mcount_enter

	mcount_get_pc0	x0		//     function's pc
	mcount_get_lr	x1		//     function's lr

SYM_INNER_LABEL(ftrace_call, SYM_L_GLOBAL)	// tracer(pc, lr);
	nop				// This will be replaced with "bl xxx"
					// where xxx can be any kind of tracer.

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
SYM_INNER_LABEL(ftrace_graph_call, SYM_L_GLOBAL) // ftrace_graph_caller();
	nop				// If enabled, this will be replaced
					// "b ftrace_graph_caller"
#endif

	mcount_exit
SYM_FUNC_END(ftrace_caller)

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
/*
 * void ftrace_graph_caller(void)
 *
 * Called from _mcount() or ftrace_caller() when function_graph tracer is
 * selected.
 * This function w/ prepare_ftrace_return() fakes link register's value on
 * the call stack in order to intercept instrumented function's return path
 * and run return_to_handler() later on its exit.
 */
SYM_FUNC_START(ftrace_graph_caller)
	mcount_get_pc		  x0	//     function's pc
	mcount_get_lr_addr	  x1	//     pointer to function's saved lr
	mcount_get_parent_fp	  x2	//     parent's fp
	bl	prepare_ftrace_return	// prepare_ftrace_return(pc, &lr, fp)

	mcount_exit
SYM_FUNC_END(ftrace_graph_caller)
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
#endif /* CONFIG_DYNAMIC_FTRACE_WITH_ARGS */

SYM_TYPED_FUNC_START(ftrace_stub)
	ret
SYM_FUNC_END(ftrace_stub)

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
SYM_TYPED_FUNC_START(ftrace_stub_graph)
	ret
SYM_FUNC_END(ftrace_stub_graph)

/*
 * void return_to_handler(void)
 *
 * Run ftrace_return_to_handler() before going back to parent.
 * @fp is checked against the value passed by ftrace_graph_caller().
 */
SYM_CODE_START(return_to_handler)
	/* Make room for ftrace_regs */
	sub	sp, sp, #FREGS_SIZE

	/* Save return value regs */
	stp	x0, x1, [sp, #FREGS_X0]
	stp	x2, x3, [sp, #FREGS_X2]
	stp	x4, x5, [sp, #FREGS_X4]
	stp	x6, x7, [sp, #FREGS_X6]

	/* Save the callsite's FP */
	str	x29, [sp, #FREGS_FP]

	mov	x0, sp
	bl	ftrace_return_to_handler	// addr = ftrace_return_to_hander(fregs);
	mov	x30, x0				// restore the original return address

	/* Restore return value regs */
	ldp	x0, x1, [sp, #FREGS_X0]
	ldp	x2, x3, [sp, #FREGS_X2]
	ldp	x4, x5, [sp, #FREGS_X4]
	ldp	x6, x7, [sp, #FREGS_X6]
	add	sp, sp, #FREGS_SIZE

	ret
SYM_CODE_END(return_to_handler)
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
