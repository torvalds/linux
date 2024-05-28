/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SiFive FU540 Platform DMA driver
 * Copyright (C) 2019 SiFive
 *
 * Based partially on:
 * - drivers/dma/fsl-edma.c
 * - drivers/dma/dw-edma/
 * - drivers/dma/pxa-dma.c
 *
 * See the following sources for further documentation:
 * - Chapter 12 "Platform DMA Engine (PDMA)" of
 *   SiFive FU540-C000 v1.0
 *   https://static.dev.sifive.com/FU540-C000-v1.0.pdf
 */
#ifndef _SF_PDMA_H
#define _SF_PDMA_H

#include <linux/dmaengine.h>
#include <linux/dma-direction.h>

#include "../dmaengine.h"
#include "../virt-dma.h"

#define PDMA_MAX_NR_CH					4

#define PDMA_BASE_ADDR					0x3000000
#define PDMA_CHAN_OFFSET				0x1000

/* Register Offset */
#define PDMA_CTRL					0x000
#define PDMA_XFER_TYPE					0x004
#define PDMA_XFER_SIZE					0x008
#define PDMA_DST_ADDR					0x010
#define PDMA_SRC_ADDR					0x018
#define PDMA_ACT_TYPE					0x104 /* Read-only */
#define PDMA_REMAINING_BYTE				0x108 /* Read-only */
#define PDMA_CUR_DST_ADDR				0x110 /* Read-only*/
#define PDMA_CUR_SRC_ADDR				0x118 /* Read-only*/

/* CTRL */
#define PDMA_CLEAR_CTRL					0x0
#define PDMA_CLAIM_MASK					GENMASK(0, 0)
#define PDMA_RUN_MASK					GENMASK(1, 1)
#define PDMA_ENABLE_DONE_INT_MASK			GENMASK(14, 14)
#define PDMA_ENABLE_ERR_INT_MASK			GENMASK(15, 15)
#define PDMA_DONE_STATUS_MASK				GENMASK(30, 30)
#define PDMA_ERR_STATUS_MASK				GENMASK(31, 31)

/* Transfer Type */
#define PDMA_FULL_SPEED					0xFF000000
#define PDMA_STRICT_ORDERING				BIT(3)

/* Error Recovery */
#define MAX_RETRY					1

#define SF_PDMA_REG_BASE(ch)	(pdma->membase + (PDMA_CHAN_OFFSET * (ch)))

struct pdma_regs {
	/* read-write regs */
	void __iomem *ctrl;		/* 4 bytes */

	void __iomem *xfer_type;	/* 4 bytes */
	void __iomem *xfer_size;	/* 8 bytes */
	void __iomem *dst_addr;		/* 8 bytes */
	void __iomem *src_addr;		/* 8 bytes */

	/* read-only */
	void __iomem *act_type;		/* 4 bytes */
	void __iomem *residue;		/* 8 bytes */
	void __iomem *cur_dst_addr;	/* 8 bytes */
	void __iomem *cur_src_addr;	/* 8 bytes */
};

struct sf_pdma_desc {
	u32				xfer_type;
	u64				xfer_size;
	u64				dst_addr;
	u64				src_addr;
	struct virt_dma_desc		vdesc;
	struct sf_pdma_chan		*chan;
	enum dma_transfer_direction	dirn;
	struct dma_async_tx_descriptor *async_tx;
};

enum sf_pdma_pm_state {
	RUNNING = 0,
	SUSPENDED,
};

struct sf_pdma_chan {
	struct virt_dma_chan		vchan;
	enum dma_status			status;
	enum sf_pdma_pm_state		pm_state;
	u32				slave_id;
	struct sf_pdma			*pdma;
	struct sf_pdma_desc		*desc;
	struct dma_slave_config		cfg;
	u32				attr;
	dma_addr_t			dma_dev_addr;
	u32				dma_dev_size;
	struct tasklet_struct		done_tasklet;
	struct tasklet_struct		err_tasklet;
	struct pdma_regs		regs;
	spinlock_t			lock; /* protect chan data */
	bool				xfer_err;
	int				txirq;
	int				errirq;
	int				retries;
};

struct sf_pdma {
	struct dma_device       dma_dev;
	void __iomem            *membase;
	void __iomem            *mappedbase;
	u32			transfer_type;
	u32			n_chans;
	struct sf_pdma_chan	chans[] __counted_by(n_chans);
};

struct sf_pdma_driver_platdata {
	u32 quirks;
};

#endif /* _SF_PDMA_H */
