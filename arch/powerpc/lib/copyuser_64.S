/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2002 Paul Mackerras, IBM Corp.
 */
#include <linux/export.h>
#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/asm-compat.h>
#include <asm/feature-fixups.h>

#ifndef SELFTEST_CASE
/* 0 == most CPUs, 1 == POWER6, 2 == Cell */
#define SELFTEST_CASE	0
#endif

#ifdef __BIG_ENDIAN__
#define sLd sld		/* Shift towards low-numbered address. */
#define sHd srd		/* Shift towards high-numbered address. */
#else
#define sLd srd		/* Shift towards low-numbered address. */
#define sHd sld		/* Shift towards high-numbered address. */
#endif

/*
 * These macros are used to generate exception table entries.
 * The exception handlers below use the original arguments
 * (stored on the stack) and the point where we're up to in
 * the destination buffer, i.e. the address of the first
 * unmodified byte.  Generally r3 points into the destination
 * buffer, but the first unmodified byte is at a variable
 * offset from r3.  In the code below, the symbol r3_offset
 * is set to indicate the current offset at each point in
 * the code.  This offset is then used as a negative offset
 * from the exception handler code, and those instructions
 * before the exception handlers are addi instructions that
 * adjust r3 to point to the correct place.
 */
	.macro	lex		/* exception handler for load */
100:	EX_TABLE(100b, .Lld_exc - r3_offset)
	.endm

	.macro	stex		/* exception handler for store */
100:	EX_TABLE(100b, .Lst_exc - r3_offset)
	.endm

	.align	7
_GLOBAL_TOC(__copy_tofrom_user)
#ifdef CONFIG_PPC_BOOK3S_64
BEGIN_FTR_SECTION
	nop
FTR_SECTION_ELSE
	b	__copy_tofrom_user_power7
ALT_FTR_SECTION_END_IFCLR(CPU_FTR_VMX_COPY)
#endif
_GLOBAL(__copy_tofrom_user_base)
	/* first check for a 4kB copy on a 4kB boundary */
	cmpldi	cr1,r5,16
	cmpdi	cr6,r5,4096
	or	r0,r3,r4
	neg	r6,r3		/* LS 3 bits = # bytes to 8-byte dest bdry */
	andi.	r0,r0,4095
	std	r3,-24(r1)
	crand	cr0*4+2,cr0*4+2,cr6*4+2
	std	r4,-16(r1)
	std	r5,-8(r1)
	dcbt	0,r4
	beq	.Lcopy_page_4K
	andi.	r6,r6,7
	PPC_MTOCRF(0x01,r5)
	blt	cr1,.Lshort_copy
/* Below we want to nop out the bne if we're on a CPU that has the
 * CPU_FTR_UNALIGNED_LD_STD bit set and the CPU_FTR_CP_USE_DCBTZ bit
 * cleared.
 * At the time of writing the only CPU that has this combination of bits
 * set is Power6.
 */
test_feature = (SELFTEST_CASE == 1)
BEGIN_FTR_SECTION
	nop
FTR_SECTION_ELSE
	bne	.Ldst_unaligned
ALT_FTR_SECTION_END(CPU_FTR_UNALIGNED_LD_STD | CPU_FTR_CP_USE_DCBTZ, \
		    CPU_FTR_UNALIGNED_LD_STD)
.Ldst_aligned:
	addi	r3,r3,-16
r3_offset = 16
test_feature = (SELFTEST_CASE == 0)
BEGIN_FTR_SECTION
	andi.	r0,r4,7
	bne	.Lsrc_unaligned
END_FTR_SECTION_IFCLR(CPU_FTR_UNALIGNED_LD_STD)
	blt	cr1,.Ldo_tail		/* if < 16 bytes to copy */
	srdi	r0,r5,5
	cmpdi	cr1,r0,0
lex;	ld	r7,0(r4)
lex;	ld	r6,8(r4)
	addi	r4,r4,16
	mtctr	r0
	andi.	r0,r5,0x10
	beq	22f
	addi	r3,r3,16
r3_offset = 0
	addi	r4,r4,-16
	mr	r9,r7
	mr	r8,r6
	beq	cr1,72f
21:
lex;	ld	r7,16(r4)
lex;	ld	r6,24(r4)
	addi	r4,r4,32
stex;	std	r9,0(r3)
r3_offset = 8
stex;	std	r8,8(r3)
r3_offset = 16
22:
lex;	ld	r9,0(r4)
lex;	ld	r8,8(r4)
stex;	std	r7,16(r3)
r3_offset = 24
stex;	std	r6,24(r3)
	addi	r3,r3,32
r3_offset = 0
	bdnz	21b
