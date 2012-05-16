/*
 *  linux/arch/arm/mm/dma-mapping.c
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
#include <linux/gfp.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <linux/slab.h>

#include <asm/memory.h>
#include <asm/highmem.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/sizes.h>
#include <asm/mach/arch.h>

#include "mm.h"

/*
 * The DMA API is built upon the notion of "buffer ownership".  A buffer
 * is either exclusively owned by the CPU (and therefore may be accessed
 * by it) or exclusively owned by the DMA device.  These helper functions
 * represent the transitions between these two ownership states.
 *
 * Note, however, that on later ARMs, this notion does not work due to
 * speculative prefetches.  We model our approach on the assumption that
 * the CPU does do speculative prefetches, which means we clean caches
 * before transfers and delay cache invalidation until transfer completion.
 *
 */
static void __dma_page_cpu_to_dev(struct page *, unsigned long,
		size_t, enum dma_data_direction);
static void __dma_page_dev_to_cpu(struct page *, unsigned long,
		size_t, enum dma_data_direction);

/**
 * arm_dma_map_page - map a portion of a page for streaming DMA
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @page: page that buffer resides in
 * @offset: offset into page for start of buffer
 * @size: size of buffer to map
 * @dir: DMA transfer direction
 *
 * Ensure that any data held in the cache is appropriately discarded
 * or written back.
 *
 * The device owns this memory once this call has completed.  The CPU
 * can regain ownership by calling dma_unmap_page().
 */
static dma_addr_t arm_dma_map_page(struct device *dev, struct page *page,
	     unsigned long offset, size_t size, enum dma_data_direction dir,
	     struct dma_attrs *attrs)
{
	if (!arch_is_coherent())
		__dma_page_cpu_to_dev(page, offset, size, dir);
	return pfn_to_dma(dev, page_to_pfn(page)) + offset;
}

/**
 * arm_dma_unmap_page - unmap a buffer previously mapped through dma_map_page()
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @handle: DMA address of buffer
 * @size: size of buffer (same as passed to dma_map_page)
 * @dir: DMA transfer direction (same as passed to dma_map_page)
 *
 * Unmap a page streaming mode DMA translation.  The handle and size
 * must match what was provided in the previous dma_map_page() call.
 * All other usages are undefined.
 *
 * After this call, reads by the CPU to the buffer are guaranteed to see
 * whatever the device wrote there.
 */
static void arm_dma_unmap_page(struct device *dev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir,
		struct dma_attrs *attrs)
{
	if (!arch_is_coherent())
		__dma_page_dev_to_cpu(pfn_to_page(dma_to_pfn(dev, handle)),
				      handle & ~PAGE_MASK, size, dir);
}

static void arm_dma_sync_single_for_cpu(struct device *dev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	unsigned int offset = handle & (PAGE_SIZE - 1);
	struct page *page = pfn_to_page(dma_to_pfn(dev, handle-offset));
	if (!arch_is_coherent())
		__dma_page_dev_to_cpu(page, offset, size, dir);
}

static void arm_dma_sync_single_for_device(struct device *dev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	unsigned int offset = handle & (PAGE_SIZE - 1);
	struct page *page = pfn_to_page(dma_to_pfn(dev, handle-offset));
	if (!arch_is_coherent())
		__dma_page_cpu_to_dev(page, offset, size, dir);
}

static int arm_dma_set_mask(struct device *dev, u64 dma_mask);

struct dma_map_ops arm_dma_ops = {
	.alloc			= arm_dma_alloc,
	.free			= arm_dma_free,
	.mmap			= arm_dma_mmap,
	.map_page		= arm_dma_map_page,
	.unmap_page		= arm_dma_unmap_page,
	.map_sg			= arm_dma_map_sg,
	.unmap_sg		= arm_dma_unmap_sg,
	.sync_single_for_cpu	= arm_dma_sync_single_for_cpu,
	.sync_single_for_device	= arm_dma_sync_single_for_device,
	.sync_sg_for_cpu	= arm_dma_sync_sg_for_cpu,
	.sync_sg_for_device	= arm_dma_sync_sg_for_device,
	.set_dma_mask		= arm_dma_set_mask,
};
EXPORT_SYMBOL(arm_dma_ops);

