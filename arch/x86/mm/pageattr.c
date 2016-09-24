/*
 * Copyright 2002 Andi Kleen, SuSE Labs.
 * Thanks to Ben LaHaise for precious feedback.
 */
#include <linux/highmem.h>
#include <linux/bootmem.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/pfn.h>
#include <linux/percpu.h>
#include <linux/gfp.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>

#include <asm/e820.h>
#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/proto.h>
#include <asm/pat.h>

/*
 * The current flushing context - we pass it instead of 5 arguments:
 */
struct cpa_data {
	unsigned long	*vaddr;
	pgd_t		*pgd;
	pgprot_t	mask_set;
	pgprot_t	mask_clr;
	unsigned long	numpages;
	int		flags;
	unsigned long	pfn;
	unsigned	force_split : 1;
	int		curpage;
	struct page	**pages;
};

/*
 * Serialize cpa() (for !DEBUG_PAGEALLOC which uses large identity mappings)
 * using cpa_lock. So that we don't allow any other cpu, with stale large tlb
 * entries change the page attribute in parallel to some other cpu
 * splitting a large page entry along with changing the attribute.
 */
static DEFINE_SPINLOCK(cpa_lock);

#define CPA_FLUSHTLB 1
#define CPA_ARRAY 2
#define CPA_PAGES_ARRAY 4

#ifdef CONFIG_PROC_FS
static unsigned long direct_pages_count[PG_LEVEL_NUM];

void update_page_count(int level, unsigned long pages)
{
	/* Protect against CPA */
	spin_lock(&pgd_lock);
	direct_pages_count[level] += pages;
	spin_unlock(&pgd_lock);
}

static void split_page_count(int level)
{
	if (direct_pages_count[level] == 0)
		return;

	direct_pages_count[level]--;
	direct_pages_count[level - 1] += PTRS_PER_PTE;
}

void arch_report_meminfo(struct seq_file *m)
{
	seq_printf(m, "DirectMap4k:    %8lu kB\n",
			direct_pages_count[PG_LEVEL_4K] << 2);
#if defined(CONFIG_X86_64) || defined(CONFIG_X86_PAE)
	seq_printf(m, "DirectMap2M:    %8lu kB\n",
			direct_pages_count[PG_LEVEL_2M] << 11);
#else
	seq_printf(m, "DirectMap4M:    %8lu kB\n",
			direct_pages_count[PG_LEVEL_2M] << 12);
#endif
	if (direct_gbpages)
		seq_printf(m, "DirectMap1G:    %8lu kB\n",
			direct_pages_count[PG_LEVEL_1G] << 20);
}
#else
static inline void split_page_count(int level) { }
#endif

#ifdef CONFIG_X86_64

static inline unsigned long highmap_start_pfn(void)
{
	return __pa_symbol(_text) >> PAGE_SHIFT;
}

static inline unsigned long highmap_end_pfn(void)
{
	/* Do not reference physical address outside the kernel. */
	return __pa_symbol(roundup(_brk_end, PMD_SIZE) - 1) >> PAGE_SHIFT;
}

#endif

static inline int
within(unsigned long addr, unsigned long start, unsigned long end)
{
	return addr >= start && addr < end;
}

static inline int
within_inclusive(unsigned long addr, unsigned long start, unsigned long end)
{
	return addr >= start && addr <= end;
}

/*
 * Flushing functions
 */

/**
 * clflush_cache_range - flush a cache range with clflush
 * @vaddr:	virtual start address
 * @size:	number of bytes to flush
 *
 * clflushopt is an unordered instruction which needs fencing with mfence or
 * sfence to avoid ordering issues.
 */
void clflush_cache_range(void *vaddr, unsigned int size)
{
	const unsigned long clflush_size = boot_cpu_data.x86_clflush_size;
	void *p = (void *)((unsigned long)vaddr & ~(clflush_size - 1));
	void *vend = vaddr + size;

	if (p >= vend)
		return;

	mb();

	for (; p < vend; p += clflush_size)
		clflushopt(p);

	mb();
}
EXPORT_SYMBOL_GPL(clflush_cache_range);

static void __cpa_flush_all(void *arg)
{
	unsigned long cache = (unsigned long)arg;

	/*
	 * Flush all to work around Errata in early athlons regarding
	 * large page flushing.
	 */
	__flush_tlb_all();

	if (cache && boot_cpu_data.x86 >= 4)
		wbinvd();
}

static void cpa_flush_all(unsigned long cache)
{
	BUG_ON(irqs_disabled());

	on_each_cpu(__cpa_flush_all, (void *) cache, 1);
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

static void cpa_flush_range(unsigned long start, int numpages, int cache)
{
	unsigned int i, level;
	unsigned long addr;

	BUG_ON(irqs_disabled());
	WARN_ON(PAGE_ALIGN(start) != start);

	on_each_cpu(__cpa_flush_range, NULL, 1);

	if (!cache)
		return;

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
		if (pte && (pte_val(*pte) & _PAGE_PRESENT))
			clflush_cache_range((void *) addr, PAGE_SIZE);
	}
}

static void cpa_flush_array(unsigned long *start, int numpages, int cache,
			    int in_flags, struct page **pages)
{
	unsigned int i, level;
	unsigned long do_wbinvd = cache && numpages >= 1024; /* 4M threshold */

	BUG_ON(irqs_disabled());

	on_each_cpu(__cpa_flush_all, (void *) do_wbinvd, 1);

	if (!cache || do_wbinvd)
		return;

	/*
	 * We only need to flush on one CPU,
	 * clflush is a MESI-coherent instruction that
	 * will cause all other CPUs to flush the same
	 * cachelines:
	 */
	for (i = 0; i < numpages; i++) {
		unsigned long addr;
		pte_t *pte;

		if (in_flags & CPA_PAGES_ARRAY)
			addr = (unsigned long)page_address(pages[i]);
		else
			addr = start[i];

		pte = lookup_address(addr, &level);

		/*
		 * Only flush present addresses:
		 */
		if (pte && (pte_val(*pte) & _PAGE_PRESENT))
			clflush_cache_range((void *)addr, PAGE_SIZE);
	}
}

/*
 * Certain areas of memory on x86 require very specific protection flags,
 * for example the BIOS area or kernel text. Callers don't always get this
 * right (again, ioremap() on BIOS memory is not uncommon) so this function
 * checks and fixes these known static required protection bits.
 */
