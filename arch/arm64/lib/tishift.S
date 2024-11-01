/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
 *
 * Copyright (C) 2017-2018 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include <linux/linkage.h>

#include <asm/assembler.h>

SYM_FUNC_START(__ashlti3)
	cbz	x2, 1f
	mov	x3, #64
	sub	x3, x3, x2
	cmp	x3, #0
	b.le	2f
	lsl	x1, x1, x2
	lsr	x3, x0, x3
	lsl	x2, x0, x2
	orr	x1, x1, x3
	mov	x0, x2
1:
	ret
2:
	neg	w1, w3
	mov	x2, #0
	lsl	x1, x0, x1
	mov	x0, x2
	ret
SYM_FUNC_END(__ashlti3)
EXPORT_SYMBOL(__ashlti3)

SYM_FUNC_START(__ashrti3)
	cbz	x2, 1f
	mov	x3, #64
	sub	x3, x3, x2
	cmp	x3, #0
	b.le	2f
	lsr	x0, x0, x2
	lsl	x3, x1, x3
	asr	x2, x1, x2
	orr	x0, x0, x3
	mov	x1, x2
1:
	ret
2:
	neg	w0, w3
	asr	x2, x1, #63
	asr	x0, x1, x0
	mov	x1, x2
	ret
SYM_FUNC_END(__ashrti3)
EXPORT_SYMBOL(__ashrti3)

SYM_FUNC_START(__lshrti3)
	cbz	x2, 1f
	mov	x3, #64
	sub	x3, x3, x2
	cmp	x3, #0
	b.le	2f
	lsr	x0, x0, x2
	lsl	x3, x1, x3
	lsr	x2, x1, x2
	orr	x0, x0, x3
	mov	x1, x2
1:
	ret
2:
	neg	w0, w3
	mov	x2, #0
	lsr	x0, x1, x0
	mov	x1, x2
	ret
SYM_FUNC_END(__lshrti3)
EXPORT_SYMBOL(__lshrti3)
