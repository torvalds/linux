// SPDX-License-Identifier: GPL-2.0
/*
 * External DMA controller driver for UniPhier SoCs
 * Copyright 2019 Socionext Inc.
 * Author: Kunihiko Hayashi <hayashi.kunihiko@socionext.com>
 */

#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "dmaengine.h"
#include "virt-dma.h"

#define XDMAC_CH_WIDTH		0x100

#define XDMAC_TFA		0x08
#define XDMAC_TFA_MCNT_MASK	GENMASK(23, 16)
#define XDMAC_TFA_MASK		GENMASK(5, 0)
#define XDMAC_SADM		0x10
#define XDMAC_SADM_STW_MASK	GENMASK(25, 24)
#define XDMAC_SADM_SAM		BIT(4)
#define XDMAC_SADM_SAM_FIXED	XDMAC_SADM_SAM
#define XDMAC_SADM_SAM_INC	0
#define XDMAC_DADM		0x14
#define XDMAC_DADM_DTW_MASK	XDMAC_SADM_STW_MASK
#define XDMAC_DADM_DAM		XDMAC_SADM_SAM
#define XDMAC_DADM_DAM_FIXED	XDMAC_SADM_SAM_FIXED
#define XDMAC_DADM_DAM_INC	XDMAC_SADM_SAM_INC
#define XDMAC_EXSAD		0x18
#define XDMAC_EXDAD		0x1c
#define XDMAC_SAD		0x20
#define XDMAC_DAD		0x24
#define XDMAC_ITS		0x28
#define XDMAC_ITS_MASK		GENMASK(25, 0)
#define XDMAC_TNUM		0x2c
#define XDMAC_TNUM_MASK		GENMASK(15, 0)
#define XDMAC_TSS		0x30
#define XDMAC_TSS_REQ		BIT(0)
#define XDMAC_IEN		0x34
#define XDMAC_IEN_ERRIEN	BIT(1)
#define XDMAC_IEN_ENDIEN	BIT(0)
#define XDMAC_STAT		0x40
#define XDMAC_STAT_TENF		BIT(0)
#define XDMAC_IR		0x44
#define XDMAC_IR_ERRF		BIT(1)
#define XDMAC_IR_ENDF		BIT(0)
#define XDMAC_ID		0x48
#define XDMAC_ID_ERRIDF		BIT(1)
#define XDMAC_ID_ENDIDF		BIT(0)

#define XDMAC_MAX_CHANS		16
#define XDMAC_INTERVAL_CLKS	20
#define XDMAC_MAX_WORDS		XDMAC_TNUM_MASK

/* cut lower bit for maintain alignment of maximum transfer size */
#define XDMAC_MAX_WORD_SIZE	(XDMAC_ITS_MASK & ~GENMASK(3, 0))

#define UNIPHIER_XDMAC_BUSWIDTHS \
	(BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) | \
	 BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) | \
	 BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) | \
	 BIT(DMA_SLAVE_BUSWIDTH_8_BYTES))

struct uniphier_xdmac_desc_node {
	dma_addr_t src;
	dma_addr_t dst;
	u32 burst_size;
	u32 nr_burst;
};

struct uniphier_xdmac_desc {
	struct virt_dma_desc vd;

	unsigned int nr_node;
	unsigned int cur_node;
	enum dma_transfer_direction dir;
	struct uniphier_xdmac_desc_node nodes[];
};

struct uniphier_xdmac_chan {
	struct virt_dma_chan vc;
	struct uniphier_xdmac_device *xdev;
	struct uniphier_xdmac_desc *xd;
	void __iomem *reg_ch_base;
	struct dma_slave_config	sconfig;
	int id;
	unsigned int req_factor;
};

struct uniphier_xdmac_device {
	struct dma_device ddev;
	void __iomem *reg_base;
	int nr_chans;
	struct uniphier_xdmac_chan channels[];
};

static struct uniphier_xdmac_chan *
to_uniphier_xdmac_chan(struct virt_dma_chan *vc)
{
	return container_of(vc, struct uniphier_xdmac_chan, vc);
}

static struct uniphier_xdmac_desc *
to_uniphier_xdmac_desc(struct virt_dma_desc *vd)
{
	return container_of(vd, struct uniphier_xdmac_desc, vd);
}

/* xc->vc.lock must be held by caller */
static struct uniphier_xdmac_desc *
uniphier_xdmac_next_desc(struct uniphier_xdmac_chan *xc)
{
	struct virt_dma_desc *vd;

	vd = vchan_next_desc(&xc->vc);
	if (!vd)
		return NULL;

	list_del(&vd->node);

	return to_uniphier_xdmac_desc(vd);
}

