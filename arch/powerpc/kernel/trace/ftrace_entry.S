/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Split from ftrace_64.S
 */

#include <linux/export.h>
#include <linux/magic.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/ftrace.h>
#include <asm/ppc-opcode.h>
#include <asm/thread_info.h>
#include <asm/bug.h>
#include <asm/ptrace.h>

/*
 *
 * ftrace_caller()/ftrace_regs_caller() is the function that replaces _mcount()
 * when ftrace is active.
 *
 * We arrive here after a function A calls function B, and we are the trace
 * function for B. When we enter r1 points to A's stack frame, B has not yet
 * had a chance to allocate one yet.
 *
 * Additionally r2 may point either to the TOC for A, or B, depending on
 * whether B did a TOC setup sequence before calling us.
 *
 * On entry the LR points back to the _mcount() call site, and r0 holds the
 * saved LR as it was on entry to B, ie. the original return address at the
 * call site in A.
 *
 * Our job is to save the register state into a struct pt_regs (on the stack)
 * and then arrange for the ftrace function to be called.
 */
.macro	ftrace_regs_entry allregs
	/* Create a minimal stack frame for representing B */
	PPC_STLU	r1, -STACK_FRAME_MIN_SIZE(r1)

	/* Create our stack frame + pt_regs */
	PPC_STLU	r1,-SWITCH_FRAME_SIZE(r1)

	.if \allregs == 1
	SAVE_GPRS(11, 12, r1)
	.endif

	/* Get the _mcount() call site out of LR */
	mflr	r11

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
	/* Load the ftrace_op */
	PPC_LL	r12, -(MCOUNT_INSN_SIZE*2 + SZL)(r11)

	/* Load direct_call from the ftrace_op */
	PPC_LL	r12, FTRACE_OPS_DIRECT_CALL(r12)
	PPC_LCMPI r12, 0
	.if \allregs == 1
	bne	.Lftrace_direct_call_regs
	.else
	bne	.Lftrace_direct_call
	.endif
#endif

	/* Save the previous LR in pt_regs->link */
	PPC_STL	r0, _LINK(r1)
	/* Also save it in A's stack frame */
	PPC_STL	r0, SWITCH_FRAME_SIZE+STACK_FRAME_MIN_SIZE+LRSAVE(r1)

	/* Save all gprs to pt_regs */
	SAVE_GPR(0, r1)
	SAVE_GPRS(3, 10, r1)

#ifdef CONFIG_PPC64
	/* Ok to continue? */
	lbz	r3, PACA_FTRACE_ENABLED(r13)
	cmpdi	r3, 0
	beq	ftrace_no_trace
#endif

	.if \allregs == 1
	SAVE_GPR(2, r1)
	SAVE_GPRS(13, 31, r1)
	.else
#if defined(CONFIG_LIVEPATCH_64) || defined(CONFIG_PPC_FTRACE_OUT_OF_LINE)
	SAVE_GPR(14, r1)
#endif
	.endif

	/* Save previous stack pointer (r1) */
	addi	r8, r1, SWITCH_FRAME_SIZE+STACK_FRAME_MIN_SIZE
	PPC_STL	r8, GPR1(r1)

	.if \allregs == 1
	/* Load special regs for save below */
	mfcr	r7
	mfmsr   r8
	mfctr   r9
	mfxer   r10
	.else
	/* Clear MSR to flag as ftrace_caller versus frace_regs_caller */
	li	r8, 0
	.endif

#ifdef CONFIG_PPC64
	/* Save callee's TOC in the ABI compliant location */
	std	r2, STK_GOT(r1)
	LOAD_PACA_TOC()		/* get kernel TOC in r2 */
#endif

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_CALL_OPS
	/* r11 points to the instruction following the call to ftrace */
	PPC_LL	r5, -(MCOUNT_INSN_SIZE*2 + SZL)(r11)
	PPC_LL	r12, FTRACE_OPS_FUNC(r5)
	mtctr	r12
