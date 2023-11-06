/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018-2019 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare eDMA core driver
 *
 * Author: Gustavo Pimentel <gustavo.pimentel@synopsys.com>
 */

#ifndef _DW_EDMA_CORE_H
#define _DW_EDMA_CORE_H

#include <linux/msi.h>
#include <linux/dma/edma.h>

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

struct dw_edma_chan;
struct dw_edma_chunk;

struct dw_edma_burst {
	struct list_head		list;
	u64				sar;
	u64				dar;
	u32				sz;
};

struct dw_edma_chunk {
	struct list_head		list;
	struct dw_edma_chan		*chan;
	struct dw_edma_burst		*burst;

	u32				bursts_alloc;

	u8				cb;
	struct dw_edma_region		ll_region;	/* Linked list */
};

struct dw_edma_desc {
	struct virt_dma_desc		vd;
	struct dw_edma_chan		*chan;
	struct dw_edma_chunk		*chunk;

	u32				chunks_alloc;

	u32				alloc_sz;
	u32				xfer_sz;
};

struct dw_edma_chan {
	struct virt_dma_chan		vc;
	struct dw_edma			*dw;
	int				id;
	enum dw_edma_dir		dir;

	u32				ll_max;

	struct msi_msg			msi;

	enum dw_edma_request		request;
	enum dw_edma_status		status;
	u8				configured;

	struct dma_slave_config		config;
};

struct dw_edma_irq {
	struct msi_msg                  msi;
	u32				wr_mask;
	u32				rd_mask;
	struct dw_edma			*dw;
};

struct dw_edma {
	char				name[32];

	struct dma_device		dma;

	u16				wr_ch_cnt;
	u16				rd_ch_cnt;

	struct dw_edma_irq		*irq;
	int				nr_irqs;

	struct dw_edma_chan		*chan;

	raw_spinlock_t			lock;		/* Only for legacy */

	struct dw_edma_chip             *chip;

	const struct dw_edma_core_ops	*core;
};

typedef void (*dw_edma_handler_t)(struct dw_edma_chan *);

struct dw_edma_core_ops {
	void (*off)(struct dw_edma *dw);
	u16 (*ch_count)(struct dw_edma *dw, enum dw_edma_dir dir);
	enum dma_status (*ch_status)(struct dw_edma_chan *chan);
	irqreturn_t (*handle_int)(struct dw_edma_irq *dw_irq, enum dw_edma_dir dir,
				  dw_edma_handler_t done, dw_edma_handler_t abort);
	void (*start)(struct dw_edma_chunk *chunk, bool first);
	void (*ch_config)(struct dw_edma_chan *chan);
	void (*debugfs_on)(struct dw_edma *dw);
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

static inline
void dw_edma_core_off(struct dw_edma *dw)
{
	dw->core->off(dw);
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
			dw_edma_handler_t done, dw_edma_handler_t abort)
{
	return dw_irq->dw->core->handle_int(dw_irq, dir, done, abort);
}

static inline
void dw_edma_core_start(struct dw_edma *dw, struct dw_edma_chunk *chunk, bool first)
{
	dw->core->start(chunk, first);
}

static inline
void dw_edma_core_ch_config(struct dw_edma_chan *chan)
{
	chan->dw->core->ch_config(chan);
}

static inline
void dw_edma_core_debugfs_on(struct dw_edma *dw)
{
	dw->core->debugfs_on(dw);
}

#endif /* _DW_EDMA_CORE_H */
