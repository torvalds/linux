/*
 * Copyright (C) 2002 Paul Mackerras, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <asm/processor.h>
#include <asm/ppc_asm.h>

	.align	7
_GLOBAL(memcpy)
	mtcrf	0x01,r5
	cmpldi	cr1,r5,16
	neg	r6,r3		# LS 3 bits = # bytes to 8-byte dest bdry
	andi.	r6,r6,7
	dcbt	0,r4
	blt	cr1,.Lshort_copy
	bne	.Ldst_unaligned
.Ldst_aligned:
	andi.	r0,r4,7
	addi	r3,r3,-16
	bne	.Lsrc_unaligned
	srdi	r7,r5,4
	ld	r9,0(r4)
	addi	r4,r4,-8
	mtctr	r7
	andi.	r5,r5,7
	bf	cr7*4+0,2f
	addi	r3,r3,8
	addi	r4,r4,8
	mr	r8,r9
	blt	cr1,3f
1:	ld	r9,8(r4)
	std	r8,8(r3)
2:	ldu	r8,16(r4)
	stdu	r9,16(r3)
	bdnz	1b
3:	std	r8,8(r3)
	beqlr
	addi	r3,r3,16
	ld	r9,8(r4)
.Ldo_tail:
	bf	cr7*4+1,1f
	rotldi	r9,r9,32
	stw	r9,0(r3)
	addi	r3,r3,4
1:	bf	cr7*4+2,2f
	rotldi	r9,r9,16
	sth	r9,0(r3)
	addi	r3,r3,2
2:	bf	cr7*4+3,3f
	rotldi	r9,r9,8
	stb	r9,0(r3)
3:	blr

.Lsrc_unaligned:
	srdi	r6,r5,3
	addi	r5,r5,-16
	subf	r4,r0,r4
	srdi	r7,r5,4
	sldi	r10,r0,3
	cmpdi	cr6,r6,3
	andi.	r5,r5,7
	mtctr	r7
	subfic	r11,r10,64
	add	r5,r5,r0

	bt	cr7*4+0,0f

	ld	r9,0(r4)	# 3+2n loads, 2+2n stores
	ld	r0,8(r4)
	sld	r6,r9,r10
	ldu	r9,16(r4)
	srd	r7,r0,r11
	sld	r8,r0,r10
	or	r7,r7,r6
	blt	cr6,4f
	ld	r0,8(r4)
	# s1<< in r8, d0=(s0<<|s1>>) in r7, s3 in r0, s2 in r9, nix in r6 & r12
	b	2f

0:	ld	r0,0(r4)	# 4+2n loads, 3+2n stores
	ldu	r9,8(r4)
	sld	r8,r0,r10
	addi	r3,r3,-8
	blt	cr6,5f
	ld	r0,8(r4)
	srd	r12,r9,r11
	sld	r6,r9,r10
	ldu	r9,16(r4)
	or	r12,r8,r12
	srd	r7,r0,r11
	sld	r8,r0,r10
	addi	r3,r3,16
	beq	cr6,3f

	# d0=(s0<<|s1>>) in r12, s1<< in r6, s2>> in r7, s2<< in r8, s3 in r9
1:	or	r7,r7,r6
	ld	r0,8(r4)
	std	r12,8(r3)
2:	srd	r12,r9,r11
	sld	r6,r9,r10
	ldu	r9,16(r4)
	or	r12,r8,r12
	stdu	r7,16(r3)
	srd	r7,r0,r11
	sld	r8,r0,r10
	bdnz	1b

3:	std	r12,8(r3)
	or	r7,r7,r6
4:	std	r7,16(r3)
5:	srd	r12,r9,r11
	or	r12,r8,r12
	std	r12,24(r3)
	beqlr
	cmpwi	cr1,r5,8
	addi	r3,r3,32
	sld	r9,r9,r10
	ble	cr1,.Ldo_tail
	ld	r0,8(r4)
	srd	r7,r0,r11
	or	r9,r7,r9
	b	.Ldo_tail

.Ldst_unaligned:
	mtcrf	0x01,r6		# put #bytes to 8B bdry into cr7
	subf	r5,r6,r5
	li	r7,0
	cmpldi	r1,r5,16
	bf	cr7*4+3,1f
	lbz	r0,0(r4)
	stb	r0,0(r3)
	addi	r7,r7,1
1:	bf	cr7*4+2,2f
	lhzx	r0,r7,r4
	sthx	r0,r7,r3
	addi	r7,r7,2
2:	bf	cr7*4+1,3f
	lwzx	r0,r7,r4
	stwx	r0,r7,r3
3:	mtcrf	0x01,r5
	add	r4,r6,r4
	add	r3,r6,r3
	b	.Ldst_aligned

.Lshort_copy:
	bf	cr7*4+0,1f
	lwz	r0,0(r4)
	lwz	r9,4(r4)
	addi	r4,r4,8
	stw	r0,0(r3)
	stw	r9,4(r3)
	addi	r3,r3,8
1:	bf	cr7*4+1,2f
	lwz	r0,0(r4)
	addi	r4,r4,4
	stw	r0,0(r3)
	addi	r3,r3,4
2:	bf	cr7*4+2,3f
	lhz	r0,0(r4)
	addi	r4,r4,2
	sth	r0,0(r3)
	addi	r3,r3,2
3:	bf	cr7*4+3,4f
	lbz	r0,0(r4)
	stb	r0,0(r3)
4:	blr
