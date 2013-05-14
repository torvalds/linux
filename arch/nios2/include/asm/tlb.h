/*
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2009 Wind River Systems Inc
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_TLB_H
#define _ASM_NIOS2_TLB_H

#define tlb_flush(tlb)	flush_tlb_mm((tlb)->mm)

#ifdef CONFIG_MMU

extern void set_mmu_pid(unsigned long pid);

/*
 * NiosII doesn't need any special per-pte or per-vma handling, except
 * we need to flush cache for the area to be unmapped.
 */
#define tlb_start_vma(tlb, vma)					\
	do {							\
		if (!tlb->fullmm)				\
			flush_cache_range(vma, vma->vm_start, vma->vm_end); \
	}  while (0)

#else

#define tlb_start_vma(tlb, vma)	do { } while (0)

#endif /* CONFIG_MMU */

#define tlb_end_vma(tlb, vma)	do { } while (0)
#define __tlb_remove_tlb_entry(tlb, ptep, address)	do { } while (0)

#include <linux/pagemap.h>
#include <asm-generic/tlb.h>

#endif /* _ASM_NIOS2_TLB_H */
