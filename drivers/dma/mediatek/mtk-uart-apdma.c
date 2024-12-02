// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek UART APDMA driver.
 *
 * Copyright (c) 2019 MediaTek Inc.
 * Author: Long Cheng <long.cheng@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "../virt-dma.h"

/* The default number of virtual channel */
#define MTK_UART_APDMA_NR_VCHANS	8

#define VFF_EN_B		BIT(0)
#define VFF_STOP_B		BIT(0)
#define VFF_FLUSH_B		BIT(0)
#define VFF_4G_EN_B		BIT(0)
/* rx valid size >=  vff thre */
#define VFF_RX_INT_EN_B		(BIT(0) | BIT(1))
/* tx left size >= vff thre */
#define VFF_TX_INT_EN_B		BIT(0)
#define VFF_WARM_RST_B		BIT(0)
#define VFF_RX_INT_CLR_B	(BIT(0) | BIT(1))
#define VFF_TX_INT_CLR_B	0
#define VFF_STOP_CLR_B		0
#define VFF_EN_CLR_B		0
#define VFF_INT_EN_CLR_B	0
#define VFF_4G_SUPPORT_CLR_B	0

/*
 * interrupt trigger level for tx
 * if threshold is n, no polling is required to start tx.
 * otherwise need polling VFF_FLUSH.
 */
#define VFF_TX_THRE(n)		(n)
/* interrupt trigger level for rx */
#define VFF_RX_THRE(n)		((n) * 3 / 4)

#define VFF_RING_SIZE	0xffff
/* invert this bit when wrap ring head again */
#define VFF_RING_WRAP	0x10000

#define VFF_INT_FLAG		0x00
#define VFF_INT_EN		0x04
#define VFF_EN			0x08
#define VFF_RST			0x0c
#define VFF_STOP		0x10
#define VFF_FLUSH		0x14
#define VFF_ADDR		0x1c
#define VFF_LEN			0x24
#define VFF_THRE		0x28
#define VFF_WPT			0x2c
#define VFF_RPT			0x30
/* TX: the buffer size HW can read. RX: the buffer size SW can read. */
#define VFF_VALID_SIZE		0x3c
/* TX: the buffer size SW can write. RX: the buffer size HW can write. */
#define VFF_LEFT_SIZE		0x40
#define VFF_DEBUG_STATUS	0x50
#define VFF_4G_SUPPORT		0x54

struct mtk_uart_apdmadev {
	struct dma_device ddev;
	struct clk *clk;
	bool support_33bits;
	unsigned int dma_requests;
};

struct mtk_uart_apdma_desc {
	struct virt_dma_desc vd;

	dma_addr_t addr;
	unsigned int avail_len;
};

struct mtk_chan {
	struct virt_dma_chan vc;
	struct dma_slave_config	cfg;
	struct mtk_uart_apdma_desc *desc;
	enum dma_transfer_direction dir;

	void __iomem *base;
	unsigned int irq;

	unsigned int rx_status;
};

static inline struct mtk_uart_apdmadev *
to_mtk_uart_apdma_dev(struct dma_device *d)
{
	return container_of(d, struct mtk_uart_apdmadev, ddev);
}

static inline struct mtk_chan *to_mtk_uart_apdma_chan(struct dma_chan *c)
{
	return container_of(c, struct mtk_chan, vc.chan);
}

static inline struct mtk_uart_apdma_desc *to_mtk_uart_apdma_desc
	(struct dma_async_tx_descriptor *t)
{
	return container_of(t, struct mtk_uart_apdma_desc, vd.tx);
}

static void mtk_uart_apdma_write(struct mtk_chan *c,
			       unsigned int reg, unsigned int val)
{
	writel(val, c->base + reg);
}

static unsigned int mtk_uart_apdma_read(struct mtk_chan *c, unsigned int reg)
{
	return readl(c->base + reg);
}

static void mtk_uart_apdma_desc_free(struct virt_dma_desc *vd)
{
	kfree(container_of(vd, struct mtk_uart_apdma_desc, vd));
}

