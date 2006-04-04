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

/* plpar_hcall_7arg_7ret(unsigned long opcode,		R3
			 unsigned long arg1,		R4
			 unsigned long arg2,		R5
			 unsigned long arg3,		R6
			 unsigned long arg4,		R7
			 unsigned long arg5,		R8
			 unsigned long arg6,		R9
			 unsigned long arg7,		R10
			 unsigned long *out1,		112(R1)
			 unsigned long *out2,		110(R1)
			 unsigned long *out3,		108(R1)
			 unsigned long *out4,		106(R1)
			 unsigned long *out5,		104(R1)
			 unsigned long *out6,		102(R1)
			 unsigned long *out7);		100(R1)
*/
_GLOBAL(plpar_hcall_7arg_7ret)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	HVSC				/* invoke the hypervisor */

	lwz	r0,8(r1)

	ld	r11,STK_PARM(r11)(r1)	/* Fetch r4 ret arg */
	std	r4,0(r11)
	ld	r11,STK_PARM(r12)(r1)	/* Fetch r5 ret arg */
	std	r5,0(r11)
	ld	r11,STK_PARM(r13)(r1)	/* Fetch r6 ret arg */
	std	r6,0(r11)
	ld	r11,STK_PARM(r14)(r1)	/* Fetch r7 ret arg */
	std	r7,0(r11)
	ld	r11,STK_PARM(r15)(r1)	/* Fetch r8 ret arg */
	std	r8,0(r11)
	ld	r11,STK_PARM(r16)(r1)	/* Fetch r9 ret arg */
	std	r9,0(r11)
	ld	r11,STK_PARM(r17)(r1)	/* Fetch r10 ret arg */
	std	r10,0(r11)

	mtcrf	0xff,r0

	blr				/* return r3 = status */

/* plpar_hcall_9arg_9ret(unsigned long opcode,		R3
			 unsigned long arg1,		R4
			 unsigned long arg2,		R5
			 unsigned long arg3,		R6
			 unsigned long arg4,		R7
			 unsigned long arg5,		R8
			 unsigned long arg6,		R9
			 unsigned long arg7,		R10
			 unsigned long arg8,		112(R1)
			 unsigned long arg9,		110(R1)
			 unsigned long *out1,		108(R1)
			 unsigned long *out2,		106(R1)
			 unsigned long *out3,		104(R1)
			 unsigned long *out4,		102(R1)
			 unsigned long *out5,		100(R1)
			 unsigned long *out6,		 98(R1)
			 unsigned long *out7);		 96(R1)
			 unsigned long *out8,		 94(R1)
		         unsigned long *out9,            92(R1)
*/
_GLOBAL(plpar_hcall_9arg_9ret)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	ld	r11,STK_PARM(r11)(r1)	 /* put arg8 in R11 */
	ld	r12,STK_PARM(r12)(r1)    /* put arg9 in R12 */

	HVSC				/* invoke the hypervisor */

	ld	r0,STK_PARM(r13)(r1)	/* Fetch r4 ret arg */
	stdx	r4,r0,r0
	ld	r0,STK_PARM(r14)(r1)	/* Fetch r5 ret arg */
	stdx	r5,r0,r0
	ld	r0,STK_PARM(r15)(r1)	/* Fetch r6 ret arg */
	stdx	r6,r0,r0
	ld	r0,STK_PARM(r16)(r1)	/* Fetch r7 ret arg */
	stdx	r7,r0,r0
	ld	r0,STK_PARM(r17)(r1)	/* Fetch r8 ret arg */
	stdx	r8,r0,r0
	ld	r0,STK_PARM(r18)(r1)	/* Fetch r9 ret arg */
	stdx	r9,r0,r0
	ld	r0,STK_PARM(r19)(r1)	/* Fetch r10 ret arg */
	stdx	r10,r0,r0
	ld	r0,STK_PARM(r20)(r1)	/* Fetch r11 ret arg */
	stdx	r11,r0,r0
	ld	r0,STK_PARM(r21)(r1)	/* Fetch r12 ret arg */
	stdx	r12,r0,r0

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr				/* return r3 = status */
