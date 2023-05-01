// SPDX-License-Identifier: GPL-2.0-only
/*
 * mmap() algorithm taken from drivers/staging/android/ion/ion_heap.c as
 * of commit a3ec289e74b4 ("arm-smmu: Fix missing qsmmuv500 callback")
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2019, 2020 Linaro Ltd.
 *
 * Portions based off of Andrew Davis' SRAM heap:
 * Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
 *
 * These ops were base of the ops in drivers/dma-buf/heaps/system-heap.c from
 * https://lore.kernel.org/lkml/20201017013255.43568-2-john.stultz@linaro.org/
 *
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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
#include <linux/vmalloc.h>
#include <linux/of.h>
#include <linux/dma-map-ops.h>
#include <linux/qcom_dma_heap.h>
#include <linux/msm_dma_iommu_mapping.h>
#include <linux/qti-smmu-proxy-callbacks.h>

#include "qcom_sg_ops.h"

int proxy_invalid_map(struct device *dev, struct sg_table *table,
		      struct dma_buf *dmabuf)
{
	WARN(1, "Trying to map with SMMU proxy driver when it has not fully probed!\n");
	return -EINVAL;
}

void proxy_invalid_unmap(struct device *dev, struct sg_table *table,
			 struct dma_buf *dmabuf)
{
	WARN(1, "Trying to unmap with SMMU proxy driver when it has not fully probed!\n");
}

static struct smmu_proxy_callbacks smmu_proxy_callback_ops = {
	.map_sgtable = proxy_invalid_map,
	.unmap_sgtable = proxy_invalid_unmap,
};

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

int qcom_sg_attach(struct dma_buf *dmabuf,
		   struct dma_buf_attachment *attachment)
{
	struct qcom_sg_buffer *buffer = dmabuf->priv;
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

	attachment->priv = a;

	mutex_lock(&buffer->lock);
	list_add(&a->list, &buffer->attachments);
	mutex_unlock(&buffer->lock);

	return 0;
}

void qcom_sg_detach(struct dma_buf *dmabuf,
		    struct dma_buf_attachment *attachment)
{
	struct qcom_sg_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a = attachment->priv;

	mutex_lock(&buffer->lock);
	list_del(&a->list);
	mutex_unlock(&buffer->lock);

	sg_free_table(a->table);
	kfree(a->table);
	kfree(a);
}

struct sg_table *qcom_sg_map_dma_buf(struct dma_buf_attachment *attachment,
				     enum dma_data_direction direction)
{
	struct dma_heap_attachment *a = attachment->priv;
	struct sg_table *table = a->table;
	struct qcom_sg_buffer *buffer;
	struct mem_buf_vmperm *vmperm;
	unsigned long attrs = attachment->dma_map_attrs;
	int ret;

	buffer = attachment->dmabuf->priv;
	vmperm = buffer->vmperm;

	if (smmu_proxy_callback_ops.map_sgtable &&
	    (attrs & DMA_ATTR_QTI_SMMU_PROXY_MAP)) {
		ret = smmu_proxy_callback_ops.map_sgtable(attachment->dev, table,
							  attachment->dmabuf);
		return ret ? ERR_PTR(ret) : table;
	}

	/* Prevent map/unmap during begin/end_cpu_access */
	mutex_lock(&buffer->lock);

	/* Ensure VM permissions are constant while the buffer is mapped */
	mem_buf_vmperm_pin(vmperm);
	if (buffer->uncached || !mem_buf_vmperm_can_cmo(vmperm))
		attrs |= DMA_ATTR_SKIP_CPU_SYNC;

	if (attrs & DMA_ATTR_DELAYED_UNMAP) {
		ret = msm_dma_map_sgtable(attachment->dev, table, direction,
					  attachment->dmabuf, attrs);
	} else if (!a->mapped) {
		ret = dma_map_sgtable(attachment->dev, table, direction, attrs);
	} else {
		dev_err(attachment->dev, "Error: Dma-buf is already mapped!\n");
		ret = -EBUSY;
	}

	if (ret) {
		table = ERR_PTR(ret);
		goto err_map_sgtable;
	}

	a->mapped = true;
	mutex_unlock(&buffer->lock);
	return table;

