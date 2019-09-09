// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2006 ARM Ltd.
 * Copyright (c) 2010 ST-Ericsson SA
 * Copyirght (c) 2017 Linaro Ltd.
 *
 * Author: Peter Pearse <peter.pearse@arm.com>
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * Documentation: ARM DDI 0196G == PL080
 * Documentation: ARM DDI 0218E == PL081
 * Documentation: S3C6410 User's Manual == PL080S
 *
 * PL080 & PL081 both have 16 sets of DMA signals that can be routed to any
 * channel.
 *
 * The PL080 has 8 channels available for simultaneous use, and the PL081
 * has only two channels. So on these DMA controllers the number of channels
 * and the number of incoming DMA signals are two totally different things.
 * It is usually not possible to theoretically handle all physical signals,
 * so a multiplexing scheme with possible denial of use is necessary.
 *
 * The PL080 has a dual bus master, PL081 has a single master.
 *
 * PL080S is a version modified by Samsung and used in S3C64xx SoCs.
 * It differs in following aspects:
 * - CH_CONFIG register at different offset,
 * - separate CH_CONTROL2 register for transfer size,
 * - bigger maximum transfer size,
 * - 8-word aligned LLI, instead of 4-word, due to extra CCTL2 word,
 * - no support for peripheral flow control.
 *
 * Memory to peripheral transfer may be visualized as
 *	Get data from memory to DMAC
 *	Until no data left
 *		On burst request from peripheral
 *			Destination burst from DMAC to peripheral
 *			Clear burst request
 *	Raise terminal count interrupt
 *
 * For peripherals with a FIFO:
 * Source      burst size == half the depth of the peripheral FIFO
 * Destination burst size == the depth of the peripheral FIFO
 *
 * (Bursts are irrelevant for mem to mem transfers - there are no burst
 * signals, the DMA controller will simply facilitate its AHB master.)
 *
 * ASSUMES default (little) endianness for DMA transfers
 *
 * The PL08x has two flow control settings:
 *  - DMAC flow control: the transfer size defines the number of transfers
 *    which occur for the current LLI entry, and the DMAC raises TC at the
 *    end of every LLI entry.  Observed behaviour shows the DMAC listening
 *    to both the BREQ and SREQ signals (contrary to documented),
 *    transferring data if either is active.  The LBREQ and LSREQ signals
 *    are ignored.
 *
 *  - Peripheral flow control: the transfer size is ignored (and should be
 *    zero).  The data is transferred from the current LLI entry, until
 *    after the final transfer signalled by LBREQ or LSREQ.  The DMAC
 *    will then move to the next LLI entry. Unsupported by PL080S.
 */
#include <linux/amba/bus.h>
#include <linux/amba/pl08x.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/dmapool.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/amba/pl080.h>

#include "dmaengine.h"
#include "virt-dma.h"

#define DRIVER_NAME	"pl08xdmac"

#define PL80X_DMA_BUSWIDTHS \
	BIT(DMA_SLAVE_BUSWIDTH_UNDEFINED) | \
	BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) | \
	BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) | \
	BIT(DMA_SLAVE_BUSWIDTH_4_BYTES)

static struct amba_driver pl08x_amba_driver;
struct pl08x_driver_data;

/**
 * struct vendor_data - vendor-specific config parameters for PL08x derivatives
 * @config_offset: offset to the configuration register
 * @channels: the number of channels available in this variant
 * @signals: the number of request signals available from the hardware
 * @dualmaster: whether this version supports dual AHB masters or not.
 * @nomadik: whether this variant is a ST Microelectronics Nomadik, where the
 *	channels have Nomadik security extension bits that need to be checked
 *	for permission before use and some registers are missing
 * @pl080s: whether this variant is a Samsung PL080S, which has separate
 *	register and LLI word for transfer size.
 * @ftdmac020: whether this variant is a Faraday Technology FTDMAC020
 * @max_transfer_size: the maximum single element transfer size for this
 *	PL08x variant.
 */
struct vendor_data {
	u8 config_offset;
	u8 channels;
	u8 signals;
	bool dualmaster;
	bool nomadik;
	bool pl080s;
	bool ftdmac020;
	u32 max_transfer_size;
};

/**
 * struct pl08x_bus_data - information of source or destination
 * busses for a transfer
 * @addr: current address
 * @maxwidth: the maximum width of a transfer on this bus
 * @buswidth: the width of this bus in bytes: 1, 2 or 4
 */
struct pl08x_bus_data {
	dma_addr_t addr;
	u8 maxwidth;
	u8 buswidth;
};

#define IS_BUS_ALIGNED(bus) IS_ALIGNED((bus)->addr, (bus)->buswidth)

/**
 * struct pl08x_phy_chan - holder for the physical channels
 * @id: physical index to this channel
 * @base: memory base address for this physical channel
 * @reg_config: configuration address for this physical channel
 * @reg_control: control address for this physical channel
 * @reg_src: transfer source address register
 * @reg_dst: transfer destination address register
 * @reg_lli: transfer LLI address register
 * @reg_busy: if the variant has a special per-channel busy register,
 * this contains a pointer to it
 * @lock: a lock to use when altering an instance of this struct
 * @serving: the virtual channel currently being served by this physical
 * channel
 * @locked: channel unavailable for the system, e.g. dedicated to secure
 * world
 * @ftdmac020: channel is on a FTDMAC020
 * @pl080s: channel is on a PL08s
 */
struct pl08x_phy_chan {
	unsigned int id;
	void __iomem *base;
	void __iomem *reg_config;
	void __iomem *reg_control;
	void __iomem *reg_src;
	void __iomem *reg_dst;
	void __iomem *reg_lli;
	void __iomem *reg_busy;
	spinlock_t lock;
	struct pl08x_dma_chan *serving;
	bool locked;
	bool ftdmac020;
	bool pl080s;
};

/**
 * struct pl08x_sg - structure containing data per sg
 * @src_addr: src address of sg
 * @dst_addr: dst address of sg
 * @len: transfer len in bytes
 * @node: node for txd's dsg_list
 */
struct pl08x_sg {
	dma_addr_t src_addr;
	dma_addr_t dst_addr;
	size_t len;
	struct list_head node;
};

/**
 * struct pl08x_txd - wrapper for struct dma_async_tx_descriptor
 * @vd: virtual DMA descriptor
 * @dsg_list: list of children sg's
 * @llis_bus: DMA memory address (physical) start for the LLIs
 * @llis_va: virtual memory address start for the LLIs
 * @cctl: control reg values for current txd
 * @ccfg: config reg values for current txd
 * @done: this marks completed descriptors, which should not have their
 *   mux released.
 * @cyclic: indicate cyclic transfers
 */
struct pl08x_txd {
	struct virt_dma_desc vd;
	struct list_head dsg_list;
	dma_addr_t llis_bus;
	u32 *llis_va;
	/* Default cctl value for LLIs */
	u32 cctl;
	/*
	 * Settings to be put into the physical channel when we
	 * trigger this txd.  Other registers are in llis_va[0].
	 */
	u32 ccfg;
	bool done;
	bool cyclic;
};

/**
 * enum pl08x_dma_chan_state - holds the PL08x specific virtual channel
 * states
 * @PL08X_CHAN_IDLE: the channel is idle
 * @PL08X_CHAN_RUNNING: the channel has allocated a physical transport
 * channel and is running a transfer on it
 * @PL08X_CHAN_PAUSED: the channel has allocated a physical transport
 * channel, but the transfer is currently paused
 * @PL08X_CHAN_WAITING: the channel is waiting for a physical transport
 * channel to become available (only pertains to memcpy channels)
 */
enum pl08x_dma_chan_state {
	PL08X_CHAN_IDLE,
	PL08X_CHAN_RUNNING,
	PL08X_CHAN_PAUSED,
	PL08X_CHAN_WAITING,
};

/**
 * struct pl08x_dma_chan - this structure wraps a DMA ENGINE channel
 * @vc: wrappped virtual channel
 * @phychan: the physical channel utilized by this channel, if there is one
 * @name: name of channel
 * @cd: channel platform data
 * @cfg: slave configuration
 * @at: active transaction on this channel
 * @host: a pointer to the host (internal use)
 * @state: whether the channel is idle, paused, running etc
 * @slave: whether this channel is a device (slave) or for memcpy
 * @signal: the physical DMA request signal which this channel is using
 * @mux_use: count of descriptors using this DMA request signal setting
 * @waiting_at: time in jiffies when this channel moved to waiting state
 */
struct pl08x_dma_chan {
	struct virt_dma_chan vc;
	struct pl08x_phy_chan *phychan;
	const char *name;
	struct pl08x_channel_data *cd;
	struct dma_slave_config cfg;
	struct pl08x_txd *at;
	struct pl08x_driver_data *host;
	enum pl08x_dma_chan_state state;
	bool slave;
	int signal;
	unsigned mux_use;
	unsigned long waiting_at;
};

/**
 * struct pl08x_driver_data - the local state holder for the PL08x
 * @slave: optional slave engine for this instance
 * @memcpy: memcpy engine for this instance
 * @has_slave: the PL08x has a slave engine (routed signals)
 * @base: virtual memory base (remapped) for the PL08x
 * @adev: the corresponding AMBA (PrimeCell) bus entry
 * @vd: vendor data for this PL08x variant
 * @pd: platform data passed in from the platform/machine
 * @phy_chans: array of data for the physical channels
 * @pool: a pool for the LLI descriptors
 * @lli_buses: bitmask to or in to LLI pointer selecting AHB port for LLI
 * fetches
 * @mem_buses: set to indicate memory transfers on AHB2.
 * @lli_words: how many words are used in each LLI item for this variant
 */
struct pl08x_driver_data {
	struct dma_device slave;
	struct dma_device memcpy;
	bool has_slave;
	void __iomem *base;
	struct amba_device *adev;
	const struct vendor_data *vd;
	struct pl08x_platform_data *pd;
	struct pl08x_phy_chan *phy_chans;
	struct dma_pool *pool;
	u8 lli_buses;
	u8 mem_buses;
	u8 lli_words;
};

/*
 * PL08X specific defines
 */

/* The order of words in an LLI. */
#define PL080_LLI_SRC		0
#define PL080_LLI_DST		1
#define PL080_LLI_LLI		2
#define PL080_LLI_CCTL		3
#define PL080S_LLI_CCTL2	4

/* Total words in an LLI. */
#define PL080_LLI_WORDS		4
#define PL080S_LLI_WORDS	8

/*
 * Number of LLIs in each LLI buffer allocated for one transfer
 * (maximum times we call dma_pool_alloc on this pool without freeing)
 */
#define MAX_NUM_TSFR_LLIS	512
#define PL08X_ALIGN		8

static inline struct pl08x_dma_chan *to_pl08x_chan(struct dma_chan *chan)
{
	return container_of(chan, struct pl08x_dma_chan, vc.chan);
}

static inline struct pl08x_txd *to_pl08x_txd(struct dma_async_tx_descriptor *tx)
{
	return container_of(tx, struct pl08x_txd, vd.tx);
}

/*
 * Mux handling.
 *
 * This gives us the DMA request input to the PL08x primecell which the
 * peripheral described by the channel data will be routed to, possibly
 * via a board/SoC specific external MUX.  One important point to note
 * here is that this does not depend on the physical channel.
 */
static int pl08x_request_mux(struct pl08x_dma_chan *plchan)
{
	const struct pl08x_platform_data *pd = plchan->host->pd;
	int ret;

	if (plchan->mux_use++ == 0 && pd->get_xfer_signal) {
		ret = pd->get_xfer_signal(plchan->cd);
		if (ret < 0) {
			plchan->mux_use = 0;
			return ret;
		}

		plchan->signal = ret;
	}
	return 0;
}

static void pl08x_release_mux(struct pl08x_dma_chan *plchan)
{
	const struct pl08x_platform_data *pd = plchan->host->pd;

	if (plchan->signal >= 0) {
		WARN_ON(plchan->mux_use == 0);

		if (--plchan->mux_use == 0 && pd->put_xfer_signal) {
			pd->put_xfer_signal(plchan->cd, plchan->signal);
			plchan->signal = -1;
		}
	}
}

/*
 * Physical channel handling
 */

/* Whether a certain channel is busy or not */
static int pl08x_phy_channel_busy(struct pl08x_phy_chan *ch)
{
	unsigned int val;

	/* If we have a special busy register, take a shortcut */
	if (ch->reg_busy) {
		val = readl(ch->reg_busy);
		return !!(val & BIT(ch->id));
	}
	val = readl(ch->reg_config);
	return val & PL080_CONFIG_ACTIVE;
}

/*
 * pl08x_write_lli() - Write an LLI into the DMA controller.
 *
 * The PL08x derivatives support linked lists, but the first item of the
 * list containing the source, destination, control word and next LLI is
 * ignored. Instead the driver has to write those values directly into the
 * SRC, DST, LLI and control registers. On FTDMAC020 also the SIZE
 * register need to be set up for the first transfer.
 */
static void pl08x_write_lli(struct pl08x_driver_data *pl08x,
		struct pl08x_phy_chan *phychan, const u32 *lli, u32 ccfg)
{
	if (pl08x->vd->pl080s)
		dev_vdbg(&pl08x->adev->dev,
			"WRITE channel %d: csrc=0x%08x, cdst=0x%08x, "
			"clli=0x%08x, cctl=0x%08x, cctl2=0x%08x, ccfg=0x%08x\n",
			phychan->id, lli[PL080_LLI_SRC], lli[PL080_LLI_DST],
			lli[PL080_LLI_LLI], lli[PL080_LLI_CCTL],
			lli[PL080S_LLI_CCTL2], ccfg);
	else
		dev_vdbg(&pl08x->adev->dev,
			"WRITE channel %d: csrc=0x%08x, cdst=0x%08x, "
			"clli=0x%08x, cctl=0x%08x, ccfg=0x%08x\n",
			phychan->id, lli[PL080_LLI_SRC], lli[PL080_LLI_DST],
			lli[PL080_LLI_LLI], lli[PL080_LLI_CCTL], ccfg);

