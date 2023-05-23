// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for the Atmel AHB DMA Controller (aka HDMA or DMAC on AT91 systems)
 *
 * Copyright (C) 2008 Atmel Corporation
 * Copyright (C) 2022 Microchip Technology, Inc. and its subsidiaries
 *
 * This supports the Atmel AHB DMA Controller found in several Atmel SoCs.
 * The only Atmel DMA Controller that is not covered by this driver is the one
 * found on AT91SAM9263.
 */

#include <dt-bindings/dma/at91.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dmapool.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/overflow.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "dmaengine.h"
#include "virt-dma.h"

/*
 * Glossary
 * --------
 *
 * at_hdmac		: Name of the ATmel AHB DMA Controller
 * at_dma_ / atdma	: ATmel DMA controller entity related
 * atc_	/ atchan	: ATmel DMA Channel entity related
 */

#define	AT_DMA_MAX_NR_CHANNELS	8

/* Global Configuration Register */
#define AT_DMA_GCFG		0x00
#define AT_DMA_IF_BIGEND(i)	BIT((i))	/* AHB-Lite Interface i in Big-endian mode */
#define AT_DMA_ARB_CFG		BIT(4)		/* Arbiter mode. */

/* Controller Enable Register */
#define AT_DMA_EN		0x04
#define AT_DMA_ENABLE		BIT(0)

/* Software Single Request Register */
#define AT_DMA_SREQ		0x08
#define AT_DMA_SSREQ(x)		BIT((x) << 1)		/* Request a source single transfer on channel x */
#define AT_DMA_DSREQ(x)		BIT(1 + ((x) << 1))	/* Request a destination single transfer on channel x */

/* Software Chunk Transfer Request Register */
#define AT_DMA_CREQ		0x0c
#define AT_DMA_SCREQ(x)		BIT((x) << 1)		/* Request a source chunk transfer on channel x */
#define AT_DMA_DCREQ(x)		BIT(1 + ((x) << 1))	/* Request a destination chunk transfer on channel x */

/* Software Last Transfer Flag Register */
#define AT_DMA_LAST		0x10
#define AT_DMA_SLAST(x)		BIT((x) << 1)		/* This src rq is last tx of buffer on channel x */
#define AT_DMA_DLAST(x)		BIT(1 + ((x) << 1))	/* This dst rq is last tx of buffer on channel x */

/* Request Synchronization Register */
#define AT_DMA_SYNC		0x14
#define AT_DMA_SYR(h)		BIT((h))		/* Synchronize handshake line h */

/* Error, Chained Buffer transfer completed and Buffer transfer completed Interrupt registers */
#define AT_DMA_EBCIER		0x18			/* Enable register */
#define AT_DMA_EBCIDR		0x1c			/* Disable register */
#define AT_DMA_EBCIMR		0x20			/* Mask Register */
#define AT_DMA_EBCISR		0x24			/* Status Register */
#define AT_DMA_CBTC_OFFSET	8
#define AT_DMA_ERR_OFFSET	16
#define AT_DMA_BTC(x)		BIT((x))
#define AT_DMA_CBTC(x)		BIT(AT_DMA_CBTC_OFFSET + (x))
#define AT_DMA_ERR(x)		BIT(AT_DMA_ERR_OFFSET + (x))

/* Channel Handler Enable Register */
#define AT_DMA_CHER		0x28
#define AT_DMA_ENA(x)		BIT((x))
#define AT_DMA_SUSP(x)		BIT(8 + (x))
#define AT_DMA_KEEP(x)		BIT(24 + (x))

/* Channel Handler Disable Register */
#define AT_DMA_CHDR		0x2c
#define AT_DMA_DIS(x)		BIT(x)
#define AT_DMA_RES(x)		BIT(8 + (x))

/* Channel Handler Status Register */
#define AT_DMA_CHSR		0x30
#define AT_DMA_EMPT(x)		BIT(16 + (x))
#define AT_DMA_STAL(x)		BIT(24 + (x))

/* Channel registers base address */
#define AT_DMA_CH_REGS_BASE	0x3c
#define ch_regs(x)		(AT_DMA_CH_REGS_BASE + (x) * 0x28) /* Channel x base addr */

/* Hardware register offset for each channel */
#define ATC_SADDR_OFFSET	0x00	/* Source Address Register */
#define ATC_DADDR_OFFSET	0x04	/* Destination Address Register */
#define ATC_DSCR_OFFSET		0x08	/* Descriptor Address Register */
#define ATC_CTRLA_OFFSET	0x0c	/* Control A Register */
#define ATC_CTRLB_OFFSET	0x10	/* Control B Register */
#define ATC_CFG_OFFSET		0x14	/* Configuration Register */
#define ATC_SPIP_OFFSET		0x18	/* Src PIP Configuration Register */
#define ATC_DPIP_OFFSET		0x1c	/* Dst PIP Configuration Register */


/* Bitfield definitions */

/* Bitfields in DSCR */
#define ATC_DSCR_IF		GENMASK(1, 0)	/* Dsc feched via AHB-Lite Interface */

/* Bitfields in CTRLA */
#define ATC_BTSIZE_MAX		GENMASK(15, 0)	/* Maximum Buffer Transfer Size */
#define ATC_BTSIZE		GENMASK(15, 0)	/* Buffer Transfer Size */
#define ATC_SCSIZE		GENMASK(18, 16)	/* Source Chunk Transfer Size */
#define ATC_DCSIZE		GENMASK(22, 20)	/* Destination Chunk Transfer Size */
#define ATC_SRC_WIDTH		GENMASK(25, 24)	/* Source Single Transfer Size */
#define ATC_DST_WIDTH		GENMASK(29, 28)	/* Destination Single Transfer Size */
#define ATC_DONE		BIT(31)	/* Tx Done (only written back in descriptor) */

/* Bitfields in CTRLB */
#define ATC_SIF			GENMASK(1, 0)	/* Src tx done via AHB-Lite Interface i */
#define ATC_DIF			GENMASK(5, 4)	/* Dst tx done via AHB-Lite Interface i */
#define AT_DMA_MEM_IF		0x0		/* interface 0 as memory interface */
#define AT_DMA_PER_IF		0x1		/* interface 1 as peripheral interface */
#define ATC_SRC_PIP		BIT(8)		/* Source Picture-in-Picture enabled */
#define ATC_DST_PIP		BIT(12)		/* Destination Picture-in-Picture enabled */
#define ATC_SRC_DSCR_DIS	BIT(16)		/* Src Descriptor fetch disable */
#define ATC_DST_DSCR_DIS	BIT(20)		/* Dst Descriptor fetch disable */
#define ATC_FC			GENMASK(23, 21)	/* Choose Flow Controller */
#define ATC_FC_MEM2MEM		0x0		/* Mem-to-Mem (DMA) */
#define ATC_FC_MEM2PER		0x1		/* Mem-to-Periph (DMA) */
#define ATC_FC_PER2MEM		0x2		/* Periph-to-Mem (DMA) */
#define ATC_FC_PER2PER		0x3		/* Periph-to-Periph (DMA) */
#define ATC_FC_PER2MEM_PER	0x4		/* Periph-to-Mem (Peripheral) */
#define ATC_FC_MEM2PER_PER	0x5		/* Mem-to-Periph (Peripheral) */
#define ATC_FC_PER2PER_SRCPER	0x6		/* Periph-to-Periph (Src Peripheral) */
#define ATC_FC_PER2PER_DSTPER	0x7		/* Periph-to-Periph (Dst Peripheral) */
#define ATC_SRC_ADDR_MODE	GENMASK(25, 24)
#define ATC_SRC_ADDR_MODE_INCR	0x0		/* Incrementing Mode */
#define ATC_SRC_ADDR_MODE_DECR	0x1		/* Decrementing Mode */
#define ATC_SRC_ADDR_MODE_FIXED	0x2		/* Fixed Mode */
#define ATC_DST_ADDR_MODE	GENMASK(29, 28)
#define ATC_DST_ADDR_MODE_INCR	0x0		/* Incrementing Mode */
#define ATC_DST_ADDR_MODE_DECR	0x1		/* Decrementing Mode */
#define ATC_DST_ADDR_MODE_FIXED	0x2		/* Fixed Mode */
#define ATC_IEN			BIT(30)		/* BTC interrupt enable (active low) */
#define ATC_AUTO		BIT(31)		/* Auto multiple buffer tx enable */

/* Bitfields in CFG */
#define ATC_SRC_PER		GENMASK(3, 0)	/* Channel src rq associated with periph handshaking ifc h */
#define ATC_DST_PER		GENMASK(7, 4)	/* Channel dst rq associated with periph handshaking ifc h */
#define ATC_SRC_REP		BIT(8)		/* Source Replay Mod */
#define ATC_SRC_H2SEL		BIT(9)		/* Source Handshaking Mod */
#define ATC_SRC_PER_MSB		GENMASK(11, 10)	/* Channel src rq (most significant bits) */
#define ATC_DST_REP		BIT(12)		/* Destination Replay Mod */
#define ATC_DST_H2SEL		BIT(13)		/* Destination Handshaking Mod */
#define ATC_DST_PER_MSB		GENMASK(15, 14)	/* Channel dst rq (most significant bits) */
#define ATC_SOD			BIT(16)		/* Stop On Done */
#define ATC_LOCK_IF		BIT(20)		/* Interface Lock */
#define ATC_LOCK_B		BIT(21)		/* AHB Bus Lock */
#define ATC_LOCK_IF_L		BIT(22)		/* Master Interface Arbiter Lock */
#define ATC_AHB_PROT		GENMASK(26, 24)	/* AHB Protection */
#define ATC_FIFOCFG		GENMASK(29, 28)	/* FIFO Request Configuration */
#define ATC_FIFOCFG_LARGESTBURST	0x0
#define ATC_FIFOCFG_HALFFIFO		0x1
#define ATC_FIFOCFG_ENOUGHSPACE		0x2

/* Bitfields in SPIP */
#define ATC_SPIP_HOLE		GENMASK(15, 0)
#define ATC_SPIP_BOUNDARY	GENMASK(25, 16)

/* Bitfields in DPIP */
#define ATC_DPIP_HOLE		GENMASK(15, 0)
#define ATC_DPIP_BOUNDARY	GENMASK(25, 16)

