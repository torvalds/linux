/*
 * Copyright (c) 2014 ROCKCHIP, Inc.
 * Author: Dai Kelin <dkl@rock-chips.com>
 * Based on codes from Heiko Stuebner <heiko@sntech.de>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/reset-controller.h>
#include <linux/spinlock.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/module.h>

#include <dt-bindings/clock/rockchip.h>


struct rockchip_reset {
	struct reset_controller_dev	rcdev;
	void __iomem			*reg_base;
	int				num_regs;
	int				num_per_reg;
	u8				flags;
	spinlock_t			lock;
};

static int rockchip_reset_assert(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct rockchip_reset *reset = container_of(rcdev,
						     struct rockchip_reset,
						     rcdev);
	int bank = id / reset->num_per_reg;
	int offset = id % reset->num_per_reg;

	if (reset->flags & ROCKCHIP_RESET_HIWORD_MASK) {
		writel(BIT(offset) | (BIT(offset) << 16),
		       reset->reg_base + (bank * 4));
	} else {
		unsigned long flags;
		u32 reg;

		spin_lock_irqsave(&reset->lock, flags);

		reg = readl(reset->reg_base + (bank * 4));
		writel(reg | BIT(offset), reset->reg_base + (bank * 4));

		spin_unlock_irqrestore(&reset->lock, flags);
	}

	return 0;
}

static int rockchip_reset_deassert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	struct rockchip_reset *reset = container_of(rcdev,
						     struct rockchip_reset,
						     rcdev);
	int bank = id / reset->num_per_reg;
	int offset = id % reset->num_per_reg;

	if (reset->flags & ROCKCHIP_RESET_HIWORD_MASK) {
		writel((BIT(offset) << 16), reset->reg_base + (bank * 4));
	} else {
		unsigned long flags;
		u32 reg;

		spin_lock_irqsave(&reset->lock, flags);

		reg = readl(reset->reg_base + (bank * 4));
		writel(reg & ~BIT(offset), reset->reg_base + (bank * 4));

		spin_unlock_irqrestore(&reset->lock, flags);
	}

	return 0;
}

static struct reset_control_ops rockchip_reset_ops = {
	.assert		= rockchip_reset_assert,
	.deassert	= rockchip_reset_deassert,
};

static int rockchip_register_reset(struct device_node *np,
				      unsigned int num_regs,
				      void __iomem *base, u8 flags)
{
	struct rockchip_reset *reset;
	int ret;

	reset = kzalloc(sizeof(*reset), GFP_KERNEL);
	if (!reset)
		return -ENOMEM;

	reset->flags = flags;
	reset->reg_base = base;
	reset->num_regs = num_regs;
	reset->num_per_reg = (flags & ROCKCHIP_RESET_HIWORD_MASK) ? 16 : 32;
	spin_lock_init(&reset->lock);

	reset->rcdev.owner = THIS_MODULE;
	reset->rcdev.nr_resets =  num_regs * reset->num_per_reg;
	reset->rcdev.ops = &rockchip_reset_ops;
	reset->rcdev.of_node = np;
	ret = reset_controller_register(&reset->rcdev);
	if (ret) {
		pr_err("%s: could not register reset controller, %d\n",
		       __func__, ret);
		kfree(reset);
		return ret;
	}

	return 0;
};

static int rockchip_reset_probe(struct platform_device *pdev)
{
	struct resource *res;
	void __iomem *base;
	resource_size_t size;
	u32 flag = 0;


	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base)) {
		pr_err("%s: ioremap err\n", __func__);
		return PTR_ERR(base);
	}

	size = resource_size(res);
	if (size%4) {
		pr_err("%s: wrong size value\n", __func__);
		return -EINVAL;
	}

	of_property_read_u32(pdev->dev.of_node, "rockchip,reset-flag", &flag);

	return rockchip_register_reset(pdev->dev.of_node, size/4, base, flag);
}

static const struct of_device_id rockchip_reset_dt_ids[]  = {
	{ .compatible = "rockchip,reset", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rockchip_reset_dt_ids);

static struct platform_driver rockchip_reset_driver = {
	.probe  = rockchip_reset_probe,
	.driver = {
		.name           = "rockchip-reset",
		.owner          = THIS_MODULE,
		.of_match_table = rockchip_reset_dt_ids,
	},
};
module_platform_driver(rockchip_reset_driver);

