/* checksum.S: Sparc V9 optimized checksum code.
 *
 *  Copyright(C) 1995 Linus Torvalds
 *  Copyright(C) 1995 Miguel de Icaza
 *  Copyright(C) 1996, 2000 David S. Miller
 *  Copyright(C) 1997 Jakub Jelinek
 *
 * derived from:
 *	Linux/Alpha checksum c-code
 *      Linux/ix86 inline checksum assembly
 *      RFC1071 Computing the Internet Checksum (esp. Jacobsons m68k code)
 *	David Mosberger-Tang for optimized reference c-code
 *	BSD4.4 portable checksum routine
 */

#include <asm/export.h>
	.text

csum_partial_fix_alignment:
	/* We checked for zero length already, so there must be
	 * at least one byte.
	 */
	be,pt		%icc, 1f
	 nop
	ldub		[%o0 + 0x00], %o4
	add		%o0, 1, %o0
	sub		%o1, 1, %o1
1:	andcc		%o0, 0x2, %g0
	be,pn		%icc, csum_partial_post_align
	 cmp		%o1, 2
	blu,pn		%icc, csum_partial_end_cruft
	 nop
	lduh		[%o0 + 0x00], %o5
	add		%o0, 2, %o0
	sub		%o1, 2, %o1
	ba,pt		%xcc, csum_partial_post_align
	 add		%o5, %o4, %o4

	.align		32
	.globl		csum_partial
	.type		csum_partial,#function
	EXPORT_SYMBOL(csum_partial)
csum_partial:		/* %o0=buff, %o1=len, %o2=sum */
	prefetch	[%o0 + 0x000], #n_reads
	clr		%o4
	prefetch	[%o0 + 0x040], #n_reads
	brz,pn		%o1, csum_partial_finish
	 andcc		%o0, 0x3, %g0

	/* We "remember" whether the lowest bit in the address
	 * was set in %g7.  Because if it is, we have to swap
	 * upper and lower 8 bit fields of the sum we calculate.
	*/
	bne,pn		%icc, csum_partial_fix_alignment
	 andcc		%o0, 0x1, %g7

csum_partial_post_align:
	prefetch	[%o0 + 0x080], #n_reads
	andncc		%o1, 0x3f, %o3

	prefetch	[%o0 + 0x0c0], #n_reads
	sub		%o1, %o3, %o1
	brz,pn		%o3, 2f
	 prefetch	[%o0 + 0x100], #n_reads

	/* So that we don't need to use the non-pairing
	 * add-with-carry instructions we accumulate 32-bit
	 * values into a 64-bit register.  At the end of the
	 * loop we fold it down to 32-bits and so on.
	 */
	prefetch	[%o0 + 0x140], #n_reads
1:	lduw		[%o0 + 0x00], %o5
	lduw		[%o0 + 0x04], %g1
	lduw		[%o0 + 0x08], %g2
	add		%o4, %o5, %o4
	lduw		[%o0 + 0x0c], %g3
	add		%o4, %g1, %o4
	lduw		[%o0 + 0x10], %o5
	add		%o4, %g2, %o4
	lduw		[%o0 + 0x14], %g1
	add		%o4, %g3, %o4
	lduw		[%o0 + 0x18], %g2
	add		%o4, %o5, %o4
	lduw		[%o0 + 0x1c], %g3
	add		%o4, %g1, %o4
	lduw		[%o0 + 0x20], %o5
	add		%o4, %g2, %o4
	lduw		[%o0 + 0x24], %g1
	add		%o4, %g3, %o4
	lduw		[%o0 + 0x28], %g2
	add		%o4, %o5, %o4
	lduw		[%o0 + 0x2c], %g3
	add		%o4, %g1, %o4
	lduw		[%o0 + 0x30], %o5
	add		%o4, %g2, %o4
	lduw		[%o0 + 0x34], %g1
	add		%o4, %g3, %o4
	lduw		[%o0 + 0x38], %g2
	add		%o4, %o5, %o4
	lduw		[%o0 + 0x3c], %g3
	add		%o4, %g1, %o4
	prefetch	[%o0 + 0x180], #n_reads
	add		%o4, %g2, %o4
	subcc		%o3, 0x40, %o3
	add		%o0, 0x40, %o0
	bne,pt		%icc, 1b
	 add		%o4, %g3, %o4

2:	and		%o1, 0x3c, %o3
	brz,pn		%o3, 2f
	 sub		%o1, %o3, %o1
1:	lduw		[%o0 + 0x00], %o5
	subcc		%o3, 0x4, %o3
	add		%o0, 0x4, %o0
	bne,pt		%icc, 1b
	 add		%o4, %o5, %o4

2:
	/* fold 64-->32 */
	srlx		%o4, 32, %o5
	srl		%o4, 0, %o4
	add		%o4, %o5, %o4
	srlx		%o4, 32, %o5
	srl		%o4, 0, %o4
	add		%o4, %o5, %o4

	/* fold 32-->16 */
	sethi		%hi(0xffff0000), %g1
	srl		%o4, 16, %o5
	andn		%o4, %g1, %g2
	add		%o5, %g2, %o4
	srl		%o4, 16, %o5
	andn		%o4, %g1, %g2
	add		%o5, %g2, %o4

csum_partial_end_cruft:
	/* %o4 has the 16-bit sum we have calculated so-far.  */
	cmp		%o1, 2
	blu,pt		%icc, 1f
	 nop
	lduh		[%o0 + 0x00], %o5
	sub		%o1, 2, %o1
	add		%o0, 2, %o0
	add		%o4, %o5, %o4
1:	brz,pt		%o1, 1f
	 nop
	ldub		[%o0 + 0x00], %o5
	sub		%o1, 1, %o1
	add		%o0, 1, %o0
	sllx		%o5, 8, %o5
	add		%o4, %o5, %o4
1:
	/* fold 32-->16 */
	sethi		%hi(0xffff0000), %g1
	srl		%o4, 16, %o5
	andn		%o4, %g1, %g2
	add		%o5, %g2, %o4
	srl		%o4, 16, %o5
	andn		%o4, %g1, %g2
	add		%o5, %g2, %o4

1:	brz,pt		%g7, 1f
	 nop

	/* We started with an odd byte, byte-swap the result.  */
	srl		%o4, 8, %o5
	and		%o4, 0xff, %g1
	sll		%g1, 8, %g1
	or		%o5, %g1, %o4

1:	addcc		%o2, %o4, %o2
	addc		%g0, %o2, %o2

csum_partial_finish:
	retl
	 srl		%o2, 0, %o0
