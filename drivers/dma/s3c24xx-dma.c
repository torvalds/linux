// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * S3C24XX DMA handling
 *
 * Copyright (c) 2013 Heiko Stuebner <heiko@sntech.de>
 *
 * based on amba-pl08x.c
 *
 * Copyright (c) 2006 ARM Ltd.
 * Copyright (c) 2010 ST-Ericsson SA
 *
 * Author: Peter Pearse <peter.pearse@arm.com>
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 *
 * The DMA controllers in S3C24XX SoCs have a varying number of DMA signals
 * that can be routed to any of the 4 to 8 hardware-channels.
 *
 * Therefore on these DMA controllers the number of channels
 * and the number of incoming DMA signals are two totally different things.
 * It is usually not possible to theoretically handle all physical signals,
 * so a multiplexing scheme with possible denial of use is necessary.
 *
 * Open items:
 * - bursts
 */

#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/platform_data/dma-s3c24xx.h>

#include "dmaengine.h"
#include "virt-dma.h"

#define MAX_DMA_CHANNELS	8

#define S3C24XX_DISRC			0x00
#define S3C24XX_DISRCC			0x04
#define S3C24XX_DISRCC_INC_INCREMENT	0
#define S3C24XX_DISRCC_INC_FIXED	BIT(0)
#define S3C24XX_DISRCC_LOC_AHB		0
#define S3C24XX_DISRCC_LOC_APB		BIT(1)

#define S3C24XX_DIDST			0x08
#define S3C24XX_DIDSTC			0x0c
#define S3C24XX_DIDSTC_INC_INCREMENT	0
#define S3C24XX_DIDSTC_INC_FIXED	BIT(0)
#define S3C24XX_DIDSTC_LOC_AHB		0
#define S3C24XX_DIDSTC_LOC_APB		BIT(1)
#define S3C24XX_DIDSTC_INT_TC0		0
#define S3C24XX_DIDSTC_INT_RELOAD	BIT(2)

#define S3C24XX_DCON			0x10

#define S3C24XX_DCON_TC_MASK		0xfffff
#define S3C24XX_DCON_DSZ_BYTE		(0 << 20)
#define S3C24XX_DCON_DSZ_HALFWORD	(1 << 20)
#define S3C24XX_DCON_DSZ_WORD		(2 << 20)
#define S3C24XX_DCON_DSZ_MASK		(3 << 20)
#define S3C24XX_DCON_DSZ_SHIFT		20
#define S3C24XX_DCON_AUTORELOAD		0
#define S3C24XX_DCON_NORELOAD		BIT(22)
#define S3C24XX_DCON_HWTRIG		BIT(23)
#define S3C24XX_DCON_HWSRC_SHIFT	24
#define S3C24XX_DCON_SERV_SINGLE	0
#define S3C24XX_DCON_SERV_WHOLE		BIT(27)
#define S3C24XX_DCON_TSZ_UNIT		0
#define S3C24XX_DCON_TSZ_BURST4		BIT(28)
#define S3C24XX_DCON_INT		BIT(29)
#define S3C24XX_DCON_SYNC_PCLK		0
#define S3C24XX_DCON_SYNC_HCLK		BIT(30)
#define S3C24XX_DCON_DEMAND		0
#define S3C24XX_DCON_HANDSHAKE		BIT(31)

#define S3C24XX_DSTAT			0x14
#define S3C24XX_DSTAT_STAT_BUSY		BIT(20)
#define S3C24XX_DSTAT_CURRTC_MASK	0xfffff

#define S3C24XX_DMASKTRIG		0x20
#define S3C24XX_DMASKTRIG_SWTRIG	BIT(0)
#define S3C24XX_DMASKTRIG_ON		BIT(1)
#define S3C24XX_DMASKTRIG_STOP		BIT(2)

#define S3C24XX_DMAREQSEL		0x24
#define S3C24XX_DMAREQSEL_HW		BIT(0)

/*
 * S3C2410, S3C2440 and S3C2442 SoCs cannot select any physical channel
 * for a DMA source. Instead only specific channels are valid.
 * All of these SoCs have 4 physical channels and the number of request
 * source bits is 3. Additionally we also need 1 bit to mark the channel
 * as valid.
 * Therefore we separate the chansel element of the channel data into 4
 * parts of 4 bits each, to hold the information if the channel is valid
 * and the hw request source to use.
 *
 * Example:
 * SDI is valid on channels 0, 2 and 3 - with varying hw request sources.
 * For it the chansel field would look like
 *
 * ((BIT(3) | 1) << 3 * 4) | // channel 3, with request source 1
 * ((BIT(3) | 2) << 2 * 4) | // channel 2, with request source 2
 * ((BIT(3) | 2) << 0 * 4)   // channel 0, with request source 2
 */
#define S3C24XX_CHANSEL_WIDTH		4
#define S3C24XX_CHANSEL_VALID		BIT(3)
#define S3C24XX_CHANSEL_REQ_MASK	7

/*
 * struct soc_data - vendor-specific config parameters for individual SoCs
 * @stride: spacing between the registers of each channel
 * @has_reqsel: does the controller use the newer requestselection mechanism
 * @has_clocks: are controllable dma-clocks present
 */
struct soc_data {
	int stride;
	bool has_reqsel;
	bool has_clocks;
};

/*
 * enum s3c24xx_dma_chan_state - holds the virtual channel states
 * @S3C24XX_DMA_CHAN_IDLE: the channel is idle
 * @S3C24XX_DMA_CHAN_RUNNING: the channel has allocated a physical transport
 * channel and is running a transfer on it
 * @S3C24XX_DMA_CHAN_WAITING: the channel is waiting for a physical transport
 * channel to become available (only pertains to memcpy channels)
 */
enum s3c24xx_dma_chan_state {
	S3C24XX_DMA_CHAN_IDLE,
	S3C24XX_DMA_CHAN_RUNNING,
	S3C24XX_DMA_CHAN_WAITING,
};

/*
 * struct s3c24xx_sg - structure containing data per sg
 * @src_addr: src address of sg
 * @dst_addr: dst address of sg
 * @len: transfer len in bytes
 * @node: node for txd's dsg_list
 */
struct s3c24xx_sg {
	dma_addr_t src_addr;
	dma_addr_t dst_addr;
	size_t len;
	struct list_head node;
};

/*
 * struct s3c24xx_txd - wrapper for struct dma_async_tx_descriptor
 * @vd: virtual DMA descriptor
 * @dsg_list: list of children sg's
 * @at: sg currently being transfered
 * @width: transfer width
 * @disrcc: value for source control register
 * @didstc: value for destination control register
 * @dcon: base value for dcon register
 * @cyclic: indicate cyclic transfer
 */
