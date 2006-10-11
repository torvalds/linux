/*
 * Copyright(c) 2004 - 2006 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 */
#ifndef IOATDMA_H
#define IOATDMA_H

#include <linux/dmaengine.h>
#include "ioatdma_hw.h"
#include <linux/init.h>
#include <linux/dmapool.h>
#include <linux/cache.h>
#include <linux/pci_ids.h>

#define IOAT_LOW_COMPLETION_MASK	0xffffffc0

extern struct list_head dma_device_list;
extern struct list_head dma_client_list;

/**
 * struct ioat_device - internal representation of a IOAT device
 * @pdev: PCI-Express device
 * @reg_base: MMIO register space base address
 * @dma_pool: for allocating DMA descriptors
 * @common: embedded struct dma_device
 * @msi: Message Signaled Interrupt number
 */

struct ioat_device {
	struct pci_dev *pdev;
	void __iomem *reg_base;
	struct pci_pool *dma_pool;
	struct pci_pool *completion_pool;

	struct dma_device common;
	u8 msi;
};

/**
 * struct ioat_dma_chan - internal representation of a DMA channel
 * @device:
 * @reg_base:
 * @sw_in_use:
 * @completion:
 * @completion_low:
 * @completion_high:
 * @completed_cookie: last cookie seen completed on cleanup
 * @cookie: value of last cookie given to client
 * @last_completion:
 * @xfercap:
 * @desc_lock:
 * @free_desc:
 * @used_desc:
 * @resource:
 * @device_node:
 */

struct ioat_dma_chan {

	void __iomem *reg_base;

	dma_cookie_t completed_cookie;
	unsigned long last_completion;

	u32 xfercap;	/* XFERCAP register value expanded out */

	spinlock_t cleanup_lock;
	spinlock_t desc_lock;
	struct list_head free_desc;
	struct list_head used_desc;

	int pending;

	struct ioat_device *device;
	struct dma_chan common;

	dma_addr_t completion_addr;
	union {
		u64 full; /* HW completion writeback */
		struct {
			u32 low;
			u32 high;
		};
	} *completion_virt;
};

/* wrapper around hardware descriptor format + additional software fields */

/**
 * struct ioat_desc_sw - wrapper around hardware descriptor
 * @hw: hardware DMA descriptor
 * @node:
 * @cookie:
 * @phys:
 */

struct ioat_desc_sw {
	struct ioat_dma_descriptor *hw;
	struct list_head node;
	dma_cookie_t cookie;
	dma_addr_t phys;
	DECLARE_PCI_UNMAP_ADDR(src)
	DECLARE_PCI_UNMAP_LEN(src_len)
	DECLARE_PCI_UNMAP_ADDR(dst)
	DECLARE_PCI_UNMAP_LEN(dst_len)
};

#endif /* IOATDMA_H */

