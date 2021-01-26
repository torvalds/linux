// SPDX-License-Identifier: GPL-2.0
/*
 * DMA-BUF chunk heap exporter
 *
 * Copyright (c) 2020 Samsung Electronics Co., Ltd.
 * Author: <hyesoo.yu@samsung.com> for Samsung Electronics.
 */

#include <linux/cma.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_reserved_mem.h>
#include <linux/scatterlist.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

struct chunk_heap {
	struct dma_heap *heap;
	uint32_t order;
	struct cma *cma;
};

struct chunk_heap_buffer {
	struct chunk_heap *heap;
	struct list_head attachments;
	struct mutex lock;
	struct sg_table sg_table;
	unsigned long len;
	int vmap_cnt;
	void *vaddr;
};

struct chunk_heap_attachment {
	struct device *dev;
	struct sg_table *table;
	struct list_head list;
	bool mapped;
};

static struct chunk_heap chunk_heaps[MAX_CMA_AREAS] __initdata;
static unsigned int chunk_heap_count __initdata;

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

static int chunk_heap_attach(struct dma_buf *dmabuf, struct dma_buf_attachment *attachment)
{
	struct chunk_heap_buffer *buffer = dmabuf->priv;
	struct chunk_heap_attachment *a;
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

	attachment->priv = a;

	mutex_lock(&buffer->lock);
	list_add(&a->list, &buffer->attachments);
	mutex_unlock(&buffer->lock);

	return 0;
}

static void chunk_heap_detach(struct dma_buf *dmabuf, struct dma_buf_attachment *attachment)
{
	struct chunk_heap_buffer *buffer = dmabuf->priv;
	struct chunk_heap_attachment *a = attachment->priv;

	mutex_lock(&buffer->lock);
	list_del(&a->list);
	mutex_unlock(&buffer->lock);

	sg_free_table(a->table);
	kfree(a->table);
	kfree(a);
}

static struct sg_table *chunk_heap_map_dma_buf(struct dma_buf_attachment *attachment,
					       enum dma_data_direction direction)
{
	struct chunk_heap_attachment *a = attachment->priv;
	struct sg_table *table = a->table;
	int ret;

	if (a->mapped)
		return table;

	ret = dma_map_sgtable(attachment->dev, table, direction, 0);
	if (ret)
		return ERR_PTR(ret);

	a->mapped = true;
	return table;
}

static void chunk_heap_unmap_dma_buf(struct dma_buf_attachment *attachment,
				     struct sg_table *table,
				     enum dma_data_direction direction)
{
	struct chunk_heap_attachment *a = attachment->priv;

	a->mapped = false;
	dma_unmap_sgtable(attachment->dev, table, direction, 0);
}

static int chunk_heap_dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
						enum dma_data_direction direction)
{
	struct chunk_heap_buffer *buffer = dmabuf->priv;
	struct chunk_heap_attachment *a;

	mutex_lock(&buffer->lock);

	if (buffer->vmap_cnt)
		invalidate_kernel_vmap_range(buffer->vaddr, buffer->len);

	list_for_each_entry(a, &buffer->attachments, list) {
		if (!a->mapped)
			continue;
		dma_sync_sgtable_for_cpu(a->dev, a->table, direction);
	}
	mutex_unlock(&buffer->lock);

	return 0;
}

static int chunk_heap_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
					      enum dma_data_direction direction)
{
	struct chunk_heap_buffer *buffer = dmabuf->priv;
	struct chunk_heap_attachment *a;

	mutex_lock(&buffer->lock);

	if (buffer->vmap_cnt)
		flush_kernel_vmap_range(buffer->vaddr, buffer->len);

	list_for_each_entry(a, &buffer->attachments, list) {
		if (!a->mapped)
			continue;
		dma_sync_sgtable_for_device(a->dev, a->table, direction);
	}
	mutex_unlock(&buffer->lock);

	return 0;
}

static int chunk_heap_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct chunk_heap_buffer *buffer = dmabuf->priv;
	struct sg_table *table = &buffer->sg_table;
	unsigned long addr = vma->vm_start;
	struct sg_page_iter piter;
	int ret;

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

