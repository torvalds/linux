// SPDX-License-Identifier: GPL-2.0
/*
 * DMABUF System heap exporter for Rockchip
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2019, 2020 Linaro Ltd.
 * Copyright (c) 2021, 2022 Rockchip Electronics Co. Ltd.
 *
 * Portions based off of Andrew Davis' SRAM heap:
 * Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
 */

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/dma-heap.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/swiotlb.h>
#include <linux/vmalloc.h>
#include <linux/rockchip/rockchip_sip.h>

#include "page_pool.h"
#include "deferred-free-helper.h"

static struct dma_heap *sys_heap;
static struct dma_heap *sys_dma32_heap;
static struct dma_heap *sys_uncached_heap;
static struct dma_heap *sys_uncached_dma32_heap;

/* Default setting */
static u32 bank_bit_first = 12;
static u32 bank_bit_mask = 0x7;

struct system_heap_buffer {
	struct dma_heap *heap;
	struct list_head attachments;
	struct mutex lock;
	unsigned long len;
	struct sg_table sg_table;
	int vmap_cnt;
	void *vaddr;
	struct deferred_freelist_item deferred_free;
	struct dmabuf_page_pool **pools;
	bool uncached;
};

struct dma_heap_attachment {
	struct device *dev;
	struct sg_table *table;
	struct list_head list;
	bool mapped;

	bool uncached;
};

#define LOW_ORDER_GFP (GFP_HIGHUSER | __GFP_ZERO | __GFP_COMP)
#define MID_ORDER_GFP (LOW_ORDER_GFP | __GFP_NOWARN)
#define HIGH_ORDER_GFP  (((GFP_HIGHUSER | __GFP_ZERO | __GFP_NOWARN \
				| __GFP_NORETRY) & ~__GFP_RECLAIM) \
				| __GFP_COMP)
static gfp_t order_flags[] = {HIGH_ORDER_GFP, MID_ORDER_GFP, LOW_ORDER_GFP};
/*
 * The selection of the orders used for allocation (1MB, 64K, 4K) is designed
 * to match with the sizes often found in IOMMUs. Using order 4 pages instead
 * of order 0 pages can significantly improve the performance of many IOMMUs
 * by reducing TLB pressure and time spent updating page tables.
 */
static unsigned int orders[] = {8, 4, 0};
#define NUM_ORDERS ARRAY_SIZE(orders)
struct dmabuf_page_pool *pools[NUM_ORDERS];
struct dmabuf_page_pool *dma32_pools[NUM_ORDERS];

static struct sg_table *dup_sg_table(struct sg_table *table)
{
	struct sg_table *new_table;
	int ret, i;
	struct scatterlist *sg, *new_sg;

	new_table = kzalloc(sizeof(*new_table), GFP_KERNEL);
	if (!new_table)
		return ERR_PTR(-ENOMEM);

	ret = sg_alloc_table(new_table, table->orig_nents, GFP_KERNEL);
	if (ret) {
		kfree(new_table);
		return ERR_PTR(-ENOMEM);
	}

	new_sg = new_table->sgl;
	for_each_sgtable_sg(table, sg, i) {
		sg_set_page(new_sg, sg_page(sg), sg->length, sg->offset);
		new_sg = sg_next(new_sg);
	}

	return new_table;
}

static int system_heap_attach(struct dma_buf *dmabuf,
			      struct dma_buf_attachment *attachment)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a;
	struct sg_table *table;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	table = dup_sg_table(&buffer->sg_table);
	if (IS_ERR(table)) {
		kfree(a);
		return -ENOMEM;
	}

	a->table = table;
	a->dev = attachment->dev;
	INIT_LIST_HEAD(&a->list);
	a->mapped = false;
	a->uncached = buffer->uncached;
	attachment->priv = a;

	mutex_lock(&buffer->lock);
	list_add(&a->list, &buffer->attachments);
	mutex_unlock(&buffer->lock);

	return 0;
}

static void system_heap_detach(struct dma_buf *dmabuf,
			       struct dma_buf_attachment *attachment)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a = attachment->priv;

	mutex_lock(&buffer->lock);
	list_del(&a->list);
	mutex_unlock(&buffer->lock);

	sg_free_table(a->table);
	kfree(a->table);
	kfree(a);
}

