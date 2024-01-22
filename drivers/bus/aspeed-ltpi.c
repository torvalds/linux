// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright ASPEED Technology

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <asm/io.h>

#define LTPI_AUTO_CAP_LOW			0x24
#define   LTPI_I2C_DATA_FRAME_MUX		GENMASK(29, 24)
#define LTPI_AUTO_CAP_HIGH			0x28
#define LTPI_LINK_CONTROLL			0x80
#define   LTPI_MANUAL_CONTROLL			GENMASK(10, 10)
#define LTPI_MANUAL_CAP_LOW			0x118
#define LTPI_MANUAL_CAP_HIGH			0x11C

static int aspeed_ltpi_probe(struct platform_device *pdev)
{
	const struct device *dev = &pdev->dev;
	const struct of_dev_auxdata *lookup = dev_get_platdata(dev);
	struct device_node *np = dev->of_node;
	const struct of_device_id *match;
	struct clk *ltpi_clk;
	void __iomem *regs;
	u32 reg;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (match && match->data) {
		if (of_property_match_string(np, "compatible", match->compatible) == 0)
			return 0;
		else
			return -ENODEV;
	}

	ltpi_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(ltpi_clk))
		return PTR_ERR(ltpi_clk);

	clk_prepare_enable(ltpi_clk);

	regs = devm_platform_ioremap_resource(pdev, 0);

	if (np)
		of_platform_populate(np, NULL, lookup, &pdev->dev);

	/* Switch I2C mux into data frame*/
	reg = readl(regs + LTPI_AUTO_CAP_LOW);
	reg &= ~(LTPI_I2C_DATA_FRAME_MUX);
	writel(reg, regs + LTPI_MANUAL_CAP_LOW);

	reg = readl(regs + LTPI_AUTO_CAP_HIGH);
	writel(reg, regs + LTPI_MANUAL_CAP_HIGH);

	/* Apply ltpi as manual mode */
	reg = readl(regs + LTPI_LINK_CONTROLL);
	reg &= ~(LTPI_MANUAL_CONTROLL);
	writel(reg, regs + LTPI_LINK_CONTROLL);

	return 0;
}

static int aspeed_ltpi_remove(struct platform_device *pdev)
{
	struct clk *ltpi_clk;

	ltpi_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(ltpi_clk))
		return PTR_ERR(ltpi_clk);

	clk_disable_unprepare(ltpi_clk);

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