static u64 get_coherent_dma_mask(struct device *dev)
{
	u64 mask = (u64)arm_dma_limit;

	if (dev) {
		mask = dev->coherent_dma_mask;

		/*
		 * Sanity check the DMA mask - it must be non-zero, and
		 * must be able to be satisfied by a DMA allocation.
		 */
		if (mask == 0) {
			dev_warn(dev, "coherent DMA mask is unset\n");
			return 0;
		}

		if ((~mask) & (u64)arm_dma_limit) {
			dev_warn(dev, "coherent DMA mask %#llx is smaller "
				 "than system GFP_DMA mask %#llx\n",
				 mask, (u64)arm_dma_limit);
			return 0;
		}
	}

	return mask;
}

/*
 * Allocate a DMA buffer for 'dev' of size 'size' using the
 * specified gfp mask.  Note that 'size' must be page aligned.
 */
static struct page *__dma_alloc_buffer(struct device *dev, size_t size, gfp_t gfp)
{
	unsigned long order = get_order(size);
	struct page *page, *p, *e;
	void *ptr;
	u64 mask = get_coherent_dma_mask(dev);

#ifdef CONFIG_DMA_API_DEBUG
	u64 limit = (mask + 1) & ~mask;
	if (limit && size >= limit) {
		dev_warn(dev, "coherent allocation too big (requested %#x mask %#llx)\n",
			size, mask);
		return NULL;
	}
#endif

	if (!mask)
		return NULL;

	if (mask < 0xffffffffULL)
		gfp |= GFP_DMA;

	page = alloc_pages(gfp, order);
	if (!page)
		return NULL;

	/*
	 * Now split the huge page and free the excess pages
	 */
	split_page(page, order);
	for (p = page + (size >> PAGE_SHIFT), e = page + (1 << order); p < e; p++)
		__free_page(p);

	/*
	 * Ensure that the allocated pages are zeroed, and that any data
	 * lurking in the kernel direct-mapped region is invalidated.
	 */
	ptr = page_address(page);
	memset(ptr, 0, size);
	dmac_flush_range(ptr, ptr + size);
	outer_flush_range(__pa(ptr), __pa(ptr) + size);

	return page;
}

/*
 * Free a DMA buffer.  'size' must be page aligned.
 */
static void __dma_free_buffer(struct page *page, size_t size)
{
	struct page *e = page + (size >> PAGE_SHIFT);

	while (page < e) {
		__free_page(page);
		page++;
	}
}

#ifdef CONFIG_MMU

#define CONSISTENT_OFFSET(x)	(((unsigned long)(x) - consistent_base) >> PAGE_SHIFT)
#define CONSISTENT_PTE_INDEX(x) (((unsigned long)(x) - consistent_base) >> PMD_SHIFT)

/*
 * These are the page tables (2MB each) covering uncached, DMA consistent allocations
 */
static pte_t **consistent_pte;

#define DEFAULT_CONSISTENT_DMA_SIZE SZ_2M

unsigned long consistent_base = CONSISTENT_END - DEFAULT_CONSISTENT_DMA_SIZE;

void __init init_consistent_dma_size(unsigned long size)
{
	unsigned long base = CONSISTENT_END - ALIGN(size, SZ_2M);

	BUG_ON(consistent_pte); /* Check we're called before DMA region init */
	BUG_ON(base < VMALLOC_END);

	/* Grow region to accommodate specified size  */
	if (base < consistent_base)
		consistent_base = base;
}

#include "vmregion.h"

