/*
 * Copyright (C) Freescale Semicondutor, Inc. 2007, 2008.
 * Copyright (C) Semihalf 2009
 *
 * Written by Piotr Ziecik <kosmo@semihalf.com>. Hardware description
 * (defines, structures and comments) was taken from MPC5121 DMA driver
 * written by Hongjun Chen <hong-jun.chen@freescale.com>.
 *
 * Approved as OSADL project by a majority of OSADL members and funded
 * by OSADL membership fees in 2009;  for details see www.osadl.org.
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
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 */

/*
 * This is initial version of MPC5121 DMA driver. Only memory to memory
 * transfers are supported (tested using dmatest module).
 */

#include <linux/module.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

#include <linux/random.h>

/* Number of DMA Transfer descriptors allocated per channel */
#define MPC_DMA_DESCRIPTORS	64

/* Macro definitions */
#define MPC_DMA_CHANNELS	64
#define MPC_DMA_TCD_OFFSET	0x1000

/* Arbitration mode of group and channel */
#define MPC_DMA_DMACR_EDCG	(1 << 31)
#define MPC_DMA_DMACR_ERGA	(1 << 3)
#define MPC_DMA_DMACR_ERCA	(1 << 2)

/* Error codes */
#define MPC_DMA_DMAES_VLD	(1 << 31)
#define MPC_DMA_DMAES_GPE	(1 << 15)
#define MPC_DMA_DMAES_CPE	(1 << 14)
#define MPC_DMA_DMAES_ERRCHN(err) \
				(((err) >> 8) & 0x3f)
#define MPC_DMA_DMAES_SAE	(1 << 7)
#define MPC_DMA_DMAES_SOE	(1 << 6)
#define MPC_DMA_DMAES_DAE	(1 << 5)
#define MPC_DMA_DMAES_DOE	(1 << 4)
#define MPC_DMA_DMAES_NCE	(1 << 3)
#define MPC_DMA_DMAES_SGE	(1 << 2)
#define MPC_DMA_DMAES_SBE	(1 << 1)
#define MPC_DMA_DMAES_DBE	(1 << 0)

#define MPC_DMA_TSIZE_1		0x00
#define MPC_DMA_TSIZE_2		0x01
#define MPC_DMA_TSIZE_4		0x02
#define MPC_DMA_TSIZE_16	0x04
#define MPC_DMA_TSIZE_32	0x05

/* MPC5121 DMA engine registers */
struct __attribute__ ((__packed__)) mpc_dma_regs {
	/* 0x00 */
	u32 dmacr;		/* DMA control register */
	u32 dmaes;		/* DMA error status */
	/* 0x08 */
	u32 dmaerqh;		/* DMA enable request high(channels 63~32) */
	u32 dmaerql;		/* DMA enable request low(channels 31~0) */
	u32 dmaeeih;		/* DMA enable error interrupt high(ch63~32) */
	u32 dmaeeil;		/* DMA enable error interrupt low(ch31~0) */
	/* 0x18 */
	u8 dmaserq;		/* DMA set enable request */
	u8 dmacerq;		/* DMA clear enable request */
	u8 dmaseei;		/* DMA set enable error interrupt */
	u8 dmaceei;		/* DMA clear enable error interrupt */
	/* 0x1c */
	u8 dmacint;		/* DMA clear interrupt request */
	u8 dmacerr;		/* DMA clear error */
	u8 dmassrt;		/* DMA set start bit */
	u8 dmacdne;		/* DMA clear DONE status bit */
	/* 0x20 */
	u32 dmainth;		/* DMA interrupt request high(ch63~32) */
	u32 dmaintl;		/* DMA interrupt request low(ch31~0) */
	u32 dmaerrh;		/* DMA error high(ch63~32) */
	u32 dmaerrl;		/* DMA error low(ch31~0) */
	/* 0x30 */
	u32 dmahrsh;		/* DMA hw request status high(ch63~32) */
	u32 dmahrsl;		/* DMA hardware request status low(ch31~0) */
	u32 dmaihsa;		/* DMA interrupt high select AXE(ch63~32) */
	u32 dmailsa;		/* DMA interrupt low select AXE(ch31~0) */
	/* 0x40 ~ 0xff */
	u32 reserve0[48];	/* Reserved */
	/* 0x100 */
	u8 dchpri[MPC_DMA_CHANNELS];
	/* DMA channels(0~63) priority */
};