static void mtk_uart_apdma_start_tx(struct mtk_chan *c)
{
	struct mtk_uart_apdmadev *mtkd =
				to_mtk_uart_apdma_dev(c->vc.chan.device);
	struct mtk_uart_apdma_desc *d = c->desc;
	unsigned int wpt, vff_sz;

	vff_sz = c->cfg.dst_port_window_size;
	if (!mtk_uart_apdma_read(c, VFF_LEN)) {
		mtk_uart_apdma_write(c, VFF_ADDR, d->addr);
		mtk_uart_apdma_write(c, VFF_LEN, vff_sz);
		mtk_uart_apdma_write(c, VFF_THRE, VFF_TX_THRE(vff_sz));
		mtk_uart_apdma_write(c, VFF_WPT, 0);
		mtk_uart_apdma_write(c, VFF_INT_FLAG, VFF_TX_INT_CLR_B);

		if (mtkd->support_33bits)
			mtk_uart_apdma_write(c, VFF_4G_SUPPORT, VFF_4G_EN_B);
	}

	mtk_uart_apdma_write(c, VFF_EN, VFF_EN_B);
	if (mtk_uart_apdma_read(c, VFF_EN) != VFF_EN_B)
		dev_err(c->vc.chan.device->dev, "Enable TX fail\n");

	if (!mtk_uart_apdma_read(c, VFF_LEFT_SIZE)) {
		mtk_uart_apdma_write(c, VFF_INT_EN, VFF_TX_INT_EN_B);
		return;
	}

	wpt = mtk_uart_apdma_read(c, VFF_WPT);

	wpt += c->desc->avail_len;
	if ((wpt & VFF_RING_SIZE) == vff_sz)
		wpt = (wpt & VFF_RING_WRAP) ^ VFF_RING_WRAP;

	/* Let DMA start moving data */
	mtk_uart_apdma_write(c, VFF_WPT, wpt);

	/* HW auto set to 0 when left size >= threshold */
	mtk_uart_apdma_write(c, VFF_INT_EN, VFF_TX_INT_EN_B);
	if (!mtk_uart_apdma_read(c, VFF_FLUSH))
		mtk_uart_apdma_write(c, VFF_FLUSH, VFF_FLUSH_B);
}

static void mtk_uart_apdma_start_rx(struct mtk_chan *c)
{
	struct mtk_uart_apdmadev *mtkd =
				to_mtk_uart_apdma_dev(c->vc.chan.device);
	struct mtk_uart_apdma_desc *d = c->desc;
	unsigned int vff_sz;

	vff_sz = c->cfg.src_port_window_size;
	if (!mtk_uart_apdma_read(c, VFF_LEN)) {
		mtk_uart_apdma_write(c, VFF_ADDR, d->addr);
		mtk_uart_apdma_write(c, VFF_LEN, vff_sz);
		mtk_uart_apdma_write(c, VFF_THRE, VFF_RX_THRE(vff_sz));
		mtk_uart_apdma_write(c, VFF_RPT, 0);
		mtk_uart_apdma_write(c, VFF_INT_FLAG, VFF_RX_INT_CLR_B);

		if (mtkd->support_33bits)
			mtk_uart_apdma_write(c, VFF_4G_SUPPORT, VFF_4G_EN_B);
	}

	mtk_uart_apdma_write(c, VFF_INT_EN, VFF_RX_INT_EN_B);
	mtk_uart_apdma_write(c, VFF_EN, VFF_EN_B);
	if (mtk_uart_apdma_read(c, VFF_EN) != VFF_EN_B)
		dev_err(c->vc.chan.device->dev, "Enable RX fail\n");
}

static void mtk_uart_apdma_tx_handler(struct mtk_chan *c)
{
	mtk_uart_apdma_write(c, VFF_INT_FLAG, VFF_TX_INT_CLR_B);
	mtk_uart_apdma_write(c, VFF_INT_EN, VFF_INT_EN_CLR_B);
	mtk_uart_apdma_write(c, VFF_EN, VFF_EN_CLR_B);
}

