/* NG4memset.S: Niagara-4 optimized memset/bzero.
 *
 * Copyright (C) 2012 David S. Miller (davem@davemloft.net)
 */

#include <asm/asi.h>

	.register	%g2, #scratch
	.register	%g3, #scratch

	.text
	.align		32
	.globl		NG4memset
NG4memset:
	andcc		%o1, 0xff, %o4
	be,pt		%icc, 1f
	 mov		%o2, %o1
	sllx		%o4, 8, %g1
	or		%g1, %o4, %o2
	sllx		%o2, 16, %g1
	or		%g1, %o2, %o2
	sllx		%o2, 32, %g1
	ba,pt		%icc, 1f
	 or		%g1, %o2, %o4
	.size		NG4memset,.-NG4memset

	.align		32
	.globl		NG4bzero
NG4bzero:
	clr		%o4
1:	cmp		%o1, 16
	ble		%icc, .Ltiny
	 mov		%o0, %o3
	sub		%g0, %o0, %g1
	and		%g1, 0x7, %g1
	brz,pt		%g1, .Laligned8
	 sub		%o1, %g1, %o1
1:	stb		%o4, [%o0 + 0x00]
	subcc		%g1, 1, %g1
	bne,pt		%icc, 1b
	 add		%o0, 1, %o0
.Laligned8:
	cmp		%o1, 64 + (64 - 8)
	ble		.Lmedium
	 sub		%g0, %o0, %g1
	andcc		%g1, (64 - 1), %g1
	brz,pn		%g1, .Laligned64
	 sub		%o1, %g1, %o1
1:	stx		%o4, [%o0 + 0x00]
	subcc		%g1, 8, %g1
	bne,pt		%icc, 1b
	 add		%o0, 0x8, %o0
.Laligned64:
	andn		%o1, 64 - 1, %g1
	sub		%o1, %g1, %o1
	brnz,pn		%o4, .Lnon_bzero_loop
	 mov		0x20, %g2
1:	stxa		%o4, [%o0 + %g0] ASI_BLK_INIT_QUAD_LDD_P
	subcc		%g1, 0x40, %g1
	stxa		%o4, [%o0 + %g2] ASI_BLK_INIT_QUAD_LDD_P
	bne,pt		%icc, 1b
	 add		%o0, 0x40, %o0
.Lpostloop:
	cmp		%o1, 8
	bl,pn		%icc, .Ltiny
	 membar		#StoreStore|#StoreLoad
.Lmedium:
	andn		%o1, 0x7, %g1
	sub		%o1, %g1, %o1
1:	stx		%o4, [%o0 + 0x00]
	subcc		%g1, 0x8, %g1
	bne,pt		%icc, 1b
	 add		%o0, 0x08, %o0
	andcc		%o1, 0x4, %g1
	be,pt		%icc, .Ltiny
	 sub		%o1, %g1, %o1
	stw		%o4, [%o0 + 0x00]
	add		%o0, 0x4, %o0
.Ltiny:
	cmp		%o1, 0
	be,pn		%icc, .Lexit
1:	 subcc		%o1, 1, %o1
	stb		%o4, [%o0 + 0x00]
	bne,pt		%icc, 1b
	 add		%o0, 1, %o0
.Lexit:
	retl
	 mov		%o3, %o0
.Lnon_bzero_loop:
	mov		0x08, %g3
	mov		0x28, %o5
1:	stxa		%o4, [%o0 + %g0] ASI_BLK_INIT_QUAD_LDD_P
	subcc		%g1, 0x40, %g1
	stxa		%o4, [%o0 + %g2] ASI_BLK_INIT_QUAD_LDD_P
	stxa		%o4, [%o0 + %g3] ASI_BLK_INIT_QUAD_LDD_P
	stxa		%o4, [%o0 + %o5] ASI_BLK_INIT_QUAD_LDD_P
	add		%o0, 0x10, %o0
	stxa		%o4, [%o0 + %g0] ASI_BLK_INIT_QUAD_LDD_P
	stxa		%o4, [%o0 + %g2] ASI_BLK_INIT_QUAD_LDD_P
	stxa		%o4, [%o0 + %g3] ASI_BLK_INIT_QUAD_LDD_P
	stxa		%o4, [%o0 + %o5] ASI_BLK_INIT_QUAD_LDD_P
	bne,pt		%icc, 1b
	 add		%o0, 0x30, %o0
	ba,a,pt		%icc, .Lpostloop
	 nop
	.size		NG4bzero,.-NG4bzero