#else /* !CONFIG_DYNAMIC_FTRACE_WITH_CALL_OPS */
#ifdef CONFIG_PPC64
	LOAD_REG_ADDR(r3, function_trace_op)
	ld	r5,0(r3)
#else
	lis	r3,function_trace_op@ha
	lwz	r5,function_trace_op@l(r3)
#endif
#endif

	/* Save special regs */
	PPC_STL	r8, _MSR(r1)
	.if \allregs == 1
	PPC_STL	r7, _CCR(r1)
	PPC_STL	r9, _CTR(r1)
	PPC_STL	r10, _XER(r1)
	.endif

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
	/* Clear orig_gpr3 to later detect ftrace_direct call */
	li	r7, 0
	PPC_STL	r7, ORIG_GPR3(r1)
#endif

#ifdef CONFIG_PPC_FTRACE_OUT_OF_LINE
	/* Save our real return address in nvr for return */
	.if \allregs == 0
	SAVE_GPR(15, r1)
	.endif
	mr	r15, r11
	/*
	 * We want the ftrace location in the function, but our lr (in r11)
	 * points at the 'mtlr r0' instruction in the out of line stub.  To
	 * recover the ftrace location, we read the branch instruction in the
	 * stub, and adjust our lr by the branch offset.
	 *
	 * See ftrace_init_ool_stub() for the profile sequence.
	 */
	lwz	r8, MCOUNT_INSN_SIZE(r11)
	slwi	r8, r8, 6
	srawi	r8, r8, 6
	add	r3, r11, r8
	/*
	 * Override our nip to point past the branch in the original function.
	 * This allows reliable stack trace and the ftrace stack tracer to work as-is.
	 */
	addi	r11, r3, MCOUNT_INSN_SIZE
#else
	/* Calculate ip from nip-4 into r3 for call below */
	subi    r3, r11, MCOUNT_INSN_SIZE
#endif

	/* Save NIP as pt_regs->nip */
	PPC_STL	r11, _NIP(r1)
	/* Also save it in B's stackframe header for proper unwind */
	PPC_STL	r11, LRSAVE+SWITCH_FRAME_SIZE(r1)
#if defined(CONFIG_LIVEPATCH_64) || defined(CONFIG_PPC_FTRACE_OUT_OF_LINE)
	mr	r14, r11	/* remember old NIP */
#endif

	/* Put the original return address in r4 as parent_ip */
	mr	r4, r0

	/* Load &pt_regs in r6 for call below */
	addi    r6, r1, STACK_INT_FRAME_REGS
.endm

.macro	ftrace_regs_exit allregs
#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
	/* Check orig_gpr3 to detect ftrace_direct call */
	PPC_LL	r3, ORIG_GPR3(r1)
	PPC_LCMPI cr1, r3, 0
	mtctr	r3
#endif

	/* Restore possibly modified LR */
	PPC_LL	r0, _LINK(r1)

#ifndef CONFIG_PPC_FTRACE_OUT_OF_LINE
	/* Load ctr with the possibly modified NIP */
	PPC_LL	r3, _NIP(r1)
#ifdef CONFIG_LIVEPATCH_64
	cmpd	r14, r3		/* has NIP been altered? */
#endif

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
	beq	cr1,2f
	mtlr	r3
	b	3f
#endif
2:	mtctr	r3
	mtlr	r0
3:

#else /* !CONFIG_PPC_FTRACE_OUT_OF_LINE */
	/* Load LR with the possibly modified NIP */
	PPC_LL	r3, _NIP(r1)
	cmpd	r14, r3		/* has NIP been altered? */
	bne-	1f

	mr	r3, r15
1:	mtlr	r3
	.if \allregs == 0
	REST_GPR(15, r1)
	.endif
#endif

	/* Restore gprs */
	.if \allregs == 1
	REST_GPRS(2, 31, r1)
	.else
	REST_GPRS(3, 10, r1)
