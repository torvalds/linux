/* $Id: strncpy_from_user.S,v 1.6 1999/05/25 16:53:05 jj Exp $
 * strncpy_from_user.S: Sparc64 strncpy from userspace.
 *
 *  Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 */

#include <asm/asi.h>
#include <asm/errno.h>

	.data
	.align	8
0:	.xword	0x0101010101010101

	.text
	.align	32

	/* Must return:
	 *
	 * -EFAULT		for an exception
	 * count		if we hit the buffer limit
	 * bytes copied		if we hit a null byte
	 * (without the null byte)
	 *
	 * This implementation assumes:
	 * %o1 is 8 aligned => !(%o2 & 7)
	 * %o0 is 8 aligned (if not, it will be slooooow, but will work)
	 *
	 * This is optimized for the common case:
	 * in my stats, 90% of src are 8 aligned (even on sparc32)
	 * and average length is 18 or so.
	 */

	.globl	__strncpy_from_user
	.type	__strncpy_from_user,#function
__strncpy_from_user:
	/* %o0=dest, %o1=src, %o2=count */
	andcc	%o1, 7, %g0		! IEU1	Group
	bne,pn	%icc, 30f		! CTI
	 add	%o0, %o2, %g3		! IEU0
60:	ldxa	[%o1] %asi, %g1		! Load	Group
	brlez,pn %o2, 10f		! CTI
	 mov	%o0, %o3		! IEU0
50:	sethi	%hi(0b), %o4		! IEU0	Group
	ldx	[%o4 + %lo(0b)], %o4	! Load
	sllx	%o4, 7, %o5		! IEU1	Group
1:	sub	%g1, %o4, %g2		! IEU0	Group
	stx	%g1, [%o0]		! Store
	add	%o0, 8, %o0		! IEU1
	andcc	%g2, %o5, %g0		! IEU1	Group
	bne,pn	%xcc, 5f		! CTI
	 add	%o1, 8, %o1		! IEU0
	cmp	%o0, %g3		! IEU1	Group
	bl,a,pt %xcc, 1b		! CTI
61:	 ldxa	[%o1] %asi, %g1		! Load
10:	retl				! CTI	Group
	 mov	%o2, %o0		! IEU0
5:	srlx	%g2, 32, %g7		! IEU0	Group
	sethi	%hi(0xff00), %o4	! IEU1
	andcc	%g7, %o5, %g0		! IEU1	Group
	be,pn	%icc, 2f		! CTI
	 or	%o4, %lo(0xff00), %o4	! IEU0
	srlx	%g1, 48, %g7		! IEU0	Group
	andcc	%g7, %o4, %g0		! IEU1	Group
	be,pn	%icc, 50f		! CTI
	 andcc	%g7, 0xff, %g0		! IEU1	Group
	be,pn	%icc, 51f		! CTI
	 srlx	%g1, 32, %g7		! IEU0
	andcc	%g7, %o4, %g0		! IEU1	Group
	be,pn	%icc, 52f		! CTI
	 andcc	%g7, 0xff, %g0		! IEU1	Group
	be,pn	%icc, 53f		! CTI
2:	 andcc	%g2, %o5, %g0		! IEU1	Group
	be,pn	%icc, 2f		! CTI
	 srl	%g1, 16, %g7		! IEU0
	andcc	%g7, %o4, %g0		! IEU1	Group
	be,pn	%icc, 54f		! CTI
	 andcc	%g7, 0xff, %g0		! IEU1	Group
	be,pn	%icc, 55f		! CTI
	 andcc	%g1, %o4, %g0		! IEU1	Group
	be,pn	%icc, 56f		! CTI
	 andcc	%g1, 0xff, %g0		! IEU1	Group
	be,a,pn	%icc, 57f		! CTI
	 sub	%o0, %o3, %o0		! IEU0
2:	cmp	%o0, %g3		! IEU1	Group
	bl,a,pt	%xcc, 50b		! CTI
62:	 ldxa	[%o1] %asi, %g1		! Load
	retl				! CTI	Group
	 mov	%o2, %o0		! IEU0
50:	sub	%o0, %o3, %o0
	retl
	 sub	%o0, 8, %o0
51:	sub	%o0, %o3, %o0
	retl
	 sub	%o0, 7, %o0
52:	sub	%o0, %o3, %o0
	retl
	 sub	%o0, 6, %o0
53:	sub	%o0, %o3, %o0
	retl
	 sub	%o0, 5, %o0
54:	sub	%o0, %o3, %o0
	retl
	 sub	%o0, 4, %o0
55:	sub	%o0, %o3, %o0
	retl
	 sub	%o0, 3, %o0
56:	sub	%o0, %o3, %o0
	retl
	 sub	%o0, 2, %o0
57:	retl
	 sub	%o0, 1, %o0
30:	brlez,pn %o2, 3f
	 sub	%g0, %o2, %o3
	add	%o0, %o2, %o0
63:	lduba	[%o1] %asi, %o4
1:	add	%o1, 1, %o1
	brz,pn	%o4, 2f
	 stb	%o4, [%o0 + %o3]
	addcc	%o3, 1, %o3
	bne,pt	%xcc, 1b
64:	 lduba	[%o1] %asi, %o4
3:	retl
	 mov	%o2, %o0
2:	retl
	 add	%o2, %o3, %o0
	.size	__strncpy_from_user, .-__strncpy_from_user

	.section __ex_table,#alloc
	.align	4
	.word	60b, __retl_efault
	.word	61b, __retl_efault
	.word	62b, __retl_efault
	.word	63b, __retl_efault
	.word	64b, __retl_efault
	.previous