struct s3c24xx_txd {
	struct virt_dma_desc vd;
	struct list_head dsg_list;
	struct list_head *at;
	u8 width;
	u32 disrcc;
	u32 didstc;
	u32 dcon;
	bool cyclic;
};

struct s3c24xx_dma_chan;

/*
 * struct s3c24xx_dma_phy - holder for the physical channels
 * @id: physical index to this channel
 * @valid: does the channel have all required elements
 * @base: virtual memory base (remapped) for the this channel
 * @irq: interrupt for this channel
 * @clk: clock for this channel
 * @lock: a lock to use when altering an instance of this struct
 * @serving: virtual channel currently being served by this physicalchannel
 * @host: a pointer to the host (internal use)
 */
struct s3c24xx_dma_phy {
	unsigned int			id;
	bool				valid;
	void __iomem			*base;
	int				irq;
	struct clk			*clk;
	spinlock_t			lock;
	struct s3c24xx_dma_chan		*serving;
	struct s3c24xx_dma_engine	*host;
};

/*
 * struct s3c24xx_dma_chan - this structure wraps a DMA ENGINE channel
 * @id: the id of the channel
 * @name: name of the channel
 * @vc: wrappped virtual channel
 * @phy: the physical channel utilized by this channel, if there is one
 * @runtime_addr: address for RX/TX according to the runtime config
 * @at: active transaction on this channel
 * @lock: a lock for this channel data
 * @host: a pointer to the host (internal use)
 * @state: whether the channel is idle, running etc
 * @slave: whether this channel is a device (slave) or for memcpy
 */
struct s3c24xx_dma_chan {
	int id;
	const char *name;
	struct virt_dma_chan vc;
	struct s3c24xx_dma_phy *phy;
	struct dma_slave_config cfg;
	struct s3c24xx_txd *at;
	struct s3c24xx_dma_engine *host;
	enum s3c24xx_dma_chan_state state;
	bool slave;
};

/*
 * struct s3c24xx_dma_engine - the local state holder for the S3C24XX
 * @pdev: the corresponding platform device
 * @pdata: platform data passed in from the platform/machine
 * @base: virtual memory base (remapped)
 * @slave: slave engine for this instance
 * @memcpy: memcpy engine for this instance
 * @phy_chans: array of data for the physical channels
 */
struct s3c24xx_dma_engine {
	struct platform_device			*pdev;
	const struct s3c24xx_dma_platdata	*pdata;
	struct soc_data				*sdata;
	void __iomem				*base;
	struct dma_device			slave;
	struct dma_device			memcpy;
	struct s3c24xx_dma_phy			*phy_chans;
};

/*
 * Physical channel handling
 */

/*
 * Check whether a certain channel is busy or not.
 */
static int s3c24xx_dma_phy_busy(struct s3c24xx_dma_phy *phy)
{
	unsigned int val = readl(phy->base + S3C24XX_DSTAT);
	return val & S3C24XX_DSTAT_STAT_BUSY;
}

static bool s3c24xx_dma_phy_valid(struct s3c24xx_dma_chan *s3cchan,
				  struct s3c24xx_dma_phy *phy)
{
	struct s3c24xx_dma_engine *s3cdma = s3cchan->host;
	const struct s3c24xx_dma_platdata *pdata = s3cdma->pdata;
	struct s3c24xx_dma_channel *cdata = &pdata->channels[s3cchan->id];
	int phyvalid;

	/* every phy is valid for memcopy channels */
	if (!s3cchan->slave)
		return true;

	/* On newer variants all phys can be used for all virtual channels */
	if (s3cdma->sdata->has_reqsel)
		return true;

	phyvalid = (cdata->chansel >> (phy->id * S3C24XX_CHANSEL_WIDTH));
	return (phyvalid & S3C24XX_CHANSEL_VALID) ? true : false;
}

/*
 * Allocate a physical channel for a virtual channel
 *
 * Try to locate a physical channel to be used for this transfer. If all
 * are taken return NULL and the requester will have to cope by using
 * some fallback PIO mode or retrying later.
 */
static
struct s3c24xx_dma_phy *s3c24xx_dma_get_phy(struct s3c24xx_dma_chan *s3cchan)
{
	struct s3c24xx_dma_engine *s3cdma = s3cchan->host;
	struct s3c24xx_dma_phy *phy = NULL;
	unsigned long flags;
	int i;
	int ret;

	for (i = 0; i < s3cdma->pdata->num_phy_channels; i++) {
		phy = &s3cdma->phy_chans[i];

		if (!phy->valid)
			continue;

		if (!s3c24xx_dma_phy_valid(s3cchan, phy))
			continue;

		spin_lock_irqsave(&phy->lock, flags);

		if (!phy->serving) {
			phy->serving = s3cchan;
			spin_unlock_irqrestore(&phy->lock, flags);
			break;
		}

		spin_unlock_irqrestore(&phy->lock, flags);
	}

	/* No physical channel available, cope with it */
	if (i == s3cdma->pdata->num_phy_channels) {
		dev_warn(&s3cdma->pdev->dev, "no phy channel available\n");
		return NULL;
	}

	/* start the phy clock */
	if (s3cdma->sdata->has_clocks) {
		ret = clk_enable(phy->clk);
		if (ret) {
			dev_err(&s3cdma->pdev->dev, "could not enable clock for channel %d, err %d\n",
				phy->id, ret);
			phy->serving = NULL;
			return NULL;
		}
	}

	return phy;
}

/*
 * Mark the physical channel as free.
 *
 * This drops the link between the physical and virtual channel.
 */
static inline void s3c24xx_dma_put_phy(struct s3c24xx_dma_phy *phy)
{
	struct s3c24xx_dma_engine *s3cdma = phy->host;

	if (s3cdma->sdata->has_clocks)
		clk_disable(phy->clk);

	phy->serving = NULL;
}

/*
 * Stops the channel by writing the stop bit.
 * This should not be used for an on-going transfer, but as a method of
 * shutting down a channel (eg, when it's no longer used) or terminating a
 * transfer.
 */
static void s3c24xx_dma_terminate_phy(struct s3c24xx_dma_phy *phy)
{
	writel(S3C24XX_DMASKTRIG_STOP, phy->base + S3C24XX_DMASKTRIG);
}

/*
 * Virtual channel handling
 */

static inline
struct s3c24xx_dma_chan *to_s3c24xx_dma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct s3c24xx_dma_chan, vc.chan);
}

