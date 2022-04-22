// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, 2020-2021, The Linux Foundation. All rights reserved.
 * Contiguous Memory Allocator for DMA mapping framework
 * Copyright (c) 2010-2011 by Samsung Electronics.
 * Written by:
 *	Marek Szyprowski <m.szyprowski@samsung.com>
 *	Michal Nazarewicz <mina86@mina86.com>
 * Copyright (C) 2012, 2014-2015 ARM Ltd.
 */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/genalloc.h>
#include <linux/dma-direct.h>
#include <linux/cma.h>
#include <linux/iova.h>
#include <linux/dma-map-ops.h>
#include <linux/dma-mapping.h>
#include <linux/qcom-dma-mapping.h>
#include <linux/of_reserved_mem.h>
#include <linux/iommu.h>
#include <linux/qcom-iommu-util.h>
#include <linux/workqueue.h>
#include <linux/debugfs.h>
#include "qcom-dma-iommu-generic.h"

static bool probe_finished;
static struct device *qcom_dma_iommu_dev;
static struct cma *qcom_dma_contiguous_default_area;

struct pci_host_bridge *qcom_pci_find_host_bridge(struct pci_bus *bus)
{
	while (bus->parent)
		bus = bus->parent;

	return to_pci_host_bridge(bus->bridge);
}

/*
 * This avoids arch-specific assembly, but may be slower since it calls
 * back into the dma layer again.
 */
void qcom_arch_sync_dma_for_device(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	dma_addr_t dma_addr = phys_to_dma(qcom_dma_iommu_dev, paddr);

	dma_sync_single_for_device(qcom_dma_iommu_dev,
		dma_addr, size, dir);
}

void qcom_arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	dma_addr_t dma_addr = phys_to_dma(qcom_dma_iommu_dev, paddr);

	dma_sync_single_for_cpu(qcom_dma_iommu_dev,
		dma_addr, size, dir);
}

void qcom_arch_dma_prep_coherent(struct page *page, size_t size)
{
	phys_addr_t phys = page_to_phys(page);
	dma_addr_t dma_addr = phys_to_dma(qcom_dma_iommu_dev, phys);

	dma_sync_single_for_device(qcom_dma_iommu_dev,
		dma_addr, size, DMA_TO_DEVICE);
}

static struct cma *qcom_dev_get_cma_area(struct device *dev)
{
	if (dev && dev->cma_area)
		return dev->cma_area;
	return qcom_dma_contiguous_default_area;
}

struct page *qcom_dma_alloc_from_contiguous(struct device *dev, size_t count,
				       unsigned int align, bool no_warn)
{
	if (align > CONFIG_CMA_ALIGNMENT)
		align = CONFIG_CMA_ALIGNMENT;

	return cma_alloc(qcom_dev_get_cma_area(dev), count, align, no_warn);
}

bool qcom_dma_release_from_contiguous(struct device *dev, struct page *pages,
				 int count)
{
	return cma_release(qcom_dev_get_cma_area(dev), pages, count);
}

static struct page *cma_alloc_aligned(struct cma *cma, size_t size, gfp_t gfp)
{
	unsigned int align = min(get_order(size), CONFIG_CMA_ALIGNMENT);

	return cma_alloc(cma, size >> PAGE_SHIFT, align, gfp & __GFP_NOWARN);
}

struct page *qcom_dma_alloc_contiguous(struct device *dev, size_t size, gfp_t gfp)
{
	/* CMA can be used only in the context which permits sleeping */
	if (!gfpflags_allow_blocking(gfp))
		return NULL;
	if (dev->cma_area)
		return cma_alloc_aligned(dev->cma_area, size, gfp);
	if (size <= PAGE_SIZE || !qcom_dma_contiguous_default_area)
		return NULL;
	return cma_alloc_aligned(qcom_dma_contiguous_default_area, size, gfp);
}

void qcom_dma_free_contiguous(struct device *dev, struct page *page, size_t size)
{
	if (!cma_release(qcom_dev_get_cma_area(dev), page,
			 PAGE_ALIGN(size) >> PAGE_SHIFT))
		__free_pages(page, get_order(size));
}


/*
 * find_vm_area is not exported. Some dma apis expect that an array of
 * struct pages can be saved in the vm_area, and retrieved at a later time.
 */
struct rb_root _root;
struct rb_root *root = &_root;
DEFINE_MUTEX(rbtree_lock);

