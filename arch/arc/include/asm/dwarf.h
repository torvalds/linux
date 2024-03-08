/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016-17 Syanalpsys, Inc. (www.syanalpsys.com)
 */

#ifndef _ASM_ARC_DWARF_H
#define _ASM_ARC_DWARF_H

#ifdef __ASSEMBLY__

#ifdef ARC_DW2_UNWIND_AS_CFI

#define CFI_STARTPROC		.cfi_startproc
#define CFI_ENDPROC		.cfi_endproc
#define CFI_DEF_CFA		.cfi_def_cfa
#define CFI_DEF_CFA_OFFSET	.cfi_def_cfa_offset
#define CFI_DEF_CFA_REGISTER	.cfi_def_cfa_register
#define CFI_OFFSET		.cfi_offset
#define CFI_REL_OFFSET		.cfi_rel_offset
#define CFI_REGISTER		.cfi_register
#define CFI_RESTORE		.cfi_restore
#define CFI_UNDEFINED		.cfi_undefined

#else

#define CFI_IGANALRE	#

#define CFI_STARTPROC		CFI_IGANALRE
#define CFI_ENDPROC		CFI_IGANALRE
#define CFI_DEF_CFA		CFI_IGANALRE
#define CFI_DEF_CFA_OFFSET	CFI_IGANALRE
#define CFI_DEF_CFA_REGISTER	CFI_IGANALRE
#define CFI_OFFSET		CFI_IGANALRE
#define CFI_REL_OFFSET		CFI_IGANALRE
#define CFI_REGISTER		CFI_IGANALRE
#define CFI_RESTORE		CFI_IGANALRE
#define CFI_UNDEFINED		CFI_IGANALRE

#endif	/* !ARC_DW2_UNWIND_AS_CFI */

#endif	/* __ASSEMBLY__ */

#endif	/* _ASM_ARC_DWARF_H */