static u32 s3c24xx_dma_getbytes_chan(struct s3c24xx_dma_chan *s3cchan)
{
	struct s3c24xx_dma_phy *phy = s3cchan->phy;
	struct s3c24xx_txd *txd = s3cchan->at;
	u32 tc = readl(phy->base + S3C24XX_DSTAT) & S3C24XX_DSTAT_CURRTC_MASK;

	return tc * txd->width;
}

static int s3c24xx_dma_set_runtime_config(struct dma_chan *chan,
				  struct dma_slave_config *config)
{
	struct s3c24xx_dma_chan *s3cchan = to_s3c24xx_dma_chan(chan);
	unsigned long flags;
	int ret = 0;

	/* Reject definitely invalid configurations */
	if (config->src_addr_width == DMA_SLAVE_BUSWIDTH_8_BYTES ||
	    config->dst_addr_width == DMA_SLAVE_BUSWIDTH_8_BYTES)
		return -EINVAL;

	spin_lock_irqsave(&s3cchan->vc.lock, flags);

	if (!s3cchan->slave) {
		ret = -EINVAL;
		goto out;
	}

	s3cchan->cfg = *config;

out:
	spin_unlock_irqrestore(&s3cchan->vc.lock, flags);
	return ret;
}

/*
 * Transfer handling
 */

static inline
struct s3c24xx_txd *to_s3c24xx_txd(struct dma_async_tx_descriptor *tx)
{
	return container_of(tx, struct s3c24xx_txd, vd.tx);
}

static struct s3c24xx_txd *s3c24xx_dma_get_txd(void)
{
	struct s3c24xx_txd *txd = kzalloc(sizeof(*txd), GFP_NOWAIT);

	if (txd) {
		INIT_LIST_HEAD(&txd->dsg_list);
		txd->dcon = S3C24XX_DCON_INT | S3C24XX_DCON_NORELOAD;
	}

	return txd;
}

static void s3c24xx_dma_free_txd(struct s3c24xx_txd *txd)
{
	struct s3c24xx_sg *dsg, *_dsg;

	list_for_each_entry_safe(dsg, _dsg, &txd->dsg_list, node) {
		list_del(&dsg->node);
		kfree(dsg);
	}

	kfree(txd);
}

static void s3c24xx_dma_start_next_sg(struct s3c24xx_dma_chan *s3cchan,
				       struct s3c24xx_txd *txd)
{
	struct s3c24xx_dma_engine *s3cdma = s3cchan->host;
	struct s3c24xx_dma_phy *phy = s3cchan->phy;
	const struct s3c24xx_dma_platdata *pdata = s3cdma->pdata;
	struct s3c24xx_sg *dsg = list_entry(txd->at, struct s3c24xx_sg, node);
	u32 dcon = txd->dcon;
	u32 val;

	/* transfer-size and -count from len and width */
	switch (txd->width) {
	case 1:
		dcon |= S3C24XX_DCON_DSZ_BYTE | dsg->len;
		break;
	case 2:
		dcon |= S3C24XX_DCON_DSZ_HALFWORD | (dsg->len / 2);
		break;
	case 4:
		dcon |= S3C24XX_DCON_DSZ_WORD | (dsg->len / 4);
		break;
	}

	if (s3cchan->slave) {
		struct s3c24xx_dma_channel *cdata =
					&pdata->channels[s3cchan->id];

		if (s3cdma->sdata->has_reqsel) {
			writel_relaxed((cdata->chansel << 1) |
							S3C24XX_DMAREQSEL_HW,
					phy->base + S3C24XX_DMAREQSEL);
		} else {
			int csel = cdata->chansel >> (phy->id *
							S3C24XX_CHANSEL_WIDTH);

			csel &= S3C24XX_CHANSEL_REQ_MASK;
			dcon |= csel << S3C24XX_DCON_HWSRC_SHIFT;
			dcon |= S3C24XX_DCON_HWTRIG;
		}
	} else {
		if (s3cdma->sdata->has_reqsel)
			writel_relaxed(0, phy->base + S3C24XX_DMAREQSEL);
	}

	writel_relaxed(dsg->src_addr, phy->base + S3C24XX_DISRC);
	writel_relaxed(txd->disrcc, phy->base + S3C24XX_DISRCC);
	writel_relaxed(dsg->dst_addr, phy->base + S3C24XX_DIDST);
	writel_relaxed(txd->didstc, phy->base + S3C24XX_DIDSTC);
	writel_relaxed(dcon, phy->base + S3C24XX_DCON);

	val = readl_relaxed(phy->base + S3C24XX_DMASKTRIG);
	val &= ~S3C24XX_DMASKTRIG_STOP;
	val |= S3C24XX_DMASKTRIG_ON;

	/* trigger the dma operation for memcpy transfers */
	if (!s3cchan->slave)
		val |= S3C24XX_DMASKTRIG_SWTRIG;

	writel(val, phy->base + S3C24XX_DMASKTRIG);
}

/*
 * Set the initial DMA register values and start first sg.
 */
static void s3c24xx_dma_start_next_txd(struct s3c24xx_dma_chan *s3cchan)
{
	struct s3c24xx_dma_phy *phy = s3cchan->phy;
	struct virt_dma_desc *vd = vchan_next_desc(&s3cchan->vc);
	struct s3c24xx_txd *txd = to_s3c24xx_txd(&vd->tx);

	list_del(&txd->vd.node);

	s3cchan->at = txd;

	/* Wait for channel inactive */
	while (s3c24xx_dma_phy_busy(phy))
		cpu_relax();

	/* point to the first element of the sg list */
	txd->at = txd->dsg_list.next;
	s3c24xx_dma_start_next_sg(s3cchan, txd);
}

static void s3c24xx_dma_free_txd_list(struct s3c24xx_dma_engine *s3cdma,
				struct s3c24xx_dma_chan *s3cchan)
{
	LIST_HEAD(head);

	vchan_get_all_descriptors(&s3cchan->vc, &head);
	vchan_dma_desc_free_list(&s3cchan->vc, &head);
}

/*
 * Try to allocate a physical channel.  When successful, assign it to
 * this virtual channel, and initiate the next descriptor.  The
 * virtual channel lock must be held at this point.
 */
