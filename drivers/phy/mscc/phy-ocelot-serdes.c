// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * SerDes PHY driver for Microsemi Ocelot
 *
 * Copyright (c) 2018 Microsemi
 *
 */

#include <linux/err.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/phy.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <soc/mscc/ocelot_hsio.h>
#include <dt-bindings/phy/phy-ocelot-serdes.h>

struct serdes_ctrl {
	struct regmap		*regs;
	struct device		*dev;
	struct phy		*phys[SERDES_MAX];
};

struct serdes_macro {
	u8			idx;
	/* Not used when in QSGMII or PCIe mode */
	int			port;
	struct serdes_ctrl	*ctrl;
};

#define MCB_S6G_CFG_TIMEOUT     50

static int __serdes_write_mcb_s6g(struct regmap *regmap, u8 macro, u32 op)
{
	unsigned int regval = 0;

	regmap_write(regmap, HSIO_MCB_S6G_ADDR_CFG, op |
		     HSIO_MCB_S6G_ADDR_CFG_SERDES6G_ADDR(BIT(macro)));

	return regmap_read_poll_timeout(regmap, HSIO_MCB_S6G_ADDR_CFG, regval,
					(regval & op) != op, 100,
					MCB_S6G_CFG_TIMEOUT * 1000);
}

static int serdes_commit_mcb_s6g(struct regmap *regmap, u8 macro)
{
	return __serdes_write_mcb_s6g(regmap, macro,
		HSIO_MCB_S6G_ADDR_CFG_SERDES6G_WR_ONE_SHOT);
}

static int serdes_update_mcb_s6g(struct regmap *regmap, u8 macro)
{
	return __serdes_write_mcb_s6g(regmap, macro,
		HSIO_MCB_S6G_ADDR_CFG_SERDES6G_RD_ONE_SHOT);
}

