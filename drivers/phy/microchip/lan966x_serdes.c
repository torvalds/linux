// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/phy.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

#include <dt-bindings/phy/phy-lan966x-serdes.h>
#include "lan966x_serdes_regs.h"

#define PLL_CONF_MASK		GENMASK(4, 3)
#define PLL_CONF_25MHZ		0
#define PLL_CONF_125MHZ		1
#define PLL_CONF_SERDES_125MHZ	2
#define PLL_CONF_BYPASS		3

#define lan_offset_(id, tinst, tcnt,			\
		   gbase, ginst, gcnt, gwidth,		\
		   raddr, rinst, rcnt, rwidth)		\
	(gbase + ((ginst) * gwidth) + raddr + ((rinst) * rwidth))
#define lan_offset(...) lan_offset_(__VA_ARGS__)

#define lan_rmw(val, mask, reg, off)		\
	lan_rmw_(val, mask, reg, lan_offset(off))

#define SERDES_MUX(_idx, _port, _mode, _submode, _mask, _mux) { \
	.idx = _idx,						\
	.port = _port,						\
	.mode = _mode,						\
	.submode = _submode,					\
	.mask = _mask,						\
	.mux = _mux,						\
}

#define SERDES_MUX_GMII(i, p, m, c) \
	SERDES_MUX(i, p, PHY_MODE_ETHERNET, PHY_INTERFACE_MODE_GMII, m, c)
#define SERDES_MUX_SGMII(i, p, m, c) \
	SERDES_MUX(i, p, PHY_MODE_ETHERNET, PHY_INTERFACE_MODE_SGMII, m, c)
#define SERDES_MUX_QSGMII(i, p, m, c) \
	SERDES_MUX(i, p, PHY_MODE_ETHERNET, PHY_INTERFACE_MODE_QSGMII, m, c)
#define SERDES_MUX_RGMII(i, p, m, c) \
	SERDES_MUX(i, p, PHY_MODE_ETHERNET, PHY_INTERFACE_MODE_RGMII, m, c)

static void lan_rmw_(u32 val, u32 mask, void __iomem *mem, u32 offset)
{
	u32 v;

	v = readl(mem + offset);
	v = (v & ~mask) | (val & mask);
	writel(v, mem + offset);
}

struct serdes_mux {
	u8			idx;
	u8			port;
	enum phy_mode		mode;
	int			submode;
	u32			mask;
	u32			mux;
};

