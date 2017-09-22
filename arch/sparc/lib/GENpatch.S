/* GENpatch.S: Patch Ultra-I routines with generic variant.
 *
 * Copyright (C) 2007 David S. Miller <davem@davemloft.net>
 */

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

	.globl	generic_patch_copyops
	.type	generic_patch_copyops,#function
generic_patch_copyops:
	GEN_DO_PATCH(memcpy, GENmemcpy)
	GEN_DO_PATCH(raw_copy_from_user, GENcopy_from_user)
	GEN_DO_PATCH(raw_copy_to_user, GENcopy_to_user)
	retl
	 nop
	.size	generic_patch_copyops,.-generic_patch_copyops
