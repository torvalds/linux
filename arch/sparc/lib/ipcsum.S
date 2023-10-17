/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/export.h>
#include <linux/linkage.h>

	.text
ENTRY(ip_fast_csum) /* %o0 = iph, %o1 = ihl */
	sub	%o1, 4, %g7
	lduw	[%o0 + 0x00], %o2
	lduw	[%o0 + 0x04], %g2
	lduw	[%o0 + 0x08], %g3
	addcc	%g2, %o2, %o2
	lduw	[%o0 + 0x0c], %g2
	addccc	%g3, %o2, %o2
	lduw	[%o0 + 0x10], %g3

	addccc	%g2, %o2, %o2
	addc	%o2, %g0, %o2
1:	addcc	%g3, %o2, %o2
	add	%o0, 4, %o0
	addccc	%o2, %g0, %o2
	subcc	%g7, 1, %g7
	be,a,pt	%icc, 2f
	 sll	%o2, 16, %g2

	lduw	[%o0 + 0x10], %g3
	ba,pt	%xcc, 1b
	 nop
2:	addcc	%o2, %g2, %g2
	srl	%g2, 16, %o2
	addc	%o2, %g0, %o2
	xnor	%g0, %o2, %o2
	set	0xffff, %o1
	retl
	 and	%o2, %o1, %o0
ENDPROC(ip_fast_csum)
EXPORT_SYMBOL(ip_fast_csum)
