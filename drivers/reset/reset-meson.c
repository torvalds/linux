// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Amlogic Meson Reset Controller driver
 *
 * Copyright (c) 2016 BayLibre, SAS.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/types.h>

#define BITS_PER_REG	32

struct meson_reset_param {
	int reg_count;
	int level_offset;
};

struct meson_reset {
	void __iomem *reg_base;
	const struct meson_reset_param *param;
	struct reset_controller_dev rcdev;
	spinlock_t lock;
};

static int meson_reset_reset(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct meson_reset *data =
		container_of(rcdev, struct meson_reset, rcdev);
	unsigned int bank = id / BITS_PER_REG;
	unsigned int offset = id % BITS_PER_REG;
	void __iomem *reg_addr = data->reg_base + (bank << 2);

	writel(BIT(offset), reg_addr);

	return 0;
}

static int meson_reset_level(struct reset_controller_dev *rcdev,
			    unsigned long id, bool assert)
{
	struct meson_reset *data =
		container_of(rcdev, struct meson_reset, rcdev);
	unsigned int bank = id / BITS_PER_REG;
	unsigned int offset = id % BITS_PER_REG;
	void __iomem *reg_addr;
	unsigned long flags;
	u32 reg;

	reg_addr = data->reg_base + data->param->level_offset + (bank << 2);

	spin_lock_irqsave(&data->lock, flags);

	reg = readl(reg_addr);
	if (assert)
		writel(reg & ~BIT(offset), reg_addr);
	else
		writel(reg | BIT(offset), reg_addr);

	spin_unlock_irqrestore(&data->lock, flags);

	return 0;
}

static int meson_reset_assert(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	return meson_reset_level(rcdev, id, true);
}

static int meson_reset_deassert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	return meson_reset_level(rcdev, id, false);
}

static const struct reset_control_ops meson_reset_ops = {
	.reset		= meson_reset_reset,
	.assert		= meson_reset_assert,
	.deassert	= meson_reset_deassert,
};

static const struct meson_reset_param meson8b_param = {
	.reg_count	= 8,
	.level_offset	= 0x7c,
};

static const struct meson_reset_param meson_a1_param = {
	.reg_count	= 3,
	.level_offset	= 0x40,
};

static const struct meson_reset_param meson_s4_param = {
	.reg_count	= 6,
	.level_offset	= 0x40,
};

static const struct of_device_id meson_reset_dt_ids[] = {
	 { .compatible = "amlogic,meson8b-reset",    .data = &meson8b_param},
	 { .compatible = "amlogic,meson-gxbb-reset", .data = &meson8b_param},
	 { .compatible = "amlogic,meson-axg-reset",  .data = &meson8b_param},
	 { .compatible = "amlogic,meson-a1-reset",   .data = &meson_a1_param},
	 { .compatible = "amlogic,meson-s4-reset",   .data = &meson_s4_param},
	 { /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, meson_reset_dt_ids);

static int meson_reset_probe(struct platform_device *pdev)
{
	struct meson_reset *data;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(data->reg_base))
		return PTR_ERR(data->reg_base);

	data->param = of_device_get_match_data(&pdev->dev);
	if (!data->param)
		return -ENODEV;

	platform_set_drvdata(pdev, data);

	spin_lock_init(&data->lock);

	data->rcdev.owner = THIS_MODULE;
	data->rcdev.nr_resets = data->param->reg_count * BITS_PER_REG;
	data->rcdev.ops = &meson_reset_ops;
	data->rcdev.of_node = pdev->dev.of_node;

	return devm_reset_controller_register(&pdev->dev, &data->rcdev);
}

static struct platform_driver meson_reset_driver = {
	.probe	= meson_reset_probe,
	.driver = {
		.name		= "meson_reset",
		.of_match_table	= meson_reset_dt_ids,
	},
};
module_platform_driver(meson_reset_driver);

MODULE_DESCRIPTION("Amlogic Meson Reset Controller driver");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_LICENSE("Dual BSD/GPL");
