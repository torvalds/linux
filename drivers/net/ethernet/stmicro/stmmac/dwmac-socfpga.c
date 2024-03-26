// SPDX-License-Identifier: GPL-2.0-only
/* Copyright Altera Corporation (C) 2014. All rights reserved.
 *
 * Adopted from dwmac-sti.c
 */

#include <linux/mfd/altera-sysmgr.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/regmap.h>
#include <linux/mdio/mdio-regmap.h>
#include <linux/pcs-lynx.h>
#include <linux/reset.h>
#include <linux/stmmac.h>

#include "stmmac.h"
#include "stmmac_platform.h"

#define SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_GMII_MII 0x0
#define SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_RGMII 0x1
#define SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_RMII 0x2
#define SYSMGR_EMACGRP_CTRL_PHYSEL_WIDTH 2
#define SYSMGR_EMACGRP_CTRL_PHYSEL_MASK 0x00000003
#define SYSMGR_EMACGRP_CTRL_PTP_REF_CLK_MASK 0x00000010
#define SYSMGR_GEN10_EMACGRP_CTRL_PTP_REF_CLK_MASK 0x00000100

#define SYSMGR_FPGAGRP_MODULE_REG  0x00000028
#define SYSMGR_FPGAGRP_MODULE_EMAC 0x00000004
#define SYSMGR_FPGAINTF_EMAC_REG	0x00000070
#define SYSMGR_FPGAINTF_EMAC_BIT	0x1

#define EMAC_SPLITTER_CTRL_REG			0x0
#define EMAC_SPLITTER_CTRL_SPEED_MASK		0x3
#define EMAC_SPLITTER_CTRL_SPEED_10		0x2
#define EMAC_SPLITTER_CTRL_SPEED_100		0x3
#define EMAC_SPLITTER_CTRL_SPEED_1000		0x0

#define SGMII_ADAPTER_CTRL_REG		0x00
#define SGMII_ADAPTER_ENABLE		0x0000
#define SGMII_ADAPTER_DISABLE		0x0001

struct socfpga_dwmac;
struct socfpga_dwmac_ops {
	int (*set_phy_mode)(struct socfpga_dwmac *dwmac_priv);
};

struct socfpga_dwmac {
	u32	reg_offset;
	u32	reg_shift;
	struct	device *dev;
	struct regmap *sys_mgr_base_addr;
	struct reset_control *stmmac_rst;
	struct reset_control *stmmac_ocp_rst;
	void __iomem *splitter_base;
	void __iomem *tse_pcs_base;
	void __iomem *sgmii_adapter_base;
	bool f2h_ptp_ref_clk;
	const struct socfpga_dwmac_ops *ops;
	struct mdio_device *pcs_mdiodev;
};

static void socfpga_dwmac_fix_mac_speed(void *priv, unsigned int speed, unsigned int mode)
{
	struct socfpga_dwmac *dwmac = (struct socfpga_dwmac *)priv;
	void __iomem *splitter_base = dwmac->splitter_base;
	void __iomem *sgmii_adapter_base = dwmac->sgmii_adapter_base;
	struct device *dev = dwmac->dev;
	struct net_device *ndev = dev_get_drvdata(dev);
	struct phy_device *phy_dev = ndev->phydev;
	u32 val;

	if (sgmii_adapter_base)
		writew(SGMII_ADAPTER_DISABLE,
		       sgmii_adapter_base + SGMII_ADAPTER_CTRL_REG);

	if (splitter_base) {
		val = readl(splitter_base + EMAC_SPLITTER_CTRL_REG);
		val &= ~EMAC_SPLITTER_CTRL_SPEED_MASK;

		switch (speed) {
		case 1000:
			val |= EMAC_SPLITTER_CTRL_SPEED_1000;
			break;
		case 100:
			val |= EMAC_SPLITTER_CTRL_SPEED_100;
			break;
		case 10:
			val |= EMAC_SPLITTER_CTRL_SPEED_10;
			break;
		default:
			return;
		}
		writel(val, splitter_base + EMAC_SPLITTER_CTRL_REG);
	}

	if (phy_dev && sgmii_adapter_base)
		writew(SGMII_ADAPTER_ENABLE,
		       sgmii_adapter_base + SGMII_ADAPTER_CTRL_REG);
}

