/*
 * Author: Anton Blanchard <anton@au.ibm.com>
 * Copyright 2015 IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <asm/ppc_asm.h>
#include <asm/export.h>

#define off8	r6
#define off16	r7
#define off24	r8

#define rA	r9
#define rB	r10
#define rC	r11
#define rD	r27
#define rE	r28
#define rF	r29
#define rG	r30
#define rH	r31

#ifdef __LITTLE_ENDIAN__
#define LH	lhbrx
#define LW	lwbrx
#define LD	ldbrx
#else
#define LH	lhzx
#define LW	lwzx
#define LD	ldx
#endif

/*
 * There are 2 categories for memcmp:
 * 1) src/dst has the same offset to the 8 bytes boundary. The handlers
 * are named like .Lsameoffset_xxxx
 * 2) src/dst has different offset to the 8 bytes boundary. The handlers
 * are named like .Ldiffoffset_xxxx
 */
_GLOBAL(memcmp)
	cmpdi	cr1,r5,0

	/* Use the short loop if the src/dst addresses are not
	 * with the same offset of 8 bytes align boundary.
	 */
	xor	r6,r3,r4
	andi.	r6,r6,7

	/* Fall back to short loop if compare at aligned addrs
	 * with less than 8 bytes.
	 */
	cmpdi   cr6,r5,7

	beq	cr1,.Lzero
	bgt	cr6,.Lno_short

.Lshort:
	mtctr	r5
1:	lbz	rA,0(r3)
	lbz	rB,0(r4)
	subf.	rC,rB,rA
	bne	.Lnon_zero
	bdz	.Lzero

	lbz	rA,1(r3)
	lbz	rB,1(r4)
	subf.	rC,rB,rA
	bne	.Lnon_zero
	bdz	.Lzero

	lbz	rA,2(r3)
	lbz	rB,2(r4)
	subf.	rC,rB,rA
	bne	.Lnon_zero
	bdz	.Lzero

	lbz	rA,3(r3)
	lbz	rB,3(r4)
	subf.	rC,rB,rA
	bne	.Lnon_zero

	addi	r3,r3,4
	addi	r4,r4,4

	bdnz	1b

.Lzero:
	li	r3,0
	blr

.Lno_short:
	dcbt	0,r3
	dcbt	0,r4
	bne	.Ldiffoffset_8bytes_make_align_start


.Lsameoffset_8bytes_make_align_start:
	/* attempt to compare bytes not aligned with 8 bytes so that
	 * rest comparison can run based on 8 bytes alignment.
	 */
	andi.   r6,r3,7

	/* Try to compare the first double word which is not 8 bytes aligned:
	 * load the first double word at (src & ~7UL) and shift left appropriate
	 * bits before comparision.
	 */
	rlwinm  r6,r3,3,26,28
	beq     .Lsameoffset_8bytes_aligned
	clrrdi	r3,r3,3
	clrrdi	r4,r4,3
	LD	rA,0,r3
	LD	rB,0,r4
	sld	rA,rA,r6
	sld	rB,rB,r6
	cmpld	cr0,rA,rB
	srwi	r6,r6,3
	bne	cr0,.LcmpAB_lightweight
	subfic  r6,r6,8
	subf.	r5,r6,r5
	addi	r3,r3,8
	addi	r4,r4,8
	beq	.Lzero

.Lsameoffset_8bytes_aligned:
	/* now we are aligned with 8 bytes.
	 * Use .Llong loop if left cmp bytes are equal or greater than 32B.
	 */
	cmpdi   cr6,r5,31
	bgt	cr6,.Llong

.Lcmp_lt32bytes:
	/* compare 1 ~ 32 bytes, at least r3 addr is 8 bytes aligned now */
	cmpdi   cr5,r5,7
	srdi    r0,r5,3
	ble	cr5,.Lcmp_rest_lt8bytes

	/* handle 8 ~ 31 bytes */
	clrldi  r5,r5,61
	mtctr   r0
2:
	LD	rA,0,r3
	LD	rB,0,r4
	cmpld	cr0,rA,rB
	addi	r3,r3,8
	addi	r4,r4,8
	bne	cr0,.LcmpAB_lightweight
	bdnz	2b

	cmpwi   r5,0
	beq	.Lzero

.Lcmp_rest_lt8bytes:
	/* Here we have only less than 8 bytes to compare with. at least s1
	 * Address is aligned with 8 bytes.
	 * The next double words are load and shift right with appropriate
	 * bits.
	 */
	subfic  r6,r5,8
	slwi	r6,r6,3
	LD	rA,0,r3
	LD	rB,0,r4
	srd	rA,rA,r6
	srd	rB,rB,r6
	cmpld	cr0,rA,rB
	bne	cr0,.LcmpAB_lightweight
	b	.Lzero

.Lnon_zero:
	mr	r3,rC
	blr

