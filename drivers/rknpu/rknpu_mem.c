// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#include <linux/version.h>
#include <linux/rk-dma-heap.h>

#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
#include <linux/dma-map-ops.h>
#endif

#include "rknpu_drv.h"
#include "rknpu_ioctl.h"
#include "rknpu_mem.h"

#ifdef CONFIG_ROCKCHIP_RKNPU_DMA_HEAP

int rknpu_mem_create_ioctl(struct rknpu_device *rknpu_dev, unsigned long data,
			   struct file *file)
{
	struct rknpu_mem_create args;
	int ret = -EINVAL;
	struct dma_buf_attachment *attachment;
	struct sg_table *table;
	struct scatterlist *sgl;
	dma_addr_t phys;
	struct dma_buf *dmabuf;
	struct page **pages;
	struct page *page;
	struct rknpu_mem_object *rknpu_obj = NULL;
	struct rknpu_session *session = NULL;
	int i, fd;
	unsigned int length, page_count;

	if (unlikely(copy_from_user(&args, (struct rknpu_mem_create *)data,
				    sizeof(struct rknpu_mem_create)))) {
		LOG_ERROR("%s: copy_from_user failed\n", __func__);
		ret = -EFAULT;
		return ret;
	}

	if (args.flags & RKNPU_MEM_NON_CONTIGUOUS) {
		LOG_ERROR("%s: malloc iommu memory unsupported in current!\n",
			  __func__);
		ret = -EINVAL;
		return ret;
	}

	rknpu_obj = kzalloc(sizeof(*rknpu_obj), GFP_KERNEL);
	if (!rknpu_obj)
		return -ENOMEM;

	if (args.handle > 0) {
		fd = args.handle;

		dmabuf = dma_buf_get(fd);
		if (IS_ERR(dmabuf)) {
			ret = PTR_ERR(dmabuf);
			goto err_free_obj;
		}

		rknpu_obj->dmabuf = dmabuf;
		rknpu_obj->owner = 0;
	} else {
		/* Start test kernel alloc/free dma buf */
		dmabuf = rk_dma_heap_buffer_alloc(rknpu_dev->heap, args.size,
						  O_CLOEXEC | O_RDWR, 0x0,
						  dev_name(rknpu_dev->dev));
		if (IS_ERR(dmabuf)) {
			LOG_ERROR("dmabuf alloc failed, args.size = %llu\n",
				  args.size);
			ret = PTR_ERR(dmabuf);
			goto err_free_obj;
		}

		rknpu_obj->dmabuf = dmabuf;
		rknpu_obj->owner = 1;

		fd = dma_buf_fd(dmabuf, O_CLOEXEC | O_RDWR);
		if (fd < 0) {
			LOG_ERROR("dmabuf fd get failed\n");
			ret = -EFAULT;
			goto err_free_dma_buf;
		}
	}

	attachment = dma_buf_attach(dmabuf, rknpu_dev->dev);
	if (IS_ERR(attachment)) {
		LOG_ERROR("dma_buf_attach failed\n");
		ret = PTR_ERR(attachment);
		goto err_free_dma_buf;
	}

	table = dma_buf_map_attachment(attachment, DMA_BIDIRECTIONAL);
	if (IS_ERR(table)) {
		LOG_ERROR("dma_buf_attach failed\n");
		dma_buf_detach(dmabuf, attachment);
		ret = PTR_ERR(table);
		goto err_free_dma_buf;
	}

	for_each_sgtable_sg(table, sgl, i) {
		phys = sg_dma_address(sgl);
		page = sg_page(sgl);
		length = sg_dma_len(sgl);
		LOG_DEBUG("%s, %d, phys: %pad, length: %u\n", __func__,
			  __LINE__, &phys, length);
	}

	page_count = length >> PAGE_SHIFT;
	pages = vmalloc(page_count * sizeof(struct page));
	if (!pages) {
		LOG_ERROR("alloc pages failed\n");
		ret = -ENOMEM;
		goto err_detach_dma_buf;
	}

	for (i = 0; i < page_count; i++)
		pages[i] = &page[i];

	rknpu_obj->kv_addr = vmap(pages, page_count, VM_MAP, PAGE_KERNEL);
	if (!rknpu_obj->kv_addr) {
		LOG_ERROR("vmap pages addr failed\n");
		ret = -ENOMEM;
		goto err_free_pages;
	}

	rknpu_obj->size = PAGE_ALIGN(args.size);
	rknpu_obj->dma_addr = phys;
	rknpu_obj->sgt = table;

	args.size = rknpu_obj->size;
	args.obj_addr = (__u64)(uintptr_t)rknpu_obj;
	args.dma_addr = rknpu_obj->dma_addr;
	args.handle = fd;

	LOG_DEBUG(
		"args.handle: %d, args.size: %lld, rknpu_obj: %#llx, rknpu_obj->dma_addr: %#llx\n",
		args.handle, args.size, (__u64)(uintptr_t)rknpu_obj,
		(__u64)rknpu_obj->dma_addr);

	if (unlikely(copy_to_user((struct rknpu_mem_create *)data, &args,
				  sizeof(struct rknpu_mem_create)))) {
		LOG_ERROR("%s: copy_to_user failed\n", __func__);
		ret = -EFAULT;
		goto err_unmap_kv_addr;
	}

	vfree(pages);
	pages = NULL;
	dma_buf_unmap_attachment(attachment, table, DMA_BIDIRECTIONAL);
	dma_buf_detach(dmabuf, attachment);

	spin_lock(&rknpu_dev->lock);

