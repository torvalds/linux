/* arch/sparc64/mm/tlb.c
 *
 * Copyright (C) 2004 David S. Miller <davem@redhat.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/preempt.h>

#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>
#include <asm/tlb.h>

/* Heavily inspired by the ppc64 code.  */

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers) = { 0, };

void flush_tlb_pending(void)
{
	struct mmu_gather *mp;

	preempt_disable();

	mp = &__get_cpu_var(mmu_gathers);
	if (mp->tlb_nr) {
		flush_tsb_user(mp);

		if (CTX_VALID(mp->mm->context)) {
#ifdef CONFIG_SMP
			smp_flush_tlb_pending(mp->mm, mp->tlb_nr,
					      &mp->vaddrs[0]);
#else
			__flush_tlb_pending(CTX_HWBITS(mp->mm->context),
					    mp->tlb_nr, &mp->vaddrs[0]);
#endif
		}
		mp->tlb_nr = 0;
	}

	preempt_enable();
}

void tlb_batch_add(struct mm_struct *mm, unsigned long vaddr, pte_t *ptep, pte_t orig)
{
	struct mmu_gather *mp = &__get_cpu_var(mmu_gathers);
	unsigned long nr;

	vaddr &= PAGE_MASK;
	if (pte_exec(orig))
		vaddr |= 0x1UL;

	if (tlb_type != hypervisor &&
	    pte_dirty(orig)) {
		unsigned long paddr, pfn = pte_pfn(orig);
		struct address_space *mapping;
		struct page *page;

		if (!pfn_valid(pfn))
			goto no_cache_flush;

		page = pfn_to_page(pfn);
		if (PageReserved(page))
			goto no_cache_flush;

		/* A real file page? */
		mapping = page_mapping(page);
		if (!mapping)
			goto no_cache_flush;

		paddr = (unsigned long) page_address(page);
		if ((paddr ^ vaddr) & (1 << 13))
			flush_dcache_page_all(mm, page);
	}

no_cache_flush:

	if (mp->fullmm)
		return;

	nr = mp->tlb_nr;

	if (unlikely(nr != 0 && mm != mp->mm)) {
		flush_tlb_pending();
		nr = 0;
	}

	if (nr == 0)
		mp->mm = mm;

	mp->vaddrs[nr] = vaddr;
	mp->tlb_nr = ++nr;
	if (nr >= TLB_BATCH_NR)
		flush_tlb_pending();
}
