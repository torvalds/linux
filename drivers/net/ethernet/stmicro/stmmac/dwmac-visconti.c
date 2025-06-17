// SPDX-License-Identifier: GPL-2.0
/* Toshiba Visconti Ethernet Support
 *
 * (C) Copyright 2020 TOSHIBA CORPORATION
 * (C) Copyright 2020 Toshiba Electronic Devices & Storage Corporation
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/of_net.h>
#include <linux/stmmac.h>

#include "stmmac_platform.h"
#include "dwmac4.h"

#define REG_ETHER_CONTROL	0x52D4
#define ETHER_ETH_CONTROL_RESET BIT(17)

#define REG_ETHER_CLOCK_SEL	0x52D0
#define ETHER_CLK_SEL_TX_CLK_EN BIT(0)
#define ETHER_CLK_SEL_RX_CLK_EN BIT(1)
#define ETHER_CLK_SEL_RMII_CLK_EN BIT(2)
#define ETHER_CLK_SEL_RMII_CLK_RST BIT(3)
#define ETHER_CLK_SEL_DIV_SEL_2 BIT(4)
#define ETHER_CLK_SEL_DIV_SEL_20 0
#define ETHER_CLK_SEL_FREQ_SEL_125M	(BIT(9) | BIT(8))
#define ETHER_CLK_SEL_FREQ_SEL_50M	BIT(9)
#define ETHER_CLK_SEL_FREQ_SEL_25M	BIT(8)
#define ETHER_CLK_SEL_FREQ_SEL_2P5M	0
#define ETHER_CLK_SEL_TX_CLK_EXT_SEL_IN 0
#define ETHER_CLK_SEL_TX_CLK_EXT_SEL_TXC BIT(10)
#define ETHER_CLK_SEL_TX_CLK_EXT_SEL_DIV BIT(11)
#define ETHER_CLK_SEL_RX_CLK_EXT_SEL_IN  0
#define ETHER_CLK_SEL_RX_CLK_EXT_SEL_RXC BIT(12)
#define ETHER_CLK_SEL_RX_CLK_EXT_SEL_DIV BIT(13)
#define ETHER_CLK_SEL_TX_CLK_O_TX_I	 0
#define ETHER_CLK_SEL_TX_CLK_O_RMII_I	 BIT(14)
#define ETHER_CLK_SEL_TX_O_E_N_IN	 BIT(15)
#define ETHER_CLK_SEL_RMII_CLK_SEL_IN	 0
#define ETHER_CLK_SEL_RMII_CLK_SEL_RX_C	 BIT(16)

#define ETHER_CLK_SEL_RX_TX_CLK_EN (ETHER_CLK_SEL_RX_CLK_EN | ETHER_CLK_SEL_TX_CLK_EN)

#define ETHER_CONFIG_INTF_MII 0
#define ETHER_CONFIG_INTF_RGMII BIT(0)
#define ETHER_CONFIG_INTF_RMII BIT(2)

struct visconti_eth {
	void __iomem *reg;
	struct clk *phy_ref_clk;
	struct device *dev;
};

static int visconti_eth_set_clk_tx_rate(void *bsp_priv, struct clk *clk_tx_i,
					phy_interface_t interface, int speed)
{
	struct visconti_eth *dwmac = bsp_priv;
	unsigned long clk_sel, val;

	if (phy_interface_mode_is_rgmii(interface)) {
		switch (speed) {
		case SPEED_1000:
			clk_sel = ETHER_CLK_SEL_FREQ_SEL_125M;
			break;

		case SPEED_100:
			clk_sel = ETHER_CLK_SEL_FREQ_SEL_25M;
			break;

		case SPEED_10:
			clk_sel = ETHER_CLK_SEL_FREQ_SEL_2P5M;
			break;

		default:
			return -EINVAL;
		}

		/* Stop internal clock */
		val = readl(dwmac->reg + REG_ETHER_CLOCK_SEL);
		val &= ~(ETHER_CLK_SEL_RMII_CLK_EN |
			 ETHER_CLK_SEL_RX_TX_CLK_EN);
		val |= ETHER_CLK_SEL_TX_O_E_N_IN;
		writel(val, dwmac->reg + REG_ETHER_CLOCK_SEL);

		/* Set Clock-Mux, Start clock, Set TX_O direction */
		val = clk_sel | ETHER_CLK_SEL_RX_CLK_EXT_SEL_RXC;
		writel(val, dwmac->reg + REG_ETHER_CLOCK_SEL);

		val |= ETHER_CLK_SEL_RX_TX_CLK_EN;
		writel(val, dwmac->reg + REG_ETHER_CLOCK_SEL);

		val &= ~ETHER_CLK_SEL_TX_O_E_N_IN;
		writel(val, dwmac->reg + REG_ETHER_CLOCK_SEL);
	} else if (interface == PHY_INTERFACE_MODE_RMII) {
		switch (speed) {
		case SPEED_100:
			clk_sel = ETHER_CLK_SEL_DIV_SEL_2;
			break;

		case SPEED_10:
			clk_sel = ETHER_CLK_SEL_DIV_SEL_20;
			break;

		default:
			return -EINVAL;
		}

		/* Stop internal clock */
		val = readl(dwmac->reg + REG_ETHER_CLOCK_SEL);
		val &= ~(ETHER_CLK_SEL_RMII_CLK_EN |
			 ETHER_CLK_SEL_RX_TX_CLK_EN);
		val |= ETHER_CLK_SEL_TX_O_E_N_IN;
		writel(val, dwmac->reg + REG_ETHER_CLOCK_SEL);

		/* Set Clock-Mux, Start clock, Set TX_O direction */
		val = clk_sel | ETHER_CLK_SEL_RX_CLK_EXT_SEL_DIV |
		      ETHER_CLK_SEL_TX_CLK_EXT_SEL_DIV |
		      ETHER_CLK_SEL_TX_O_E_N_IN |
		      ETHER_CLK_SEL_RMII_CLK_SEL_RX_C;
		writel(val, dwmac->reg + REG_ETHER_CLOCK_SEL);

		val |= ETHER_CLK_SEL_RMII_CLK_RST;
		writel(val, dwmac->reg + REG_ETHER_CLOCK_SEL);

		val |= ETHER_CLK_SEL_RMII_CLK_EN | ETHER_CLK_SEL_RX_TX_CLK_EN;
		writel(val, dwmac->reg + REG_ETHER_CLOCK_SEL);
	} else {
		/* Stop internal clock */
		val = readl(dwmac->reg + REG_ETHER_CLOCK_SEL);
		val &= ~(ETHER_CLK_SEL_RMII_CLK_EN |
			 ETHER_CLK_SEL_RX_TX_CLK_EN);
		val |= ETHER_CLK_SEL_TX_O_E_N_IN;
		writel(val, dwmac->reg + REG_ETHER_CLOCK_SEL);

		/* Set Clock-Mux, Start clock, Set TX_O direction */
		val = ETHER_CLK_SEL_RX_CLK_EXT_SEL_RXC |
		      ETHER_CLK_SEL_TX_CLK_EXT_SEL_TXC |
		      ETHER_CLK_SEL_TX_O_E_N_IN;
		writel(val, dwmac->reg + REG_ETHER_CLOCK_SEL);

		val |= ETHER_CLK_SEL_RX_TX_CLK_EN;
		writel(val, dwmac->reg + REG_ETHER_CLOCK_SEL);
	}

	return 0;
}

