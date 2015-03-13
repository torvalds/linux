/*
 * USB cluster support for Armada 375 platform.
 *
 * Copyright (C) 2014 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2 or later. This program is licensed "as is"
 * without any warranty of any kind, whether express or implied.
 *
 * Armada 375 comes with an USB2 host and device controller and an
 * USB3 controller. The USB cluster control register allows to manage
 * common features of both USB controllers.
 */

#include <dt-bindings/phy/phy.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

#define USB2_PHY_CONFIG_DISABLE BIT(0)

struct armada375_cluster_phy {
	struct phy *phy;
	void __iomem *reg;
	bool use_usb3;
	int phy_provided;
};

static int armada375_usb_phy_init(struct phy *phy)
{
	struct armada375_cluster_phy *cluster_phy;
	u32 reg;

	cluster_phy = dev_get_drvdata(phy->dev.parent);
	if (!cluster_phy)
		return -ENODEV;

	reg = readl(cluster_phy->reg);
	if (cluster_phy->use_usb3)
		reg |= USB2_PHY_CONFIG_DISABLE;
	else
		reg &= ~USB2_PHY_CONFIG_DISABLE;
	writel(reg, cluster_phy->reg);

	return 0;
}

static struct phy_ops armada375_usb_phy_ops = {
	.init = armada375_usb_phy_init,
	.owner = THIS_MODULE,
};

/*
 * Only one controller can use this PHY. We shouldn't have the case
 * when two controllers want to use this PHY. But if this case occurs
 * then we provide a phy to the first one and return an error for the
 * next one. This error has also to be an error returned by
 * devm_phy_optional_get() so different from ENODEV for USB2. In the
 * USB3 case it still optional and we use ENODEV.
 */
static struct phy *armada375_usb_phy_xlate(struct device *dev,
					struct of_phandle_args *args)
{
	struct armada375_cluster_phy *cluster_phy = dev_get_drvdata(dev);

	if (!cluster_phy)
		return  ERR_PTR(-ENODEV);

	/*
	 * Either the phy had never been requested and then the first
	 * usb claiming it can get it, or it had already been
	 * requested in this case, we only allow to use it with the
	 * same configuration.
	 */
	if (WARN_ON((cluster_phy->phy_provided != PHY_NONE) &&
			(cluster_phy->phy_provided != args->args[0]))) {
		dev_err(dev, "This PHY has already been provided!\n");
		dev_err(dev, "Check your device tree, only one controller can use it\n.");
		if (args->args[0] == PHY_TYPE_USB2)
			return ERR_PTR(-EBUSY);
		else
			return ERR_PTR(-ENODEV);
	}

	if (args->args[0] == PHY_TYPE_USB2)
		cluster_phy->use_usb3 = false;
	else if (args->args[0] == PHY_TYPE_USB3)
		cluster_phy->use_usb3 = true;
	else {
		dev_err(dev, "Invalid PHY mode\n");
		return ERR_PTR(-ENODEV);
	}

	/* Store which phy mode is used for next test */
	cluster_phy->phy_provided = args->args[0];

	return cluster_phy->phy;
}

static int armada375_usb_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy *phy;
	struct phy_provider *phy_provider;
	void __iomem *usb_cluster_base;
	struct resource *res;
	struct armada375_cluster_phy *cluster_phy;

	cluster_phy = devm_kzalloc(dev, sizeof(*cluster_phy), GFP_KERNEL);
	if (!cluster_phy)
		return  -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	usb_cluster_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(usb_cluster_base))
		return PTR_ERR(usb_cluster_base);

	phy = devm_phy_create(dev, NULL, &armada375_usb_phy_ops);
	if (IS_ERR(phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(phy);
	}

	cluster_phy->phy = phy;
	cluster_phy->reg = usb_cluster_base;

	dev_set_drvdata(dev, cluster_phy);

	phy_provider = devm_of_phy_provider_register(&pdev->dev,
						     armada375_usb_phy_xlate);
	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id of_usb_cluster_table[] = {
	{ .compatible = "marvell,armada-375-usb-cluster", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, of_usb_cluster_table);

static struct platform_driver armada375_usb_phy_driver = {
	.probe	= armada375_usb_phy_probe,
	.driver = {
		.of_match_table	= of_usb_cluster_table,
		.name  = "armada-375-usb-cluster",
		.owner = THIS_MODULE,
	}
};
module_platform_driver(armada375_usb_phy_driver);

MODULE_DESCRIPTION("Armada 375 USB cluster driver");
MODULE_AUTHOR("Gregory CLEMENT <gregory.clement@free-electrons.com>");
MODULE_LICENSE("GPL");
