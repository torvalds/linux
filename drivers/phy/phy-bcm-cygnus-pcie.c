/*
 * Copyright (C) 2015 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

#define PCIE_CFG_OFFSET         0x00
#define PCIE1_PHY_IDDQ_SHIFT    10
#define PCIE0_PHY_IDDQ_SHIFT    2

enum cygnus_pcie_phy_id {
	CYGNUS_PHY_PCIE0 = 0,
	CYGNUS_PHY_PCIE1,
	MAX_NUM_PHYS,
};

struct cygnus_pcie_phy_core;

/**
 * struct cygnus_pcie_phy - Cygnus PCIe PHY device
 * @core: pointer to the Cygnus PCIe PHY core control
 * @id: internal ID to identify the Cygnus PCIe PHY
 * @phy: pointer to the kernel PHY device
 */
struct cygnus_pcie_phy {
	struct cygnus_pcie_phy_core *core;
	enum cygnus_pcie_phy_id id;
	struct phy *phy;
};

/**
 * struct cygnus_pcie_phy_core - Cygnus PCIe PHY core control
 * @dev: pointer to device
 * @base: base register
 * @lock: mutex to protect access to individual PHYs
 * @phys: pointer to Cygnus PHY device
 */
struct cygnus_pcie_phy_core {
	struct device *dev;
	void __iomem *base;
	struct mutex lock;
	struct cygnus_pcie_phy phys[MAX_NUM_PHYS];
};

static int cygnus_pcie_power_config(struct cygnus_pcie_phy *phy, bool enable)
{
	struct cygnus_pcie_phy_core *core = phy->core;
	unsigned shift;
	u32 val;

	mutex_lock(&core->lock);

	switch (phy->id) {
	case CYGNUS_PHY_PCIE0:
		shift = PCIE0_PHY_IDDQ_SHIFT;
		break;

	case CYGNUS_PHY_PCIE1:
		shift = PCIE1_PHY_IDDQ_SHIFT;
		break;

	default:
		mutex_unlock(&core->lock);
		dev_err(core->dev, "PCIe PHY %d invalid\n", phy->id);
		return -EINVAL;
	}

	if (enable) {
		val = readl(core->base + PCIE_CFG_OFFSET);
		val &= ~BIT(shift);
		writel(val, core->base + PCIE_CFG_OFFSET);
		/*
		 * Wait 50 ms for the PCIe Serdes to stabilize after the analog
		 * front end is brought up
		 */
		msleep(50);
	} else {
		val = readl(core->base + PCIE_CFG_OFFSET);
		val |= BIT(shift);
		writel(val, core->base + PCIE_CFG_OFFSET);
	}

	mutex_unlock(&core->lock);
	dev_dbg(core->dev, "PCIe PHY %d %s\n", phy->id,
		enable ? "enabled" : "disabled");
	return 0;
}

static int cygnus_pcie_phy_power_on(struct phy *p)
{
	struct cygnus_pcie_phy *phy = phy_get_drvdata(p);

	return cygnus_pcie_power_config(phy, true);
}

static int cygnus_pcie_phy_power_off(struct phy *p)
{
	struct cygnus_pcie_phy *phy = phy_get_drvdata(p);

	return cygnus_pcie_power_config(phy, false);
}

static struct phy_ops cygnus_pcie_phy_ops = {
	.power_on = cygnus_pcie_phy_power_on,
	.power_off = cygnus_pcie_phy_power_off,
	.owner = THIS_MODULE,
};

static int cygnus_pcie_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node, *child;
	struct cygnus_pcie_phy_core *core;
	struct phy_provider *provider;
	struct resource *res;
	unsigned cnt = 0;
	int ret;

	if (of_get_child_count(node) == 0) {
		dev_err(dev, "PHY no child node\n");
		return -ENODEV;
	}

	core = devm_kzalloc(dev, sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;

	core->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	core->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(core->base))
		return PTR_ERR(core->base);

	mutex_init(&core->lock);

	for_each_available_child_of_node(node, child) {
		unsigned int id;
		struct cygnus_pcie_phy *p;

		if (of_property_read_u32(child, "reg", &id)) {
			dev_err(dev, "missing reg property for %s\n",
				child->name);
			ret = -EINVAL;
			goto put_child;
		}

		if (id >= MAX_NUM_PHYS) {
			dev_err(dev, "invalid PHY id: %u\n", id);
			ret = -EINVAL;
			goto put_child;
		}

		if (core->phys[id].phy) {
			dev_err(dev, "duplicated PHY id: %u\n", id);
			ret = -EINVAL;
			goto put_child;
		}

		p = &core->phys[id];
		p->phy = devm_phy_create(dev, child, &cygnus_pcie_phy_ops);
		if (IS_ERR(p->phy)) {
			dev_err(dev, "failed to create PHY\n");
			ret = PTR_ERR(p->phy);
			goto put_child;
		}

		p->core = core;
		p->id = id;
		phy_set_drvdata(p->phy, p);
		cnt++;
	}

	dev_set_drvdata(dev, core);

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(provider)) {
		dev_err(dev, "failed to register PHY provider\n");
		return PTR_ERR(provider);
	}

	dev_dbg(dev, "registered %u PCIe PHY(s)\n", cnt);

	return 0;
put_child:
	of_node_put(child);
	return ret;
}

static const struct of_device_id cygnus_pcie_phy_match_table[] = {
	{ .compatible = "brcm,cygnus-pcie-phy" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, cygnus_pcie_phy_match_table);

static struct platform_driver cygnus_pcie_phy_driver = {
	.driver = {
		.name = "cygnus-pcie-phy",
		.of_match_table = cygnus_pcie_phy_match_table,
	},
	.probe = cygnus_pcie_phy_probe,
};
module_platform_driver(cygnus_pcie_phy_driver);

MODULE_AUTHOR("Ray Jui <rjui@broadcom.com>");
MODULE_DESCRIPTION("Broadcom Cygnus PCIe PHY driver");
MODULE_LICENSE("GPL v2");
