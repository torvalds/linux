// SPDX-License-Identifier: GPL-2.0-only
/*
 * Core driver for the Intel integrated DMA 64-bit
 *
 * Copyright (C) 2015 Intel Corporation
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/dma/idma64.h>

#include "idma64.h"

/* For now we support only two channels */
#define IDMA64_NR_CHAN		2

/* ---------------------------------------------------------------------- */

static struct device *chan2dev(struct dma_chan *chan)
{
	return &chan->dev->device;
}

/* ---------------------------------------------------------------------- */

static void idma64_off(struct idma64 *idma64)
{
	unsigned short count = 100;

	dma_writel(idma64, CFG, 0);

	channel_clear_bit(idma64, MASK(XFER), idma64->all_chan_mask);
	channel_clear_bit(idma64, MASK(BLOCK), idma64->all_chan_mask);
	channel_clear_bit(idma64, MASK(SRC_TRAN), idma64->all_chan_mask);
	channel_clear_bit(idma64, MASK(DST_TRAN), idma64->all_chan_mask);
	channel_clear_bit(idma64, MASK(ERROR), idma64->all_chan_mask);

	do {
		cpu_relax();
	} while (dma_readl(idma64, CFG) & IDMA64_CFG_DMA_EN && --count);
}

static void idma64_on(struct idma64 *idma64)
{
	dma_writel(idma64, CFG, IDMA64_CFG_DMA_EN);
}

/* ---------------------------------------------------------------------- */

static void idma64_chan_init(struct idma64 *idma64, struct idma64_chan *idma64c)
{
	u32 cfghi = IDMA64C_CFGH_SRC_PER(1) | IDMA64C_CFGH_DST_PER(0);
	u32 cfglo = 0;

	/* Set default burst alignment */
	cfglo |= IDMA64C_CFGL_DST_BURST_ALIGN | IDMA64C_CFGL_SRC_BURST_ALIGN;

	channel_writel(idma64c, CFG_LO, cfglo);
	channel_writel(idma64c, CFG_HI, cfghi);

	/* Enable interrupts */
	channel_set_bit(idma64, MASK(XFER), idma64c->mask);
	channel_set_bit(idma64, MASK(ERROR), idma64c->mask);

	/*
	 * Enforce the controller to be turned on.
	 *
	 * The iDMA is turned off in ->probe() and looses context during system
	 * suspend / resume cycle. That's why we have to enable it each time we
	 * use it.
	 */
	idma64_on(idma64);
}

static void idma64_chan_stop(struct idma64 *idma64, struct idma64_chan *idma64c)
{
	channel_clear_bit(idma64, CH_EN, idma64c->mask);
}

static void idma64_chan_start(struct idma64 *idma64, struct idma64_chan *idma64c)
{
	struct idma64_desc *desc = idma64c->desc;
	struct idma64_hw_desc *hw = &desc->hw[0];

	channel_writeq(idma64c, SAR, 0);
	channel_writeq(idma64c, DAR, 0);

	channel_writel(idma64c, CTL_HI, IDMA64C_CTLH_BLOCK_TS(~0UL));
	channel_writel(idma64c, CTL_LO, IDMA64C_CTLL_LLP_S_EN | IDMA64C_CTLL_LLP_D_EN);

	channel_writeq(idma64c, LLP, hw->llp);

	channel_set_bit(idma64, CH_EN, idma64c->mask);
}

static void idma64_stop_transfer(struct idma64_chan *idma64c)
{
	struct idma64 *idma64 = to_idma64(idma64c->vchan.chan.device);

	idma64_chan_stop(idma64, idma64c);
}