static int socfpga_dwmac_parse_data(struct socfpga_dwmac *dwmac, struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct regmap *sys_mgr_base_addr;
	u32 reg_offset, reg_shift;
	int ret, index;
	struct device_node *np_splitter = NULL;
	struct device_node *np_sgmii_adapter = NULL;
	struct resource res_splitter;
	struct resource res_tse_pcs;
	struct resource res_sgmii_adapter;

	sys_mgr_base_addr =
		altr_sysmgr_regmap_lookup_by_phandle(np, "altr,sysmgr-syscon");
	if (IS_ERR(sys_mgr_base_addr)) {
		dev_info(dev, "No sysmgr-syscon node found\n");
		return PTR_ERR(sys_mgr_base_addr);
	}

	ret = of_property_read_u32_index(np, "altr,sysmgr-syscon", 1, &reg_offset);
	if (ret) {
		dev_info(dev, "Could not read reg_offset from sysmgr-syscon!\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_index(np, "altr,sysmgr-syscon", 2, &reg_shift);
	if (ret) {
		dev_info(dev, "Could not read reg_shift from sysmgr-syscon!\n");
		return -EINVAL;
	}

	dwmac->f2h_ptp_ref_clk = of_property_read_bool(np, "altr,f2h_ptp_ref_clk");

	np_splitter = of_parse_phandle(np, "altr,emac-splitter", 0);
	if (np_splitter) {
		ret = of_address_to_resource(np_splitter, 0, &res_splitter);
		of_node_put(np_splitter);
		if (ret) {
			dev_info(dev, "Missing emac splitter address\n");
			return -EINVAL;
		}

		dwmac->splitter_base = devm_ioremap_resource(dev, &res_splitter);
		if (IS_ERR(dwmac->splitter_base)) {
			dev_info(dev, "Failed to mapping emac splitter\n");
			return PTR_ERR(dwmac->splitter_base);
		}
	}

	np_sgmii_adapter = of_parse_phandle(np,
					    "altr,gmii-to-sgmii-converter", 0);
	if (np_sgmii_adapter) {
		index = of_property_match_string(np_sgmii_adapter, "reg-names",
						 "hps_emac_interface_splitter_avalon_slave");

		if (index >= 0) {
			if (of_address_to_resource(np_sgmii_adapter, index,
						   &res_splitter)) {
				dev_err(dev,
					"%s: ERROR: missing emac splitter address\n",
					__func__);
				ret = -EINVAL;
				goto err_node_put;
			}

			dwmac->splitter_base =
			    devm_ioremap_resource(dev, &res_splitter);

			if (IS_ERR(dwmac->splitter_base)) {
				ret = PTR_ERR(dwmac->splitter_base);
				goto err_node_put;
			}
		}

		index = of_property_match_string(np_sgmii_adapter, "reg-names",
						 "gmii_to_sgmii_adapter_avalon_slave");

		if (index >= 0) {
			if (of_address_to_resource(np_sgmii_adapter, index,
						   &res_sgmii_adapter)) {
				dev_err(dev,
					"%s: ERROR: failed mapping adapter\n",
					__func__);
				ret = -EINVAL;
				goto err_node_put;
			}

			dwmac->sgmii_adapter_base =
			    devm_ioremap_resource(dev, &res_sgmii_adapter);

			if (IS_ERR(dwmac->sgmii_adapter_base)) {
				ret = PTR_ERR(dwmac->sgmii_adapter_base);
				goto err_node_put;
			}
		}

		index = of_property_match_string(np_sgmii_adapter, "reg-names",
						 "eth_tse_control_port");

		if (index >= 0) {
			if (of_address_to_resource(np_sgmii_adapter, index,
						   &res_tse_pcs)) {
				dev_err(dev,
					"%s: ERROR: failed mapping tse control port\n",
					__func__);
				ret = -EINVAL;
				goto err_node_put;
			}

			dwmac->tse_pcs_base =
			    devm_ioremap_resource(dev, &res_tse_pcs);

			if (IS_ERR(dwmac->tse_pcs_base)) {
				ret = PTR_ERR(dwmac->tse_pcs_base);
				goto err_node_put;
			}
		}
	}
	dwmac->reg_offset = reg_offset;
	dwmac->reg_shift = reg_shift;
	dwmac->sys_mgr_base_addr = sys_mgr_base_addr;
	dwmac->dev = dev;
	of_node_put(np_sgmii_adapter);

	return 0;

err_node_put:
	of_node_put(np_sgmii_adapter);
	return ret;
}

static int socfpga_get_plat_phymode(struct socfpga_dwmac *dwmac)
{
	struct net_device *ndev = dev_get_drvdata(dwmac->dev);
	struct stmmac_priv *priv = netdev_priv(ndev);

	return priv->plat->mac_interface;
}

static void socfpga_sgmii_config(struct socfpga_dwmac *dwmac, bool enable)
{
	u16 val = enable ? SGMII_ADAPTER_ENABLE : SGMII_ADAPTER_DISABLE;

	writew(val, dwmac->sgmii_adapter_base + SGMII_ADAPTER_CTRL_REG);
}

static int socfpga_set_phy_mode_common(int phymode, u32 *val)
{
	switch (phymode) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		*val = SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_RGMII;
		break;
	case PHY_INTERFACE_MODE_MII:
	case PHY_INTERFACE_MODE_GMII:
	case PHY_INTERFACE_MODE_SGMII:
		*val = SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_GMII_MII;
		break;
	case PHY_INTERFACE_MODE_RMII:
		*val = SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_RMII;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int socfpga_gen5_set_phy_mode(struct socfpga_dwmac *dwmac)
{
	struct regmap *sys_mgr_base_addr = dwmac->sys_mgr_base_addr;
	int phymode = socfpga_get_plat_phymode(dwmac);
	u32 reg_offset = dwmac->reg_offset;
	u32 reg_shift = dwmac->reg_shift;
	u32 ctrl, val, module;

	if (socfpga_set_phy_mode_common(phymode, &val)) {
		dev_err(dwmac->dev, "bad phy mode %d\n", phymode);
		return -EINVAL;
	}

	/* Overwrite val to GMII if splitter core is enabled. The phymode here
	 * is the actual phy mode on phy hardware, but phy interface from
	 * EMAC core is GMII.
	 */
	if (dwmac->splitter_base)
		val = SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_GMII_MII;

	/* Assert reset to the enet controller before changing the phy mode */
	reset_control_assert(dwmac->stmmac_ocp_rst);
	reset_control_assert(dwmac->stmmac_rst);

	regmap_read(sys_mgr_base_addr, reg_offset, &ctrl);
	ctrl &= ~(SYSMGR_EMACGRP_CTRL_PHYSEL_MASK << reg_shift);
	ctrl |= val << reg_shift;

	if (dwmac->f2h_ptp_ref_clk ||
	    phymode == PHY_INTERFACE_MODE_MII ||
	    phymode == PHY_INTERFACE_MODE_GMII ||
	    phymode == PHY_INTERFACE_MODE_SGMII) {
		regmap_read(sys_mgr_base_addr, SYSMGR_FPGAGRP_MODULE_REG,
			    &module);
		module |= (SYSMGR_FPGAGRP_MODULE_EMAC << (reg_shift / 2));
		regmap_write(sys_mgr_base_addr, SYSMGR_FPGAGRP_MODULE_REG,
			     module);
	}

	if (dwmac->f2h_ptp_ref_clk)
		ctrl |= SYSMGR_EMACGRP_CTRL_PTP_REF_CLK_MASK << (reg_shift / 2);
	else
		ctrl &= ~(SYSMGR_EMACGRP_CTRL_PTP_REF_CLK_MASK <<
			  (reg_shift / 2));

	regmap_write(sys_mgr_base_addr, reg_offset, ctrl);

	/* Deassert reset for the phy configuration to be sampled by
	 * the enet controller, and operation to start in requested mode
	 */
	reset_control_deassert(dwmac->stmmac_ocp_rst);
	reset_control_deassert(dwmac->stmmac_rst);
	if (phymode == PHY_INTERFACE_MODE_SGMII)
		socfpga_sgmii_config(dwmac, true);

	return 0;
}

static int socfpga_gen10_set_phy_mode(struct socfpga_dwmac *dwmac)
{
	struct regmap *sys_mgr_base_addr = dwmac->sys_mgr_base_addr;
	int phymode = socfpga_get_plat_phymode(dwmac);
	u32 reg_offset = dwmac->reg_offset;
	u32 reg_shift = dwmac->reg_shift;
	u32 ctrl, val, module;

	if (socfpga_set_phy_mode_common(phymode, &val))
		return -EINVAL;

	/* Overwrite val to GMII if splitter core is enabled. The phymode here
	 * is the actual phy mode on phy hardware, but phy interface from
	 * EMAC core is GMII.
	 */
	if (dwmac->splitter_base)
		val = SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_GMII_MII;

	/* Assert reset to the enet controller before changing the phy mode */
	reset_control_assert(dwmac->stmmac_ocp_rst);
	reset_control_assert(dwmac->stmmac_rst);

	regmap_read(sys_mgr_base_addr, reg_offset, &ctrl);
	ctrl &= ~(SYSMGR_EMACGRP_CTRL_PHYSEL_MASK);
	ctrl |= val;

	if (dwmac->f2h_ptp_ref_clk ||
	    phymode == PHY_INTERFACE_MODE_MII ||
	    phymode == PHY_INTERFACE_MODE_GMII ||
	    phymode == PHY_INTERFACE_MODE_SGMII) {
		ctrl |= SYSMGR_GEN10_EMACGRP_CTRL_PTP_REF_CLK_MASK;
		regmap_read(sys_mgr_base_addr, SYSMGR_FPGAINTF_EMAC_REG,
			    &module);
		module |= (SYSMGR_FPGAINTF_EMAC_BIT << reg_shift);
		regmap_write(sys_mgr_base_addr, SYSMGR_FPGAINTF_EMAC_REG,
			     module);
	} else {
		ctrl &= ~SYSMGR_GEN10_EMACGRP_CTRL_PTP_REF_CLK_MASK;
	}

	regmap_write(sys_mgr_base_addr, reg_offset, ctrl);

	/* Deassert reset for the phy configuration to be sampled by
	 * the enet controller, and operation to start in requested mode
	 */
	reset_control_deassert(dwmac->stmmac_ocp_rst);
	reset_control_deassert(dwmac->stmmac_rst);
	if (phymode == PHY_INTERFACE_MODE_SGMII)
		socfpga_sgmii_config(dwmac, true);
	return 0;
}

static int socfpga_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct device		*dev = &pdev->dev;
	int			ret;
	struct socfpga_dwmac	*dwmac;
	struct net_device	*ndev;
	struct stmmac_priv	*stpriv;
	const struct socfpga_dwmac_ops *ops;

	ops = device_get_match_data(&pdev->dev);
	if (!ops) {
		dev_err(&pdev->dev, "no of match data provided\n");
		return -EINVAL;
	}

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	dwmac = devm_kzalloc(dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac)
		return -ENOMEM;

	dwmac->stmmac_ocp_rst = devm_reset_control_get_optional(dev, "stmmaceth-ocp");
	if (IS_ERR(dwmac->stmmac_ocp_rst)) {
		ret = PTR_ERR(dwmac->stmmac_ocp_rst);
		dev_err(dev, "error getting reset control of ocp %d\n", ret);
		return ret;
	}

	reset_control_deassert(dwmac->stmmac_ocp_rst);

	ret = socfpga_dwmac_parse_data(dwmac, dev);
	if (ret) {
		dev_err(dev, "Unable to parse OF data\n");
		return ret;
	}

	dwmac->ops = ops;
	plat_dat->bsp_priv = dwmac;
	plat_dat->fix_mac_speed = socfpga_dwmac_fix_mac_speed;

	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret)
		return ret;

	ndev = platform_get_drvdata(pdev);
	stpriv = netdev_priv(ndev);

	/* The socfpga driver needs to control the stmmac reset to set the phy
	 * mode. Create a copy of the core reset handle so it can be used by
	 * the driver later.
	 */
	dwmac->stmmac_rst = stpriv->plat->stmmac_rst;

	ret = ops->set_phy_mode(dwmac);
	if (ret)
		goto err_dvr_remove;

	/* Create a regmap for the PCS so that it can be used by the PCS driver,
	 * if we have such a PCS
	 */
	if (dwmac->tse_pcs_base) {
		struct regmap_config pcs_regmap_cfg;
		struct mdio_regmap_config mrc;
		struct regmap *pcs_regmap;
		struct mii_bus *pcs_bus;

		memset(&pcs_regmap_cfg, 0, sizeof(pcs_regmap_cfg));
		memset(&mrc, 0, sizeof(mrc));

		pcs_regmap_cfg.reg_bits = 16;
		pcs_regmap_cfg.val_bits = 16;
		pcs_regmap_cfg.reg_shift = REGMAP_UPSHIFT(1);

		pcs_regmap = devm_regmap_init_mmio(&pdev->dev, dwmac->tse_pcs_base,
						   &pcs_regmap_cfg);
		if (IS_ERR(pcs_regmap)) {
			ret = PTR_ERR(pcs_regmap);
			goto err_dvr_remove;
		}

		mrc.regmap = pcs_regmap;
		mrc.parent = &pdev->dev;
		mrc.valid_addr = 0x0;
		mrc.autoscan = false;

		snprintf(mrc.name, MII_BUS_ID_SIZE, "%s-pcs-mii", ndev->name);
		pcs_bus = devm_mdio_regmap_register(&pdev->dev, &mrc);
		if (IS_ERR(pcs_bus)) {
			ret = PTR_ERR(pcs_bus);
			goto err_dvr_remove;
		}

		stpriv->hw->lynx_pcs = lynx_pcs_create_mdiodev(pcs_bus, 0);
		if (IS_ERR(stpriv->hw->lynx_pcs)) {
			ret = PTR_ERR(stpriv->hw->lynx_pcs);
			goto err_dvr_remove;
		}
	}

	return 0;

err_dvr_remove:
	stmmac_dvr_remove(&pdev->dev);

	return ret;
}

static void socfpga_dwmac_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct phylink_pcs *pcs = priv->hw->lynx_pcs;

	stmmac_pltfr_remove(pdev);

	lynx_pcs_destroy(pcs);
}

