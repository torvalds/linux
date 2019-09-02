// SPDX-License-Identifier: GPL-2.0
/*
 * ION Memory Allocator - dmabuf interface
 *
 * Copyright (c) 2019, Google, Inc.
 */

#include <linux/device.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "ion_private.h"

static struct sg_table *dup_sg_table(struct sg_table *table)
{
	struct sg_table *new_table;
	int ret, i;
	struct scatterlist *sg, *new_sg;

	new_table = kzalloc(sizeof(*new_table), GFP_KERNEL);
	if (!new_table)
		return ERR_PTR(-ENOMEM);

	ret = sg_alloc_table(new_table, table->nents, GFP_KERNEL);
	if (ret) {
		kfree(new_table);
		return ERR_PTR(-ENOMEM);
	}

	new_sg = new_table->sgl;
	for_each_sg(table->sgl, sg, table->nents, i) {
		memcpy(new_sg, sg, sizeof(*sg));
		new_sg->dma_address = 0;
		new_sg = sg_next(new_sg);
	}

	return new_table;
}

static void free_duped_table(struct sg_table *table)
{
	sg_free_table(table);
	kfree(table);
}

struct ion_dma_buf_attachment {
	struct device *dev;
	struct sg_table *table;
	struct list_head list;
};

static int ion_dma_buf_attach(struct dma_buf *dmabuf,
			      struct dma_buf_attachment *attachment)
{
	struct ion_dma_buf_attachment *a;
	struct sg_table *table;
	struct ion_buffer *buffer = dmabuf->priv;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	table = dup_sg_table(buffer->sg_table);
	if (IS_ERR(table)) {
		kfree(a);
		return -ENOMEM;
	}

	a->table = table;
	a->dev = attachment->dev;
	INIT_LIST_HEAD(&a->list);

	attachment->priv = a;

	mutex_lock(&buffer->lock);
	list_add(&a->list, &buffer->attachments);
	mutex_unlock(&buffer->lock);

	return 0;
}

static void ion_dma_buf_detatch(struct dma_buf *dmabuf,
				struct dma_buf_attachment *attachment)
{
	struct ion_dma_buf_attachment *a = attachment->priv;
	struct ion_buffer *buffer = dmabuf->priv;

	mutex_lock(&buffer->lock);
	list_del(&a->list);
	mutex_unlock(&buffer->lock);
	free_duped_table(a->table);

	kfree(a);
}

static struct sg_table *ion_map_dma_buf(struct dma_buf_attachment *attachment,
					enum dma_data_direction direction)
{
	struct ion_buffer *buffer = attachment->dmabuf->priv;
	struct ion_heap *heap = buffer->heap;
	struct ion_dma_buf_attachment *a;
	struct sg_table *table;

	if (heap->buf_ops.map_dma_buf)
		return heap->buf_ops.map_dma_buf(attachment, direction);

	a = attachment->priv;
	table = a->table;

	if (!dma_map_sg(attachment->dev, table->sgl, table->nents, direction))
		return ERR_PTR(-ENOMEM);

	return table;
}

static void ion_unmap_dma_buf(struct dma_buf_attachment *attachment,
			      struct sg_table *table,
			      enum dma_data_direction direction)
{
	struct ion_buffer *buffer = attachment->dmabuf->priv;
	struct ion_heap *heap = buffer->heap;

	if (heap->buf_ops.unmap_dma_buf)
		return heap->buf_ops.unmap_dma_buf(attachment, table,
						   direction);

	dma_unmap_sg(attachment->dev, table->sgl, table->nents, direction);
}

static void ion_dma_buf_release(struct dma_buf *dmabuf)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct ion_heap *heap = buffer->heap;

	if (heap->buf_ops.release)
		return heap->buf_ops.release(dmabuf);

	ion_free(buffer);
}

static int ion_dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
					enum dma_data_direction direction)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct ion_heap *heap = buffer->heap;
	void *vaddr;
	struct ion_dma_buf_attachment *a;
	int ret;

	if (heap->buf_ops.begin_cpu_access)
		return heap->buf_ops.begin_cpu_access(dmabuf, direction);

	/*
	 * TODO: Move this elsewhere because we don't always need a vaddr
	 */
	ret = 0;
	if (heap->ops->map_kernel) {
		mutex_lock(&buffer->lock);
		vaddr = ion_buffer_kmap_get(buffer);
		if (IS_ERR(vaddr)) {
			ret = PTR_ERR(vaddr);
			goto unlock;
		}
		mutex_unlock(&buffer->lock);
	}

	mutex_lock(&buffer->lock);
	list_for_each_entry(a, &buffer->attachments, list) {
		dma_sync_sg_for_cpu(a->dev, a->table->sgl, a->table->nents,
				    direction);
	}

unlock:
	mutex_unlock(&buffer->lock);
	return ret;
}

static int
ion_dma_buf_begin_cpu_access_partial(struct dma_buf *dmabuf,
				     enum dma_data_direction direction,
				     unsigned int offset, unsigned int len)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct ion_heap *heap = buffer->heap;

	/* This is done to make sure partial buffer cache flush / invalidate is
	 * allowed. The implementation may be vendor specific in this case, so
	 * ion core does not provide a default implementation
	 */
	if (!heap->buf_ops.begin_cpu_access_partial)
		return -EOPNOTSUPP;

	return heap->buf_ops.begin_cpu_access_partial(dmabuf, direction, offset,
						      len);
}

