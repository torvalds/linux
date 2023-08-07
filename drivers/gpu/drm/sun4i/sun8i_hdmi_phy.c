// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2018 Jernej Skrabec <jernej.skrabec@siol.net>
 */

#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "sun8i_dw_hdmi.h"

/*
 * Address can be actually any value. Here is set to same value as
 * it is set in BSP driver.
 */
#define I2C_ADDR	0x69

static const struct dw_hdmi_mpll_config sun50i_h6_mpll_cfg[] = {
	{
		30666000, {
			{ 0x00b3, 0x0000 },
			{ 0x2153, 0x0000 },
			{ 0x40f3, 0x0000 },
		},
	},  {
		36800000, {
			{ 0x00b3, 0x0000 },
			{ 0x2153, 0x0000 },
			{ 0x40a2, 0x0001 },
		},
	},  {
		46000000, {
			{ 0x00b3, 0x0000 },
			{ 0x2142, 0x0001 },
			{ 0x40a2, 0x0001 },
		},
	},  {
		61333000, {
			{ 0x0072, 0x0001 },
			{ 0x2142, 0x0001 },
			{ 0x40a2, 0x0001 },
		},
	},  {
		73600000, {
			{ 0x0072, 0x0001 },
			{ 0x2142, 0x0001 },
			{ 0x4061, 0x0002 },
		},
	},  {
		92000000, {
			{ 0x0072, 0x0001 },
			{ 0x2145, 0x0002 },
			{ 0x4061, 0x0002 },
		},
	},  {
		122666000, {
			{ 0x0051, 0x0002 },
			{ 0x2145, 0x0002 },
			{ 0x4061, 0x0002 },
		},
	},  {
		147200000, {
			{ 0x0051, 0x0002 },
			{ 0x2145, 0x0002 },
			{ 0x4064, 0x0003 },
		},
	},  {
		184000000, {
			{ 0x0051, 0x0002 },
			{ 0x214c, 0x0003 },
			{ 0x4064, 0x0003 },
		},
	},  {
		226666000, {
			{ 0x0040, 0x0003 },
			{ 0x214c, 0x0003 },
			{ 0x4064, 0x0003 },
		},
	},  {
		272000000, {
			{ 0x0040, 0x0003 },
			{ 0x214c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	},  {
		340000000, {
			{ 0x0040, 0x0003 },
			{ 0x3b4c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	},  {
		594000000, {
			{ 0x1a40, 0x0003 },
			{ 0x3b4c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	}, {
		~0UL, {
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
		},
	}
};

static const struct dw_hdmi_curr_ctrl sun50i_h6_cur_ctr[] = {
	/* pixelclk    bpp8    bpp10   bpp12 */
	{ 27000000,  { 0x0012, 0x0000, 0x0000 }, },
	{ 74250000,  { 0x0013, 0x001a, 0x001b }, },
	{ 148500000, { 0x0019, 0x0033, 0x0034 }, },
	{ 297000000, { 0x0019, 0x001b, 0x001b }, },
	{ 594000000, { 0x0010, 0x001b, 0x001b }, },
	{ ~0UL,      { 0x0000, 0x0000, 0x0000 }, }
};

static const struct dw_hdmi_phy_config sun50i_h6_phy_config[] = {
	/*pixelclk   symbol   term   vlev*/
	{ 27000000,  0x8009, 0x0007, 0x02b0 },
	{ 74250000,  0x8009, 0x0006, 0x022d },
	{ 148500000, 0x8029, 0x0006, 0x0270 },
	{ 297000000, 0x8039, 0x0005, 0x01ab },
	{ 594000000, 0x8029, 0x0000, 0x008a },
	{ ~0UL,	     0x0000, 0x0000, 0x0000}
};

static void sun8i_hdmi_phy_set_polarity(struct sun8i_hdmi_phy *phy,
					const struct drm_display_mode *mode)
{
	u32 val = 0;

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		val |= SUN8I_HDMI_PHY_DBG_CTRL_POL_NHSYNC;

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		val |= SUN8I_HDMI_PHY_DBG_CTRL_POL_NVSYNC;

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_DBG_CTRL_REG,
			   SUN8I_HDMI_PHY_DBG_CTRL_POL_MASK, val);
};

static int sun8i_a83t_hdmi_phy_config(struct dw_hdmi *hdmi, void *data,
				      const struct drm_display_info *display,
				      const struct drm_display_mode *mode)
{
	unsigned int clk_rate = mode->crtc_clock * 1000;
	struct sun8i_hdmi_phy *phy = data;

	sun8i_hdmi_phy_set_polarity(phy, mode);

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_REXT_CTRL_REG,
			   SUN8I_HDMI_PHY_REXT_CTRL_REXT_EN,
			   SUN8I_HDMI_PHY_REXT_CTRL_REXT_EN);

	/* power down */
	dw_hdmi_phy_gen2_txpwron(hdmi, 0);
	dw_hdmi_phy_gen2_pddq(hdmi, 1);

	dw_hdmi_phy_gen2_reset(hdmi);

	dw_hdmi_phy_gen2_pddq(hdmi, 0);

	dw_hdmi_phy_i2c_set_addr(hdmi, I2C_ADDR);

	/*
	 * Values are taken from BSP HDMI driver. Although AW didn't
	 * release any documentation, explanation of this values can
	 * be found in i.MX 6Dual/6Quad Reference Manual.
	 */
	if (clk_rate <= 27000000) {
		dw_hdmi_phy_i2c_write(hdmi, 0x01e0, 0x06);
		dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x15);
		dw_hdmi_phy_i2c_write(hdmi, 0x08da, 0x10);
		dw_hdmi_phy_i2c_write(hdmi, 0x0007, 0x19);
		dw_hdmi_phy_i2c_write(hdmi, 0x0318, 0x0e);
		dw_hdmi_phy_i2c_write(hdmi, 0x8009, 0x09);
	} else if (clk_rate <= 74250000) {
		dw_hdmi_phy_i2c_write(hdmi, 0x0540, 0x06);
		dw_hdmi_phy_i2c_write(hdmi, 0x0005, 0x15);
		dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x10);
		dw_hdmi_phy_i2c_write(hdmi, 0x0007, 0x19);
		dw_hdmi_phy_i2c_write(hdmi, 0x02b5, 0x0e);
		dw_hdmi_phy_i2c_write(hdmi, 0x8009, 0x09);
	} else if (clk_rate <= 148500000) {
		dw_hdmi_phy_i2c_write(hdmi, 0x04a0, 0x06);
		dw_hdmi_phy_i2c_write(hdmi, 0x000a, 0x15);
		dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x10);
		dw_hdmi_phy_i2c_write(hdmi, 0x0002, 0x19);
		dw_hdmi_phy_i2c_write(hdmi, 0x0021, 0x0e);
		dw_hdmi_phy_i2c_write(hdmi, 0x8029, 0x09);
	} else {
		dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x06);
		dw_hdmi_phy_i2c_write(hdmi, 0x000f, 0x15);
		dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x10);
		dw_hdmi_phy_i2c_write(hdmi, 0x0002, 0x19);
		dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x0e);
		dw_hdmi_phy_i2c_write(hdmi, 0x802b, 0x09);
	}

	dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x1e);
	dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x13);
	dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x17);

	dw_hdmi_phy_gen2_txpwron(hdmi, 1);

	return 0;
}

