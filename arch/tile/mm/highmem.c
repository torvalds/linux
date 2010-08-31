/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <asm/homecache.h>

#define kmap_get_pte(vaddr) \
	pte_offset_kernel(pmd_offset(pud_offset(pgd_offset_k(vaddr), (vaddr)),\
		(vaddr)), (vaddr))


void *kmap(struct page *page)
{
	void *kva;
	unsigned long flags;
	pte_t *ptep;

	might_sleep();
	if (!PageHighMem(page))
		return page_address(page);
	kva = kmap_high(page);

	/*
	 * Rewrite the PTE under the lock.  This ensures that the page
	 * is not currently migrating.
	 */
	ptep = kmap_get_pte((unsigned long)kva);
	flags = homecache_kpte_lock();
	set_pte_at(&init_mm, kva, ptep, mk_pte(page, page_to_kpgprot(page)));
	homecache_kpte_unlock(flags);

	return kva;
}
EXPORT_SYMBOL(kmap);

void kunmap(struct page *page)
{
	if (in_interrupt())
		BUG();
	if (!PageHighMem(page))
		return;
	kunmap_high(page);
}
EXPORT_SYMBOL(kunmap);

static void debug_kmap_atomic_prot(enum km_type type)
{
#ifdef CONFIG_DEBUG_HIGHMEM
	static unsigned warn_count = 10;

	if (unlikely(warn_count == 0))
		return;

	if (unlikely(in_interrupt())) {
		if (in_irq()) {
			if (type != KM_IRQ0 && type != KM_IRQ1 &&
			    type != KM_BIO_SRC_IRQ &&
			    /* type != KM_BIO_DST_IRQ && */
			    type != KM_BOUNCE_READ) {
				WARN_ON(1);
				warn_count--;
			}
		} else if (!irqs_disabled()) {	/* softirq */
			if (type != KM_IRQ0 && type != KM_IRQ1 &&
			    type != KM_SOFTIRQ0 && type != KM_SOFTIRQ1 &&
			    type != KM_SKB_SUNRPC_DATA &&
			    type != KM_SKB_DATA_SOFTIRQ &&
			    type != KM_BOUNCE_READ) {
				WARN_ON(1);
				warn_count--;
			}
		}
	}

	if (type == KM_IRQ0 || type == KM_IRQ1 || type == KM_BOUNCE_READ ||
	    type == KM_BIO_SRC_IRQ /* || type == KM_BIO_DST_IRQ */) {
		if (!irqs_disabled()) {
			WARN_ON(1);
			warn_count--;
		}
	} else if (type == KM_SOFTIRQ0 || type == KM_SOFTIRQ1) {
		if (irq_count() == 0 && !irqs_disabled()) {
			WARN_ON(1);
			warn_count--;
		}
	}
#endif
}

/*
 * Describe a single atomic mapping of a page on a given cpu at a
 * given address, and allow it to be linked into a list.
 */
struct atomic_mapped_page {
	struct list_head list;
	struct page *page;
	int cpu;
	unsigned long va;
};

static spinlock_t amp_lock = __SPIN_LOCK_UNLOCKED(&amp_lock);
static struct list_head amp_list = LIST_HEAD_INIT(amp_list);

/*
 * Combining this structure with a per-cpu declaration lets us give
 * each cpu an atomic_mapped_page structure per type.
 */
struct kmap_amps {
	struct atomic_mapped_page per_type[KM_TYPE_NR];
};
static DEFINE_PER_CPU(struct kmap_amps, amps);

/*
 * Add a page and va, on this cpu, to the list of kmap_atomic pages,
 * and write the new pte to memory.  Writing the new PTE under the
 * lock guarantees that it is either on the list before migration starts
 * (if we won the race), or set_pte() sets the migrating bit in the PTE
 * (if we lost the race).  And doing it under the lock guarantees
 * that when kmap_atomic_fix_one_pte() comes along, it finds a valid
 * PTE in memory, iff the mapping is still on the amp_list.
 *
 * Finally, doing it under the lock lets us safely examine the page
 * to see if it is immutable or not, for the generic kmap_atomic() case.
 * If we examine it earlier we are exposed to a race where it looks
 * writable earlier, but becomes immutable before we write the PTE.
 */
static void kmap_atomic_register(struct page *page, enum km_type type,
				 unsigned long va, pte_t *ptep, pte_t pteval)
{
	unsigned long flags;
	struct atomic_mapped_page *amp;

	flags = homecache_kpte_lock();
	spin_lock(&amp_lock);

	/* With interrupts disabled, now fill in the per-cpu info. */
	amp = &__get_cpu_var(amps).per_type[type];
	amp->page = page;
	amp->cpu = smp_processor_id();
	amp->va = va;

	/* For generic kmap_atomic(), choose the PTE writability now. */
	if (!pte_read(pteval))
		pteval = mk_pte(page, page_to_kpgprot(page));

	list_add(&amp->list, &amp_list);
	set_pte(ptep, pteval);
	arch_flush_lazy_mmu_mode();

	spin_unlock(&amp_lock);
	homecache_kpte_unlock(flags);
}

/*
 * Remove a page and va, on this cpu, from the list of kmap_atomic pages.
 * Linear-time search, but we count on the lists being short.
 * We don't need to adjust the PTE under the lock (as opposed to the
 * kmap_atomic_register() case), since we're just unconditionally
 * zeroing the PTE after it's off the list.
 */
