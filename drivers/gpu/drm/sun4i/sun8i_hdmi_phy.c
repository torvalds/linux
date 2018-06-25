// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2018 Jernej Skrabec <jernej.skrabec@siol.net>
 */

#include <linux/delay.h>
#include <linux/of_address.h>

#include "sun8i_dw_hdmi.h"

/*
 * Address can be actually any value. Here is set to same value as
 * it is set in BSP driver.
 */
#define I2C_ADDR	0x69

static int sun8i_hdmi_phy_config_a83t(struct dw_hdmi *hdmi,
				      struct sun8i_hdmi_phy *phy,
				      unsigned int clk_rate)
{
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_REXT_CTRL_REG,
			   SUN8I_HDMI_PHY_REXT_CTRL_REXT_EN,
			   SUN8I_HDMI_PHY_REXT_CTRL_REXT_EN);

	/* power down */
	dw_hdmi_phy_gen2_txpwron(hdmi, 0);
	dw_hdmi_phy_gen2_pddq(hdmi, 1);

	dw_hdmi_phy_reset(hdmi);

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

static int sun8i_hdmi_phy_config_h3(struct dw_hdmi *hdmi,
				    struct sun8i_hdmi_phy *phy,
				    unsigned int clk_rate)
{
	u32 pll_cfg1_init;
	u32 pll_cfg2_init;
	u32 ana_cfg1_end;
	u32 ana_cfg2_init;
	u32 ana_cfg3_init;
	u32 b_offset = 0;
	u32 val;

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
				 SUN8I_HDMI_PHY_ANA_CFG3_REG_AMP(13);
	}

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_TXEN_MASK, 0);

	regmap_write(phy->regs, SUN8I_HDMI_PHY_PLL_CFG1_REG, pll_cfg1_init);
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

static int sun8i_hdmi_phy_config(struct dw_hdmi *hdmi, void *data,
				 struct drm_display_mode *mode)
{
	struct sun8i_hdmi_phy *phy = (struct sun8i_hdmi_phy *)data;
	u32 val = 0;

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		val |= SUN8I_HDMI_PHY_DBG_CTRL_POL_NHSYNC;

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		val |= SUN8I_HDMI_PHY_DBG_CTRL_POL_NVSYNC;

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_DBG_CTRL_REG,
			   SUN8I_HDMI_PHY_DBG_CTRL_POL_MASK, val);

	if (phy->variant->has_phy_clk)
		clk_set_rate(phy->clk_phy, mode->crtc_clock * 1000);

	return phy->variant->phy_config(hdmi, phy, mode->crtc_clock * 1000);
};

static void sun8i_hdmi_phy_disable_a83t(struct dw_hdmi *hdmi,
					struct sun8i_hdmi_phy *phy)
{
	dw_hdmi_phy_gen2_txpwron(hdmi, 0);
	dw_hdmi_phy_gen2_pddq(hdmi, 1);

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_REXT_CTRL_REG,
			   SUN8I_HDMI_PHY_REXT_CTRL_REXT_EN, 0);
}

static void sun8i_hdmi_phy_disable_h3(struct dw_hdmi *hdmi,
				      struct sun8i_hdmi_phy *phy)
{
	regmap_write(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
		     SUN8I_HDMI_PHY_ANA_CFG1_LDOEN |
		     SUN8I_HDMI_PHY_ANA_CFG1_ENVBS |
		     SUN8I_HDMI_PHY_ANA_CFG1_ENBI);
	regmap_write(phy->regs, SUN8I_HDMI_PHY_PLL_CFG1_REG, 0);
}

static void sun8i_hdmi_phy_disable(struct dw_hdmi *hdmi, void *data)
{
	struct sun8i_hdmi_phy *phy = (struct sun8i_hdmi_phy *)data;

	phy->variant->phy_disable(hdmi, phy);
}

static const struct dw_hdmi_phy_ops sun8i_hdmi_phy_ops = {
	.init = &sun8i_hdmi_phy_config,
	.disable = &sun8i_hdmi_phy_disable,
	.read_hpd = &dw_hdmi_phy_read_hpd,
	.update_hpd = &dw_hdmi_phy_update_hpd,
	.setup_hpd = &dw_hdmi_phy_setup_hpd,
};

static void sun8i_hdmi_phy_init_a83t(struct sun8i_hdmi_phy *phy)
{
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

	/* set HW control of CEC pins */
	regmap_write(phy->regs, SUN8I_HDMI_PHY_CEC_REG, 0);

	/* read calibration data */
	regmap_read(phy->regs, SUN8I_HDMI_PHY_ANA_STS_REG, &val);
	phy->rcal = (val & SUN8I_HDMI_PHY_ANA_STS_RCAL_MASK) >> 2;
}

void sun8i_hdmi_phy_init(struct sun8i_hdmi_phy *phy)
{
	/* enable read access to HDMI controller */
	regmap_write(phy->regs, SUN8I_HDMI_PHY_READ_EN_REG,
		     SUN8I_HDMI_PHY_READ_EN_MAGIC);

	/* unscramble register offsets */
	regmap_write(phy->regs, SUN8I_HDMI_PHY_UNSCRAMBLE_REG,
		     SUN8I_HDMI_PHY_UNSCRAMBLE_MAGIC);

	phy->variant->phy_init(phy);
}