#ifdef CONFIG_PM_SLEEP
static int socfpga_dwmac_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct socfpga_dwmac *dwmac_priv = get_stmmac_bsp_priv(dev);

	dwmac_priv->ops->set_phy_mode(priv->plat->bsp_priv);

	/* Before the enet controller is suspended, the phy is suspended.
	 * This causes the phy clock to be gated. The enet controller is
	 * resumed before the phy, so the clock is still gated "off" when
	 * the enet controller is resumed. This code makes sure the phy
	 * is "resumed" before reinitializing the enet controller since
	 * the enet controller depends on an active phy clock to complete
	 * a DMA reset. A DMA reset will "time out" if executed
	 * with no phy clock input on the Synopsys enet controller.
	 * Verified through Synopsys Case #8000711656.
	 *
	 * Note that the phy clock is also gated when the phy is isolated.
	 * Phy "suspend" and "isolate" controls are located in phy basic
	 * control register 0, and can be modified by the phy driver
	 * framework.
	 */
	if (ndev->phydev)
		phy_resume(ndev->phydev);

	return stmmac_resume(dev);
}
#endif /* CONFIG_PM_SLEEP */

static int __maybe_unused socfpga_dwmac_runtime_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);

	stmmac_bus_clks_config(priv, false);

	return 0;
}

