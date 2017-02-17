/*
 * ispcsiphy.c
 *
 * TI OMAP3 ISP - CSI PHY module
 *
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include "isp.h"
#include "ispreg.h"
#include "ispcsiphy.h"

static void csiphy_routing_cfg_3630(struct isp_csiphy *phy,
				    enum isp_interface_type iface,
				    bool ccp2_strobe)
{
	u32 reg;
	u32 shift, mode;

	regmap_read(phy->isp->syscon, phy->isp->syscon_offset, &reg);

	switch (iface) {
	default:
	/* Should not happen in practice, but let's keep the compiler happy. */
	case ISP_INTERFACE_CCP2B_PHY1:
		reg &= ~OMAP3630_CONTROL_CAMERA_PHY_CTRL_CSI1_RX_SEL_PHY2;
		shift = OMAP3630_CONTROL_CAMERA_PHY_CTRL_CAMMODE_PHY1_SHIFT;
		break;
	case ISP_INTERFACE_CSI2C_PHY1:
		shift = OMAP3630_CONTROL_CAMERA_PHY_CTRL_CAMMODE_PHY1_SHIFT;
		mode = OMAP3630_CONTROL_CAMERA_PHY_CTRL_CAMMODE_DPHY;
		break;
	case ISP_INTERFACE_CCP2B_PHY2:
		reg |= OMAP3630_CONTROL_CAMERA_PHY_CTRL_CSI1_RX_SEL_PHY2;
		shift = OMAP3630_CONTROL_CAMERA_PHY_CTRL_CAMMODE_PHY2_SHIFT;
		break;
	case ISP_INTERFACE_CSI2A_PHY2:
		shift = OMAP3630_CONTROL_CAMERA_PHY_CTRL_CAMMODE_PHY2_SHIFT;
		mode = OMAP3630_CONTROL_CAMERA_PHY_CTRL_CAMMODE_DPHY;
		break;
	}

	/* Select data/clock or data/strobe mode for CCP2 */
	if (iface == ISP_INTERFACE_CCP2B_PHY1 ||
	    iface == ISP_INTERFACE_CCP2B_PHY2) {
		if (ccp2_strobe)
			mode = OMAP3630_CONTROL_CAMERA_PHY_CTRL_CAMMODE_CCP2_DATA_STROBE;
		else
			mode = OMAP3630_CONTROL_CAMERA_PHY_CTRL_CAMMODE_CCP2_DATA_CLOCK;
	}

	reg &= ~(OMAP3630_CONTROL_CAMERA_PHY_CTRL_CAMMODE_MASK << shift);
	reg |= mode << shift;

	regmap_write(phy->isp->syscon, phy->isp->syscon_offset, reg);
}

static void csiphy_routing_cfg_3430(struct isp_csiphy *phy, u32 iface, bool on,
				    bool ccp2_strobe)
{
	u32 csirxfe = OMAP343X_CONTROL_CSIRXFE_PWRDNZ
		| OMAP343X_CONTROL_CSIRXFE_RESET;

	/* Only the CCP2B on PHY1 is configurable. */
	if (iface != ISP_INTERFACE_CCP2B_PHY1)
		return;

	if (!on) {
		regmap_write(phy->isp->syscon, phy->isp->syscon_offset, 0);
		return;
	}

	if (ccp2_strobe)
		csirxfe |= OMAP343X_CONTROL_CSIRXFE_SELFORM;

	regmap_write(phy->isp->syscon, phy->isp->syscon_offset, csirxfe);
}

/*
 * Configure OMAP 3 CSI PHY routing.
 * @phy: relevant phy device
 * @iface: ISP_INTERFACE_*
 * @on: power on or off
 * @ccp2_strobe: false: data/clock, true: data/strobe
 *
 * Note that the underlying routing configuration registers are part of the
 * control (SCM) register space and part of the CORE power domain on both 3430
 * and 3630, so they will not hold their contents in off-mode. This isn't an
 * issue since the MPU power domain is forced on whilst the ISP is in use.
 */
static void csiphy_routing_cfg(struct isp_csiphy *phy,
			       enum isp_interface_type iface, bool on,
			       bool ccp2_strobe)
{
	if (phy->isp->phy_type == ISP_PHY_TYPE_3630 && on)
		return csiphy_routing_cfg_3630(phy, iface, ccp2_strobe);
	if (phy->isp->phy_type == ISP_PHY_TYPE_3430)
		return csiphy_routing_cfg_3430(phy, iface, on, ccp2_strobe);
}

/*
 * csiphy_power_autoswitch_enable
 * @enable: Sets or clears the autoswitch function enable flag.
 */
