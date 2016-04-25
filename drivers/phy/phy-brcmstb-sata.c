/*
 * Broadcom SATA3 AHCI Controller PHY Driver
 *
 * Copyright © 2009-2015 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

#define SATA_MDIO_BANK_OFFSET				0x23c
#define SATA_MDIO_REG_OFFSET(ofs)			((ofs) * 4)

#define MAX_PORTS					2

/* Register offset between PHYs in PCB space */
#define SATA_MDIO_REG_28NM_SPACE_SIZE			0x1000

/* The older SATA PHY registers duplicated per port registers within the map,
 * rather than having a separate map per port.
 */
#define SATA_MDIO_REG_40NM_SPACE_SIZE			0x10

enum brcm_sata_phy_version {
	BRCM_SATA_PHY_28NM,
	BRCM_SATA_PHY_40NM,
};

struct brcm_sata_port {
	int portnum;
	struct phy *phy;
	struct brcm_sata_phy *phy_priv;
	bool ssc_en;
};

struct brcm_sata_phy {
	struct device *dev;
	void __iomem *phy_base;
	enum brcm_sata_phy_version version;

	struct brcm_sata_port phys[MAX_PORTS];
};

enum sata_mdio_phy_regs {
	PLL_REG_BANK_0				= 0x50,
	PLL_REG_BANK_0_PLLCONTROL_0		= 0x81,

	TXPMD_REG_BANK				= 0x1a0,
	TXPMD_CONTROL1				= 0x81,
	TXPMD_CONTROL1_TX_SSC_EN_FRC		= BIT(0),
	TXPMD_CONTROL1_TX_SSC_EN_FRC_VAL	= BIT(1),
	TXPMD_TX_FREQ_CTRL_CONTROL1		= 0x82,
	TXPMD_TX_FREQ_CTRL_CONTROL2		= 0x83,
	TXPMD_TX_FREQ_CTRL_CONTROL2_FMIN_MASK	= 0x3ff,
	TXPMD_TX_FREQ_CTRL_CONTROL3		= 0x84,
	TXPMD_TX_FREQ_CTRL_CONTROL3_FMAX_MASK	= 0x3ff,
};

static inline void __iomem *brcm_sata_phy_base(struct brcm_sata_port *port)
{
	struct brcm_sata_phy *priv = port->phy_priv;
	u32 offset = 0;

	if (priv->version == BRCM_SATA_PHY_28NM)
		offset = SATA_MDIO_REG_28NM_SPACE_SIZE;
	else if (priv->version == BRCM_SATA_PHY_40NM)
		offset = SATA_MDIO_REG_40NM_SPACE_SIZE;
	else
		dev_err(priv->dev, "invalid phy version\n");

	return priv->phy_base + (port->portnum * offset);
}

static void brcm_sata_mdio_wr(void __iomem *addr, u32 bank, u32 ofs,
			      u32 msk, u32 value)
{
	u32 tmp;

	writel(bank, addr + SATA_MDIO_BANK_OFFSET);
	tmp = readl(addr + SATA_MDIO_REG_OFFSET(ofs));
	tmp = (tmp & msk) | value;
	writel(tmp, addr + SATA_MDIO_REG_OFFSET(ofs));
}

/* These defaults were characterized by H/W group */
#define FMIN_VAL_DEFAULT	0x3df
#define FMAX_VAL_DEFAULT	0x3df
#define FMAX_VAL_SSC		0x83