	writel_relaxed(lli[PL080_LLI_SRC], phychan->reg_src);
	writel_relaxed(lli[PL080_LLI_DST], phychan->reg_dst);
	writel_relaxed(lli[PL080_LLI_LLI], phychan->reg_lli);

	/*
	 * The FTMAC020 has a different layout in the CCTL word of the LLI
	 * and the CCTL register which is split in CSR and SIZE registers.
	 * Convert the LLI item CCTL into the proper values to write into
	 * the CSR and SIZE registers.
	 */
	if (phychan->ftdmac020) {
		u32 llictl = lli[PL080_LLI_CCTL];
		u32 val = 0;

		/* Write the transfer size (12 bits) to the size register */
		writel_relaxed(llictl & FTDMAC020_LLI_TRANSFER_SIZE_MASK,
			       phychan->base + FTDMAC020_CH_SIZE);
		/*
		 * Then write the control bits 28..16 to the control register
		 * by shuffleing the bits around to where they are in the
		 * main register. The mapping is as follows:
		 * Bit 28: TC_MSK - mask on all except last LLI
		 * Bit 27..25: SRC_WIDTH
		 * Bit 24..22: DST_WIDTH
		 * Bit 21..20: SRCAD_CTRL
		 * Bit 19..17: DSTAD_CTRL
		 * Bit 17: SRC_SEL
		 * Bit 16: DST_SEL
		 */
		if (llictl & FTDMAC020_LLI_TC_MSK)
			val |= FTDMAC020_CH_CSR_TC_MSK;
		val |= ((llictl  & FTDMAC020_LLI_SRC_WIDTH_MSK) >>
			(FTDMAC020_LLI_SRC_WIDTH_SHIFT -
			 FTDMAC020_CH_CSR_SRC_WIDTH_SHIFT));
		val |= ((llictl  & FTDMAC020_LLI_DST_WIDTH_MSK) >>
			(FTDMAC020_LLI_DST_WIDTH_SHIFT -
			 FTDMAC020_CH_CSR_DST_WIDTH_SHIFT));
		val |= ((llictl  & FTDMAC020_LLI_SRCAD_CTL_MSK) >>
			(FTDMAC020_LLI_SRCAD_CTL_SHIFT -
			 FTDMAC020_CH_CSR_SRCAD_CTL_SHIFT));
		val |= ((llictl  & FTDMAC020_LLI_DSTAD_CTL_MSK) >>
			(FTDMAC020_LLI_DSTAD_CTL_SHIFT -
			 FTDMAC020_CH_CSR_DSTAD_CTL_SHIFT));
		if (llictl & FTDMAC020_LLI_SRC_SEL)
			val |= FTDMAC020_CH_CSR_SRC_SEL;
		if (llictl & FTDMAC020_LLI_DST_SEL)
			val |= FTDMAC020_CH_CSR_DST_SEL;

		/*
		 * Set up the bits that exist in the CSR but are not
		 * part the LLI, i.e. only gets written to the control
		 * register right here.
		 *
		 * FIXME: do not just handle memcpy, also handle slave DMA.
		 */
		switch (pl08x->pd->memcpy_burst_size) {
		default:
		case PL08X_BURST_SZ_1:
			val |= PL080_BSIZE_1 <<
				FTDMAC020_CH_CSR_SRC_SIZE_SHIFT;
			break;
		case PL08X_BURST_SZ_4:
			val |= PL080_BSIZE_4 <<
				FTDMAC020_CH_CSR_SRC_SIZE_SHIFT;
			break;
		case PL08X_BURST_SZ_8:
			val |= PL080_BSIZE_8 <<
				FTDMAC020_CH_CSR_SRC_SIZE_SHIFT;
			break;
		case PL08X_BURST_SZ_16:
			val |= PL080_BSIZE_16 <<
				FTDMAC020_CH_CSR_SRC_SIZE_SHIFT;
			break;
		case PL08X_BURST_SZ_32:
			val |= PL080_BSIZE_32 <<
				FTDMAC020_CH_CSR_SRC_SIZE_SHIFT;
			break;
		case PL08X_BURST_SZ_64:
			val |= PL080_BSIZE_64 <<
				FTDMAC020_CH_CSR_SRC_SIZE_SHIFT;
			break;
		case PL08X_BURST_SZ_128:
			val |= PL080_BSIZE_128 <<
				FTDMAC020_CH_CSR_SRC_SIZE_SHIFT;
			break;
		case PL08X_BURST_SZ_256:
			val |= PL080_BSIZE_256 <<
				FTDMAC020_CH_CSR_SRC_SIZE_SHIFT;
			break;
		}

		/* Protection flags */
		if (pl08x->pd->memcpy_prot_buff)
			val |= FTDMAC020_CH_CSR_PROT2;
		if (pl08x->pd->memcpy_prot_cache)
			val |= FTDMAC020_CH_CSR_PROT3;
		/* We are the kernel, so we are in privileged mode */
		val |= FTDMAC020_CH_CSR_PROT1;

		writel_relaxed(val, phychan->reg_control);
	} else {
		/* Bits are just identical */
		writel_relaxed(lli[PL080_LLI_CCTL], phychan->reg_control);
	}

	/* Second control word on the PL080s */
	if (pl08x->vd->pl080s)
		writel_relaxed(lli[PL080S_LLI_CCTL2],
				phychan->base + PL080S_CH_CONTROL2);

	writel(ccfg, phychan->reg_config);
}

/*
 * Set the initial DMA register values i.e. those for the first LLI
 * The next LLI pointer and the configuration interrupt bit have
 * been set when the LLIs were constructed.  Poke them into the hardware
 * and start the transfer.
 */
static void pl08x_start_next_txd(struct pl08x_dma_chan *plchan)
{
	struct pl08x_driver_data *pl08x = plchan->host;
	struct pl08x_phy_chan *phychan = plchan->phychan;
	struct virt_dma_desc *vd = vchan_next_desc(&plchan->vc);
	struct pl08x_txd *txd = to_pl08x_txd(&vd->tx);
	u32 val;

	list_del(&txd->vd.node);

	plchan->at = txd;

	/* Wait for channel inactive */
	while (pl08x_phy_channel_busy(phychan))
		cpu_relax();

	pl08x_write_lli(pl08x, phychan, &txd->llis_va[0], txd->ccfg);

	/* Enable the DMA channel */
	/* Do not access config register until channel shows as disabled */
	while (readl(pl08x->base + PL080_EN_CHAN) & BIT(phychan->id))
		cpu_relax();

	/* Do not access config register until channel shows as inactive */
	if (phychan->ftdmac020) {
		val = readl(phychan->reg_config);
		while (val & FTDMAC020_CH_CFG_BUSY)
			val = readl(phychan->reg_config);

		val = readl(phychan->reg_control);
		while (val & FTDMAC020_CH_CSR_EN)
			val = readl(phychan->reg_control);

		writel(val | FTDMAC020_CH_CSR_EN,
		       phychan->reg_control);
	} else {
		val = readl(phychan->reg_config);
		while ((val & PL080_CONFIG_ACTIVE) ||
		       (val & PL080_CONFIG_ENABLE))
			val = readl(phychan->reg_config);

		writel(val | PL080_CONFIG_ENABLE, phychan->reg_config);
	}
}

/*
 * Pause the channel by setting the HALT bit.
 *
 * For M->P transfers, pause the DMAC first and then stop the peripheral -
 * the FIFO can only drain if the peripheral is still requesting data.
 * (note: this can still timeout if the DMAC FIFO never drains of data.)
 *
 * For P->M transfers, disable the peripheral first to stop it filling
 * the DMAC FIFO, and then pause the DMAC.
 */
static void pl08x_pause_phy_chan(struct pl08x_phy_chan *ch)
{
	u32 val;
	int timeout;

	if (ch->ftdmac020) {
		/* Use the enable bit on the FTDMAC020 */
		val = readl(ch->reg_control);
		val &= ~FTDMAC020_CH_CSR_EN;
		writel(val, ch->reg_control);
		return;
	}

	/* Set the HALT bit and wait for the FIFO to drain */
	val = readl(ch->reg_config);
	val |= PL080_CONFIG_HALT;
	writel(val, ch->reg_config);

	/* Wait for channel inactive */
	for (timeout = 1000; timeout; timeout--) {
		if (!pl08x_phy_channel_busy(ch))
			break;
		udelay(1);
	}
	if (pl08x_phy_channel_busy(ch))
		pr_err("pl08x: channel%u timeout waiting for pause\n", ch->id);
}

static void pl08x_resume_phy_chan(struct pl08x_phy_chan *ch)
{
	u32 val;

	/* Use the enable bit on the FTDMAC020 */
	if (ch->ftdmac020) {
		val = readl(ch->reg_control);
		val |= FTDMAC020_CH_CSR_EN;
		writel(val, ch->reg_control);
		return;
	}

	/* Clear the HALT bit */
	val = readl(ch->reg_config);
	val &= ~PL080_CONFIG_HALT;
	writel(val, ch->reg_config);
}

/*
 * pl08x_terminate_phy_chan() stops the channel, clears the FIFO and
 * clears any pending interrupt status.  This should not be used for
 * an on-going transfer, but as a method of shutting down a channel
 * (eg, when it's no longer used) or terminating a transfer.
 */
static void pl08x_terminate_phy_chan(struct pl08x_driver_data *pl08x,
	struct pl08x_phy_chan *ch)
{
	u32 val;

	/* The layout for the FTDMAC020 is different */
	if (ch->ftdmac020) {
		/* Disable all interrupts */
		val = readl(ch->reg_config);
		val |= (FTDMAC020_CH_CFG_INT_ABT_MASK |
			FTDMAC020_CH_CFG_INT_ERR_MASK |
			FTDMAC020_CH_CFG_INT_TC_MASK);
		writel(val, ch->reg_config);

		/* Abort and disable channel */
		val = readl(ch->reg_control);
		val &= ~FTDMAC020_CH_CSR_EN;
		val |= FTDMAC020_CH_CSR_ABT;
		writel(val, ch->reg_control);

		/* Clear ABT and ERR interrupt flags */
		writel(BIT(ch->id) | BIT(ch->id + 16),
		       pl08x->base + PL080_ERR_CLEAR);
		writel(BIT(ch->id), pl08x->base + PL080_TC_CLEAR);

		return;
	}

	val = readl(ch->reg_config);
	val &= ~(PL080_CONFIG_ENABLE | PL080_CONFIG_ERR_IRQ_MASK |
		 PL080_CONFIG_TC_IRQ_MASK);
	writel(val, ch->reg_config);

	writel(BIT(ch->id), pl08x->base + PL080_ERR_CLEAR);
	writel(BIT(ch->id), pl08x->base + PL080_TC_CLEAR);
}

static u32 get_bytes_in_phy_channel(struct pl08x_phy_chan *ch)
{
	u32 val;
	u32 bytes;

	if (ch->ftdmac020) {
		bytes = readl(ch->base + FTDMAC020_CH_SIZE);

		val = readl(ch->reg_control);
		val &= FTDMAC020_CH_CSR_SRC_WIDTH_MSK;
		val >>= FTDMAC020_CH_CSR_SRC_WIDTH_SHIFT;
	} else if (ch->pl080s) {
		val = readl(ch->base + PL080S_CH_CONTROL2);
		bytes = val & PL080S_CONTROL_TRANSFER_SIZE_MASK;

		val = readl(ch->reg_control);
		val &= PL080_CONTROL_SWIDTH_MASK;
		val >>= PL080_CONTROL_SWIDTH_SHIFT;
	} else {
		/* Plain PL08x */
		val = readl(ch->reg_control);
		bytes = val & PL080_CONTROL_TRANSFER_SIZE_MASK;

		val &= PL080_CONTROL_SWIDTH_MASK;
		val >>= PL080_CONTROL_SWIDTH_SHIFT;
	}

	switch (val) {
	case PL080_WIDTH_8BIT:
		break;
	case PL080_WIDTH_16BIT:
		bytes *= 2;
		break;
	case PL080_WIDTH_32BIT:
		bytes *= 4;
		break;
	}
	return bytes;
}

static u32 get_bytes_in_lli(struct pl08x_phy_chan *ch, const u32 *llis_va)
{
	u32 val;
	u32 bytes;

	if (ch->ftdmac020) {
		val = llis_va[PL080_LLI_CCTL];
		bytes = val & FTDMAC020_LLI_TRANSFER_SIZE_MASK;

		val = llis_va[PL080_LLI_CCTL];
		val &= FTDMAC020_LLI_SRC_WIDTH_MSK;
		val >>= FTDMAC020_LLI_SRC_WIDTH_SHIFT;
	} else if (ch->pl080s) {
		val = llis_va[PL080S_LLI_CCTL2];
		bytes = val & PL080S_CONTROL_TRANSFER_SIZE_MASK;

		val = llis_va[PL080_LLI_CCTL];
		val &= PL080_CONTROL_SWIDTH_MASK;
		val >>= PL080_CONTROL_SWIDTH_SHIFT;
	} else {
		/* Plain PL08x */
		val = llis_va[PL080_LLI_CCTL];
		bytes = val & PL080_CONTROL_TRANSFER_SIZE_MASK;

		val &= PL080_CONTROL_SWIDTH_MASK;
		val >>= PL080_CONTROL_SWIDTH_SHIFT;
	}

	switch (val) {
	case PL080_WIDTH_8BIT:
		break;
	case PL080_WIDTH_16BIT:
		bytes *= 2;
		break;
	case PL080_WIDTH_32BIT:
		bytes *= 4;
		break;
	}
	return bytes;
}

