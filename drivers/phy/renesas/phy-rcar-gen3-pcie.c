// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas R-Car Gen3 PCIe PHY driver
 *
 * Copyright (C) 2018 Cogent Embedded, Inc.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#define PHY_CTRL		0x4000		/* R8A77980 only */

/* PHY control register (PHY_CTRL) */
#define PHY_CTRL_PHY_PWDN	BIT(2)

struct rcar_gen3_phy {
	struct phy *phy;
	spinlock_t lock;
	void __iomem *base;
};

static void rcar_gen3_phy_pcie_modify_reg(struct phy *p, unsigned int reg,
					  u32 clear, u32 set)
{
	struct rcar_gen3_phy *phy = phy_get_drvdata(p);
	void __iomem *base = phy->base;
	unsigned long flags;
	u32 value;

	spin_lock_irqsave(&phy->lock, flags);

	value = readl(base + reg);
	value &= ~clear;
	value |= set;
	writel(value, base + reg);

	spin_unlock_irqrestore(&phy->lock, flags);
}

static int r8a77980_phy_pcie_power_on(struct phy *p)
{
	/* Power on the PCIe PHY */
	rcar_gen3_phy_pcie_modify_reg(p, PHY_CTRL, PHY_CTRL_PHY_PWDN, 0);

	return 0;
}

static int r8a77980_phy_pcie_power_off(struct phy *p)
{
	/* Power off the PCIe PHY */
	rcar_gen3_phy_pcie_modify_reg(p, PHY_CTRL, 0, PHY_CTRL_PHY_PWDN);

	return 0;
}

static const struct phy_ops r8a77980_phy_pcie_ops = {
	.power_on	= r8a77980_phy_pcie_power_on,
	.power_off	= r8a77980_phy_pcie_power_off,
	.owner		= THIS_MODULE,
};

static const struct of_device_id rcar_gen3_phy_pcie_match_table[] = {
	{ .compatible = "renesas,r8a77980-pcie-phy" },
	{ }
};
MODULE_DEVICE_TABLE(of, rcar_gen3_phy_pcie_match_table);

static int rcar_gen3_phy_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *provider;
	struct rcar_gen3_phy *phy;
	void __iomem *base;
	int error;

	if (!dev->of_node) {
		dev_err(dev,
			"This driver must only be instantiated from the device tree\n");
		return -EINVAL;
	}

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	spin_lock_init(&phy->lock);

	phy->base = base;

	/*
	 * devm_phy_create() will call pm_runtime_enable(&phy->dev);
	 * And then, phy-core will manage runtime PM for this device.
	 */
	pm_runtime_enable(dev);

	phy->phy = devm_phy_create(dev, NULL, &r8a77980_phy_pcie_ops);
	if (IS_ERR(phy->phy)) {
		dev_err(dev, "Failed to create PCIe PHY\n");
		error = PTR_ERR(phy->phy);
		goto error;
	}
	phy_set_drvdata(phy->phy, phy);

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(provider)) {
		dev_err(dev, "Failed to register PHY provider\n");
		error = PTR_ERR(provider);
		goto error;
	}

	return 0;

error:
	pm_runtime_disable(dev);

	return error;
}

static int rcar_gen3_phy_pcie_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	return 0;
};

static struct platform_driver rcar_gen3_phy_driver = {
	.driver = {
		.name		= "phy_rcar_gen3_pcie",
		.of_match_table	= rcar_gen3_phy_pcie_match_table,
	},
	.probe	= rcar_gen3_phy_pcie_probe,
	.remove = rcar_gen3_phy_pcie_remove,
};

module_platform_driver(rcar_gen3_phy_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Renesas R-Car Gen3 PCIe PHY");
MODULE_AUTHOR("Sergei Shtylyov <sergei.shtylyov@cogentembedded.com>");