static void idma64_start_transfer(struct idma64_chan *idma64c)
{
	struct idma64 *idma64 = to_idma64(idma64c->vchan.chan.device);
	struct virt_dma_desc *vdesc;

	/* Get the next descriptor */
	vdesc = vchan_next_desc(&idma64c->vchan);
	if (!vdesc) {
		idma64c->desc = NULL;
		return;
	}

	list_del(&vdesc->node);
	idma64c->desc = to_idma64_desc(vdesc);

	/* Configure the channel */
	idma64_chan_init(idma64, idma64c);

	/* Start the channel with a new descriptor */
	idma64_chan_start(idma64, idma64c);
}

/* ---------------------------------------------------------------------- */

static void idma64_chan_irq(struct idma64 *idma64, unsigned short c,
		u32 status_err, u32 status_xfer)
{
	struct idma64_chan *idma64c = &idma64->chan[c];
	struct idma64_desc *desc;

	spin_lock(&idma64c->vchan.lock);
	desc = idma64c->desc;
	if (desc) {
		if (status_err & (1 << c)) {
			dma_writel(idma64, CLEAR(ERROR), idma64c->mask);
			desc->status = DMA_ERROR;
		} else if (status_xfer & (1 << c)) {
			dma_writel(idma64, CLEAR(XFER), idma64c->mask);
			desc->status = DMA_COMPLETE;
			vchan_cookie_complete(&desc->vdesc);
			idma64_start_transfer(idma64c);
		}

		/* idma64_start_transfer() updates idma64c->desc */
		if (idma64c->desc == NULL || desc->status == DMA_ERROR)
			idma64_stop_transfer(idma64c);
	}
	spin_unlock(&idma64c->vchan.lock);
}

static irqreturn_t idma64_irq(int irq, void *dev)
{
	struct idma64 *idma64 = dev;
	u32 status = dma_readl(idma64, STATUS_INT);
	u32 status_xfer;
	u32 status_err;
	unsigned short i;

	/* Since IRQ may be shared, check if DMA controller is powered on */
	if (status == GENMASK(31, 0))
		return IRQ_NONE;

	dev_vdbg(idma64->dma.dev, "%s: status=%#x\n", __func__, status);

	/* Check if we have any interrupt from the DMA controller */
	if (!status)
		return IRQ_NONE;

	status_xfer = dma_readl(idma64, RAW(XFER));
	status_err = dma_readl(idma64, RAW(ERROR));

	for (i = 0; i < idma64->dma.chancnt; i++)
		idma64_chan_irq(idma64, i, status_err, status_xfer);

	return IRQ_HANDLED;
}

/* ---------------------------------------------------------------------- */

static struct idma64_desc *idma64_alloc_desc(unsigned int ndesc)
{
	struct idma64_desc *desc;

	desc = kzalloc(sizeof(*desc), GFP_NOWAIT);
	if (!desc)
		return NULL;

	desc->hw = kcalloc(ndesc, sizeof(*desc->hw), GFP_NOWAIT);
	if (!desc->hw) {
		kfree(desc);
		return NULL;
	}

	return desc;
}

static void idma64_desc_free(struct idma64_chan *idma64c,
		struct idma64_desc *desc)
{
	struct idma64_hw_desc *hw;

	if (desc->ndesc) {
		unsigned int i = desc->ndesc;

		do {
			hw = &desc->hw[--i];
			dma_pool_free(idma64c->pool, hw->lli, hw->llp);
		} while (i);
	}

	kfree(desc->hw);
	kfree(desc);
}

static void idma64_vdesc_free(struct virt_dma_desc *vdesc)
{
	struct idma64_chan *idma64c = to_idma64_chan(vdesc->tx.chan);

	idma64_desc_free(idma64c, to_idma64_desc(vdesc));
}

static void idma64_hw_desc_fill(struct idma64_hw_desc *hw,
		struct dma_slave_config *config,
		enum dma_transfer_direction direction, u64 llp)
{
	struct idma64_lli *lli = hw->lli;
	u64 sar, dar;
	u32 ctlhi = IDMA64C_CTLH_BLOCK_TS(hw->len);
	u32 ctllo = IDMA64C_CTLL_LLP_S_EN | IDMA64C_CTLL_LLP_D_EN;
	u32 src_width, dst_width;