static void sun8i_a83t_hdmi_phy_disable(struct dw_hdmi *hdmi, void *data)
{
	struct sun8i_hdmi_phy *phy = data;

	dw_hdmi_phy_gen2_txpwron(hdmi, 0);
	dw_hdmi_phy_gen2_pddq(hdmi, 1);

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_REXT_CTRL_REG,
			   SUN8I_HDMI_PHY_REXT_CTRL_REXT_EN, 0);
}

static const struct dw_hdmi_phy_ops sun8i_a83t_hdmi_phy_ops = {
	.init		= sun8i_a83t_hdmi_phy_config,
	.disable	= sun8i_a83t_hdmi_phy_disable,
	.read_hpd	= dw_hdmi_phy_read_hpd,
	.update_hpd	= dw_hdmi_phy_update_hpd,
	.setup_hpd	= dw_hdmi_phy_setup_hpd,
};

static int sun8i_h3_hdmi_phy_config(struct dw_hdmi *hdmi, void *data,
				    const struct drm_display_info *display,
				    const struct drm_display_mode *mode)
{
	unsigned int clk_rate = mode->crtc_clock * 1000;
	struct sun8i_hdmi_phy *phy = data;
	u32 pll_cfg1_init;
	u32 pll_cfg2_init;
	u32 ana_cfg1_end;
	u32 ana_cfg2_init;
	u32 ana_cfg3_init;
	u32 b_offset = 0;
	u32 val;

	if (phy->variant->has_phy_clk)
		clk_set_rate(phy->clk_phy, clk_rate);

	sun8i_hdmi_phy_set_polarity(phy, mode);

	/* bandwidth / frequency independent settings */

	pll_cfg1_init = SUN8I_HDMI_PHY_PLL_CFG1_LDO2_EN |
			SUN8I_HDMI_PHY_PLL_CFG1_LDO1_EN |
			SUN8I_HDMI_PHY_PLL_CFG1_LDO_VSET(7) |
			SUN8I_HDMI_PHY_PLL_CFG1_UNKNOWN(1) |
			SUN8I_HDMI_PHY_PLL_CFG1_PLLDBEN |
			SUN8I_HDMI_PHY_PLL_CFG1_CS |
			SUN8I_HDMI_PHY_PLL_CFG1_CP_S(2) |
			SUN8I_HDMI_PHY_PLL_CFG1_CNT_INT(63) |
			SUN8I_HDMI_PHY_PLL_CFG1_BWS;

	pll_cfg2_init = SUN8I_HDMI_PHY_PLL_CFG2_SV_H |
			SUN8I_HDMI_PHY_PLL_CFG2_VCOGAIN_EN |
			SUN8I_HDMI_PHY_PLL_CFG2_SDIV2;

	ana_cfg1_end = SUN8I_HDMI_PHY_ANA_CFG1_REG_SVBH(1) |
		       SUN8I_HDMI_PHY_ANA_CFG1_AMP_OPT |
		       SUN8I_HDMI_PHY_ANA_CFG1_EMP_OPT |
		       SUN8I_HDMI_PHY_ANA_CFG1_AMPCK_OPT |
		       SUN8I_HDMI_PHY_ANA_CFG1_EMPCK_OPT |
		       SUN8I_HDMI_PHY_ANA_CFG1_ENRCAL |
		       SUN8I_HDMI_PHY_ANA_CFG1_ENCALOG |
		       SUN8I_HDMI_PHY_ANA_CFG1_REG_SCKTMDS |
		       SUN8I_HDMI_PHY_ANA_CFG1_TMDSCLK_EN |
		       SUN8I_HDMI_PHY_ANA_CFG1_TXEN_MASK |
		       SUN8I_HDMI_PHY_ANA_CFG1_TXEN_ALL |
		       SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDSCLK |
		       SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS2 |
		       SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS1 |
		       SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS0 |
		       SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS2 |
		       SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS1 |
		       SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS0 |
		       SUN8I_HDMI_PHY_ANA_CFG1_CKEN |
		       SUN8I_HDMI_PHY_ANA_CFG1_LDOEN |
		       SUN8I_HDMI_PHY_ANA_CFG1_ENVBS |
		       SUN8I_HDMI_PHY_ANA_CFG1_ENBI;

	ana_cfg2_init = SUN8I_HDMI_PHY_ANA_CFG2_M_EN |
			SUN8I_HDMI_PHY_ANA_CFG2_REG_DENCK |
			SUN8I_HDMI_PHY_ANA_CFG2_REG_DEN |
			SUN8I_HDMI_PHY_ANA_CFG2_REG_CKSS(1) |
			SUN8I_HDMI_PHY_ANA_CFG2_REG_CSMPS(1);

	ana_cfg3_init = SUN8I_HDMI_PHY_ANA_CFG3_REG_WIRE(0x3e0) |
			SUN8I_HDMI_PHY_ANA_CFG3_SDAEN |
			SUN8I_HDMI_PHY_ANA_CFG3_SCLEN;

	/* bandwidth / frequency dependent settings */
	if (clk_rate <= 27000000) {
		pll_cfg1_init |= SUN8I_HDMI_PHY_PLL_CFG1_HV_IS_33 |
				 SUN8I_HDMI_PHY_PLL_CFG1_CNT_INT(32);
		pll_cfg2_init |= SUN8I_HDMI_PHY_PLL_CFG2_VCO_S(4) |
				 SUN8I_HDMI_PHY_PLL_CFG2_S(4);
		ana_cfg1_end |= SUN8I_HDMI_PHY_ANA_CFG1_REG_CALSW;
		ana_cfg2_init |= SUN8I_HDMI_PHY_ANA_CFG2_REG_SLV(4) |
				 SUN8I_HDMI_PHY_ANA_CFG2_REG_RESDI(phy->rcal);
		ana_cfg3_init |= SUN8I_HDMI_PHY_ANA_CFG3_REG_AMPCK(3) |
				 SUN8I_HDMI_PHY_ANA_CFG3_REG_AMP(5);
	} else if (clk_rate <= 74250000) {
		pll_cfg1_init |= SUN8I_HDMI_PHY_PLL_CFG1_HV_IS_33 |
				 SUN8I_HDMI_PHY_PLL_CFG1_CNT_INT(32);
		pll_cfg2_init |= SUN8I_HDMI_PHY_PLL_CFG2_VCO_S(4) |
				 SUN8I_HDMI_PHY_PLL_CFG2_S(5);
		ana_cfg1_end |= SUN8I_HDMI_PHY_ANA_CFG1_REG_CALSW;
		ana_cfg2_init |= SUN8I_HDMI_PHY_ANA_CFG2_REG_SLV(4) |
				 SUN8I_HDMI_PHY_ANA_CFG2_REG_RESDI(phy->rcal);
		ana_cfg3_init |= SUN8I_HDMI_PHY_ANA_CFG3_REG_AMPCK(5) |
				 SUN8I_HDMI_PHY_ANA_CFG3_REG_AMP(7);
	} else if (clk_rate <= 148500000) {
		pll_cfg1_init |= SUN8I_HDMI_PHY_PLL_CFG1_HV_IS_33 |
				 SUN8I_HDMI_PHY_PLL_CFG1_CNT_INT(32);
		pll_cfg2_init |= SUN8I_HDMI_PHY_PLL_CFG2_VCO_S(4) |
				 SUN8I_HDMI_PHY_PLL_CFG2_S(6);
		ana_cfg2_init |= SUN8I_HDMI_PHY_ANA_CFG2_REG_BIGSWCK |
				 SUN8I_HDMI_PHY_ANA_CFG2_REG_BIGSW |
				 SUN8I_HDMI_PHY_ANA_CFG2_REG_SLV(2);
		ana_cfg3_init |= SUN8I_HDMI_PHY_ANA_CFG3_REG_AMPCK(7) |
				 SUN8I_HDMI_PHY_ANA_CFG3_REG_AMP(9);
	} else {
		b_offset = 2;
		pll_cfg1_init |= SUN8I_HDMI_PHY_PLL_CFG1_CNT_INT(63);
		pll_cfg2_init |= SUN8I_HDMI_PHY_PLL_CFG2_VCO_S(6) |
				 SUN8I_HDMI_PHY_PLL_CFG2_S(7);
		ana_cfg2_init |= SUN8I_HDMI_PHY_ANA_CFG2_REG_BIGSWCK |
				 SUN8I_HDMI_PHY_ANA_CFG2_REG_BIGSW |
				 SUN8I_HDMI_PHY_ANA_CFG2_REG_SLV(4);
		ana_cfg3_init |= SUN8I_HDMI_PHY_ANA_CFG3_REG_AMPCK(9) |
				 SUN8I_HDMI_PHY_ANA_CFG3_REG_AMP(13) |
				 SUN8I_HDMI_PHY_ANA_CFG3_REG_EMP(3);
	}

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_TXEN_MASK, 0);

	/*
	 * NOTE: We have to be careful not to overwrite PHY parent
	 * clock selection bit and clock divider.
	 */
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_PLL_CFG1_REG,
			   (u32)~SUN8I_HDMI_PHY_PLL_CFG1_CKIN_SEL_MSK,
			   pll_cfg1_init);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_PLL_CFG2_REG,
			   (u32)~SUN8I_HDMI_PHY_PLL_CFG2_PREDIV_MSK,
			   pll_cfg2_init);
	usleep_range(10000, 15000);
	regmap_write(phy->regs, SUN8I_HDMI_PHY_PLL_CFG3_REG,
		     SUN8I_HDMI_PHY_PLL_CFG3_SOUT_DIV2);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_PLL_CFG1_REG,
			   SUN8I_HDMI_PHY_PLL_CFG1_PLLEN,
			   SUN8I_HDMI_PHY_PLL_CFG1_PLLEN);
	msleep(100);

	/* get B value */
	regmap_read(phy->regs, SUN8I_HDMI_PHY_ANA_STS_REG, &val);
	val = (val & SUN8I_HDMI_PHY_ANA_STS_B_OUT_MSK) >>
		SUN8I_HDMI_PHY_ANA_STS_B_OUT_SHIFT;
	val = min(val + b_offset, (u32)0x3f);

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_PLL_CFG1_REG,
			   SUN8I_HDMI_PHY_PLL_CFG1_REG_OD1 |
			   SUN8I_HDMI_PHY_PLL_CFG1_REG_OD,
			   SUN8I_HDMI_PHY_PLL_CFG1_REG_OD1 |
			   SUN8I_HDMI_PHY_PLL_CFG1_REG_OD);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_PLL_CFG1_REG,
			   SUN8I_HDMI_PHY_PLL_CFG1_B_IN_MSK,
			   val << SUN8I_HDMI_PHY_PLL_CFG1_B_IN_SHIFT);
	msleep(100);
	regmap_write(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG, ana_cfg1_end);
	regmap_write(phy->regs, SUN8I_HDMI_PHY_ANA_CFG2_REG, ana_cfg2_init);
	regmap_write(phy->regs, SUN8I_HDMI_PHY_ANA_CFG3_REG, ana_cfg3_init);

	return 0;
}

