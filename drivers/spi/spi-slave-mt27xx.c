// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2018 MediaTek Inc.

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <linux/of.h>


#define SPIS_IRQ_EN_REG		0x0
#define SPIS_IRQ_CLR_REG	0x4
#define SPIS_IRQ_ST_REG		0x8
#define SPIS_IRQ_MASK_REG	0xc
#define SPIS_CFG_REG		0x10
#define SPIS_RX_DATA_REG	0x14
#define SPIS_TX_DATA_REG	0x18
#define SPIS_RX_DST_REG		0x1c
#define SPIS_TX_SRC_REG		0x20
#define SPIS_DMA_CFG_REG	0x30
#define SPIS_SOFT_RST_REG	0x40

/* SPIS_IRQ_EN_REG */
#define DMA_DONE_EN		BIT(7)
#define DATA_DONE_EN		BIT(2)
#define RSTA_DONE_EN		BIT(1)
#define CMD_INVALID_EN		BIT(0)

/* SPIS_IRQ_ST_REG */
#define DMA_DONE_ST		BIT(7)
#define DATA_DONE_ST		BIT(2)
#define RSTA_DONE_ST		BIT(1)
#define CMD_INVALID_ST		BIT(0)

/* SPIS_IRQ_MASK_REG */
#define DMA_DONE_MASK		BIT(7)
#define DATA_DONE_MASK		BIT(2)
#define RSTA_DONE_MASK		BIT(1)
#define CMD_INVALID_MASK	BIT(0)

/* SPIS_CFG_REG */
#define SPIS_TX_ENDIAN		BIT(7)
#define SPIS_RX_ENDIAN		BIT(6)
#define SPIS_TXMSBF		BIT(5)
#define SPIS_RXMSBF		BIT(4)
#define SPIS_CPHA		BIT(3)
#define SPIS_CPOL		BIT(2)
#define SPIS_TX_EN		BIT(1)
#define SPIS_RX_EN		BIT(0)

/* SPIS_DMA_CFG_REG */
#define TX_DMA_TRIG_EN		BIT(31)
#define TX_DMA_EN		BIT(30)
#define RX_DMA_EN		BIT(29)
#define TX_DMA_LEN		0xfffff

/* SPIS_SOFT_RST_REG */
#define SPIS_DMA_ADDR_EN	BIT(1)
#define SPIS_SOFT_RST		BIT(0)

struct mtk_spi_slave {
	struct device *dev;
	void __iomem *base;
	struct clk *spi_clk;
	struct completion xfer_done;
	struct spi_transfer *cur_transfer;
	bool target_aborted;
	const struct mtk_spi_compatible *dev_comp;
};

struct mtk_spi_compatible {
	const u32 max_fifo_size;
	bool must_rx;
};

static const struct mtk_spi_compatible mt2712_compat = {
	.max_fifo_size = 512,
};
static const struct mtk_spi_compatible mt8195_compat = {
	.max_fifo_size = 128,
	.must_rx = true,
};

static const struct of_device_id mtk_spi_slave_of_match[] = {
	{ .compatible = "mediatek,mt2712-spi-slave",
	  .data = (void *)&mt2712_compat,},
	{ .compatible = "mediatek,mt8195-spi-slave",
	  .data = (void *)&mt8195_compat,},
	{}
};
MODULE_DEVICE_TABLE(of, mtk_spi_slave_of_match);

static void mtk_spi_slave_disable_dma(struct mtk_spi_slave *mdata)
{
	u32 reg_val;

	reg_val = readl(mdata->base + SPIS_DMA_CFG_REG);
	reg_val &= ~RX_DMA_EN;
	reg_val &= ~TX_DMA_EN;
	writel(reg_val, mdata->base + SPIS_DMA_CFG_REG);
}