static struct sg_table *system_heap_map_dma_buf(struct dma_buf_attachment *attachment,
						enum dma_data_direction direction)
{
	struct dma_heap_attachment *a = attachment->priv;
	struct sg_table *table = a->table;
	int attr = attachment->dma_map_attrs;
	int ret;

	if (a->uncached)
		attr |= DMA_ATTR_SKIP_CPU_SYNC;

	ret = dma_map_sgtable(attachment->dev, table, direction, attr);
	if (ret)
		return ERR_PTR(ret);

	a->mapped = true;
	return table;
}

static void system_heap_unmap_dma_buf(struct dma_buf_attachment *attachment,
				      struct sg_table *table,
				      enum dma_data_direction direction)
{
	struct dma_heap_attachment *a = attachment->priv;
	int attr = attachment->dma_map_attrs;

	if (a->uncached)
		attr |= DMA_ATTR_SKIP_CPU_SYNC;
	a->mapped = false;
	dma_unmap_sgtable(attachment->dev, table, direction, attr);
}

static int system_heap_dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
						enum dma_data_direction direction)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a;

	mutex_lock(&buffer->lock);

	if (buffer->vmap_cnt)
		invalidate_kernel_vmap_range(buffer->vaddr, buffer->len);

	if (!buffer->uncached) {
		list_for_each_entry(a, &buffer->attachments, list) {
			if (!a->mapped)
				continue;
			dma_sync_sgtable_for_cpu(a->dev, a->table, direction);
		}
	}
	mutex_unlock(&buffer->lock);

	return 0;
}

static int system_heap_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
					      enum dma_data_direction direction)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a;

	mutex_lock(&buffer->lock);

	if (buffer->vmap_cnt)
		flush_kernel_vmap_range(buffer->vaddr, buffer->len);

	if (!buffer->uncached) {
		list_for_each_entry(a, &buffer->attachments, list) {
			if (!a->mapped)
				continue;
			dma_sync_sgtable_for_device(a->dev, a->table, direction);
		}
	}
	mutex_unlock(&buffer->lock);

	return 0;
}

static int system_heap_sgl_sync_range(struct device *dev,
				      struct sg_table *sgt,
				      unsigned int offset,
				      unsigned int length,
				      enum dma_data_direction dir,
				      bool for_cpu)
{
	struct scatterlist *sg;
	unsigned int len = 0;
	dma_addr_t sg_dma_addr;
	int i;

	for_each_sgtable_sg(sgt, sg, i) {
		unsigned int sg_offset, sg_left, size = 0;

		sg_dma_addr = sg_phys(sg);

		len += sg->length;
		if (len <= offset)
			continue;

		sg_left = len - offset;
		sg_offset = sg->length - sg_left;

		size = (length < sg_left) ? length : sg_left;
		if (for_cpu)
			dma_sync_single_range_for_cpu(dev, sg_dma_addr,
						      sg_offset, size, dir);
		else
			dma_sync_single_range_for_device(dev, sg_dma_addr,
							 sg_offset, size, dir);

		offset += size;
		length -= size;

		if (length == 0)
			break;
	}

	return 0;
}

static int
system_heap_dma_buf_begin_cpu_access_partial(struct dma_buf *dmabuf,
					     enum dma_data_direction direction,
					     unsigned int offset,
					     unsigned int len)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap *heap = buffer->heap;
	struct sg_table *table = &buffer->sg_table;
	int ret;

	if (direction == DMA_TO_DEVICE)
		return 0;

	mutex_lock(&buffer->lock);
	if (buffer->vmap_cnt)
		invalidate_kernel_vmap_range(buffer->vaddr, buffer->len);

	if (buffer->uncached) {
		mutex_unlock(&buffer->lock);
		return 0;
	}

	ret = system_heap_sgl_sync_range(dma_heap_get_dev(heap), table,
					 offset, len, direction, true);
	mutex_unlock(&buffer->lock);

	return ret;
}