static int __maybe_unused socfpga_dwmac_runtime_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);

	return stmmac_bus_clks_config(priv, true);
}

static const struct dev_pm_ops socfpga_dwmac_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(stmmac_suspend, socfpga_dwmac_resume)
	SET_RUNTIME_PM_OPS(socfpga_dwmac_runtime_suspend, socfpga_dwmac_runtime_resume, NULL)
};

static const struct socfpga_dwmac_ops socfpga_gen5_ops = {
	.set_phy_mode = socfpga_gen5_set_phy_mode,
};

static const struct socfpga_dwmac_ops socfpga_gen10_ops = {
	.set_phy_mode = socfpga_gen10_set_phy_mode,
};

static const struct of_device_id socfpga_dwmac_match[] = {
	{ .compatible = "altr,socfpga-stmmac", .data = &socfpga_gen5_ops },
	{ .compatible = "altr,socfpga-stmmac-a10-s10", .data = &socfpga_gen10_ops },
	{ }
};
MODULE_DEVICE_TABLE(of, socfpga_dwmac_match);

static struct platform_driver socfpga_dwmac_driver = {
	.probe  = socfpga_dwmac_probe,
	.remove_new = socfpga_dwmac_remove,
	.driver = {
		.name           = "socfpga-dwmac",
		.pm		= &socfpga_dwmac_pm_ops,
		.of_match_table = socfpga_dwmac_match,
	},
};
module_platform_driver(socfpga_dwmac_driver);

MODULE_DESCRIPTION("Altera SOC DWMAC Specific Glue layer");
MODULE_LICENSE("GPL v2");
