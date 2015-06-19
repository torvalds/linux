/*
 * TI OMAP4 ISS V4L2 Driver - CSI PHY module
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 *
 * Author: Sergio Aguirre <sergio.a.aguirre@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/regmap.h>

#include "../../../../arch/arm/mach-omap2/control.h"

#include "iss.h"
#include "iss_regs.h"
#include "iss_csiphy.h"

/*
 * csiphy_lanes_config - Configuration of CSIPHY lanes.
 *
 * Updates HW configuration.
 * Called with phy->mutex taken.
 */
static void csiphy_lanes_config(struct iss_csiphy *phy)
{
	unsigned int i;
	u32 reg;

	reg = iss_reg_read(phy->iss, phy->cfg_regs, CSI2_COMPLEXIO_CFG);

	for (i = 0; i < phy->max_data_lanes; i++) {
		reg &= ~(CSI2_COMPLEXIO_CFG_DATA_POL(i + 1) |
			 CSI2_COMPLEXIO_CFG_DATA_POSITION_MASK(i + 1));
		reg |= (phy->lanes.data[i].pol ?
			CSI2_COMPLEXIO_CFG_DATA_POL(i + 1) : 0);
		reg |= (phy->lanes.data[i].pos <<
			CSI2_COMPLEXIO_CFG_DATA_POSITION_SHIFT(i + 1));
	}

	reg &= ~(CSI2_COMPLEXIO_CFG_CLOCK_POL |
		 CSI2_COMPLEXIO_CFG_CLOCK_POSITION_MASK);
	reg |= phy->lanes.clk.pol ? CSI2_COMPLEXIO_CFG_CLOCK_POL : 0;
	reg |= phy->lanes.clk.pos << CSI2_COMPLEXIO_CFG_CLOCK_POSITION_SHIFT;

	iss_reg_write(phy->iss, phy->cfg_regs, CSI2_COMPLEXIO_CFG, reg);
}

/*
 * csiphy_set_power
 * @power: Power state to be set.
 *
 * Returns 0 if successful, or -EBUSY if the retry count is exceeded.
 */
static int csiphy_set_power(struct iss_csiphy *phy, u32 power)
{
	u32 reg;
	u8 retry_count;

	iss_reg_update(phy->iss, phy->cfg_regs, CSI2_COMPLEXIO_CFG,
		       CSI2_COMPLEXIO_CFG_PWD_CMD_MASK,
		       power | CSI2_COMPLEXIO_CFG_PWR_AUTO);

	retry_count = 0;
	do {
		udelay(1);
		reg = iss_reg_read(phy->iss, phy->cfg_regs, CSI2_COMPLEXIO_CFG)
		    & CSI2_COMPLEXIO_CFG_PWD_STATUS_MASK;

		if (reg != power >> 2)
			retry_count++;

	} while ((reg != power >> 2) && (retry_count < 250));

	if (retry_count == 250) {
		dev_err(phy->iss->dev, "CSI2 CIO set power failed!\n");
		return -EBUSY;
	}

	return 0;
}

/*
 * csiphy_dphy_config - Configure CSI2 D-PHY parameters.
 *
 * Called with phy->mutex taken.
 */
static void csiphy_dphy_config(struct iss_csiphy *phy)
{
	u32 reg;

	/* Set up REGISTER0 */
	reg = phy->dphy.ths_term << REGISTER0_THS_TERM_SHIFT;
	reg |= phy->dphy.ths_settle << REGISTER0_THS_SETTLE_SHIFT;

	iss_reg_write(phy->iss, phy->phy_regs, REGISTER0, reg);

	/* Set up REGISTER1 */
	reg = phy->dphy.tclk_term << REGISTER1_TCLK_TERM_SHIFT;
	reg |= phy->dphy.tclk_miss << REGISTER1_CTRLCLK_DIV_FACTOR_SHIFT;
	reg |= phy->dphy.tclk_settle << REGISTER1_TCLK_SETTLE_SHIFT;
	reg |= 0xb8 << REGISTER1_DPHY_HS_SYNC_PATTERN_SHIFT;

	iss_reg_write(phy->iss, phy->phy_regs, REGISTER1, reg);
}

/*
 * TCLK values are OK at their reset values
 */
#define TCLK_TERM	0
#define TCLK_MISS	1
#define TCLK_SETTLE	14

int omap4iss_csiphy_config(struct iss_device *iss,
			   struct v4l2_subdev *csi2_subdev)
{
	struct iss_csi2_device *csi2 = v4l2_get_subdevdata(csi2_subdev);
	struct iss_pipeline *pipe = to_iss_pipeline(&csi2_subdev->entity);
	struct iss_v4l2_subdevs_group *subdevs = pipe->external->host_priv;
	struct iss_csiphy_dphy_cfg csi2phy;
	int csi2_ddrclk_khz;
	struct iss_csiphy_lanes_cfg *lanes;
	unsigned int used_lanes = 0;
	u32 cam_rx_ctrl;
	unsigned int i;

	lanes = &subdevs->bus.csi2.lanecfg;

	/*
	 * SCM.CONTROL_CAMERA_RX
	 * - bit [31] : CSIPHY2 lane 2 enable (4460+ only)
	 * - bit [30:29] : CSIPHY2 per-lane enable (1 to 0)
	 * - bit [28:24] : CSIPHY1 per-lane enable (4 to 0)
	 * - bit [21] : CSIPHY2 CTRLCLK enable
	 * - bit [20:19] : CSIPHY2 config: 00 d-phy, 01/10 ccp2
	 * - bit [18] : CSIPHY1 CTRLCLK enable
	 * - bit [17:16] : CSIPHY1 config: 00 d-phy, 01/10 ccp2
	 */
	/*
	 * TODO: When implementing DT support specify the CONTROL_CAMERA_RX
	 * register offset in the syscon property instead of hardcoding it.
	 */
	regmap_read(iss->syscon, 0x68, &cam_rx_ctrl);

