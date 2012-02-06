/*
 * Header file for the Atmel AHB DMA Controller driver
 *
 * Copyright (C) 2008 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef AT_HDMAC_REGS_H
#define	AT_HDMAC_REGS_H

#include <mach/at_hdmac.h>

#define	AT_DMA_MAX_NR_CHANNELS	8


#define	AT_DMA_GCFG	0x00	/* Global Configuration Register */
#define		AT_DMA_IF_BIGEND(i)	(0x1 << (i))	/* AHB-Lite Interface i in Big-endian mode */
#define		AT_DMA_ARB_CFG	(0x1 << 4)	/* Arbiter mode. */
#define			AT_DMA_ARB_CFG_FIXED		(0x0 << 4)
#define			AT_DMA_ARB_CFG_ROUND_ROBIN	(0x1 << 4)

#define	AT_DMA_EN	0x04	/* Controller Enable Register */
#define		AT_DMA_ENABLE	(0x1 << 0)

#define	AT_DMA_SREQ	0x08	/* Software Single Request Register */
#define		AT_DMA_SSREQ(x)	(0x1 << ((x) << 1))		/* Request a source single transfer on channel x */
#define		AT_DMA_DSREQ(x)	(0x1 << (1 + ((x) << 1)))	/* Request a destination single transfer on channel x */

#define	AT_DMA_CREQ	0x0C	/* Software Chunk Transfer Request Register */
#define		AT_DMA_SCREQ(x)	(0x1 << ((x) << 1))		/* Request a source chunk transfer on channel x */
#define		AT_DMA_DCREQ(x)	(0x1 << (1 + ((x) << 1)))	/* Request a destination chunk transfer on channel x */

#define	AT_DMA_LAST	0x10	/* Software Last Transfer Flag Register */
#define		AT_DMA_SLAST(x)	(0x1 << ((x) << 1))		/* This src rq is last tx of buffer on channel x */
#define		AT_DMA_DLAST(x)	(0x1 << (1 + ((x) << 1)))	/* This dst rq is last tx of buffer on channel x */

#define	AT_DMA_SYNC	0x14	/* Request Synchronization Register */
#define		AT_DMA_SYR(h)	(0x1 << (h))			/* Synchronize handshake line h */

/* Error, Chained Buffer transfer completed and Buffer transfer completed Interrupt registers */
#define	AT_DMA_EBCIER	0x18	/* Enable register */
#define	AT_DMA_EBCIDR	0x1C	/* Disable register */
#define	AT_DMA_EBCIMR	0x20	/* Mask Register */
#define	AT_DMA_EBCISR	0x24	/* Status Register */
#define		AT_DMA_CBTC_OFFSET	8
#define		AT_DMA_ERR_OFFSET	16
#define		AT_DMA_BTC(x)	(0x1 << (x))
#define		AT_DMA_CBTC(x)	(0x1 << (AT_DMA_CBTC_OFFSET + (x)))
#define		AT_DMA_ERR(x)	(0x1 << (AT_DMA_ERR_OFFSET + (x)))

#define	AT_DMA_CHER	0x28	/* Channel Handler Enable Register */
#define		AT_DMA_ENA(x)	(0x1 << (x))
#define		AT_DMA_SUSP(x)	(0x1 << ( 8 + (x)))
#define		AT_DMA_KEEP(x)	(0x1 << (24 + (x)))

#define	AT_DMA_CHDR	0x2C	/* Channel Handler Disable Register */
#define		AT_DMA_DIS(x)	(0x1 << (x))
#define		AT_DMA_RES(x)	(0x1 << ( 8 + (x)))

#define	AT_DMA_CHSR	0x30	/* Channel Handler Status Register */
#define		AT_DMA_EMPT(x)	(0x1 << (16 + (x)))
#define		AT_DMA_STAL(x)	(0x1 << (24 + (x)))


#define	AT_DMA_CH_REGS_BASE	0x3C	/* Channel registers base address */
#define	ch_regs(x)	(AT_DMA_CH_REGS_BASE + (x) * 0x28) /* Channel x base addr */

/* Hardware register offset for each channel */
#define	ATC_SADDR_OFFSET	0x00	/* Source Address Register */
#define	ATC_DADDR_OFFSET	0x04	/* Destination Address Register */
#define	ATC_DSCR_OFFSET		0x08	/* Descriptor Address Register */
#define	ATC_CTRLA_OFFSET	0x0C	/* Control A Register */
#define	ATC_CTRLB_OFFSET	0x10	/* Control B Register */
#define	ATC_CFG_OFFSET		0x14	/* Configuration Register */
#define	ATC_SPIP_OFFSET		0x18	/* Src PIP Configuration Register */
#define	ATC_DPIP_OFFSET		0x1C	/* Dst PIP Configuration Register */


