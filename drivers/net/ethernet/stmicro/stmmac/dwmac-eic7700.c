// SPDX-License-Identifier: GPL-2.0
/*
 * Eswin DWC Ethernet linux driver
 *
 * Copyright 2025, Beijing ESWIN Computing Technology Co., Ltd.
 *
 * Authors:
 *   Zhi Li <lizhi2@eswincomputing.com>
 *   Shuang Liang <liangshuang@eswincomputing.com>
 *   Shangjuan Wei <weishangjuan@eswincomputing.com>
 */

#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/pm_runtime.h>
#include <linux/stmmac.h>
#include <linux/regmap.h>
#include <linux/of.h>

#include "stmmac_platform.h"

/* eth_phy_ctrl_offset eth0:0x100 */
#define EIC7700_ETH_TX_CLK_SEL		BIT(16)
#define EIC7700_ETH_PHY_INTF_SELI	BIT(0)

/* eth_axi_lp_ctrl_offset eth0:0x108 */
#define EIC7700_ETH_CSYSREQ_VAL		BIT(0)

/*
 * TX/RX Clock Delay Bit Masks:
 * - TX Delay: bits [14:8] — TX_CLK delay (unit: 0.1ns per bit)
 * - RX Delay: bits [30:24] — RX_CLK delay (unit: 0.1ns per bit)
 */
#define EIC7700_ETH_TX_ADJ_DELAY	GENMASK(14, 8)
#define EIC7700_ETH_RX_ADJ_DELAY	GENMASK(30, 24)

#define EIC7700_MAX_DELAY_UNIT 0x7F

static const char * const eic7700_clk_names[] = {
	"tx", "axi", "cfg",
};

struct eic7700_qos_priv {
	struct plat_stmmacenet_data *plat_dat;
};

static int eic7700_clks_config(void *priv, bool enabled)
{
	struct eic7700_qos_priv *dwc = (struct eic7700_qos_priv *)priv;
	struct plat_stmmacenet_data *plat = dwc->plat_dat;
	int ret = 0;

	if (enabled)
		ret = clk_bulk_prepare_enable(plat->num_clks, plat->clks);
	else
		clk_bulk_disable_unprepare(plat->num_clks, plat->clks);

	return ret;
}

static int eic7700_dwmac_init(struct platform_device *pdev, void *priv)
{
	struct eic7700_qos_priv *dwc = priv;

	return eic7700_clks_config(dwc, true);
}

static void eic7700_dwmac_exit(struct platform_device *pdev, void *priv)
{
	struct eic7700_qos_priv *dwc = priv;

	eic7700_clks_config(dwc, false);
}

static int eic7700_dwmac_suspend(struct device *dev, void *priv)
{
	return pm_runtime_force_suspend(dev);
}

static int eic7700_dwmac_resume(struct device *dev, void *priv)
{
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret)
		dev_err(dev, "%s failed: %d\n", __func__, ret);

	return ret;
}

