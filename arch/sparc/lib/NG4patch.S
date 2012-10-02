/* NG4patch.S: Patch Ultra-I routines with Niagara-4 variant.
 *
 * Copyright (C) 2012 David S. Miller <davem@davemloft.net>
 */

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

	.globl	niagara4_patch_copyops
	.type	niagara4_patch_copyops,#function
niagara4_patch_copyops:
	NG_DO_PATCH(memcpy, NG4memcpy)
	NG_DO_PATCH(___copy_from_user, NG4copy_from_user)
	NG_DO_PATCH(___copy_to_user, NG4copy_to_user)
	retl
	 nop
	.size	niagara4_patch_copyops,.-niagara4_patch_copyops

	.globl	niagara4_patch_pageops
	.type	niagara4_patch_pageops,#function
niagara4_patch_pageops:
	NG_DO_PATCH(copy_user_page, NG4copy_user_page)
	NG_DO_PATCH(_clear_page, NGclear_page)
	NG_DO_PATCH(clear_user_page, NGclear_user_page)
	retl
	 nop
	.size	niagara4_patch_pageops,.-niagara4_patch_pageops