static void s3c24xx_dma_phy_alloc_and_start(struct s3c24xx_dma_chan *s3cchan)
{
	struct s3c24xx_dma_engine *s3cdma = s3cchan->host;
	struct s3c24xx_dma_phy *phy;

	phy = s3c24xx_dma_get_phy(s3cchan);
	if (!phy) {
		dev_dbg(&s3cdma->pdev->dev, "no physical channel available for xfer on %s\n",
			s3cchan->name);
		s3cchan->state = S3C24XX_DMA_CHAN_WAITING;
		return;
	}

	dev_dbg(&s3cdma->pdev->dev, "allocated physical channel %d for xfer on %s\n",
		phy->id, s3cchan->name);

	s3cchan->phy = phy;
	s3cchan->state = S3C24XX_DMA_CHAN_RUNNING;

	s3c24xx_dma_start_next_txd(s3cchan);
}

static void s3c24xx_dma_phy_reassign_start(struct s3c24xx_dma_phy *phy,
	struct s3c24xx_dma_chan *s3cchan)
{
	struct s3c24xx_dma_engine *s3cdma = s3cchan->host;

	dev_dbg(&s3cdma->pdev->dev, "reassigned physical channel %d for xfer on %s\n",
		phy->id, s3cchan->name);

	/*
	 * We do this without taking the lock; we're really only concerned
	 * about whether this pointer is NULL or not, and we're guaranteed
	 * that this will only be called when it _already_ is non-NULL.
	 */
	phy->serving = s3cchan;
	s3cchan->phy = phy;
	s3cchan->state = S3C24XX_DMA_CHAN_RUNNING;
	s3c24xx_dma_start_next_txd(s3cchan);
}

/*
 * Free a physical DMA channel, potentially reallocating it to another
 * virtual channel if we have any pending.
 */
static void s3c24xx_dma_phy_free(struct s3c24xx_dma_chan *s3cchan)
{
	struct s3c24xx_dma_engine *s3cdma = s3cchan->host;
	struct s3c24xx_dma_chan *p, *next;

retry:
	next = NULL;

	/* Find a waiting virtual channel for the next transfer. */
	list_for_each_entry(p, &s3cdma->memcpy.channels, vc.chan.device_node)
		if (p->state == S3C24XX_DMA_CHAN_WAITING) {
			next = p;
			break;
		}

	if (!next) {
		list_for_each_entry(p, &s3cdma->slave.channels,
				    vc.chan.device_node)
			if (p->state == S3C24XX_DMA_CHAN_WAITING &&
				      s3c24xx_dma_phy_valid(p, s3cchan->phy)) {
				next = p;
				break;
			}
	}

	/* Ensure that the physical channel is stopped */
	s3c24xx_dma_terminate_phy(s3cchan->phy);

	if (next) {
		bool success;

		/*
		 * Eww.  We know this isn't going to deadlock
		 * but lockdep probably doesn't.
		 */
		spin_lock(&next->vc.lock);
		/* Re-check the state now that we have the lock */
		success = next->state == S3C24XX_DMA_CHAN_WAITING;
		if (success)
			s3c24xx_dma_phy_reassign_start(s3cchan->phy, next);
		spin_unlock(&next->vc.lock);

		/* If the state changed, try to find another channel */
		if (!success)
			goto retry;
	} else {
		/* No more jobs, so free up the physical channel */
		s3c24xx_dma_put_phy(s3cchan->phy);
	}

	s3cchan->phy = NULL;
	s3cchan->state = S3C24XX_DMA_CHAN_IDLE;
}

static void s3c24xx_dma_desc_free(struct virt_dma_desc *vd)
{
	struct s3c24xx_txd *txd = to_s3c24xx_txd(&vd->tx);
	struct s3c24xx_dma_chan *s3cchan = to_s3c24xx_dma_chan(vd->tx.chan);

	if (!s3cchan->slave)
		dma_descriptor_unmap(&vd->tx);

	s3c24xx_dma_free_txd(txd);
}

static irqreturn_t s3c24xx_dma_irq(int irq, void *data)
{
	struct s3c24xx_dma_phy *phy = data;
	struct s3c24xx_dma_chan *s3cchan = phy->serving;
	struct s3c24xx_txd *txd;

	dev_dbg(&phy->host->pdev->dev, "interrupt on channel %d\n", phy->id);

	/*
	 * Interrupts happen to notify the completion of a transfer and the
	 * channel should have moved into its stop state already on its own.
	 * Therefore interrupts on channels not bound to a virtual channel
	 * should never happen. Nevertheless send a terminate command to the
	 * channel if the unlikely case happens.
	 */
	if (unlikely(!s3cchan)) {
		dev_err(&phy->host->pdev->dev, "interrupt on unused channel %d\n",
			phy->id);

		s3c24xx_dma_terminate_phy(phy);

		return IRQ_HANDLED;
	}

	spin_lock(&s3cchan->vc.lock);
	txd = s3cchan->at;
	if (txd) {
		/* when more sg's are in this txd, start the next one */
		if (!list_is_last(txd->at, &txd->dsg_list)) {
			txd->at = txd->at->next;
			if (txd->cyclic)
				vchan_cyclic_callback(&txd->vd);
			s3c24xx_dma_start_next_sg(s3cchan, txd);
		} else if (!txd->cyclic) {
			s3cchan->at = NULL;
			vchan_cookie_complete(&txd->vd);

			/*
			 * And start the next descriptor (if any),
			 * otherwise free this channel.
			 */
			if (vchan_next_desc(&s3cchan->vc))
				s3c24xx_dma_start_next_txd(s3cchan);
			else
				s3c24xx_dma_phy_free(s3cchan);
		} else {
			vchan_cyclic_callback(&txd->vd);

			/* Cyclic: reset at beginning */
			txd->at = txd->dsg_list.next;
			s3c24xx_dma_start_next_sg(s3cchan, txd);
		}
	}
	spin_unlock(&s3cchan->vc.lock);

	return IRQ_HANDLED;
}

/*
 * The DMA ENGINE API
 */

static int s3c24xx_dma_terminate_all(struct dma_chan *chan)
{
	struct s3c24xx_dma_chan *s3cchan = to_s3c24xx_dma_chan(chan);
	struct s3c24xx_dma_engine *s3cdma = s3cchan->host;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&s3cchan->vc.lock, flags);

	if (!s3cchan->phy && !s3cchan->at) {
		dev_err(&s3cdma->pdev->dev, "trying to terminate already stopped channel %d\n",
			s3cchan->id);
		ret = -EINVAL;
		goto unlock;
	}

	s3cchan->state = S3C24XX_DMA_CHAN_IDLE;

	/* Mark physical channel as free */
	if (s3cchan->phy)
		s3c24xx_dma_phy_free(s3cchan);

	/* Dequeue current job */
	if (s3cchan->at) {
		vchan_terminate_vdesc(&s3cchan->at->vd);
		s3cchan->at = NULL;
	}

	/* Dequeue jobs not yet fired as well */
	s3c24xx_dma_free_txd_list(s3cdma, s3cchan);