#define ATC_PER_MSB		GENMASK(5, 4)	/* Extract MSBs of a handshaking identifier */
#define ATC_SRC_PER_ID(id)					       \
	({ typeof(id) _id = (id);				       \
	   FIELD_PREP(ATC_SRC_PER_MSB, FIELD_GET(ATC_PER_MSB, _id)) |  \
	   FIELD_PREP(ATC_SRC_PER, _id); })
#define ATC_DST_PER_ID(id)					       \
	({ typeof(id) _id = (id);				       \
	   FIELD_PREP(ATC_DST_PER_MSB, FIELD_GET(ATC_PER_MSB, _id)) |  \
	   FIELD_PREP(ATC_DST_PER, _id); })



/*--  descriptors  -----------------------------------------------------*/

/* LLI == Linked List Item; aka DMA buffer descriptor */
struct at_lli {
	/* values that are not changed by hardware */
	u32 saddr;
	u32 daddr;
	/* value that may get written back: */
	u32 ctrla;
	/* more values that are not changed by hardware */
	u32 ctrlb;
	u32 dscr;	/* chain to next lli */
};

/**
 * struct atdma_sg - atdma scatter gather entry
 * @len: length of the current Linked List Item.
 * @lli: linked list item that is passed to the DMA controller
 * @lli_phys: physical address of the LLI.
 */
struct atdma_sg {
	unsigned int len;
	struct at_lli *lli;
	dma_addr_t lli_phys;
};

/**
 * struct at_desc - software descriptor
 * @vd: pointer to the virtual dma descriptor.
 * @atchan: pointer to the atmel dma channel.
 * @total_len: total transaction byte count
 * @sg_len: number of sg entries.
 * @sg: array of sgs.
 */
struct at_desc {
	struct				virt_dma_desc vd;
	struct				at_dma_chan *atchan;
	size_t				total_len;
	unsigned int			sglen;
	/* Interleaved data */
	size_t				boundary;
	size_t				dst_hole;
	size_t				src_hole;

	/* Memset temporary buffer */
	bool				memset_buffer;
	dma_addr_t			memset_paddr;
	int				*memset_vaddr;
	struct atdma_sg			sg[];
};

/*--  Channels  --------------------------------------------------------*/

/**
 * atc_status - information bits stored in channel status flag
 *
 * Manipulated with atomic operations.
 */
enum atc_status {
	ATC_IS_PAUSED = 1,
	ATC_IS_CYCLIC = 24,
};

/**
 * struct at_dma_chan - internal representation of an Atmel HDMAC channel
 * @vc: virtual dma channel entry.
 * @atdma: pointer to the driver data.
 * @ch_regs: memory mapped register base
 * @mask: channel index in a mask
 * @per_if: peripheral interface
 * @mem_if: memory interface
 * @status: transmit status information from irq/prep* functions
 *                to tasklet (use atomic operations)
 * @save_cfg: configuration register that is saved on suspend/resume cycle
 * @save_dscr: for cyclic operations, preserve next descriptor address in
 *             the cyclic list on suspend/resume cycle
 * @dma_sconfig: configuration for slave transfers, passed via
 * .device_config
 * @desc: pointer to the atmel dma descriptor.
 */
struct at_dma_chan {
	struct virt_dma_chan	vc;
	struct at_dma		*atdma;
	void __iomem		*ch_regs;
	u8			mask;
	u8			per_if;
	u8			mem_if;
	unsigned long		status;
	u32			save_cfg;
	u32			save_dscr;
	struct dma_slave_config	dma_sconfig;
	bool			cyclic;
	struct at_desc		*desc;
};