err_map_sgtable:
	mem_buf_vmperm_unpin(vmperm);
	mutex_unlock(&buffer->lock);
	return table;
}

void qcom_sg_unmap_dma_buf(struct dma_buf_attachment *attachment,
			   struct sg_table *table,
			   enum dma_data_direction direction)
{
	struct dma_heap_attachment *a = attachment->priv;
	struct qcom_sg_buffer *buffer;
	struct mem_buf_vmperm *vmperm;
	unsigned long attrs = attachment->dma_map_attrs;

	buffer = attachment->dmabuf->priv;
	vmperm = buffer->vmperm;

	if (smmu_proxy_callback_ops.unmap_sgtable &&
	    (attrs & DMA_ATTR_QTI_SMMU_PROXY_MAP)) {
		smmu_proxy_callback_ops.unmap_sgtable(attachment->dev, table,
						      attachment->dmabuf);
		return;
	}

	/* Prevent map/unmap during begin/end_cpu_access */
	mutex_lock(&buffer->lock);

	if (buffer->uncached || !mem_buf_vmperm_can_cmo(vmperm))
		attrs |= DMA_ATTR_SKIP_CPU_SYNC;

	a->mapped = false;

	if (attrs & DMA_ATTR_DELAYED_UNMAP) {
		msm_dma_unmap_sgtable(attachment->dev, table, direction,
				      attachment->dmabuf, attrs);
	} else {
		dma_unmap_sgtable(attachment->dev, table, direction, attrs);
	}
	mem_buf_vmperm_unpin(vmperm);
	mutex_unlock(&buffer->lock);
}

int qcom_sg_dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
				     enum dma_data_direction direction)
{
	struct qcom_sg_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a;

	if (buffer->uncached)
		return 0;

	mutex_lock(&buffer->lock);

	/* Keep the same behavior as ion by returning 0 instead of -EPERM */
	if (!mem_buf_vmperm_can_cmo(buffer->vmperm)) {
		mutex_unlock(&buffer->lock);
		return 0;
	}

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

int qcom_sg_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
				   enum dma_data_direction direction)
{
	struct qcom_sg_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a;

	if (buffer->uncached)
		return 0;

	mutex_lock(&buffer->lock);

	/* Keep the same behavior as ion by returning 0 instead of -EPERM */
	if (!mem_buf_vmperm_can_cmo(buffer->vmperm)) {
		mutex_unlock(&buffer->lock);
		return 0;
	}

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

static int sgl_sync_range(struct device *dev, struct scatterlist *sgl,
			  unsigned int nents, unsigned long offset,
			  unsigned long length,
			  enum dma_data_direction dir, bool for_cpu)
{
	int i;
	struct scatterlist *sg;
	unsigned int len = 0;
	dma_addr_t sg_dma_addr;

	for_each_sg(sgl, sg, nents, i) {
		if (sg_dma_len(sg) == 0)
			break;

		if (i > 0) {
			if (!get_dma_ops(dev))
				return 0;

			pr_warn_ratelimited("Partial cmo only supported with 1 segment\n"
				"is dma_set_max_seg_size being set on dev:%s\n",
				dev_name(dev));
			return -EINVAL;
		}
	}

	for_each_sg(sgl, sg, nents, i) {
		unsigned int sg_offset, sg_left, size = 0;

		if (i == 0)
			sg_dma_addr = sg_dma_address(sg);

		len += sg->length;
		if (len <= offset) {
			sg_dma_addr += sg->length;
			continue;
		}

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
		sg_dma_addr += sg->length;

		if (length == 0)
			break;
	}

