/* U3patch.S: Patch Ultra-I routines with Ultra-III variant.
 *
 * Copyright (C) 2004 David S. Miller <davem@redhat.com>
 */

#define BRANCH_ALWAYS	0x10680000
#define NOP		0x01000000
#define ULTRA3_DO_PATCH(OLD, NEW)	\
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

	.globl	cheetah_patch_copyops
	.type	cheetah_patch_copyops,#function
cheetah_patch_copyops:
	ULTRA3_DO_PATCH(memcpy, U3memcpy)
	ULTRA3_DO_PATCH(raw_copy_from_user, U3copy_from_user)
	ULTRA3_DO_PATCH(raw_copy_to_user, U3copy_to_user)
	retl
	 nop
	.size	cheetah_patch_copyops,.-cheetah_patch_copyops
