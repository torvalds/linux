/* $Id: memmove.S,v 1.2 2001/07/27 11:51:09 gniibe Exp $
 *
 * "memmove" implementation of SuperH
 *
 * Copyright (C) 1999  Niibe Yutaka
 *
 */

/*
 * void *memmove(void *dst, const void *src, size_t n);
 * The memory areas may overlap.
 */

#include <linux/linkage.h>
ENTRY(memmove)
	! if dest > src, call memcpy (it copies in decreasing order)
	cmp/hi	r5,r4
	bf	1f
	mov.l	2f,r0
	jmp	@r0
	 nop
	.balign 4
2:	.long	memcpy
1:
	sub	r5,r4		! From here, r4 has the distance to r0
	tst	r6,r6
	bt/s	9f		! if n=0, do nothing
	 mov	r5,r0
	add	r6,r5
	mov	#12,r1
	cmp/gt	r6,r1
	bt/s	8f		! if it's too small, copy a byte at once
	 add	#-1,r4
	add	#1,r4
	!
	!                [ ...  ] DST             [ ...  ] SRC
	!	         [ ...  ]                 [ ...  ]
	!	           :                        :
	!      r0+r4-->  [ ...  ]       r0    --> [ ...  ]
	!	           :                        :
	!	         [ ...  ]                 [ ...  ]
	!			        r5    -->
	!
	mov	r4,r1
	mov	#3,r2
	and	r2,r1
	shll2	r1
	mov	r0,r3		! Save the value on R0 to R3
	mova	jmptable,r0
	add	r1,r0
	mov.l	@r0,r1
	jmp	@r1
	 mov	r3,r0		! and back to R0
	.balign	4
jmptable:
	.long	case0
	.long	case1
	.long	case2
	.long	case3

	! copy a byte at once
8:	mov.b	@r0+,r1
	cmp/hs	r5,r0
	bf/s	8b			! while (r0<r5)
	 mov.b	r1,@(r0,r4)
	add	#1,r4
9:
	add	r4,r0
	rts
	 sub	r6,r0

case_none:
	bra	8b
	 add	#-1,r4

case0:
	!
	!	GHIJ KLMN OPQR -->  GHIJ KLMN OPQR
	!
	! First, align to long word boundary
	mov	r0,r3
	and	r2,r3
	tst	r3,r3
	bt/s	2f
	 add	#-1,r4
	mov	#4,r2
	sub	r3,r2
1:	dt	r2
	mov.b	@r0+,r1
	bf/s	1b
	 mov.b	r1,@(r0,r4)
	!
2:	! Second, copy a long word at once
	add	#-3,r4
	add	#-3,r5
3:	mov.l	@r0+,r1
	cmp/hs	r5,r0
	bf/s	3b
	 mov.l	r1,@(r0,r4)
	add	#3,r5
	!
	! Third, copy a byte at once, if necessary
	cmp/eq	r5,r0
	bt/s	9b
	 add	#4,r4
	bra	8b
	 add	#-1,r4

case3:
	!
	!	GHIJ KLMN OPQR -->  ...G HIJK LMNO PQR.
	!
	! First, align to long word boundary
	mov	r0,r3
	and	r2,r3
	tst	r3,r3
	bt/s	2f
	 add	#-1,r4
	mov	#4,r2
	sub	r3,r2
1:	dt	r2
	mov.b	@r0+,r1
	bf/s	1b
	 mov.b	r1,@(r0,r4)
	!
2:	! Second, read a long word and write a long word at once
	add	#-2,r4
	mov.l	@(r0,r4),r1
	add	#-7,r5
	add	#-4,r4
	!
#ifdef __LITTLE_ENDIAN__
	shll8	r1
3:	mov	r1,r3		! JIHG
	shlr8	r3		! xJIH
	mov.l	@r0+,r1		! NMLK
	mov	r1,r2
	shll16	r2
	shll8	r2		! Kxxx
	or	r2,r3		! KJIH
	cmp/hs	r5,r0
	bf/s	3b
	 mov.l	r3,@(r0,r4)
#else
	shlr8	r1
3:	mov	r1,r3		! GHIJ
	shll8	r3		! HIJx
	mov.l	@r0+,r1		! KLMN
	mov	r1,r2
	shlr16	r2
	shlr8	r2		! xxxK
	or	r2,r3		! HIJK
	cmp/hs	r5,r0
	bf/s	3b
	 mov.l	r3,@(r0,r4)
#endif
	add	#7,r5
	!
	! Third, copy a byte at once, if necessary
	cmp/eq	r5,r0
	bt/s	9b
	 add	#7,r4
	add	#-3,r0
	bra	8b
	 add	#-1,r4

case2:
	!
	!	GHIJ KLMN OPQR -->  ..GH IJKL MNOP QR..
	!
	! First, align to word boundary
	tst	#1,r0
	bt/s	2f
	 add	#-1,r4
	mov.b	@r0+,r1
	mov.b	r1,@(r0,r4)
	!
2:	! Second, read a word and write a word at once
	add	#-1,r4
	add	#-1,r5
	!
3:	mov.w	@r0+,r1
	cmp/hs	r5,r0
	bf/s	3b
	 mov.w	r1,@(r0,r4)
	add	#1,r5
	!
	! Third, copy a byte at once, if necessary
	cmp/eq	r5,r0
	bt/s	9b
	 add	#2,r4
	mov.b	@r0,r1
	mov.b	r1,@(r0,r4)
	bra	9b
	 add	#1,r0

case1:
	!
	!	GHIJ KLMN OPQR -->  .GHI JKLM NOPQ R...
	!
	! First, align to long word boundary
	mov	r0,r3
	and	r2,r3
	tst	r3,r3
	bt/s	2f
	 add	#-1,r4
	mov	#4,r2
	sub	r3,r2
1:	dt	r2
	mov.b	@r0+,r1
	bf/s	1b
	 mov.b	r1,@(r0,r4)
	!
2:	! Second, read a long word and write a long word at once
	mov.l	@(r0,r4),r1
	add	#-7,r5
	add	#-4,r4
	!
#ifdef __LITTLE_ENDIAN__
	shll16	r1
	shll8	r1
3:	mov	r1,r3		! JIHG
	shlr16	r3
	shlr8	r3		! xxxJ
	mov.l	@r0+,r1		! NMLK
	mov	r1,r2
	shll8	r2		! MLKx
	or	r2,r3		! MLKJ
	cmp/hs	r5,r0
	bf/s	3b
	 mov.l	r3,@(r0,r4)
#else
	shlr16	r1
	shlr8	r1
3:	mov	r1,r3		! GHIJ
	shll16	r3
	shll8	r3		! Jxxx
	mov.l	@r0+,r1		! KLMN
	mov	r1,r2
	shlr8	r2		! xKLM
	or	r2,r3		! JKLM
	cmp/hs	r5,r0
	bf/s	3b		! while(r0<r5)
	 mov.l	r3,@(r0,r4)
#endif
	add	#7,r5
	!
	! Third, copy a byte at once, if necessary
	cmp/eq	r5,r0
	bt/s	9b
	 add	#5,r4
	add	#-3,r0
	bra	8b
	 add	#-1,r4