	if (direction == DMA_MEM_TO_DEV) {
		sar = hw->phys;
		dar = config->dst_addr;
		ctllo |= IDMA64C_CTLL_DST_FIX | IDMA64C_CTLL_SRC_INC |
			 IDMA64C_CTLL_FC_M2P;
		src_width = __ffs(sar | hw->len | 4);
		dst_width = __ffs(config->dst_addr_width);
	} else {	/* DMA_DEV_TO_MEM */
		sar = config->src_addr;
		dar = hw->phys;
		ctllo |= IDMA64C_CTLL_DST_INC | IDMA64C_CTLL_SRC_FIX |
			 IDMA64C_CTLL_FC_P2M;
		src_width = __ffs(config->src_addr_width);
		dst_width = __ffs(dar | hw->len | 4);
	}

	lli->sar = sar;
	lli->dar = dar;

	lli->ctlhi = ctlhi;
	lli->ctllo = ctllo |
		     IDMA64C_CTLL_SRC_MSIZE(config->src_maxburst) |
		     IDMA64C_CTLL_DST_MSIZE(config->dst_maxburst) |
		     IDMA64C_CTLL_DST_WIDTH(dst_width) |
		     IDMA64C_CTLL_SRC_WIDTH(src_width);

	lli->llp = llp;
}

static void idma64_desc_fill(struct idma64_chan *idma64c,
		struct idma64_desc *desc)
{
	struct dma_slave_config *config = &idma64c->config;
	unsigned int i = desc->ndesc;
	struct idma64_hw_desc *hw = &desc->hw[i - 1];
	struct idma64_lli *lli = hw->lli;
	u64 llp = 0;

	/* Fill the hardware descriptors and link them to a list */
	do {
		hw = &desc->hw[--i];
		idma64_hw_desc_fill(hw, config, desc->direction, llp);
		llp = hw->llp;
		desc->length += hw->len;
	} while (i);

	/* Trigger an interrupt after the last block is transfered */
	lli->ctllo |= IDMA64C_CTLL_INT_EN;

	/* Disable LLP transfer in the last block */
	lli->ctllo &= ~(IDMA64C_CTLL_LLP_S_EN | IDMA64C_CTLL_LLP_D_EN);
}

static struct dma_async_tx_descriptor *idma64_prep_slave_sg(
		struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context)
{
	struct idma64_chan *idma64c = to_idma64_chan(chan);
	struct idma64_desc *desc;
	struct scatterlist *sg;
	unsigned int i;

	desc = idma64_alloc_desc(sg_len);
	if (!desc)
		return NULL;

	for_each_sg(sgl, sg, sg_len, i) {
		struct idma64_hw_desc *hw = &desc->hw[i];

		/* Allocate DMA capable memory for hardware descriptor */
		hw->lli = dma_pool_alloc(idma64c->pool, GFP_NOWAIT, &hw->llp);
		if (!hw->lli) {
			desc->ndesc = i;
			idma64_desc_free(idma64c, desc);
			return NULL;
		}

		hw->phys = sg_dma_address(sg);
		hw->len = sg_dma_len(sg);
	}

	desc->ndesc = sg_len;
	desc->direction = direction;
	desc->status = DMA_IN_PROGRESS;

	idma64_desc_fill(idma64c, desc);
	return vchan_tx_prep(&idma64c->vchan, &desc->vdesc, flags);
}

static void idma64_issue_pending(struct dma_chan *chan)
{
	struct idma64_chan *idma64c = to_idma64_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&idma64c->vchan.lock, flags);
	if (vchan_issue_pending(&idma64c->vchan) && !idma64c->desc)
		idma64_start_transfer(idma64c);
	spin_unlock_irqrestore(&idma64c->vchan.lock, flags);
}

