/*
 * Copyright (C) 2014 Altera Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/stmmac.h>
#include <linux/timer.h>

#include "core.h"
#include "dwmac-socfpga.h"

static void config_tx_buffer(u16 data, void __iomem *base)
{
	writew(data, base + SGMII_ADAPTER_CTRL_REG);
}

static void tse_pcs_reset(void __iomem *base)
{
	int counter = 0;
	u16 val;

	val = readw(base + TSE_PCS_CONTROL_REG);
	val |= TSE_PCS_SW_RST_MASK;
	writew(val, base + TSE_PCS_CONTROL_REG);

	while (counter < TSE_PCS_SW_RESET_TIMEOUT) {
		val = readw(base + TSE_PCS_CONTROL_REG);
		val &= TSE_PCS_SW_RST_MASK;
		if (val == 0)
			break;
		counter++;
		udelay(1);
	}
	if (counter >= TSE_PCS_SW_RESET_TIMEOUT)
		pr_err("PCS could not get out of sw reset\n");
}

static void pcs_link_timer_callback(unsigned long data)
{
	u16 val = 0;

	struct dwmac_plat_priv *bsp_priv = (struct dwmac_plat_priv *)data;
	void __iomem *tse_pcs_base = bsp_priv->tse_pcs_base;
	void __iomem *sgmii_adapter_base = bsp_priv->sgmii_adapter_base;

	val = readw(tse_pcs_base + TSE_PCS_STATUS_REG);
	val &=  TSE_PCS_STATUS_LINK_MASK;

	if (val != 0) {
		pr_debug("Adapter: Link is established\n");
		config_tx_buffer(SGMII_ADAPTER_ENABLE, sgmii_adapter_base);
		return;
	} else {
		mod_timer(&bsp_priv->link_timer, jiffies +
				msecs_to_jiffies(LINK_TIMER));
	}
}

static void auto_nego_timer_callback(unsigned long data)
{
	u16 val = 0;
	u16 speed = 0;
	u16 duplex = 0;

	struct dwmac_plat_priv *bsp_priv = (struct dwmac_plat_priv *)data;
	void __iomem *tse_pcs_base = bsp_priv->tse_pcs_base;
	void __iomem *sgmii_adapter_base = bsp_priv->sgmii_adapter_base;

	val = readw(tse_pcs_base + TSE_PCS_STATUS_REG);
	val &=  TSE_PCS_STATUS_AN_COMPLETED_MASK;

	if (val != 0) {
		pr_debug("Adapter: Auto Negotiation is completed\n");
		val = readw(tse_pcs_base + TSE_PCS_PARTNER_ABILITY_REG);
		speed = val & TSE_PCS_PARTNER_SPEED_MASK;
		duplex = val & TSE_PCS_PARTNER_DUPLEX_MASK;

		if (speed == TSE_PCS_PARTNER_SPEED_10 &&
		    duplex == TSE_PCS_PARTNER_DUPLEX_FULL)
			pr_debug("Adapter: Link Partner is Up - 10/Full\n");
		else if (speed == TSE_PCS_PARTNER_SPEED_100 &&
			duplex == TSE_PCS_PARTNER_DUPLEX_FULL)
			pr_debug("Adapter: Link Partner is Up - 100/Full\n");
		else if (speed == TSE_PCS_PARTNER_SPEED_1000 &&
			duplex == TSE_PCS_PARTNER_DUPLEX_FULL)
			pr_debug("Adapter: Link Partner is Up - 1000/Full\n");
		else if (speed == TSE_PCS_PARTNER_SPEED_10 &&
			duplex == TSE_PCS_PARTNER_DUPLEX_HALF)
			pr_err("Adapter does not support Half Duplex\n");
		else if (speed == TSE_PCS_PARTNER_SPEED_100 &&
			duplex == TSE_PCS_PARTNER_DUPLEX_HALF)
			pr_err("Adapter does not support Half Duplex\n");
		else if (speed == TSE_PCS_PARTNER_SPEED_1000 &&
			duplex == TSE_PCS_PARTNER_DUPLEX_HALF)
			pr_err("Adapter does not support Half Duplex\n");
		else
			pr_err("Adapter: Invalid Partner Speed and Duplex\n");

		config_tx_buffer(SGMII_ADAPTER_ENABLE, sgmii_adapter_base);
		return;
	} else {
		val = readw(tse_pcs_base + TSE_PCS_CONTROL_REG);
		val |= TSE_PCS_CONTROL_RESTART_AN_MASK;
		writew(val, tse_pcs_base + TSE_PCS_CONTROL_REG);

		tse_pcs_reset(tse_pcs_base);

		mod_timer(&bsp_priv->an_timer, jiffies +
				  msecs_to_jiffies(AUTONEGO_TIMER));
	}
}

static void tse_pcs_init(void __iomem *base)
{
	writew(0x0001, base + TSE_PCS_IF_MODE_REG);

	writew(TSE_PCS_SGMII_LINK_TIMER_0, base + TSE_PCS_LINK_TIMER_0_REG);
	writew(TSE_PCS_SGMII_LINK_TIMER_1, base + TSE_PCS_LINK_TIMER_1_REG);

	tse_pcs_reset(base);
}

static void config_emac_splitter_speed(void __iomem *base, unsigned int speed)
{
	u32 val;

	val = readl(base + EMAC_SPLITTER_CTRL_REG);
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
	writel(val, base + EMAC_SPLITTER_CTRL_REG);
}

void adapter_config(void *priv, unsigned int speed)
{
	struct dwmac_plat_priv *bsp_priv = (struct dwmac_plat_priv *)priv;

	void __iomem *splitter_base = bsp_priv->emac_splitter_base;
	void __iomem *tse_pcs_base = bsp_priv->tse_pcs_base;
	void __iomem *sgmii_adapter_base = bsp_priv->sgmii_adapter_base;

	struct platform_device *pdev = bsp_priv->pdev;
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct phy_device *phy_dev = ndev->phydev;

	u32 val;

	if ((splitter_base) && (tse_pcs_base) && (sgmii_adapter_base)) {
		config_tx_buffer(SGMII_ADAPTER_DISABLE, sgmii_adapter_base);
		config_emac_splitter_speed(splitter_base, speed);

		if (phy_dev->autoneg == AUTONEG_ENABLE) {
			val = readw(tse_pcs_base + TSE_PCS_CONTROL_REG);
			val |= TSE_PCS_CONTROL_AN_EN_MASK;
			writew(val, tse_pcs_base + TSE_PCS_CONTROL_REG);

			val = readw(tse_pcs_base + TSE_PCS_IF_MODE_REG);
			val |= TSE_PCS_USE_SGMII_AN_MASK;
			writew(val, tse_pcs_base + TSE_PCS_IF_MODE_REG);

			val = readw(tse_pcs_base + TSE_PCS_CONTROL_REG);
			val |= TSE_PCS_CONTROL_RESTART_AN_MASK;
			writew(val, tse_pcs_base + TSE_PCS_CONTROL_REG);

			tse_pcs_reset(tse_pcs_base);

			setup_timer(&bsp_priv->an_timer,
				    auto_nego_timer_callback,
						(unsigned long)bsp_priv);
			mod_timer(&bsp_priv->an_timer, jiffies +
				  msecs_to_jiffies(AUTONEGO_TIMER));
		} else if (phy_dev->autoneg == AUTONEG_DISABLE) {
			val = readw(tse_pcs_base + TSE_PCS_CONTROL_REG);
			val &= ~TSE_PCS_CONTROL_AN_EN_MASK;
			writew(val, tse_pcs_base + TSE_PCS_CONTROL_REG);

			val = readw(tse_pcs_base + TSE_PCS_IF_MODE_REG);
			val &= ~TSE_PCS_USE_SGMII_AN_MASK;
			writew(val, tse_pcs_base + TSE_PCS_IF_MODE_REG);

			val = readw(tse_pcs_base + TSE_PCS_IF_MODE_REG);
			val &= ~TSE_PCS_SGMII_SPEED_MASK;

			switch (speed) {
			case 1000:
				val |= TSE_PCS_SGMII_SPEED_1000;
				break;
			case 100:
				val |= TSE_PCS_SGMII_SPEED_100;
				break;
			case 10:
				val |= TSE_PCS_SGMII_SPEED_10;
				break;
			default:
				return;
			}
			writew(val, tse_pcs_base + TSE_PCS_IF_MODE_REG);

			tse_pcs_reset(tse_pcs_base);

			setup_timer(&bsp_priv->link_timer,
				    pcs_link_timer_callback,
						(unsigned long)bsp_priv);
			mod_timer(&bsp_priv->link_timer, jiffies +
				  msecs_to_jiffies(LINK_TIMER));
		}
	} else if (splitter_base) {
		config_emac_splitter_speed(splitter_base, speed);
	}
}

int adapter_init(struct platform_device *pdev, int phymode, u32 *val)
{
	struct device_node *np_splitter = NULL;
	struct device_node *np_sgmii = NULL;

	struct resource res_splitter;
	struct resource res_tse_pcs;
	struct resource res_sgmii_adapter;

	struct dwmac_plat_priv *bsp_priv = NULL;
	struct plat_stmmacenet_data *plat = NULL;

	int error = 0;

	bsp_priv = kzalloc(sizeof(*bsp_priv), GFP_KERNEL);
	if (!bsp_priv) {
		pr_err("%s: ERROR: no memory\n", __func__);
		return -ENOMEM;
	}

	plat = dev_get_platdata(&pdev->dev);

	np_splitter = of_parse_phandle(pdev->dev.of_node,
				"altr,emac-splitter", 0);
	if (np_splitter) {
		if (of_address_to_resource(np_splitter, 0, &res_splitter)) {
			pr_err("%s: ERROR: missing emac splitter address\n",
			       __func__);
			error = -EINVAL;
			goto free_bsp_priv;
		}

		bsp_priv->emac_splitter_base =
			devm_ioremap_resource(&pdev->dev, &res_splitter);

		if (IS_ERR(bsp_priv->emac_splitter_base)) {
			pr_err("%s: ERROR: failed to mapping emac splitter\n",
			       __func__);
			error = PTR_ERR(bsp_priv->emac_splitter_base);
			goto free_bsp_priv;
		}

		/* Overwrite val to GMII if splitter core is enabled. The
		 * phymode here is the actual phy mode on phy hardware,
		 * but phy interface from EMAC core is GMII.
		 */
		*val = SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_GMII_MII;
	}

	np_sgmii = of_parse_phandle(pdev->dev.of_node,
				"altr,gmii_to_sgmii_converter", 0);
	if (np_sgmii) {
		if (of_address_to_resource(np_sgmii, 0, &res_splitter)) {
			pr_err("%s: ERROR: missing emac splitter address\n",
			       __func__);
			error = -EINVAL;
			goto free_bsp_priv;
		}
		bsp_priv->emac_splitter_base =
			devm_ioremap_resource(&pdev->dev, &res_splitter);

		if (IS_ERR(bsp_priv->emac_splitter_base)) {
			pr_err("%s: ERROR: failed to mapping emac splitter\n",
			       __func__);
			error = PTR_ERR(bsp_priv->emac_splitter_base);
			goto free_bsp_priv;
		}

		if (of_address_to_resource(np_sgmii, 1,
					   &res_sgmii_adapter)) {
			pr_err("%s: ERROR: missing adapter address\n",
			       __func__);
			error = -EINVAL;
			goto free_bsp_priv;
		}

		bsp_priv->sgmii_adapter_base =
			devm_ioremap_resource(&pdev->dev,
					      &res_sgmii_adapter);

		if (IS_ERR(bsp_priv->sgmii_adapter_base)) {
			pr_err("%s: ERROR: failed to mapping adapter\n",
			       __func__);
			error = PTR_ERR(bsp_priv->sgmii_adapter_base);
			goto free_bsp_priv;
		}

		if (of_address_to_resource(np_sgmii, 2, &res_tse_pcs)) {
			pr_err("%s: ERROR: missing tse pcs address\n",
			       __func__);
			error = -EINVAL;
			goto free_bsp_priv;
		}

		bsp_priv->tse_pcs_base =
			devm_ioremap_resource(&pdev->dev, &res_tse_pcs);

		if (IS_ERR(bsp_priv->tse_pcs_base)) {
			pr_err("%s: ERROR: failed to mapping tse pcs\n",
			       __func__);
			error = PTR_ERR(bsp_priv->tse_pcs_base);
			goto free_bsp_priv;
		}

		if (phymode == PHY_INTERFACE_MODE_SGMII) {
			tse_pcs_init(bsp_priv->tse_pcs_base);
			config_tx_buffer(SGMII_ADAPTER_ENABLE,
					 bsp_priv->sgmii_adapter_base);
		}
	}

	bsp_priv->pdev = pdev;
	plat->bsp_priv = (void *)bsp_priv;
	return 0;
free_bsp_priv:
	kfree(bsp_priv);
	return error;
}
