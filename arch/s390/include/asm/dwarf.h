/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_DWARF_H
#define _ASM_S390_DWARF_H

#ifdef __ASSEMBLER__

#define CFI_STARTPROC		.cfi_startproc
#define CFI_ENDPROC		.cfi_endproc
#define CFI_DEF_CFA_OFFSET	.cfi_def_cfa_offset
#define CFI_ADJUST_CFA_OFFSET	.cfi_adjust_cfa_offset
#define CFI_RESTORE		.cfi_restore
#define CFI_REL_OFFSET		.cfi_rel_offset

#ifdef CONFIG_AS_CFI_VAL_OFFSET
#define CFI_VAL_OFFSET		.cfi_val_offset
#else
#define CFI_VAL_OFFSET		#
#endif

#ifndef BUILD_VDSO
	/*
	 * Emit CFI data in .debug_frame sections and not in .eh_frame
	 * sections.  The .eh_frame CFI is used for runtime unwind
	 * information that is not being used.  Hence, vmlinux.lds.S
	 * can discard the .eh_frame sections.
	 */
	.cfi_sections .debug_frame
#else
	/*
	 * For vDSO, emit CFI data in both, .eh_frame and .debug_frame
	 * sections.
	 */
	.cfi_sections .eh_frame, .debug_frame
#endif

#endif	/* __ASSEMBLER__ */

#endif	/* _ASM_S390_DWARF_H */
