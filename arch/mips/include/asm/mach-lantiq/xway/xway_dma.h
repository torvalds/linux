/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 *   Copyright (C) 2011 John Crispin <john@phrozen.org>
 */

#ifndef LTQ_DMA_H__
#define LTQ_DMA_H__

#define LTQ_DESC_SIZE		0x08	/* each descriptor is 64bit */
#define LTQ_DESC_NUM		0x40	/* 64 descriptors / channel */

#define LTQ_DMA_OWN		BIT(31) /* owner bit */
#define LTQ_DMA_C		BIT(30) /* complete bit */
#define LTQ_DMA_SOP		BIT(29) /* start of packet */
#define LTQ_DMA_EOP		BIT(28) /* end of packet */
#define LTQ_DMA_TX_OFFSET(x)	((x & 0x1f) << 23) /* data bytes offset */
#define LTQ_DMA_RX_OFFSET(x)	((x & 0x7) << 23) /* data bytes offset */
#define LTQ_DMA_SIZE_MASK	(0xffff) /* the size field is 16 bit */

struct ltq_dma_desc {
	u32 ctl;
	u32 addr;
};

struct ltq_dma_channel {
	int nr;				/* the channel number */
	int irq;			/* the mapped irq */
	int desc;			/* the current descriptor */
	struct ltq_dma_desc *desc_base; /* the descriptor base */
	int phys;			/* physical addr */
	struct device *dev;
};

enum {
	DMA_PORT_ETOP = 0,
	DMA_PORT_DEU,
};

extern void ltq_dma_enable_irq(struct ltq_dma_channel *ch);
extern void ltq_dma_disable_irq(struct ltq_dma_channel *ch);
extern void ltq_dma_ack_irq(struct ltq_dma_channel *ch);
extern void ltq_dma_open(struct ltq_dma_channel *ch);
extern void ltq_dma_close(struct ltq_dma_channel *ch);
extern void ltq_dma_alloc_tx(struct ltq_dma_channel *ch);
extern void ltq_dma_alloc_rx(struct ltq_dma_channel *ch);
extern void ltq_dma_free(struct ltq_dma_channel *ch);
extern void ltq_dma_init_port(int p);

#endif