/* Bitfield definitions */

/* Bitfields in DSCR */
#define	ATC_DSCR_IF(i)		(0x3 & (i))	/* Dsc feched via AHB-Lite Interface i */

/* Bitfields in CTRLA */
#define	ATC_BTSIZE_MAX		0xFFFFUL	/* Maximum Buffer Transfer Size */
#define	ATC_BTSIZE(x)		(ATC_BTSIZE_MAX & (x)) /* Buffer Transfer Size */
/* Chunck Tranfer size definitions are in at_hdmac.h */
#define	ATC_SRC_WIDTH_MASK	(0x3 << 24)	/* Source Single Transfer Size */
#define		ATC_SRC_WIDTH(x)	((x) << 24)
#define		ATC_SRC_WIDTH_BYTE	(0x0 << 24)
#define		ATC_SRC_WIDTH_HALFWORD	(0x1 << 24)
#define		ATC_SRC_WIDTH_WORD	(0x2 << 24)
#define	ATC_DST_WIDTH_MASK	(0x3 << 28)	/* Destination Single Transfer Size */
#define		ATC_DST_WIDTH(x)	((x) << 28)
#define		ATC_DST_WIDTH_BYTE	(0x0 << 28)
#define		ATC_DST_WIDTH_HALFWORD	(0x1 << 28)
#define		ATC_DST_WIDTH_WORD	(0x2 << 28)
#define	ATC_DONE		(0x1 << 31)	/* Tx Done (only written back in descriptor) */

/* Bitfields in CTRLB */
#define	ATC_SIF(i)		(0x3 & (i))	/* Src tx done via AHB-Lite Interface i */
#define	ATC_DIF(i)		((0x3 & (i)) <<  4)	/* Dst tx done via AHB-Lite Interface i */
				  /* Specify AHB interfaces */
#define AT_DMA_MEM_IF		0 /* interface 0 as memory interface */
#define AT_DMA_PER_IF		1 /* interface 1 as peripheral interface */

#define	ATC_SRC_PIP		(0x1 <<  8)	/* Source Picture-in-Picture enabled */
#define	ATC_DST_PIP		(0x1 << 12)	/* Destination Picture-in-Picture enabled */
#define	ATC_SRC_DSCR_DIS	(0x1 << 16)	/* Src Descriptor fetch disable */
#define	ATC_DST_DSCR_DIS	(0x1 << 20)	/* Dst Descriptor fetch disable */
#define	ATC_FC_MASK		(0x7 << 21)	/* Choose Flow Controller */
#define		ATC_FC_MEM2MEM		(0x0 << 21)	/* Mem-to-Mem (DMA) */
#define		ATC_FC_MEM2PER		(0x1 << 21)	/* Mem-to-Periph (DMA) */
#define		ATC_FC_PER2MEM		(0x2 << 21)	/* Periph-to-Mem (DMA) */
#define		ATC_FC_PER2PER		(0x3 << 21)	/* Periph-to-Periph (DMA) */
#define		ATC_FC_PER2MEM_PER	(0x4 << 21)	/* Periph-to-Mem (Peripheral) */
#define		ATC_FC_MEM2PER_PER	(0x5 << 21)	/* Mem-to-Periph (Peripheral) */
#define		ATC_FC_PER2PER_SRCPER	(0x6 << 21)	/* Periph-to-Periph (Src Peripheral) */
#define		ATC_FC_PER2PER_DSTPER	(0x7 << 21)	/* Periph-to-Periph (Dst Peripheral) */
#define	ATC_SRC_ADDR_MODE_MASK	(0x3 << 24)
#define		ATC_SRC_ADDR_MODE_INCR	(0x0 << 24)	/* Incrementing Mode */
#define		ATC_SRC_ADDR_MODE_DECR	(0x1 << 24)	/* Decrementing Mode */
#define		ATC_SRC_ADDR_MODE_FIXED	(0x2 << 24)	/* Fixed Mode */
#define	ATC_DST_ADDR_MODE_MASK	(0x3 << 28)
#define		ATC_DST_ADDR_MODE_INCR	(0x0 << 28)	/* Incrementing Mode */
#define		ATC_DST_ADDR_MODE_DECR	(0x1 << 28)	/* Decrementing Mode */
#define		ATC_DST_ADDR_MODE_FIXED	(0x2 << 28)	/* Fixed Mode */
#define	ATC_IEN			(0x1 << 30)	/* BTC interrupt enable (active low) */
#define	ATC_AUTO		(0x1 << 31)	/* Auto multiple buffer tx enable */

