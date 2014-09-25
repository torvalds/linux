/*
 * Samsung S5P/EXYNOS SoC series MIPI CSIS/DSIM DPHY driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Author: Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

/* MIPI_PHYn_CONTROL register offset: n = 0..1 */
#define EXYNOS_MIPI_PHY_CONTROL(n)	((n) * 4)
#define EXYNOS_MIPI_PHY_ENABLE		(1 << 0)
#define EXYNOS_MIPI_PHY_SRESETN		(1 << 1)
#define EXYNOS_MIPI_PHY_MRESETN		(1 << 2)
#define EXYNOS_MIPI_PHY_RESET_MASK	(3 << 1)

enum exynos_mipi_phy_id {
	EXYNOS_MIPI_PHY_ID_CSIS0,
	EXYNOS_MIPI_PHY_ID_DSIM0,
	EXYNOS_MIPI_PHY_ID_CSIS1,
	EXYNOS_MIPI_PHY_ID_DSIM1,
	EXYNOS_MIPI_PHYS_NUM
};

#define is_mipi_dsim_phy_id(id) \
	((id) == EXYNOS_MIPI_PHY_ID_DSIM0 || (id) == EXYNOS_MIPI_PHY_ID_DSIM1)

struct exynos_mipi_video_phy {
	spinlock_t slock;
	struct video_phy_desc {
		struct phy *phy;
		unsigned int index;
	} phys[EXYNOS_MIPI_PHYS_NUM];
	void __iomem *regs;
};

static int __set_phy_state(struct exynos_mipi_video_phy *state,
			enum exynos_mipi_phy_id id, unsigned int on)
{
	void __iomem *addr;
	u32 reg, reset;

	addr = state->regs + EXYNOS_MIPI_PHY_CONTROL(id / 2);

	if (is_mipi_dsim_phy_id(id))
		reset = EXYNOS_MIPI_PHY_MRESETN;
	else
		reset = EXYNOS_MIPI_PHY_SRESETN;

	spin_lock(&state->slock);
	reg = readl(addr);
	if (on)
		reg |= reset;
	else
		reg &= ~reset;
	writel(reg, addr);

	/* Clear ENABLE bit only if MRESETN, SRESETN bits are not set. */
	if (on)
		reg |= EXYNOS_MIPI_PHY_ENABLE;
	else if (!(reg & EXYNOS_MIPI_PHY_RESET_MASK))
		reg &= ~EXYNOS_MIPI_PHY_ENABLE;

	writel(reg, addr);
	spin_unlock(&state->slock);
	return 0;
}

#define to_mipi_video_phy(desc) \
	container_of((desc), struct exynos_mipi_video_phy, phys[(desc)->index]);

static int exynos_mipi_video_phy_power_on(struct phy *phy)
{
	struct video_phy_desc *phy_desc = phy_get_drvdata(phy);
	struct exynos_mipi_video_phy *state = to_mipi_video_phy(phy_desc);

	return __set_phy_state(state, phy_desc->index, 1);
}

static int exynos_mipi_video_phy_power_off(struct phy *phy)
{
	struct video_phy_desc *phy_desc = phy_get_drvdata(phy);
	struct exynos_mipi_video_phy *state = to_mipi_video_phy(phy_desc);

	return __set_phy_state(state, phy_desc->index, 0);
}

static struct phy *exynos_mipi_video_phy_xlate(struct device *dev,
					struct of_phandle_args *args)
{
	struct exynos_mipi_video_phy *state = dev_get_drvdata(dev);

	if (WARN_ON(args->args[0] >= EXYNOS_MIPI_PHYS_NUM))
		return ERR_PTR(-ENODEV);

	return state->phys[args->args[0]].phy;
}

static struct phy_ops exynos_mipi_video_phy_ops = {
	.power_on	= exynos_mipi_video_phy_power_on,
	.power_off	= exynos_mipi_video_phy_power_off,
	.owner		= THIS_MODULE,
};

static int exynos_mipi_video_phy_probe(struct platform_device *pdev)
{
	struct exynos_mipi_video_phy *state;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct phy_provider *phy_provider;
	unsigned int i;

	state = devm_kzalloc(dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	state->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(state->regs))
		return PTR_ERR(state->regs);

	dev_set_drvdata(dev, state);
	spin_lock_init(&state->slock);

	for (i = 0; i < EXYNOS_MIPI_PHYS_NUM; i++) {
		struct phy *phy = devm_phy_create(dev, NULL,
					&exynos_mipi_video_phy_ops, NULL);
		if (IS_ERR(phy)) {
			dev_err(dev, "failed to create PHY %d\n", i);
			return PTR_ERR(phy);
		}

		state->phys[i].phy = phy;
		state->phys[i].index = i;
		phy_set_drvdata(phy, &state->phys[i]);
	}

	phy_provider = devm_of_phy_provider_register(dev,
					exynos_mipi_video_phy_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id exynos_mipi_video_phy_of_match[] = {
	{ .compatible = "samsung,s5pv210-mipi-video-phy" },
	{ },
};
MODULE_DEVICE_TABLE(of, exynos_mipi_video_phy_of_match);

static struct platform_driver exynos_mipi_video_phy_driver = {
	.probe	= exynos_mipi_video_phy_probe,
	.driver = {
		.of_match_table	= exynos_mipi_video_phy_of_match,
		.name  = "exynos-mipi-video-phy",
	}
};
module_platform_driver(exynos_mipi_video_phy_driver);

MODULE_DESCRIPTION("Samsung S5P/EXYNOS SoC MIPI CSI-2/DSI PHY driver");
MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_LICENSE("GPL v2");
