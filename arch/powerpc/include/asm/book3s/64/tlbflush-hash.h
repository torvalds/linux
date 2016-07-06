#ifndef _ASM_POWERPC_BOOK3S_64_TLBFLUSH_HASH_H
#define _ASM_POWERPC_BOOK3S_64_TLBFLUSH_HASH_H

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
	unsigned long		vpn[PPC64_TLB_BATCH_NR];
	unsigned int		psize;
	int			ssize;
};
DECLARE_PER_CPU(struct ppc64_tlb_batch, ppc64_tlb_batch);

extern void __flush_tlb_pending(struct ppc64_tlb_batch *batch);

#define __HAVE_ARCH_ENTER_LAZY_MMU_MODE

static inline void arch_enter_lazy_mmu_mode(void)
{
	struct ppc64_tlb_batch *batch;

	if (radix_enabled())
		return;
	batch = this_cpu_ptr(&ppc64_tlb_batch);
	batch->active = 1;
}

static inline void arch_leave_lazy_mmu_mode(void)
{
	struct ppc64_tlb_batch *batch;

	if (radix_enabled())
		return;
	batch = this_cpu_ptr(&ppc64_tlb_batch);

	if (batch->index)
		__flush_tlb_pending(batch);
	batch->active = 0;
}

#define arch_flush_lazy_mmu_mode()      do {} while (0)


extern void flush_hash_page(unsigned long vpn, real_pte_t pte, int psize,
			    int ssize, unsigned long flags);
extern void flush_hash_range(unsigned long number, int local);
extern void flush_hash_hugepage(unsigned long vsid, unsigned long addr,
				pmd_t *pmdp, unsigned int psize, int ssize,
				unsigned long flags);
static inline void hash__local_flush_tlb_mm(struct mm_struct *mm)
{
}

static inline void hash__flush_tlb_mm(struct mm_struct *mm)
{
}

static inline void hash__local_flush_tlb_page(struct vm_area_struct *vma,
					  unsigned long vmaddr)
{
}

static inline void hash__flush_tlb_page(struct vm_area_struct *vma,
				    unsigned long vmaddr)
{
}

static inline void hash__flush_tlb_page_nohash(struct vm_area_struct *vma,
					   unsigned long vmaddr)
{
}

static inline void hash__flush_tlb_range(struct vm_area_struct *vma,
				     unsigned long start, unsigned long end)
{
}

static inline void hash__flush_tlb_kernel_range(unsigned long start,
					    unsigned long end)
{
}


struct mmu_gather;
extern void hash__tlb_flush(struct mmu_gather *tlb);
/* Private function for use by PCI IO mapping code */
extern void __flush_hash_table_range(struct mm_struct *mm, unsigned long start,
				     unsigned long end);
extern void flush_tlb_pmd_range(struct mm_struct *mm, pmd_t *pmd,
				unsigned long addr);
#endif /*  _ASM_POWERPC_BOOK3S_64_TLBFLUSH_HASH_H */