/* Bitfields in CFG */
/* are in at_hdmac.h */

/* Bitfields in SPIP */
#define	ATC_SPIP_HOLE(x)	(0xFFFFU & (x))
#define	ATC_SPIP_BOUNDARY(x)	((0x3FF & (x)) << 16)

/* Bitfields in DPIP */
#define	ATC_DPIP_HOLE(x)	(0xFFFFU & (x))
#define	ATC_DPIP_BOUNDARY(x)	((0x3FF & (x)) << 16)


/*--  descriptors  -----------------------------------------------------*/

/* LLI == Linked List Item; aka DMA buffer descriptor */
struct at_lli {
	/* values that are not changed by hardware */
	dma_addr_t	saddr;
	dma_addr_t	daddr;
	/* value that may get written back: */
	u32		ctrla;
	/* more values that are not changed by hardware */
	u32		ctrlb;
	dma_addr_t	dscr;	/* chain to next lli */
};

/**
 * struct at_desc - software descriptor
 * @at_lli: hardware lli structure
 * @txd: support for the async_tx api
 * @desc_node: node on the channed descriptors list
 * @len: total transaction bytecount
 */
struct at_desc {
	/* FIRST values the hardware uses */
	struct at_lli			lli;

	/* THEN values for driver housekeeping */
	struct list_head		tx_list;
	struct dma_async_tx_descriptor	txd;
	struct list_head		desc_node;
	size_t				len;
};

static inline struct at_desc *
txd_to_at_desc(struct dma_async_tx_descriptor *txd)
{
	return container_of(txd, struct at_desc, txd);
}


/*--  Channels  --------------------------------------------------------*/

/**
 * atc_status - information bits stored in channel status flag
 *
 * Manipulated with atomic operations.
 */
enum atc_status {
	ATC_IS_ERROR = 0,
	ATC_IS_PAUSED = 1,
	ATC_IS_CYCLIC = 24,
};

/**
 * struct at_dma_chan - internal representation of an Atmel HDMAC channel
 * @chan_common: common dmaengine channel object members
 * @device: parent device
 * @ch_regs: memory mapped register base
 * @mask: channel index in a mask
 * @status: transmit status information from irq/prep* functions
 *                to tasklet (use atomic operations)
 * @tasklet: bottom half to finish transaction work
 * @save_cfg: configuration register that is saved on suspend/resume cycle
 * @save_dscr: for cyclic operations, preserve next descriptor address in
 *             the cyclic list on suspend/resume cycle
 * @lock: serializes enqueue/dequeue operations to descriptors lists
 * @completed_cookie: identifier for the most recently completed operation
 * @active_list: list of descriptors dmaengine is being running on
 * @queue: list of descriptors ready to be submitted to engine
 * @free_list: list of descriptors usable by the channel
 * @descs_allocated: records the actual size of the descriptor pool
 */
struct at_dma_chan {
	struct dma_chan		chan_common;
	struct at_dma		*device;
	void __iomem		*ch_regs;
	u8			mask;
	unsigned long		status;
	struct tasklet_struct	tasklet;
	u32			save_cfg;
	u32			save_dscr;

	spinlock_t		lock;

	/* these other elements are all protected by lock */
	dma_cookie_t		completed_cookie;
	struct list_head	active_list;
	struct list_head	queue;
	struct list_head	free_list;
	unsigned int		descs_allocated;
};

