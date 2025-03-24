// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SiFive FU540 Platform DMA driver
 * Copyright (C) 2019 SiFive
 *
 * Based partially on:
 * - drivers/dma/fsl-edma.c
 * - drivers/dma/dw-edma/
 * - drivers/dma/pxa-dma.c
 *
 * See the following sources for further documentation:
 * - Chapter 12 "Platform DMA Engine (PDMA)" of
 *   SiFive FU540-C000 v1.0
 *   https://static.dev.sifive.com/FU540-C000-v1.0.pdf
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/slab.h>

#include "sf-pdma.h"

#define PDMA_QUIRK_NO_STRICT_ORDERING   BIT(0)

#ifndef readq
static inline unsigned long long readq(void __iomem *addr)
{
	return readl(addr) | (((unsigned long long)readl(addr + 4)) << 32LL);
}
#endif

#ifndef writeq
static inline void writeq(unsigned long long v, void __iomem *addr)
{
	writel(lower_32_bits(v), addr);
	writel(upper_32_bits(v), addr + 4);
}
#endif

static inline struct sf_pdma_chan *to_sf_pdma_chan(struct dma_chan *dchan)
{
	return container_of(dchan, struct sf_pdma_chan, vchan.chan);
}

static inline struct sf_pdma_desc *to_sf_pdma_desc(struct virt_dma_desc *vd)
{
	return container_of(vd, struct sf_pdma_desc, vdesc);
}

static struct sf_pdma_desc *sf_pdma_alloc_desc(struct sf_pdma_chan *chan)
{
	struct sf_pdma_desc *desc;

	desc = kzalloc(sizeof(*desc), GFP_NOWAIT);
	if (!desc)
		return NULL;

	desc->chan = chan;

	return desc;
}

static void sf_pdma_fill_desc(struct sf_pdma_desc *desc,
			      u64 dst, u64 src, u64 size)
{
	desc->xfer_type =  desc->chan->pdma->transfer_type;
	desc->xfer_size = size;
	desc->dst_addr = dst;
	desc->src_addr = src;
}

static void sf_pdma_disclaim_chan(struct sf_pdma_chan *chan)
{
	struct pdma_regs *regs = &chan->regs;

	writel(PDMA_CLEAR_CTRL, regs->ctrl);
}

static struct dma_async_tx_descriptor *
sf_pdma_prep_dma_memcpy(struct dma_chan *dchan,	dma_addr_t dest, dma_addr_t src,
			size_t len, unsigned long flags)
{
	struct sf_pdma_chan *chan = to_sf_pdma_chan(dchan);
	struct sf_pdma_desc *desc;
	unsigned long iflags;

	if (chan && (!len || !dest || !src)) {
		dev_err(chan->pdma->dma_dev.dev,
			"Please check dma len, dest, src!\n");
		return NULL;
	}

	desc = sf_pdma_alloc_desc(chan);
	if (!desc)
		return NULL;

	desc->dirn = DMA_MEM_TO_MEM;
	desc->async_tx = vchan_tx_prep(&chan->vchan, &desc->vdesc, flags);

	spin_lock_irqsave(&chan->vchan.lock, iflags);
	sf_pdma_fill_desc(desc, dest, src, len);
	spin_unlock_irqrestore(&chan->vchan.lock, iflags);

	return desc->async_tx;
}

static int sf_pdma_slave_config(struct dma_chan *dchan,
				struct dma_slave_config *cfg)
{
	struct sf_pdma_chan *chan = to_sf_pdma_chan(dchan);

	memcpy(&chan->cfg, cfg, sizeof(*cfg));

	return 0;
}

static int sf_pdma_alloc_chan_resources(struct dma_chan *dchan)
{
	struct sf_pdma_chan *chan = to_sf_pdma_chan(dchan);
	struct pdma_regs *regs = &chan->regs;

	dma_cookie_init(dchan);
	writel(PDMA_CLAIM_MASK, regs->ctrl);

	return 0;
}

static void sf_pdma_disable_request(struct sf_pdma_chan *chan)
{
	struct pdma_regs *regs = &chan->regs;

	writel(readl(regs->ctrl) & ~PDMA_RUN_MASK, regs->ctrl);
}

