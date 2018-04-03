// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <linux/string.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/cache.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/dma-mapping.h>
#include <asm/proc-fns.h>

/*
 * This is the page table (2MB) covering uncached, DMA consistent allocations
 */
static pte_t *consistent_pte;
static DEFINE_RAW_SPINLOCK(consistent_lock);

enum master_type {
	FOR_CPU = 0,
	FOR_DEVICE = 1,
};

/*
 * VM region handling support.
 *
 * This should become something generic, handling VM region allocations for
 * vmalloc and similar (ioremap, module space, etc).
 *
 * I envisage vmalloc()'s supporting vm_struct becoming:
 *
 *  struct vm_struct {
 *    struct vm_region	region;
 *    unsigned long	flags;
 *    struct page	**pages;
 *    unsigned int	nr_pages;
 *    unsigned long	phys_addr;
 *  };
 *
 * get_vm_area() would then call vm_region_alloc with an appropriate
 * struct vm_region head (eg):
 *
 *  struct vm_region vmalloc_head = {
 *	.vm_list	= LIST_HEAD_INIT(vmalloc_head.vm_list),
 *	.vm_start	= VMALLOC_START,
 *	.vm_end		= VMALLOC_END,
 *  };
 *
 * However, vmalloc_head.vm_start is variable (typically, it is dependent on
 * the amount of RAM found at boot time.)  I would imagine that get_vm_area()
 * would have to initialise this each time prior to calling vm_region_alloc().
 */
struct arch_vm_region {
	struct list_head vm_list;
	unsigned long vm_start;
	unsigned long vm_end;
	struct page *vm_pages;
};

static struct arch_vm_region consistent_head = {
	.vm_list = LIST_HEAD_INIT(consistent_head.vm_list),
	.vm_start = CONSISTENT_BASE,
	.vm_end = CONSISTENT_END,
};

static struct arch_vm_region *vm_region_alloc(struct arch_vm_region *head,
					      size_t size, int gfp)
{
	unsigned long addr = head->vm_start, end = head->vm_end - size;
	unsigned long flags;
	struct arch_vm_region *c, *new;

	new = kmalloc(sizeof(struct arch_vm_region), gfp);
	if (!new)
		goto out;

	raw_spin_lock_irqsave(&consistent_lock, flags);

	list_for_each_entry(c, &head->vm_list, vm_list) {
		if ((addr + size) < addr)
			goto nospc;
		if ((addr + size) <= c->vm_start)
			goto found;
		addr = c->vm_end;
		if (addr > end)
			goto nospc;
	}

found:
	/*
	 * Insert this entry _before_ the one we found.
	 */
	list_add_tail(&new->vm_list, &c->vm_list);
	new->vm_start = addr;
	new->vm_end = addr + size;

	raw_spin_unlock_irqrestore(&consistent_lock, flags);
	return new;

nospc:
	raw_spin_unlock_irqrestore(&consistent_lock, flags);
	kfree(new);
out:
	return NULL;
}

static struct arch_vm_region *vm_region_find(struct arch_vm_region *head,
					     unsigned long addr)
{
	struct arch_vm_region *c;

	list_for_each_entry(c, &head->vm_list, vm_list) {
		if (c->vm_start == addr)
			goto out;
	}
	c = NULL;
out:
	return c;
}

