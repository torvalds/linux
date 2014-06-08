/*
 * Driver for the Synopsys DesignWare AHB DMA Controller
 *
 * Copyright (C) 2005-2007 Atmel Corporation
 * Copyright (C) 2010-2011 ST Microelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <linux/dmaengine.h>
#include <linux/dw_dmac.h>

#define DW_DMA_MAX_NR_CHANNELS	8
#define DW_DMA_MAX_NR_REQUESTS	16

/* flow controller */
enum dw_dma_fc {
	DW_DMA_FC_D_M2M,
	DW_DMA_FC_D_M2P,
	DW_DMA_FC_D_P2M,
	DW_DMA_FC_D_P2P,
	DW_DMA_FC_P_P2M,
	DW_DMA_FC_SP_P2P,
	DW_DMA_FC_P_M2P,
	DW_DMA_FC_DP_P2P,
};

/*
 * Redefine this macro to handle differences between 32- and 64-bit
 * addressing, big vs. little endian, etc.
 */
#define DW_REG(name)		u32 name; u32 __pad_##name

/* Hardware register definitions. */
struct dw_dma_chan_regs {
	DW_REG(SAR);		/* Source Address Register */
	DW_REG(DAR);		/* Destination Address Register */
	DW_REG(LLP);		/* Linked List Pointer */
	u32	CTL_LO;		/* Control Register Low */
	u32	CTL_HI;		/* Control Register High */
	DW_REG(SSTAT);
	DW_REG(DSTAT);
	DW_REG(SSTATAR);
	DW_REG(DSTATAR);
	u32	CFG_LO;		/* Configuration Register Low */
	u32	CFG_HI;		/* Configuration Register High */
	DW_REG(SGR);
	DW_REG(DSR);
};

struct dw_dma_irq_regs {
	DW_REG(XFER);
	DW_REG(BLOCK);
	DW_REG(SRC_TRAN);
	DW_REG(DST_TRAN);
	DW_REG(ERROR);
};

struct dw_dma_regs {
	/* per-channel registers */
	struct dw_dma_chan_regs	CHAN[DW_DMA_MAX_NR_CHANNELS];

	/* irq handling */
	struct dw_dma_irq_regs	RAW;		/* r */
	struct dw_dma_irq_regs	STATUS;		/* r (raw & mask) */
	struct dw_dma_irq_regs	MASK;		/* rw (set = irq enabled) */
	struct dw_dma_irq_regs	CLEAR;		/* w (ack, affects "raw") */

	DW_REG(STATUS_INT);			/* r */

	/* software handshaking */
	DW_REG(REQ_SRC);
	DW_REG(REQ_DST);
	DW_REG(SGL_REQ_SRC);
	DW_REG(SGL_REQ_DST);
	DW_REG(LAST_SRC);
	DW_REG(LAST_DST);

	/* miscellaneous */
	DW_REG(CFG);
	DW_REG(CH_EN);
	DW_REG(ID);
	DW_REG(TEST);

	/* reserved */
	DW_REG(__reserved0);
	DW_REG(__reserved1);

	/* optional encoded params, 0x3c8..0x3f7 */
	u32	__reserved;

	/* per-channel configuration registers */
	u32	DWC_PARAMS[DW_DMA_MAX_NR_CHANNELS];
	u32	MULTI_BLK_TYPE;
	u32	MAX_BLK_SIZE;

	/* top-level parameters */
	u32	DW_PARAMS;
};

/*
 * Big endian I/O access when reading and writing to the DMA controller
 * registers.  This is needed on some platforms, like the Atmel AVR32
 * architecture.
 */

#ifdef CONFIG_DW_DMAC_BIG_ENDIAN_IO
#define dma_readl_native ioread32be
#define dma_writel_native iowrite32be
#else
#define dma_readl_native readl
#define dma_writel_native writel
#endif

/* To access the registers in early stage of probe */
#define dma_read_byaddr(addr, name) \
	dma_readl_native((addr) + offsetof(struct dw_dma_regs, name))