static const struct serdes_mux lan966x_serdes_muxes[] = {
	SERDES_MUX_QSGMII(SERDES6G(1), 0, HSIO_HW_CFG_QSGMII_ENA,
			  HSIO_HW_CFG_QSGMII_ENA_SET(BIT(0))),
	SERDES_MUX_QSGMII(SERDES6G(1), 1, HSIO_HW_CFG_QSGMII_ENA,
			  HSIO_HW_CFG_QSGMII_ENA_SET(BIT(0))),
	SERDES_MUX_QSGMII(SERDES6G(1), 2, HSIO_HW_CFG_QSGMII_ENA,
			  HSIO_HW_CFG_QSGMII_ENA_SET(BIT(0))),
	SERDES_MUX_QSGMII(SERDES6G(1), 3, HSIO_HW_CFG_QSGMII_ENA,
			  HSIO_HW_CFG_QSGMII_ENA_SET(BIT(0))),

	SERDES_MUX_QSGMII(SERDES6G(2), 4, HSIO_HW_CFG_QSGMII_ENA,
			  HSIO_HW_CFG_QSGMII_ENA_SET(BIT(1))),
	SERDES_MUX_QSGMII(SERDES6G(2), 5, HSIO_HW_CFG_QSGMII_ENA,
			  HSIO_HW_CFG_QSGMII_ENA_SET(BIT(1))),
	SERDES_MUX_QSGMII(SERDES6G(2), 6, HSIO_HW_CFG_QSGMII_ENA,
			  HSIO_HW_CFG_QSGMII_ENA_SET(BIT(1))),
	SERDES_MUX_QSGMII(SERDES6G(2), 7, HSIO_HW_CFG_QSGMII_ENA,
			  HSIO_HW_CFG_QSGMII_ENA_SET(BIT(1))),

	SERDES_MUX_GMII(CU(0), 0, HSIO_HW_CFG_GMII_ENA,
			HSIO_HW_CFG_GMII_ENA_SET(BIT(0))),
	SERDES_MUX_GMII(CU(1), 1, HSIO_HW_CFG_GMII_ENA,
			HSIO_HW_CFG_GMII_ENA_SET(BIT(1))),

	SERDES_MUX_SGMII(SERDES6G(0), 0, HSIO_HW_CFG_SD6G_0_CFG, 0),
	SERDES_MUX_SGMII(SERDES6G(1), 1, HSIO_HW_CFG_SD6G_1_CFG, 0),
	SERDES_MUX_SGMII(SERDES6G(0), 2, HSIO_HW_CFG_SD6G_0_CFG,
			 HSIO_HW_CFG_SD6G_0_CFG_SET(1)),
	SERDES_MUX_SGMII(SERDES6G(1), 3, HSIO_HW_CFG_SD6G_1_CFG,
			 HSIO_HW_CFG_SD6G_1_CFG_SET(1)),

	SERDES_MUX_RGMII(RGMII(0), 2, HSIO_HW_CFG_RGMII_0_CFG |
			 HSIO_HW_CFG_RGMII_ENA,
			 HSIO_HW_CFG_RGMII_0_CFG_SET(BIT(0)) |
			 HSIO_HW_CFG_RGMII_ENA_SET(BIT(0))),
	SERDES_MUX_RGMII(RGMII(1), 3, HSIO_HW_CFG_RGMII_1_CFG |
			 HSIO_HW_CFG_RGMII_ENA,
			 HSIO_HW_CFG_RGMII_1_CFG_SET(BIT(0)) |
			 HSIO_HW_CFG_RGMII_ENA_SET(BIT(1))),
	SERDES_MUX_RGMII(RGMII(0), 5, HSIO_HW_CFG_RGMII_0_CFG |
			 HSIO_HW_CFG_RGMII_ENA,
			 HSIO_HW_CFG_RGMII_0_CFG_SET(BIT(0)) |
			 HSIO_HW_CFG_RGMII_ENA_SET(BIT(0))),
	SERDES_MUX_RGMII(RGMII(1), 6, HSIO_HW_CFG_RGMII_1_CFG |
			 HSIO_HW_CFG_RGMII_ENA,
			 HSIO_HW_CFG_RGMII_1_CFG_SET(BIT(0)) |
			 HSIO_HW_CFG_RGMII_ENA_SET(BIT(1))),
};

struct serdes_ctrl {
	void __iomem		*regs;
	struct device		*dev;
	struct phy		*phys[SERDES_MAX];
	int			ref125;
};

struct serdes_macro {
	u8			idx;
	int			port;
	struct serdes_ctrl	*ctrl;
	int			speed;
	phy_interface_t		mode;
};

enum lan966x_sd6g40_mode {
	LAN966X_SD6G40_MODE_QSGMII,
	LAN966X_SD6G40_MODE_SGMII,
};

enum lan966x_sd6g40_ltx2rx {
	LAN966X_SD6G40_TX2RX_LOOP_NONE,
	LAN966X_SD6G40_LTX2RX
};

struct lan966x_sd6g40_setup_args {
	enum lan966x_sd6g40_mode	mode;
	enum lan966x_sd6g40_ltx2rx	tx2rx_loop;
	bool				txinvert;
	bool				rxinvert;
	bool				refclk125M;
	bool				mute;
};

