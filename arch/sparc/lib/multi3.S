#include <linux/linkage.h>
#include <asm/export.h>

	.text
	.align	4
ENTRY(__multi3) /* %o0 = u, %o1 = v */
	mov	%o1, %g1
	srl	%o3, 0, %g4
	mulx	%g4, %g1, %o1
	srlx	%g1, 0x20, %g3
	mulx	%g3, %g4, %g5
	sllx	%g5, 0x20, %o5
	srl	%g1, 0, %g4
	sub	%o1, %o5, %o5
	srlx	%o5, 0x20, %o5
	addcc	%g5, %o5, %g5
	srlx	%o3, 0x20, %o5
	mulx	%g4, %o5, %g4
	mulx	%g3, %o5, %o5
	sethi	%hi(0x80000000), %g3
	addcc	%g5, %g4, %g5
	srlx	%g5, 0x20, %g5
	add	%g3, %g3, %g3
	movcc	%xcc, %g0, %g3
	addcc	%o5, %g5, %o5
	sllx	%g4, 0x20, %g4
	add	%o1, %g4, %o1
	add	%o5, %g3, %g2
	mulx	%g1, %o2, %g1
	add	%g1, %g2, %g1
	mulx	%o0, %o3, %o0
	retl
	 add	%g1, %o0, %o0
ENDPROC(__multi3)
EXPORT_SYMBOL(__multi3)