	return 0;
}

int qcom_sg_dma_buf_begin_cpu_access_partial(struct dma_buf *dmabuf,
					     enum dma_data_direction dir,
					     unsigned int offset,
					     unsigned int len)
{
	struct qcom_sg_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a;
	int ret = 0;

	if (buffer->uncached)
		return 0;

	mutex_lock(&buffer->lock);

	/* Keep the same behavior as ion by returning 0 instead of -EPERM */
	if (!mem_buf_vmperm_can_cmo(buffer->vmperm)) {
		mutex_unlock(&buffer->lock);
		return 0;
	}

	if (buffer->vmap_cnt)
		invalidate_kernel_vmap_range(buffer->vaddr + offset, len);

	list_for_each_entry(a, &buffer->attachments, list) {
		if (!a->mapped)
			continue;

		ret = sgl_sync_range(a->dev, a->table->sgl, a->table->orig_nents,
				     offset, len, dir, true);
	}
	mutex_unlock(&buffer->lock);

	return ret;
}

int qcom_sg_dma_buf_end_cpu_access_partial(struct dma_buf *dmabuf,
					   enum dma_data_direction direction,
					   unsigned int offset,
					   unsigned int len)
{
	struct qcom_sg_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a;
	int ret = 0;

	if (buffer->uncached)
		return 0;

	mutex_lock(&buffer->lock);

	/* Keep the same behavior as ion by returning 0 instead of -EPERM */
	if (!mem_buf_vmperm_can_cmo(buffer->vmperm)) {
		mutex_unlock(&buffer->lock);
		return 0;
	}

	if (buffer->vmap_cnt)
		flush_kernel_vmap_range(buffer->vaddr + offset, len);

	list_for_each_entry(a, &buffer->attachments, list) {
		if (!a->mapped)
			continue;

		ret = sgl_sync_range(a->dev, a->table->sgl, a->table->orig_nents,
				     offset, len, direction, false);
	}
	mutex_unlock(&buffer->lock);

	return ret;
}

static void qcom_sg_vm_ops_open(struct vm_area_struct *vma)
{
	struct mem_buf_vmperm *vmperm = vma->vm_private_data;

	mem_buf_vmperm_pin(vmperm);
}

static void qcom_sg_vm_ops_close(struct vm_area_struct *vma)
{
	struct mem_buf_vmperm *vmperm = vma->vm_private_data;

	mem_buf_vmperm_unpin(vmperm);
}

static const struct vm_operations_struct qcom_sg_vm_ops = {
	.open = qcom_sg_vm_ops_open,
	.close = qcom_sg_vm_ops_close,
};

int qcom_sg_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct qcom_sg_buffer *buffer = dmabuf->priv;
	struct sg_table *table = &buffer->sg_table;
	struct scatterlist *sg;
	unsigned long addr = vma->vm_start;
	unsigned long offset = vma->vm_pgoff * PAGE_SIZE;
	int ret;
	int i;

	mem_buf_vmperm_pin(buffer->vmperm);
	if (!mem_buf_vmperm_can_mmap(buffer->vmperm, vma)) {
		mem_buf_vmperm_unpin(buffer->vmperm);
		return -EPERM;
	}

	vma->vm_ops = &qcom_sg_vm_ops;
	vma->vm_private_data = buffer->vmperm;
	if (buffer->uncached)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	for_each_sg(table->sgl, sg, table->nents, i) {
		struct page *page = sg_page(sg);
		unsigned long remainder = vma->vm_end - addr;
		unsigned long len = sg->length;

		if (offset >= sg->length) {
			offset -= sg->length;
			continue;
		} else if (offset) {
			page += offset / PAGE_SIZE;
			len = sg->length - offset;
			offset = 0;
		}
		len = min(len, remainder);
		ret = remap_pfn_range(vma, addr, page_to_pfn(page), len,
				      vma->vm_page_prot);
		if (ret) {
			mem_buf_vmperm_unpin(buffer->vmperm);
			return ret;
		}
		addr += len;
		if (addr >= vma->vm_end)
			return 0;
	}
	return 0;
}

