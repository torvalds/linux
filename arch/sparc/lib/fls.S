/* fls.S: SPARC default fls definition.
 *
 * SPARC default fls definition, which follows the same algorithm as
 * in generic fls(). This function will be boot time patched on T4
 * and onward.
 */

#include <linux/export.h>
#include <linux/linkage.h>

	.text
	.register	%g2, #scratch
	.register	%g3, #scratch
ENTRY(fls)
	brz,pn	%o0, 6f
	 mov	0, %o1
	sethi	%hi(0xffff0000), %g3
	mov	%o0, %g2
	andcc	%o0, %g3, %g0
	be,pt	%icc, 8f
	 mov	32, %o1
	sethi	%hi(0xff000000), %g3
	andcc	%g2, %g3, %g0
	bne,pt	%icc, 3f
	 sethi	%hi(0xf0000000), %g3
	sll	%o0, 8, %o0
1:
	add	%o1, -8, %o1
	sra	%o0, 0, %o0
	mov	%o0, %g2
2:
	sethi	%hi(0xf0000000), %g3
3:
	andcc	%g2, %g3, %g0
	bne,pt	%icc, 4f
	 sethi	%hi(0xc0000000), %g3
	sll	%o0, 4, %o0
	add	%o1, -4, %o1
	sra	%o0, 0, %o0
	mov	%o0, %g2
4:
	andcc	%g2, %g3, %g0
	be,a,pt	%icc, 7f
	 sll	%o0, 2, %o0
5:
	xnor	%g0, %o0, %o0
	srl	%o0, 31, %o0
	sub	%o1, %o0, %o1
6:
	jmp	%o7 + 8
	 sra	%o1, 0, %o0
7:
	add	%o1, -2, %o1
	ba,pt	%xcc, 5b
	 sra	%o0, 0, %o0
8:
	sll	%o0, 16, %o0
	sethi	%hi(0xff000000), %g3
	sra	%o0, 0, %o0
	mov	%o0, %g2
	andcc	%g2, %g3, %g0
	bne,pt	%icc, 2b
	 mov	16, %o1
	ba,pt	%xcc, 1b
	 sll	%o0, 8, %o0
ENDPROC(fls)
EXPORT_SYMBOL(fls)
