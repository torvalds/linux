/* SPDX-License-Identifier: GPL-2.0 */
/* NGpage.S: Niagara optimize clear and copy page.
 *
 * Copyright (C) 2006 (davem@davemloft.net)
 */

#include <asm/asi.h>
#include <asm/page.h>

	.text
	.align	32

	/* This is heavily simplified from the sun4u variants
	 * because Niagara does not have any D-cache aliasing issues
	 * and also we don't need to use the FPU in order to implement
	 * an optimal page copy/clear.
	 */

NGcopy_user_page:	/* %o0=dest, %o1=src, %o2=vaddr */
	save		%sp, -192, %sp
	rd		%asi, %g3
	wr		%g0, ASI_BLK_INIT_QUAD_LDD_P, %asi
	set		PAGE_SIZE, %g7
	prefetch	[%i1 + 0x00], #one_read
	prefetch	[%i1 + 0x40], #one_read

1:	prefetch	[%i1 + 0x80], #one_read
	prefetch	[%i1 + 0xc0], #one_read
	ldda		[%i1 + 0x00] %asi, %o2
	ldda		[%i1 + 0x10] %asi, %o4
	ldda		[%i1 + 0x20] %asi, %l2
	ldda		[%i1 + 0x30] %asi, %l4
	stxa		%o2, [%i0 + 0x00] %asi
	stxa		%o3, [%i0 + 0x08] %asi
	stxa		%o4, [%i0 + 0x10] %asi
	stxa		%o5, [%i0 + 0x18] %asi
	stxa		%l2, [%i0 + 0x20] %asi
	stxa		%l3, [%i0 + 0x28] %asi
	stxa		%l4, [%i0 + 0x30] %asi
	stxa		%l5, [%i0 + 0x38] %asi
	ldda		[%i1 + 0x40] %asi, %o2
	ldda		[%i1 + 0x50] %asi, %o4
	ldda		[%i1 + 0x60] %asi, %l2
	ldda		[%i1 + 0x70] %asi, %l4
	stxa		%o2, [%i0 + 0x40] %asi
	stxa		%o3, [%i0 + 0x48] %asi
	stxa		%o4, [%i0 + 0x50] %asi
	stxa		%o5, [%i0 + 0x58] %asi
	stxa		%l2, [%i0 + 0x60] %asi
	stxa		%l3, [%i0 + 0x68] %asi
	stxa		%l4, [%i0 + 0x70] %asi
	stxa		%l5, [%i0 + 0x78] %asi
	add		%i1, 128, %i1
	subcc		%g7, 128, %g7
	bne,pt		%xcc, 1b
	 add		%i0, 128, %i0
	wr		%g3, 0x0, %asi
	membar		#Sync
	ret
	 restore

	.align		32
	.globl		NGclear_page
	.globl		NGclear_user_page
NGclear_page:		/* %o0=dest */
NGclear_user_page:	/* %o0=dest, %o1=vaddr */
	rd		%asi, %g3
	wr		%g0, ASI_BLK_INIT_QUAD_LDD_P, %asi
	set		PAGE_SIZE, %g7

1:	stxa		%g0, [%o0 + 0x00] %asi
	stxa		%g0, [%o0 + 0x08] %asi
	stxa		%g0, [%o0 + 0x10] %asi
	stxa		%g0, [%o0 + 0x18] %asi
	stxa		%g0, [%o0 + 0x20] %asi
	stxa		%g0, [%o0 + 0x28] %asi
	stxa		%g0, [%o0 + 0x30] %asi
	stxa		%g0, [%o0 + 0x38] %asi
	stxa		%g0, [%o0 + 0x40] %asi
	stxa		%g0, [%o0 + 0x48] %asi
	stxa		%g0, [%o0 + 0x50] %asi
	stxa		%g0, [%o0 + 0x58] %asi
	stxa		%g0, [%o0 + 0x60] %asi
	stxa		%g0, [%o0 + 0x68] %asi
	stxa		%g0, [%o0 + 0x70] %asi
	stxa		%g0, [%o0 + 0x78] %asi
	stxa		%g0, [%o0 + 0x80] %asi
	stxa		%g0, [%o0 + 0x88] %asi
	stxa		%g0, [%o0 + 0x90] %asi
	stxa		%g0, [%o0 + 0x98] %asi
	stxa		%g0, [%o0 + 0xa0] %asi
	stxa		%g0, [%o0 + 0xa8] %asi
	stxa		%g0, [%o0 + 0xb0] %asi
	stxa		%g0, [%o0 + 0xb8] %asi
	stxa		%g0, [%o0 + 0xc0] %asi
	stxa		%g0, [%o0 + 0xc8] %asi
	stxa		%g0, [%o0 + 0xd0] %asi
	stxa		%g0, [%o0 + 0xd8] %asi
	stxa		%g0, [%o0 + 0xe0] %asi
	stxa		%g0, [%o0 + 0xe8] %asi
	stxa		%g0, [%o0 + 0xf0] %asi
	stxa		%g0, [%o0 + 0xf8] %asi
	subcc		%g7, 256, %g7
	bne,pt		%xcc, 1b
	 add		%o0, 256, %o0
	wr		%g3, 0x0, %asi
	membar		#Sync
	retl
	 nop

#define BRANCH_ALWAYS	0x10680000
#define NOP		0x01000000
#define NG_DO_PATCH(OLD, NEW)	\
	sethi	%hi(NEW), %g1; \
	or	%g1, %lo(NEW), %g1; \
	sethi	%hi(OLD), %g2; \
	or	%g2, %lo(OLD), %g2; \
	sub	%g1, %g2, %g1; \
	sethi	%hi(BRANCH_ALWAYS), %g3; \
	sll	%g1, 11, %g1; \
	srl	%g1, 11 + 2, %g1; \
	or	%g3, %lo(BRANCH_ALWAYS), %g3; \
	or	%g3, %g1, %g3; \
	stw	%g3, [%g2]; \
	sethi	%hi(NOP), %g3; \
	or	%g3, %lo(NOP), %g3; \
	stw	%g3, [%g2 + 0x4]; \
	flush	%g2;

	.globl	niagara_patch_pageops
	.type	niagara_patch_pageops,#function
niagara_patch_pageops:
	NG_DO_PATCH(copy_user_page, NGcopy_user_page)
	NG_DO_PATCH(_clear_page, NGclear_page)
	NG_DO_PATCH(clear_user_page, NGclear_user_page)
	retl
	 nop
	.size	niagara_patch_pageops,.-niagara_patch_pageops