static int serdes_init_s6g(struct regmap *regmap, u8 serdes, int mode)
{
	u32 pll_fsm_ctrl_data;
	u32 ob_ena1v_mode;
	u32 des_bw_ana;
	u32 ob_ena_cas;
	u32 if_mode;
	u32 ob_lev;
	u32 qrate;
	int ret;

	if (mode == PHY_INTERFACE_MODE_QSGMII) {
		pll_fsm_ctrl_data = 120;
		ob_ena1v_mode = 0;
		ob_ena_cas = 0;
		des_bw_ana = 5;
		ob_lev = 24;
		if_mode = 3;
		qrate = 0;
	} else {
		pll_fsm_ctrl_data = 60;
		ob_ena1v_mode = 1;
		ob_ena_cas = 2;
		des_bw_ana = 3;
		ob_lev = 48;
		if_mode = 1;
		qrate = 1;
	}

	ret = serdes_update_mcb_s6g(regmap, serdes);
	if (ret)
		return ret;

	/* Test pattern */

	regmap_update_bits(regmap, HSIO_S6G_COMMON_CFG,
			   HSIO_S6G_COMMON_CFG_SYS_RST, 0);

	regmap_update_bits(regmap, HSIO_S6G_PLL_CFG,
			   HSIO_S6G_PLL_CFG_PLL_FSM_ENA, 0);

	regmap_update_bits(regmap, HSIO_S6G_IB_CFG,
			   HSIO_S6G_IB_CFG_IB_SIG_DET_ENA |
			   HSIO_S6G_IB_CFG_IB_REG_ENA |
			   HSIO_S6G_IB_CFG_IB_SAM_ENA |
			   HSIO_S6G_IB_CFG_IB_EQZ_ENA |
			   HSIO_S6G_IB_CFG_IB_CONCUR |
			   HSIO_S6G_IB_CFG_IB_CAL_ENA,
			   HSIO_S6G_IB_CFG_IB_SIG_DET_ENA |
			   HSIO_S6G_IB_CFG_IB_REG_ENA |
			   HSIO_S6G_IB_CFG_IB_SAM_ENA |
			   HSIO_S6G_IB_CFG_IB_EQZ_ENA |
			   HSIO_S6G_IB_CFG_IB_CONCUR);

	regmap_update_bits(regmap, HSIO_S6G_IB_CFG1,
			   HSIO_S6G_IB_CFG1_IB_FRC_OFFSET |
			   HSIO_S6G_IB_CFG1_IB_FRC_LP |
			   HSIO_S6G_IB_CFG1_IB_FRC_MID |
			   HSIO_S6G_IB_CFG1_IB_FRC_HP |
			   HSIO_S6G_IB_CFG1_IB_FILT_OFFSET |
			   HSIO_S6G_IB_CFG1_IB_FILT_LP |
			   HSIO_S6G_IB_CFG1_IB_FILT_MID |
			   HSIO_S6G_IB_CFG1_IB_FILT_HP,
			   HSIO_S6G_IB_CFG1_IB_FILT_OFFSET |
			   HSIO_S6G_IB_CFG1_IB_FILT_HP |
			   HSIO_S6G_IB_CFG1_IB_FILT_LP |
			   HSIO_S6G_IB_CFG1_IB_FILT_MID);

	regmap_update_bits(regmap, HSIO_S6G_IB_CFG2,
			   HSIO_S6G_IB_CFG2_IB_UREG_M,
			   HSIO_S6G_IB_CFG2_IB_UREG(4));

	regmap_update_bits(regmap, HSIO_S6G_IB_CFG3,
			   HSIO_S6G_IB_CFG3_IB_INI_OFFSET_M |
			   HSIO_S6G_IB_CFG3_IB_INI_LP_M |
			   HSIO_S6G_IB_CFG3_IB_INI_MID_M |
			   HSIO_S6G_IB_CFG3_IB_INI_HP_M,
			   HSIO_S6G_IB_CFG3_IB_INI_OFFSET(31) |
			   HSIO_S6G_IB_CFG3_IB_INI_LP(1) |
			   HSIO_S6G_IB_CFG3_IB_INI_MID(31) |
			   HSIO_S6G_IB_CFG3_IB_INI_HP(0));

	regmap_update_bits(regmap, HSIO_S6G_MISC_CFG,
			   HSIO_S6G_MISC_CFG_LANE_RST,
			   HSIO_S6G_MISC_CFG_LANE_RST);

	ret = serdes_commit_mcb_s6g(regmap, serdes);
	if (ret)
		return ret;

	/* OB + DES + IB + SER CFG */
	regmap_update_bits(regmap, HSIO_S6G_OB_CFG,
			   HSIO_S6G_OB_CFG_OB_IDLE |
			   HSIO_S6G_OB_CFG_OB_ENA1V_MODE |
			   HSIO_S6G_OB_CFG_OB_POST0_M |
			   HSIO_S6G_OB_CFG_OB_PREC_M,
			   (ob_ena1v_mode ? HSIO_S6G_OB_CFG_OB_ENA1V_MODE : 0) |
			   HSIO_S6G_OB_CFG_OB_POST0(0) |
			   HSIO_S6G_OB_CFG_OB_PREC(0));

	regmap_update_bits(regmap, HSIO_S6G_OB_CFG1,
			   HSIO_S6G_OB_CFG1_OB_ENA_CAS_M |
			   HSIO_S6G_OB_CFG1_OB_LEV_M,
			   HSIO_S6G_OB_CFG1_OB_LEV(ob_lev) |
			   HSIO_S6G_OB_CFG1_OB_ENA_CAS(ob_ena_cas));

	regmap_update_bits(regmap, HSIO_S6G_DES_CFG,
			   HSIO_S6G_DES_CFG_DES_PHS_CTRL_M |
			   HSIO_S6G_DES_CFG_DES_CPMD_SEL_M |
			   HSIO_S6G_DES_CFG_DES_BW_ANA_M,
			   HSIO_S6G_DES_CFG_DES_PHS_CTRL(2) |
			   HSIO_S6G_DES_CFG_DES_CPMD_SEL(0) |
			   HSIO_S6G_DES_CFG_DES_BW_ANA(des_bw_ana));

	regmap_update_bits(regmap, HSIO_S6G_IB_CFG,
			   HSIO_S6G_IB_CFG_IB_SIG_DET_CLK_SEL_M |
			   HSIO_S6G_IB_CFG_IB_REG_PAT_SEL_OFFSET_M,
			   HSIO_S6G_IB_CFG_IB_REG_PAT_SEL_OFFSET(0) |
			   HSIO_S6G_IB_CFG_IB_SIG_DET_CLK_SEL(0));

	regmap_update_bits(regmap, HSIO_S6G_IB_CFG1,
			   HSIO_S6G_IB_CFG1_IB_TSDET_M,
			   HSIO_S6G_IB_CFG1_IB_TSDET(16));

	regmap_update_bits(regmap, HSIO_S6G_SER_CFG,
			   HSIO_S6G_SER_CFG_SER_ALISEL_M |
			   HSIO_S6G_SER_CFG_SER_ENALI,
			   HSIO_S6G_SER_CFG_SER_ALISEL(0));

	regmap_update_bits(regmap, HSIO_S6G_PLL_CFG,
			   HSIO_S6G_PLL_CFG_PLL_DIV4 |
			   HSIO_S6G_PLL_CFG_PLL_ENA_ROT |
			   HSIO_S6G_PLL_CFG_PLL_FSM_CTRL_DATA_M |
			   HSIO_S6G_PLL_CFG_PLL_ROT_DIR |
			   HSIO_S6G_PLL_CFG_PLL_ROT_FRQ,
			   HSIO_S6G_PLL_CFG_PLL_FSM_CTRL_DATA
			   (pll_fsm_ctrl_data));

	regmap_update_bits(regmap, HSIO_S6G_COMMON_CFG,
			   HSIO_S6G_COMMON_CFG_SYS_RST |
			   HSIO_S6G_COMMON_CFG_ENA_LANE |
			   HSIO_S6G_COMMON_CFG_PWD_RX |
			   HSIO_S6G_COMMON_CFG_PWD_TX |
			   HSIO_S6G_COMMON_CFG_HRATE |
			   HSIO_S6G_COMMON_CFG_QRATE |
			   HSIO_S6G_COMMON_CFG_ENA_ELOOP |
			   HSIO_S6G_COMMON_CFG_ENA_FLOOP |
			   HSIO_S6G_COMMON_CFG_IF_MODE_M,
			   HSIO_S6G_COMMON_CFG_SYS_RST |
			   HSIO_S6G_COMMON_CFG_ENA_LANE |
			   (qrate ? HSIO_S6G_COMMON_CFG_QRATE : 0) |
			   HSIO_S6G_COMMON_CFG_IF_MODE(if_mode));

	regmap_update_bits(regmap, HSIO_S6G_MISC_CFG,
			   HSIO_S6G_MISC_CFG_LANE_RST |
			   HSIO_S6G_MISC_CFG_DES_100FX_CPMD_ENA |
			   HSIO_S6G_MISC_CFG_RX_LPI_MODE_ENA |
			   HSIO_S6G_MISC_CFG_TX_LPI_MODE_ENA,
			   HSIO_S6G_MISC_CFG_LANE_RST |
			   HSIO_S6G_MISC_CFG_RX_LPI_MODE_ENA);


	ret = serdes_commit_mcb_s6g(regmap, serdes);
	if (ret)
		return ret;

	regmap_update_bits(regmap, HSIO_S6G_PLL_CFG,
			   HSIO_S6G_PLL_CFG_PLL_FSM_ENA,
			   HSIO_S6G_PLL_CFG_PLL_FSM_ENA);

	ret = serdes_commit_mcb_s6g(regmap, serdes);
	if (ret)
		return ret;

	/* Wait for PLL bringup */
	msleep(20);

	regmap_update_bits(regmap, HSIO_S6G_IB_CFG,
			   HSIO_S6G_IB_CFG_IB_CAL_ENA,
			   HSIO_S6G_IB_CFG_IB_CAL_ENA);

	regmap_update_bits(regmap, HSIO_S6G_MISC_CFG,
			   HSIO_S6G_MISC_CFG_LANE_RST, 0);

	ret = serdes_commit_mcb_s6g(regmap, serdes);
	if (ret)
		return ret;

	/* Wait for calibration */
	msleep(60);

	regmap_update_bits(regmap, HSIO_S6G_IB_CFG,
			   HSIO_S6G_IB_CFG_IB_REG_PAT_SEL_OFFSET_M |
			   HSIO_S6G_IB_CFG_IB_SIG_DET_CLK_SEL_M,
			   HSIO_S6G_IB_CFG_IB_REG_PAT_SEL_OFFSET(0) |
			   HSIO_S6G_IB_CFG_IB_SIG_DET_CLK_SEL(7));

	regmap_update_bits(regmap, HSIO_S6G_IB_CFG1,
			   HSIO_S6G_IB_CFG1_IB_TSDET_M,
			   HSIO_S6G_IB_CFG1_IB_TSDET(3));

	/* IB CFG */

	return 0;
}

