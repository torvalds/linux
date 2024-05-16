// SPDX-License-Identifier: GPL-2.0
/*
 * Marvell Berlin SATA PHY driver
 *
 * Copyright (C) 2014 Marvell Technology Group Ltd.
 *
 * Antoine Ténart <antoine.tenart@free-electrons.com>
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/io.h>
#include <linux/platform_device.h>

#define HOST_VSA_ADDR		0x0
#define HOST_VSA_DATA		0x4
#define PORT_SCR_CTL		0x2c
#define PORT_VSR_ADDR		0x78
#define PORT_VSR_DATA		0x7c

#define CONTROL_REGISTER	0x0
#define MBUS_SIZE_CONTROL	0x4

#define POWER_DOWN_PHY0			BIT(6)
#define POWER_DOWN_PHY1			BIT(14)
#define MBUS_WRITE_REQUEST_SIZE_128	(BIT(2) << 16)
#define MBUS_READ_REQUEST_SIZE_128	(BIT(2) << 19)

#define BG2_PHY_BASE		0x080
#define BG2Q_PHY_BASE		0x200

/* register 0x01 */
#define REF_FREF_SEL_25		BIT(0)
#define PHY_BERLIN_MODE_SATA	(0x0 << 5)

/* register 0x02 */
#define USE_MAX_PLL_RATE	BIT(12)

/* register 0x23 */
#define DATA_BIT_WIDTH_10	(0x0 << 10)
#define DATA_BIT_WIDTH_20	(0x1 << 10)
#define DATA_BIT_WIDTH_40	(0x2 << 10)

/* register 0x25 */
#define PHY_GEN_MAX_1_5		(0x0 << 10)
#define PHY_GEN_MAX_3_0		(0x1 << 10)
#define PHY_GEN_MAX_6_0		(0x2 << 10)

struct phy_berlin_desc {
	struct phy	*phy;
	u32		power_bit;
	unsigned	index;
};

struct phy_berlin_priv {
	void __iomem		*base;
	spinlock_t		lock;
	struct clk		*clk;
	struct phy_berlin_desc	**phys;
	unsigned		nphys;
	u32			phy_base;
};

static inline void phy_berlin_sata_reg_setbits(void __iomem *ctrl_reg,
			       u32 phy_base, u32 reg, u32 mask, u32 val)
{
	u32 regval;

	/* select register */
	writel(phy_base + reg, ctrl_reg + PORT_VSR_ADDR);

	/* set bits */
	regval = readl(ctrl_reg + PORT_VSR_DATA);
	regval &= ~mask;
	regval |= val;
	writel(regval, ctrl_reg + PORT_VSR_DATA);
}

static int phy_berlin_sata_power_on(struct phy *phy)
{
	struct phy_berlin_desc *desc = phy_get_drvdata(phy);
	struct phy_berlin_priv *priv = dev_get_drvdata(phy->dev.parent);
	void __iomem *ctrl_reg = priv->base + 0x60 + (desc->index * 0x80);
	u32 regval;

	clk_prepare_enable(priv->clk);

	spin_lock(&priv->lock);

	/* Power on PHY */
	writel(CONTROL_REGISTER, priv->base + HOST_VSA_ADDR);
	regval = readl(priv->base + HOST_VSA_DATA);
	regval &= ~desc->power_bit;
	writel(regval, priv->base + HOST_VSA_DATA);

	/* Configure MBus */
	writel(MBUS_SIZE_CONTROL, priv->base + HOST_VSA_ADDR);
	regval = readl(priv->base + HOST_VSA_DATA);
	regval |= MBUS_WRITE_REQUEST_SIZE_128 | MBUS_READ_REQUEST_SIZE_128;
	writel(regval, priv->base + HOST_VSA_DATA);

	/* set PHY mode and ref freq to 25 MHz */
	phy_berlin_sata_reg_setbits(ctrl_reg, priv->phy_base, 0x01,
				    0x00ff,
				    REF_FREF_SEL_25 | PHY_BERLIN_MODE_SATA);

	/* set PHY up to 6 Gbps */
	phy_berlin_sata_reg_setbits(ctrl_reg, priv->phy_base, 0x25,
				    0x0c00, PHY_GEN_MAX_6_0);

	/* set 40 bits width */
	phy_berlin_sata_reg_setbits(ctrl_reg, priv->phy_base, 0x23,
				    0x0c00, DATA_BIT_WIDTH_40);

	/* use max pll rate */
	phy_berlin_sata_reg_setbits(ctrl_reg, priv->phy_base, 0x02,
				    0x0000, USE_MAX_PLL_RATE);

	/* set Gen3 controller speed */
	regval = readl(ctrl_reg + PORT_SCR_CTL);
	regval &= ~GENMASK(7, 4);
	regval |= 0x30;
	writel(regval, ctrl_reg + PORT_SCR_CTL);

	spin_unlock(&priv->lock);

	clk_disable_unprepare(priv->clk);

	return 0;
}

