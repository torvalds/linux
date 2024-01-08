// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017, National Instruments Corp.
 * Copyright (c) 2017, Xilinx Inc
 *
 * FPGA Bridge Driver for the Xilinx LogiCORE Partial Reconfiguration
 * Decoupler IP Core.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/fpga/fpga-bridge.h>

#define CTRL_CMD_DECOUPLE	BIT(0)
#define CTRL_CMD_COUPLE		0
#define CTRL_OFFSET		0

struct xlnx_config_data {
	const char *name;
};

struct xlnx_pr_decoupler_data {
	const struct xlnx_config_data *ipconfig;
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

	status = xlnx_pr_decouple_read(priv, CTRL_OFFSET);

	clk_disable(priv->clk);

	return !status;
}

static const struct fpga_bridge_ops xlnx_pr_decoupler_br_ops = {
	.enable_set = xlnx_pr_decoupler_enable_set,
	.enable_show = xlnx_pr_decoupler_enable_show,
};

static const struct xlnx_config_data decoupler_config = {
	.name = "Xilinx PR Decoupler",
};

static const struct xlnx_config_data shutdown_config = {
	.name = "Xilinx DFX AXI Shutdown Manager",
};

static const struct of_device_id xlnx_pr_decoupler_of_match[] = {
	{ .compatible = "xlnx,pr-decoupler-1.00", .data = &decoupler_config },
	{ .compatible = "xlnx,pr-decoupler", .data = &decoupler_config },
	{ .compatible = "xlnx,dfx-axi-shutdown-manager-1.00",
					.data = &shutdown_config },
	{ .compatible = "xlnx,dfx-axi-shutdown-manager",
					.data = &shutdown_config },
	{},
};
MODULE_DEVICE_TABLE(of, xlnx_pr_decoupler_of_match);

static int xlnx_pr_decoupler_probe(struct platform_device *pdev)
{
	struct xlnx_pr_decoupler_data *priv;
	struct fpga_bridge *br;
	int err;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->ipconfig = device_get_match_data(&pdev->dev);

	priv->io_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->io_base))
		return PTR_ERR(priv->io_base);

	priv->clk = devm_clk_get(&pdev->dev, "aclk");
	if (IS_ERR(priv->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->clk),
				     "input clock not found\n");

	err = clk_prepare_enable(priv->clk);
	if (err) {
		dev_err(&pdev->dev, "unable to enable clock\n");
		return err;
	}

	clk_disable(priv->clk);

	br = fpga_bridge_register(&pdev->dev, priv->ipconfig->name,
				  &xlnx_pr_decoupler_br_ops, priv);
	if (IS_ERR(br)) {
		err = PTR_ERR(br);
		dev_err(&pdev->dev, "unable to register %s",
			priv->ipconfig->name);
		goto err_clk;
	}

	platform_set_drvdata(pdev, br);

	return 0;

err_clk:
	clk_unprepare(priv->clk);

	return err;
}

static int xlnx_pr_decoupler_remove(struct platform_device *pdev)
{
	struct fpga_bridge *bridge = platform_get_drvdata(pdev);
	struct xlnx_pr_decoupler_data *p = bridge->priv;

	fpga_bridge_unregister(bridge);

	clk_unprepare(p->clk);

	return 0;
}

static struct platform_driver xlnx_pr_decoupler_driver = {
	.probe = xlnx_pr_decoupler_probe,
	.remove = xlnx_pr_decoupler_remove,
	.driver = {
		.name = "xlnx_pr_decoupler",
		.of_match_table = xlnx_pr_decoupler_of_match,
	},
};

module_platform_driver(xlnx_pr_decoupler_driver);

MODULE_DESCRIPTION("Xilinx Partial Reconfiguration Decoupler");
MODULE_AUTHOR("Moritz Fischer <mdf@kernel.org>");
MODULE_AUTHOR("Michal Simek <michal.simek@xilinx.com>");
MODULE_LICENSE("GPL v2");
