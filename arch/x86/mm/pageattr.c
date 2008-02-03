/*
 * Copyright 2002 Andi Kleen, SuSE Labs.
 * Thanks to Ben LaHaise for precious feedback.
 */
#include <linux/highmem.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <asm/e820.h>
#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>

static inline int
within(unsigned long addr, unsigned long start, unsigned long end)
{
	return addr >= start && addr < end;
}

/*
 * Flushing functions
 */

/**
 * clflush_cache_range - flush a cache range with clflush
 * @addr:	virtual start address
 * @size:	number of bytes to flush
 *
 * clflush is an unordered instruction which needs fencing with mfence
 * to avoid ordering issues.
 */
void clflush_cache_range(void *vaddr, unsigned int size)
{
	void *vend = vaddr + size - 1;

	mb();

	for (; vaddr < vend; vaddr += boot_cpu_data.x86_clflush_size)
		clflush(vaddr);
	/*
	 * Flush any possible final partial cacheline:
	 */
	clflush(vend);

	mb();
}

static void __cpa_flush_all(void *arg)
{
	/*
	 * Flush all to work around Errata in early athlons regarding
	 * large page flushing.
	 */
	__flush_tlb_all();

	if (boot_cpu_data.x86_model >= 4)
		wbinvd();
}

static void cpa_flush_all(void)
{
	BUG_ON(irqs_disabled());

	on_each_cpu(__cpa_flush_all, NULL, 1, 1);
}

static void __cpa_flush_range(void *arg)
{
	/*
	 * We could optimize that further and do individual per page
	 * tlb invalidates for a low number of pages. Caveat: we must
	 * flush the high aliases on 64bit as well.
	 */
	__flush_tlb_all();
}

static void cpa_flush_range(unsigned long start, int numpages)
{
	unsigned int i, level;
	unsigned long addr;

	BUG_ON(irqs_disabled());
	WARN_ON(PAGE_ALIGN(start) != start);

	on_each_cpu(__cpa_flush_range, NULL, 1, 1);

	/*
	 * We only need to flush on one CPU,
	 * clflush is a MESI-coherent instruction that
	 * will cause all other CPUs to flush the same
	 * cachelines:
	 */
	for (i = 0, addr = start; i < numpages; i++, addr += PAGE_SIZE) {
		pte_t *pte = lookup_address(addr, &level);

		/*
		 * Only flush present addresses:
		 */
		if (pte && pte_present(*pte))
			clflush_cache_range((void *) addr, PAGE_SIZE);
	}
}

/*
 * Certain areas of memory on x86 require very specific protection flags,
 * for example the BIOS area or kernel text. Callers don't always get this
 * right (again, ioremap() on BIOS memory is not uncommon) so this function
 * checks and fixes these known static required protection bits.
 */
static inline pgprot_t static_protections(pgprot_t prot, unsigned long address)
{
	pgprot_t forbidden = __pgprot(0);

	/*
	 * The BIOS area between 640k and 1Mb needs to be executable for
	 * PCI BIOS based config access (CONFIG_PCI_GOBIOS) support.
	 */
	if (within(__pa(address), BIOS_BEGIN, BIOS_END))
		pgprot_val(forbidden) |= _PAGE_NX;

	/*
	 * The kernel text needs to be executable for obvious reasons
	 * Does not cover __inittext since that is gone later on
	 */
	if (within(address, (unsigned long)_text, (unsigned long)_etext))
		pgprot_val(forbidden) |= _PAGE_NX;

#ifdef CONFIG_DEBUG_RODATA
	/* The .rodata section needs to be read-only */
	if (within(address, (unsigned long)__start_rodata,
				(unsigned long)__end_rodata))
		pgprot_val(forbidden) |= _PAGE_RW;
#endif

	prot = __pgprot(pgprot_val(prot) & ~pgprot_val(forbidden));

	return prot;
}

pte_t *lookup_address(unsigned long address, int *level)
{
	pgd_t *pgd = pgd_offset_k(address);
	pud_t *pud;
	pmd_t *pmd;

	*level = PG_LEVEL_NONE;

	if (pgd_none(*pgd))
		return NULL;
	pud = pud_offset(pgd, address);
	if (pud_none(*pud))
		return NULL;
	pmd = pmd_offset(pud, address);
	if (pmd_none(*pmd))
		return NULL;

	*level = PG_LEVEL_2M;
	if (pmd_large(*pmd))
		return (pte_t *)pmd;

	*level = PG_LEVEL_4K;
	return pte_offset_kernel(pmd, address);
}

