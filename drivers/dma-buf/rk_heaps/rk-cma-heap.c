// SPDX-License-Identifier: GPL-2.0
/*
 * DMABUF CMA heap exporter
 *
 * Copyright (C) 2012, 2019, 2020 Linaro Ltd.
 * Author: <benjamin.gaignard@linaro.org> for ST-Ericsson.
 *
 * Also utilizing parts of Andrew Davis' SRAM heap:
 * Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
 *
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 * Author: Simon Xue <xxm@rock-chips.com>
 */

#include <linux/cma.h>
#include <linux/dma-buf.h>
#include <linux/dma-map-ops.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <uapi/linux/rk-dma-heap.h>
#include <linux/proc_fs.h>
#include "../../../mm/cma.h"
#include "rk-dma-heap.h"

struct rk_cma_heap {
	struct rk_dma_heap *heap;
	struct cma *cma;
};

struct rk_cma_heap_buffer {
	struct rk_cma_heap *heap;
	struct list_head attachments;
	struct mutex lock;
	unsigned long len;
	struct page *cma_pages;
	struct page **pages;
	pgoff_t pagecount;
	int vmap_cnt;
	void *vaddr;
	phys_addr_t phys;
	bool attached;
};

struct rk_cma_heap_attachment {
	struct device *dev;
	struct sg_table table;
	struct list_head list;
	bool mapped;
};

static int rk_cma_heap_attach(struct dma_buf *dmabuf,
			      struct dma_buf_attachment *attachment)
{
	struct rk_cma_heap_buffer *buffer = dmabuf->priv;
	struct rk_cma_heap_attachment *a;
	struct sg_table *table;
	size_t size = buffer->pagecount << PAGE_SHIFT;
	int ret;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	table = &a->table;

	ret = sg_alloc_table(table, 1, GFP_KERNEL);
	if (ret) {
		kfree(a);
		return ret;
	}
	sg_set_page(table->sgl, buffer->cma_pages, PAGE_ALIGN(size), 0);

	a->dev = attachment->dev;
	INIT_LIST_HEAD(&a->list);
	a->mapped = false;

	attachment->priv = a;

	buffer->attached = true;

	mutex_lock(&buffer->lock);
	list_add(&a->list, &buffer->attachments);
	mutex_unlock(&buffer->lock);

	return 0;
}

static void rk_cma_heap_detach(struct dma_buf *dmabuf,
			       struct dma_buf_attachment *attachment)
{
	struct rk_cma_heap_buffer *buffer = dmabuf->priv;
	struct rk_cma_heap_attachment *a = attachment->priv;

	mutex_lock(&buffer->lock);
	list_del(&a->list);
	mutex_unlock(&buffer->lock);

	buffer->attached = false;

	sg_free_table(&a->table);
	kfree(a);
}

static struct sg_table *rk_cma_heap_map_dma_buf(struct dma_buf_attachment *attachment,
						enum dma_data_direction direction)
{
	struct rk_cma_heap_attachment *a = attachment->priv;
	struct sg_table *table = &a->table;
	int attrs = attachment->dma_map_attrs;
	int ret;

	ret = dma_map_sgtable(attachment->dev, table, direction, attrs);
	if (ret)
		return ERR_PTR(-ENOMEM);
	a->mapped = true;
	return table;
}

static void rk_cma_heap_unmap_dma_buf(struct dma_buf_attachment *attachment,
				      struct sg_table *table,
				      enum dma_data_direction direction)
{
	struct rk_cma_heap_attachment *a = attachment->priv;
	int attrs = attachment->dma_map_attrs;

	a->mapped = false;
	dma_unmap_sgtable(attachment->dev, table, direction, attrs);
}

static int
rk_cma_heap_dma_buf_begin_cpu_access_partial(struct dma_buf *dmabuf,
					     enum dma_data_direction direction,
					     unsigned int offset,
					     unsigned int len)
{
	struct rk_cma_heap_buffer *buffer = dmabuf->priv;
	struct rk_cma_heap_attachment *a;

	if (buffer->vmap_cnt)
		invalidate_kernel_vmap_range(buffer->vaddr, buffer->len);

	mutex_lock(&buffer->lock);
	list_for_each_entry(a, &buffer->attachments, list) {
		if (!a->mapped)
			continue;
		dma_sync_sgtable_for_cpu(a->dev, &a->table, direction);
	}

	/* For userspace that not attach yet */
	if (buffer->phys && !buffer->attached)
		dma_sync_single_for_cpu(rk_dma_heap_get_dev(buffer->heap->heap),
					buffer->phys + offset,
					len,
					direction);
	mutex_unlock(&buffer->lock);

	return 0;
}

