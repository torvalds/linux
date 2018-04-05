/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 Jernej Skrabec <jernej.skrabec@siol.net>
 */

#ifndef _SUN8I_DW_HDMI_H_
#define _SUN8I_DW_HDMI_H_

#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_encoder.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#define SUN8I_HDMI_PHY_DBG_CTRL_REG	0x0000
#define SUN8I_HDMI_PHY_DBG_CTRL_PX_LOCK		BIT(0)
#define SUN8I_HDMI_PHY_DBG_CTRL_POL_MASK	GENMASK(15, 8)
#define SUN8I_HDMI_PHY_DBG_CTRL_POL_NHSYNC	BIT(8)
#define SUN8I_HDMI_PHY_DBG_CTRL_POL_NVSYNC	BIT(9)
#define SUN8I_HDMI_PHY_DBG_CTRL_ADDR_MASK	GENMASK(23, 16)
#define SUN8I_HDMI_PHY_DBG_CTRL_ADDR(addr)	(addr << 16)

#define SUN8I_HDMI_PHY_REXT_CTRL_REG	0x0004
#define SUN8I_HDMI_PHY_REXT_CTRL_REXT_EN	BIT(31)

#define SUN8I_HDMI_PHY_READ_EN_REG	0x0010
#define SUN8I_HDMI_PHY_READ_EN_MAGIC		0x54524545

#define SUN8I_HDMI_PHY_UNSCRAMBLE_REG	0x0014
#define SUN8I_HDMI_PHY_UNSCRAMBLE_MAGIC		0x42494E47

#define SUN8I_HDMI_PHY_ANA_CFG1_REG	0x0020
#define SUN8I_HDMI_PHY_ANA_CFG1_REG_SWI		BIT(31)
#define SUN8I_HDMI_PHY_ANA_CFG1_REG_PWEND	BIT(30)
#define SUN8I_HDMI_PHY_ANA_CFG1_REG_PWENC	BIT(29)
#define SUN8I_HDMI_PHY_ANA_CFG1_REG_CALSW	BIT(28)
#define SUN8I_HDMI_PHY_ANA_CFG1_REG_SVRCAL(x)	((x) << 26)
#define SUN8I_HDMI_PHY_ANA_CFG1_REG_SVBH(x)	((x) << 24)
#define SUN8I_HDMI_PHY_ANA_CFG1_AMP_OPT		BIT(23)
#define SUN8I_HDMI_PHY_ANA_CFG1_EMP_OPT		BIT(22)
#define SUN8I_HDMI_PHY_ANA_CFG1_AMPCK_OPT	BIT(21)
#define SUN8I_HDMI_PHY_ANA_CFG1_EMPCK_OPT	BIT(20)
#define SUN8I_HDMI_PHY_ANA_CFG1_ENRCAL		BIT(19)
#define SUN8I_HDMI_PHY_ANA_CFG1_ENCALOG		BIT(18)
#define SUN8I_HDMI_PHY_ANA_CFG1_REG_SCKTMDS	BIT(17)
#define SUN8I_HDMI_PHY_ANA_CFG1_TMDSCLK_EN	BIT(16)
#define SUN8I_HDMI_PHY_ANA_CFG1_TXEN_MASK	GENMASK(15, 12)
#define SUN8I_HDMI_PHY_ANA_CFG1_TXEN_ALL	(0xf << 12)
#define SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDSCLK	BIT(11)
#define SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS2	BIT(10)
#define SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS1	BIT(9)
#define SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS0	BIT(8)
#define SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDSCLK	BIT(7)
#define SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS2	BIT(6)
#define SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS1	BIT(5)
#define SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS0	BIT(4)
#define SUN8I_HDMI_PHY_ANA_CFG1_CKEN		BIT(3)
#define SUN8I_HDMI_PHY_ANA_CFG1_LDOEN		BIT(2)
#define SUN8I_HDMI_PHY_ANA_CFG1_ENVBS		BIT(1)
#define SUN8I_HDMI_PHY_ANA_CFG1_ENBI		BIT(0)