static int
system_heap_dma_buf_end_cpu_access_partial(struct dma_buf *dmabuf,
					   enum dma_data_direction direction,
					   unsigned int offset,
					   unsigned int len)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap *heap = buffer->heap;
	struct sg_table *table = &buffer->sg_table;
	int ret;

	mutex_lock(&buffer->lock);
	if (buffer->vmap_cnt)
		flush_kernel_vmap_range(buffer->vaddr, buffer->len);

	if (buffer->uncached) {
		mutex_unlock(&buffer->lock);
		return 0;
	}

	ret = system_heap_sgl_sync_range(dma_heap_get_dev(heap), table,
					 offset, len, direction, false);
	mutex_unlock(&buffer->lock);

	return ret;
}

static int system_heap_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	struct sg_table *table = &buffer->sg_table;
	unsigned long addr = vma->vm_start;
	struct sg_page_iter piter;
	int ret;

	if (buffer->uncached)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	for_each_sgtable_page(table, &piter, vma->vm_pgoff) {
		struct page *page = sg_page_iter_page(&piter);

		ret = remap_pfn_range(vma, addr, page_to_pfn(page), PAGE_SIZE,
				      vma->vm_page_prot);
		if (ret)
			return ret;
		addr += PAGE_SIZE;
		if (addr >= vma->vm_end)
			return 0;
	}
	return 0;
}

static void *system_heap_do_vmap(struct system_heap_buffer *buffer)
{
	struct sg_table *table = &buffer->sg_table;
	int npages = PAGE_ALIGN(buffer->len) / PAGE_SIZE;
	struct page **pages = vmalloc(sizeof(struct page *) * npages);
	struct page **tmp = pages;
	struct sg_page_iter piter;
	pgprot_t pgprot = PAGE_KERNEL;
	void *vaddr;

	if (!pages)
		return ERR_PTR(-ENOMEM);

	if (buffer->uncached)
		pgprot = pgprot_writecombine(PAGE_KERNEL);

	for_each_sgtable_page(table, &piter, 0) {
		WARN_ON(tmp - pages >= npages);
		*tmp++ = sg_page_iter_page(&piter);
	}

	vaddr = vmap(pages, npages, VM_MAP, pgprot);
	vfree(pages);

	if (!vaddr)
		return ERR_PTR(-ENOMEM);

	return vaddr;
}

static void *system_heap_vmap(struct dma_buf *dmabuf)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	void *vaddr;

	mutex_lock(&buffer->lock);
	if (buffer->vmap_cnt) {
		buffer->vmap_cnt++;
		vaddr = buffer->vaddr;
		goto out;
	}

	vaddr = system_heap_do_vmap(buffer);
	if (IS_ERR(vaddr))
		goto out;

	buffer->vaddr = vaddr;
	buffer->vmap_cnt++;
out:
	mutex_unlock(&buffer->lock);

	return vaddr;
}

static void system_heap_vunmap(struct dma_buf *dmabuf, void *vaddr)
{
	struct system_heap_buffer *buffer = dmabuf->priv;

	mutex_lock(&buffer->lock);
	if (!--buffer->vmap_cnt) {
		vunmap(buffer->vaddr);
		buffer->vaddr = NULL;
	}
	mutex_unlock(&buffer->lock);
}

static int system_heap_zero_buffer(struct system_heap_buffer *buffer)
{
	struct sg_table *sgt = &buffer->sg_table;
	struct sg_page_iter piter;
	struct page *p;
	void *vaddr;
	int ret = 0;

	for_each_sgtable_page(sgt, &piter, 0) {
		p = sg_page_iter_page(&piter);
		vaddr = kmap_atomic(p);
		memset(vaddr, 0, PAGE_SIZE);
		kunmap_atomic(vaddr);
	}

	return ret;
}

static void system_heap_buf_free(struct deferred_freelist_item *item,
				 enum df_reason reason)
{
	struct system_heap_buffer *buffer;
	struct sg_table *table;
	struct scatterlist *sg;
	int i, j;

	buffer = container_of(item, struct system_heap_buffer, deferred_free);
	/* Zero the buffer pages before adding back to the pool */
	if (reason == DF_NORMAL)
		if (system_heap_zero_buffer(buffer))
			reason = DF_UNDER_PRESSURE; // On failure, just free

	table = &buffer->sg_table;
	for_each_sgtable_sg(table, sg, i) {
		struct page *page = sg_page(sg);

		if (reason == DF_UNDER_PRESSURE) {
			__free_pages(page, compound_order(page));
		} else {
			for (j = 0; j < NUM_ORDERS; j++) {
				if (compound_order(page) == orders[j])
					break;
			}
			dmabuf_page_pool_free(buffer->pools[j], page);
		}
	}
	sg_free_table(table);
	kfree(buffer);
}