static int
rk_cma_heap_dma_buf_end_cpu_access_partial(struct dma_buf *dmabuf,
					   enum dma_data_direction direction,
					   unsigned int offset,
					   unsigned int len)
{
	struct rk_cma_heap_buffer *buffer = dmabuf->priv;
	struct rk_cma_heap_attachment *a;

	if (buffer->vmap_cnt)
		flush_kernel_vmap_range(buffer->vaddr, buffer->len);

	mutex_lock(&buffer->lock);
	list_for_each_entry(a, &buffer->attachments, list) {
		if (!a->mapped)
			continue;
		dma_sync_sgtable_for_device(a->dev, &a->table, direction);
	}

	/* For userspace that not attach yet */
	if (buffer->phys && !buffer->attached)
		dma_sync_single_for_device(rk_dma_heap_get_dev(buffer->heap->heap),
					   buffer->phys + offset,
					   len,
					   direction);
	mutex_unlock(&buffer->lock);

	return 0;
}

static int rk_cma_heap_dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
						enum dma_data_direction dir)
{
	struct rk_cma_heap_buffer *buffer = dmabuf->priv;
	unsigned int len = buffer->pagecount * PAGE_SIZE;

	return rk_cma_heap_dma_buf_begin_cpu_access_partial(dmabuf, dir, 0, len);
}

static int rk_cma_heap_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
					      enum dma_data_direction dir)
{
	struct rk_cma_heap_buffer *buffer = dmabuf->priv;
	unsigned int len = buffer->pagecount * PAGE_SIZE;

	return rk_cma_heap_dma_buf_end_cpu_access_partial(dmabuf, dir, 0, len);
}

static int rk_cma_heap_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct rk_cma_heap_buffer *buffer = dmabuf->priv;
	size_t size = vma->vm_end - vma->vm_start;
	int ret;

	ret = remap_pfn_range(vma, vma->vm_start, __phys_to_pfn(buffer->phys),
			      size, vma->vm_page_prot);
	if (ret)
		return -EAGAIN;

	return 0;
}

static void *rk_cma_heap_do_vmap(struct rk_cma_heap_buffer *buffer)
{
	void *vaddr;
	pgprot_t pgprot = PAGE_KERNEL;

	vaddr = vmap(buffer->pages, buffer->pagecount, VM_MAP, pgprot);
	if (!vaddr)
		return ERR_PTR(-ENOMEM);

	return vaddr;
}

static void *rk_cma_heap_vmap(struct dma_buf *dmabuf)
{
	struct rk_cma_heap_buffer *buffer = dmabuf->priv;
	void *vaddr;

	mutex_lock(&buffer->lock);
	if (buffer->vmap_cnt) {
		buffer->vmap_cnt++;
		vaddr = buffer->vaddr;
		goto out;
	}

	vaddr = rk_cma_heap_do_vmap(buffer);
	if (IS_ERR(vaddr))
		goto out;

	buffer->vaddr = vaddr;
	buffer->vmap_cnt++;
out:
	mutex_unlock(&buffer->lock);

	return vaddr;
}

static void rk_cma_heap_vunmap(struct dma_buf *dmabuf, void *vaddr)
{
	struct rk_cma_heap_buffer *buffer = dmabuf->priv;

	mutex_lock(&buffer->lock);
	if (!--buffer->vmap_cnt) {
		vunmap(buffer->vaddr);
		buffer->vaddr = NULL;
	}
	mutex_unlock(&buffer->lock);
}

static void rk_cma_heap_remove_dmabuf_list(struct dma_buf *dmabuf)
{
	struct rk_dma_heap_dmabuf *buf;
	struct rk_cma_heap_buffer *buffer = dmabuf->priv;
	struct rk_cma_heap *cma_heap = buffer->heap;
	struct rk_dma_heap *heap = cma_heap->heap;

	mutex_lock(&heap->dmabuf_lock);
	list_for_each_entry(buf, &heap->dmabuf_list, node) {
		if (buf->dmabuf == dmabuf) {
			dma_heap_print("<%s> free dmabuf<ino-%ld>@[%pa-%pa] to heap-<%s>\n",
				       dmabuf->name,
				       dmabuf->file->f_inode->i_ino,
				       &buf->start, &buf->end,
				       rk_dma_heap_get_name(heap));
			list_del(&buf->node);
			kfree(buf);
			break;
		}
	}
	mutex_unlock(&heap->dmabuf_lock);
}