struct lan966x_sd6g40_mode_args {
	enum lan966x_sd6g40_mode	mode;
	u8				 lane_10bit_sel;
	u8				 mpll_multiplier;
	u8				 ref_clkdiv2;
	u8				 tx_rate;
	u8				 rx_rate;
};

struct lan966x_sd6g40_setup {
	u8	rx_term_en;
	u8	lane_10bit_sel;
	u8	tx_invert;
	u8	rx_invert;
	u8	mpll_multiplier;
	u8	lane_loopbk_en;
	u8	ref_clkdiv2;
	u8	tx_rate;
	u8	rx_rate;
};

static int lan966x_sd6g40_reg_cfg(struct serdes_macro *macro,
				  struct lan966x_sd6g40_setup *res_struct,
				  u32 idx)
{
	u32 value;

	/* Note: SerDes HSIO is configured in 1G_LAN mode */
	lan_rmw(HSIO_SD_CFG_LANE_10BIT_SEL_SET(res_struct->lane_10bit_sel) |
		HSIO_SD_CFG_RX_RATE_SET(res_struct->rx_rate) |
		HSIO_SD_CFG_TX_RATE_SET(res_struct->tx_rate) |
		HSIO_SD_CFG_TX_INVERT_SET(res_struct->tx_invert) |
		HSIO_SD_CFG_RX_INVERT_SET(res_struct->rx_invert) |
		HSIO_SD_CFG_LANE_LOOPBK_EN_SET(res_struct->lane_loopbk_en) |
		HSIO_SD_CFG_RX_RESET_SET(0) |
		HSIO_SD_CFG_TX_RESET_SET(0),
		HSIO_SD_CFG_LANE_10BIT_SEL |
		HSIO_SD_CFG_RX_RATE |
		HSIO_SD_CFG_TX_RATE |
		HSIO_SD_CFG_TX_INVERT |
		HSIO_SD_CFG_RX_INVERT |
		HSIO_SD_CFG_LANE_LOOPBK_EN |
		HSIO_SD_CFG_RX_RESET |
		HSIO_SD_CFG_TX_RESET,
		macro->ctrl->regs, HSIO_SD_CFG(idx));

	lan_rmw(HSIO_MPLL_CFG_MPLL_MULTIPLIER_SET(res_struct->mpll_multiplier) |
		HSIO_MPLL_CFG_REF_CLKDIV2_SET(res_struct->ref_clkdiv2),
		HSIO_MPLL_CFG_MPLL_MULTIPLIER |
		HSIO_MPLL_CFG_REF_CLKDIV2,
		macro->ctrl->regs, HSIO_MPLL_CFG(idx));

	lan_rmw(HSIO_SD_CFG_RX_TERM_EN_SET(res_struct->rx_term_en),
		HSIO_SD_CFG_RX_TERM_EN,
		macro->ctrl->regs, HSIO_SD_CFG(idx));

	lan_rmw(HSIO_MPLL_CFG_REF_SSP_EN_SET(1),
		HSIO_MPLL_CFG_REF_SSP_EN,
		macro->ctrl->regs, HSIO_MPLL_CFG(idx));

	usleep_range(USEC_PER_MSEC, 2 * USEC_PER_MSEC);

	lan_rmw(HSIO_SD_CFG_PHY_RESET_SET(0),
		HSIO_SD_CFG_PHY_RESET,
		macro->ctrl->regs, HSIO_SD_CFG(idx));

	usleep_range(USEC_PER_MSEC, 2 * USEC_PER_MSEC);

	lan_rmw(HSIO_MPLL_CFG_MPLL_EN_SET(1),
		HSIO_MPLL_CFG_MPLL_EN,
		macro->ctrl->regs, HSIO_MPLL_CFG(idx));

	usleep_range(7 * USEC_PER_MSEC, 8 * USEC_PER_MSEC);

	value = readl(macro->ctrl->regs + lan_offset(HSIO_SD_STAT(idx)));
	value = HSIO_SD_STAT_MPLL_STATE_GET(value);
	if (value != 0x1) {
		dev_err(macro->ctrl->dev,
			"Unexpected sd_sd_stat[%u] mpll_state was 0x1 but is 0x%x\n",
			idx, value);
		return -EIO;
	}

	lan_rmw(HSIO_SD_CFG_TX_CM_EN_SET(1),
		HSIO_SD_CFG_TX_CM_EN,
		macro->ctrl->regs, HSIO_SD_CFG(idx));

	usleep_range(USEC_PER_MSEC, 2 * USEC_PER_MSEC);

	value = readl(macro->ctrl->regs + lan_offset(HSIO_SD_STAT(idx)));
	value = HSIO_SD_STAT_TX_CM_STATE_GET(value);
	if (value != 0x1) {
		dev_err(macro->ctrl->dev,
			"Unexpected sd_sd_stat[%u] tx_cm_state was 0x1 but is 0x%x\n",
			idx, value);
		return -EIO;
	}

	lan_rmw(HSIO_SD_CFG_RX_PLL_EN_SET(1) |
		HSIO_SD_CFG_TX_EN_SET(1),
		HSIO_SD_CFG_RX_PLL_EN |
		HSIO_SD_CFG_TX_EN,
		macro->ctrl->regs, HSIO_SD_CFG(idx));

	usleep_range(USEC_PER_MSEC, 2 * USEC_PER_MSEC);

	/* Waiting for serdes 0 rx DPLL to lock...  */
	value = readl(macro->ctrl->regs + lan_offset(HSIO_SD_STAT(idx)));
	value = HSIO_SD_STAT_RX_PLL_STATE_GET(value);
	if (value != 0x1) {
		dev_err(macro->ctrl->dev,
			"Unexpected sd_sd_stat[%u] rx_pll_state was 0x1 but is 0x%x\n",
			idx, value);
		return -EIO;
	}

	/* Waiting for serdes 0 tx operational...  */
	value = readl(macro->ctrl->regs + lan_offset(HSIO_SD_STAT(idx)));
	value = HSIO_SD_STAT_TX_STATE_GET(value);
	if (value != 0x1) {
		dev_err(macro->ctrl->dev,
			"Unexpected sd_sd_stat[%u] tx_state was 0x1 but is 0x%x\n",
			idx, value);
		return -EIO;
	}

	lan_rmw(HSIO_SD_CFG_TX_DATA_EN_SET(1) |
		HSIO_SD_CFG_RX_DATA_EN_SET(1),
		HSIO_SD_CFG_TX_DATA_EN |
		HSIO_SD_CFG_RX_DATA_EN,
		macro->ctrl->regs, HSIO_SD_CFG(idx));

	return 0;
}