unlock:
	spin_unlock_irqrestore(&s3cchan->vc.lock, flags);

	return ret;
}

static void s3c24xx_dma_synchronize(struct dma_chan *chan)
{
	struct s3c24xx_dma_chan *s3cchan = to_s3c24xx_dma_chan(chan);

	vchan_synchronize(&s3cchan->vc);
}

static void s3c24xx_dma_free_chan_resources(struct dma_chan *chan)
{
	/* Ensure all queued descriptors are freed */
	vchan_free_chan_resources(to_virt_chan(chan));
}

static enum dma_status s3c24xx_dma_tx_status(struct dma_chan *chan,
		dma_cookie_t cookie, struct dma_tx_state *txstate)
{
	struct s3c24xx_dma_chan *s3cchan = to_s3c24xx_dma_chan(chan);
	struct s3c24xx_txd *txd;
	struct s3c24xx_sg *dsg;
	struct virt_dma_desc *vd;
	unsigned long flags;
	enum dma_status ret;
	size_t bytes = 0;

	spin_lock_irqsave(&s3cchan->vc.lock, flags);
	ret = dma_cookie_status(chan, cookie, txstate);

	/*
	 * There's no point calculating the residue if there's
	 * no txstate to store the value.
	 */
	if (ret == DMA_COMPLETE || !txstate) {
		spin_unlock_irqrestore(&s3cchan->vc.lock, flags);
		return ret;
	}

	vd = vchan_find_desc(&s3cchan->vc, cookie);
	if (vd) {
		/* On the issued list, so hasn't been processed yet */
		txd = to_s3c24xx_txd(&vd->tx);

		list_for_each_entry(dsg, &txd->dsg_list, node)
			bytes += dsg->len;
	} else {
		/*
		 * Currently running, so sum over the pending sg's and
		 * the currently active one.
		 */
		txd = s3cchan->at;

		dsg = list_entry(txd->at, struct s3c24xx_sg, node);
		list_for_each_entry_from(dsg, &txd->dsg_list, node)
			bytes += dsg->len;

		bytes += s3c24xx_dma_getbytes_chan(s3cchan);
	}
	spin_unlock_irqrestore(&s3cchan->vc.lock, flags);

	/*
	 * This cookie not complete yet
	 * Get number of bytes left in the active transactions and queue
	 */
	dma_set_residue(txstate, bytes);

	/* Whether waiting or running, we're in progress */
	return ret;
}

/*
 * Initialize a descriptor to be used by memcpy submit
 */
static struct dma_async_tx_descriptor *s3c24xx_dma_prep_memcpy(
		struct dma_chan *chan, dma_addr_t dest, dma_addr_t src,
		size_t len, unsigned long flags)
{
	struct s3c24xx_dma_chan *s3cchan = to_s3c24xx_dma_chan(chan);
	struct s3c24xx_dma_engine *s3cdma = s3cchan->host;
	struct s3c24xx_txd *txd;
	struct s3c24xx_sg *dsg;
	int src_mod, dest_mod;

	dev_dbg(&s3cdma->pdev->dev, "prepare memcpy of %zu bytes from %s\n",
			len, s3cchan->name);

	if ((len & S3C24XX_DCON_TC_MASK) != len) {
		dev_err(&s3cdma->pdev->dev, "memcpy size %zu to large\n", len);
		return NULL;
	}

	txd = s3c24xx_dma_get_txd();
	if (!txd)
		return NULL;

	dsg = kzalloc(sizeof(*dsg), GFP_NOWAIT);
	if (!dsg) {
		s3c24xx_dma_free_txd(txd);
		return NULL;
	}
	list_add_tail(&dsg->node, &txd->dsg_list);

	dsg->src_addr = src;
	dsg->dst_addr = dest;
	dsg->len = len;

	/*
	 * Determine a suitable transfer width.
	 * The DMA controller cannot fetch/store information which is not
	 * naturally aligned on the bus, i.e., a 4 byte fetch must start at
	 * an address divisible by 4 - more generally addr % width must be 0.
	 */
	src_mod = src % 4;
	dest_mod = dest % 4;
	switch (len % 4) {
	case 0:
		txd->width = (src_mod == 0 && dest_mod == 0) ? 4 : 1;
		break;
	case 2:
		txd->width = ((src_mod == 2 || src_mod == 0) &&
			      (dest_mod == 2 || dest_mod == 0)) ? 2 : 1;
		break;
	default:
		txd->width = 1;
		break;
	}

	txd->disrcc = S3C24XX_DISRCC_LOC_AHB | S3C24XX_DISRCC_INC_INCREMENT;
	txd->didstc = S3C24XX_DIDSTC_LOC_AHB | S3C24XX_DIDSTC_INC_INCREMENT;
	txd->dcon |= S3C24XX_DCON_DEMAND | S3C24XX_DCON_SYNC_HCLK |
		     S3C24XX_DCON_SERV_WHOLE;

	return vchan_tx_prep(&s3cchan->vc, &txd->vd, flags);
}

static struct dma_async_tx_descriptor *s3c24xx_dma_prep_dma_cyclic(
	struct dma_chan *chan, dma_addr_t addr, size_t size, size_t period,
	enum dma_transfer_direction direction, unsigned long flags)
{
	struct s3c24xx_dma_chan *s3cchan = to_s3c24xx_dma_chan(chan);
	struct s3c24xx_dma_engine *s3cdma = s3cchan->host;
	const struct s3c24xx_dma_platdata *pdata = s3cdma->pdata;
	struct s3c24xx_dma_channel *cdata = &pdata->channels[s3cchan->id];
	struct s3c24xx_txd *txd;
	struct s3c24xx_sg *dsg;
	unsigned sg_len;
	dma_addr_t slave_addr;
	u32 hwcfg = 0;
	int i;

	dev_dbg(&s3cdma->pdev->dev,
		"prepare cyclic transaction of %zu bytes with period %zu from %s\n",
		size, period, s3cchan->name);

	if (!is_slave_direction(direction)) {
		dev_err(&s3cdma->pdev->dev,
			"direction %d unsupported\n", direction);
		return NULL;
	}

	txd = s3c24xx_dma_get_txd();
	if (!txd)
		return NULL;

	txd->cyclic = 1;

	if (cdata->handshake)
		txd->dcon |= S3C24XX_DCON_HANDSHAKE;

