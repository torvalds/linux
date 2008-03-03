/* una_asm.S: Kernel unaligned trap assembler helpers.
 *
 * Copyright (C) 1996,2005,2008 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/errno.h>

	.text

retl_efault:
	retl
	 mov	-EFAULT, %o0

	/* int __do_int_store(unsigned long *dst_addr, int size,
	 *                    unsigned long *src_val)
	 *
	 * %o0 = dest_addr
	 * %o1 = size
	 * %o2 = src_val
	 *
	 * Return '0' on success, -EFAULT on failure.
	 */
	.globl	__do_int_store
__do_int_store:
	ld	[%o2], %g1
	cmp	%1, 2
	be	2f
	 cmp	%1, 4
	be	1f
	 srl	%g1, 24, %g2
	srl	%g1, 16, %g7
4:	stb	%g2, [%o0]
	srl	%g1, 8, %g2
5:	stb	%g7, [%o0 + 1]
	ld	[%o2 + 4], %g7
6:	stb	%g2, [%o0 + 2]
	srl	%g7, 24, %g2
7:	stb	%g1, [%o0 + 3]
	srl	%g7, 16, %g1
8:	stb	%g2, [%o0 + 4]
	srl	%g7, 8, %g2
9:	stb	%g1, [%o0 + 5]
10:	stb	%g2, [%o0 + 6]
	b	0f
11:	 stb	%g7, [%o0 + 7]
1:	srl	%g1, 16, %g7
12:	stb	%g2, [%o0]
	srl	%g1, 8, %g2
13:	stb	%g7, [%o0 + 1]
14:	stb	%g2, [%o0 + 2]
	b	0f
15:	 stb	%g1, [%o0 + 3]
2:	srl	%g1, 8, %g2
16:	stb	%g2, [%o0]
17:	stb	%g1, [%o0 + 1]
0:	retl
	 mov	0, %o0

	.section __ex_table,#alloc
	.word	4b, retl_efault
	.word	5b, retl_efault
	.word	6b, retl_efault
	.word	7b, retl_efault
	.word	8b, retl_efault
	.word	9b, retl_efault
	.word	10b, retl_efault
	.word	11b, retl_efault
	.word	12b, retl_efault
	.word	13b, retl_efault
	.word	14b, retl_efault
	.word	15b, retl_efault
	.word	16b, retl_efault
	.word	17b, retl_efault
	.previous

	/* int do_int_load(unsigned long *dest_reg, int size,
	 *                 unsigned long *saddr, int is_signed)
	 *
	 * %o0 = dest_reg
	 * %o1 = size
	 * %o2 = saddr
	 * %o3 = is_signed
	 *
	 * Return '0' on success, -EFAULT on failure.
	 */
	.globl	do_int_load
do_int_load:
	cmp	%o1, 8
	be	9f
	 cmp	%o1, 4
	be	6f
4:	 ldub	[%o2], %g1
5:	ldub	[%o2 + 1], %g2
	sll	%g1, 8, %g1
	tst	%o3
	be	3f
	 or	%g1, %g2, %g1
	sll	%g1, 16, %g1
	sra	%g1, 16, %g1
3:	b	0f
	 st	%g1, [%o0]
6:	ldub	[%o2 + 1], %g2
	sll	%g1, 24, %g1
7:	ldub	[%o2 + 2], %g7
	sll	%g2, 16, %g2
8:	ldub	[%o2 + 3], %g3
	sll	%g7, 8, %g7
	or	%g3, %g2, %g3
	or	%g7, %g3, %g7
	or	%g1, %g7, %g1
	b	0f
	 st	%g1, [%o0]
9:	ldub	[%o2], %g1
10:	ldub	[%o2 + 1], %g2
	sll	%g1, 24, %g1
11:	ldub	[%o2 + 2], %g7
	sll	%g2, 16, %g2
12:	ldub	[%o2 + 3], %g3
	sll	%g7, 8, %g7
	or	%g1, %g2, %g1
	or	%g7, %g3, %g7
	or	%g1, %g7, %g7
13:	ldub	[%o2 + 4], %g1
	st	%g7, [%o0]
14:	ldub	[%o2 + 5], %g2
	sll	%g1, 24, %g1
15:	ldub	[%o2 + 6], %g7
	sll	%g2, 16, %g2
16:	ldub	[%o2 + 7], %g3
	sll	%g7, 8, %g7
	or	%g1, %g2, %g1
	or	%g7, %g3, %g7
	or	%g1, %g7, %g7
	st	%g7, [%o0 + 4]
0:	retl
	 mov	0, %o0

	.section __ex_table,#alloc
	.word	4b, retl_efault
	.word	5b, retl_efault
	.word	6b, retl_efault
	.word	7b, retl_efault
	.word	8b, retl_efault
	.word	9b, retl_efault
	.word	10b, retl_efault
	.word	11b, retl_efault
	.word	12b, retl_efault
	.word	13b, retl_efault
	.word	14b, retl_efault
	.word	15b, retl_efault
	.word	16b, retl_efault
	.previous
