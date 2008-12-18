#ifndef _ASM_POWERPC_TLBFLUSH_H
#define _ASM_POWERPC_TLBFLUSH_H

/*
 * TLB flushing:
 *
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - local_flush_tlb_mm(mm) flushes the specified mm context on
 *                           the local processor
 *  - local_flush_tlb_page(vma, vmaddr) flushes one page on the local processor
 *  - flush_tlb_page_nohash(vma, vmaddr) flushes one page if SW loaded TLB
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes a range of kernel pages
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#ifdef __KERNEL__

#ifdef CONFIG_PPC_MMU_NOHASH
/*
 * TLB flushing for software loaded TLB chips
 *
 * TODO: (CONFIG_FSL_BOOKE) determine if flush_tlb_range &
 * flush_tlb_kernel_range are best implemented as tlbia vs
 * specific tlbie's
 */

#include <linux/mm.h>

#define MMU_NO_CONTEXT      	((unsigned int)-1)

extern void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			    unsigned long end);
extern void flush_tlb_kernel_range(unsigned long start, unsigned long end);

extern void local_flush_tlb_mm(struct mm_struct *mm);
extern void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);

#ifdef CONFIG_SMP
extern void flush_tlb_mm(struct mm_struct *mm);
extern void flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
#else
#define flush_tlb_mm(mm)		local_flush_tlb_mm(mm)
#define flush_tlb_page(vma,addr)	local_flush_tlb_page(vma,addr)
#endif
#define flush_tlb_page_nohash(vma,addr)	flush_tlb_page(vma,addr)

#elif defined(CONFIG_PPC_STD_MMU_32)

/*
 * TLB flushing for "classic" hash-MMU 32-bit CPUs, 6xx, 7xx, 7xxx
 */
extern void flush_tlb_mm(struct mm_struct *mm);
extern void flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
extern void flush_tlb_page_nohash(struct vm_area_struct *vma, unsigned long addr);
extern void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			    unsigned long end);
extern void flush_tlb_kernel_range(unsigned long start, unsigned long end);
static inline void local_flush_tlb_page(struct vm_area_struct *vma,
					unsigned long vmaddr)
{
	flush_tlb_page(vma, vmaddr);
}
static inline void local_flush_tlb_mm(struct mm_struct *mm)
{
	flush_tlb_mm(mm);
}

#elif defined(CONFIG_PPC_STD_MMU_64)

/*
 * TLB flushing for 64-bit hash-MMU CPUs
 */

#include <linux/percpu.h>
#include <asm/page.h>

#define PPC64_TLB_BATCH_NR 192

struct ppc64_tlb_batch {
	int			active;
	unsigned long		index;
	struct mm_struct	*mm;
	real_pte_t		pte[PPC64_TLB_BATCH_NR];
	unsigned long		vaddr[PPC64_TLB_BATCH_NR];
	unsigned int		psize;
	int			ssize;
};
DECLARE_PER_CPU(struct ppc64_tlb_batch, ppc64_tlb_batch);

extern void __flush_tlb_pending(struct ppc64_tlb_batch *batch);

extern void hpte_need_flush(struct mm_struct *mm, unsigned long addr,
			    pte_t *ptep, unsigned long pte, int huge);

#define __HAVE_ARCH_ENTER_LAZY_MMU_MODE

static inline void arch_enter_lazy_mmu_mode(void)
{
	struct ppc64_tlb_batch *batch = &__get_cpu_var(ppc64_tlb_batch);

	batch->active = 1;
}

static inline void arch_leave_lazy_mmu_mode(void)
{
	struct ppc64_tlb_batch *batch = &__get_cpu_var(ppc64_tlb_batch);

	if (batch->index)
		__flush_tlb_pending(batch);
	batch->active = 0;
}

#define arch_flush_lazy_mmu_mode()      do {} while (0)


extern void flush_hash_page(unsigned long va, real_pte_t pte, int psize,
			    int ssize, int local);
extern void flush_hash_range(unsigned long number, int local);


static inline void local_flush_tlb_mm(struct mm_struct *mm)
{
}

static inline void flush_tlb_mm(struct mm_struct *mm)
{
}

static inline void local_flush_tlb_page(struct vm_area_struct *vma,
					unsigned long vmaddr)
{
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long vmaddr)
{
}

static inline void flush_tlb_page_nohash(struct vm_area_struct *vma,
					 unsigned long vmaddr)
{
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
}

static inline void flush_tlb_kernel_range(unsigned long start,
					  unsigned long end)
{
}

/* Private function for use by PCI IO mapping code */
extern void __flush_hash_table_range(struct mm_struct *mm, unsigned long start,
				     unsigned long end);

#else
#error Unsupported MMU type
#endif

#endif /*__KERNEL__ */
#endif /* _ASM_POWERPC_TLBFLUSH_H */
