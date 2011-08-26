/*
 * Copyright (c) 2006 ARM Ltd.
 * Copyright (c) 2010 ST-Ericsson SA
 *
 * Author: Peter Pearse <peter.pearse@arm.com>
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is in this distribution in the file
 * called COPYING.
 *
 * Documentation: ARM DDI 0196G == PL080
 * Documentation: ARM DDI 0218E == PL081
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
 *    will then move to the next LLI entry.
 *
 * Only the former works sanely with scatter lists, so we only implement
 * the DMAC flow control method.  However, peripherals which use the LBREQ
 * and LSREQ signals (eg, MMCI) are unable to use this mode, which through
 * these hardware restrictions prevents them from using scatter DMA.
 *
 * Global TODO:
 * - Break out common code from arch/arm/mach-s3c64xx and share
 */
#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/dmaengine.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl08x.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <asm/hardware/pl080.h>

#define DRIVER_NAME	"pl08xdmac"

/**
 * struct vendor_data - vendor-specific config parameters for PL08x derivatives
 * @channels: the number of channels available in this variant
 * @dualmaster: whether this version supports dual AHB masters or not.
 */
struct vendor_data {
	u8 channels;
	bool dualmaster;
};

/*
 * PL08X private data structures
 * An LLI struct - see PL08x TRM.  Note that next uses bit[0] as a bus bit,
 * start & end do not - their bus bit info is in cctl.  Also note that these
 * are fixed 32-bit quantities.
 */
struct pl08x_lli {
	u32 src;
	u32 dst;
	u32 lli;
	u32 cctl;
};

/**
 * struct pl08x_driver_data - the local state holder for the PL08x
 * @slave: slave engine for this instance
 * @memcpy: memcpy engine for this instance
 * @base: virtual memory base (remapped) for the PL08x
 * @adev: the corresponding AMBA (PrimeCell) bus entry
 * @vd: vendor data for this PL08x variant
 * @pd: platform data passed in from the platform/machine
 * @phy_chans: array of data for the physical channels
 * @pool: a pool for the LLI descriptors
 * @pool_ctr: counter of LLIs in the pool
 * @lli_buses: bitmask to or in to LLI pointer selecting AHB port for LLI fetches
 * @mem_buses: set to indicate memory transfers on AHB2.
 * @lock: a spinlock for this struct
 */
struct pl08x_driver_data {
	struct dma_device slave;
	struct dma_device memcpy;
	void __iomem *base;
	struct amba_device *adev;
	const struct vendor_data *vd;
	struct pl08x_platform_data *pd;
	struct pl08x_phy_chan *phy_chans;
	struct dma_pool *pool;
	int pool_ctr;
	u8 lli_buses;
	u8 mem_buses;
	spinlock_t lock;
};

/*
 * PL08X specific defines
 */

/*
 * Memory boundaries: the manual for PL08x says that the controller
 * cannot read past a 1KiB boundary, so these defines are used to
 * create transfer LLIs that do not cross such boundaries.
 */
#define PL08X_BOUNDARY_SHIFT		(10)	/* 1KB 0x400 */
#define PL08X_BOUNDARY_SIZE		(1 << PL08X_BOUNDARY_SHIFT)

/* Size (bytes) of each LLI buffer allocated for one transfer */
# define PL08X_LLI_TSFR_SIZE	0x2000

/* Maximum times we call dma_pool_alloc on this pool without freeing */
#define MAX_NUM_TSFR_LLIS	(PL08X_LLI_TSFR_SIZE/sizeof(struct pl08x_lli))
#define PL08X_ALIGN		8

static inline struct pl08x_dma_chan *to_pl08x_chan(struct dma_chan *chan)
{
	return container_of(chan, struct pl08x_dma_chan, chan);
}

static inline struct pl08x_txd *to_pl08x_txd(struct dma_async_tx_descriptor *tx)
{
	return container_of(tx, struct pl08x_txd, tx);
}

/*
 * Physical channel handling
 */

/* Whether a certain channel is busy or not */
static int pl08x_phy_channel_busy(struct pl08x_phy_chan *ch)
{
	unsigned int val;

	val = readl(ch->base + PL080_CH_CONFIG);
	return val & PL080_CONFIG_ACTIVE;
}

/*
 * Set the initial DMA register values i.e. those for the first LLI
 * The next LLI pointer and the configuration interrupt bit have
 * been set when the LLIs were constructed.  Poke them into the hardware
 * and start the transfer.
 */
static void pl08x_start_txd(struct pl08x_dma_chan *plchan,
	struct pl08x_txd *txd)
{
	struct pl08x_driver_data *pl08x = plchan->host;
	struct pl08x_phy_chan *phychan = plchan->phychan;
	struct pl08x_lli *lli = &txd->llis_va[0];
	u32 val;

	plchan->at = txd;

	/* Wait for channel inactive */
	while (pl08x_phy_channel_busy(phychan))
		cpu_relax();

	dev_vdbg(&pl08x->adev->dev,
		"WRITE channel %d: csrc=0x%08x, cdst=0x%08x, "
		"clli=0x%08x, cctl=0x%08x, ccfg=0x%08x\n",
		phychan->id, lli->src, lli->dst, lli->lli, lli->cctl,
		txd->ccfg);

	writel(lli->src, phychan->base + PL080_CH_SRC_ADDR);
	writel(lli->dst, phychan->base + PL080_CH_DST_ADDR);
	writel(lli->lli, phychan->base + PL080_CH_LLI);
	writel(lli->cctl, phychan->base + PL080_CH_CONTROL);
	writel(txd->ccfg, phychan->base + PL080_CH_CONFIG);

	/* Enable the DMA channel */
	/* Do not access config register until channel shows as disabled */
	while (readl(pl08x->base + PL080_EN_CHAN) & (1 << phychan->id))
		cpu_relax();

	/* Do not access config register until channel shows as inactive */
	val = readl(phychan->base + PL080_CH_CONFIG);
	while ((val & PL080_CONFIG_ACTIVE) || (val & PL080_CONFIG_ENABLE))
		val = readl(phychan->base + PL080_CH_CONFIG);

	writel(val | PL080_CONFIG_ENABLE, phychan->base + PL080_CH_CONFIG);
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

	/* Set the HALT bit and wait for the FIFO to drain */
	val = readl(ch->base + PL080_CH_CONFIG);
	val |= PL080_CONFIG_HALT;
	writel(val, ch->base + PL080_CH_CONFIG);

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

	/* Clear the HALT bit */
	val = readl(ch->base + PL080_CH_CONFIG);
	val &= ~PL080_CONFIG_HALT;
	writel(val, ch->base + PL080_CH_CONFIG);
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
	u32 val = readl(ch->base + PL080_CH_CONFIG);

	val &= ~(PL080_CONFIG_ENABLE | PL080_CONFIG_ERR_IRQ_MASK |
	         PL080_CONFIG_TC_IRQ_MASK);

	writel(val, ch->base + PL080_CH_CONFIG);

	writel(1 << ch->id, pl08x->base + PL080_ERR_CLEAR);
	writel(1 << ch->id, pl08x->base + PL080_TC_CLEAR);
}

