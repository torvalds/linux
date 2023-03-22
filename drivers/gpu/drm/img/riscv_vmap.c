#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/pfn.h>
#include <linux/kmemleak.h>
#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/llist.h>
#include <linux/bitops.h>
#include <linux/rbtree_augmented.h>
#include <linux/overflow.h>
#include <linux/hugetlb.h>

#include <linux/uaccess.h>
#include <asm/tlbflush.h>
#include <asm/shmparam.h>

#include "pgalloc-track.h"
#include "riscv_vmap.h"

#define SYSPORT_MEM_PFN_OFFSET	 (0x400000000 >> PAGE_SHIFT)
#define __mk_pte(page, prot)     pfn_pte(page_to_pfn(page) + SYSPORT_MEM_PFN_OFFSET, prot)
#define __mk_pte_cached(page, prot)     pfn_pte(page_to_pfn(page), prot)

static int vmap_pte_range(pmd_t *pmd, unsigned long addr,
		unsigned long end, pgprot_t prot, struct page **pages, int *nr,
		pgtbl_mod_mask *mask, int cached)
{
	pte_t *pte;

	/*
	 * nr is a running index into the array which helps higher level
	 * callers keep track of where we're up to.
	 */

	pte = pte_alloc_kernel_track(pmd, addr, mask);
	if (!pte)
		return -ENOMEM;
	do {
		struct page *page = pages[*nr];

		if (WARN_ON(!pte_none(*pte)))
			return -EBUSY;
		if (WARN_ON(!page))
			return -ENOMEM;
		/* Special processing for starfive RISC-V, pfn add an offset of 0x400000*/
		if (cached)
			set_pte_at(&init_mm, addr, pte, __mk_pte_cached(page, prot));
		else
			set_pte_at(&init_mm, addr, pte, __mk_pte(page, prot));
		(*nr)++;
	} while (pte++, addr += PAGE_SIZE, addr != end);
	*mask |= PGTBL_PTE_MODIFIED;
	return 0;
}

static int vmap_pmd_range(pud_t *pud, unsigned long addr,
		unsigned long end, pgprot_t prot, struct page **pages, int *nr,
		pgtbl_mod_mask *mask, int cached)
{
	pmd_t *pmd;
	unsigned long next;

	pmd = pmd_alloc_track(&init_mm, pud, addr, mask);
	if (!pmd)
		return -ENOMEM;
	do {
		next = pmd_addr_end(addr, end);
		if (vmap_pte_range(pmd, addr, next, prot, pages, nr, mask,
			cached))
			return -ENOMEM;
	} while (pmd++, addr = next, addr != end);
	return 0;
}

static int vmap_pud_range(p4d_t *p4d, unsigned long addr,
		unsigned long end, pgprot_t prot, struct page **pages, int *nr,
		pgtbl_mod_mask *mask, int cached)
{
	pud_t *pud;
	unsigned long next;

	pud = pud_alloc_track(&init_mm, p4d, addr, mask);
	if (!pud)
		return -ENOMEM;
	do {
		next = pud_addr_end(addr, end);
		if (vmap_pmd_range(pud, addr, next, prot, pages, nr, mask,
			cached))
			return -ENOMEM;
	} while (pud++, addr = next, addr != end);
	return 0;
}

static int vmap_p4d_range(pgd_t *pgd, unsigned long addr,
		unsigned long end, pgprot_t prot, struct page **pages, int *nr,
		pgtbl_mod_mask *mask, int cached)
{
	p4d_t *p4d;
	unsigned long next;

	p4d = p4d_alloc_track(&init_mm, pgd, addr, mask);
	if (!p4d)
		return -ENOMEM;
	do {
		next = p4d_addr_end(addr, end);
		if (vmap_pud_range(p4d, addr, next, prot, pages, nr, mask,
			cached))
			return -ENOMEM;
	} while (p4d++, addr = next, addr != end);
	return 0;
}

static int __map_kernel_range_noflush(unsigned long addr, unsigned long size,
			     pgprot_t prot, struct page **pages, int cached)
{
	unsigned long start = addr;
	unsigned long end = addr + size;
	unsigned long next;
	pgd_t *pgd;
	int err = 0;
	int nr = 0;
	pgtbl_mod_mask mask = 0;

	BUG_ON(addr >= end);
	pgd = pgd_offset_k(addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_bad(*pgd))
			mask |= PGTBL_PGD_MODIFIED;
		err = vmap_p4d_range(pgd, addr, next, prot, pages, &nr, &mask,
			cached);
		if (err)
			return err;
	} while (pgd++, addr = next, addr != end);

	if (mask & ARCH_PAGE_TABLE_SYNC_MASK)
		arch_sync_kernel_mappings(start, end);

	return 0;
}

static int __map_kernel_range(unsigned long start, unsigned long size, pgprot_t prot,
		struct page **pages, int cached)
{
	int ret;

	ret = __map_kernel_range_noflush(start, size, prot, pages, cached);
	flush_cache_vmap(start, start + size);
	return ret;
}

void *riscv_vmap(struct page **pages, unsigned int count,
	   unsigned long flags, pgprot_t prot, int cached)
{
	struct vm_struct *area;
	unsigned long size;		/* In bytes */

	might_sleep();

	if (count > totalram_pages())
		return NULL;

	size = (unsigned long)count << PAGE_SHIFT;
	area = get_vm_area_caller(size, flags, __builtin_return_address(0));
	if (!area)
		return NULL;

	if (__map_kernel_range((unsigned long)area->addr, size, pgprot_nx(prot),
			pages, cached) < 0) {
		vunmap(area->addr);
		return NULL;
	}

	if (flags & VM_MAP_PUT_PAGES)
		area->pages = pages;
	return area->addr;
}
EXPORT_SYMBOL_GPL(riscv_vmap);

/*---------------Test code-----*/
#ifdef IMG_GPU_DEBUG
extern unsigned long va2pa(unsigned long *vaddr_in);
void test_riscv_vmap(void)
{
	struct page *pages[2];
	void *tmp;
	pgprot_t prot = PAGE_KERNEL;
	
	prot = pgprot_noncached(prot);
	printk("Page prot = %lx", pgprot_val(prot));
	
	pages[0] = alloc_page(GFP_KERNEL);
	pages[1] = alloc_page(GFP_KERNEL);
	
	printk("Page struct: %lx %lx\n", (unsigned long)pages[0], (unsigned long)pages[1]);
	printk("virt_addr_valid: %d %d\n", virt_addr_valid(pages[0]), virt_addr_valid(pages[1]));
	printk("PPN: %lx %lx\n", page_to_pfn(pages[0]), page_to_pfn(pages[1]));
	printk("Page address: %lx %lx\n", (unsigned long)page_address(pages[0]), (unsigned long)page_address(pages[1]));
	tmp = riscv_vmap(pages, 2, VM_READ | VM_WRITE, prot);
	va2pa(tmp);
	memset(tmp, 0x78, 8 * 1024);
       
	vunmap(tmp);
	__free_pages(pages[0], 0);
	__free_pages(pages[1], 0);
	printk("Pages freed!\n");
	//free_page((unsigned long)pages[0]);
	//free_page((unsigned long)pages[1]);
}
#endif
