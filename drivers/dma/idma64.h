/*
 * Driver for the Intel integrated DMA 64-bit
 *
 * Copyright (C) 2015 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __DMA_IDMA64_H__
#define __DMA_IDMA64_H__

#include <linux/device.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <linux/io-64-nonatomic-lo-hi.h>

#include "virt-dma.h"

/* Channel registers */

#define IDMA64_CH_SAR		0x00	/* Source Address Register */
#define IDMA64_CH_DAR		0x08	/* Destination Address Register */
#define IDMA64_CH_LLP		0x10	/* Linked List Pointer */
#define IDMA64_CH_CTL_LO	0x18	/* Control Register Low */
#define IDMA64_CH_CTL_HI	0x1c	/* Control Register High */
#define IDMA64_CH_SSTAT		0x20
#define IDMA64_CH_DSTAT		0x28
#define IDMA64_CH_SSTATAR	0x30
#define IDMA64_CH_DSTATAR	0x38
#define IDMA64_CH_CFG_LO	0x40	/* Configuration Register Low */
#define IDMA64_CH_CFG_HI	0x44	/* Configuration Register High */
#define IDMA64_CH_SGR		0x48
#define IDMA64_CH_DSR		0x50

#define IDMA64_CH_LENGTH	0x58

/* Bitfields in CTL_LO */
#define IDMA64C_CTLL_INT_EN		(1 << 0)	/* irqs enabled? */
#define IDMA64C_CTLL_DST_WIDTH(x)	((x) << 1)	/* bytes per element */
#define IDMA64C_CTLL_SRC_WIDTH(x)	((x) << 4)
#define IDMA64C_CTLL_DST_INC		(0 << 8)	/* DAR update/not */
#define IDMA64C_CTLL_DST_FIX		(1 << 8)
#define IDMA64C_CTLL_SRC_INC		(0 << 10)	/* SAR update/not */
#define IDMA64C_CTLL_SRC_FIX		(1 << 10)
#define IDMA64C_CTLL_DST_MSIZE(x)	((x) << 11)	/* burst, #elements */
#define IDMA64C_CTLL_SRC_MSIZE(x)	((x) << 14)
#define IDMA64C_CTLL_FC_M2P		(1 << 20)	/* mem-to-periph */
#define IDMA64C_CTLL_FC_P2M		(2 << 20)	/* periph-to-mem */
#define IDMA64C_CTLL_LLP_D_EN		(1 << 27)	/* dest block chain */
#define IDMA64C_CTLL_LLP_S_EN		(1 << 28)	/* src block chain */

/* Bitfields in CTL_HI */
#define IDMA64C_CTLH_BLOCK_TS_MASK	((1 << 17) - 1)
#define IDMA64C_CTLH_BLOCK_TS(x)	((x) & IDMA64C_CTLH_BLOCK_TS_MASK)
#define IDMA64C_CTLH_DONE		(1 << 17)

/* Bitfields in CFG_LO */
#define IDMA64C_CFGL_DST_BURST_ALIGN	(1 << 0)	/* dst burst align */
#define IDMA64C_CFGL_SRC_BURST_ALIGN	(1 << 1)	/* src burst align */
#define IDMA64C_CFGL_CH_SUSP		(1 << 8)
#define IDMA64C_CFGL_FIFO_EMPTY		(1 << 9)
#define IDMA64C_CFGL_CH_DRAIN		(1 << 10)	/* drain FIFO */
#define IDMA64C_CFGL_DST_OPT_BL		(1 << 20)	/* optimize dst burst length */
#define IDMA64C_CFGL_SRC_OPT_BL		(1 << 21)	/* optimize src burst length */

/* Bitfields in CFG_HI */
#define IDMA64C_CFGH_SRC_PER(x)		((x) << 0)	/* src peripheral */
#define IDMA64C_CFGH_DST_PER(x)		((x) << 4)	/* dst peripheral */
#define IDMA64C_CFGH_RD_ISSUE_THD(x)	((x) << 8)
#define IDMA64C_CFGH_WR_ISSUE_THD(x)	((x) << 18)

/* Interrupt registers */

#define IDMA64_INT_XFER		0x00
#define IDMA64_INT_BLOCK	0x08
#define IDMA64_INT_SRC_TRAN	0x10
#define IDMA64_INT_DST_TRAN	0x18
#define IDMA64_INT_ERROR	0x20