/* Bitfields in DW_PARAMS */
#define DW_PARAMS_NR_CHAN	8		/* number of channels */
#define DW_PARAMS_NR_MASTER	11		/* number of AHB masters */
#define DW_PARAMS_DATA_WIDTH(n)	(15 + 2 * (n))
#define DW_PARAMS_DATA_WIDTH1	15		/* master 1 data width */
#define DW_PARAMS_DATA_WIDTH2	17		/* master 2 data width */
#define DW_PARAMS_DATA_WIDTH3	19		/* master 3 data width */
#define DW_PARAMS_DATA_WIDTH4	21		/* master 4 data width */
#define DW_PARAMS_EN		28		/* encoded parameters */

/* Bitfields in DWC_PARAMS */
#define DWC_PARAMS_MBLK_EN	11		/* multi block transfer */

/* Bitfields in CTL_LO */
#define DWC_CTLL_INT_EN		(1 << 0)	/* irqs enabled? */
#define DWC_CTLL_DST_WIDTH(n)	((n)<<1)	/* bytes per element */
#define DWC_CTLL_SRC_WIDTH(n)	((n)<<4)
#define DWC_CTLL_DST_INC	(0<<7)		/* DAR update/not */
#define DWC_CTLL_DST_DEC	(1<<7)
#define DWC_CTLL_DST_FIX	(2<<7)
#define DWC_CTLL_SRC_INC	(0<<7)		/* SAR update/not */
#define DWC_CTLL_SRC_DEC	(1<<9)
#define DWC_CTLL_SRC_FIX	(2<<9)
#define DWC_CTLL_DST_MSIZE(n)	((n)<<11)	/* burst, #elements */
#define DWC_CTLL_SRC_MSIZE(n)	((n)<<14)
#define DWC_CTLL_S_GATH_EN	(1 << 17)	/* src gather, !FIX */
#define DWC_CTLL_D_SCAT_EN	(1 << 18)	/* dst scatter, !FIX */
#define DWC_CTLL_FC(n)		((n) << 20)
#define DWC_CTLL_FC_M2M		(0 << 20)	/* mem-to-mem */
#define DWC_CTLL_FC_M2P		(1 << 20)	/* mem-to-periph */
#define DWC_CTLL_FC_P2M		(2 << 20)	/* periph-to-mem */
#define DWC_CTLL_FC_P2P		(3 << 20)	/* periph-to-periph */
/* plus 4 transfer types for peripheral-as-flow-controller */
#define DWC_CTLL_DMS(n)		((n)<<23)	/* dst master select */
#define DWC_CTLL_SMS(n)		((n)<<25)	/* src master select */
#define DWC_CTLL_LLP_D_EN	(1 << 27)	/* dest block chain */
#define DWC_CTLL_LLP_S_EN	(1 << 28)	/* src block chain */

/* Bitfields in CTL_HI */
#define DWC_CTLH_DONE		0x00001000
#define DWC_CTLH_BLOCK_TS_MASK	0x00000fff

/* Bitfields in CFG_LO. Platform-configurable bits are in <linux/dw_dmac.h> */
#define DWC_CFGL_CH_PRIOR_MASK	(0x7 << 5)	/* priority mask */
#define DWC_CFGL_CH_PRIOR(x)	((x) << 5)	/* priority */
#define DWC_CFGL_CH_SUSP	(1 << 8)	/* pause xfer */
#define DWC_CFGL_FIFO_EMPTY	(1 << 9)	/* pause xfer */
#define DWC_CFGL_HS_DST		(1 << 10)	/* handshake w/dst */
#define DWC_CFGL_HS_SRC		(1 << 11)	/* handshake w/src */
#define DWC_CFGL_MAX_BURST(x)	((x) << 20)
#define DWC_CFGL_RELOAD_SAR	(1 << 30)
#define DWC_CFGL_RELOAD_DAR	(1 << 31)

/* Bitfields in CFG_HI. Platform-configurable bits are in <linux/dw_dmac.h> */
#define DWC_CFGH_DS_UPD_EN	(1 << 5)
#define DWC_CFGH_SS_UPD_EN	(1 << 6)

