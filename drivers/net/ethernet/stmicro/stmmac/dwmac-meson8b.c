// SPDX-License-Identifier: GPL-2.0-only
/*
 * Amlogic Meson8b, Meson8m2 and GXBB DWMAC glue layer
 *
 * Copyright (C) 2016 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/ethtool.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_net.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/stmmac.h>

#include "stmmac_platform.h"

#define PRG_ETH0			0x0

#define PRG_ETH0_RGMII_MODE		BIT(0)

#define PRG_ETH0_EXT_PHY_MODE_MASK	GENMASK(2, 0)
#define PRG_ETH0_EXT_RGMII_MODE		1
#define PRG_ETH0_EXT_RMII_MODE		4

/* mux to choose between fclk_div2 (bit unset) and mpll2 (bit set) */
#define PRG_ETH0_CLK_M250_SEL_MASK	GENMASK(4, 4)

/* TX clock delay in ns = "8ns / 4 * tx_dly_val" (where 8ns are exactly one
 * cycle of the 125MHz RGMII TX clock):
 * 0ns = 0x0, 2ns = 0x1, 4ns = 0x2, 6ns = 0x3
 */
#define PRG_ETH0_TXDLY_MASK		GENMASK(6, 5)

/* divider for the result of m250_sel */
#define PRG_ETH0_CLK_M250_DIV_SHIFT	7
#define PRG_ETH0_CLK_M250_DIV_WIDTH	3

#define PRG_ETH0_RGMII_TX_CLK_EN	10

#define PRG_ETH0_INVERTED_RMII_CLK	BIT(11)
#define PRG_ETH0_TX_AND_PHY_REF_CLK	BIT(12)

/* Bypass (= 0, the signal from the GPIO input directly connects to the
 * internal sampling) or enable (= 1) the internal logic for RXEN and RXD[3:0]
 * timing tuning.
 */
#define PRG_ETH0_ADJ_ENABLE		BIT(13)
/* Controls whether the RXEN and RXD[3:0] signals should be aligned with the
 * input RX rising/falling edge and sent to the Ethernet internals. This sets
 * the automatically delay and skew automatically (internally).
 */
#define PRG_ETH0_ADJ_SETUP		BIT(14)
/* An internal counter based on the "timing-adjustment" clock. The counter is
 * cleared on both, the falling and rising edge of the RX_CLK. This selects the
 * delay (= the counter value) when to start sampling RXEN and RXD[3:0].
 */
#define PRG_ETH0_ADJ_DELAY		GENMASK(19, 15)
/* Adjusts the skew between each bit of RXEN and RXD[3:0]. If a signal has a
 * large input delay, the bit for that signal (RXEN = bit 0, RXD[3] = bit 1,
 * ...) can be configured to be 1 to compensate for a delay of about 1ns.
 */
#define PRG_ETH0_ADJ_SKEW		GENMASK(24, 20)

struct meson8b_dwmac;

struct meson8b_dwmac_data {
	int (*set_phy_mode)(struct meson8b_dwmac *dwmac);
};

struct meson8b_dwmac {
	struct device			*dev;
	void __iomem			*regs;

	const struct meson8b_dwmac_data	*data;
	phy_interface_t			phy_mode;
	struct clk			*rgmii_tx_clk;
	u32				tx_delay_ns;
	u32				rx_delay_ns;
	struct clk			*timing_adj_clk;
};

struct meson8b_dwmac_clk_configs {
	struct clk_mux		m250_mux;
	struct clk_divider	m250_div;
	struct clk_fixed_factor	fixed_div2;
	struct clk_gate		rgmii_tx_en;
};

static void meson8b_dwmac_mask_bits(struct meson8b_dwmac *dwmac, u32 reg,
				    u32 mask, u32 value)
{
	u32 data;

	data = readl(dwmac->regs + reg);
	data &= ~mask;
	data |= (value & mask);

	writel(data, dwmac->regs + reg);
}

static struct clk *meson8b_dwmac_register_clk(struct meson8b_dwmac *dwmac,
					      const char *name_suffix,
					      const struct clk_parent_data *parents,
					      int num_parents,
					      const struct clk_ops *ops,
					      struct clk_hw *hw)
{
	struct clk_init_data init = { };
	char clk_name[32];

	snprintf(clk_name, sizeof(clk_name), "%s#%s", dev_name(dwmac->dev),
		 name_suffix);

	init.name = clk_name;
	init.ops = ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_data = parents;
	init.num_parents = num_parents;

	hw->init = &init;