static void mtk_uart_apdma_rx_handler(struct mtk_chan *c)
{
	struct mtk_uart_apdma_desc *d = c->desc;
	unsigned int len, wg, rg;
	int cnt;

	mtk_uart_apdma_write(c, VFF_INT_FLAG, VFF_RX_INT_CLR_B);

	if (!mtk_uart_apdma_read(c, VFF_VALID_SIZE))
		return;

	mtk_uart_apdma_write(c, VFF_EN, VFF_EN_CLR_B);
	mtk_uart_apdma_write(c, VFF_INT_EN, VFF_INT_EN_CLR_B);

	len = c->cfg.src_port_window_size;
	rg = mtk_uart_apdma_read(c, VFF_RPT);
	wg = mtk_uart_apdma_read(c, VFF_WPT);
	cnt = (wg & VFF_RING_SIZE) - (rg & VFF_RING_SIZE);

	/*
	 * The buffer is ring buffer. If wrap bit different,
	 * represents the start of the next cycle for WPT
	 */
	if ((rg ^ wg) & VFF_RING_WRAP)
		cnt += len;

	c->rx_status = d->avail_len - cnt;
	mtk_uart_apdma_write(c, VFF_RPT, wg);
}

static void mtk_uart_apdma_chan_complete_handler(struct mtk_chan *c)
{
	struct mtk_uart_apdma_desc *d = c->desc;

	if (d) {
		list_del(&d->vd.node);
		vchan_cookie_complete(&d->vd);
		c->desc = NULL;
	}
}

static irqreturn_t mtk_uart_apdma_irq_handler(int irq, void *dev_id)
{
	struct dma_chan *chan = (struct dma_chan *)dev_id;
	struct mtk_chan *c = to_mtk_uart_apdma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&c->vc.lock, flags);
	if (c->dir == DMA_DEV_TO_MEM)
		mtk_uart_apdma_rx_handler(c);
	else if (c->dir == DMA_MEM_TO_DEV)
		mtk_uart_apdma_tx_handler(c);
	mtk_uart_apdma_chan_complete_handler(c);
	spin_unlock_irqrestore(&c->vc.lock, flags);

	return IRQ_HANDLED;
}

static int mtk_uart_apdma_alloc_chan_resources(struct dma_chan *chan)
{
	struct mtk_uart_apdmadev *mtkd = to_mtk_uart_apdma_dev(chan->device);
	struct mtk_chan *c = to_mtk_uart_apdma_chan(chan);
	unsigned int status;
	int ret;

	ret = pm_runtime_resume_and_get(mtkd->ddev.dev);
	if (ret < 0) {
		pm_runtime_put_noidle(chan->device->dev);
		return ret;
	}

	mtk_uart_apdma_write(c, VFF_ADDR, 0);
	mtk_uart_apdma_write(c, VFF_THRE, 0);
	mtk_uart_apdma_write(c, VFF_LEN, 0);
	mtk_uart_apdma_write(c, VFF_RST, VFF_WARM_RST_B);

	ret = readx_poll_timeout(readl, c->base + VFF_EN,
			  status, !status, 10, 100);
	if (ret)
		goto err_pm;

	ret = request_irq(c->irq, mtk_uart_apdma_irq_handler,
			  IRQF_TRIGGER_NONE, KBUILD_MODNAME, chan);
	if (ret < 0) {
		dev_err(chan->device->dev, "Can't request dma IRQ\n");
		ret = -EINVAL;
		goto err_pm;
	}

	if (mtkd->support_33bits)
		mtk_uart_apdma_write(c, VFF_4G_SUPPORT, VFF_4G_SUPPORT_CLR_B);

err_pm:
	pm_runtime_put_noidle(mtkd->ddev.dev);
	return ret;
}