static void mtk_spi_slave_disable_xfer(struct mtk_spi_slave *mdata)
{
	u32 reg_val;

	reg_val = readl(mdata->base + SPIS_CFG_REG);
	reg_val &= ~SPIS_TX_EN;
	reg_val &= ~SPIS_RX_EN;
	writel(reg_val, mdata->base + SPIS_CFG_REG);
}

static int mtk_spi_slave_wait_for_completion(struct mtk_spi_slave *mdata)
{
	if (wait_for_completion_interruptible(&mdata->xfer_done) ||
	    mdata->target_aborted) {
		dev_err(mdata->dev, "interrupted\n");
		return -EINTR;
	}

	return 0;
}

static int mtk_spi_slave_prepare_message(struct spi_controller *ctlr,
					 struct spi_message *msg)
{
	struct mtk_spi_slave *mdata = spi_controller_get_devdata(ctlr);
	struct spi_device *spi = msg->spi;
	bool cpha, cpol;
	u32 reg_val;

	cpha = spi->mode & SPI_CPHA ? 1 : 0;
	cpol = spi->mode & SPI_CPOL ? 1 : 0;

	reg_val = readl(mdata->base + SPIS_CFG_REG);
	if (cpha)
		reg_val |= SPIS_CPHA;
	else
		reg_val &= ~SPIS_CPHA;
	if (cpol)
		reg_val |= SPIS_CPOL;
	else
		reg_val &= ~SPIS_CPOL;

	if (spi->mode & SPI_LSB_FIRST)
		reg_val &= ~(SPIS_TXMSBF | SPIS_RXMSBF);
	else
		reg_val |= SPIS_TXMSBF | SPIS_RXMSBF;

	reg_val &= ~SPIS_TX_ENDIAN;
	reg_val &= ~SPIS_RX_ENDIAN;
	writel(reg_val, mdata->base + SPIS_CFG_REG);

	return 0;
}

static int mtk_spi_slave_fifo_transfer(struct spi_controller *ctlr,
				       struct spi_device *spi,
				       struct spi_transfer *xfer)
{
	struct mtk_spi_slave *mdata = spi_controller_get_devdata(ctlr);
	int reg_val, cnt, remainder, ret;

	writel(SPIS_SOFT_RST, mdata->base + SPIS_SOFT_RST_REG);

	reg_val = readl(mdata->base + SPIS_CFG_REG);
	if (xfer->rx_buf)
		reg_val |= SPIS_RX_EN;
	if (xfer->tx_buf)
		reg_val |= SPIS_TX_EN;
	writel(reg_val, mdata->base + SPIS_CFG_REG);

	cnt = xfer->len / 4;
	if (xfer->tx_buf)
		iowrite32_rep(mdata->base + SPIS_TX_DATA_REG,
			      xfer->tx_buf, cnt);

	remainder = xfer->len % 4;
	if (xfer->tx_buf && remainder > 0) {
		reg_val = 0;
		memcpy(&reg_val, xfer->tx_buf + cnt * 4, remainder);
		writel(reg_val, mdata->base + SPIS_TX_DATA_REG);
	}

	ret = mtk_spi_slave_wait_for_completion(mdata);
	if (ret) {
		mtk_spi_slave_disable_xfer(mdata);
		writel(SPIS_SOFT_RST, mdata->base + SPIS_SOFT_RST_REG);
	}

	return ret;
}

static int mtk_spi_slave_dma_transfer(struct spi_controller *ctlr,
				      struct spi_device *spi,
				      struct spi_transfer *xfer)
{
	struct mtk_spi_slave *mdata = spi_controller_get_devdata(ctlr);
	struct device *dev = mdata->dev;
	int reg_val, ret;

	writel(SPIS_SOFT_RST, mdata->base + SPIS_SOFT_RST_REG);

