/* strlen_user.S: Sparc64 optimized strlen_user code
 *
 * Return length of string in userspace including terminating 0
 * or 0 for error
 *
 * Copyright (C) 1991,1996 Free Software Foundation
 * Copyright (C) 1996,1999 David S. Miller (davem@redhat.com)
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/linkage.h>
#include <asm/asi.h>

#define LO_MAGIC 0x01010101
#define HI_MAGIC 0x80808080

	.align 4
ENTRY(__strlen_user)
	sethi	%hi(32768), %o1
ENTRY(__strnlen_user)
	mov	%o1, %g1
	mov	%o0, %o1
	andcc	%o0, 3, %g0
	be,pt	%icc, 9f
	 sethi	%hi(HI_MAGIC), %o4
10:	lduba	[%o0] %asi, %o5
	brz,pn	%o5, 21f
	 add	%o0, 1, %o0
	andcc	%o0, 3, %g0
	be,pn	%icc, 4f
	 or	%o4, %lo(HI_MAGIC), %o3
11:	lduba	[%o0] %asi, %o5
	brz,pn	%o5, 22f
	 add	%o0, 1, %o0
	andcc	%o0, 3, %g0
	be,pt	%icc, 13f
	 srl	%o3, 7, %o2
12:	lduba	[%o0] %asi, %o5
	brz,pn	%o5, 23f
	 add	%o0, 1, %o0
	ba,pt	%icc, 2f
15:	 lda	[%o0] %asi, %o5
9:	or	%o4, %lo(HI_MAGIC), %o3
4:	srl	%o3, 7, %o2
13:	lda	[%o0] %asi, %o5
2:	sub	%o5, %o2, %o4
	andcc	%o4, %o3, %g0
	bne,pn	%icc, 82f
	 add	%o0, 4, %o0
	sub	%o0, %o1, %g2
81:	cmp	%g2, %g1
	blu,pt	%icc, 13b
	 mov	%o0, %o4
	ba,a,pt	%xcc, 1f

	/* Check every byte. */
82:	srl	%o5, 24, %g7
	andcc	%g7, 0xff, %g0
	be,pn	%icc, 1f
	 add	%o0, -3, %o4
	srl	%o5, 16, %g7
	andcc	%g7, 0xff, %g0
	be,pn	%icc, 1f
	 add	%o4, 1, %o4
	srl	%o5, 8, %g7
	andcc	%g7, 0xff, %g0
	be,pn	%icc, 1f
	 add	%o4, 1, %o4
	andcc	%o5, 0xff, %g0
	bne,pt	%icc, 81b
	 sub	%o0, %o1, %g2
	add	%o4, 1, %o4
1:	retl
	 sub	%o4, %o1, %o0
21:	retl
	 mov	1, %o0
22:	retl
	 mov	2, %o0
23:	retl
	 mov	3, %o0
ENDPROC(__strlen_user)
ENDPROC(__strnlen_user)

        .section .fixup,#alloc,#execinstr
        .align  4
30:
        retl
         clr    %o0

	.section __ex_table,"a"
	.align	4

	.word	10b, 30b
	.word	11b, 30b
	.word	12b, 30b
	.word	15b, 30b
	.word	13b, 30b