struct qcom_iommu_dma_area {
	struct rb_node node;
	unsigned long addr;
	struct page **pages;
};

static void qcom_insert_vm_area(struct qcom_iommu_dma_area *area)
{
	struct rb_node **new, *parent;

	mutex_lock(&rbtree_lock);

	parent = NULL;
	new = &root->rb_node;
	while (*new) {
		struct qcom_iommu_dma_area *entry;

		entry = rb_entry(*new,
				struct qcom_iommu_dma_area,
				node);

		parent = *new;
		if (area->addr < entry->addr)
			new = &((*new)->rb_left);
		else if (area->addr > entry->addr)
			new = &((*new)->rb_right);
		else {
			mutex_unlock(&rbtree_lock);
			WARN_ON(1);
			return;
		}
	}

	rb_link_node(&area->node, parent, new);
	rb_insert_color(&area->node, root);
	mutex_unlock(&rbtree_lock);
}

static struct qcom_iommu_dma_area *qcom_find_vm_area(const void *cpu_addr)
{
	struct rb_node *node;
	struct qcom_iommu_dma_area *entry;
	unsigned long addr = (unsigned long)cpu_addr;

	mutex_lock(&rbtree_lock);
	node = root->rb_node;
	while (node) {
		entry = rb_entry(node,
				struct qcom_iommu_dma_area,
				node);

		if (addr < entry->addr)
			node = node->rb_left;
		else if (addr > entry->addr)
			node = node->rb_right;
		else {
			mutex_unlock(&rbtree_lock);
			return entry;
		}
	}

	mutex_unlock(&rbtree_lock);

	return NULL;
}

struct page **qcom_dma_common_find_pages(void *cpu_addr)
{
	struct qcom_iommu_dma_area *area = qcom_find_vm_area(cpu_addr);

	if (!area)
		return NULL;
	return area->pages;
}

/*
 * Remaps an array of PAGE_SIZE pages into another vm_area.
 * Cannot be used in non-sleeping contexts
 */
void *qcom_dma_common_pages_remap(struct page **pages, size_t size,
			 pgprot_t prot, const void *caller)
{
	struct qcom_iommu_dma_area *area;
	void *vaddr;

	area = kzalloc(sizeof(*area), GFP_KERNEL);
	if (!area)
		return NULL;

	vaddr = vmap(pages, PAGE_ALIGN(size) >> PAGE_SHIFT,
		     VM_DMA_COHERENT, prot);
	if (!vaddr) {
		kfree(area);
		return NULL;
	}

	area->pages = pages;
	area->addr = (unsigned long)vaddr;
	qcom_insert_vm_area(area);

	return vaddr;
}

/*
 * Remaps an allocated contiguous region into another vm_area.
 * Cannot be used in non-sleeping contexts
 */
