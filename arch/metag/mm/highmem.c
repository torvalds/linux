#include <linux/export.h>
#include <linux/highmem.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <asm/fixmap.h>
#include <asm/tlbflush.h>

static pte_t *kmap_pte;

unsigned long highstart_pfn, highend_pfn;

void *kmap(struct page *page)
{
	might_sleep();
	if (!PageHighMem(page))
		return page_address(page);
	return kmap_high(page);
}
EXPORT_SYMBOL(kmap);

void kunmap(struct page *page)
{
	BUG_ON(in_interrupt());
	if (!PageHighMem(page))
		return;
	kunmap_high(page);
}
EXPORT_SYMBOL(kunmap);

/*
 * kmap_atomic/kunmap_atomic is significantly faster than kmap/kunmap because
 * no global lock is needed and because the kmap code must perform a global TLB
 * invalidation when the kmap pool wraps.
 *
 * However when holding an atomic kmap is is not legal to sleep, so atomic
 * kmaps are appropriate for short, tight code paths only.
 */

void *kmap_atomic(struct page *page)
{
	enum fixed_addresses idx;
	unsigned long vaddr;
	int type;

	preempt_disable();
	pagefault_disable();
	if (!PageHighMem(page))
		return page_address(page);

	type = kmap_atomic_idx_push();
	idx = type + KM_TYPE_NR * smp_processor_id();
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
#ifdef CONFIG_DEBUG_HIGHMEM
	BUG_ON(!pte_none(*(kmap_pte - idx)));
#endif
	set_pte(kmap_pte - idx, mk_pte(page, PAGE_KERNEL));

	return (void *)vaddr;
}
EXPORT_SYMBOL(kmap_atomic);

void __kunmap_atomic(void *kvaddr)
{
	unsigned long vaddr = (unsigned long) kvaddr & PAGE_MASK;
	int idx, type;

	if (kvaddr >= (void *)FIXADDR_START) {
		type = kmap_atomic_idx();
		idx = type + KM_TYPE_NR * smp_processor_id();

		/*
		 * Force other mappings to Oops if they'll try to access this
		 * pte without first remap it.  Keeping stale mappings around
		 * is a bad idea also, in case the page changes cacheability
		 * attributes or becomes a protected page in a hypervisor.
		 */
		pte_clear(&init_mm, vaddr, kmap_pte-idx);
		flush_tlb_kernel_range(vaddr, vaddr + PAGE_SIZE);

		kmap_atomic_idx_pop();
	}

	pagefault_enable();
	preempt_enable();
}
EXPORT_SYMBOL(__kunmap_atomic);

/*
 * This is the same as kmap_atomic() but can map memory that doesn't
 * have a struct page associated with it.
 */
void *kmap_atomic_pfn(unsigned long pfn)
{
	enum fixed_addresses idx;
	unsigned long vaddr;
	int type;

	preempt_disable();
	pagefault_disable();

	type = kmap_atomic_idx_push();
	idx = type + KM_TYPE_NR * smp_processor_id();
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
#ifdef CONFIG_DEBUG_HIGHMEM
	BUG_ON(!pte_none(*(kmap_pte - idx)));
#endif
	set_pte(kmap_pte - idx, pfn_pte(pfn, PAGE_KERNEL));
	flush_tlb_kernel_range(vaddr, vaddr + PAGE_SIZE);

	return (void *)vaddr;
}

void __init kmap_init(void)
{
	unsigned long kmap_vstart;

	/* cache the first kmap pte */
	kmap_vstart = __fix_to_virt(FIX_KMAP_BEGIN);
	kmap_pte = kmap_get_fixmap_pte(kmap_vstart);
}
