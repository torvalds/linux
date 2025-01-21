// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Marvell
 *
 * Antoine Tenart <antoine.tenart@free-electrons.com>
 */

#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* Relative to priv->base */
#define MVEBU_COMPHY_SERDES_CFG0(n)		(0x0 + (n) * 0x1000)
#define     MVEBU_COMPHY_SERDES_CFG0_PU_PLL	BIT(1)
#define     MVEBU_COMPHY_SERDES_CFG0_GEN_RX(n)	((n) << 3)
#define     MVEBU_COMPHY_SERDES_CFG0_GEN_TX(n)	((n) << 7)
#define     MVEBU_COMPHY_SERDES_CFG0_PU_RX	BIT(11)
#define     MVEBU_COMPHY_SERDES_CFG0_PU_TX	BIT(12)
#define     MVEBU_COMPHY_SERDES_CFG0_HALF_BUS	BIT(14)
#define     MVEBU_COMPHY_SERDES_CFG0_RXAUI_MODE	BIT(15)
#define MVEBU_COMPHY_SERDES_CFG1(n)		(0x4 + (n) * 0x1000)
#define     MVEBU_COMPHY_SERDES_CFG1_RESET	BIT(3)
#define     MVEBU_COMPHY_SERDES_CFG1_RX_INIT	BIT(4)
#define     MVEBU_COMPHY_SERDES_CFG1_CORE_RESET	BIT(5)
#define     MVEBU_COMPHY_SERDES_CFG1_RF_RESET	BIT(6)
#define MVEBU_COMPHY_SERDES_CFG2(n)		(0x8 + (n) * 0x1000)
#define     MVEBU_COMPHY_SERDES_CFG2_DFE_EN	BIT(4)
#define MVEBU_COMPHY_SERDES_STATUS0(n)		(0x18 + (n) * 0x1000)
#define     MVEBU_COMPHY_SERDES_STATUS0_TX_PLL_RDY	BIT(2)
#define     MVEBU_COMPHY_SERDES_STATUS0_RX_PLL_RDY	BIT(3)
#define     MVEBU_COMPHY_SERDES_STATUS0_RX_INIT		BIT(4)
#define MVEBU_COMPHY_PWRPLL_CTRL(n)		(0x804 + (n) * 0x1000)
#define     MVEBU_COMPHY_PWRPLL_CTRL_RFREQ(n)	((n) << 0)
#define     MVEBU_COMPHY_PWRPLL_PHY_MODE(n)	((n) << 5)
#define MVEBU_COMPHY_IMP_CAL(n)			(0x80c + (n) * 0x1000)
#define     MVEBU_COMPHY_IMP_CAL_TX_EXT(n)	((n) << 10)
#define     MVEBU_COMPHY_IMP_CAL_TX_EXT_EN	BIT(15)
#define MVEBU_COMPHY_DFE_RES(n)			(0x81c + (n) * 0x1000)
#define     MVEBU_COMPHY_DFE_RES_FORCE_GEN_TBL	BIT(15)
#define MVEBU_COMPHY_COEF(n)			(0x828 + (n) * 0x1000)
#define     MVEBU_COMPHY_COEF_DFE_EN		BIT(14)
#define     MVEBU_COMPHY_COEF_DFE_CTRL		BIT(15)
#define MVEBU_COMPHY_GEN1_S0(n)			(0x834 + (n) * 0x1000)
#define     MVEBU_COMPHY_GEN1_S0_TX_AMP(n)	((n) << 1)
#define     MVEBU_COMPHY_GEN1_S0_TX_EMPH(n)	((n) << 7)
#define MVEBU_COMPHY_GEN1_S1(n)			(0x838 + (n) * 0x1000)
#define     MVEBU_COMPHY_GEN1_S1_RX_MUL_PI(n)	((n) << 0)
#define     MVEBU_COMPHY_GEN1_S1_RX_MUL_PF(n)	((n) << 3)
#define     MVEBU_COMPHY_GEN1_S1_RX_MUL_FI(n)	((n) << 6)
#define     MVEBU_COMPHY_GEN1_S1_RX_MUL_FF(n)	((n) << 8)
#define     MVEBU_COMPHY_GEN1_S1_RX_DFE_EN	BIT(10)
#define     MVEBU_COMPHY_GEN1_S1_RX_DIV(n)	((n) << 11)
#define MVEBU_COMPHY_GEN1_S2(n)			(0x8f4 + (n) * 0x1000)
#define     MVEBU_COMPHY_GEN1_S2_TX_EMPH(n)	((n) << 0)
#define     MVEBU_COMPHY_GEN1_S2_TX_EMPH_EN	BIT(4)
#define MVEBU_COMPHY_LOOPBACK(n)		(0x88c + (n) * 0x1000)
#define     MVEBU_COMPHY_LOOPBACK_DBUS_WIDTH(n)	((n) << 1)
#define MVEBU_COMPHY_VDD_CAL0(n)		(0x908 + (n) * 0x1000)
#define     MVEBU_COMPHY_VDD_CAL0_CONT_MODE	BIT(15)
#define MVEBU_COMPHY_EXT_SELV(n)		(0x914 + (n) * 0x1000)
#define     MVEBU_COMPHY_EXT_SELV_RX_SAMPL(n)	((n) << 5)
#define MVEBU_COMPHY_MISC_CTRL0(n)		(0x93c + (n) * 0x1000)
#define     MVEBU_COMPHY_MISC_CTRL0_ICP_FORCE	BIT(5)
#define     MVEBU_COMPHY_MISC_CTRL0_REFCLK_SEL	BIT(10)
#define MVEBU_COMPHY_RX_CTRL1(n)		(0x940 + (n) * 0x1000)
#define     MVEBU_COMPHY_RX_CTRL1_RXCLK2X_SEL	BIT(11)
#define     MVEBU_COMPHY_RX_CTRL1_CLK8T_EN	BIT(12)
#define MVEBU_COMPHY_SPEED_DIV(n)		(0x954 + (n) * 0x1000)
#define     MVEBU_COMPHY_SPEED_DIV_TX_FORCE	BIT(7)
#define MVEBU_SP_CALIB(n)			(0x96c + (n) * 0x1000)
#define     MVEBU_SP_CALIB_SAMPLER(n)		((n) << 8)
#define     MVEBU_SP_CALIB_SAMPLER_EN		BIT(12)
#define MVEBU_COMPHY_TX_SLEW_RATE(n)		(0x974 + (n) * 0x1000)
#define     MVEBU_COMPHY_TX_SLEW_RATE_EMPH(n)	((n) << 5)
#define     MVEBU_COMPHY_TX_SLEW_RATE_SLC(n)	((n) << 10)
#define MVEBU_COMPHY_DTL_CTRL(n)		(0x984 + (n) * 0x1000)
#define     MVEBU_COMPHY_DTL_CTRL_DTL_FLOOP_EN	BIT(2)
#define MVEBU_COMPHY_FRAME_DETECT0(n)		(0xa14 + (n) * 0x1000)
#define     MVEBU_COMPHY_FRAME_DETECT0_PATN(n)	((n) << 7)
#define MVEBU_COMPHY_FRAME_DETECT3(n)		(0xa20 + (n) * 0x1000)
#define     MVEBU_COMPHY_FRAME_DETECT3_LOST_TIMEOUT_EN	BIT(12)
#define MVEBU_COMPHY_DME(n)			(0xa28 + (n) * 0x1000)
#define     MVEBU_COMPHY_DME_ETH_MODE		BIT(7)
#define MVEBU_COMPHY_TRAINING0(n)		(0xa68 + (n) * 0x1000)
#define     MVEBU_COMPHY_TRAINING0_P2P_HOLD	BIT(15)
#define MVEBU_COMPHY_TRAINING5(n)		(0xaa4 + (n) * 0x1000)
#define	    MVEBU_COMPHY_TRAINING5_RX_TIMER(n)	((n) << 0)
#define MVEBU_COMPHY_TX_TRAIN_PRESET(n)		(0xb1c + (n) * 0x1000)
#define     MVEBU_COMPHY_TX_TRAIN_PRESET_16B_AUTO_EN	BIT(8)
#define     MVEBU_COMPHY_TX_TRAIN_PRESET_PRBS11		BIT(9)
#define MVEBU_COMPHY_GEN1_S3(n)			(0xc40 + (n) * 0x1000)
#define     MVEBU_COMPHY_GEN1_S3_FBCK_SEL	BIT(9)
#define MVEBU_COMPHY_GEN1_S4(n)			(0xc44 + (n) * 0x1000)
#define	    MVEBU_COMPHY_GEN1_S4_DFE_RES(n)	((n) << 8)
#define MVEBU_COMPHY_TX_PRESET(n)		(0xc68 + (n) * 0x1000)
#define     MVEBU_COMPHY_TX_PRESET_INDEX(n)	((n) << 0)
#define MVEBU_COMPHY_GEN1_S5(n)			(0xd38 + (n) * 0x1000)
#define     MVEBU_COMPHY_GEN1_S5_ICP(n)		((n) << 0)

