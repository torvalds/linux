/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_TLBFLUSH_RADIX_H
#define _ASM_POWERPC_TLBFLUSH_RADIX_H

#include <asm/hvcall.h>

#define RIC_FLUSH_TLB 0
#define RIC_FLUSH_PWC 1
#define RIC_FLUSH_ALL 2

struct vm_area_struct;
struct mm_struct;
struct mmu_gather;

static inline u64 psize_to_rpti_pgsize(unsigned long psize)
{
	if (psize == MMU_PAGE_4K)
		return H_RPTI_PAGE_4K;
	if (psize == MMU_PAGE_64K)
		return H_RPTI_PAGE_64K;
	if (psize == MMU_PAGE_2M)
		return H_RPTI_PAGE_2M;
	if (psize == MMU_PAGE_1G)
		return H_RPTI_PAGE_1G;
	return H_RPTI_PAGE_ALL;
}

static inline int mmu_get_ap(int psize)
{
	return mmu_psize_defs[psize].ap;
}

#ifdef CONFIG_PPC_RADIX_MMU
extern void radix__tlbiel_all(unsigned int action);
extern void radix__flush_tlb_lpid_page(unsigned int lpid,
					unsigned long addr,
					unsigned long page_size);
extern void radix__flush_pwc_lpid(unsigned int lpid);
extern void radix__flush_all_lpid(unsigned int lpid);
extern void radix__flush_all_lpid_guest(unsigned int lpid);
#else
static inline void radix__tlbiel_all(unsigned int action) { WARN_ON(1); }
static inline void radix__flush_tlb_lpid_page(unsigned int lpid,
					unsigned long addr,
					unsigned long page_size)
{
	WARN_ON(1);
}
static inline void radix__flush_pwc_lpid(unsigned int lpid)
{
	WARN_ON(1);
}
static inline void radix__flush_all_lpid(unsigned int lpid)
{
	WARN_ON(1);
}
static inline void radix__flush_all_lpid_guest(unsigned int lpid)
{
	WARN_ON(1);
}
#endif

extern void radix__flush_hugetlb_tlb_range(struct vm_area_struct *vma,
					   unsigned long start, unsigned long end);
extern void radix__flush_tlb_range_psize(struct mm_struct *mm, unsigned long start,
					 unsigned long end, int psize);
extern void radix__flush_pmd_tlb_range(struct vm_area_struct *vma,
				       unsigned long start, unsigned long end);
extern void radix__flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			    unsigned long end);
extern void radix__flush_tlb_kernel_range(unsigned long start, unsigned long end);

extern void radix__local_flush_tlb_mm(struct mm_struct *mm);
extern void radix__local_flush_all_mm(struct mm_struct *mm);
extern void radix__local_flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
extern void radix__local_flush_tlb_page_psize(struct mm_struct *mm, unsigned long vmaddr,
					      int psize);
extern void radix__tlb_flush(struct mmu_gather *tlb);
#ifdef CONFIG_SMP
extern void radix__flush_tlb_mm(struct mm_struct *mm);
extern void radix__flush_all_mm(struct mm_struct *mm);
extern void radix__flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
extern void radix__flush_tlb_page_psize(struct mm_struct *mm, unsigned long vmaddr,
					int psize);
#else
#define radix__flush_tlb_mm(mm)		radix__local_flush_tlb_mm(mm)
#define radix__flush_all_mm(mm)		radix__local_flush_all_mm(mm)
#define radix__flush_tlb_page(vma,addr)	radix__local_flush_tlb_page(vma,addr)
#define radix__flush_tlb_page_psize(mm,addr,p) radix__local_flush_tlb_page_psize(mm,addr,p)
#endif
extern void radix__flush_tlb_pwc(struct mmu_gather *tlb, unsigned long addr);
extern void radix__flush_tlb_collapsed_pmd(struct mm_struct *mm, unsigned long addr);
extern void radix__flush_tlb_all(void);

#endif