#define MCB_S1G_CFG_TIMEOUT     50

static int __serdes_write_mcb_s1g(struct regmap *regmap, u8 macro, u32 op)
{
	unsigned int regval;

	regmap_write(regmap, HSIO_MCB_S1G_ADDR_CFG, op |
		     HSIO_MCB_S1G_ADDR_CFG_SERDES1G_ADDR(BIT(macro)));

	return regmap_read_poll_timeout(regmap, HSIO_MCB_S1G_ADDR_CFG, regval,
					(regval & op) != op, 100,
					MCB_S1G_CFG_TIMEOUT * 1000);
}

static int serdes_commit_mcb_s1g(struct regmap *regmap, u8 macro)
{
	return __serdes_write_mcb_s1g(regmap, macro,
		HSIO_MCB_S1G_ADDR_CFG_SERDES1G_WR_ONE_SHOT);
}

static int serdes_update_mcb_s1g(struct regmap *regmap, u8 macro)
{
	return __serdes_write_mcb_s1g(regmap, macro,
		HSIO_MCB_S1G_ADDR_CFG_SERDES1G_RD_ONE_SHOT);
}

static int serdes_init_s1g(struct regmap *regmap, u8 serdes)
{
	int ret;

	ret = serdes_update_mcb_s1g(regmap, serdes);
	if (ret)
		return ret;

	regmap_update_bits(regmap, HSIO_S1G_COMMON_CFG,
			   HSIO_S1G_COMMON_CFG_SYS_RST |
			   HSIO_S1G_COMMON_CFG_ENA_LANE |
			   HSIO_S1G_COMMON_CFG_ENA_ELOOP |
			   HSIO_S1G_COMMON_CFG_ENA_FLOOP,
			   HSIO_S1G_COMMON_CFG_ENA_LANE);

	regmap_update_bits(regmap, HSIO_S1G_PLL_CFG,
			   HSIO_S1G_PLL_CFG_PLL_FSM_ENA |
			   HSIO_S1G_PLL_CFG_PLL_FSM_CTRL_DATA_M,
			   HSIO_S1G_PLL_CFG_PLL_FSM_CTRL_DATA(200) |
			   HSIO_S1G_PLL_CFG_PLL_FSM_ENA);

	regmap_update_bits(regmap, HSIO_S1G_MISC_CFG,
			   HSIO_S1G_MISC_CFG_DES_100FX_CPMD_ENA |
			   HSIO_S1G_MISC_CFG_LANE_RST,
			   HSIO_S1G_MISC_CFG_LANE_RST);

	ret = serdes_commit_mcb_s1g(regmap, serdes);
	if (ret)
		return ret;

	regmap_update_bits(regmap, HSIO_S1G_COMMON_CFG,
			   HSIO_S1G_COMMON_CFG_SYS_RST,
			   HSIO_S1G_COMMON_CFG_SYS_RST);

	regmap_update_bits(regmap, HSIO_S1G_MISC_CFG,
			   HSIO_S1G_MISC_CFG_LANE_RST, 0);

	ret = serdes_commit_mcb_s1g(regmap, serdes);
	if (ret)
		return ret;

	return 0;
}