static inline pgprot_t static_protections(pgprot_t prot, unsigned long address,
				   unsigned long pfn)
{
	pgprot_t forbidden = __pgprot(0);

	/*
	 * The BIOS area between 640k and 1Mb needs to be executable for
	 * PCI BIOS based config access (CONFIG_PCI_GOBIOS) support.
	 */
#ifdef CONFIG_PCI_BIOS
	if (pcibios_enabled && within(pfn, BIOS_BEGIN >> PAGE_SHIFT, BIOS_END >> PAGE_SHIFT))
		pgprot_val(forbidden) |= _PAGE_NX;
#endif

	/*
	 * The kernel text needs to be executable for obvious reasons
	 * Does not cover __inittext since that is gone later on. On
	 * 64bit we do not enforce !NX on the low mapping
	 */
	if (within(address, (unsigned long)_text, (unsigned long)_etext))
		pgprot_val(forbidden) |= _PAGE_NX;

	/*
	 * The .rodata section needs to be read-only. Using the pfn
	 * catches all aliases.
	 */
	if (within(pfn, __pa_symbol(__start_rodata) >> PAGE_SHIFT,
		   __pa_symbol(__end_rodata) >> PAGE_SHIFT))
		pgprot_val(forbidden) |= _PAGE_RW;

#if defined(CONFIG_X86_64)
	/*
	 * Once the kernel maps the text as RO (kernel_set_to_readonly is set),
	 * kernel text mappings for the large page aligned text, rodata sections
	 * will be always read-only. For the kernel identity mappings covering
	 * the holes caused by this alignment can be anything that user asks.
	 *
	 * This will preserve the large page mappings for kernel text/data
	 * at no extra cost.
	 */
	if (kernel_set_to_readonly &&
	    within(address, (unsigned long)_text,
		   (unsigned long)__end_rodata_hpage_align)) {
		unsigned int level;

		/*
		 * Don't enforce the !RW mapping for the kernel text mapping,
		 * if the current mapping is already using small page mapping.
		 * No need to work hard to preserve large page mappings in this
		 * case.
		 *
		 * This also fixes the Linux Xen paravirt guest boot failure
		 * (because of unexpected read-only mappings for kernel identity
		 * mappings). In this paravirt guest case, the kernel text
		 * mapping and the kernel identity mapping share the same
		 * page-table pages. Thus we can't really use different
		 * protections for the kernel text and identity mappings. Also,
		 * these shared mappings are made of small page mappings.
		 * Thus this don't enforce !RW mapping for small page kernel
		 * text mapping logic will help Linux Xen parvirt guest boot
		 * as well.
		 */
		if (lookup_address(address, &level) && (level != PG_LEVEL_4K))
			pgprot_val(forbidden) |= _PAGE_RW;
	}
#endif

	prot = __pgprot(pgprot_val(prot) & ~pgprot_val(forbidden));

	return prot;
}

/*
 * Lookup the page table entry for a virtual address in a specific pgd.
 * Return a pointer to the entry and the level of the mapping.
 */
pte_t *lookup_address_in_pgd(pgd_t *pgd, unsigned long address,
			     unsigned int *level)
{
	pud_t *pud;
	pmd_t *pmd;

	*level = PG_LEVEL_NONE;

	if (pgd_none(*pgd))
		return NULL;

	pud = pud_offset(pgd, address);
	if (pud_none(*pud))
		return NULL;

	*level = PG_LEVEL_1G;
	if (pud_large(*pud) || !pud_present(*pud))
		return (pte_t *)pud;

	pmd = pmd_offset(pud, address);
	if (pmd_none(*pmd))
		return NULL;

	*level = PG_LEVEL_2M;
	if (pmd_large(*pmd) || !pmd_present(*pmd))
		return (pte_t *)pmd;

	*level = PG_LEVEL_4K;

	return pte_offset_kernel(pmd, address);
}

/*
 * Lookup the page table entry for a virtual address. Return a pointer
 * to the entry and the level of the mapping.
 *
 * Note: We return pud and pmd either when the entry is marked large
 * or when the present bit is not set. Otherwise we would return a
 * pointer to a nonexisting mapping.
 */
pte_t *lookup_address(unsigned long address, unsigned int *level)
{
        return lookup_address_in_pgd(pgd_offset_k(address), address, level);
}
EXPORT_SYMBOL_GPL(lookup_address);

static pte_t *_lookup_address_cpa(struct cpa_data *cpa, unsigned long address,
				  unsigned int *level)
{
        if (cpa->pgd)
		return lookup_address_in_pgd(cpa->pgd + pgd_index(address),
					       address, level);

        return lookup_address(address, level);
}

/*
 * Lookup the PMD entry for a virtual address. Return a pointer to the entry
 * or NULL if not present.
 */
pmd_t *lookup_pmd_address(unsigned long address)
{
	pgd_t *pgd;
	pud_t *pud;

	pgd = pgd_offset_k(address);
	if (pgd_none(*pgd))
		return NULL;

	pud = pud_offset(pgd, address);
	if (pud_none(*pud) || pud_large(*pud) || !pud_present(*pud))
		return NULL;

	return pmd_offset(pud, address);
}

/*
 * This is necessary because __pa() does not work on some
 * kinds of memory, like vmalloc() or the alloc_remap()
 * areas on 32-bit NUMA systems.  The percpu areas can
 * end up in this kind of memory, for instance.
 *
 * This could be optimized, but it is only intended to be
 * used at inititalization time, and keeping it
 * unoptimized should increase the testing coverage for
 * the more obscure platforms.
 */
phys_addr_t slow_virt_to_phys(void *__virt_addr)
{
	unsigned long virt_addr = (unsigned long)__virt_addr;
	phys_addr_t phys_addr;
	unsigned long offset;
	enum pg_level level;
	pte_t *pte;

	pte = lookup_address(virt_addr, &level);
	BUG_ON(!pte);

	/*
	 * pXX_pfn() returns unsigned long, which must be cast to phys_addr_t
	 * before being left-shifted PAGE_SHIFT bits -- this trick is to
	 * make 32-PAE kernel work correctly.
	 */
	switch (level) {
	case PG_LEVEL_1G:
		phys_addr = (phys_addr_t)pud_pfn(*(pud_t *)pte) << PAGE_SHIFT;
		offset = virt_addr & ~PUD_PAGE_MASK;
		break;
	case PG_LEVEL_2M:
		phys_addr = (phys_addr_t)pmd_pfn(*(pmd_t *)pte) << PAGE_SHIFT;
		offset = virt_addr & ~PMD_PAGE_MASK;
		break;
	default:
		phys_addr = (phys_addr_t)pte_pfn(*pte) << PAGE_SHIFT;
		offset = virt_addr & ~PAGE_MASK;
	}

	return (phys_addr_t)(phys_addr | offset);
}
EXPORT_SYMBOL_GPL(slow_virt_to_phys);

/*
 * Set the new pmd in all the pgds we know about:
 */
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

static int
try_preserve_large_page(pte_t *kpte, unsigned long address,
			struct cpa_data *cpa)
{
	unsigned long nextpage_addr, numpages, pmask, psize, addr, pfn, old_pfn;
	pte_t new_pte, old_pte, *tmp;
	pgprot_t old_prot, new_prot, req_prot;
	int i, do_split = 1;
	enum pg_level level;

	if (cpa->force_split)
		return 1;

	spin_lock(&pgd_lock);
	/*
	 * Check for races, another CPU might have split this page
	 * up already:
	 */
	tmp = _lookup_address_cpa(cpa, address, &level);
	if (tmp != kpte)
		goto out_unlock;

	switch (level) {
	case PG_LEVEL_2M:
		old_prot = pmd_pgprot(*(pmd_t *)kpte);
		old_pfn = pmd_pfn(*(pmd_t *)kpte);
		break;
	case PG_LEVEL_1G:
		old_prot = pud_pgprot(*(pud_t *)kpte);
		old_pfn = pud_pfn(*(pud_t *)kpte);
		break;
	default:
		do_split = -EINVAL;
		goto out_unlock;
	}

	psize = page_level_size(level);
	pmask = page_level_mask(level);

	/*
	 * Calculate the number of pages, which fit into this large
	 * page starting at address:
	 */
	nextpage_addr = (address + psize) & pmask;
	numpages = (nextpage_addr - address) >> PAGE_SHIFT;
	if (numpages < cpa->numpages)
		cpa->numpages = numpages;

	/*
	 * We are safe now. Check whether the new pgprot is the same:
	 * Convert protection attributes to 4k-format, as cpa->mask* are set
	 * up accordingly.
	 */
	old_pte = *kpte;
	req_prot = pgprot_large_2_4k(old_prot);

