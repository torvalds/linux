/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * This file contains the generic code to perform a call to the
 * pSeries LPAR hypervisor.
 */
#include <linux/jump_label.h>
#include <asm/hvcall.h>
#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/ptrace.h>
#include <asm/feature-fixups.h>

	.section	".text"
	
#ifdef CONFIG_TRACEPOINTS

#ifndef CONFIG_JUMP_LABEL
	.data

	.globl hcall_tracepoint_refcount
hcall_tracepoint_refcount:
	.8byte	0

	.section	".text"
#endif

/*
 * precall must preserve all registers.  use unused STK_PARAM()
 * areas to save snapshots and opcode. STK_PARAM() in the caller's
 * frame will be available even on ELFv2 because these are all
 * variadic functions.
 */
#define HCALL_INST_PRECALL(FIRST_REG)				\
	mflr	r0;						\
	std	r3,STK_PARAM(R3)(r1);				\
	std	r4,STK_PARAM(R4)(r1);				\
	std	r5,STK_PARAM(R5)(r1);				\
	std	r6,STK_PARAM(R6)(r1);				\
	std	r7,STK_PARAM(R7)(r1);				\
	std	r8,STK_PARAM(R8)(r1);				\
	std	r9,STK_PARAM(R9)(r1);				\
	std	r10,STK_PARAM(R10)(r1);				\
	std	r0,16(r1);					\
	addi	r4,r1,STK_PARAM(FIRST_REG);			\
	stdu	r1,-STACK_FRAME_MIN_SIZE(r1);			\
	bl	CFUNC(__trace_hcall_entry);			\
	ld	r3,STACK_FRAME_MIN_SIZE+STK_PARAM(R3)(r1);	\
	ld	r4,STACK_FRAME_MIN_SIZE+STK_PARAM(R4)(r1);	\
	ld	r5,STACK_FRAME_MIN_SIZE+STK_PARAM(R5)(r1);	\
	ld	r6,STACK_FRAME_MIN_SIZE+STK_PARAM(R6)(r1);	\
	ld	r7,STACK_FRAME_MIN_SIZE+STK_PARAM(R7)(r1);	\
	ld	r8,STACK_FRAME_MIN_SIZE+STK_PARAM(R8)(r1);	\
	ld	r9,STACK_FRAME_MIN_SIZE+STK_PARAM(R9)(r1);	\
	ld	r10,STACK_FRAME_MIN_SIZE+STK_PARAM(R10)(r1)

/*
 * postcall is performed immediately before function return which
 * allows liberal use of volatile registers.
 */
#define __HCALL_INST_POSTCALL					\
	ld	r0,STACK_FRAME_MIN_SIZE+STK_PARAM(R3)(r1);	\
	std	r3,STACK_FRAME_MIN_SIZE+STK_PARAM(R3)(r1);	\
	mr	r4,r3;						\
	mr	r3,r0;						\
	bl	CFUNC(__trace_hcall_exit);			\
	ld	r0,STACK_FRAME_MIN_SIZE+16(r1);			\
	addi	r1,r1,STACK_FRAME_MIN_SIZE;			\
	ld	r3,STK_PARAM(R3)(r1);				\
	mtlr	r0

#define HCALL_INST_POSTCALL_NORETS				\
	li	r5,0;						\
	__HCALL_INST_POSTCALL

#define HCALL_INST_POSTCALL(BUFREG)				\
	mr	r5,BUFREG;					\
	__HCALL_INST_POSTCALL

#ifdef CONFIG_JUMP_LABEL
#define HCALL_BRANCH(LABEL)					\
	ARCH_STATIC_BRANCH(LABEL, hcall_tracepoint_key)
#else

/*
 * We branch around this in early init (eg when populating the MMU
 * hashtable) by using an unconditional cpu feature.
 */
#define HCALL_BRANCH(LABEL)					\
BEGIN_FTR_SECTION;						\
	b	1f;						\
END_FTR_SECTION(0, 1);						\
	LOAD_REG_ADDR(r12, hcall_tracepoint_refcount) ;		\
	ld	r12,0(r12);					\
	std	r12,32(r1);					\
	cmpdi	r12,0;						\
	bne-	LABEL;						\
1:
#endif

#else
#define HCALL_INST_PRECALL(FIRST_ARG)
#define HCALL_INST_POSTCALL_NORETS
#define HCALL_INST_POSTCALL(BUFREG)
#define HCALL_BRANCH(LABEL)
#endif

_GLOBAL_TOC(plpar_hcall_norets_notrace)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)
	HVSC				/* invoke the hypervisor */

	li	r4,0
	stb	r4,PACASRR_VALID(r13)

	lwz	r0,8(r1)
	mtcrf	0xff,r0
	blr				/* return r3 = status */

_GLOBAL_TOC(plpar_hcall_norets)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)
	HCALL_BRANCH(plpar_hcall_norets_trace)
	HVSC				/* invoke the hypervisor */

	li	r4,0
	stb	r4,PACASRR_VALID(r13)

	lwz	r0,8(r1)
	mtcrf	0xff,r0
	blr				/* return r3 = status */

#ifdef CONFIG_TRACEPOINTS
plpar_hcall_norets_trace:
	HCALL_INST_PRECALL(R4)
	HVSC
	HCALL_INST_POSTCALL_NORETS

	li	r4,0
	stb	r4,PACASRR_VALID(r13)

	lwz	r0,8(r1)
	mtcrf	0xff,r0
	blr
#endif