static void __set_pmd_pte(pte_t *kpte, unsigned long address, pte_t pte)
{
	/* change init_mm */
	set_pte_atomic(kpte, pte);
#ifdef CONFIG_X86_32
	if (!SHARED_KERNEL_PMD) {
		struct page *page;

		list_for_each_entry(page, &pgd_list, lru) {
			pgd_t *pgd;
			pud_t *pud;
			pmd_t *pmd;

			pgd = (pgd_t *)page_address(page) + pgd_index(address);
			pud = pud_offset(pgd, address);
			pmd = pmd_offset(pud, address);
			set_pte_atomic((pte_t *)pmd, pte);
		}
	}
#endif
}

static int split_large_page(pte_t *kpte, unsigned long address)
{
	pgprot_t ref_prot = pte_pgprot(pte_clrhuge(*kpte));
	gfp_t gfp_flags = GFP_KERNEL;
	unsigned long flags;
	unsigned long addr;
	pte_t *pbase, *tmp;
	struct page *base;
	unsigned int i, level;

#ifdef CONFIG_DEBUG_PAGEALLOC
	gfp_flags = __GFP_HIGH | __GFP_NOFAIL | __GFP_NOWARN;
	gfp_flags = GFP_ATOMIC | __GFP_NOWARN;
#endif
	base = alloc_pages(gfp_flags, 0);
	if (!base)
		return -ENOMEM;

	spin_lock_irqsave(&pgd_lock, flags);
	/*
	 * Check for races, another CPU might have split this page
	 * up for us already:
	 */
	tmp = lookup_address(address, &level);
	if (tmp != kpte) {
		WARN_ON_ONCE(1);
		goto out_unlock;
	}

	address = __pa(address);
	addr = address & LARGE_PAGE_MASK;
	pbase = (pte_t *)page_address(base);
#ifdef CONFIG_X86_32
	paravirt_alloc_pt(&init_mm, page_to_pfn(base));
#endif

	pgprot_val(ref_prot) &= ~_PAGE_NX;
	for (i = 0; i < PTRS_PER_PTE; i++, addr += PAGE_SIZE)
		set_pte(&pbase[i], pfn_pte(addr >> PAGE_SHIFT, ref_prot));

	/*
	 * Install the new, split up pagetable. Important detail here:
	 *
	 * On Intel the NX bit of all levels must be cleared to make a
	 * page executable. See section 4.13.2 of Intel 64 and IA-32
	 * Architectures Software Developer's Manual).
	 */
	ref_prot = pte_pgprot(pte_mkexec(pte_clrhuge(*kpte)));
	__set_pmd_pte(kpte, address, mk_pte(base, ref_prot));
	base = NULL;

out_unlock:
	spin_unlock_irqrestore(&pgd_lock, flags);

	if (base)
		__free_pages(base, 0);

	return 0;
}

static int
__change_page_attr(unsigned long address, unsigned long pfn,
		   pgprot_t mask_set, pgprot_t mask_clr)
{
	struct page *kpte_page;
	int level, err = 0;
	pte_t *kpte;

#ifdef CONFIG_X86_32
	BUG_ON(pfn > max_low_pfn);
#endif

repeat:
	kpte = lookup_address(address, &level);
	if (!kpte)
		return -EINVAL;

	kpte_page = virt_to_page(kpte);
	BUG_ON(PageLRU(kpte_page));
	BUG_ON(PageCompound(kpte_page));

	if (level == PG_LEVEL_4K) {
		pgprot_t new_prot = pte_pgprot(*kpte);
		pte_t new_pte, old_pte = *kpte;

		pgprot_val(new_prot) &= ~pgprot_val(mask_clr);
		pgprot_val(new_prot) |= pgprot_val(mask_set);

		new_prot = static_protections(new_prot, address);

		new_pte = pfn_pte(pfn, canon_pgprot(new_prot));
		BUG_ON(pte_pfn(new_pte) != pte_pfn(old_pte));

		set_pte_atomic(kpte, new_pte);
	} else {
		err = split_large_page(kpte, address);
		if (!err)
			goto repeat;
	}
	return err;
}