#define IDMA64_RAW(x)		(0x2c0 + IDMA64_INT_##x)	/* r */
#define IDMA64_STATUS(x)	(0x2e8 + IDMA64_INT_##x)	/* r (raw & mask) */
#define IDMA64_MASK(x)		(0x310 + IDMA64_INT_##x)	/* rw (set = irq enabled) */
#define IDMA64_CLEAR(x)		(0x338 + IDMA64_INT_##x)	/* w (ack, affects "raw") */

/* Common registers */

#define IDMA64_STATUS_INT	0x360	/* r */
#define IDMA64_CFG		0x398
#define IDMA64_CH_EN		0x3a0

/* Bitfields in CFG */
#define IDMA64_CFG_DMA_EN		(1 << 0)

/* Hardware descriptor for Linked LIst transfers */
struct idma64_lli {
	u64		sar;
	u64		dar;
	u64		llp;
	u32		ctllo;
	u32		ctlhi;
	u32		sstat;
	u32		dstat;
};

struct idma64_hw_desc {
	struct idma64_lli *lli;
	dma_addr_t llp;
	dma_addr_t phys;
	unsigned int len;
};

struct idma64_desc {
	struct virt_dma_desc vdesc;
	enum dma_transfer_direction direction;
	struct idma64_hw_desc *hw;
	unsigned int ndesc;
	size_t length;
	enum dma_status status;
};

static inline struct idma64_desc *to_idma64_desc(struct virt_dma_desc *vdesc)
{
	return container_of(vdesc, struct idma64_desc, vdesc);
}

struct idma64_chan {
	struct virt_dma_chan vchan;

	void __iomem *regs;

	/* hardware configuration */
	enum dma_transfer_direction direction;
	unsigned int mask;
	struct dma_slave_config config;

	void *pool;
	struct idma64_desc *desc;
};

static inline struct idma64_chan *to_idma64_chan(struct dma_chan *chan)
{
	return container_of(chan, struct idma64_chan, vchan.chan);
}

#define channel_set_bit(idma64, reg, mask)	\
	dma_writel(idma64, reg, ((mask) << 8) | (mask))
#define channel_clear_bit(idma64, reg, mask)	\
	dma_writel(idma64, reg, ((mask) << 8) | 0)

static inline u32 idma64c_readl(struct idma64_chan *idma64c, int offset)
{
	return readl(idma64c->regs + offset);
}

static inline void idma64c_writel(struct idma64_chan *idma64c, int offset,
				  u32 value)
{
	writel(value, idma64c->regs + offset);
}

#define channel_readl(idma64c, reg)		\
	idma64c_readl(idma64c, IDMA64_CH_##reg)
#define channel_writel(idma64c, reg, value)	\
	idma64c_writel(idma64c, IDMA64_CH_##reg, (value))

static inline u64 idma64c_readq(struct idma64_chan *idma64c, int offset)
{
	return lo_hi_readq(idma64c->regs + offset);
}

static inline void idma64c_writeq(struct idma64_chan *idma64c, int offset,
				  u64 value)
{
	lo_hi_writeq(value, idma64c->regs + offset);
}

#define channel_readq(idma64c, reg)		\
	idma64c_readq(idma64c, IDMA64_CH_##reg)
#define channel_writeq(idma64c, reg, value)	\
	idma64c_writeq(idma64c, IDMA64_CH_##reg, (value))

struct idma64 {
	struct dma_device dma;

	void __iomem *regs;

	/* channels */
	unsigned short all_chan_mask;
	struct idma64_chan *chan;
};

static inline struct idma64 *to_idma64(struct dma_device *ddev)
{
	return container_of(ddev, struct idma64, dma);
}

static inline u32 idma64_readl(struct idma64 *idma64, int offset)
{
	return readl(idma64->regs + offset);
}

static inline void idma64_writel(struct idma64 *idma64, int offset, u32 value)
{
	writel(value, idma64->regs + offset);
}

#define dma_readl(idma64, reg)			\
	idma64_readl(idma64, IDMA64_##reg)
#define dma_writel(idma64, reg, value)		\
	idma64_writel(idma64, IDMA64_##reg, (value))

/**
 * struct idma64_chip - representation of iDMA 64-bit controller hardware
 * @dev:		struct device of the DMA controller
 * @sysdev:		struct device of the physical device that does DMA
 * @irq:		irq line
 * @regs:		memory mapped I/O space
 * @idma64:		struct idma64 that is filed by idma64_probe()
 */
struct idma64_chip {
	struct device	*dev;
	struct device	*sysdev;
	int		irq;
	void __iomem	*regs;
	struct idma64	*idma64;
};

#endif /* __DMA_IDMA64_H__ */
