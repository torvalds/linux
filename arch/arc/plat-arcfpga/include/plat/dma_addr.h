/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * vineetg: Feb 2009
 *  -For AA4 board, kernel to DMA address APIs
 */

/*
 * kernel addresses are 0x800_000 based, while Bus addr are 0 based
 */

#ifndef __PLAT_DMA_ADDR_H
#define __PLAT_DMA_ADDR_H

#include <linux/device.h>

static inline unsigned long plat_dma_addr_to_kernel(struct device *dev,
						    dma_addr_t dma_addr)
{
	return dma_addr + PAGE_OFFSET;
}

static inline dma_addr_t plat_kernel_addr_to_dma(struct device *dev, void *ptr)
{
	unsigned long addr = (unsigned long)ptr;
	/*
	 * To Catch buggy drivers which can call DMA map API with kernel vaddr
	 * i.e. for buffers alloc via vmalloc or ioremap which are not
	 * gaurnateed to be PHY contiguous and hence unfit for DMA anyways.
	 * On ARC kernel virtual address is 0x7000_0000 to 0x7FFF_FFFF, so
	 * ideally we want to check this range here, but our implementation is
	 * better as it checks for even worse user virtual address as well.
	 */
	if (likely(addr >= PAGE_OFFSET))
		return addr - PAGE_OFFSET;

	BUG();
	return addr;
}

#endif