struct __attribute__ ((__packed__)) mpc_dma_tcd {
	/* 0x00 */
	u32 saddr;		/* Source address */

	u32 smod:5;		/* Source address modulo */
	u32 ssize:3;		/* Source data transfer size */
	u32 dmod:5;		/* Destination address modulo */
	u32 dsize:3;		/* Destination data transfer size */
	u32 soff:16;		/* Signed source address offset */

	/* 0x08 */
	u32 nbytes;		/* Inner "minor" byte count */
	u32 slast;		/* Last source address adjustment */
	u32 daddr;		/* Destination address */

	/* 0x14 */
	u32 citer_elink:1;	/* Enable channel-to-channel linking on
				 * minor loop complete
				 */
	u32 citer_linkch:6;	/* Link channel for minor loop complete */
	u32 citer:9;		/* Current "major" iteration count */
	u32 doff:16;		/* Signed destination address offset */

	/* 0x18 */
	u32 dlast_sga;		/* Last Destination address adjustment/scatter
				 * gather address
				 */

	/* 0x1c */
	u32 biter_elink:1;	/* Enable channel-to-channel linking on major
				 * loop complete
				 */
	u32 biter_linkch:6;
	u32 biter:9;		/* Beginning "major" iteration count */
	u32 bwc:2;		/* Bandwidth control */
	u32 major_linkch:6;	/* Link channel number */
	u32 done:1;		/* Channel done */
	u32 active:1;		/* Channel active */
	u32 major_elink:1;	/* Enable channel-to-channel linking on major
				 * loop complete
				 */
	u32 e_sg:1;		/* Enable scatter/gather processing */
	u32 d_req:1;		/* Disable request */
	u32 int_half:1;		/* Enable an interrupt when major counter is
				 * half complete
				 */
	u32 int_maj:1;		/* Enable an interrupt when major iteration
				 * count completes
				 */
	u32 start:1;		/* Channel start */
};

struct mpc_dma_desc {
	struct dma_async_tx_descriptor	desc;
	struct mpc_dma_tcd		*tcd;
	dma_addr_t			tcd_paddr;
	int				error;
	struct list_head		node;
};

struct mpc_dma_chan {
	struct dma_chan			chan;
	struct list_head		free;
	struct list_head		prepared;
	struct list_head		queued;
	struct list_head		active;
	struct list_head		completed;
	struct mpc_dma_tcd		*tcd;
	dma_addr_t			tcd_paddr;
	dma_cookie_t			completed_cookie;

	/* Lock for this structure */
	spinlock_t			lock;
};

struct mpc_dma {
	struct dma_device		dma;
	struct tasklet_struct		tasklet;
	struct mpc_dma_chan		channels[MPC_DMA_CHANNELS];
	struct mpc_dma_regs __iomem	*regs;
	struct mpc_dma_tcd __iomem	*tcd;
	int				irq;
	uint				error_status;

	/* Lock for error_status field in this structure */
	spinlock_t			error_status_lock;
};

#define DRV_NAME	"mpc512x_dma"

/* Convert struct dma_chan to struct mpc_dma_chan */
static inline struct mpc_dma_chan *dma_chan_to_mpc_dma_chan(struct dma_chan *c)
{
	return container_of(c, struct mpc_dma_chan, chan);
}

/* Convert struct dma_chan to struct mpc_dma */
static inline struct mpc_dma *dma_chan_to_mpc_dma(struct dma_chan *c)
{
	struct mpc_dma_chan *mchan = dma_chan_to_mpc_dma_chan(c);
	return container_of(mchan, struct mpc_dma, channels[c->chan_id]);
}

/*
 * Execute all queued DMA descriptors.
 *
 * Following requirements must be met while calling mpc_dma_execute():
 * 	a) mchan->lock is acquired,
 * 	b) mchan->active list is empty,
 * 	c) mchan->queued list contains at least one entry.
 */