#define	channel_readl(atchan, name) \
	__raw_readl((atchan)->ch_regs + ATC_##name##_OFFSET)

#define	channel_writel(atchan, name, val) \
	__raw_writel((val), (atchan)->ch_regs + ATC_##name##_OFFSET)

/*
 * Fix sconfig's burst size according to at_hdmac. We need to convert them as:
 * 1 -> 0, 4 -> 1, 8 -> 2, 16 -> 3, 32 -> 4, 64 -> 5, 128 -> 6, 256 -> 7.
 *
 * This can be done by finding most significant bit set.
 */
static inline void convert_burst(u32 *maxburst)
{
	if (*maxburst > 1)
		*maxburst = fls(*maxburst) - 2;
	else
		*maxburst = 0;
}

/*
 * Fix sconfig's bus width according to at_hdmac.
 * 1 byte -> 0, 2 bytes -> 1, 4 bytes -> 2.
 */
static inline u8 convert_buswidth(enum dma_slave_buswidth addr_width)
{
	switch (addr_width) {
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
		return 1;
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
		return 2;
	default:
		/* For 1 byte width or fallback */
		return 0;
	}
}

/*--  Controller  ------------------------------------------------------*/

/**
 * struct at_dma - internal representation of an Atmel HDMA Controller
 * @dma_device: dmaengine dma_device object members
 * @atdma_devtype: identifier of DMA controller compatibility
 * @ch_regs: memory mapped register base
 * @clk: dma controller clock
 * @save_imr: interrupt mask register that is saved on suspend/resume cycle
 * @all_chan_mask: all channels availlable in a mask
 * @lli_pool: hw lli table
 * @chan: channels table to store at_dma_chan structures
 */
struct at_dma {
	struct dma_device	dma_device;
	void __iomem		*regs;
	struct clk		*clk;
	u32			save_imr;

	u8			all_chan_mask;

	struct dma_pool		*lli_pool;
	struct dma_pool		*memset_pool;
	/* AT THE END channels table */
	struct at_dma_chan	chan[];
};

#define	dma_readl(atdma, name) \
	__raw_readl((atdma)->regs + AT_DMA_##name)
#define	dma_writel(atdma, name, val) \
	__raw_writel((val), (atdma)->regs + AT_DMA_##name)

static inline struct at_desc *to_atdma_desc(struct dma_async_tx_descriptor *t)
{
	return container_of(t, struct at_desc, vd.tx);
}

static inline struct at_dma_chan *to_at_dma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct at_dma_chan, vc.chan);
}

static inline struct at_dma *to_at_dma(struct dma_device *ddev)
{
	return container_of(ddev, struct at_dma, dma_device);
}


/*--  Helper functions  ------------------------------------------------*/

static struct device *chan2dev(struct dma_chan *chan)
{
	return &chan->dev->device;
}

#if defined(VERBOSE_DEBUG)
static void vdbg_dump_regs(struct at_dma_chan *atchan)
{
	struct at_dma	*atdma = to_at_dma(atchan->vc.chan.device);

	dev_err(chan2dev(&atchan->vc.chan),
		"  channel %d : imr = 0x%x, chsr = 0x%x\n",
		atchan->vc.chan.chan_id,
		dma_readl(atdma, EBCIMR),
		dma_readl(atdma, CHSR));

	dev_err(chan2dev(&atchan->vc.chan),
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
	dev_crit(chan2dev(&atchan->vc.chan),
		 "desc: s%pad d%pad ctrl0x%x:0x%x l%pad\n",
		 &lli->saddr, &lli->daddr,
		 lli->ctrla, lli->ctrlb, &lli->dscr);
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
	struct at_dma *atdma = to_at_dma(atchan->vc.chan.device);

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
 * set_lli_eol - set end-of-link to descriptor so it will end transfer
 * @desc: descriptor, signle or at the end of a chain, to end chain on
 * @i: index of the atmel scatter gather entry that is at the end of the chain.
 */
static void set_lli_eol(struct at_desc *desc, unsigned int i)
{
	u32 ctrlb = desc->sg[i].lli->ctrlb;

	ctrlb &= ~ATC_IEN;
	ctrlb |= ATC_SRC_DSCR_DIS | ATC_DST_DSCR_DIS;

	desc->sg[i].lli->ctrlb = ctrlb;
	desc->sg[i].lli->dscr = 0;
}

#define	ATC_DEFAULT_CFG		FIELD_PREP(ATC_FIFOCFG, ATC_FIFOCFG_HALFFIFO)
#define	ATC_DEFAULT_CTRLB	(FIELD_PREP(ATC_SIF, AT_DMA_MEM_IF) | \
				 FIELD_PREP(ATC_DIF, AT_DMA_MEM_IF))
#define ATC_DMA_BUSWIDTHS\
	(BIT(DMA_SLAVE_BUSWIDTH_UNDEFINED) |\
	BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |\
	BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |\
	BIT(DMA_SLAVE_BUSWIDTH_4_BYTES))

#define ATC_MAX_DSCR_TRIALS	10

/*
 * Initial number of descriptors to allocate for each channel. This could
 * be increased during dma usage.
 */
static unsigned int init_nr_desc_per_channel = 64;
module_param(init_nr_desc_per_channel, uint, 0644);
MODULE_PARM_DESC(init_nr_desc_per_channel,
		 "initial descriptors per channel (default: 64)");

/**
 * struct at_dma_platform_data - Controller configuration parameters
 * @nr_channels: Number of channels supported by hardware (max 8)
 * @cap_mask: dma_capability flags supported by the platform
 */
struct at_dma_platform_data {
	unsigned int	nr_channels;
	dma_cap_mask_t  cap_mask;
};

/**
 * struct at_dma_slave - Controller-specific information about a slave
 * @dma_dev: required DMA master device
 * @cfg: Platform-specific initializer for the CFG register
 */
struct at_dma_slave {
	struct device		*dma_dev;
	u32			cfg;
};

static inline unsigned int atc_get_xfer_width(dma_addr_t src, dma_addr_t dst,
						size_t len)
{
	unsigned int width;

	if (!((src | dst  | len) & 3))
		width = 2;
	else if (!((src | dst | len) & 1))
		width = 1;
	else
		width = 0;

	return width;
}

static void atdma_lli_chain(struct at_desc *desc, unsigned int i)
{
	struct atdma_sg *atdma_sg = &desc->sg[i];

	if (i)
		desc->sg[i - 1].lli->dscr = atdma_sg->lli_phys;
}

/**
 * atc_dostart - starts the DMA engine for real
 * @atchan: the channel we want to start
 */
static void atc_dostart(struct at_dma_chan *atchan)
{
	struct virt_dma_desc *vd = vchan_next_desc(&atchan->vc);
	struct at_desc *desc;

	if (!vd) {
		atchan->desc = NULL;
		return;
	}

	vdbg_dump_regs(atchan);

	list_del(&vd->node);
	atchan->desc = desc = to_atdma_desc(&vd->tx);

	channel_writel(atchan, SADDR, 0);
	channel_writel(atchan, DADDR, 0);
	channel_writel(atchan, CTRLA, 0);
	channel_writel(atchan, CTRLB, 0);
	channel_writel(atchan, DSCR, desc->sg[0].lli_phys);
	channel_writel(atchan, SPIP,
		       FIELD_PREP(ATC_SPIP_HOLE, desc->src_hole) |
		       FIELD_PREP(ATC_SPIP_BOUNDARY, desc->boundary));
	channel_writel(atchan, DPIP,
		       FIELD_PREP(ATC_DPIP_HOLE, desc->dst_hole) |
		       FIELD_PREP(ATC_DPIP_BOUNDARY, desc->boundary));

	/* Don't allow CPU to reorder channel enable. */
	wmb();
	dma_writel(atchan->atdma, CHER, atchan->mask);

	vdbg_dump_regs(atchan);
}

static void atdma_desc_free(struct virt_dma_desc *vd)
{
	struct at_dma *atdma = to_at_dma(vd->tx.chan->device);
	struct at_desc *desc = to_atdma_desc(&vd->tx);
	unsigned int i;

	for (i = 0; i < desc->sglen; i++) {
		if (desc->sg[i].lli)
			dma_pool_free(atdma->lli_pool, desc->sg[i].lli,
				      desc->sg[i].lli_phys);
	}

	/* If the transfer was a memset, free our temporary buffer */
	if (desc->memset_buffer) {
		dma_pool_free(atdma->memset_pool, desc->memset_vaddr,
			      desc->memset_paddr);
		desc->memset_buffer = false;
	}

	kfree(desc);
}

/**
 * atc_calc_bytes_left - calculates the number of bytes left according to the
 * value read from CTRLA.
 *
 * @current_len: the number of bytes left before reading CTRLA
 * @ctrla: the value of CTRLA
 */
static inline u32 atc_calc_bytes_left(u32 current_len, u32 ctrla)
{
	u32 btsize = FIELD_GET(ATC_BTSIZE, ctrla);
	u32 src_width = FIELD_GET(ATC_SRC_WIDTH, ctrla);

	/*
	 * According to the datasheet, when reading the Control A Register
	 * (ctrla), the Buffer Transfer Size (btsize) bitfield refers to the
	 * number of transfers completed on the Source Interface.
	 * So btsize is always a number of source width transfers.
	 */
	return current_len - (btsize << src_width);
}

/**
 * atc_get_llis_residue - Get residue for a hardware linked list transfer
 *
 * Calculate the residue by removing the length of the Linked List Item (LLI)
 * already transferred from the total length. To get the current LLI we can use
 * the value of the channel's DSCR register and compare it against the DSCR
 * value of each LLI.
 *
 * The CTRLA register provides us with the amount of data already read from the
 * source for the LLI. So we can compute a more accurate residue by also
 * removing the number of bytes corresponding to this amount of data.
 *
 * However, the DSCR and CTRLA registers cannot be read both atomically. Hence a
 * race condition may occur: the first read register may refer to one LLI
 * whereas the second read may refer to a later LLI in the list because of the
 * DMA transfer progression inbetween the two reads.
 *
 * One solution could have been to pause the DMA transfer, read the DSCR and
 * CTRLA then resume the DMA transfer. Nonetheless, this approach presents some
 * drawbacks:
 * - If the DMA transfer is paused, RX overruns or TX underruns are more likey
 *   to occur depending on the system latency. Taking the USART driver as an
 *   example, it uses a cyclic DMA transfer to read data from the Receive
 *   Holding Register (RHR) to avoid RX overruns since the RHR is not protected
 *   by any FIFO on most Atmel SoCs. So pausing the DMA transfer to compute the
 *   residue would break the USART driver design.
 * - The atc_pause() function masks interrupts but we'd rather avoid to do so
 * for system latency purpose.
 *
 * Then we'd rather use another solution: the DSCR is read a first time, the
 * CTRLA is read in turn, next the DSCR is read a second time. If the two
 * consecutive read values of the DSCR are the same then we assume both refers
 * to the very same LLI as well as the CTRLA value read inbetween does. For
 * cyclic tranfers, the assumption is that a full loop is "not so fast". If the
 * two DSCR values are different, we read again the CTRLA then the DSCR till two
 * consecutive read values from DSCR are equal or till the maximum trials is
 * reach. This algorithm is very unlikely not to find a stable value for DSCR.
 * @atchan: pointer to an atmel hdmac channel.
 * @desc: pointer to the descriptor for which the residue is calculated.
 * @residue: residue to be set to dma_tx_state.
 * Returns 0 on success, -errno otherwise.
 */
static int atc_get_llis_residue(struct at_dma_chan *atchan,
				struct at_desc *desc, u32 *residue)
{
	u32 len, ctrla, dscr;
	unsigned int i;

	len = desc->total_len;
	dscr = channel_readl(atchan, DSCR);
	rmb(); /* ensure DSCR is read before CTRLA */
	ctrla = channel_readl(atchan, CTRLA);
	for (i = 0; i < ATC_MAX_DSCR_TRIALS; ++i) {
		u32 new_dscr;

		rmb(); /* ensure DSCR is read after CTRLA */
		new_dscr = channel_readl(atchan, DSCR);

		/*
		 * If the DSCR register value has not changed inside the DMA
		 * controller since the previous read, we assume that both the
		 * dscr and ctrla values refers to the very same descriptor.
		 */
		if (likely(new_dscr == dscr))
			break;

		/*
		 * DSCR has changed inside the DMA controller, so the previouly
		 * read value of CTRLA may refer to an already processed
		 * descriptor hence could be outdated. We need to update ctrla
		 * to match the current descriptor.
		 */
		dscr = new_dscr;
		rmb(); /* ensure DSCR is read before CTRLA */
		ctrla = channel_readl(atchan, CTRLA);
	}
	if (unlikely(i == ATC_MAX_DSCR_TRIALS))
		return -ETIMEDOUT;

	/* For the first descriptor we can be more accurate. */
	if (desc->sg[0].lli->dscr == dscr) {
		*residue = atc_calc_bytes_left(len, ctrla);
		return 0;
	}
	len -= desc->sg[0].len;

	for (i = 1; i < desc->sglen; i++) {
		if (desc->sg[i].lli && desc->sg[i].lli->dscr == dscr)
			break;
		len -= desc->sg[i].len;
	}

	/*
	 * For the current LLI in the chain we can calculate the remaining bytes
	 * using the channel's CTRLA register.
	 */
	*residue = atc_calc_bytes_left(len, ctrla);
	return 0;

}

/**
 * atc_get_residue - get the number of bytes residue for a cookie.
 * The residue is passed by address and updated on success.
 * @chan: DMA channel
 * @cookie: transaction identifier to check status of
 * @residue: residue to be updated.
 * Return 0 on success, -errono otherwise.
 */
static int atc_get_residue(struct dma_chan *chan, dma_cookie_t cookie,
			   u32 *residue)
{
	struct at_dma_chan *atchan = to_at_dma_chan(chan);
	struct virt_dma_desc *vd;
	struct at_desc *desc = NULL;
	u32 len, ctrla;

	vd = vchan_find_desc(&atchan->vc, cookie);
	if (vd)
		desc = to_atdma_desc(&vd->tx);
	else if (atchan->desc && atchan->desc->vd.tx.cookie == cookie)
		desc = atchan->desc;

	if (!desc)
		return -EINVAL;

	if (desc->sg[0].lli->dscr)
		/* hardware linked list transfer */
		return atc_get_llis_residue(atchan, desc, residue);

	/* single transfer */
	len = desc->total_len;
	ctrla = channel_readl(atchan, CTRLA);
	*residue = atc_calc_bytes_left(len, ctrla);
	return 0;
}

/**
 * atc_handle_error - handle errors reported by DMA controller
 * @atchan: channel where error occurs.
 * @i: channel index
 */
static void atc_handle_error(struct at_dma_chan *atchan, unsigned int i)
{
	struct at_desc *desc = atchan->desc;

	/* Disable channel on AHB error */
	dma_writel(atchan->atdma, CHDR, AT_DMA_RES(i) | atchan->mask);

	/*
	 * KERN_CRITICAL may seem harsh, but since this only happens
	 * when someone submits a bad physical address in a
	 * descriptor, we should consider ourselves lucky that the
	 * controller flagged an error instead of scribbling over
	 * random memory locations.
	 */
	dev_crit(chan2dev(&atchan->vc.chan), "Bad descriptor submitted for DMA!\n");
	dev_crit(chan2dev(&atchan->vc.chan), "cookie: %d\n",
		 desc->vd.tx.cookie);
	for (i = 0; i < desc->sglen; i++)
		atc_dump_lli(atchan, desc->sg[i].lli);
}

static void atdma_handle_chan_done(struct at_dma_chan *atchan, u32 pending,
				   unsigned int i)
{
	struct at_desc *desc;

	spin_lock(&atchan->vc.lock);
	desc = atchan->desc;

	if (desc) {
		if (pending & AT_DMA_ERR(i)) {
			atc_handle_error(atchan, i);
			/* Pretend the descriptor completed successfully */
		}

		if (atc_chan_is_cyclic(atchan)) {
			vchan_cyclic_callback(&desc->vd);
		} else {
			vchan_cookie_complete(&desc->vd);
			atchan->desc = NULL;
			if (!(atc_chan_is_enabled(atchan)))
				atc_dostart(atchan);
		}
	}
	spin_unlock(&atchan->vc.lock);
}

static irqreturn_t at_dma_interrupt(int irq, void *dev_id)
{
	struct at_dma		*atdma = dev_id;
	struct at_dma_chan	*atchan;
	int			i;
	u32			status, pending, imr;
	int			ret = IRQ_NONE;

	do {
		imr = dma_readl(atdma, EBCIMR);
		status = dma_readl(atdma, EBCISR);
		pending = status & imr;

		if (!pending)
			break;

		dev_vdbg(atdma->dma_device.dev,
			"interrupt: status = 0x%08x, 0x%08x, 0x%08x\n",
			 status, imr, pending);

		for (i = 0; i < atdma->dma_device.chancnt; i++) {
			atchan = &atdma->chan[i];
			if (!(pending & (AT_DMA_BTC(i) | AT_DMA_ERR(i))))
				continue;
			atdma_handle_chan_done(atchan, pending, i);
			ret = IRQ_HANDLED;
		}

	} while (pending);

	return ret;
}

/*--  DMA Engine API  --------------------------------------------------*/
/**
 * atc_prep_dma_interleaved - prepare memory to memory interleaved operation
 * @chan: the channel to prepare operation on
 * @xt: Interleaved transfer template
 * @flags: tx descriptor status flags
 */
static struct dma_async_tx_descriptor *
atc_prep_dma_interleaved(struct dma_chan *chan,
			 struct dma_interleaved_template *xt,
			 unsigned long flags)
{
	struct at_dma		*atdma = to_at_dma(chan->device);
	struct at_dma_chan	*atchan = to_at_dma_chan(chan);
	struct data_chunk	*first;
	struct atdma_sg		*atdma_sg;
	struct at_desc		*desc;
	struct at_lli		*lli;
	size_t			xfer_count;
	unsigned int		dwidth;
	u32			ctrla;
	u32			ctrlb;
	size_t			len = 0;
	int			i;

	if (unlikely(!xt || xt->numf != 1 || !xt->frame_size))
		return NULL;

	first = xt->sgl;

	dev_info(chan2dev(chan),
		 "%s: src=%pad, dest=%pad, numf=%d, frame_size=%d, flags=0x%lx\n",
		__func__, &xt->src_start, &xt->dst_start, xt->numf,
		xt->frame_size, flags);

	/*
	 * The controller can only "skip" X bytes every Y bytes, so we
	 * need to make sure we are given a template that fit that
	 * description, ie a template with chunks that always have the
	 * same size, with the same ICGs.
	 */
	for (i = 0; i < xt->frame_size; i++) {
		struct data_chunk *chunk = xt->sgl + i;

		if ((chunk->size != xt->sgl->size) ||
		    (dmaengine_get_dst_icg(xt, chunk) != dmaengine_get_dst_icg(xt, first)) ||
		    (dmaengine_get_src_icg(xt, chunk) != dmaengine_get_src_icg(xt, first))) {
			dev_err(chan2dev(chan),
				"%s: the controller can transfer only identical chunks\n",
				__func__);
			return NULL;
		}

		len += chunk->size;
	}

	dwidth = atc_get_xfer_width(xt->src_start, xt->dst_start, len);

	xfer_count = len >> dwidth;
	if (xfer_count > ATC_BTSIZE_MAX) {
		dev_err(chan2dev(chan), "%s: buffer is too big\n", __func__);
		return NULL;
	}

	ctrla = FIELD_PREP(ATC_SRC_WIDTH, dwidth) |
		FIELD_PREP(ATC_DST_WIDTH, dwidth);

	ctrlb = ATC_DEFAULT_CTRLB | ATC_IEN |
		FIELD_PREP(ATC_SRC_ADDR_MODE, ATC_SRC_ADDR_MODE_INCR) |
		FIELD_PREP(ATC_DST_ADDR_MODE, ATC_DST_ADDR_MODE_INCR) |
		ATC_SRC_PIP | ATC_DST_PIP |
		FIELD_PREP(ATC_FC, ATC_FC_MEM2MEM);

	desc = kzalloc(struct_size(desc, sg, 1), GFP_ATOMIC);
	if (!desc)
		return NULL;
	desc->sglen = 1;

	atdma_sg = desc->sg;
	atdma_sg->lli = dma_pool_alloc(atdma->lli_pool, GFP_NOWAIT,
				       &atdma_sg->lli_phys);
	if (!atdma_sg->lli) {
		kfree(desc);
		return NULL;
	}
	lli = atdma_sg->lli;

	lli->saddr = xt->src_start;
	lli->daddr = xt->dst_start;
	lli->ctrla = ctrla | xfer_count;
	lli->ctrlb = ctrlb;

	desc->boundary = first->size >> dwidth;
	desc->dst_hole = (dmaengine_get_dst_icg(xt, first) >> dwidth) + 1;
	desc->src_hole = (dmaengine_get_src_icg(xt, first) >> dwidth) + 1;

	atdma_sg->len = len;
	desc->total_len = len;

	set_lli_eol(desc, 0);
	return vchan_tx_prep(&atchan->vc, &desc->vd, flags);
}

/**
 * atc_prep_dma_memcpy - prepare a memcpy operation
 * @chan: the channel to prepare operation on
 * @dest: operation virtual destination address
 * @src: operation virtual source address
 * @len: operation length
 * @flags: tx descriptor status flags
 */
static struct dma_async_tx_descriptor *
atc_prep_dma_memcpy(struct dma_chan *chan, dma_addr_t dest, dma_addr_t src,
		size_t len, unsigned long flags)
{
	struct at_dma		*atdma = to_at_dma(chan->device);
	struct at_dma_chan	*atchan = to_at_dma_chan(chan);
	struct at_desc		*desc = NULL;
	size_t			xfer_count;
	size_t			offset;
	size_t			sg_len;
	unsigned int		src_width;
	unsigned int		dst_width;
	unsigned int		i;
	u32			ctrla;
	u32			ctrlb;

	dev_dbg(chan2dev(chan), "prep_dma_memcpy: d%pad s%pad l0x%zx f0x%lx\n",
		&dest, &src, len, flags);

	if (unlikely(!len)) {
		dev_err(chan2dev(chan), "prep_dma_memcpy: length is zero!\n");
		return NULL;
	}

	sg_len = DIV_ROUND_UP(len, ATC_BTSIZE_MAX);
	desc = kzalloc(struct_size(desc, sg, sg_len), GFP_ATOMIC);
	if (!desc)
		return NULL;
	desc->sglen = sg_len;

	ctrlb = ATC_DEFAULT_CTRLB | ATC_IEN |
		FIELD_PREP(ATC_SRC_ADDR_MODE, ATC_SRC_ADDR_MODE_INCR) |
		FIELD_PREP(ATC_DST_ADDR_MODE, ATC_DST_ADDR_MODE_INCR) |
		FIELD_PREP(ATC_FC, ATC_FC_MEM2MEM);

	/*
	 * We can be a lot more clever here, but this should take care
	 * of the most common optimization.
	 */
	src_width = dst_width = atc_get_xfer_width(src, dest, len);

	ctrla = FIELD_PREP(ATC_SRC_WIDTH, src_width) |
		FIELD_PREP(ATC_DST_WIDTH, dst_width);

	for (offset = 0, i = 0; offset < len;
	     offset += xfer_count << src_width, i++) {
		struct atdma_sg *atdma_sg = &desc->sg[i];
		struct at_lli *lli;

		atdma_sg->lli = dma_pool_alloc(atdma->lli_pool, GFP_NOWAIT,
					       &atdma_sg->lli_phys);
		if (!atdma_sg->lli)
			goto err_desc_get;
		lli = atdma_sg->lli;

		xfer_count = min_t(size_t, (len - offset) >> src_width,
				   ATC_BTSIZE_MAX);

		lli->saddr = src + offset;
		lli->daddr = dest + offset;
		lli->ctrla = ctrla | xfer_count;
		lli->ctrlb = ctrlb;

		desc->sg[i].len = xfer_count << src_width;

		atdma_lli_chain(desc, i);
	}

	desc->total_len = len;

	/* set end-of-link to the last link descriptor of list*/
	set_lli_eol(desc, i - 1);

	return vchan_tx_prep(&atchan->vc, &desc->vd, flags);

err_desc_get:
	atdma_desc_free(&desc->vd);
	return NULL;
}

static int atdma_create_memset_lli(struct dma_chan *chan,
				   struct atdma_sg *atdma_sg,
				   dma_addr_t psrc, dma_addr_t pdst, size_t len)
{
	struct at_dma *atdma = to_at_dma(chan->device);
	struct at_lli *lli;
	size_t xfer_count;
	u32 ctrla = FIELD_PREP(ATC_SRC_WIDTH, 2) | FIELD_PREP(ATC_DST_WIDTH, 2);
	u32 ctrlb = ATC_DEFAULT_CTRLB | ATC_IEN |
		    FIELD_PREP(ATC_SRC_ADDR_MODE, ATC_SRC_ADDR_MODE_FIXED) |
		    FIELD_PREP(ATC_DST_ADDR_MODE, ATC_DST_ADDR_MODE_INCR) |
		    FIELD_PREP(ATC_FC, ATC_FC_MEM2MEM);

	xfer_count = len >> 2;
	if (xfer_count > ATC_BTSIZE_MAX) {
		dev_err(chan2dev(chan), "%s: buffer is too big\n", __func__);
		return -EINVAL;
	}

	atdma_sg->lli = dma_pool_alloc(atdma->lli_pool, GFP_NOWAIT,
				       &atdma_sg->lli_phys);
	if (!atdma_sg->lli)
		return -ENOMEM;
	lli = atdma_sg->lli;

	lli->saddr = psrc;
	lli->daddr = pdst;
	lli->ctrla = ctrla | xfer_count;
	lli->ctrlb = ctrlb;

	atdma_sg->len = len;

	return 0;
}

/**
 * atc_prep_dma_memset - prepare a memcpy operation
 * @chan: the channel to prepare operation on
 * @dest: operation virtual destination address
 * @value: value to set memory buffer to
 * @len: operation length
 * @flags: tx descriptor status flags
 */
static struct dma_async_tx_descriptor *
atc_prep_dma_memset(struct dma_chan *chan, dma_addr_t dest, int value,
		    size_t len, unsigned long flags)
{
	struct at_dma_chan	*atchan = to_at_dma_chan(chan);
	struct at_dma		*atdma = to_at_dma(chan->device);
	struct at_desc		*desc;
	void __iomem		*vaddr;
	dma_addr_t		paddr;
	char			fill_pattern;
	int			ret;

	dev_vdbg(chan2dev(chan), "%s: d%pad v0x%x l0x%zx f0x%lx\n", __func__,
		&dest, value, len, flags);

	if (unlikely(!len)) {
		dev_dbg(chan2dev(chan), "%s: length is zero!\n", __func__);
		return NULL;
	}

	if (!is_dma_fill_aligned(chan->device, dest, 0, len)) {
		dev_dbg(chan2dev(chan), "%s: buffer is not aligned\n",
			__func__);
		return NULL;
	}

	vaddr = dma_pool_alloc(atdma->memset_pool, GFP_NOWAIT, &paddr);
	if (!vaddr) {
		dev_err(chan2dev(chan), "%s: couldn't allocate buffer\n",
			__func__);
		return NULL;
	}

	/* Only the first byte of value is to be used according to dmaengine */
	fill_pattern = (char)value;

	*(u32*)vaddr = (fill_pattern << 24) |
		       (fill_pattern << 16) |
		       (fill_pattern << 8) |
		       fill_pattern;

	desc = kzalloc(struct_size(desc, sg, 1), GFP_ATOMIC);
	if (!desc)
		goto err_free_buffer;
	desc->sglen = 1;

	ret = atdma_create_memset_lli(chan, desc->sg, paddr, dest, len);
	if (ret)
		goto err_free_desc;

	desc->memset_paddr = paddr;
	desc->memset_vaddr = vaddr;
	desc->memset_buffer = true;

	desc->total_len = len;

	/* set end-of-link on the descriptor */
	set_lli_eol(desc, 0);

	return vchan_tx_prep(&atchan->vc, &desc->vd, flags);

err_free_desc:
	kfree(desc);
err_free_buffer:
	dma_pool_free(atdma->memset_pool, vaddr, paddr);
	return NULL;
}

static struct dma_async_tx_descriptor *
atc_prep_dma_memset_sg(struct dma_chan *chan,
		       struct scatterlist *sgl,
		       unsigned int sg_len, int value,
		       unsigned long flags)
{
	struct at_dma_chan	*atchan = to_at_dma_chan(chan);
	struct at_dma		*atdma = to_at_dma(chan->device);
	struct at_desc		*desc;
	struct scatterlist	*sg;
	void __iomem		*vaddr;
	dma_addr_t		paddr;
	size_t			total_len = 0;
	int			i;
	int			ret;

	dev_vdbg(chan2dev(chan), "%s: v0x%x l0x%zx f0x%lx\n", __func__,
		 value, sg_len, flags);

	if (unlikely(!sgl || !sg_len)) {
		dev_dbg(chan2dev(chan), "%s: scatterlist is empty!\n",
			__func__);
		return NULL;
	}

	vaddr = dma_pool_alloc(atdma->memset_pool, GFP_NOWAIT, &paddr);
	if (!vaddr) {
		dev_err(chan2dev(chan), "%s: couldn't allocate buffer\n",
			__func__);
		return NULL;
	}
	*(u32*)vaddr = value;

	desc = kzalloc(struct_size(desc, sg, sg_len), GFP_ATOMIC);
	if (!desc)
		goto err_free_dma_buf;
	desc->sglen = sg_len;

	for_each_sg(sgl, sg, sg_len, i) {
		dma_addr_t dest = sg_dma_address(sg);
		size_t len = sg_dma_len(sg);

		dev_vdbg(chan2dev(chan), "%s: d%pad, l0x%zx\n",
			 __func__, &dest, len);

		if (!is_dma_fill_aligned(chan->device, dest, 0, len)) {
			dev_err(chan2dev(chan), "%s: buffer is not aligned\n",
				__func__);
			goto err_free_desc;
		}

		ret = atdma_create_memset_lli(chan, &desc->sg[i], paddr, dest,
					      len);
		if (ret)
			goto err_free_desc;

		atdma_lli_chain(desc, i);
		total_len += len;
	}

	desc->memset_paddr = paddr;
	desc->memset_vaddr = vaddr;
	desc->memset_buffer = true;

	desc->total_len = total_len;

	/* set end-of-link on the descriptor */
	set_lli_eol(desc, i - 1);

	return vchan_tx_prep(&atchan->vc, &desc->vd, flags);

err_free_desc:
	atdma_desc_free(&desc->vd);
err_free_dma_buf:
	dma_pool_free(atdma->memset_pool, vaddr, paddr);
	return NULL;
}

/**
 * atc_prep_slave_sg - prepare descriptors for a DMA_SLAVE transaction
 * @chan: DMA channel
 * @sgl: scatterlist to transfer to/from
 * @sg_len: number of entries in @scatterlist
 * @direction: DMA direction
 * @flags: tx descriptor status flags
 * @context: transaction context (ignored)
 */
static struct dma_async_tx_descriptor *
atc_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context)
{
	struct at_dma		*atdma = to_at_dma(chan->device);
	struct at_dma_chan	*atchan = to_at_dma_chan(chan);
	struct at_dma_slave	*atslave = chan->private;
	struct dma_slave_config	*sconfig = &atchan->dma_sconfig;
	struct at_desc		*desc;
	u32			ctrla;
	u32			ctrlb;
	dma_addr_t		reg;
	unsigned int		reg_width;
	unsigned int		mem_width;
	unsigned int		i;
	struct scatterlist	*sg;
	size_t			total_len = 0;

	dev_vdbg(chan2dev(chan), "prep_slave_sg (%d): %s f0x%lx\n",
			sg_len,
			direction == DMA_MEM_TO_DEV ? "TO DEVICE" : "FROM DEVICE",
			flags);

	if (unlikely(!atslave || !sg_len)) {
		dev_dbg(chan2dev(chan), "prep_slave_sg: sg length is zero!\n");
		return NULL;
	}

	desc = kzalloc(struct_size(desc, sg, sg_len), GFP_ATOMIC);
	if (!desc)
		return NULL;
	desc->sglen = sg_len;

	ctrla = FIELD_PREP(ATC_SCSIZE, sconfig->src_maxburst) |
		FIELD_PREP(ATC_DCSIZE, sconfig->dst_maxburst);
	ctrlb = ATC_IEN;

	switch (direction) {
	case DMA_MEM_TO_DEV:
		reg_width = convert_buswidth(sconfig->dst_addr_width);
		ctrla |= FIELD_PREP(ATC_DST_WIDTH, reg_width);
		ctrlb |= FIELD_PREP(ATC_DST_ADDR_MODE,
				    ATC_DST_ADDR_MODE_FIXED) |
			 FIELD_PREP(ATC_SRC_ADDR_MODE, ATC_SRC_ADDR_MODE_INCR) |
			 FIELD_PREP(ATC_FC, ATC_FC_MEM2PER) |
			 FIELD_PREP(ATC_SIF, atchan->mem_if) |
			 FIELD_PREP(ATC_DIF, atchan->per_if);
		reg = sconfig->dst_addr;
		for_each_sg(sgl, sg, sg_len, i) {
			struct atdma_sg *atdma_sg = &desc->sg[i];
			struct at_lli *lli;
			u32		len;
			u32		mem;

			atdma_sg->lli = dma_pool_alloc(atdma->lli_pool,
						       GFP_NOWAIT,
						       &atdma_sg->lli_phys);
			if (!atdma_sg->lli)
				goto err_desc_get;
			lli = atdma_sg->lli;

			mem = sg_dma_address(sg);
			len = sg_dma_len(sg);
			if (unlikely(!len)) {
				dev_dbg(chan2dev(chan),
					"prep_slave_sg: sg(%d) data length is zero\n", i);
				goto err;
			}
			mem_width = 2;
			if (unlikely(mem & 3 || len & 3))
				mem_width = 0;

			lli->saddr = mem;
			lli->daddr = reg;
			lli->ctrla = ctrla |
				     FIELD_PREP(ATC_SRC_WIDTH, mem_width) |
				     len >> mem_width;
			lli->ctrlb = ctrlb;

			atdma_sg->len = len;
			total_len += len;

			desc->sg[i].len = len;
			atdma_lli_chain(desc, i);
		}
		break;
	case DMA_DEV_TO_MEM:
		reg_width = convert_buswidth(sconfig->src_addr_width);
		ctrla |= FIELD_PREP(ATC_SRC_WIDTH, reg_width);
		ctrlb |= FIELD_PREP(ATC_DST_ADDR_MODE, ATC_DST_ADDR_MODE_INCR) |
			 FIELD_PREP(ATC_SRC_ADDR_MODE,
				    ATC_SRC_ADDR_MODE_FIXED) |
			 FIELD_PREP(ATC_FC, ATC_FC_PER2MEM) |
			 FIELD_PREP(ATC_SIF, atchan->per_if) |
			 FIELD_PREP(ATC_DIF, atchan->mem_if);

		reg = sconfig->src_addr;
		for_each_sg(sgl, sg, sg_len, i) {
			struct atdma_sg *atdma_sg = &desc->sg[i];
			struct at_lli *lli;
			u32		len;
			u32		mem;

			atdma_sg->lli = dma_pool_alloc(atdma->lli_pool,
						       GFP_NOWAIT,
						       &atdma_sg->lli_phys);
			if (!atdma_sg->lli)
				goto err_desc_get;
			lli = atdma_sg->lli;

			mem = sg_dma_address(sg);
			len = sg_dma_len(sg);
			if (unlikely(!len)) {
				dev_dbg(chan2dev(chan),
					"prep_slave_sg: sg(%d) data length is zero\n", i);
				goto err;
			}
			mem_width = 2;
			if (unlikely(mem & 3 || len & 3))
				mem_width = 0;

			lli->saddr = reg;
			lli->daddr = mem;
			lli->ctrla = ctrla |
				     FIELD_PREP(ATC_DST_WIDTH, mem_width) |
				     len >> reg_width;
			lli->ctrlb = ctrlb;

			desc->sg[i].len = len;
			total_len += len;

			atdma_lli_chain(desc, i);
		}
		break;
	default:
		return NULL;
	}

	/* set end-of-link to the last link descriptor of list*/
	set_lli_eol(desc, i - 1);

	desc->total_len = total_len;

	return vchan_tx_prep(&atchan->vc, &desc->vd, flags);

err_desc_get:
	dev_err(chan2dev(chan), "not enough descriptors available\n");
err:
	atdma_desc_free(&desc->vd);
	return NULL;
}

/*
 * atc_dma_cyclic_check_values
 * Check for too big/unaligned periods and unaligned DMA buffer
 */
static int
atc_dma_cyclic_check_values(unsigned int reg_width, dma_addr_t buf_addr,
		size_t period_len)
{
	if (period_len > (ATC_BTSIZE_MAX << reg_width))
		goto err_out;
	if (unlikely(period_len & ((1 << reg_width) - 1)))
		goto err_out;
	if (unlikely(buf_addr & ((1 << reg_width) - 1)))
		goto err_out;

	return 0;

err_out:
	return -EINVAL;
}

/*
 * atc_dma_cyclic_fill_desc - Fill one period descriptor
 */
static int
atc_dma_cyclic_fill_desc(struct dma_chan *chan, struct at_desc *desc,
		unsigned int i, dma_addr_t buf_addr,
		unsigned int reg_width, size_t period_len,
		enum dma_transfer_direction direction)
{
	struct at_dma		*atdma = to_at_dma(chan->device);
	struct at_dma_chan	*atchan = to_at_dma_chan(chan);
	struct dma_slave_config	*sconfig = &atchan->dma_sconfig;
	struct atdma_sg		*atdma_sg = &desc->sg[i];
	struct at_lli		*lli;

	atdma_sg->lli = dma_pool_alloc(atdma->lli_pool, GFP_ATOMIC,
				       &atdma_sg->lli_phys);
	if (!atdma_sg->lli)
		return -ENOMEM;
	lli = atdma_sg->lli;

	switch (direction) {
	case DMA_MEM_TO_DEV:
		lli->saddr = buf_addr + (period_len * i);
		lli->daddr = sconfig->dst_addr;
		lli->ctrlb = FIELD_PREP(ATC_DST_ADDR_MODE,
					ATC_DST_ADDR_MODE_FIXED) |
			     FIELD_PREP(ATC_SRC_ADDR_MODE,
					ATC_SRC_ADDR_MODE_INCR) |
			     FIELD_PREP(ATC_FC, ATC_FC_MEM2PER) |
			     FIELD_PREP(ATC_SIF, atchan->mem_if) |
			     FIELD_PREP(ATC_DIF, atchan->per_if);

		break;

	case DMA_DEV_TO_MEM:
		lli->saddr = sconfig->src_addr;
		lli->daddr = buf_addr + (period_len * i);
		lli->ctrlb = FIELD_PREP(ATC_DST_ADDR_MODE,
					ATC_DST_ADDR_MODE_INCR) |
			     FIELD_PREP(ATC_SRC_ADDR_MODE,
					ATC_SRC_ADDR_MODE_FIXED) |
			     FIELD_PREP(ATC_FC, ATC_FC_PER2MEM) |
			     FIELD_PREP(ATC_SIF, atchan->per_if) |
			     FIELD_PREP(ATC_DIF, atchan->mem_if);
		break;

	default:
		return -EINVAL;
	}

	lli->ctrla = FIELD_PREP(ATC_SCSIZE, sconfig->src_maxburst) |
		     FIELD_PREP(ATC_DCSIZE, sconfig->dst_maxburst) |
		     FIELD_PREP(ATC_DST_WIDTH, reg_width) |
		     FIELD_PREP(ATC_SRC_WIDTH, reg_width) |
		     period_len >> reg_width;
	desc->sg[i].len = period_len;

	return 0;
}

/**
 * atc_prep_dma_cyclic - prepare the cyclic DMA transfer
 * @chan: the DMA channel to prepare
 * @buf_addr: physical DMA address where the buffer starts
 * @buf_len: total number of bytes for the entire buffer
 * @period_len: number of bytes for each period
 * @direction: transfer direction, to or from device
 * @flags: tx descriptor status flags
 */
static struct dma_async_tx_descriptor *
atc_prep_dma_cyclic(struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
		size_t period_len, enum dma_transfer_direction direction,
		unsigned long flags)
{
	struct at_dma_chan	*atchan = to_at_dma_chan(chan);
	struct at_dma_slave	*atslave = chan->private;
	struct dma_slave_config	*sconfig = &atchan->dma_sconfig;
	struct at_desc		*desc;
	unsigned long		was_cyclic;
	unsigned int		reg_width;
	unsigned int		periods = buf_len / period_len;
	unsigned int		i;

	dev_vdbg(chan2dev(chan), "prep_dma_cyclic: %s buf@%pad - %d (%d/%d)\n",
			direction == DMA_MEM_TO_DEV ? "TO DEVICE" : "FROM DEVICE",
			&buf_addr,
			periods, buf_len, period_len);

	if (unlikely(!atslave || !buf_len || !period_len)) {
		dev_dbg(chan2dev(chan), "prep_dma_cyclic: length is zero!\n");
		return NULL;
	}

	was_cyclic = test_and_set_bit(ATC_IS_CYCLIC, &atchan->status);
	if (was_cyclic) {
		dev_dbg(chan2dev(chan), "prep_dma_cyclic: channel in use!\n");
		return NULL;
	}

	if (unlikely(!is_slave_direction(direction)))
		goto err_out;

	if (direction == DMA_MEM_TO_DEV)
		reg_width = convert_buswidth(sconfig->dst_addr_width);
	else
		reg_width = convert_buswidth(sconfig->src_addr_width);

	/* Check for too big/unaligned periods and unaligned DMA buffer */
	if (atc_dma_cyclic_check_values(reg_width, buf_addr, period_len))
		goto err_out;

	desc = kzalloc(struct_size(desc, sg, periods), GFP_ATOMIC);
	if (!desc)
		goto err_out;
	desc->sglen = periods;

	/* build cyclic linked list */
	for (i = 0; i < periods; i++) {
		if (atc_dma_cyclic_fill_desc(chan, desc, i, buf_addr,
					     reg_width, period_len, direction))
			goto err_fill_desc;
		atdma_lli_chain(desc, i);
	}
	desc->total_len = buf_len;
	/* lets make a cyclic list */
	desc->sg[i - 1].lli->dscr = desc->sg[0].lli_phys;

	return vchan_tx_prep(&atchan->vc, &desc->vd, flags);

err_fill_desc:
	atdma_desc_free(&desc->vd);
err_out:
	clear_bit(ATC_IS_CYCLIC, &atchan->status);
	return NULL;
}

static int atc_config(struct dma_chan *chan,
		      struct dma_slave_config *sconfig)
{
	struct at_dma_chan	*atchan = to_at_dma_chan(chan);

	dev_vdbg(chan2dev(chan), "%s\n", __func__);

	/* Check if it is chan is configured for slave transfers */
	if (!chan->private)
		return -EINVAL;

	memcpy(&atchan->dma_sconfig, sconfig, sizeof(*sconfig));

	convert_burst(&atchan->dma_sconfig.src_maxburst);
	convert_burst(&atchan->dma_sconfig.dst_maxburst);

	return 0;
}

static int atc_pause(struct dma_chan *chan)
{
	struct at_dma_chan	*atchan = to_at_dma_chan(chan);
	struct at_dma		*atdma = to_at_dma(chan->device);
	int			chan_id = atchan->vc.chan.chan_id;
	unsigned long		flags;

	dev_vdbg(chan2dev(chan), "%s\n", __func__);

	spin_lock_irqsave(&atchan->vc.lock, flags);

	dma_writel(atdma, CHER, AT_DMA_SUSP(chan_id));
	set_bit(ATC_IS_PAUSED, &atchan->status);

	spin_unlock_irqrestore(&atchan->vc.lock, flags);

	return 0;
}

static int atc_resume(struct dma_chan *chan)
{
	struct at_dma_chan	*atchan = to_at_dma_chan(chan);
	struct at_dma		*atdma = to_at_dma(chan->device);
	int			chan_id = atchan->vc.chan.chan_id;
	unsigned long		flags;

	dev_vdbg(chan2dev(chan), "%s\n", __func__);

	if (!atc_chan_is_paused(atchan))
		return 0;

	spin_lock_irqsave(&atchan->vc.lock, flags);

	dma_writel(atdma, CHDR, AT_DMA_RES(chan_id));
	clear_bit(ATC_IS_PAUSED, &atchan->status);

	spin_unlock_irqrestore(&atchan->vc.lock, flags);

	return 0;
}

static int atc_terminate_all(struct dma_chan *chan)
{
	struct at_dma_chan	*atchan = to_at_dma_chan(chan);
	struct at_dma		*atdma = to_at_dma(chan->device);
	int			chan_id = atchan->vc.chan.chan_id;
	unsigned long		flags;

	LIST_HEAD(list);

	dev_vdbg(chan2dev(chan), "%s\n", __func__);

	/*
	 * This is only called when something went wrong elsewhere, so
	 * we don't really care about the data. Just disable the
	 * channel. We still have to poll the channel enable bit due
	 * to AHB/HSB limitations.
	 */
	spin_lock_irqsave(&atchan->vc.lock, flags);

	/* disabling channel: must also remove suspend state */
	dma_writel(atdma, CHDR, AT_DMA_RES(chan_id) | atchan->mask);

	/* confirm that this channel is disabled */
	while (dma_readl(atdma, CHSR) & atchan->mask)
		cpu_relax();

	if (atchan->desc) {
		vchan_terminate_vdesc(&atchan->desc->vd);
		atchan->desc = NULL;
	}

	vchan_get_all_descriptors(&atchan->vc, &list);

	clear_bit(ATC_IS_PAUSED, &atchan->status);
	/* if channel dedicated to cyclic operations, free it */
	clear_bit(ATC_IS_CYCLIC, &atchan->status);

	spin_unlock_irqrestore(&atchan->vc.lock, flags);

	vchan_dma_desc_free_list(&atchan->vc, &list);

	return 0;
}

/**
 * atc_tx_status - poll for transaction completion
 * @chan: DMA channel
 * @cookie: transaction identifier to check status of
 * @txstate: if not %NULL updated with transaction state
 *
 * If @txstate is passed in, upon return it reflect the driver
 * internal state and can be used with dma_async_is_complete() to check
 * the status of multiple cookies without re-checking hardware state.
 */
static enum dma_status
atc_tx_status(struct dma_chan *chan,
		dma_cookie_t cookie,
		struct dma_tx_state *txstate)
{
	struct at_dma_chan	*atchan = to_at_dma_chan(chan);
	unsigned long		flags;
	enum dma_status		dma_status;
	u32 residue;
	int ret;

	dma_status = dma_cookie_status(chan, cookie, txstate);
	if (dma_status == DMA_COMPLETE || !txstate)
		return dma_status;

	spin_lock_irqsave(&atchan->vc.lock, flags);
	/*  Get number of bytes left in the active transactions */
	ret = atc_get_residue(chan, cookie, &residue);
	spin_unlock_irqrestore(&atchan->vc.lock, flags);

	if (unlikely(ret < 0)) {
		dev_vdbg(chan2dev(chan), "get residual bytes error\n");
		return DMA_ERROR;
	} else {
		dma_set_residue(txstate, residue);
	}

	dev_vdbg(chan2dev(chan), "tx_status %d: cookie = %d residue = %u\n",
		 dma_status, cookie, residue);

	return dma_status;
}

static void atc_issue_pending(struct dma_chan *chan)
{
	struct at_dma_chan *atchan = to_at_dma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&atchan->vc.lock, flags);
	if (vchan_issue_pending(&atchan->vc) && !atchan->desc) {
		if (!(atc_chan_is_enabled(atchan)))
			atc_dostart(atchan);
	}
	spin_unlock_irqrestore(&atchan->vc.lock, flags);
}

/**
 * atc_alloc_chan_resources - allocate resources for DMA channel
 * @chan: allocate descriptor resources for this channel
 *
 * return - the number of allocated descriptors
 */
static int atc_alloc_chan_resources(struct dma_chan *chan)
{
	struct at_dma_chan	*atchan = to_at_dma_chan(chan);
	struct at_dma		*atdma = to_at_dma(chan->device);
	struct at_dma_slave	*atslave;
	u32			cfg;

	dev_vdbg(chan2dev(chan), "alloc_chan_resources\n");

	/* ASSERT:  channel is idle */
	if (atc_chan_is_enabled(atchan)) {
		dev_dbg(chan2dev(chan), "DMA channel not idle ?\n");
		return -EIO;
	}

	cfg = ATC_DEFAULT_CFG;

	atslave = chan->private;
	if (atslave) {
		/*
		 * We need controller-specific data to set up slave
		 * transfers.
		 */
		BUG_ON(!atslave->dma_dev || atslave->dma_dev != atdma->dma_device.dev);

		/* if cfg configuration specified take it instead of default */
		if (atslave->cfg)
			cfg = atslave->cfg;
	}

	/* channel parameters */
	channel_writel(atchan, CFG, cfg);

	return 0;
}

/**
 * atc_free_chan_resources - free all channel resources
 * @chan: DMA channel
 */
static void atc_free_chan_resources(struct dma_chan *chan)
{
	struct at_dma_chan	*atchan = to_at_dma_chan(chan);

	BUG_ON(atc_chan_is_enabled(atchan));

	vchan_free_chan_resources(to_virt_chan(chan));
	atchan->status = 0;

	/*
	 * Free atslave allocated in at_dma_xlate()
	 */
	kfree(chan->private);
	chan->private = NULL;

	dev_vdbg(chan2dev(chan), "free_chan_resources: done\n");
}

#ifdef CONFIG_OF
static bool at_dma_filter(struct dma_chan *chan, void *slave)
{
	struct at_dma_slave *atslave = slave;

	if (atslave->dma_dev == chan->device->dev) {
		chan->private = atslave;
		return true;
	} else {
		return false;
	}
}

static struct dma_chan *at_dma_xlate(struct of_phandle_args *dma_spec,
				     struct of_dma *of_dma)
{
	struct dma_chan *chan;
	struct at_dma_chan *atchan;
	struct at_dma_slave *atslave;
	dma_cap_mask_t mask;
	unsigned int per_id;
	struct platform_device *dmac_pdev;

	if (dma_spec->args_count != 2)
		return NULL;

	dmac_pdev = of_find_device_by_node(dma_spec->np);
	if (!dmac_pdev)
		return NULL;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	atslave = kmalloc(sizeof(*atslave), GFP_KERNEL);
	if (!atslave) {
		put_device(&dmac_pdev->dev);
		return NULL;
	}

	atslave->cfg = ATC_DST_H2SEL | ATC_SRC_H2SEL;
	/*
	 * We can fill both SRC_PER and DST_PER, one of these fields will be
	 * ignored depending on DMA transfer direction.
	 */
	per_id = dma_spec->args[1] & AT91_DMA_CFG_PER_ID_MASK;
	atslave->cfg |= ATC_DST_PER_ID(per_id) |  ATC_SRC_PER_ID(per_id);
	/*
	 * We have to translate the value we get from the device tree since
	 * the half FIFO configuration value had to be 0 to keep backward
	 * compatibility.
	 */
	switch (dma_spec->args[1] & AT91_DMA_CFG_FIFOCFG_MASK) {
	case AT91_DMA_CFG_FIFOCFG_ALAP:
		atslave->cfg |= FIELD_PREP(ATC_FIFOCFG,
					   ATC_FIFOCFG_LARGESTBURST);
		break;
	case AT91_DMA_CFG_FIFOCFG_ASAP:
		atslave->cfg |= FIELD_PREP(ATC_FIFOCFG,
					   ATC_FIFOCFG_ENOUGHSPACE);
		break;
	case AT91_DMA_CFG_FIFOCFG_HALF:
	default:
		atslave->cfg |= FIELD_PREP(ATC_FIFOCFG, ATC_FIFOCFG_HALFFIFO);
	}
	atslave->dma_dev = &dmac_pdev->dev;

	chan = dma_request_channel(mask, at_dma_filter, atslave);
	if (!chan) {
		put_device(&dmac_pdev->dev);
		kfree(atslave);
		return NULL;
	}

	atchan = to_at_dma_chan(chan);
	atchan->per_if = dma_spec->args[0] & 0xff;
	atchan->mem_if = (dma_spec->args[0] >> 16) & 0xff;

	return chan;
}
#else
static struct dma_chan *at_dma_xlate(struct of_phandle_args *dma_spec,
				     struct of_dma *of_dma)
{
	return NULL;
}
#endif

/*--  Module Management  -----------------------------------------------*/

/* cap_mask is a multi-u32 bitfield, fill it with proper C code. */
static struct at_dma_platform_data at91sam9rl_config = {
	.nr_channels = 2,
};
static struct at_dma_platform_data at91sam9g45_config = {
	.nr_channels = 8,
};

#if defined(CONFIG_OF)
static const struct of_device_id atmel_dma_dt_ids[] = {
	{
		.compatible = "atmel,at91sam9rl-dma",
		.data = &at91sam9rl_config,
	}, {
		.compatible = "atmel,at91sam9g45-dma",
		.data = &at91sam9g45_config,
	}, {
		/* sentinel */
	}
};

MODULE_DEVICE_TABLE(of, atmel_dma_dt_ids);
#endif

static const struct platform_device_id atdma_devtypes[] = {
	{
		.name = "at91sam9rl_dma",
		.driver_data = (unsigned long) &at91sam9rl_config,
	}, {
		.name = "at91sam9g45_dma",
		.driver_data = (unsigned long) &at91sam9g45_config,
	}, {
		/* sentinel */
	}
};

static inline const struct at_dma_platform_data * __init at_dma_get_driver_data(
						struct platform_device *pdev)
{
	if (pdev->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_node(atmel_dma_dt_ids, pdev->dev.of_node);
		if (match == NULL)
			return NULL;
		return match->data;
	}
	return (struct at_dma_platform_data *)
			platform_get_device_id(pdev)->driver_data;
}

/**
 * at_dma_off - disable DMA controller
 * @atdma: the Atmel HDAMC device
 */
static void at_dma_off(struct at_dma *atdma)
{
	dma_writel(atdma, EN, 0);

	/* disable all interrupts */
	dma_writel(atdma, EBCIDR, -1L);

	/* confirm that all channels are disabled */
	while (dma_readl(atdma, CHSR) & atdma->all_chan_mask)
		cpu_relax();
}

static int __init at_dma_probe(struct platform_device *pdev)
{
	struct at_dma		*atdma;
	int			irq;
	int			err;
	int			i;
	const struct at_dma_platform_data *plat_dat;

	/* setup platform data for each SoC */
	dma_cap_set(DMA_MEMCPY, at91sam9rl_config.cap_mask);
	dma_cap_set(DMA_INTERLEAVE, at91sam9g45_config.cap_mask);
	dma_cap_set(DMA_MEMCPY, at91sam9g45_config.cap_mask);
	dma_cap_set(DMA_MEMSET, at91sam9g45_config.cap_mask);
	dma_cap_set(DMA_MEMSET_SG, at91sam9g45_config.cap_mask);
	dma_cap_set(DMA_PRIVATE, at91sam9g45_config.cap_mask);
	dma_cap_set(DMA_SLAVE, at91sam9g45_config.cap_mask);

	/* get DMA parameters from controller type */
	plat_dat = at_dma_get_driver_data(pdev);
	if (!plat_dat)
		return -ENODEV;

	atdma = devm_kzalloc(&pdev->dev,
			     struct_size(atdma, chan, plat_dat->nr_channels),
			     GFP_KERNEL);
	if (!atdma)
		return -ENOMEM;

	atdma->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(atdma->regs))
		return PTR_ERR(atdma->regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	/* discover transaction capabilities */
	atdma->dma_device.cap_mask = plat_dat->cap_mask;
	atdma->all_chan_mask = (1 << plat_dat->nr_channels) - 1;

	atdma->clk = devm_clk_get(&pdev->dev, "dma_clk");
	if (IS_ERR(atdma->clk))
		return PTR_ERR(atdma->clk);

	err = clk_prepare_enable(atdma->clk);
	if (err)
		return err;

	/* force dma off, just in case */
	at_dma_off(atdma);

	err = request_irq(irq, at_dma_interrupt, 0, "at_hdmac", atdma);
	if (err)
		goto err_irq;

	platform_set_drvdata(pdev, atdma);

	/* create a pool of consistent memory blocks for hardware descriptors */
	atdma->lli_pool = dma_pool_create("at_hdmac_lli_pool",
					  &pdev->dev, sizeof(struct at_lli),
					  4 /* word alignment */, 0);
	if (!atdma->lli_pool) {
		dev_err(&pdev->dev, "Unable to allocate DMA LLI descriptor pool\n");
		err = -ENOMEM;
		goto err_desc_pool_create;
	}

	/* create a pool of consistent memory blocks for memset blocks */
	atdma->memset_pool = dma_pool_create("at_hdmac_memset_pool",
					     &pdev->dev, sizeof(int), 4, 0);
	if (!atdma->memset_pool) {
		dev_err(&pdev->dev, "No memory for memset dma pool\n");
		err = -ENOMEM;
		goto err_memset_pool_create;
	}

	/* clear any pending interrupt */
	while (dma_readl(atdma, EBCISR))
		cpu_relax();

	/* initialize channels related values */
	INIT_LIST_HEAD(&atdma->dma_device.channels);
	for (i = 0; i < plat_dat->nr_channels; i++) {
		struct at_dma_chan	*atchan = &atdma->chan[i];

		atchan->mem_if = AT_DMA_MEM_IF;
		atchan->per_if = AT_DMA_PER_IF;

		atchan->ch_regs = atdma->regs + ch_regs(i);
		atchan->mask = 1 << i;

		atchan->atdma = atdma;
		atchan->vc.desc_free = atdma_desc_free;
		vchan_init(&atchan->vc, &atdma->dma_device);
		atc_enable_chan_irq(atdma, i);
	}

	/* set base routines */
	atdma->dma_device.device_alloc_chan_resources = atc_alloc_chan_resources;
	atdma->dma_device.device_free_chan_resources = atc_free_chan_resources;
	atdma->dma_device.device_tx_status = atc_tx_status;
	atdma->dma_device.device_issue_pending = atc_issue_pending;
	atdma->dma_device.dev = &pdev->dev;

	/* set prep routines based on capability */
	if (dma_has_cap(DMA_INTERLEAVE, atdma->dma_device.cap_mask))
		atdma->dma_device.device_prep_interleaved_dma = atc_prep_dma_interleaved;

	if (dma_has_cap(DMA_MEMCPY, atdma->dma_device.cap_mask))
		atdma->dma_device.device_prep_dma_memcpy = atc_prep_dma_memcpy;

	if (dma_has_cap(DMA_MEMSET, atdma->dma_device.cap_mask)) {
		atdma->dma_device.device_prep_dma_memset = atc_prep_dma_memset;
		atdma->dma_device.device_prep_dma_memset_sg = atc_prep_dma_memset_sg;
		atdma->dma_device.fill_align = DMAENGINE_ALIGN_4_BYTES;
	}

	if (dma_has_cap(DMA_SLAVE, atdma->dma_device.cap_mask)) {
		atdma->dma_device.device_prep_slave_sg = atc_prep_slave_sg;
		/* controller can do slave DMA: can trigger cyclic transfers */
		dma_cap_set(DMA_CYCLIC, atdma->dma_device.cap_mask);
		atdma->dma_device.device_prep_dma_cyclic = atc_prep_dma_cyclic;
		atdma->dma_device.device_config = atc_config;
		atdma->dma_device.device_pause = atc_pause;
		atdma->dma_device.device_resume = atc_resume;
		atdma->dma_device.device_terminate_all = atc_terminate_all;
		atdma->dma_device.src_addr_widths = ATC_DMA_BUSWIDTHS;
		atdma->dma_device.dst_addr_widths = ATC_DMA_BUSWIDTHS;
		atdma->dma_device.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
		atdma->dma_device.residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;
	}

	dma_writel(atdma, EN, AT_DMA_ENABLE);

	dev_info(&pdev->dev, "Atmel AHB DMA Controller ( %s%s%s), %d channels\n",
	  dma_has_cap(DMA_MEMCPY, atdma->dma_device.cap_mask) ? "cpy " : "",
	  dma_has_cap(DMA_MEMSET, atdma->dma_device.cap_mask) ? "set " : "",
	  dma_has_cap(DMA_SLAVE, atdma->dma_device.cap_mask)  ? "slave " : "",
	  plat_dat->nr_channels);

	err = dma_async_device_register(&atdma->dma_device);
	if (err) {
		dev_err(&pdev->dev, "Unable to register: %d.\n", err);
		goto err_dma_async_device_register;
	}

	/*
	 * Do not return an error if the dmac node is not present in order to
	 * not break the existing way of requesting channel with
	 * dma_request_channel().
	 */
	if (pdev->dev.of_node) {
		err = of_dma_controller_register(pdev->dev.of_node,
						 at_dma_xlate, atdma);
		if (err) {
			dev_err(&pdev->dev, "could not register of_dma_controller\n");
			goto err_of_dma_controller_register;
		}
	}

	return 0;

err_of_dma_controller_register:
	dma_async_device_unregister(&atdma->dma_device);
err_dma_async_device_register:
	dma_pool_destroy(atdma->memset_pool);
err_memset_pool_create:
	dma_pool_destroy(atdma->lli_pool);
err_desc_pool_create:
	free_irq(platform_get_irq(pdev, 0), atdma);
err_irq:
	clk_disable_unprepare(atdma->clk);
	return err;
}

static int at_dma_remove(struct platform_device *pdev)
{
	struct at_dma		*atdma = platform_get_drvdata(pdev);
	struct dma_chan		*chan, *_chan;

	at_dma_off(atdma);
	if (pdev->dev.of_node)
		of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&atdma->dma_device);

	dma_pool_destroy(atdma->memset_pool);
	dma_pool_destroy(atdma->lli_pool);
	free_irq(platform_get_irq(pdev, 0), atdma);

	list_for_each_entry_safe(chan, _chan, &atdma->dma_device.channels,
			device_node) {
		/* Disable interrupts */
		atc_disable_chan_irq(atdma, chan->chan_id);
		list_del(&chan->device_node);
	}

	clk_disable_unprepare(atdma->clk);

	return 0;
}