static int rk_cma_heap_add_dmabuf_list(struct dma_buf *dmabuf, const char *name)
{
	struct rk_dma_heap_dmabuf *buf;
	struct rk_cma_heap_buffer *buffer = dmabuf->priv;
	struct rk_cma_heap *cma_heap = buffer->heap;
	struct rk_dma_heap *heap = cma_heap->heap;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	INIT_LIST_HEAD(&buf->node);
	buf->dmabuf = dmabuf;
	buf->start = buffer->phys;
	buf->end = buf->start + buffer->len - 1;
	mutex_lock(&heap->dmabuf_lock);
	list_add_tail(&buf->node, &heap->dmabuf_list);
	mutex_unlock(&heap->dmabuf_lock);

	dma_heap_print("<%s> alloc dmabuf<ino-%ld>@[%pa-%pa] from heap-<%s>\n",
		       dmabuf->name, dmabuf->file->f_inode->i_ino,
		       &buf->start, &buf->end, rk_dma_heap_get_name(heap));

	return 0;
}

static int rk_cma_heap_remove_contig_list(struct rk_dma_heap *heap,
					  struct page *page, const char *name)
{
	struct rk_dma_heap_contig_buf *buf;

	mutex_lock(&heap->contig_lock);
	list_for_each_entry(buf, &heap->contig_list, node) {
		if (buf->start == page_to_phys(page)) {
			dma_heap_print("<%s> free contig-buf@[%pa-%pa] to heap-<%s>\n",
				       buf->orig_alloc, &buf->start, &buf->end,
				       rk_dma_heap_get_name(heap));
			list_del(&buf->node);
			kfree(buf->orig_alloc);
			kfree(buf);
			break;
		}
	}
	mutex_unlock(&heap->contig_lock);

	return 0;
}

static int rk_cma_heap_add_contig_list(struct rk_dma_heap *heap,
				       struct page *page, unsigned long size,
				       const char *name)
{
	struct rk_dma_heap_contig_buf *buf;
	const char *name_tmp;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	INIT_LIST_HEAD(&buf->node);
	if (!name)
		name_tmp = current->comm;
	else
		name_tmp = name;

	buf->orig_alloc = kstrndup(name_tmp, RK_DMA_HEAP_NAME_LEN, GFP_KERNEL);
	if (!buf->orig_alloc) {
		kfree(buf);
		return -ENOMEM;
	}

	buf->start = page_to_phys(page);
	buf->end = buf->start + size - 1;
	mutex_lock(&heap->contig_lock);
	list_add_tail(&buf->node, &heap->contig_list);
	mutex_unlock(&heap->contig_lock);

	dma_heap_print("<%s> alloc contig-buf@[%pa-%pa] from heap-<%s>\n",
		       buf->orig_alloc, &buf->start, &buf->end,
		       rk_dma_heap_get_name(heap));

	return 0;
}

static void rk_cma_heap_dma_buf_release(struct dma_buf *dmabuf)
{
	struct rk_cma_heap_buffer *buffer = dmabuf->priv;
	struct rk_cma_heap *cma_heap = buffer->heap;
	struct rk_dma_heap *heap = cma_heap->heap;

	if (buffer->vmap_cnt > 0) {
		WARN(1, "%s: buffer still mapped in the kernel\n", __func__);
		vunmap(buffer->vaddr);
	}

	rk_cma_heap_remove_dmabuf_list(dmabuf);

	/* free page list */
	kfree(buffer->pages);
	/* release memory */
	cma_release(cma_heap->cma, buffer->cma_pages, buffer->pagecount);
	rk_dma_heap_total_dec(heap, buffer->len);

	kfree(buffer);
}