static void system_heap_dma_buf_release(struct dma_buf *dmabuf)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	int npages = PAGE_ALIGN(buffer->len) / PAGE_SIZE;

	deferred_free(&buffer->deferred_free, system_heap_buf_free, npages);
}

static const struct dma_buf_ops system_heap_buf_ops = {
	.attach = system_heap_attach,
	.detach = system_heap_detach,
	.map_dma_buf = system_heap_map_dma_buf,
	.unmap_dma_buf = system_heap_unmap_dma_buf,
	.begin_cpu_access = system_heap_dma_buf_begin_cpu_access,
	.end_cpu_access = system_heap_dma_buf_end_cpu_access,
	.begin_cpu_access_partial = system_heap_dma_buf_begin_cpu_access_partial,
	.end_cpu_access_partial = system_heap_dma_buf_end_cpu_access_partial,
	.mmap = system_heap_mmap,
	.vmap = system_heap_vmap,
	.vunmap = system_heap_vunmap,
	.release = system_heap_dma_buf_release,
};

static struct page *system_heap_alloc_largest_available(struct dma_heap *heap,
							struct dmabuf_page_pool **pool,
							unsigned long size,
							unsigned int max_order)
{
	struct page *page;
	int i;

	for (i = 0; i < NUM_ORDERS; i++) {
		if (size <  (PAGE_SIZE << orders[i]))
			continue;
		if (max_order < orders[i])
			continue;
		page = dmabuf_page_pool_alloc(pool[i]);
		if (!page)
			continue;
		return page;
	}
	return NULL;
}

static struct dma_buf *system_heap_do_allocate(struct dma_heap *heap,
					       unsigned long len,
					       unsigned long fd_flags,
					       unsigned long heap_flags,
					       bool uncached)
{
	struct system_heap_buffer *buffer;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	unsigned long size_remaining = len;
	unsigned int max_order = orders[0];
	struct dma_buf *dmabuf;
	struct sg_table *table;
	struct scatterlist *sg;
	struct list_head pages;
	struct page *page, *tmp_page;
	int i, ret = -ENOMEM;
	struct list_head lists[8];
	unsigned int block_index[8] = {0};
	unsigned int block_1M = 0;
	unsigned int block_64K = 0;
	unsigned int maximum;
	int j;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&buffer->attachments);
	mutex_init(&buffer->lock);
	buffer->heap = heap;
	buffer->len = len;
	buffer->uncached = uncached;
	buffer->pools = strstr(dma_heap_get_name(heap), "dma32") ? dma32_pools : pools;

	INIT_LIST_HEAD(&pages);
	for (i = 0; i < 8; i++)
		INIT_LIST_HEAD(&lists[i]);
	i = 0;
	while (size_remaining > 0) {
		/*
		 * Avoid trying to allocate memory if the process
		 * has been killed by SIGKILL
		 */
		if (fatal_signal_pending(current))
			goto free_buffer;

		page = system_heap_alloc_largest_available(heap, buffer->pools,
							   size_remaining,
							   max_order);
		if (!page)
			goto free_buffer;

		size_remaining -= page_size(page);
		max_order = compound_order(page);
		if (max_order) {
			if (max_order == 8)
				block_1M++;
			if (max_order == 4)
				block_64K++;
			list_add_tail(&page->lru, &pages);
		} else {
			dma_addr_t phys = page_to_phys(page);
			unsigned int bit_index = ((phys >> bank_bit_first) & bank_bit_mask) & 0x7;

			list_add_tail(&page->lru, &lists[bit_index]);
			block_index[bit_index]++;
		}
		i++;
	}

	table = &buffer->sg_table;
	if (sg_alloc_table(table, i, GFP_KERNEL))
		goto free_buffer;

	maximum = block_index[0];
	for (i = 1; i < 8; i++)
		maximum = max(maximum, block_index[i]);
	sg = table->sgl;
	list_for_each_entry_safe(page, tmp_page, &pages, lru) {
		sg_set_page(sg, page, page_size(page), 0);
		sg = sg_next(sg);
		list_del(&page->lru);
	}
	for (i = 0; i < maximum; i++) {
		for (j = 0; j < 8; j++) {
			if (!list_empty(&lists[j])) {
				page = list_first_entry(&lists[j], struct page, lru);
				sg_set_page(sg, page, PAGE_SIZE, 0);
				sg = sg_next(sg);
				list_del(&page->lru);
			}
		}
	}

	/* create the dmabuf */
	exp_info.exp_name = dma_heap_get_name(heap);
	exp_info.ops = &system_heap_buf_ops;
	exp_info.size = buffer->len;
	exp_info.flags = fd_flags;
	exp_info.priv = buffer;
	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto free_pages;
	}

	/*
	 * For uncached buffers, we need to initially flush cpu cache, since
	 * the __GFP_ZERO on the allocation means the zeroing was done by the
	 * cpu and thus it is likely cached. Map (and implicitly flush) and
	 * unmap it now so we don't get corruption later on.
	 */
	if (buffer->uncached) {
		dma_map_sgtable(dma_heap_get_dev(heap), table, DMA_BIDIRECTIONAL, 0);
		dma_unmap_sgtable(dma_heap_get_dev(heap), table, DMA_BIDIRECTIONAL, 0);
	}

	return dmabuf;