/* Relative to priv->regmap */
#define MVEBU_COMPHY_CONF1(n)			(0x1000 + (n) * 0x28)
#define     MVEBU_COMPHY_CONF1_PWRUP		BIT(1)
#define     MVEBU_COMPHY_CONF1_USB_PCIE		BIT(2)	/* 0: Ethernet/SATA */
#define MVEBU_COMPHY_CONF6(n)			(0x1014 + (n) * 0x28)
#define     MVEBU_COMPHY_CONF6_40B		BIT(18)
#define MVEBU_COMPHY_SELECTOR			0x1140
#define     MVEBU_COMPHY_SELECTOR_PHY(n)	((n) * 0x4)
#define MVEBU_COMPHY_PIPE_SELECTOR		0x1144
#define     MVEBU_COMPHY_PIPE_SELECTOR_PIPE(n)	((n) * 0x4)
#define MVEBU_COMPHY_SD1_CTRL1			0x1148
#define     MVEBU_COMPHY_SD1_CTRL1_RXAUI1_EN	BIT(26)
#define     MVEBU_COMPHY_SD1_CTRL1_RXAUI0_EN	BIT(27)

#define MVEBU_COMPHY_LANES	6
#define MVEBU_COMPHY_PORTS	3

#define COMPHY_SIP_POWER_ON	0x82000001
#define COMPHY_SIP_POWER_OFF	0x82000002

/*
 * A lane is described by the following bitfields:
 * [ 1- 0]: COMPHY polarity invertion
 * [ 2- 7]: COMPHY speed
 * [ 5-11]: COMPHY port index
 * [12-16]: COMPHY mode
 * [17]: Clock source
 * [18-20]: PCIe width (x1, x2, x4)
 */
#define COMPHY_FW_POL_OFFSET	0
#define COMPHY_FW_POL_MASK	GENMASK(1, 0)
#define COMPHY_FW_SPEED_OFFSET	2
#define COMPHY_FW_SPEED_MASK	GENMASK(7, 2)
#define COMPHY_FW_SPEED_MAX	COMPHY_FW_SPEED_MASK
#define COMPHY_FW_SPEED_1250	0
#define COMPHY_FW_SPEED_3125	2
#define COMPHY_FW_SPEED_5000	3
#define COMPHY_FW_SPEED_515625	4
#define COMPHY_FW_SPEED_103125	6
#define COMPHY_FW_PORT_OFFSET	8
#define COMPHY_FW_PORT_MASK	GENMASK(11, 8)
#define COMPHY_FW_MODE_OFFSET	12
#define COMPHY_FW_MODE_MASK	GENMASK(16, 12)
#define COMPHY_FW_WIDTH_OFFSET	18
#define COMPHY_FW_WIDTH_MASK	GENMASK(20, 18)

#define COMPHY_FW_PARAM_FULL(mode, port, speed, pol, width)		\
	((((pol) << COMPHY_FW_POL_OFFSET) & COMPHY_FW_POL_MASK) |	\
	 (((mode) << COMPHY_FW_MODE_OFFSET) & COMPHY_FW_MODE_MASK) |	\
	 (((port) << COMPHY_FW_PORT_OFFSET) & COMPHY_FW_PORT_MASK) |	\
	 (((speed) << COMPHY_FW_SPEED_OFFSET) & COMPHY_FW_SPEED_MASK) |	\
	 (((width) << COMPHY_FW_WIDTH_OFFSET) & COMPHY_FW_WIDTH_MASK))

#define COMPHY_FW_PARAM(mode, port)					\
	COMPHY_FW_PARAM_FULL(mode, port, COMPHY_FW_SPEED_MAX, 0, 0)

#define COMPHY_FW_PARAM_ETH(mode, port, speed)				\
	COMPHY_FW_PARAM_FULL(mode, port, speed, 0, 0)

#define COMPHY_FW_PARAM_PCIE(mode, port, width)				\
	COMPHY_FW_PARAM_FULL(mode, port, COMPHY_FW_SPEED_5000, 0, width)

#define COMPHY_FW_MODE_SATA		0x1
#define COMPHY_FW_MODE_SGMII		0x2 /* SGMII 1G */
#define COMPHY_FW_MODE_2500BASEX	0x3 /* 2500BASE-X */
#define COMPHY_FW_MODE_USB3H		0x4
#define COMPHY_FW_MODE_USB3D		0x5
#define COMPHY_FW_MODE_PCIE		0x6
#define COMPHY_FW_MODE_RXAUI		0x7
#define COMPHY_FW_MODE_XFI		0x8 /* SFI: 0x9 (is treated like XFI) */

struct mvebu_comphy_conf {
	enum phy_mode mode;
	int submode;
	unsigned lane;
	unsigned port;
	u32 mux;
	u32 fw_mode;
};

#define ETH_CONF(_lane, _port, _submode, _mux, _fw)	\
	{						\
		.lane = _lane,				\
		.port = _port,				\
		.mode = PHY_MODE_ETHERNET,		\
		.submode = _submode,			\
		.mux = _mux,				\
		.fw_mode = _fw,				\
	}

#define GEN_CONF(_lane, _port, _mode, _fw)		\
	{						\
		.lane = _lane,				\
		.port = _port,				\
		.mode = _mode,				\
		.submode = PHY_INTERFACE_MODE_NA,	\
		.mux = -1,				\
		.fw_mode = _fw,				\
	}

