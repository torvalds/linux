#include <linux/linkage.h>
#include <asm/export.h>

	.text
	.align	4
ENTRY(__multi3) /* %o0 = u, %o1 = v */
	mov	%o1, %g1
	srl	%o3, 0, %o4
	mulx	%o4, %g1, %o1
	srlx	%g1, 0x20, %g3
	mulx	%g3, %o4, %g7
	sllx	%g7, 0x20, %o5
	srl	%g1, 0, %o4
	sub	%o1, %o5, %o5
	srlx	%o5, 0x20, %o5
	addcc	%g7, %o5, %g7
	srlx	%o3, 0x20, %o5
	mulx	%o4, %o5, %o4
	mulx	%g3, %o5, %o5
	sethi	%hi(0x80000000), %g3
	addcc	%g7, %o4, %g7
	srlx	%g7, 0x20, %g7
	add	%g3, %g3, %g3
	movcc	%xcc, %g0, %g3
	addcc	%o5, %g7, %o5
	sllx	%o4, 0x20, %o4
	add	%o1, %o4, %o1
	add	%o5, %g3, %g2
	mulx	%g1, %o2, %g1
	add	%g1, %g2, %g1
	mulx	%o0, %o3, %o0
	retl
	 add	%g1, %o0, %o0
ENDPROC(__multi3)
EXPORT_SYMBOL(__multi3)