static struct arm_vmregion_head consistent_head = {
	.vm_lock	= __SPIN_LOCK_UNLOCKED(&consistent_head.vm_lock),
	.vm_list	= LIST_HEAD_INIT(consistent_head.vm_list),
	.vm_end		= CONSISTENT_END,
};

#ifdef CONFIG_HUGETLB_PAGE
#error ARM Coherent DMA allocator does not (yet) support huge TLB
#endif

/*
 * Initialise the consistent memory allocation.
 */
static int __init consistent_init(void)
{
	int ret = 0;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	int i = 0;
	unsigned long base = consistent_base;
	unsigned long num_ptes = (CONSISTENT_END - base) >> PMD_SHIFT;

	consistent_pte = kmalloc(num_ptes * sizeof(pte_t), GFP_KERNEL);
	if (!consistent_pte) {
		pr_err("%s: no memory\n", __func__);
		return -ENOMEM;
	}

	pr_debug("DMA memory: 0x%08lx - 0x%08lx:\n", base, CONSISTENT_END);
	consistent_head.vm_start = base;

	do {
		pgd = pgd_offset(&init_mm, base);

		pud = pud_alloc(&init_mm, pgd, base);
		if (!pud) {
			pr_err("%s: no pud tables\n", __func__);
			ret = -ENOMEM;
			break;
		}

		pmd = pmd_alloc(&init_mm, pud, base);
		if (!pmd) {
			pr_err("%s: no pmd tables\n", __func__);
			ret = -ENOMEM;
			break;
		}
		WARN_ON(!pmd_none(*pmd));

		pte = pte_alloc_kernel(pmd, base);
		if (!pte) {
			pr_err("%s: no pte tables\n", __func__);
			ret = -ENOMEM;
			break;
		}

		consistent_pte[i++] = pte;
		base += PMD_SIZE;
	} while (base < CONSISTENT_END);

	return ret;
}

core_initcall(consistent_init);

static void *
__dma_alloc_remap(struct page *page, size_t size, gfp_t gfp, pgprot_t prot,
	const void *caller)
{
	struct arm_vmregion *c;
	size_t align;
	int bit;

	if (!consistent_pte) {
		pr_err("%s: not initialised\n", __func__);
		dump_stack();
		return NULL;
	}

	/*
	 * Align the virtual region allocation - maximum alignment is
	 * a section size, minimum is a page size.  This helps reduce
	 * fragmentation of the DMA space, and also prevents allocations
	 * smaller than a section from crossing a section boundary.
	 */
	bit = fls(size - 1);
	if (bit > SECTION_SHIFT)
		bit = SECTION_SHIFT;
	align = 1 << bit;

	/*
	 * Allocate a virtual address in the consistent mapping region.
	 */
	c = arm_vmregion_alloc(&consistent_head, align, size,
			    gfp & ~(__GFP_DMA | __GFP_HIGHMEM), caller);
	if (c) {
		pte_t *pte;
		int idx = CONSISTENT_PTE_INDEX(c->vm_start);
		u32 off = CONSISTENT_OFFSET(c->vm_start) & (PTRS_PER_PTE-1);

		pte = consistent_pte[idx] + off;
		c->vm_pages = page;

		do {
			BUG_ON(!pte_none(*pte));

			set_pte_ext(pte, mk_pte(page, prot), 0);
			page++;
			pte++;
			off++;
			if (off >= PTRS_PER_PTE) {
				off = 0;
				pte = consistent_pte[++idx];
			}
		} while (size -= PAGE_SIZE);

		dsb();

		return (void *)c->vm_start;
	}
	return NULL;
}