free_pages:
	for_each_sgtable_sg(table, sg, i) {
		struct page *p = sg_page(sg);

		__free_pages(p, compound_order(p));
	}
	sg_free_table(table);
free_buffer:
	list_for_each_entry_safe(page, tmp_page, &pages, lru)
		__free_pages(page, compound_order(page));
	for (i = 0; i < 8; i++) {
		list_for_each_entry_safe(page, tmp_page, &lists[i], lru)
			__free_pages(page, compound_order(page));
	}
	kfree(buffer);

	return ERR_PTR(ret);
}

static struct dma_buf *system_heap_allocate(struct dma_heap *heap,
					    unsigned long len,
					    unsigned long fd_flags,
					    unsigned long heap_flags)
{
	return system_heap_do_allocate(heap, len, fd_flags, heap_flags, false);
}

static long system_get_pool_size(struct dma_heap *heap)
{
	int i;
	long num_pages = 0;
	struct dmabuf_page_pool **pool;

	pool = strstr(dma_heap_get_name(heap), "dma32") ? dma32_pools : pools;
	for (i = 0; i < NUM_ORDERS; i++, pool++) {
		num_pages += ((*pool)->count[POOL_LOWPAGE] +
			      (*pool)->count[POOL_HIGHPAGE]) << (*pool)->order;
	}

	return num_pages << PAGE_SHIFT;
}

static const struct dma_heap_ops system_heap_ops = {
	.allocate = system_heap_allocate,
	.get_pool_size = system_get_pool_size,
};

static struct dma_buf *system_uncached_heap_allocate(struct dma_heap *heap,
						     unsigned long len,
						     unsigned long fd_flags,
						     unsigned long heap_flags)
{
	return system_heap_do_allocate(heap, len, fd_flags, heap_flags, true);
}

/* Dummy function to be used until we can call coerce_mask_and_coherent */
static struct dma_buf *system_uncached_heap_not_initialized(struct dma_heap *heap,
							    unsigned long len,
							    unsigned long fd_flags,
							    unsigned long heap_flags)
{
	return ERR_PTR(-EBUSY);
}

static struct dma_heap_ops system_uncached_heap_ops = {
	/* After system_heap_create is complete, we will swap this */
	.allocate = system_uncached_heap_not_initialized,
};

static int set_heap_dev_dma(struct device *heap_dev)
{
	int err = 0;

	if (!heap_dev)
		return -EINVAL;

	dma_coerce_mask_and_coherent(heap_dev, DMA_BIT_MASK(64));

	if (!heap_dev->dma_parms) {
		heap_dev->dma_parms = devm_kzalloc(heap_dev,
						   sizeof(*heap_dev->dma_parms),
						   GFP_KERNEL);
		if (!heap_dev->dma_parms)
			return -ENOMEM;

		err = dma_set_max_seg_size(heap_dev, (unsigned int)DMA_BIT_MASK(64));
		if (err) {
			devm_kfree(heap_dev, heap_dev->dma_parms);
			dev_err(heap_dev, "Failed to set DMA segment size, err:%d\n", err);
			return err;
		}
	}

	return 0;
}

