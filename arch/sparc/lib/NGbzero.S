/* NGbzero.S: Niagara optimized memset/clear_user.
 *
 * Copyright (C) 2006 David S. Miller (davem@davemloft.net)
 */
#include <asm/asi.h>

#define EX_ST(x,y)		\
98:	x,y;			\
	.section __ex_table,"a";\
	.align 4;		\
	.word 98b, __retl_o1;	\
	.text;			\
	.align 4;

	.text

	.globl		NGmemset
	.type		NGmemset, #function
NGmemset:		/* %o0=buf, %o1=pat, %o2=len */
	and		%o1, 0xff, %o3
	mov		%o2, %o1
	sllx		%o3, 8, %g1
	or		%g1, %o3, %o2
	sllx		%o2, 16, %g1
	or		%g1, %o2, %o2
	sllx		%o2, 32, %g1
	ba,pt		%xcc, 1f
	 or		%g1, %o2, %o2

	.globl		NGbzero
	.type		NGbzero, #function
NGbzero:
	clr		%o2
1:	brz,pn		%o1, NGbzero_return
	 mov		%o0, %o3

	/* %o5: saved %asi, restored at NGbzero_done
	 * %g7: store-init %asi to use
	 * %o4:	non-store-init %asi to use
	 */
	rd		%asi, %o5
	mov		ASI_BLK_INIT_QUAD_LDD_P, %g7
	mov		ASI_P, %o4
	wr		%o4, 0x0, %asi

NGbzero_from_clear_user:
	cmp		%o1, 15
	bl,pn		%icc, NGbzero_tiny
	 andcc		%o0, 0x7, %g1
	be,pt		%xcc, 2f
	 mov		8, %g2
	sub		%g2, %g1, %g1
	sub		%o1, %g1, %o1
1:	EX_ST(stba %o2, [%o0 + 0x00] %asi)
	subcc		%g1, 1, %g1
	bne,pt		%xcc, 1b
	 add		%o0, 1, %o0
2:	cmp		%o1, 128
	bl,pn		%icc, NGbzero_medium
	 andcc		%o0, (64 - 1), %g1
	be,pt		%xcc, NGbzero_pre_loop
	 mov		64, %g2
	sub		%g2, %g1, %g1
	sub		%o1, %g1, %o1
1:	EX_ST(stxa %o2, [%o0 + 0x00] %asi)
	subcc		%g1, 8, %g1
	bne,pt		%xcc, 1b
	 add		%o0, 8, %o0

NGbzero_pre_loop:
	wr		%g7, 0x0, %asi
	andn		%o1, (64 - 1), %g1
	sub		%o1, %g1, %o1
NGbzero_loop:
	EX_ST(stxa %o2, [%o0 + 0x00] %asi)
	EX_ST(stxa %o2, [%o0 + 0x08] %asi)
	EX_ST(stxa %o2, [%o0 + 0x10] %asi)
	EX_ST(stxa %o2, [%o0 + 0x18] %asi)
	EX_ST(stxa %o2, [%o0 + 0x20] %asi)
	EX_ST(stxa %o2, [%o0 + 0x28] %asi)
	EX_ST(stxa %o2, [%o0 + 0x30] %asi)
	EX_ST(stxa %o2, [%o0 + 0x38] %asi)
	subcc		%g1, 64, %g1
	bne,pt		%xcc, NGbzero_loop
	 add		%o0, 64, %o0

	membar		#Sync
	wr		%o4, 0x0, %asi
	brz,pn		%o1, NGbzero_done
NGbzero_medium:
	 andncc		%o1, 0x7, %g1
	be,pn		%xcc, 2f
	 sub		%o1, %g1, %o1
1:	EX_ST(stxa %o2, [%o0 + 0x00] %asi)
	subcc		%g1, 8, %g1
	bne,pt		%xcc, 1b
	 add		%o0, 8, %o0
2:	brz,pt		%o1, NGbzero_done
	 nop

NGbzero_tiny:
1:	EX_ST(stba %o2, [%o0 + 0x00] %asi)
	subcc		%o1, 1, %o1
	bne,pt		%icc, 1b
	 add		%o0, 1, %o0

	/* fallthrough */

NGbzero_done:
	wr		%o5, 0x0, %asi

NGbzero_return:
	retl
	 mov		%o3, %o0
	.size		NGbzero, .-NGbzero
	.size		NGmemset, .-NGmemset

	.globl		NGclear_user
	.type		NGclear_user, #function
NGclear_user:		/* %o0=buf, %o1=len */
	rd		%asi, %o5
	brz,pn		%o1, NGbzero_done
	 clr		%o3
	cmp		%o5, ASI_AIUS
	bne,pn		%icc, NGbzero
	 clr		%o2
	mov		ASI_BLK_INIT_QUAD_LDD_AIUS, %g7
	ba,pt		%xcc, NGbzero_from_clear_user
	 mov		ASI_AIUS, %o4
	.size		NGclear_user, .-NGclear_user

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

	.globl	niagara_patch_bzero
	.type	niagara_patch_bzero,#function
niagara_patch_bzero:
	NG_DO_PATCH(memset, NGmemset)
	NG_DO_PATCH(__bzero, NGbzero)
	NG_DO_PATCH(__clear_user, NGclear_user)
	NG_DO_PATCH(tsb_init, NGtsb_init)
	retl
	 nop
	.size	niagara_patch_bzero,.-niagara_patch_bzero