static void *chunk_heap_do_vmap(struct chunk_heap_buffer *buffer)
{
	struct sg_table *table = &buffer->sg_table;
	int npages = PAGE_ALIGN(buffer->len) / PAGE_SIZE;
	struct page **pages = vmalloc(sizeof(struct page *) * npages);
	struct page **tmp = pages;
	struct sg_page_iter piter;
	void *vaddr;

	if (!pages)
		return ERR_PTR(-ENOMEM);

	for_each_sgtable_page(table, &piter, 0) {
		WARN_ON(tmp - pages >= npages);
		*tmp++ = sg_page_iter_page(&piter);
	}

	vaddr = vmap(pages, npages, VM_MAP, PAGE_KERNEL);
	vfree(pages);

	if (!vaddr)
		return ERR_PTR(-ENOMEM);

	return vaddr;
}

static void *chunk_heap_vmap(struct dma_buf *dmabuf)
{
	struct chunk_heap_buffer *buffer = dmabuf->priv;
	void *vaddr;

	mutex_lock(&buffer->lock);
	if (buffer->vmap_cnt) {
		vaddr = buffer->vaddr;
	} else {
		vaddr = chunk_heap_do_vmap(buffer);
		if (IS_ERR(vaddr)) {
			mutex_unlock(&buffer->lock);

			return vaddr;
		}
		buffer->vaddr = vaddr;
	}
	buffer->vmap_cnt++;

	mutex_unlock(&buffer->lock);

	return vaddr;
}

static void chunk_heap_vunmap(struct dma_buf *dmabuf, void *vaddr)
{
	struct chunk_heap_buffer *buffer = dmabuf->priv;

	mutex_lock(&buffer->lock);
	if (!--buffer->vmap_cnt) {
		vunmap(buffer->vaddr);
		buffer->vaddr = NULL;
	}
	mutex_unlock(&buffer->lock);
}

static void chunk_heap_dma_buf_release(struct dma_buf *dmabuf)
{
	struct chunk_heap_buffer *buffer = dmabuf->priv;
	struct chunk_heap *chunk_heap = buffer->heap;
	struct sg_table *table;
	struct scatterlist *sg;
	int i;

	table = &buffer->sg_table;
	for_each_sgtable_sg(table, sg, i)
		cma_release(chunk_heap->cma, sg_page(sg), 1 << chunk_heap->order);
	sg_free_table(table);
	kfree(buffer);
}

static const struct dma_buf_ops chunk_heap_buf_ops = {
	.attach = chunk_heap_attach,
	.detach = chunk_heap_detach,
	.map_dma_buf = chunk_heap_map_dma_buf,
	.unmap_dma_buf = chunk_heap_unmap_dma_buf,
	.begin_cpu_access = chunk_heap_dma_buf_begin_cpu_access,
	.end_cpu_access = chunk_heap_dma_buf_end_cpu_access,
	.mmap = chunk_heap_mmap,
	.vmap = chunk_heap_vmap,
	.vunmap = chunk_heap_vunmap,
	.release = chunk_heap_dma_buf_release,
};

struct dma_buf *chunk_heap_allocate(struct dma_heap *heap, unsigned long len,
			       unsigned long fd_flags, unsigned long heap_flags)
{
	struct chunk_heap *chunk_heap = dma_heap_get_drvdata(heap);
	struct chunk_heap_buffer *buffer;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dmabuf;
	struct sg_table *table;
	struct scatterlist *sg;
	struct page **pages;
	unsigned int chunk_size = PAGE_SIZE << chunk_heap->order;
	unsigned int count, alloced = 0;
	unsigned int alloc_order = max_t(unsigned int, pageblock_order, chunk_heap->order);
	unsigned int nr_chunks_per_alloc = 1 << (alloc_order - chunk_heap->order);
	gfp_t gfp_flags = GFP_KERNEL|__GFP_NORETRY;
	int ret = -ENOMEM;
	pgoff_t pg;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(ret);

	INIT_LIST_HEAD(&buffer->attachments);
	mutex_init(&buffer->lock);
	buffer->heap = chunk_heap;
	buffer->len = ALIGN(len, chunk_size);
	count = buffer->len / chunk_size;

	pages = kvmalloc_array(count, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		goto err_pages;

	while (alloced < count) {
		struct page *page;
		int i;

		while (count - alloced < nr_chunks_per_alloc) {
			alloc_order--;
			nr_chunks_per_alloc >>= 1;
		}

		page = cma_alloc(chunk_heap->cma, 1 << alloc_order,
					alloc_order, gfp_flags);
		if (!page) {
			if (gfp_flags & __GFP_NORETRY) {
				gfp_flags &= ~__GFP_NORETRY;
				continue;
			}
			break;
		}

		for (i = 0; i < nr_chunks_per_alloc; i++, alloced++) {
			pages[alloced] = page;
			page += 1 << chunk_heap->order;
		}
	}

	if (alloced < count)
		goto err_alloc;

	table = &buffer->sg_table;
	if (sg_alloc_table(table, count, GFP_KERNEL))
		goto err_alloc;

	sg = table->sgl;
	for (pg = 0; pg < count; pg++) {
		sg_set_page(sg, pages[pg], chunk_size, 0);
		sg = sg_next(sg);
	}

	exp_info.ops = &chunk_heap_buf_ops;
	exp_info.size = buffer->len;
	exp_info.flags = fd_flags;
	exp_info.priv = buffer;
	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto err_export;
	}
	kvfree(pages);

	ret = dma_buf_fd(dmabuf, fd_flags);
	if (ret < 0) {
		dma_buf_put(dmabuf);
		return ERR_PTR(ret);
	}

	return dmabuf;
err_export:
	sg_free_table(table);
err_alloc:
	for (pg = 0; pg < alloced; pg++)
		cma_release(chunk_heap->cma, pages[pg], 1 << chunk_heap->order);
	kvfree(pages);
err_pages:
	kfree(buffer);

	return ERR_PTR(ret);
}