static int system_heap_create(void)
{
	struct dma_heap_export_info exp_info;
	int i, err = 0;
	struct dram_addrmap_info *ddr_map_info;

	/*
	 * Since swiotlb has memory size limitation, this will calculate
	 * the maximum size locally.
	 *
	 * Once swiotlb_max_segment() return not '0', means that the totalram size
	 * is larger than 4GiB and swiotlb is not force mode, in this case, system
	 * heap should limit largest allocation.
	 *
	 * FIX: fix the orders[] as a workaround.
	 */
	if (swiotlb_max_segment()) {
		unsigned int max_size = (1 << IO_TLB_SHIFT) * IO_TLB_SEGSIZE;
		int max_order = MAX_ORDER;
		int i;

		max_size = max_t(unsigned int, max_size, PAGE_SIZE) >> PAGE_SHIFT;
		max_order = min(max_order, ilog2(max_size));
		for (i = 0; i < NUM_ORDERS; i++) {
			if (max_order < orders[i])
				orders[i] = max_order;
			pr_info("system_heap: orders[%d] = %u\n", i, orders[i]);
		}
	}

	for (i = 0; i < NUM_ORDERS; i++) {
		pools[i] = dmabuf_page_pool_create(order_flags[i], orders[i]);

		if (!pools[i]) {
			int j;

			pr_err("%s: page pool creation failed!\n", __func__);
			for (j = 0; j < i; j++)
				dmabuf_page_pool_destroy(pools[j]);
			return -ENOMEM;
		}
	}

	for (i = 0; i < NUM_ORDERS; i++) {
		dma32_pools[i] = dmabuf_page_pool_create(order_flags[i] | GFP_DMA32, orders[i]);

		if (!dma32_pools[i]) {
			int j;

			pr_err("%s: page dma32 pool creation failed!\n", __func__);
			for (j = 0; j < i; j++)
				dmabuf_page_pool_destroy(dma32_pools[j]);
			goto err_dma32_pool;
		}
	}

	exp_info.name = "system";
	exp_info.ops = &system_heap_ops;
	exp_info.priv = NULL;

	sys_heap = dma_heap_add(&exp_info);
	if (IS_ERR(sys_heap))
		return PTR_ERR(sys_heap);

	exp_info.name = "system-dma32";
	exp_info.ops = &system_heap_ops;
	exp_info.priv = NULL;

	sys_dma32_heap = dma_heap_add(&exp_info);
	if (IS_ERR(sys_dma32_heap))
		return PTR_ERR(sys_dma32_heap);

	exp_info.name = "system-uncached";
	exp_info.ops = &system_uncached_heap_ops;
	exp_info.priv = NULL;

	sys_uncached_heap = dma_heap_add(&exp_info);
	if (IS_ERR(sys_uncached_heap))
		return PTR_ERR(sys_uncached_heap);

	err = set_heap_dev_dma(dma_heap_get_dev(sys_uncached_heap));
	if (err)
		return err;

	exp_info.name = "system-uncached-dma32";
	exp_info.ops = &system_uncached_heap_ops;
	exp_info.priv = NULL;

	sys_uncached_dma32_heap = dma_heap_add(&exp_info);
	if (IS_ERR(sys_uncached_dma32_heap))
		return PTR_ERR(sys_uncached_dma32_heap);

	err = set_heap_dev_dma(dma_heap_get_dev(sys_uncached_dma32_heap));
	if (err)
		return err;
	dma_coerce_mask_and_coherent(dma_heap_get_dev(sys_uncached_dma32_heap), DMA_BIT_MASK(32));

	mb(); /* make sure we only set allocate after dma_mask is set */
	system_uncached_heap_ops.allocate = system_uncached_heap_allocate;

	ddr_map_info = sip_smc_get_dram_map();
	if (ddr_map_info) {
		bank_bit_first = ddr_map_info->bank_bit_first;
		bank_bit_mask = ddr_map_info->bank_bit_mask;
	}

	return 0;
err_dma32_pool:
	for (i = 0; i < NUM_ORDERS; i++)
		dmabuf_page_pool_destroy(pools[i]);

	return -ENOMEM;
}
module_init(system_heap_create);
MODULE_LICENSE("GPL v2");