static void at_dma_shutdown(struct platform_device *pdev)
{
	struct at_dma	*atdma = platform_get_drvdata(pdev);

	at_dma_off(platform_get_drvdata(pdev));
	clk_disable_unprepare(atdma->clk);
}

static int at_dma_prepare(struct device *dev)
{
	struct at_dma *atdma = dev_get_drvdata(dev);
	struct dma_chan *chan, *_chan;

	list_for_each_entry_safe(chan, _chan, &atdma->dma_device.channels,
			device_node) {
		struct at_dma_chan *atchan = to_at_dma_chan(chan);
		/* wait for transaction completion (except in cyclic case) */
		if (atc_chan_is_enabled(atchan) && !atc_chan_is_cyclic(atchan))
			return -EAGAIN;
	}
	return 0;
}

static void atc_suspend_cyclic(struct at_dma_chan *atchan)
{
	struct dma_chan	*chan = &atchan->vc.chan;

	/* Channel should be paused by user
	 * do it anyway even if it is not done already */
	if (!atc_chan_is_paused(atchan)) {
		dev_warn(chan2dev(chan),
		"cyclic channel not paused, should be done by channel user\n");
		atc_pause(chan);
	}

	/* now preserve additional data for cyclic operations */
	/* next descriptor address in the cyclic list */
	atchan->save_dscr = channel_readl(atchan, DSCR);

	vdbg_dump_regs(atchan);
}