	switch (cdata->bus) {
	case S3C24XX_DMA_APB:
		txd->dcon |= S3C24XX_DCON_SYNC_PCLK;
		hwcfg |= S3C24XX_DISRCC_LOC_APB;
		break;
	case S3C24XX_DMA_AHB:
		txd->dcon |= S3C24XX_DCON_SYNC_HCLK;
		hwcfg |= S3C24XX_DISRCC_LOC_AHB;
		break;
	}

	/*
	 * Always assume our peripheral desintation is a fixed
	 * address in memory.
	 */
	hwcfg |= S3C24XX_DISRCC_INC_FIXED;

	/*
	 * Individual dma operations are requested by the slave,
	 * so serve only single atomic operations (S3C24XX_DCON_SERV_SINGLE).
	 */
	txd->dcon |= S3C24XX_DCON_SERV_SINGLE;

	if (direction == DMA_MEM_TO_DEV) {
		txd->disrcc = S3C24XX_DISRCC_LOC_AHB |
			      S3C24XX_DISRCC_INC_INCREMENT;
		txd->didstc = hwcfg;
		slave_addr = s3cchan->cfg.dst_addr;
		txd->width = s3cchan->cfg.dst_addr_width;
	} else {
		txd->disrcc = hwcfg;
		txd->didstc = S3C24XX_DIDSTC_LOC_AHB |
			      S3C24XX_DIDSTC_INC_INCREMENT;
		slave_addr = s3cchan->cfg.src_addr;
		txd->width = s3cchan->cfg.src_addr_width;
	}

	sg_len = size / period;

	for (i = 0; i < sg_len; i++) {
		dsg = kzalloc(sizeof(*dsg), GFP_NOWAIT);
		if (!dsg) {
			s3c24xx_dma_free_txd(txd);
			return NULL;
		}
		list_add_tail(&dsg->node, &txd->dsg_list);

		dsg->len = period;
		/* Check last period length */
		if (i == sg_len - 1)
			dsg->len = size - period * i;
		if (direction == DMA_MEM_TO_DEV) {
			dsg->src_addr = addr + period * i;
			dsg->dst_addr = slave_addr;
		} else { /* DMA_DEV_TO_MEM */
			dsg->src_addr = slave_addr;
			dsg->dst_addr = addr + period * i;
		}
	}

	return vchan_tx_prep(&s3cchan->vc, &txd->vd, flags);
}

static struct dma_async_tx_descriptor *s3c24xx_dma_prep_slave_sg(
		struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context)
{
	struct s3c24xx_dma_chan *s3cchan = to_s3c24xx_dma_chan(chan);
	struct s3c24xx_dma_engine *s3cdma = s3cchan->host;
	const struct s3c24xx_dma_platdata *pdata = s3cdma->pdata;
	struct s3c24xx_dma_channel *cdata = &pdata->channels[s3cchan->id];
	struct s3c24xx_txd *txd;
	struct s3c24xx_sg *dsg;
	struct scatterlist *sg;
	dma_addr_t slave_addr;
	u32 hwcfg = 0;
	int tmp;

	dev_dbg(&s3cdma->pdev->dev, "prepare transaction of %d bytes from %s\n",
			sg_dma_len(sgl), s3cchan->name);

	txd = s3c24xx_dma_get_txd();
	if (!txd)
		return NULL;

	if (cdata->handshake)
		txd->dcon |= S3C24XX_DCON_HANDSHAKE;

	switch (cdata->bus) {
	case S3C24XX_DMA_APB:
		txd->dcon |= S3C24XX_DCON_SYNC_PCLK;
		hwcfg |= S3C24XX_DISRCC_LOC_APB;
		break;
	case S3C24XX_DMA_AHB:
		txd->dcon |= S3C24XX_DCON_SYNC_HCLK;
		hwcfg |= S3C24XX_DISRCC_LOC_AHB;
		break;
	}

	/*
	 * Always assume our peripheral desintation is a fixed
	 * address in memory.
	 */
	hwcfg |= S3C24XX_DISRCC_INC_FIXED;

	/*
	 * Individual dma operations are requested by the slave,
	 * so serve only single atomic operations (S3C24XX_DCON_SERV_SINGLE).
	 */
	txd->dcon |= S3C24XX_DCON_SERV_SINGLE;

	if (direction == DMA_MEM_TO_DEV) {
		txd->disrcc = S3C24XX_DISRCC_LOC_AHB |
			      S3C24XX_DISRCC_INC_INCREMENT;
		txd->didstc = hwcfg;
		slave_addr = s3cchan->cfg.dst_addr;
		txd->width = s3cchan->cfg.dst_addr_width;
	} else if (direction == DMA_DEV_TO_MEM) {
		txd->disrcc = hwcfg;
		txd->didstc = S3C24XX_DIDSTC_LOC_AHB |
			      S3C24XX_DIDSTC_INC_INCREMENT;
		slave_addr = s3cchan->cfg.src_addr;
		txd->width = s3cchan->cfg.src_addr_width;
	} else {
		s3c24xx_dma_free_txd(txd);
		dev_err(&s3cdma->pdev->dev,
			"direction %d unsupported\n", direction);
		return NULL;
	}

	for_each_sg(sgl, sg, sg_len, tmp) {
		dsg = kzalloc(sizeof(*dsg), GFP_NOWAIT);
		if (!dsg) {
			s3c24xx_dma_free_txd(txd);
			return NULL;
		}
		list_add_tail(&dsg->node, &txd->dsg_list);

		dsg->len = sg_dma_len(sg);
		if (direction == DMA_MEM_TO_DEV) {
			dsg->src_addr = sg_dma_address(sg);
			dsg->dst_addr = slave_addr;
		} else { /* DMA_DEV_TO_MEM */
			dsg->src_addr = slave_addr;
			dsg->dst_addr = sg_dma_address(sg);
		}
	}

	return vchan_tx_prep(&s3cchan->vc, &txd->vd, flags);
}

/*
 * Slave transactions callback to the slave device to allow
 * synchronization of slave DMA signals with the DMAC enable
 */
static void s3c24xx_dma_issue_pending(struct dma_chan *chan)
{
	struct s3c24xx_dma_chan *s3cchan = to_s3c24xx_dma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&s3cchan->vc.lock, flags);
	if (vchan_issue_pending(&s3cchan->vc)) {
		if (!s3cchan->phy && s3cchan->state != S3C24XX_DMA_CHAN_WAITING)
			s3c24xx_dma_phy_alloc_and_start(s3cchan);
	}
	spin_unlock_irqrestore(&s3cchan->vc.lock, flags);
}

/*
 * Bringup and teardown
 */

