/*
 * Copyright (c) 2013  Luis R. Rodriguez <mcgrof@do-not-panic.com>
 *
 * Backport compatibility file for Linux for some DMA helpers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/dma-attrs.h>
#include <linux/device.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0)
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,6,0)
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <asm/dma-mapping.h>
#endif /* LINUX_VERSION_CODE <= KERNEL_VERSION(3,6,0) */
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0) */

#if RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(7,0)
/*
 * Create scatter-list for the already allocated DMA buffer.
 */
int dma_common_get_sgtable(struct device *dev, struct sg_table *sgt,
		 void *cpu_addr, dma_addr_t handle, size_t size)
{
	struct page *page = virt_to_page(cpu_addr);
	int ret;

	ret = sg_alloc_table(sgt, 1, GFP_KERNEL);
	if (unlikely(ret))
		return ret;

	sg_set_page(sgt->sgl, page, PAGE_ALIGN(size), 0);
	return 0;
}
EXPORT_SYMBOL_GPL(dma_common_get_sgtable);
#endif /* RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(7,0) */
