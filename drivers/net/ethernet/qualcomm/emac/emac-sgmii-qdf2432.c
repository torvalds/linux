/* Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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

/* Qualcomm Technologies, Inc. QDF2432 EMAC SGMII Controller driver.
 */

#include <linux/iopoll.h>
#include "emac.h"

/* EMAC_SGMII register offsets */
#define EMAC_SGMII_PHY_TX_PWR_CTRL		0x000C
#define EMAC_SGMII_PHY_LANE_CTRL1		0x0018
#define EMAC_SGMII_PHY_CDR_CTRL0		0x0058
#define EMAC_SGMII_PHY_POW_DWN_CTRL0		0x0080
#define EMAC_SGMII_PHY_RESET_CTRL		0x00a8
#define EMAC_SGMII_PHY_INTERRUPT_MASK		0x00b4

/* SGMII digital lane registers */
#define EMAC_SGMII_LN_DRVR_CTRL0		0x000C
#define EMAC_SGMII_LN_DRVR_TAP_EN		0x0018
#define EMAC_SGMII_LN_TX_MARGINING		0x001C
#define EMAC_SGMII_LN_TX_PRE			0x0020
#define EMAC_SGMII_LN_TX_POST			0x0024
#define EMAC_SGMII_LN_TX_BAND_MODE		0x0060
#define EMAC_SGMII_LN_LANE_MODE			0x0064
#define EMAC_SGMII_LN_PARALLEL_RATE		0x0078
#define EMAC_SGMII_LN_CML_CTRL_MODE0		0x00B8
#define EMAC_SGMII_LN_MIXER_CTRL_MODE0		0x00D0
#define EMAC_SGMII_LN_VGA_INITVAL		0x0134
#define EMAC_SGMII_LN_UCDR_FO_GAIN_MODE0	0x017C
#define EMAC_SGMII_LN_UCDR_SO_GAIN_MODE0	0x0188
#define EMAC_SGMII_LN_UCDR_SO_CONFIG		0x0194
#define EMAC_SGMII_LN_RX_BAND			0x019C
#define EMAC_SGMII_LN_RX_RCVR_PATH1_MODE0	0x01B8
#define EMAC_SGMII_LN_RSM_CONFIG		0x01F0
#define EMAC_SGMII_LN_SIGDET_ENABLES		0x0224
#define EMAC_SGMII_LN_SIGDET_CNTRL		0x0228
#define EMAC_SGMII_LN_SIGDET_DEGLITCH_CNTRL	0x022C
#define EMAC_SGMII_LN_RX_EN_SIGNAL		0x02A0
#define EMAC_SGMII_LN_RX_MISC_CNTRL0		0x02AC
#define EMAC_SGMII_LN_DRVR_LOGIC_CLKDIV		0x02BC

/* SGMII digital lane register values */
#define UCDR_STEP_BY_TWO_MODE0			BIT(7)
#define UCDR_xO_GAIN_MODE(x)			((x) & 0x7f)
#define UCDR_ENABLE				BIT(6)
#define UCDR_SO_SATURATION(x)			((x) & 0x3f)

#define SIGDET_LP_BYP_PS4			BIT(7)
#define SIGDET_EN_PS0_TO_PS2			BIT(6)

#define TXVAL_VALID_INIT			BIT(4)
#define KR_PCIGEN3_MODE				BIT(0)

#define MAIN_EN					BIT(0)

#define TX_MARGINING_MUX			BIT(6)
#define TX_MARGINING(x)				((x) & 0x3f)

#define TX_PRE_MUX				BIT(6)

#define TX_POST_MUX				BIT(6)

#define CML_GEAR_MODE(x)			(((x) & 7) << 3)
#define CML2CMOS_IBOOST_MODE(x)			((x) & 7)

#define MIXER_LOADB_MODE(x)			(((x) & 0xf) << 2)
#define MIXER_DATARATE_MODE(x)			((x) & 3)

#define VGA_THRESH_DFE(x)			((x) & 0x3f)

#define SIGDET_LP_BYP_PS0_TO_PS2		BIT(5)
#define SIGDET_FLT_BYP				BIT(0)

#define SIGDET_LVL(x)				(((x) & 0xf) << 4)

#define SIGDET_DEGLITCH_CTRL(x)			(((x) & 0xf) << 1)

#define DRVR_LOGIC_CLK_EN			BIT(4)
#define DRVR_LOGIC_CLK_DIV(x)			((x) & 0xf)

#define PARALLEL_RATE_MODE0(x)			((x) & 0x3)

#define BAND_MODE0(x)				((x) & 0x3)

#define LANE_MODE(x)				((x) & 0x1f)

#define CDR_PD_SEL_MODE0(x)			(((x) & 0x3) << 5)
#define BYPASS_RSM_SAMP_CAL			BIT(1)
#define BYPASS_RSM_DLL_CAL			BIT(0)

#define L0_RX_EQUALIZE_ENABLE			BIT(6)

#define PWRDN_B					BIT(0)

#define CDR_MAX_CNT(x)				((x) & 0xff)

#define SERDES_START_WAIT_TIMES			100

struct emac_reg_write {
	unsigned int offset;
	u32 val;
};

static void emac_reg_write_all(void __iomem *base,
			       const struct emac_reg_write *itr, size_t size)
{
	size_t i;

	for (i = 0; i < size; ++itr, ++i)
		writel(itr->val, base + itr->offset);
}

