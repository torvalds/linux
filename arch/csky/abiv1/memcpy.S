/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/linkage.h>

.macro	GET_FRONT_BITS rx y
#ifdef	__cskyLE__
	lsri	\rx, \y
#else
	lsli	\rx, \y
#endif
.endm

.macro	GET_AFTER_BITS rx y
#ifdef	__cskyLE__
	lsli	\rx, \y
#else
	lsri	\rx, \y
#endif
.endm

/* void *memcpy(void *dest, const void *src, size_t n); */
ENTRY(memcpy)
	mov	r7, r2
	cmplti	r4, 4
	bt	.L_copy_by_byte
	mov	r6, r2
	andi	r6, 3
	cmpnei	r6, 0
	jbt	.L_dest_not_aligned
	mov	r6, r3
	andi	r6, 3
	cmpnei	r6, 0
	jbt	.L_dest_aligned_but_src_not_aligned
.L0:
	cmplti	r4, 16
	jbt	.L_aligned_and_len_less_16bytes
	subi	sp, 8
	stw	r8, (sp, 0)
.L_aligned_and_len_larger_16bytes:
	ldw	r1, (r3, 0)
	ldw	r5, (r3, 4)
	ldw	r8, (r3, 8)
	stw	r1, (r7, 0)
	ldw	r1, (r3, 12)
	stw	r5, (r7, 4)
	stw	r8, (r7, 8)
	stw	r1, (r7, 12)
	subi	r4, 16
	addi	r3, 16
	addi	r7, 16
	cmplti	r4, 16
	jbf	.L_aligned_and_len_larger_16bytes
	ldw	r8, (sp, 0)
	addi	sp, 8
	cmpnei	r4, 0
	jbf	.L_return

.L_aligned_and_len_less_16bytes:
	cmplti	r4, 4
	bt	.L_copy_by_byte
.L1:
	ldw	r1, (r3, 0)
	stw	r1, (r7, 0)
	subi	r4, 4
	addi	r3, 4
	addi	r7, 4
	cmplti	r4, 4
	jbf	.L1
	br	.L_copy_by_byte

.L_return:
	rts

.L_copy_by_byte:                      /* len less than 4 bytes */
	cmpnei	r4, 0
	jbf	.L_return
.L4:
	ldb	r1, (r3, 0)
	stb	r1, (r7, 0)
	addi	r3, 1
	addi	r7, 1
	decne	r4
	jbt	.L4
	rts

/*
 * If dest is not aligned, just copying some bytes makes the dest align.
 * Afther that, we judge whether the src is aligned.
 */
.L_dest_not_aligned:
	mov	r5, r3
	rsub	r5, r5, r7
	abs	r5, r5
	cmplt	r5, r4
	bt	.L_copy_by_byte
	mov	r5, r7
	sub	r5, r3
	cmphs	r5, r4
	bf	.L_copy_by_byte
	mov	r5, r6
.L5:
	ldb	r1, (r3, 0)              /* makes the dest align. */
	stb	r1, (r7, 0)
	addi	r5, 1
	subi	r4, 1
	addi	r3, 1
	addi	r7, 1
	cmpnei	r5, 4
	jbt	.L5
	cmplti	r4, 4
	jbt	.L_copy_by_byte
	mov	r6, r3                   /* judge whether the src is aligned. */
	andi	r6, 3
	cmpnei	r6, 0
	jbf	.L0

/* Judge the number of misaligned, 1, 2, 3? */
.L_dest_aligned_but_src_not_aligned:
	mov	r5, r3
	rsub	r5, r5, r7
	abs	r5, r5
	cmplt	r5, r4
	bt	.L_copy_by_byte
	bclri	r3, 0
	bclri	r3, 1
	ldw	r1, (r3, 0)
	addi	r3, 4
	cmpnei	r6, 2
	bf	.L_dest_aligned_but_src_not_aligned_2bytes
	cmpnei	r6, 3
	bf	.L_dest_aligned_but_src_not_aligned_3bytes

.L_dest_aligned_but_src_not_aligned_1byte:
	mov	r5, r7
	sub	r5, r3
	cmphs	r5, r4
	bf	.L_copy_by_byte
	cmplti	r4, 16
	bf	.L11
.L10:                                     /* If the len is less than 16 bytes */
	GET_FRONT_BITS r1 8
	mov	r5, r1
	ldw	r6, (r3, 0)
	mov	r1, r6
	GET_AFTER_BITS r6 24
	or	r5, r6
	stw	r5, (r7, 0)
	subi	r4, 4
	addi	r3, 4
	addi	r7, 4
	cmplti	r4, 4
	bf	.L10
	subi	r3, 3
	br	.L_copy_by_byte
.L11:
	subi	sp, 16
	stw	r8, (sp, 0)
	stw	r9, (sp, 4)
	stw	r10, (sp, 8)
	stw	r11, (sp, 12)