static void mtk_uart_apdma_free_chan_resources(struct dma_chan *chan)
{
	struct mtk_uart_apdmadev *mtkd = to_mtk_uart_apdma_dev(chan->device);
	struct mtk_chan *c = to_mtk_uart_apdma_chan(chan);

	free_irq(c->irq, chan);

	tasklet_kill(&c->vc.task);

	vchan_free_chan_resources(&c->vc);

	pm_runtime_put_sync(mtkd->ddev.dev);
}

static enum dma_status mtk_uart_apdma_tx_status(struct dma_chan *chan,
					 dma_cookie_t cookie,
					 struct dma_tx_state *txstate)
{
	struct mtk_chan *c = to_mtk_uart_apdma_chan(chan);
	enum dma_status ret;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (!txstate)
		return ret;

	dma_set_residue(txstate, c->rx_status);

	return ret;
}

/*
 * dmaengine_prep_slave_single will call the function. and sglen is 1.
 * 8250 uart using one ring buffer, and deal with one sg.
 */
static struct dma_async_tx_descriptor *mtk_uart_apdma_prep_slave_sg
	(struct dma_chan *chan, struct scatterlist *sgl,
	unsigned int sglen, enum dma_transfer_direction dir,
	unsigned long tx_flags, void *context)
{
	struct mtk_chan *c = to_mtk_uart_apdma_chan(chan);
	struct mtk_uart_apdma_desc *d;

	if (!is_slave_direction(dir) || sglen != 1)
		return NULL;

	/* Now allocate and setup the descriptor */
	d = kzalloc(sizeof(*d), GFP_NOWAIT);
	if (!d)
		return NULL;

	d->avail_len = sg_dma_len(sgl);
	d->addr = sg_dma_address(sgl);
	c->dir = dir;

	return vchan_tx_prep(&c->vc, &d->vd, tx_flags);
}

static void mtk_uart_apdma_issue_pending(struct dma_chan *chan)
{
	struct mtk_chan *c = to_mtk_uart_apdma_chan(chan);
	struct virt_dma_desc *vd;
	unsigned long flags;

	spin_lock_irqsave(&c->vc.lock, flags);
	if (vchan_issue_pending(&c->vc) && !c->desc) {
		vd = vchan_next_desc(&c->vc);
		c->desc = to_mtk_uart_apdma_desc(&vd->tx);

		if (c->dir == DMA_DEV_TO_MEM)
			mtk_uart_apdma_start_rx(c);
		else if (c->dir == DMA_MEM_TO_DEV)
			mtk_uart_apdma_start_tx(c);
	}

	spin_unlock_irqrestore(&c->vc.lock, flags);
}

static int mtk_uart_apdma_slave_config(struct dma_chan *chan,
				   struct dma_slave_config *config)
{
	struct mtk_chan *c = to_mtk_uart_apdma_chan(chan);

	memcpy(&c->cfg, config, sizeof(*config));

	return 0;
}

static int mtk_uart_apdma_terminate_all(struct dma_chan *chan)
{
	struct mtk_chan *c = to_mtk_uart_apdma_chan(chan);
	unsigned long flags;
	unsigned int status;
	LIST_HEAD(head);
	int ret;

	mtk_uart_apdma_write(c, VFF_FLUSH, VFF_FLUSH_B);

	ret = readx_poll_timeout(readl, c->base + VFF_FLUSH,
			  status, status != VFF_FLUSH_B, 10, 100);
	if (ret)
		dev_err(c->vc.chan.device->dev, "flush: fail, status=0x%x\n",
			mtk_uart_apdma_read(c, VFF_DEBUG_STATUS));

	/*
	 * Stop need 3 steps.
	 * 1. set stop to 1
	 * 2. wait en to 0
	 * 3. set stop as 0
	 */
	mtk_uart_apdma_write(c, VFF_STOP, VFF_STOP_B);
	ret = readx_poll_timeout(readl, c->base + VFF_EN,
			  status, !status, 10, 100);
	if (ret)
		dev_err(c->vc.chan.device->dev, "stop: fail, status=0x%x\n",
			mtk_uart_apdma_read(c, VFF_DEBUG_STATUS));

	mtk_uart_apdma_write(c, VFF_STOP, VFF_STOP_CLR_B);
	mtk_uart_apdma_write(c, VFF_INT_EN, VFF_INT_EN_CLR_B);

	if (c->dir == DMA_DEV_TO_MEM)
		mtk_uart_apdma_write(c, VFF_INT_FLAG, VFF_RX_INT_CLR_B);
	else if (c->dir == DMA_MEM_TO_DEV)
		mtk_uart_apdma_write(c, VFF_INT_FLAG, VFF_TX_INT_CLR_B);

	synchronize_irq(c->irq);

	spin_lock_irqsave(&c->vc.lock, flags);
	vchan_get_all_descriptors(&c->vc, &head);
	spin_unlock_irqrestore(&c->vc.lock, flags);

	vchan_dma_desc_free_list(&c->vc, &head);

	return 0;
}

