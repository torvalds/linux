/*
 * Copyright 2002 Andi Kleen, SuSE Labs.
 * Thanks to Ben LaHaise for precious feedback.
 */

#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>

void clflush_cache_range(void *addr, int size)
{
	int i;

	for (i = 0; i < size; i += boot_cpu_data.x86_clflush_size)
		clflush(addr+i);
}

#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>
#include <asm/io.h>

pte_t *lookup_address(unsigned long address, int *level)
{
	pgd_t *pgd = pgd_offset_k(address);
	pud_t *pud;
	pmd_t *pmd;

	if (pgd_none(*pgd))
		return NULL;
	pud = pud_offset(pgd, address);
	if (pud_none(*pud))
		return NULL;
	pmd = pmd_offset(pud, address);
	if (pmd_none(*pmd))
		return NULL;
	*level = 3;
	if (pmd_large(*pmd))
		return (pte_t *)pmd;
	*level = 4;

	return pte_offset_kernel(pmd, address);
}

static struct page *
split_large_page(unsigned long address, pgprot_t prot, pgprot_t ref_prot)
{
	unsigned long addr;
	struct page *base;
	pte_t *pbase;
	int i;

	base = alloc_pages(GFP_KERNEL, 0);
	if (!base)
		return NULL;

	address = __pa(address);
	addr = address & LARGE_PAGE_MASK;
	pbase = (pte_t *)page_address(base);
	for (i = 0; i < PTRS_PER_PTE; i++, addr += PAGE_SIZE) {
		pbase[i] = pfn_pte(addr >> PAGE_SHIFT,
				   addr == address ? prot : ref_prot);
	}
	return base;
}

static int
__change_page_attr(unsigned long address, unsigned long pfn, pgprot_t prot,
		   pgprot_t ref_prot)
{
	struct page *kpte_page;
	pte_t *kpte;
	pgprot_t ref_prot2, oldprot;
	int level;

	kpte = lookup_address(address, &level);
	if (!kpte)
		return 0;

	kpte_page = virt_to_page(kpte);
	oldprot = pte_pgprot(*kpte);
	BUG_ON(PageLRU(kpte_page));
	BUG_ON(PageCompound(kpte_page));
	ref_prot = canon_pgprot(ref_prot);
	prot = canon_pgprot(prot);

	if (pgprot_val(prot) != pgprot_val(ref_prot)) {
		if (level == 4) {
			set_pte(kpte, pfn_pte(pfn, prot));
		} else {
			/*
			 * split_large_page will take the reference for this
			 * change_page_attr on the split page.
			 */
			struct page *split;

			ref_prot2 = pte_pgprot(pte_clrhuge(*kpte));
			split = split_large_page(address, prot, ref_prot2);
			if (!split)
				return -ENOMEM;
			pgprot_val(ref_prot2) &= ~_PAGE_NX;
			set_pte(kpte, mk_pte(split, ref_prot2));
			kpte_page = split;
		}
	} else {
		if (level == 4) {
			set_pte(kpte, pfn_pte(pfn, ref_prot));
		} else
			BUG();
	}

	return 0;
}

/**
 * change_page_attr_addr - Change page table attributes in linear mapping
 * @address: Virtual address in linear mapping.
 * @numpages: Number of pages to change
 * @prot:    New page table attribute (PAGE_*)
 *
 * Change page attributes of a page in the direct mapping. This is a variant
 * of change_page_attr() that also works on memory holes that do not have
 * mem_map entry (pfn_valid() is false).
 *
 * See change_page_attr() documentation for more details.
 */

int change_page_attr_addr(unsigned long address, int numpages, pgprot_t prot)
{
	int err = 0, kernel_map = 0, i;

	if (address >= __START_KERNEL_map &&
			address < __START_KERNEL_map + KERNEL_TEXT_SIZE) {

		address = (unsigned long)__va(__pa(address));
		kernel_map = 1;
	}

	down_write(&init_mm.mmap_sem);
	for (i = 0; i < numpages; i++, address += PAGE_SIZE) {
		unsigned long pfn = __pa(address) >> PAGE_SHIFT;

		if (!kernel_map || pte_present(pfn_pte(0, prot))) {
			err = __change_page_attr(address, pfn, prot,
						PAGE_KERNEL);
			if (err)
				break;
		}
		/* Handle kernel mapping too which aliases part of the
		 * lowmem */
		if (__pa(address) < KERNEL_TEXT_SIZE) {
			unsigned long addr2;
			pgprot_t prot2;

			addr2 = __START_KERNEL_map + __pa(address);
			/* Make sure the kernel mappings stay executable */
			prot2 = pte_pgprot(pte_mkexec(pfn_pte(0, prot)));
			err = __change_page_attr(addr2, pfn, prot2,
						 PAGE_KERNEL_EXEC);
		}
	}
	up_write(&init_mm.mmap_sem);

	return err;
}

/**
 * change_page_attr - Change page table attributes in the linear mapping.
 * @page: First page to change
 * @numpages: Number of pages to change
 * @prot: New protection/caching type (PAGE_*)
 *
 * Returns 0 on success, otherwise a negated errno.
 *
 * This should be used when a page is mapped with a different caching policy
 * than write-back somewhere - some CPUs do not like it when mappings with
 * different caching policies exist. This changes the page attributes of the
 * in kernel linear mapping too.
 *
 * Caller must call global_flush_tlb() later to make the changes active.
 *
 * The caller needs to ensure that there are no conflicting mappings elsewhere
 * (e.g. in user space) * This function only deals with the kernel linear map.
 *
 * For MMIO areas without mem_map use change_page_attr_addr() instead.
 */
int change_page_attr(struct page *page, int numpages, pgprot_t prot)
{
	unsigned long addr = (unsigned long)page_address(page);

	return change_page_attr_addr(addr, numpages, prot);
}
EXPORT_SYMBOL(change_page_attr);

static void flush_kernel_map(void *arg)
{
	/*
	 * Flush all to work around Errata in early athlons regarding
	 * large page flushing.
	 */
	__flush_tlb_all();

	if (boot_cpu_data.x86_model >= 4)
		wbinvd();
}

void global_flush_tlb(void)
{
	BUG_ON(irqs_disabled());

	on_each_cpu(flush_kernel_map, NULL, 1, 1);
}
EXPORT_SYMBOL(global_flush_tlb);
