/* fls64.S: SPARC default __fls definition.
 *
 * SPARC default __fls definition, which follows the same algorithm as
 * in generic __fls(). This function will be boot time patched on T4
 * and onward.
 */

#include <linux/export.h>
#include <linux/linkage.h>

	.text
	.register	%g2, #scratch
	.register	%g3, #scratch
ENTRY(__fls)
	mov	-1, %g2
	sllx	%g2, 32, %g2
	and	%o0, %g2, %g2
	brnz,pt	%g2, 1f
	 mov	63, %g1
	sllx	%o0, 32, %o0
	mov	31, %g1
1:
	mov	-1, %g2
	sllx	%g2, 48, %g2
	and	%o0, %g2, %g2
	brnz,pt	%g2, 2f
	 mov	-1, %g2
	sllx	%o0, 16, %o0
	add	%g1, -16, %g1
2:
	mov	-1, %g2
	sllx	%g2, 56, %g2
	and	%o0, %g2, %g2
	brnz,pt	%g2, 3f
	 mov	-1, %g2
	sllx	%o0, 8, %o0
	add	%g1, -8, %g1
3:
	sllx	%g2, 60, %g2
	and	%o0, %g2, %g2
	brnz,pt	%g2, 4f
	 mov	-1, %g2
	sllx	%o0, 4, %o0
	add	%g1, -4, %g1
4:
	sllx	%g2, 62, %g2
	and	%o0, %g2, %g2
	brnz,pt	%g2, 5f
	 mov	-1, %g3
	sllx	%o0, 2, %o0
	add	%g1, -2, %g1
5:
	mov	0, %g2
	sllx	%g3, 63, %g3
	and	%o0, %g3, %o0
	movre	%o0, 1, %g2
	sub	%g1, %g2, %g1
	jmp	%o7+8
	 sra	%g1, 0, %o0
ENDPROC(__fls)
EXPORT_SYMBOL(__fls)