static int lan966x_sd6g40_get_conf_from_mode(struct serdes_macro *macro,
					     enum lan966x_sd6g40_mode f_mode,
					     bool ref125M,
					     struct lan966x_sd6g40_mode_args *ret_val)
{
	switch (f_mode) {
	case LAN966X_SD6G40_MODE_QSGMII:
		ret_val->lane_10bit_sel = 0;
		if (ref125M) {
			ret_val->mpll_multiplier = 40;
			ret_val->ref_clkdiv2 = 0x1;
			ret_val->tx_rate = 0x0;
			ret_val->rx_rate = 0x0;
		} else {
			ret_val->mpll_multiplier = 100;
			ret_val->ref_clkdiv2 = 0x0;
			ret_val->tx_rate = 0x0;
			ret_val->rx_rate = 0x0;
		}
		break;

	case LAN966X_SD6G40_MODE_SGMII:
		ret_val->lane_10bit_sel = 1;
		if (ref125M) {
			ret_val->mpll_multiplier = macro->speed == SPEED_2500 ? 50 : 40;
			ret_val->ref_clkdiv2 = 0x1;
			ret_val->tx_rate = macro->speed == SPEED_2500 ? 0x1 : 0x2;
			ret_val->rx_rate = macro->speed == SPEED_2500 ? 0x1 : 0x2;
		} else {
			ret_val->mpll_multiplier = macro->speed == SPEED_2500 ? 125 : 100;
			ret_val->ref_clkdiv2 = 0x0;
			ret_val->tx_rate = macro->speed == SPEED_2500 ? 0x1 : 0x2;
			ret_val->rx_rate = macro->speed == SPEED_2500 ? 0x1 : 0x2;
		}
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int lan966x_calc_sd6g40_setup_lane(struct serdes_macro *macro,
					  struct lan966x_sd6g40_setup_args config,
					  struct lan966x_sd6g40_setup *ret_val)
{
	struct lan966x_sd6g40_mode_args sd6g40_mode;
	struct lan966x_sd6g40_mode_args *mode_args = &sd6g40_mode;
	int ret;

	ret = lan966x_sd6g40_get_conf_from_mode(macro, config.mode,
						config.refclk125M, mode_args);
	if (ret)
		return ret;

	ret_val->lane_10bit_sel = mode_args->lane_10bit_sel;
	ret_val->rx_rate = mode_args->rx_rate;
	ret_val->tx_rate = mode_args->tx_rate;
	ret_val->mpll_multiplier = mode_args->mpll_multiplier;
	ret_val->ref_clkdiv2 = mode_args->ref_clkdiv2;
	ret_val->rx_term_en = 0;

	if (config.tx2rx_loop == LAN966X_SD6G40_LTX2RX)
		ret_val->lane_loopbk_en = 1;
	else
		ret_val->lane_loopbk_en = 0;

	ret_val->tx_invert = !!config.txinvert;
	ret_val->rx_invert = !!config.rxinvert;

	return 0;
}

static int lan966x_sd6g40_setup_lane(struct serdes_macro *macro,
				     struct lan966x_sd6g40_setup_args config,
				     u32 idx)
{
	struct lan966x_sd6g40_setup calc_results = {};
	int ret;

	ret = lan966x_calc_sd6g40_setup_lane(macro, config, &calc_results);
	if (ret)
		return ret;

	return lan966x_sd6g40_reg_cfg(macro, &calc_results, idx);
}

static int lan966x_sd6g40_setup(struct serdes_macro *macro, u32 idx, int mode)
{
	struct lan966x_sd6g40_setup_args conf = {};

	conf.refclk125M = macro->ctrl->ref125;

	if (mode == PHY_INTERFACE_MODE_QSGMII)
		conf.mode = LAN966X_SD6G40_MODE_QSGMII;
	else
		conf.mode = LAN966X_SD6G40_MODE_SGMII;

	return lan966x_sd6g40_setup_lane(macro, conf, idx);
}

static int serdes_set_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	struct serdes_macro *macro = phy_get_drvdata(phy);
	unsigned int i;
	int val;

	/* As of now only PHY_MODE_ETHERNET is supported */
	if (mode != PHY_MODE_ETHERNET)
		return -EOPNOTSUPP;

	if (submode == PHY_INTERFACE_MODE_2500BASEX)
		macro->speed = SPEED_2500;
	else
		macro->speed = SPEED_1000;

	if (submode == PHY_INTERFACE_MODE_1000BASEX ||
	    submode == PHY_INTERFACE_MODE_2500BASEX)
		submode = PHY_INTERFACE_MODE_SGMII;

	for (i = 0; i < ARRAY_SIZE(lan966x_serdes_muxes); i++) {
		if (macro->idx != lan966x_serdes_muxes[i].idx ||
		    mode != lan966x_serdes_muxes[i].mode ||
		    submode != lan966x_serdes_muxes[i].submode ||
		    macro->port != lan966x_serdes_muxes[i].port)
			continue;

		val = readl(macro->ctrl->regs + lan_offset(HSIO_HW_CFG));
		val |= lan966x_serdes_muxes[i].mux;
		lan_rmw(val, lan966x_serdes_muxes[i].mask,
			macro->ctrl->regs, HSIO_HW_CFG);

		macro->mode = lan966x_serdes_muxes[i].submode;

		if (macro->idx < CU_MAX)
			return 0;

		if (macro->idx < SERDES6G_MAX)
			return lan966x_sd6g40_setup(macro,
						    macro->idx - (CU_MAX + 1),
						    macro->mode);

		if (macro->idx < RGMII_MAX)
			return 0;

		return -EOPNOTSUPP;
	}

	return -EINVAL;
}

static const struct phy_ops serdes_ops = {
	.set_mode	= serdes_set_mode,
	.owner		= THIS_MODULE,
};

static struct phy *serdes_simple_xlate(struct device *dev,
				       struct of_phandle_args *args)
{
	struct serdes_ctrl *ctrl = dev_get_drvdata(dev);
	unsigned int port, idx, i;