void *qcom_dma_common_contiguous_remap(struct page *page, size_t size,
			pgprot_t prot, const void *caller)
{
	int count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	struct page **pages;
	void *vaddr;
	int i;

	pages = kmalloc_array(count, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return NULL;
	for (i = 0; i < count; i++)
		pages[i] = nth_page(page, i);
	vaddr = vmap(pages, count, VM_DMA_COHERENT, prot);
	kfree(pages);

	return vaddr;
}

/*
 * Unmaps a range previously mapped by dma_common_contiguous_remap or
 * dma_common_pages_remap. Note that dma_common_contiguous_remap does
 * not insert an rb_tree entry since there is no pages array to save.
 */
void qcom_dma_common_free_remap(void *cpu_addr, size_t size)
{
	struct qcom_iommu_dma_area *area;

	/* qcom_dma_common_contiguous_remap doesn't save the pages array */
	area = qcom_find_vm_area(cpu_addr);
	if (area) {
		mutex_lock(&rbtree_lock);
		rb_erase(&area->node, root);
		mutex_unlock(&rbtree_lock);
		kfree(area);
	}

	vunmap(cpu_addr);
}

static struct gen_pool *atomic_pool __ro_after_init;

static size_t atomic_pool_size;
static unsigned long current_pool_size;

/* Dynamic background expansion when the atomic pool is near capacity */
static struct work_struct atomic_pool_work;

static void dma_atomic_pool_debugfs_init(void)
{
	struct dentry *root;

	root = debugfs_create_dir("qcom_dma_pools", NULL);
	if (IS_ERR_OR_NULL(root))
		return;

	debugfs_create_ulong("pool_size", 0400, root, &current_pool_size);
}

static void dma_atomic_pool_size_add(gfp_t gfp, size_t size)
{
	current_pool_size += size;
}

static int atomic_pool_expand(struct gen_pool *pool, size_t pool_size,
			      gfp_t gfp)
{
	unsigned int order;
	struct page *page = NULL;
	void *addr;
	int ret = -ENOMEM;

	/* Cannot allocate larger than MAX_ORDER - 1 */
	order = min(get_order(pool_size), MAX_ORDER - 1);

	do {
		pool_size = 1 << (PAGE_SHIFT + order);

		if (qcom_dev_get_cma_area(NULL))
			page = qcom_dma_alloc_from_contiguous(NULL, 1 << order,
							      order, false);
		else
			page = alloc_pages(gfp, order);
	} while (!page && order-- > 0);
	if (!page)
		goto out;

	qcom_arch_dma_prep_coherent(page, pool_size);

	addr = qcom_dma_common_contiguous_remap(page, pool_size,
						pgprot_dmacoherent(PAGE_KERNEL),
						__builtin_return_address(0));
	if (!addr)
		goto free_page;

	ret = gen_pool_add_virt(pool, (unsigned long)addr, page_to_phys(page),
				pool_size, NUMA_NO_NODE);
	if (ret)
		goto remove_mapping;

	dma_atomic_pool_size_add(gfp, pool_size);
	return 0;

remove_mapping:
	qcom_dma_common_free_remap(addr, pool_size);
free_page:
	if (!qcom_dma_release_from_contiguous(NULL, page, 1 << order))
		__free_pages(page, order);
out:
	return ret;
}

static void atomic_pool_resize(struct gen_pool *pool, gfp_t gfp)
{
	if (pool && gen_pool_avail(pool) < atomic_pool_size)
		atomic_pool_expand(pool, gen_pool_size(pool), gfp);
}

static void atomic_pool_work_fn(struct work_struct *work)
{
	atomic_pool_resize(atomic_pool, GFP_KERNEL);
}

static struct gen_pool *__dma_atomic_pool_init(size_t pool_size, gfp_t gfp)
{
	struct gen_pool *pool;
	int ret;

	pool = gen_pool_create(PAGE_SHIFT, NUMA_NO_NODE);
	if (!pool)
		return NULL;

	gen_pool_set_algo(pool, gen_pool_first_fit_order_align, NULL);

	ret = atomic_pool_expand(pool, pool_size, gfp);
	if (ret) {
		gen_pool_destroy(pool);
		pr_err("DMA: failed to allocate %zu KiB %pGg pool for atomic allocation\n",
		       pool_size >> 10, &gfp);
		return NULL;
	}

	pr_info("DMA preallocated %zu KiB %pGg pool for atomic allocations\n",
		gen_pool_size(pool) >> 10, &gfp);
	return pool;
}

static int dma_atomic_pool_init(struct device *dev)
{
	int ret = 0;
	unsigned long pages;

	/* Default the pool size to 128KB per 1 GB of memory, min 128 KB, max MAX_ORDER - 1. */
	pages = totalram_pages() / (SZ_1G / SZ_128K);
	pages = min_t(unsigned long, pages, MAX_ORDER_NR_PAGES);
	atomic_pool_size = max_t(size_t, pages << PAGE_SHIFT, SZ_128K);
	INIT_WORK(&atomic_pool_work, atomic_pool_work_fn);

	atomic_pool = __dma_atomic_pool_init(atomic_pool_size, GFP_KERNEL);
	if (!atomic_pool)
		return -ENOMEM;

	dma_atomic_pool_debugfs_init();
	return ret;
}

/*
 * Couldn't implement this via dma_alloc_attrs(qcom_iommu_dma_dev, GFP_ATOMIC)
 * due to dma_free_from_pool only passing in cpu_addr & not dma_handle.
 */
void *qcom_dma_alloc_from_pool(struct device *dev, size_t size,
			struct page **ret_page, gfp_t flags)
{
	unsigned long val;
	void *ptr = NULL;

	if (!atomic_pool) {
		WARN(1, "coherent pool not initialised!\n");
		return NULL;
	}

	val = gen_pool_alloc(atomic_pool, size);
	if (val) {
		phys_addr_t phys = gen_pool_virt_to_phys(atomic_pool, val);

		*ret_page = pfn_to_page(__phys_to_pfn(phys));
		ptr = (void *)val;
		memset(ptr, 0, size);
	}
	if (gen_pool_avail(atomic_pool) < atomic_pool_size)
		schedule_work(&atomic_pool_work);

	return ptr;
}

bool qcom_dma_free_from_pool(struct device *dev, void *start, size_t size)
{
	if (!atomic_pool || !gen_pool_has_addr(atomic_pool, (unsigned long)start, size))
		return false;
	gen_pool_free(atomic_pool, (unsigned long)start, size);
	return true;
}

static void qcom_dma_atomic_pool_exit(struct device *dev)
{
	unsigned long nr_pages = atomic_pool_size >> PAGE_SHIFT;
	void *addr;
	struct page *page;

	/*
	 * Find the starting address. Pool is expected to be unused.
	 *
	 * While the pool size can expand, it is okay to use the initial size
	 * here, as this function can only ever be called prior to the pool
	 * ever being used. The pool can only expand when an allocation is satisfied
	 * from it, which would not be possible by the time this function is called.
	 * Therefore the size of the pool will be the initial size.
	 */
	addr = (void *)gen_pool_alloc(atomic_pool, atomic_pool_size);
	if (!addr) {
		WARN_ON(1);
		return;
	}

	gen_pool_free(atomic_pool, (unsigned long)addr, atomic_pool_size);
	gen_pool_destroy(atomic_pool);
	page = vmalloc_to_page(addr);
	qcom_dma_common_free_remap(addr, atomic_pool_size);
	qcom_dma_release_from_contiguous(dev, page, nr_pages);
}

/*
 * struct dma_coherent_mem is private, so we cna't access it. 0 indicates
 * an error condition for dma_mmap_from_dev_coherent.
 */
int qcom_dma_mmap_from_dev_coherent(struct device *dev, struct vm_area_struct *vma,
			   void *vaddr, size_t size, int *ret)
{
	return 0;
}

/*
 * Return the page attributes used for mapping dma_alloc_* memory, either in
 * kernel space if remapping is needed, or to userspace through dma_mmap_*.
 */
pgprot_t qcom_dma_pgprot(struct device *dev, pgprot_t prot, unsigned long attrs)
{
	if (dev_is_dma_coherent(dev))
		return prot;
#ifdef CONFIG_ARCH_HAS_DMA_WRITE_COMBINE
	if (attrs & DMA_ATTR_WRITE_COMBINE)
		return pgprot_writecombine(prot);
#endif
	return pgprot_dmacoherent(prot);
}

/**
 * dma_info_to_prot - Translate DMA API directions and attributes to IOMMU API
 *                    page flags.
 * @dir: Direction of DMA transfer
 * @coherent: Is the DMA master cache-coherent?
 * @attrs: DMA attributes for the mapping
 *
 * Return: corresponding IOMMU API page protection flags
 */
int qcom_dma_info_to_prot(enum dma_data_direction dir, bool coherent,
		     unsigned long attrs)
{
	int prot = coherent ? IOMMU_CACHE : 0;

	if (attrs & DMA_ATTR_PRIVILEGED)
		prot |= IOMMU_PRIV;

	if (attrs & DMA_ATTR_SYS_CACHE_ONLY)
		prot |= IOMMU_SYS_CACHE;

	if (attrs & DMA_ATTR_SYS_CACHE_ONLY_NWA)
		prot |= IOMMU_SYS_CACHE_NWA;

	switch (dir) {
	case DMA_BIDIRECTIONAL:
		return prot | IOMMU_READ | IOMMU_WRITE;
	case DMA_TO_DEVICE:
		return prot | IOMMU_READ;
	case DMA_FROM_DEVICE:
		return prot | IOMMU_WRITE;
	default:
		return 0;
	}
}

/*
 * The DMA API client is passing in a scatterlist which could describe
 * any old buffer layout, but the IOMMU API requires everything to be
 * aligned to IOMMU pages. Hence the need for this complicated bit of
 * impedance-matching, to be able to hand off a suitably-aligned list,
 * but still preserve the original offsets and sizes for the caller.
 */
size_t qcom_iommu_dma_prepare_map_sg(struct device *dev, struct iova_domain *iovad,
				struct scatterlist *sg, int nents)
{
	struct scatterlist *s, *prev = NULL;
	size_t iova_len = 0;
	unsigned long mask = dma_get_seg_boundary(dev);
	int i;

	/*
	 * Work out how much IOVA space we need, and align the segments to
	 * IOVA granules for the IOMMU driver to handle. With some clever
	 * trickery we can modify the list in-place, but reversibly, by
	 * stashing the unaligned parts in the as-yet-unused DMA fields.
	 */
	for_each_sg(sg, s, nents, i) {
		size_t s_iova_off = iova_offset(iovad, s->offset);
		size_t s_length = s->length;
		size_t pad_len = (mask - iova_len + 1) & mask;

		sg_dma_address(s) = s_iova_off;
		sg_dma_len(s) = s_length;
		s->offset -= s_iova_off;
		s_length = iova_align(iovad, s_length + s_iova_off);
		s->length = s_length;

		/*
		 * Due to the alignment of our single IOVA allocation, we can
		 * depend on these assumptions about the segment boundary mask:
		 * - If mask size >= IOVA size, then the IOVA range cannot
		 *   possibly fall across a boundary, so we don't care.
		 * - If mask size < IOVA size, then the IOVA range must start
		 *   exactly on a boundary, therefore we can lay things out
		 *   based purely on segment lengths without needing to know
		 *   the actual addresses beforehand.
		 * - The mask must be a power of 2, so pad_len == 0 if
		 *   iova_len == 0, thus we cannot dereference prev the first
		 *   time through here (i.e. before it has a meaningful value).
		 */
		if (pad_len && pad_len < s_length - 1) {
			prev->length += pad_len;
			iova_len += pad_len;
		}

		iova_len += s_length;
		prev = s;
	}

	return iova_len;
}

/*
 * Prepare a successfully-mapped scatterlist to give back to the caller.
 *
 * At this point the segments are already laid out by iommu_dma_map_sg() to
 * avoid individually crossing any boundaries, so we merely need to check a
 * segment's start address to avoid concatenating across one.
 */
int qcom_iommu_dma_finalise_sg(struct device *dev, struct scatterlist *sg, int nents,
		dma_addr_t dma_addr)
{
	struct scatterlist *s, *cur = sg;
	unsigned long seg_mask = dma_get_seg_boundary(dev);
	unsigned int cur_len = 0, max_len = dma_get_max_seg_size(dev);
	int i, count = 0;

	for_each_sg(sg, s, nents, i) {
		/* Restore this segment's original unaligned fields first */
		unsigned int s_iova_off = sg_dma_address(s);
		unsigned int s_length = sg_dma_len(s);
		unsigned int s_iova_len = s->length;

		s->offset += s_iova_off;
		s->length = s_length;
		sg_dma_address(s) = DMA_MAPPING_ERROR;
		sg_dma_len(s) = 0;

		/*
		 * Now fill in the real DMA data. If...
		 * - there is a valid output segment to append to
		 * - and this segment starts on an IOVA page boundary
		 * - but doesn't fall at a segment boundary
		 * - and wouldn't make the resulting output segment too long
		 */
		if (cur_len && !s_iova_off && (dma_addr & seg_mask) &&
		    (max_len - cur_len >= s_length)) {
			/* ...then concatenate it with the previous one */
			cur_len += s_length;
		} else {
			/* Otherwise start the next output segment */
			if (i > 0)
				cur = sg_next(cur);
			cur_len = s_length;
			count++;

			sg_dma_address(cur) = dma_addr + s_iova_off;
		}

		sg_dma_len(cur) = cur_len;
		dma_addr += s_iova_len;

		if (s_length + s_iova_off < s_iova_len)
			cur_len = 0;
	}
	return count;
}

/*
 * If mapping failed, then just restore the original list,
 * but making sure the DMA fields are invalidated.
 */
void qcom_iommu_dma_invalidate_sg(struct scatterlist *sg, int nents)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i) {
		if (sg_dma_address(s) != DMA_MAPPING_ERROR)
			s->offset += sg_dma_address(s);
		if (sg_dma_len(s))
			s->length = sg_dma_len(s);
		sg_dma_address(s) = DMA_MAPPING_ERROR;
		sg_dma_len(s) = 0;
	}
}