	pgprot_val(req_prot) &= ~pgprot_val(cpa->mask_clr);
	pgprot_val(req_prot) |= pgprot_val(cpa->mask_set);

	/*
	 * req_prot is in format of 4k pages. It must be converted to large
	 * page format: the caching mode includes the PAT bit located at
	 * different bit positions in the two formats.
	 */
	req_prot = pgprot_4k_2_large(req_prot);

	/*
	 * Set the PSE and GLOBAL flags only if the PRESENT flag is
	 * set otherwise pmd_present/pmd_huge will return true even on
	 * a non present pmd. The canon_pgprot will clear _PAGE_GLOBAL
	 * for the ancient hardware that doesn't support it.
	 */
	if (pgprot_val(req_prot) & _PAGE_PRESENT)
		pgprot_val(req_prot) |= _PAGE_PSE | _PAGE_GLOBAL;
	else
		pgprot_val(req_prot) &= ~(_PAGE_PSE | _PAGE_GLOBAL);

	req_prot = canon_pgprot(req_prot);

	/*
	 * old_pfn points to the large page base pfn. So we need
	 * to add the offset of the virtual address:
	 */
	pfn = old_pfn + ((address & (psize - 1)) >> PAGE_SHIFT);
	cpa->pfn = pfn;

	new_prot = static_protections(req_prot, address, pfn);

	/*
	 * We need to check the full range, whether
	 * static_protection() requires a different pgprot for one of
	 * the pages in the range we try to preserve:
	 */
	addr = address & pmask;
	pfn = old_pfn;
	for (i = 0; i < (psize >> PAGE_SHIFT); i++, addr += PAGE_SIZE, pfn++) {
		pgprot_t chk_prot = static_protections(req_prot, addr, pfn);

		if (pgprot_val(chk_prot) != pgprot_val(new_prot))
			goto out_unlock;
	}

	/*
	 * If there are no changes, return. maxpages has been updated
	 * above:
	 */
	if (pgprot_val(new_prot) == pgprot_val(old_prot)) {
		do_split = 0;
		goto out_unlock;
	}

	/*
	 * We need to change the attributes. Check, whether we can
	 * change the large page in one go. We request a split, when
	 * the address is not aligned and the number of pages is
	 * smaller than the number of pages in the large page. Note
	 * that we limited the number of possible pages already to
	 * the number of pages in the large page.
	 */
	if (address == (address & pmask) && cpa->numpages == (psize >> PAGE_SHIFT)) {
		/*
		 * The address is aligned and the number of pages
		 * covers the full page.
		 */
		new_pte = pfn_pte(old_pfn, new_prot);
		__set_pmd_pte(kpte, address, new_pte);
		cpa->flags |= CPA_FLUSHTLB;
		do_split = 0;
	}

out_unlock:
	spin_unlock(&pgd_lock);

	return do_split;
}

static int
__split_large_page(struct cpa_data *cpa, pte_t *kpte, unsigned long address,
		   struct page *base)
{
	pte_t *pbase = (pte_t *)page_address(base);
	unsigned long ref_pfn, pfn, pfninc = 1;
	unsigned int i, level;
	pte_t *tmp;
	pgprot_t ref_prot;

	spin_lock(&pgd_lock);
	/*
	 * Check for races, another CPU might have split this page
	 * up for us already:
	 */
	tmp = _lookup_address_cpa(cpa, address, &level);
	if (tmp != kpte) {
		spin_unlock(&pgd_lock);
		return 1;
	}

	paravirt_alloc_pte(&init_mm, page_to_pfn(base));

	switch (level) {
	case PG_LEVEL_2M:
		ref_prot = pmd_pgprot(*(pmd_t *)kpte);
		/* clear PSE and promote PAT bit to correct position */
		ref_prot = pgprot_large_2_4k(ref_prot);
		ref_pfn = pmd_pfn(*(pmd_t *)kpte);
		break;

	case PG_LEVEL_1G:
		ref_prot = pud_pgprot(*(pud_t *)kpte);
		ref_pfn = pud_pfn(*(pud_t *)kpte);
		pfninc = PMD_PAGE_SIZE >> PAGE_SHIFT;

		/*
		 * Clear the PSE flags if the PRESENT flag is not set
		 * otherwise pmd_present/pmd_huge will return true
		 * even on a non present pmd.
		 */
		if (!(pgprot_val(ref_prot) & _PAGE_PRESENT))
			pgprot_val(ref_prot) &= ~_PAGE_PSE;
		break;

	default:
		spin_unlock(&pgd_lock);
		return 1;
	}

	/*
	 * Set the GLOBAL flags only if the PRESENT flag is set
	 * otherwise pmd/pte_present will return true even on a non
	 * present pmd/pte. The canon_pgprot will clear _PAGE_GLOBAL
	 * for the ancient hardware that doesn't support it.
	 */
	if (pgprot_val(ref_prot) & _PAGE_PRESENT)
		pgprot_val(ref_prot) |= _PAGE_GLOBAL;
	else
		pgprot_val(ref_prot) &= ~_PAGE_GLOBAL;

	/*
	 * Get the target pfn from the original entry:
	 */
	pfn = ref_pfn;
	for (i = 0; i < PTRS_PER_PTE; i++, pfn += pfninc)
		set_pte(&pbase[i], pfn_pte(pfn, canon_pgprot(ref_prot)));

	if (virt_addr_valid(address)) {
		unsigned long pfn = PFN_DOWN(__pa(address));

		if (pfn_range_is_mapped(pfn, pfn + 1))
			split_page_count(level);
	}

	/*
	 * Install the new, split up pagetable.
	 *
	 * We use the standard kernel pagetable protections for the new
	 * pagetable protections, the actual ptes set above control the
	 * primary protection behavior:
	 */
	__set_pmd_pte(kpte, address, mk_pte(base, __pgprot(_KERNPG_TABLE)));

	/*
	 * Intel Atom errata AAH41 workaround.
	 *
	 * The real fix should be in hw or in a microcode update, but
	 * we also probabilistically try to reduce the window of having
	 * a large TLB mixed with 4K TLBs while instruction fetches are
	 * going on.
	 */
	__flush_tlb_all();
	spin_unlock(&pgd_lock);

	return 0;
}

static int split_large_page(struct cpa_data *cpa, pte_t *kpte,
			    unsigned long address)
{
	struct page *base;

	if (!debug_pagealloc_enabled())
		spin_unlock(&cpa_lock);
	base = alloc_pages(GFP_KERNEL | __GFP_NOTRACK, 0);
	if (!debug_pagealloc_enabled())
		spin_lock(&cpa_lock);
	if (!base)
		return -ENOMEM;

	if (__split_large_page(cpa, kpte, address, base))
		__free_page(base);

	return 0;
}

static bool try_to_free_pte_page(pte_t *pte)
{
	int i;

	for (i = 0; i < PTRS_PER_PTE; i++)
		if (!pte_none(pte[i]))
			return false;

	free_page((unsigned long)pte);
	return true;
}

static bool try_to_free_pmd_page(pmd_t *pmd)
{
	int i;

	for (i = 0; i < PTRS_PER_PMD; i++)
		if (!pmd_none(pmd[i]))
			return false;

	free_page((unsigned long)pmd);
	return true;
}

static bool unmap_pte_range(pmd_t *pmd, unsigned long start, unsigned long end)
{
	pte_t *pte = pte_offset_kernel(pmd, start);

	while (start < end) {
		set_pte(pte, __pte(0));

		start += PAGE_SIZE;
		pte++;
	}

	if (try_to_free_pte_page((pte_t *)pmd_page_vaddr(*pmd))) {
		pmd_clear(pmd);
		return true;
	}
	return false;
}

