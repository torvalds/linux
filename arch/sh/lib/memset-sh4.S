/* SPDX-License-Identifier: GPL-2.0 */
/*
 * "memset" implementation for SH4
 *
 * Copyright (C) 1999  Niibe Yutaka
 * Copyright (c) 2009  STMicroelectronics Limited
 * Author: Stuart Menefy <stuart.menefy:st.com>
 */

/*
 *            void *memset(void *s, int c, size_t n);
 */

#include <linux/linkage.h>

ENTRY(memset)
	mov	#12,r0
	add	r6,r4
	cmp/gt	r6,r0
	bt/s	40f		! if it's too small, set a byte at once
	 mov	r4,r0
	and	#3,r0
	cmp/eq	#0,r0
	bt/s	2f		! It's aligned
	 sub	r0,r6
1:
	dt	r0
	bf/s	1b
	 mov.b	r5,@-r4
2:				! make VVVV
	extu.b	r5,r5
	swap.b	r5,r0		!   V0
	or	r0,r5		!   VV
	swap.w	r5,r0		! VV00
	or	r0,r5		! VVVV

	! Check if enough bytes need to be copied to be worth the big loop
	mov	#0x40, r0	! (MT)
	cmp/gt	r6,r0		! (MT)  64 > len => slow loop

	bt/s	22f
	 mov	r6,r0

	! align the dst to the cache block size if necessary
	mov	r4, r3
	mov	#~(0x1f), r1

	and	r3, r1
	cmp/eq	r3, r1

	bt/s	11f		! dst is already aligned
	 sub	r1, r3		! r3-r1 -> r3
	shlr2	r3		! number of loops

10:	mov.l	r5,@-r4
	dt	r3
	bf/s	10b
	 add	#-4, r6

11:	! dst is 32byte aligned
	mov	r6,r2
	mov	#-5,r0
	shld	r0,r2		! number of loops

	add	#-32, r4
	mov	r5, r0
12:
	movca.l	r0,@r4
	mov.l	r5,@(4, r4)
	mov.l	r5,@(8, r4)
	mov.l	r5,@(12,r4)
	mov.l	r5,@(16,r4)
	mov.l	r5,@(20,r4)
	add	#-0x20, r6
	mov.l	r5,@(24,r4)
	dt	r2
	mov.l	r5,@(28,r4)
	bf/s	12b
	 add	#-32, r4

	add	#32, r4
	mov	#8, r0
	cmp/ge	r0, r6
	bf	40f

	mov	r6,r0
22:
	shlr2	r0
	shlr	r0		! r0 = r6 >> 3
3:
	dt	r0
	mov.l	r5,@-r4		! set 8-byte at once
	bf/s	3b
	 mov.l	r5,@-r4
	!
	mov	#7,r0
	and	r0,r6

	! fill bytes (length may be zero)
40:	tst	r6,r6
	bt	5f
4:
	dt	r6
	bf/s	4b
	 mov.b	r5,@-r4
5:
	rts
	 mov	r4,r0
