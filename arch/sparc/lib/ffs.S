#include <linux/linkage.h>

	.register	%g2,#scratch

	.text
	.align	32

ENTRY(ffs)
	brnz,pt	%o0, 1f
	 mov	1, %o1
	retl
	 clr	%o0
	nop
	nop
ENTRY(__ffs)
	sllx	%o0, 32, %g1		/* 1  */
	srlx	%o0, 32, %g2

	clr	%o1			/* 2  */
	movrz	%g1, %g2, %o0

	movrz	%g1, 32, %o1		/* 3  */
1:	clr	%o2

	sllx	%o0, (64 - 16), %g1	/* 4  */
	srlx	%o0, 16, %g2

	movrz	%g1, %g2, %o0		/* 5  */
	clr	%o3

	movrz	%g1, 16, %o2		/* 6  */
	clr	%o4

	and	%o0, 0xff, %g1		/* 7  */
	srlx	%o0, 8, %g2

	movrz	%g1, %g2, %o0		/* 8  */
	clr	%o5

	movrz	%g1, 8, %o3		/* 9  */
	add	%o2, %o1, %o2

	and	%o0, 0xf, %g1		/* 10 */
	srlx	%o0, 4, %g2

	movrz	%g1, %g2, %o0		/* 11 */
	add	%o2, %o3, %o2

	movrz	%g1, 4, %o4		/* 12 */

	and	%o0, 0x3, %g1		/* 13 */
	srlx	%o0, 2, %g2

	movrz	%g1, %g2, %o0		/* 14 */
	add	%o2, %o4, %o2

	movrz	%g1, 2, %o5		/* 15 */

	and	%o0, 0x1, %g1		/* 16 */

	add	%o2, %o5, %o2		/* 17 */
	xor	%g1, 0x1, %g1

	retl				/* 18 */
	 add	%o2, %g1, %o0
ENDPROC(ffs)
ENDPROC(__ffs)

	.section	.popc_6insn_patch, "ax"
	.word		ffs
	brz,pn	%o0, 98f
	 neg	%o0, %g1
	xnor	%o0, %g1, %o1
	popc	%o1, %o0
98:	retl
	 nop
	.word		__ffs
	neg	%o0, %g1
	xnor	%o0, %g1, %o1
	popc	%o1, %o0
	retl
	 sub	%o0, 1, %o0
	nop
	.previous