/**
 * __iommu_dma_mmap - Map a buffer into provided user VMA
 * @pages: Array representing buffer from __iommu_dma_alloc()
 * @size: Size of buffer in bytes
 * @vma: VMA describing requested userspace mapping
 *
 * Maps the pages of the buffer in @pages into @vma. The caller is responsible
 * for verifying the correct size and protection of @vma beforehand.
 */
static int __qcom_iommu_dma_mmap(struct page **pages, size_t size,
		struct vm_area_struct *vma)
{
	return vm_map_pages(vma, pages, PAGE_ALIGN(size) >> PAGE_SHIFT);
}

int qcom_iommu_dma_mmap(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs)
{
	unsigned long nr_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
	unsigned long pfn, off = vma->vm_pgoff;
	int ret;

	vma->vm_page_prot = qcom_dma_pgprot(dev, vma->vm_page_prot, attrs);

	if (qcom_dma_mmap_from_dev_coherent(dev, vma, cpu_addr, size, &ret))
		return ret;

	if (off >= nr_pages || vma_pages(vma) > nr_pages - off)
		return -ENXIO;

	if (IS_ENABLED(CONFIG_DMA_REMAP) && is_vmalloc_addr(cpu_addr)) {
		struct page **pages = qcom_dma_common_find_pages(cpu_addr);

		if (pages)
			return __qcom_iommu_dma_mmap(pages, size, vma);
		pfn = vmalloc_to_pfn(cpu_addr);
	} else {
		pfn = page_to_pfn(virt_to_page(cpu_addr));
	}

	return remap_pfn_range(vma, vma->vm_start, pfn + off,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot);
}