static int eic7700_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct eic7700_qos_priv *dwc_priv;
	struct regmap *eic7700_hsp_regmap;
	u32 eth_axi_lp_ctrl_offset;
	u32 eth_phy_ctrl_offset;
	u32 eth_phy_ctrl_regset;
	u32 eth_rxd_dly_offset;
	u32 eth_dly_param = 0;
	u32 delay_ps;
	int i, ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				"failed to get resources\n");

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return dev_err_probe(&pdev->dev, PTR_ERR(plat_dat),
				"dt configuration failed\n");

	dwc_priv = devm_kzalloc(&pdev->dev, sizeof(*dwc_priv), GFP_KERNEL);
	if (!dwc_priv)
		return -ENOMEM;

	/* Read rx-internal-delay-ps and update rx_clk delay */
	if (!of_property_read_u32(pdev->dev.of_node,
				  "rx-internal-delay-ps", &delay_ps)) {
		u32 val = min(delay_ps / 100, EIC7700_MAX_DELAY_UNIT);

		eth_dly_param &= ~EIC7700_ETH_RX_ADJ_DELAY;
		eth_dly_param |= FIELD_PREP(EIC7700_ETH_RX_ADJ_DELAY, val);
	} else {
		return dev_err_probe(&pdev->dev, -EINVAL,
			"missing required property rx-internal-delay-ps\n");
	}

	/* Read tx-internal-delay-ps and update tx_clk delay */
	if (!of_property_read_u32(pdev->dev.of_node,
				  "tx-internal-delay-ps", &delay_ps)) {
		u32 val = min(delay_ps / 100, EIC7700_MAX_DELAY_UNIT);

		eth_dly_param &= ~EIC7700_ETH_TX_ADJ_DELAY;
		eth_dly_param |= FIELD_PREP(EIC7700_ETH_TX_ADJ_DELAY, val);
	} else {
		return dev_err_probe(&pdev->dev, -EINVAL,
			"missing required property tx-internal-delay-ps\n");
	}

	eic7700_hsp_regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
							     "eswin,hsp-sp-csr");
	if (IS_ERR(eic7700_hsp_regmap))
		return dev_err_probe(&pdev->dev,
				PTR_ERR(eic7700_hsp_regmap),
				"Failed to get hsp-sp-csr regmap\n");

	ret = of_property_read_u32_index(pdev->dev.of_node,
					 "eswin,hsp-sp-csr",
					 1, &eth_phy_ctrl_offset);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "can't get eth_phy_ctrl_offset\n");

	regmap_read(eic7700_hsp_regmap, eth_phy_ctrl_offset,
		    &eth_phy_ctrl_regset);
	eth_phy_ctrl_regset |=
		(EIC7700_ETH_TX_CLK_SEL | EIC7700_ETH_PHY_INTF_SELI);
	regmap_write(eic7700_hsp_regmap, eth_phy_ctrl_offset,
		     eth_phy_ctrl_regset);

	ret = of_property_read_u32_index(pdev->dev.of_node,
					 "eswin,hsp-sp-csr",
					 2, &eth_axi_lp_ctrl_offset);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "can't get eth_axi_lp_ctrl_offset\n");

	regmap_write(eic7700_hsp_regmap, eth_axi_lp_ctrl_offset,
		     EIC7700_ETH_CSYSREQ_VAL);

	ret = of_property_read_u32_index(pdev->dev.of_node,
					 "eswin,hsp-sp-csr",
					 3, &eth_rxd_dly_offset);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "can't get eth_rxd_dly_offset\n");

	regmap_write(eic7700_hsp_regmap, eth_rxd_dly_offset,
		     eth_dly_param);

	plat_dat->num_clks = ARRAY_SIZE(eic7700_clk_names);
	plat_dat->clks = devm_kcalloc(&pdev->dev,
				      plat_dat->num_clks,
				      sizeof(*plat_dat->clks),
				      GFP_KERNEL);
	if (!plat_dat->clks)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(eic7700_clk_names); i++)
		plat_dat->clks[i].id = eic7700_clk_names[i];

	ret = devm_clk_bulk_get_optional(&pdev->dev,
					 plat_dat->num_clks,
					 plat_dat->clks);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to get clocks\n");

	plat_dat->clk_tx_i = stmmac_pltfr_find_clk(plat_dat, "tx");
	plat_dat->set_clk_tx_rate = stmmac_set_clk_tx_rate;
	plat_dat->clks_config = eic7700_clks_config;
	plat_dat->bsp_priv = dwc_priv;
	dwc_priv->plat_dat = plat_dat;
	plat_dat->init = eic7700_dwmac_init;
	plat_dat->exit = eic7700_dwmac_exit;
	plat_dat->suspend = eic7700_dwmac_suspend;
	plat_dat->resume = eic7700_dwmac_resume;

	return devm_stmmac_pltfr_probe(pdev, plat_dat, &stmmac_res);
}

static const struct of_device_id eic7700_dwmac_match[] = {
	{ .compatible = "eswin,eic7700-qos-eth" },
	{ }
};
MODULE_DEVICE_TABLE(of, eic7700_dwmac_match);

static struct platform_driver eic7700_dwmac_driver = {
	.probe  = eic7700_dwmac_probe,
	.driver = {
		.name           = "eic7700-eth-dwmac",
		.pm             = &stmmac_pltfr_pm_ops,
		.of_match_table = eic7700_dwmac_match,
	},
};
module_platform_driver(eic7700_dwmac_driver);

MODULE_AUTHOR("Zhi Li <lizhi2@eswincomputing.com>");
MODULE_AUTHOR("Shuang Liang <liangshuang@eswincomputing.com>");
MODULE_AUTHOR("Shangjuan Wei <weishangjuan@eswincomputing.com>");
MODULE_DESCRIPTION("Eswin eic7700 qos ethernet driver");
MODULE_LICENSE("GPL");
