/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/linkage.h>
#include "sysdep.h"

	.weak memset
ENTRY(__memset)
ENTRY(memset)
	/* Test if len less than 4 bytes.  */
	mov	r12, r0
	cmplti	r2, 8
	bt	.L_set_by_byte

	andi	r13, r0, 3
	movi	r19, 4
	/* Test if dest is not 4 bytes aligned.  */
	bnez	r13, .L_dest_not_aligned
	/* Hardware can handle unaligned access directly.  */
.L_dest_aligned:
        zextb   r3, r1
        lsli    r1, 8
        or      r1, r3
        lsli    r3, r1, 16
        or      r3, r1

	/* If dest is aligned, then copy.  */
	zext	r18, r2, 31, 4
	/* Test if len less than 16 bytes.  */
	bez	r18, .L_len_less_16bytes

	LABLE_ALIGN
.L_len_larger_16bytes:
	stw	r3, (r0, 0)
	stw	r3, (r0, 4)
	stw	r3, (r0, 8)
	stw	r3, (r0, 12)
	PRE_BNEZAD (r18)
	addi	r0, 16
	BNEZAD (r18, .L_len_larger_16bytes)

.L_len_less_16bytes:
	zext	r18, r2, 3, 2
	andi	r2, 3
	bez	r18, .L_set_by_byte
.L_len_less_16bytes_loop:
	stw	r3, (r0, 0)
	PRE_BNEZAD (r18)
	addi	r0, 4
	BNEZAD (r18, .L_len_less_16bytes_loop)

	/* Test if len less than 4 bytes.  */
.L_set_by_byte:
	zext	r18, r2, 2, 0
	bez	r18, .L_return
.L_set_by_byte_loop:
	stb	r1, (r0, 0)
	PRE_BNEZAD (r18)
	addi	r0, 1
	BNEZAD (r18, .L_set_by_byte_loop)

.L_return:
	mov	r0, r12
	rts

	/* If dest is not aligned, just set some bytes makes the dest
	   align.  */

.L_dest_not_aligned:
	sub	r13, r19, r13
	sub	r2, r13
.L_dest_not_aligned_loop:
	/* Makes the dest align.  */
	stb	r1, (r0, 0)
	PRE_BNEZAD (r13)
	addi	r0, 1
	BNEZAD (r13, .L_dest_not_aligned_loop)
	cmplti	r2, 8
	bt	.L_set_by_byte
	/* Check whether the src is aligned.  */
	jbr	.L_dest_aligned
ENDPROC(memset)
ENDPROC(__memset)