static void __unmap_pmd_range(pud_t *pud, pmd_t *pmd,
			      unsigned long start, unsigned long end)
{
	if (unmap_pte_range(pmd, start, end))
		if (try_to_free_pmd_page((pmd_t *)pud_page_vaddr(*pud)))
			pud_clear(pud);
}

static void unmap_pmd_range(pud_t *pud, unsigned long start, unsigned long end)
{
	pmd_t *pmd = pmd_offset(pud, start);

	/*
	 * Not on a 2MB page boundary?
	 */
	if (start & (PMD_SIZE - 1)) {
		unsigned long next_page = (start + PMD_SIZE) & PMD_MASK;
		unsigned long pre_end = min_t(unsigned long, end, next_page);

		__unmap_pmd_range(pud, pmd, start, pre_end);

		start = pre_end;
		pmd++;
	}

	/*
	 * Try to unmap in 2M chunks.
	 */
	while (end - start >= PMD_SIZE) {
		if (pmd_large(*pmd))
			pmd_clear(pmd);
		else
			__unmap_pmd_range(pud, pmd, start, start + PMD_SIZE);

		start += PMD_SIZE;
		pmd++;
	}

	/*
	 * 4K leftovers?
	 */
	if (start < end)
		return __unmap_pmd_range(pud, pmd, start, end);

	/*
	 * Try again to free the PMD page if haven't succeeded above.
	 */
	if (!pud_none(*pud))
		if (try_to_free_pmd_page((pmd_t *)pud_page_vaddr(*pud)))
			pud_clear(pud);
}

static void unmap_pud_range(pgd_t *pgd, unsigned long start, unsigned long end)
{
	pud_t *pud = pud_offset(pgd, start);

	/*
	 * Not on a GB page boundary?
	 */
	if (start & (PUD_SIZE - 1)) {
		unsigned long next_page = (start + PUD_SIZE) & PUD_MASK;
		unsigned long pre_end	= min_t(unsigned long, end, next_page);

		unmap_pmd_range(pud, start, pre_end);

		start = pre_end;
		pud++;
	}

	/*
	 * Try to unmap in 1G chunks?
	 */
	while (end - start >= PUD_SIZE) {

		if (pud_large(*pud))
			pud_clear(pud);
		else
			unmap_pmd_range(pud, start, start + PUD_SIZE);

		start += PUD_SIZE;
		pud++;
	}

	/*
	 * 2M leftovers?
	 */
	if (start < end)
		unmap_pmd_range(pud, start, end);

	/*
	 * No need to try to free the PUD page because we'll free it in
	 * populate_pgd's error path
	 */
}

static int alloc_pte_page(pmd_t *pmd)
{
	pte_t *pte = (pte_t *)get_zeroed_page(GFP_KERNEL | __GFP_NOTRACK);
	if (!pte)
		return -1;

	set_pmd(pmd, __pmd(__pa(pte) | _KERNPG_TABLE));
	return 0;
}

static int alloc_pmd_page(pud_t *pud)
{
	pmd_t *pmd = (pmd_t *)get_zeroed_page(GFP_KERNEL | __GFP_NOTRACK);
	if (!pmd)
		return -1;

	set_pud(pud, __pud(__pa(pmd) | _KERNPG_TABLE));
	return 0;
}

static void populate_pte(struct cpa_data *cpa,
			 unsigned long start, unsigned long end,
			 unsigned num_pages, pmd_t *pmd, pgprot_t pgprot)
{
	pte_t *pte;

	pte = pte_offset_kernel(pmd, start);

	/*
	 * Set the GLOBAL flags only if the PRESENT flag is
	 * set otherwise pte_present will return true even on
	 * a non present pte. The canon_pgprot will clear
	 * _PAGE_GLOBAL for the ancient hardware that doesn't
	 * support it.
	 */
	if (pgprot_val(pgprot) & _PAGE_PRESENT)
		pgprot_val(pgprot) |= _PAGE_GLOBAL;
	else
		pgprot_val(pgprot) &= ~_PAGE_GLOBAL;

	pgprot = canon_pgprot(pgprot);

	while (num_pages-- && start < end) {
		set_pte(pte, pfn_pte(cpa->pfn, pgprot));

		start	 += PAGE_SIZE;
		cpa->pfn++;
		pte++;
	}
}

static int populate_pmd(struct cpa_data *cpa,
			unsigned long start, unsigned long end,
			unsigned num_pages, pud_t *pud, pgprot_t pgprot)
{
	unsigned int cur_pages = 0;
	pmd_t *pmd;
	pgprot_t pmd_pgprot;

	/*
	 * Not on a 2M boundary?
	 */
	if (start & (PMD_SIZE - 1)) {
		unsigned long pre_end = start + (num_pages << PAGE_SHIFT);
		unsigned long next_page = (start + PMD_SIZE) & PMD_MASK;

		pre_end   = min_t(unsigned long, pre_end, next_page);
		cur_pages = (pre_end - start) >> PAGE_SHIFT;
		cur_pages = min_t(unsigned int, num_pages, cur_pages);

		/*
		 * Need a PTE page?
		 */
		pmd = pmd_offset(pud, start);
		if (pmd_none(*pmd))
			if (alloc_pte_page(pmd))
				return -1;

		populate_pte(cpa, start, pre_end, cur_pages, pmd, pgprot);

		start = pre_end;
	}

	/*
	 * We mapped them all?
	 */
	if (num_pages == cur_pages)
		return cur_pages;

	pmd_pgprot = pgprot_4k_2_large(pgprot);

	while (end - start >= PMD_SIZE) {

		/*
		 * We cannot use a 1G page so allocate a PMD page if needed.
		 */
		if (pud_none(*pud))
			if (alloc_pmd_page(pud))
				return -1;

		pmd = pmd_offset(pud, start);

		set_pmd(pmd, __pmd(cpa->pfn << PAGE_SHIFT | _PAGE_PSE |
				   massage_pgprot(pmd_pgprot)));

		start	  += PMD_SIZE;
		cpa->pfn  += PMD_SIZE >> PAGE_SHIFT;
		cur_pages += PMD_SIZE >> PAGE_SHIFT;
	}

	/*
	 * Map trailing 4K pages.
	 */
	if (start < end) {
		pmd = pmd_offset(pud, start);
		if (pmd_none(*pmd))
			if (alloc_pte_page(pmd))
				return -1;

		populate_pte(cpa, start, end, num_pages - cur_pages,
			     pmd, pgprot);
	}
	return num_pages;
}