.Llong:
	/* At least s1 addr is aligned with 8 bytes */
	li	off8,8
	li	off16,16
	li	off24,24

	std	r31,-8(r1)
	std	r30,-16(r1)
	std	r29,-24(r1)
	std	r28,-32(r1)
	std	r27,-40(r1)

	srdi	r0,r5,5
	mtctr	r0
	andi.	r5,r5,31

	LD	rA,0,r3
	LD	rB,0,r4

	LD	rC,off8,r3
	LD	rD,off8,r4

	LD	rE,off16,r3
	LD	rF,off16,r4

	LD	rG,off24,r3
	LD	rH,off24,r4
	cmpld	cr0,rA,rB

	addi	r3,r3,32
	addi	r4,r4,32

	bdz	.Lfirst32

	LD	rA,0,r3
	LD	rB,0,r4
	cmpld	cr1,rC,rD

	LD	rC,off8,r3
	LD	rD,off8,r4
	cmpld	cr6,rE,rF

	LD	rE,off16,r3
	LD	rF,off16,r4
	cmpld	cr7,rG,rH
	bne	cr0,.LcmpAB

	LD	rG,off24,r3
	LD	rH,off24,r4
	cmpld	cr0,rA,rB
	bne	cr1,.LcmpCD

	addi	r3,r3,32
	addi	r4,r4,32

	bdz	.Lsecond32

	.balign	16

1:	LD	rA,0,r3
	LD	rB,0,r4
	cmpld	cr1,rC,rD
	bne	cr6,.LcmpEF

	LD	rC,off8,r3
	LD	rD,off8,r4
	cmpld	cr6,rE,rF
	bne	cr7,.LcmpGH

	LD	rE,off16,r3
	LD	rF,off16,r4
	cmpld	cr7,rG,rH
	bne	cr0,.LcmpAB

	LD	rG,off24,r3
	LD	rH,off24,r4
	cmpld	cr0,rA,rB
	bne	cr1,.LcmpCD

	addi	r3,r3,32
	addi	r4,r4,32

	bdnz	1b

.Lsecond32:
	cmpld	cr1,rC,rD
	bne	cr6,.LcmpEF

	cmpld	cr6,rE,rF
	bne	cr7,.LcmpGH

	cmpld	cr7,rG,rH
	bne	cr0,.LcmpAB

	bne	cr1,.LcmpCD
	bne	cr6,.LcmpEF
	bne	cr7,.LcmpGH

.Ltail:
	ld	r31,-8(r1)
	ld	r30,-16(r1)
	ld	r29,-24(r1)
	ld	r28,-32(r1)
	ld	r27,-40(r1)

	cmpdi	r5,0
	beq	.Lzero
	b	.Lshort

.Lfirst32:
	cmpld	cr1,rC,rD
	cmpld	cr6,rE,rF
	cmpld	cr7,rG,rH

	bne	cr0,.LcmpAB
	bne	cr1,.LcmpCD
	bne	cr6,.LcmpEF
	bne	cr7,.LcmpGH

	b	.Ltail

.LcmpAB:
	li	r3,1
	bgt	cr0,.Lout
	li	r3,-1
	b	.Lout

.LcmpCD:
	li	r3,1
	bgt	cr1,.Lout
	li	r3,-1
	b	.Lout

.LcmpEF:
	li	r3,1
	bgt	cr6,.Lout
	li	r3,-1
	b	.Lout

.LcmpGH:
	li	r3,1
	bgt	cr7,.Lout
	li	r3,-1

.Lout:
	ld	r31,-8(r1)
	ld	r30,-16(r1)
	ld	r29,-24(r1)
	ld	r28,-32(r1)
	ld	r27,-40(r1)
	blr

.LcmpAB_lightweight:   /* skip NV GPRS restore */
	li	r3,1
	bgtlr
	li	r3,-1
	blr

.Ldiffoffset_8bytes_make_align_start:
	/* now try to align s1 with 8 bytes */
	rlwinm  r6,r3,3,26,28
	beq     .Ldiffoffset_align_s1_8bytes

	clrrdi	r3,r3,3
	LD	rA,0,r3
	LD	rB,0,r4  /* unaligned load */
	sld	rA,rA,r6
	srd	rA,rA,r6
	srd	rB,rB,r6
	cmpld	cr0,rA,rB
	srwi	r6,r6,3
	bne	cr0,.LcmpAB_lightweight

	subfic  r6,r6,8
	subf.	r5,r6,r5
	addi	r3,r3,8
	add	r4,r4,r6

	beq	.Lzero

.Ldiffoffset_align_s1_8bytes:
	/* now s1 is aligned with 8 bytes. */
	cmpdi   cr5,r5,31
	ble	cr5,.Lcmp_lt32bytes
	b	.Llong

EXPORT_SYMBOL(memcmp)