/* The channel should be paused when calling this */
static u32 pl08x_getbytes_chan(struct pl08x_dma_chan *plchan)
{
	struct pl08x_driver_data *pl08x = plchan->host;
	const u32 *llis_va, *llis_va_limit;
	struct pl08x_phy_chan *ch;
	dma_addr_t llis_bus;
	struct pl08x_txd *txd;
	u32 llis_max_words;
	size_t bytes;
	u32 clli;

	ch = plchan->phychan;
	txd = plchan->at;

	if (!ch || !txd)
		return 0;

	/*
	 * Follow the LLIs to get the number of remaining
	 * bytes in the currently active transaction.
	 */
	clli = readl(ch->reg_lli) & ~PL080_LLI_LM_AHB2;

	/* First get the remaining bytes in the active transfer */
	bytes = get_bytes_in_phy_channel(ch);

	if (!clli)
		return bytes;

	llis_va = txd->llis_va;
	llis_bus = txd->llis_bus;

	llis_max_words = pl08x->lli_words * MAX_NUM_TSFR_LLIS;
	BUG_ON(clli < llis_bus || clli >= llis_bus +
						sizeof(u32) * llis_max_words);

	/*
	 * Locate the next LLI - as this is an array,
	 * it's simple maths to find.
	 */
	llis_va += (clli - llis_bus) / sizeof(u32);

	llis_va_limit = llis_va + llis_max_words;

	for (; llis_va < llis_va_limit; llis_va += pl08x->lli_words) {
		bytes += get_bytes_in_lli(ch, llis_va);

		/*
		 * A LLI pointer going backward terminates the LLI list
		 */
		if (llis_va[PL080_LLI_LLI] <= clli)
			break;
	}

	return bytes;
}

/*
 * Allocate a physical channel for a virtual channel
 *
 * Try to locate a physical channel to be used for this transfer. If all
 * are taken return NULL and the requester will have to cope by using
 * some fallback PIO mode or retrying later.
 */
static struct pl08x_phy_chan *
pl08x_get_phy_channel(struct pl08x_driver_data *pl08x,
		      struct pl08x_dma_chan *virt_chan)
{
	struct pl08x_phy_chan *ch = NULL;
	unsigned long flags;
	int i;

	for (i = 0; i < pl08x->vd->channels; i++) {
		ch = &pl08x->phy_chans[i];

		spin_lock_irqsave(&ch->lock, flags);

		if (!ch->locked && !ch->serving) {
			ch->serving = virt_chan;
			spin_unlock_irqrestore(&ch->lock, flags);
			break;
		}

		spin_unlock_irqrestore(&ch->lock, flags);
	}

	if (i == pl08x->vd->channels) {
		/* No physical channel available, cope with it */
		return NULL;
	}

	return ch;
}

/* Mark the physical channel as free.  Note, this write is atomic. */
static inline void pl08x_put_phy_channel(struct pl08x_driver_data *pl08x,
					 struct pl08x_phy_chan *ch)
{
	ch->serving = NULL;
}

/*
 * Try to allocate a physical channel.  When successful, assign it to
 * this virtual channel, and initiate the next descriptor.  The
 * virtual channel lock must be held at this point.
 */
static void pl08x_phy_alloc_and_start(struct pl08x_dma_chan *plchan)
{
	struct pl08x_driver_data *pl08x = plchan->host;
	struct pl08x_phy_chan *ch;

	ch = pl08x_get_phy_channel(pl08x, plchan);
	if (!ch) {
		dev_dbg(&pl08x->adev->dev, "no physical channel available for xfer on %s\n", plchan->name);
		plchan->state = PL08X_CHAN_WAITING;
		plchan->waiting_at = jiffies;
		return;
	}

	dev_dbg(&pl08x->adev->dev, "allocated physical channel %d for xfer on %s\n",
		ch->id, plchan->name);

	plchan->phychan = ch;
	plchan->state = PL08X_CHAN_RUNNING;
	pl08x_start_next_txd(plchan);
}

static void pl08x_phy_reassign_start(struct pl08x_phy_chan *ch,
	struct pl08x_dma_chan *plchan)
{
	struct pl08x_driver_data *pl08x = plchan->host;

	dev_dbg(&pl08x->adev->dev, "reassigned physical channel %d for xfer on %s\n",
		ch->id, plchan->name);

	/*
	 * We do this without taking the lock; we're really only concerned
	 * about whether this pointer is NULL or not, and we're guaranteed
	 * that this will only be called when it _already_ is non-NULL.
	 */
	ch->serving = plchan;
	plchan->phychan = ch;
	plchan->state = PL08X_CHAN_RUNNING;
	pl08x_start_next_txd(plchan);
}

/*
 * Free a physical DMA channel, potentially reallocating it to another
 * virtual channel if we have any pending.
 */
static void pl08x_phy_free(struct pl08x_dma_chan *plchan)
{
	struct pl08x_driver_data *pl08x = plchan->host;
	struct pl08x_dma_chan *p, *next;
	unsigned long waiting_at;
 retry:
	next = NULL;
	waiting_at = jiffies;

	/*
	 * Find a waiting virtual channel for the next transfer.
	 * To be fair, time when each channel reached waiting state is compared
	 * to select channel that is waiting for the longest time.
	 */
	list_for_each_entry(p, &pl08x->memcpy.channels, vc.chan.device_node)
		if (p->state == PL08X_CHAN_WAITING &&
		    p->waiting_at <= waiting_at) {
			next = p;
			waiting_at = p->waiting_at;
		}

	if (!next && pl08x->has_slave) {
		list_for_each_entry(p, &pl08x->slave.channels, vc.chan.device_node)
			if (p->state == PL08X_CHAN_WAITING &&
			    p->waiting_at <= waiting_at) {
				next = p;
				waiting_at = p->waiting_at;
			}
	}

	/* Ensure that the physical channel is stopped */
	pl08x_terminate_phy_chan(pl08x, plchan->phychan);

	if (next) {
		bool success;

		/*
		 * Eww.  We know this isn't going to deadlock
		 * but lockdep probably doesn't.
		 */
		spin_lock(&next->vc.lock);
		/* Re-check the state now that we have the lock */
		success = next->state == PL08X_CHAN_WAITING;
		if (success)
			pl08x_phy_reassign_start(plchan->phychan, next);
		spin_unlock(&next->vc.lock);

		/* If the state changed, try to find another channel */
		if (!success)
			goto retry;
	} else {
		/* No more jobs, so free up the physical channel */
		pl08x_put_phy_channel(pl08x, plchan->phychan);
	}

	plchan->phychan = NULL;
	plchan->state = PL08X_CHAN_IDLE;
}

/*
 * LLI handling
 */

static inline unsigned int
pl08x_get_bytes_for_lli(struct pl08x_driver_data *pl08x,
			u32 cctl,
			bool source)
{
	u32 val;

	if (pl08x->vd->ftdmac020) {
		if (source)
			val = (cctl & FTDMAC020_LLI_SRC_WIDTH_MSK) >>
				FTDMAC020_LLI_SRC_WIDTH_SHIFT;
		else
			val = (cctl & FTDMAC020_LLI_DST_WIDTH_MSK) >>
				FTDMAC020_LLI_DST_WIDTH_SHIFT;
	} else {
		if (source)
			val = (cctl & PL080_CONTROL_SWIDTH_MASK) >>
				PL080_CONTROL_SWIDTH_SHIFT;
		else
			val = (cctl & PL080_CONTROL_DWIDTH_MASK) >>
				PL080_CONTROL_DWIDTH_SHIFT;
	}

	switch (val) {
	case PL080_WIDTH_8BIT:
		return 1;
	case PL080_WIDTH_16BIT:
		return 2;
	case PL080_WIDTH_32BIT:
		return 4;
	default:
		break;
	}
	BUG();
	return 0;
}

static inline u32 pl08x_lli_control_bits(struct pl08x_driver_data *pl08x,
					 u32 cctl,
					 u8 srcwidth, u8 dstwidth,
					 size_t tsize)
{
	u32 retbits = cctl;

	/*
	 * Remove all src, dst and transfer size bits, then set the
	 * width and size according to the parameters. The bit offsets
	 * are different in the FTDMAC020 so we need to accound for this.
	 */
	if (pl08x->vd->ftdmac020) {
		retbits &= ~FTDMAC020_LLI_DST_WIDTH_MSK;
		retbits &= ~FTDMAC020_LLI_SRC_WIDTH_MSK;
		retbits &= ~FTDMAC020_LLI_TRANSFER_SIZE_MASK;

		switch (srcwidth) {
		case 1:
			retbits |= PL080_WIDTH_8BIT <<
				FTDMAC020_LLI_SRC_WIDTH_SHIFT;
			break;
		case 2:
			retbits |= PL080_WIDTH_16BIT <<
				FTDMAC020_LLI_SRC_WIDTH_SHIFT;
			break;
		case 4:
			retbits |= PL080_WIDTH_32BIT <<
				FTDMAC020_LLI_SRC_WIDTH_SHIFT;
			break;
		default:
			BUG();
			break;
		}

		switch (dstwidth) {
		case 1:
			retbits |= PL080_WIDTH_8BIT <<
				FTDMAC020_LLI_DST_WIDTH_SHIFT;
			break;
		case 2:
			retbits |= PL080_WIDTH_16BIT <<
				FTDMAC020_LLI_DST_WIDTH_SHIFT;
			break;
		case 4:
			retbits |= PL080_WIDTH_32BIT <<
				FTDMAC020_LLI_DST_WIDTH_SHIFT;
			break;
		default:
			BUG();
			break;
		}

		tsize &= FTDMAC020_LLI_TRANSFER_SIZE_MASK;
		retbits |= tsize << FTDMAC020_LLI_TRANSFER_SIZE_SHIFT;
	} else {
		retbits &= ~PL080_CONTROL_DWIDTH_MASK;
		retbits &= ~PL080_CONTROL_SWIDTH_MASK;
		retbits &= ~PL080_CONTROL_TRANSFER_SIZE_MASK;

		switch (srcwidth) {
		case 1:
			retbits |= PL080_WIDTH_8BIT <<
				PL080_CONTROL_SWIDTH_SHIFT;
			break;
		case 2:
			retbits |= PL080_WIDTH_16BIT <<
				PL080_CONTROL_SWIDTH_SHIFT;
			break;
		case 4:
			retbits |= PL080_WIDTH_32BIT <<
				PL080_CONTROL_SWIDTH_SHIFT;
			break;
		default:
			BUG();
			break;
		}

		switch (dstwidth) {
		case 1:
			retbits |= PL080_WIDTH_8BIT <<
				PL080_CONTROL_DWIDTH_SHIFT;
			break;
		case 2:
			retbits |= PL080_WIDTH_16BIT <<
				PL080_CONTROL_DWIDTH_SHIFT;
			break;
		case 4:
			retbits |= PL080_WIDTH_32BIT <<
				PL080_CONTROL_DWIDTH_SHIFT;
			break;
		default:
			BUG();
			break;
		}

		tsize &= PL080_CONTROL_TRANSFER_SIZE_MASK;
		retbits |= tsize << PL080_CONTROL_TRANSFER_SIZE_SHIFT;
	}

	return retbits;
}

struct pl08x_lli_build_data {
	struct pl08x_txd *txd;
	struct pl08x_bus_data srcbus;
	struct pl08x_bus_data dstbus;
	size_t remainder;
	u32 lli_bus;
};

/*
 * Autoselect a master bus to use for the transfer. Slave will be the chosen as
 * victim in case src & dest are not similarly aligned. i.e. If after aligning
 * masters address with width requirements of transfer (by sending few byte by
 * byte data), slave is still not aligned, then its width will be reduced to
 * BYTE.
 * - prefers the destination bus if both available
 * - prefers bus with fixed address (i.e. peripheral)
 */
static void pl08x_choose_master_bus(struct pl08x_driver_data *pl08x,
				    struct pl08x_lli_build_data *bd,
				    struct pl08x_bus_data **mbus,
				    struct pl08x_bus_data **sbus,
				    u32 cctl)
{
	bool dst_incr;
	bool src_incr;

	/*
	 * The FTDMAC020 only supports memory-to-memory transfer, so
	 * source and destination always increase.
	 */
	if (pl08x->vd->ftdmac020) {
		dst_incr = true;
		src_incr = true;
	} else {
		dst_incr = !!(cctl & PL080_CONTROL_DST_INCR);
		src_incr = !!(cctl & PL080_CONTROL_SRC_INCR);
	}

	/*
	 * If either bus is not advancing, i.e. it is a peripheral, that
	 * one becomes master
	 */
	if (!dst_incr) {
		*mbus = &bd->dstbus;
		*sbus = &bd->srcbus;
	} else if (!src_incr) {
		*mbus = &bd->srcbus;
		*sbus = &bd->dstbus;
	} else {
		if (bd->dstbus.buswidth >= bd->srcbus.buswidth) {
			*mbus = &bd->dstbus;
			*sbus = &bd->srcbus;
		} else {
			*mbus = &bd->srcbus;
			*sbus = &bd->dstbus;
		}
	}
}

/*
 * Fills in one LLI for a certain transfer descriptor and advance the counter
 */