static int populate_pud(struct cpa_data *cpa, unsigned long start, pgd_t *pgd,
			pgprot_t pgprot)
{
	pud_t *pud;
	unsigned long end;
	int cur_pages = 0;
	pgprot_t pud_pgprot;

	end = start + (cpa->numpages << PAGE_SHIFT);

	/*
	 * Not on a Gb page boundary? => map everything up to it with
	 * smaller pages.
	 */
	if (start & (PUD_SIZE - 1)) {
		unsigned long pre_end;
		unsigned long next_page = (start + PUD_SIZE) & PUD_MASK;

		pre_end   = min_t(unsigned long, end, next_page);
		cur_pages = (pre_end - start) >> PAGE_SHIFT;
		cur_pages = min_t(int, (int)cpa->numpages, cur_pages);

		pud = pud_offset(pgd, start);

		/*
		 * Need a PMD page?
		 */
		if (pud_none(*pud))
			if (alloc_pmd_page(pud))
				return -1;

		cur_pages = populate_pmd(cpa, start, pre_end, cur_pages,
					 pud, pgprot);
		if (cur_pages < 0)
			return cur_pages;

		start = pre_end;
	}

	/* We mapped them all? */
	if (cpa->numpages == cur_pages)
		return cur_pages;

	pud = pud_offset(pgd, start);
	pud_pgprot = pgprot_4k_2_large(pgprot);

	/*
	 * Map everything starting from the Gb boundary, possibly with 1G pages
	 */
	while (boot_cpu_has(X86_FEATURE_GBPAGES) && end - start >= PUD_SIZE) {
		set_pud(pud, __pud(cpa->pfn << PAGE_SHIFT | _PAGE_PSE |
				   massage_pgprot(pud_pgprot)));

		start	  += PUD_SIZE;
		cpa->pfn  += PUD_SIZE >> PAGE_SHIFT;
		cur_pages += PUD_SIZE >> PAGE_SHIFT;
		pud++;
	}

	/* Map trailing leftover */
	if (start < end) {
		int tmp;

		pud = pud_offset(pgd, start);
		if (pud_none(*pud))
			if (alloc_pmd_page(pud))
				return -1;

		tmp = populate_pmd(cpa, start, end, cpa->numpages - cur_pages,
				   pud, pgprot);
		if (tmp < 0)
			return cur_pages;

		cur_pages += tmp;
	}
	return cur_pages;
}

/*
 * Restrictions for kernel page table do not necessarily apply when mapping in
 * an alternate PGD.
 */
static int populate_pgd(struct cpa_data *cpa, unsigned long addr)
{
	pgprot_t pgprot = __pgprot(_KERNPG_TABLE);
	pud_t *pud = NULL;	/* shut up gcc */
	pgd_t *pgd_entry;
	int ret;

	pgd_entry = cpa->pgd + pgd_index(addr);

	/*
	 * Allocate a PUD page and hand it down for mapping.
	 */
	if (pgd_none(*pgd_entry)) {
		pud = (pud_t *)get_zeroed_page(GFP_KERNEL | __GFP_NOTRACK);
		if (!pud)
			return -1;

		set_pgd(pgd_entry, __pgd(__pa(pud) | _KERNPG_TABLE));
	}

	pgprot_val(pgprot) &= ~pgprot_val(cpa->mask_clr);
	pgprot_val(pgprot) |=  pgprot_val(cpa->mask_set);

	ret = populate_pud(cpa, addr, pgd_entry, pgprot);
	if (ret < 0) {
		/*
		 * Leave the PUD page in place in case some other CPU or thread
		 * already found it, but remove any useless entries we just
		 * added to it.
		 */
		unmap_pud_range(pgd_entry, addr,
				addr + (cpa->numpages << PAGE_SHIFT));
		return ret;
	}

	cpa->numpages = ret;
	return 0;
}

static int __cpa_process_fault(struct cpa_data *cpa, unsigned long vaddr,
			       int primary)
{
	if (cpa->pgd) {
		/*
		 * Right now, we only execute this code path when mapping
		 * the EFI virtual memory map regions, no other users
		 * provide a ->pgd value. This may change in the future.
		 */
		return populate_pgd(cpa, vaddr);
	}

	/*
	 * Ignore all non primary paths.
	 */
	if (!primary) {
		cpa->numpages = 1;
		return 0;
	}

	/*
	 * Ignore the NULL PTE for kernel identity mapping, as it is expected
	 * to have holes.
	 * Also set numpages to '1' indicating that we processed cpa req for
	 * one virtual address page and its pfn. TBD: numpages can be set based
	 * on the initial value and the level returned by lookup_address().
	 */
	if (within(vaddr, PAGE_OFFSET,
		   PAGE_OFFSET + (max_pfn_mapped << PAGE_SHIFT))) {
		cpa->numpages = 1;
		cpa->pfn = __pa(vaddr) >> PAGE_SHIFT;
		return 0;
	} else {
		WARN(1, KERN_WARNING "CPA: called for zero pte. "
			"vaddr = %lx cpa->vaddr = %lx\n", vaddr,
			*cpa->vaddr);

		return -EFAULT;
	}
}

static int __change_page_attr(struct cpa_data *cpa, int primary)
{
	unsigned long address;
	int do_split, err;
	unsigned int level;
	pte_t *kpte, old_pte;

	if (cpa->flags & CPA_PAGES_ARRAY) {
		struct page *page = cpa->pages[cpa->curpage];
		if (unlikely(PageHighMem(page)))
			return 0;
		address = (unsigned long)page_address(page);
	} else if (cpa->flags & CPA_ARRAY)
		address = cpa->vaddr[cpa->curpage];
	else
		address = *cpa->vaddr;
repeat:
	kpte = _lookup_address_cpa(cpa, address, &level);
	if (!kpte)
		return __cpa_process_fault(cpa, address, primary);

	old_pte = *kpte;
	if (pte_none(old_pte))
		return __cpa_process_fault(cpa, address, primary);

	if (level == PG_LEVEL_4K) {
		pte_t new_pte;
		pgprot_t new_prot = pte_pgprot(old_pte);
		unsigned long pfn = pte_pfn(old_pte);

		pgprot_val(new_prot) &= ~pgprot_val(cpa->mask_clr);
		pgprot_val(new_prot) |= pgprot_val(cpa->mask_set);

		new_prot = static_protections(new_prot, address, pfn);

		/*
		 * Set the GLOBAL flags only if the PRESENT flag is
		 * set otherwise pte_present will return true even on
		 * a non present pte. The canon_pgprot will clear
		 * _PAGE_GLOBAL for the ancient hardware that doesn't
		 * support it.
		 */
		if (pgprot_val(new_prot) & _PAGE_PRESENT)
			pgprot_val(new_prot) |= _PAGE_GLOBAL;
		else
			pgprot_val(new_prot) &= ~_PAGE_GLOBAL;

		/*
		 * We need to keep the pfn from the existing PTE,
		 * after all we're only going to change it's attributes
		 * not the memory it points to
		 */
		new_pte = pfn_pte(pfn, canon_pgprot(new_prot));
		cpa->pfn = pfn;
		/*
		 * Do we really change anything ?
		 */
		if (pte_val(old_pte) != pte_val(new_pte)) {
			set_pte_atomic(kpte, new_pte);
			cpa->flags |= CPA_FLUSHTLB;
		}
		cpa->numpages = 1;
		return 0;
	}

	/*
	 * Check, whether we can keep the large page intact
	 * and just change the pte:
	 */
	do_split = try_preserve_large_page(kpte, address, cpa);
	/*
	 * When the range fits into the existing large page,
	 * return. cp->numpages and cpa->tlbflush have been updated in
	 * try_large_page:
	 */
	if (do_split <= 0)
		return do_split;

	/*
	 * We have to split the large page:
	 */
	err = split_large_page(cpa, kpte, address);
	if (!err) {
		/*
	 	 * Do a global flush tlb after splitting the large page
	 	 * and before we do the actual change page attribute in the PTE.
	 	 *
	 	 * With out this, we violate the TLB application note, that says
	 	 * "The TLBs may contain both ordinary and large-page
		 *  translations for a 4-KByte range of linear addresses. This
		 *  may occur if software modifies the paging structures so that
		 *  the page size used for the address range changes. If the two
		 *  translations differ with respect to page frame or attributes
		 *  (e.g., permissions), processor behavior is undefined and may
		 *  be implementation-specific."
	 	 *
	 	 * We do this global tlb flush inside the cpa_lock, so that we
		 * don't allow any other cpu, with stale tlb entries change the
		 * page attribute in parallel, that also falls into the
		 * just split large page entry.
	 	 */
		flush_tlb_all();
		goto repeat;
	}

	return err;
}