static size_t idma64_active_desc_size(struct idma64_chan *idma64c)
{
	struct idma64_desc *desc = idma64c->desc;
	struct idma64_hw_desc *hw;
	size_t bytes = desc->length;
	u64 llp = channel_readq(idma64c, LLP);
	u32 ctlhi = channel_readl(idma64c, CTL_HI);
	unsigned int i = 0;

	do {
		hw = &desc->hw[i];
		if (hw->llp == llp)
			break;
		bytes -= hw->len;
	} while (++i < desc->ndesc);

	if (!i)
		return bytes;

	/* The current chunk is not fully transfered yet */
	bytes += desc->hw[--i].len;

	return bytes - IDMA64C_CTLH_BLOCK_TS(ctlhi);
}

static enum dma_status idma64_tx_status(struct dma_chan *chan,
		dma_cookie_t cookie, struct dma_tx_state *state)
{
	struct idma64_chan *idma64c = to_idma64_chan(chan);
	struct virt_dma_desc *vdesc;
	enum dma_status status;
	size_t bytes;
	unsigned long flags;

	status = dma_cookie_status(chan, cookie, state);
	if (status == DMA_COMPLETE)
		return status;

	spin_lock_irqsave(&idma64c->vchan.lock, flags);
	vdesc = vchan_find_desc(&idma64c->vchan, cookie);
	if (idma64c->desc && cookie == idma64c->desc->vdesc.tx.cookie) {
		bytes = idma64_active_desc_size(idma64c);
		dma_set_residue(state, bytes);
		status = idma64c->desc->status;
	} else if (vdesc) {
		bytes = to_idma64_desc(vdesc)->length;
		dma_set_residue(state, bytes);
	}
	spin_unlock_irqrestore(&idma64c->vchan.lock, flags);

	return status;
}

static void convert_burst(u32 *maxburst)
{
	if (*maxburst)
		*maxburst = __fls(*maxburst);
	else
		*maxburst = 0;
}

static int idma64_slave_config(struct dma_chan *chan,
		struct dma_slave_config *config)
{
	struct idma64_chan *idma64c = to_idma64_chan(chan);

	memcpy(&idma64c->config, config, sizeof(idma64c->config));

	convert_burst(&idma64c->config.src_maxburst);
	convert_burst(&idma64c->config.dst_maxburst);

	return 0;
}

static void idma64_chan_deactivate(struct idma64_chan *idma64c, bool drain)
{
	unsigned short count = 100;
	u32 cfglo;

	cfglo = channel_readl(idma64c, CFG_LO);
	if (drain)
		cfglo |= IDMA64C_CFGL_CH_DRAIN;
	else
		cfglo &= ~IDMA64C_CFGL_CH_DRAIN;

	channel_writel(idma64c, CFG_LO, cfglo | IDMA64C_CFGL_CH_SUSP);
	do {
		udelay(1);
		cfglo = channel_readl(idma64c, CFG_LO);
	} while (!(cfglo & IDMA64C_CFGL_FIFO_EMPTY) && --count);
}

static void idma64_chan_activate(struct idma64_chan *idma64c)
{
	u32 cfglo;

	cfglo = channel_readl(idma64c, CFG_LO);
	channel_writel(idma64c, CFG_LO, cfglo & ~IDMA64C_CFGL_CH_SUSP);
}

static int idma64_pause(struct dma_chan *chan)
{
	struct idma64_chan *idma64c = to_idma64_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&idma64c->vchan.lock, flags);
	if (idma64c->desc && idma64c->desc->status == DMA_IN_PROGRESS) {
		idma64_chan_deactivate(idma64c, false);
		idma64c->desc->status = DMA_PAUSED;
	}
	spin_unlock_irqrestore(&idma64c->vchan.lock, flags);

	return 0;
}