static int phy_berlin_sata_power_off(struct phy *phy)
{
	struct phy_berlin_desc *desc = phy_get_drvdata(phy);
	struct phy_berlin_priv *priv = dev_get_drvdata(phy->dev.parent);
	u32 regval;

	clk_prepare_enable(priv->clk);

	spin_lock(&priv->lock);

	/* Power down PHY */
	writel(CONTROL_REGISTER, priv->base + HOST_VSA_ADDR);
	regval = readl(priv->base + HOST_VSA_DATA);
	regval |= desc->power_bit;
	writel(regval, priv->base + HOST_VSA_DATA);

	spin_unlock(&priv->lock);

	clk_disable_unprepare(priv->clk);

	return 0;
}

static struct phy *phy_berlin_sata_phy_xlate(struct device *dev,
					     struct of_phandle_args *args)
{
	struct phy_berlin_priv *priv = dev_get_drvdata(dev);
	int i;

	if (WARN_ON(args->args[0] >= priv->nphys))
		return ERR_PTR(-ENODEV);

	for (i = 0; i < priv->nphys; i++) {
		if (priv->phys[i]->index == args->args[0])
			break;
	}

	if (i == priv->nphys)
		return ERR_PTR(-ENODEV);

	return priv->phys[i]->phy;
}

static const struct phy_ops phy_berlin_sata_ops = {
	.power_on	= phy_berlin_sata_power_on,
	.power_off	= phy_berlin_sata_power_off,
	.owner		= THIS_MODULE,
};

static u32 phy_berlin_power_down_bits[] = {
	POWER_DOWN_PHY0,
	POWER_DOWN_PHY1,
};

static int phy_berlin_sata_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *child;
	struct phy *phy;
	struct phy_provider *phy_provider;
	struct phy_berlin_priv *priv;
	struct resource *res;
	int ret, i = 0;
	u32 phy_id;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	priv->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!priv->base)
		return -ENOMEM;

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	priv->nphys = of_get_child_count(dev->of_node);
	if (priv->nphys == 0)
		return -ENODEV;

	priv->phys = devm_kcalloc(dev, priv->nphys, sizeof(*priv->phys),
				  GFP_KERNEL);
	if (!priv->phys)
		return -ENOMEM;

	if (of_device_is_compatible(dev->of_node, "marvell,berlin2-sata-phy"))
		priv->phy_base = BG2_PHY_BASE;
	else
		priv->phy_base = BG2Q_PHY_BASE;

	dev_set_drvdata(dev, priv);
	spin_lock_init(&priv->lock);

	for_each_available_child_of_node(dev->of_node, child) {
		struct phy_berlin_desc *phy_desc;

		if (of_property_read_u32(child, "reg", &phy_id)) {
			dev_err(dev, "missing reg property in node %pOFn\n",
				child);
			ret = -EINVAL;
			goto put_child;
		}

		if (phy_id >= ARRAY_SIZE(phy_berlin_power_down_bits)) {
			dev_err(dev, "invalid reg in node %pOFn\n", child);
			ret = -EINVAL;
			goto put_child;
		}

		phy_desc = devm_kzalloc(dev, sizeof(*phy_desc), GFP_KERNEL);
		if (!phy_desc) {
			ret = -ENOMEM;
			goto put_child;
		}

		phy = devm_phy_create(dev, NULL, &phy_berlin_sata_ops);
		if (IS_ERR(phy)) {
			dev_err(dev, "failed to create PHY %d\n", phy_id);
			ret = PTR_ERR(phy);
			goto put_child;
		}

		phy_desc->phy = phy;
		phy_desc->power_bit = phy_berlin_power_down_bits[phy_id];
		phy_desc->index = phy_id;
		phy_set_drvdata(phy, phy_desc);

		priv->phys[i++] = phy_desc;

		/* Make sure the PHY is off */
		phy_berlin_sata_power_off(phy);
	}

	phy_provider =
		devm_of_phy_provider_register(dev, phy_berlin_sata_phy_xlate);
	return PTR_ERR_OR_ZERO(phy_provider);
put_child:
	of_node_put(child);
	return ret;
}

static const struct of_device_id phy_berlin_sata_of_match[] = {
	{ .compatible = "marvell,berlin2-sata-phy" },
	{ .compatible = "marvell,berlin2q-sata-phy" },
	{ },
};
MODULE_DEVICE_TABLE(of, phy_berlin_sata_of_match);

static struct platform_driver phy_berlin_sata_driver = {
	.probe	= phy_berlin_sata_probe,
	.driver	= {
		.name		= "phy-berlin-sata",
		.of_match_table	= phy_berlin_sata_of_match,
	},
};
module_platform_driver(phy_berlin_sata_driver);

MODULE_DESCRIPTION("Marvell Berlin SATA PHY driver");
MODULE_AUTHOR("Antoine Ténart <antoine.tenart@free-electrons.com>");
MODULE_LICENSE("GPL v2");
