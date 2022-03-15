// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
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

int rknpu_mem_create_ioctl(struct rknpu_device *rknpu_dev, unsigned long data)
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
		ret = -EFAULT;
		return ret;
	}

	rknpu_obj = kzalloc(sizeof(*rknpu_obj), GFP_KERNEL);
	if (!rknpu_obj)
		return PTR_ERR(rknpu_obj);

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
		if (IS_ERR(dmabuf))
			return PTR_ERR(dmabuf);

		rknpu_obj->dmabuf = dmabuf;
		rknpu_obj->owner = 1;

		fd = dma_buf_fd(dmabuf, O_CLOEXEC | O_RDWR);
		if (fd < 0) {
			ret = -EFAULT;
			goto err_free_dma_buf;
		}
	}

	attachment = dma_buf_attach(dmabuf, rknpu_dev->dev);
	if (IS_ERR(attachment)) {
		ret = PTR_ERR(attachment);
		goto err_free_dma_buf;
	}

	table = dma_buf_map_attachment(attachment, DMA_BIDIRECTIONAL);
	if (IS_ERR(table)) {
		ret = PTR_ERR(table);
		goto err_free_dma_buf;
	}

	for_each_sgtable_sg(table, sgl, i) {
		phys = sg_dma_address(sgl);
		page = sg_page(sgl);
		LOG_DEBUG("%s, %d, phys = %pad, length = 0x%x\n", __func__,
			  __LINE__, &phys, sg_dma_len(sgl));
		length = sg_dma_len(sgl);
	}

	page_count = length >> PAGE_SHIFT;
	pages = kmalloc_array(page_count, sizeof(struct page), GFP_KERNEL);
	for (i = 0; i < page_count; i++)
		pages[i] = &page[i];

	rknpu_obj->kv_addr = vmap(pages, page_count, VM_MAP, PAGE_KERNEL);
	kfree(pages);

	dma_buf_unmap_attachment(attachment, table, DMA_BIDIRECTIONAL);
	dma_buf_detach(dmabuf, attachment);

	rknpu_obj->size = PAGE_ALIGN(args.size);
	rknpu_obj->dma_addr = phys;
	rknpu_obj->sgt = table;

	args.size = rknpu_obj->size;
	args.obj_addr = (__u64)(uintptr_t)rknpu_obj;
	args.dma_addr = rknpu_obj->dma_addr;
	args.handle = fd;

	LOG_DEBUG(
		"args.handle = %d, args.size = %lld, rknpu_obj = %#llx, rknpu_obj->dma_addr = %#llx\n",
		args.handle, args.size, (__u64)(uintptr_t)rknpu_obj,
		(__u64)rknpu_obj->dma_addr);

	if (unlikely(copy_to_user((struct rknpu_mem_create *)data, &args,
				  sizeof(struct rknpu_mem_create)))) {
		LOG_ERROR("%s: copy_to_user failed\n", __func__);
		ret = -EFAULT;
		goto err_free_dma_buf;
	}
	return 0;

err_free_dma_buf:
	dma_buf_put(dmabuf);
	if (rknpu_obj->owner)
		rk_dma_heap_buffer_free(dmabuf);
	return ret;

err_free_obj:
	kfree(rknpu_obj);
	return ret;
}

int rknpu_mem_destroy_ioctl(struct rknpu_device *rknpu_dev, unsigned long data)
{
	struct rknpu_mem_object *rknpu_obj = NULL;
	struct rknpu_mem_destroy args;
	struct dma_buf *dmabuf;
	int ret = -EFAULT;

	if (unlikely(copy_from_user(&args, (struct rknpu_mem_destroy *)data,
				    sizeof(struct rknpu_mem_destroy)))) {
		LOG_ERROR("%s: copy_from_user failed\n", __func__);
		ret = -EFAULT;
		return ret;
	}

	if (!kern_addr_valid(args.obj_addr)) {
		LOG_ERROR("%s: invalid params, unknown obj_addr\n", __func__);
		ret = -EFAULT;
		return ret;
	}

	rknpu_obj = (struct rknpu_mem_object *)(uintptr_t)args.obj_addr;
	dmabuf = rknpu_obj->dmabuf;
	LOG_DEBUG(
		"free args.handle = %d, rknpu_obj = %#llx, rknpu_obj->dma_addr = %#llx\n",
		args.handle, (__u64)(uintptr_t)rknpu_obj,
		(__u64)rknpu_obj->dma_addr);

	vunmap(rknpu_obj->kv_addr);

	kfree(rknpu_obj);

	return 0;
}

int rknpu_mem_sync_ioctl(struct rknpu_device *rknpu_dev, unsigned long data)
{
	struct rknpu_mem_object *rknpu_obj = NULL;
	struct rknpu_mem_sync args;
	struct dma_buf *dmabuf;
	int ret = -EFAULT;

	if (unlikely(copy_from_user(&args, (struct rknpu_mem_sync *)data,
				    sizeof(struct rknpu_mem_sync)))) {
		LOG_ERROR("%s: copy_from_user failed\n", __func__);
		ret = -EFAULT;
		return ret;
	}

	rknpu_obj = (struct rknpu_mem_object *)(uintptr_t)args.obj_addr;
	dmabuf = rknpu_obj->dmabuf;

	if (args.flags & RKNPU_MEM_SYNC_TO_DEVICE) {
		dmabuf->ops->end_cpu_access_partial(dmabuf, DMA_TO_DEVICE,
						    args.offset, args.size);
	}
	if (args.flags & RKNPU_MEM_SYNC_FROM_DEVICE) {
		dmabuf->ops->begin_cpu_access_partial(dmabuf, DMA_FROM_DEVICE,
						      args.offset, args.size);
	}

	return 0;
}