int qcom_iommu_dma_get_sgtable(struct device *dev, struct sg_table *sgt,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs)
{
	struct page *page;
	int ret;

	if (IS_ENABLED(CONFIG_DMA_REMAP) && is_vmalloc_addr(cpu_addr)) {
		struct page **pages = qcom_dma_common_find_pages(cpu_addr);

		if (pages) {
			return sg_alloc_table_from_pages(sgt, pages,
					PAGE_ALIGN(size) >> PAGE_SHIFT,
					0, size, GFP_KERNEL);
		}

		page = vmalloc_to_page(cpu_addr);
	} else {
		page = virt_to_page(cpu_addr);
	}

	ret = sg_alloc_table(sgt, 1, GFP_KERNEL);
	if (!ret)
		sg_set_page(sgt->sgl, page, PAGE_ALIGN(size), 0);
	return ret;
}

static int qcom_dma_iommu_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;

	qcom_dma_iommu_dev = dev;
	if (dev_is_dma_coherent(dev)) {
		dev_err(dev, "Cannot be dma-coherent\n");
		return -EINVAL;
	}

	/* Should be connected to linux,cma-default node */
	ret = of_reserved_mem_device_init_by_idx(dev, dev->of_node, 0);
	if (ret)
		return ret;

	qcom_dma_contiguous_default_area = dev->cma_area;
	if (!qcom_dma_contiguous_default_area) {
		dev_err(dev, "Unable to find cma area\n");
		return -EINVAL;
	}

	ret = dma_atomic_pool_init(dev);
	if (ret)
		goto out_iova_cache;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret)
		goto out_atomic_pool;

	probe_finished = true;
	return 0;