static void mpc_dma_execute(struct mpc_dma_chan *mchan)
{
	struct mpc_dma *mdma = dma_chan_to_mpc_dma(&mchan->chan);
	struct mpc_dma_desc *first = NULL;
	struct mpc_dma_desc *prev = NULL;
	struct mpc_dma_desc *mdesc;
	int cid = mchan->chan.chan_id;

	/* Move all queued descriptors to active list */
	list_splice_tail_init(&mchan->queued, &mchan->active);

	/* Chain descriptors into one transaction */
	list_for_each_entry(mdesc, &mchan->active, node) {
		if (!first)
			first = mdesc;

		if (!prev) {
			prev = mdesc;
			continue;
		}

		prev->tcd->dlast_sga = mdesc->tcd_paddr;
		prev->tcd->e_sg = 1;
		mdesc->tcd->start = 1;

		prev = mdesc;
	}

	prev->tcd->start = 0;
	prev->tcd->int_maj = 1;

	/* Send first descriptor in chain into hardware */
	memcpy_toio(&mdma->tcd[cid], first->tcd, sizeof(struct mpc_dma_tcd));
	out_8(&mdma->regs->dmassrt, cid);
}

/* Handle interrupt on one half of DMA controller (32 channels) */
static void mpc_dma_irq_process(struct mpc_dma *mdma, u32 is, u32 es, int off)
{
	struct mpc_dma_chan *mchan;
	struct mpc_dma_desc *mdesc;
	u32 status = is | es;
	int ch;

	while ((ch = fls(status) - 1) >= 0) {
		status &= ~(1 << ch);
		mchan = &mdma->channels[ch + off];

		spin_lock(&mchan->lock);

		/* Check error status */
		if (es & (1 << ch))
			list_for_each_entry(mdesc, &mchan->active, node)
				mdesc->error = -EIO;

		/* Execute queued descriptors */
		list_splice_tail_init(&mchan->active, &mchan->completed);
		if (!list_empty(&mchan->queued))
			mpc_dma_execute(mchan);

		spin_unlock(&mchan->lock);
	}
}

/* Interrupt handler */
static irqreturn_t mpc_dma_irq(int irq, void *data)
{
	struct mpc_dma *mdma = data;
	uint es;

	/* Save error status register */
	es = in_be32(&mdma->regs->dmaes);
	spin_lock(&mdma->error_status_lock);
	if ((es & MPC_DMA_DMAES_VLD) && mdma->error_status == 0)
		mdma->error_status = es;
	spin_unlock(&mdma->error_status_lock);

	/* Handle interrupt on each channel */
	mpc_dma_irq_process(mdma, in_be32(&mdma->regs->dmainth),
					in_be32(&mdma->regs->dmaerrh), 32);
	mpc_dma_irq_process(mdma, in_be32(&mdma->regs->dmaintl),
					in_be32(&mdma->regs->dmaerrl), 0);

	/* Ack interrupt on all channels */
	out_be32(&mdma->regs->dmainth, 0xFFFFFFFF);
	out_be32(&mdma->regs->dmaintl, 0xFFFFFFFF);
	out_be32(&mdma->regs->dmaerrh, 0xFFFFFFFF);
	out_be32(&mdma->regs->dmaerrl, 0xFFFFFFFF);

	/* Schedule tasklet */
	tasklet_schedule(&mdma->tasklet);

	return IRQ_HANDLED;
}