/*
 * Initialise the DMAC memcpy/slave channels.
 * Make a local wrapper to hold required data
 */
static int s3c24xx_dma_init_virtual_channels(struct s3c24xx_dma_engine *s3cdma,
		struct dma_device *dmadev, unsigned int channels, bool slave)
{
	struct s3c24xx_dma_chan *chan;
	int i;

	INIT_LIST_HEAD(&dmadev->channels);

	/*
	 * Register as many many memcpy as we have physical channels,
	 * we won't always be able to use all but the code will have
	 * to cope with that situation.
	 */
	for (i = 0; i < channels; i++) {
		chan = devm_kzalloc(dmadev->dev, sizeof(*chan), GFP_KERNEL);
		if (!chan)
			return -ENOMEM;

		chan->id = i;
		chan->host = s3cdma;
		chan->state = S3C24XX_DMA_CHAN_IDLE;

		if (slave) {
			chan->slave = true;
			chan->name = kasprintf(GFP_KERNEL, "slave%d", i);
			if (!chan->name)
				return -ENOMEM;
		} else {
			chan->name = kasprintf(GFP_KERNEL, "memcpy%d", i);
			if (!chan->name)
				return -ENOMEM;
		}
		dev_dbg(dmadev->dev,
			 "initialize virtual channel \"%s\"\n",
			 chan->name);

		chan->vc.desc_free = s3c24xx_dma_desc_free;
		vchan_init(&chan->vc, dmadev);
	}
	dev_info(dmadev->dev, "initialized %d virtual %s channels\n",
		 i, slave ? "slave" : "memcpy");
	return i;
}

static void s3c24xx_dma_free_virtual_channels(struct dma_device *dmadev)
{
	struct s3c24xx_dma_chan *chan = NULL;
	struct s3c24xx_dma_chan *next;

	list_for_each_entry_safe(chan,
				 next, &dmadev->channels, vc.chan.device_node) {
		list_del(&chan->vc.chan.device_node);
		tasklet_kill(&chan->vc.task);
	}
}

/* s3c2410, s3c2440 and s3c2442 have a 0x40 stride without separate clocks */
static struct soc_data soc_s3c2410 = {
	.stride = 0x40,
	.has_reqsel = false,
	.has_clocks = false,
};

/* s3c2412 and s3c2413 have a 0x40 stride and dmareqsel mechanism */
static struct soc_data soc_s3c2412 = {
	.stride = 0x40,
	.has_reqsel = true,
	.has_clocks = true,
};

/* s3c2443 and following have a 0x100 stride and dmareqsel mechanism */
static struct soc_data soc_s3c2443 = {
	.stride = 0x100,
	.has_reqsel = true,
	.has_clocks = true,
};

static const struct platform_device_id s3c24xx_dma_driver_ids[] = {
	{
		.name		= "s3c2410-dma",
		.driver_data	= (kernel_ulong_t)&soc_s3c2410,
	}, {
		.name		= "s3c2412-dma",
		.driver_data	= (kernel_ulong_t)&soc_s3c2412,
	}, {
		.name		= "s3c2443-dma",
		.driver_data	= (kernel_ulong_t)&soc_s3c2443,
	},
	{ },
};

static struct soc_data *s3c24xx_dma_get_soc_data(struct platform_device *pdev)
{
	return (struct soc_data *)
			 platform_get_device_id(pdev)->driver_data;
}