#if defined(CONFIG_LIVEPATCH_64) || defined(CONFIG_PPC_FTRACE_OUT_OF_LINE)
	REST_GPR(14, r1)
#endif
	.endif

#ifdef CONFIG_PPC64
	/* Restore callee's TOC */
	ld	r2, STK_GOT(r1)
#endif

	/* Pop our stack frame */
	addi r1, r1, SWITCH_FRAME_SIZE+STACK_FRAME_MIN_SIZE

#ifdef CONFIG_LIVEPATCH_64
        /* Based on the cmpd above, if the NIP was altered handle livepatch */
	bne-	livepatch_handler
#endif

	/* jump after _mcount site */
#ifdef CONFIG_PPC_FTRACE_OUT_OF_LINE
#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
	bnectr	cr1
#endif
	/*
	 * Return with blr to keep the link stack balanced. The function profiling sequence
	 * uses 'mtlr r0' to restore LR.
	 */
	blr
#else
	bctr
#endif
.endm

.macro ftrace_regs_func allregs
#ifdef CONFIG_DYNAMIC_FTRACE_WITH_CALL_OPS
	bctrl
#else
	.if \allregs == 1
.globl ftrace_regs_call
ftrace_regs_call:
	.else
.globl ftrace_call
ftrace_call:
	.endif
	/* ftrace_call(r3, r4, r5, r6) */
	bl	ftrace_stub
#endif
.endm

_GLOBAL(ftrace_regs_caller)
	ftrace_regs_entry 1
	ftrace_regs_func 1
	ftrace_regs_exit 1

_GLOBAL(ftrace_caller)
	ftrace_regs_entry 0
	ftrace_regs_func 0
	ftrace_regs_exit 0

_GLOBAL(ftrace_stub)
	blr

#ifdef CONFIG_PPC64
ftrace_no_trace:
#ifdef CONFIG_PPC_FTRACE_OUT_OF_LINE
	REST_GPR(3, r1)
	addi	r1, r1, SWITCH_FRAME_SIZE+STACK_FRAME_MIN_SIZE
	blr
#else
	mflr	r3
	mtctr	r3
	REST_GPR(3, r1)
	addi	r1, r1, SWITCH_FRAME_SIZE+STACK_FRAME_MIN_SIZE
	mtlr	r0
	bctr
#endif
#endif

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
.Lftrace_direct_call_regs:
	mtctr	r12
	REST_GPRS(11, 12, r1)
	addi	r1, r1, SWITCH_FRAME_SIZE+STACK_FRAME_MIN_SIZE
	bctr
.Lftrace_direct_call:
	mtctr	r12
	addi	r1, r1, SWITCH_FRAME_SIZE+STACK_FRAME_MIN_SIZE
	bctr
SYM_FUNC_START(ftrace_stub_direct_tramp)
	blr
SYM_FUNC_END(ftrace_stub_direct_tramp)
#endif

#ifdef CONFIG_LIVEPATCH_64
	/*
	 * This function runs in the mcount context, between two functions. As
	 * such it can only clobber registers which are volatile and used in
	 * function linkage.
	 *
	 * We get here when a function A, calls another function B, but B has
	 * been live patched with a new function C.
	 *
	 * On entry, we have no stack frame and can not allocate one.
	 *
	 * With PPC_FTRACE_OUT_OF_LINE=n, on entry:
	 *  - LR points back to the original caller (in A)
	 *  - CTR holds the new NIP in C
	 *  - r0, r11 & r12 are free
	 *
	 * With PPC_FTRACE_OUT_OF_LINE=y, on entry:
	 *  - r0 points back to the original caller (in A)
	 *  - LR holds the new NIP in C
	 *  - r11 & r12 are free
	 */