out_atomic_pool:
	qcom_dma_atomic_pool_exit(dev);

out_iova_cache:
	return ret;
}

bool qcom_dma_iommu_is_ready(void)
{
	if (!probe_finished)
		return false;
	return true;
}
EXPORT_SYMBOL(qcom_dma_iommu_is_ready);

static int qcom_dma_iommu_remove(struct platform_device *pdev)
{
	qcom_dma_atomic_pool_exit(&pdev->dev);
	return 0;
}

static const struct of_device_id qcom_dma_iommu_of_match[] = {
	{.compatible = "qcom,iommu-dma"},
	{}
};
MODULE_DEVICE_TABLE(of, qcom_dma_iommu_of_match);

static struct platform_driver qcom_dma_iommu_driver = {
	.probe = qcom_dma_iommu_probe,
	.remove = qcom_dma_iommu_remove,
	.driver = {
		.name = "qcom_dma_iommu",
		.of_match_table = qcom_dma_iommu_of_match,
		.suppress_bind_attrs    = true,
	},
};

int __init qcom_dma_iommu_generic_driver_init(void)
{
	return platform_driver_register(&qcom_dma_iommu_driver);
}

void qcom_dma_iommu_generic_driver_exit(void)
{
	platform_driver_unregister(&qcom_dma_iommu_driver);
}