static int s3c24xx_dma_probe(struct platform_device *pdev)
{
	const struct s3c24xx_dma_platdata *pdata = dev_get_platdata(&pdev->dev);
	struct s3c24xx_dma_engine *s3cdma;
	struct soc_data *sdata;
	struct resource *res;
	int ret;
	int i;

	if (!pdata) {
		dev_err(&pdev->dev, "platform data missing\n");
		return -ENODEV;
	}

	/* Basic sanity check */
	if (pdata->num_phy_channels > MAX_DMA_CHANNELS) {
		dev_err(&pdev->dev, "to many dma channels %d, max %d\n",
			pdata->num_phy_channels, MAX_DMA_CHANNELS);
		return -EINVAL;
	}

	sdata = s3c24xx_dma_get_soc_data(pdev);
	if (!sdata)
		return -EINVAL;

	s3cdma = devm_kzalloc(&pdev->dev, sizeof(*s3cdma), GFP_KERNEL);
	if (!s3cdma)
		return -ENOMEM;

	s3cdma->pdev = pdev;
	s3cdma->pdata = pdata;
	s3cdma->sdata = sdata;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	s3cdma->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(s3cdma->base))
		return PTR_ERR(s3cdma->base);

	s3cdma->phy_chans = devm_kcalloc(&pdev->dev,
					      pdata->num_phy_channels,
					      sizeof(struct s3c24xx_dma_phy),
					      GFP_KERNEL);
	if (!s3cdma->phy_chans)
		return -ENOMEM;

	/* acquire irqs and clocks for all physical channels */
	for (i = 0; i < pdata->num_phy_channels; i++) {
		struct s3c24xx_dma_phy *phy = &s3cdma->phy_chans[i];
		char clk_name[6];

		phy->id = i;
		phy->base = s3cdma->base + (i * sdata->stride);
		phy->host = s3cdma;

		phy->irq = platform_get_irq(pdev, i);
		if (phy->irq < 0) {
			dev_err(&pdev->dev, "failed to get irq %d, err %d\n",
				i, phy->irq);
			continue;
		}

		ret = devm_request_irq(&pdev->dev, phy->irq, s3c24xx_dma_irq,
				       0, pdev->name, phy);
		if (ret) {
			dev_err(&pdev->dev, "Unable to request irq for channel %d, error %d\n",
				i, ret);
			continue;
		}

		if (sdata->has_clocks) {
			sprintf(clk_name, "dma.%d", i);
			phy->clk = devm_clk_get(&pdev->dev, clk_name);
			if (IS_ERR(phy->clk) && sdata->has_clocks) {
				dev_err(&pdev->dev, "unable to acquire clock for channel %d, error %lu\n",
					i, PTR_ERR(phy->clk));
				continue;
			}

			ret = clk_prepare(phy->clk);
			if (ret) {
				dev_err(&pdev->dev, "clock for phy %d failed, error %d\n",
					i, ret);
				continue;
			}
		}

		spin_lock_init(&phy->lock);
		phy->valid = true;

		dev_dbg(&pdev->dev, "physical channel %d is %s\n",
			i, s3c24xx_dma_phy_busy(phy) ? "BUSY" : "FREE");
	}

	/* Initialize memcpy engine */
	dma_cap_set(DMA_MEMCPY, s3cdma->memcpy.cap_mask);
	dma_cap_set(DMA_PRIVATE, s3cdma->memcpy.cap_mask);
	s3cdma->memcpy.dev = &pdev->dev;
	s3cdma->memcpy.device_free_chan_resources =
					s3c24xx_dma_free_chan_resources;
	s3cdma->memcpy.device_prep_dma_memcpy = s3c24xx_dma_prep_memcpy;
	s3cdma->memcpy.device_tx_status = s3c24xx_dma_tx_status;
	s3cdma->memcpy.device_issue_pending = s3c24xx_dma_issue_pending;
	s3cdma->memcpy.device_config = s3c24xx_dma_set_runtime_config;
	s3cdma->memcpy.device_terminate_all = s3c24xx_dma_terminate_all;
	s3cdma->memcpy.device_synchronize = s3c24xx_dma_synchronize;

	/* Initialize slave engine for SoC internal dedicated peripherals */
	dma_cap_set(DMA_SLAVE, s3cdma->slave.cap_mask);
	dma_cap_set(DMA_CYCLIC, s3cdma->slave.cap_mask);
	dma_cap_set(DMA_PRIVATE, s3cdma->slave.cap_mask);
	s3cdma->slave.dev = &pdev->dev;
	s3cdma->slave.device_free_chan_resources =
					s3c24xx_dma_free_chan_resources;
	s3cdma->slave.device_tx_status = s3c24xx_dma_tx_status;
	s3cdma->slave.device_issue_pending = s3c24xx_dma_issue_pending;
	s3cdma->slave.device_prep_slave_sg = s3c24xx_dma_prep_slave_sg;
	s3cdma->slave.device_prep_dma_cyclic = s3c24xx_dma_prep_dma_cyclic;
	s3cdma->slave.device_config = s3c24xx_dma_set_runtime_config;
	s3cdma->slave.device_terminate_all = s3c24xx_dma_terminate_all;
	s3cdma->slave.device_synchronize = s3c24xx_dma_synchronize;
	s3cdma->slave.filter.map = pdata->slave_map;
	s3cdma->slave.filter.mapcnt = pdata->slavecnt;
	s3cdma->slave.filter.fn = s3c24xx_dma_filter;

	/* Register as many memcpy channels as there are physical channels */
	ret = s3c24xx_dma_init_virtual_channels(s3cdma, &s3cdma->memcpy,
						pdata->num_phy_channels, false);
	if (ret <= 0) {
		dev_warn(&pdev->dev,
			 "%s failed to enumerate memcpy channels - %d\n",
			 __func__, ret);
		goto err_memcpy;
	}

	/* Register slave channels */
	ret = s3c24xx_dma_init_virtual_channels(s3cdma, &s3cdma->slave,
				pdata->num_channels, true);
	if (ret <= 0) {
		dev_warn(&pdev->dev,
			"%s failed to enumerate slave channels - %d\n",
				__func__, ret);
		goto err_slave;
	}

	ret = dma_async_device_register(&s3cdma->memcpy);
	if (ret) {
		dev_warn(&pdev->dev,
			"%s failed to register memcpy as an async device - %d\n",
			__func__, ret);
		goto err_memcpy_reg;
	}

	ret = dma_async_device_register(&s3cdma->slave);
	if (ret) {
		dev_warn(&pdev->dev,
			"%s failed to register slave as an async device - %d\n",
			__func__, ret);
		goto err_slave_reg;
	}

	platform_set_drvdata(pdev, s3cdma);
	dev_info(&pdev->dev, "Loaded dma driver with %d physical channels\n",
		 pdata->num_phy_channels);

	return 0;

err_slave_reg:
	dma_async_device_unregister(&s3cdma->memcpy);
err_memcpy_reg:
	s3c24xx_dma_free_virtual_channels(&s3cdma->slave);
err_slave:
	s3c24xx_dma_free_virtual_channels(&s3cdma->memcpy);
err_memcpy:
	if (sdata->has_clocks)
		for (i = 0; i < pdata->num_phy_channels; i++) {
			struct s3c24xx_dma_phy *phy = &s3cdma->phy_chans[i];
			if (phy->valid)
				clk_unprepare(phy->clk);
		}

	return ret;
}

static void s3c24xx_dma_free_irq(struct platform_device *pdev,
				struct s3c24xx_dma_engine *s3cdma)
{
	int i;

	for (i = 0; i < s3cdma->pdata->num_phy_channels; i++) {
		struct s3c24xx_dma_phy *phy = &s3cdma->phy_chans[i];

		devm_free_irq(&pdev->dev, phy->irq, phy);
	}
}

static int s3c24xx_dma_remove(struct platform_device *pdev)
{
	const struct s3c24xx_dma_platdata *pdata = dev_get_platdata(&pdev->dev);
	struct s3c24xx_dma_engine *s3cdma = platform_get_drvdata(pdev);
	struct soc_data *sdata = s3c24xx_dma_get_soc_data(pdev);
	int i;

	dma_async_device_unregister(&s3cdma->slave);
	dma_async_device_unregister(&s3cdma->memcpy);

	s3c24xx_dma_free_irq(pdev, s3cdma);

	s3c24xx_dma_free_virtual_channels(&s3cdma->slave);
	s3c24xx_dma_free_virtual_channels(&s3cdma->memcpy);

	if (sdata->has_clocks)
		for (i = 0; i < pdata->num_phy_channels; i++) {
			struct s3c24xx_dma_phy *phy = &s3cdma->phy_chans[i];
			if (phy->valid)
				clk_unprepare(phy->clk);
		}

	return 0;
}

static struct platform_driver s3c24xx_dma_driver = {
	.driver		= {
		.name	= "s3c24xx-dma",
	},
	.id_table	= s3c24xx_dma_driver_ids,
	.probe		= s3c24xx_dma_probe,
	.remove		= s3c24xx_dma_remove,
};

module_platform_driver(s3c24xx_dma_driver);

bool s3c24xx_dma_filter(struct dma_chan *chan, void *param)
{
	struct s3c24xx_dma_chan *s3cchan;

	if (chan->device->dev->driver != &s3c24xx_dma_driver.driver)
		return false;

	s3cchan = to_s3c24xx_dma_chan(chan);

	return s3cchan->id == (uintptr_t)param;
}
EXPORT_SYMBOL(s3c24xx_dma_filter);

MODULE_DESCRIPTION("S3C24XX DMA Driver");
MODULE_AUTHOR("Heiko Stuebner");
MODULE_LICENSE("GPL v2");
