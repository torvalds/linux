/* SPDX-License-Identifier: GPL-2.0 */
/* Assembler variants of srmmu access functions.
 * Implemented in assembler to allow run-time patching.
 * LEON uses a different ASI for MMUREGS than SUN.
 *
 * The leon_1insn_patch infrastructure is used
 * for the run-time patching.
 */

#include <linux/linkage.h>

#include <asm/asmmacro.h>
#include <asm/pgtsrmmu.h>
#include <asm/asi.h>

/* unsigned int srmmu_get_mmureg(void) */
ENTRY(srmmu_get_mmureg)
LEON_PI(lda	[%g0] ASI_LEON_MMUREGS, %o0)
SUN_PI_(lda	[%g0] ASI_M_MMUREGS, %o0)
	retl
	 nop
ENDPROC(srmmu_get_mmureg)

/* void srmmu_set_mmureg(unsigned long regval) */
ENTRY(srmmu_set_mmureg)
LEON_PI(sta	%o0, [%g0] ASI_LEON_MMUREGS)
SUN_PI_(sta	%o0, [%g0] ASI_M_MMUREGS)
	retl
	 nop
ENDPROC(srmmu_set_mmureg)

/* void srmmu_set_ctable_ptr(unsigned long paddr) */
ENTRY(srmmu_set_ctable_ptr)
	/* paddr = ((paddr >> 4) & SRMMU_CTX_PMASK); */
	srl	%o0, 4, %g1
	and	%g1, SRMMU_CTX_PMASK, %g1

	mov	SRMMU_CTXTBL_PTR, %g2
LEON_PI(sta	%g1, [%g2] ASI_LEON_MMUREGS)
SUN_PI_(sta	%g1, [%g2] ASI_M_MMUREGS)
	retl
	 nop
ENDPROC(srmmu_set_ctable_ptr)


/* void srmmu_set_context(int context) */
ENTRY(srmmu_set_context)
	mov	SRMMU_CTX_REG, %g1
LEON_PI(sta	%o0, [%g1] ASI_LEON_MMUREGS)
SUN_PI_(sta	%o0, [%g1] ASI_M_MMUREGS)
	retl
	 nop
ENDPROC(srmmu_set_context)


/* int srmmu_get_context(void) */
ENTRY(srmmu_get_context)
	mov	SRMMU_CTX_REG, %o0
LEON_PI(lda     [%o0] ASI_LEON_MMUREGS, %o0)
SUN_PI_(lda	[%o0] ASI_M_MMUREGS, %o0)
	retl
	 nop
ENDPROC(srmmu_get_context)


/* unsigned int srmmu_get_fstatus(void) */
ENTRY(srmmu_get_fstatus)
	mov	SRMMU_FAULT_STATUS, %o0
LEON_PI(lda     [%o0] ASI_LEON_MMUREGS, %o0)
SUN_PI_(lda	[%o0] ASI_M_MMUREGS, %o0)
	retl
	 nop
ENDPROC(srmmu_get_fstatus)


/* unsigned int srmmu_get_faddr(void) */
ENTRY(srmmu_get_faddr)
	mov	SRMMU_FAULT_ADDR, %o0
LEON_PI(lda     [%o0] ASI_LEON_MMUREGS, %o0)
SUN_PI_(lda	[%o0] ASI_M_MMUREGS, %o0)
	retl
	 nop
ENDPROC(srmmu_get_faddr)
