/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_DWARF_H
#define _ASM_S390_DWARF_H

#ifdef __ASSEMBLY__

#define CFI_STARTPROC		.cfi_startproc
#define CFI_ENDPROC		.cfi_endproc
#define CFI_DEF_CFA_OFFSET	.cfi_def_cfa_offset
#define CFI_ADJUST_CFA_OFFSET	.cfi_adjust_cfa_offset
#define CFI_RESTORE		.cfi_restore

#ifdef CONFIG_AS_CFI_VAL_OFFSET
#define CFI_VAL_OFFSET		.cfi_val_offset
#else
#define CFI_VAL_OFFSET		#
#endif

#ifndef BUILD_VDSO
	/*
	 * Emit CFI data in .de_frame sections and not in .eh_frame
	 * sections.  The .eh_frame CFI is used for runtime unwind
	 * information that is not being used.  Hence, vmlinux.lds.S
	 * can discard the .eh_frame sections.
	 */
	.cfi_sections .de_frame
#else
	/*
	 * For vDSO, emit CFI data in both, .eh_frame and .de_frame
	 * sections.
	 */
	.cfi_sections .eh_frame, .de_frame
#endif

#endif	/* __ASSEMBLY__ */

#endif	/* _ASM_S390_DWARF_H */