.L12:
	ldw	r5, (r3, 0)
	ldw	r11, (r3, 4)
	ldw	r8, (r3, 8)
	ldw	r9, (r3, 12)

	GET_FRONT_BITS r1 8               /* little or big endian? */
	mov	r10, r5
	GET_AFTER_BITS r5 24
	or	r5, r1

	GET_FRONT_BITS r10 8
	mov	r1, r11
	GET_AFTER_BITS r11 24
	or	r11, r10

	GET_FRONT_BITS r1 8
	mov	r10, r8
	GET_AFTER_BITS r8 24
	or	r8, r1

	GET_FRONT_BITS r10 8
	mov	r1, r9
	GET_AFTER_BITS r9 24
	or	r9, r10

	stw	r5, (r7, 0)
	stw	r11, (r7, 4)
	stw	r8, (r7, 8)
	stw	r9, (r7, 12)
	subi	r4, 16
	addi	r3, 16
	addi	r7, 16
	cmplti	r4, 16
	jbf	.L12
	ldw	r8, (sp, 0)
	ldw	r9, (sp, 4)
	ldw	r10, (sp, 8)
	ldw	r11, (sp, 12)
	addi	sp , 16
	cmplti	r4, 4
	bf	.L10
	subi	r3, 3
	br	.L_copy_by_byte

.L_dest_aligned_but_src_not_aligned_2bytes:
	cmplti	r4, 16
	bf	.L21
.L20:
	GET_FRONT_BITS r1 16
	mov	r5, r1
	ldw	r6, (r3, 0)
	mov	r1, r6
	GET_AFTER_BITS r6 16
	or	r5, r6
	stw	r5, (r7, 0)
	subi	r4, 4
	addi	r3, 4
	addi	r7, 4
	cmplti	r4, 4
	bf	.L20
	subi	r3, 2
	br	.L_copy_by_byte
	rts

.L21:	/* n > 16 */
	subi 	sp, 16
	stw	r8, (sp, 0)
	stw	r9, (sp, 4)
	stw	r10, (sp, 8)
	stw	r11, (sp, 12)

.L22:
	ldw	r5, (r3, 0)
	ldw	r11, (r3, 4)
	ldw	r8, (r3, 8)
	ldw	r9, (r3, 12)

	GET_FRONT_BITS r1 16
	mov	r10, r5
	GET_AFTER_BITS r5 16
	or	r5, r1

	GET_FRONT_BITS r10 16
	mov	r1, r11
	GET_AFTER_BITS r11 16
	or	r11, r10

	GET_FRONT_BITS r1 16
	mov	r10, r8
	GET_AFTER_BITS r8 16
	or	r8, r1

	GET_FRONT_BITS r10 16
	mov	r1, r9
	GET_AFTER_BITS r9 16
	or	r9, r10

	stw	r5, (r7, 0)
	stw	r11, (r7, 4)
	stw	r8, (r7, 8)
	stw	r9, (r7, 12)
	subi	r4, 16
	addi	r3, 16
	addi	r7, 16
	cmplti	r4, 16
	jbf	.L22
	ldw	r8, (sp, 0)
	ldw	r9, (sp, 4)
	ldw	r10, (sp, 8)
	ldw	r11, (sp, 12)
	addi	sp, 16
	cmplti	r4, 4
	bf	.L20
	subi	r3, 2
	br	.L_copy_by_byte


.L_dest_aligned_but_src_not_aligned_3bytes:
	cmplti	r4, 16
	bf	.L31
.L30:
	GET_FRONT_BITS r1 24
	mov	r5, r1
	ldw	r6, (r3, 0)
	mov	r1, r6
	GET_AFTER_BITS r6 8
	or	r5, r6
	stw	r5, (r7, 0)
	subi	r4, 4
	addi	r3, 4
	addi	r7, 4
	cmplti	r4, 4
	bf	.L30
	subi	r3, 1
	br	.L_copy_by_byte
.L31:
	subi	sp, 16
	stw	r8, (sp, 0)
	stw	r9, (sp, 4)
	stw	r10, (sp, 8)
	stw	r11, (sp, 12)
.L32:
	ldw	r5, (r3, 0)
	ldw	r11, (r3, 4)
	ldw	r8, (r3, 8)
	ldw	r9, (r3, 12)

	GET_FRONT_BITS r1 24
	mov	r10, r5
	GET_AFTER_BITS r5 8
	or	r5, r1

	GET_FRONT_BITS r10 24
	mov	r1, r11
	GET_AFTER_BITS r11 8
	or	r11, r10

	GET_FRONT_BITS r1 24
	mov	r10, r8
	GET_AFTER_BITS r8 8
	or	r8, r1

	GET_FRONT_BITS r10 24
	mov	r1, r9
	GET_AFTER_BITS r9 8
	or	r9, r10

	stw	r5, (r7, 0)
	stw	r11, (r7, 4)
	stw	r8, (r7, 8)
	stw	r9, (r7, 12)
	subi	r4, 16
	addi	r3, 16
	addi	r7, 16
	cmplti	r4, 16
	jbf	.L32
	ldw	r8, (sp, 0)
	ldw	r9, (sp, 4)
	ldw	r10, (sp, 8)
	ldw	r11, (sp, 12)
	addi	sp, 16
	cmplti	r4, 4
	bf	.L30
	subi	r3, 1
	br	.L_copy_by_byte
