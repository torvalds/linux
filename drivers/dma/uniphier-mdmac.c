// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2018 Socionext Inc.
//   Author: Masahiro Yamada <yamada.masahiro@socionext.com>

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "virt-dma.h"

/* registers common for all channels */
#define UNIPHIER_MDMAC_CMD		0x000	/* issue DMA start/abort */
#define   UNIPHIER_MDMAC_CMD_ABORT		BIT(31) /* 1: abort, 0: start */

/* per-channel registers */
#define UNIPHIER_MDMAC_CH_OFFSET	0x100
#define UNIPHIER_MDMAC_CH_STRIDE	0x040

#define UNIPHIER_MDMAC_CH_IRQ_STAT	0x010	/* current hw status (RO) */
#define UNIPHIER_MDMAC_CH_IRQ_REQ	0x014	/* latched STAT (WOC) */
#define UNIPHIER_MDMAC_CH_IRQ_EN	0x018	/* IRQ enable mask */
#define UNIPHIER_MDMAC_CH_IRQ_DET	0x01c	/* REQ & EN (RO) */
#define   UNIPHIER_MDMAC_CH_IRQ__ABORT		BIT(13)
#define   UNIPHIER_MDMAC_CH_IRQ__DONE		BIT(1)
#define UNIPHIER_MDMAC_CH_SRC_MODE	0x020	/* mode of source */
#define UNIPHIER_MDMAC_CH_DEST_MODE	0x024	/* mode of destination */
#define   UNIPHIER_MDMAC_CH_MODE__ADDR_INC	(0 << 4)
#define   UNIPHIER_MDMAC_CH_MODE__ADDR_DEC	(1 << 4)
#define   UNIPHIER_MDMAC_CH_MODE__ADDR_FIXED	(2 << 4)
#define UNIPHIER_MDMAC_CH_SRC_ADDR	0x028	/* source address */
#define UNIPHIER_MDMAC_CH_DEST_ADDR	0x02c	/* destination address */
#define UNIPHIER_MDMAC_CH_SIZE		0x030	/* transfer bytes */

#define UNIPHIER_MDMAC_SLAVE_BUSWIDTHS \
	(BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) | \
	 BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) | \
	 BIT(DMA_SLAVE_BUSWIDTH_3_BYTES) | \
	 BIT(DMA_SLAVE_BUSWIDTH_4_BYTES))

struct uniphier_mdmac_desc {
	struct virt_dma_desc vd;
	struct scatterlist *sgl;
	unsigned int sg_len;
	unsigned int sg_cur;
	enum dma_transfer_direction dir;
};

struct uniphier_mdmac_chan {
	struct virt_dma_chan vc;
	struct uniphier_mdmac_device *mdev;
	struct uniphier_mdmac_desc *md;
	void __iomem *reg_ch_base;
	unsigned int chan_id;
};

struct uniphier_mdmac_device {
	struct dma_device ddev;
	struct clk *clk;
	void __iomem *reg_base;
	struct uniphier_mdmac_chan channels[0];
};

static struct uniphier_mdmac_chan *
to_uniphier_mdmac_chan(struct virt_dma_chan *vc)
{
	return container_of(vc, struct uniphier_mdmac_chan, vc);
}

static struct uniphier_mdmac_desc *
to_uniphier_mdmac_desc(struct virt_dma_desc *vd)
{
	return container_of(vd, struct uniphier_mdmac_desc, vd);
}

/* mc->vc.lock must be held by caller */
static struct uniphier_mdmac_desc *
uniphier_mdmac_next_desc(struct uniphier_mdmac_chan *mc)
{
	struct virt_dma_desc *vd;

	vd = vchan_next_desc(&mc->vc);
	if (!vd) {
		mc->md = NULL;
		return NULL;
	}

	list_del(&vd->node);

	mc->md = to_uniphier_mdmac_desc(vd);

	return mc->md;
}

/* mc->vc.lock must be held by caller */
static void uniphier_mdmac_handle(struct uniphier_mdmac_chan *mc,
				  struct uniphier_mdmac_desc *md)
{
	struct uniphier_mdmac_device *mdev = mc->mdev;
	struct scatterlist *sg;
	u32 irq_flag = UNIPHIER_MDMAC_CH_IRQ__DONE;
	u32 src_mode, src_addr, dest_mode, dest_addr, chunk_size;

	sg = &md->sgl[md->sg_cur];