/* DMA Tasklet */
static void mpc_dma_tasklet(unsigned long data)
{
	struct mpc_dma *mdma = (void *)data;
	dma_cookie_t last_cookie = 0;
	struct mpc_dma_chan *mchan;
	struct mpc_dma_desc *mdesc;
	struct dma_async_tx_descriptor *desc;
	unsigned long flags;
	LIST_HEAD(list);
	uint es;
	int i;

	spin_lock_irqsave(&mdma->error_status_lock, flags);
	es = mdma->error_status;
	mdma->error_status = 0;
	spin_unlock_irqrestore(&mdma->error_status_lock, flags);

	/* Print nice error report */
	if (es) {
		dev_err(mdma->dma.dev,
			"Hardware reported following error(s) on channel %u:\n",
						      MPC_DMA_DMAES_ERRCHN(es));

		if (es & MPC_DMA_DMAES_GPE)
			dev_err(mdma->dma.dev, "- Group Priority Error\n");
		if (es & MPC_DMA_DMAES_CPE)
			dev_err(mdma->dma.dev, "- Channel Priority Error\n");
		if (es & MPC_DMA_DMAES_SAE)
			dev_err(mdma->dma.dev, "- Source Address Error\n");
		if (es & MPC_DMA_DMAES_SOE)
			dev_err(mdma->dma.dev, "- Source Offset"
						" Configuration Error\n");
		if (es & MPC_DMA_DMAES_DAE)
			dev_err(mdma->dma.dev, "- Destination Address"
								" Error\n");
		if (es & MPC_DMA_DMAES_DOE)
			dev_err(mdma->dma.dev, "- Destination Offset"
						" Configuration Error\n");
		if (es & MPC_DMA_DMAES_NCE)
			dev_err(mdma->dma.dev, "- NBytes/Citter"
						" Configuration Error\n");
		if (es & MPC_DMA_DMAES_SGE)
			dev_err(mdma->dma.dev, "- Scatter/Gather"
						" Configuration Error\n");
		if (es & MPC_DMA_DMAES_SBE)
			dev_err(mdma->dma.dev, "- Source Bus Error\n");
		if (es & MPC_DMA_DMAES_DBE)
			dev_err(mdma->dma.dev, "- Destination Bus Error\n");
	}

	for (i = 0; i < mdma->dma.chancnt; i++) {
		mchan = &mdma->channels[i];

		/* Get all completed descriptors */
		spin_lock_irqsave(&mchan->lock, flags);
		if (!list_empty(&mchan->completed))
			list_splice_tail_init(&mchan->completed, &list);
		spin_unlock_irqrestore(&mchan->lock, flags);

		if (list_empty(&list))
			continue;

		/* Execute callbacks and run dependencies */
		list_for_each_entry(mdesc, &list, node) {
			desc = &mdesc->desc;

			if (desc->callback)
				desc->callback(desc->callback_param);

			last_cookie = desc->cookie;
			dma_run_dependencies(desc);
		}

		/* Free descriptors */
		spin_lock_irqsave(&mchan->lock, flags);
		list_splice_tail_init(&list, &mchan->free);
		mchan->completed_cookie = last_cookie;
		spin_unlock_irqrestore(&mchan->lock, flags);
	}
}

/* Submit descriptor to hardware */
static dma_cookie_t mpc_dma_tx_submit(struct dma_async_tx_descriptor *txd)
{
	struct mpc_dma_chan *mchan = dma_chan_to_mpc_dma_chan(txd->chan);
	struct mpc_dma_desc *mdesc;
	unsigned long flags;
	dma_cookie_t cookie;

	mdesc = container_of(txd, struct mpc_dma_desc, desc);

	spin_lock_irqsave(&mchan->lock, flags);

	/* Move descriptor to queue */
	list_move_tail(&mdesc->node, &mchan->queued);

	/* If channel is idle, execute all queued descriptors */
	if (list_empty(&mchan->active))
		mpc_dma_execute(mchan);

	/* Update cookie */
	cookie = mchan->chan.cookie + 1;
	if (cookie <= 0)
		cookie = 1;

	mchan->chan.cookie = cookie;
	mdesc->desc.cookie = cookie;

	spin_unlock_irqrestore(&mchan->lock, flags);

	return cookie;
}