/* xc->vc.lock must be held by caller */
static void uniphier_xdmac_chan_start(struct uniphier_xdmac_chan *xc,
				      struct uniphier_xdmac_desc *xd)
{
	u32 src_mode, src_width;
	u32 dst_mode, dst_width;
	dma_addr_t src_addr, dst_addr;
	u32 val, its, tnum;
	enum dma_slave_buswidth buswidth;

	src_addr = xd->nodes[xd->cur_node].src;
	dst_addr = xd->nodes[xd->cur_node].dst;
	its      = xd->nodes[xd->cur_node].burst_size;
	tnum     = xd->nodes[xd->cur_node].nr_burst;

	/*
	 * The width of MEM side must be 4 or 8 bytes, that does not
	 * affect that of DEV side and transfer size.
	 */
	if (xd->dir == DMA_DEV_TO_MEM) {
		src_mode = XDMAC_SADM_SAM_FIXED;
		buswidth = xc->sconfig.src_addr_width;
	} else {
		src_mode = XDMAC_SADM_SAM_INC;
		buswidth = DMA_SLAVE_BUSWIDTH_8_BYTES;
	}
	src_width = FIELD_PREP(XDMAC_SADM_STW_MASK, __ffs(buswidth));

	if (xd->dir == DMA_MEM_TO_DEV) {
		dst_mode = XDMAC_DADM_DAM_FIXED;
		buswidth = xc->sconfig.dst_addr_width;
	} else {
		dst_mode = XDMAC_DADM_DAM_INC;
		buswidth = DMA_SLAVE_BUSWIDTH_8_BYTES;
	}
	dst_width = FIELD_PREP(XDMAC_DADM_DTW_MASK, __ffs(buswidth));

	/* setup transfer factor */
	val = FIELD_PREP(XDMAC_TFA_MCNT_MASK, XDMAC_INTERVAL_CLKS);
	val |= FIELD_PREP(XDMAC_TFA_MASK, xc->req_factor);
	writel(val, xc->reg_ch_base + XDMAC_TFA);

	/* setup the channel */
	writel(lower_32_bits(src_addr), xc->reg_ch_base + XDMAC_SAD);
	writel(upper_32_bits(src_addr), xc->reg_ch_base + XDMAC_EXSAD);

	writel(lower_32_bits(dst_addr), xc->reg_ch_base + XDMAC_DAD);
	writel(upper_32_bits(dst_addr), xc->reg_ch_base + XDMAC_EXDAD);

	src_mode |= src_width;
	dst_mode |= dst_width;
	writel(src_mode, xc->reg_ch_base + XDMAC_SADM);
	writel(dst_mode, xc->reg_ch_base + XDMAC_DADM);

	writel(its, xc->reg_ch_base + XDMAC_ITS);
	writel(tnum, xc->reg_ch_base + XDMAC_TNUM);

	/* enable interrupt */
	writel(XDMAC_IEN_ENDIEN | XDMAC_IEN_ERRIEN,
	       xc->reg_ch_base + XDMAC_IEN);

	/* start XDMAC */
	val = readl(xc->reg_ch_base + XDMAC_TSS);
	val |= XDMAC_TSS_REQ;
	writel(val, xc->reg_ch_base + XDMAC_TSS);
}

/* xc->vc.lock must be held by caller */
static int uniphier_xdmac_chan_stop(struct uniphier_xdmac_chan *xc)
{
	u32 val;

	/* disable interrupt */
	val = readl(xc->reg_ch_base + XDMAC_IEN);
	val &= ~(XDMAC_IEN_ENDIEN | XDMAC_IEN_ERRIEN);
	writel(val, xc->reg_ch_base + XDMAC_IEN);

	/* stop XDMAC */
	val = readl(xc->reg_ch_base + XDMAC_TSS);
	val &= ~XDMAC_TSS_REQ;
	writel(0, xc->reg_ch_base + XDMAC_TSS);

	/* wait until transfer is stopped */
	return readl_poll_timeout_atomic(xc->reg_ch_base + XDMAC_STAT, val,
					 !(val & XDMAC_STAT_TENF), 100, 1000);
}

/* xc->vc.lock must be held by caller */
static void uniphier_xdmac_start(struct uniphier_xdmac_chan *xc)
{
	struct uniphier_xdmac_desc *xd;

	xd = uniphier_xdmac_next_desc(xc);
	if (xd)
		uniphier_xdmac_chan_start(xc, xd);

	/* set desc to chan regardless of xd is null */
	xc->xd = xd;
}

