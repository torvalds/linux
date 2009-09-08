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
 * @enumerate_channels: hw version specific channel enumeration
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
	int (*enumerate_channels)(struct ioatdma_device *device);
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

	dma_addr_t completion_dma;
	u64 *completion;
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

/**
 * ioat_is_complete - poll the status of an ioat transaction
 * @c: channel handle
 * @cookie: transaction identifier
 * @done: if set, updated with last completed transaction
 * @used: if set, updated with last used transaction
 */
static inline enum dma_status
ioat_is_complete(struct dma_chan *c, dma_cookie_t cookie,
		 dma_cookie_t *done, dma_cookie_t *used)
{
	struct ioat_chan_common *chan = to_chan_common(c);
	dma_cookie_t last_used;
	dma_cookie_t last_complete;

	last_used = c->cookie;
	last_complete = chan->completed_cookie;
	chan->watchdog_tcp_cookie = cookie;

	if (done)
		*done = last_complete;
	if (used)
		*used = last_used;

	return dma_async_is_complete(cookie, last_complete, last_used);
}

/* wrapper around hardware descriptor format + additional software fields */

/**
 * struct ioat_desc_sw - wrapper around hardware descriptor
 * @hw: hardware DMA descriptor
 * @node: this descriptor will either be on the free list,
 *     or attached to a transaction list (async_tx.tx_list)
 * @txd: the generic software descriptor for all engines
 * @id: identifier for debug
 */
struct ioat_desc_sw {
	struct ioat_dma_descriptor *hw;
	struct list_head node;
	size_t len;
	struct dma_async_tx_descriptor txd;
	#ifdef DEBUG
	int id;
	#endif
};

#ifdef DEBUG
#define set_desc_id(desc, i) ((desc)->id = (i))
#define desc_id(desc) ((desc)->id)
#else
#define set_desc_id(desc, i)
#define desc_id(desc) (0)
#endif

static inline void
__dump_desc_dbg(struct ioat_chan_common *chan, struct ioat_dma_descriptor *hw,
		struct dma_async_tx_descriptor *tx, int id)
{
	struct device *dev = to_dev(chan);

	dev_dbg(dev, "desc[%d]: (%#llx->%#llx) cookie: %d flags: %#x"
		" ctl: %#x (op: %d int_en: %d compl: %d)\n", id,
		(unsigned long long) tx->phys,
		(unsigned long long) hw->next, tx->cookie, tx->flags,
		hw->ctl, hw->ctl_f.op, hw->ctl_f.int_en, hw->ctl_f.compl_write);
}

#define dump_desc_dbg(c, d) \
	({ if (d) __dump_desc_dbg(&c->base, d->hw, &d->txd, desc_id(d)); 0; })

static inline void ioat_set_tcp_copy_break(unsigned long copybreak)
{
	#ifdef CONFIG_NET_DMA
	sysctl_tcp_dma_copybreak = copybreak;
	#endif
}

static inline struct ioat_chan_common *
ioat_chan_by_index(struct ioatdma_device *device, int index)
{
	return device->idx[index];
}

int __devinit ioat_probe(struct ioatdma_device *device);
int __devinit ioat_register(struct ioatdma_device *device);
int __devinit ioat1_dma_probe(struct ioatdma_device *dev, int dca);
void __devexit ioat_dma_remove(struct ioatdma_device *device);
struct dca_provider * __devinit ioat_dca_init(struct pci_dev *pdev,
					      void __iomem *iobase);
unsigned long ioat_get_current_completion(struct ioat_chan_common *chan);
void ioat_init_channel(struct ioatdma_device *device,
		       struct ioat_chan_common *chan, int idx,
		       work_func_t work_fn, void (*tasklet)(unsigned long),
		       unsigned long tasklet_data);
void ioat_dma_unmap(struct ioat_chan_common *chan, enum dma_ctrl_flags flags,
		    size_t len, struct ioat_dma_descriptor *hw);
#endif /* IOATDMA_H */
