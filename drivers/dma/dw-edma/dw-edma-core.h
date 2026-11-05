/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018-2019 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare eDMA core driver
 *
 * Author: Gustavo Pimentel <gustavo.pimentel@synopsys.com>
 */

#ifndef _DW_EDMA_CORE_H
#define _DW_EDMA_CORE_H

#include <linux/atomic.h>
#include <linux/msi.h>
#include <linux/dma/edma.h>
#include <linux/workqueue.h>

#include "../virt-dma.h"

#define EDMA_LL_SZ					24

enum dw_edma_dir {
	EDMA_DIR_WRITE = 0,
	EDMA_DIR_READ
};

enum dw_edma_request {
	EDMA_REQ_NONE = 0,
	EDMA_REQ_STOP,
	EDMA_REQ_PAUSE
};

enum dw_edma_status {
	EDMA_ST_IDLE = 0,
	EDMA_ST_PAUSE,
	EDMA_ST_BUSY
};

enum dw_edma_xfer_type {
	EDMA_XFER_SCATTER_GATHER = 0,
	EDMA_XFER_CYCLIC,
	EDMA_XFER_INTERLEAVED
};

enum dw_edma_irq_event {
	DW_EDMA_IRQ_DONE	= BIT(0),
	DW_EDMA_IRQ_PROGRESS	= BIT(1),
	DW_EDMA_IRQ_STOP	= BIT(2),
	DW_EDMA_IRQ_ABORT	= BIT(3),
};

struct dw_edma_chan;
struct dw_edma_chunk;

struct dw_edma_burst {
	u64				sar;
	u64				dar;
	u32				sz;
	/* precalulate summary of previous burst total size */
	u32				xfer_sz;
};

struct dw_edma_desc {
	struct virt_dma_desc		vd;
	struct dw_edma_chan		*chan;

	u32				alloc_sz;

	size_t				done_burst;
	size_t				start_burst;
	size_t				nburst;
	struct dw_edma_burst            burst[] __counted_by(nburst);
};

struct dw_edma_chan {
	struct virt_dma_chan		vc;
	struct dw_edma			*dw;
	int				id;
	enum dw_edma_dir		dir;
	u8				func_no;

	/*
	 * New LL entries are appended at ll_head. Entries between ll_done
	 * and ll_head, modulo the LL ring, are owned by DMA; the rest are
	 * owned by software.
	 *
	 *   software-owned      DMA-owned       software-owned
	 * +---------------+-------------------+---------------+
	 * ^               ^                   ^
	 * 0             ll_done             ll_head
	 *
	 * The link entry points back to the region start. ll_head == ll_done
	 * means all entries are software-owned and previous DMA work is
	 * done.
	 *
	 * Software always keeps at least one free entry, so the ring is
	 * never completely DMA-owned. That keeps a hardware-reported physical
	 * LL index unique within the current ll_done..ll_head producer window.
	 */
	u32				ll_head;
	u32				ll_done;

	u32				ll_max;		/* Data entries */
	struct dw_edma_region		ll_region;	/* Linked list */
	bool				ll_valid;	/* LL context programmed */

	bool				cb;

	struct msi_msg			msi;

	enum dw_edma_ch_irq_mode	irq_mode;

	enum dw_edma_request		request;
	enum dw_edma_status		status;
	u8				configured;

	struct dma_slave_config		config;
	bool				non_ll;

	struct work_struct		irq_work;
	atomic_t			irq_pending;
};

struct dw_edma_irq {
	struct msi_msg                  msi;
	struct dw_edma			*dw;

	DECLARE_BITMAP(wr_mask, HDMA_MAX_WR_CH);
	DECLARE_BITMAP(rd_mask, HDMA_MAX_RD_CH);
};

struct dw_edma {
	char				name[32];

	struct dma_device		dma;

	u16				wr_ch_cnt;
	u16				rd_ch_cnt;

	struct dw_edma_irq		*irq;
	int				nr_irqs;

	struct dw_edma_chan		*chan;

	/*
	 * WQ_HIGHPRI keeps completion processing responsive under heavy load;
	 * WQ_UNBOUND lets different channels run on different CPUs.
	 */
	struct workqueue_struct		*wq;

	raw_spinlock_t			lock;		/* Protect v0 shared registers */

	struct dw_edma_chip             *chip;

	const struct dw_edma_core_ops	*core;
};

typedef void (*dw_edma_handler_t)(struct dw_edma_chan *chan,
				  unsigned int events);

struct dw_edma_core_ops {
	void (*off)(struct dw_edma *dw);
	int (*quiesce)(struct dw_edma *dw);
	int (*ch_quiesce)(struct dw_edma_chan *chan);
	u16 (*ch_count)(struct dw_edma *dw, enum dw_edma_dir dir);
	enum dma_status (*ch_status)(struct dw_edma_chan *chan);
	irqreturn_t (*handle_int)(struct dw_edma_irq *dw_irq, enum dw_edma_dir dir,
				  dw_edma_handler_t handler);
	void (*non_ll_start)(struct dw_edma_chan *chan, struct dw_edma_burst *child);
	void (*ll_data)(struct dw_edma_chan *chan, struct dw_edma_burst *burst,
			u32 idx, bool cb, bool irq);
	void (*ll_link)(struct dw_edma_chan *chan, u32 idx, bool cb, u64 addr);
	void (*ll_clear)(struct dw_edma_chan *chan, u32 idx);
	int (*ll_cur_idx)(struct dw_edma_chan *chan);
	void (*ll_irq_clear)(struct dw_edma_chan *chan);
	void (*ch_doorbell)(struct dw_edma_chan *chan);
	void (*ch_enable)(struct dw_edma_chan *chan);
	void (*ch_config)(struct dw_edma_chan *chan);
	void (*debugfs_on)(struct dw_edma *dw);
	void (*ack_emulated_irq)(struct dw_edma *dw);
	resource_size_t (*db_offset)(struct dw_edma *dw);
};