/**
 * change_page_attr_addr - Change page table attributes in linear mapping
 * @address: Virtual address in linear mapping.
 * @prot:    New page table attribute (PAGE_*)
 *
 * Change page attributes of a page in the direct mapping. This is a variant
 * of change_page_attr() that also works on memory holes that do not have
 * mem_map entry (pfn_valid() is false).
 *
 * See change_page_attr() documentation for more details.
 *
 * Modules and drivers should use the set_memory_* APIs instead.
 */

#define HIGH_MAP_START	__START_KERNEL_map
#define HIGH_MAP_END	(__START_KERNEL_map + KERNEL_TEXT_SIZE)

static int
change_page_attr_addr(unsigned long address, pgprot_t mask_set,
		      pgprot_t mask_clr)
{
	unsigned long phys_addr = __pa(address);
	unsigned long pfn = phys_addr >> PAGE_SHIFT;
	int err;

#ifdef CONFIG_X86_64
	/*
	 * If we are inside the high mapped kernel range, then we
	 * fixup the low mapping first. __va() returns the virtual
	 * address in the linear mapping:
	 */
	if (within(address, HIGH_MAP_START, HIGH_MAP_END))
		address = (unsigned long) __va(phys_addr);
#endif

	err = __change_page_attr(address, pfn, mask_set, mask_clr);
	if (err)
		return err;

#ifdef CONFIG_X86_64
	/*
	 * If the physical address is inside the kernel map, we need
	 * to touch the high mapped kernel as well:
	 */
	if (within(phys_addr, 0, KERNEL_TEXT_SIZE)) {
		/*
		 * Calc the high mapping address. See __phys_addr()
		 * for the non obvious details.
		 */
		address = phys_addr + HIGH_MAP_START - phys_base;
		/* Make sure the kernel mappings stay executable */
		pgprot_val(mask_clr) |= _PAGE_NX;

		/*
		 * Our high aliases are imprecise, because we check
		 * everything between 0 and KERNEL_TEXT_SIZE, so do
		 * not propagate lookup failures back to users:
		 */
		__change_page_attr(address, pfn, mask_set, mask_clr);
	}
#endif
	return err;
}

static int __change_page_attr_set_clr(unsigned long addr, int numpages,
				      pgprot_t mask_set, pgprot_t mask_clr)
{
	unsigned int i;
	int ret;

	for (i = 0; i < numpages ; i++, addr += PAGE_SIZE) {
		ret = change_page_attr_addr(addr, mask_set, mask_clr);
		if (ret)
			return ret;
	}

	return 0;
}

static int change_page_attr_set_clr(unsigned long addr, int numpages,
				    pgprot_t mask_set, pgprot_t mask_clr)
{
	int ret = __change_page_attr_set_clr(addr, numpages, mask_set,
					     mask_clr);

	/*
	 * On success we use clflush, when the CPU supports it to
	 * avoid the wbindv. If the CPU does not support it and in the
	 * error case we fall back to cpa_flush_all (which uses
	 * wbindv):
	 */
	if (!ret && cpu_has_clflush)
		cpa_flush_range(addr, numpages);
	else
		cpa_flush_all();

	return ret;
}

static inline int change_page_attr_set(unsigned long addr, int numpages,
				       pgprot_t mask)
{
	return change_page_attr_set_clr(addr, numpages, mask, __pgprot(0));
}

static inline int change_page_attr_clear(unsigned long addr, int numpages,
					 pgprot_t mask)
{
	return change_page_attr_set_clr(addr, numpages, __pgprot(0), mask);
}

int set_memory_uc(unsigned long addr, int numpages)
{
	return change_page_attr_set(addr, numpages,
				    __pgprot(_PAGE_PCD | _PAGE_PWT));
}
EXPORT_SYMBOL(set_memory_uc);

int set_memory_wb(unsigned long addr, int numpages)
{
	return change_page_attr_clear(addr, numpages,
				      __pgprot(_PAGE_PCD | _PAGE_PWT));
}
EXPORT_SYMBOL(set_memory_wb);

