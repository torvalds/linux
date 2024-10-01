/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/linkage.h>
#include "sysdep.h"

ENTRY(strlen)
	/* Check if the start addr is aligned.  */
	mov	r3, r0
	andi	r1, r0, 3
	movi	r2, 4
	movi	r0, 0
	bnez	r1, .L_start_not_aligned

	LABLE_ALIGN
.L_start_addr_aligned:
	/* Check if all the bytes in the word are not zero.  */
	ldw	r1, (r3)
	tstnbz	r1
	bf	.L_string_tail

	ldw	r1, (r3, 4)
	addi	r0, 4
	tstnbz	r1
	bf	.L_string_tail

	ldw	r1, (r3, 8)
	addi	r0, 4
	tstnbz	r1
	bf	.L_string_tail

	ldw	r1, (r3, 12)
	addi	r0, 4
	tstnbz	r1
	bf	.L_string_tail

	ldw	r1, (r3, 16)
	addi	r0, 4
	tstnbz	r1
	bf	.L_string_tail

	ldw	r1, (r3, 20)
	addi	r0, 4
	tstnbz	r1
	bf	.L_string_tail

	ldw	r1, (r3, 24)
	addi	r0, 4
	tstnbz	r1
	bf	.L_string_tail

	ldw	r1, (r3, 28)
	addi	r0, 4
	tstnbz	r1
	bf	.L_string_tail

	addi	r0, 4
	addi	r3, 32
	br	.L_start_addr_aligned

.L_string_tail:
# ifdef __CSKYBE__
	xtrb0	r3, r1
	bez	r3, .L_return
	addi	r0, 1
	xtrb1	r3, r1
	bez	r3, .L_return
	addi	r0, 1
	xtrb2	r3, r1
	bez	r3, .L_return
	addi	r0, 1
# else
	xtrb3	r3, r1
	bez	r3, .L_return
	addi	r0, 1
	xtrb2	r3, r1
	bez	r3, .L_return
	addi	r0, 1
	xtrb1	r3, r1
	bez	r3, .L_return
	addi	r0, 1
# endif	/* !__CSKYBE__ */

.L_return:
	rts

.L_start_not_aligned:
	sub	r2, r2, r1
.L_start_not_aligned_loop:
	ldb	r1, (r3)
	PRE_BNEZAD (r2)
	addi	r3, 1
	bez	r1, .L_return
	addi	r0, 1
	BNEZAD (r2, .L_start_not_aligned_loop)
	br	.L_start_addr_aligned
ENDPROC(strlen)
