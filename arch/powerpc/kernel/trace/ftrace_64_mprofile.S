/*
 * Split from ftrace_64.S
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/magic.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/ftrace.h>
#include <asm/ppc-opcode.h>
#include <asm/export.h>
#include <asm/thread_info.h>
#include <asm/bug.h>
#include <asm/ptrace.h>

#ifdef CONFIG_DYNAMIC_FTRACE
/*
 *
 * ftrace_caller() is the function that replaces _mcount() when ftrace is
 * active.
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
_GLOBAL(ftrace_caller)
	/* Save the original return address in A's stack frame */
	std	r0,LRSAVE(r1)

	/* Create our stack frame + pt_regs */
	stdu	r1,-SWITCH_FRAME_SIZE(r1)

	/* Save all gprs to pt_regs */
	SAVE_GPR(0, r1)
	SAVE_10GPRS(2, r1)

	/* Ok to continue? */
	lbz	r3, PACA_FTRACE_ENABLED(r13)
	cmpdi	r3, 0
	beq	ftrace_no_trace

	SAVE_10GPRS(12, r1)
	SAVE_10GPRS(22, r1)

	/* Save previous stack pointer (r1) */
	addi	r8, r1, SWITCH_FRAME_SIZE
	std	r8, GPR1(r1)

	/* Load special regs for save below */
	mfmsr   r8
	mfctr   r9
	mfxer   r10
	mfcr	r11

	/* Get the _mcount() call site out of LR */
	mflr	r7
	/* Save it as pt_regs->nip */
	std     r7, _NIP(r1)
	/* Save the read LR in pt_regs->link */
	std     r0, _LINK(r1)

	/* Save callee's TOC in the ABI compliant location */
	std	r2, 24(r1)
	ld	r2,PACATOC(r13)	/* get kernel TOC in r2 */

	addis	r3,r2,function_trace_op@toc@ha
	addi	r3,r3,function_trace_op@toc@l
	ld	r5,0(r3)

#ifdef CONFIG_LIVEPATCH
	mr	r14,r7		/* remember old NIP */
#endif
	/* Calculate ip from nip-4 into r3 for call below */
	subi    r3, r7, MCOUNT_INSN_SIZE

	/* Put the original return address in r4 as parent_ip */
	mr	r4, r0

	/* Save special regs */
	std     r8, _MSR(r1)
	std     r9, _CTR(r1)
	std     r10, _XER(r1)
	std     r11, _CCR(r1)

	/* Load &pt_regs in r6 for call below */
	addi    r6, r1 ,STACK_FRAME_OVERHEAD

	/* ftrace_call(r3, r4, r5, r6) */
.globl ftrace_call
ftrace_call:
	bl	ftrace_stub
	nop

	/* Load the possibly modified NIP */
	ld	r15, _NIP(r1)

#ifdef CONFIG_LIVEPATCH
	cmpd	r14, r15	/* has NIP been altered? */
#endif

#if defined(CONFIG_LIVEPATCH) && defined(CONFIG_KPROBES_ON_FTRACE)
	/* NIP has not been altered, skip over further checks */
	beq	1f

	/* Check if there is an active jprobe on us */
	subi	r3, r14, 4
	bl	__is_active_jprobe
	nop

	/*
	 * If r3 == 1, then this is a kprobe/jprobe.
	 * else, this is livepatched function.
	 *
	 * The conditional branch for livepatch_handler below will use the
	 * result of this comparison. For kprobe/jprobe, we just need to branch to
	 * the new NIP, not call livepatch_handler. The branch below is bne, so we
	 * want CR0[EQ] to be true if this is a kprobe/jprobe. Which means we want
	 * CR0[EQ] = (r3 == 1).
	 */
	cmpdi	r3, 1
1:
#endif

	/* Load CTR with the possibly modified NIP */
	mtctr	r15

	/* Restore gprs */
	REST_GPR(0,r1)
	REST_10GPRS(2,r1)
	REST_10GPRS(12,r1)
	REST_10GPRS(22,r1)

	/* Restore possibly modified LR */
	ld	r0, _LINK(r1)
	mtlr	r0

	/* Restore callee's TOC */
	ld	r2, 24(r1)

	/* Pop our stack frame */
	addi r1, r1, SWITCH_FRAME_SIZE