livepatch_handler:
	ld	r12, PACA_THREAD_INFO(r13)

	/* Allocate 3 x 8 bytes */
	ld	r11, TI_livepatch_sp(r12)
	addi	r11, r11, 24
	std	r11, TI_livepatch_sp(r12)

	/* Store stack end marker */
	lis     r12, STACK_END_MAGIC@h
	ori     r12, r12, STACK_END_MAGIC@l
	std	r12, -8(r11)

	/* Save toc & real LR on livepatch stack */
	std	r2,  -24(r11)
#ifndef CONFIG_PPC_FTRACE_OUT_OF_LINE
	mflr	r12
	std	r12, -16(r11)
	mfctr	r12
#else
	std	r0, -16(r11)
	mflr	r12
	/* Put ctr in r12 for global entry and branch there */
	mtctr	r12
#endif
	bctrl

	/*
	 * Now we are returning from the patched function to the original
	 * caller A. We are free to use r11, r12 and we can use r2 until we
	 * restore it.
	 */

	ld	r12, PACA_THREAD_INFO(r13)

	ld	r11, TI_livepatch_sp(r12)

	/* Check stack marker hasn't been trashed */
	lis     r2,  STACK_END_MAGIC@h
	ori     r2,  r2, STACK_END_MAGIC@l
	ld	r12, -8(r11)
1:	tdne	r12, r2
	EMIT_BUG_ENTRY 1b, __FILE__, __LINE__ - 1, 0

	/* Restore LR & toc from livepatch stack */
	ld	r12, -16(r11)
	mtlr	r12
	ld	r2,  -24(r11)

	/* Pop livepatch stack frame */
	ld	r12, PACA_THREAD_INFO(r13)
	subi	r11, r11, 24
	std	r11, TI_livepatch_sp(r12)

	/* Return to original caller of live patched function */
	blr
#endif /* CONFIG_LIVEPATCH */

#ifndef CONFIG_ARCH_USING_PATCHABLE_FUNCTION_ENTRY
_GLOBAL(mcount)
_GLOBAL(_mcount)
EXPORT_SYMBOL(_mcount)
	mflr	r12
	mtctr	r12
	mtlr	r0
	bctr
#endif

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
_GLOBAL(return_to_handler)
	/* need to save return values */
#ifdef CONFIG_PPC64
	std	r4,  -32(r1)
	std	r3,  -24(r1)
	/* save TOC */
	std	r2,  -16(r1)
	std	r31, -8(r1)
	mr	r31, r1
	stdu	r1, -112(r1)

	/*
	 * We might be called from a module.
	 * Switch to our TOC to run inside the core kernel.
	 */
	LOAD_PACA_TOC()
#else
	stwu	r1, -16(r1)
	stw	r3, 8(r1)
	stw	r4, 12(r1)
#endif

	bl	ftrace_return_to_handler
	nop

	/* return value has real return address */
	mtlr	r3

#ifdef CONFIG_PPC64
	ld	r1, 0(r1)
	ld	r4,  -32(r1)
	ld	r3,  -24(r1)
	ld	r2,  -16(r1)
	ld	r31, -8(r1)
#else
	lwz	r3, 8(r1)
	lwz	r4, 12(r1)
	addi	r1, r1, 16
#endif

	/* Jump back to real return address */
	blr
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

#ifdef CONFIG_PPC_FTRACE_OUT_OF_LINE
SYM_DATA(ftrace_ool_stub_text_count, .long CONFIG_PPC_FTRACE_OUT_OF_LINE_NUM_RESERVE)

SYM_START(ftrace_ool_stub_text, SYM_L_GLOBAL, .balign SZL)
	.space CONFIG_PPC_FTRACE_OUT_OF_LINE_NUM_RESERVE * FTRACE_OOL_STUB_SIZE
SYM_CODE_END(ftrace_ool_stub_text)
#endif

.pushsection ".tramp.ftrace.text","aw",@progbits;
.globl ftrace_tramp_text
ftrace_tramp_text:
	.space 32
.popsection

.pushsection ".tramp.ftrace.init","aw",@progbits;
.globl ftrace_tramp_init
ftrace_tramp_init:
	.space 32
.popsection
