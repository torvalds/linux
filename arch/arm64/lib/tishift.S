/*
 * Copyright (C) 2017 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/linkage.h>

ENTRY(__ashlti3)
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
ENDPROC(__ashlti3)

ENTRY(__ashrti3)
	cbz	x2, 3f
	mov	x3, #64
	sub	x3, x3, x2
	cmp	x3, #0
	b.le	4f
	lsr	x0, x0, x2
	lsl	x3, x1, x3
	asr	x2, x1, x2
	orr	x0, x0, x3
	mov	x1, x2
3:
	ret
4:
	neg	w0, w3
	asr	x2, x1, #63
	asr	x0, x1, x0
	mov	x1, x2
	ret
ENDPROC(__ashrti3)

ENTRY(__lshrti3)
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
ENDPROC(__lshrti3)