static void sf_pdma_free_chan_resources(struct dma_chan *dchan)
{
	struct sf_pdma_chan *chan = to_sf_pdma_chan(dchan);
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&chan->vchan.lock, flags);
	sf_pdma_disable_request(chan);
	kfree(chan->desc);
	chan->desc = NULL;
	vchan_get_all_descriptors(&chan->vchan, &head);
	sf_pdma_disclaim_chan(chan);
	spin_unlock_irqrestore(&chan->vchan.lock, flags);
	vchan_dma_desc_free_list(&chan->vchan, &head);
}

static size_t sf_pdma_desc_residue(struct sf_pdma_chan *chan,
				   dma_cookie_t cookie)
{
	struct virt_dma_desc *vd = NULL;
	struct pdma_regs *regs = &chan->regs;
	unsigned long flags;
	u64 residue = 0;
	struct sf_pdma_desc *desc;
	struct dma_async_tx_descriptor *tx = NULL;

	spin_lock_irqsave(&chan->vchan.lock, flags);

	list_for_each_entry(vd, &chan->vchan.desc_submitted, node)
		if (vd->tx.cookie == cookie)
			tx = &vd->tx;

	if (!tx)
		goto out;

	if (cookie == tx->chan->completed_cookie)
		goto out;

	if (cookie == tx->cookie) {
		residue = readq(regs->residue);
	} else {
		vd = vchan_find_desc(&chan->vchan, cookie);
		if (!vd)
			goto out;

		desc = to_sf_pdma_desc(vd);
		residue = desc->xfer_size;
	}

out:
	spin_unlock_irqrestore(&chan->vchan.lock, flags);
	return residue;
}

static enum dma_status
sf_pdma_tx_status(struct dma_chan *dchan,
		  dma_cookie_t cookie,
		  struct dma_tx_state *txstate)
{
	struct sf_pdma_chan *chan = to_sf_pdma_chan(dchan);
	enum dma_status status;

	status = dma_cookie_status(dchan, cookie, txstate);

	if (txstate && status != DMA_ERROR)
		dma_set_residue(txstate, sf_pdma_desc_residue(chan, cookie));

	return status;
}

static int sf_pdma_terminate_all(struct dma_chan *dchan)
{
	struct sf_pdma_chan *chan = to_sf_pdma_chan(dchan);
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&chan->vchan.lock, flags);
	sf_pdma_disable_request(chan);
	kfree(chan->desc);
	chan->desc = NULL;
	chan->xfer_err = false;
	vchan_get_all_descriptors(&chan->vchan, &head);
	spin_unlock_irqrestore(&chan->vchan.lock, flags);
	vchan_dma_desc_free_list(&chan->vchan, &head);

	return 0;
}

static void sf_pdma_enable_request(struct sf_pdma_chan *chan)
{
	struct pdma_regs *regs = &chan->regs;
	u32 v;

	v = PDMA_CLAIM_MASK |
		PDMA_ENABLE_DONE_INT_MASK |
		PDMA_ENABLE_ERR_INT_MASK |
		PDMA_RUN_MASK;

	writel(v, regs->ctrl);
}

static struct sf_pdma_desc *sf_pdma_get_first_pending_desc(struct sf_pdma_chan *chan)
{
	struct virt_dma_chan *vchan = &chan->vchan;
	struct virt_dma_desc *vdesc;

	if (list_empty(&vchan->desc_issued))
		return NULL;

	vdesc = list_first_entry(&vchan->desc_issued, struct virt_dma_desc, node);

	return container_of(vdesc, struct sf_pdma_desc, vdesc);
}

static void sf_pdma_xfer_desc(struct sf_pdma_chan *chan)
{
	struct sf_pdma_desc *desc = chan->desc;
	struct pdma_regs *regs = &chan->regs;

	if (!desc) {
		dev_err(chan->pdma->dma_dev.dev, "NULL desc.\n");
		return;
	}

	writel(desc->xfer_type, regs->xfer_type);
	writeq(desc->xfer_size, regs->xfer_size);
	writeq(desc->dst_addr, regs->dst_addr);
	writeq(desc->src_addr, regs->src_addr);

	chan->desc = desc;
	chan->status = DMA_IN_PROGRESS;
	sf_pdma_enable_request(chan);
}

static void sf_pdma_issue_pending(struct dma_chan *dchan)
{
	struct sf_pdma_chan *chan = to_sf_pdma_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&chan->vchan.lock, flags);

	if (!chan->desc && vchan_issue_pending(&chan->vchan)) {
		/* vchan_issue_pending has made a check that desc in not NULL */
		chan->desc = sf_pdma_get_first_pending_desc(chan);
		sf_pdma_xfer_desc(chan);
	}

	spin_unlock_irqrestore(&chan->vchan.lock, flags);
}

