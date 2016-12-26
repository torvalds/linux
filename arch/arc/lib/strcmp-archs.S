/*
 * Copyright (C) 2014-15 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>

ENTRY_CFI(strcmp)
	or	r2, r0, r1
	bmsk_s	r2, r2, 1
	brne	r2, 0, @.Lcharloop

;;; s1 and s2 are word aligned
	ld.ab	r2, [r0, 4]

	mov_s	r12, 0x01010101
	ror	r11, r12
	.align  4
.LwordLoop:
	ld.ab	r3, [r1, 4]
	;; Detect NULL char in str1
	sub	r4, r2, r12
	ld.ab	r5, [r0, 4]
	bic	r4, r4, r2
	and	r4, r4, r11
	brne.d.nt	r4, 0, .LfoundNULL
	;; Check if the read locations are the same
	cmp	r2, r3
	beq.d	.LwordLoop
	mov.eq	r2, r5

	;; A match is found, spot it out
#ifdef __LITTLE_ENDIAN__
	swape	r3, r3
	mov_s	r0, 1
	swape	r2, r2
#else
	mov_s	r0, 1
#endif
	cmp_s	r2, r3
	j_s.d	[blink]
	bset.lo	r0, r0, 31

	.align 4
.LfoundNULL:
#ifdef __BIG_ENDIAN__
	swape	r4, r4
	swape	r2, r2
	swape	r3, r3
#endif
	;; Find null byte
	ffs	r0, r4
	bmsk	r2, r2, r0
	bmsk	r3, r3, r0
	swape	r2, r2
	swape	r3, r3
	;; make the return value
	sub.f	r0, r2, r3
	mov.hi	r0, 1
	j_s.d	[blink]
	bset.lo	r0, r0, 31

	.align 4
.Lcharloop:
	ldb.ab	r2, [r0, 1]
	ldb.ab	r3, [r1, 1]
	nop
	breq	r2, 0, .Lcmpend
	breq	r2, r3, .Lcharloop

	.align 4
.Lcmpend:
	j_s.d	[blink]
	sub	r0, r2, r3
END_CFI(strcmp)