static int visconti_eth_init_hw(struct platform_device *pdev, struct plat_stmmacenet_data *plat_dat)
{
	struct visconti_eth *dwmac = plat_dat->bsp_priv;
	unsigned int clk_sel_val;
	u32 phy_intf_sel;

	switch (plat_dat->phy_interface) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		phy_intf_sel = ETHER_CONFIG_INTF_RGMII;
		break;
	case PHY_INTERFACE_MODE_MII:
		phy_intf_sel = ETHER_CONFIG_INTF_MII;
		break;
	case PHY_INTERFACE_MODE_RMII:
		phy_intf_sel = ETHER_CONFIG_INTF_RMII;
		break;
	default:
		dev_err(&pdev->dev, "Unsupported phy-mode (%d)\n", plat_dat->phy_interface);
		return -EOPNOTSUPP;
	}

	writel(phy_intf_sel, dwmac->reg + REG_ETHER_CONTROL);

	/* Enable TX/RX clock */
	clk_sel_val = ETHER_CLK_SEL_FREQ_SEL_125M;
	writel(clk_sel_val, dwmac->reg + REG_ETHER_CLOCK_SEL);

	writel((clk_sel_val | ETHER_CLK_SEL_RMII_CLK_EN | ETHER_CLK_SEL_RX_TX_CLK_EN),
	       dwmac->reg + REG_ETHER_CLOCK_SEL);

	/* release internal-reset */
	phy_intf_sel |= ETHER_ETH_CONTROL_RESET;
	writel(phy_intf_sel, dwmac->reg + REG_ETHER_CONTROL);

	return 0;
}