static void sf_pdma_free_desc(struct virt_dma_desc *vdesc)
{
	struct sf_pdma_desc *desc;

	desc = to_sf_pdma_desc(vdesc);
	kfree(desc);
}

static void sf_pdma_donebh_tasklet(struct tasklet_struct *t)
{
	struct sf_pdma_chan *chan = from_tasklet(chan, t, done_tasklet);
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	if (chan->xfer_err) {
		chan->retries = MAX_RETRY;
		chan->status = DMA_COMPLETE;
		chan->xfer_err = false;
	}
	spin_unlock_irqrestore(&chan->lock, flags);

	spin_lock_irqsave(&chan->vchan.lock, flags);
	list_del(&chan->desc->vdesc.node);
	vchan_cookie_complete(&chan->desc->vdesc);

	chan->desc = sf_pdma_get_first_pending_desc(chan);
	if (chan->desc)
		sf_pdma_xfer_desc(chan);

	spin_unlock_irqrestore(&chan->vchan.lock, flags);
}

static void sf_pdma_errbh_tasklet(struct tasklet_struct *t)
{
	struct sf_pdma_chan *chan = from_tasklet(chan, t, err_tasklet);
	struct sf_pdma_desc *desc = chan->desc;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	if (chan->retries <= 0) {
		/* fail to recover */
		spin_unlock_irqrestore(&chan->lock, flags);
		dmaengine_desc_get_callback_invoke(desc->async_tx, NULL);
	} else {
		/* retry */
		chan->retries--;
		chan->xfer_err = true;
		chan->status = DMA_ERROR;

		sf_pdma_enable_request(chan);
		spin_unlock_irqrestore(&chan->lock, flags);
	}
}

static irqreturn_t sf_pdma_done_isr(int irq, void *dev_id)
{
	struct sf_pdma_chan *chan = dev_id;
	struct pdma_regs *regs = &chan->regs;
	u64 residue;

	spin_lock(&chan->vchan.lock);
	writel((readl(regs->ctrl)) & ~PDMA_DONE_STATUS_MASK, regs->ctrl);
	residue = readq(regs->residue);

	if (!residue) {
		tasklet_hi_schedule(&chan->done_tasklet);
	} else {
		/* submit next transaction if possible */
		struct sf_pdma_desc *desc = chan->desc;

		desc->src_addr += desc->xfer_size - residue;
		desc->dst_addr += desc->xfer_size - residue;
		desc->xfer_size = residue;

		sf_pdma_xfer_desc(chan);
	}

	spin_unlock(&chan->vchan.lock);

	return IRQ_HANDLED;
}

static irqreturn_t sf_pdma_err_isr(int irq, void *dev_id)
{
	struct sf_pdma_chan *chan = dev_id;
	struct pdma_regs *regs = &chan->regs;

	spin_lock(&chan->lock);
	writel((readl(regs->ctrl)) & ~PDMA_ERR_STATUS_MASK, regs->ctrl);
	spin_unlock(&chan->lock);

	tasklet_schedule(&chan->err_tasklet);

	return IRQ_HANDLED;
}

/**
 * sf_pdma_irq_init() - Init PDMA IRQ Handlers
 * @pdev: pointer of platform_device
 * @pdma: pointer of PDMA engine. Caller should check NULL
 *
 * Initialize DONE and ERROR interrupt handler for 4 channels. Caller should
 * make sure the pointer passed in are non-NULL. This function should be called
 * only one time during the device probe.
 *
 * Context: Any context.
 *
 * Return:
 * * 0		- OK to init all IRQ handlers
 * * -EINVAL	- Fail to request IRQ
 */
static int sf_pdma_irq_init(struct platform_device *pdev, struct sf_pdma *pdma)
{
	int irq, r, i;
	struct sf_pdma_chan *chan;

	for (i = 0; i < pdma->n_chans; i++) {
		chan = &pdma->chans[i];

		irq = platform_get_irq(pdev, i * 2);
		if (irq < 0)
			return -EINVAL;

		r = devm_request_irq(&pdev->dev, irq, sf_pdma_done_isr, 0,
				     dev_name(&pdev->dev), (void *)chan);
		if (r) {
			dev_err(&pdev->dev, "Fail to attach done ISR: %d\n", r);
			return -EINVAL;
		}

		chan->txirq = irq;

		irq = platform_get_irq(pdev, (i * 2) + 1);
		if (irq < 0)
			return -EINVAL;

		r = devm_request_irq(&pdev->dev, irq, sf_pdma_err_isr, 0,
				     dev_name(&pdev->dev), (void *)chan);
		if (r) {
			dev_err(&pdev->dev, "Fail to attach err ISR: %d\n", r);
			return -EINVAL;
		}

		chan->errirq = irq;
	}

	return 0;
}

