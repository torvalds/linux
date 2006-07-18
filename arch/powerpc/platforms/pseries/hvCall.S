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
	
#define STK_PARM(i)     (48 + ((i)-3)*8)

	.text

_GLOBAL(plpar_hcall_norets)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	HVSC				/* invoke the hypervisor */

	lwz	r0,8(r1)
	mtcrf	0xff,r0
	blr				/* return r3 = status */

_GLOBAL(plpar_hcall)
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

	ld	r12,STK_PARM(r4)(r1)
	std	r4,  0(r12)
	std	r5,  8(r12)
	std	r6, 16(r12)
	std	r7, 24(r12)
	std	r8, 32(r12)
	std	r9, 40(r12)
	std	r10,48(r12)
	std	r11,56(r12)
	std	r12,64(r12)

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr				/* return r3 = status */