#ifdef CONFIG_LIVEPATCH
        /*
	 * Based on the cmpd or cmpdi above, if the NIP was altered and we're
	 * not on a kprobe/jprobe, then handle livepatch.
	 */
	bne-	livepatch_handler
#endif

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
.globl ftrace_graph_call
ftrace_graph_call:
	b	ftrace_graph_stub
_GLOBAL(ftrace_graph_stub)
#endif

	bctr			/* jump after _mcount site */

_GLOBAL(ftrace_stub)
	blr

ftrace_no_trace:
	mflr	r3
	mtctr	r3
	REST_GPR(3, r1)
	addi	r1, r1, SWITCH_FRAME_SIZE
	mtlr	r0
	bctr

#ifdef CONFIG_LIVEPATCH
	/*
	 * This function runs in the mcount context, between two functions. As
	 * such it can only clobber registers which are volatile and used in
	 * function linkage.
	 *
	 * We get here when a function A, calls another function B, but B has
	 * been live patched with a new function C.
	 *
	 * On entry:
	 *  - we have no stack frame and can not allocate one
	 *  - LR points back to the original caller (in A)
	 *  - CTR holds the new NIP in C
	 *  - r0, r11 & r12 are free
	 */
livepatch_handler:
	CURRENT_THREAD_INFO(r12, r1)

	/* Allocate 3 x 8 bytes */
	ld	r11, TI_livepatch_sp(r12)
	addi	r11, r11, 24
	std	r11, TI_livepatch_sp(r12)

	/* Save toc & real LR on livepatch stack */
	std	r2,  -24(r11)
	mflr	r12
	std	r12, -16(r11)

	/* Store stack end marker */
	lis     r12, STACK_END_MAGIC@h
	ori     r12, r12, STACK_END_MAGIC@l
	std	r12, -8(r11)

	/* Put ctr in r12 for global entry and branch there */
	mfctr	r12
	bctrl

	/*
	 * Now we are returning from the patched function to the original
	 * caller A. We are free to use r11, r12 and we can use r2 until we
	 * restore it.
	 */

	CURRENT_THREAD_INFO(r12, r1)

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
	CURRENT_THREAD_INFO(r12, r1)
	subi	r11, r11, 24
	std	r11, TI_livepatch_sp(r12)

	/* Return to original caller of live patched function */
	blr
#endif /* CONFIG_LIVEPATCH */

#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
_GLOBAL(ftrace_graph_caller)
	stdu	r1, -112(r1)
	/* with -mprofile-kernel, parameter regs are still alive at _mcount */
	std	r10, 104(r1)
	std	r9, 96(r1)
	std	r8, 88(r1)
	std	r7, 80(r1)
	std	r6, 72(r1)
	std	r5, 64(r1)
	std	r4, 56(r1)
	std	r3, 48(r1)

	/* Save callee's TOC in the ABI compliant location */
	std	r2, 24(r1)
	ld	r2, PACATOC(r13)	/* get kernel TOC in r2 */

	mfctr	r4		/* ftrace_caller has moved local addr here */
	std	r4, 40(r1)
	mflr	r3		/* ftrace_caller has restored LR from stack */
	subi	r4, r4, MCOUNT_INSN_SIZE

	bl	prepare_ftrace_return
	nop

	/*
	 * prepare_ftrace_return gives us the address we divert to.
	 * Change the LR to this.
	 */
	mtlr	r3

	ld	r0, 40(r1)
	mtctr	r0
	ld	r10, 104(r1)
	ld	r9, 96(r1)
	ld	r8, 88(r1)
	ld	r7, 80(r1)
	ld	r6, 72(r1)
	ld	r5, 64(r1)
	ld	r4, 56(r1)
	ld	r3, 48(r1)

	/* Restore callee's TOC */
	ld	r2, 24(r1)

	addi	r1, r1, 112
	mflr	r0
	std	r0, LRSAVE(r1)
	bctr
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