static void pl08x_fill_lli_for_desc(struct pl08x_driver_data *pl08x,
				    struct pl08x_lli_build_data *bd,
				    int num_llis, int len, u32 cctl, u32 cctl2)
{
	u32 offset = num_llis * pl08x->lli_words;
	u32 *llis_va = bd->txd->llis_va + offset;
	dma_addr_t llis_bus = bd->txd->llis_bus;

	BUG_ON(num_llis >= MAX_NUM_TSFR_LLIS);

	/* Advance the offset to next LLI. */
	offset += pl08x->lli_words;

	llis_va[PL080_LLI_SRC] = bd->srcbus.addr;
	llis_va[PL080_LLI_DST] = bd->dstbus.addr;
	llis_va[PL080_LLI_LLI] = (llis_bus + sizeof(u32) * offset);
	llis_va[PL080_LLI_LLI] |= bd->lli_bus;
	llis_va[PL080_LLI_CCTL] = cctl;
	if (pl08x->vd->pl080s)
		llis_va[PL080S_LLI_CCTL2] = cctl2;

	if (pl08x->vd->ftdmac020) {
		/* FIXME: only memcpy so far so both increase */
		bd->srcbus.addr += len;
		bd->dstbus.addr += len;
	} else {
		if (cctl & PL080_CONTROL_SRC_INCR)
			bd->srcbus.addr += len;
		if (cctl & PL080_CONTROL_DST_INCR)
			bd->dstbus.addr += len;
	}

	BUG_ON(bd->remainder < len);

	bd->remainder -= len;
}

static inline void prep_byte_width_lli(struct pl08x_driver_data *pl08x,
			struct pl08x_lli_build_data *bd, u32 *cctl, u32 len,
			int num_llis, size_t *total_bytes)
{
	*cctl = pl08x_lli_control_bits(pl08x, *cctl, 1, 1, len);
	pl08x_fill_lli_for_desc(pl08x, bd, num_llis, len, *cctl, len);
	(*total_bytes) += len;
}

#if 1
static void pl08x_dump_lli(struct pl08x_driver_data *pl08x,
			   const u32 *llis_va, int num_llis)
{
	int i;

	if (pl08x->vd->pl080s) {
		dev_vdbg(&pl08x->adev->dev,
			"%-3s %-9s  %-10s %-10s %-10s %-10s %s\n",
			"lli", "", "csrc", "cdst", "clli", "cctl", "cctl2");
		for (i = 0; i < num_llis; i++) {
			dev_vdbg(&pl08x->adev->dev,
				"%3d @%p: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
				i, llis_va, llis_va[PL080_LLI_SRC],
				llis_va[PL080_LLI_DST], llis_va[PL080_LLI_LLI],
				llis_va[PL080_LLI_CCTL],
				llis_va[PL080S_LLI_CCTL2]);
			llis_va += pl08x->lli_words;
		}
	} else {
		dev_vdbg(&pl08x->adev->dev,
			"%-3s %-9s  %-10s %-10s %-10s %s\n",
			"lli", "", "csrc", "cdst", "clli", "cctl");
		for (i = 0; i < num_llis; i++) {
			dev_vdbg(&pl08x->adev->dev,
				"%3d @%p: 0x%08x 0x%08x 0x%08x 0x%08x\n",
				i, llis_va, llis_va[PL080_LLI_SRC],
				llis_va[PL080_LLI_DST], llis_va[PL080_LLI_LLI],
				llis_va[PL080_LLI_CCTL]);
			llis_va += pl08x->lli_words;
		}
	}
}
#else
static inline void pl08x_dump_lli(struct pl08x_driver_data *pl08x,
				  const u32 *llis_va, int num_llis) {}
#endif

/*
 * This fills in the table of LLIs for the transfer descriptor
 * Note that we assume we never have to change the burst sizes
 * Return 0 for error
 */
static int pl08x_fill_llis_for_desc(struct pl08x_driver_data *pl08x,
			      struct pl08x_txd *txd)
{
	struct pl08x_bus_data *mbus, *sbus;
	struct pl08x_lli_build_data bd;
	int num_llis = 0;
	u32 cctl, early_bytes = 0;
	size_t max_bytes_per_lli, total_bytes;
	u32 *llis_va, *last_lli;
	struct pl08x_sg *dsg;

	txd->llis_va = dma_pool_alloc(pl08x->pool, GFP_NOWAIT, &txd->llis_bus);
	if (!txd->llis_va) {
		dev_err(&pl08x->adev->dev, "%s no memory for llis\n", __func__);
		return 0;
	}

	bd.txd = txd;
	bd.lli_bus = (pl08x->lli_buses & PL08X_AHB2) ? PL080_LLI_LM_AHB2 : 0;
	cctl = txd->cctl;

	/* Find maximum width of the source bus */
	bd.srcbus.maxwidth = pl08x_get_bytes_for_lli(pl08x, cctl, true);

	/* Find maximum width of the destination bus */
	bd.dstbus.maxwidth = pl08x_get_bytes_for_lli(pl08x, cctl, false);

	list_for_each_entry(dsg, &txd->dsg_list, node) {
		total_bytes = 0;
		cctl = txd->cctl;

		bd.srcbus.addr = dsg->src_addr;
		bd.dstbus.addr = dsg->dst_addr;
		bd.remainder = dsg->len;
		bd.srcbus.buswidth = bd.srcbus.maxwidth;
		bd.dstbus.buswidth = bd.dstbus.maxwidth;

		pl08x_choose_master_bus(pl08x, &bd, &mbus, &sbus, cctl);

		dev_vdbg(&pl08x->adev->dev,
			"src=0x%08llx%s/%u dst=0x%08llx%s/%u len=%zu\n",
			(u64)bd.srcbus.addr,
			cctl & PL080_CONTROL_SRC_INCR ? "+" : "",
			bd.srcbus.buswidth,
			(u64)bd.dstbus.addr,
			cctl & PL080_CONTROL_DST_INCR ? "+" : "",
			bd.dstbus.buswidth,
			bd.remainder);
		dev_vdbg(&pl08x->adev->dev, "mbus=%s sbus=%s\n",
			mbus == &bd.srcbus ? "src" : "dst",
			sbus == &bd.srcbus ? "src" : "dst");

		/*
		 * Zero length is only allowed if all these requirements are
		 * met:
		 * - flow controller is peripheral.
		 * - src.addr is aligned to src.width
		 * - dst.addr is aligned to dst.width
		 *
		 * sg_len == 1 should be true, as there can be two cases here:
		 *
		 * - Memory addresses are contiguous and are not scattered.
		 *   Here, Only one sg will be passed by user driver, with
		 *   memory address and zero length. We pass this to controller
		 *   and after the transfer it will receive the last burst
		 *   request from peripheral and so transfer finishes.
		 *
		 * - Memory addresses are scattered and are not contiguous.
		 *   Here, Obviously as DMA controller doesn't know when a lli's
		 *   transfer gets over, it can't load next lli. So in this
		 *   case, there has to be an assumption that only one lli is
		 *   supported. Thus, we can't have scattered addresses.
		 */
		if (!bd.remainder) {
			u32 fc;

			/* FTDMAC020 only does memory-to-memory */
			if (pl08x->vd->ftdmac020)
				fc = PL080_FLOW_MEM2MEM;
			else
				fc = (txd->ccfg & PL080_CONFIG_FLOW_CONTROL_MASK) >>
					PL080_CONFIG_FLOW_CONTROL_SHIFT;
			if (!((fc >= PL080_FLOW_SRC2DST_DST) &&
					(fc <= PL080_FLOW_SRC2DST_SRC))) {
				dev_err(&pl08x->adev->dev, "%s sg len can't be zero",
					__func__);
				return 0;
			}

			if (!IS_BUS_ALIGNED(&bd.srcbus) ||
				!IS_BUS_ALIGNED(&bd.dstbus)) {
				dev_err(&pl08x->adev->dev,
					"%s src & dst address must be aligned to src"
					" & dst width if peripheral is flow controller",
					__func__);
				return 0;
			}

			cctl = pl08x_lli_control_bits(pl08x, cctl,
					bd.srcbus.buswidth, bd.dstbus.buswidth,
					0);
			pl08x_fill_lli_for_desc(pl08x, &bd, num_llis++,
					0, cctl, 0);
			break;
		}

		/*
		 * Send byte by byte for following cases
		 * - Less than a bus width available
		 * - until master bus is aligned
		 */
		if (bd.remainder < mbus->buswidth)
			early_bytes = bd.remainder;
		else if (!IS_BUS_ALIGNED(mbus)) {
			early_bytes = mbus->buswidth -
				(mbus->addr & (mbus->buswidth - 1));
			if ((bd.remainder - early_bytes) < mbus->buswidth)
				early_bytes = bd.remainder;
		}

		if (early_bytes) {
			dev_vdbg(&pl08x->adev->dev,
				"%s byte width LLIs (remain 0x%08zx)\n",
				__func__, bd.remainder);
			prep_byte_width_lli(pl08x, &bd, &cctl, early_bytes,
				num_llis++, &total_bytes);
		}

		if (bd.remainder) {
			/*
			 * Master now aligned
			 * - if slave is not then we must set its width down
			 */
			if (!IS_BUS_ALIGNED(sbus)) {
				dev_dbg(&pl08x->adev->dev,
					"%s set down bus width to one byte\n",
					__func__);

				sbus->buswidth = 1;
			}

			/*
			 * Bytes transferred = tsize * src width, not
			 * MIN(buswidths)
			 */
			max_bytes_per_lli = bd.srcbus.buswidth *
						pl08x->vd->max_transfer_size;
			dev_vdbg(&pl08x->adev->dev,
				"%s max bytes per lli = %zu\n",
				__func__, max_bytes_per_lli);

			/*
			 * Make largest possible LLIs until less than one bus
			 * width left
			 */
			while (bd.remainder > (mbus->buswidth - 1)) {
				size_t lli_len, tsize, width;

				/*
				 * If enough left try to send max possible,
				 * otherwise try to send the remainder
				 */
				lli_len = min(bd.remainder, max_bytes_per_lli);

				/*
				 * Check against maximum bus alignment:
				 * Calculate actual transfer size in relation to
				 * bus width an get a maximum remainder of the
				 * highest bus width - 1
				 */
				width = max(mbus->buswidth, sbus->buswidth);
				lli_len = (lli_len / width) * width;
				tsize = lli_len / bd.srcbus.buswidth;

				dev_vdbg(&pl08x->adev->dev,
					"%s fill lli with single lli chunk of "
					"size 0x%08zx (remainder 0x%08zx)\n",
					__func__, lli_len, bd.remainder);

				cctl = pl08x_lli_control_bits(pl08x, cctl,
					bd.srcbus.buswidth, bd.dstbus.buswidth,
					tsize);
				pl08x_fill_lli_for_desc(pl08x, &bd, num_llis++,
						lli_len, cctl, tsize);
				total_bytes += lli_len;
			}

			/*
			 * Send any odd bytes
			 */
			if (bd.remainder) {
				dev_vdbg(&pl08x->adev->dev,
					"%s align with boundary, send odd bytes (remain %zu)\n",
					__func__, bd.remainder);
				prep_byte_width_lli(pl08x, &bd, &cctl,
					bd.remainder, num_llis++, &total_bytes);
			}
		}

		if (total_bytes != dsg->len) {
			dev_err(&pl08x->adev->dev,
				"%s size of encoded lli:s don't match total txd, transferred 0x%08zx from size 0x%08zx\n",
				__func__, total_bytes, dsg->len);
			return 0;
		}

		if (num_llis >= MAX_NUM_TSFR_LLIS) {
			dev_err(&pl08x->adev->dev,
				"%s need to increase MAX_NUM_TSFR_LLIS from 0x%08x\n",
				__func__, MAX_NUM_TSFR_LLIS);
			return 0;
		}
	}

	llis_va = txd->llis_va;
	last_lli = llis_va + (num_llis - 1) * pl08x->lli_words;

	if (txd->cyclic) {
		/* Link back to the first LLI. */
		last_lli[PL080_LLI_LLI] = txd->llis_bus | bd.lli_bus;
	} else {
		/* The final LLI terminates the LLI. */
		last_lli[PL080_LLI_LLI] = 0;
		/* The final LLI element shall also fire an interrupt. */
		if (pl08x->vd->ftdmac020)
			last_lli[PL080_LLI_CCTL] &= ~FTDMAC020_LLI_TC_MSK;
		else
			last_lli[PL080_LLI_CCTL] |= PL080_CONTROL_TC_IRQ_EN;
	}

	pl08x_dump_lli(pl08x, llis_va, num_llis);

	return num_llis;
}

static void pl08x_free_txd(struct pl08x_driver_data *pl08x,
			   struct pl08x_txd *txd)
{
	struct pl08x_sg *dsg, *_dsg;

	if (txd->llis_va)
		dma_pool_free(pl08x->pool, txd->llis_va, txd->llis_bus);

	list_for_each_entry_safe(dsg, _dsg, &txd->dsg_list, node) {
		list_del(&dsg->node);
		kfree(dsg);
	}

	kfree(txd);
}

static void pl08x_desc_free(struct virt_dma_desc *vd)
{
	struct pl08x_txd *txd = to_pl08x_txd(&vd->tx);
	struct pl08x_dma_chan *plchan = to_pl08x_chan(vd->tx.chan);

	dma_descriptor_unmap(&vd->tx);
	if (!txd->done)
		pl08x_release_mux(plchan);

	pl08x_free_txd(plchan->host, txd);
}

static void pl08x_free_txd_list(struct pl08x_driver_data *pl08x,
				struct pl08x_dma_chan *plchan)
{
	LIST_HEAD(head);

	vchan_get_all_descriptors(&plchan->vc, &head);
	vchan_dma_desc_free_list(&plchan->vc, &head);
}

/*
 * The DMA ENGINE API
 */
