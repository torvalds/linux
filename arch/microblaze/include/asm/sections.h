/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_MICROBLAZE_SECTIONS_H
#define _ASM_MICROBLAZE_SECTIONS_H

#include <asm-generic/sections.h>

# ifndef __ASSEMBLER__
extern char _ssbss[], _esbss[];
extern unsigned long __ivt_start[], __ivt_end[];

extern u32 _fdt_start[], _fdt_end[];

# endif /* !__ASSEMBLER__ */
#endif /* _ASM_MICROBLAZE_SECTIONS_H */
