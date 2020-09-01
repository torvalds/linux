// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2019 Linaro Ltd.
// Copyright (C) 2019 Socionext Inc.

#include <linux/bits.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/bitfield.h>

#include "virt-dma.h"

/* global register */
#define M10V_XDACS 0x00

/* channel local register */
#define M10V_XDTBC 0x10
#define M10V_XDSSA 0x14
#define M10V_XDDSA 0x18
#define M10V_XDSAC 0x1C
#define M10V_XDDAC 0x20
#define M10V_XDDCC 0x24
#define M10V_XDDES 0x28
#define M10V_XDDPC 0x2C
#define M10V_XDDSD 0x30

#define M10V_XDACS_XE BIT(28)

#define M10V_DEFBS	0x3
#define M10V_DEFBL	0xf

#define M10V_XDSAC_SBS	GENMASK(17, 16)
#define M10V_XDSAC_SBL	GENMASK(11, 8)

#define M10V_XDDAC_DBS	GENMASK(17, 16)
#define M10V_XDDAC_DBL	GENMASK(11, 8)

#define M10V_XDDES_CE	BIT(28)
#define M10V_XDDES_SE	BIT(24)
#define M10V_XDDES_SA	BIT(15)
#define M10V_XDDES_TF	GENMASK(23, 20)
#define M10V_XDDES_EI	BIT(1)
#define M10V_XDDES_TI	BIT(0)

#define M10V_XDDSD_IS_MASK	GENMASK(3, 0)
#define M10V_XDDSD_IS_NORMAL	0x8

#define MLB_XDMAC_BUSWIDTHS	(BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) | \
				 BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) | \
				 BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) | \
				 BIT(DMA_SLAVE_BUSWIDTH_8_BYTES))

struct milbeaut_xdmac_desc {
	struct virt_dma_desc vd;
	size_t len;
	dma_addr_t src;
	dma_addr_t dst;
};

struct milbeaut_xdmac_chan {
	struct virt_dma_chan vc;
	struct milbeaut_xdmac_desc *md;
	void __iomem *reg_ch_base;
};

struct milbeaut_xdmac_device {
	struct dma_device ddev;
	void __iomem *reg_base;
	struct milbeaut_xdmac_chan channels[];
};

static struct milbeaut_xdmac_chan *
to_milbeaut_xdmac_chan(struct virt_dma_chan *vc)
{
	return container_of(vc, struct milbeaut_xdmac_chan, vc);
}

static struct milbeaut_xdmac_desc *
to_milbeaut_xdmac_desc(struct virt_dma_desc *vd)
{
	return container_of(vd, struct milbeaut_xdmac_desc, vd);
}

/* mc->vc.lock must be held by caller */
static struct milbeaut_xdmac_desc *
milbeaut_xdmac_next_desc(struct milbeaut_xdmac_chan *mc)
{
	struct virt_dma_desc *vd;

	vd = vchan_next_desc(&mc->vc);
	if (!vd) {
		mc->md = NULL;
		return NULL;
	}

	list_del(&vd->node);

	mc->md = to_milbeaut_xdmac_desc(vd);

	return mc->md;
}

/* mc->vc.lock must be held by caller */
static void milbeaut_chan_start(struct milbeaut_xdmac_chan *mc,
				struct milbeaut_xdmac_desc *md)
{
	u32 val;

	/* Setup the channel */
	val = md->len - 1;
	writel_relaxed(val, mc->reg_ch_base + M10V_XDTBC);

	val = md->src;
	writel_relaxed(val, mc->reg_ch_base + M10V_XDSSA);

	val = md->dst;
	writel_relaxed(val, mc->reg_ch_base + M10V_XDDSA);

	val = readl_relaxed(mc->reg_ch_base + M10V_XDSAC);
	val &= ~(M10V_XDSAC_SBS | M10V_XDSAC_SBL);
	val |= FIELD_PREP(M10V_XDSAC_SBS, M10V_DEFBS) |
		FIELD_PREP(M10V_XDSAC_SBL, M10V_DEFBL);
	writel_relaxed(val, mc->reg_ch_base + M10V_XDSAC);

	val = readl_relaxed(mc->reg_ch_base + M10V_XDDAC);
	val &= ~(M10V_XDDAC_DBS | M10V_XDDAC_DBL);
	val |= FIELD_PREP(M10V_XDDAC_DBS, M10V_DEFBS) |
		FIELD_PREP(M10V_XDDAC_DBL, M10V_DEFBL);
	writel_relaxed(val, mc->reg_ch_base + M10V_XDDAC);

