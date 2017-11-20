/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_DWARF_H
#define _ASM_S390_DWARF_H

#ifdef __ASSEMBLY__

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

#endif	/* __ASSEMBLY__ */

#endif	/* _ASM_S390_DWARF_H */