static void csiphy_power_autoswitch_enable(struct isp_csiphy *phy, bool enable)
{
	isp_reg_clr_set(phy->isp, phy->cfg_regs, ISPCSI2_PHY_CFG,
			ISPCSI2_PHY_CFG_PWR_AUTO,
			enable ? ISPCSI2_PHY_CFG_PWR_AUTO : 0);
}

/*
 * csiphy_set_power
 * @power: Power state to be set.
 *
 * Returns 0 if successful, or -EBUSY if the retry count is exceeded.
 */
static int csiphy_set_power(struct isp_csiphy *phy, u32 power)
{
	u32 reg;
	u8 retry_count;

	isp_reg_clr_set(phy->isp, phy->cfg_regs, ISPCSI2_PHY_CFG,
			ISPCSI2_PHY_CFG_PWR_CMD_MASK, power);

	retry_count = 0;
	do {
		udelay(50);
		reg = isp_reg_readl(phy->isp, phy->cfg_regs, ISPCSI2_PHY_CFG) &
				    ISPCSI2_PHY_CFG_PWR_STATUS_MASK;

		if (reg != power >> 2)
			retry_count++;

	} while ((reg != power >> 2) && (retry_count < 100));

	if (retry_count == 100) {
		dev_err(phy->isp->dev, "CSI2 CIO set power failed!\n");
		return -EBUSY;
	}

	return 0;
}

/*
 * TCLK values are OK at their reset values
 */
#define TCLK_TERM	0
#define TCLK_MISS	1
#define TCLK_SETTLE	14

static int omap3isp_csiphy_config(struct isp_csiphy *phy)
{
	struct isp_csi2_device *csi2 = phy->csi2;
	struct isp_pipeline *pipe = to_isp_pipeline(&csi2->subdev.entity);
	struct isp_bus_cfg *buscfg = pipe->external->host_priv;
	struct isp_csiphy_lanes_cfg *lanes;
	int csi2_ddrclk_khz;
	unsigned int used_lanes = 0;
	unsigned int i;
	u32 reg;

	if (!buscfg) {
		struct isp_async_subdev *isd =
			container_of(pipe->external->asd,
				     struct isp_async_subdev, asd);
		buscfg = &isd->bus;
	}

	if (buscfg->interface == ISP_INTERFACE_CCP2B_PHY1
	    || buscfg->interface == ISP_INTERFACE_CCP2B_PHY2)
		lanes = &buscfg->bus.ccp2.lanecfg;
	else
		lanes = &buscfg->bus.csi2.lanecfg;

	/* Clock and data lanes verification */
	for (i = 0; i < phy->num_data_lanes; i++) {
		if (lanes->data[i].pol > 1 || lanes->data[i].pos > 3)
			return -EINVAL;

		if (used_lanes & (1 << lanes->data[i].pos))
			return -EINVAL;

		used_lanes |= 1 << lanes->data[i].pos;
	}

	if (lanes->clk.pol > 1 || lanes->clk.pos > 3)
		return -EINVAL;

	if (lanes->clk.pos == 0 || used_lanes & (1 << lanes->clk.pos))
		return -EINVAL;

	/*
	 * The PHY configuration is lost in off mode, that's not an
	 * issue since the MPU power domain is forced on whilst the
	 * ISP is in use.
	 */
	csiphy_routing_cfg(phy, buscfg->interface, true,
			   buscfg->bus.ccp2.phy_layer);

	/* DPHY timing configuration */
	/* CSI-2 is DDR and we only count used lanes. */
	csi2_ddrclk_khz = pipe->external_rate / 1000
		/ (2 * hweight32(used_lanes)) * pipe->external_width;

	reg = isp_reg_readl(csi2->isp, phy->phy_regs, ISPCSIPHY_REG0);

	reg &= ~(ISPCSIPHY_REG0_THS_TERM_MASK |
		 ISPCSIPHY_REG0_THS_SETTLE_MASK);
	/* THS_TERM: Programmed value = ceil(12.5 ns/DDRClk period) - 1. */
	reg |= (DIV_ROUND_UP(25 * csi2_ddrclk_khz, 2000000) - 1)
		<< ISPCSIPHY_REG0_THS_TERM_SHIFT;
	/* THS_SETTLE: Programmed value = ceil(90 ns/DDRClk period) + 3. */
	reg |= (DIV_ROUND_UP(90 * csi2_ddrclk_khz, 1000000) + 3)
		<< ISPCSIPHY_REG0_THS_SETTLE_SHIFT;

	isp_reg_writel(csi2->isp, reg, phy->phy_regs, ISPCSIPHY_REG0);

	reg = isp_reg_readl(csi2->isp, phy->phy_regs, ISPCSIPHY_REG1);

	reg &= ~(ISPCSIPHY_REG1_TCLK_TERM_MASK |
		 ISPCSIPHY_REG1_TCLK_MISS_MASK |
		 ISPCSIPHY_REG1_TCLK_SETTLE_MASK);
	reg |= TCLK_TERM << ISPCSIPHY_REG1_TCLK_TERM_SHIFT;
	reg |= TCLK_MISS << ISPCSIPHY_REG1_TCLK_MISS_SHIFT;
	reg |= TCLK_SETTLE << ISPCSIPHY_REG1_TCLK_SETTLE_SHIFT;

	isp_reg_writel(csi2->isp, reg, phy->phy_regs, ISPCSIPHY_REG1);

	/* DPHY lane configuration */
	reg = isp_reg_readl(csi2->isp, phy->cfg_regs, ISPCSI2_PHY_CFG);

	for (i = 0; i < phy->num_data_lanes; i++) {
		reg &= ~(ISPCSI2_PHY_CFG_DATA_POL_MASK(i + 1) |
			 ISPCSI2_PHY_CFG_DATA_POSITION_MASK(i + 1));
		reg |= (lanes->data[i].pol <<
			ISPCSI2_PHY_CFG_DATA_POL_SHIFT(i + 1));
		reg |= (lanes->data[i].pos <<
			ISPCSI2_PHY_CFG_DATA_POSITION_SHIFT(i + 1));
	}

	reg &= ~(ISPCSI2_PHY_CFG_CLOCK_POL_MASK |
		 ISPCSI2_PHY_CFG_CLOCK_POSITION_MASK);
	reg |= lanes->clk.pol << ISPCSI2_PHY_CFG_CLOCK_POL_SHIFT;
	reg |= lanes->clk.pos << ISPCSI2_PHY_CFG_CLOCK_POSITION_SHIFT;

	isp_reg_writel(csi2->isp, reg, phy->cfg_regs, ISPCSI2_PHY_CFG);

	return 0;
}