/**
 * sf_pdma_setup_chans() - Init settings of each channel
 * @pdma: pointer of PDMA engine. Caller should check NULL
 *
 * Initialize all data structure and register base. Caller should make sure
 * the pointer passed in are non-NULL. This function should be called only
 * one time during the device probe.
 *
 * Context: Any context.
 *
 * Return: none
 */
static void sf_pdma_setup_chans(struct sf_pdma *pdma)
{
	int i;
	struct sf_pdma_chan *chan;

	INIT_LIST_HEAD(&pdma->dma_dev.channels);

	for (i = 0; i < pdma->n_chans; i++) {
		chan = &pdma->chans[i];

		chan->regs.ctrl =
			SF_PDMA_REG_BASE(i) + PDMA_CTRL;
		chan->regs.xfer_type =
			SF_PDMA_REG_BASE(i) + PDMA_XFER_TYPE;
		chan->regs.xfer_size =
			SF_PDMA_REG_BASE(i) + PDMA_XFER_SIZE;
		chan->regs.dst_addr =
			SF_PDMA_REG_BASE(i) + PDMA_DST_ADDR;
		chan->regs.src_addr =
			SF_PDMA_REG_BASE(i) + PDMA_SRC_ADDR;
		chan->regs.act_type =
			SF_PDMA_REG_BASE(i) + PDMA_ACT_TYPE;
		chan->regs.residue =
			SF_PDMA_REG_BASE(i) + PDMA_REMAINING_BYTE;
		chan->regs.cur_dst_addr =
			SF_PDMA_REG_BASE(i) + PDMA_CUR_DST_ADDR;
		chan->regs.cur_src_addr =
			SF_PDMA_REG_BASE(i) + PDMA_CUR_SRC_ADDR;

		chan->pdma = pdma;
		chan->pm_state = RUNNING;
		chan->slave_id = i;
		chan->xfer_err = false;
		spin_lock_init(&chan->lock);

		chan->vchan.desc_free = sf_pdma_free_desc;
		vchan_init(&chan->vchan, &pdma->dma_dev);

		writel(PDMA_CLEAR_CTRL, chan->regs.ctrl);

		tasklet_setup(&chan->done_tasklet, sf_pdma_donebh_tasklet);
		tasklet_setup(&chan->err_tasklet, sf_pdma_errbh_tasklet);
	}
}