#define SUN8I_HDMI_PHY_ANA_CFG2_REG	0x0024
#define SUN8I_HDMI_PHY_ANA_CFG2_M_EN		BIT(31)
#define SUN8I_HDMI_PHY_ANA_CFG2_PLLDBEN		BIT(30)
#define SUN8I_HDMI_PHY_ANA_CFG2_SEN		BIT(29)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_HPDPD	BIT(28)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_HPDEN	BIT(27)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_PLRCK	BIT(26)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_PLR(x)	((x) << 23)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_DENCK	BIT(22)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_DEN		BIT(21)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_CD(x)	((x) << 19)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_CKSS(x)	((x) << 17)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_BIGSWCK	BIT(16)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_BIGSW	BIT(15)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_CSMPS(x)	((x) << 13)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_SLV(x)	((x) << 10)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_BOOSTCK(x)	((x) << 8)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_BOOST(x)	((x) << 6)
#define SUN8I_HDMI_PHY_ANA_CFG2_REG_RESDI(x)	((x) << 0)

#define SUN8I_HDMI_PHY_ANA_CFG3_REG	0x0028
#define SUN8I_HDMI_PHY_ANA_CFG3_REG_SLOWCK(x)	((x) << 30)
#define SUN8I_HDMI_PHY_ANA_CFG3_REG_SLOW(x)	((x) << 28)
#define SUN8I_HDMI_PHY_ANA_CFG3_REG_WIRE(x)	((x) << 18)
#define SUN8I_HDMI_PHY_ANA_CFG3_REG_AMPCK(x)	((x) << 14)
#define SUN8I_HDMI_PHY_ANA_CFG3_REG_EMPCK(x)	((x) << 11)
#define SUN8I_HDMI_PHY_ANA_CFG3_REG_AMP(x)	((x) << 7)
#define SUN8I_HDMI_PHY_ANA_CFG3_REG_EMP(x)	((x) << 4)
#define SUN8I_HDMI_PHY_ANA_CFG3_SDAPD		BIT(3)
#define SUN8I_HDMI_PHY_ANA_CFG3_SDAEN		BIT(2)
#define SUN8I_HDMI_PHY_ANA_CFG3_SCLPD		BIT(1)
#define SUN8I_HDMI_PHY_ANA_CFG3_SCLEN		BIT(0)

#define SUN8I_HDMI_PHY_PLL_CFG1_REG	0x002c
#define SUN8I_HDMI_PHY_PLL_CFG1_REG_OD1		BIT(31)
#define SUN8I_HDMI_PHY_PLL_CFG1_REG_OD		BIT(30)
#define SUN8I_HDMI_PHY_PLL_CFG1_LDO2_EN		BIT(29)
#define SUN8I_HDMI_PHY_PLL_CFG1_LDO1_EN		BIT(28)
#define SUN8I_HDMI_PHY_PLL_CFG1_HV_IS_33	BIT(27)
#define SUN8I_HDMI_PHY_PLL_CFG1_CKIN_SEL	BIT(26)
#define SUN8I_HDMI_PHY_PLL_CFG1_PLLEN		BIT(25)
#define SUN8I_HDMI_PHY_PLL_CFG1_LDO_VSET(x)	((x) << 22)
#define SUN8I_HDMI_PHY_PLL_CFG1_UNKNOWN(x)	((x) << 20)
#define SUN8I_HDMI_PHY_PLL_CFG1_PLLDBEN		BIT(19)
#define SUN8I_HDMI_PHY_PLL_CFG1_CS		BIT(18)
#define SUN8I_HDMI_PHY_PLL_CFG1_CP_S(x)		((x) << 13)
#define SUN8I_HDMI_PHY_PLL_CFG1_CNT_INT(x)	((x) << 7)
#define SUN8I_HDMI_PHY_PLL_CFG1_BWS		BIT(6)
#define SUN8I_HDMI_PHY_PLL_CFG1_B_IN_MSK	GENMASK(5, 0)
#define SUN8I_HDMI_PHY_PLL_CFG1_B_IN_SHIFT	0