struct serdes_mux {
	u8			idx;
	u8			port;
	enum phy_mode		mode;
	int			submode;
	u32			mask;
	u32			mux;
};

#define SERDES_MUX(_idx, _port, _mode, _submode, _mask, _mux) { \
	.idx = _idx,						\
	.port = _port,						\
	.mode = _mode,						\
	.submode = _submode,					\
	.mask = _mask,						\
	.mux = _mux,						\
}

#define SERDES_MUX_SGMII(i, p, m, c) \
	SERDES_MUX(i, p, PHY_MODE_ETHERNET, PHY_INTERFACE_MODE_SGMII, m, c)
#define SERDES_MUX_QSGMII(i, p, m, c) \
	SERDES_MUX(i, p, PHY_MODE_ETHERNET, PHY_INTERFACE_MODE_QSGMII, m, c)

static const struct serdes_mux ocelot_serdes_muxes[] = {
	SERDES_MUX_SGMII(SERDES1G(0), 0, 0, 0),
	SERDES_MUX_SGMII(SERDES1G(1), 1, HSIO_HW_CFG_DEV1G_5_MODE, 0),
	SERDES_MUX_SGMII(SERDES1G(1), 5, HSIO_HW_CFG_QSGMII_ENA |
			 HSIO_HW_CFG_DEV1G_5_MODE, HSIO_HW_CFG_DEV1G_5_MODE),
	SERDES_MUX_SGMII(SERDES1G(2), 2, HSIO_HW_CFG_DEV1G_4_MODE, 0),
	SERDES_MUX_SGMII(SERDES1G(2), 4, HSIO_HW_CFG_QSGMII_ENA |
			 HSIO_HW_CFG_DEV1G_4_MODE, HSIO_HW_CFG_DEV1G_4_MODE),
	SERDES_MUX_SGMII(SERDES1G(3), 3, HSIO_HW_CFG_DEV1G_6_MODE, 0),
	SERDES_MUX_SGMII(SERDES1G(3), 6, HSIO_HW_CFG_QSGMII_ENA |
			 HSIO_HW_CFG_DEV1G_6_MODE, HSIO_HW_CFG_DEV1G_6_MODE),
	SERDES_MUX_SGMII(SERDES1G(4), 4, HSIO_HW_CFG_QSGMII_ENA |
			 HSIO_HW_CFG_DEV1G_4_MODE | HSIO_HW_CFG_DEV1G_9_MODE,
			 0),
	SERDES_MUX_SGMII(SERDES1G(4), 9, HSIO_HW_CFG_DEV1G_4_MODE |
			 HSIO_HW_CFG_DEV1G_9_MODE, HSIO_HW_CFG_DEV1G_4_MODE |
			 HSIO_HW_CFG_DEV1G_9_MODE),
	SERDES_MUX_SGMII(SERDES1G(5), 5, HSIO_HW_CFG_QSGMII_ENA |
			 HSIO_HW_CFG_DEV1G_5_MODE | HSIO_HW_CFG_DEV2G5_10_MODE,
			 0),
	SERDES_MUX_SGMII(SERDES1G(5), 10, HSIO_HW_CFG_PCIE_ENA |
			 HSIO_HW_CFG_DEV1G_5_MODE | HSIO_HW_CFG_DEV2G5_10_MODE,
			 HSIO_HW_CFG_DEV1G_5_MODE | HSIO_HW_CFG_DEV2G5_10_MODE),
	SERDES_MUX_QSGMII(SERDES6G(0), 4, HSIO_HW_CFG_QSGMII_ENA,
			  HSIO_HW_CFG_QSGMII_ENA),
	SERDES_MUX_QSGMII(SERDES6G(0), 5, HSIO_HW_CFG_QSGMII_ENA,
			  HSIO_HW_CFG_QSGMII_ENA),
	SERDES_MUX_QSGMII(SERDES6G(0), 6, HSIO_HW_CFG_QSGMII_ENA,
			  HSIO_HW_CFG_QSGMII_ENA),
	SERDES_MUX_SGMII(SERDES6G(0), 7, HSIO_HW_CFG_QSGMII_ENA, 0),
	SERDES_MUX_QSGMII(SERDES6G(0), 7, HSIO_HW_CFG_QSGMII_ENA,
			  HSIO_HW_CFG_QSGMII_ENA),
	SERDES_MUX_SGMII(SERDES6G(1), 8, 0, 0),
	SERDES_MUX_SGMII(SERDES6G(2), 10, HSIO_HW_CFG_PCIE_ENA |
			 HSIO_HW_CFG_DEV2G5_10_MODE, 0),
	SERDES_MUX(SERDES6G(2), 10, PHY_MODE_PCIE, 0, HSIO_HW_CFG_PCIE_ENA,
		   HSIO_HW_CFG_PCIE_ENA),
};

