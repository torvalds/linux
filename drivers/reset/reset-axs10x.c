/*
 * Copyright (C) 2017 Synopsys.
 *
 * Synopsys AXS10x reset driver.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>

#define to_axs10x_rst(p)	container_of((p), struct axs10x_rst, rcdev)

#define AXS10X_MAX_RESETS	32

struct axs10x_rst {
	void __iomem			*regs_rst;
	spinlock_t			lock;
	struct reset_controller_dev	rcdev;
};

static int axs10x_reset_reset(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct axs10x_rst *rst = to_axs10x_rst(rcdev);
	unsigned long flags;

	spin_lock_irqsave(&rst->lock, flags);
	writel(BIT(id), rst->regs_rst);
	spin_unlock_irqrestore(&rst->lock, flags);

	return 0;
}

static const struct reset_control_ops axs10x_reset_ops = {
	.reset	= axs10x_reset_reset,
};

static int axs10x_reset_probe(struct platform_device *pdev)
{
	struct axs10x_rst *rst;

	rst = devm_kzalloc(&pdev->dev, sizeof(*rst), GFP_KERNEL);
	if (!rst)
		return -ENOMEM;

	rst->regs_rst = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rst->regs_rst))
		return PTR_ERR(rst->regs_rst);

	spin_lock_init(&rst->lock);

	rst->rcdev.owner = THIS_MODULE;
	rst->rcdev.ops = &axs10x_reset_ops;
	rst->rcdev.of_node = pdev->dev.of_node;
	rst->rcdev.nr_resets = AXS10X_MAX_RESETS;

	return devm_reset_controller_register(&pdev->dev, &rst->rcdev);
}

static const struct of_device_id axs10x_reset_dt_match[] = {
	{ .compatible = "snps,axs10x-reset" },
	{ },
};

static struct platform_driver axs10x_reset_driver = {
	.probe	= axs10x_reset_probe,
	.driver	= {
		.name = "axs10x-reset",
		.of_match_table = axs10x_reset_dt_match,
	},
};
builtin_platform_driver(axs10x_reset_driver);

MODULE_AUTHOR("Eugeniy Paltsev <Eugeniy.Paltsev@synopsys.com>");
MODULE_DESCRIPTION("Synopsys AXS10x reset driver");
MODULE_LICENSE("GPL v2");