static void pl08x_free_chan_resources(struct dma_chan *chan)
{
	/* Ensure all queued descriptors are freed */
	vchan_free_chan_resources(to_virt_chan(chan));
}

static struct dma_async_tx_descriptor *pl08x_prep_dma_interrupt(
		struct dma_chan *chan, unsigned long flags)
{
	struct dma_async_tx_descriptor *retval = NULL;

	return retval;
}

/*
 * Code accessing dma_async_is_complete() in a tight loop may give problems.
 * If slaves are relying on interrupts to signal completion this function
 * must not be called with interrupts disabled.
 */
static enum dma_status pl08x_dma_tx_status(struct dma_chan *chan,
		dma_cookie_t cookie, struct dma_tx_state *txstate)
{
	struct pl08x_dma_chan *plchan = to_pl08x_chan(chan);
	struct virt_dma_desc *vd;
	unsigned long flags;
	enum dma_status ret;
	size_t bytes = 0;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	/*
	 * There's no point calculating the residue if there's
	 * no txstate to store the value.
	 */
	if (!txstate) {
		if (plchan->state == PL08X_CHAN_PAUSED)
			ret = DMA_PAUSED;
		return ret;
	}

	spin_lock_irqsave(&plchan->vc.lock, flags);
	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret != DMA_COMPLETE) {
		vd = vchan_find_desc(&plchan->vc, cookie);
		if (vd) {
			/* On the issued list, so hasn't been processed yet */
			struct pl08x_txd *txd = to_pl08x_txd(&vd->tx);
			struct pl08x_sg *dsg;

			list_for_each_entry(dsg, &txd->dsg_list, node)
				bytes += dsg->len;
		} else {
			bytes = pl08x_getbytes_chan(plchan);
		}
	}
	spin_unlock_irqrestore(&plchan->vc.lock, flags);

	/*
	 * This cookie not complete yet
	 * Get number of bytes left in the active transactions and queue
	 */
	dma_set_residue(txstate, bytes);

	if (plchan->state == PL08X_CHAN_PAUSED && ret == DMA_IN_PROGRESS)
		ret = DMA_PAUSED;

	/* Whether waiting or running, we're in progress */
	return ret;
}

/* PrimeCell DMA extension */
struct burst_table {
	u32 burstwords;
	u32 reg;
};

static const struct burst_table burst_sizes[] = {
	{
		.burstwords = 256,
		.reg = PL080_BSIZE_256,
	},
	{
		.burstwords = 128,
		.reg = PL080_BSIZE_128,
	},
	{
		.burstwords = 64,
		.reg = PL080_BSIZE_64,
	},
	{
		.burstwords = 32,
		.reg = PL080_BSIZE_32,
	},
	{
		.burstwords = 16,
		.reg = PL080_BSIZE_16,
	},
	{
		.burstwords = 8,
		.reg = PL080_BSIZE_8,
	},
	{
		.burstwords = 4,
		.reg = PL080_BSIZE_4,
	},
	{
		.burstwords = 0,
		.reg = PL080_BSIZE_1,
	},
};

/*
 * Given the source and destination available bus masks, select which
 * will be routed to each port.  We try to have source and destination
 * on separate ports, but always respect the allowable settings.
 */
static u32 pl08x_select_bus(bool ftdmac020, u8 src, u8 dst)
{
	u32 cctl = 0;
	u32 dst_ahb2;
	u32 src_ahb2;

	/* The FTDMAC020 use different bits to indicate src/dst bus */
	if (ftdmac020) {
		dst_ahb2 = FTDMAC020_LLI_DST_SEL;
		src_ahb2 = FTDMAC020_LLI_SRC_SEL;
	} else {
		dst_ahb2 = PL080_CONTROL_DST_AHB2;
		src_ahb2 = PL080_CONTROL_SRC_AHB2;
	}

	if (!(dst & PL08X_AHB1) || ((dst & PL08X_AHB2) && (src & PL08X_AHB1)))
		cctl |= dst_ahb2;
	if (!(src & PL08X_AHB1) || ((src & PL08X_AHB2) && !(dst & PL08X_AHB2)))
		cctl |= src_ahb2;

	return cctl;
}

static u32 pl08x_cctl(u32 cctl)
{
	cctl &= ~(PL080_CONTROL_SRC_AHB2 | PL080_CONTROL_DST_AHB2 |
		  PL080_CONTROL_SRC_INCR | PL080_CONTROL_DST_INCR |
		  PL080_CONTROL_PROT_MASK);

	/* Access the cell in privileged mode, non-bufferable, non-cacheable */
	return cctl | PL080_CONTROL_PROT_SYS;
}

static u32 pl08x_width(enum dma_slave_buswidth width)
{
	switch (width) {
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
		return PL080_WIDTH_8BIT;
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
		return PL080_WIDTH_16BIT;
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
		return PL080_WIDTH_32BIT;
	default:
		return ~0;
	}
}

static u32 pl08x_burst(u32 maxburst)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(burst_sizes); i++)
		if (burst_sizes[i].burstwords <= maxburst)
			break;

	return burst_sizes[i].reg;
}

static u32 pl08x_get_cctl(struct pl08x_dma_chan *plchan,
	enum dma_slave_buswidth addr_width, u32 maxburst)
{
	u32 width, burst, cctl = 0;

	width = pl08x_width(addr_width);
	if (width == ~0)
		return ~0;

	cctl |= width << PL080_CONTROL_SWIDTH_SHIFT;
	cctl |= width << PL080_CONTROL_DWIDTH_SHIFT;

	/*
	 * If this channel will only request single transfers, set this
	 * down to ONE element.  Also select one element if no maxburst
	 * is specified.
	 */
	if (plchan->cd->single)
		maxburst = 1;

	burst = pl08x_burst(maxburst);
	cctl |= burst << PL080_CONTROL_SB_SIZE_SHIFT;
	cctl |= burst << PL080_CONTROL_DB_SIZE_SHIFT;

	return pl08x_cctl(cctl);
}

/*
 * Slave transactions callback to the slave device to allow
 * synchronization of slave DMA signals with the DMAC enable
 */
static void pl08x_issue_pending(struct dma_chan *chan)
{
	struct pl08x_dma_chan *plchan = to_pl08x_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&plchan->vc.lock, flags);
	if (vchan_issue_pending(&plchan->vc)) {
		if (!plchan->phychan && plchan->state != PL08X_CHAN_WAITING)
			pl08x_phy_alloc_and_start(plchan);
	}
	spin_unlock_irqrestore(&plchan->vc.lock, flags);
}

static struct pl08x_txd *pl08x_get_txd(struct pl08x_dma_chan *plchan)
{
	struct pl08x_txd *txd = kzalloc(sizeof(*txd), GFP_NOWAIT);

	if (txd)
		INIT_LIST_HEAD(&txd->dsg_list);
	return txd;
}

static u32 pl08x_memcpy_cctl(struct pl08x_driver_data *pl08x)
{
	u32 cctl = 0;

	/* Conjure cctl */
	switch (pl08x->pd->memcpy_burst_size) {
	default:
		dev_err(&pl08x->adev->dev,
			"illegal burst size for memcpy, set to 1\n");
		/* Fall through */
	case PL08X_BURST_SZ_1:
		cctl |= PL080_BSIZE_1 << PL080_CONTROL_SB_SIZE_SHIFT |
			PL080_BSIZE_1 << PL080_CONTROL_DB_SIZE_SHIFT;
		break;
	case PL08X_BURST_SZ_4:
		cctl |= PL080_BSIZE_4 << PL080_CONTROL_SB_SIZE_SHIFT |
			PL080_BSIZE_4 << PL080_CONTROL_DB_SIZE_SHIFT;
		break;
	case PL08X_BURST_SZ_8:
		cctl |= PL080_BSIZE_8 << PL080_CONTROL_SB_SIZE_SHIFT |
			PL080_BSIZE_8 << PL080_CONTROL_DB_SIZE_SHIFT;
		break;
	case PL08X_BURST_SZ_16:
		cctl |= PL080_BSIZE_16 << PL080_CONTROL_SB_SIZE_SHIFT |
			PL080_BSIZE_16 << PL080_CONTROL_DB_SIZE_SHIFT;
		break;
	case PL08X_BURST_SZ_32:
		cctl |= PL080_BSIZE_32 << PL080_CONTROL_SB_SIZE_SHIFT |
			PL080_BSIZE_32 << PL080_CONTROL_DB_SIZE_SHIFT;
		break;
	case PL08X_BURST_SZ_64:
		cctl |= PL080_BSIZE_64 << PL080_CONTROL_SB_SIZE_SHIFT |
			PL080_BSIZE_64 << PL080_CONTROL_DB_SIZE_SHIFT;
		break;
	case PL08X_BURST_SZ_128:
		cctl |= PL080_BSIZE_128 << PL080_CONTROL_SB_SIZE_SHIFT |
			PL080_BSIZE_128 << PL080_CONTROL_DB_SIZE_SHIFT;
		break;
	case PL08X_BURST_SZ_256:
		cctl |= PL080_BSIZE_256 << PL080_CONTROL_SB_SIZE_SHIFT |
			PL080_BSIZE_256 << PL080_CONTROL_DB_SIZE_SHIFT;
		break;
	}

	switch (pl08x->pd->memcpy_bus_width) {
	default:
		dev_err(&pl08x->adev->dev,
			"illegal bus width for memcpy, set to 8 bits\n");
		/* Fall through */
	case PL08X_BUS_WIDTH_8_BITS:
		cctl |= PL080_WIDTH_8BIT << PL080_CONTROL_SWIDTH_SHIFT |
			PL080_WIDTH_8BIT << PL080_CONTROL_DWIDTH_SHIFT;
		break;
	case PL08X_BUS_WIDTH_16_BITS:
		cctl |= PL080_WIDTH_16BIT << PL080_CONTROL_SWIDTH_SHIFT |
			PL080_WIDTH_16BIT << PL080_CONTROL_DWIDTH_SHIFT;
		break;
	case PL08X_BUS_WIDTH_32_BITS:
		cctl |= PL080_WIDTH_32BIT << PL080_CONTROL_SWIDTH_SHIFT |
			PL080_WIDTH_32BIT << PL080_CONTROL_DWIDTH_SHIFT;
		break;
	}

	/* Protection flags */
	if (pl08x->pd->memcpy_prot_buff)
		cctl |= PL080_CONTROL_PROT_BUFF;
	if (pl08x->pd->memcpy_prot_cache)
		cctl |= PL080_CONTROL_PROT_CACHE;

	/* We are the kernel, so we are in privileged mode */
	cctl |= PL080_CONTROL_PROT_SYS;

	/* Both to be incremented or the code will break */
	cctl |= PL080_CONTROL_SRC_INCR | PL080_CONTROL_DST_INCR;

	if (pl08x->vd->dualmaster)
		cctl |= pl08x_select_bus(false,
					 pl08x->mem_buses,
					 pl08x->mem_buses);

	return cctl;
}

static u32 pl08x_ftdmac020_memcpy_cctl(struct pl08x_driver_data *pl08x)
{
	u32 cctl = 0;

	/* Conjure cctl */
	switch (pl08x->pd->memcpy_bus_width) {
	default:
		dev_err(&pl08x->adev->dev,
			"illegal bus width for memcpy, set to 8 bits\n");
		/* Fall through */
	case PL08X_BUS_WIDTH_8_BITS:
		cctl |= PL080_WIDTH_8BIT << FTDMAC020_LLI_SRC_WIDTH_SHIFT |
			PL080_WIDTH_8BIT << FTDMAC020_LLI_DST_WIDTH_SHIFT;
		break;
	case PL08X_BUS_WIDTH_16_BITS:
		cctl |= PL080_WIDTH_16BIT << FTDMAC020_LLI_SRC_WIDTH_SHIFT |
			PL080_WIDTH_16BIT << FTDMAC020_LLI_DST_WIDTH_SHIFT;
		break;
	case PL08X_BUS_WIDTH_32_BITS:
		cctl |= PL080_WIDTH_32BIT << FTDMAC020_LLI_SRC_WIDTH_SHIFT |
			PL080_WIDTH_32BIT << FTDMAC020_LLI_DST_WIDTH_SHIFT;
		break;
	}

	/*
	 * By default mask the TC IRQ on all LLIs, it will be unmasked on
	 * the last LLI item by other code.
	 */
	cctl |= FTDMAC020_LLI_TC_MSK;

	/*
	 * Both to be incremented so leave bits FTDMAC020_LLI_SRCAD_CTL
	 * and FTDMAC020_LLI_DSTAD_CTL as zero
	 */
	if (pl08x->vd->dualmaster)
		cctl |= pl08x_select_bus(true,
					 pl08x->mem_buses,
					 pl08x->mem_buses);

	return cctl;
}

/*
 * Initialize a descriptor to be used by memcpy submit
 */
static struct dma_async_tx_descriptor *pl08x_prep_dma_memcpy(
		struct dma_chan *chan, dma_addr_t dest, dma_addr_t src,
		size_t len, unsigned long flags)
{
	struct pl08x_dma_chan *plchan = to_pl08x_chan(chan);
	struct pl08x_driver_data *pl08x = plchan->host;
	struct pl08x_txd *txd;
	struct pl08x_sg *dsg;
	int ret;

	txd = pl08x_get_txd(plchan);
	if (!txd) {
		dev_err(&pl08x->adev->dev,
			"%s no memory for descriptor\n", __func__);
		return NULL;
	}

	dsg = kzalloc(sizeof(struct pl08x_sg), GFP_NOWAIT);
	if (!dsg) {
		pl08x_free_txd(pl08x, txd);
		return NULL;
	}
	list_add_tail(&dsg->node, &txd->dsg_list);

	dsg->src_addr = src;
	dsg->dst_addr = dest;
	dsg->len = len;
	if (pl08x->vd->ftdmac020) {
		/* Writing CCFG zero ENABLES all interrupts */
		txd->ccfg = 0;
		txd->cctl = pl08x_ftdmac020_memcpy_cctl(pl08x);
	} else {
		txd->ccfg = PL080_CONFIG_ERR_IRQ_MASK |
			PL080_CONFIG_TC_IRQ_MASK |
			PL080_FLOW_MEM2MEM << PL080_CONFIG_FLOW_CONTROL_SHIFT;
		txd->cctl = pl08x_memcpy_cctl(pl08x);
	}

	ret = pl08x_fill_llis_for_desc(plchan->host, txd);
	if (!ret) {
		pl08x_free_txd(pl08x, txd);
		return NULL;
	}

	return vchan_tx_prep(&plchan->vc, &txd->vd, flags);
}

