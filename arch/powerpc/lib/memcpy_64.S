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
#include <asm/export.h>
#include <asm/asm-compat.h>
#include <asm/feature-fixups.h>

#ifndef SELFTEST_CASE
/* For big-endian, 0 == most CPUs, 1 == POWER6, 2 == Cell */
#define SELFTEST_CASE	0
#endif

	.align	7
_GLOBAL_TOC(memcpy)
BEGIN_FTR_SECTION
#ifdef __LITTLE_ENDIAN__
	cmpdi	cr7,r5,0
#else
	std	r3,-STACKFRAMESIZE+STK_REG(R31)(r1)	/* save destination pointer for return value */
#endif
FTR_SECTION_ELSE
#ifdef CONFIG_PPC_BOOK3S_64
	b	memcpy_power7
#endif
ALT_FTR_SECTION_END_IFCLR(CPU_FTR_VMX_COPY)
#ifdef __LITTLE_ENDIAN__
	/* dumb little-endian memcpy that will get replaced at runtime */
	addi r9,r3,-1
	addi r4,r4,-1
	beqlr cr7
	mtctr r5
1:	lbzu r10,1(r4)
	stbu r10,1(r9)
	bdnz 1b
	blr
#else
	PPC_MTOCRF(0x01,r5)
	cmpldi	cr1,r5,16
	neg	r6,r3		# LS 3 bits = # bytes to 8-byte dest bdry
	andi.	r6,r6,7
	dcbt	0,r4
	blt	cr1,.Lshort_copy
/* Below we want to nop out the bne if we're on a CPU that has the
   CPU_FTR_UNALIGNED_LD_STD bit set and the CPU_FTR_CP_USE_DCBTZ bit
   cleared.
   At the time of writing the only CPU that has this combination of bits
   set is Power6. */
test_feature = (SELFTEST_CASE == 1)
BEGIN_FTR_SECTION
	nop
FTR_SECTION_ELSE
	bne	.Ldst_unaligned
ALT_FTR_SECTION_END(CPU_FTR_UNALIGNED_LD_STD | CPU_FTR_CP_USE_DCBTZ, \
                    CPU_FTR_UNALIGNED_LD_STD)
.Ldst_aligned:
	addi	r3,r3,-16
test_feature = (SELFTEST_CASE == 0)
BEGIN_FTR_SECTION
	andi.	r0,r4,7
	bne	.Lsrc_unaligned
END_FTR_SECTION_IFCLR(CPU_FTR_UNALIGNED_LD_STD)
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
	beq	3f
	addi	r3,r3,16
.Ldo_tail:
	bf	cr7*4+1,1f
	lwz	r9,8(r4)
	addi	r4,r4,4
	stw	r9,0(r3)
	addi	r3,r3,4
1:	bf	cr7*4+2,2f
	lhz	r9,8(r4)
	addi	r4,r4,2
	sth	r9,0(r3)
	addi	r3,r3,2
2:	bf	cr7*4+3,3f
	lbz	r9,8(r4)
	stb	r9,0(r3)
3:	ld	r3,-STACKFRAMESIZE+STK_REG(R31)(r1)	/* return dest pointer */
	blr

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
	beq	4f
	cmpwi	cr1,r5,8
	addi	r3,r3,32
	sld	r9,r9,r10
	ble	cr1,6f
	ld	r0,8(r4)
	srd	r7,r0,r11
	or	r9,r7,r9
6:
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
3:	ld	r3,-STACKFRAMESIZE+STK_REG(R31)(r1)	/* return dest pointer */
	blr

.Ldst_unaligned:
	PPC_MTOCRF(0x01,r6)		# put #bytes to 8B bdry into cr7
	subf	r5,r6,r5
	li	r7,0
	cmpldi	cr1,r5,16
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
3:	PPC_MTOCRF(0x01,r5)
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
4:	ld	r3,-STACKFRAMESIZE+STK_REG(R31)(r1)	/* return dest pointer */
	blr
#endif
EXPORT_SYMBOL(memcpy)