72:
stex;	std	r9,0(r3)
r3_offset = 8
stex;	std	r8,8(r3)
r3_offset = 16
	andi.	r5,r5,0xf
	beq+	3f
	addi	r4,r4,16
.Ldo_tail:
	addi	r3,r3,16
r3_offset = 0
	bf	cr7*4+0,246f
lex;	ld	r9,0(r4)
	addi	r4,r4,8
stex;	std	r9,0(r3)
	addi	r3,r3,8
246:	bf	cr7*4+1,1f
lex;	lwz	r9,0(r4)
	addi	r4,r4,4
stex;	stw	r9,0(r3)
	addi	r3,r3,4
1:	bf	cr7*4+2,2f
lex;	lhz	r9,0(r4)
	addi	r4,r4,2
stex;	sth	r9,0(r3)
	addi	r3,r3,2
2:	bf	cr7*4+3,3f
lex;	lbz	r9,0(r4)
stex;	stb	r9,0(r3)
3:	li	r3,0
	blr

.Lsrc_unaligned:
r3_offset = 16
	srdi	r6,r5,3
	addi	r5,r5,-16
	subf	r4,r0,r4
	srdi	r7,r5,4
	sldi	r10,r0,3
	cmpldi	cr6,r6,3
	andi.	r5,r5,7
	mtctr	r7
	subfic	r11,r10,64
	add	r5,r5,r0
	bt	cr7*4+0,28f

lex;	ld	r9,0(r4)	/* 3+2n loads, 2+2n stores */
lex;	ld	r0,8(r4)
	sLd	r6,r9,r10
lex;	ldu	r9,16(r4)
	sHd	r7,r0,r11
	sLd	r8,r0,r10
	or	r7,r7,r6
	blt	cr6,79f
lex;	ld	r0,8(r4)
	b	2f

28:
lex;	ld	r0,0(r4)	/* 4+2n loads, 3+2n stores */
lex;	ldu	r9,8(r4)
	sLd	r8,r0,r10
	addi	r3,r3,-8
r3_offset = 24
	blt	cr6,5f
lex;	ld	r0,8(r4)
	sHd	r12,r9,r11
	sLd	r6,r9,r10
lex;	ldu	r9,16(r4)
	or	r12,r8,r12
	sHd	r7,r0,r11
	sLd	r8,r0,r10
	addi	r3,r3,16
r3_offset = 8
	beq	cr6,78f

1:	or	r7,r7,r6
lex;	ld	r0,8(r4)
stex;	std	r12,8(r3)
r3_offset = 16
2:	sHd	r12,r9,r11
	sLd	r6,r9,r10
lex;	ldu	r9,16(r4)
	or	r12,r8,r12
stex;	stdu	r7,16(r3)
r3_offset = 8
	sHd	r7,r0,r11
	sLd	r8,r0,r10
	bdnz	1b

78:
stex;	std	r12,8(r3)
r3_offset = 16
	or	r7,r7,r6
79:
stex;	std	r7,16(r3)
r3_offset = 24
5:	sHd	r12,r9,r11
	or	r12,r8,r12
stex;	std	r12,24(r3)
r3_offset = 32
	bne	6f
	li	r3,0
	blr
6:	cmpwi	cr1,r5,8
	addi	r3,r3,32
r3_offset = 0
	sLd	r9,r9,r10
	ble	cr1,7f
lex;	ld	r0,8(r4)
	sHd	r7,r0,r11
	or	r9,r7,r9
7:
	bf	cr7*4+1,1f
#ifdef __BIG_ENDIAN__
	rotldi	r9,r9,32
#endif
stex;	stw	r9,0(r3)
#ifdef __LITTLE_ENDIAN__
	rotrdi	r9,r9,32
#endif
	addi	r3,r3,4
1:	bf	cr7*4+2,2f
#ifdef __BIG_ENDIAN__
	rotldi	r9,r9,16
#endif
stex;	sth	r9,0(r3)
#ifdef __LITTLE_ENDIAN__
	rotrdi	r9,r9,16
#endif
	addi	r3,r3,2
2:	bf	cr7*4+3,3f
#ifdef __BIG_ENDIAN__
	rotldi	r9,r9,8
#endif
stex;	stb	r9,0(r3)
#ifdef __LITTLE_ENDIAN__
	rotrdi	r9,r9,8
#endif
3:	li	r3,0
	blr

.Ldst_unaligned:
r3_offset = 0
	PPC_MTOCRF(0x01,r6)		/* put #bytes to 8B bdry into cr7 */
	subf	r5,r6,r5
	li	r7,0
	cmpldi	cr1,r5,16
	bf	cr7*4+3,1f
100:	EX_TABLE(100b, .Lld_exc_r7)
	lbz	r0,0(r4)
100:	EX_TABLE(100b, .Lst_exc_r7)
	stb	r0,0(r3)
	addi	r7,r7,1