static const struct mvebu_comphy_conf mvebu_comphy_cp110_modes[] = {
	/* lane 0 */
	GEN_CONF(0, 0, PHY_MODE_PCIE, COMPHY_FW_MODE_PCIE),
	ETH_CONF(0, 1, PHY_INTERFACE_MODE_SGMII, 0x1, COMPHY_FW_MODE_SGMII),
	ETH_CONF(0, 1, PHY_INTERFACE_MODE_2500BASEX, 0x1, COMPHY_FW_MODE_2500BASEX),
	GEN_CONF(0, 1, PHY_MODE_SATA, COMPHY_FW_MODE_SATA),
	/* lane 1 */
	GEN_CONF(1, 0, PHY_MODE_USB_HOST_SS, COMPHY_FW_MODE_USB3H),
	GEN_CONF(1, 0, PHY_MODE_USB_DEVICE_SS, COMPHY_FW_MODE_USB3D),
	GEN_CONF(1, 0, PHY_MODE_SATA, COMPHY_FW_MODE_SATA),
	GEN_CONF(1, 0, PHY_MODE_PCIE, COMPHY_FW_MODE_PCIE),
	ETH_CONF(1, 2, PHY_INTERFACE_MODE_SGMII, 0x1, COMPHY_FW_MODE_SGMII),
	ETH_CONF(1, 2, PHY_INTERFACE_MODE_2500BASEX, 0x1, COMPHY_FW_MODE_2500BASEX),
	/* lane 2 */
	ETH_CONF(2, 0, PHY_INTERFACE_MODE_SGMII, 0x1, COMPHY_FW_MODE_SGMII),
	ETH_CONF(2, 0, PHY_INTERFACE_MODE_2500BASEX, 0x1, COMPHY_FW_MODE_2500BASEX),
	ETH_CONF(2, 0, PHY_INTERFACE_MODE_RXAUI, 0x1, COMPHY_FW_MODE_RXAUI),
	ETH_CONF(2, 0, PHY_INTERFACE_MODE_5GBASER, 0x1, COMPHY_FW_MODE_XFI),
	ETH_CONF(2, 0, PHY_INTERFACE_MODE_10GBASER, 0x1, COMPHY_FW_MODE_XFI),
	GEN_CONF(2, 0, PHY_MODE_USB_HOST_SS, COMPHY_FW_MODE_USB3H),
	GEN_CONF(2, 0, PHY_MODE_SATA, COMPHY_FW_MODE_SATA),
	GEN_CONF(2, 0, PHY_MODE_PCIE, COMPHY_FW_MODE_PCIE),
	/* lane 3 */
	GEN_CONF(3, 0, PHY_MODE_PCIE, COMPHY_FW_MODE_PCIE),
	ETH_CONF(3, 1, PHY_INTERFACE_MODE_SGMII, 0x2, COMPHY_FW_MODE_SGMII),
	ETH_CONF(3, 1, PHY_INTERFACE_MODE_2500BASEX, 0x2, COMPHY_FW_MODE_2500BASEX),
	ETH_CONF(3, 1, PHY_INTERFACE_MODE_RXAUI, 0x1, COMPHY_FW_MODE_RXAUI),
	GEN_CONF(3, 1, PHY_MODE_USB_HOST_SS, COMPHY_FW_MODE_USB3H),
	GEN_CONF(3, 1, PHY_MODE_SATA, COMPHY_FW_MODE_SATA),
	/* lane 4 */
	ETH_CONF(4, 0, PHY_INTERFACE_MODE_SGMII, 0x2, COMPHY_FW_MODE_SGMII),
	ETH_CONF(4, 0, PHY_INTERFACE_MODE_2500BASEX, 0x2, COMPHY_FW_MODE_2500BASEX),
	ETH_CONF(4, 0, PHY_INTERFACE_MODE_5GBASER, 0x2, COMPHY_FW_MODE_XFI),
	ETH_CONF(4, 0, PHY_INTERFACE_MODE_10GBASER, 0x2, COMPHY_FW_MODE_XFI),
	ETH_CONF(4, 0, PHY_INTERFACE_MODE_RXAUI, 0x2, COMPHY_FW_MODE_RXAUI),
	GEN_CONF(4, 0, PHY_MODE_USB_DEVICE_SS, COMPHY_FW_MODE_USB3D),
	GEN_CONF(4, 1, PHY_MODE_USB_HOST_SS, COMPHY_FW_MODE_USB3H),
	GEN_CONF(4, 1, PHY_MODE_PCIE, COMPHY_FW_MODE_PCIE),
	ETH_CONF(4, 1, PHY_INTERFACE_MODE_SGMII, 0x1, COMPHY_FW_MODE_SGMII),
	ETH_CONF(4, 1, PHY_INTERFACE_MODE_2500BASEX, 0x1, COMPHY_FW_MODE_2500BASEX),
	ETH_CONF(4, 1, PHY_INTERFACE_MODE_5GBASER, 0x1, COMPHY_FW_MODE_XFI),
	ETH_CONF(4, 1, PHY_INTERFACE_MODE_10GBASER, -1, COMPHY_FW_MODE_XFI),
	/* lane 5 */
	ETH_CONF(5, 1, PHY_INTERFACE_MODE_RXAUI, 0x2, COMPHY_FW_MODE_RXAUI),
	GEN_CONF(5, 1, PHY_MODE_SATA, COMPHY_FW_MODE_SATA),
	ETH_CONF(5, 2, PHY_INTERFACE_MODE_SGMII, 0x1, COMPHY_FW_MODE_SGMII),
	ETH_CONF(5, 2, PHY_INTERFACE_MODE_2500BASEX, 0x1, COMPHY_FW_MODE_2500BASEX),
	GEN_CONF(5, 2, PHY_MODE_PCIE, COMPHY_FW_MODE_PCIE),
};

struct mvebu_comphy_priv {
	void __iomem *base;
	struct regmap *regmap;
	struct device *dev;
	struct clk *mg_domain_clk;
	struct clk *mg_core_clk;
	struct clk *axi_clk;
	unsigned long cp_phys;
};

struct mvebu_comphy_lane {
	struct mvebu_comphy_priv *priv;
	unsigned id;
	enum phy_mode mode;
	int submode;
	int port;
};

static int mvebu_comphy_smc(unsigned long function, unsigned long phys,
			    unsigned long lane, unsigned long mode)
{
	struct arm_smccc_res res;
	s32 ret;

	arm_smccc_smc(function, phys, lane, mode, 0, 0, 0, 0, &res);
	ret = res.a0;

	switch (ret) {
	case SMCCC_RET_SUCCESS:
		return 0;
	case SMCCC_RET_NOT_SUPPORTED:
		return -EOPNOTSUPP;
	default:
		return -EINVAL;
	}
}

static int mvebu_comphy_get_mode(bool fw_mode, int lane, int port,
				 enum phy_mode mode, int submode)
{
	int i, n = ARRAY_SIZE(mvebu_comphy_cp110_modes);
	/* Ignore PCIe submode: it represents the width */
	bool ignore_submode = (mode == PHY_MODE_PCIE);
	const struct mvebu_comphy_conf *conf;

	/* Unused PHY mux value is 0x0 */
	if (mode == PHY_MODE_INVALID)
		return 0;