static int sf_pdma_probe(struct platform_device *pdev)
{
	const struct sf_pdma_driver_platdata *ddata;
	struct sf_pdma *pdma;
	int ret, n_chans;
	const enum dma_slave_buswidth widths =
		DMA_SLAVE_BUSWIDTH_1_BYTE | DMA_SLAVE_BUSWIDTH_2_BYTES |
		DMA_SLAVE_BUSWIDTH_4_BYTES | DMA_SLAVE_BUSWIDTH_8_BYTES |
		DMA_SLAVE_BUSWIDTH_16_BYTES | DMA_SLAVE_BUSWIDTH_32_BYTES |
		DMA_SLAVE_BUSWIDTH_64_BYTES;

	ret = of_property_read_u32(pdev->dev.of_node, "dma-channels", &n_chans);
	if (ret) {
		/* backwards-compatibility for no dma-channels property */
		dev_dbg(&pdev->dev, "set number of channels to default value: 4\n");
		n_chans = PDMA_MAX_NR_CH;
	} else if (n_chans > PDMA_MAX_NR_CH) {
		dev_err(&pdev->dev, "the number of channels exceeds the maximum\n");
		return -EINVAL;
	}

	pdma = devm_kzalloc(&pdev->dev, struct_size(pdma, chans, n_chans),
			    GFP_KERNEL);
	if (!pdma)
		return -ENOMEM;

	pdma->n_chans = n_chans;

	pdma->transfer_type = PDMA_FULL_SPEED | PDMA_STRICT_ORDERING;

	ddata  = device_get_match_data(&pdev->dev);
	if (ddata) {
		if (ddata->quirks & PDMA_QUIRK_NO_STRICT_ORDERING)
			pdma->transfer_type &= ~PDMA_STRICT_ORDERING;
	}

	pdma->membase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pdma->membase))
		return PTR_ERR(pdma->membase);

	ret = sf_pdma_irq_init(pdev, pdma);
	if (ret)
		return ret;

	sf_pdma_setup_chans(pdma);

	pdma->dma_dev.dev = &pdev->dev;

	/* Setup capability */
	dma_cap_set(DMA_MEMCPY, pdma->dma_dev.cap_mask);
	pdma->dma_dev.copy_align = 2;
	pdma->dma_dev.src_addr_widths = widths;
	pdma->dma_dev.dst_addr_widths = widths;
	pdma->dma_dev.directions = BIT(DMA_MEM_TO_MEM);
	pdma->dma_dev.residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;
	pdma->dma_dev.descriptor_reuse = true;

	/* Setup DMA APIs */
	pdma->dma_dev.device_alloc_chan_resources =
		sf_pdma_alloc_chan_resources;
	pdma->dma_dev.device_free_chan_resources =
		sf_pdma_free_chan_resources;
	pdma->dma_dev.device_tx_status = sf_pdma_tx_status;
	pdma->dma_dev.device_prep_dma_memcpy = sf_pdma_prep_dma_memcpy;
	pdma->dma_dev.device_config = sf_pdma_slave_config;
	pdma->dma_dev.device_terminate_all = sf_pdma_terminate_all;
	pdma->dma_dev.device_issue_pending = sf_pdma_issue_pending;

	platform_set_drvdata(pdev, pdma);

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret)
		dev_warn(&pdev->dev,
			 "Failed to set DMA mask. Fall back to default.\n");

	ret = dma_async_device_register(&pdma->dma_dev);
	if (ret) {
		dev_err(&pdev->dev,
			"Can't register SiFive Platform DMA. (%d)\n", ret);
		return ret;
	}

	ret = of_dma_controller_register(pdev->dev.of_node,
					 of_dma_xlate_by_chan_id, pdma);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Can't register SiFive Platform OF_DMA. (%d)\n", ret);
		goto err_unregister;
	}

	return 0;

err_unregister:
	dma_async_device_unregister(&pdma->dma_dev);

	return ret;
}

static void sf_pdma_remove(struct platform_device *pdev)
{
	struct sf_pdma *pdma = platform_get_drvdata(pdev);
	struct sf_pdma_chan *ch;
	int i;

	for (i = 0; i < pdma->n_chans; i++) {
		ch = &pdma->chans[i];

		devm_free_irq(&pdev->dev, ch->txirq, ch);
		devm_free_irq(&pdev->dev, ch->errirq, ch);
		list_del(&ch->vchan.chan.device_node);
		tasklet_kill(&ch->vchan.task);
		tasklet_kill(&ch->done_tasklet);
		tasklet_kill(&ch->err_tasklet);
	}

	if (pdev->dev.of_node)
		of_dma_controller_free(pdev->dev.of_node);

	dma_async_device_unregister(&pdma->dma_dev);
}

static const struct sf_pdma_driver_platdata mpfs_pdma = {
	.quirks = PDMA_QUIRK_NO_STRICT_ORDERING,
};

static const struct of_device_id sf_pdma_dt_ids[] = {
	{
		.compatible = "sifive,fu540-c000-pdma",
	}, {
		.compatible = "sifive,pdma0",
	}, {
		.compatible = "microchip,mpfs-pdma",
		.data	    = &mpfs_pdma,
	},
	{},
};
MODULE_DEVICE_TABLE(of, sf_pdma_dt_ids);

static struct platform_driver sf_pdma_driver = {
	.probe		= sf_pdma_probe,
	.remove		= sf_pdma_remove,
	.driver		= {
		.name	= "sf-pdma",
		.of_match_table = sf_pdma_dt_ids,
	},
};

static int __init sf_pdma_init(void)
{
	return platform_driver_register(&sf_pdma_driver);
}

static void __exit sf_pdma_exit(void)
{
	platform_driver_unregister(&sf_pdma_driver);
}

/* do early init */
subsys_initcall(sf_pdma_init);
module_exit(sf_pdma_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("SiFive Platform DMA driver");
MODULE_AUTHOR("Green Wan <green.wan@sifive.com>");
