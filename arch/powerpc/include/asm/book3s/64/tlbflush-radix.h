#ifndef _ASM_POWERPC_TLBFLUSH_RADIX_H
#define _ASM_POWERPC_TLBFLUSH_RADIX_H

struct vm_area_struct;
struct mm_struct;
struct mmu_gather;

static inline int mmu_get_ap(int psize)
{
	return mmu_psize_defs[psize].ap;
}

extern void radix__flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			    unsigned long end);
extern void radix__flush_tlb_kernel_range(unsigned long start, unsigned long end);

extern void radix__local_flush_tlb_mm(struct mm_struct *mm);
extern void radix__local_flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
extern void radix___local_flush_tlb_page(struct mm_struct *mm, unsigned long vmaddr,
				    unsigned long ap, int nid);
extern void radix__tlb_flush(struct mmu_gather *tlb);
#ifdef CONFIG_SMP
extern void radix__flush_tlb_mm(struct mm_struct *mm);
extern void radix__flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
extern void radix___flush_tlb_page(struct mm_struct *mm, unsigned long vmaddr,
			      unsigned long ap, int nid);
#else
#define radix__flush_tlb_mm(mm)		radix__local_flush_tlb_mm(mm)
#define radix__flush_tlb_page(vma,addr)	radix__local_flush_tlb_page(vma,addr)
#define radix___flush_tlb_page(mm,addr,p,i)	radix___local_flush_tlb_page(mm,addr,p,i)
#endif

#endif