static int idma64_resume(struct dma_chan *chan)
{
	struct idma64_chan *idma64c = to_idma64_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&idma64c->vchan.lock, flags);
	if (idma64c->desc && idma64c->desc->status == DMA_PAUSED) {
		idma64c->desc->status = DMA_IN_PROGRESS;
		idma64_chan_activate(idma64c);
	}
	spin_unlock_irqrestore(&idma64c->vchan.lock, flags);

	return 0;
}

static int idma64_terminate_all(struct dma_chan *chan)
{
	struct idma64_chan *idma64c = to_idma64_chan(chan);
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&idma64c->vchan.lock, flags);
	idma64_chan_deactivate(idma64c, true);
	idma64_stop_transfer(idma64c);
	if (idma64c->desc) {
		idma64_vdesc_free(&idma64c->desc->vdesc);
		idma64c->desc = NULL;
	}
	vchan_get_all_descriptors(&idma64c->vchan, &head);
	spin_unlock_irqrestore(&idma64c->vchan.lock, flags);

	vchan_dma_desc_free_list(&idma64c->vchan, &head);
	return 0;
}

static void idma64_synchronize(struct dma_chan *chan)
{
	struct idma64_chan *idma64c = to_idma64_chan(chan);

	vchan_synchronize(&idma64c->vchan);
}

static int idma64_alloc_chan_resources(struct dma_chan *chan)
{
	struct idma64_chan *idma64c = to_idma64_chan(chan);

	/* Create a pool of consistent memory blocks for hardware descriptors */
	idma64c->pool = dma_pool_create(dev_name(chan2dev(chan)),
					chan->device->dev,
					sizeof(struct idma64_lli), 8, 0);
	if (!idma64c->pool) {
		dev_err(chan2dev(chan), "No memory for descriptors\n");
		return -ENOMEM;
	}

	return 0;
}

static void idma64_free_chan_resources(struct dma_chan *chan)
{
	struct idma64_chan *idma64c = to_idma64_chan(chan);

	vchan_free_chan_resources(to_virt_chan(chan));
	dma_pool_destroy(idma64c->pool);
	idma64c->pool = NULL;
}

/* ---------------------------------------------------------------------- */

#define IDMA64_BUSWIDTHS				\
	BIT(DMA_SLAVE_BUSWIDTH_1_BYTE)		|	\
	BIT(DMA_SLAVE_BUSWIDTH_2_BYTES)		|	\
	BIT(DMA_SLAVE_BUSWIDTH_4_BYTES)

static int idma64_probe(struct idma64_chip *chip)
{
	struct idma64 *idma64;
	unsigned short nr_chan = IDMA64_NR_CHAN;
	unsigned short i;
	int ret;

	idma64 = devm_kzalloc(chip->dev, sizeof(*idma64), GFP_KERNEL);
	if (!idma64)
		return -ENOMEM;

	idma64->regs = chip->regs;
	chip->idma64 = idma64;

	idma64->chan = devm_kcalloc(chip->dev, nr_chan, sizeof(*idma64->chan),
				    GFP_KERNEL);
	if (!idma64->chan)
		return -ENOMEM;

	idma64->all_chan_mask = (1 << nr_chan) - 1;

	/* Turn off iDMA controller */
	idma64_off(idma64);

	ret = devm_request_irq(chip->dev, chip->irq, idma64_irq, IRQF_SHARED,
			       dev_name(chip->dev), idma64);
	if (ret)
		return ret;

	INIT_LIST_HEAD(&idma64->dma.channels);
	for (i = 0; i < nr_chan; i++) {
		struct idma64_chan *idma64c = &idma64->chan[i];

		idma64c->vchan.desc_free = idma64_vdesc_free;
		vchan_init(&idma64c->vchan, &idma64->dma);

		idma64c->regs = idma64->regs + i * IDMA64_CH_LENGTH;
		idma64c->mask = BIT(i);
	}

	dma_cap_set(DMA_SLAVE, idma64->dma.cap_mask);
	dma_cap_set(DMA_PRIVATE, idma64->dma.cap_mask);

	idma64->dma.device_alloc_chan_resources = idma64_alloc_chan_resources;
	idma64->dma.device_free_chan_resources = idma64_free_chan_resources;

	idma64->dma.device_prep_slave_sg = idma64_prep_slave_sg;

	idma64->dma.device_issue_pending = idma64_issue_pending;
	idma64->dma.device_tx_status = idma64_tx_status;

	idma64->dma.device_config = idma64_slave_config;
	idma64->dma.device_pause = idma64_pause;
	idma64->dma.device_resume = idma64_resume;
	idma64->dma.device_terminate_all = idma64_terminate_all;
	idma64->dma.device_synchronize = idma64_synchronize;

	idma64->dma.src_addr_widths = IDMA64_BUSWIDTHS;
	idma64->dma.dst_addr_widths = IDMA64_BUSWIDTHS;
	idma64->dma.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	idma64->dma.residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;

	idma64->dma.dev = chip->sysdev;

	ret = dma_set_max_seg_size(idma64->dma.dev, IDMA64C_CTLH_BLOCK_TS_MASK);
	if (ret)
		return ret;

	ret = dma_async_device_register(&idma64->dma);
	if (ret)
		return ret;

	dev_info(chip->dev, "Found Intel integrated DMA 64-bit\n");
	return 0;
}