static void sun8i_h3_hdmi_phy_disable(struct dw_hdmi *hdmi, void *data)
{
	struct sun8i_hdmi_phy *phy = data;

	regmap_write(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
		     SUN8I_HDMI_PHY_ANA_CFG1_LDOEN |
		     SUN8I_HDMI_PHY_ANA_CFG1_ENVBS |
		     SUN8I_HDMI_PHY_ANA_CFG1_ENBI);
	regmap_write(phy->regs, SUN8I_HDMI_PHY_PLL_CFG1_REG, 0);
}

static const struct dw_hdmi_phy_ops sun8i_h3_hdmi_phy_ops = {
	.init		= sun8i_h3_hdmi_phy_config,
	.disable	= sun8i_h3_hdmi_phy_disable,
	.read_hpd	= dw_hdmi_phy_read_hpd,
	.update_hpd	= dw_hdmi_phy_update_hpd,
	.setup_hpd	= dw_hdmi_phy_setup_hpd,
};

static void sun8i_hdmi_phy_unlock(struct sun8i_hdmi_phy *phy)
{
	/* enable read access to HDMI controller */
	regmap_write(phy->regs, SUN8I_HDMI_PHY_READ_EN_REG,
		     SUN8I_HDMI_PHY_READ_EN_MAGIC);

	/* unscramble register offsets */
	regmap_write(phy->regs, SUN8I_HDMI_PHY_UNSCRAMBLE_REG,
		     SUN8I_HDMI_PHY_UNSCRAMBLE_MAGIC);
}