static const struct emac_reg_write sgmii_laned[] = {
	/* CDR Settings */
	{EMAC_SGMII_LN_UCDR_FO_GAIN_MODE0,
		UCDR_STEP_BY_TWO_MODE0 | UCDR_xO_GAIN_MODE(10)},
	{EMAC_SGMII_LN_UCDR_SO_GAIN_MODE0, UCDR_xO_GAIN_MODE(0)},
	{EMAC_SGMII_LN_UCDR_SO_CONFIG, UCDR_ENABLE | UCDR_SO_SATURATION(12)},

	/* TX/RX Settings */
	{EMAC_SGMII_LN_RX_EN_SIGNAL, SIGDET_LP_BYP_PS4 | SIGDET_EN_PS0_TO_PS2},

	{EMAC_SGMII_LN_DRVR_CTRL0, TXVAL_VALID_INIT | KR_PCIGEN3_MODE},
	{EMAC_SGMII_LN_DRVR_TAP_EN, MAIN_EN},
	{EMAC_SGMII_LN_TX_MARGINING, TX_MARGINING_MUX | TX_MARGINING(25)},
	{EMAC_SGMII_LN_TX_PRE, TX_PRE_MUX},
	{EMAC_SGMII_LN_TX_POST, TX_POST_MUX},

	{EMAC_SGMII_LN_CML_CTRL_MODE0,
		CML_GEAR_MODE(1) | CML2CMOS_IBOOST_MODE(1)},
	{EMAC_SGMII_LN_MIXER_CTRL_MODE0,
		MIXER_LOADB_MODE(12) | MIXER_DATARATE_MODE(1)},
	{EMAC_SGMII_LN_VGA_INITVAL, VGA_THRESH_DFE(31)},
	{EMAC_SGMII_LN_SIGDET_ENABLES,
		SIGDET_LP_BYP_PS0_TO_PS2 | SIGDET_FLT_BYP},
	{EMAC_SGMII_LN_SIGDET_CNTRL, SIGDET_LVL(8)},

	{EMAC_SGMII_LN_SIGDET_DEGLITCH_CNTRL, SIGDET_DEGLITCH_CTRL(4)},
	{EMAC_SGMII_LN_RX_MISC_CNTRL0, 0},
	{EMAC_SGMII_LN_DRVR_LOGIC_CLKDIV,
		DRVR_LOGIC_CLK_EN | DRVR_LOGIC_CLK_DIV(4)},

	{EMAC_SGMII_LN_PARALLEL_RATE, PARALLEL_RATE_MODE0(1)},
	{EMAC_SGMII_LN_TX_BAND_MODE, BAND_MODE0(2)},
	{EMAC_SGMII_LN_RX_BAND, BAND_MODE0(3)},
	{EMAC_SGMII_LN_LANE_MODE, LANE_MODE(26)},
	{EMAC_SGMII_LN_RX_RCVR_PATH1_MODE0, CDR_PD_SEL_MODE0(3)},
	{EMAC_SGMII_LN_RSM_CONFIG, BYPASS_RSM_SAMP_CAL | BYPASS_RSM_DLL_CAL},
};

static const struct emac_reg_write physical_coding_sublayer_programming[] = {
	{EMAC_SGMII_PHY_POW_DWN_CTRL0, PWRDN_B},
	{EMAC_SGMII_PHY_CDR_CTRL0, CDR_MAX_CNT(15)},
	{EMAC_SGMII_PHY_TX_PWR_CTRL, 0},
	{EMAC_SGMII_PHY_LANE_CTRL1, L0_RX_EQUALIZE_ENABLE},
};

int emac_sgmii_init_qdf2432(struct emac_adapter *adpt)
{
	struct emac_phy *phy = &adpt->phy;
	void __iomem *phy_regs = phy->base;
	void __iomem *laned = phy->digital;
	unsigned int i;
	u32 lnstatus;

	/* PCS lane-x init */
	emac_reg_write_all(phy->base, physical_coding_sublayer_programming,
			   ARRAY_SIZE(physical_coding_sublayer_programming));

	/* SGMII lane-x init */
	emac_reg_write_all(phy->digital, sgmii_laned, ARRAY_SIZE(sgmii_laned));

	/* Power up PCS and start reset lane state machine */

	writel(0, phy_regs + EMAC_SGMII_PHY_RESET_CTRL);
	writel(1, laned + SGMII_LN_RSM_START);

	/* Wait for c_ready assertion */
	for (i = 0; i < SERDES_START_WAIT_TIMES; i++) {
		lnstatus = readl(phy_regs + SGMII_PHY_LN_LANE_STATUS);
		if (lnstatus & BIT(1))
			break;
		usleep_range(100, 200);
	}

	if (i == SERDES_START_WAIT_TIMES) {
		netdev_err(adpt->netdev, "SGMII failed to start\n");
		return -EIO;
	}

	/* Disable digital and SERDES loopback */
	writel(0, phy_regs + SGMII_PHY_LN_BIST_GEN0);
	writel(0, phy_regs + SGMII_PHY_LN_BIST_GEN2);
	writel(0, phy_regs + SGMII_PHY_LN_CDR_CTRL1);

	/* Mask out all the SGMII Interrupt */
	writel(0, phy_regs + EMAC_SGMII_PHY_INTERRUPT_MASK);

	return 0;
}