	if (xfer->tx_buf) {
		/* tx_buf is a const void* where we need a void * for
		 * the dma mapping
		 */
		void *nonconst_tx = (void *)xfer->tx_buf;

		xfer->tx_dma = dma_map_single(dev, nonconst_tx,
					      xfer->len, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, xfer->tx_dma)) {
			ret = -ENOMEM;
			goto disable_transfer;
		}
	}

	if (xfer->rx_buf) {
		xfer->rx_dma = dma_map_single(dev, xfer->rx_buf,
					      xfer->len, DMA_FROM_DEVICE);
		if (dma_mapping_error(dev, xfer->rx_dma)) {
			ret = -ENOMEM;
			goto unmap_txdma;
		}
	}

	writel(xfer->tx_dma, mdata->base + SPIS_TX_SRC_REG);
	writel(xfer->rx_dma, mdata->base + SPIS_RX_DST_REG);

	writel(SPIS_DMA_ADDR_EN, mdata->base + SPIS_SOFT_RST_REG);

	/* enable config reg tx rx_enable */
	reg_val = readl(mdata->base + SPIS_CFG_REG);
	if (xfer->tx_buf)
		reg_val |= SPIS_TX_EN;
	if (xfer->rx_buf)
		reg_val |= SPIS_RX_EN;
	writel(reg_val, mdata->base + SPIS_CFG_REG);

	/* config dma */
	reg_val = 0;
	reg_val |= (xfer->len - 1) & TX_DMA_LEN;
	writel(reg_val, mdata->base + SPIS_DMA_CFG_REG);

	reg_val = readl(mdata->base + SPIS_DMA_CFG_REG);
	if (xfer->tx_buf)
		reg_val |= TX_DMA_EN;
	if (xfer->rx_buf)
		reg_val |= RX_DMA_EN;
	reg_val |= TX_DMA_TRIG_EN;
	writel(reg_val, mdata->base + SPIS_DMA_CFG_REG);

	ret = mtk_spi_slave_wait_for_completion(mdata);
	if (ret)
		goto unmap_rxdma;

	return 0;

unmap_rxdma:
	if (xfer->rx_buf)
		dma_unmap_single(dev, xfer->rx_dma,
				 xfer->len, DMA_FROM_DEVICE);

unmap_txdma:
	if (xfer->tx_buf)
		dma_unmap_single(dev, xfer->tx_dma,
				 xfer->len, DMA_TO_DEVICE);

disable_transfer:
	mtk_spi_slave_disable_dma(mdata);
	mtk_spi_slave_disable_xfer(mdata);
	writel(SPIS_SOFT_RST, mdata->base + SPIS_SOFT_RST_REG);

	return ret;
}

static int mtk_spi_slave_transfer_one(struct spi_controller *ctlr,
				      struct spi_device *spi,
				      struct spi_transfer *xfer)
{
	struct mtk_spi_slave *mdata = spi_controller_get_devdata(ctlr);

	reinit_completion(&mdata->xfer_done);
	mdata->target_aborted = false;
	mdata->cur_transfer = xfer;

	if (xfer->len > mdata->dev_comp->max_fifo_size)
		return mtk_spi_slave_dma_transfer(ctlr, spi, xfer);
	else
		return mtk_spi_slave_fifo_transfer(ctlr, spi, xfer);
}

static int mtk_spi_slave_setup(struct spi_device *spi)
{
	struct mtk_spi_slave *mdata = spi_controller_get_devdata(spi->controller);
	u32 reg_val;

	reg_val = DMA_DONE_EN | DATA_DONE_EN |
		  RSTA_DONE_EN | CMD_INVALID_EN;
	writel(reg_val, mdata->base + SPIS_IRQ_EN_REG);

	reg_val = DMA_DONE_MASK | DATA_DONE_MASK |
		  RSTA_DONE_MASK | CMD_INVALID_MASK;
	writel(reg_val, mdata->base + SPIS_IRQ_MASK_REG);

	mtk_spi_slave_disable_dma(mdata);
	mtk_spi_slave_disable_xfer(mdata);

	return 0;
}

static int mtk_target_abort(struct spi_controller *ctlr)
{
	struct mtk_spi_slave *mdata = spi_controller_get_devdata(ctlr);

	mdata->target_aborted = true;
	complete(&mdata->xfer_done);

	return 0;
}