/* Alloc channel resources */
static int mpc_dma_alloc_chan_resources(struct dma_chan *chan)
{
	struct mpc_dma *mdma = dma_chan_to_mpc_dma(chan);
	struct mpc_dma_chan *mchan = dma_chan_to_mpc_dma_chan(chan);
	struct mpc_dma_desc *mdesc;
	struct mpc_dma_tcd *tcd;
	dma_addr_t tcd_paddr;
	unsigned long flags;
	LIST_HEAD(descs);
	int i;

	/* Alloc DMA memory for Transfer Control Descriptors */
	tcd = dma_alloc_coherent(mdma->dma.dev,
			MPC_DMA_DESCRIPTORS * sizeof(struct mpc_dma_tcd),
							&tcd_paddr, GFP_KERNEL);
	if (!tcd)
		return -ENOMEM;

	/* Alloc descriptors for this channel */
	for (i = 0; i < MPC_DMA_DESCRIPTORS; i++) {
		mdesc = kzalloc(sizeof(struct mpc_dma_desc), GFP_KERNEL);
		if (!mdesc) {
			dev_notice(mdma->dma.dev, "Memory allocation error. "
					"Allocated only %u descriptors\n", i);
			break;
		}

		dma_async_tx_descriptor_init(&mdesc->desc, chan);
		mdesc->desc.flags = DMA_CTRL_ACK;
		mdesc->desc.tx_submit = mpc_dma_tx_submit;

		mdesc->tcd = &tcd[i];
		mdesc->tcd_paddr = tcd_paddr + (i * sizeof(struct mpc_dma_tcd));

		list_add_tail(&mdesc->node, &descs);
	}

	/* Return error only if no descriptors were allocated */
	if (i == 0) {
		dma_free_coherent(mdma->dma.dev,
			MPC_DMA_DESCRIPTORS * sizeof(struct mpc_dma_tcd),
								tcd, tcd_paddr);
		return -ENOMEM;
	}

	spin_lock_irqsave(&mchan->lock, flags);
	mchan->tcd = tcd;
	mchan->tcd_paddr = tcd_paddr;
	list_splice_tail_init(&descs, &mchan->free);
	spin_unlock_irqrestore(&mchan->lock, flags);

	/* Enable Error Interrupt */
	out_8(&mdma->regs->dmaseei, chan->chan_id);

	return 0;
}

/* Free channel resources */
static void mpc_dma_free_chan_resources(struct dma_chan *chan)
{
	struct mpc_dma *mdma = dma_chan_to_mpc_dma(chan);
	struct mpc_dma_chan *mchan = dma_chan_to_mpc_dma_chan(chan);
	struct mpc_dma_desc *mdesc, *tmp;
	struct mpc_dma_tcd *tcd;
	dma_addr_t tcd_paddr;
	unsigned long flags;
	LIST_HEAD(descs);

	spin_lock_irqsave(&mchan->lock, flags);

	/* Channel must be idle */
	BUG_ON(!list_empty(&mchan->prepared));
	BUG_ON(!list_empty(&mchan->queued));
	BUG_ON(!list_empty(&mchan->active));
	BUG_ON(!list_empty(&mchan->completed));

	/* Move data */
	list_splice_tail_init(&mchan->free, &descs);
	tcd = mchan->tcd;
	tcd_paddr = mchan->tcd_paddr;

	spin_unlock_irqrestore(&mchan->lock, flags);

	/* Free DMA memory used by descriptors */
	dma_free_coherent(mdma->dma.dev,
			MPC_DMA_DESCRIPTORS * sizeof(struct mpc_dma_tcd),
								tcd, tcd_paddr);

	/* Free descriptors */
	list_for_each_entry_safe(mdesc, tmp, &descs, node)
		kfree(mdesc);

	/* Disable Error Interrupt */
	out_8(&mdma->regs->dmaceei, chan->chan_id);
}

/* Send all pending descriptor to hardware */
static void mpc_dma_issue_pending(struct dma_chan *chan)
{
	/*
	 * We are posting descriptors to the hardware as soon as
	 * they are ready, so this function does nothing.
	 */
}

/* Check request completion status */
static enum dma_status
mpc_dma_is_tx_complete(struct dma_chan *chan, dma_cookie_t cookie,
					dma_cookie_t *done, dma_cookie_t *used)
{
	struct mpc_dma_chan *mchan = dma_chan_to_mpc_dma_chan(chan);
	unsigned long flags;
	dma_cookie_t last_used;
	dma_cookie_t last_complete;

	spin_lock_irqsave(&mchan->lock, flags);
	last_used = mchan->chan.cookie;
	last_complete = mchan->completed_cookie;
	spin_unlock_irqrestore(&mchan->lock, flags);

	if (done)
		*done = last_complete;

	if (used)
		*used = last_used;

	return dma_async_is_complete(cookie, last_complete, last_used);
}