_GLOBAL_TOC(plpar_hcall)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	HCALL_BRANCH(plpar_hcall_trace)

	std     r4,STK_PARAM(R4)(r1)     /* Save ret buffer */

	mr	r4,r5
	mr	r5,r6
	mr	r6,r7
	mr	r7,r8
	mr	r8,r9
	mr	r9,r10

	HVSC				/* invoke the hypervisor */

	ld	r12,STK_PARAM(R4)(r1)
	std	r4,  0(r12)
	std	r5,  8(r12)
	std	r6, 16(r12)
	std	r7, 24(r12)

	li	r4,0
	stb	r4,PACASRR_VALID(r13)

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr				/* return r3 = status */

#ifdef CONFIG_TRACEPOINTS
plpar_hcall_trace:
	HCALL_INST_PRECALL(R5)

	std	r4,STK_PARAM(R4)(r1)
	mr	r0,r4

	mr	r4,r5
	mr	r5,r6
	mr	r6,r7
	mr	r7,r8
	mr	r8,r9
	mr	r9,r10

	HVSC

	ld	r12,STK_PARAM(R4)(r1)
	std	r4,0(r12)
	std	r5,8(r12)
	std	r6,16(r12)
	std	r7,24(r12)

	HCALL_INST_POSTCALL(r12)

	li	r4,0
	stb	r4,PACASRR_VALID(r13)

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr
#endif

/*
 * plpar_hcall_raw can be called in real mode. kexec/kdump need some
 * hypervisor calls to be executed in real mode. So plpar_hcall_raw
 * does not access the per cpu hypervisor call statistics variables,
 * since these variables may not be present in the RMO region.
 */
_GLOBAL(plpar_hcall_raw)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	std     r4,STK_PARAM(R4)(r1)     /* Save ret buffer */

	mr	r4,r5
	mr	r5,r6
	mr	r6,r7
	mr	r7,r8
	mr	r8,r9
	mr	r9,r10

	HVSC				/* invoke the hypervisor */

	ld	r12,STK_PARAM(R4)(r1)
	std	r4,  0(r12)
	std	r5,  8(r12)
	std	r6, 16(r12)
	std	r7, 24(r12)

	li	r4,0
	stb	r4,PACASRR_VALID(r13)

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr				/* return r3 = status */

_GLOBAL_TOC(plpar_hcall9)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	HCALL_BRANCH(plpar_hcall9_trace)

	std     r4,STK_PARAM(R4)(r1)     /* Save ret buffer */

	mr	r4,r5
	mr	r5,r6
	mr	r6,r7
	mr	r7,r8
	mr	r8,r9
	mr	r9,r10
	ld	r10,STK_PARAM(R11)(r1)	 /* put arg7 in R10 */
	ld	r11,STK_PARAM(R12)(r1)	 /* put arg8 in R11 */
	ld	r12,STK_PARAM(R13)(r1)    /* put arg9 in R12 */

	HVSC				/* invoke the hypervisor */

	mr	r0,r12
	ld	r12,STK_PARAM(R4)(r1)
	std	r4,  0(r12)
	std	r5,  8(r12)
	std	r6, 16(r12)
	std	r7, 24(r12)
	std	r8, 32(r12)
	std	r9, 40(r12)
	std	r10,48(r12)
	std	r11,56(r12)
	std	r0, 64(r12)

	li	r4,0
	stb	r4,PACASRR_VALID(r13)

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr				/* return r3 = status */

#ifdef CONFIG_TRACEPOINTS
plpar_hcall9_trace:
	HCALL_INST_PRECALL(R5)

	std	r4,STK_PARAM(R4)(r1)
	mr	r0,r4

	mr	r4,r5
	mr	r5,r6
	mr	r6,r7
	mr	r7,r8
	mr	r8,r9
	mr	r9,r10
	ld	r10,STACK_FRAME_MIN_SIZE+STK_PARAM(R11)(r1)
	ld	r11,STACK_FRAME_MIN_SIZE+STK_PARAM(R12)(r1)
	ld	r12,STACK_FRAME_MIN_SIZE+STK_PARAM(R13)(r1)

	HVSC

	mr	r0,r12
	ld	r12,STACK_FRAME_MIN_SIZE+STK_PARAM(R4)(r1)
	std	r4,0(r12)
	std	r5,8(r12)
	std	r6,16(r12)
	std	r7,24(r12)
	std	r8,32(r12)
	std	r9,40(r12)
	std	r10,48(r12)
	std	r11,56(r12)
	std	r0,64(r12)

	HCALL_INST_POSTCALL(r12)

	li	r4,0
	stb	r4,PACASRR_VALID(r13)

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr
#endif

/* See plpar_hcall_raw to see why this is needed */
_GLOBAL(plpar_hcall9_raw)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	std     r4,STK_PARAM(R4)(r1)     /* Save ret buffer */

	mr	r4,r5
	mr	r5,r6
	mr	r6,r7
	mr	r7,r8
	mr	r8,r9
	mr	r9,r10
	ld	r10,STK_PARAM(R11)(r1)	 /* put arg7 in R10 */
	ld	r11,STK_PARAM(R12)(r1)	 /* put arg8 in R11 */
	ld	r12,STK_PARAM(R13)(r1)    /* put arg9 in R12 */

	HVSC				/* invoke the hypervisor */

	mr	r0,r12
	ld	r12,STK_PARAM(R4)(r1)
	std	r4,  0(r12)
	std	r5,  8(r12)
	std	r6, 16(r12)
	std	r7, 24(r12)
	std	r8, 32(r12)
	std	r9, 40(r12)
	std	r10,48(r12)
	std	r11,56(r12)
	std	r0, 64(r12)

	li	r4,0
	stb	r4,PACASRR_VALID(r13)

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr				/* return r3 = status */