static irqreturn_t mtk_spi_slave_interrupt(int irq, void *dev_id)
{
	struct spi_controller *ctlr = dev_id;
	struct mtk_spi_slave *mdata = spi_controller_get_devdata(ctlr);
	struct spi_transfer *trans = mdata->cur_transfer;
	u32 int_status, reg_val, cnt, remainder;

	int_status = readl(mdata->base + SPIS_IRQ_ST_REG);
	writel(int_status, mdata->base + SPIS_IRQ_CLR_REG);

	if (!trans)
		return IRQ_NONE;

	if ((int_status & DMA_DONE_ST) &&
	    ((int_status & DATA_DONE_ST) ||
	    (int_status & RSTA_DONE_ST))) {
		writel(SPIS_SOFT_RST, mdata->base + SPIS_SOFT_RST_REG);

		if (trans->tx_buf)
			dma_unmap_single(mdata->dev, trans->tx_dma,
					 trans->len, DMA_TO_DEVICE);
		if (trans->rx_buf)
			dma_unmap_single(mdata->dev, trans->rx_dma,
					 trans->len, DMA_FROM_DEVICE);

		mtk_spi_slave_disable_dma(mdata);
		mtk_spi_slave_disable_xfer(mdata);
	}

	if ((!(int_status & DMA_DONE_ST)) &&
	    ((int_status & DATA_DONE_ST) ||
	    (int_status & RSTA_DONE_ST))) {
		cnt = trans->len / 4;
		if (trans->rx_buf)
			ioread32_rep(mdata->base + SPIS_RX_DATA_REG,
				     trans->rx_buf, cnt);
		remainder = trans->len % 4;
		if (trans->rx_buf && remainder > 0) {
			reg_val = readl(mdata->base + SPIS_RX_DATA_REG);
			memcpy(trans->rx_buf + (cnt * 4),
			       &reg_val, remainder);
		}

		mtk_spi_slave_disable_xfer(mdata);
	}

	if (int_status & CMD_INVALID_ST) {
		dev_warn(&ctlr->dev, "cmd invalid\n");
		return IRQ_NONE;
	}

	mdata->cur_transfer = NULL;
	complete(&mdata->xfer_done);

	return IRQ_HANDLED;
}

static int mtk_spi_slave_probe(struct platform_device *pdev)
{
	struct spi_controller *ctlr;
	struct mtk_spi_slave *mdata;
	int irq, ret;
	const struct of_device_id *of_id;

	ctlr = spi_alloc_slave(&pdev->dev, sizeof(*mdata));
	if (!ctlr) {
		dev_err(&pdev->dev, "failed to alloc spi slave\n");
		return -ENOMEM;
	}

	ctlr->auto_runtime_pm = true;
	ctlr->dev.of_node = pdev->dev.of_node;
	ctlr->mode_bits = SPI_CPOL | SPI_CPHA;
	ctlr->mode_bits |= SPI_LSB_FIRST;

	ctlr->prepare_message = mtk_spi_slave_prepare_message;
	ctlr->transfer_one = mtk_spi_slave_transfer_one;
	ctlr->setup = mtk_spi_slave_setup;
	ctlr->target_abort = mtk_target_abort;

	of_id = of_match_node(mtk_spi_slave_of_match, pdev->dev.of_node);
	if (!of_id) {
		dev_err(&pdev->dev, "failed to probe of_node\n");
		ret = -EINVAL;
		goto err_put_ctlr;
	}
	mdata = spi_controller_get_devdata(ctlr);
	mdata->dev_comp = of_id->data;

	if (mdata->dev_comp->must_rx)
		ctlr->flags = SPI_CONTROLLER_MUST_RX;

	platform_set_drvdata(pdev, ctlr);

	init_completion(&mdata->xfer_done);
	mdata->dev = &pdev->dev;
	mdata->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mdata->base)) {
		ret = PTR_ERR(mdata->base);
		goto err_put_ctlr;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto err_put_ctlr;
	}

	ret = devm_request_irq(&pdev->dev, irq, mtk_spi_slave_interrupt,
			       IRQF_TRIGGER_NONE, dev_name(&pdev->dev), ctlr);
	if (ret) {
		dev_err(&pdev->dev, "failed to register irq (%d)\n", ret);
		goto err_put_ctlr;
	}

	mdata->spi_clk = devm_clk_get(&pdev->dev, "spi");
	if (IS_ERR(mdata->spi_clk)) {
		ret = PTR_ERR(mdata->spi_clk);
		dev_err(&pdev->dev, "failed to get spi-clk: %d\n", ret);
		goto err_put_ctlr;
	}

	ret = clk_prepare_enable(mdata->spi_clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to enable spi_clk (%d)\n", ret);
		goto err_put_ctlr;
	}

	pm_runtime_enable(&pdev->dev);

	ret = devm_spi_register_controller(&pdev->dev, ctlr);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to register slave controller(%d)\n", ret);
		clk_disable_unprepare(mdata->spi_clk);
		goto err_disable_runtime_pm;
	}

	clk_disable_unprepare(mdata->spi_clk);

	return 0;

