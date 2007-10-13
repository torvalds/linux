/* GENpage.S: Generic clear and copy page.
 *
 * Copyright (C) 2007 (davem@davemloft.net)
 */
#include <asm/page.h>

	.text
	.align	32

GENcopy_user_page:
	set	PAGE_SIZE, %g7
1:	ldx	[%o1 + 0x00], %o2
	ldx	[%o1 + 0x08], %o3
	ldx	[%o1 + 0x10], %o4
	ldx	[%o1 + 0x18], %o5
	stx	%o2, [%o0 + 0x00]
	stx	%o3, [%o0 + 0x08]
	stx	%o4, [%o0 + 0x10]
	stx	%o5, [%o0 + 0x18]
	ldx	[%o1 + 0x20], %o2
	ldx	[%o1 + 0x28], %o3
	ldx	[%o1 + 0x30], %o4
	ldx	[%o1 + 0x38], %o5
	stx	%o2, [%o0 + 0x20]
	stx	%o3, [%o0 + 0x28]
	stx	%o4, [%o0 + 0x30]
	stx	%o5, [%o0 + 0x38]
	subcc	%g7, 64, %g7
	add	%o1, 64, %o1
	bne,pt	%xcc, 1b
	 add	%o0, 64, %o0
	retl
	 nop

GENclear_page:
GENclear_user_page:
	set	PAGE_SIZE, %g7
1:	stx	%g0, [%o0 + 0x00]
	stx	%g0, [%o0 + 0x08]
	stx	%g0, [%o0 + 0x10]
	stx	%g0, [%o0 + 0x18]
	stx	%g0, [%o0 + 0x20]
	stx	%g0, [%o0 + 0x28]
	stx	%g0, [%o0 + 0x30]
	stx	%g0, [%o0 + 0x38]
	subcc	%g7, 64, %g7
	bne,pt	%xcc, 1b
	 add	%o0, 64, %o0

#define BRANCH_ALWAYS	0x10680000
#define NOP		0x01000000
#define GEN_DO_PATCH(OLD, NEW)	\
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

	.globl	generic_patch_pageops
	.type	generic_patch_pageops,#function
generic_patch_pageops:
	GEN_DO_PATCH(copy_user_page, GENcopy_user_page)
	GEN_DO_PATCH(_clear_page, GENclear_page)
	GEN_DO_PATCH(clear_user_page, GENclear_user_page)
	retl
	 nop
	.size	generic_patch_pageops,.-generic_patch_pageops