static int __change_page_attr_set_clr(struct cpa_data *cpa, int checkalias);

static int cpa_process_alias(struct cpa_data *cpa)
{
	struct cpa_data alias_cpa;
	unsigned long laddr = (unsigned long)__va(cpa->pfn << PAGE_SHIFT);
	unsigned long vaddr;
	int ret;

	if (!pfn_range_is_mapped(cpa->pfn, cpa->pfn + 1))
		return 0;

	/*
	 * No need to redo, when the primary call touched the direct
	 * mapping already:
	 */
	if (cpa->flags & CPA_PAGES_ARRAY) {
		struct page *page = cpa->pages[cpa->curpage];
		if (unlikely(PageHighMem(page)))
			return 0;
		vaddr = (unsigned long)page_address(page);
	} else if (cpa->flags & CPA_ARRAY)
		vaddr = cpa->vaddr[cpa->curpage];
	else
		vaddr = *cpa->vaddr;

	if (!(within(vaddr, PAGE_OFFSET,
		    PAGE_OFFSET + (max_pfn_mapped << PAGE_SHIFT)))) {

		alias_cpa = *cpa;
		alias_cpa.vaddr = &laddr;
		alias_cpa.flags &= ~(CPA_PAGES_ARRAY | CPA_ARRAY);

		ret = __change_page_attr_set_clr(&alias_cpa, 0);
		if (ret)
			return ret;
	}

#ifdef CONFIG_X86_64
	/*
	 * If the primary call didn't touch the high mapping already
	 * and the physical address is inside the kernel map, we need
	 * to touch the high mapped kernel as well:
	 */
	if (!within(vaddr, (unsigned long)_text, _brk_end) &&
	    within_inclusive(cpa->pfn, highmap_start_pfn(),
			     highmap_end_pfn())) {
		unsigned long temp_cpa_vaddr = (cpa->pfn << PAGE_SHIFT) +
					       __START_KERNEL_map - phys_base;
		alias_cpa = *cpa;
		alias_cpa.vaddr = &temp_cpa_vaddr;
		alias_cpa.flags &= ~(CPA_PAGES_ARRAY | CPA_ARRAY);

		/*
		 * The high mapping range is imprecise, so ignore the
		 * return value.
		 */
		__change_page_attr_set_clr(&alias_cpa, 0);
	}
#endif

	return 0;
}

static int __change_page_attr_set_clr(struct cpa_data *cpa, int checkalias)
{
	int ret, numpages = cpa->numpages;

	while (numpages) {
		/*
		 * Store the remaining nr of pages for the large page
		 * preservation check.
		 */
		cpa->numpages = numpages;
		/* for array changes, we can't use large page */
		if (cpa->flags & (CPA_ARRAY | CPA_PAGES_ARRAY))
			cpa->numpages = 1;

		if (!debug_pagealloc_enabled())
			spin_lock(&cpa_lock);
		ret = __change_page_attr(cpa, checkalias);
		if (!debug_pagealloc_enabled())
			spin_unlock(&cpa_lock);
		if (ret)
			return ret;

		if (checkalias) {
			ret = cpa_process_alias(cpa);
			if (ret)
				return ret;
		}

		/*
		 * Adjust the number of pages with the result of the
		 * CPA operation. Either a large page has been
		 * preserved or a single page update happened.
		 */
		BUG_ON(cpa->numpages > numpages || !cpa->numpages);
		numpages -= cpa->numpages;
		if (cpa->flags & (CPA_PAGES_ARRAY | CPA_ARRAY))
			cpa->curpage++;
		else
			*cpa->vaddr += cpa->numpages * PAGE_SIZE;

	}
	return 0;
}

static int change_page_attr_set_clr(unsigned long *addr, int numpages,
				    pgprot_t mask_set, pgprot_t mask_clr,
				    int force_split, int in_flag,
				    struct page **pages)
{
	struct cpa_data cpa;
	int ret, cache, checkalias;
	unsigned long baddr = 0;

	memset(&cpa, 0, sizeof(cpa));

	/*
	 * Check, if we are requested to change a not supported
	 * feature:
	 */
	mask_set = canon_pgprot(mask_set);
	mask_clr = canon_pgprot(mask_clr);
	if (!pgprot_val(mask_set) && !pgprot_val(mask_clr) && !force_split)
		return 0;

	/* Ensure we are PAGE_SIZE aligned */
	if (in_flag & CPA_ARRAY) {
		int i;
		for (i = 0; i < numpages; i++) {
			if (addr[i] & ~PAGE_MASK) {
				addr[i] &= PAGE_MASK;
				WARN_ON_ONCE(1);
			}
		}
	} else if (!(in_flag & CPA_PAGES_ARRAY)) {
		/*
		 * in_flag of CPA_PAGES_ARRAY implies it is aligned.
		 * No need to cehck in that case
		 */
		if (*addr & ~PAGE_MASK) {
			*addr &= PAGE_MASK;
			/*
			 * People should not be passing in unaligned addresses:
			 */
			WARN_ON_ONCE(1);
		}
		/*
		 * Save address for cache flush. *addr is modified in the call
		 * to __change_page_attr_set_clr() below.
		 */
		baddr = *addr;
	}

	/* Must avoid aliasing mappings in the highmem code */
	kmap_flush_unused();

	vm_unmap_aliases();

	cpa.vaddr = addr;
	cpa.pages = pages;
	cpa.numpages = numpages;
	cpa.mask_set = mask_set;
	cpa.mask_clr = mask_clr;
	cpa.flags = 0;
	cpa.curpage = 0;
	cpa.force_split = force_split;

	if (in_flag & (CPA_ARRAY | CPA_PAGES_ARRAY))
		cpa.flags |= in_flag;

	/* No alias checking for _NX bit modifications */
	checkalias = (pgprot_val(mask_set) | pgprot_val(mask_clr)) != _PAGE_NX;

	ret = __change_page_attr_set_clr(&cpa, checkalias);

	/*
	 * Check whether we really changed something:
	 */
	if (!(cpa.flags & CPA_FLUSHTLB))
		goto out;

	/*
	 * No need to flush, when we did not set any of the caching
	 * attributes:
	 */
	cache = !!pgprot2cachemode(mask_set);

	/*
	 * On success we use CLFLUSH, when the CPU supports it to
	 * avoid the WBINVD. If the CPU does not support it and in the
	 * error case we fall back to cpa_flush_all (which uses
	 * WBINVD):
	 */
	if (!ret && boot_cpu_has(X86_FEATURE_CLFLUSH)) {
		if (cpa.flags & (CPA_PAGES_ARRAY | CPA_ARRAY)) {
			cpa_flush_array(addr, numpages, cache,
					cpa.flags, pages);
		} else
			cpa_flush_range(baddr, numpages, cache);
	} else
		cpa_flush_all(cache);

out:
	return ret;
}

static inline int change_page_attr_set(unsigned long *addr, int numpages,
				       pgprot_t mask, int array)
{
	return change_page_attr_set_clr(addr, numpages, mask, __pgprot(0), 0,
		(array ? CPA_ARRAY : 0), NULL);
}

