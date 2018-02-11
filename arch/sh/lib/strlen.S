/* SPDX-License-Identifier: GPL-2.0 */
/* $Id: strlen.S,v 1.2 2001/06/29 14:07:15 gniibe Exp $
 *
 * "strlen" implementation of SuperH
 *
 * Copyright (C) 1999  Kaz Kojima
 *
 */

/* size_t strlen (const char *s)  */

#include <linux/linkage.h>
ENTRY(strlen)
	mov	r4,r0
	and	#3,r0
	tst	r0,r0
	bt/s	1f
	 mov	#0,r2

	add	#-1,r0
	shll2	r0
	shll	r0
	braf	r0
	 nop

	mov.b	@r4+,r1
	tst	r1,r1
	bt	8f
	add	#1,r2

	mov.b	@r4+,r1
	tst	r1,r1
	bt	8f
	add	#1,r2

	mov.b	@r4+,r1
	tst	r1,r1
	bt	8f
	add	#1,r2

1:
	mov	#0,r3
2:
	mov.l	@r4+,r1
	cmp/str	r3,r1
	bf/s	2b
	 add	#4,r2

	add	#-4,r2
#ifndef __LITTLE_ENDIAN__
	swap.b	r1,r1
	swap.w	r1,r1
	swap.b	r1,r1
#endif
	extu.b	r1,r0
	tst	r0,r0
	bt/s	8f
	 shlr8	r1
	add	#1,r2
	extu.b	r1,r0
	tst	r0,r0
	bt/s	8f
	 shlr8	r1
	add	#1,r2
	extu.b	r1,r0
	tst	r0,r0
	bt	8f
	add	#1,r2
8:
	rts
	 mov	r2,r0