static void __dma_free_remap(void *cpu_addr, size_t size)
{
	struct arm_vmregion *c;
	unsigned long addr;
	pte_t *ptep;
	int idx;
	u32 off;

	c = arm_vmregion_find_remove(&consistent_head, (unsigned long)cpu_addr);
	if (!c) {
		pr_err("%s: trying to free invalid coherent area: %p\n",
		       __func__, cpu_addr);
		dump_stack();
		return;
	}

	if ((c->vm_end - c->vm_start) != size) {
		pr_err("%s: freeing wrong coherent size (%ld != %d)\n",
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

		ptep++;
		addr += PAGE_SIZE;
		off++;
		if (off >= PTRS_PER_PTE) {
			off = 0;
			ptep = consistent_pte[++idx];
		}

		if (pte_none(pte) || !pte_present(pte))
			pr_crit("%s: bad page in kernel page table\n",
				__func__);
	} while (size -= PAGE_SIZE);

	flush_tlb_kernel_range(c->vm_start, c->vm_end);

	arm_vmregion_free(&consistent_head, c);
}

static inline pgprot_t __get_dma_pgprot(struct dma_attrs *attrs, pgprot_t prot)
{
	prot = dma_get_attr(DMA_ATTR_WRITE_COMBINE, attrs) ?
			    pgprot_writecombine(prot) :
			    pgprot_dmacoherent(prot);
	return prot;
}

#else	/* !CONFIG_MMU */

#define __dma_alloc_remap(page, size, gfp, prot, c)	page_address(page)
#define __dma_free_remap(addr, size)			do { } while (0)
#define __get_dma_pgprot(attrs, prot)	__pgprot(0)

#endif	/* CONFIG_MMU */

static void *
__dma_alloc(struct device *dev, size_t size, dma_addr_t *handle, gfp_t gfp,
	    pgprot_t prot, const void *caller)
{
	struct page *page;
	void *addr;

	/*
	 * Following is a work-around (a.k.a. hack) to prevent pages
	 * with __GFP_COMP being passed to split_page() which cannot
	 * handle them.  The real problem is that this flag probably
	 * should be 0 on ARM as it is not supported on this
	 * platform; see CONFIG_HUGETLBFS.
	 */
	gfp &= ~(__GFP_COMP);

	*handle = DMA_ERROR_CODE;
	size = PAGE_ALIGN(size);

	page = __dma_alloc_buffer(dev, size, gfp);
	if (!page)
		return NULL;

	if (!arch_is_coherent())
		addr = __dma_alloc_remap(page, size, gfp, prot, caller);
	else
		addr = page_address(page);

	if (addr)
		*handle = pfn_to_dma(dev, page_to_pfn(page));
	else
		__dma_free_buffer(page, size);

	return addr;
}

/*
 * Allocate DMA-coherent memory space and return both the kernel remapped
 * virtual and bus address for that space.
 */
void *arm_dma_alloc(struct device *dev, size_t size, dma_addr_t *handle,
		    gfp_t gfp, struct dma_attrs *attrs)
{
	pgprot_t prot = __get_dma_pgprot(attrs, pgprot_kernel);
	void *memory;

	if (dma_alloc_from_coherent(dev, size, handle, &memory))
		return memory;

	return __dma_alloc(dev, size, handle, gfp, prot,
			   __builtin_return_address(0));
}

/*
 * Create userspace mapping for the DMA-coherent memory.
 */
int arm_dma_mmap(struct device *dev, struct vm_area_struct *vma,
		 void *cpu_addr, dma_addr_t dma_addr, size_t size,
		 struct dma_attrs *attrs)
{
	int ret = -ENXIO;
#ifdef CONFIG_MMU
	unsigned long user_size, kern_size;
	struct arm_vmregion *c;

	vma->vm_page_prot = __get_dma_pgprot(attrs, vma->vm_page_prot);

	if (dma_mmap_from_coherent(dev, vma, cpu_addr, size, &ret))
		return ret;

	user_size = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;

	c = arm_vmregion_find(&consistent_head, (unsigned long)cpu_addr);
	if (c) {
		unsigned long off = vma->vm_pgoff;

		kern_size = (c->vm_end - c->vm_start) >> PAGE_SHIFT;

		if (off < kern_size &&
		    user_size <= (kern_size - off)) {
			ret = remap_pfn_range(vma, vma->vm_start,
					      page_to_pfn(c->vm_pages) + off,
					      user_size << PAGE_SHIFT,
					      vma->vm_page_prot);
		}
	}
#endif	/* CONFIG_MMU */

	return ret;
}

/*
 * free a page as defined by the above mapping.
 * Must not be called with IRQs disabled.
 */
void arm_dma_free(struct device *dev, size_t size, void *cpu_addr,
		  dma_addr_t handle, struct dma_attrs *attrs)
{
	WARN_ON(irqs_disabled());

	if (dma_release_from_coherent(dev, get_order(size), cpu_addr))
		return;

	size = PAGE_ALIGN(size);

	if (!arch_is_coherent())
		__dma_free_remap(cpu_addr, size);

	__dma_free_buffer(pfn_to_page(dma_to_pfn(dev, handle)), size);
}

static void dma_cache_maint_page(struct page *page, unsigned long offset,
	size_t size, enum dma_data_direction dir,
	void (*op)(const void *, size_t, int))
{
	/*
	 * A single sg entry may refer to multiple physically contiguous
	 * pages.  But we still need to process highmem pages individually.
	 * If highmem is not configured then the bulk of this loop gets
	 * optimized out.
	 */
	size_t left = size;
	do {
		size_t len = left;
		void *vaddr;

		if (PageHighMem(page)) {
			if (len + offset > PAGE_SIZE) {
				if (offset >= PAGE_SIZE) {
					page += offset / PAGE_SIZE;
					offset %= PAGE_SIZE;
				}
				len = PAGE_SIZE - offset;
			}
			vaddr = kmap_high_get(page);
			if (vaddr) {
				vaddr += offset;
				op(vaddr, len, dir);
				kunmap_high(page);
			} else if (cache_is_vipt()) {
				/* unmapped pages might still be cached */
				vaddr = kmap_atomic(page);
				op(vaddr + offset, len, dir);
				kunmap_atomic(vaddr);
			}
		} else {
			vaddr = page_address(page) + offset;
			op(vaddr, len, dir);
		}
		offset = 0;
		page++;
		left -= len;
	} while (left);
}

/*
 * Make an area consistent for devices.
 * Note: Drivers should NOT use this function directly, as it will break
 * platforms with CONFIG_DMABOUNCE.
 * Use the driver DMA support - see dma-mapping.h (dma_sync_*)
 */
static void __dma_page_cpu_to_dev(struct page *page, unsigned long off,
	size_t size, enum dma_data_direction dir)
{
	unsigned long paddr;

	dma_cache_maint_page(page, off, size, dir, dmac_map_area);

	paddr = page_to_phys(page) + off;
	if (dir == DMA_FROM_DEVICE) {
		outer_inv_range(paddr, paddr + size);
	} else {
		outer_clean_range(paddr, paddr + size);
	}
	/* FIXME: non-speculating: flush on bidirectional mappings? */
}

static void __dma_page_dev_to_cpu(struct page *page, unsigned long off,
	size_t size, enum dma_data_direction dir)
{
	unsigned long paddr = page_to_phys(page) + off;

	/* FIXME: non-speculating: not required */
	/* don't bother invalidating if DMA to device */
	if (dir != DMA_TO_DEVICE)
		outer_inv_range(paddr, paddr + size);

	dma_cache_maint_page(page, off, size, dir, dmac_unmap_area);

	/*
	 * Mark the D-cache clean for this page to avoid extra flushing.
	 */
	if (dir != DMA_TO_DEVICE && off == 0 && size >= PAGE_SIZE)
		set_bit(PG_dcache_clean, &page->flags);
}

/**
 * arm_dma_map_sg - map a set of SG buffers for streaming mode DMA
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @sg: list of buffers
 * @nents: number of buffers to map
 * @dir: DMA transfer direction
 *
 * Map a set of buffers described by scatterlist in streaming mode for DMA.
 * This is the scatter-gather version of the dma_map_single interface.
 * Here the scatter gather list elements are each tagged with the
 * appropriate dma address and length.  They are obtained via
 * sg_dma_{address,length}.
 *
 * Device ownership issues as mentioned for dma_map_single are the same
 * here.
 */
int arm_dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
		enum dma_data_direction dir, struct dma_attrs *attrs)
{
	struct dma_map_ops *ops = get_dma_ops(dev);
	struct scatterlist *s;
	int i, j;

