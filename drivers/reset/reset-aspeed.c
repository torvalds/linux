// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright ASPEED Technology

#include <linux/bits.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <dt-bindings/reset/aspeed,ast2700-reset.h>

struct aspeed_rst_soc_data {
	u32 ctrl2_offset;
	u32 num_resets;
};

struct aspeed_rst_data {
	struct reset_controller_dev rcdev;
	void __iomem *base;
	const struct aspeed_rst_soc_data *data;
};

#define to_rc_data(p) container_of(p, struct aspeed_rst_data, rcdev)

static int aspeed_reset_assert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct aspeed_rst_data *rc = to_rc_data(rcdev);
	u32 rst = BIT(id % 32);
	u32 reg = id >= 32 ? rc->data->ctrl2_offset : 0x00;

	writel(rst, rc->base + reg);
	return 0;
}

static int aspeed_reset_deassert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct aspeed_rst_data *rc = to_rc_data(rcdev);
	u32 rst = BIT(id % 32);
	u32 reg = id >= 32 ? rc->data->ctrl2_offset : 0x00;

	/* Use set to clear register */
	writel(rst, rc->base + reg + 0x04);
	return 0;
}

static int aspeed_reset_status(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct aspeed_rst_data *rc = to_rc_data(rcdev);
	u32 rst = BIT(id % 32);
	u32 reg = id >= 32 ? rc->data->ctrl2_offset : 0x00;

	return (readl(rc->base + reg) & rst);
}

static const struct aspeed_rst_soc_data ast2600_reset_data = {
	.ctrl2_offset = 0x10,
	.num_resets = 64,
};

static const struct aspeed_rst_soc_data ast2700_cpu_reset_data = {
	.ctrl2_offset = 0x20,
	.num_resets = ASPEED_CPU_RESET_NUMS,
};

static const struct aspeed_rst_soc_data ast2700_io_reset_data = {
	.ctrl2_offset = 0x20,
	.num_resets = ASPEED_IO_RESET_NUMS,
};

static const struct reset_control_ops aspeed_reset_ops = {
	.assert = aspeed_reset_assert,
	.deassert = aspeed_reset_deassert,
	.status = aspeed_reset_status,
};

static const struct of_device_id aspeed_reset_dt_ids[] = {
	{ .compatible = "aspeed,ast2600-reset", .data = &ast2600_reset_data, },
	{ .compatible = "aspeed,ast2700-cpu-reset", .data = &ast2700_cpu_reset_data, },
	{ .compatible = "aspeed,ast2700-io-reset", .data = &ast2700_io_reset_data, },
	{ .compatible = "aspeed,ast1700-reset", .data = &ast2700_io_reset_data, },
	{ },
};

static int aspeed_reset_probe(struct platform_device *pdev)
{
	struct aspeed_rst_data *reset_data;
	struct device *dev = &pdev->dev;

	reset_data = devm_kzalloc(dev, sizeof(*reset_data), GFP_KERNEL);
	if (!reset_data)
		return -ENOMEM;

	reset_data->data = of_device_get_match_data(&pdev->dev);
	if (!reset_data->data)
		return -EINVAL;

	platform_set_drvdata(pdev, reset_data);

	reset_data->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reset_data->base))
		return PTR_ERR(reset_data->base);

	reset_data->rcdev.owner = THIS_MODULE;
	reset_data->rcdev.ops = &aspeed_reset_ops;
	reset_data->rcdev.of_node = dev->of_node;
	reset_data->rcdev.nr_resets = reset_data->data->num_resets;

	return devm_reset_controller_register(dev, &reset_data->rcdev);
}

static struct platform_driver aspeed_reset_driver = {
	.probe = aspeed_reset_probe,
	.driver = {
		.name = "aspeed-reset",
		.of_match_table	= aspeed_reset_dt_ids,
	},
};

builtin_platform_driver(aspeed_reset_driver);