#define	channel_readl(atchan, name) \
	__raw_readl((atchan)->ch_regs + ATC_##name##_OFFSET)

#define	channel_writel(atchan, name, val) \
	__raw_writel((val), (atchan)->ch_regs + ATC_##name##_OFFSET)

static inline struct at_dma_chan *to_at_dma_chan(struct dma_chan *dchan)
{
	return container_of(dchan, struct at_dma_chan, chan_common);
}


/*--  Controller  ------------------------------------------------------*/

/**
 * struct at_dma - internal representation of an Atmel HDMA Controller
 * @chan_common: common dmaengine dma_device object members
 * @atdma_devtype: identifier of DMA controller compatibility
 * @ch_regs: memory mapped register base
 * @clk: dma controller clock
 * @save_imr: interrupt mask register that is saved on suspend/resume cycle
 * @all_chan_mask: all channels availlable in a mask
 * @dma_desc_pool: base of DMA descriptor region (DMA address)
 * @chan: channels table to store at_dma_chan structures
 */
struct at_dma {
	struct dma_device	dma_common;
	void __iomem		*regs;
	struct clk		*clk;
	u32			save_imr;

	u8			all_chan_mask;

	struct dma_pool		*dma_desc_pool;
	/* AT THE END channels table */
	struct at_dma_chan	chan[0];
};

#define	dma_readl(atdma, name) \
	__raw_readl((atdma)->regs + AT_DMA_##name)
#define	dma_writel(atdma, name, val) \
	__raw_writel((val), (atdma)->regs + AT_DMA_##name)

static inline struct at_dma *to_at_dma(struct dma_device *ddev)
{
	return container_of(ddev, struct at_dma, dma_common);
}


/*--  Helper functions  ------------------------------------------------*/

static struct device *chan2dev(struct dma_chan *chan)
{
	return &chan->dev->device;
}
static struct device *chan2parent(struct dma_chan *chan)
{
	return chan->dev->device.parent;
}

#if defined(VERBOSE_DEBUG)
static void vdbg_dump_regs(struct at_dma_chan *atchan)
{
	struct at_dma	*atdma = to_at_dma(atchan->chan_common.device);

	dev_err(chan2dev(&atchan->chan_common),
		"  channel %d : imr = 0x%x, chsr = 0x%x\n",
		atchan->chan_common.chan_id,
		dma_readl(atdma, EBCIMR),
		dma_readl(atdma, CHSR));

	dev_err(chan2dev(&atchan->chan_common),
		"  channel: s0x%x d0x%x ctrl0x%x:0x%x cfg0x%x l0x%x\n",
		channel_readl(atchan, SADDR),
		channel_readl(atchan, DADDR),
		channel_readl(atchan, CTRLA),
		channel_readl(atchan, CTRLB),
		channel_readl(atchan, CFG),
		channel_readl(atchan, DSCR));
}
#else
static void vdbg_dump_regs(struct at_dma_chan *atchan) {}
#endif

static void atc_dump_lli(struct at_dma_chan *atchan, struct at_lli *lli)
{
	dev_printk(KERN_CRIT, chan2dev(&atchan->chan_common),
			"  desc: s0x%x d0x%x ctrl0x%x:0x%x l0x%x\n",
			lli->saddr, lli->daddr,
			lli->ctrla, lli->ctrlb, lli->dscr);
}


static void atc_setup_irq(struct at_dma *atdma, int chan_id, int on)
{
	u32 ebci;

	/* enable interrupts on buffer transfer completion & error */
	ebci =    AT_DMA_BTC(chan_id)
		| AT_DMA_ERR(chan_id);
	if (on)
		dma_writel(atdma, EBCIER, ebci);
	else
		dma_writel(atdma, EBCIDR, ebci);
}

static void atc_enable_chan_irq(struct at_dma *atdma, int chan_id)
{
	atc_setup_irq(atdma, chan_id, 1);
}

static void atc_disable_chan_irq(struct at_dma *atdma, int chan_id)
{
	atc_setup_irq(atdma, chan_id, 0);
}


/**
 * atc_chan_is_enabled - test if given channel is enabled
 * @atchan: channel we want to test status
 */
static inline int atc_chan_is_enabled(struct at_dma_chan *atchan)
{
	struct at_dma	*atdma = to_at_dma(atchan->chan_common.device);

	return !!(dma_readl(atdma, CHSR) & atchan->mask);
}

/**
 * atc_chan_is_paused - test channel pause/resume status
 * @atchan: channel we want to test status
 */
static inline int atc_chan_is_paused(struct at_dma_chan *atchan)
{
	return test_bit(ATC_IS_PAUSED, &atchan->status);
}

/**
 * atc_chan_is_cyclic - test if given channel has cyclic property set
 * @atchan: channel we want to test status
 */
static inline int atc_chan_is_cyclic(struct at_dma_chan *atchan)
{
	return test_bit(ATC_IS_CYCLIC, &atchan->status);
}

/**
 * set_desc_eol - set end-of-link to descriptor so it will end transfer
 * @desc: descriptor, signle or at the end of a chain, to end chain on
 */
static void set_desc_eol(struct at_desc *desc)
{
	u32 ctrlb = desc->lli.ctrlb;

	ctrlb &= ~ATC_IEN;
	ctrlb |= ATC_SRC_DSCR_DIS | ATC_DST_DSCR_DIS;

	desc->lli.ctrlb = ctrlb;
	desc->lli.dscr = 0;
}

#endif /* AT_HDMAC_REGS_H */