	for_each_sg(sg, s, nents, i) {
		s->dma_address = ops->map_page(dev, sg_page(s), s->offset,
						s->length, dir, attrs);
		if (dma_mapping_error(dev, s->dma_address))
			goto bad_mapping;
	}
	return nents;

 bad_mapping:
	for_each_sg(sg, s, i, j)
		ops->unmap_page(dev, sg_dma_address(s), sg_dma_len(s), dir, attrs);
	return 0;
}

/**
 * arm_dma_unmap_sg - unmap a set of SG buffers mapped by dma_map_sg
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @sg: list of buffers
 * @nents: number of buffers to unmap (same as was passed to dma_map_sg)
 * @dir: DMA transfer direction (same as was passed to dma_map_sg)
 *
 * Unmap a set of streaming mode DMA translations.  Again, CPU access
 * rules concerning calls here are the same as for dma_unmap_single().
 */
void arm_dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nents,
		enum dma_data_direction dir, struct dma_attrs *attrs)
{
	struct dma_map_ops *ops = get_dma_ops(dev);
	struct scatterlist *s;

	int i;

	for_each_sg(sg, s, nents, i)
		ops->unmap_page(dev, sg_dma_address(s), sg_dma_len(s), dir, attrs);
}

/**
 * arm_dma_sync_sg_for_cpu
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @sg: list of buffers
 * @nents: number of buffers to map (returned from dma_map_sg)
 * @dir: DMA transfer direction (same as was passed to dma_map_sg)
 */
