/*
 * include/asm-xtensa/tlb.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_TLB_H
#define _XTENSA_TLB_H

#include <asm/cache.h>
#include <asm/page.h>

#if (DCACHE_WAY_SIZE <= PAGE_SIZE)

/* Note, read http://lkml.org/lkml/2004/1/15/6 */

# define tlb_start_vma(tlb,vma)			do { } while (0)
# define tlb_end_vma(tlb,vma)			do { } while (0)

#else

# define tlb_start_vma(tlb, vma)					      \
	do {								      \
		if (!tlb->fullmm)					      \
			flush_cache_range(vma, vma->vm_start, vma->vm_end);   \
	} while(0)

# define tlb_end_vma(tlb, vma)						      \
	do {								      \
		if (!tlb->fullmm)					      \
			flush_tlb_range(vma, vma->vm_start, vma->vm_end);     \
	} while(0)

#endif

#define __tlb_remove_tlb_entry(tlb,pte,addr)	do { } while (0)
#define tlb_flush(tlb)				flush_tlb_mm((tlb)->mm)

#include <asm-generic/tlb.h>

#define __pte_free_tlb(tlb, pte)		pte_free((tlb)->mm, pte)

#endif	/* _XTENSA_TLB_H */
