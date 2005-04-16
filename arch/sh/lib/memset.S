/* $Id: memset.S,v 1.1 2000/04/14 16:49:01 mjd Exp $
 *
 * "memset" implementation of SuperH
 *
 * Copyright (C) 1999  Niibe Yutaka
 *
 */

/*
 *            void *memset(void *s, int c, size_t n);
 */

#include <linux/linkage.h>

ENTRY(memset)
	tst	r6,r6
	bt/s	5f		! if n=0, do nothing
	 add	r6,r4
	mov	#12,r0
	cmp/gt	r6,r0
	bt/s	4f		! if it's too small, set a byte at once
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
	swap.b	r5,r0		!   V0
	or	r0,r5		!   VV
	swap.w	r5,r0		! VV00
	or	r0,r5		! VVVV
	!
	mov	r6,r0
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
	tst	r6,r6
	bt	5f
	! fill bytes
4:
	dt	r6
	bf/s	4b
	 mov.b	r5,@-r4
5:
	rts
	 mov	r4,r0
