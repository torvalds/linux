/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016-17 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef _ASM_ARC_DWARF_H
#define _ASM_ARC_DWARF_H

#ifdef __ASSEMBLY__

#ifdef ARC_DW2_UNWIND_AS_CFI

#define CFI_STARTPROC	.cfi_startproc
#define CFI_ENDPROC	.cfi_endproc
#define CFI_DEF_CFA	.cfi_def_cfa
#define CFI_REGISTER	.cfi_register
#define CFI_REL_OFFSET	.cfi_rel_offset
#define CFI_UNDEFINED	.cfi_undefined

#else

#define CFI_IGNORE	#

#define CFI_STARTPROC	CFI_IGNORE
#define CFI_ENDPROC	CFI_IGNORE
#define CFI_DEF_CFA	CFI_IGNORE
#define CFI_REGISTER	CFI_IGNORE
#define CFI_REL_OFFSET	CFI_IGNORE
#define CFI_UNDEFINED	CFI_IGNORE

#endif	/* !ARC_DW2_UNWIND_AS_CFI */

#endif	/* __ASSEMBLY__ */

#endif	/* _ASM_ARC_DWARF_H */
