/*
 * This file contains the generic code to perform a call to the
 * pSeries LPAR hypervisor.
 * NOTE: this file will go away when we move to inline this work.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <asm/hvcall.h>
#include <asm/processor.h>
#include <asm/ppc_asm.h>
	
#define STK_PARM(i)     (48 + ((i)-3)*8)

	.text

/* long plpar_hcall(unsigned long opcode,		R3
			unsigned long arg1,		R4
			unsigned long arg2,		R5
			unsigned long arg3,		R6
			unsigned long arg4,		R7
			unsigned long *out1,		R8
			unsigned long *out2,		R9
			unsigned long *out3);		R10
 */
_GLOBAL(plpar_hcall)
	HMT_MEDIUM

	mfcr	r0

	std	r8,STK_PARM(r8)(r1)	/* Save out ptrs */
	std	r9,STK_PARM(r9)(r1)
	std	r10,STK_PARM(r10)(r1)

	stw	r0,8(r1)

	HVSC				/* invoke the hypervisor */

	lwz	r0,8(r1)

	ld	r8,STK_PARM(r8)(r1)	/* Fetch r4-r6 ret args */
	ld	r9,STK_PARM(r9)(r1)
	ld	r10,STK_PARM(r10)(r1)
	std	r4,0(r8)
	std	r5,0(r9)
	std	r6,0(r10)

	mtcrf	0xff,r0
	blr				/* return r3 = status */


/* Simple interface with no output values (other than status) */
_GLOBAL(plpar_hcall_norets)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	HVSC				/* invoke the hypervisor */

	lwz	r0,8(r1)
	mtcrf	0xff,r0
	blr				/* return r3 = status */


/* long plpar_hcall_8arg_2ret(unsigned long opcode,	R3
			unsigned long arg1,		R4
			unsigned long arg2,		R5
			unsigned long arg3,		R6
			unsigned long arg4,		R7
			unsigned long arg5,		R8
			unsigned long arg6,		R9
			unsigned long arg7,		R10
			unsigned long arg8,		112(R1)
			unsigned long *out1);		120(R1)
 */
_GLOBAL(plpar_hcall_8arg_2ret)
	HMT_MEDIUM

	mfcr	r0
	ld	r11,STK_PARM(r11)(r1)	/* put arg8 in R11 */
	stw	r0,8(r1)

	HVSC				/* invoke the hypervisor */

	lwz	r0,8(r1)
	ld	r10,STK_PARM(r12)(r1)	/* Fetch r4 ret arg */
	std	r4,0(r10)
	mtcrf	0xff,r0
	blr				/* return r3 = status */


/* long plpar_hcall_4out(unsigned long opcode,		R3
		 	unsigned long arg1,		R4
		 	unsigned long arg2,		R5
		 	unsigned long arg3,		R6
		 	unsigned long arg4,		R7
		 	unsigned long *out1,		R8
		 	unsigned long *out2,		R9
		 	unsigned long *out3,		R10
		 	unsigned long *out4);		112(R1)
 */
_GLOBAL(plpar_hcall_4out)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	std	r8,STK_PARM(r8)(r1)	/* Save out ptrs */
	std	r9,STK_PARM(r9)(r1)
	std	r10,STK_PARM(r10)(r1)

	HVSC				/* invoke the hypervisor */

	lwz	r0,8(r1)

	ld	r8,STK_PARM(r8)(r1)	/* Fetch r4-r7 ret args */
	ld	r9,STK_PARM(r9)(r1)
	ld	r10,STK_PARM(r10)(r1)
	ld	r11,STK_PARM(r11)(r1)
	std	r4,0(r8)
	std	r5,0(r9)
	std	r6,0(r10)
	std	r7,0(r11)

	mtcrf	0xff,r0
	blr				/* return r3 = status */