const struct dw_hdmi_phy_ops *sun8i_hdmi_phy_get_ops(void)
{
	return &sun8i_hdmi_phy_ops;
}

static struct regmap_config sun8i_hdmi_phy_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= SUN8I_HDMI_PHY_CEC_REG,
	.name		= "phy"
};

static const struct sun8i_hdmi_phy_variant sun8i_a83t_hdmi_phy = {
	.phy_init = &sun8i_hdmi_phy_init_a83t,
	.phy_disable = &sun8i_hdmi_phy_disable_a83t,
	.phy_config = &sun8i_hdmi_phy_config_a83t,
};

static const struct sun8i_hdmi_phy_variant sun8i_h3_hdmi_phy = {
	.has_phy_clk = true,
	.phy_init = &sun8i_hdmi_phy_init_h3,
	.phy_disable = &sun8i_hdmi_phy_disable_h3,
	.phy_config = &sun8i_hdmi_phy_config_h3,
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
	{ /* sentinel */ }
};

int sun8i_hdmi_phy_probe(struct sun8i_dw_hdmi *hdmi, struct device_node *node)
{
	const struct of_device_id *match;
	struct device *dev = hdmi->dev;
	struct sun8i_hdmi_phy *phy;
	struct resource res;
	void __iomem *regs;
	int ret;

	match = of_match_node(sun8i_hdmi_phy_of_table, node);
	if (!match) {
		dev_err(dev, "Incompatible HDMI PHY\n");
		return -EINVAL;
	}

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->variant = (struct sun8i_hdmi_phy_variant *)match->data;

	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		dev_err(dev, "phy: Couldn't get our resources\n");
		return ret;
	}

	regs = devm_ioremap_resource(dev, &res);
	if (IS_ERR(regs)) {
		dev_err(dev, "Couldn't map the HDMI PHY registers\n");
		return PTR_ERR(regs);
	}

	phy->regs = devm_regmap_init_mmio(dev, regs,
					  &sun8i_hdmi_phy_regmap_config);
	if (IS_ERR(phy->regs)) {
		dev_err(dev, "Couldn't create the HDMI PHY regmap\n");
		return PTR_ERR(phy->regs);
	}

	phy->clk_bus = of_clk_get_by_name(node, "bus");
	if (IS_ERR(phy->clk_bus)) {
		dev_err(dev, "Could not get bus clock\n");
		return PTR_ERR(phy->clk_bus);
	}

	phy->clk_mod = of_clk_get_by_name(node, "mod");
	if (IS_ERR(phy->clk_mod)) {
		dev_err(dev, "Could not get mod clock\n");
		ret = PTR_ERR(phy->clk_mod);
		goto err_put_clk_bus;
	}

	if (phy->variant->has_phy_clk) {
		phy->clk_pll0 = of_clk_get_by_name(node, "pll-0");
		if (IS_ERR(phy->clk_pll0)) {
			dev_err(dev, "Could not get pll-0 clock\n");
			ret = PTR_ERR(phy->clk_pll0);
			goto err_put_clk_mod;
		}

		ret = sun8i_phy_clk_create(phy, dev);
		if (ret) {
			dev_err(dev, "Couldn't create the PHY clock\n");
			goto err_put_clk_pll0;
		}

		clk_prepare_enable(phy->clk_phy);
	}

	phy->rst_phy = of_reset_control_get_shared(node, "phy");
	if (IS_ERR(phy->rst_phy)) {
		dev_err(dev, "Could not get phy reset control\n");
		ret = PTR_ERR(phy->rst_phy);
		goto err_disable_clk_phy;
	}

	ret = reset_control_deassert(phy->rst_phy);
	if (ret) {
		dev_err(dev, "Cannot deassert phy reset control: %d\n", ret);
		goto err_put_rst_phy;
	}

	ret = clk_prepare_enable(phy->clk_bus);
	if (ret) {
		dev_err(dev, "Cannot enable bus clock: %d\n", ret);
		goto err_deassert_rst_phy;
	}

	ret = clk_prepare_enable(phy->clk_mod);
	if (ret) {
		dev_err(dev, "Cannot enable mod clock: %d\n", ret);
		goto err_disable_clk_bus;
	}

	hdmi->phy = phy;

	return 0;

err_disable_clk_bus:
	clk_disable_unprepare(phy->clk_bus);
err_deassert_rst_phy:
	reset_control_assert(phy->rst_phy);
err_put_rst_phy:
	reset_control_put(phy->rst_phy);
err_disable_clk_phy:
	clk_disable_unprepare(phy->clk_phy);
err_put_clk_pll0:
	if (phy->variant->has_phy_clk)
		clk_put(phy->clk_pll0);
err_put_clk_mod:
	clk_put(phy->clk_mod);
err_put_clk_bus:
	clk_put(phy->clk_bus);

	return ret;
}

void sun8i_hdmi_phy_remove(struct sun8i_dw_hdmi *hdmi)
{
	struct sun8i_hdmi_phy *phy = hdmi->phy;

	clk_disable_unprepare(phy->clk_mod);
	clk_disable_unprepare(phy->clk_bus);
	clk_disable_unprepare(phy->clk_phy);

	reset_control_assert(phy->rst_phy);

	reset_control_put(phy->rst_phy);

	if (phy->variant->has_phy_clk)
		clk_put(phy->clk_pll0);
	clk_put(phy->clk_mod);
	clk_put(phy->clk_bus);
}
