/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#ifndef __LINUX_RKNPU_MEM_H
#define __LINUX_RKNPU_MEM_H

#include <linux/mm_types.h>
#include <linux/version.h>

/*
 * rknpu DMA buffer structure.
 *
 * @flags: indicate memory type to allocated buffer and cache attribute.
 * @size: size requested from user, in bytes and this size is aligned
 *	in page unit.
 * @kv_addr: kernel virtual address to allocated memory region.
 * @dma_addr: bus address(accessed by dma) to allocated memory region.
 *	- this address could be physical address without IOMMU and
 *	device address with IOMMU.
 * @pages: Array of backing pages.
 * @sgt: Imported sg_table.
 * @dmabuf: buffer for this attachment.
 * @owner: Is this memory internally allocated.
 */
struct rknpu_mem_object {
	unsigned long flags;
	unsigned long size;
	void __iomem *kv_addr;
	dma_addr_t dma_addr;
	struct page **pages;
	struct sg_table *sgt;
	struct dma_buf *dmabuf;
	unsigned int owner;
};

int rknpu_mem_create_ioctl(struct rknpu_device *rknpu_dev, unsigned long data);
int rknpu_mem_destroy_ioctl(struct rknpu_device *rknpu_dev, unsigned long data);
int rknpu_mem_sync_ioctl(struct rknpu_device *rknpu_dev, unsigned long data);

#endif