static inline int change_page_attr_clear(unsigned long *addr, int numpages,
					 pgprot_t mask, int array)
{
	return change_page_attr_set_clr(addr, numpages, __pgprot(0), mask, 0,
		(array ? CPA_ARRAY : 0), NULL);
}

static inline int cpa_set_pages_array(struct page **pages, int numpages,
				       pgprot_t mask)
{
	return change_page_attr_set_clr(NULL, numpages, mask, __pgprot(0), 0,
		CPA_PAGES_ARRAY, pages);
}

static inline int cpa_clear_pages_array(struct page **pages, int numpages,
					 pgprot_t mask)
{
	return change_page_attr_set_clr(NULL, numpages, __pgprot(0), mask, 0,
		CPA_PAGES_ARRAY, pages);
}

int _set_memory_uc(unsigned long addr, int numpages)
{
	/*
	 * for now UC MINUS. see comments in ioremap_nocache()
	 * If you really need strong UC use ioremap_uc(), but note
	 * that you cannot override IO areas with set_memory_*() as
	 * these helpers cannot work with IO memory.
	 */
	return change_page_attr_set(&addr, numpages,
				    cachemode2pgprot(_PAGE_CACHE_MODE_UC_MINUS),
				    0);
}

int set_memory_uc(unsigned long addr, int numpages)
{
	int ret;

	/*
	 * for now UC MINUS. see comments in ioremap_nocache()
	 */
	ret = reserve_memtype(__pa(addr), __pa(addr) + numpages * PAGE_SIZE,
			      _PAGE_CACHE_MODE_UC_MINUS, NULL);
	if (ret)
		goto out_err;

	ret = _set_memory_uc(addr, numpages);
	if (ret)
		goto out_free;

	return 0;

out_free:
	free_memtype(__pa(addr), __pa(addr) + numpages * PAGE_SIZE);
out_err:
	return ret;
}
EXPORT_SYMBOL(set_memory_uc);

static int _set_memory_array(unsigned long *addr, int addrinarray,
		enum page_cache_mode new_type)
{
	enum page_cache_mode set_type;
	int i, j;
	int ret;

	for (i = 0; i < addrinarray; i++) {
		ret = reserve_memtype(__pa(addr[i]), __pa(addr[i]) + PAGE_SIZE,
					new_type, NULL);
		if (ret)
			goto out_free;
	}

	/* If WC, set to UC- first and then WC */
	set_type = (new_type == _PAGE_CACHE_MODE_WC) ?
				_PAGE_CACHE_MODE_UC_MINUS : new_type;

	ret = change_page_attr_set(addr, addrinarray,
				   cachemode2pgprot(set_type), 1);

	if (!ret && new_type == _PAGE_CACHE_MODE_WC)
		ret = change_page_attr_set_clr(addr, addrinarray,
					       cachemode2pgprot(
						_PAGE_CACHE_MODE_WC),
					       __pgprot(_PAGE_CACHE_MASK),
					       0, CPA_ARRAY, NULL);
	if (ret)
		goto out_free;

	return 0;

out_free:
	for (j = 0; j < i; j++)
		free_memtype(__pa(addr[j]), __pa(addr[j]) + PAGE_SIZE);

	return ret;
}

int set_memory_array_uc(unsigned long *addr, int addrinarray)
{
	return _set_memory_array(addr, addrinarray, _PAGE_CACHE_MODE_UC_MINUS);
}
EXPORT_SYMBOL(set_memory_array_uc);

int set_memory_array_wc(unsigned long *addr, int addrinarray)
{
	return _set_memory_array(addr, addrinarray, _PAGE_CACHE_MODE_WC);
}
EXPORT_SYMBOL(set_memory_array_wc);

int set_memory_array_wt(unsigned long *addr, int addrinarray)
{
	return _set_memory_array(addr, addrinarray, _PAGE_CACHE_MODE_WT);
}
EXPORT_SYMBOL_GPL(set_memory_array_wt);

int _set_memory_wc(unsigned long addr, int numpages)
{
	int ret;
	unsigned long addr_copy = addr;

	ret = change_page_attr_set(&addr, numpages,
				   cachemode2pgprot(_PAGE_CACHE_MODE_UC_MINUS),
				   0);
	if (!ret) {
		ret = change_page_attr_set_clr(&addr_copy, numpages,
					       cachemode2pgprot(
						_PAGE_CACHE_MODE_WC),
					       __pgprot(_PAGE_CACHE_MASK),
					       0, 0, NULL);
	}
	return ret;
}

int set_memory_wc(unsigned long addr, int numpages)
{
	int ret;

	ret = reserve_memtype(__pa(addr), __pa(addr) + numpages * PAGE_SIZE,
		_PAGE_CACHE_MODE_WC, NULL);
	if (ret)
		return ret;

	ret = _set_memory_wc(addr, numpages);
	if (ret)
		free_memtype(__pa(addr), __pa(addr) + numpages * PAGE_SIZE);

	return ret;
}
EXPORT_SYMBOL(set_memory_wc);

int _set_memory_wt(unsigned long addr, int numpages)
{
	return change_page_attr_set(&addr, numpages,
				    cachemode2pgprot(_PAGE_CACHE_MODE_WT), 0);
}

int set_memory_wt(unsigned long addr, int numpages)
{
	int ret;

	ret = reserve_memtype(__pa(addr), __pa(addr) + numpages * PAGE_SIZE,
			      _PAGE_CACHE_MODE_WT, NULL);
	if (ret)
		return ret;

	ret = _set_memory_wt(addr, numpages);
	if (ret)
		free_memtype(__pa(addr), __pa(addr) + numpages * PAGE_SIZE);

	return ret;
}
EXPORT_SYMBOL_GPL(set_memory_wt);

int _set_memory_wb(unsigned long addr, int numpages)
{
	/* WB cache mode is hard wired to all cache attribute bits being 0 */
	return change_page_attr_clear(&addr, numpages,
				      __pgprot(_PAGE_CACHE_MASK), 0);
}

int set_memory_wb(unsigned long addr, int numpages)
{
	int ret;

	ret = _set_memory_wb(addr, numpages);
	if (ret)
		return ret;

	free_memtype(__pa(addr), __pa(addr) + numpages * PAGE_SIZE);
	return 0;
}
EXPORT_SYMBOL(set_memory_wb);

int set_memory_array_wb(unsigned long *addr, int addrinarray)
{
	int i;
	int ret;

	/* WB cache mode is hard wired to all cache attribute bits being 0 */
	ret = change_page_attr_clear(addr, addrinarray,
				      __pgprot(_PAGE_CACHE_MASK), 1);
	if (ret)
		return ret;

	for (i = 0; i < addrinarray; i++)
		free_memtype(__pa(addr[i]), __pa(addr[i]) + PAGE_SIZE);

	return 0;
}
EXPORT_SYMBOL(set_memory_array_wb);

int set_memory_x(unsigned long addr, int numpages)
{
	if (!(__supported_pte_mask & _PAGE_NX))
		return 0;

	return change_page_attr_clear(&addr, numpages, __pgprot(_PAGE_NX), 0);
}
EXPORT_SYMBOL(set_memory_x);

int set_memory_nx(unsigned long addr, int numpages)
{
	if (!(__supported_pte_mask & _PAGE_NX))
		return 0;

	return change_page_attr_set(&addr, numpages, __pgprot(_PAGE_NX), 0);
}
EXPORT_SYMBOL(set_memory_nx);

int set_memory_ro(unsigned long addr, int numpages)
{
	return change_page_attr_clear(&addr, numpages, __pgprot(_PAGE_RW), 0);
}