int omap3isp_csiphy_acquire(struct isp_csiphy *phy)
{
	int rval;

	if (phy->vdd == NULL) {
		dev_err(phy->isp->dev,
			"Power regulator for CSI PHY not available\n");
		return -ENODEV;
	}

	mutex_lock(&phy->mutex);

	rval = regulator_enable(phy->vdd);
	if (rval < 0)
		goto done;

	rval = omap3isp_csi2_reset(phy->csi2);
	if (rval < 0)
		goto done;

	rval = omap3isp_csiphy_config(phy);
	if (rval < 0)
		goto done;

	rval = csiphy_set_power(phy, ISPCSI2_PHY_CFG_PWR_CMD_ON);
	if (rval) {
		regulator_disable(phy->vdd);
		goto done;
	}

	csiphy_power_autoswitch_enable(phy, true);
	phy->phy_in_use = 1;

done:
	mutex_unlock(&phy->mutex);
	return rval;
}

void omap3isp_csiphy_release(struct isp_csiphy *phy)
{
	mutex_lock(&phy->mutex);
	if (phy->phy_in_use) {
		struct isp_csi2_device *csi2 = phy->csi2;
		struct isp_pipeline *pipe =
			to_isp_pipeline(&csi2->subdev.entity);
		struct isp_bus_cfg *buscfg = pipe->external->host_priv;

		csiphy_routing_cfg(phy, buscfg->interface, false,
				   buscfg->bus.ccp2.phy_layer);
		csiphy_power_autoswitch_enable(phy, false);
		csiphy_set_power(phy, ISPCSI2_PHY_CFG_PWR_CMD_OFF);
		regulator_disable(phy->vdd);
		phy->phy_in_use = 0;
	}
	mutex_unlock(&phy->mutex);
}

/*
 * omap3isp_csiphy_init - Initialize the CSI PHY frontends
 */
int omap3isp_csiphy_init(struct isp_device *isp)
{
	struct isp_csiphy *phy1 = &isp->isp_csiphy1;
	struct isp_csiphy *phy2 = &isp->isp_csiphy2;

	phy2->isp = isp;
	phy2->csi2 = &isp->isp_csi2a;
	phy2->num_data_lanes = ISP_CSIPHY2_NUM_DATA_LANES;
	phy2->cfg_regs = OMAP3_ISP_IOMEM_CSI2A_REGS1;
	phy2->phy_regs = OMAP3_ISP_IOMEM_CSIPHY2;
	mutex_init(&phy2->mutex);

	if (isp->revision == ISP_REVISION_15_0) {
		phy1->isp = isp;
		phy1->csi2 = &isp->isp_csi2c;
		phy1->num_data_lanes = ISP_CSIPHY1_NUM_DATA_LANES;
		phy1->cfg_regs = OMAP3_ISP_IOMEM_CSI2C_REGS1;
		phy1->phy_regs = OMAP3_ISP_IOMEM_CSIPHY1;
		mutex_init(&phy1->mutex);
	}

	return 0;
}