static inline u32 get_bytes_in_cctl(u32 cctl)
{
	/* The source width defines the number of bytes */
	u32 bytes = cctl & PL080_CONTROL_TRANSFER_SIZE_MASK;

	switch (cctl >> PL080_CONTROL_SWIDTH_SHIFT) {
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
	struct pl08x_phy_chan *ch;
	struct pl08x_txd *txd;
	unsigned long flags;
	size_t bytes = 0;

	spin_lock_irqsave(&plchan->lock, flags);
	ch = plchan->phychan;
	txd = plchan->at;

	/*
	 * Follow the LLIs to get the number of remaining
	 * bytes in the currently active transaction.
	 */
	if (ch && txd) {
		u32 clli = readl(ch->base + PL080_CH_LLI) & ~PL080_LLI_LM_AHB2;

		/* First get the remaining bytes in the active transfer */
		bytes = get_bytes_in_cctl(readl(ch->base + PL080_CH_CONTROL));

		if (clli) {
			struct pl08x_lli *llis_va = txd->llis_va;
			dma_addr_t llis_bus = txd->llis_bus;
			int index;

			BUG_ON(clli < llis_bus || clli >= llis_bus +
				sizeof(struct pl08x_lli) * MAX_NUM_TSFR_LLIS);

			/*
			 * Locate the next LLI - as this is an array,
			 * it's simple maths to find.
			 */
			index = (clli - llis_bus) / sizeof(struct pl08x_lli);

			for (; index < MAX_NUM_TSFR_LLIS; index++) {
				bytes += get_bytes_in_cctl(llis_va[index].cctl);

				/*
				 * A LLI pointer of 0 terminates the LLI list
				 */
				if (!llis_va[index].lli)
					break;
			}
		}
	}

	/* Sum up all queued transactions */
	if (!list_empty(&plchan->pend_list)) {
		struct pl08x_txd *txdi;
		list_for_each_entry(txdi, &plchan->pend_list, node) {
			bytes += txdi->len;
		}
	}

	spin_unlock_irqrestore(&plchan->lock, flags);

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

		if (!ch->serving) {
			ch->serving = virt_chan;
			ch->signal = -1;
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

static inline void pl08x_put_phy_channel(struct pl08x_driver_data *pl08x,
					 struct pl08x_phy_chan *ch)
{
	unsigned long flags;

	spin_lock_irqsave(&ch->lock, flags);

	/* Stop the channel and clear its interrupts */
	pl08x_terminate_phy_chan(pl08x, ch);

	/* Mark it as free */
	ch->serving = NULL;
	spin_unlock_irqrestore(&ch->lock, flags);
}

/*
 * LLI handling
 */

static inline unsigned int pl08x_get_bytes_for_cctl(unsigned int coded)
{
	switch (coded) {
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

static inline u32 pl08x_cctl_bits(u32 cctl, u8 srcwidth, u8 dstwidth,
				  size_t tsize)
{
	u32 retbits = cctl;

	/* Remove all src, dst and transfer size bits */
	retbits &= ~PL080_CONTROL_DWIDTH_MASK;
	retbits &= ~PL080_CONTROL_SWIDTH_MASK;
	retbits &= ~PL080_CONTROL_TRANSFER_SIZE_MASK;

	/* Then set the bits according to the parameters */
	switch (srcwidth) {
	case 1:
		retbits |= PL080_WIDTH_8BIT << PL080_CONTROL_SWIDTH_SHIFT;
		break;
	case 2:
		retbits |= PL080_WIDTH_16BIT << PL080_CONTROL_SWIDTH_SHIFT;
		break;
	case 4:
		retbits |= PL080_WIDTH_32BIT << PL080_CONTROL_SWIDTH_SHIFT;
		break;
	default:
		BUG();
		break;
	}

	switch (dstwidth) {
	case 1:
		retbits |= PL080_WIDTH_8BIT << PL080_CONTROL_DWIDTH_SHIFT;
		break;
	case 2:
		retbits |= PL080_WIDTH_16BIT << PL080_CONTROL_DWIDTH_SHIFT;
		break;
	case 4:
		retbits |= PL080_WIDTH_32BIT << PL080_CONTROL_DWIDTH_SHIFT;
		break;
	default:
		BUG();
		break;
	}

	retbits |= tsize << PL080_CONTROL_TRANSFER_SIZE_SHIFT;
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
 * Autoselect a master bus to use for the transfer this prefers the
 * destination bus if both available if fixed address on one bus the
 * other will be chosen
 */
static void pl08x_choose_master_bus(struct pl08x_lli_build_data *bd,
	struct pl08x_bus_data **mbus, struct pl08x_bus_data **sbus, u32 cctl)
{
	if (!(cctl & PL080_CONTROL_DST_INCR)) {
		*mbus = &bd->srcbus;
		*sbus = &bd->dstbus;
	} else if (!(cctl & PL080_CONTROL_SRC_INCR)) {
		*mbus = &bd->dstbus;
		*sbus = &bd->srcbus;
	} else {
		if (bd->dstbus.buswidth == 4) {
			*mbus = &bd->dstbus;
			*sbus = &bd->srcbus;
		} else if (bd->srcbus.buswidth == 4) {
			*mbus = &bd->srcbus;
			*sbus = &bd->dstbus;
		} else if (bd->dstbus.buswidth == 2) {
			*mbus = &bd->dstbus;
			*sbus = &bd->srcbus;
		} else if (bd->srcbus.buswidth == 2) {
			*mbus = &bd->srcbus;
			*sbus = &bd->dstbus;
		} else {
			/* bd->srcbus.buswidth == 1 */
			*mbus = &bd->dstbus;
			*sbus = &bd->srcbus;
		}
	}
}

/*
 * Fills in one LLI for a certain transfer descriptor and advance the counter
 */
static void pl08x_fill_lli_for_desc(struct pl08x_lli_build_data *bd,
	int num_llis, int len, u32 cctl)
{
	struct pl08x_lli *llis_va = bd->txd->llis_va;
	dma_addr_t llis_bus = bd->txd->llis_bus;

	BUG_ON(num_llis >= MAX_NUM_TSFR_LLIS);

	llis_va[num_llis].cctl = cctl;
	llis_va[num_llis].src = bd->srcbus.addr;
	llis_va[num_llis].dst = bd->dstbus.addr;
	llis_va[num_llis].lli = llis_bus + (num_llis + 1) * sizeof(struct pl08x_lli);
	llis_va[num_llis].lli |= bd->lli_bus;

	if (cctl & PL080_CONTROL_SRC_INCR)
		bd->srcbus.addr += len;
	if (cctl & PL080_CONTROL_DST_INCR)
		bd->dstbus.addr += len;

	BUG_ON(bd->remainder < len);

	bd->remainder -= len;
}

/*
 * Return number of bytes to fill to boundary, or len.
 * This calculation works for any value of addr.
 */
static inline size_t pl08x_pre_boundary(u32 addr, size_t len)
{
	size_t boundary_len = PL08X_BOUNDARY_SIZE -
			(addr & (PL08X_BOUNDARY_SIZE - 1));

	return min(boundary_len, len);
}

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
	u32 cctl;
	size_t max_bytes_per_lli;
	size_t total_bytes = 0;
	struct pl08x_lli *llis_va;

	txd->llis_va = dma_pool_alloc(pl08x->pool, GFP_NOWAIT,
				      &txd->llis_bus);
	if (!txd->llis_va) {
		dev_err(&pl08x->adev->dev, "%s no memory for llis\n", __func__);
		return 0;
	}

	pl08x->pool_ctr++;

	/* Get the default CCTL */
	cctl = txd->cctl;

	bd.txd = txd;
	bd.srcbus.addr = txd->src_addr;
	bd.dstbus.addr = txd->dst_addr;
	bd.lli_bus = (pl08x->lli_buses & PL08X_AHB2) ? PL080_LLI_LM_AHB2 : 0;

	/* Find maximum width of the source bus */
	bd.srcbus.maxwidth =
		pl08x_get_bytes_for_cctl((cctl & PL080_CONTROL_SWIDTH_MASK) >>
				       PL080_CONTROL_SWIDTH_SHIFT);

	/* Find maximum width of the destination bus */
	bd.dstbus.maxwidth =
		pl08x_get_bytes_for_cctl((cctl & PL080_CONTROL_DWIDTH_MASK) >>
				       PL080_CONTROL_DWIDTH_SHIFT);

	/* Set up the bus widths to the maximum */
	bd.srcbus.buswidth = bd.srcbus.maxwidth;
	bd.dstbus.buswidth = bd.dstbus.maxwidth;

	/*
	 * Bytes transferred == tsize * MIN(buswidths), not max(buswidths)
	 */
	max_bytes_per_lli = min(bd.srcbus.buswidth, bd.dstbus.buswidth) *
		PL080_CONTROL_TRANSFER_SIZE_MASK;

	/* We need to count this down to zero */
	bd.remainder = txd->len;

	/*
	 * Choose bus to align to
	 * - prefers destination bus if both available
	 * - if fixed address on one bus chooses other
	 */
	pl08x_choose_master_bus(&bd, &mbus, &sbus, cctl);

	dev_vdbg(&pl08x->adev->dev, "src=0x%08x%s/%u dst=0x%08x%s/%u len=%zu llimax=%zu\n",
		 bd.srcbus.addr, cctl & PL080_CONTROL_SRC_INCR ? "+" : "",
		 bd.srcbus.buswidth,
		 bd.dstbus.addr, cctl & PL080_CONTROL_DST_INCR ? "+" : "",
		 bd.dstbus.buswidth,
		 bd.remainder, max_bytes_per_lli);
	dev_vdbg(&pl08x->adev->dev, "mbus=%s sbus=%s\n",
		 mbus == &bd.srcbus ? "src" : "dst",
		 sbus == &bd.srcbus ? "src" : "dst");

	if (txd->len < mbus->buswidth) {
		/* Less than a bus width available - send as single bytes */
		while (bd.remainder) {
			dev_vdbg(&pl08x->adev->dev,
				 "%s single byte LLIs for a transfer of "
				 "less than a bus width (remain 0x%08x)\n",
				 __func__, bd.remainder);
			cctl = pl08x_cctl_bits(cctl, 1, 1, 1);
			pl08x_fill_lli_for_desc(&bd, num_llis++, 1, cctl);
			total_bytes++;
		}
	} else {
		/* Make one byte LLIs until master bus is aligned */
		while ((mbus->addr) % (mbus->buswidth)) {
			dev_vdbg(&pl08x->adev->dev,
				"%s adjustment lli for less than bus width "
				 "(remain 0x%08x)\n",
				 __func__, bd.remainder);
			cctl = pl08x_cctl_bits(cctl, 1, 1, 1);
			pl08x_fill_lli_for_desc(&bd, num_llis++, 1, cctl);
			total_bytes++;
		}

		/*
		 * Master now aligned
		 * - if slave is not then we must set its width down
		 */
		if (sbus->addr % sbus->buswidth) {
			dev_dbg(&pl08x->adev->dev,
				"%s set down bus width to one byte\n",
				 __func__);

			sbus->buswidth = 1;
		}

		/*
		 * Make largest possible LLIs until less than one bus
		 * width left
		 */
		while (bd.remainder > (mbus->buswidth - 1)) {
			size_t lli_len, target_len, tsize, odd_bytes;

			/*
			 * If enough left try to send max possible,
			 * otherwise try to send the remainder
			 */
			target_len = min(bd.remainder, max_bytes_per_lli);

			/*
			 * Set bus lengths for incrementing buses to the
			 * number of bytes which fill to next memory boundary,
			 * limiting on the target length calculated above.
			 */
			if (cctl & PL080_CONTROL_SRC_INCR)
				bd.srcbus.fill_bytes =
					pl08x_pre_boundary(bd.srcbus.addr,
						target_len);
			else
				bd.srcbus.fill_bytes = target_len;

			if (cctl & PL080_CONTROL_DST_INCR)
				bd.dstbus.fill_bytes =
					pl08x_pre_boundary(bd.dstbus.addr,
						target_len);
			else
				bd.dstbus.fill_bytes = target_len;

			/* Find the nearest */
			lli_len	= min(bd.srcbus.fill_bytes,
				      bd.dstbus.fill_bytes);

			BUG_ON(lli_len > bd.remainder);

			if (lli_len <= 0) {
				dev_err(&pl08x->adev->dev,
					"%s lli_len is %zu, <= 0\n",
						__func__, lli_len);
				return 0;
			}

			if (lli_len == target_len) {
				/*
				 * Can send what we wanted.
				 * Maintain alignment
				 */
				lli_len	= (lli_len/mbus->buswidth) *
							mbus->buswidth;
				odd_bytes = 0;
			} else {
				/*
				 * So now we know how many bytes to transfer
				 * to get to the nearest boundary.  The next
				 * LLI will past the boundary.  However, we
				 * may be working to a boundary on the slave
				 * bus.  We need to ensure the master stays
				 * aligned, and that we are working in
				 * multiples of the bus widths.
				 */
				odd_bytes = lli_len % mbus->buswidth;
				lli_len -= odd_bytes;

			}

			if (lli_len) {
				/*
				 * Check against minimum bus alignment:
				 * Calculate actual transfer size in relation
				 * to bus width an get a maximum remainder of
				 * the smallest bus width - 1
				 */
				/* FIXME: use round_down()? */
				tsize = lli_len / min(mbus->buswidth,
						      sbus->buswidth);
				lli_len	= tsize * min(mbus->buswidth,
						      sbus->buswidth);

				if (target_len != lli_len) {
					dev_vdbg(&pl08x->adev->dev,
					"%s can't send what we want. Desired 0x%08zx, lli of 0x%08zx bytes in txd of 0x%08zx\n",
					__func__, target_len, lli_len, txd->len);
				}

				cctl = pl08x_cctl_bits(cctl,
						       bd.srcbus.buswidth,
						       bd.dstbus.buswidth,
						       tsize);

				dev_vdbg(&pl08x->adev->dev,
					"%s fill lli with single lli chunk of size 0x%08zx (remainder 0x%08zx)\n",
					__func__, lli_len, bd.remainder);
				pl08x_fill_lli_for_desc(&bd, num_llis++,
					lli_len, cctl);
				total_bytes += lli_len;
			}


			if (odd_bytes) {
				/*
				 * Creep past the boundary, maintaining
				 * master alignment
				 */
				int j;
				for (j = 0; (j < mbus->buswidth)
						&& (bd.remainder); j++) {
					cctl = pl08x_cctl_bits(cctl, 1, 1, 1);
					dev_vdbg(&pl08x->adev->dev,
						"%s align with boundary, single byte (remain 0x%08zx)\n",
						__func__, bd.remainder);
					pl08x_fill_lli_for_desc(&bd,
						num_llis++, 1, cctl);
					total_bytes++;
				}
			}
		}

		/*
		 * Send any odd bytes
		 */
		while (bd.remainder) {
			cctl = pl08x_cctl_bits(cctl, 1, 1, 1);
			dev_vdbg(&pl08x->adev->dev,
				"%s align with boundary, single odd byte (remain %zu)\n",
				__func__, bd.remainder);
			pl08x_fill_lli_for_desc(&bd, num_llis++, 1, cctl);
			total_bytes++;
		}
	}
	if (total_bytes != txd->len) {
		dev_err(&pl08x->adev->dev,
			"%s size of encoded lli:s don't match total txd, transferred 0x%08zx from size 0x%08zx\n",
			__func__, total_bytes, txd->len);
		return 0;
	}

	if (num_llis >= MAX_NUM_TSFR_LLIS) {
		dev_err(&pl08x->adev->dev,
			"%s need to increase MAX_NUM_TSFR_LLIS from 0x%08x\n",
			__func__, (u32) MAX_NUM_TSFR_LLIS);
		return 0;
	}

	llis_va = txd->llis_va;
	/* The final LLI terminates the LLI. */
	llis_va[num_llis - 1].lli = 0;
	/* The final LLI element shall also fire an interrupt. */
	llis_va[num_llis - 1].cctl |= PL080_CONTROL_TC_IRQ_EN;

#ifdef VERBOSE_DEBUG
	{
		int i;

		dev_vdbg(&pl08x->adev->dev,
			 "%-3s %-9s  %-10s %-10s %-10s %s\n",
			 "lli", "", "csrc", "cdst", "clli", "cctl");
		for (i = 0; i < num_llis; i++) {
			dev_vdbg(&pl08x->adev->dev,
				 "%3d @%p: 0x%08x 0x%08x 0x%08x 0x%08x\n",
				 i, &llis_va[i], llis_va[i].src,
				 llis_va[i].dst, llis_va[i].lli, llis_va[i].cctl
				);
		}
	}
#endif

	return num_llis;
}

/* You should call this with the struct pl08x lock held */
static void pl08x_free_txd(struct pl08x_driver_data *pl08x,
			   struct pl08x_txd *txd)
{
	/* Free the LLI */
	dma_pool_free(pl08x->pool, txd->llis_va, txd->llis_bus);

	pl08x->pool_ctr--;

	kfree(txd);
}

static void pl08x_free_txd_list(struct pl08x_driver_data *pl08x,
				struct pl08x_dma_chan *plchan)
{
	struct pl08x_txd *txdi = NULL;
	struct pl08x_txd *next;

	if (!list_empty(&plchan->pend_list)) {
		list_for_each_entry_safe(txdi,
					 next, &plchan->pend_list, node) {
			list_del(&txdi->node);
			pl08x_free_txd(pl08x, txdi);
		}
	}
}

/*
 * The DMA ENGINE API
 */
static int pl08x_alloc_chan_resources(struct dma_chan *chan)
{
	return 0;
}

static void pl08x_free_chan_resources(struct dma_chan *chan)
{
}

/*
 * This should be called with the channel plchan->lock held
 */
static int prep_phy_channel(struct pl08x_dma_chan *plchan,
			    struct pl08x_txd *txd)
{
	struct pl08x_driver_data *pl08x = plchan->host;
	struct pl08x_phy_chan *ch;
	int ret;

	/* Check if we already have a channel */
	if (plchan->phychan)
		return 0;

	ch = pl08x_get_phy_channel(pl08x, plchan);
	if (!ch) {
		/* No physical channel available, cope with it */
		dev_dbg(&pl08x->adev->dev, "no physical channel available for xfer on %s\n", plchan->name);
		return -EBUSY;
	}

	/*
	 * OK we have a physical channel: for memcpy() this is all we
	 * need, but for slaves the physical signals may be muxed!
	 * Can the platform allow us to use this channel?
	 */
	if (plchan->slave &&
	    ch->signal < 0 &&
	    pl08x->pd->get_signal) {
		ret = pl08x->pd->get_signal(plchan);
		if (ret < 0) {
			dev_dbg(&pl08x->adev->dev,
				"unable to use physical channel %d for transfer on %s due to platform restrictions\n",
				ch->id, plchan->name);
			/* Release physical channel & return */
			pl08x_put_phy_channel(pl08x, ch);
			return -EBUSY;
		}
		ch->signal = ret;

		/* Assign the flow control signal to this channel */
		if (txd->direction == DMA_TO_DEVICE)
			txd->ccfg |= ch->signal << PL080_CONFIG_DST_SEL_SHIFT;
		else if (txd->direction == DMA_FROM_DEVICE)
			txd->ccfg |= ch->signal << PL080_CONFIG_SRC_SEL_SHIFT;
	}

	dev_dbg(&pl08x->adev->dev, "allocated physical channel %d and signal %d for xfer on %s\n",
		 ch->id,
		 ch->signal,
		 plchan->name);

	plchan->phychan_hold++;
	plchan->phychan = ch;

	return 0;
}

static void release_phy_channel(struct pl08x_dma_chan *plchan)
{
	struct pl08x_driver_data *pl08x = plchan->host;

	if ((plchan->phychan->signal >= 0) && pl08x->pd->put_signal) {
		pl08x->pd->put_signal(plchan);
		plchan->phychan->signal = -1;
	}
	pl08x_put_phy_channel(pl08x, plchan->phychan);
	plchan->phychan = NULL;
}

static dma_cookie_t pl08x_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct pl08x_dma_chan *plchan = to_pl08x_chan(tx->chan);
	struct pl08x_txd *txd = to_pl08x_txd(tx);
	unsigned long flags;

	spin_lock_irqsave(&plchan->lock, flags);

	plchan->chan.cookie += 1;
	if (plchan->chan.cookie < 0)
		plchan->chan.cookie = 1;
	tx->cookie = plchan->chan.cookie;

	/* Put this onto the pending list */
	list_add_tail(&txd->node, &plchan->pend_list);

	/*
	 * If there was no physical channel available for this memcpy,
	 * stack the request up and indicate that the channel is waiting
	 * for a free physical channel.
	 */
	if (!plchan->slave && !plchan->phychan) {
		/* Do this memcpy whenever there is a channel ready */
		plchan->state = PL08X_CHAN_WAITING;
		plchan->waiting = txd;
	} else {
		plchan->phychan_hold--;
	}

	spin_unlock_irqrestore(&plchan->lock, flags);

	return tx->cookie;
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
static enum dma_status
pl08x_dma_tx_status(struct dma_chan *chan,
		    dma_cookie_t cookie,
		    struct dma_tx_state *txstate)
{
	struct pl08x_dma_chan *plchan = to_pl08x_chan(chan);
	dma_cookie_t last_used;
	dma_cookie_t last_complete;
	enum dma_status ret;
	u32 bytesleft = 0;

	last_used = plchan->chan.cookie;
	last_complete = plchan->lc;

	ret = dma_async_is_complete(cookie, last_complete, last_used);
	if (ret == DMA_SUCCESS) {
		dma_set_tx_state(txstate, last_complete, last_used, 0);
		return ret;
	}

	/*
	 * This cookie not complete yet
	 */
	last_used = plchan->chan.cookie;
	last_complete = plchan->lc;

	/* Get number of bytes left in the active transactions and queue */
	bytesleft = pl08x_getbytes_chan(plchan);

	dma_set_tx_state(txstate, last_complete, last_used,
			 bytesleft);

	if (plchan->state == PL08X_CHAN_PAUSED)
		return DMA_PAUSED;

	/* Whether waiting or running, we're in progress */
	return DMA_IN_PROGRESS;
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
static u32 pl08x_select_bus(u8 src, u8 dst)
{
	u32 cctl = 0;

	if (!(dst & PL08X_AHB1) || ((dst & PL08X_AHB2) && (src & PL08X_AHB1)))
		cctl |= PL080_CONTROL_DST_AHB2;
	if (!(src & PL08X_AHB1) || ((src & PL08X_AHB2) && !(dst & PL08X_AHB2)))
		cctl |= PL080_CONTROL_SRC_AHB2;

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

static int dma_set_runtime_config(struct dma_chan *chan,
				  struct dma_slave_config *config)
{
	struct pl08x_dma_chan *plchan = to_pl08x_chan(chan);
	struct pl08x_driver_data *pl08x = plchan->host;
	enum dma_slave_buswidth addr_width;
	u32 width, burst, maxburst;
	u32 cctl = 0;

	if (!plchan->slave)
		return -EINVAL;

	/* Transfer direction */
	plchan->runtime_direction = config->direction;
	if (config->direction == DMA_TO_DEVICE) {
		addr_width = config->dst_addr_width;
		maxburst = config->dst_maxburst;
	} else if (config->direction == DMA_FROM_DEVICE) {
		addr_width = config->src_addr_width;
		maxburst = config->src_maxburst;
	} else {
		dev_err(&pl08x->adev->dev,
			"bad runtime_config: alien transfer direction\n");
		return -EINVAL;
	}

	width = pl08x_width(addr_width);
	if (width == ~0) {
		dev_err(&pl08x->adev->dev,
			"bad runtime_config: alien address width\n");
		return -EINVAL;
	}

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

	if (plchan->runtime_direction == DMA_FROM_DEVICE) {
		plchan->src_addr = config->src_addr;
		plchan->src_cctl = pl08x_cctl(cctl) | PL080_CONTROL_DST_INCR |
			pl08x_select_bus(plchan->cd->periph_buses,
					 pl08x->mem_buses);
	} else {
		plchan->dst_addr = config->dst_addr;
		plchan->dst_cctl = pl08x_cctl(cctl) | PL080_CONTROL_SRC_INCR |
			pl08x_select_bus(pl08x->mem_buses,
					 plchan->cd->periph_buses);
	}

	dev_dbg(&pl08x->adev->dev,
		"configured channel %s (%s) for %s, data width %d, "
		"maxburst %d words, LE, CCTL=0x%08x\n",
		dma_chan_name(chan), plchan->name,
		(config->direction == DMA_FROM_DEVICE) ? "RX" : "TX",
		addr_width,
		maxburst,
		cctl);

	return 0;
}

/*
 * Slave transactions callback to the slave device to allow
 * synchronization of slave DMA signals with the DMAC enable
 */
static void pl08x_issue_pending(struct dma_chan *chan)
{
	struct pl08x_dma_chan *plchan = to_pl08x_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&plchan->lock, flags);
	/* Something is already active, or we're waiting for a channel... */
	if (plchan->at || plchan->state == PL08X_CHAN_WAITING) {
		spin_unlock_irqrestore(&plchan->lock, flags);
		return;
	}

	/* Take the first element in the queue and execute it */
	if (!list_empty(&plchan->pend_list)) {
		struct pl08x_txd *next;

		next = list_first_entry(&plchan->pend_list,
					struct pl08x_txd,
					node);
		list_del(&next->node);
		plchan->state = PL08X_CHAN_RUNNING;

		pl08x_start_txd(plchan, next);
	}

	spin_unlock_irqrestore(&plchan->lock, flags);
}

static int pl08x_prep_channel_resources(struct pl08x_dma_chan *plchan,
					struct pl08x_txd *txd)
{
	struct pl08x_driver_data *pl08x = plchan->host;
	unsigned long flags;
	int num_llis, ret;

	num_llis = pl08x_fill_llis_for_desc(pl08x, txd);
	if (!num_llis) {
		kfree(txd);
		return -EINVAL;
	}

	spin_lock_irqsave(&plchan->lock, flags);

	/*
	 * See if we already have a physical channel allocated,
	 * else this is the time to try to get one.
	 */
	ret = prep_phy_channel(plchan, txd);
	if (ret) {
		/*
		 * No physical channel was available.
		 *
		 * memcpy transfers can be sorted out at submission time.
		 *
		 * Slave transfers may have been denied due to platform
		 * channel muxing restrictions.  Since there is no guarantee
		 * that this will ever be resolved, and the signal must be
		 * acquired AFTER acquiring the physical channel, we will let
		 * them be NACK:ed with -EBUSY here. The drivers can retry
		 * the prep() call if they are eager on doing this using DMA.
		 */
		if (plchan->slave) {
			pl08x_free_txd_list(pl08x, plchan);
			pl08x_free_txd(pl08x, txd);
			spin_unlock_irqrestore(&plchan->lock, flags);
			return -EBUSY;
		}
	} else
		/*
		 * Else we're all set, paused and ready to roll, status
		 * will switch to PL08X_CHAN_RUNNING when we call
		 * issue_pending(). If there is something running on the
		 * channel already we don't change its state.
		 */
		if (plchan->state == PL08X_CHAN_IDLE)
			plchan->state = PL08X_CHAN_PAUSED;

	spin_unlock_irqrestore(&plchan->lock, flags);

	return 0;
}

static struct pl08x_txd *pl08x_get_txd(struct pl08x_dma_chan *plchan,
	unsigned long flags)
{
	struct pl08x_txd *txd = kzalloc(sizeof(struct pl08x_txd), GFP_NOWAIT);

	if (txd) {
		dma_async_tx_descriptor_init(&txd->tx, &plchan->chan);
		txd->tx.flags = flags;
		txd->tx.tx_submit = pl08x_tx_submit;
		INIT_LIST_HEAD(&txd->node);

		/* Always enable error and terminal interrupts */
		txd->ccfg = PL080_CONFIG_ERR_IRQ_MASK |
			    PL080_CONFIG_TC_IRQ_MASK;
	}
	return txd;
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
	int ret;

	txd = pl08x_get_txd(plchan, flags);
	if (!txd) {
		dev_err(&pl08x->adev->dev,
			"%s no memory for descriptor\n", __func__);
		return NULL;
	}

	txd->direction = DMA_NONE;
	txd->src_addr = src;
	txd->dst_addr = dest;
	txd->len = len;

	/* Set platform data for m2m */
	txd->ccfg |= PL080_FLOW_MEM2MEM << PL080_CONFIG_FLOW_CONTROL_SHIFT;
	txd->cctl = pl08x->pd->memcpy_channel.cctl &
			~(PL080_CONTROL_DST_AHB2 | PL080_CONTROL_SRC_AHB2);

	/* Both to be incremented or the code will break */
	txd->cctl |= PL080_CONTROL_SRC_INCR | PL080_CONTROL_DST_INCR;

	if (pl08x->vd->dualmaster)
		txd->cctl |= pl08x_select_bus(pl08x->mem_buses,
					      pl08x->mem_buses);

	ret = pl08x_prep_channel_resources(plchan, txd);
	if (ret)
		return NULL;

	return &txd->tx;
}

static struct dma_async_tx_descriptor *pl08x_prep_slave_sg(
		struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_data_direction direction,
		unsigned long flags)
{
	struct pl08x_dma_chan *plchan = to_pl08x_chan(chan);
	struct pl08x_driver_data *pl08x = plchan->host;
	struct pl08x_txd *txd;
	int ret;

	/*
	 * Current implementation ASSUMES only one sg
	 */
	if (sg_len != 1) {
		dev_err(&pl08x->adev->dev, "%s prepared too long sglist\n",
			__func__);
		BUG();
	}

	dev_dbg(&pl08x->adev->dev, "%s prepare transaction of %d bytes from %s\n",
		__func__, sgl->length, plchan->name);

	txd = pl08x_get_txd(plchan, flags);
	if (!txd) {
		dev_err(&pl08x->adev->dev, "%s no txd\n", __func__);
		return NULL;
	}

	if (direction != plchan->runtime_direction)
		dev_err(&pl08x->adev->dev, "%s DMA setup does not match "
			"the direction configured for the PrimeCell\n",
			__func__);

	/*
	 * Set up addresses, the PrimeCell configured address
	 * will take precedence since this may configure the
	 * channel target address dynamically at runtime.
	 */
	txd->direction = direction;
	txd->len = sgl->length;

	if (direction == DMA_TO_DEVICE) {
		txd->ccfg |= PL080_FLOW_MEM2PER << PL080_CONFIG_FLOW_CONTROL_SHIFT;
		txd->cctl = plchan->dst_cctl;
		txd->src_addr = sgl->dma_address;
		txd->dst_addr = plchan->dst_addr;
	} else if (direction == DMA_FROM_DEVICE) {
		txd->ccfg |= PL080_FLOW_PER2MEM << PL080_CONFIG_FLOW_CONTROL_SHIFT;
		txd->cctl = plchan->src_cctl;
		txd->src_addr = plchan->src_addr;
		txd->dst_addr = sgl->dma_address;
	} else {
		dev_err(&pl08x->adev->dev,
			"%s direction unsupported\n", __func__);
		return NULL;
	}

	ret = pl08x_prep_channel_resources(plchan, txd);
	if (ret)
		return NULL;

	return &txd->tx;
}

static int pl08x_control(struct dma_chan *chan, enum dma_ctrl_cmd cmd,
			 unsigned long arg)
{
	struct pl08x_dma_chan *plchan = to_pl08x_chan(chan);
	struct pl08x_driver_data *pl08x = plchan->host;
	unsigned long flags;
	int ret = 0;

	/* Controls applicable to inactive channels */
	if (cmd == DMA_SLAVE_CONFIG) {
		return dma_set_runtime_config(chan,
					      (struct dma_slave_config *)arg);
	}

	/*
	 * Anything succeeds on channels with no physical allocation and
	 * no queued transfers.
	 */
	spin_lock_irqsave(&plchan->lock, flags);
	if (!plchan->phychan && !plchan->at) {
		spin_unlock_irqrestore(&plchan->lock, flags);
		return 0;
	}

	switch (cmd) {
	case DMA_TERMINATE_ALL:
		plchan->state = PL08X_CHAN_IDLE;

		if (plchan->phychan) {
			pl08x_terminate_phy_chan(pl08x, plchan->phychan);

			/*
			 * Mark physical channel as free and free any slave
			 * signal
			 */
			release_phy_channel(plchan);
		}
		/* Dequeue jobs and free LLIs */
		if (plchan->at) {
			pl08x_free_txd(pl08x, plchan->at);
			plchan->at = NULL;
		}
		/* Dequeue jobs not yet fired as well */
		pl08x_free_txd_list(pl08x, plchan);
		break;
	case DMA_PAUSE:
		pl08x_pause_phy_chan(plchan->phychan);
		plchan->state = PL08X_CHAN_PAUSED;
		break;
	case DMA_RESUME:
		pl08x_resume_phy_chan(plchan->phychan);
		plchan->state = PL08X_CHAN_RUNNING;
		break;
	default:
		/* Unknown command */
		ret = -ENXIO;
		break;
	}

	spin_unlock_irqrestore(&plchan->lock, flags);

	return ret;
}

bool pl08x_filter_id(struct dma_chan *chan, void *chan_id)
{
	struct pl08x_dma_chan *plchan = to_pl08x_chan(chan);
	char *name = chan_id;

	/* Check that the channel is not taken! */
	if (!strcmp(plchan->name, name))
		return true;

	return false;
}

/*
 * Just check that the device is there and active
 * TODO: turn this bit on/off depending on the number of physical channels
 * actually used, if it is zero... well shut it off. That will save some
 * power. Cut the clock at the same time.
 */
static void pl08x_ensure_on(struct pl08x_driver_data *pl08x)
{
	u32 val;

	val = readl(pl08x->base + PL080_CONFIG);
	val &= ~(PL080_CONFIG_M2_BE | PL080_CONFIG_M1_BE | PL080_CONFIG_ENABLE);
	/* We implicitly clear bit 1 and that means little-endian mode */
	val |= PL080_CONFIG_ENABLE;
	writel(val, pl08x->base + PL080_CONFIG);
}

static void pl08x_unmap_buffers(struct pl08x_txd *txd)
{
	struct device *dev = txd->tx.chan->device->dev;

	if (!(txd->tx.flags & DMA_COMPL_SKIP_SRC_UNMAP)) {
		if (txd->tx.flags & DMA_COMPL_SRC_UNMAP_SINGLE)
			dma_unmap_single(dev, txd->src_addr, txd->len,
				DMA_TO_DEVICE);
		else
			dma_unmap_page(dev, txd->src_addr, txd->len,
				DMA_TO_DEVICE);
	}
	if (!(txd->tx.flags & DMA_COMPL_SKIP_DEST_UNMAP)) {
		if (txd->tx.flags & DMA_COMPL_DEST_UNMAP_SINGLE)
			dma_unmap_single(dev, txd->dst_addr, txd->len,
				DMA_FROM_DEVICE);
		else
			dma_unmap_page(dev, txd->dst_addr, txd->len,
				DMA_FROM_DEVICE);
	}
}

static void pl08x_tasklet(unsigned long data)
{
	struct pl08x_dma_chan *plchan = (struct pl08x_dma_chan *) data;
	struct pl08x_driver_data *pl08x = plchan->host;
	struct pl08x_txd *txd;
	unsigned long flags;

	spin_lock_irqsave(&plchan->lock, flags);

	txd = plchan->at;
	plchan->at = NULL;

	if (txd) {
		/* Update last completed */
		plchan->lc = txd->tx.cookie;
	}

	/* If a new descriptor is queued, set it up plchan->at is NULL here */
	if (!list_empty(&plchan->pend_list)) {
		struct pl08x_txd *next;

		next = list_first_entry(&plchan->pend_list,
					struct pl08x_txd,
					node);
		list_del(&next->node);

		pl08x_start_txd(plchan, next);
	} else if (plchan->phychan_hold) {
		/*
		 * This channel is still in use - we have a new txd being
		 * prepared and will soon be queued.  Don't give up the
		 * physical channel.
		 */
	} else {
		struct pl08x_dma_chan *waiting = NULL;

		/*
		 * No more jobs, so free up the physical channel
		 * Free any allocated signal on slave transfers too
		 */
		release_phy_channel(plchan);
		plchan->state = PL08X_CHAN_IDLE;

		/*
		 * And NOW before anyone else can grab that free:d up
		 * physical channel, see if there is some memcpy pending
		 * that seriously needs to start because of being stacked
		 * up while we were choking the physical channels with data.
		 */
		list_for_each_entry(waiting, &pl08x->memcpy.channels,
				    chan.device_node) {
		  if (waiting->state == PL08X_CHAN_WAITING &&
			    waiting->waiting != NULL) {
				int ret;

				/* This should REALLY not fail now */
				ret = prep_phy_channel(waiting,
						       waiting->waiting);
				BUG_ON(ret);
				waiting->phychan_hold--;
				waiting->state = PL08X_CHAN_RUNNING;
				waiting->waiting = NULL;
				pl08x_issue_pending(&waiting->chan);
				break;
			}
		}
	}

	spin_unlock_irqrestore(&plchan->lock, flags);

	if (txd) {
		dma_async_tx_callback callback = txd->tx.callback;
		void *callback_param = txd->tx.callback_param;

		/* Don't try to unmap buffers on slave channels */
		if (!plchan->slave)
			pl08x_unmap_buffers(txd);

		/* Free the descriptor */
		spin_lock_irqsave(&plchan->lock, flags);
		pl08x_free_txd(pl08x, txd);
		spin_unlock_irqrestore(&plchan->lock, flags);

		/* Callback to signal completion */
		if (callback)
			callback(callback_param);
	}
}

static irqreturn_t pl08x_irq(int irq, void *dev)
{
	struct pl08x_driver_data *pl08x = dev;
	u32 mask = 0;
	u32 val;
	int i;

	val = readl(pl08x->base + PL080_ERR_STATUS);
	if (val) {
		/* An error interrupt (on one or more channels) */
		dev_err(&pl08x->adev->dev,
			"%s error interrupt, register value 0x%08x\n",
				__func__, val);
		/*
		 * Simply clear ALL PL08X error interrupts,
		 * regardless of channel and cause
		 * FIXME: should be 0x00000003 on PL081 really.
		 */
		writel(0x000000FF, pl08x->base + PL080_ERR_CLEAR);
	}
	val = readl(pl08x->base + PL080_INT_STATUS);
	for (i = 0; i < pl08x->vd->channels; i++) {
		if ((1 << i) & val) {
			/* Locate physical channel */
			struct pl08x_phy_chan *phychan = &pl08x->phy_chans[i];
			struct pl08x_dma_chan *plchan = phychan->serving;

			/* Schedule tasklet on this channel */
			tasklet_schedule(&plchan->tasklet);

			mask |= (1 << i);
		}
	}
	/* Clear only the terminal interrupts on channels we processed */
	writel(mask, pl08x->base + PL080_TC_CLEAR);

	return mask ? IRQ_HANDLED : IRQ_NONE;
}

static void pl08x_dma_slave_init(struct pl08x_dma_chan *chan)
{
	u32 cctl = pl08x_cctl(chan->cd->cctl);

	chan->slave = true;
	chan->name = chan->cd->bus_id;
	chan->src_addr = chan->cd->addr;
	chan->dst_addr = chan->cd->addr;
	chan->src_cctl = cctl | PL080_CONTROL_DST_INCR |
		pl08x_select_bus(chan->cd->periph_buses, chan->host->mem_buses);
	chan->dst_cctl = cctl | PL080_CONTROL_SRC_INCR |
		pl08x_select_bus(chan->host->mem_buses, chan->cd->periph_buses);
}

/*
 * Initialise the DMAC memcpy/slave channels.
 * Make a local wrapper to hold required data
 */
static int pl08x_dma_init_virtual_channels(struct pl08x_driver_data *pl08x,
					   struct dma_device *dmadev,
					   unsigned int channels,
					   bool slave)
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
		chan = kzalloc(sizeof(struct pl08x_dma_chan), GFP_KERNEL);
		if (!chan) {
			dev_err(&pl08x->adev->dev,
				"%s no memory for channel\n", __func__);
			return -ENOMEM;
		}

		chan->host = pl08x;
		chan->state = PL08X_CHAN_IDLE;

		if (slave) {
			chan->cd = &pl08x->pd->slave_channels[i];
			pl08x_dma_slave_init(chan);
		} else {
			chan->cd = &pl08x->pd->memcpy_channel;
			chan->name = kasprintf(GFP_KERNEL, "memcpy%d", i);
			if (!chan->name) {
				kfree(chan);
				return -ENOMEM;
			}
		}
		if (chan->cd->circular_buffer) {
			dev_err(&pl08x->adev->dev,
				"channel %s: circular buffers not supported\n",
				chan->name);
			kfree(chan);
			continue;
		}
		dev_info(&pl08x->adev->dev,
			 "initialize virtual channel \"%s\"\n",
			 chan->name);

		chan->chan.device = dmadev;
		chan->chan.cookie = 0;
		chan->lc = 0;

		spin_lock_init(&chan->lock);
		INIT_LIST_HEAD(&chan->pend_list);
		tasklet_init(&chan->tasklet, pl08x_tasklet,
			     (unsigned long) chan);

		list_add_tail(&chan->chan.device_node, &dmadev->channels);
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
				 next, &dmadev->channels, chan.device_node) {
		list_del(&chan->chan.device_node);
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

		seq_printf(s, "%d\t\t%s\n",
			   ch->id, virt_chan ? virt_chan->name : "(none)");

		spin_unlock_irqrestore(&ch->lock, flags);
	}

	seq_printf(s, "\nPL08x virtual memcpy channels:\n");
	seq_printf(s, "CHANNEL:\tSTATE:\n");
	seq_printf(s, "--------\t------\n");
	list_for_each_entry(chan, &pl08x->memcpy.channels, chan.device_node) {
		seq_printf(s, "%s\t\t%s\n", chan->name,
			   pl08x_state_str(chan->state));
	}

	seq_printf(s, "\nPL08x virtual slave channels:\n");
	seq_printf(s, "CHANNEL:\tSTATE:\n");
	seq_printf(s, "--------\t------\n");
	list_for_each_entry(chan, &pl08x->slave.channels, chan.device_node) {
		seq_printf(s, "%s\t\t%s\n", chan->name,
			   pl08x_state_str(chan->state));
	}

	return 0;
}

static int pl08x_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, pl08x_debugfs_show, inode->i_private);
}

static const struct file_operations pl08x_debugfs_operations = {
	.open		= pl08x_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void init_pl08x_debugfs(struct pl08x_driver_data *pl08x)
{
	/* Expose a simple debugfs interface to view all clocks */
	(void) debugfs_create_file(dev_name(&pl08x->adev->dev), S_IFREG | S_IRUGO,
				   NULL, pl08x,
				   &pl08x_debugfs_operations);
}

#else
static inline void init_pl08x_debugfs(struct pl08x_driver_data *pl08x)
{
}
#endif

static int pl08x_probe(struct amba_device *adev, const struct amba_id *id)
{
	struct pl08x_driver_data *pl08x;
	const struct vendor_data *vd = id->data;
	int ret = 0;
	int i;

	ret = amba_request_regions(adev, NULL);
	if (ret)
		return ret;

	/* Create the driver state holder */
	pl08x = kzalloc(sizeof(struct pl08x_driver_data), GFP_KERNEL);
	if (!pl08x) {
		ret = -ENOMEM;
		goto out_no_pl08x;
	}

	/* Initialize memcpy engine */
	dma_cap_set(DMA_MEMCPY, pl08x->memcpy.cap_mask);
	pl08x->memcpy.dev = &adev->dev;
	pl08x->memcpy.device_alloc_chan_resources = pl08x_alloc_chan_resources;
	pl08x->memcpy.device_free_chan_resources = pl08x_free_chan_resources;
	pl08x->memcpy.device_prep_dma_memcpy = pl08x_prep_dma_memcpy;
	pl08x->memcpy.device_prep_dma_interrupt = pl08x_prep_dma_interrupt;
	pl08x->memcpy.device_tx_status = pl08x_dma_tx_status;
	pl08x->memcpy.device_issue_pending = pl08x_issue_pending;
	pl08x->memcpy.device_control = pl08x_control;

	/* Initialize slave engine */
	dma_cap_set(DMA_SLAVE, pl08x->slave.cap_mask);
	pl08x->slave.dev = &adev->dev;
	pl08x->slave.device_alloc_chan_resources = pl08x_alloc_chan_resources;
	pl08x->slave.device_free_chan_resources = pl08x_free_chan_resources;
	pl08x->slave.device_prep_dma_interrupt = pl08x_prep_dma_interrupt;
	pl08x->slave.device_tx_status = pl08x_dma_tx_status;
	pl08x->slave.device_issue_pending = pl08x_issue_pending;
	pl08x->slave.device_prep_slave_sg = pl08x_prep_slave_sg;
	pl08x->slave.device_control = pl08x_control;

	/* Get the platform data */
	pl08x->pd = dev_get_platdata(&adev->dev);
	if (!pl08x->pd) {
		dev_err(&adev->dev, "no platform data supplied\n");
		goto out_no_platdata;
	}

	/* Assign useful pointers to the driver state */
	pl08x->adev = adev;
	pl08x->vd = vd;

	/* By default, AHB1 only.  If dualmaster, from platform */
	pl08x->lli_buses = PL08X_AHB1;
	pl08x->mem_buses = PL08X_AHB1;
	if (pl08x->vd->dualmaster) {
		pl08x->lli_buses = pl08x->pd->lli_buses;
		pl08x->mem_buses = pl08x->pd->mem_buses;
	}

	/* A DMA memory pool for LLIs, align on 1-byte boundary */
	pl08x->pool = dma_pool_create(DRIVER_NAME, &pl08x->adev->dev,
			PL08X_LLI_TSFR_SIZE, PL08X_ALIGN, 0);
	if (!pl08x->pool) {
		ret = -ENOMEM;
		goto out_no_lli_pool;
	}

	spin_lock_init(&pl08x->lock);

	pl08x->base = ioremap(adev->res.start, resource_size(&adev->res));
	if (!pl08x->base) {
		ret = -ENOMEM;
		goto out_no_ioremap;
	}

	/* Turn on the PL08x */
	pl08x_ensure_on(pl08x);

	/* Attach the interrupt handler */
	writel(0x000000FF, pl08x->base + PL080_ERR_CLEAR);
	writel(0x000000FF, pl08x->base + PL080_TC_CLEAR);

	ret = request_irq(adev->irq[0], pl08x_irq, IRQF_DISABLED,
			  DRIVER_NAME, pl08x);
	if (ret) {
		dev_err(&adev->dev, "%s failed to request interrupt %d\n",
			__func__, adev->irq[0]);
		goto out_no_irq;
	}

	/* Initialize physical channels */
	pl08x->phy_chans = kmalloc((vd->channels * sizeof(struct pl08x_phy_chan)),
			GFP_KERNEL);
	if (!pl08x->phy_chans) {
		dev_err(&adev->dev, "%s failed to allocate "
			"physical channel holders\n",
			__func__);
		goto out_no_phychans;
	}

	for (i = 0; i < vd->channels; i++) {
		struct pl08x_phy_chan *ch = &pl08x->phy_chans[i];

		ch->id = i;
		ch->base = pl08x->base + PL080_Cx_BASE(i);
		spin_lock_init(&ch->lock);
		ch->serving = NULL;
		ch->signal = -1;
		dev_info(&adev->dev,
			 "physical channel %d is %s\n", i,
			 pl08x_phy_channel_busy(ch) ? "BUSY" : "FREE");
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
	pl08x->memcpy.chancnt = ret;

	/* Register slave channels */
	ret = pl08x_dma_init_virtual_channels(pl08x, &pl08x->slave,
					      pl08x->pd->num_slave_channels,
					      true);
	if (ret <= 0) {
		dev_warn(&pl08x->adev->dev,
			"%s failed to enumerate slave channels - %d\n",
				__func__, ret);
		goto out_no_slave;
	}
	pl08x->slave.chancnt = ret;

	ret = dma_async_device_register(&pl08x->memcpy);
	if (ret) {
		dev_warn(&pl08x->adev->dev,
			"%s failed to register memcpy as an async device - %d\n",
			__func__, ret);
		goto out_no_memcpy_reg;
	}

	ret = dma_async_device_register(&pl08x->slave);
	if (ret) {
		dev_warn(&pl08x->adev->dev,
			"%s failed to register slave as an async device - %d\n",
			__func__, ret);
		goto out_no_slave_reg;
	}

	amba_set_drvdata(adev, pl08x);
	init_pl08x_debugfs(pl08x);
	dev_info(&pl08x->adev->dev, "DMA: PL%03x rev%u at 0x%08llx irq %d\n",
		 amba_part(adev), amba_rev(adev),
		 (unsigned long long)adev->res.start, adev->irq[0]);
	return 0;

out_no_slave_reg:
	dma_async_device_unregister(&pl08x->memcpy);
out_no_memcpy_reg:
	pl08x_free_virtual_channels(&pl08x->slave);
out_no_slave:
	pl08x_free_virtual_channels(&pl08x->memcpy);
out_no_memcpy:
	kfree(pl08x->phy_chans);
out_no_phychans:
	free_irq(adev->irq[0], pl08x);
out_no_irq:
	iounmap(pl08x->base);
out_no_ioremap:
	dma_pool_destroy(pl08x->pool);
out_no_lli_pool:
out_no_platdata:
	kfree(pl08x);
out_no_pl08x:
	amba_release_regions(adev);
	return ret;
}

/* PL080 has 8 channels and the PL080 have just 2 */
static struct vendor_data vendor_pl080 = {
	.channels = 8,
	.dualmaster = true,
};

static struct vendor_data vendor_pl081 = {
	.channels = 2,
	.dualmaster = false,
};

static struct amba_id pl08x_ids[] = {
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
		.id	= 0x00280880,
		.mask	= 0x00ffffff,
		.data	= &vendor_pl080,
	},
	{ 0, 0 },
};

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
