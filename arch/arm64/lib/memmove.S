/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 ARM Ltd.
 * Copyright (C) 2013 Linaro.
 *
 * This code is based on glibc cortex strings work originally authored by Linaro
 * be found @
 *
 * http://bazaar.launchpad.net/~linaro-toolchain-dev/cortex-strings/trunk/
 * files/head:/src/aarch64/
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/cache.h>

/*
 * Move a buffer from src to test (alignment handled by the hardware).
 * If dest <= src, call memcpy, otherwise copy in reverse order.
 *
 * Parameters:
 *	x0 - dest
 *	x1 - src
 *	x2 - n
 * Returns:
 *	x0 - dest
 */
dstin	.req	x0
src	.req	x1
count	.req	x2
tmp1	.req	x3
tmp1w	.req	w3
tmp2	.req	x4
tmp2w	.req	w4
tmp3	.req	x5
tmp3w	.req	w5
dst	.req	x6

A_l	.req	x7
A_h	.req	x8
B_l	.req	x9
B_h	.req	x10
C_l	.req	x11
C_h	.req	x12
D_l	.req	x13
D_h	.req	x14

SYM_FUNC_START_ALIAS(__memmove)
SYM_FUNC_START_WEAK_PI(memmove)
	cmp	dstin, src
	b.lo	__memcpy
	add	tmp1, src, count
	cmp	dstin, tmp1
	b.hs	__memcpy		/* No overlap.  */

	add	dst, dstin, count
	add	src, src, count
	cmp	count, #16
	b.lo	.Ltail15  /*probably non-alignment accesses.*/

	ands	tmp2, src, #15     /* Bytes to reach alignment.  */
	b.eq	.LSrcAligned
	sub	count, count, tmp2
	/*
	* process the aligned offset length to make the src aligned firstly.
	* those extra instructions' cost is acceptable. It also make the
	* coming accesses are based on aligned address.
	*/
	tbz	tmp2, #0, 1f
	ldrb	tmp1w, [src, #-1]!
	strb	tmp1w, [dst, #-1]!
1:
	tbz	tmp2, #1, 2f
	ldrh	tmp1w, [src, #-2]!
	strh	tmp1w, [dst, #-2]!
2:
	tbz	tmp2, #2, 3f
	ldr	tmp1w, [src, #-4]!
	str	tmp1w, [dst, #-4]!
3:
	tbz	tmp2, #3, .LSrcAligned
	ldr	tmp1, [src, #-8]!
	str	tmp1, [dst, #-8]!

.LSrcAligned:
	cmp	count, #64
	b.ge	.Lcpy_over64

	/*
	* Deal with small copies quickly by dropping straight into the
	* exit block.
	*/
.Ltail63:
	/*
	* Copy up to 48 bytes of data. At this point we only need the
	* bottom 6 bits of count to be accurate.
	*/
	ands	tmp1, count, #0x30
	b.eq	.Ltail15
	cmp	tmp1w, #0x20
	b.eq	1f
	b.lt	2f
	ldp	A_l, A_h, [src, #-16]!
	stp	A_l, A_h, [dst, #-16]!
1:
	ldp	A_l, A_h, [src, #-16]!
	stp	A_l, A_h, [dst, #-16]!
2:
	ldp	A_l, A_h, [src, #-16]!
	stp	A_l, A_h, [dst, #-16]!

.Ltail15:
	tbz	count, #3, 1f
	ldr	tmp1, [src, #-8]!
	str	tmp1, [dst, #-8]!
1:
	tbz	count, #2, 2f
	ldr	tmp1w, [src, #-4]!
	str	tmp1w, [dst, #-4]!
2:
	tbz	count, #1, 3f
	ldrh	tmp1w, [src, #-2]!
	strh	tmp1w, [dst, #-2]!
3:
	tbz	count, #0, .Lexitfunc
	ldrb	tmp1w, [src, #-1]
	strb	tmp1w, [dst, #-1]

.Lexitfunc:
	ret

.Lcpy_over64:
	subs	count, count, #128
	b.ge	.Lcpy_body_large
	/*
	* Less than 128 bytes to copy, so handle 64 bytes here and then jump
	* to the tail.
	*/
	ldp	A_l, A_h, [src, #-16]
	stp	A_l, A_h, [dst, #-16]
	ldp	B_l, B_h, [src, #-32]
	ldp	C_l, C_h, [src, #-48]
	stp	B_l, B_h, [dst, #-32]
	stp	C_l, C_h, [dst, #-48]
	ldp	D_l, D_h, [src, #-64]!
	stp	D_l, D_h, [dst, #-64]!

	tst	count, #0x3f
	b.ne	.Ltail63
	ret

	/*
	* Critical loop. Start at a new cache line boundary. Assuming
	* 64 bytes per line this ensures the entire loop is in one line.
	*/
	.p2align	L1_CACHE_SHIFT
.Lcpy_body_large:
	/* pre-load 64 bytes data. */
	ldp	A_l, A_h, [src, #-16]
	ldp	B_l, B_h, [src, #-32]
	ldp	C_l, C_h, [src, #-48]
	ldp	D_l, D_h, [src, #-64]!
1:
	/*
	* interlace the load of next 64 bytes data block with store of the last
	* loaded 64 bytes data.
	*/
	stp	A_l, A_h, [dst, #-16]
	ldp	A_l, A_h, [src, #-16]
	stp	B_l, B_h, [dst, #-32]
	ldp	B_l, B_h, [src, #-32]
	stp	C_l, C_h, [dst, #-48]
	ldp	C_l, C_h, [src, #-48]
	stp	D_l, D_h, [dst, #-64]!
	ldp	D_l, D_h, [src, #-64]!
	subs	count, count, #64
	b.ge	1b
	stp	A_l, A_h, [dst, #-16]
	stp	B_l, B_h, [dst, #-32]
	stp	C_l, C_h, [dst, #-48]
	stp	D_l, D_h, [dst, #-64]!

	tst	count, #0x3f
	b.ne	.Ltail63
	ret
SYM_FUNC_END_PI(memmove)
EXPORT_SYMBOL(memmove)
SYM_FUNC_END_ALIAS(__memmove)
EXPORT_SYMBOL(__memmove)
