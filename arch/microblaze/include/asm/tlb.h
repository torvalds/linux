/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_TLB_H
#define _ASM_MICROBLAZE_TLB_H

#define tlb_flush(tlb)	flush_tlb_mm((tlb)->mm)

#include <asm-generic/tlb.h>

#ifdef CONFIG_MMU
#define tlb_start_vma(tlb, vma)		do { } while (0)
#define tlb_end_vma(tlb, vma)		do { } while (0)
#define __tlb_remove_tlb_entry(tlb, pte, address) do { } while (0)
#endif

#endif /* _ASM_MICROBLAZE_TLB_H */