1:	bf	cr7*4+2,2f
100:	EX_TABLE(100b, .Lld_exc_r7)
	lhzx	r0,r7,r4
100:	EX_TABLE(100b, .Lst_exc_r7)
	sthx	r0,r7,r3
	addi	r7,r7,2
2:	bf	cr7*4+1,3f
100:	EX_TABLE(100b, .Lld_exc_r7)
	lwzx	r0,r7,r4
100:	EX_TABLE(100b, .Lst_exc_r7)
	stwx	r0,r7,r3
3:	PPC_MTOCRF(0x01,r5)
	add	r4,r6,r4
	add	r3,r6,r3
	b	.Ldst_aligned

.Lshort_copy:
r3_offset = 0
	bf	cr7*4+0,1f
lex;	lwz	r0,0(r4)
lex;	lwz	r9,4(r4)
	addi	r4,r4,8
stex;	stw	r0,0(r3)
stex;	stw	r9,4(r3)
	addi	r3,r3,8
1:	bf	cr7*4+1,2f
lex;	lwz	r0,0(r4)
	addi	r4,r4,4
stex;	stw	r0,0(r3)
	addi	r3,r3,4
2:	bf	cr7*4+2,3f
lex;	lhz	r0,0(r4)
	addi	r4,r4,2
stex;	sth	r0,0(r3)
	addi	r3,r3,2
3:	bf	cr7*4+3,4f
lex;	lbz	r0,0(r4)
stex;	stb	r0,0(r3)
4:	li	r3,0
	blr

/*
 * exception handlers follow
 * we have to return the number of bytes not copied
 * for an exception on a load, we set the rest of the destination to 0
 * Note that the number of bytes of instructions for adjusting r3 needs
 * to equal the amount of the adjustment, due to the trick of using
 * .Lld_exc - r3_offset as the handler address.
 */

.Lld_exc_r7:
	add	r3,r3,r7
	b	.Lld_exc

	/* adjust by 24 */
	addi	r3,r3,8
	nop
	/* adjust by 16 */
	addi	r3,r3,8
	nop
	/* adjust by 8 */
	addi	r3,r3,8
	nop

/*
 * Here we have had a fault on a load and r3 points to the first
 * unmodified byte of the destination.  We use the original arguments
 * and r3 to work out how much wasn't copied.  Since we load some
 * distance ahead of the stores, we continue copying byte-by-byte until
 * we hit the load fault again in order to copy as much as possible.
 */
.Lld_exc:
	ld	r6,-24(r1)
	ld	r4,-16(r1)
	ld	r5,-8(r1)
	subf	r6,r6,r3
	add	r4,r4,r6
	subf	r5,r6,r5	/* #bytes left to go */

/*
 * first see if we can copy any more bytes before hitting another exception
 */
	mtctr	r5
r3_offset = 0
100:	EX_TABLE(100b, .Ldone)
43:	lbz	r0,0(r4)
	addi	r4,r4,1
stex;	stb	r0,0(r3)
	addi	r3,r3,1
	bdnz	43b
	li	r3,0		/* huh? all copied successfully this time? */
	blr

/*
 * here we have trapped again, amount remaining is in ctr.
 */
.Ldone:
	mfctr	r3
	blr

/*
 * exception handlers for stores: we need to work out how many bytes
 * weren't copied, and we may need to copy some more.
 * Note that the number of bytes of instructions for adjusting r3 needs
 * to equal the amount of the adjustment, due to the trick of using
 * .Lst_exc - r3_offset as the handler address.
 */
.Lst_exc_r7:
	add	r3,r3,r7
	b	.Lst_exc

	/* adjust by 24 */
	addi	r3,r3,8
	nop
	/* adjust by 16 */
	addi	r3,r3,8
	nop
	/* adjust by 8 */
	addi	r3,r3,4
	/* adjust by 4 */
	addi	r3,r3,4
.Lst_exc:
	ld	r6,-24(r1)	/* original destination pointer */
	ld	r4,-16(r1)	/* original source pointer */
	ld	r5,-8(r1)	/* original number of bytes */
	add	r7,r6,r5
	/*
	 * If the destination pointer isn't 8-byte aligned,
	 * we may have got the exception as a result of a
	 * store that overlapped a page boundary, so we may be
	 * able to copy a few more bytes.
	 */
17:	andi.	r0,r3,7
	beq	19f
	subf	r8,r6,r3	/* #bytes copied */
100:	EX_TABLE(100b,19f)
	lbzx	r0,r8,r4
100:	EX_TABLE(100b,19f)
	stb	r0,0(r3)
	addi	r3,r3,1
	cmpld	r3,r7
	blt	17b
19:	subf	r3,r3,r7	/* #bytes not copied in r3 */
	blr

