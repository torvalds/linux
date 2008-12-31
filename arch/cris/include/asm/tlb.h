#ifndef _CRIS_TLB_H
#define _CRIS_TLB_H

#include <linux/pagemap.h>

#include <arch/tlb.h>

/*
 * cris doesn't need any special per-pte or
 * per-vma handling..
 */
#define tlb_start_vma(tlb, vma) do { } while (0)
#define tlb_end_vma(tlb, vma) do { } while (0)
#define __tlb_remove_tlb_entry(tlb, ptep, address) do { } while (0)

#define tlb_flush(tlb) flush_tlb_mm((tlb)->mm)
#include <asm-generic/tlb.h>

#endif
