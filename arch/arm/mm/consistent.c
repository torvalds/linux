/*
 *  linux/arch/arm/mm/consistent.c
 *
 *  Copyright (C) 2000-2004 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  DMA uncached mapping support.
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>

#include <asm/memory.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/sizes.h>

/* Sanity check size */
#if (CONSISTENT_DMA_SIZE % SZ_2M)
#error "CONSISTENT_DMA_SIZE must be multiple of 2MiB"
#endif

#define CONSISTENT_END	(0xffe00000)
#define CONSISTENT_BASE	(CONSISTENT_END - CONSISTENT_DMA_SIZE)

#define CONSISTENT_OFFSET(x)	(((unsigned long)(x) - CONSISTENT_BASE) >> PAGE_SHIFT)
#define CONSISTENT_PTE_INDEX(x) (((unsigned long)(x) - CONSISTENT_BASE) >> PGDIR_SHIFT)
#define NUM_CONSISTENT_PTES (CONSISTENT_DMA_SIZE >> PGDIR_SHIFT)


/*
 * These are the page tables (2MB each) covering uncached, DMA consistent allocations
 */
static pte_t *consistent_pte[NUM_CONSISTENT_PTES];
static DEFINE_SPINLOCK(consistent_lock);

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
struct vm_region {
	struct list_head	vm_list;
	unsigned long		vm_start;
	unsigned long		vm_end;
	struct page		*vm_pages;
	int			vm_active;
};

static struct vm_region consistent_head = {
	.vm_list	= LIST_HEAD_INIT(consistent_head.vm_list),
	.vm_start	= CONSISTENT_BASE,
	.vm_end		= CONSISTENT_END,
};

static struct vm_region *
vm_region_alloc(struct vm_region *head, size_t size, gfp_t gfp)
{
	unsigned long addr = head->vm_start, end = head->vm_end - size;
	unsigned long flags;
	struct vm_region *c, *new;

	new = kmalloc(sizeof(struct vm_region), gfp);
	if (!new)
		goto out;

	spin_lock_irqsave(&consistent_lock, flags);

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
	new->vm_active = 1;

	spin_unlock_irqrestore(&consistent_lock, flags);
	return new;

 nospc:
	spin_unlock_irqrestore(&consistent_lock, flags);
	kfree(new);
 out:
	return NULL;
}

static struct vm_region *vm_region_find(struct vm_region *head, unsigned long addr)
{
	struct vm_region *c;
	
	list_for_each_entry(c, &head->vm_list, vm_list) {
		if (c->vm_active && c->vm_start == addr)
			goto out;
	}
	c = NULL;
 out:
	return c;
}

#ifdef CONFIG_HUGETLB_PAGE
#error ARM Coherent DMA allocator does not (yet) support huge TLB
#endif