static int serdes_set_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	struct serdes_macro *macro = phy_get_drvdata(phy);
	unsigned int i;
	int ret;

	/* As of now only PHY_MODE_ETHERNET is supported */
	if (mode != PHY_MODE_ETHERNET)
		return -EOPNOTSUPP;

	for (i = 0; i < ARRAY_SIZE(ocelot_serdes_muxes); i++) {
		if (macro->idx != ocelot_serdes_muxes[i].idx ||
		    mode != ocelot_serdes_muxes[i].mode ||
		    submode != ocelot_serdes_muxes[i].submode)
			continue;

		if (submode != PHY_INTERFACE_MODE_QSGMII &&
		    macro->port != ocelot_serdes_muxes[i].port)
			continue;

		ret = regmap_update_bits(macro->ctrl->regs, HSIO_HW_CFG,
					 ocelot_serdes_muxes[i].mask,
					 ocelot_serdes_muxes[i].mux);
		if (ret)
			return ret;

		if (macro->idx <= SERDES1G_MAX)
			return serdes_init_s1g(macro->ctrl->regs, macro->idx);
		else if (macro->idx <= SERDES6G_MAX)
			return serdes_init_s6g(macro->ctrl->regs,
					       macro->idx - (SERDES1G_MAX + 1),
					       ocelot_serdes_muxes[i].submode);

		/* PCIe not supported yet */
		return -EOPNOTSUPP;
	}

	return -EINVAL;
}

