/*
 * Copyright(c) 2004 - 2009 Intel Corporation. All rights reserved.
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
#include "hw.h"
#include <linux/init.h>
#include <linux/dmapool.h>
#include <linux/cache.h>
#include <linux/pci_ids.h>
#include <net/tcp.h>

#define IOAT_DMA_VERSION  "3.64"

#define IOAT_LOW_COMPLETION_MASK	0xffffffc0
#define IOAT_DMA_DCA_ANY_CPU		~0
#define IOAT_WATCHDOG_PERIOD		(2 * HZ)

#define to_ioatdma_device(dev) container_of(dev, struct ioatdma_device, common)
#define to_ioat_desc(lh) container_of(lh, struct ioat_desc_sw, node)
#define tx_to_ioat_desc(tx) container_of(tx, struct ioat_desc_sw, txd)
#define to_dev(ioat_chan) (&(ioat_chan)->device->pdev->dev)

#define chan_num(ch) ((int)((ch)->reg_base - (ch)->device->reg_base) / 0x80)

#define RESET_DELAY  msecs_to_jiffies(100)
#define WATCHDOG_DELAY  round_jiffies(msecs_to_jiffies(2000))

/*
 * workaround for IOAT ver.3.0 null descriptor issue
 * (channel returns error when size is 0)
 */
#define NULL_DESC_BUFFER_SIZE 1

/**
 * struct ioatdma_device - internal representation of a IOAT device
 * @pdev: PCI-Express device
 * @reg_base: MMIO register space base address
 * @dma_pool: for allocating DMA descriptors
 * @common: embedded struct dma_device
 * @version: version of ioatdma device
 * @msix_entries: irq handlers
 * @idx: per channel data
 * @dca: direct cache access context
 * @intr_quirk: interrupt setup quirk (for ioat_v1 devices)
 */

struct ioatdma_device {
	struct pci_dev *pdev;
	void __iomem *reg_base;
	struct pci_pool *dma_pool;
	struct pci_pool *completion_pool;
	struct dma_device common;
	u8 version;
	struct delayed_work work;
	struct msix_entry msix_entries[4];
	struct ioat_chan_common *idx[4];
	struct dca_provider *dca;
	void (*intr_quirk)(struct ioatdma_device *device);
};

struct ioat_chan_common {
	void __iomem *reg_base;

	unsigned long last_completion;
	unsigned long last_completion_time;

	spinlock_t cleanup_lock;
	dma_cookie_t completed_cookie;
	unsigned long watchdog_completion;
	int watchdog_tcp_cookie;
	u32 watchdog_last_tcp_cookie;
	struct delayed_work work;

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
	unsigned long last_compl_desc_addr_hw;
	struct tasklet_struct cleanup_task;
};

/**
 * struct ioat_dma_chan - internal representation of a DMA channel
 */
struct ioat_dma_chan {
	struct ioat_chan_common base;

	size_t xfercap;	/* XFERCAP register value expanded out */

	spinlock_t desc_lock;
	struct list_head free_desc;
	struct list_head used_desc;

	int pending;
	u16 dmacount;
	u16 desccount;
};

static inline struct ioat_chan_common *to_chan_common(struct dma_chan *c)
{
	return container_of(c, struct ioat_chan_common, common);
}

static inline struct ioat_dma_chan *to_ioat_chan(struct dma_chan *c)
{
	struct ioat_chan_common *chan = to_chan_common(c);

	return container_of(chan, struct ioat_dma_chan, base);
}

/* wrapper around hardware descriptor format + additional software fields */

/**
 * struct ioat_desc_sw - wrapper around hardware descriptor
 * @hw: hardware DMA descriptor
 * @node: this descriptor will either be on the free list,
 *     or attached to a transaction list (async_tx.tx_list)
 * @tx_cnt: number of descriptors required to complete the transaction
 * @txd: the generic software descriptor for all engines
 */
struct ioat_desc_sw {
	struct ioat_dma_descriptor *hw;
	struct list_head node;
	int tx_cnt;
	size_t len;
	dma_addr_t src;
	dma_addr_t dst;
	struct dma_async_tx_descriptor txd;
};

static inline void ioat_set_tcp_copy_break(unsigned long copybreak)
{
	#ifdef CONFIG_NET_DMA
	sysctl_tcp_dma_copybreak = copybreak;
	#endif
}

int ioat1_dma_probe(struct ioatdma_device *dev, int dca);
int ioat2_dma_probe(struct ioatdma_device *dev, int dca);
int ioat3_dma_probe(struct ioatdma_device *dev, int dca);
void ioat_dma_remove(struct ioatdma_device *device);
struct dca_provider *ioat_dca_init(struct pci_dev *pdev, void __iomem *iobase);
struct dca_provider *ioat2_dca_init(struct pci_dev *pdev, void __iomem *iobase);
struct dca_provider *ioat3_dca_init(struct pci_dev *pdev, void __iomem *iobase);
#endif /* IOATDMA_H */