static void *
__dma_alloc(struct device *dev, size_t size, dma_addr_t *handle, gfp_t gfp,
	    pgprot_t prot)
{
	struct page *page;
	struct vm_region *c;
	unsigned long order;
	u64 mask = ISA_DMA_THRESHOLD, limit;

	if (!consistent_pte[0]) {
		printk(KERN_ERR "%s: not initialised\n", __func__);
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

		if ((~mask) & ISA_DMA_THRESHOLD) {
			dev_warn(dev, "coherent DMA mask %#llx is smaller "
				 "than system GFP_DMA mask %#llx\n",
				 mask, (unsigned long long)ISA_DMA_THRESHOLD);
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
		printk(KERN_WARNING "coherent allocation too big "
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
		dmac_flush_range(kaddr, kaddr + size);
	}

	/*
	 * Allocate a virtual address in the consistent mapping region.
	 */
	c = vm_region_alloc(&consistent_head, size,
			    gfp & ~(__GFP_DMA | __GFP_HIGHMEM));
	if (c) {
		pte_t *pte;
		struct page *end = page + (1 << order);
		int idx = CONSISTENT_PTE_INDEX(c->vm_start);
		u32 off = CONSISTENT_OFFSET(c->vm_start) & (PTRS_PER_PTE-1);

		pte = consistent_pte[idx] + off;
		c->vm_pages = page;

		split_page(page, order);

		/*
		 * Set the "dma handle"
		 */
		*handle = page_to_dma(dev, page);

		do {
			BUG_ON(!pte_none(*pte));

			/*
			 * x86 does not mark the pages reserved...
			 */
			SetPageReserved(page);
			set_pte(pte, mk_pte(page, prot));
			page++;
			pte++;
			off++;
			if (off >= PTRS_PER_PTE) {
				off = 0;
				pte = consistent_pte[++idx];
			}
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

/*
 * Allocate DMA-coherent memory space and return both the kernel remapped
 * virtual and bus address for that space.
 */
void *
dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *handle, gfp_t gfp)
{
	if (arch_is_coherent()) {
		void *virt;

		virt = kmalloc(size, gfp);
		if (!virt)
			return NULL;
		*handle =  virt_to_dma(dev, virt);

		return virt;
	}

	return __dma_alloc(dev, size, handle, gfp,
			   pgprot_noncached(pgprot_kernel));
}
EXPORT_SYMBOL(dma_alloc_coherent);

/*
 * Allocate a writecombining region, in much the same way as
 * dma_alloc_coherent above.
 */
void *
dma_alloc_writecombine(struct device *dev, size_t size, dma_addr_t *handle, gfp_t gfp)
{
	return __dma_alloc(dev, size, handle, gfp,
			   pgprot_writecombine(pgprot_kernel));
}
EXPORT_SYMBOL(dma_alloc_writecombine);

static int dma_mmap(struct device *dev, struct vm_area_struct *vma,
		    void *cpu_addr, dma_addr_t dma_addr, size_t size)
{
	unsigned long flags, user_size, kern_size;
	struct vm_region *c;
	int ret = -ENXIO;

	user_size = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;

	spin_lock_irqsave(&consistent_lock, flags);
	c = vm_region_find(&consistent_head, (unsigned long)cpu_addr);
	spin_unlock_irqrestore(&consistent_lock, flags);

	if (c) {
		unsigned long off = vma->vm_pgoff;

		kern_size = (c->vm_end - c->vm_start) >> PAGE_SHIFT;

		if (off < kern_size &&
		    user_size <= (kern_size - off)) {
			vma->vm_flags |= VM_RESERVED;
			ret = remap_pfn_range(vma, vma->vm_start,
					      page_to_pfn(c->vm_pages) + off,
					      user_size << PAGE_SHIFT,
					      vma->vm_page_prot);
		}
	}

	return ret;
}

int dma_mmap_coherent(struct device *dev, struct vm_area_struct *vma,
		      void *cpu_addr, dma_addr_t dma_addr, size_t size)
{
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	return dma_mmap(dev, vma, cpu_addr, dma_addr, size);
}
EXPORT_SYMBOL(dma_mmap_coherent);

int dma_mmap_writecombine(struct device *dev, struct vm_area_struct *vma,
			  void *cpu_addr, dma_addr_t dma_addr, size_t size)
{
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	return dma_mmap(dev, vma, cpu_addr, dma_addr, size);
}
EXPORT_SYMBOL(dma_mmap_writecombine);

/*
 * free a page as defined by the above mapping.
 * Must not be called with IRQs disabled.
 */
void dma_free_coherent(struct device *dev, size_t size, void *cpu_addr, dma_addr_t handle)
{
	struct vm_region *c;
	unsigned long flags, addr;
	pte_t *ptep;
	int idx;
	u32 off;

	WARN_ON(irqs_disabled());

	if (arch_is_coherent()) {
		kfree(cpu_addr);
		return;
	}

	size = PAGE_ALIGN(size);

	spin_lock_irqsave(&consistent_lock, flags);
	c = vm_region_find(&consistent_head, (unsigned long)cpu_addr);
	if (!c)
		goto no_area;

	c->vm_active = 0;
	spin_unlock_irqrestore(&consistent_lock, flags);

	if ((c->vm_end - c->vm_start) != size) {
		printk(KERN_ERR "%s: freeing wrong coherent size (%ld != %d)\n",
		       __func__, c->vm_end - c->vm_start, size);
		dump_stack();
		size = c->vm_end - c->vm_start;
	}

	idx = CONSISTENT_PTE_INDEX(c->vm_start);
	off = CONSISTENT_OFFSET(c->vm_start) & (PTRS_PER_PTE-1);
	ptep = consistent_pte[idx] + off;
	addr = c->vm_start;
	do {
		pte_t pte = ptep_get_and_clear(&init_mm, addr, ptep);
		unsigned long pfn;

		ptep++;
		addr += PAGE_SIZE;
		off++;
		if (off >= PTRS_PER_PTE) {
			off = 0;
			ptep = consistent_pte[++idx];
		}

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

		printk(KERN_CRIT "%s: bad page in kernel page table\n",
		       __func__);
	} while (size -= PAGE_SIZE);

	flush_tlb_kernel_range(c->vm_start, c->vm_end);

	spin_lock_irqsave(&consistent_lock, flags);
	list_del(&c->vm_list);
	spin_unlock_irqrestore(&consistent_lock, flags);

	kfree(c);
	return;

 no_area:
	spin_unlock_irqrestore(&consistent_lock, flags);
	printk(KERN_ERR "%s: trying to free invalid coherent area: %p\n",
	       __func__, cpu_addr);
	dump_stack();
}
EXPORT_SYMBOL(dma_free_coherent);

/*
 * Initialise the consistent memory allocation.
 */
static int __init consistent_init(void)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	int ret = 0, i = 0;
	u32 base = CONSISTENT_BASE;

	do {
		pgd = pgd_offset(&init_mm, base);
		pmd = pmd_alloc(&init_mm, pgd, base);
		if (!pmd) {
			printk(KERN_ERR "%s: no pmd tables\n", __func__);
			ret = -ENOMEM;
			break;
		}
		WARN_ON(!pmd_none(*pmd));

		pte = pte_alloc_kernel(pmd, base);
		if (!pte) {
			printk(KERN_ERR "%s: no pte tables\n", __func__);
			ret = -ENOMEM;
			break;
		}

		consistent_pte[i++] = pte;
		base += (1 << PGDIR_SHIFT);
	} while (base < CONSISTENT_END);

	return ret;
}

core_initcall(consistent_init);

/*
 * Make an area consistent for devices.
 */
void consistent_sync(void *vaddr, size_t size, int direction)
{
	unsigned long start = (unsigned long)vaddr;
	unsigned long end   = start + size;

	switch (direction) {
	case DMA_FROM_DEVICE:		/* invalidate only */
		dmac_inv_range(start, end);
		break;
	case DMA_TO_DEVICE:		/* writeback only */
		dmac_clean_range(start, end);
		break;
	case DMA_BIDIRECTIONAL:		/* writeback and invalidate */
		dmac_flush_range(start, end);
		break;
	default:
		BUG();
	}
}
EXPORT_SYMBOL(consistent_sync);