static const struct phy_ops serdes_ops = {
	.set_mode	= serdes_set_mode,
	.owner		= THIS_MODULE,
};

static struct phy *serdes_simple_xlate(struct device *dev,
				       const struct of_phandle_args *args)
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

		/* SERDES6G(0) is the only SerDes capable of QSGMII */
		if (idx != SERDES6G(0) && macro->port >= 0)
			return ERR_PTR(-EBUSY);

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
	struct resource *res;
	unsigned int i;
	int ret;

	ctrl = devm_kzalloc(&pdev->dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	ctrl->dev = &pdev->dev;
	ctrl->regs = syscon_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(ctrl->regs)) {
		/* Fall back to using IORESOURCE_REG, if possible */
		res = platform_get_resource(pdev, IORESOURCE_REG, 0);
		if (res)
			ctrl->regs = dev_get_regmap(ctrl->dev->parent,
						    res->name);
	}

	if (IS_ERR(ctrl->regs))
		return PTR_ERR(ctrl->regs);

	for (i = 0; i < SERDES_MAX; i++) {
		ret = serdes_phy_create(ctrl, i, &ctrl->phys[i]);
		if (ret)
			return ret;
	}

	dev_set_drvdata(&pdev->dev, ctrl);

	provider = devm_of_phy_provider_register(ctrl->dev,
						 serdes_simple_xlate);

	return PTR_ERR_OR_ZERO(provider);
}

static const struct of_device_id serdes_ids[] = {
	{ .compatible = "mscc,vsc7514-serdes", },
	{},
};
MODULE_DEVICE_TABLE(of, serdes_ids);

static struct platform_driver mscc_ocelot_serdes = {
	.probe		= serdes_probe,
	.driver		= {
		.name	= "mscc,ocelot-serdes",
		.of_match_table = of_match_ptr(serdes_ids),
	},
};

module_platform_driver(mscc_ocelot_serdes);

MODULE_AUTHOR("Quentin Schulz <quentin.schulz@bootlin.com>");
MODULE_DESCRIPTION("SerDes driver for Microsemi Ocelot");
MODULE_LICENSE("Dual MIT/GPL");