	if (subdevs->interface == ISS_INTERFACE_CSI2A_PHY1) {
		cam_rx_ctrl &= ~(OMAP4_CAMERARX_CSI21_LANEENABLE_MASK |
				OMAP4_CAMERARX_CSI21_CAMMODE_MASK);
		/* NOTE: Leave CSIPHY1 config to 0x0: D-PHY mode */
		/* Enable all lanes for now */
		cam_rx_ctrl |=
			0x1f << OMAP4_CAMERARX_CSI21_LANEENABLE_SHIFT;
		/* Enable CTRLCLK */
		cam_rx_ctrl |= OMAP4_CAMERARX_CSI21_CTRLCLKEN_MASK;
	}

	if (subdevs->interface == ISS_INTERFACE_CSI2B_PHY2) {
		cam_rx_ctrl &= ~(OMAP4_CAMERARX_CSI22_LANEENABLE_MASK |
				OMAP4_CAMERARX_CSI22_CAMMODE_MASK);
		/* NOTE: Leave CSIPHY2 config to 0x0: D-PHY mode */
		/* Enable all lanes for now */
		cam_rx_ctrl |=
			0x3 << OMAP4_CAMERARX_CSI22_LANEENABLE_SHIFT;
		/* Enable CTRLCLK */
		cam_rx_ctrl |= OMAP4_CAMERARX_CSI22_CTRLCLKEN_MASK;
	}

	regmap_write(iss->syscon, 0x68, cam_rx_ctrl);

	/* Reset used lane count */
	csi2->phy->used_data_lanes = 0;

	/* Clock and data lanes verification */
	for (i = 0; i < csi2->phy->max_data_lanes; i++) {
		if (lanes->data[i].pos == 0)
			continue;

		if (lanes->data[i].pol > 1 ||
		    lanes->data[i].pos > (csi2->phy->max_data_lanes + 1))
			return -EINVAL;

		if (used_lanes & (1 << lanes->data[i].pos))
			return -EINVAL;

		used_lanes |= 1 << lanes->data[i].pos;
		csi2->phy->used_data_lanes++;
	}

	if (lanes->clk.pol > 1 ||
	    lanes->clk.pos > (csi2->phy->max_data_lanes + 1))
		return -EINVAL;

	if (lanes->clk.pos == 0 || used_lanes & (1 << lanes->clk.pos))
		return -EINVAL;

	csi2_ddrclk_khz = pipe->external_rate / 1000
		/ (2 * csi2->phy->used_data_lanes)
		* pipe->external_bpp;

	/*
	 * THS_TERM: Programmed value = ceil(12.5 ns/DDRClk period) - 1.
	 * THS_SETTLE: Programmed value = ceil(90 ns/DDRClk period) + 3.
	 */
	csi2phy.ths_term = DIV_ROUND_UP(25 * csi2_ddrclk_khz, 2000000) - 1;
	csi2phy.ths_settle = DIV_ROUND_UP(90 * csi2_ddrclk_khz, 1000000) + 3;
	csi2phy.tclk_term = TCLK_TERM;
	csi2phy.tclk_miss = TCLK_MISS;
	csi2phy.tclk_settle = TCLK_SETTLE;

	mutex_lock(&csi2->phy->mutex);
	csi2->phy->dphy = csi2phy;
	csi2->phy->lanes = *lanes;
	mutex_unlock(&csi2->phy->mutex);

	return 0;
}

int omap4iss_csiphy_acquire(struct iss_csiphy *phy)
{
	int rval;

	mutex_lock(&phy->mutex);

	rval = omap4iss_csi2_reset(phy->csi2);
	if (rval)
		goto done;

	csiphy_dphy_config(phy);
	csiphy_lanes_config(phy);

	rval = csiphy_set_power(phy, CSI2_COMPLEXIO_CFG_PWD_CMD_ON);
	if (rval)
		goto done;

	phy->phy_in_use = 1;

done:
	mutex_unlock(&phy->mutex);
	return rval;
}

void omap4iss_csiphy_release(struct iss_csiphy *phy)
{
	mutex_lock(&phy->mutex);
	if (phy->phy_in_use) {
		csiphy_set_power(phy, CSI2_COMPLEXIO_CFG_PWD_CMD_OFF);
		phy->phy_in_use = 0;
	}
	mutex_unlock(&phy->mutex);
}

/*
 * omap4iss_csiphy_init - Initialize the CSI PHY frontends
 */
int omap4iss_csiphy_init(struct iss_device *iss)
{
	struct iss_csiphy *phy1 = &iss->csiphy1;
	struct iss_csiphy *phy2 = &iss->csiphy2;

	phy1->iss = iss;
	phy1->csi2 = &iss->csi2a;
	phy1->max_data_lanes = ISS_CSIPHY1_NUM_DATA_LANES;
	phy1->used_data_lanes = 0;
	phy1->cfg_regs = OMAP4_ISS_MEM_CSI2_A_REGS1;
	phy1->phy_regs = OMAP4_ISS_MEM_CAMERARX_CORE1;
	mutex_init(&phy1->mutex);

	phy2->iss = iss;
	phy2->csi2 = &iss->csi2b;
	phy2->max_data_lanes = ISS_CSIPHY2_NUM_DATA_LANES;
	phy2->used_data_lanes = 0;
	phy2->cfg_regs = OMAP4_ISS_MEM_CSI2_B_REGS1;
	phy2->phy_regs = OMAP4_ISS_MEM_CAMERARX_CORE2;
	mutex_init(&phy2->mutex);

	return 0;
}