static struct pl08x_txd *pl08x_init_txd(
		struct dma_chan *chan,
		enum dma_transfer_direction direction,
		dma_addr_t *slave_addr)
{
	struct pl08x_dma_chan *plchan = to_pl08x_chan(chan);
	struct pl08x_driver_data *pl08x = plchan->host;
	struct pl08x_txd *txd;
	enum dma_slave_buswidth addr_width;
	int ret, tmp;
	u8 src_buses, dst_buses;
	u32 maxburst, cctl;

	txd = pl08x_get_txd(plchan);
	if (!txd) {
		dev_err(&pl08x->adev->dev, "%s no txd\n", __func__);
		return NULL;
	}

	/*
	 * Set up addresses, the PrimeCell configured address
	 * will take precedence since this may configure the
	 * channel target address dynamically at runtime.
	 */
	if (direction == DMA_MEM_TO_DEV) {
		cctl = PL080_CONTROL_SRC_INCR;
		*slave_addr = plchan->cfg.dst_addr;
		addr_width = plchan->cfg.dst_addr_width;
		maxburst = plchan->cfg.dst_maxburst;
		src_buses = pl08x->mem_buses;
		dst_buses = plchan->cd->periph_buses;
	} else if (direction == DMA_DEV_TO_MEM) {
		cctl = PL080_CONTROL_DST_INCR;
		*slave_addr = plchan->cfg.src_addr;
		addr_width = plchan->cfg.src_addr_width;
		maxburst = plchan->cfg.src_maxburst;
		src_buses = plchan->cd->periph_buses;
		dst_buses = pl08x->mem_buses;
	} else {
		pl08x_free_txd(pl08x, txd);
		dev_err(&pl08x->adev->dev,
			"%s direction unsupported\n", __func__);
		return NULL;
	}

	cctl |= pl08x_get_cctl(plchan, addr_width, maxburst);
	if (cctl == ~0) {
		pl08x_free_txd(pl08x, txd);
		dev_err(&pl08x->adev->dev,
			"DMA slave configuration botched?\n");
		return NULL;
	}

	txd->cctl = cctl | pl08x_select_bus(false, src_buses, dst_buses);

	if (plchan->cfg.device_fc)
		tmp = (direction == DMA_MEM_TO_DEV) ? PL080_FLOW_MEM2PER_PER :
			PL080_FLOW_PER2MEM_PER;
	else
		tmp = (direction == DMA_MEM_TO_DEV) ? PL080_FLOW_MEM2PER :
			PL080_FLOW_PER2MEM;

	txd->ccfg = PL080_CONFIG_ERR_IRQ_MASK |
		PL080_CONFIG_TC_IRQ_MASK |
		tmp << PL080_CONFIG_FLOW_CONTROL_SHIFT;

	ret = pl08x_request_mux(plchan);
	if (ret < 0) {
		pl08x_free_txd(pl08x, txd);
		dev_dbg(&pl08x->adev->dev,
			"unable to mux for transfer on %s due to platform restrictions\n",
			plchan->name);
		return NULL;
	}

	dev_dbg(&pl08x->adev->dev, "allocated DMA request signal %d for xfer on %s\n",
		 plchan->signal, plchan->name);

	/* Assign the flow control signal to this channel */
	if (direction == DMA_MEM_TO_DEV)
		txd->ccfg |= plchan->signal << PL080_CONFIG_DST_SEL_SHIFT;
	else
		txd->ccfg |= plchan->signal << PL080_CONFIG_SRC_SEL_SHIFT;

	return txd;
}

static int pl08x_tx_add_sg(struct pl08x_txd *txd,
			   enum dma_transfer_direction direction,
			   dma_addr_t slave_addr,
			   dma_addr_t buf_addr,
			   unsigned int len)
{
	struct pl08x_sg *dsg;

	dsg = kzalloc(sizeof(struct pl08x_sg), GFP_NOWAIT);
	if (!dsg)
		return -ENOMEM;

	list_add_tail(&dsg->node, &txd->dsg_list);

	dsg->len = len;
	if (direction == DMA_MEM_TO_DEV) {
		dsg->src_addr = buf_addr;
		dsg->dst_addr = slave_addr;
	} else {
		dsg->src_addr = slave_addr;
		dsg->dst_addr = buf_addr;
	}

	return 0;
}

static struct dma_async_tx_descriptor *pl08x_prep_slave_sg(
		struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context)
{
	struct pl08x_dma_chan *plchan = to_pl08x_chan(chan);
	struct pl08x_driver_data *pl08x = plchan->host;
	struct pl08x_txd *txd;
	struct scatterlist *sg;
	int ret, tmp;
	dma_addr_t slave_addr;

	dev_dbg(&pl08x->adev->dev, "%s prepare transaction of %d bytes from %s\n",
			__func__, sg_dma_len(sgl), plchan->name);

	txd = pl08x_init_txd(chan, direction, &slave_addr);
	if (!txd)
		return NULL;

	for_each_sg(sgl, sg, sg_len, tmp) {
		ret = pl08x_tx_add_sg(txd, direction, slave_addr,
				      sg_dma_address(sg),
				      sg_dma_len(sg));
		if (ret) {
			pl08x_release_mux(plchan);
			pl08x_free_txd(pl08x, txd);
			dev_err(&pl08x->adev->dev, "%s no mem for pl080 sg\n",
					__func__);
			return NULL;
		}
	}

	ret = pl08x_fill_llis_for_desc(plchan->host, txd);
	if (!ret) {
		pl08x_release_mux(plchan);
		pl08x_free_txd(pl08x, txd);
		return NULL;
	}

	return vchan_tx_prep(&plchan->vc, &txd->vd, flags);
}

static struct dma_async_tx_descriptor *pl08x_prep_dma_cyclic(
		struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
		size_t period_len, enum dma_transfer_direction direction,
		unsigned long flags)
{
	struct pl08x_dma_chan *plchan = to_pl08x_chan(chan);
	struct pl08x_driver_data *pl08x = plchan->host;
	struct pl08x_txd *txd;
	int ret, tmp;
	dma_addr_t slave_addr;

	dev_dbg(&pl08x->adev->dev,
		"%s prepare cyclic transaction of %zd/%zd bytes %s %s\n",
		__func__, period_len, buf_len,
		direction == DMA_MEM_TO_DEV ? "to" : "from",
		plchan->name);

	txd = pl08x_init_txd(chan, direction, &slave_addr);
	if (!txd)
		return NULL;

	txd->cyclic = true;
	txd->cctl |= PL080_CONTROL_TC_IRQ_EN;
	for (tmp = 0; tmp < buf_len; tmp += period_len) {
		ret = pl08x_tx_add_sg(txd, direction, slave_addr,
				      buf_addr + tmp, period_len);
		if (ret) {
			pl08x_release_mux(plchan);
			pl08x_free_txd(pl08x, txd);
			return NULL;
		}
	}

	ret = pl08x_fill_llis_for_desc(plchan->host, txd);
	if (!ret) {
		pl08x_release_mux(plchan);
		pl08x_free_txd(pl08x, txd);
		return NULL;
	}

	return vchan_tx_prep(&plchan->vc, &txd->vd, flags);
}

static int pl08x_config(struct dma_chan *chan,
			struct dma_slave_config *config)
{
	struct pl08x_dma_chan *plchan = to_pl08x_chan(chan);
	struct pl08x_driver_data *pl08x = plchan->host;

	if (!plchan->slave)
		return -EINVAL;

	/* Reject definitely invalid configurations */
	if (config->src_addr_width == DMA_SLAVE_BUSWIDTH_8_BYTES ||
	    config->dst_addr_width == DMA_SLAVE_BUSWIDTH_8_BYTES)
		return -EINVAL;

	if (config->device_fc && pl08x->vd->pl080s) {
		dev_err(&pl08x->adev->dev,
			"%s: PL080S does not support peripheral flow control\n",
			__func__);
		return -EINVAL;
	}

	plchan->cfg = *config;

	return 0;
}

static int pl08x_terminate_all(struct dma_chan *chan)
{
	struct pl08x_dma_chan *plchan = to_pl08x_chan(chan);
	struct pl08x_driver_data *pl08x = plchan->host;
	unsigned long flags;

	spin_lock_irqsave(&plchan->vc.lock, flags);
	if (!plchan->phychan && !plchan->at) {
		spin_unlock_irqrestore(&plchan->vc.lock, flags);
		return 0;
	}

	plchan->state = PL08X_CHAN_IDLE;

	if (plchan->phychan) {
		/*
		 * Mark physical channel as free and free any slave
		 * signal
		 */
		pl08x_phy_free(plchan);
	}
	/* Dequeue jobs and free LLIs */
	if (plchan->at) {
		vchan_terminate_vdesc(&plchan->at->vd);
		plchan->at = NULL;
	}
	/* Dequeue jobs not yet fired as well */
	pl08x_free_txd_list(pl08x, plchan);

	spin_unlock_irqrestore(&plchan->vc.lock, flags);

	return 0;
}

static void pl08x_synchronize(struct dma_chan *chan)
{
	struct pl08x_dma_chan *plchan = to_pl08x_chan(chan);

	vchan_synchronize(&plchan->vc);
}

static int pl08x_pause(struct dma_chan *chan)
{
	struct pl08x_dma_chan *plchan = to_pl08x_chan(chan);
	unsigned long flags;

	/*
	 * Anything succeeds on channels with no physical allocation and
	 * no queued transfers.
	 */
	spin_lock_irqsave(&plchan->vc.lock, flags);
	if (!plchan->phychan && !plchan->at) {
		spin_unlock_irqrestore(&plchan->vc.lock, flags);
		return 0;
	}

	pl08x_pause_phy_chan(plchan->phychan);
	plchan->state = PL08X_CHAN_PAUSED;

	spin_unlock_irqrestore(&plchan->vc.lock, flags);

	return 0;
}

static int pl08x_resume(struct dma_chan *chan)
{
	struct pl08x_dma_chan *plchan = to_pl08x_chan(chan);
	unsigned long flags;

	/*
	 * Anything succeeds on channels with no physical allocation and
	 * no queued transfers.
	 */
	spin_lock_irqsave(&plchan->vc.lock, flags);
	if (!plchan->phychan && !plchan->at) {
		spin_unlock_irqrestore(&plchan->vc.lock, flags);
		return 0;
	}

	pl08x_resume_phy_chan(plchan->phychan);
	plchan->state = PL08X_CHAN_RUNNING;

	spin_unlock_irqrestore(&plchan->vc.lock, flags);

	return 0;
}

bool pl08x_filter_id(struct dma_chan *chan, void *chan_id)
{
	struct pl08x_dma_chan *plchan;
	char *name = chan_id;

	/* Reject channels for devices not bound to this driver */
	if (chan->device->dev->driver != &pl08x_amba_driver.drv)
		return false;

	plchan = to_pl08x_chan(chan);

	/* Check that the channel is not taken! */
	if (!strcmp(plchan->name, name))
		return true;

	return false;
}
EXPORT_SYMBOL_GPL(pl08x_filter_id);

static bool pl08x_filter_fn(struct dma_chan *chan, void *chan_id)
{
	struct pl08x_dma_chan *plchan = to_pl08x_chan(chan);

	return plchan->cd == chan_id;
}

/*
 * Just check that the device is there and active
 * TODO: turn this bit on/off depending on the number of physical channels
 * actually used, if it is zero... well shut it off. That will save some
 * power. Cut the clock at the same time.
 */
static void pl08x_ensure_on(struct pl08x_driver_data *pl08x)
{
	/* The Nomadik variant does not have the config register */
	if (pl08x->vd->nomadik)
		return;
	/* The FTDMAC020 variant does this in another register */
	if (pl08x->vd->ftdmac020) {
		writel(PL080_CONFIG_ENABLE, pl08x->base + FTDMAC020_CSR);
		return;
	}
	writel(PL080_CONFIG_ENABLE, pl08x->base + PL080_CONFIG);
}