static void brcm_sata_cfg_ssc(struct brcm_sata_port *port)
{
	void __iomem *base = brcm_sata_phy_base(port);
	struct brcm_sata_phy *priv = port->phy_priv;
	u32 tmp;

	/* override the TX spread spectrum setting */
	tmp = TXPMD_CONTROL1_TX_SSC_EN_FRC_VAL | TXPMD_CONTROL1_TX_SSC_EN_FRC;
	brcm_sata_mdio_wr(base, TXPMD_REG_BANK, TXPMD_CONTROL1, ~tmp, tmp);

	/* set fixed min freq */
	brcm_sata_mdio_wr(base, TXPMD_REG_BANK, TXPMD_TX_FREQ_CTRL_CONTROL2,
			  ~TXPMD_TX_FREQ_CTRL_CONTROL2_FMIN_MASK,
			  FMIN_VAL_DEFAULT);

	/* set fixed max freq depending on SSC config */
	if (port->ssc_en) {
		dev_info(priv->dev, "enabling SSC on port %d\n", port->portnum);
		tmp = FMAX_VAL_SSC;
	} else {
		tmp = FMAX_VAL_DEFAULT;
	}

	brcm_sata_mdio_wr(base, TXPMD_REG_BANK, TXPMD_TX_FREQ_CTRL_CONTROL3,
			  ~TXPMD_TX_FREQ_CTRL_CONTROL3_FMAX_MASK, tmp);
}

static int brcm_sata_phy_init(struct phy *phy)
{
	struct brcm_sata_port *port = phy_get_drvdata(phy);

	brcm_sata_cfg_ssc(port);

	return 0;
}

static const struct phy_ops phy_ops = {
	.init		= brcm_sata_phy_init,
	.owner		= THIS_MODULE,
};

static const struct of_device_id brcm_sata_phy_of_match[] = {
	{ .compatible	= "brcm,bcm7445-sata-phy",
	  .data = (void *)BRCM_SATA_PHY_28NM },
	{ .compatible	= "brcm,bcm7425-sata-phy",
	  .data = (void *)BRCM_SATA_PHY_40NM },
	{},
};
MODULE_DEVICE_TABLE(of, brcm_sata_phy_of_match);

static int brcm_sata_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *dn = dev->of_node, *child;
	const struct of_device_id *of_id;
	struct brcm_sata_phy *priv;
	struct resource *res;
	struct phy_provider *provider;
	int ret, count = 0;

	if (of_get_child_count(dn) == 0)
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	dev_set_drvdata(dev, priv);
	priv->dev = dev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy");
	priv->phy_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->phy_base))
		return PTR_ERR(priv->phy_base);

	of_id = of_match_node(brcm_sata_phy_of_match, dn);
	if (of_id)
		priv->version = (enum brcm_sata_phy_version)of_id->data;
	else
		priv->version = BRCM_SATA_PHY_28NM;

	for_each_available_child_of_node(dn, child) {
		unsigned int id;
		struct brcm_sata_port *port;

		if (of_property_read_u32(child, "reg", &id)) {
			dev_err(dev, "missing reg property in node %s\n",
					child->name);
			ret = -EINVAL;
			goto put_child;
		}

		if (id >= MAX_PORTS) {
			dev_err(dev, "invalid reg: %u\n", id);
			ret = -EINVAL;
			goto put_child;
		}
		if (priv->phys[id].phy) {
			dev_err(dev, "already registered port %u\n", id);
			ret = -EINVAL;
			goto put_child;
		}

		port = &priv->phys[id];
		port->portnum = id;
		port->phy_priv = priv;
		port->phy = devm_phy_create(dev, child, &phy_ops);
		port->ssc_en = of_property_read_bool(child, "brcm,enable-ssc");
		if (IS_ERR(port->phy)) {
			dev_err(dev, "failed to create PHY\n");
			ret = PTR_ERR(port->phy);
			goto put_child;
		}

		phy_set_drvdata(port->phy, port);
		count++;
	}

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(provider)) {
		dev_err(dev, "could not register PHY provider\n");
		return PTR_ERR(provider);
	}

	dev_info(dev, "registered %d port(s)\n", count);

	return 0;
put_child:
	of_node_put(child);
	return ret;
}

static struct platform_driver brcm_sata_phy_driver = {
	.probe	= brcm_sata_phy_probe,
	.driver	= {
		.of_match_table	= brcm_sata_phy_of_match,
		.name		= "brcmstb-sata-phy",
	}
};
module_platform_driver(brcm_sata_phy_driver);

MODULE_DESCRIPTION("Broadcom STB SATA PHY driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marc Carino");
MODULE_AUTHOR("Brian Norris");
MODULE_ALIAS("platform:phy-brcmstb-sata");