static int at_dma_suspend_noirq(struct device *dev)
{
	struct at_dma *atdma = dev_get_drvdata(dev);
	struct dma_chan *chan, *_chan;

	/* preserve data */
	list_for_each_entry_safe(chan, _chan, &atdma->dma_device.channels,
			device_node) {
		struct at_dma_chan *atchan = to_at_dma_chan(chan);

		if (atc_chan_is_cyclic(atchan))
			atc_suspend_cyclic(atchan);
		atchan->save_cfg = channel_readl(atchan, CFG);
	}
	atdma->save_imr = dma_readl(atdma, EBCIMR);

	/* disable DMA controller */
	at_dma_off(atdma);
	clk_disable_unprepare(atdma->clk);
	return 0;
}

static void atc_resume_cyclic(struct at_dma_chan *atchan)
{
	struct at_dma	*atdma = to_at_dma(atchan->vc.chan.device);

	/* restore channel status for cyclic descriptors list:
	 * next descriptor in the cyclic list at the time of suspend */
	channel_writel(atchan, SADDR, 0);
	channel_writel(atchan, DADDR, 0);
	channel_writel(atchan, CTRLA, 0);
	channel_writel(atchan, CTRLB, 0);
	channel_writel(atchan, DSCR, atchan->save_dscr);
	dma_writel(atdma, CHER, atchan->mask);

	/* channel pause status should be removed by channel user
	 * We cannot take the initiative to do it here */

	vdbg_dump_regs(atchan);
}