#define SUN8I_HDMI_PHY_PLL_CFG2_REG	0x0030
#define SUN8I_HDMI_PHY_PLL_CFG2_SV_H		BIT(31)
#define SUN8I_HDMI_PHY_PLL_CFG2_PDCLKSEL(x)	((x) << 29)
#define SUN8I_HDMI_PHY_PLL_CFG2_CLKSTEP(x)	((x) << 27)
#define SUN8I_HDMI_PHY_PLL_CFG2_PSET(x)		((x) << 24)
#define SUN8I_HDMI_PHY_PLL_CFG2_PCLK_SEL	BIT(23)
#define SUN8I_HDMI_PHY_PLL_CFG2_AUTOSYNC_DIS	BIT(22)
#define SUN8I_HDMI_PHY_PLL_CFG2_VREG2_OUT_EN	BIT(21)
#define SUN8I_HDMI_PHY_PLL_CFG2_VREG1_OUT_EN	BIT(20)
#define SUN8I_HDMI_PHY_PLL_CFG2_VCOGAIN_EN	BIT(19)
#define SUN8I_HDMI_PHY_PLL_CFG2_VCOGAIN(x)	((x) << 16)
#define SUN8I_HDMI_PHY_PLL_CFG2_VCO_S(x)	((x) << 12)
#define SUN8I_HDMI_PHY_PLL_CFG2_VCO_RST_IN	BIT(11)
#define SUN8I_HDMI_PHY_PLL_CFG2_SINT_FRAC	BIT(10)
#define SUN8I_HDMI_PHY_PLL_CFG2_SDIV2		BIT(9)
#define SUN8I_HDMI_PHY_PLL_CFG2_S(x)		((x) << 6)
#define SUN8I_HDMI_PHY_PLL_CFG2_S6P25_7P5	BIT(5)
#define SUN8I_HDMI_PHY_PLL_CFG2_S5_7		BIT(4)
#define SUN8I_HDMI_PHY_PLL_CFG2_PREDIV_MSK	GENMASK(3, 0)
#define SUN8I_HDMI_PHY_PLL_CFG2_PREDIV_SHIFT	0
#define SUN8I_HDMI_PHY_PLL_CFG2_PREDIV(x)	(((x) - 1) << 0)

#define SUN8I_HDMI_PHY_PLL_CFG3_REG	0x0034
#define SUN8I_HDMI_PHY_PLL_CFG3_SOUT_DIV2	BIT(0)

#define SUN8I_HDMI_PHY_ANA_STS_REG	0x0038
#define SUN8I_HDMI_PHY_ANA_STS_B_OUT_SHIFT	11
#define SUN8I_HDMI_PHY_ANA_STS_B_OUT_MSK	GENMASK(16, 11)
#define SUN8I_HDMI_PHY_ANA_STS_RCALEND2D	BIT(7)
#define SUN8I_HDMI_PHY_ANA_STS_RCAL_MASK	GENMASK(5, 0)

#define SUN8I_HDMI_PHY_CEC_REG		0x003c

struct sun8i_hdmi_phy;

struct sun8i_hdmi_phy_variant {
	bool has_phy_clk;
	void (*phy_init)(struct sun8i_hdmi_phy *phy);
	void (*phy_disable)(struct dw_hdmi *hdmi,
			    struct sun8i_hdmi_phy *phy);
	int  (*phy_config)(struct dw_hdmi *hdmi,
			   struct sun8i_hdmi_phy *phy,
			   unsigned int clk_rate);
};

struct sun8i_hdmi_phy {
	struct clk			*clk_bus;
	struct clk			*clk_mod;
	struct clk			*clk_phy;
	struct clk			*clk_pll0;
	unsigned int			rcal;
	struct regmap			*regs;
	struct reset_control		*rst_phy;
	struct sun8i_hdmi_phy_variant	*variant;
};

struct sun8i_dw_hdmi {
	struct clk			*clk_tmds;
	struct device			*dev;
	struct dw_hdmi			*hdmi;
	struct drm_encoder		encoder;
	struct sun8i_hdmi_phy		*phy;
	struct dw_hdmi_plat_data	plat_data;
	struct reset_control		*rst_ctrl;
};

static inline struct sun8i_dw_hdmi *
encoder_to_sun8i_dw_hdmi(struct drm_encoder *encoder)
{
	return container_of(encoder, struct sun8i_dw_hdmi, encoder);
}

int sun8i_hdmi_phy_probe(struct sun8i_dw_hdmi *hdmi, struct device_node *node);
void sun8i_hdmi_phy_remove(struct sun8i_dw_hdmi *hdmi);

void sun8i_hdmi_phy_init(struct sun8i_hdmi_phy *phy);
const struct dw_hdmi_phy_ops *sun8i_hdmi_phy_get_ops(void);

int sun8i_phy_clk_create(struct sun8i_hdmi_phy *phy, struct device *dev);

#endif /* _SUN8I_DW_HDMI_H_ */