static void sun50i_hdmi_phy_init_h6(struct sun8i_hdmi_phy *phy)
{
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_REXT_CTRL_REG,
			   SUN8I_HDMI_PHY_REXT_CTRL_REXT_EN,
			   SUN8I_HDMI_PHY_REXT_CTRL_REXT_EN);

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_REXT_CTRL_REG,
			   0xffff0000, 0x80c00000);
}

static void sun8i_hdmi_phy_init_a83t(struct sun8i_hdmi_phy *phy)
{
	sun8i_hdmi_phy_unlock(phy);

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_DBG_CTRL_REG,
			   SUN8I_HDMI_PHY_DBG_CTRL_PX_LOCK,
			   SUN8I_HDMI_PHY_DBG_CTRL_PX_LOCK);

	/*
	 * Set PHY I2C address. It must match to the address set by
	 * dw_hdmi_phy_set_slave_addr().
	 */
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_DBG_CTRL_REG,
			   SUN8I_HDMI_PHY_DBG_CTRL_ADDR_MASK,
			   SUN8I_HDMI_PHY_DBG_CTRL_ADDR(I2C_ADDR));
}

static void sun8i_hdmi_phy_init_h3(struct sun8i_hdmi_phy *phy)
{
	unsigned int val;

	sun8i_hdmi_phy_unlock(phy);

	regmap_write(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG, 0);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENBI,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENBI);
	udelay(5);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_TMDSCLK_EN,
			   SUN8I_HDMI_PHY_ANA_CFG1_TMDSCLK_EN);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENVBS,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENVBS);
	usleep_range(10, 20);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_LDOEN,
			   SUN8I_HDMI_PHY_ANA_CFG1_LDOEN);
	udelay(5);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_CKEN,
			   SUN8I_HDMI_PHY_ANA_CFG1_CKEN);
	usleep_range(40, 100);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENRCAL,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENRCAL);
	usleep_range(100, 200);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENCALOG,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENCALOG);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS0 |
			   SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS1 |
			   SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS2,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS0 |
			   SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS1 |
			   SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS2);

	/* wait for calibration to finish */
	regmap_read_poll_timeout(phy->regs, SUN8I_HDMI_PHY_ANA_STS_REG, val,
				 (val & SUN8I_HDMI_PHY_ANA_STS_RCALEND2D),
				 100, 2000);

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDSCLK,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDSCLK);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS0 |
			   SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS1 |
			   SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS2 |
			   SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDSCLK,
			   SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS0 |
			   SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS1 |
			   SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS2 |
			   SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDSCLK);

	/* enable DDC communication */
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG3_REG,
			   SUN8I_HDMI_PHY_ANA_CFG3_SCLEN |
			   SUN8I_HDMI_PHY_ANA_CFG3_SDAEN,
			   SUN8I_HDMI_PHY_ANA_CFG3_SCLEN |
			   SUN8I_HDMI_PHY_ANA_CFG3_SDAEN);

	/* reset PHY PLL clock parent */
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_PLL_CFG1_REG,
			   SUN8I_HDMI_PHY_PLL_CFG1_CKIN_SEL_MSK, 0);

	/* set HW control of CEC pins */
	regmap_write(phy->regs, SUN8I_HDMI_PHY_CEC_REG, 0);

	/* read calibration data */
	regmap_read(phy->regs, SUN8I_HDMI_PHY_ANA_STS_REG, &val);
	phy->rcal = (val & SUN8I_HDMI_PHY_ANA_STS_RCAL_MASK) >> 2;
}