struct dw_edma_sg {
	struct scatterlist		*sgl;
	unsigned int			len;
};

struct dw_edma_cyclic {
	dma_addr_t			paddr;
	size_t				len;
	size_t				cnt;
};

struct dw_edma_transfer {
	struct dma_chan			*dchan;
	union dw_edma_xfer {
		struct dw_edma_sg		sg;
		struct dw_edma_cyclic		cyclic;
		struct dma_interleaved_template *il;
	} xfer;
	enum dma_transfer_direction	direction;
	unsigned long			flags;
	enum dw_edma_xfer_type		type;
};

static inline
struct dw_edma_chan *vc2dw_edma_chan(struct virt_dma_chan *vc)
{
	return container_of(vc, struct dw_edma_chan, vc);
}

static inline
struct dw_edma_chan *dchan2dw_edma_chan(struct dma_chan *dchan)
{
	return vc2dw_edma_chan(to_virt_chan(dchan));
}

/*
 * Return the current LL entry index. A negative value means that the channel
 * context is not initialized or was lost after a link reset.
 */
static inline int dw_edma_core_ll_cur_idx(struct dw_edma_chan *chan)
{
	return chan->dw->core->ll_cur_idx(chan);
}

static inline u64 dw_edma_core_get_ll_paddr(struct dw_edma_chan *chan)
{
	if (chan->dir == EDMA_DIR_WRITE)
		return chan->dw->chip->ll_region_wr[chan->id].paddr;

	return chan->dw->chip->ll_region_rd[chan->id].paddr;
}

static inline
void dw_edma_core_off(struct dw_edma *dw)
{
	dw->core->off(dw);
}

static inline
int dw_edma_core_quiesce(struct dw_edma *dw)
{
	return dw->core->quiesce(dw);
}

static inline
int dw_edma_core_ch_quiesce(struct dw_edma_chan *chan)
{
	return chan->dw->core->ch_quiesce(chan);
}

static inline
u16 dw_edma_core_ch_count(struct dw_edma *dw, enum dw_edma_dir dir)
{
	return dw->core->ch_count(dw, dir);
}

static inline
enum dma_status dw_edma_core_ch_status(struct dw_edma_chan *chan)
{
	return chan->dw->core->ch_status(chan);
}

static inline irqreturn_t
dw_edma_core_handle_int(struct dw_edma_irq *dw_irq, enum dw_edma_dir dir,
			dw_edma_handler_t handler)
{
	return dw_irq->dw->core->handle_int(dw_irq, dir, handler);
}

static inline
void dw_edma_core_ch_config(struct dw_edma_chan *chan)
{
	chan->dw->core->ch_config(chan);
}

static inline void
dw_edma_core_ll_data(struct dw_edma_chan *chan, struct dw_edma_burst *burst,
		     u32 idx, bool cb, bool irq)
{
	chan->dw->core->ll_data(chan, burst, idx, cb, irq);
}

static inline void
dw_edma_core_ll_link(struct dw_edma_chan *chan, u32 idx, bool cb, u64 addr)
{
	chan->dw->core->ll_link(chan, idx, cb, addr);
}

static inline void dw_edma_core_ll_clear(struct dw_edma_chan *chan, u32 idx)
{
	chan->dw->core->ll_clear(chan, idx);
}

static inline void dw_edma_core_ll_irq_clear(struct dw_edma_chan *chan)
{
	chan->dw->core->ll_irq_clear(chan);
}

static inline void dw_edma_core_do_ch_doorbell(struct dw_edma_chan *chan)
{
	chan->dw->core->ch_doorbell(chan);
}

static inline void dw_edma_core_ch_enable(struct dw_edma_chan *chan)
{
	chan->dw->core->ch_enable(chan);
}

static inline
void dw_edma_core_debugfs_on(struct dw_edma *dw)
{
	dw->core->debugfs_on(dw);
}

static inline int dw_edma_core_ack_emulated_irq(struct dw_edma *dw)
{
	if (!dw->core->ack_emulated_irq)
		return -EOPNOTSUPP;

	dw->core->ack_emulated_irq(dw);
	return 0;
}

static inline resource_size_t
dw_edma_core_db_offset(struct dw_edma *dw)
{
	return dw->core->db_offset(dw);
}

static inline bool
dw_edma_core_ch_ignore_irq(struct dw_edma_chan *chan)
{
	struct dw_edma *dw = chan->dw;

	if (dw->chip->flags & DW_EDMA_CHIP_LOCAL)
		return chan->irq_mode == DW_EDMA_CH_IRQ_REMOTE;
	else
		return chan->irq_mode == DW_EDMA_CH_IRQ_LOCAL;
}

#endif /* _DW_EDMA_CORE_H */