static void uniphier_xdmac_chan_irq(struct uniphier_xdmac_chan *xc)
{
	u32 stat;
	int ret;

	spin_lock(&xc->vc.lock);

	stat = readl(xc->reg_ch_base + XDMAC_ID);

	if (stat & XDMAC_ID_ERRIDF) {
		ret = uniphier_xdmac_chan_stop(xc);
		if (ret)
			dev_err(xc->xdev->ddev.dev,
				"DMA transfer error with aborting issue\n");
		else
			dev_err(xc->xdev->ddev.dev,
				"DMA transfer error\n");

	} else if ((stat & XDMAC_ID_ENDIDF) && xc->xd) {
		xc->xd->cur_node++;
		if (xc->xd->cur_node >= xc->xd->nr_node) {
			vchan_cookie_complete(&xc->xd->vd);
			uniphier_xdmac_start(xc);
		} else {
			uniphier_xdmac_chan_start(xc, xc->xd);
		}
	}

	/* write bits to clear */
	writel(stat, xc->reg_ch_base + XDMAC_IR);

	spin_unlock(&xc->vc.lock);
}

static irqreturn_t uniphier_xdmac_irq_handler(int irq, void *dev_id)
{
	struct uniphier_xdmac_device *xdev = dev_id;
	int i;

	for (i = 0; i < xdev->nr_chans; i++)
		uniphier_xdmac_chan_irq(&xdev->channels[i]);

	return IRQ_HANDLED;
}

static void uniphier_xdmac_free_chan_resources(struct dma_chan *chan)
{
	vchan_free_chan_resources(to_virt_chan(chan));
}

static struct dma_async_tx_descriptor *
uniphier_xdmac_prep_dma_memcpy(struct dma_chan *chan, dma_addr_t dst,
			       dma_addr_t src, size_t len, unsigned long flags)
{
	struct virt_dma_chan *vc = to_virt_chan(chan);
	struct uniphier_xdmac_desc *xd;
	unsigned int nr;
	size_t burst_size, tlen;
	int i;

	if (len > XDMAC_MAX_WORD_SIZE * XDMAC_MAX_WORDS)
		return NULL;

	nr = 1 + len / XDMAC_MAX_WORD_SIZE;

	xd = kzalloc(struct_size(xd, nodes, nr), GFP_NOWAIT);
	if (!xd)
		return NULL;

	for (i = 0; i < nr; i++) {
		burst_size = min_t(size_t, len, XDMAC_MAX_WORD_SIZE);
		xd->nodes[i].src = src;
		xd->nodes[i].dst = dst;
		xd->nodes[i].burst_size = burst_size;
		xd->nodes[i].nr_burst = len / burst_size;
		tlen = rounddown(len, burst_size);
		src += tlen;
		dst += tlen;
		len -= tlen;
	}

	xd->dir = DMA_MEM_TO_MEM;
	xd->nr_node = nr;
	xd->cur_node = 0;

	return vchan_tx_prep(vc, &xd->vd, flags);
}