	return devm_clk_register(dwmac->dev, hw);
}

static int meson8b_init_rgmii_tx_clk(struct meson8b_dwmac *dwmac)
{
	struct clk *clk;
	struct device *dev = dwmac->dev;
	static const struct clk_parent_data mux_parents[] = {
		{ .fw_name = "clkin0", },
		{ .index = -1, },
	};
	static const struct clk_div_table div_table[] = {
		{ .div = 2, .val = 2, },
		{ .div = 3, .val = 3, },
		{ .div = 4, .val = 4, },
		{ .div = 5, .val = 5, },
		{ .div = 6, .val = 6, },
		{ .div = 7, .val = 7, },
		{ /* end of array */ }
	};
	struct meson8b_dwmac_clk_configs *clk_configs;
	struct clk_parent_data parent_data = { };

	clk_configs = devm_kzalloc(dev, sizeof(*clk_configs), GFP_KERNEL);
	if (!clk_configs)
		return -ENOMEM;

	clk_configs->m250_mux.reg = dwmac->regs + PRG_ETH0;
	clk_configs->m250_mux.shift = __ffs(PRG_ETH0_CLK_M250_SEL_MASK);
	clk_configs->m250_mux.mask = PRG_ETH0_CLK_M250_SEL_MASK >>
				     clk_configs->m250_mux.shift;
	clk = meson8b_dwmac_register_clk(dwmac, "m250_sel", mux_parents,
					 ARRAY_SIZE(mux_parents), &clk_mux_ops,
					 &clk_configs->m250_mux.hw);
	if (WARN_ON(IS_ERR(clk)))
		return PTR_ERR(clk);

	parent_data.hw = &clk_configs->m250_mux.hw;
	clk_configs->m250_div.reg = dwmac->regs + PRG_ETH0;
	clk_configs->m250_div.shift = PRG_ETH0_CLK_M250_DIV_SHIFT;
	clk_configs->m250_div.width = PRG_ETH0_CLK_M250_DIV_WIDTH;
	clk_configs->m250_div.table = div_table;
	clk_configs->m250_div.flags = CLK_DIVIDER_ALLOW_ZERO |
				      CLK_DIVIDER_ROUND_CLOSEST;
	clk = meson8b_dwmac_register_clk(dwmac, "m250_div", &parent_data, 1,
					 &clk_divider_ops,
					 &clk_configs->m250_div.hw);
	if (WARN_ON(IS_ERR(clk)))
		return PTR_ERR(clk);

	parent_data.hw = &clk_configs->m250_div.hw;
	clk_configs->fixed_div2.mult = 1;
	clk_configs->fixed_div2.div = 2;
	clk = meson8b_dwmac_register_clk(dwmac, "fixed_div2", &parent_data, 1,
					 &clk_fixed_factor_ops,
					 &clk_configs->fixed_div2.hw);
	if (WARN_ON(IS_ERR(clk)))
		return PTR_ERR(clk);

	parent_data.hw = &clk_configs->fixed_div2.hw;
	clk_configs->rgmii_tx_en.reg = dwmac->regs + PRG_ETH0;
	clk_configs->rgmii_tx_en.bit_idx = PRG_ETH0_RGMII_TX_CLK_EN;
	clk = meson8b_dwmac_register_clk(dwmac, "rgmii_tx_en", &parent_data, 1,
					 &clk_gate_ops,
					 &clk_configs->rgmii_tx_en.hw);
	if (WARN_ON(IS_ERR(clk)))
		return PTR_ERR(clk);

	dwmac->rgmii_tx_clk = clk;

	return 0;
}

static int meson8b_set_phy_mode(struct meson8b_dwmac *dwmac)
{
	switch (dwmac->phy_mode) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		/* enable RGMII mode */
		meson8b_dwmac_mask_bits(dwmac, PRG_ETH0,
					PRG_ETH0_RGMII_MODE,
					PRG_ETH0_RGMII_MODE);
		break;
	case PHY_INTERFACE_MODE_RMII:
		/* disable RGMII mode -> enables RMII mode */
		meson8b_dwmac_mask_bits(dwmac, PRG_ETH0,
					PRG_ETH0_RGMII_MODE, 0);
		break;
	default:
		dev_err(dwmac->dev, "fail to set phy-mode %s\n",
			phy_modes(dwmac->phy_mode));
		return -EINVAL;
	}

	return 0;
}

