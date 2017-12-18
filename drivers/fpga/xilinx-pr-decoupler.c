/*
 * Copyright (c) 2017, National Instruments Corp.
 * Copyright (c) 2017, Xilix Inc
 *
 * FPGA Bridge Driver for the Xilinx LogiCORE Partial Reconfiguration
 * Decoupler IP Core.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/fpga/fpga-bridge.h>

#define CTRL_CMD_DECOUPLE	BIT(0)
#define CTRL_CMD_COUPLE		0
#define CTRL_OFFSET		0

struct xlnx_pr_decoupler_data {
	void __iomem *io_base;
	struct clk *clk;
};

static inline void xlnx_pr_decoupler_write(struct xlnx_pr_decoupler_data *d,
					   u32 offset, u32 val)
{
	writel(val, d->io_base + offset);
}

static inline u32 xlnx_pr_decouple_read(const struct xlnx_pr_decoupler_data *d,
					u32 offset)
{
	return readl(d->io_base + offset);
}

static int xlnx_pr_decoupler_enable_set(struct fpga_bridge *bridge, bool enable)
{
	int err;
	struct xlnx_pr_decoupler_data *priv = bridge->priv;

	err = clk_enable(priv->clk);
	if (err)
		return err;

	if (enable)
		xlnx_pr_decoupler_write(priv, CTRL_OFFSET, CTRL_CMD_COUPLE);
	else
		xlnx_pr_decoupler_write(priv, CTRL_OFFSET, CTRL_CMD_DECOUPLE);

	clk_disable(priv->clk);

	return 0;
}

static int xlnx_pr_decoupler_enable_show(struct fpga_bridge *bridge)
{
	const struct xlnx_pr_decoupler_data *priv = bridge->priv;
	u32 status;
	int err;

	err = clk_enable(priv->clk);
	if (err)
		return err;

	status = readl(priv->io_base);

	clk_disable(priv->clk);

	return !status;
}

static const struct fpga_bridge_ops xlnx_pr_decoupler_br_ops = {
	.enable_set = xlnx_pr_decoupler_enable_set,
	.enable_show = xlnx_pr_decoupler_enable_show,
};

static const struct of_device_id xlnx_pr_decoupler_of_match[] = {
	{ .compatible = "xlnx,pr-decoupler-1.00", },
	{ .compatible = "xlnx,pr-decoupler", },
	{},
};
MODULE_DEVICE_TABLE(of, xlnx_pr_decoupler_of_match);

static int xlnx_pr_decoupler_probe(struct platform_device *pdev)
{
	struct xlnx_pr_decoupler_data *priv;
	int err;
	struct resource *res;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->io_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->io_base))
		return PTR_ERR(priv->io_base);

	priv->clk = devm_clk_get(&pdev->dev, "aclk");
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "input clock not found\n");
		return PTR_ERR(priv->clk);
	}

	err = clk_prepare_enable(priv->clk);
	if (err) {
		dev_err(&pdev->dev, "unable to enable clock\n");
		return err;
	}

	clk_disable(priv->clk);

	err = fpga_bridge_register(&pdev->dev, "Xilinx PR Decoupler",
				   &xlnx_pr_decoupler_br_ops, priv);

	if (err) {
		dev_err(&pdev->dev, "unable to register Xilinx PR Decoupler");
		clk_unprepare(priv->clk);
		return err;
	}

	return 0;
}

static int xlnx_pr_decoupler_remove(struct platform_device *pdev)
{
	struct fpga_bridge *bridge = platform_get_drvdata(pdev);
	struct xlnx_pr_decoupler_data *p = bridge->priv;

	fpga_bridge_unregister(&pdev->dev);

	clk_unprepare(p->clk);

	return 0;
}

static struct platform_driver xlnx_pr_decoupler_driver = {
	.probe = xlnx_pr_decoupler_probe,
	.remove = xlnx_pr_decoupler_remove,
	.driver = {
		.name = "xlnx_pr_decoupler",
		.of_match_table = of_match_ptr(xlnx_pr_decoupler_of_match),
	},
};

module_platform_driver(xlnx_pr_decoupler_driver);

MODULE_DESCRIPTION("Xilinx Partial Reconfiguration Decoupler");
MODULE_AUTHOR("Moritz Fischer <mdf@kernel.org>");
MODULE_AUTHOR("Michal Simek <michal.simek@xilinx.com>");
MODULE_LICENSE("GPL v2");
