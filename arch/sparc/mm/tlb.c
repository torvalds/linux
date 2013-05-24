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

static DEFINE_PER_CPU(struct tlb_batch, tlb_batch);

void flush_tlb_pending(void)
{
	struct tlb_batch *tb = &get_cpu_var(tlb_batch);
	struct mm_struct *mm = tb->mm;

	if (!tb->tlb_nr)
		goto out;

	flush_tsb_user(tb);

	if (CTX_VALID(mm->context)) {
		if (tb->tlb_nr == 1) {
			global_flush_tlb_page(mm, tb->vaddrs[0]);
		} else {
#ifdef CONFIG_SMP
			smp_flush_tlb_pending(tb->mm, tb->tlb_nr,
					      &tb->vaddrs[0]);
#else
			__flush_tlb_pending(CTX_HWBITS(tb->mm->context),
					    tb->tlb_nr, &tb->vaddrs[0]);
#endif
		}
	}

	tb->tlb_nr = 0;

out:
	put_cpu_var(tlb_batch);
}

void arch_enter_lazy_mmu_mode(void)
{
	struct tlb_batch *tb = &__get_cpu_var(tlb_batch);

	tb->active = 1;
}

void arch_leave_lazy_mmu_mode(void)
{
	struct tlb_batch *tb = &__get_cpu_var(tlb_batch);

	if (tb->tlb_nr)
		flush_tlb_pending();
	tb->active = 0;
}

void tlb_batch_add(struct mm_struct *mm, unsigned long vaddr,
		   pte_t *ptep, pte_t orig, int fullmm)
{
	struct tlb_batch *tb = &get_cpu_var(tlb_batch);
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

	if (fullmm) {
		put_cpu_var(tlb_batch);
		return;
	}

	nr = tb->tlb_nr;

	if (unlikely(nr != 0 && mm != tb->mm)) {
		flush_tlb_pending();
		nr = 0;
	}

	if (!tb->active) {
		global_flush_tlb_page(mm, vaddr);
		flush_tsb_user_page(mm, vaddr);
		goto out;
	}

	if (nr == 0)
		tb->mm = mm;

	tb->vaddrs[nr] = vaddr;
	tb->tlb_nr = ++nr;
	if (nr >= TLB_BATCH_NR)
		flush_tlb_pending();

out:
	put_cpu_var(tlb_batch);
}