static int meson_axg_set_phy_mode(struct meson8b_dwmac *dwmac)
{
	switch (dwmac->phy_mode) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		/* enable RGMII mode */
		meson8b_dwmac_mask_bits(dwmac, PRG_ETH0,
					PRG_ETH0_EXT_PHY_MODE_MASK,
					PRG_ETH0_EXT_RGMII_MODE);
		break;
	case PHY_INTERFACE_MODE_RMII:
		/* disable RGMII mode -> enables RMII mode */
		meson8b_dwmac_mask_bits(dwmac, PRG_ETH0,
					PRG_ETH0_EXT_PHY_MODE_MASK,
					PRG_ETH0_EXT_RMII_MODE);
		break;
	default:
		dev_err(dwmac->dev, "fail to set phy-mode %s\n",
			phy_modes(dwmac->phy_mode));
		return -EINVAL;
	}

	return 0;
}

static int meson8b_devm_clk_prepare_enable(struct meson8b_dwmac *dwmac,
					   struct clk *clk)
{
	int ret;

	ret = clk_prepare_enable(clk);
	if (ret)
		return ret;

	devm_add_action_or_reset(dwmac->dev,
				 (void(*)(void *))clk_disable_unprepare,
				 dwmac->rgmii_tx_clk);

	return 0;
}

static int meson8b_init_prg_eth(struct meson8b_dwmac *dwmac)
{
	u32 tx_dly_config, rx_dly_config, delay_config;
	int ret;

	tx_dly_config = FIELD_PREP(PRG_ETH0_TXDLY_MASK,
				   dwmac->tx_delay_ns >> 1);

	if (dwmac->rx_delay_ns == 2)
		rx_dly_config = PRG_ETH0_ADJ_ENABLE | PRG_ETH0_ADJ_SETUP;
	else
		rx_dly_config = 0;

	switch (dwmac->phy_mode) {
	case PHY_INTERFACE_MODE_RGMII:
		delay_config = tx_dly_config | rx_dly_config;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		delay_config = tx_dly_config;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		delay_config = rx_dly_config;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RMII:
		delay_config = 0;
		break;
	default:
		dev_err(dwmac->dev, "unsupported phy-mode %s\n",
			phy_modes(dwmac->phy_mode));
		return -EINVAL;
	};

	if (rx_dly_config & PRG_ETH0_ADJ_ENABLE) {
		if (!dwmac->timing_adj_clk) {
			dev_err(dwmac->dev,
				"The timing-adjustment clock is mandatory for the RX delay re-timing\n");
			return -EINVAL;
		}

		/* The timing adjustment logic is driven by a separate clock */
		ret = meson8b_devm_clk_prepare_enable(dwmac,
						      dwmac->timing_adj_clk);
		if (ret) {
			dev_err(dwmac->dev,
				"Failed to enable the timing-adjustment clock\n");
			return ret;
		}
	}

	meson8b_dwmac_mask_bits(dwmac, PRG_ETH0, PRG_ETH0_TXDLY_MASK |
				PRG_ETH0_ADJ_ENABLE | PRG_ETH0_ADJ_SETUP |
				PRG_ETH0_ADJ_DELAY | PRG_ETH0_ADJ_SKEW,
				delay_config);

	if (phy_interface_mode_is_rgmii(dwmac->phy_mode)) {
		/* only relevant for RMII mode -> disable in RGMII mode */
		meson8b_dwmac_mask_bits(dwmac, PRG_ETH0,
					PRG_ETH0_INVERTED_RMII_CLK, 0);

		/* Configure the 125MHz RGMII TX clock, the IP block changes
		 * the output automatically (= without us having to configure
		 * a register) based on the line-speed (125MHz for Gbit speeds,
		 * 25MHz for 100Mbit/s and 2.5MHz for 10Mbit/s).
		 */
		ret = clk_set_rate(dwmac->rgmii_tx_clk, 125 * 1000 * 1000);
		if (ret) {
			dev_err(dwmac->dev,
				"failed to set RGMII TX clock\n");
			return ret;
		}

		ret = meson8b_devm_clk_prepare_enable(dwmac,
						      dwmac->rgmii_tx_clk);
		if (ret) {
			dev_err(dwmac->dev,
				"failed to enable the RGMII TX clock\n");
			return ret;
		}
	} else {
		/* invert internal clk_rmii_i to generate 25/2.5 tx_rx_clk */
		meson8b_dwmac_mask_bits(dwmac, PRG_ETH0,
					PRG_ETH0_INVERTED_RMII_CLK,
					PRG_ETH0_INVERTED_RMII_CLK);
	}

	/* enable TX_CLK and PHY_REF_CLK generator */
	meson8b_dwmac_mask_bits(dwmac, PRG_ETH0, PRG_ETH0_TX_AND_PHY_REF_CLK,
				PRG_ETH0_TX_AND_PHY_REF_CLK);

	return 0;
}