static int visconti_eth_clock_probe(struct platform_device *pdev,
				    struct plat_stmmacenet_data *plat_dat)
{
	struct visconti_eth *dwmac = plat_dat->bsp_priv;
	int err;

	dwmac->phy_ref_clk = devm_clk_get(&pdev->dev, "phy_ref_clk");
	if (IS_ERR(dwmac->phy_ref_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(dwmac->phy_ref_clk),
				     "phy_ref_clk clock not found.\n");

	err = clk_prepare_enable(dwmac->phy_ref_clk);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to enable phy_ref clock: %d\n", err);
		return err;
	}

	return 0;
}

static void visconti_eth_clock_remove(struct platform_device *pdev)
{
	struct visconti_eth *dwmac = get_stmmac_bsp_priv(&pdev->dev);
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct stmmac_priv *priv = netdev_priv(ndev);

	clk_disable_unprepare(dwmac->phy_ref_clk);
	clk_disable_unprepare(priv->plat->stmmac_clk);
}

static int visconti_eth_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct visconti_eth *dwmac;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	dwmac = devm_kzalloc(&pdev->dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac)
		return -ENOMEM;

	dwmac->reg = stmmac_res.addr;
	dwmac->dev = &pdev->dev;
	plat_dat->bsp_priv = dwmac;
	plat_dat->set_clk_tx_rate = visconti_eth_set_clk_tx_rate;

	ret = visconti_eth_clock_probe(pdev, plat_dat);
	if (ret)
		return ret;

	visconti_eth_init_hw(pdev, plat_dat);

	plat_dat->dma_cfg->aal = 1;

	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret)
		goto remove;

	return ret;

remove:
	visconti_eth_clock_remove(pdev);

	return ret;
}

static void visconti_eth_dwmac_remove(struct platform_device *pdev)
{
	stmmac_pltfr_remove(pdev);
	visconti_eth_clock_remove(pdev);
}

static const struct of_device_id visconti_eth_dwmac_match[] = {
	{ .compatible = "toshiba,visconti-dwmac" },
	{ }
};
MODULE_DEVICE_TABLE(of, visconti_eth_dwmac_match);

static struct platform_driver visconti_eth_dwmac_driver = {
	.probe  = visconti_eth_dwmac_probe,
	.remove = visconti_eth_dwmac_remove,
	.driver = {
		.name           = "visconti-eth-dwmac",
		.of_match_table = visconti_eth_dwmac_match,
	},
};
module_platform_driver(visconti_eth_dwmac_driver);

MODULE_AUTHOR("Toshiba");
MODULE_DESCRIPTION("Toshiba Visconti Ethernet DWMAC glue driver");
MODULE_AUTHOR("Nobuhiro Iwamatsu <nobuhiro1.iwamatsu@toshiba.co.jp");
MODULE_LICENSE("GPL v2");