/* FIXME: attrs is not used. */
static void *nds32_dma_alloc_coherent(struct device *dev, size_t size,
				      dma_addr_t * handle, gfp_t gfp,
				      unsigned long attrs)
{
	struct page *page;
	struct arch_vm_region *c;
	unsigned long order;
	u64 mask = ~0ULL, limit;
	pgprot_t prot = pgprot_noncached(PAGE_KERNEL);

	if (!consistent_pte) {
		pr_err("%s: not initialized\n", __func__);
		dump_stack();
		return NULL;
	}

	if (dev) {
		mask = dev->coherent_dma_mask;

		/*
		 * Sanity check the DMA mask - it must be non-zero, and
		 * must be able to be satisfied by a DMA allocation.
		 */
		if (mask == 0) {
			dev_warn(dev, "coherent DMA mask is unset\n");
			goto no_page;
		}

	}

	/*
	 * Sanity check the allocation size.
	 */
	size = PAGE_ALIGN(size);
	limit = (mask + 1) & ~mask;
	if ((limit && size >= limit) ||
	    size >= (CONSISTENT_END - CONSISTENT_BASE)) {
		pr_warn("coherent allocation too big "
			"(requested %#x mask %#llx)\n", size, mask);
		goto no_page;
	}

	order = get_order(size);

	if (mask != 0xffffffff)
		gfp |= GFP_DMA;

	page = alloc_pages(gfp, order);
	if (!page)
		goto no_page;

	/*
	 * Invalidate any data that might be lurking in the
	 * kernel direct-mapped region for device DMA.
	 */
	{
		unsigned long kaddr = (unsigned long)page_address(page);
		memset(page_address(page), 0, size);
		cpu_dma_wbinval_range(kaddr, kaddr + size);
	}

	/*
	 * Allocate a virtual address in the consistent mapping region.
	 */
	c = vm_region_alloc(&consistent_head, size,
			    gfp & ~(__GFP_DMA | __GFP_HIGHMEM));
	if (c) {
		pte_t *pte = consistent_pte + CONSISTENT_OFFSET(c->vm_start);
		struct page *end = page + (1 << order);

		c->vm_pages = page;

		/*
		 * Set the "dma handle"
		 */
		*handle = page_to_phys(page);

		do {
			BUG_ON(!pte_none(*pte));

			/*
			 * x86 does not mark the pages reserved...
			 */
			SetPageReserved(page);
			set_pte(pte, mk_pte(page, prot));
			page++;
			pte++;
		} while (size -= PAGE_SIZE);

		/*
		 * Free the otherwise unused pages.
		 */
		while (page < end) {
			__free_page(page);
			page++;
		}

		return (void *)c->vm_start;
	}

	if (page)
		__free_pages(page, order);
no_page:
	*handle = ~0;
	return NULL;
}

static void nds32_dma_free(struct device *dev, size_t size, void *cpu_addr,
			   dma_addr_t handle, unsigned long attrs)
{
	struct arch_vm_region *c;
	unsigned long flags, addr;
	pte_t *ptep;

	size = PAGE_ALIGN(size);

	raw_spin_lock_irqsave(&consistent_lock, flags);

	c = vm_region_find(&consistent_head, (unsigned long)cpu_addr);
	if (!c)
		goto no_area;

	if ((c->vm_end - c->vm_start) != size) {
		pr_err("%s: freeing wrong coherent size (%ld != %d)\n",
		       __func__, c->vm_end - c->vm_start, size);
		dump_stack();
		size = c->vm_end - c->vm_start;
	}

	ptep = consistent_pte + CONSISTENT_OFFSET(c->vm_start);
	addr = c->vm_start;
	do {
		pte_t pte = ptep_get_and_clear(&init_mm, addr, ptep);
		unsigned long pfn;

		ptep++;
		addr += PAGE_SIZE;

		if (!pte_none(pte) && pte_present(pte)) {
			pfn = pte_pfn(pte);

			if (pfn_valid(pfn)) {
				struct page *page = pfn_to_page(pfn);

				/*
				 * x86 does not mark the pages reserved...
				 */
				ClearPageReserved(page);

				__free_page(page);
				continue;
			}
		}

		pr_crit("%s: bad page in kernel page table\n", __func__);
	} while (size -= PAGE_SIZE);

	flush_tlb_kernel_range(c->vm_start, c->vm_end);

	list_del(&c->vm_list);

	raw_spin_unlock_irqrestore(&consistent_lock, flags);

	kfree(c);
	return;

no_area:
	raw_spin_unlock_irqrestore(&consistent_lock, flags);
	pr_err("%s: trying to free invalid coherent area: %p\n",
	       __func__, cpu_addr);
	dump_stack();
}

/*
 * Initialise the consistent memory allocation.
 */
static int __init consistent_init(void)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	int ret = 0;

	do {
		pgd = pgd_offset(&init_mm, CONSISTENT_BASE);
		pmd = pmd_alloc(&init_mm, pgd, CONSISTENT_BASE);
		if (!pmd) {
			pr_err("%s: no pmd tables\n", __func__);
			ret = -ENOMEM;
			break;
		}
		/* The first level mapping may be created in somewhere.
		 * It's not necessary to warn here. */
		/* WARN_ON(!pmd_none(*pmd)); */

		pte = pte_alloc_kernel(pmd, CONSISTENT_BASE);
		if (!pte) {
			ret = -ENOMEM;
			break;
		}

		consistent_pte = pte;
	} while (0);

	return ret;
}