static int mtk_uart_apdma_device_pause(struct dma_chan *chan)
{
	struct mtk_chan *c = to_mtk_uart_apdma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&c->vc.lock, flags);

	mtk_uart_apdma_write(c, VFF_EN, VFF_EN_CLR_B);
	mtk_uart_apdma_write(c, VFF_INT_EN, VFF_INT_EN_CLR_B);

	spin_unlock_irqrestore(&c->vc.lock, flags);
	synchronize_irq(c->irq);

	return 0;
}

static void mtk_uart_apdma_free(struct mtk_uart_apdmadev *mtkd)
{
	while (!list_empty(&mtkd->ddev.channels)) {
		struct mtk_chan *c = list_first_entry(&mtkd->ddev.channels,
			struct mtk_chan, vc.chan.device_node);

		list_del(&c->vc.chan.device_node);
		tasklet_kill(&c->vc.task);
	}
}

static const struct of_device_id mtk_uart_apdma_match[] = {
	{ .compatible = "mediatek,mt6577-uart-dma", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mtk_uart_apdma_match);

static int mtk_uart_apdma_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct mtk_uart_apdmadev *mtkd;
	int bit_mask = 32, rc;
	struct mtk_chan *c;
	unsigned int i;

	mtkd = devm_kzalloc(&pdev->dev, sizeof(*mtkd), GFP_KERNEL);
	if (!mtkd)
		return -ENOMEM;

	mtkd->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(mtkd->clk)) {
		dev_err(&pdev->dev, "No clock specified\n");
		rc = PTR_ERR(mtkd->clk);
		return rc;
	}

	if (of_property_read_bool(np, "mediatek,dma-33bits"))
		mtkd->support_33bits = true;

	if (mtkd->support_33bits)
		bit_mask = 33;

	rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(bit_mask));
	if (rc)
		return rc;

	dma_cap_set(DMA_SLAVE, mtkd->ddev.cap_mask);
	mtkd->ddev.device_alloc_chan_resources =
				mtk_uart_apdma_alloc_chan_resources;
	mtkd->ddev.device_free_chan_resources =
				mtk_uart_apdma_free_chan_resources;
	mtkd->ddev.device_tx_status = mtk_uart_apdma_tx_status;
	mtkd->ddev.device_issue_pending = mtk_uart_apdma_issue_pending;
	mtkd->ddev.device_prep_slave_sg = mtk_uart_apdma_prep_slave_sg;
	mtkd->ddev.device_config = mtk_uart_apdma_slave_config;
	mtkd->ddev.device_pause = mtk_uart_apdma_device_pause;
	mtkd->ddev.device_terminate_all = mtk_uart_apdma_terminate_all;
	mtkd->ddev.src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE);
	mtkd->ddev.dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE);
	mtkd->ddev.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	mtkd->ddev.residue_granularity = DMA_RESIDUE_GRANULARITY_SEGMENT;
	mtkd->ddev.dev = &pdev->dev;
	INIT_LIST_HEAD(&mtkd->ddev.channels);

	mtkd->dma_requests = MTK_UART_APDMA_NR_VCHANS;
	if (of_property_read_u32(np, "dma-requests", &mtkd->dma_requests)) {
		dev_info(&pdev->dev,
			 "Using %u as missing dma-requests property\n",
			 MTK_UART_APDMA_NR_VCHANS);
	}

	for (i = 0; i < mtkd->dma_requests; i++) {
		c = devm_kzalloc(mtkd->ddev.dev, sizeof(*c), GFP_KERNEL);
		if (!c) {
			rc = -ENODEV;
			goto err_no_dma;
		}

		c->base = devm_platform_ioremap_resource(pdev, i);
		if (IS_ERR(c->base)) {
			rc = PTR_ERR(c->base);
			goto err_no_dma;
		}
		c->vc.desc_free = mtk_uart_apdma_desc_free;
		vchan_init(&c->vc, &mtkd->ddev);

		rc = platform_get_irq(pdev, i);
		if (rc < 0)
			goto err_no_dma;
		c->irq = rc;
	}

	pm_runtime_enable(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);

	rc = dma_async_device_register(&mtkd->ddev);
	if (rc)
		goto rpm_disable;

	platform_set_drvdata(pdev, mtkd);

	/* Device-tree DMA controller registration */
	rc = of_dma_controller_register(np, of_dma_xlate_by_chan_id, mtkd);
	if (rc)
		goto dma_remove;

	return rc;