/* Prepare descriptor for memory to memory copy */
static struct dma_async_tx_descriptor *
mpc_dma_prep_memcpy(struct dma_chan *chan, dma_addr_t dst, dma_addr_t src,
					size_t len, unsigned long flags)
{
	struct mpc_dma_chan *mchan = dma_chan_to_mpc_dma_chan(chan);
	struct mpc_dma_desc *mdesc = NULL;
	struct mpc_dma_tcd *tcd;
	unsigned long iflags;

	/* Get free descriptor */
	spin_lock_irqsave(&mchan->lock, iflags);
	if (!list_empty(&mchan->free)) {
		mdesc = list_first_entry(&mchan->free, struct mpc_dma_desc,
									node);
		list_del(&mdesc->node);
	}
	spin_unlock_irqrestore(&mchan->lock, iflags);

	if (!mdesc)
		return NULL;

	mdesc->error = 0;
	tcd = mdesc->tcd;

	/* Prepare Transfer Control Descriptor for this transaction */
	memset(tcd, 0, sizeof(struct mpc_dma_tcd));

	if (IS_ALIGNED(src | dst | len, 32)) {
		tcd->ssize = MPC_DMA_TSIZE_32;
		tcd->dsize = MPC_DMA_TSIZE_32;
		tcd->soff = 32;
		tcd->doff = 32;
	} else if (IS_ALIGNED(src | dst | len, 16)) {
		tcd->ssize = MPC_DMA_TSIZE_16;
		tcd->dsize = MPC_DMA_TSIZE_16;
		tcd->soff = 16;
		tcd->doff = 16;
	} else if (IS_ALIGNED(src | dst | len, 4)) {
		tcd->ssize = MPC_DMA_TSIZE_4;
		tcd->dsize = MPC_DMA_TSIZE_4;
		tcd->soff = 4;
		tcd->doff = 4;
	} else if (IS_ALIGNED(src | dst | len, 2)) {
		tcd->ssize = MPC_DMA_TSIZE_2;
		tcd->dsize = MPC_DMA_TSIZE_2;
		tcd->soff = 2;
		tcd->doff = 2;
	} else {
		tcd->ssize = MPC_DMA_TSIZE_1;
		tcd->dsize = MPC_DMA_TSIZE_1;
		tcd->soff = 1;
		tcd->doff = 1;
	}

	tcd->saddr = src;
	tcd->daddr = dst;
	tcd->nbytes = len;
	tcd->biter = 1;
	tcd->citer = 1;

	/* Place descriptor in prepared list */
	spin_lock_irqsave(&mchan->lock, iflags);
	list_add_tail(&mdesc->node, &mchan->prepared);
	spin_unlock_irqrestore(&mchan->lock, iflags);

	return &mdesc->desc;
}

