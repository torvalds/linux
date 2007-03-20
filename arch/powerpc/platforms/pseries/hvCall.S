/*
 * This file contains the generic code to perform a call to the
 * pSeries LPAR hypervisor.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <asm/hvcall.h>
#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
	
#define STK_PARM(i)     (48 + ((i)-3)*8)

#ifdef CONFIG_HCALL_STATS
/*
 * precall must preserve all registers.  use unused STK_PARM()
 * areas to save snapshots and opcode.
 */
#define HCALL_INST_PRECALL					\
	std	r3,STK_PARM(r3)(r1);	/* save opcode */	\
	mftb	r0;			/* get timebase and */	\
	std     r0,STK_PARM(r5)(r1);	/* save for later */	\
BEGIN_FTR_SECTION;						\
	mfspr	r0,SPRN_PURR;		/* get PURR and */	\
	std	r0,STK_PARM(r6)(r1);	/* save for later */	\
END_FTR_SECTION_IFSET(CPU_FTR_PURR);
	
/*
 * postcall is performed immediately before function return which
 * allows liberal use of volatile registers.  We branch around this
 * in early init (eg when populating the MMU hashtable) by using an
 * unconditional cpu feature.
 */
#define HCALL_INST_POSTCALL					\
BEGIN_FTR_SECTION;						\
	b	1f;						\
END_FTR_SECTION(0, 1);						\
	ld	r4,STK_PARM(r3)(r1);	/* validate opcode */	\
	cmpldi	cr7,r4,MAX_HCALL_OPCODE;			\
	bgt-	cr7,1f;						\
								\
	/* get time and PURR snapshots after hcall */		\
	mftb	r7;			/* timebase after */	\
BEGIN_FTR_SECTION;						\
	mfspr	r8,SPRN_PURR;		/* PURR after */	\
	ld	r6,STK_PARM(r6)(r1);	/* PURR before */	\
	subf	r6,r6,r8;		/* delta */		\
END_FTR_SECTION_IFSET(CPU_FTR_PURR);				\
	ld	r5,STK_PARM(r5)(r1);	/* timebase before */	\
	subf	r5,r5,r7;		/* time delta */	\
								\
	/* calculate address of stat structure r4 = opcode */	\
	srdi	r4,r4,2;		/* index into array */	\
	mulli	r4,r4,HCALL_STAT_SIZE;				\
	LOAD_REG_ADDR(r7, per_cpu__hcall_stats);		\
	add	r4,r4,r7;					\
	ld	r7,PACA_DATA_OFFSET(r13); /* per cpu offset */	\
	add	r4,r4,r7;					\
								\
	/* update stats	*/					\
	ld	r7,HCALL_STAT_CALLS(r4); /* count */		\
	addi	r7,r7,1;					\
	std	r7,HCALL_STAT_CALLS(r4);			\
	ld      r7,HCALL_STAT_TB(r4);	/* timebase */		\
	add	r7,r7,r5;					\
	std	r7,HCALL_STAT_TB(r4);				\
BEGIN_FTR_SECTION;						\
	ld	r7,HCALL_STAT_PURR(r4);	/* PURR */		\
	add	r7,r7,r6;					\
	std	r7,HCALL_STAT_PURR(r4);				\
END_FTR_SECTION_IFSET(CPU_FTR_PURR);				\
1:
#else
#define HCALL_INST_PRECALL
#define HCALL_INST_POSTCALL
#endif

	.text

_GLOBAL(plpar_hcall_norets)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	HCALL_INST_PRECALL

	HVSC				/* invoke the hypervisor */

	HCALL_INST_POSTCALL

	lwz	r0,8(r1)
	mtcrf	0xff,r0
	blr				/* return r3 = status */

_GLOBAL(plpar_hcall)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	HCALL_INST_PRECALL

	std     r4,STK_PARM(r4)(r1)     /* Save ret buffer */

	mr	r4,r5
	mr	r5,r6
	mr	r6,r7
	mr	r7,r8
	mr	r8,r9
	mr	r9,r10

	HVSC				/* invoke the hypervisor */

	ld	r12,STK_PARM(r4)(r1)
	std	r4,  0(r12)
	std	r5,  8(r12)
	std	r6, 16(r12)
	std	r7, 24(r12)

	HCALL_INST_POSTCALL

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr				/* return r3 = status */

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

	std     r4,STK_PARM(r4)(r1)     /* Save ret buffer */

	mr	r4,r5
	mr	r5,r6
	mr	r6,r7
	mr	r7,r8
	mr	r8,r9
	mr	r9,r10

	HVSC				/* invoke the hypervisor */

	ld	r12,STK_PARM(r4)(r1)
	std	r4,  0(r12)
	std	r5,  8(r12)
	std	r6, 16(r12)
	std	r7, 24(r12)

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr				/* return r3 = status */

_GLOBAL(plpar_hcall9)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	HCALL_INST_PRECALL

	std     r4,STK_PARM(r4)(r1)     /* Save ret buffer */

	mr	r4,r5
	mr	r5,r6
	mr	r6,r7
	mr	r7,r8
	mr	r8,r9
	mr	r9,r10
	ld	r10,STK_PARM(r11)(r1)	 /* put arg7 in R10 */
	ld	r11,STK_PARM(r12)(r1)	 /* put arg8 in R11 */
	ld	r12,STK_PARM(r13)(r1)    /* put arg9 in R12 */

	HVSC				/* invoke the hypervisor */

	mr	r0,r12
	ld	r12,STK_PARM(r4)(r1)
	std	r4,  0(r12)
	std	r5,  8(r12)
	std	r6, 16(r12)
	std	r7, 24(r12)
	std	r8, 32(r12)
	std	r9, 40(r12)
	std	r10,48(r12)
	std	r11,56(r12)
	std	r0, 64(r12)

	HCALL_INST_POSTCALL

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr				/* return r3 = status */