err_disable_runtime_pm:
	pm_runtime_disable(&pdev->dev);
err_put_ctlr:
	spi_controller_put(ctlr);

	return ret;
}

static void mtk_spi_slave_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
}

#ifdef CONFIG_PM_SLEEP
static int mtk_spi_slave_suspend(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct mtk_spi_slave *mdata = spi_controller_get_devdata(ctlr);
	int ret;

	ret = spi_controller_suspend(ctlr);
	if (ret)
		return ret;

	if (!pm_runtime_suspended(dev))
		clk_disable_unprepare(mdata->spi_clk);

	return ret;
}

static int mtk_spi_slave_resume(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct mtk_spi_slave *mdata = spi_controller_get_devdata(ctlr);
	int ret;

	if (!pm_runtime_suspended(dev)) {
		ret = clk_prepare_enable(mdata->spi_clk);
		if (ret < 0) {
			dev_err(dev, "failed to enable spi_clk (%d)\n", ret);
			return ret;
		}
	}

	ret = spi_controller_resume(ctlr);
	if (ret < 0)
		clk_disable_unprepare(mdata->spi_clk);

	return ret;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM
static int mtk_spi_slave_runtime_suspend(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct mtk_spi_slave *mdata = spi_controller_get_devdata(ctlr);

	clk_disable_unprepare(mdata->spi_clk);

	return 0;
}

static int mtk_spi_slave_runtime_resume(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct mtk_spi_slave *mdata = spi_controller_get_devdata(ctlr);
	int ret;

	ret = clk_prepare_enable(mdata->spi_clk);
	if (ret < 0) {
		dev_err(dev, "failed to enable spi_clk (%d)\n", ret);
		return ret;
	}

	return 0;
}
#endif /* CONFIG_PM */

static const struct dev_pm_ops mtk_spi_slave_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(mtk_spi_slave_suspend, mtk_spi_slave_resume)
	SET_RUNTIME_PM_OPS(mtk_spi_slave_runtime_suspend,
			   mtk_spi_slave_runtime_resume, NULL)
};

static struct platform_driver mtk_spi_slave_driver = {
	.driver = {
		.name = "mtk-spi-slave",
		.pm	= &mtk_spi_slave_pm,
		.of_match_table = mtk_spi_slave_of_match,
	},
	.probe = mtk_spi_slave_probe,
	.remove_new = mtk_spi_slave_remove,
};

module_platform_driver(mtk_spi_slave_driver);

MODULE_DESCRIPTION("MTK SPI Slave Controller driver");
MODULE_AUTHOR("Leilk Liu <leilk.liu@mediatek.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:mtk-spi-slave");