/*
 * Routine to copy a whole page of data, optimized for POWER4.
 * On POWER4 it is more than 50% faster than the simple loop
 * above (following the .Ldst_aligned label).
 */
	.macro	exc
100:	EX_TABLE(100b, .Labort)
	.endm
.Lcopy_page_4K:
	std	r31,-32(1)
	std	r30,-40(1)
	std	r29,-48(1)
	std	r28,-56(1)
	std	r27,-64(1)
	std	r26,-72(1)
	std	r25,-80(1)
	std	r24,-88(1)
	std	r23,-96(1)
	std	r22,-104(1)
	std	r21,-112(1)
	std	r20,-120(1)
	li	r5,4096/32 - 1
	addi	r3,r3,-8
	li	r0,5
0:	addi	r5,r5,-24
	mtctr	r0
exc;	ld	r22,640(4)
exc;	ld	r21,512(4)
exc;	ld	r20,384(4)
exc;	ld	r11,256(4)
exc;	ld	r9,128(4)
exc;	ld	r7,0(4)
exc;	ld	r25,648(4)
exc;	ld	r24,520(4)
exc;	ld	r23,392(4)
exc;	ld	r10,264(4)
exc;	ld	r8,136(4)
exc;	ldu	r6,8(4)
	cmpwi	r5,24
1:
exc;	std	r22,648(3)
exc;	std	r21,520(3)
exc;	std	r20,392(3)
exc;	std	r11,264(3)
exc;	std	r9,136(3)
exc;	std	r7,8(3)
exc;	ld	r28,648(4)
exc;	ld	r27,520(4)
exc;	ld	r26,392(4)
exc;	ld	r31,264(4)
exc;	ld	r30,136(4)
exc;	ld	r29,8(4)
exc;	std	r25,656(3)
exc;	std	r24,528(3)
exc;	std	r23,400(3)
exc;	std	r10,272(3)
exc;	std	r8,144(3)
exc;	std	r6,16(3)
exc;	ld	r22,656(4)
exc;	ld	r21,528(4)
exc;	ld	r20,400(4)
exc;	ld	r11,272(4)
exc;	ld	r9,144(4)
exc;	ld	r7,16(4)
exc;	std	r28,664(3)
exc;	std	r27,536(3)
exc;	std	r26,408(3)
exc;	std	r31,280(3)
exc;	std	r30,152(3)
exc;	stdu	r29,24(3)
exc;	ld	r25,664(4)
exc;	ld	r24,536(4)
exc;	ld	r23,408(4)
exc;	ld	r10,280(4)
exc;	ld	r8,152(4)
exc;	ldu	r6,24(4)
	bdnz	1b
exc;	std	r22,648(3)
exc;	std	r21,520(3)
exc;	std	r20,392(3)
exc;	std	r11,264(3)
exc;	std	r9,136(3)
exc;	std	r7,8(3)
	addi	r4,r4,640
	addi	r3,r3,648
	bge	0b
	mtctr	r5
exc;	ld	r7,0(4)
exc;	ld	r8,8(4)
exc;	ldu	r9,16(4)
3:
exc;	ld	r10,8(4)
exc;	std	r7,8(3)
exc;	ld	r7,16(4)
exc;	std	r8,16(3)
exc;	ld	r8,24(4)
exc;	std	r9,24(3)
exc;	ldu	r9,32(4)
exc;	stdu	r10,32(3)
	bdnz	3b
4:
exc;	ld	r10,8(4)
exc;	std	r7,8(3)
exc;	std	r8,16(3)
exc;	std	r9,24(3)
exc;	std	r10,32(3)
9:	ld	r20,-120(1)
	ld	r21,-112(1)
	ld	r22,-104(1)
	ld	r23,-96(1)
	ld	r24,-88(1)
	ld	r25,-80(1)
	ld	r26,-72(1)
	ld	r27,-64(1)
	ld	r28,-56(1)
	ld	r29,-48(1)
	ld	r30,-40(1)
	ld	r31,-32(1)
	li	r3,0
	blr

/*
 * on an exception, reset to the beginning and jump back into the
 * standard __copy_tofrom_user
 */
.Labort:
	ld	r20,-120(1)
	ld	r21,-112(1)
	ld	r22,-104(1)
	ld	r23,-96(1)
	ld	r24,-88(1)
	ld	r25,-80(1)
	ld	r26,-72(1)
	ld	r27,-64(1)
	ld	r28,-56(1)
	ld	r29,-48(1)
	ld	r30,-40(1)
	ld	r31,-32(1)
	ld	r3,-24(r1)
	ld	r4,-16(r1)
	li	r5,4096
	b	.Ldst_aligned
EXPORT_SYMBOL(__copy_tofrom_user)
