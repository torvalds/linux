/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/linkage.h>
#include "sysdep.h"

	.weak memmove
ENTRY(__memmove)
ENTRY(memmove)
	subu	r3, r0, r1
	cmphs	r3, r2
	bt	memcpy

	mov	r12, r0
	addu	r0, r0, r2
	addu	r1, r1, r2

	/* Test if len less than 4 bytes.  */
	cmplti	r2, 4
	bt	.L_copy_by_byte

	andi	r13, r0, 3
	/* Test if dest is not 4 bytes aligned.  */
	bnez	r13, .L_dest_not_aligned
	/* Hardware can handle unaligned access directly.  */
.L_dest_aligned:
	/* If dest is aligned, then copy.  */
	zext	r18, r2, 31, 4
	/* Test if len less than 16 bytes.  */
	bez	r18, .L_len_less_16bytes
	movi	r19, 0

	/* len > 16 bytes */
	LABLE_ALIGN
.L_len_larger_16bytes:
	subi	r1, 16
	subi	r0, 16
#if defined(__CK860__)
	ldw	r3, (r1, 12)
	stw	r3, (r0, 12)
	ldw	r3, (r1, 8)
	stw	r3, (r0, 8)
	ldw	r3, (r1, 4)
	stw	r3, (r0, 4)
	ldw	r3, (r1, 0)
	stw	r3, (r0, 0)
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
#endif
	BNEZAD (r18, .L_len_larger_16bytes)

.L_len_less_16bytes:
	zext	r18, r2, 3, 2
	bez	r18, .L_copy_by_byte
.L_len_less_16bytes_loop:
	subi	r1, 4
	subi	r0, 4
	ldw	r3, (r1, 0)
	PRE_BNEZAD (r18)
	stw	r3, (r0, 0)
	BNEZAD (r18, .L_len_less_16bytes_loop)

	/* Test if len less than 4 bytes.  */
.L_copy_by_byte:
	zext	r18, r2, 1, 0
	bez	r18, .L_return
.L_copy_by_byte_loop:
	subi	r1, 1
	subi	r0, 1
	ldb	r3, (r1, 0)
	PRE_BNEZAD (r18)
	stb	r3, (r0, 0)
	BNEZAD (r18, .L_copy_by_byte_loop)

.L_return:
	mov	r0, r12
	rts

	/* If dest is not aligned, just copy some bytes makes the dest
	   align.  */
.L_dest_not_aligned:
	sub	r2, r13
.L_dest_not_aligned_loop:
	subi	r1, 1
	subi	r0, 1
	/* Makes the dest align.  */
	ldb	r3, (r1, 0)
	PRE_BNEZAD (r13)
	stb	r3, (r0, 0)
	BNEZAD (r13, .L_dest_not_aligned_loop)
	cmplti	r2, 4
	bt	.L_copy_by_byte
	/* Check whether the src is aligned.  */
	jbr	.L_dest_aligned
ENDPROC(memmove)
ENDPROC(__memmove)