static struct dma_async_tx_descriptor *
uniphier_xdmac_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
			     unsigned int sg_len,
			     enum dma_transfer_direction direction,
			     unsigned long flags, void *context)
{
	struct virt_dma_chan *vc = to_virt_chan(chan);
	struct uniphier_xdmac_chan *xc = to_uniphier_xdmac_chan(vc);
	struct uniphier_xdmac_desc *xd;
	struct scatterlist *sg;
	enum dma_slave_buswidth buswidth;
	u32 maxburst;
	int i;

	if (!is_slave_direction(direction))
		return NULL;

	if (direction == DMA_DEV_TO_MEM) {
		buswidth = xc->sconfig.src_addr_width;
		maxburst = xc->sconfig.src_maxburst;
	} else {
		buswidth = xc->sconfig.dst_addr_width;
		maxburst = xc->sconfig.dst_maxburst;
	}

	if (!maxburst)
		maxburst = 1;
	if (maxburst > xc->xdev->ddev.max_burst) {
		dev_err(xc->xdev->ddev.dev,
			"Exceed maximum number of burst words\n");
		return NULL;
	}

	xd = kzalloc(struct_size(xd, nodes, sg_len), GFP_NOWAIT);
	if (!xd)
		return NULL;

	for_each_sg(sgl, sg, sg_len, i) {
		xd->nodes[i].src = (direction == DMA_DEV_TO_MEM)
			? xc->sconfig.src_addr : sg_dma_address(sg);
		xd->nodes[i].dst = (direction == DMA_MEM_TO_DEV)
			? xc->sconfig.dst_addr : sg_dma_address(sg);
		xd->nodes[i].burst_size = maxburst * buswidth;
		xd->nodes[i].nr_burst =
			sg_dma_len(sg) / xd->nodes[i].burst_size;

		/*
		 * Currently transfer that size doesn't align the unit size
		 * (the number of burst words * bus-width) is not allowed,
		 * because the driver does not support the way to transfer
		 * residue size. As a matter of fact, in order to transfer
		 * arbitrary size, 'src_maxburst' or 'dst_maxburst' of
		 * dma_slave_config must be 1.
		 */
		if (sg_dma_len(sg) % xd->nodes[i].burst_size) {
			dev_err(xc->xdev->ddev.dev,
				"Unaligned transfer size: %d", sg_dma_len(sg));
			kfree(xd);
			return NULL;
		}

		if (xd->nodes[i].nr_burst > XDMAC_MAX_WORDS) {
			dev_err(xc->xdev->ddev.dev,
				"Exceed maximum transfer size");
			kfree(xd);
			return NULL;
		}
	}

	xd->dir = direction;
	xd->nr_node = sg_len;
	xd->cur_node = 0;

	return vchan_tx_prep(vc, &xd->vd, flags);
}

static int uniphier_xdmac_slave_config(struct dma_chan *chan,
				       struct dma_slave_config *config)
{
	struct virt_dma_chan *vc = to_virt_chan(chan);
	struct uniphier_xdmac_chan *xc = to_uniphier_xdmac_chan(vc);

	memcpy(&xc->sconfig, config, sizeof(*config));

	return 0;
}

static int uniphier_xdmac_terminate_all(struct dma_chan *chan)
{
	struct virt_dma_chan *vc = to_virt_chan(chan);
	struct uniphier_xdmac_chan *xc = to_uniphier_xdmac_chan(vc);
	unsigned long flags;
	int ret = 0;
	LIST_HEAD(head);

	spin_lock_irqsave(&vc->lock, flags);

	if (xc->xd) {
		vchan_terminate_vdesc(&xc->xd->vd);
		xc->xd = NULL;
		ret = uniphier_xdmac_chan_stop(xc);
	}

	vchan_get_all_descriptors(vc, &head);

	spin_unlock_irqrestore(&vc->lock, flags);

	vchan_dma_desc_free_list(vc, &head);

	return ret;
}

static void uniphier_xdmac_synchronize(struct dma_chan *chan)
{
	vchan_synchronize(to_virt_chan(chan));
}

static void uniphier_xdmac_issue_pending(struct dma_chan *chan)
{
	struct virt_dma_chan *vc = to_virt_chan(chan);
	struct uniphier_xdmac_chan *xc = to_uniphier_xdmac_chan(vc);
	unsigned long flags;

	spin_lock_irqsave(&vc->lock, flags);

	if (vchan_issue_pending(vc) && !xc->xd)
		uniphier_xdmac_start(xc);

	spin_unlock_irqrestore(&vc->lock, flags);
}

static void uniphier_xdmac_desc_free(struct virt_dma_desc *vd)
{
	kfree(to_uniphier_xdmac_desc(vd));
}

static void uniphier_xdmac_chan_init(struct uniphier_xdmac_device *xdev,
				     int ch)
{
	struct uniphier_xdmac_chan *xc = &xdev->channels[ch];

	xc->xdev = xdev;
	xc->reg_ch_base = xdev->reg_base + XDMAC_CH_WIDTH * ch;
	xc->vc.desc_free = uniphier_xdmac_desc_free;

	vchan_init(&xc->vc, &xdev->ddev);
}

static struct dma_chan *of_dma_uniphier_xlate(struct of_phandle_args *dma_spec,
					      struct of_dma *ofdma)
{
	struct uniphier_xdmac_device *xdev = ofdma->of_dma_data;
	int chan_id = dma_spec->args[0];

	if (chan_id >= xdev->nr_chans)
		return NULL;

	xdev->channels[chan_id].id = chan_id;
	xdev->channels[chan_id].req_factor = dma_spec->args[1];

	return dma_get_slave_channel(&xdev->channels[chan_id].vc.chan);
}