static int at_dma_resume_noirq(struct device *dev)
{
	struct at_dma *atdma = dev_get_drvdata(dev);
	struct dma_chan *chan, *_chan;

	/* bring back DMA controller */
	clk_prepare_enable(atdma->clk);
	dma_writel(atdma, EN, AT_DMA_ENABLE);

	/* clear any pending interrupt */
	while (dma_readl(atdma, EBCISR))
		cpu_relax();

	/* restore saved data */
	dma_writel(atdma, EBCIER, atdma->save_imr);
	list_for_each_entry_safe(chan, _chan, &atdma->dma_device.channels,
			device_node) {
		struct at_dma_chan *atchan = to_at_dma_chan(chan);

		channel_writel(atchan, CFG, atchan->save_cfg);
		if (atc_chan_is_cyclic(atchan))
			atc_resume_cyclic(atchan);
	}
	return 0;
}

static const struct dev_pm_ops __maybe_unused at_dma_dev_pm_ops = {
	.prepare = at_dma_prepare,
	.suspend_noirq = at_dma_suspend_noirq,
	.resume_noirq = at_dma_resume_noirq,
};

static struct platform_driver at_dma_driver = {
	.remove		= at_dma_remove,
	.shutdown	= at_dma_shutdown,
	.id_table	= atdma_devtypes,
	.driver = {
		.name	= "at_hdmac",
		.pm	= pm_ptr(&at_dma_dev_pm_ops),
		.of_match_table	= of_match_ptr(atmel_dma_dt_ids),
	},
};

static int __init at_dma_init(void)
{
	return platform_driver_probe(&at_dma_driver, at_dma_probe);
}
subsys_initcall(at_dma_init);

static void __exit at_dma_exit(void)
{
	platform_driver_unregister(&at_dma_driver);
}
module_exit(at_dma_exit);

MODULE_DESCRIPTION("Atmel AHB DMA Controller driver");
MODULE_AUTHOR("Nicolas Ferre <nicolas.ferre@atmel.com>");
MODULE_AUTHOR("Tudor Ambarus <tudor.ambarus@microchip.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:at_hdmac");
