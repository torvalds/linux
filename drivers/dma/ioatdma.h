/*
 * Copyright(c) 2004 - 2007 Intel Corporation. All rights reserved.
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

#define IOAT_DMA_VERSION  "2.04"

enum ioat_interrupt {
	none = 0,
	msix_multi_vector = 1,
	msix_single_vector = 2,
	msi = 3,
	intx = 4,
};

#define IOAT_LOW_COMPLETION_MASK	0xffffffc0
#define IOAT_DMA_DCA_ANY_CPU		~0


/**
 * struct ioatdma_device - internal representation of a IOAT device
 * @pdev: PCI-Express device
 * @reg_base: MMIO register space base address
 * @dma_pool: for allocating DMA descriptors
 * @common: embedded struct dma_device
 * @version: version of ioatdma device
 * @irq_mode: which style irq to use
 * @msix_entries: irq handlers
 * @idx: per channel data
 */

struct ioatdma_device {
	struct pci_dev *pdev;
	void __iomem *reg_base;
	struct pci_pool *dma_pool;
	struct pci_pool *completion_pool;
	struct dma_device common;
	u8 version;
	enum ioat_interrupt irq_mode;
	struct msix_entry msix_entries[4];
	struct ioat_dma_chan *idx[4];
};

/**
 * struct ioat_dma_chan - internal representation of a DMA channel
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
	int dmacount;
	int desccount;

	struct ioatdma_device *device;
	struct dma_chan common;

	dma_addr_t completion_addr;
	union {
		u64 full; /* HW completion writeback */
		struct {
			u32 low;
			u32 high;
		};
	} *completion_virt;
	struct tasklet_struct cleanup_task;
};

/* wrapper around hardware descriptor format + additional software fields */

/**
 * struct ioat_desc_sw - wrapper around hardware descriptor
 * @hw: hardware DMA descriptor
 * @node: this descriptor will either be on the free list,
 *     or attached to a transaction list (async_tx.tx_list)
 * @tx_cnt: number of descriptors required to complete the transaction
 * @async_tx: the generic software descriptor for all engines
 */
struct ioat_desc_sw {
	struct ioat_dma_descriptor *hw;
	struct list_head node;
	int tx_cnt;
	size_t len;
	dma_addr_t src;
	dma_addr_t dst;
	struct dma_async_tx_descriptor async_tx;
};

#if defined(CONFIG_INTEL_IOATDMA) || defined(CONFIG_INTEL_IOATDMA_MODULE)
struct ioatdma_device *ioat_dma_probe(struct pci_dev *pdev,
				      void __iomem *iobase);
void ioat_dma_remove(struct ioatdma_device *device);
struct dca_provider *ioat_dca_init(struct pci_dev *pdev, void __iomem *iobase);
struct dca_provider *ioat2_dca_init(struct pci_dev *pdev, void __iomem *iobase);
#else
#define ioat_dma_probe(pdev, iobase)    NULL
#define ioat_dma_remove(device)         do { } while (0)
#define ioat_dca_init(pdev, iobase)	NULL
#define ioat2_dca_init(pdev, iobase)	NULL
#endif

#endif /* IOATDMA_H */
