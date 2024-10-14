/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Userland implementation of getrandom() for processes
 * for use in the vDSO
 *
 * Copyright (C) 2024 Christophe Leroy <christophe.leroy@csgroup.eu>, CS GROUP France
 */
#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/vdso.h>
#include <asm/vdso_datapage.h>
#include <asm/asm-offsets.h>
#include <asm/unistd.h>

/*
 * The macro sets two stack frames, one for the caller and one for the callee
 * because there are no requirement for the caller to set a stack frame when
 * calling VDSO so it may have omitted to set one, especially on PPC64
 */

.macro cvdso_call funct
  .cfi_startproc
	PPC_STLU	r1, -PPC_MIN_STKFRM(r1)
  .cfi_adjust_cfa_offset PPC_MIN_STKFRM
	mflr		r0
	PPC_STLU	r1, -PPC_MIN_STKFRM(r1)
  .cfi_adjust_cfa_offset PPC_MIN_STKFRM
	PPC_STL		r0, PPC_MIN_STKFRM + PPC_LR_STKOFF(r1)
  .cfi_rel_offset lr, PPC_MIN_STKFRM + PPC_LR_STKOFF
#ifdef __powerpc64__
	PPC_STL		r2, PPC_MIN_STKFRM + STK_GOT(r1)
  .cfi_rel_offset r2, PPC_MIN_STKFRM + STK_GOT
#endif
	get_realdatapage	r8, r11
	addi		r8, r8, VDSO_RNG_DATA_OFFSET
	bl		CFUNC(DOTSYM(\funct))
	PPC_LL		r0, PPC_MIN_STKFRM + PPC_LR_STKOFF(r1)
#ifdef __powerpc64__
	PPC_LL		r2, PPC_MIN_STKFRM + STK_GOT(r1)
  .cfi_restore r2
#endif
	cmpwi		r3, 0
	mtlr		r0
	addi		r1, r1, 2 * PPC_MIN_STKFRM
  .cfi_restore lr
  .cfi_def_cfa_offset 0
	crclr		so
	bgelr+
	crset		so
	neg		r3, r3
	blr
  .cfi_endproc
.endm

	.text
V_FUNCTION_BEGIN(__kernel_getrandom)
	cvdso_call __c_kernel_getrandom
V_FUNCTION_END(__kernel_getrandom)