	/* Start the channel */
	val = readl_relaxed(mc->reg_ch_base + M10V_XDDES);
	val &= ~(M10V_XDDES_CE | M10V_XDDES_SE | M10V_XDDES_TF |
		 M10V_XDDES_EI | M10V_XDDES_TI);
	val |= FIELD_PREP(M10V_XDDES_CE, 1) | FIELD_PREP(M10V_XDDES_SE, 1) |
		FIELD_PREP(M10V_XDDES_TF, 1) | FIELD_PREP(M10V_XDDES_EI, 1) |
		FIELD_PREP(M10V_XDDES_TI, 1);
	writel_relaxed(val, mc->reg_ch_base + M10V_XDDES);
}

/* mc->vc.lock must be held by caller */
static void milbeaut_xdmac_start(struct milbeaut_xdmac_chan *mc)
{
	struct milbeaut_xdmac_desc *md;

	md = milbeaut_xdmac_next_desc(mc);
	if (md)
		milbeaut_chan_start(mc, md);
}

static irqreturn_t milbeaut_xdmac_interrupt(int irq, void *dev_id)
{
	struct milbeaut_xdmac_chan *mc = dev_id;
	struct milbeaut_xdmac_desc *md;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&mc->vc.lock, flags);

	/* Ack and Stop */
	val = FIELD_PREP(M10V_XDDSD_IS_MASK, 0x0);
	writel_relaxed(val, mc->reg_ch_base + M10V_XDDSD);

	md = mc->md;
	if (!md)
		goto out;

	vchan_cookie_complete(&md->vd);

	milbeaut_xdmac_start(mc);
out:
	spin_unlock_irqrestore(&mc->vc.lock, flags);
	return IRQ_HANDLED;
}

static void milbeaut_xdmac_free_chan_resources(struct dma_chan *chan)
{
	vchan_free_chan_resources(to_virt_chan(chan));
}

static struct dma_async_tx_descriptor *
milbeaut_xdmac_prep_memcpy(struct dma_chan *chan, dma_addr_t dst,
			   dma_addr_t src, size_t len, unsigned long flags)
{
	struct virt_dma_chan *vc = to_virt_chan(chan);
	struct milbeaut_xdmac_desc *md;

	md = kzalloc(sizeof(*md), GFP_NOWAIT);
	if (!md)
		return NULL;

	md->len = len;
	md->src = src;
	md->dst = dst;

	return vchan_tx_prep(vc, &md->vd, flags);
}

static int milbeaut_xdmac_terminate_all(struct dma_chan *chan)
{
	struct virt_dma_chan *vc = to_virt_chan(chan);
	struct milbeaut_xdmac_chan *mc = to_milbeaut_xdmac_chan(vc);
	unsigned long flags;
	u32 val;

	LIST_HEAD(head);

	spin_lock_irqsave(&vc->lock, flags);

	/* Halt the channel */
	val = readl(mc->reg_ch_base + M10V_XDDES);
	val &= ~M10V_XDDES_CE;
	val |= FIELD_PREP(M10V_XDDES_CE, 0);
	writel(val, mc->reg_ch_base + M10V_XDDES);

	if (mc->md) {
		vchan_terminate_vdesc(&mc->md->vd);
		mc->md = NULL;
	}

	vchan_get_all_descriptors(vc, &head);

	spin_unlock_irqrestore(&vc->lock, flags);

	vchan_dma_desc_free_list(vc, &head);

	return 0;
}

static void milbeaut_xdmac_synchronize(struct dma_chan *chan)
{
	vchan_synchronize(to_virt_chan(chan));
}

static void milbeaut_xdmac_issue_pending(struct dma_chan *chan)
{
	struct virt_dma_chan *vc = to_virt_chan(chan);
	struct milbeaut_xdmac_chan *mc = to_milbeaut_xdmac_chan(vc);
	unsigned long flags;

	spin_lock_irqsave(&vc->lock, flags);

	if (vchan_issue_pending(vc) && !mc->md)
		milbeaut_xdmac_start(mc);

	spin_unlock_irqrestore(&vc->lock, flags);
}

static void milbeaut_xdmac_desc_free(struct virt_dma_desc *vd)
{
	kfree(to_milbeaut_xdmac_desc(vd));
}

