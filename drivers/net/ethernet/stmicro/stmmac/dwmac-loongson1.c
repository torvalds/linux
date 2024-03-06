// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Loongson-1 DWMAC glue layer
 *
 * Copyright (C) 2011-2023 Keguang Zhang <keguang.zhang@gmail.com>
 */

#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "stmmac.h"
#include "stmmac_platform.h"

#define LS1B_GMAC0_BASE		(0x1fe10000)
#define LS1B_GMAC1_BASE		(0x1fe20000)

/* Loongson-1 SYSCON Registers */
#define LS1X_SYSCON0		(0x0)
#define LS1X_SYSCON1		(0x4)

/* Loongson-1B SYSCON Register Bits */
#define GMAC1_USE_UART1		BIT(4)
#define GMAC1_USE_UART0		BIT(3)

#define GMAC1_SHUT		BIT(13)
#define GMAC0_SHUT		BIT(12)

#define GMAC1_USE_TXCLK		BIT(3)
#define GMAC0_USE_TXCLK		BIT(2)
#define GMAC1_USE_PWM23		BIT(1)
#define GMAC0_USE_PWM01		BIT(0)

/* Loongson-1C SYSCON Register Bits */
#define GMAC_SHUT		BIT(6)

#define PHY_INTF_SELI		GENMASK(30, 28)
#define PHY_INTF_MII		FIELD_PREP(PHY_INTF_SELI, 0)
#define PHY_INTF_RMII		FIELD_PREP(PHY_INTF_SELI, 4)

struct ls1x_dwmac {
	struct plat_stmmacenet_data *plat_dat;
	struct regmap *regmap;
};

static int ls1b_dwmac_syscon_init(struct platform_device *pdev, void *priv)
{
	struct ls1x_dwmac *dwmac = priv;
	struct plat_stmmacenet_data *plat = dwmac->plat_dat;
	struct regmap *regmap = dwmac->regmap;
	struct resource *res;
	unsigned long reg_base;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Could not get IO_MEM resources\n");
		return -EINVAL;
	}
	reg_base = (unsigned long)res->start;

	if (reg_base == LS1B_GMAC0_BASE) {
		switch (plat->phy_interface) {
		case PHY_INTERFACE_MODE_RGMII_ID:
			regmap_update_bits(regmap, LS1X_SYSCON0,
					   GMAC0_USE_TXCLK | GMAC0_USE_PWM01,
					   0);
			break;
		case PHY_INTERFACE_MODE_MII:
			regmap_update_bits(regmap, LS1X_SYSCON0,
					   GMAC0_USE_TXCLK | GMAC0_USE_PWM01,
					   GMAC0_USE_TXCLK | GMAC0_USE_PWM01);
			break;
		default:
			dev_err(&pdev->dev, "Unsupported PHY mode %u\n",
				plat->phy_interface);
			return -EOPNOTSUPP;
		}

		regmap_update_bits(regmap, LS1X_SYSCON0, GMAC0_SHUT, 0);
	} else if (reg_base == LS1B_GMAC1_BASE) {
		regmap_update_bits(regmap, LS1X_SYSCON0,
				   GMAC1_USE_UART1 | GMAC1_USE_UART0,
				   GMAC1_USE_UART1 | GMAC1_USE_UART0);

		switch (plat->phy_interface) {
		case PHY_INTERFACE_MODE_RGMII_ID:
			regmap_update_bits(regmap, LS1X_SYSCON1,
					   GMAC1_USE_TXCLK | GMAC1_USE_PWM23,
					   0);

			break;
		case PHY_INTERFACE_MODE_MII:
			regmap_update_bits(regmap, LS1X_SYSCON1,
					   GMAC1_USE_TXCLK | GMAC1_USE_PWM23,
					   GMAC1_USE_TXCLK | GMAC1_USE_PWM23);
			break;
		default:
			dev_err(&pdev->dev, "Unsupported PHY mode %u\n",
				plat->phy_interface);
			return -EOPNOTSUPP;
		}

		regmap_update_bits(regmap, LS1X_SYSCON1, GMAC1_SHUT, 0);
	} else {
		dev_err(&pdev->dev, "Invalid Ethernet MAC base address %lx",
			reg_base);
		return -EINVAL;
	}

	return 0;
}

static int ls1c_dwmac_syscon_init(struct platform_device *pdev, void *priv)
{
	struct ls1x_dwmac *dwmac = priv;
	struct plat_stmmacenet_data *plat = dwmac->plat_dat;
	struct regmap *regmap = dwmac->regmap;

	switch (plat->phy_interface) {
	case PHY_INTERFACE_MODE_MII:
		regmap_update_bits(regmap, LS1X_SYSCON1, PHY_INTF_SELI,
				   PHY_INTF_MII);
		break;
	case PHY_INTERFACE_MODE_RMII:
		regmap_update_bits(regmap, LS1X_SYSCON1, PHY_INTF_SELI,
				   PHY_INTF_RMII);
		break;
	default:
		dev_err(&pdev->dev, "Unsupported PHY-mode %u\n",
			plat->phy_interface);
		return -EOPNOTSUPP;
	}

	regmap_update_bits(regmap, LS1X_SYSCON0, GMAC0_SHUT, 0);

	return 0;
}

static int ls1x_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct regmap *regmap;
	struct ls1x_dwmac *dwmac;
	int (*init)(struct platform_device *pdev, void *priv);
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	/* Probe syscon */
	regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						 "loongson,ls1-syscon");
	if (IS_ERR(regmap))
		return dev_err_probe(&pdev->dev, PTR_ERR(regmap),
				     "Unable to find syscon\n");

	init = of_device_get_match_data(&pdev->dev);
	if (!init) {
		dev_err(&pdev->dev, "No of match data provided\n");
		return -EINVAL;
	}

	dwmac = devm_kzalloc(&pdev->dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac)
		return -ENOMEM;

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return dev_err_probe(&pdev->dev, PTR_ERR(plat_dat),
				     "dt configuration failed\n");

	plat_dat->bsp_priv = dwmac;
	plat_dat->init = init;
	dwmac->plat_dat = plat_dat;
	dwmac->regmap = regmap;

	return devm_stmmac_pltfr_probe(pdev, plat_dat, &stmmac_res);
}

static const struct of_device_id ls1x_dwmac_match[] = {
	{
		.compatible = "loongson,ls1b-gmac",
		.data = &ls1b_dwmac_syscon_init,
	},
	{
		.compatible = "loongson,ls1c-emac",
		.data = &ls1c_dwmac_syscon_init,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, ls1x_dwmac_match);

static struct platform_driver ls1x_dwmac_driver = {
	.probe = ls1x_dwmac_probe,
	.driver = {
		.name = "loongson1-dwmac",
		.of_match_table = ls1x_dwmac_match,
	},
};
module_platform_driver(ls1x_dwmac_driver);

MODULE_AUTHOR("Keguang Zhang <keguang.zhang@gmail.com>");
MODULE_DESCRIPTION("Loongson-1 DWMAC glue layer");
MODULE_LICENSE("GPL");