static int ion_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
				      enum dma_data_direction direction)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct ion_heap *heap = buffer->heap;
	struct ion_dma_buf_attachment *a;

	if (heap->buf_ops.end_cpu_access)
		return heap->buf_ops.end_cpu_access(dmabuf, direction);

	if (heap->ops->map_kernel) {
		mutex_lock(&buffer->lock);
		ion_buffer_kmap_put(buffer);
		mutex_unlock(&buffer->lock);
	}

	mutex_lock(&buffer->lock);
	list_for_each_entry(a, &buffer->attachments, list) {
		dma_sync_sg_for_device(a->dev, a->table->sgl, a->table->nents,
				       direction);
	}
	mutex_unlock(&buffer->lock);

	return 0;
}

static int ion_dma_buf_end_cpu_access_partial(struct dma_buf *dmabuf,
					      enum dma_data_direction direction,
					      unsigned int offset,
					      unsigned int len)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct ion_heap *heap = buffer->heap;

	/* This is done to make sure partial buffer cache flush / invalidate is
	 * allowed. The implementation may be vendor specific in this case, so
	 * ion core does not provide a default implementation
	 */
	if (!heap->buf_ops.end_cpu_access_partial)
		return -EOPNOTSUPP;

	return heap->buf_ops.end_cpu_access_partial(dmabuf, direction, offset,
						    len);
}

static void *ion_dma_buf_map(struct dma_buf *dmabuf, unsigned long offset)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct ion_heap *heap = buffer->heap;

	if (heap->buf_ops.map)
		return heap->buf_ops.map(dmabuf, offset);

	return buffer->vaddr + offset * PAGE_SIZE;
}

static int ion_dma_buf_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct ion_heap *heap = buffer->heap;
	int ret;

	/* now map it to userspace */
	if (heap->buf_ops.mmap) {
		ret = heap->buf_ops.mmap(dmabuf, vma);
	} else {
		mutex_lock(&buffer->lock);
		if (!(buffer->flags & ION_FLAG_CACHED))
			vma->vm_page_prot =
				pgprot_writecombine(vma->vm_page_prot);

		ret = ion_heap_map_user(heap, buffer, vma);
		mutex_unlock(&buffer->lock);
	}

	if (ret)
		pr_err("%s: failure mapping buffer to userspace\n", __func__);

	return ret;
}

static void ion_dma_buf_unmap(struct dma_buf *dmabuf, unsigned long offset,
			      void *addr)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct ion_heap *heap = buffer->heap;

	if (!heap->buf_ops.unmap)
		return;
	heap->buf_ops.unmap(dmabuf, offset, addr);
}

static void *ion_dma_buf_vmap(struct dma_buf *dmabuf)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct ion_heap *heap = buffer->heap;

	if (!heap->buf_ops.vmap)
		return ERR_PTR(-EOPNOTSUPP);

	return heap->buf_ops.vmap(dmabuf);
}

static void ion_dma_buf_vunmap(struct dma_buf *dmabuf, void *vaddr)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct ion_heap *heap = buffer->heap;

	if (!heap->buf_ops.vunmap)
		return;

	return heap->buf_ops.vunmap(dmabuf, vaddr);
}

static int ion_dma_buf_get_flags(struct dma_buf *dmabuf, unsigned long *flags)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct ion_heap *heap = buffer->heap;

	if (!heap->buf_ops.get_flags)
		return -EOPNOTSUPP;

	return heap->buf_ops.get_flags(dmabuf, flags);
}

static const struct dma_buf_ops dma_buf_ops = {
	.attach = ion_dma_buf_attach,
	.detach = ion_dma_buf_detatch,
	.map_dma_buf = ion_map_dma_buf,
	.unmap_dma_buf = ion_unmap_dma_buf,
	.release = ion_dma_buf_release,
	.begin_cpu_access = ion_dma_buf_begin_cpu_access,
	.begin_cpu_access_partial = ion_dma_buf_begin_cpu_access_partial,
	.end_cpu_access = ion_dma_buf_end_cpu_access,
	.end_cpu_access_partial = ion_dma_buf_end_cpu_access_partial,
	.mmap = ion_dma_buf_mmap,
	.map = ion_dma_buf_map,
	.unmap = ion_dma_buf_unmap,
	.vmap = ion_dma_buf_vmap,
	.vunmap = ion_dma_buf_vunmap,
	.get_flags = ion_dma_buf_get_flags,
};

struct dma_buf *ion_dmabuf_alloc(struct ion_device *dev, size_t len,
				 unsigned int heap_id_mask,
				 unsigned int flags)
{
	struct ion_buffer *buffer;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dmabuf;

	pr_debug("%s: len %zu heap_id_mask %u flags %x\n", __func__,
		 len, heap_id_mask, flags);

	buffer = ion_buffer_alloc(dev, len, heap_id_mask, flags);
	if (IS_ERR(buffer))
		return ERR_CAST(buffer);

	exp_info.ops = &dma_buf_ops;
	exp_info.size = buffer->size;
	exp_info.flags = O_RDWR;
	exp_info.priv = buffer;

	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf))
		ion_buffer_destroy(dev, buffer);

	return dmabuf;
}
