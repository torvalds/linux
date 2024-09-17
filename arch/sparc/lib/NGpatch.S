/* SPDX-License-Identifier: GPL-2.0 */
/* NGpatch.S: Patch Ultra-I routines with Niagara variant.
 *
 * Copyright (C) 2006 David S. Miller <davem@davemloft.net>
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

	.globl	niagara_patch_copyops
	.type	niagara_patch_copyops,#function
niagara_patch_copyops:
	NG_DO_PATCH(memcpy, NGmemcpy)
	NG_DO_PATCH(raw_copy_from_user, NGcopy_from_user)
	NG_DO_PATCH(raw_copy_to_user, NGcopy_to_user)
	retl
	 nop
	.size	niagara_patch_copyops,.-niagara_patch_copyops