/* Bitfields in SGR */
#define DWC_SGR_SGI(x)		((x) << 0)
#define DWC_SGR_SGC(x)		((x) << 20)

/* Bitfields in DSR */
#define DWC_DSR_DSI(x)		((x) << 0)
#define DWC_DSR_DSC(x)		((x) << 20)

/* Bitfields in CFG */
#define DW_CFG_DMA_EN		(1 << 0)

enum dw_dmac_flags {
	DW_DMA_IS_CYCLIC = 0,
	DW_DMA_IS_SOFT_LLP = 1,
};

struct dw_dma_chan {
	struct dma_chan			chan;
	void __iomem			*ch_regs;
	u8				mask;
	u8				priority;
	enum dma_transfer_direction	direction;
	bool				paused;
	bool				initialized;

	/* software emulation of the LLP transfers */
	struct list_head	*tx_node_active;

	spinlock_t		lock;

	/* these other elements are all protected by lock */
	unsigned long		flags;
	struct list_head	active_list;
	struct list_head	queue;
	struct list_head	free_list;
	u32			residue;
	struct dw_cyclic_desc	*cdesc;

	unsigned int		descs_allocated;

	/* hardware configuration */
	unsigned int		block_size;
	bool			nollp;

	/* custom slave configuration */
	unsigned int		request_line;
	unsigned char		src_master;
	unsigned char		dst_master;

	/* configuration passed via DMA_SLAVE_CONFIG */
	struct dma_slave_config dma_sconfig;
};

static inline struct dw_dma_chan_regs __iomem *
__dwc_regs(struct dw_dma_chan *dwc)
{
	return dwc->ch_regs;
}

#define channel_readl(dwc, name) \
	dma_readl_native(&(__dwc_regs(dwc)->name))
#define channel_writel(dwc, name, val) \
	dma_writel_native((val), &(__dwc_regs(dwc)->name))

static inline struct dw_dma_chan *to_dw_dma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct dw_dma_chan, chan);
}

struct dw_dma {
	struct dma_device	dma;
	void __iomem		*regs;
	struct dma_pool		*desc_pool;
	struct tasklet_struct	tasklet;
	struct clk		*clk;

	/* channels */
	struct dw_dma_chan	*chan;
	u8			all_chan_mask;

	/* hardware configuration */
	unsigned char		nr_masters;
	unsigned char		data_width[4];
};

static inline struct dw_dma_regs __iomem *__dw_regs(struct dw_dma *dw)
{
	return dw->regs;
}

#define dma_readl(dw, name) \
	dma_readl_native(&(__dw_regs(dw)->name))
#define dma_writel(dw, name, val) \
	dma_writel_native((val), &(__dw_regs(dw)->name))

#define channel_set_bit(dw, reg, mask) \
	dma_writel(dw, reg, ((mask) << 8) | (mask))
#define channel_clear_bit(dw, reg, mask) \
	dma_writel(dw, reg, ((mask) << 8) | 0)

static inline struct dw_dma *to_dw_dma(struct dma_device *ddev)
{
	return container_of(ddev, struct dw_dma, dma);
}

/* LLI == Linked List Item; a.k.a. DMA block descriptor */
struct dw_lli {
	/* values that are not changed by hardware */
	u32		sar;
	u32		dar;
	u32		llp;		/* chain to next lli */
	u32		ctllo;
	/* values that may get written back: */
	u32		ctlhi;
	/* sstat and dstat can snapshot peripheral register state.
	 * silicon config may discard either or both...
	 */
	u32		sstat;
	u32		dstat;
};

struct dw_desc {
	/* FIRST values the hardware uses */
	struct dw_lli			lli;

	/* THEN values for driver housekeeping */
	struct list_head		desc_node;
	struct list_head		tx_list;
	struct dma_async_tx_descriptor	txd;
	size_t				len;
	size_t				total_len;
};

#define to_dw_desc(h)	list_entry(h, struct dw_desc, desc_node)

static inline struct dw_desc *
txd_to_dw_desc(struct dma_async_tx_descriptor *txd)
{
	return container_of(txd, struct dw_desc, txd);
}