	if (args->args_count != 2)
		return ERR_PTR(-EINVAL);

	port = args->args[0];
	idx = args->args[1];

	for (i = 0; i < SERDES_MAX; i++) {
		struct serdes_macro *macro = phy_get_drvdata(ctrl->phys[i]);

		if (idx != macro->idx)
			continue;

		macro->port = port;
		return ctrl->phys[i];
	}

	return ERR_PTR(-ENODEV);
}

static int serdes_phy_create(struct serdes_ctrl *ctrl, u8 idx, struct phy **phy)
{
	struct serdes_macro *macro;

	*phy = devm_phy_create(ctrl->dev, NULL, &serdes_ops);
	if (IS_ERR(*phy))
		return PTR_ERR(*phy);

	macro = devm_kzalloc(ctrl->dev, sizeof(*macro), GFP_KERNEL);
	if (!macro)
		return -ENOMEM;

	macro->idx = idx;
	macro->ctrl = ctrl;
	macro->port = -1;

	phy_set_drvdata(*phy, macro);

	return 0;
}

static int serdes_probe(struct platform_device *pdev)
{
	struct phy_provider *provider;
	struct serdes_ctrl *ctrl;
	void __iomem *hw_stat;
	unsigned int i;
	u32 val;
	int ret;

	ctrl = devm_kzalloc(&pdev->dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	ctrl->dev = &pdev->dev;
	ctrl->regs = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(ctrl->regs))
		return PTR_ERR(ctrl->regs);

	hw_stat = devm_platform_get_and_ioremap_resource(pdev, 1, NULL);
	if (IS_ERR(hw_stat))
		return PTR_ERR(hw_stat);

	for (i = 0; i < SERDES_MAX; i++) {
		ret = serdes_phy_create(ctrl, i, &ctrl->phys[i]);
		if (ret)
			return ret;
	}

	val = readl(hw_stat);
	val = FIELD_GET(PLL_CONF_MASK, val);
	ctrl->ref125 = (val == PLL_CONF_125MHZ ||
			val == PLL_CONF_SERDES_125MHZ);

	dev_set_drvdata(&pdev->dev, ctrl);

	provider = devm_of_phy_provider_register(ctrl->dev,
						 serdes_simple_xlate);

	return PTR_ERR_OR_ZERO(provider);
}

static const struct of_device_id serdes_ids[] = {
	{ .compatible = "microchip,lan966x-serdes", },
	{},
};
MODULE_DEVICE_TABLE(of, serdes_ids);

static struct platform_driver mscc_lan966x_serdes = {
	.probe		= serdes_probe,
	.driver		= {
		.name	= "microchip,lan966x-serdes",
		.of_match_table = of_match_ptr(serdes_ids),
	},
};

module_platform_driver(mscc_lan966x_serdes);

MODULE_DESCRIPTION("Microchip lan966x switch serdes driver");
MODULE_AUTHOR("Horatiu Vultur <horatiu.vultur@microchip.com>");
MODULE_LICENSE("GPL v2");