	for (i = 0; i < n; i++) {
		conf = &mvebu_comphy_cp110_modes[i];
		if (conf->lane == lane &&
		    conf->port == port &&
		    conf->mode == mode &&
		    (conf->submode == submode || ignore_submode))
			break;
	}

	if (i == n)
		return -EINVAL;

	if (fw_mode)
		return conf->fw_mode;
	else
		return conf->mux;
}

static inline int mvebu_comphy_get_mux(int lane, int port,
				       enum phy_mode mode, int submode)
{
	return mvebu_comphy_get_mode(false, lane, port, mode, submode);
}

static inline int mvebu_comphy_get_fw_mode(int lane, int port,
					   enum phy_mode mode, int submode)
{
	return mvebu_comphy_get_mode(true, lane, port, mode, submode);
}

static int mvebu_comphy_ethernet_init_reset(struct mvebu_comphy_lane *lane)
{
	struct mvebu_comphy_priv *priv = lane->priv;
	u32 val;

	regmap_read(priv->regmap, MVEBU_COMPHY_CONF1(lane->id), &val);
	val &= ~MVEBU_COMPHY_CONF1_USB_PCIE;
	val |= MVEBU_COMPHY_CONF1_PWRUP;
	regmap_write(priv->regmap, MVEBU_COMPHY_CONF1(lane->id), val);

	/* Select baud rates and PLLs */
	val = readl(priv->base + MVEBU_COMPHY_SERDES_CFG0(lane->id));
	val &= ~(MVEBU_COMPHY_SERDES_CFG0_PU_PLL |
		 MVEBU_COMPHY_SERDES_CFG0_PU_RX |
		 MVEBU_COMPHY_SERDES_CFG0_PU_TX |
		 MVEBU_COMPHY_SERDES_CFG0_HALF_BUS |
		 MVEBU_COMPHY_SERDES_CFG0_GEN_RX(0xf) |
		 MVEBU_COMPHY_SERDES_CFG0_GEN_TX(0xf) |
		 MVEBU_COMPHY_SERDES_CFG0_RXAUI_MODE);

	switch (lane->submode) {
	case PHY_INTERFACE_MODE_10GBASER:
		val |= MVEBU_COMPHY_SERDES_CFG0_GEN_RX(0xe) |
		       MVEBU_COMPHY_SERDES_CFG0_GEN_TX(0xe);
		break;
	case PHY_INTERFACE_MODE_RXAUI:
		val |= MVEBU_COMPHY_SERDES_CFG0_GEN_RX(0xb) |
		       MVEBU_COMPHY_SERDES_CFG0_GEN_TX(0xb) |
		       MVEBU_COMPHY_SERDES_CFG0_RXAUI_MODE;
		break;
	case PHY_INTERFACE_MODE_2500BASEX:
		val |= MVEBU_COMPHY_SERDES_CFG0_GEN_RX(0x8) |
		       MVEBU_COMPHY_SERDES_CFG0_GEN_TX(0x8) |
		       MVEBU_COMPHY_SERDES_CFG0_HALF_BUS;
		break;
	case PHY_INTERFACE_MODE_SGMII:
		val |= MVEBU_COMPHY_SERDES_CFG0_GEN_RX(0x6) |
		       MVEBU_COMPHY_SERDES_CFG0_GEN_TX(0x6) |
		       MVEBU_COMPHY_SERDES_CFG0_HALF_BUS;
		break;
	default:
		dev_err(priv->dev,
			"unsupported comphy submode (%d) on lane %d\n",
			lane->submode,
			lane->id);
		return -ENOTSUPP;
	}

	writel(val, priv->base + MVEBU_COMPHY_SERDES_CFG0(lane->id));

	if (lane->submode == PHY_INTERFACE_MODE_RXAUI) {
		regmap_read(priv->regmap, MVEBU_COMPHY_SD1_CTRL1, &val);

		switch (lane->id) {
		case 2:
		case 3:
			val |= MVEBU_COMPHY_SD1_CTRL1_RXAUI0_EN;
			break;
		case 4:
		case 5:
			val |= MVEBU_COMPHY_SD1_CTRL1_RXAUI1_EN;
			break;
		default:
			dev_err(priv->dev,
				"RXAUI is not supported on comphy lane %d\n",
				lane->id);
			return -EINVAL;
		}

		regmap_write(priv->regmap, MVEBU_COMPHY_SD1_CTRL1, val);
	}

	/* reset */
	val = readl(priv->base + MVEBU_COMPHY_SERDES_CFG1(lane->id));
	val &= ~(MVEBU_COMPHY_SERDES_CFG1_RESET |
		 MVEBU_COMPHY_SERDES_CFG1_CORE_RESET |
		 MVEBU_COMPHY_SERDES_CFG1_RF_RESET);
	writel(val, priv->base + MVEBU_COMPHY_SERDES_CFG1(lane->id));

	/* de-assert reset */
	val = readl(priv->base + MVEBU_COMPHY_SERDES_CFG1(lane->id));
	val |= MVEBU_COMPHY_SERDES_CFG1_RESET |
	       MVEBU_COMPHY_SERDES_CFG1_CORE_RESET;
	writel(val, priv->base + MVEBU_COMPHY_SERDES_CFG1(lane->id));

	/* wait until clocks are ready */
	mdelay(1);

	/* exlicitly disable 40B, the bits isn't clear on reset */
	regmap_read(priv->regmap, MVEBU_COMPHY_CONF6(lane->id), &val);
	val &= ~MVEBU_COMPHY_CONF6_40B;
	regmap_write(priv->regmap, MVEBU_COMPHY_CONF6(lane->id), val);

	/* refclk selection */
	val = readl(priv->base + MVEBU_COMPHY_MISC_CTRL0(lane->id));
	val &= ~MVEBU_COMPHY_MISC_CTRL0_REFCLK_SEL;
	if (lane->submode == PHY_INTERFACE_MODE_10GBASER)
		val |= MVEBU_COMPHY_MISC_CTRL0_ICP_FORCE;
	writel(val, priv->base + MVEBU_COMPHY_MISC_CTRL0(lane->id));

	/* power and pll selection */
	val = readl(priv->base + MVEBU_COMPHY_PWRPLL_CTRL(lane->id));
	val &= ~(MVEBU_COMPHY_PWRPLL_CTRL_RFREQ(0x1f) |
		 MVEBU_COMPHY_PWRPLL_PHY_MODE(0x7));
	val |= MVEBU_COMPHY_PWRPLL_CTRL_RFREQ(0x1) |
	       MVEBU_COMPHY_PWRPLL_PHY_MODE(0x4);
	writel(val, priv->base + MVEBU_COMPHY_PWRPLL_CTRL(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_LOOPBACK(lane->id));
	val &= ~MVEBU_COMPHY_LOOPBACK_DBUS_WIDTH(0x7);
	val |= MVEBU_COMPHY_LOOPBACK_DBUS_WIDTH(0x1);
	writel(val, priv->base + MVEBU_COMPHY_LOOPBACK(lane->id));

	return 0;
}

static int mvebu_comphy_init_plls(struct mvebu_comphy_lane *lane)
{
	struct mvebu_comphy_priv *priv = lane->priv;
	u32 val;

	/* SERDES external config */
	val = readl(priv->base + MVEBU_COMPHY_SERDES_CFG0(lane->id));
	val |= MVEBU_COMPHY_SERDES_CFG0_PU_PLL |
	       MVEBU_COMPHY_SERDES_CFG0_PU_RX |
	       MVEBU_COMPHY_SERDES_CFG0_PU_TX;
	writel(val, priv->base + MVEBU_COMPHY_SERDES_CFG0(lane->id));

	/* check rx/tx pll */
	readl_poll_timeout(priv->base + MVEBU_COMPHY_SERDES_STATUS0(lane->id),
			   val,
			   val & (MVEBU_COMPHY_SERDES_STATUS0_RX_PLL_RDY |
				  MVEBU_COMPHY_SERDES_STATUS0_TX_PLL_RDY),
			   1000, 150000);
	if (!(val & (MVEBU_COMPHY_SERDES_STATUS0_RX_PLL_RDY |
		     MVEBU_COMPHY_SERDES_STATUS0_TX_PLL_RDY)))
		return -ETIMEDOUT;

	/* rx init */
	val = readl(priv->base + MVEBU_COMPHY_SERDES_CFG1(lane->id));
	val |= MVEBU_COMPHY_SERDES_CFG1_RX_INIT;
	writel(val, priv->base + MVEBU_COMPHY_SERDES_CFG1(lane->id));

	/* check rx */
	readl_poll_timeout(priv->base + MVEBU_COMPHY_SERDES_STATUS0(lane->id),
			   val, val & MVEBU_COMPHY_SERDES_STATUS0_RX_INIT,
			   1000, 10000);
	if (!(val & MVEBU_COMPHY_SERDES_STATUS0_RX_INIT))
		return -ETIMEDOUT;

	val = readl(priv->base + MVEBU_COMPHY_SERDES_CFG1(lane->id));
	val &= ~MVEBU_COMPHY_SERDES_CFG1_RX_INIT;
	writel(val, priv->base + MVEBU_COMPHY_SERDES_CFG1(lane->id));

	return 0;
}

static int mvebu_comphy_set_mode_sgmii(struct phy *phy)
{
	struct mvebu_comphy_lane *lane = phy_get_drvdata(phy);
	struct mvebu_comphy_priv *priv = lane->priv;
	u32 val;
	int err;

	err = mvebu_comphy_ethernet_init_reset(lane);
	if (err)
		return err;

	val = readl(priv->base + MVEBU_COMPHY_RX_CTRL1(lane->id));
	val &= ~MVEBU_COMPHY_RX_CTRL1_CLK8T_EN;
	val |= MVEBU_COMPHY_RX_CTRL1_RXCLK2X_SEL;
	writel(val, priv->base + MVEBU_COMPHY_RX_CTRL1(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_DTL_CTRL(lane->id));
	val &= ~MVEBU_COMPHY_DTL_CTRL_DTL_FLOOP_EN;
	writel(val, priv->base + MVEBU_COMPHY_DTL_CTRL(lane->id));

	regmap_read(priv->regmap, MVEBU_COMPHY_CONF1(lane->id), &val);
	val &= ~MVEBU_COMPHY_CONF1_USB_PCIE;
	val |= MVEBU_COMPHY_CONF1_PWRUP;
	regmap_write(priv->regmap, MVEBU_COMPHY_CONF1(lane->id), val);

	val = readl(priv->base + MVEBU_COMPHY_GEN1_S0(lane->id));
	val &= ~MVEBU_COMPHY_GEN1_S0_TX_EMPH(0xf);
	val |= MVEBU_COMPHY_GEN1_S0_TX_EMPH(0x1);
	writel(val, priv->base + MVEBU_COMPHY_GEN1_S0(lane->id));

	return mvebu_comphy_init_plls(lane);
}

static int mvebu_comphy_set_mode_rxaui(struct phy *phy)
{
	struct mvebu_comphy_lane *lane = phy_get_drvdata(phy);
	struct mvebu_comphy_priv *priv = lane->priv;
	u32 val;
	int err;

	err = mvebu_comphy_ethernet_init_reset(lane);
	if (err)
		return err;

	val = readl(priv->base + MVEBU_COMPHY_RX_CTRL1(lane->id));
	val |= MVEBU_COMPHY_RX_CTRL1_RXCLK2X_SEL |
	       MVEBU_COMPHY_RX_CTRL1_CLK8T_EN;
	writel(val, priv->base + MVEBU_COMPHY_RX_CTRL1(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_DTL_CTRL(lane->id));
	val |= MVEBU_COMPHY_DTL_CTRL_DTL_FLOOP_EN;
	writel(val, priv->base + MVEBU_COMPHY_DTL_CTRL(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_SERDES_CFG2(lane->id));
	val |= MVEBU_COMPHY_SERDES_CFG2_DFE_EN;
	writel(val, priv->base + MVEBU_COMPHY_SERDES_CFG2(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_DFE_RES(lane->id));
	val |= MVEBU_COMPHY_DFE_RES_FORCE_GEN_TBL;
	writel(val, priv->base + MVEBU_COMPHY_DFE_RES(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_GEN1_S0(lane->id));
	val &= ~MVEBU_COMPHY_GEN1_S0_TX_EMPH(0xf);
	val |= MVEBU_COMPHY_GEN1_S0_TX_EMPH(0xd);
	writel(val, priv->base + MVEBU_COMPHY_GEN1_S0(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_GEN1_S1(lane->id));
	val &= ~(MVEBU_COMPHY_GEN1_S1_RX_MUL_PI(0x7) |
		 MVEBU_COMPHY_GEN1_S1_RX_MUL_PF(0x7));
	val |= MVEBU_COMPHY_GEN1_S1_RX_MUL_PI(0x1) |
	       MVEBU_COMPHY_GEN1_S1_RX_MUL_PF(0x1) |
	       MVEBU_COMPHY_GEN1_S1_RX_DFE_EN;
	writel(val, priv->base + MVEBU_COMPHY_GEN1_S1(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_COEF(lane->id));
	val &= ~(MVEBU_COMPHY_COEF_DFE_EN | MVEBU_COMPHY_COEF_DFE_CTRL);
	writel(val, priv->base + MVEBU_COMPHY_COEF(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_GEN1_S4(lane->id));
	val &= ~MVEBU_COMPHY_GEN1_S4_DFE_RES(0x3);
	val |= MVEBU_COMPHY_GEN1_S4_DFE_RES(0x1);
	writel(val, priv->base + MVEBU_COMPHY_GEN1_S4(lane->id));

	return mvebu_comphy_init_plls(lane);
}

static int mvebu_comphy_set_mode_10gbaser(struct phy *phy)
{
	struct mvebu_comphy_lane *lane = phy_get_drvdata(phy);
	struct mvebu_comphy_priv *priv = lane->priv;
	u32 val;
	int err;

	err = mvebu_comphy_ethernet_init_reset(lane);
	if (err)
		return err;

	val = readl(priv->base + MVEBU_COMPHY_RX_CTRL1(lane->id));
	val |= MVEBU_COMPHY_RX_CTRL1_RXCLK2X_SEL |
	       MVEBU_COMPHY_RX_CTRL1_CLK8T_EN;
	writel(val, priv->base + MVEBU_COMPHY_RX_CTRL1(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_DTL_CTRL(lane->id));
	val |= MVEBU_COMPHY_DTL_CTRL_DTL_FLOOP_EN;
	writel(val, priv->base + MVEBU_COMPHY_DTL_CTRL(lane->id));

	/* Speed divider */
	val = readl(priv->base + MVEBU_COMPHY_SPEED_DIV(lane->id));
	val |= MVEBU_COMPHY_SPEED_DIV_TX_FORCE;
	writel(val, priv->base + MVEBU_COMPHY_SPEED_DIV(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_SERDES_CFG2(lane->id));
	val |= MVEBU_COMPHY_SERDES_CFG2_DFE_EN;
	writel(val, priv->base + MVEBU_COMPHY_SERDES_CFG2(lane->id));

	/* DFE resolution */
	val = readl(priv->base + MVEBU_COMPHY_DFE_RES(lane->id));
	val |= MVEBU_COMPHY_DFE_RES_FORCE_GEN_TBL;
	writel(val, priv->base + MVEBU_COMPHY_DFE_RES(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_GEN1_S0(lane->id));
	val &= ~(MVEBU_COMPHY_GEN1_S0_TX_AMP(0x1f) |
		 MVEBU_COMPHY_GEN1_S0_TX_EMPH(0xf));
	val |= MVEBU_COMPHY_GEN1_S0_TX_AMP(0x1c) |
	       MVEBU_COMPHY_GEN1_S0_TX_EMPH(0xe);
	writel(val, priv->base + MVEBU_COMPHY_GEN1_S0(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_GEN1_S2(lane->id));
	val &= ~MVEBU_COMPHY_GEN1_S2_TX_EMPH(0xf);
	val |= MVEBU_COMPHY_GEN1_S2_TX_EMPH_EN;
	writel(val, priv->base + MVEBU_COMPHY_GEN1_S2(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_TX_SLEW_RATE(lane->id));
	val |= MVEBU_COMPHY_TX_SLEW_RATE_EMPH(0x3) |
	       MVEBU_COMPHY_TX_SLEW_RATE_SLC(0x3f);
	writel(val, priv->base + MVEBU_COMPHY_TX_SLEW_RATE(lane->id));

	/* Impedance calibration */
	val = readl(priv->base + MVEBU_COMPHY_IMP_CAL(lane->id));
	val &= ~MVEBU_COMPHY_IMP_CAL_TX_EXT(0x1f);
	val |= MVEBU_COMPHY_IMP_CAL_TX_EXT(0xe) |
	       MVEBU_COMPHY_IMP_CAL_TX_EXT_EN;
	writel(val, priv->base + MVEBU_COMPHY_IMP_CAL(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_GEN1_S5(lane->id));
	val &= ~MVEBU_COMPHY_GEN1_S5_ICP(0xf);
	writel(val, priv->base + MVEBU_COMPHY_GEN1_S5(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_GEN1_S1(lane->id));
	val &= ~(MVEBU_COMPHY_GEN1_S1_RX_MUL_PI(0x7) |
		 MVEBU_COMPHY_GEN1_S1_RX_MUL_PF(0x7) |
		 MVEBU_COMPHY_GEN1_S1_RX_MUL_FI(0x3) |
		 MVEBU_COMPHY_GEN1_S1_RX_MUL_FF(0x3));
	val |= MVEBU_COMPHY_GEN1_S1_RX_DFE_EN |
	       MVEBU_COMPHY_GEN1_S1_RX_MUL_PI(0x2) |
	       MVEBU_COMPHY_GEN1_S1_RX_MUL_PF(0x2) |
	       MVEBU_COMPHY_GEN1_S1_RX_MUL_FF(0x1) |
	       MVEBU_COMPHY_GEN1_S1_RX_DIV(0x3);
	writel(val, priv->base + MVEBU_COMPHY_GEN1_S1(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_COEF(lane->id));
	val &= ~(MVEBU_COMPHY_COEF_DFE_EN | MVEBU_COMPHY_COEF_DFE_CTRL);
	writel(val, priv->base + MVEBU_COMPHY_COEF(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_GEN1_S4(lane->id));
	val &= ~MVEBU_COMPHY_GEN1_S4_DFE_RES(0x3);
	val |= MVEBU_COMPHY_GEN1_S4_DFE_RES(0x1);
	writel(val, priv->base + MVEBU_COMPHY_GEN1_S4(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_GEN1_S3(lane->id));
	val |= MVEBU_COMPHY_GEN1_S3_FBCK_SEL;
	writel(val, priv->base + MVEBU_COMPHY_GEN1_S3(lane->id));

	/* rx training timer */
	val = readl(priv->base + MVEBU_COMPHY_TRAINING5(lane->id));
	val &= ~MVEBU_COMPHY_TRAINING5_RX_TIMER(0x3ff);
	val |= MVEBU_COMPHY_TRAINING5_RX_TIMER(0x13);
	writel(val, priv->base + MVEBU_COMPHY_TRAINING5(lane->id));

	/* tx train peak to peak hold */
	val = readl(priv->base + MVEBU_COMPHY_TRAINING0(lane->id));
	val |= MVEBU_COMPHY_TRAINING0_P2P_HOLD;
	writel(val, priv->base + MVEBU_COMPHY_TRAINING0(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_TX_PRESET(lane->id));
	val &= ~MVEBU_COMPHY_TX_PRESET_INDEX(0xf);
	val |= MVEBU_COMPHY_TX_PRESET_INDEX(0x2);	/* preset coeff */
	writel(val, priv->base + MVEBU_COMPHY_TX_PRESET(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_FRAME_DETECT3(lane->id));
	val &= ~MVEBU_COMPHY_FRAME_DETECT3_LOST_TIMEOUT_EN;
	writel(val, priv->base + MVEBU_COMPHY_FRAME_DETECT3(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_TX_TRAIN_PRESET(lane->id));
	val |= MVEBU_COMPHY_TX_TRAIN_PRESET_16B_AUTO_EN |
	       MVEBU_COMPHY_TX_TRAIN_PRESET_PRBS11;
	writel(val, priv->base + MVEBU_COMPHY_TX_TRAIN_PRESET(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_FRAME_DETECT0(lane->id));
	val &= ~MVEBU_COMPHY_FRAME_DETECT0_PATN(0x1ff);
	val |= MVEBU_COMPHY_FRAME_DETECT0_PATN(0x88);
	writel(val, priv->base + MVEBU_COMPHY_FRAME_DETECT0(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_DME(lane->id));
	val |= MVEBU_COMPHY_DME_ETH_MODE;
	writel(val, priv->base + MVEBU_COMPHY_DME(lane->id));

	val = readl(priv->base + MVEBU_COMPHY_VDD_CAL0(lane->id));
	val |= MVEBU_COMPHY_VDD_CAL0_CONT_MODE;
	writel(val, priv->base + MVEBU_COMPHY_VDD_CAL0(lane->id));

	val = readl(priv->base + MVEBU_SP_CALIB(lane->id));
	val &= ~MVEBU_SP_CALIB_SAMPLER(0x3);
	val |= MVEBU_SP_CALIB_SAMPLER(0x3) |
	       MVEBU_SP_CALIB_SAMPLER_EN;
	writel(val, priv->base + MVEBU_SP_CALIB(lane->id));
	val &= ~MVEBU_SP_CALIB_SAMPLER_EN;
	writel(val, priv->base + MVEBU_SP_CALIB(lane->id));

	/* External rx regulator */
	val = readl(priv->base + MVEBU_COMPHY_EXT_SELV(lane->id));
	val &= ~MVEBU_COMPHY_EXT_SELV_RX_SAMPL(0x1f);
	val |= MVEBU_COMPHY_EXT_SELV_RX_SAMPL(0x1a);
	writel(val, priv->base + MVEBU_COMPHY_EXT_SELV(lane->id));

	return mvebu_comphy_init_plls(lane);
}

static int mvebu_comphy_power_on_legacy(struct phy *phy)
{
	struct mvebu_comphy_lane *lane = phy_get_drvdata(phy);
	struct mvebu_comphy_priv *priv = lane->priv;
	int ret, mux;
	u32 val;

	mux = mvebu_comphy_get_mux(lane->id, lane->port,
				   lane->mode, lane->submode);
	if (mux < 0)
		return -ENOTSUPP;

	regmap_read(priv->regmap, MVEBU_COMPHY_PIPE_SELECTOR, &val);
	val &= ~(0xf << MVEBU_COMPHY_PIPE_SELECTOR_PIPE(lane->id));
	regmap_write(priv->regmap, MVEBU_COMPHY_PIPE_SELECTOR, val);

	regmap_read(priv->regmap, MVEBU_COMPHY_SELECTOR, &val);
	val &= ~(0xf << MVEBU_COMPHY_SELECTOR_PHY(lane->id));
	val |= mux << MVEBU_COMPHY_SELECTOR_PHY(lane->id);
	regmap_write(priv->regmap, MVEBU_COMPHY_SELECTOR, val);

	switch (lane->submode) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_2500BASEX:
		ret = mvebu_comphy_set_mode_sgmii(phy);
		break;
	case PHY_INTERFACE_MODE_RXAUI:
		ret = mvebu_comphy_set_mode_rxaui(phy);
		break;
	case PHY_INTERFACE_MODE_10GBASER:
		ret = mvebu_comphy_set_mode_10gbaser(phy);
		break;
	default:
		return -ENOTSUPP;
	}

	/* digital reset */
	val = readl(priv->base + MVEBU_COMPHY_SERDES_CFG1(lane->id));
	val |= MVEBU_COMPHY_SERDES_CFG1_RF_RESET;
	writel(val, priv->base + MVEBU_COMPHY_SERDES_CFG1(lane->id));

	return ret;
}

static int mvebu_comphy_power_on(struct phy *phy)
{
	struct mvebu_comphy_lane *lane = phy_get_drvdata(phy);
	struct mvebu_comphy_priv *priv = lane->priv;
	int fw_mode, fw_speed;
	u32 fw_param = 0;
	int ret;

	fw_mode = mvebu_comphy_get_fw_mode(lane->id, lane->port,
					   lane->mode, lane->submode);
	if (fw_mode < 0)
		goto try_legacy;

	/* Try SMC flow first */
	switch (lane->mode) {
	case PHY_MODE_ETHERNET:
		switch (lane->submode) {
		case PHY_INTERFACE_MODE_RXAUI:
			dev_dbg(priv->dev, "set lane %d to RXAUI mode\n",
				lane->id);
			fw_speed = 0;
			break;
		case PHY_INTERFACE_MODE_SGMII:
			dev_dbg(priv->dev, "set lane %d to 1000BASE-X mode\n",
				lane->id);
			fw_speed = COMPHY_FW_SPEED_1250;
			break;
		case PHY_INTERFACE_MODE_2500BASEX:
			dev_dbg(priv->dev, "set lane %d to 2500BASE-X mode\n",
				lane->id);
			fw_speed = COMPHY_FW_SPEED_3125;
			break;
		case PHY_INTERFACE_MODE_5GBASER:
			dev_dbg(priv->dev, "set lane %d to 5GBASE-R mode\n",
				lane->id);
			fw_speed = COMPHY_FW_SPEED_515625;
			break;
		case PHY_INTERFACE_MODE_10GBASER:
			dev_dbg(priv->dev, "set lane %d to 10GBASE-R mode\n",
				lane->id);
			fw_speed = COMPHY_FW_SPEED_103125;
			break;
		default:
			dev_err(priv->dev, "unsupported Ethernet mode (%d)\n",
				lane->submode);
			return -ENOTSUPP;
		}
		fw_param = COMPHY_FW_PARAM_ETH(fw_mode, lane->port, fw_speed);
		break;
	case PHY_MODE_USB_HOST_SS:
	case PHY_MODE_USB_DEVICE_SS:
		dev_dbg(priv->dev, "set lane %d to USB3 mode\n", lane->id);
		fw_param = COMPHY_FW_PARAM(fw_mode, lane->port);
		break;
	case PHY_MODE_SATA:
		dev_dbg(priv->dev, "set lane %d to SATA mode\n", lane->id);
		fw_param = COMPHY_FW_PARAM(fw_mode, lane->port);
		break;
	case PHY_MODE_PCIE:
		dev_dbg(priv->dev, "set lane %d to PCIe mode (x%d)\n", lane->id,
			lane->submode);
		fw_param = COMPHY_FW_PARAM_PCIE(fw_mode, lane->port,
						lane->submode);
		break;
	default:
		dev_err(priv->dev, "unsupported PHY mode (%d)\n", lane->mode);
		return -ENOTSUPP;
	}

	ret = mvebu_comphy_smc(COMPHY_SIP_POWER_ON, priv->cp_phys, lane->id,
			       fw_param);
	if (!ret)
		return ret;

	if (ret == -EOPNOTSUPP)
		dev_err(priv->dev,
			"unsupported SMC call, try updating your firmware\n");

	dev_warn(priv->dev,
		 "Firmware could not configure PHY %d with mode %d (ret: %d), trying legacy method\n",
		 lane->id, lane->mode, ret);

try_legacy:
	/* Fallback to Linux's implementation */
	return mvebu_comphy_power_on_legacy(phy);
}

static int mvebu_comphy_set_mode(struct phy *phy,
				 enum phy_mode mode, int submode)
{
	struct mvebu_comphy_lane *lane = phy_get_drvdata(phy);

	if (submode == PHY_INTERFACE_MODE_1000BASEX)
		submode = PHY_INTERFACE_MODE_SGMII;

	if (mvebu_comphy_get_fw_mode(lane->id, lane->port, mode, submode) < 0)
		return -EINVAL;

	lane->mode = mode;
	lane->submode = submode;

	/* PCIe submode represents the width */
	if (mode == PHY_MODE_PCIE && !lane->submode)
		lane->submode = 1;

	return 0;
}

static int mvebu_comphy_power_off_legacy(struct phy *phy)
{
	struct mvebu_comphy_lane *lane = phy_get_drvdata(phy);
	struct mvebu_comphy_priv *priv = lane->priv;
	u32 val;

	val = readl(priv->base + MVEBU_COMPHY_SERDES_CFG1(lane->id));
	val &= ~(MVEBU_COMPHY_SERDES_CFG1_RESET |
		 MVEBU_COMPHY_SERDES_CFG1_CORE_RESET |
		 MVEBU_COMPHY_SERDES_CFG1_RF_RESET);
	writel(val, priv->base + MVEBU_COMPHY_SERDES_CFG1(lane->id));

	regmap_read(priv->regmap, MVEBU_COMPHY_SELECTOR, &val);
	val &= ~(0xf << MVEBU_COMPHY_SELECTOR_PHY(lane->id));
	regmap_write(priv->regmap, MVEBU_COMPHY_SELECTOR, val);

	regmap_read(priv->regmap, MVEBU_COMPHY_PIPE_SELECTOR, &val);
	val &= ~(0xf << MVEBU_COMPHY_PIPE_SELECTOR_PIPE(lane->id));
	regmap_write(priv->regmap, MVEBU_COMPHY_PIPE_SELECTOR, val);

	return 0;
}

static int mvebu_comphy_power_off(struct phy *phy)
{
	struct mvebu_comphy_lane *lane = phy_get_drvdata(phy);
	struct mvebu_comphy_priv *priv = lane->priv;
	int ret;

	ret = mvebu_comphy_smc(COMPHY_SIP_POWER_OFF, priv->cp_phys,
			       lane->id, 0);
	if (!ret)
		return ret;

	/* Fallback to Linux's implementation */
	return mvebu_comphy_power_off_legacy(phy);
}

static const struct phy_ops mvebu_comphy_ops = {
	.power_on	= mvebu_comphy_power_on,
	.power_off	= mvebu_comphy_power_off,
	.set_mode	= mvebu_comphy_set_mode,
	.owner		= THIS_MODULE,
};

static struct phy *mvebu_comphy_xlate(struct device *dev,
				      const struct of_phandle_args *args)
{
	struct mvebu_comphy_lane *lane;
	struct phy *phy;

	if (WARN_ON(args->args[0] >= MVEBU_COMPHY_PORTS))
		return ERR_PTR(-EINVAL);

	phy = of_phy_simple_xlate(dev, args);
	if (IS_ERR(phy))
		return phy;

	lane = phy_get_drvdata(phy);
	lane->port = args->args[0];

	return phy;
}

static int mvebu_comphy_init_clks(struct mvebu_comphy_priv *priv)
{
	int ret;

	priv->mg_domain_clk = devm_clk_get(priv->dev, "mg_clk");
	if (IS_ERR(priv->mg_domain_clk))
		return PTR_ERR(priv->mg_domain_clk);

	ret = clk_prepare_enable(priv->mg_domain_clk);
	if (ret < 0)
		return ret;

	priv->mg_core_clk = devm_clk_get(priv->dev, "mg_core_clk");
	if (IS_ERR(priv->mg_core_clk)) {
		ret = PTR_ERR(priv->mg_core_clk);
		goto dis_mg_domain_clk;
	}

	ret = clk_prepare_enable(priv->mg_core_clk);
	if (ret < 0)
		goto dis_mg_domain_clk;

	priv->axi_clk = devm_clk_get(priv->dev, "axi_clk");
	if (IS_ERR(priv->axi_clk)) {
		ret = PTR_ERR(priv->axi_clk);
		goto dis_mg_core_clk;
	}

	ret = clk_prepare_enable(priv->axi_clk);
	if (ret < 0)
		goto dis_mg_core_clk;

	return 0;

dis_mg_core_clk:
	clk_disable_unprepare(priv->mg_core_clk);

dis_mg_domain_clk:
	clk_disable_unprepare(priv->mg_domain_clk);

	priv->mg_domain_clk = NULL;
	priv->mg_core_clk = NULL;
	priv->axi_clk = NULL;

	return ret;
};

static void mvebu_comphy_disable_unprepare_clks(struct mvebu_comphy_priv *priv)
{
	if (priv->axi_clk)
		clk_disable_unprepare(priv->axi_clk);

	if (priv->mg_core_clk)
		clk_disable_unprepare(priv->mg_core_clk);

	if (priv->mg_domain_clk)
		clk_disable_unprepare(priv->mg_domain_clk);
}

static int mvebu_comphy_probe(struct platform_device *pdev)
{
	struct mvebu_comphy_priv *priv;
	struct phy_provider *provider;
	struct device_node *child;
	struct resource *res;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	priv->regmap =
		syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						"marvell,system-controller");
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);
	priv->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	/*
	 * Ignore error if clocks have not been initialized properly for DT
	 * compatibility reasons.
	 */
	ret = mvebu_comphy_init_clks(priv);
	if (ret) {
		if (ret == -EPROBE_DEFER)
			return ret;
		dev_warn(&pdev->dev, "cannot initialize clocks\n");
	}

	/*
	 * Hack to retrieve a physical offset relative to this CP that will be
	 * given to the firmware
	 */
	priv->cp_phys = res->start;

	for_each_available_child_of_node(pdev->dev.of_node, child) {
		struct mvebu_comphy_lane *lane;
		struct phy *phy;
		u32 val;

		ret = of_property_read_u32(child, "reg", &val);
		if (ret < 0) {
			dev_err(&pdev->dev, "missing 'reg' property (%d)\n",
				ret);
			continue;
		}

		if (val >= MVEBU_COMPHY_LANES) {
			dev_err(&pdev->dev, "invalid 'reg' property\n");
			continue;
		}

		lane = devm_kzalloc(&pdev->dev, sizeof(*lane), GFP_KERNEL);
		if (!lane) {
			of_node_put(child);
			ret = -ENOMEM;
			goto disable_clks;
		}

		phy = devm_phy_create(&pdev->dev, child, &mvebu_comphy_ops);
		if (IS_ERR(phy)) {
			of_node_put(child);
			ret = PTR_ERR(phy);
			goto disable_clks;
		}

		lane->priv = priv;
		lane->mode = PHY_MODE_INVALID;
		lane->submode = PHY_INTERFACE_MODE_NA;
		lane->id = val;
		lane->port = -1;
		phy_set_drvdata(phy, lane);

		/*
		 * All modes are supported in this driver so we could call
		 * mvebu_comphy_power_off(phy) here to avoid relying on the
		 * bootloader/firmware configuration, but for compatibility
		 * reasons we cannot de-configure the COMPHY without being sure
		 * that the firmware is up-to-date and fully-featured.
		 */
	}

	dev_set_drvdata(&pdev->dev, priv);
	provider = devm_of_phy_provider_register(&pdev->dev,
						 mvebu_comphy_xlate);

	return PTR_ERR_OR_ZERO(provider);

disable_clks:
	mvebu_comphy_disable_unprepare_clks(priv);

	return ret;
}

static const struct of_device_id mvebu_comphy_of_match_table[] = {
	{ .compatible = "marvell,comphy-cp110" },
	{ },
};
MODULE_DEVICE_TABLE(of, mvebu_comphy_of_match_table);

static struct platform_driver mvebu_comphy_driver = {
	.probe	= mvebu_comphy_probe,
	.driver	= {
		.name = "mvebu-comphy",
		.of_match_table = mvebu_comphy_of_match_table,
	},
};
module_platform_driver(mvebu_comphy_driver);

MODULE_AUTHOR("Antoine Tenart <antoine.tenart@free-electrons.com>");
MODULE_DESCRIPTION("Common PHY driver for mvebu SoCs");
MODULE_LICENSE("GPL v2");