static int meson8b_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct meson8b_dwmac *dwmac;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	plat_dat = stmmac_probe_config_dt(pdev, &stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	dwmac = devm_kzalloc(&pdev->dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac) {
		ret = -ENOMEM;
		goto err_remove_config_dt;
	}

	dwmac->data = (const struct meson8b_dwmac_data *)
		of_device_get_match_data(&pdev->dev);
	if (!dwmac->data) {
		ret = -EINVAL;
		goto err_remove_config_dt;
	}
	dwmac->regs = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(dwmac->regs)) {
		ret = PTR_ERR(dwmac->regs);
		goto err_remove_config_dt;
	}

	dwmac->dev = &pdev->dev;
	ret = of_get_phy_mode(pdev->dev.of_node, &dwmac->phy_mode);
	if (ret) {
		dev_err(&pdev->dev, "missing phy-mode property\n");
		goto err_remove_config_dt;
	}

	/* use 2ns as fallback since this value was previously hardcoded */
	if (of_property_read_u32(pdev->dev.of_node, "amlogic,tx-delay-ns",
				 &dwmac->tx_delay_ns))
		dwmac->tx_delay_ns = 2;

	/* use 0ns as fallback since this is what most boards actually use */
	if (of_property_read_u32(pdev->dev.of_node, "amlogic,rx-delay-ns",
				 &dwmac->rx_delay_ns))
		dwmac->rx_delay_ns = 0;

	if (dwmac->rx_delay_ns != 0 && dwmac->rx_delay_ns != 2) {
		dev_err(&pdev->dev,
			"The only allowed RX delays values are: 0ns, 2ns");
		ret = -EINVAL;
		goto err_remove_config_dt;
	}

	dwmac->timing_adj_clk = devm_clk_get_optional(dwmac->dev,
						      "timing-adjustment");
	if (IS_ERR(dwmac->timing_adj_clk)) {
		ret = PTR_ERR(dwmac->timing_adj_clk);
		goto err_remove_config_dt;
	}

	ret = meson8b_init_rgmii_tx_clk(dwmac);
	if (ret)
		goto err_remove_config_dt;

	ret = dwmac->data->set_phy_mode(dwmac);
	if (ret)
		goto err_remove_config_dt;

	ret = meson8b_init_prg_eth(dwmac);
	if (ret)
		goto err_remove_config_dt;

	plat_dat->bsp_priv = dwmac;

	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret)
		goto err_remove_config_dt;

	return 0;

err_remove_config_dt:
	stmmac_remove_config_dt(pdev, plat_dat);

	return ret;
}

static const struct meson8b_dwmac_data meson8b_dwmac_data = {
	.set_phy_mode = meson8b_set_phy_mode,
};

static const struct meson8b_dwmac_data meson_axg_dwmac_data = {
	.set_phy_mode = meson_axg_set_phy_mode,
};

static const struct of_device_id meson8b_dwmac_match[] = {
	{
		.compatible = "amlogic,meson8b-dwmac",
		.data = &meson8b_dwmac_data,
	},
	{
		.compatible = "amlogic,meson8m2-dwmac",
		.data = &meson8b_dwmac_data,
	},
	{
		.compatible = "amlogic,meson-gxbb-dwmac",
		.data = &meson8b_dwmac_data,
	},
	{
		.compatible = "amlogic,meson-axg-dwmac",
		.data = &meson_axg_dwmac_data,
	},
	{
		.compatible = "amlogic,meson-g12a-dwmac",
		.data = &meson_axg_dwmac_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, meson8b_dwmac_match);

static struct platform_driver meson8b_dwmac_driver = {
	.probe  = meson8b_dwmac_probe,
	.remove = stmmac_pltfr_remove,
	.driver = {
		.name           = "meson8b-dwmac",
		.pm		= &stmmac_pltfr_pm_ops,
		.of_match_table = meson8b_dwmac_match,
	},
};
module_platform_driver(meson8b_dwmac_driver);

MODULE_AUTHOR("Martin Blumenstingl <martin.blumenstingl@googlemail.com>");
MODULE_DESCRIPTION("Amlogic Meson8b, Meson8m2 and GXBB DWMAC glue layer");
MODULE_LICENSE("GPL v2");