	if (md->dir == DMA_MEM_TO_DEV) {
		src_mode = UNIPHIER_MDMAC_CH_MODE__ADDR_INC;
		src_addr = sg_dma_address(sg);
		dest_mode = UNIPHIER_MDMAC_CH_MODE__ADDR_FIXED;
		dest_addr = 0;
	} else {
		src_mode = UNIPHIER_MDMAC_CH_MODE__ADDR_FIXED;
		src_addr = 0;
		dest_mode = UNIPHIER_MDMAC_CH_MODE__ADDR_INC;
		dest_addr = sg_dma_address(sg);
	}

	chunk_size = sg_dma_len(sg);

	writel(src_mode, mc->reg_ch_base + UNIPHIER_MDMAC_CH_SRC_MODE);
	writel(dest_mode, mc->reg_ch_base + UNIPHIER_MDMAC_CH_DEST_MODE);
	writel(src_addr, mc->reg_ch_base + UNIPHIER_MDMAC_CH_SRC_ADDR);
	writel(dest_addr, mc->reg_ch_base + UNIPHIER_MDMAC_CH_DEST_ADDR);
	writel(chunk_size, mc->reg_ch_base + UNIPHIER_MDMAC_CH_SIZE);

	/* write 1 to clear */
	writel(irq_flag, mc->reg_ch_base + UNIPHIER_MDMAC_CH_IRQ_REQ);

	writel(irq_flag, mc->reg_ch_base + UNIPHIER_MDMAC_CH_IRQ_EN);

	writel(BIT(mc->chan_id), mdev->reg_base + UNIPHIER_MDMAC_CMD);
}

/* mc->vc.lock must be held by caller */
static void uniphier_mdmac_start(struct uniphier_mdmac_chan *mc)
{
	struct uniphier_mdmac_desc *md;

	md = uniphier_mdmac_next_desc(mc);
	if (md)
		uniphier_mdmac_handle(mc, md);
}

/* mc->vc.lock must be held by caller */
static int uniphier_mdmac_abort(struct uniphier_mdmac_chan *mc)
{
	struct uniphier_mdmac_device *mdev = mc->mdev;
	u32 irq_flag = UNIPHIER_MDMAC_CH_IRQ__ABORT;
	u32 val;

	/* write 1 to clear */
	writel(irq_flag, mc->reg_ch_base + UNIPHIER_MDMAC_CH_IRQ_REQ);

	writel(UNIPHIER_MDMAC_CMD_ABORT | BIT(mc->chan_id),
	       mdev->reg_base + UNIPHIER_MDMAC_CMD);

	/*
	 * Abort should be accepted soon. We poll the bit here instead of
	 * waiting for the interrupt.
	 */
	return readl_poll_timeout(mc->reg_ch_base + UNIPHIER_MDMAC_CH_IRQ_REQ,
				  val, val & irq_flag, 0, 20);
}

static irqreturn_t uniphier_mdmac_interrupt(int irq, void *dev_id)
{
	struct uniphier_mdmac_chan *mc = dev_id;
	struct uniphier_mdmac_desc *md;
	irqreturn_t ret = IRQ_HANDLED;
	u32 irq_stat;

	spin_lock(&mc->vc.lock);

	irq_stat = readl(mc->reg_ch_base + UNIPHIER_MDMAC_CH_IRQ_DET);

	/*
	 * Some channels share a single interrupt line. If the IRQ status is 0,
	 * this is probably triggered by a different channel.
	 */
	if (!irq_stat) {
		ret = IRQ_NONE;
		goto out;
	}

	/* write 1 to clear */
	writel(irq_stat, mc->reg_ch_base + UNIPHIER_MDMAC_CH_IRQ_REQ);

	/*
	 * UNIPHIER_MDMAC_CH_IRQ__DONE interrupt is asserted even when the DMA
	 * is aborted. To distinguish the normal completion and the abort,
	 * check mc->md. If it is NULL, we are aborting.
	 */
	md = mc->md;
	if (!md)
		goto out;

	md->sg_cur++;

	if (md->sg_cur >= md->sg_len) {
		vchan_cookie_complete(&md->vd);
		md = uniphier_mdmac_next_desc(mc);
		if (!md)
			goto out;
	}

	uniphier_mdmac_handle(mc, md);

out:
	spin_unlock(&mc->vc.lock);

	return ret;
}

static void uniphier_mdmac_free_chan_resources(struct dma_chan *chan)
{
	vchan_free_chan_resources(to_virt_chan(chan));
}

