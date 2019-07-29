/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/linkage.h>
#include "sysdep.h"

ENTRY(__memcpy)
ENTRY(memcpy)
	/* Test if len less than 4 bytes.  */
	mov	r12, r0
	cmplti	r2, 4
	bt	.L_copy_by_byte

	andi	r13, r0, 3
	movi	r19, 4
	/* Test if dest is not 4 bytes aligned.  */
	bnez	r13, .L_dest_not_aligned

/* Hardware can handle unaligned access directly.  */
.L_dest_aligned:
	/* If dest is aligned, then copy.  */
	zext	r18, r2, 31, 4

	/* Test if len less than 16 bytes.  */
	bez	r18, .L_len_less_16bytes
	movi	r19, 0

	LABLE_ALIGN
.L_len_larger_16bytes:
#if defined(__CSKY_VDSPV2__)
	vldx.8	vr0, (r1), r19
	PRE_BNEZAD (r18)
	addi	r1, 16
	vstx.8	vr0, (r0), r19
	addi	r0, 16
#elif defined(__CK860__)
	ldw	r3, (r1, 0)
	stw	r3, (r0, 0)
	ldw	r3, (r1, 4)
	stw	r3, (r0, 4)
	ldw	r3, (r1, 8)
	stw	r3, (r0, 8)
	ldw	r3, (r1, 12)
	addi	r1, 16
	stw	r3, (r0, 12)
	addi	r0, 16
#else
	ldw	r20, (r1, 0)
	ldw	r21, (r1, 4)
	ldw	r22, (r1, 8)
	ldw	r23, (r1, 12)
	stw	r20, (r0, 0)
	stw	r21, (r0, 4)
	stw	r22, (r0, 8)
	stw	r23, (r0, 12)
	PRE_BNEZAD (r18)
	addi	r1, 16
	addi	r0, 16
#endif
	BNEZAD (r18, .L_len_larger_16bytes)

.L_len_less_16bytes:
	zext	r18, r2, 3, 2
	bez	r18, .L_copy_by_byte
.L_len_less_16bytes_loop:
	ldw	r3, (r1, 0)
	PRE_BNEZAD (r18)
	addi	r1, 4
	stw	r3, (r0, 0)
	addi	r0, 4
	BNEZAD (r18, .L_len_less_16bytes_loop)

/* Test if len less than 4 bytes.  */
.L_copy_by_byte:
	zext	r18, r2, 1, 0
	bez	r18, .L_return
.L_copy_by_byte_loop:
	ldb	r3, (r1, 0)
	PRE_BNEZAD (r18)
	addi	r1, 1
	stb	r3, (r0, 0)
	addi	r0, 1
	BNEZAD (r18, .L_copy_by_byte_loop)

.L_return:
	mov	r0, r12
	rts

/*
 * If dest is not aligned, just copying some bytes makes the
 * dest align.
 */
.L_dest_not_aligned:
	sub	r13, r19, r13
	sub	r2, r13

/* Makes the dest align.  */
.L_dest_not_aligned_loop:
	ldb	r3, (r1, 0)
	PRE_BNEZAD (r13)
	addi	r1, 1
	stb	r3, (r0, 0)
	addi	r0, 1
	BNEZAD (r13, .L_dest_not_aligned_loop)
	cmplti	r2, 4
	bt	.L_copy_by_byte

	/* Check whether the src is aligned.  */
	jbr	.L_dest_aligned
ENDPROC(__memcpy)
