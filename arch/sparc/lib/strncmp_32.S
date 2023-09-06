/* SPDX-License-Identifier: GPL-2.0 */
/*
 * strncmp.S: Hand optimized Sparc assembly of GCC output from GNU libc
 *            generic strncmp routine.
 */

#include <linux/export.h>
#include <linux/linkage.h>

	.text
ENTRY(strncmp)
	mov	%o0, %g3
	mov	0, %o3

	cmp	%o2, 3
	ble	7f
	 mov	0, %g2

	sra	%o2, 2, %o4
	ldub	[%g3], %o3

0:
	ldub	[%o1], %g2
	add	%g3, 1, %g3
	and	%o3, 0xff, %o0

	cmp	%o0, 0
	be	8f
	 add	%o1, 1, %o1

	cmp	%o0, %g2
	be,a	1f
	 ldub	[%g3], %o3

	retl
	 sub	%o0, %g2, %o0

1:
	ldub	[%o1], %g2
	add	%g3,1, %g3
	and	%o3, 0xff, %o0

	cmp	%o0, 0
	be	8f
	 add	%o1, 1, %o1

	cmp	%o0, %g2
	be,a	1f
	 ldub	[%g3], %o3

	retl
	 sub	%o0, %g2, %o0

1:
	ldub	[%o1], %g2
	add	%g3, 1, %g3
	and	%o3, 0xff, %o0

	cmp	%o0, 0
	be	8f
	 add	%o1, 1, %o1

	cmp	%o0, %g2
	be,a	1f
	 ldub	[%g3], %o3

	retl
	 sub	%o0, %g2, %o0

1:
	ldub	[%o1], %g2
	add	%g3, 1, %g3
	and	%o3, 0xff, %o0

	cmp	%o0, 0
	be	8f
	 add	%o1, 1, %o1

	cmp	%o0, %g2
	be	1f
	 add	%o4, -1, %o4

	retl
	 sub	%o0, %g2, %o0

1:

	cmp	%o4, 0
	bg,a	0b
	 ldub	[%g3], %o3

	b	7f
	 and	%o2, 3, %o2

9:
	ldub	[%o1], %g2
	add	%g3, 1, %g3
	and	%o3, 0xff, %o0

	cmp	%o0, 0
	be	8f
	 add	%o1, 1, %o1

	cmp	%o0, %g2
	be	7f
	 add	%o2, -1, %o2

8:
	retl
	 sub	%o0, %g2, %o0

7:
	cmp	%o2, 0
	bg,a	9b
	 ldub	[%g3], %o3

	and	%g2, 0xff, %o0
	retl
	 sub	%o3, %o0, %o0
ENDPROC(strncmp)
EXPORT_SYMBOL(strncmp)