static int uniphier_xdmac_probe(struct platform_device *pdev)
{
	struct uniphier_xdmac_device *xdev;
	struct device *dev = &pdev->dev;
	struct dma_device *ddev;
	int irq;
	int nr_chans;
	int i, ret;

	if (of_property_read_u32(dev->of_node, "dma-channels", &nr_chans))
		return -EINVAL;
	if (nr_chans > XDMAC_MAX_CHANS)
		nr_chans = XDMAC_MAX_CHANS;

	xdev = devm_kzalloc(dev, struct_size(xdev, channels, nr_chans),
			    GFP_KERNEL);
	if (!xdev)
		return -ENOMEM;

	xdev->nr_chans = nr_chans;
	xdev->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(xdev->reg_base))
		return PTR_ERR(xdev->reg_base);

	ddev = &xdev->ddev;
	ddev->dev = dev;
	dma_cap_zero(ddev->cap_mask);
	dma_cap_set(DMA_MEMCPY, ddev->cap_mask);
	dma_cap_set(DMA_SLAVE, ddev->cap_mask);
	ddev->src_addr_widths = UNIPHIER_XDMAC_BUSWIDTHS;
	ddev->dst_addr_widths = UNIPHIER_XDMAC_BUSWIDTHS;
	ddev->directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV) |
			   BIT(DMA_MEM_TO_MEM);
	ddev->residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;
	ddev->max_burst = XDMAC_MAX_WORDS;
	ddev->device_free_chan_resources = uniphier_xdmac_free_chan_resources;
	ddev->device_prep_dma_memcpy = uniphier_xdmac_prep_dma_memcpy;
	ddev->device_prep_slave_sg = uniphier_xdmac_prep_slave_sg;
	ddev->device_config = uniphier_xdmac_slave_config;
	ddev->device_terminate_all = uniphier_xdmac_terminate_all;
	ddev->device_synchronize = uniphier_xdmac_synchronize;
	ddev->device_tx_status = dma_cookie_status;
	ddev->device_issue_pending = uniphier_xdmac_issue_pending;
	INIT_LIST_HEAD(&ddev->channels);

	for (i = 0; i < nr_chans; i++)
		uniphier_xdmac_chan_init(xdev, i);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, uniphier_xdmac_irq_handler,
			       IRQF_SHARED, "xdmac", xdev);
	if (ret) {
		dev_err(dev, "Failed to request IRQ\n");
		return ret;
	}

	ret = dma_async_device_register(ddev);
	if (ret) {
		dev_err(dev, "Failed to register XDMA device\n");
		return ret;
	}

	ret = of_dma_controller_register(dev->of_node,
					 of_dma_uniphier_xlate, xdev);
	if (ret) {
		dev_err(dev, "Failed to register XDMA controller\n");
		goto out_unregister_dmac;
	}

	platform_set_drvdata(pdev, xdev);

	dev_info(&pdev->dev, "UniPhier XDMAC driver (%d channels)\n",
		 nr_chans);

	return 0;

out_unregister_dmac:
	dma_async_device_unregister(ddev);

	return ret;
}

static int uniphier_xdmac_remove(struct platform_device *pdev)
{
	struct uniphier_xdmac_device *xdev = platform_get_drvdata(pdev);
	struct dma_device *ddev = &xdev->ddev;
	struct dma_chan *chan;
	int ret;

	/*
	 * Before reaching here, almost all descriptors have been freed by the
	 * ->device_free_chan_resources() hook. However, each channel might
	 * be still holding one descriptor that was on-flight at that moment.
	 * Terminate it to make sure this hardware is no longer running. Then,
	 * free the channel resources once again to avoid memory leak.
	 */
	list_for_each_entry(chan, &ddev->channels, device_node) {
		ret = dmaengine_terminate_sync(chan);
		if (ret)
			return ret;
		uniphier_xdmac_free_chan_resources(chan);
	}

	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(ddev);

	return 0;
}

static const struct of_device_id uniphier_xdmac_match[] = {
	{ .compatible = "socionext,uniphier-xdmac" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uniphier_xdmac_match);

static struct platform_driver uniphier_xdmac_driver = {
	.probe = uniphier_xdmac_probe,
	.remove = uniphier_xdmac_remove,
	.driver = {
		.name = "uniphier-xdmac",
		.of_match_table = uniphier_xdmac_match,
	},
};
module_platform_driver(uniphier_xdmac_driver);

MODULE_AUTHOR("Kunihiko Hayashi <hayashi.kunihiko@socionext.com>");
MODULE_DESCRIPTION("UniPhier external DMA controller driver");
MODULE_LICENSE("GPL v2");
