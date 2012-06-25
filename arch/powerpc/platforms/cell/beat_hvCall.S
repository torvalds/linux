/*
 * Beat hypervisor call I/F
 *
 * (C) Copyright 2007 TOSHIBA CORPORATION
 *
 * This code is based on arch/powerpc/platforms/pseries/hvCall.S.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <asm/ppc_asm.h>

#define	STK_PARM(i)	(48 + ((i)-3)*8)

/* Not implemented on Beat, now */
#define	HCALL_INST_PRECALL
#define	HCALL_INST_POSTCALL

	.text

#define	HVSC	.long	0x44000022

/* Note: takes only 7 input parameters at maximum */
_GLOBAL(beat_hcall_norets)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	HCALL_INST_PRECALL

	mr	r11,r3
	mr	r3,r4
	mr	r4,r5
	mr	r5,r6
	mr	r6,r7
	mr	r7,r8
	mr	r8,r9

	HVSC				/* invoke the hypervisor */

	HCALL_INST_POSTCALL

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr				/* return r3 = status */

/* Note: takes 8 input parameters at maximum */
_GLOBAL(beat_hcall_norets8)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	HCALL_INST_PRECALL

	mr	r11,r3
	mr	r3,r4
	mr	r4,r5
	mr	r5,r6
	mr	r6,r7
	mr	r7,r8
	mr	r8,r9
	ld	r10,STK_PARM(R10)(r1)

	HVSC				/* invoke the hypervisor */

	HCALL_INST_POSTCALL

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr				/* return r3 = status */

/* Note: takes only 6 input parameters, 1 output parameters at maximum */
_GLOBAL(beat_hcall1)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	HCALL_INST_PRECALL

	std	r4,STK_PARM(R4)(r1)	/* save ret buffer */

	mr	r11,r3
	mr	r3,r5
	mr	r4,r6
	mr	r5,r7
	mr	r6,r8
	mr	r7,r9
	mr	r8,r10

	HVSC				/* invoke the hypervisor */

	HCALL_INST_POSTCALL

	ld	r12,STK_PARM(R4)(r1)
	std	r4,  0(r12)

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr				/* return r3 = status */

/* Note: takes only 6 input parameters, 2 output parameters at maximum */
_GLOBAL(beat_hcall2)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	HCALL_INST_PRECALL

	std	r4,STK_PARM(R4)(r1)	/* save ret buffer */

	mr	r11,r3
	mr	r3,r5
	mr	r4,r6
	mr	r5,r7
	mr	r6,r8
	mr	r7,r9
	mr	r8,r10

	HVSC				/* invoke the hypervisor */

	HCALL_INST_POSTCALL

	ld	r12,STK_PARM(R4)(r1)
	std	r4,  0(r12)
	std	r5,  8(r12)

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr				/* return r3 = status */

/* Note: takes only 6 input parameters, 3 output parameters at maximum */
_GLOBAL(beat_hcall3)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	HCALL_INST_PRECALL

	std	r4,STK_PARM(R4)(r1)	/* save ret buffer */

	mr	r11,r3
	mr	r3,r5
	mr	r4,r6
	mr	r5,r7
	mr	r6,r8
	mr	r7,r9
	mr	r8,r10

	HVSC				/* invoke the hypervisor */

	HCALL_INST_POSTCALL

	ld	r12,STK_PARM(R4)(r1)
	std	r4,  0(r12)
	std	r5,  8(r12)
	std	r6, 16(r12)

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr				/* return r3 = status */

/* Note: takes only 6 input parameters, 4 output parameters at maximum */
_GLOBAL(beat_hcall4)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	HCALL_INST_PRECALL

	std	r4,STK_PARM(R4)(r1)	/* save ret buffer */

	mr	r11,r3
	mr	r3,r5
	mr	r4,r6
	mr	r5,r7
	mr	r6,r8
	mr	r7,r9
	mr	r8,r10

	HVSC				/* invoke the hypervisor */

	HCALL_INST_POSTCALL

	ld	r12,STK_PARM(R4)(r1)
	std	r4,  0(r12)
	std	r5,  8(r12)
	std	r6, 16(r12)
	std	r7, 24(r12)

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr				/* return r3 = status */

/* Note: takes only 6 input parameters, 5 output parameters at maximum */
_GLOBAL(beat_hcall5)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	HCALL_INST_PRECALL

	std	r4,STK_PARM(R4)(r1)	/* save ret buffer */

	mr	r11,r3
	mr	r3,r5
	mr	r4,r6
	mr	r5,r7
	mr	r6,r8
	mr	r7,r9
	mr	r8,r10

	HVSC				/* invoke the hypervisor */

	HCALL_INST_POSTCALL

	ld	r12,STK_PARM(R4)(r1)
	std	r4,  0(r12)
	std	r5,  8(r12)
	std	r6, 16(r12)
	std	r7, 24(r12)
	std	r8, 32(r12)

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr				/* return r3 = status */

/* Note: takes only 6 input parameters, 6 output parameters at maximum */
_GLOBAL(beat_hcall6)
	HMT_MEDIUM

	mfcr	r0
	stw	r0,8(r1)

	HCALL_INST_PRECALL

	std	r4,STK_PARM(R4)(r1)	/* save ret buffer */

	mr	r11,r3
	mr	r3,r5
	mr	r4,r6
	mr	r5,r7
	mr	r6,r8
	mr	r7,r9
	mr	r8,r10

	HVSC				/* invoke the hypervisor */

	HCALL_INST_POSTCALL

	ld	r12,STK_PARM(R4)(r1)
	std	r4,  0(r12)
	std	r5,  8(r12)
	std	r6, 16(r12)
	std	r7, 24(r12)
	std	r8, 32(r12)
	std	r9, 40(r12)

	lwz	r0,8(r1)
	mtcrf	0xff,r0

	blr				/* return r3 = status */
