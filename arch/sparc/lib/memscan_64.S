/*
 * memscan.S: Optimized memscan for Sparc64.
 *
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1998 David S. Miller (davem@redhat.com)
 */

	#include <asm/export.h>

#define HI_MAGIC	0x8080808080808080
#define LO_MAGIC	0x0101010101010101
#define ASI_PL		0x88

	.text
	.align	32
	.globl		__memscan_zero, __memscan_generic
	.globl		memscan
	EXPORT_SYMBOL(__memscan_zero)
	EXPORT_SYMBOL(__memscan_generic)

__memscan_zero:
	/* %o0 = bufp, %o1 = size */
	brlez,pn	%o1, szzero
	 andcc		%o0, 7, %g0
	be,pt		%icc, we_are_aligned
	 sethi		%hi(HI_MAGIC), %o4
	ldub		[%o0], %o5
1:	subcc		%o1, 1, %o1
	brz,pn		%o5, 10f
	 add		%o0, 1, %o0

	be,pn		%xcc, szzero
	 andcc		%o0, 7, %g0
	bne,a,pn	%icc, 1b
	 ldub		[%o0], %o5
we_are_aligned:
	ldxa		[%o0] ASI_PL, %o5
	or		%o4, %lo(HI_MAGIC), %o3
	sllx		%o3, 32, %o4
	or		%o4, %o3, %o3

	srlx		%o3, 7, %o2
msloop:
	sub		%o1, 8, %o1
	add		%o0, 8, %o0
	sub		%o5, %o2, %o4
	xor		%o4, %o5, %o4
	andcc		%o4, %o3, %g3
	bne,pn		%xcc, check_bytes
	 srlx		%o4, 32, %g3

	brgz,a,pt	%o1, msloop
	 ldxa		[%o0] ASI_PL, %o5
check_bytes:
	bne,a,pn	%icc, 2f
	 andcc		%o5, 0xff, %g0
	add		%o0, -5, %g2
	ba,pt		%xcc, 3f
	 srlx		%o5, 32, %g7

2:	srlx		%o5, 8, %g7
	be,pn		%icc, 1f
	 add		%o0, -8, %g2
	andcc		%g7, 0xff, %g0
	srlx		%g7, 8, %g7
	be,pn		%icc, 1f
	 inc		%g2
	andcc		%g7, 0xff, %g0

	srlx		%g7, 8, %g7
	be,pn		%icc, 1f
	 inc		%g2
	andcc		%g7, 0xff, %g0
	srlx		%g7, 8, %g7
	be,pn		%icc, 1f
	 inc		%g2
	andcc		%g3, %o3, %g0

	be,a,pn		%icc, 2f
	 mov		%o0, %g2
3:	andcc		%g7, 0xff, %g0
	srlx		%g7, 8, %g7
	be,pn		%icc, 1f
	 inc		%g2
	andcc		%g7, 0xff, %g0
	srlx		%g7, 8, %g7

	be,pn		%icc, 1f
	 inc		%g2
	andcc		%g7, 0xff, %g0
	srlx		%g7, 8, %g7
	be,pn		%icc, 1f
	 inc		%g2
	andcc		%g7, 0xff, %g0
	srlx		%g7, 8, %g7

	be,pn		%icc, 1f
	 inc		%g2
2:	brgz,a,pt	%o1, msloop
	 ldxa		[%o0] ASI_PL, %o5
	inc		%g2
1:	add		%o0, %o1, %o0
	cmp		%g2, %o0
	retl

	 movle		%xcc, %g2, %o0
10:	retl
	 sub		%o0, 1, %o0
szzero:	retl
	 nop

memscan:
__memscan_generic:
	/* %o0 = addr, %o1 = c, %o2 = size */
	brz,pn		%o2, 3f
	 add		%o0, %o2, %o3
	ldub		[%o0], %o5
	sub		%g0, %o2, %o4
1:
	cmp		%o5, %o1
	be,pn		%icc, 2f
	 addcc		%o4, 1, %o4
	bne,a,pt 	%xcc, 1b
	 ldub		[%o3 + %o4], %o5
	retl
	/* The delay slot is the same as the next insn, this is just to make it look more awful */
2:
	 add		%o3, %o4, %o0
	retl
	 sub		%o0, 1, %o0
3:
	retl
	 nop
