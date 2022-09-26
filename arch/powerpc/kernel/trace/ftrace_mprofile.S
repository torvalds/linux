/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Split from ftrace_64.S
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
	/* Create our stack frame + pt_regs */
	PPC_STLU	r1,-SWITCH_FRAME_SIZE(r1)

	/* Save all gprs to pt_regs */
	SAVE_GPR(0, r1)
	SAVE_GPRS(3, 10, r1)

#ifdef CONFIG_PPC64
	/* Save the original return address in A's stack frame */
	std	r0, LRSAVE+SWITCH_FRAME_SIZE(r1)
	/* Ok to continue? */
	lbz	r3, PACA_FTRACE_ENABLED(r13)
	cmpdi	r3, 0
	beq	ftrace_no_trace
#endif

	.if \allregs == 1
	SAVE_GPR(2, r1)
	SAVE_GPRS(11, 31, r1)
	.else
#ifdef CONFIG_LIVEPATCH_64
	SAVE_GPR(14, r1)
#endif
	.endif

	/* Save previous stack pointer (r1) */
	addi	r8, r1, SWITCH_FRAME_SIZE
	PPC_STL	r8, GPR1(r1)

	.if \allregs == 1
	/* Load special regs for save below */
	mfmsr   r8
	mfctr   r9
	mfxer   r10
	mfcr	r11
	.else
	/* Clear MSR to flag as ftrace_caller versus frace_regs_caller */
	li	r8, 0
	.endif

	/* Get the _mcount() call site out of LR */
	mflr	r7
	/* Save it as pt_regs->nip */
	PPC_STL	r7, _NIP(r1)
	/* Save the read LR in pt_regs->link */
	PPC_STL	r0, _LINK(r1)

#ifdef CONFIG_PPC64
	/* Save callee's TOC in the ABI compliant location */
	std	r2, STK_GOT(r1)
	LOAD_PACA_TOC()		/* get kernel TOC in r2 */
	LOAD_REG_ADDR(r3, function_trace_op)
	ld	r5,0(r3)
#else
	lis	r3,function_trace_op@ha
	lwz	r5,function_trace_op@l(r3)
#endif

#ifdef CONFIG_LIVEPATCH_64
	mr	r14, r7		/* remember old NIP */
#endif

	/* Calculate ip from nip-4 into r3 for call below */
	subi    r3, r7, MCOUNT_INSN_SIZE

	/* Put the original return address in r4 as parent_ip */
	mr	r4, r0

	/* Save special regs */
	PPC_STL	r8, _MSR(r1)
	.if \allregs == 1
	PPC_STL	r9, _CTR(r1)
	PPC_STL	r10, _XER(r1)
	PPC_STL	r11, _CCR(r1)
	.endif

	/* Load &pt_regs in r6 for call below */
	addi    r6, r1, STACK_FRAME_OVERHEAD
.endm

.macro	ftrace_regs_exit allregs
	/* Load ctr with the possibly modified NIP */
	PPC_LL	r3, _NIP(r1)
	mtctr	r3

#ifdef CONFIG_LIVEPATCH_64
	cmpd	r14, r3		/* has NIP been altered? */
#endif

	/* Restore gprs */
	.if \allregs == 1
	REST_GPRS(2, 31, r1)
	.else
	REST_GPRS(3, 10, r1)
#ifdef CONFIG_LIVEPATCH_64
	REST_GPR(14, r1)
#endif
	.endif

	/* Restore possibly modified LR */
	PPC_LL	r0, _LINK(r1)
	mtlr	r0

#ifdef CONFIG_PPC64
	/* Restore callee's TOC */
	ld	r2, STK_GOT(r1)
#endif

	/* Pop our stack frame */
	addi r1, r1, SWITCH_FRAME_SIZE

#ifdef CONFIG_LIVEPATCH_64
        /* Based on the cmpd above, if the NIP was altered handle livepatch */
	bne-	livepatch_handler
#endif
	bctr			/* jump after _mcount site */
.endm

_GLOBAL(ftrace_regs_caller)
	ftrace_regs_entry 1
	/* ftrace_call(r3, r4, r5, r6) */
.globl ftrace_regs_call
ftrace_regs_call:
	bl	ftrace_stub
	nop
	ftrace_regs_exit 1

_GLOBAL(ftrace_caller)
	ftrace_regs_entry 0
	/* ftrace_call(r3, r4, r5, r6) */
.globl ftrace_call
ftrace_call:
	bl	ftrace_stub
	nop
	ftrace_regs_exit 0

_GLOBAL(ftrace_stub)
	blr

#ifdef CONFIG_PPC64
ftrace_no_trace:
	mflr	r3
	mtctr	r3
	REST_GPR(3, r1)
	addi	r1, r1, SWITCH_FRAME_SIZE
	mtlr	r0
	bctr
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
	 * On entry:
	 *  - we have no stack frame and can not allocate one
	 *  - LR points back to the original caller (in A)
	 *  - CTR holds the new NIP in C
	 *  - r0, r11 & r12 are free
	 */
livepatch_handler:
	ld	r12, PACA_THREAD_INFO(r13)

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