int set_memory_x(unsigned long addr, int numpages)
{
	return change_page_attr_clear(addr, numpages, __pgprot(_PAGE_NX));
}
EXPORT_SYMBOL(set_memory_x);

int set_memory_nx(unsigned long addr, int numpages)
{
	return change_page_attr_set(addr, numpages, __pgprot(_PAGE_NX));
}
EXPORT_SYMBOL(set_memory_nx);

int set_memory_ro(unsigned long addr, int numpages)
{
	return change_page_attr_clear(addr, numpages, __pgprot(_PAGE_RW));
}

int set_memory_rw(unsigned long addr, int numpages)
{
	return change_page_attr_set(addr, numpages, __pgprot(_PAGE_RW));
}

int set_memory_np(unsigned long addr, int numpages)
{
	return change_page_attr_clear(addr, numpages, __pgprot(_PAGE_PRESENT));
}

int set_pages_uc(struct page *page, int numpages)
{
	unsigned long addr = (unsigned long)page_address(page);

	return set_memory_uc(addr, numpages);
}
EXPORT_SYMBOL(set_pages_uc);

int set_pages_wb(struct page *page, int numpages)
{
	unsigned long addr = (unsigned long)page_address(page);

	return set_memory_wb(addr, numpages);
}
EXPORT_SYMBOL(set_pages_wb);

int set_pages_x(struct page *page, int numpages)
{
	unsigned long addr = (unsigned long)page_address(page);

	return set_memory_x(addr, numpages);
}
EXPORT_SYMBOL(set_pages_x);

int set_pages_nx(struct page *page, int numpages)
{
	unsigned long addr = (unsigned long)page_address(page);

	return set_memory_nx(addr, numpages);
}
EXPORT_SYMBOL(set_pages_nx);

int set_pages_ro(struct page *page, int numpages)
{
	unsigned long addr = (unsigned long)page_address(page);

	return set_memory_ro(addr, numpages);
}

int set_pages_rw(struct page *page, int numpages)
{
	unsigned long addr = (unsigned long)page_address(page);

	return set_memory_rw(addr, numpages);
}


#if defined(CONFIG_DEBUG_PAGEALLOC) || defined(CONFIG_CPA_DEBUG)
static inline int __change_page_attr_set(unsigned long addr, int numpages,
					 pgprot_t mask)
{
	return __change_page_attr_set_clr(addr, numpages, mask, __pgprot(0));
}

static inline int __change_page_attr_clear(unsigned long addr, int numpages,
					   pgprot_t mask)
{
	return __change_page_attr_set_clr(addr, numpages, __pgprot(0), mask);
}
#endif

#ifdef CONFIG_DEBUG_PAGEALLOC

static int __set_pages_p(struct page *page, int numpages)
{
	unsigned long addr = (unsigned long)page_address(page);

	return __change_page_attr_set(addr, numpages,
				      __pgprot(_PAGE_PRESENT | _PAGE_RW));
}

static int __set_pages_np(struct page *page, int numpages)
{
	unsigned long addr = (unsigned long)page_address(page);

	return __change_page_attr_clear(addr, numpages,
					__pgprot(_PAGE_PRESENT));
}

void kernel_map_pages(struct page *page, int numpages, int enable)
{
	if (PageHighMem(page))
		return;
	if (!enable) {
		debug_check_no_locks_freed(page_address(page),
					   numpages * PAGE_SIZE);
	}

	/*
	 * If page allocator is not up yet then do not call c_p_a():
	 */
	if (!debug_pagealloc_enabled)
		return;

	/*
	 * The return value is ignored - the calls cannot fail,
	 * large pages are disabled at boot time:
	 */
	if (enable)
		__set_pages_p(page, numpages);
	else
		__set_pages_np(page, numpages);

	/*
	 * We should perform an IPI and flush all tlbs,
	 * but that can deadlock->flush only current cpu:
	 */
	__flush_tlb_all();
}
#endif

/*
 * The testcases use internal knowledge of the implementation that shouldn't
 * be exposed to the rest of the kernel. Include these directly here.
 */
#ifdef CONFIG_CPA_DEBUG
#include "pageattr-test.c"
#endif