static const struct dma_buf_ops rk_cma_heap_buf_ops = {
	.cache_sgt_mapping = true,
	.attach = rk_cma_heap_attach,
	.detach = rk_cma_heap_detach,
	.map_dma_buf = rk_cma_heap_map_dma_buf,
	.unmap_dma_buf = rk_cma_heap_unmap_dma_buf,
	.begin_cpu_access = rk_cma_heap_dma_buf_begin_cpu_access,
	.end_cpu_access = rk_cma_heap_dma_buf_end_cpu_access,
	.begin_cpu_access_partial = rk_cma_heap_dma_buf_begin_cpu_access_partial,
	.end_cpu_access_partial = rk_cma_heap_dma_buf_end_cpu_access_partial,
	.mmap = rk_cma_heap_mmap,
	.vmap = rk_cma_heap_vmap,
	.vunmap = rk_cma_heap_vunmap,
	.release = rk_cma_heap_dma_buf_release,
};

static struct dma_buf *rk_cma_heap_allocate(struct rk_dma_heap *heap,
					    unsigned long len,
					    unsigned long fd_flags,
					    unsigned long heap_flags,
					    const char *name)
{
	struct rk_cma_heap *cma_heap = rk_dma_heap_get_drvdata(heap);
	struct rk_cma_heap_buffer *buffer;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	size_t size = PAGE_ALIGN(len);
	pgoff_t pagecount = size >> PAGE_SHIFT;
	unsigned long align = get_order(size);
	struct page *cma_pages;
	struct dma_buf *dmabuf;
	pgoff_t pg;
	int ret = -ENOMEM;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&buffer->attachments);
	mutex_init(&buffer->lock);
	buffer->len = size;

	if (align > CONFIG_DMABUF_HEAPS_ROCKCHIP_CMA_ALIGNMENT)
		align = CONFIG_DMABUF_HEAPS_ROCKCHIP_CMA_ALIGNMENT;

	cma_pages = cma_alloc(cma_heap->cma, pagecount, align, GFP_KERNEL);
	if (!cma_pages)
		goto free_buffer;

	/* Clear the cma pages */
	if (PageHighMem(cma_pages)) {
		unsigned long nr_clear_pages = pagecount;
		struct page *page = cma_pages;

		while (nr_clear_pages > 0) {
			void *vaddr = kmap_atomic(page);

			memset(vaddr, 0, PAGE_SIZE);
			kunmap_atomic(vaddr);
			/*
			 * Avoid wasting time zeroing memory if the process
			 * has been killed by SIGKILL
			 */
			if (fatal_signal_pending(current))
				goto free_cma;
			page++;
			nr_clear_pages--;
		}
	} else {
		memset(page_address(cma_pages), 0, size);
	}

	buffer->pages = kmalloc_array(pagecount, sizeof(*buffer->pages),
				      GFP_KERNEL);
	if (!buffer->pages) {
		ret = -ENOMEM;
		goto free_cma;
	}

	for (pg = 0; pg < pagecount; pg++)
		buffer->pages[pg] = &cma_pages[pg];

	buffer->cma_pages = cma_pages;
	buffer->heap = cma_heap;
	buffer->pagecount = pagecount;

	/* create the dmabuf */
	exp_info.exp_name = rk_dma_heap_get_name(heap);
	exp_info.ops = &rk_cma_heap_buf_ops;
	exp_info.size = buffer->len;
	exp_info.flags = fd_flags;
	exp_info.priv = buffer;
	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto free_pages;
	}

	buffer->phys = page_to_phys(cma_pages);
	dma_sync_single_for_cpu(rk_dma_heap_get_dev(heap), buffer->phys,
				buffer->pagecount * PAGE_SIZE,
				DMA_FROM_DEVICE);

	ret = rk_cma_heap_add_dmabuf_list(dmabuf, name);
	if (ret)
		goto fail_dma_buf;

	rk_dma_heap_total_inc(heap, buffer->len);

	return dmabuf;

fail_dma_buf:
	dma_buf_put(dmabuf);
free_pages:
	kfree(buffer->pages);
free_cma:
	cma_release(cma_heap->cma, cma_pages, pagecount);
free_buffer:
	kfree(buffer);

	return ERR_PTR(ret);
}

static struct page *rk_cma_heap_allocate_pages(struct rk_dma_heap *heap,
					       size_t len, const char *name)
{
	struct rk_cma_heap *cma_heap = rk_dma_heap_get_drvdata(heap);
	size_t size = PAGE_ALIGN(len);
	pgoff_t pagecount = size >> PAGE_SHIFT;
	unsigned long align = get_order(size);
	struct page *page;
	int ret;

	if (align > CONFIG_DMABUF_HEAPS_ROCKCHIP_CMA_ALIGNMENT)
		align = CONFIG_DMABUF_HEAPS_ROCKCHIP_CMA_ALIGNMENT;

	page = cma_alloc(cma_heap->cma, pagecount, align, GFP_KERNEL);
	if (!page)
		return ERR_PTR(-ENOMEM);

	ret = rk_cma_heap_add_contig_list(heap, page, size, name);
	if (ret) {
		cma_release(cma_heap->cma, page, pagecount);
		return ERR_PTR(-EINVAL);
	}

	rk_dma_heap_total_inc(heap, size);

	return page;
}

