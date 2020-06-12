// SPDX-License-Identifier: GPL-2.0
/*
 *  highmem.c: virtual kernel memory mappings for high memory
 *
 *  Provides kernel-static versions of atomic kmap functions originally
 *  found as inlines in include/asm-sparc/highmem.h.  These became
 *  needed as kmap_atomic() and kunmap_atomic() started getting
 *  called from within modules.
 *  -- Tomas Szepe <szepe@pinerecords.com>, September 2002
 *
 *  But kmap_atomic() and kunmap_atomic() cannot be inlined in
 *  modules because they are loaded with btfixup-ped functions.
 */

/*
 * The use of kmap_atomic/kunmap_atomic is discouraged - kmap/kunmap
 * gives a more generic (and caching) interface. But kmap_atomic can
 * be used in IRQ contexts, so in some (very limited) cases we need it.
 *
 * XXX This is an old text. Actually, it's good to use atomic kmaps,
 * provided you remember that they are atomic and not try to sleep
 * with a kmap taken, much like a spinlock. Non-atomic kmaps are
 * shared by CPUs, and so precious, and establishing them requires IPI.
 * Atomic kmaps are lightweight and we may have NCPUS more of them.
 */
#include <linux/highmem.h>
#include <linux/export.h>
#include <linux/mm.h>

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/pgalloc.h>
#include <asm/vaddrs.h>

static pte_t *kmap_pte;

void __init kmap_init(void)
{
	unsigned long address = __fix_to_virt(FIX_KMAP_BEGIN);

        /* cache the first kmap pte */
        kmap_pte = virt_to_kpte(address);
}

void *kmap_atomic_high_prot(struct page *page, pgprot_t prot)
{
	unsigned long vaddr;
	long idx, type;

	type = kmap_atomic_idx_push();
	idx = type + KM_TYPE_NR*smp_processor_id();
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);

/* XXX Fix - Anton */
#if 0
	__flush_cache_one(vaddr);
#else
	flush_cache_all();
#endif

#ifdef CONFIG_DEBUG_HIGHMEM
	BUG_ON(!pte_none(*(kmap_pte-idx)));
#endif
	set_pte(kmap_pte-idx, mk_pte(page, prot));
/* XXX Fix - Anton */
#if 0
	__flush_tlb_one(vaddr);
#else
	flush_tlb_all();
#endif

	return (void*) vaddr;
}
EXPORT_SYMBOL(kmap_atomic_high_prot);

void kunmap_atomic_high(void *kvaddr)
{
	unsigned long vaddr = (unsigned long) kvaddr & PAGE_MASK;
	int type;

	if (vaddr < FIXADDR_START)
		return;

	type = kmap_atomic_idx();

#ifdef CONFIG_DEBUG_HIGHMEM
	{
		unsigned long idx;

		idx = type + KM_TYPE_NR * smp_processor_id();
		BUG_ON(vaddr != __fix_to_virt(FIX_KMAP_BEGIN+idx));

		/* XXX Fix - Anton */
#if 0
		__flush_cache_one(vaddr);
#else
		flush_cache_all();
#endif

		/*
		 * force other mappings to Oops if they'll try to access
		 * this pte without first remap it
		 */
		pte_clear(&init_mm, vaddr, kmap_pte-idx);
		/* XXX Fix - Anton */
#if 0
		__flush_tlb_one(vaddr);
#else
		flush_tlb_all();
#endif
	}
#endif

	kmap_atomic_idx_pop();
}
EXPORT_SYMBOL(kunmap_atomic_high);
