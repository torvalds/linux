// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright ASPEED Technology

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/interrupt.h>
#include <asm/io.h>

#define LTPI_AUTO_CAP_LOW			0x24
#define   LTPI_I2C_DATA_FRAME_MUX		GENMASK(29, 24)
#define LTPI_AUTO_CAP_HIGH			0x28
#define LTPI_LINK_CONTROLL			0x80
#define   LTPI_MANUAL_CONTROLL			GENMASK(10, 10)
#define LTPI_INTR_STATUS			0x100
#define LTPI_INTR_EN				0x104
#define   LTPI_INTR_EN_OP_LINK_LOST		BIT(4)
#define LTPI_MANUAL_CAP_LOW			0x118
#define LTPI_MANUAL_CAP_HIGH			0x11C

struct aspeed_ltpi_priv {
	struct device *dev;
	void __iomem *regs;
	struct clk *ltpi_clk;
	struct reset_control *ltpi_rst;
};

static irqreturn_t aspeed_ltpi_irq_handler(int irq, void *dev_id)
{
	struct aspeed_ltpi_priv *priv = dev_id;
	u32 status = readl(priv->regs + LTPI_INTR_STATUS);

	if (status & LTPI_INTR_EN_OP_LINK_LOST) {
		writel(0, priv->regs + LTPI_INTR_EN);
		writel(status, priv->regs + LTPI_INTR_STATUS);
		panic("LTPI link lost!\n");
		/* Will not return */
	}

	writel(status, priv->regs + LTPI_INTR_STATUS);

	return IRQ_HANDLED;
}

static int aspeed_ltpi_init_mux(struct aspeed_ltpi_priv *priv)
{
	u32 reg;

	/* Switch I2C mux into data frame*/
	reg = readl(priv->regs + LTPI_AUTO_CAP_LOW);
	reg &= ~(LTPI_I2C_DATA_FRAME_MUX);
	writel(reg, priv->regs + LTPI_MANUAL_CAP_LOW);

	reg = readl(priv->regs + LTPI_AUTO_CAP_HIGH);
	writel(reg, priv->regs + LTPI_MANUAL_CAP_HIGH);

	/* Apply ltpi as manual mode */
	reg = readl(priv->regs + LTPI_LINK_CONTROLL);
	reg &= ~(LTPI_MANUAL_CONTROLL);
	writel(reg, priv->regs + LTPI_LINK_CONTROLL);

	return 0;
}

static int aspeed_ltpi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_dev_auxdata *lookup = dev_get_platdata(dev);
	struct device_node *np = dev->of_node;
	const struct of_device_id *match;
	struct aspeed_ltpi_priv *priv;
	int irq, ret;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (match && match->data) {
		if (of_property_match_string(np, "compatible", match->compatible) == 0)
			return 0;
		else
			return -ENODEV;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	priv->ltpi_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->ltpi_clk))
		return PTR_ERR(priv->ltpi_clk);

	clk_prepare_enable(priv->ltpi_clk);

	priv->ltpi_rst = devm_reset_control_get_optional_shared(&pdev->dev, NULL);
	if (IS_ERR(priv->ltpi_rst))
		return PTR_ERR(priv->ltpi_rst);

	reset_control_deassert(priv->ltpi_rst);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		/* Only controllers on the near end (AST2700) have IRQ signals */
		dev_info(priv->dev, "No IRQ connected. It is okay for remote controllers\n");
	} else {
		ret = devm_request_irq(priv->dev, irq, aspeed_ltpi_irq_handler,
				       0, dev_name(priv->dev), priv);

		writel(LTPI_INTR_EN_OP_LINK_LOST, priv->regs + LTPI_INTR_EN);
	}

	aspeed_ltpi_init_mux(priv);

	platform_set_drvdata(pdev, priv);
	if (np)
		of_platform_populate(np, NULL, lookup, priv->dev);

	return 0;
}

static int aspeed_ltpi_remove(struct platform_device *pdev)
{
	struct aspeed_ltpi_priv *priv;

	priv = platform_get_drvdata(pdev);
	reset_control_assert(priv->ltpi_rst);
	clk_disable_unprepare(priv->ltpi_clk);

	return 0;
}

static const struct of_device_id aspeed_ltpi_of_match[] = {
	{ .compatible = "aspeed-ltpi", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, aspeed_ltpi_of_match);

static struct platform_driver aspeed_ltpi_driver = {
	.probe = aspeed_ltpi_probe,
	.remove = aspeed_ltpi_remove,
	.driver = {
		.name = "aspeed-ltpi",
		.of_match_table = aspeed_ltpi_of_match,
	},
};

module_platform_driver(aspeed_ltpi_driver);

MODULE_DESCRIPTION("LVDS Tunneling Protocol and Interface Bus Driver");
MODULE_AUTHOR("Dylan Hung <dylan_hung@aspeedtech.com>");
MODULE_LICENSE("GPL");