static struct dma_async_tx_descriptor *
uniphier_mdmac_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
			     unsigned int sg_len,
			     enum dma_transfer_direction direction,
			     unsigned long flags, void *context)
{
	struct virt_dma_chan *vc = to_virt_chan(chan);
	struct uniphier_mdmac_desc *md;

	if (!is_slave_direction(direction))
		return NULL;

	md = kzalloc(sizeof(*md), GFP_NOWAIT);
	if (!md)
		return NULL;

	md->sgl = sgl;
	md->sg_len = sg_len;
	md->dir = direction;

	return vchan_tx_prep(vc, &md->vd, flags);
}

static int uniphier_mdmac_terminate_all(struct dma_chan *chan)
{
	struct virt_dma_chan *vc = to_virt_chan(chan);
	struct uniphier_mdmac_chan *mc = to_uniphier_mdmac_chan(vc);
	unsigned long flags;
	int ret = 0;
	LIST_HEAD(head);

	spin_lock_irqsave(&vc->lock, flags);

	if (mc->md) {
		vchan_terminate_vdesc(&mc->md->vd);
		mc->md = NULL;
		ret = uniphier_mdmac_abort(mc);
	}
	vchan_get_all_descriptors(vc, &head);

	spin_unlock_irqrestore(&vc->lock, flags);

	vchan_dma_desc_free_list(vc, &head);

	return ret;
}

static void uniphier_mdmac_synchronize(struct dma_chan *chan)
{
	vchan_synchronize(to_virt_chan(chan));
}

static enum dma_status uniphier_mdmac_tx_status(struct dma_chan *chan,
						dma_cookie_t cookie,
						struct dma_tx_state *txstate)
{
	struct virt_dma_chan *vc;
	struct virt_dma_desc *vd;
	struct uniphier_mdmac_chan *mc;
	struct uniphier_mdmac_desc *md = NULL;
	enum dma_status stat;
	unsigned long flags;
	int i;

	stat = dma_cookie_status(chan, cookie, txstate);
	/* Return immediately if we do not need to compute the residue. */
	if (stat == DMA_COMPLETE || !txstate)
		return stat;

	vc = to_virt_chan(chan);

	spin_lock_irqsave(&vc->lock, flags);

	mc = to_uniphier_mdmac_chan(vc);

	if (mc->md && mc->md->vd.tx.cookie == cookie) {
		/* residue from the on-flight chunk */
		txstate->residue = readl(mc->reg_ch_base +
					 UNIPHIER_MDMAC_CH_SIZE);
		md = mc->md;
	}

	if (!md) {
		vd = vchan_find_desc(vc, cookie);
		if (vd)
			md = to_uniphier_mdmac_desc(vd);
	}

	if (md) {
		/* residue from the queued chunks */
		for (i = md->sg_cur; i < md->sg_len; i++)
			txstate->residue += sg_dma_len(&md->sgl[i]);
	}

	spin_unlock_irqrestore(&vc->lock, flags);

	return stat;
}

static void uniphier_mdmac_issue_pending(struct dma_chan *chan)
{
	struct virt_dma_chan *vc = to_virt_chan(chan);
	struct uniphier_mdmac_chan *mc = to_uniphier_mdmac_chan(vc);
	unsigned long flags;

	spin_lock_irqsave(&vc->lock, flags);

	if (vchan_issue_pending(vc) && !mc->md)
		uniphier_mdmac_start(mc);

	spin_unlock_irqrestore(&vc->lock, flags);
}

static void uniphier_mdmac_desc_free(struct virt_dma_desc *vd)
{
	kfree(to_uniphier_mdmac_desc(vd));
}

static int uniphier_mdmac_chan_init(struct platform_device *pdev,
				    struct uniphier_mdmac_device *mdev,
				    int chan_id)
{
	struct device *dev = &pdev->dev;
	struct uniphier_mdmac_chan *mc = &mdev->channels[chan_id];
	char *irq_name;
	int irq, ret;

	irq = platform_get_irq(pdev, chan_id);
	if (irq < 0)
		return irq;

	irq_name = devm_kasprintf(dev, GFP_KERNEL, "uniphier-mio-dmac-ch%d",
				  chan_id);
	if (!irq_name)
		return -ENOMEM;

	ret = devm_request_irq(dev, irq, uniphier_mdmac_interrupt,
			       IRQF_SHARED, irq_name, mc);
	if (ret)
		return ret;

	mc->mdev = mdev;
	mc->reg_ch_base = mdev->reg_base + UNIPHIER_MDMAC_CH_OFFSET +
					UNIPHIER_MDMAC_CH_STRIDE * chan_id;
	mc->chan_id = chan_id;
	mc->vc.desc_free = uniphier_mdmac_desc_free;
	vchan_init(&mc->vc, &mdev->ddev);

	return 0;
}