void *qcom_sg_do_vmap(struct qcom_sg_buffer *buffer)
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

int qcom_sg_vmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct qcom_sg_buffer *buffer = dmabuf->priv;
	void *vaddr;
	int ret = 0;

	mem_buf_vmperm_pin(buffer->vmperm);
	if (!mem_buf_vmperm_can_vmap(buffer->vmperm)) {
		mem_buf_vmperm_unpin(buffer->vmperm);
		return -EPERM;
	}

	mutex_lock(&buffer->lock);
	if (buffer->vmap_cnt) {
		buffer->vmap_cnt++;
		iosys_map_set_vaddr(map, buffer->vaddr);
		goto out;
	}

	vaddr = qcom_sg_do_vmap(buffer);
	if (IS_ERR(vaddr)) {
		ret = PTR_ERR(vaddr);
		mem_buf_vmperm_unpin(buffer->vmperm);
		goto out;
	}

	buffer->vaddr = vaddr;
	buffer->vmap_cnt++;
	iosys_map_set_vaddr(map, buffer->vaddr);
out:
	mutex_unlock(&buffer->lock);

	return ret;
}

void qcom_sg_vunmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct qcom_sg_buffer *buffer = dmabuf->priv;

	mutex_lock(&buffer->lock);
	if (!--buffer->vmap_cnt) {
		vunmap(buffer->vaddr);
		buffer->vaddr = NULL;
	}
	mem_buf_vmperm_unpin(buffer->vmperm);
	mutex_unlock(&buffer->lock);
	iosys_map_clear(map);
}

void qcom_sg_release(struct dma_buf *dmabuf)
{
	struct qcom_sg_buffer *buffer = dmabuf->priv;

	if (mem_buf_vmperm_release(buffer->vmperm))
		return;

	msm_dma_buf_freed(buffer);
	buffer->free(buffer);
}

struct mem_buf_vmperm *qcom_sg_lookup_vmperm(struct dma_buf *dmabuf)
{
	struct qcom_sg_buffer *buffer = dmabuf->priv;

	return buffer->vmperm;
}

struct mem_buf_dma_buf_ops qcom_sg_buf_ops = {
	.attach = qcom_sg_attach,
	.lookup = qcom_sg_lookup_vmperm,
	.dma_ops = {
		.attach = NULL, /* Will be set by mem_buf_dma_buf_export */
		.detach = qcom_sg_detach,
		.map_dma_buf = qcom_sg_map_dma_buf,
		.unmap_dma_buf = qcom_sg_unmap_dma_buf,
		.begin_cpu_access = qcom_sg_dma_buf_begin_cpu_access,
		.end_cpu_access = qcom_sg_dma_buf_end_cpu_access,
		.begin_cpu_access_partial = qcom_sg_dma_buf_begin_cpu_access_partial,
		.end_cpu_access_partial = qcom_sg_dma_buf_end_cpu_access_partial,
		.mmap = qcom_sg_mmap,
		.vmap = qcom_sg_vmap,
		.vunmap = qcom_sg_vunmap,
		.release = qcom_sg_release,
	}
};
EXPORT_SYMBOL(qcom_sg_buf_ops);

int qti_smmu_proxy_register_callbacks(smmu_proxy_map_sgtable map_sgtable_fn_ptr,
				      smmu_proxy_unmap_sgtable unmap_sgtable_fn_ptr)
{
	smmu_proxy_callback_ops.map_sgtable = map_sgtable_fn_ptr;
	smmu_proxy_callback_ops.unmap_sgtable = unmap_sgtable_fn_ptr;

	return 0;
}
EXPORT_SYMBOL(qti_smmu_proxy_register_callbacks);
