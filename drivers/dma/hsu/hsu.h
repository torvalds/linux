/*
 * Driver for the High Speed UART DMA
 *
 * Copyright (C) 2015 Intel Corporation
 *
 * Partially based on the bits found in drivers/tty/serial/mfd.c.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __DMA_HSU_H__
#define __DMA_HSU_H__

#include <linux/spinlock.h>
#include <linux/dma/hsu.h>

#include "../virt-dma.h"

#define HSU_CH_SR		0x00			/* channel status */
#define HSU_CH_CR		0x04			/* channel control */
#define HSU_CH_DCR		0x08			/* descriptor control */
#define HSU_CH_BSR		0x10			/* FIFO buffer size */
#define HSU_CH_MTSR		0x14			/* minimum transfer size */
#define HSU_CH_DxSAR(x)		(0x20 + 8 * (x))	/* desc start addr */
#define HSU_CH_DxTSR(x)		(0x24 + 8 * (x))	/* desc transfer size */
#define HSU_CH_D0SAR		0x20			/* desc 0 start addr */
#define HSU_CH_D0TSR		0x24			/* desc 0 transfer size */
#define HSU_CH_D1SAR		0x28
#define HSU_CH_D1TSR		0x2c
#define HSU_CH_D2SAR		0x30
#define HSU_CH_D2TSR		0x34
#define HSU_CH_D3SAR		0x38
#define HSU_CH_D3TSR		0x3c

#define HSU_DMA_CHAN_NR_DESC	4
#define HSU_DMA_CHAN_LENGTH	0x40

/* Bits in HSU_CH_SR */
#define HSU_CH_SR_DESCTO(x)	BIT(8 + (x))
#define HSU_CH_SR_DESCTO_ANY	(BIT(11) | BIT(10) | BIT(9) | BIT(8))
#define HSU_CH_SR_CHE		BIT(15)

/* Bits in HSU_CH_CR */
#define HSU_CH_CR_CHA		BIT(0)
#define HSU_CH_CR_CHD		BIT(1)

/* Bits in HSU_CH_DCR */
#define HSU_CH_DCR_DESCA(x)	BIT(0 + (x))
#define HSU_CH_DCR_CHSOD(x)	BIT(8 + (x))
#define HSU_CH_DCR_CHSOTO	BIT(14)
#define HSU_CH_DCR_CHSOE	BIT(15)
#define HSU_CH_DCR_CHDI(x)	BIT(16 + (x))
#define HSU_CH_DCR_CHEI		BIT(23)
#define HSU_CH_DCR_CHTOI(x)	BIT(24 + (x))

struct hsu_dma_sg {
	dma_addr_t addr;
	unsigned int len;
};

struct hsu_dma_desc {
	struct virt_dma_desc vdesc;
	enum dma_transfer_direction direction;
	struct hsu_dma_sg *sg;
	unsigned int nents;
	unsigned int active;
	enum dma_status status;
};

static inline struct hsu_dma_desc *to_hsu_dma_desc(struct virt_dma_desc *vdesc)
{
	return container_of(vdesc, struct hsu_dma_desc, vdesc);
}

struct hsu_dma_chan {
	struct virt_dma_chan vchan;

	void __iomem *reg;

	/* hardware configuration */
	enum dma_transfer_direction direction;
	struct dma_slave_config config;

	struct hsu_dma_desc *desc;
};

static inline struct hsu_dma_chan *to_hsu_dma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct hsu_dma_chan, vchan.chan);
}

static inline u32 hsu_chan_readl(struct hsu_dma_chan *hsuc, int offset)
{
	return readl(hsuc->reg + offset);
}

static inline void hsu_chan_writel(struct hsu_dma_chan *hsuc, int offset,
				   u32 value)
{
	writel(value, hsuc->reg + offset);
}

struct hsu_dma {
	struct dma_device		dma;

	/* channels */
	struct hsu_dma_chan		*chan;
	unsigned short			nr_channels;
};

static inline struct hsu_dma *to_hsu_dma(struct dma_device *ddev)
{
	return container_of(ddev, struct hsu_dma, dma);
}

#endif /* __DMA_HSU_H__ */