static int idma64_remove(struct idma64_chip *chip)
{
	struct idma64 *idma64 = chip->idma64;
	unsigned short i;

	dma_async_device_unregister(&idma64->dma);

	/*
	 * Explicitly call devm_request_irq() to avoid the side effects with
	 * the scheduled tasklets.
	 */
	devm_free_irq(chip->dev, chip->irq, idma64);

	for (i = 0; i < idma64->dma.chancnt; i++) {
		struct idma64_chan *idma64c = &idma64->chan[i];

		tasklet_kill(&idma64c->vchan.task);
	}

	return 0;
}

/* ---------------------------------------------------------------------- */

static int idma64_platform_probe(struct platform_device *pdev)
{
	struct idma64_chip *chip;
	struct device *dev = &pdev->dev;
	struct device *sysdev = dev->parent;
	struct resource *mem;
	int ret;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->irq = platform_get_irq(pdev, 0);
	if (chip->irq < 0)
		return chip->irq;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	chip->regs = devm_ioremap_resource(dev, mem);
	if (IS_ERR(chip->regs))
		return PTR_ERR(chip->regs);

	ret = dma_coerce_mask_and_coherent(sysdev, DMA_BIT_MASK(64));
	if (ret)
		return ret;

	chip->dev = dev;
	chip->sysdev = sysdev;

	ret = idma64_probe(chip);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, chip);
	return 0;
}

static int idma64_platform_remove(struct platform_device *pdev)
{
	struct idma64_chip *chip = platform_get_drvdata(pdev);

	return idma64_remove(chip);
}

static int __maybe_unused idma64_pm_suspend(struct device *dev)
{
	struct idma64_chip *chip = dev_get_drvdata(dev);

	idma64_off(chip->idma64);
	return 0;
}

static int __maybe_unused idma64_pm_resume(struct device *dev)
{
	struct idma64_chip *chip = dev_get_drvdata(dev);

	idma64_on(chip->idma64);
	return 0;
}

static const struct dev_pm_ops idma64_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(idma64_pm_suspend, idma64_pm_resume)
};

static struct platform_driver idma64_platform_driver = {
	.probe		= idma64_platform_probe,
	.remove		= idma64_platform_remove,
	.driver = {
		.name	= LPSS_IDMA64_DRIVER_NAME,
		.pm	= &idma64_dev_pm_ops,
	},
};

module_platform_driver(idma64_platform_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("iDMA64 core driver");
MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_ALIAS("platform:" LPSS_IDMA64_DRIVER_NAME);