static int __devinit mpc_dma_probe(struct of_device *op,
					const struct of_device_id *match)
{
	struct device_node *dn = op->node;
	struct device *dev = &op->dev;
	struct dma_device *dma;
	struct mpc_dma *mdma;
	struct mpc_dma_chan *mchan;
	struct resource res;
	ulong regs_start, regs_size;
	int retval, i;

	mdma = devm_kzalloc(dev, sizeof(struct mpc_dma), GFP_KERNEL);
	if (!mdma) {
		dev_err(dev, "Memory exhausted!\n");
		return -ENOMEM;
	}

	mdma->irq = irq_of_parse_and_map(dn, 0);
	if (mdma->irq == NO_IRQ) {
		dev_err(dev, "Error mapping IRQ!\n");
		return -EINVAL;
	}

	retval = of_address_to_resource(dn, 0, &res);
	if (retval) {
		dev_err(dev, "Error parsing memory region!\n");
		return retval;
	}

	regs_start = res.start;
	regs_size = res.end - res.start + 1;

	if (!devm_request_mem_region(dev, regs_start, regs_size, DRV_NAME)) {
		dev_err(dev, "Error requesting memory region!\n");
		return -EBUSY;
	}

	mdma->regs = devm_ioremap(dev, regs_start, regs_size);
	if (!mdma->regs) {
		dev_err(dev, "Error mapping memory region!\n");
		return -ENOMEM;
	}

	mdma->tcd = (struct mpc_dma_tcd *)((u8 *)(mdma->regs)
							+ MPC_DMA_TCD_OFFSET);

	retval = devm_request_irq(dev, mdma->irq, &mpc_dma_irq, 0, DRV_NAME,
									mdma);
	if (retval) {
		dev_err(dev, "Error requesting IRQ!\n");
		return -EINVAL;
	}

	spin_lock_init(&mdma->error_status_lock);

	dma = &mdma->dma;
	dma->dev = dev;
	dma->chancnt = MPC_DMA_CHANNELS;
	dma->device_alloc_chan_resources = mpc_dma_alloc_chan_resources;
	dma->device_free_chan_resources = mpc_dma_free_chan_resources;
	dma->device_issue_pending = mpc_dma_issue_pending;
	dma->device_is_tx_complete = mpc_dma_is_tx_complete;
	dma->device_prep_dma_memcpy = mpc_dma_prep_memcpy;

	INIT_LIST_HEAD(&dma->channels);
	dma_cap_set(DMA_MEMCPY, dma->cap_mask);

	for (i = 0; i < dma->chancnt; i++) {
		mchan = &mdma->channels[i];

		mchan->chan.device = dma;
		mchan->chan.chan_id = i;
		mchan->chan.cookie = 1;
		mchan->completed_cookie = mchan->chan.cookie;

		INIT_LIST_HEAD(&mchan->free);
		INIT_LIST_HEAD(&mchan->prepared);
		INIT_LIST_HEAD(&mchan->queued);
		INIT_LIST_HEAD(&mchan->active);
		INIT_LIST_HEAD(&mchan->completed);

		spin_lock_init(&mchan->lock);
		list_add_tail(&mchan->chan.device_node, &dma->channels);
	}

	tasklet_init(&mdma->tasklet, mpc_dma_tasklet, (unsigned long)mdma);

	/*
	 * Configure DMA Engine:
	 * - Dynamic clock,
	 * - Round-robin group arbitration,
	 * - Round-robin channel arbitration.
	 */
	out_be32(&mdma->regs->dmacr, MPC_DMA_DMACR_EDCG |
				MPC_DMA_DMACR_ERGA | MPC_DMA_DMACR_ERCA);

	/* Disable hardware DMA requests */
	out_be32(&mdma->regs->dmaerqh, 0);
	out_be32(&mdma->regs->dmaerql, 0);

	/* Disable error interrupts */
	out_be32(&mdma->regs->dmaeeih, 0);
	out_be32(&mdma->regs->dmaeeil, 0);

	/* Clear interrupts status */
	out_be32(&mdma->regs->dmainth, 0xFFFFFFFF);
	out_be32(&mdma->regs->dmaintl, 0xFFFFFFFF);
	out_be32(&mdma->regs->dmaerrh, 0xFFFFFFFF);
	out_be32(&mdma->regs->dmaerrl, 0xFFFFFFFF);

	/* Route interrupts to IPIC */
	out_be32(&mdma->regs->dmaihsa, 0);
	out_be32(&mdma->regs->dmailsa, 0);

	/* Register DMA engine */
	dev_set_drvdata(dev, mdma);
	retval = dma_async_device_register(dma);
	if (retval) {
		devm_free_irq(dev, mdma->irq, mdma);
		irq_dispose_mapping(mdma->irq);
	}

	return retval;
}

static int __devexit mpc_dma_remove(struct of_device *op)
{
	struct device *dev = &op->dev;
	struct mpc_dma *mdma = dev_get_drvdata(dev);

	dma_async_device_unregister(&mdma->dma);
	devm_free_irq(dev, mdma->irq, mdma);
	irq_dispose_mapping(mdma->irq);

	return 0;
}

static struct of_device_id mpc_dma_match[] = {
	{ .compatible = "fsl,mpc5121-dma", },
	{},
};

static struct of_platform_driver mpc_dma_driver = {
	.match_table	= mpc_dma_match,
	.probe		= mpc_dma_probe,
	.remove		= __devexit_p(mpc_dma_remove),
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init mpc_dma_init(void)
{
	return of_register_platform_driver(&mpc_dma_driver);
}
module_init(mpc_dma_init);

static void __exit mpc_dma_exit(void)
{
	of_unregister_platform_driver(&mpc_dma_driver);
}
module_exit(mpc_dma_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Piotr Ziecik <kosmo@semihalf.com>");