core_initcall(consistent_init);
static void consistent_sync(void *vaddr, size_t size, int direction, int master_type);
static dma_addr_t nds32_dma_map_page(struct device *dev, struct page *page,
				     unsigned long offset, size_t size,
				     enum dma_data_direction dir,
				     unsigned long attrs)
{
	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		consistent_sync((void *)(page_address(page) + offset), size, dir, FOR_DEVICE);
	return page_to_phys(page) + offset;
}

static void nds32_dma_unmap_page(struct device *dev, dma_addr_t handle,
				 size_t size, enum dma_data_direction dir,
				 unsigned long attrs)
{
	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		consistent_sync(phys_to_virt(handle), size, dir, FOR_CPU);
}

/*
 * Make an area consistent for devices.
 */
static void consistent_sync(void *vaddr, size_t size, int direction, int master_type)
{
	unsigned long start = (unsigned long)vaddr;
	unsigned long end = start + size;

	if (master_type == FOR_CPU) {
		switch (direction) {
		case DMA_TO_DEVICE:
			break;
		case DMA_FROM_DEVICE:
		case DMA_BIDIRECTIONAL:
			cpu_dma_inval_range(start, end);
			break;
		default:
			BUG();
		}
	} else {
		/* FOR_DEVICE */
		switch (direction) {
		case DMA_FROM_DEVICE:
			break;
		case DMA_TO_DEVICE:
		case DMA_BIDIRECTIONAL:
			cpu_dma_wb_range(start, end);
			break;
		default:
			BUG();
		}
	}
}

static int nds32_dma_map_sg(struct device *dev, struct scatterlist *sg,
			    int nents, enum dma_data_direction dir,
			    unsigned long attrs)
{
	int i;

	for (i = 0; i < nents; i++, sg++) {
		void *virt;
		unsigned long pfn;
		struct page *page = sg_page(sg);

		sg->dma_address = sg_phys(sg);
		pfn = page_to_pfn(page) + sg->offset / PAGE_SIZE;
		page = pfn_to_page(pfn);
		if (PageHighMem(page)) {
			virt = kmap_atomic(page);
			consistent_sync(virt, sg->length, dir, FOR_CPU);
			kunmap_atomic(virt);
		} else {
			if (sg->offset > PAGE_SIZE)
				panic("sg->offset:%08x > PAGE_SIZE\n",
				      sg->offset);
			virt = page_address(page) + sg->offset;
			consistent_sync(virt, sg->length, dir, FOR_CPU);
		}
	}
	return nents;
}

static void nds32_dma_unmap_sg(struct device *dev, struct scatterlist *sg,
			       int nhwentries, enum dma_data_direction dir,
			       unsigned long attrs)
{
}

static void
nds32_dma_sync_single_for_cpu(struct device *dev, dma_addr_t handle,
			      size_t size, enum dma_data_direction dir)
{
	consistent_sync((void *)phys_to_virt(handle), size, dir, FOR_CPU);
}

static void
nds32_dma_sync_single_for_device(struct device *dev, dma_addr_t handle,
				 size_t size, enum dma_data_direction dir)
{
	consistent_sync((void *)phys_to_virt(handle), size, dir, FOR_DEVICE);
}

static void
nds32_dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg, int nents,
			  enum dma_data_direction dir)
{
	int i;

	for (i = 0; i < nents; i++, sg++) {
		char *virt =
		    page_address((struct page *)sg->page_link) + sg->offset;
		consistent_sync(virt, sg->length, dir, FOR_CPU);
	}
}

static void
nds32_dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
			     int nents, enum dma_data_direction dir)
{
	int i;

	for (i = 0; i < nents; i++, sg++) {
		char *virt =
		    page_address((struct page *)sg->page_link) + sg->offset;
		consistent_sync(virt, sg->length, dir, FOR_DEVICE);
	}
}

struct dma_map_ops nds32_dma_ops = {
	.alloc = nds32_dma_alloc_coherent,
	.free = nds32_dma_free,
	.map_page = nds32_dma_map_page,
	.unmap_page = nds32_dma_unmap_page,
	.map_sg = nds32_dma_map_sg,
	.unmap_sg = nds32_dma_unmap_sg,
	.sync_single_for_device = nds32_dma_sync_single_for_device,
	.sync_single_for_cpu = nds32_dma_sync_single_for_cpu,
	.sync_sg_for_cpu = nds32_dma_sync_sg_for_cpu,
	.sync_sg_for_device = nds32_dma_sync_sg_for_device,
};

EXPORT_SYMBOL(nds32_dma_ops);
