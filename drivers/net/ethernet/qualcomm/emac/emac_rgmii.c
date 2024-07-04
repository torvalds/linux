// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
 *
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/* Qualcomm Technologies, Inc. EMAC RGMII Controller driver.
 */

#include "emac_main.h"
#include "emac_hw.h"

/* RGMII specific macros */
#define EMAC_RGMII_PLL_LOCK_TIMEOUT     (HZ / 1000) /* 1ms */
#define EMAC_RGMII_CORE_IE_C            0x2001
#define EMAC_RGMII_PLL_L_VAL            0x14
#define EMAC_RGMII_PHY_MODE             0

static int emac_rgmii_init(struct emac_adapter *adpt)
{
	u32 val;
	unsigned long timeout;
	struct emac_hw *hw = &adpt->hw;

	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR1, 0, FREQ_MODE);
	emac_reg_w32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR18,
		     EMAC_RGMII_CORE_IE_C);
	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR2,
			  RGMII_PHY_MODE_BMSK,
			  (EMAC_RGMII_PHY_MODE << RGMII_PHY_MODE_SHFT));
	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR2, PHY_RESET, 0);
	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR3,
			  PLL_L_VAL_5_0_BMSK,
			  (EMAC_RGMII_PLL_L_VAL << PLL_L_VAL_5_0_SHFT));

	/* Reset PHY PLL */
	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR3, 0, PLL_RESET);
	/* Ensure PLL is in reset */
	wmb();
	usleep_range(10, 15);

	/* power down analog sections of PLL and ensure the same */
	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR3, 0, BYPASSNL);
	/* Ensure power down is complete before setting configuration */
	wmb();
	usleep_range(10, 15);

	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR2, 0, CKEDGE_SEL);
	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR2,
			  TX_ID_EN_L, RX_ID_EN_L);
	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR2,
			  HDRIVE_BMSK, (0x0 << HDRIVE_SHFT));
	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR2, WOL_EN, 0);

	/* Reset PHY */
	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR2, 0, PHY_RESET);
	/* Ensure reset is complete before pulling out of reset */
	wmb();
	usleep_range(10, 15);

	/* Pull PHY out of reset */
	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR2, PHY_RESET, 0);
	/* Ensure that pulling PHY out of reset is complete before enabling the
	 * enabling
	 */
	wmb();
	usleep_range(1000, 1500);

	/* Pull PHY PLL out of reset */
	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR3, PLL_RESET, 0);
	/* Ensure PLL is enabled before enabling the AHB clock*/
	wmb();
	usleep_range(10, 15);

	emac_reg_update32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_CSR5,
			  0, RMII_125_CLK_EN);
	/* Ensure AHB clock enable is written to HW before the loop waiting for
	 * it to complete
	 */
	wmb();

	/* wait for PLL to lock */
	timeout = jiffies + EMAC_RGMII_PLL_LOCK_TIMEOUT;
	do {
		val = emac_reg_r32(hw, EMAC_CSR, EMAC_EMAC_WRAPPER_STATUS);
		if (val & PLL_LOCK_DET)
			break;
		usleep_range(100, 150);
	} while (time_after_eq(timeout, jiffies));

	if (time_after(jiffies, timeout)) {
		emac_err(adpt, "PHY PLL lock failed\n");
		return -EIO;
	}

	return 0;
}

static int emac_rgmii_config(struct platform_device *pdev,
			     struct emac_adapter *adpt)
{
	/* For rgmii phy, the mdio lines are dedicated pins */
	return emac_rgmii_init(adpt);
}

static void emac_rgmii_reset_nop(struct emac_adapter *adpt)
{
}

static int emac_rgmii_link_setup_no_ephy(struct emac_adapter *adpt)
{
	emac_err(adpt, "error rgmii can't setup phy link without ephy\n");
	return -EOPNOTSUPP;
}

static int emac_rgmii_link_check_no_ephy(struct emac_adapter *adpt,
					 struct phy_device *phydev)
{
	emac_err(adpt, "error rgmii can't check phy link without ephy\n");
	return -EOPNOTSUPP;
}

static int emac_rgmii_up_nop(struct emac_adapter *adpt)
{
	return 0;
}

static void emac_rgmii_down_nop(struct emac_adapter *adpt)
{
}

static void emac_rgmii_tx_clk_set_rate(struct emac_adapter *adpt)
{
	struct phy_device *phydev = adpt->phydev;

	switch (phydev->speed) {
	case SPEED_1000:
		clk_set_rate(adpt->clk[EMAC_CLK_TX].clk, EMC_CLK_RATE_125MHZ);
		break;
	case SPEED_100:
		clk_set_rate(adpt->clk[EMAC_CLK_TX].clk, EMC_CLK_RATE_25MHZ);
		break;
	case SPEED_10:
		clk_set_rate(adpt->clk[EMAC_CLK_TX].clk, EMC_CLK_RATE_2_5MHZ);
		break;
	default:
		emac_err(adpt, "error tx clk set rate because of unknown speed\n");
	}
}

static void emac_rgmii_periodic_nop(struct emac_adapter *adpt)
{
}

struct emac_phy_ops emac_rgmii_ops = {
	.config			= emac_rgmii_config,
	.up			= emac_rgmii_up_nop,
	.down			= emac_rgmii_down_nop,
	.reset			= emac_rgmii_reset_nop,
	.link_setup_no_ephy	= emac_rgmii_link_setup_no_ephy,
	.link_check_no_ephy	= emac_rgmii_link_check_no_ephy,
	.tx_clk_set_rate	= emac_rgmii_tx_clk_set_rate,
	.periodic_task		= emac_rgmii_periodic_nop,
};