static irqreturn_t pl08x_irq(int irq, void *dev)
{
	struct pl08x_driver_data *pl08x = dev;
	u32 mask = 0, err, tc, i;

	/* check & clear - ERR & TC interrupts */
	err = readl(pl08x->base + PL080_ERR_STATUS);
	if (err) {
		dev_err(&pl08x->adev->dev, "%s error interrupt, register value 0x%08x\n",
			__func__, err);
		writel(err, pl08x->base + PL080_ERR_CLEAR);
	}
	tc = readl(pl08x->base + PL080_TC_STATUS);
	if (tc)
		writel(tc, pl08x->base + PL080_TC_CLEAR);

	if (!err && !tc)
		return IRQ_NONE;

	for (i = 0; i < pl08x->vd->channels; i++) {
		if ((BIT(i) & err) || (BIT(i) & tc)) {
			/* Locate physical channel */
			struct pl08x_phy_chan *phychan = &pl08x->phy_chans[i];
			struct pl08x_dma_chan *plchan = phychan->serving;
			struct pl08x_txd *tx;

			if (!plchan) {
				dev_err(&pl08x->adev->dev,
					"%s Error TC interrupt on unused channel: 0x%08x\n",
					__func__, i);
				continue;
			}

			spin_lock(&plchan->vc.lock);
			tx = plchan->at;
			if (tx && tx->cyclic) {
				vchan_cyclic_callback(&tx->vd);
			} else if (tx) {
				plchan->at = NULL;
				/*
				 * This descriptor is done, release its mux
				 * reservation.
				 */
				pl08x_release_mux(plchan);
				tx->done = true;
				vchan_cookie_complete(&tx->vd);

				/*
				 * And start the next descriptor (if any),
				 * otherwise free this channel.
				 */
				if (vchan_next_desc(&plchan->vc))
					pl08x_start_next_txd(plchan);
				else
					pl08x_phy_free(plchan);
			}
			spin_unlock(&plchan->vc.lock);

			mask |= BIT(i);
		}
	}

	return mask ? IRQ_HANDLED : IRQ_NONE;
}

static void pl08x_dma_slave_init(struct pl08x_dma_chan *chan)
{
	chan->slave = true;
	chan->name = chan->cd->bus_id;
	chan->cfg.src_addr = chan->cd->addr;
	chan->cfg.dst_addr = chan->cd->addr;
}

/*
 * Initialise the DMAC memcpy/slave channels.
 * Make a local wrapper to hold required data
 */
static int pl08x_dma_init_virtual_channels(struct pl08x_driver_data *pl08x,
		struct dma_device *dmadev, unsigned int channels, bool slave)
{
	struct pl08x_dma_chan *chan;
	int i;

	INIT_LIST_HEAD(&dmadev->channels);

	/*
	 * Register as many many memcpy as we have physical channels,
	 * we won't always be able to use all but the code will have
	 * to cope with that situation.
	 */
	for (i = 0; i < channels; i++) {
		chan = kzalloc(sizeof(*chan), GFP_KERNEL);
		if (!chan)
			return -ENOMEM;

		chan->host = pl08x;
		chan->state = PL08X_CHAN_IDLE;
		chan->signal = -1;

		if (slave) {
			chan->cd = &pl08x->pd->slave_channels[i];
			/*
			 * Some implementations have muxed signals, whereas some
			 * use a mux in front of the signals and need dynamic
			 * assignment of signals.
			 */
			chan->signal = i;
			pl08x_dma_slave_init(chan);
		} else {
			chan->cd = kzalloc(sizeof(*chan->cd), GFP_KERNEL);
			if (!chan->cd) {
				kfree(chan);
				return -ENOMEM;
			}
			chan->cd->bus_id = "memcpy";
			chan->cd->periph_buses = pl08x->pd->mem_buses;
			chan->name = kasprintf(GFP_KERNEL, "memcpy%d", i);
			if (!chan->name) {
				kfree(chan->cd);
				kfree(chan);
				return -ENOMEM;
			}
		}
		dev_dbg(&pl08x->adev->dev,
			 "initialize virtual channel \"%s\"\n",
			 chan->name);

		chan->vc.desc_free = pl08x_desc_free;
		vchan_init(&chan->vc, dmadev);
	}
	dev_info(&pl08x->adev->dev, "initialized %d virtual %s channels\n",
		 i, slave ? "slave" : "memcpy");
	return i;
}

static void pl08x_free_virtual_channels(struct dma_device *dmadev)
{
	struct pl08x_dma_chan *chan = NULL;
	struct pl08x_dma_chan *next;

	list_for_each_entry_safe(chan,
				 next, &dmadev->channels, vc.chan.device_node) {
		list_del(&chan->vc.chan.device_node);
		kfree(chan);
	}
}

#ifdef CONFIG_DEBUG_FS
static const char *pl08x_state_str(enum pl08x_dma_chan_state state)
{
	switch (state) {
	case PL08X_CHAN_IDLE:
		return "idle";
	case PL08X_CHAN_RUNNING:
		return "running";
	case PL08X_CHAN_PAUSED:
		return "paused";
	case PL08X_CHAN_WAITING:
		return "waiting";
	default:
		break;
	}
	return "UNKNOWN STATE";
}

