/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 Helge Deller <deller@gmx.de>
 */

#ifndef _ASM_PARISC_DWARF_H
#define _ASM_PARISC_DWARF_H

#ifdef __ASSEMBLY__

#define CFI_STARTPROC	.cfi_startproc
#define CFI_ENDPROC	.cfi_endproc
#define CFI_DEF_CFA	.cfi_def_cfa
#define CFI_REGISTER	.cfi_register
#define CFI_REL_OFFSET	.cfi_rel_offset
#define CFI_UNDEFINED	.cfi_undefined

#endif	/* __ASSEMBLY__ */

#endif	/* _ASM_PARISC_DWARF_H */