static int milbeaut_xdmac_chan_init(struct platform_device *pdev,
				    struct milbeaut_xdmac_device *mdev,
				    int chan_id)
{
	struct device *dev = &pdev->dev;
	struct milbeaut_xdmac_chan *mc = &mdev->channels[chan_id];
	char *irq_name;
	int irq, ret;

	irq = platform_get_irq(pdev, chan_id);
	if (irq < 0)
		return irq;

	irq_name = devm_kasprintf(dev, GFP_KERNEL, "milbeaut-xdmac-%d",
				  chan_id);
	if (!irq_name)
		return -ENOMEM;

	ret = devm_request_irq(dev, irq, milbeaut_xdmac_interrupt,
			       IRQF_SHARED, irq_name, mc);
	if (ret)
		return ret;

	mc->reg_ch_base = mdev->reg_base + chan_id * 0x30;

	mc->vc.desc_free = milbeaut_xdmac_desc_free;
	vchan_init(&mc->vc, &mdev->ddev);

	return 0;
}

static void enable_xdmac(struct milbeaut_xdmac_device *mdev)
{
	unsigned int val;

	val = readl(mdev->reg_base + M10V_XDACS);
	val |= M10V_XDACS_XE;
	writel(val, mdev->reg_base + M10V_XDACS);
}

static void disable_xdmac(struct milbeaut_xdmac_device *mdev)
{
	unsigned int val;

	val = readl(mdev->reg_base + M10V_XDACS);
	val &= ~M10V_XDACS_XE;
	writel(val, mdev->reg_base + M10V_XDACS);
}

static int milbeaut_xdmac_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct milbeaut_xdmac_device *mdev;
	struct dma_device *ddev;
	int nr_chans, ret, i;

	nr_chans = platform_irq_count(pdev);
	if (nr_chans < 0)
		return nr_chans;

	mdev = devm_kzalloc(dev, struct_size(mdev, channels, nr_chans),
			    GFP_KERNEL);
	if (!mdev)
		return -ENOMEM;

	mdev->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mdev->reg_base))
		return PTR_ERR(mdev->reg_base);

	ddev = &mdev->ddev;
	ddev->dev = dev;
	dma_cap_set(DMA_MEMCPY, ddev->cap_mask);
	ddev->src_addr_widths = MLB_XDMAC_BUSWIDTHS;
	ddev->dst_addr_widths = MLB_XDMAC_BUSWIDTHS;
	ddev->device_free_chan_resources = milbeaut_xdmac_free_chan_resources;
	ddev->device_prep_dma_memcpy = milbeaut_xdmac_prep_memcpy;
	ddev->device_terminate_all = milbeaut_xdmac_terminate_all;
	ddev->device_synchronize = milbeaut_xdmac_synchronize;
	ddev->device_tx_status = dma_cookie_status;
	ddev->device_issue_pending = milbeaut_xdmac_issue_pending;
	INIT_LIST_HEAD(&ddev->channels);

	for (i = 0; i < nr_chans; i++) {
		ret = milbeaut_xdmac_chan_init(pdev, mdev, i);
		if (ret)
			return ret;
	}

	enable_xdmac(mdev);

	ret = dma_async_device_register(ddev);
	if (ret)
		return ret;

	ret = of_dma_controller_register(dev->of_node,
					 of_dma_simple_xlate, mdev);
	if (ret)
		goto unregister_dmac;

	platform_set_drvdata(pdev, mdev);

	return 0;

unregister_dmac:
	dma_async_device_unregister(ddev);
	return ret;
}

static int milbeaut_xdmac_remove(struct platform_device *pdev)
{
	struct milbeaut_xdmac_device *mdev = platform_get_drvdata(pdev);
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
		milbeaut_xdmac_free_chan_resources(chan);
	}

	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&mdev->ddev);

	disable_xdmac(mdev);

	return 0;
}

static const struct of_device_id milbeaut_xdmac_match[] = {
	{ .compatible = "socionext,milbeaut-m10v-xdmac" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, milbeaut_xdmac_match);

static struct platform_driver milbeaut_xdmac_driver = {
	.probe = milbeaut_xdmac_probe,
	.remove = milbeaut_xdmac_remove,
	.driver = {
		.name = "milbeaut-m10v-xdmac",
		.of_match_table = milbeaut_xdmac_match,
	},
};
module_platform_driver(milbeaut_xdmac_driver);

MODULE_DESCRIPTION("Milbeaut XDMAC DmaEngine driver");
MODULE_LICENSE("GPL v2");