int sun8i_hdmi_phy_init(struct sun8i_hdmi_phy *phy)
{
	int ret;

	ret = reset_control_deassert(phy->rst_phy);
	if (ret) {
		dev_err(phy->dev, "Cannot deassert phy reset control: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(phy->clk_bus);
	if (ret) {
		dev_err(phy->dev, "Cannot enable bus clock: %d\n", ret);
		goto err_assert_rst_phy;
	}

	ret = clk_prepare_enable(phy->clk_mod);
	if (ret) {
		dev_err(phy->dev, "Cannot enable mod clock: %d\n", ret);
		goto err_disable_clk_bus;
	}

	if (phy->variant->has_phy_clk) {
		ret = sun8i_phy_clk_create(phy, phy->dev,
					   phy->variant->has_second_pll);
		if (ret) {
			dev_err(phy->dev, "Couldn't create the PHY clock\n");
			goto err_disable_clk_mod;
		}

		clk_prepare_enable(phy->clk_phy);
	}

	phy->variant->phy_init(phy);

	return 0;

err_disable_clk_mod:
	clk_disable_unprepare(phy->clk_mod);
err_disable_clk_bus:
	clk_disable_unprepare(phy->clk_bus);
err_assert_rst_phy:
	reset_control_assert(phy->rst_phy);

	return ret;
}

void sun8i_hdmi_phy_deinit(struct sun8i_hdmi_phy *phy)
{
	clk_disable_unprepare(phy->clk_mod);
	clk_disable_unprepare(phy->clk_bus);
	clk_disable_unprepare(phy->clk_phy);

	reset_control_assert(phy->rst_phy);
}

void sun8i_hdmi_phy_set_ops(struct sun8i_hdmi_phy *phy,
			    struct dw_hdmi_plat_data *plat_data)
{
	const struct sun8i_hdmi_phy_variant *variant = phy->variant;

	if (variant->phy_ops) {
		plat_data->phy_ops = variant->phy_ops;
		plat_data->phy_name = "sun8i_dw_hdmi_phy";
		plat_data->phy_data = phy;
	} else {
		plat_data->mpll_cfg = variant->mpll_cfg;
		plat_data->cur_ctr = variant->cur_ctr;
		plat_data->phy_config = variant->phy_cfg;
	}
}

static const struct regmap_config sun8i_hdmi_phy_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= SUN8I_HDMI_PHY_CEC_REG,
	.name		= "phy"
};

static const struct sun8i_hdmi_phy_variant sun8i_a83t_hdmi_phy = {
	.phy_ops = &sun8i_a83t_hdmi_phy_ops,
	.phy_init = &sun8i_hdmi_phy_init_a83t,
};

static const struct sun8i_hdmi_phy_variant sun8i_h3_hdmi_phy = {
	.has_phy_clk = true,
	.phy_ops = &sun8i_h3_hdmi_phy_ops,
	.phy_init = &sun8i_hdmi_phy_init_h3,
};

static const struct sun8i_hdmi_phy_variant sun8i_r40_hdmi_phy = {
	.has_phy_clk = true,
	.has_second_pll = true,
	.phy_ops = &sun8i_h3_hdmi_phy_ops,
	.phy_init = &sun8i_hdmi_phy_init_h3,
};

static const struct sun8i_hdmi_phy_variant sun50i_a64_hdmi_phy = {
	.has_phy_clk = true,
	.phy_ops = &sun8i_h3_hdmi_phy_ops,
	.phy_init = &sun8i_hdmi_phy_init_h3,
};

static const struct sun8i_hdmi_phy_variant sun50i_h6_hdmi_phy = {
	.cur_ctr  = sun50i_h6_cur_ctr,
	.mpll_cfg = sun50i_h6_mpll_cfg,
	.phy_cfg  = sun50i_h6_phy_config,
	.phy_init = &sun50i_hdmi_phy_init_h6,
};

static const struct of_device_id sun8i_hdmi_phy_of_table[] = {
	{
		.compatible = "allwinner,sun8i-a83t-hdmi-phy",
		.data = &sun8i_a83t_hdmi_phy,
	},
	{
		.compatible = "allwinner,sun8i-h3-hdmi-phy",
		.data = &sun8i_h3_hdmi_phy,
	},
	{
		.compatible = "allwinner,sun8i-r40-hdmi-phy",
		.data = &sun8i_r40_hdmi_phy,
	},
	{
		.compatible = "allwinner,sun50i-a64-hdmi-phy",
		.data = &sun50i_a64_hdmi_phy,
	},
	{
		.compatible = "allwinner,sun50i-h6-hdmi-phy",
		.data = &sun50i_h6_hdmi_phy,
	},
	{ /* sentinel */ }
};

int sun8i_hdmi_phy_get(struct sun8i_dw_hdmi *hdmi, struct device_node *node)
{
	struct platform_device *pdev = of_find_device_by_node(node);
	struct sun8i_hdmi_phy *phy;

	if (!pdev)
		return -EPROBE_DEFER;

	phy = platform_get_drvdata(pdev);
	if (!phy) {
		put_device(&pdev->dev);
		return -EPROBE_DEFER;
	}

	hdmi->phy = phy;

	put_device(&pdev->dev);

	return 0;
}

static int sun8i_hdmi_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sun8i_hdmi_phy *phy;
	void __iomem *regs;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->variant = of_device_get_match_data(dev);
	phy->dev = dev;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return dev_err_probe(dev, PTR_ERR(regs),
				     "Couldn't map the HDMI PHY registers\n");

	phy->regs = devm_regmap_init_mmio(dev, regs,
					  &sun8i_hdmi_phy_regmap_config);
	if (IS_ERR(phy->regs))
		return dev_err_probe(dev, PTR_ERR(phy->regs),
				     "Couldn't create the HDMI PHY regmap\n");

	phy->clk_bus = devm_clk_get(dev, "bus");
	if (IS_ERR(phy->clk_bus))
		return dev_err_probe(dev, PTR_ERR(phy->clk_bus),
				     "Could not get bus clock\n");

	phy->clk_mod = devm_clk_get(dev, "mod");
	if (IS_ERR(phy->clk_mod))
		return dev_err_probe(dev, PTR_ERR(phy->clk_mod),
				     "Could not get mod clock\n");

	if (phy->variant->has_phy_clk) {
		phy->clk_pll0 = devm_clk_get(dev, "pll-0");
		if (IS_ERR(phy->clk_pll0))
			return dev_err_probe(dev, PTR_ERR(phy->clk_pll0),
					     "Could not get pll-0 clock\n");

		if (phy->variant->has_second_pll) {
			phy->clk_pll1 = devm_clk_get(dev, "pll-1");
			if (IS_ERR(phy->clk_pll1))
				return dev_err_probe(dev, PTR_ERR(phy->clk_pll1),
						     "Could not get pll-1 clock\n");
		}
	}

	phy->rst_phy = devm_reset_control_get_shared(dev, "phy");
	if (IS_ERR(phy->rst_phy))
		return dev_err_probe(dev, PTR_ERR(phy->rst_phy),
				     "Could not get phy reset control\n");

	platform_set_drvdata(pdev, phy);

	return 0;
}

struct platform_driver sun8i_hdmi_phy_driver = {
	.probe  = sun8i_hdmi_phy_probe,
	.driver = {
		.name = "sun8i-hdmi-phy",
		.of_match_table = sun8i_hdmi_phy_of_table,
	},
};