dma_remove:
	dma_async_device_unregister(&mtkd->ddev);
rpm_disable:
	pm_runtime_disable(&pdev->dev);
err_no_dma:
	mtk_uart_apdma_free(mtkd);
	return rc;
}

static int mtk_uart_apdma_remove(struct platform_device *pdev)
{
	struct mtk_uart_apdmadev *mtkd = platform_get_drvdata(pdev);

	of_dma_controller_free(pdev->dev.of_node);

	mtk_uart_apdma_free(mtkd);

	dma_async_device_unregister(&mtkd->ddev);

	pm_runtime_disable(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mtk_uart_apdma_suspend(struct device *dev)
{
	struct mtk_uart_apdmadev *mtkd = dev_get_drvdata(dev);

	if (!pm_runtime_suspended(dev))
		clk_disable_unprepare(mtkd->clk);

	return 0;
}

static int mtk_uart_apdma_resume(struct device *dev)
{
	int ret;
	struct mtk_uart_apdmadev *mtkd = dev_get_drvdata(dev);

	if (!pm_runtime_suspended(dev)) {
		ret = clk_prepare_enable(mtkd->clk);
		if (ret)
			return ret;
	}

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM
static int mtk_uart_apdma_runtime_suspend(struct device *dev)
{
	struct mtk_uart_apdmadev *mtkd = dev_get_drvdata(dev);

	clk_disable_unprepare(mtkd->clk);

	return 0;
}

static int mtk_uart_apdma_runtime_resume(struct device *dev)
{
	struct mtk_uart_apdmadev *mtkd = dev_get_drvdata(dev);

	return clk_prepare_enable(mtkd->clk);
}
#endif /* CONFIG_PM */

static const struct dev_pm_ops mtk_uart_apdma_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mtk_uart_apdma_suspend, mtk_uart_apdma_resume)
	SET_RUNTIME_PM_OPS(mtk_uart_apdma_runtime_suspend,
			   mtk_uart_apdma_runtime_resume, NULL)
};

static struct platform_driver mtk_uart_apdma_driver = {
	.probe	= mtk_uart_apdma_probe,
	.remove	= mtk_uart_apdma_remove,
	.driver = {
		.name		= KBUILD_MODNAME,
		.pm		= &mtk_uart_apdma_pm_ops,
		.of_match_table = of_match_ptr(mtk_uart_apdma_match),
	},
};

module_platform_driver(mtk_uart_apdma_driver);

MODULE_DESCRIPTION("MediaTek UART APDMA Controller Driver");
MODULE_AUTHOR("Long Cheng <long.cheng@mediatek.com>");
MODULE_LICENSE("GPL v2");