static int uniphier_mdmac_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uniphier_mdmac_device *mdev;
	struct dma_device *ddev;
	struct resource *res;
	int nr_chans, ret, i;

	nr_chans = platform_irq_count(pdev);
	if (nr_chans < 0)
		return nr_chans;

	ret = dma_set_mask(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	mdev = devm_kzalloc(dev, struct_size(mdev, channels, nr_chans),
			    GFP_KERNEL);
	if (!mdev)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mdev->reg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(mdev->reg_base))
		return PTR_ERR(mdev->reg_base);

	mdev->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(mdev->clk)) {
		dev_err(dev, "failed to get clock\n");
		return PTR_ERR(mdev->clk);
	}

	ret = clk_prepare_enable(mdev->clk);
	if (ret)
		return ret;

	ddev = &mdev->ddev;
	ddev->dev = dev;
	dma_cap_set(DMA_PRIVATE, ddev->cap_mask);
	ddev->src_addr_widths = UNIPHIER_MDMAC_SLAVE_BUSWIDTHS;
	ddev->dst_addr_widths = UNIPHIER_MDMAC_SLAVE_BUSWIDTHS;
	ddev->directions = BIT(DMA_MEM_TO_DEV) | BIT(DMA_DEV_TO_MEM);
	ddev->residue_granularity = DMA_RESIDUE_GRANULARITY_SEGMENT;
	ddev->device_free_chan_resources = uniphier_mdmac_free_chan_resources;
	ddev->device_prep_slave_sg = uniphier_mdmac_prep_slave_sg;
	ddev->device_terminate_all = uniphier_mdmac_terminate_all;
	ddev->device_synchronize = uniphier_mdmac_synchronize;
	ddev->device_tx_status = uniphier_mdmac_tx_status;
	ddev->device_issue_pending = uniphier_mdmac_issue_pending;
	INIT_LIST_HEAD(&ddev->channels);

	for (i = 0; i < nr_chans; i++) {
		ret = uniphier_mdmac_chan_init(pdev, mdev, i);
		if (ret)
			goto disable_clk;
	}

	ret = dma_async_device_register(ddev);
	if (ret)
		goto disable_clk;

	ret = of_dma_controller_register(dev->of_node, of_dma_xlate_by_chan_id,
					 ddev);
	if (ret)
		goto unregister_dmac;

	platform_set_drvdata(pdev, mdev);

	return 0;

unregister_dmac:
	dma_async_device_unregister(ddev);
disable_clk:
	clk_disable_unprepare(mdev->clk);

	return ret;
}

static int uniphier_mdmac_remove(struct platform_device *pdev)
{
	struct uniphier_mdmac_device *mdev = platform_get_drvdata(pdev);
	struct dma_chan *chan;
	int ret;

	/*
	 * Before reaching here, almost all descriptors have been freed by the
	 * ->device_free_chan_resources() hook. However, each channel might
	 * be still holding one descriptor that was on-flight at that moment.
	 * Terminate it to make sure this hardware is no longer running. Then,
	 * free the channel resources once again to avoid memory leak.
	 */
	list_for_each_entry(chan, &mdev->ddev.channels, device_node) {
		ret = dmaengine_terminate_sync(chan);
		if (ret)
			return ret;
		uniphier_mdmac_free_chan_resources(chan);
	}

	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&mdev->ddev);
	clk_disable_unprepare(mdev->clk);

	return 0;
}

static const struct of_device_id uniphier_mdmac_match[] = {
	{ .compatible = "socionext,uniphier-mio-dmac" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uniphier_mdmac_match);

static struct platform_driver uniphier_mdmac_driver = {
	.probe = uniphier_mdmac_probe,
	.remove = uniphier_mdmac_remove,
	.driver = {
		.name = "uniphier-mio-dmac",
		.of_match_table = uniphier_mdmac_match,
	},
};
module_platform_driver(uniphier_mdmac_driver);

MODULE_AUTHOR("Masahiro Yamada <yamada.masahiro@socionext.com>");
MODULE_DESCRIPTION("UniPhier MIO DMAC driver");
MODULE_LICENSE("GPL v2");