static void kmap_atomic_unregister(struct page *page, unsigned long va)
{
	unsigned long flags;
	struct atomic_mapped_page *amp;
	int cpu = smp_processor_id();
	spin_lock_irqsave(&amp_lock, flags);
	list_for_each_entry(amp, &amp_list, list) {
		if (amp->page == page && amp->cpu == cpu && amp->va == va)
			break;
	}
	BUG_ON(&amp->list == &amp_list);
	list_del(&amp->list);
	spin_unlock_irqrestore(&amp_lock, flags);
}

/* Helper routine for kmap_atomic_fix_kpte(), below. */
static void kmap_atomic_fix_one_kpte(struct atomic_mapped_page *amp,
				     int finished)
{
	pte_t *ptep = kmap_get_pte(amp->va);
	if (!finished) {
		set_pte(ptep, pte_mkmigrate(*ptep));
		flush_remote(0, 0, NULL, amp->va, PAGE_SIZE, PAGE_SIZE,
			     cpumask_of(amp->cpu), NULL, 0);
	} else {
		/*
		 * Rewrite a default kernel PTE for this page.
		 * We rely on the fact that set_pte() writes the
		 * present+migrating bits last.
		 */
		pte_t pte = mk_pte(amp->page, page_to_kpgprot(amp->page));
		set_pte(ptep, pte);
	}
}

/*
 * This routine is a helper function for homecache_fix_kpte(); see
 * its comments for more information on the "finished" argument here.
 *
 * Note that we hold the lock while doing the remote flushes, which
 * will stall any unrelated cpus trying to do kmap_atomic operations.
 * We could just update the PTEs under the lock, and save away copies
 * of the structs (or just the va+cpu), then flush them after we
 * release the lock, but it seems easier just to do it all under the lock.
 */
void kmap_atomic_fix_kpte(struct page *page, int finished)
{
	struct atomic_mapped_page *amp;
	unsigned long flags;
	spin_lock_irqsave(&amp_lock, flags);
	list_for_each_entry(amp, &amp_list, list) {
		if (amp->page == page)
			kmap_atomic_fix_one_kpte(amp, finished);
	}
	spin_unlock_irqrestore(&amp_lock, flags);
}

/*
 * kmap_atomic/kunmap_atomic is significantly faster than kmap/kunmap
 * because the kmap code must perform a global TLB invalidation when
 * the kmap pool wraps.
 *
 * Note that they may be slower than on x86 (etc.) because unlike on
 * those platforms, we do have to take a global lock to map and unmap
 * pages on Tile (see above).
 *
 * When holding an atomic kmap is is not legal to sleep, so atomic
 * kmaps are appropriate for short, tight code paths only.
 */
void *kmap_atomic_prot(struct page *page, enum km_type type, pgprot_t prot)
{
	enum fixed_addresses idx;
	unsigned long vaddr;
	pte_t *pte;

	/* even !CONFIG_PREEMPT needs this, for in_atomic in do_page_fault */
	pagefault_disable();

	/* Avoid icache flushes by disallowing atomic executable mappings. */
	BUG_ON(pte_exec(prot));

	if (!PageHighMem(page))
		return page_address(page);

	debug_kmap_atomic_prot(type);

	idx = type + KM_TYPE_NR*smp_processor_id();
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
	pte = kmap_get_pte(vaddr);
	BUG_ON(!pte_none(*pte));

	/* Register that this page is mapped atomically on this cpu. */
	kmap_atomic_register(page, type, vaddr, pte, mk_pte(page, prot));

	return (void *)vaddr;
}
EXPORT_SYMBOL(kmap_atomic_prot);

void *kmap_atomic(struct page *page, enum km_type type)
{
	/* PAGE_NONE is a magic value that tells us to check immutability. */
	return kmap_atomic_prot(page, type, PAGE_NONE);
}
EXPORT_SYMBOL(kmap_atomic);

void kunmap_atomic_notypecheck(void *kvaddr, enum km_type type)
{
	unsigned long vaddr = (unsigned long) kvaddr & PAGE_MASK;
	enum fixed_addresses idx = type + KM_TYPE_NR*smp_processor_id();

	/*
	 * Force other mappings to Oops if they try to access this pte without
	 * first remapping it.  Keeping stale mappings around is a bad idea.
	 */
	if (vaddr == __fix_to_virt(FIX_KMAP_BEGIN+idx)) {
		pte_t *pte = kmap_get_pte(vaddr);
		pte_t pteval = *pte;
		BUG_ON(!pte_present(pteval) && !pte_migrating(pteval));
		kmap_atomic_unregister(pte_page(pteval), vaddr);
		kpte_clear_flush(pte, vaddr);
	} else {
		/* Must be a lowmem page */
		BUG_ON(vaddr < PAGE_OFFSET);
		BUG_ON(vaddr >= (unsigned long)high_memory);
	}

	arch_flush_lazy_mmu_mode();
	pagefault_enable();
}
EXPORT_SYMBOL(kunmap_atomic_notypecheck);

/*
 * This API is supposed to allow us to map memory without a "struct page".
 * Currently we don't support this, though this may change in the future.
 */
void *kmap_atomic_pfn(unsigned long pfn, enum km_type type)
{
	return kmap_atomic(pfn_to_page(pfn), type);
}
void *kmap_atomic_prot_pfn(unsigned long pfn, enum km_type type, pgprot_t prot)
{
	return kmap_atomic_prot(pfn_to_page(pfn), type, prot);
}

struct page *kmap_atomic_to_page(void *ptr)
{
	pte_t *pte;
	unsigned long vaddr = (unsigned long)ptr;

	if (vaddr < FIXADDR_START)
		return virt_to_page(ptr);

	pte = kmap_get_pte(vaddr);
	return pte_page(*pte);
}