static void rk_cma_heap_free_pages(struct rk_dma_heap *heap,
				   struct page *page, size_t len,
				   const char *name)
{
	struct rk_cma_heap *cma_heap = rk_dma_heap_get_drvdata(heap);
	pgoff_t pagecount = len >> PAGE_SHIFT;

	rk_cma_heap_remove_contig_list(heap, page, name);

	cma_release(cma_heap->cma, page, pagecount);

	rk_dma_heap_total_dec(heap, len);
}

static const struct rk_dma_heap_ops rk_cma_heap_ops = {
	.allocate = rk_cma_heap_allocate,
	.alloc_contig_pages = rk_cma_heap_allocate_pages,
	.free_contig_pages = rk_cma_heap_free_pages,
};

static int cma_procfs_show(struct seq_file *s, void *private);

static int __rk_add_cma_heap(struct cma *cma, void *data)
{
	struct rk_cma_heap *cma_heap;
	struct rk_dma_heap_export_info exp_info;

	cma_heap = kzalloc(sizeof(*cma_heap), GFP_KERNEL);
	if (!cma_heap)
		return -ENOMEM;
	cma_heap->cma = cma;

	exp_info.name = cma_get_name(cma);
	exp_info.ops = &rk_cma_heap_ops;
	exp_info.priv = cma_heap;
	exp_info.support_cma = true;

	cma_heap->heap = rk_dma_heap_add(&exp_info);
	if (IS_ERR(cma_heap->heap)) {
		int ret = PTR_ERR(cma_heap->heap);

		kfree(cma_heap);
		return ret;
	}

	if (cma_heap->heap->procfs)
		proc_create_single_data("alloc_bitmap", 0, cma_heap->heap->procfs,
					cma_procfs_show, cma);

	return 0;
}

static int rk_add_default_cma_heap(void)
{
	struct cma *cma = rk_dma_heap_get_cma();

	if (WARN_ON(!cma))
		return -EINVAL;

	return __rk_add_cma_heap(cma, NULL);
}
module_init(rk_add_default_cma_heap);

static void cma_procfs_format_array(char *buf, size_t bufsize, u32 *array, int array_size)
{
	int i = 0;

	while (--array_size >= 0) {
		size_t len;
		char term = (array_size && (++i % 8)) ? ' ' : '\n';

		len = snprintf(buf, bufsize, "%08X%c", *array++, term);
		buf += len;
		bufsize -= len;
	}
}

static void cma_procfs_show_bitmap(struct seq_file *s, struct cma *cma)
{
	int elements = DIV_ROUND_UP(cma_bitmap_maxno(cma), BITS_PER_BYTE * sizeof(u32));
	int size = elements * 9;
	u32 *array = (u32 *)cma->bitmap;
	char *buf;

	buf = kmalloc(size + 1, GFP_KERNEL);
	if (!buf)
		return;

	buf[size] = 0;

	cma_procfs_format_array(buf, size + 1, array, elements);
	seq_printf(s, "%s", buf);
	kfree(buf);
}

static u64 cma_procfs_used_get(struct cma *cma)
{
	unsigned long used;

	mutex_lock(&cma->lock);
	used = bitmap_weight(cma->bitmap, (int)cma_bitmap_maxno(cma));
	mutex_unlock(&cma->lock);

	return (u64)used << cma->order_per_bit;
}

static int cma_procfs_show(struct seq_file *s, void *private)
{
	struct cma *cma = s->private;
	u64 used = cma_procfs_used_get(cma);

	seq_printf(s, "Total: %lu KiB\n", cma->count << (PAGE_SHIFT - 10));
	seq_printf(s, " Used: %llu KiB\n\n", used << (PAGE_SHIFT - 10));

	cma_procfs_show_bitmap(s, cma);

	return 0;
}

MODULE_DESCRIPTION("RockChip DMA-BUF CMA Heap");
MODULE_LICENSE("GPL v2");
