/* una_asm.S: Kernel unaligned trap assembler helpers.
 *
 * Copyright (C) 1996,2005 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

	.text

	.globl	__do_int_store
__do_int_store:
	rd	%asi, %o4
	wr	%o3, 0, %asi
	mov	%o2, %g3
	cmp	%o1, 2
	be,pn	%icc, 2f
	 cmp	%o1, 4
	be,pt	%icc, 1f
	 srlx	%g3, 24, %g2
	srlx	%g3, 56, %g1
	srlx	%g3, 48, %g7
4:	stba	%g1, [%o0] %asi
	srlx	%g3, 40, %g1
5:	stba	%g7, [%o0 + 1] %asi
	srlx	%g3, 32, %g7
6:	stba	%g1, [%o0 + 2] %asi
7:	stba	%g7, [%o0 + 3] %asi
	srlx	%g3, 16, %g1
8:	stba	%g2, [%o0 + 4] %asi
	srlx	%g3, 8, %g7
9:	stba	%g1, [%o0 + 5] %asi
10:	stba	%g7, [%o0 + 6] %asi
	ba,pt	%xcc, 0f
11:	 stba	%g3, [%o0 + 7] %asi
1:	srl	%g3, 16, %g7
12:	stba	%g2, [%o0] %asi
	srl	%g3, 8, %g2
13:	stba	%g7, [%o0 + 1] %asi
14:	stba	%g2, [%o0 + 2] %asi
	ba,pt	%xcc, 0f
15:	 stba	%g3, [%o0 + 3] %asi
2:	srl	%g3, 8, %g2
16:	stba	%g2, [%o0] %asi
17:	stba	%g3, [%o0 + 1] %asi
0:
	wr	%o4, 0x0, %asi
	retl
	 mov	0, %o0
	.size	__do_int_store, .-__do_int_store

	.section	__ex_table,"a"
	.word		4b, __retl_efault
	.word		5b, __retl_efault
	.word		6b, __retl_efault
	.word		7b, __retl_efault
	.word		8b, __retl_efault
	.word		9b, __retl_efault
	.word		10b, __retl_efault
	.word		11b, __retl_efault
	.word		12b, __retl_efault
	.word		13b, __retl_efault
	.word		14b, __retl_efault
	.word		15b, __retl_efault
	.word		16b, __retl_efault
	.word		17b, __retl_efault
	.previous

	.globl	do_int_load
do_int_load:
	rd	%asi, %o5
	wr	%o4, 0, %asi
	cmp	%o1, 8
	bge,pn	%icc, 9f
	 cmp	%o1, 4
	be,pt	%icc, 6f
4:	 lduba	[%o2] %asi, %g2
5:	lduba	[%o2 + 1] %asi, %g3
	sll	%g2, 8, %g2
	brz,pt	%o3, 3f
	 add	%g2, %g3, %g2
	sllx	%g2, 48, %g2
	srax	%g2, 48, %g2
3:	ba,pt	%xcc, 0f
	 stx	%g2, [%o0]
6:	lduba	[%o2 + 1] %asi, %g3
	sll	%g2, 24, %g2
7:	lduba	[%o2 + 2] %asi, %g7
	sll	%g3, 16, %g3
8:	lduba	[%o2 + 3] %asi, %g1
	sll	%g7, 8, %g7
	or	%g2, %g3, %g2
	or	%g7, %g1, %g7
	or	%g2, %g7, %g2
	brnz,a,pt %o3, 3f
	 sra	%g2, 0, %g2
3:	ba,pt	%xcc, 0f
	 stx	%g2, [%o0]
9:	lduba	[%o2] %asi, %g2
10:	lduba	[%o2 + 1] %asi, %g3
	sllx	%g2, 56, %g2
11:	lduba	[%o2 + 2] %asi, %g7
	sllx	%g3, 48, %g3
12:	lduba	[%o2 + 3] %asi, %g1
	sllx	%g7, 40, %g7
	sllx	%g1, 32, %g1
	or	%g2, %g3, %g2
	or	%g7, %g1, %g7
13:	lduba	[%o2 + 4] %asi, %g3
	or	%g2, %g7, %g7
14:	lduba	[%o2 + 5] %asi, %g1
	sllx	%g3, 24, %g3
15:	lduba	[%o2 + 6] %asi, %g2
	sllx	%g1, 16, %g1
	or	%g7, %g3, %g7
16:	lduba	[%o2 + 7] %asi, %g3
	sllx	%g2, 8, %g2
	or	%g7, %g1, %g7
	or	%g2, %g3, %g2
	or	%g7, %g2, %g7
	cmp	%o1, 8
	be,a,pt %icc, 0f
	 stx	%g7, [%o0]
	srlx	%g7, 32, %g2
	sra	%g7, 0, %g7
	stx	%g2, [%o0]
	stx	%g7, [%o0 + 8]
0:
	wr	%o5, 0x0, %asi
	retl
	 mov	0, %o0
	.size	do_int_load, .-do_int_load

	.section	__ex_table,"a"
	.word		4b, __retl_efault
	.word		5b, __retl_efault
	.word		6b, __retl_efault
	.word		7b, __retl_efault
	.word		8b, __retl_efault
	.word		9b, __retl_efault
	.word		10b, __retl_efault
	.word		11b, __retl_efault
	.word		12b, __retl_efault
	.word		13b, __retl_efault
	.word		14b, __retl_efault
	.word		15b, __retl_efault
	.word		16b, __retl_efault
	.previous