void arm_dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg,
			int nents, enum dma_data_direction dir)
{
	struct dma_map_ops *ops = get_dma_ops(dev);
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i)
		ops->sync_single_for_cpu(dev, sg_dma_address(s), s->length,
					 dir);
}

/**
 * arm_dma_sync_sg_for_device
 * @dev: valid struct device pointer, or NULL for ISA and EISA-like devices
 * @sg: list of buffers
 * @nents: number of buffers to map (returned from dma_map_sg)
 * @dir: DMA transfer direction (same as was passed to dma_map_sg)
 */
void arm_dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
			int nents, enum dma_data_direction dir)
{
	struct dma_map_ops *ops = get_dma_ops(dev);
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i)
		ops->sync_single_for_device(dev, sg_dma_address(s), s->length,
					    dir);
}

/*
 * Return whether the given device DMA address mask can be supported
 * properly.  For example, if your device can only drive the low 24-bits
 * during bus mastering, then you would pass 0x00ffffff as the mask
 * to this function.
 */
int dma_supported(struct device *dev, u64 mask)
{
	if (mask < (u64)arm_dma_limit)
		return 0;
	return 1;
}
EXPORT_SYMBOL(dma_supported);

static int arm_dma_set_mask(struct device *dev, u64 dma_mask)
{
	if (!dev->dma_mask || !dma_supported(dev, dma_mask))
		return -EIO;

	*dev->dma_mask = dma_mask;

	return 0;
}

#define PREALLOC_DMA_DEBUG_ENTRIES	4096

static int __init dma_debug_do_init(void)
{
#ifdef CONFIG_MMU
	arm_vmregion_create_proc("dma-mappings", &consistent_head);
#endif
	dma_debug_init(PREALLOC_DMA_DEBUG_ENTRIES);
	return 0;
}
fs_initcall(dma_debug_do_init);