static const struct dma_heap_ops chunk_heap_ops = {
	.allocate = chunk_heap_allocate,
};

#define CHUNK_PREFIX "chunk-"

static int register_chunk_heap(struct chunk_heap *chunk_heap_info)
{
	struct dma_heap_export_info exp_info;
	const char *name = cma_get_name(chunk_heap_info->cma);
	size_t len = strlen(CHUNK_PREFIX) + strlen(name) + 1;
	char *buf = kmalloc(len, GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	sprintf(buf, CHUNK_PREFIX"%s", cma_get_name(chunk_heap_info->cma));
	buf[len] = '\0';

	exp_info.name = buf;
	exp_info.name = cma_get_name(chunk_heap_info->cma);
	exp_info.ops = &chunk_heap_ops;
	exp_info.priv = chunk_heap_info;

	chunk_heap_info->heap = dma_heap_add(&exp_info);
	if (IS_ERR(chunk_heap_info->heap)) {
		kfree(buf);
		return PTR_ERR(chunk_heap_info->heap);
	}

	return 0;
}

static int __init chunk_heap_init(void)
{
	unsigned int i;

	for (i = 0; i < chunk_heap_count; i++)
		register_chunk_heap(&chunk_heaps[i]);

	return 0;
}
module_init(chunk_heap_init);

#ifdef CONFIG_OF_EARLY_FLATTREE

static int __init dmabuf_chunk_heap_area_init(struct reserved_mem *rmem)
{
	int ret;
	struct cma *cma;
	struct chunk_heap *chunk_heap_info;
	const __be32 *chunk_order;

	phys_addr_t align = PAGE_SIZE << max(MAX_ORDER - 1, pageblock_order);
	phys_addr_t mask = align - 1;

	if ((rmem->base & mask) || (rmem->size & mask)) {
		pr_err("Incorrect alignment for CMA region\n");
		return -EINVAL;
	}

	ret = cma_init_reserved_mem(rmem->base, rmem->size, 0, rmem->name, &cma);
	if (ret) {
		pr_err("Reserved memory: unable to setup CMA region\n");
		return ret;
	}

	/* Architecture specific contiguous memory fixup. */
	dma_contiguous_early_fixup(rmem->base, rmem->size);

	chunk_heap_info = &chunk_heaps[chunk_heap_count];
	chunk_heap_info->cma = cma;

	chunk_order = of_get_flat_dt_prop(rmem->fdt_node, "chunk-order", NULL);

	if (chunk_order)
		chunk_heap_info->order = be32_to_cpu(*chunk_order);
	else
		chunk_heap_info->order = 4;

	chunk_heap_count++;

	return 0;
}
RESERVEDMEM_OF_DECLARE(dmabuf_chunk_heap, "dma_heap,chunk",
		       dmabuf_chunk_heap_area_init);
#endif

MODULE_DESCRIPTION("DMA-BUF Chunk Heap");
MODULE_LICENSE("GPL v2");