	session = file->private_data;
	if (!session) {
		spin_unlock(&rknpu_dev->lock);
		ret = -EFAULT;
		goto err_unmap_kv_addr;
	}
	list_add_tail(&rknpu_obj->head, &session->list);

	spin_unlock(&rknpu_dev->lock);

	return 0;

err_unmap_kv_addr:
	vunmap(rknpu_obj->kv_addr);
	rknpu_obj->kv_addr = NULL;

err_free_pages:
	vfree(pages);
	pages = NULL;

err_detach_dma_buf:
	dma_buf_unmap_attachment(attachment, table, DMA_BIDIRECTIONAL);
	dma_buf_detach(dmabuf, attachment);

err_free_dma_buf:
	if (rknpu_obj->owner)
		rk_dma_heap_buffer_free(dmabuf);
	else
		dma_buf_put(dmabuf);

err_free_obj:
	kfree(rknpu_obj);

	return ret;
}

int rknpu_mem_destroy_ioctl(struct rknpu_device *rknpu_dev, unsigned long data,
			    struct file *file)
{
	struct rknpu_mem_object *rknpu_obj, *entry, *q;
	struct rknpu_session *session = NULL;
	struct rknpu_mem_destroy args;
	int ret = -EFAULT;

	if (unlikely(copy_from_user(&args, (struct rknpu_mem_destroy *)data,
				    sizeof(struct rknpu_mem_destroy)))) {
		LOG_ERROR("%s: copy_from_user failed\n", __func__);
		ret = -EFAULT;
		return ret;
	}

	if (!kern_addr_valid(args.obj_addr)) {
		LOG_ERROR("%s: invalid obj_addr: %#llx\n", __func__,
			  (__u64)(uintptr_t)args.obj_addr);
		ret = -EINVAL;
		return ret;
	}

	rknpu_obj = (struct rknpu_mem_object *)(uintptr_t)args.obj_addr;
	LOG_DEBUG(
		"free args.handle: %d, rknpu_obj: %#llx, rknpu_obj->dma_addr: %#llx\n",
		args.handle, (__u64)(uintptr_t)rknpu_obj,
		(__u64)rknpu_obj->dma_addr);

	spin_lock(&rknpu_dev->lock);
	session = file->private_data;
	if (!session) {
		spin_unlock(&rknpu_dev->lock);
		ret = -EFAULT;
		return ret;
	}
	list_for_each_entry_safe(entry, q, &session->list, head) {
		if (entry == rknpu_obj) {
			list_del(&entry->head);
			break;
		}
	}
	spin_unlock(&rknpu_dev->lock);

	if (rknpu_obj == entry) {
		vunmap(rknpu_obj->kv_addr);
		rknpu_obj->kv_addr = NULL;

		if (!rknpu_obj->owner)
			dma_buf_put(rknpu_obj->dmabuf);

		kfree(rknpu_obj);
	}

	return 0;
}

/*
 * begin cpu access => for_cpu = true
 * end cpu access => for_cpu = false
 */
static void __maybe_unused rknpu_dma_buf_sync(
	struct rknpu_device *rknpu_dev, struct rknpu_mem_object *rknpu_obj,
	u32 offset, u32 length, enum dma_data_direction dir, bool for_cpu)
{
	struct device *dev = rknpu_dev->dev;
	struct sg_table *sgt = rknpu_obj->sgt;
	struct scatterlist *sg = sgt->sgl;
	dma_addr_t sg_dma_addr = sg_dma_address(sg);
	unsigned int len = 0;
	int i;

	for_each_sgtable_sg(sgt, sg, i) {
		unsigned int sg_offset, sg_left, size = 0;

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
}

int rknpu_mem_sync_ioctl(struct rknpu_device *rknpu_dev, unsigned long data)
{
	struct rknpu_mem_object *rknpu_obj = NULL;
	struct rknpu_mem_sync args;
#ifdef CONFIG_DMABUF_PARTIAL
	struct dma_buf *dmabuf;
#endif
	int ret = -EFAULT;

	if (unlikely(copy_from_user(&args, (struct rknpu_mem_sync *)data,
				    sizeof(struct rknpu_mem_sync)))) {
		LOG_ERROR("%s: copy_from_user failed\n", __func__);
		ret = -EFAULT;
		return ret;
	}

	if (!kern_addr_valid(args.obj_addr)) {
		LOG_ERROR("%s: invalid obj_addr: %#llx\n", __func__,
			  (__u64)(uintptr_t)args.obj_addr);
		ret = -EINVAL;
		return ret;
	}

	rknpu_obj = (struct rknpu_mem_object *)(uintptr_t)args.obj_addr;

#ifndef CONFIG_DMABUF_PARTIAL
	if (args.flags & RKNPU_MEM_SYNC_TO_DEVICE) {
		rknpu_dma_buf_sync(rknpu_dev, rknpu_obj, args.offset, args.size,
				   DMA_TO_DEVICE, false);
	}
	if (args.flags & RKNPU_MEM_SYNC_FROM_DEVICE) {
		rknpu_dma_buf_sync(rknpu_dev, rknpu_obj, args.offset, args.size,
				   DMA_FROM_DEVICE, true);
	}
#else
	dmabuf = rknpu_obj->dmabuf;
	if (args.flags & RKNPU_MEM_SYNC_TO_DEVICE) {
		dmabuf->ops->end_cpu_access_partial(dmabuf, DMA_TO_DEVICE,
						    args.offset, args.size);
	}
	if (args.flags & RKNPU_MEM_SYNC_FROM_DEVICE) {
		dmabuf->ops->begin_cpu_access_partial(dmabuf, DMA_FROM_DEVICE,
						      args.offset, args.size);
	}
#endif

	return 0;
}

#endif