static int pl08x_debugfs_show(struct seq_file *s, void *data)
{
	struct pl08x_driver_data *pl08x = s->private;
	struct pl08x_dma_chan *chan;
	struct pl08x_phy_chan *ch;
	unsigned long flags;
	int i;

	seq_printf(s, "PL08x physical channels:\n");
	seq_printf(s, "CHANNEL:\tUSER:\n");
	seq_printf(s, "--------\t-----\n");
	for (i = 0; i < pl08x->vd->channels; i++) {
		struct pl08x_dma_chan *virt_chan;

		ch = &pl08x->phy_chans[i];

		spin_lock_irqsave(&ch->lock, flags);
		virt_chan = ch->serving;

		seq_printf(s, "%d\t\t%s%s\n",
			   ch->id,
			   virt_chan ? virt_chan->name : "(none)",
			   ch->locked ? " LOCKED" : "");

		spin_unlock_irqrestore(&ch->lock, flags);
	}

	seq_printf(s, "\nPL08x virtual memcpy channels:\n");
	seq_printf(s, "CHANNEL:\tSTATE:\n");
	seq_printf(s, "--------\t------\n");
	list_for_each_entry(chan, &pl08x->memcpy.channels, vc.chan.device_node) {
		seq_printf(s, "%s\t\t%s\n", chan->name,
			   pl08x_state_str(chan->state));
	}

	if (pl08x->has_slave) {
		seq_printf(s, "\nPL08x virtual slave channels:\n");
		seq_printf(s, "CHANNEL:\tSTATE:\n");
		seq_printf(s, "--------\t------\n");
		list_for_each_entry(chan, &pl08x->slave.channels,
				    vc.chan.device_node) {
			seq_printf(s, "%s\t\t%s\n", chan->name,
				   pl08x_state_str(chan->state));
		}
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(pl08x_debugfs);

static void init_pl08x_debugfs(struct pl08x_driver_data *pl08x)
{
	/* Expose a simple debugfs interface to view all clocks */
	debugfs_create_file(dev_name(&pl08x->adev->dev), S_IFREG | S_IRUGO,
			    NULL, pl08x, &pl08x_debugfs_fops);
}

#else
static inline void init_pl08x_debugfs(struct pl08x_driver_data *pl08x)
{
}
#endif

#ifdef CONFIG_OF
static struct dma_chan *pl08x_find_chan_id(struct pl08x_driver_data *pl08x,
					 u32 id)
{
	struct pl08x_dma_chan *chan;

	/* Trying to get a slave channel from something with no slave support */
	if (!pl08x->has_slave)
		return NULL;

	list_for_each_entry(chan, &pl08x->slave.channels, vc.chan.device_node) {
		if (chan->signal == id)
			return &chan->vc.chan;
	}

	return NULL;
}

static struct dma_chan *pl08x_of_xlate(struct of_phandle_args *dma_spec,
				       struct of_dma *ofdma)
{
	struct pl08x_driver_data *pl08x = ofdma->of_dma_data;
	struct dma_chan *dma_chan;
	struct pl08x_dma_chan *plchan;

	if (!pl08x)
		return NULL;

	if (dma_spec->args_count != 2) {
		dev_err(&pl08x->adev->dev,
			"DMA channel translation requires two cells\n");
		return NULL;
	}

	dma_chan = pl08x_find_chan_id(pl08x, dma_spec->args[0]);
	if (!dma_chan) {
		dev_err(&pl08x->adev->dev,
			"DMA slave channel not found\n");
		return NULL;
	}

	plchan = to_pl08x_chan(dma_chan);
	dev_dbg(&pl08x->adev->dev,
		"translated channel for signal %d\n",
		dma_spec->args[0]);

	/* Augment channel data for applicable AHB buses */
	plchan->cd->periph_buses = dma_spec->args[1];
	return dma_get_slave_channel(dma_chan);
}

static int pl08x_of_probe(struct amba_device *adev,
			  struct pl08x_driver_data *pl08x,
			  struct device_node *np)
{
	struct pl08x_platform_data *pd;
	struct pl08x_channel_data *chanp = NULL;
	u32 val;
	int ret;
	int i;

	pd = devm_kzalloc(&adev->dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	/* Eligible bus masters for fetching LLIs */
	if (of_property_read_bool(np, "lli-bus-interface-ahb1"))
		pd->lli_buses |= PL08X_AHB1;
	if (of_property_read_bool(np, "lli-bus-interface-ahb2"))
		pd->lli_buses |= PL08X_AHB2;
	if (!pd->lli_buses) {
		dev_info(&adev->dev, "no bus masters for LLIs stated, assume all\n");
		pd->lli_buses |= PL08X_AHB1 | PL08X_AHB2;
	}

	/* Eligible bus masters for memory access */
	if (of_property_read_bool(np, "mem-bus-interface-ahb1"))
		pd->mem_buses |= PL08X_AHB1;
	if (of_property_read_bool(np, "mem-bus-interface-ahb2"))
		pd->mem_buses |= PL08X_AHB2;
	if (!pd->mem_buses) {
		dev_info(&adev->dev, "no bus masters for memory stated, assume all\n");
		pd->mem_buses |= PL08X_AHB1 | PL08X_AHB2;
	}

	/* Parse the memcpy channel properties */
	ret = of_property_read_u32(np, "memcpy-burst-size", &val);
	if (ret) {
		dev_info(&adev->dev, "no memcpy burst size specified, using 1 byte\n");
		val = 1;
	}
	switch (val) {
	default:
		dev_err(&adev->dev, "illegal burst size for memcpy, set to 1\n");
		/* Fall through */
	case 1:
		pd->memcpy_burst_size = PL08X_BURST_SZ_1;
		break;
	case 4:
		pd->memcpy_burst_size = PL08X_BURST_SZ_4;
		break;
	case 8:
		pd->memcpy_burst_size = PL08X_BURST_SZ_8;
		break;
	case 16:
		pd->memcpy_burst_size = PL08X_BURST_SZ_16;
		break;
	case 32:
		pd->memcpy_burst_size = PL08X_BURST_SZ_32;
		break;
	case 64:
		pd->memcpy_burst_size = PL08X_BURST_SZ_64;
		break;
	case 128:
		pd->memcpy_burst_size = PL08X_BURST_SZ_128;
		break;
	case 256:
		pd->memcpy_burst_size = PL08X_BURST_SZ_256;
		break;
	}

	ret = of_property_read_u32(np, "memcpy-bus-width", &val);
	if (ret) {
		dev_info(&adev->dev, "no memcpy bus width specified, using 8 bits\n");
		val = 8;
	}
	switch (val) {
	default:
		dev_err(&adev->dev, "illegal bus width for memcpy, set to 8 bits\n");
		/* Fall through */
	case 8:
		pd->memcpy_bus_width = PL08X_BUS_WIDTH_8_BITS;
		break;
	case 16:
		pd->memcpy_bus_width = PL08X_BUS_WIDTH_16_BITS;
		break;
	case 32:
		pd->memcpy_bus_width = PL08X_BUS_WIDTH_32_BITS;
		break;
	}

	/*
	 * Allocate channel data for all possible slave channels (one
	 * for each possible signal), channels will then be allocated
	 * for a device and have it's AHB interfaces set up at
	 * translation time.
	 */
	if (pl08x->vd->signals) {
		chanp = devm_kcalloc(&adev->dev,
				     pl08x->vd->signals,
				     sizeof(struct pl08x_channel_data),
				     GFP_KERNEL);
		if (!chanp)
			return -ENOMEM;

		pd->slave_channels = chanp;
		for (i = 0; i < pl08x->vd->signals; i++) {
			/*
			 * chanp->periph_buses will be assigned at translation
			 */
			chanp->bus_id = kasprintf(GFP_KERNEL, "slave%d", i);
			chanp++;
		}
		pd->num_slave_channels = pl08x->vd->signals;
	}

	pl08x->pd = pd;

	return of_dma_controller_register(adev->dev.of_node, pl08x_of_xlate,
					  pl08x);
}
#else
static inline int pl08x_of_probe(struct amba_device *adev,
				 struct pl08x_driver_data *pl08x,
				 struct device_node *np)
{
	return -EINVAL;
}
#endif

static int pl08x_probe(struct amba_device *adev, const struct amba_id *id)
{
	struct pl08x_driver_data *pl08x;
	struct vendor_data *vd = id->data;
	struct device_node *np = adev->dev.of_node;
	u32 tsfr_size;
	int ret = 0;
	int i;

	ret = amba_request_regions(adev, NULL);
	if (ret)
		return ret;

	/* Ensure that we can do DMA */
	ret = dma_set_mask_and_coherent(&adev->dev, DMA_BIT_MASK(32));
	if (ret)
		goto out_no_pl08x;

	/* Create the driver state holder */
	pl08x = kzalloc(sizeof(*pl08x), GFP_KERNEL);
	if (!pl08x) {
		ret = -ENOMEM;
		goto out_no_pl08x;
	}

	/* Assign useful pointers to the driver state */
	pl08x->adev = adev;
	pl08x->vd = vd;

	pl08x->base = ioremap(adev->res.start, resource_size(&adev->res));
	if (!pl08x->base) {
		ret = -ENOMEM;
		goto out_no_ioremap;
	}

	if (vd->ftdmac020) {
		u32 val;

		val = readl(pl08x->base + FTDMAC020_REVISION);
		dev_info(&pl08x->adev->dev, "FTDMAC020 %d.%d rel %d\n",
			 (val >> 16) & 0xff, (val >> 8) & 0xff, val & 0xff);
		val = readl(pl08x->base + FTDMAC020_FEATURE);
		dev_info(&pl08x->adev->dev, "FTDMAC020 %d channels, "
			 "%s built-in bridge, %s, %s linked lists\n",
			 (val >> 12) & 0x0f,
			 (val & BIT(10)) ? "no" : "has",
			 (val & BIT(9)) ? "AHB0 and AHB1" : "AHB0",
			 (val & BIT(8)) ? "supports" : "does not support");

		/* Vendor data from feature register */
		if (!(val & BIT(8)))
			dev_warn(&pl08x->adev->dev,
				 "linked lists not supported, required\n");
		vd->channels = (val >> 12) & 0x0f;
		vd->dualmaster = !!(val & BIT(9));
	}

	/* Initialize memcpy engine */
	dma_cap_set(DMA_MEMCPY, pl08x->memcpy.cap_mask);
	pl08x->memcpy.dev = &adev->dev;
	pl08x->memcpy.device_free_chan_resources = pl08x_free_chan_resources;
	pl08x->memcpy.device_prep_dma_memcpy = pl08x_prep_dma_memcpy;
	pl08x->memcpy.device_prep_dma_interrupt = pl08x_prep_dma_interrupt;
	pl08x->memcpy.device_tx_status = pl08x_dma_tx_status;
	pl08x->memcpy.device_issue_pending = pl08x_issue_pending;
	pl08x->memcpy.device_config = pl08x_config;
	pl08x->memcpy.device_pause = pl08x_pause;
	pl08x->memcpy.device_resume = pl08x_resume;
	pl08x->memcpy.device_terminate_all = pl08x_terminate_all;
	pl08x->memcpy.device_synchronize = pl08x_synchronize;
	pl08x->memcpy.src_addr_widths = PL80X_DMA_BUSWIDTHS;
	pl08x->memcpy.dst_addr_widths = PL80X_DMA_BUSWIDTHS;
	pl08x->memcpy.directions = BIT(DMA_MEM_TO_MEM);
	pl08x->memcpy.residue_granularity = DMA_RESIDUE_GRANULARITY_SEGMENT;
	if (vd->ftdmac020)
		pl08x->memcpy.copy_align = DMAENGINE_ALIGN_4_BYTES;


	/*
	 * Initialize slave engine, if the block has no signals, that means
	 * we have no slave support.
	 */
	if (vd->signals) {
		pl08x->has_slave = true;
		dma_cap_set(DMA_SLAVE, pl08x->slave.cap_mask);
		dma_cap_set(DMA_CYCLIC, pl08x->slave.cap_mask);
		pl08x->slave.dev = &adev->dev;
		pl08x->slave.device_free_chan_resources =
			pl08x_free_chan_resources;
		pl08x->slave.device_prep_dma_interrupt =
			pl08x_prep_dma_interrupt;
		pl08x->slave.device_tx_status = pl08x_dma_tx_status;
		pl08x->slave.device_issue_pending = pl08x_issue_pending;
		pl08x->slave.device_prep_slave_sg = pl08x_prep_slave_sg;
		pl08x->slave.device_prep_dma_cyclic = pl08x_prep_dma_cyclic;
		pl08x->slave.device_config = pl08x_config;
		pl08x->slave.device_pause = pl08x_pause;
		pl08x->slave.device_resume = pl08x_resume;
		pl08x->slave.device_terminate_all = pl08x_terminate_all;
		pl08x->slave.device_synchronize = pl08x_synchronize;
		pl08x->slave.src_addr_widths = PL80X_DMA_BUSWIDTHS;
		pl08x->slave.dst_addr_widths = PL80X_DMA_BUSWIDTHS;
		pl08x->slave.directions =
			BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
		pl08x->slave.residue_granularity =
			DMA_RESIDUE_GRANULARITY_SEGMENT;
	}

	/* Get the platform data */
	pl08x->pd = dev_get_platdata(&adev->dev);
	if (!pl08x->pd) {
		if (np) {
			ret = pl08x_of_probe(adev, pl08x, np);
			if (ret)
				goto out_no_platdata;
		} else {
			dev_err(&adev->dev, "no platform data supplied\n");
			ret = -EINVAL;
			goto out_no_platdata;
		}
	} else {
		pl08x->slave.filter.map = pl08x->pd->slave_map;
		pl08x->slave.filter.mapcnt = pl08x->pd->slave_map_len;
		pl08x->slave.filter.fn = pl08x_filter_fn;
	}

	/* By default, AHB1 only.  If dualmaster, from platform */
	pl08x->lli_buses = PL08X_AHB1;
	pl08x->mem_buses = PL08X_AHB1;
	if (pl08x->vd->dualmaster) {
		pl08x->lli_buses = pl08x->pd->lli_buses;
		pl08x->mem_buses = pl08x->pd->mem_buses;
	}

	if (vd->pl080s)
		pl08x->lli_words = PL080S_LLI_WORDS;
	else
		pl08x->lli_words = PL080_LLI_WORDS;
	tsfr_size = MAX_NUM_TSFR_LLIS * pl08x->lli_words * sizeof(u32);

	/* A DMA memory pool for LLIs, align on 1-byte boundary */
	pl08x->pool = dma_pool_create(DRIVER_NAME, &pl08x->adev->dev,
						tsfr_size, PL08X_ALIGN, 0);
	if (!pl08x->pool) {
		ret = -ENOMEM;
		goto out_no_lli_pool;
	}

	/* Turn on the PL08x */
	pl08x_ensure_on(pl08x);

	/* Clear any pending interrupts */
	if (vd->ftdmac020)
		/* This variant has error IRQs in bits 16-19 */
		writel(0x0000FFFF, pl08x->base + PL080_ERR_CLEAR);
	else
		writel(0x000000FF, pl08x->base + PL080_ERR_CLEAR);
	writel(0x000000FF, pl08x->base + PL080_TC_CLEAR);

	/* Attach the interrupt handler */
	ret = request_irq(adev->irq[0], pl08x_irq, 0, DRIVER_NAME, pl08x);
	if (ret) {
		dev_err(&adev->dev, "%s failed to request interrupt %d\n",
			__func__, adev->irq[0]);
		goto out_no_irq;
	}

	/* Initialize physical channels */
	pl08x->phy_chans = kzalloc((vd->channels * sizeof(*pl08x->phy_chans)),
			GFP_KERNEL);
	if (!pl08x->phy_chans) {
		ret = -ENOMEM;
		goto out_no_phychans;
	}

	for (i = 0; i < vd->channels; i++) {
		struct pl08x_phy_chan *ch = &pl08x->phy_chans[i];

		ch->id = i;
		ch->base = pl08x->base + PL080_Cx_BASE(i);
		if (vd->ftdmac020) {
			/* FTDMA020 has a special channel busy register */
			ch->reg_busy = ch->base + FTDMAC020_CH_BUSY;
			ch->reg_config = ch->base + FTDMAC020_CH_CFG;
			ch->reg_control = ch->base + FTDMAC020_CH_CSR;
			ch->reg_src = ch->base + FTDMAC020_CH_SRC_ADDR;
			ch->reg_dst = ch->base + FTDMAC020_CH_DST_ADDR;
			ch->reg_lli = ch->base + FTDMAC020_CH_LLP;
			ch->ftdmac020 = true;
		} else {
			ch->reg_config = ch->base + vd->config_offset;
			ch->reg_control = ch->base + PL080_CH_CONTROL;
			ch->reg_src = ch->base + PL080_CH_SRC_ADDR;
			ch->reg_dst = ch->base + PL080_CH_DST_ADDR;
			ch->reg_lli = ch->base + PL080_CH_LLI;
		}
		if (vd->pl080s)
			ch->pl080s = true;

		spin_lock_init(&ch->lock);

		/*
		 * Nomadik variants can have channels that are locked
		 * down for the secure world only. Lock up these channels
		 * by perpetually serving a dummy virtual channel.
		 */
		if (vd->nomadik) {
			u32 val;

			val = readl(ch->reg_config);
			if (val & (PL080N_CONFIG_ITPROT | PL080N_CONFIG_SECPROT)) {
				dev_info(&adev->dev, "physical channel %d reserved for secure access only\n", i);
				ch->locked = true;
			}
		}

		dev_dbg(&adev->dev, "physical channel %d is %s\n",
			i, pl08x_phy_channel_busy(ch) ? "BUSY" : "FREE");
	}

	/* Register as many memcpy channels as there are physical channels */
	ret = pl08x_dma_init_virtual_channels(pl08x, &pl08x->memcpy,
					      pl08x->vd->channels, false);
	if (ret <= 0) {
		dev_warn(&pl08x->adev->dev,
			 "%s failed to enumerate memcpy channels - %d\n",
			 __func__, ret);
		goto out_no_memcpy;
	}

	/* Register slave channels */
	if (pl08x->has_slave) {
		ret = pl08x_dma_init_virtual_channels(pl08x, &pl08x->slave,
					pl08x->pd->num_slave_channels, true);
		if (ret < 0) {
			dev_warn(&pl08x->adev->dev,
				 "%s failed to enumerate slave channels - %d\n",
				 __func__, ret);
			goto out_no_slave;
		}
	}

	ret = dma_async_device_register(&pl08x->memcpy);
	if (ret) {
		dev_warn(&pl08x->adev->dev,
			"%s failed to register memcpy as an async device - %d\n",
			__func__, ret);
		goto out_no_memcpy_reg;
	}

	if (pl08x->has_slave) {
		ret = dma_async_device_register(&pl08x->slave);
		if (ret) {
			dev_warn(&pl08x->adev->dev,
			"%s failed to register slave as an async device - %d\n",
			__func__, ret);
			goto out_no_slave_reg;
		}
	}

	amba_set_drvdata(adev, pl08x);
	init_pl08x_debugfs(pl08x);
	dev_info(&pl08x->adev->dev, "DMA: PL%03x%s rev%u at 0x%08llx irq %d\n",
		 amba_part(adev), pl08x->vd->pl080s ? "s" : "", amba_rev(adev),
		 (unsigned long long)adev->res.start, adev->irq[0]);

	return 0;

out_no_slave_reg:
	dma_async_device_unregister(&pl08x->memcpy);
out_no_memcpy_reg:
	if (pl08x->has_slave)
		pl08x_free_virtual_channels(&pl08x->slave);
out_no_slave:
	pl08x_free_virtual_channels(&pl08x->memcpy);
out_no_memcpy:
	kfree(pl08x->phy_chans);
out_no_phychans:
	free_irq(adev->irq[0], pl08x);
out_no_irq:
	dma_pool_destroy(pl08x->pool);
out_no_lli_pool:
out_no_platdata:
	iounmap(pl08x->base);
out_no_ioremap:
	kfree(pl08x);
out_no_pl08x:
	amba_release_regions(adev);
	return ret;
}

/* PL080 has 8 channels and the PL080 have just 2 */
static struct vendor_data vendor_pl080 = {
	.config_offset = PL080_CH_CONFIG,
	.channels = 8,
	.signals = 16,
	.dualmaster = true,
	.max_transfer_size = PL080_CONTROL_TRANSFER_SIZE_MASK,
};

static struct vendor_data vendor_nomadik = {
	.config_offset = PL080_CH_CONFIG,
	.channels = 8,
	.signals = 32,
	.dualmaster = true,
	.nomadik = true,
	.max_transfer_size = PL080_CONTROL_TRANSFER_SIZE_MASK,
};

static struct vendor_data vendor_pl080s = {
	.config_offset = PL080S_CH_CONFIG,
	.channels = 8,
	.signals = 32,
	.pl080s = true,
	.max_transfer_size = PL080S_CONTROL_TRANSFER_SIZE_MASK,
};

static struct vendor_data vendor_pl081 = {
	.config_offset = PL080_CH_CONFIG,
	.channels = 2,
	.signals = 16,
	.dualmaster = false,
	.max_transfer_size = PL080_CONTROL_TRANSFER_SIZE_MASK,
};

static struct vendor_data vendor_ftdmac020 = {
	.config_offset = PL080_CH_CONFIG,
	.ftdmac020 = true,
	.max_transfer_size = PL080_CONTROL_TRANSFER_SIZE_MASK,
};

static const struct amba_id pl08x_ids[] = {
	/* Samsung PL080S variant */
	{
		.id	= 0x0a141080,
		.mask	= 0xffffffff,
		.data	= &vendor_pl080s,
	},
	/* PL080 */
	{
		.id	= 0x00041080,
		.mask	= 0x000fffff,
		.data	= &vendor_pl080,
	},
	/* PL081 */
	{
		.id	= 0x00041081,
		.mask	= 0x000fffff,
		.data	= &vendor_pl081,
	},
	/* Nomadik 8815 PL080 variant */
	{
		.id	= 0x00280080,
		.mask	= 0x00ffffff,
		.data	= &vendor_nomadik,
	},
	/* Faraday Technology FTDMAC020 */
	{
		.id	= 0x0003b080,
		.mask	= 0x000fffff,
		.data	= &vendor_ftdmac020,
	},
	{ 0, 0 },
};

MODULE_DEVICE_TABLE(amba, pl08x_ids);

static struct amba_driver pl08x_amba_driver = {
	.drv.name	= DRIVER_NAME,
	.id_table	= pl08x_ids,
	.probe		= pl08x_probe,
};

static int __init pl08x_init(void)
{
	int retval;
	retval = amba_driver_register(&pl08x_amba_driver);
	if (retval)
		printk(KERN_WARNING DRIVER_NAME
		       "failed to register as an AMBA device (%d)\n",
		       retval);
	return retval;
}
subsys_initcall(pl08x_init);