int set_memory_rw(unsigned long addr, int numpages)
{
	return change_page_attr_set(&addr, numpages, __pgprot(_PAGE_RW), 0);
}

int set_memory_np(unsigned long addr, int numpages)
{
	return change_page_attr_clear(&addr, numpages, __pgprot(_PAGE_PRESENT), 0);
}

int set_memory_4k(unsigned long addr, int numpages)
{
	return change_page_attr_set_clr(&addr, numpages, __pgprot(0),
					__pgprot(0), 1, 0, NULL);
}

int set_pages_uc(struct page *page, int numpages)
{
	unsigned long addr = (unsigned long)page_address(page);

	return set_memory_uc(addr, numpages);
}
EXPORT_SYMBOL(set_pages_uc);

static int _set_pages_array(struct page **pages, int addrinarray,
		enum page_cache_mode new_type)
{
	unsigned long start;
	unsigned long end;
	enum page_cache_mode set_type;
	int i;
	int free_idx;
	int ret;

	for (i = 0; i < addrinarray; i++) {
		if (PageHighMem(pages[i]))
			continue;
		start = page_to_pfn(pages[i]) << PAGE_SHIFT;
		end = start + PAGE_SIZE;
		if (reserve_memtype(start, end, new_type, NULL))
			goto err_out;
	}

	/* If WC, set to UC- first and then WC */
	set_type = (new_type == _PAGE_CACHE_MODE_WC) ?
				_PAGE_CACHE_MODE_UC_MINUS : new_type;

	ret = cpa_set_pages_array(pages, addrinarray,
				  cachemode2pgprot(set_type));
	if (!ret && new_type == _PAGE_CACHE_MODE_WC)
		ret = change_page_attr_set_clr(NULL, addrinarray,
					       cachemode2pgprot(
						_PAGE_CACHE_MODE_WC),
					       __pgprot(_PAGE_CACHE_MASK),
					       0, CPA_PAGES_ARRAY, pages);
	if (ret)
		goto err_out;
	return 0; /* Success */
err_out:
	free_idx = i;
	for (i = 0; i < free_idx; i++) {
		if (PageHighMem(pages[i]))
			continue;
		start = page_to_pfn(pages[i]) << PAGE_SHIFT;
		end = start + PAGE_SIZE;
		free_memtype(start, end);
	}
	return -EINVAL;
}

int set_pages_array_uc(struct page **pages, int addrinarray)
{
	return _set_pages_array(pages, addrinarray, _PAGE_CACHE_MODE_UC_MINUS);
}
EXPORT_SYMBOL(set_pages_array_uc);

int set_pages_array_wc(struct page **pages, int addrinarray)
{
	return _set_pages_array(pages, addrinarray, _PAGE_CACHE_MODE_WC);
}
EXPORT_SYMBOL(set_pages_array_wc);

int set_pages_array_wt(struct page **pages, int addrinarray)
{
	return _set_pages_array(pages, addrinarray, _PAGE_CACHE_MODE_WT);
}
EXPORT_SYMBOL_GPL(set_pages_array_wt);

int set_pages_wb(struct page *page, int numpages)
{
	unsigned long addr = (unsigned long)page_address(page);

	return set_memory_wb(addr, numpages);
}
EXPORT_SYMBOL(set_pages_wb);

int set_pages_array_wb(struct page **pages, int addrinarray)
{
	int retval;
	unsigned long start;
	unsigned long end;
	int i;

	/* WB cache mode is hard wired to all cache attribute bits being 0 */
	retval = cpa_clear_pages_array(pages, addrinarray,
			__pgprot(_PAGE_CACHE_MASK));
	if (retval)
		return retval;

	for (i = 0; i < addrinarray; i++) {
		if (PageHighMem(pages[i]))
			continue;
		start = page_to_pfn(pages[i]) << PAGE_SHIFT;
		end = start + PAGE_SIZE;
		free_memtype(start, end);
	}

	return 0;
}
EXPORT_SYMBOL(set_pages_array_wb);

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

#ifdef CONFIG_DEBUG_PAGEALLOC

static int __set_pages_p(struct page *page, int numpages)
{
	unsigned long tempaddr = (unsigned long) page_address(page);
	struct cpa_data cpa = { .vaddr = &tempaddr,
				.pgd = NULL,
				.numpages = numpages,
				.mask_set = __pgprot(_PAGE_PRESENT | _PAGE_RW),
				.mask_clr = __pgprot(0),
				.flags = 0};

	/*
	 * No alias checking needed for setting present flag. otherwise,
	 * we may need to break large pages for 64-bit kernel text
	 * mappings (this adds to complexity if we want to do this from
	 * atomic context especially). Let's keep it simple!
	 */
	return __change_page_attr_set_clr(&cpa, 0);
}

static int __set_pages_np(struct page *page, int numpages)
{
	unsigned long tempaddr = (unsigned long) page_address(page);
	struct cpa_data cpa = { .vaddr = &tempaddr,
				.pgd = NULL,
				.numpages = numpages,
				.mask_set = __pgprot(0),
				.mask_clr = __pgprot(_PAGE_PRESENT | _PAGE_RW),
				.flags = 0};

	/*
	 * No alias checking needed for setting not present flag. otherwise,
	 * we may need to break large pages for 64-bit kernel text
	 * mappings (this adds to complexity if we want to do this from
	 * atomic context especially). Let's keep it simple!
	 */
	return __change_page_attr_set_clr(&cpa, 0);
}

void __kernel_map_pages(struct page *page, int numpages, int enable)
{
	if (PageHighMem(page))
		return;
	if (!enable) {
		debug_check_no_locks_freed(page_address(page),
					   numpages * PAGE_SIZE);
	}

	/*
	 * The return value is ignored as the calls cannot fail.
	 * Large pages for identity mappings are not used at boot time
	 * and hence no memory allocations during large page split.
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

	arch_flush_lazy_mmu_mode();
}

#ifdef CONFIG_HIBERNATION

bool kernel_page_present(struct page *page)
{
	unsigned int level;
	pte_t *pte;

	if (PageHighMem(page))
		return false;

	pte = lookup_address((unsigned long)page_address(page), &level);
	return (pte_val(*pte) & _PAGE_PRESENT);
}

#endif /* CONFIG_HIBERNATION */

#endif /* CONFIG_DEBUG_PAGEALLOC */

int kernel_map_pages_in_pgd(pgd_t *pgd, u64 pfn, unsigned long address,
			    unsigned numpages, unsigned long page_flags)
{
	int retval = -EINVAL;

	struct cpa_data cpa = {
		.vaddr = &address,
		.pfn = pfn,
		.pgd = pgd,
		.numpages = numpages,
		.mask_set = __pgprot(0),
		.mask_clr = __pgprot(0),
		.flags = 0,
	};

	if (!(__supported_pte_mask & _PAGE_NX))
		goto out;

	if (!(page_flags & _PAGE_NX))
		cpa.mask_clr = __pgprot(_PAGE_NX);

	if (!(page_flags & _PAGE_RW))
		cpa.mask_clr = __pgprot(_PAGE_RW);

	cpa.mask_set = __pgprot(_PAGE_PRESENT | page_flags);

	retval = __change_page_attr_set_clr(&cpa, 0);
	__flush_tlb_all();

out:
	return retval;
}

/*
 * The testcases use internal knowledge of the implementation that shouldn't
 * be exposed to the rest of the kernel. Include these directly here.
 */
#ifdef CONFIG_CPA_DEBUG
#include "pageattr-test.c"
#endif
