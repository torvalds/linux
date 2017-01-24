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

/* Qualcomm Technologies, Inc. FSM9900 EMAC SGMII Controller driver.
 */

#include <linux/iopoll.h>
#include "emac.h"

/* EMAC_QSERDES register offsets */
#define EMAC_QSERDES_COM_SYS_CLK_CTRL		0x0000
#define EMAC_QSERDES_COM_PLL_CNTRL		0x0014
#define EMAC_QSERDES_COM_PLL_IP_SETI		0x0018
#define EMAC_QSERDES_COM_PLL_CP_SETI		0x0024
#define EMAC_QSERDES_COM_PLL_IP_SETP		0x0028
#define EMAC_QSERDES_COM_PLL_CP_SETP		0x002c
#define EMAC_QSERDES_COM_SYSCLK_EN_SEL		0x0038
#define EMAC_QSERDES_COM_RESETSM_CNTRL		0x0040
#define EMAC_QSERDES_COM_PLLLOCK_CMP1		0x0044
#define EMAC_QSERDES_COM_PLLLOCK_CMP2		0x0048
#define EMAC_QSERDES_COM_PLLLOCK_CMP3		0x004c
#define EMAC_QSERDES_COM_PLLLOCK_CMP_EN		0x0050
#define EMAC_QSERDES_COM_DEC_START1		0x0064
#define EMAC_QSERDES_COM_DIV_FRAC_START1	0x0098
#define EMAC_QSERDES_COM_DIV_FRAC_START2	0x009c
#define EMAC_QSERDES_COM_DIV_FRAC_START3	0x00a0
#define EMAC_QSERDES_COM_DEC_START2		0x00a4
#define EMAC_QSERDES_COM_PLL_CRCTRL		0x00ac
#define EMAC_QSERDES_COM_RESET_SM		0x00bc
#define EMAC_QSERDES_TX_BIST_MODE_LANENO	0x0100
#define EMAC_QSERDES_TX_TX_EMP_POST1_LVL	0x0108
#define EMAC_QSERDES_TX_TX_DRV_LVL		0x010c
#define EMAC_QSERDES_TX_LANE_MODE		0x0150
#define EMAC_QSERDES_TX_TRAN_DRVR_EMP_EN	0x0170
#define EMAC_QSERDES_RX_CDR_CONTROL		0x0200
#define EMAC_QSERDES_RX_CDR_CONTROL2		0x0210
#define EMAC_QSERDES_RX_RX_EQ_GAIN12		0x0230

/* EMAC_SGMII register offsets */
#define EMAC_SGMII_PHY_SERDES_START		0x0000
#define EMAC_SGMII_PHY_CMN_PWR_CTRL		0x0004
#define EMAC_SGMII_PHY_RX_PWR_CTRL		0x0008
#define EMAC_SGMII_PHY_TX_PWR_CTRL		0x000C
#define EMAC_SGMII_PHY_LANE_CTRL1		0x0018
#define EMAC_SGMII_PHY_CDR_CTRL0		0x0058
#define EMAC_SGMII_PHY_POW_DWN_CTRL0		0x0080
#define EMAC_SGMII_PHY_INTERRUPT_MASK		0x00b4

#define PLL_IPSETI(x)				((x) & 0x3f)

#define PLL_CPSETI(x)				((x) & 0xff)

#define PLL_IPSETP(x)				((x) & 0x3f)

#define PLL_CPSETP(x)				((x) & 0x1f)

#define PLL_RCTRL(x)				(((x) & 0xf) << 4)
#define PLL_CCTRL(x)				((x) & 0xf)

#define LANE_MODE(x)				((x) & 0x1f)

#define SYSCLK_CM				BIT(4)
#define SYSCLK_AC_COUPLE			BIT(3)

#define OCP_EN					BIT(5)
#define PLL_DIV_FFEN				BIT(2)
#define PLL_DIV_ORD				BIT(1)

#define SYSCLK_SEL_CMOS				BIT(3)

#define FRQ_TUNE_MODE				BIT(4)

#define PLLLOCK_CMP_EN				BIT(0)

#define DEC_START1_MUX				BIT(7)
#define DEC_START1(x)				((x) & 0x7f)

#define DIV_FRAC_START_MUX			BIT(7)
#define DIV_FRAC_START(x)			((x) & 0x7f)

#define DIV_FRAC_START3_MUX			BIT(4)
#define DIV_FRAC_START3(x)			((x) & 0xf)

#define DEC_START2_MUX				BIT(1)
#define DEC_START2				BIT(0)

#define READY					BIT(5)

#define TX_EMP_POST1_LVL_MUX			BIT(5)
#define TX_EMP_POST1_LVL(x)			((x) & 0x1f)

#define TX_DRV_LVL_MUX				BIT(4)
#define TX_DRV_LVL(x)				((x) & 0xf)

#define EMP_EN_MUX				BIT(1)
#define EMP_EN					BIT(0)

#define SECONDORDERENABLE			BIT(6)
#define FIRSTORDER_THRESH(x)			(((x) & 0x7) << 3)
#define SECONDORDERGAIN(x)			((x) & 0x7)

#define RX_EQ_GAIN2(x)				(((x) & 0xf) << 4)
#define RX_EQ_GAIN1(x)				((x) & 0xf)

#define SERDES_START				BIT(0)

#define BIAS_EN					BIT(6)
#define PLL_EN					BIT(5)
#define SYSCLK_EN				BIT(4)
#define CLKBUF_L_EN				BIT(3)
#define PLL_TXCLK_EN				BIT(1)
#define PLL_RXCLK_EN				BIT(0)

#define L0_RX_SIGDET_EN				BIT(7)
#define L0_RX_TERM_MODE(x)			(((x) & 3) << 4)
#define L0_RX_I_EN				BIT(1)

#define L0_TX_EN				BIT(5)
#define L0_CLKBUF_EN				BIT(4)
#define L0_TRAN_BIAS_EN				BIT(1)

#define L0_RX_EQUALIZE_ENABLE			BIT(6)
#define L0_RESET_TSYNC_EN			BIT(4)
#define L0_DRV_LVL(x)				((x) & 0xf)

#define PWRDN_B					BIT(0)
#define CDR_MAX_CNT(x)				((x) & 0xff)

#define PLLLOCK_CMP(x)				((x) & 0xff)

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

static const struct emac_reg_write physical_coding_sublayer_programming[] = {
	{EMAC_SGMII_PHY_CDR_CTRL0, CDR_MAX_CNT(15)},
	{EMAC_SGMII_PHY_POW_DWN_CTRL0, PWRDN_B},
	{EMAC_SGMII_PHY_CMN_PWR_CTRL,
		BIAS_EN | SYSCLK_EN | CLKBUF_L_EN | PLL_TXCLK_EN | PLL_RXCLK_EN},
	{EMAC_SGMII_PHY_TX_PWR_CTRL, L0_TX_EN | L0_CLKBUF_EN | L0_TRAN_BIAS_EN},
	{EMAC_SGMII_PHY_RX_PWR_CTRL,
		L0_RX_SIGDET_EN | L0_RX_TERM_MODE(1) | L0_RX_I_EN},
	{EMAC_SGMII_PHY_CMN_PWR_CTRL,
		BIAS_EN | PLL_EN | SYSCLK_EN | CLKBUF_L_EN | PLL_TXCLK_EN |
		PLL_RXCLK_EN},
	{EMAC_SGMII_PHY_LANE_CTRL1,
		L0_RX_EQUALIZE_ENABLE | L0_RESET_TSYNC_EN | L0_DRV_LVL(15)},
};

static const struct emac_reg_write sysclk_refclk_setting[] = {
	{EMAC_QSERDES_COM_SYSCLK_EN_SEL, SYSCLK_SEL_CMOS},
	{EMAC_QSERDES_COM_SYS_CLK_CTRL,	SYSCLK_CM | SYSCLK_AC_COUPLE},
};

static const struct emac_reg_write pll_setting[] = {
	{EMAC_QSERDES_COM_PLL_IP_SETI, PLL_IPSETI(1)},
	{EMAC_QSERDES_COM_PLL_CP_SETI, PLL_CPSETI(59)},
	{EMAC_QSERDES_COM_PLL_IP_SETP, PLL_IPSETP(10)},
	{EMAC_QSERDES_COM_PLL_CP_SETP, PLL_CPSETP(9)},
	{EMAC_QSERDES_COM_PLL_CRCTRL, PLL_RCTRL(15) | PLL_CCTRL(11)},
	{EMAC_QSERDES_COM_PLL_CNTRL, OCP_EN | PLL_DIV_FFEN | PLL_DIV_ORD},
	{EMAC_QSERDES_COM_DEC_START1, DEC_START1_MUX | DEC_START1(2)},
	{EMAC_QSERDES_COM_DEC_START2, DEC_START2_MUX | DEC_START2},
	{EMAC_QSERDES_COM_DIV_FRAC_START1,
		DIV_FRAC_START_MUX | DIV_FRAC_START(85)},
	{EMAC_QSERDES_COM_DIV_FRAC_START2,
		DIV_FRAC_START_MUX | DIV_FRAC_START(42)},
	{EMAC_QSERDES_COM_DIV_FRAC_START3,
		DIV_FRAC_START3_MUX | DIV_FRAC_START3(3)},
	{EMAC_QSERDES_COM_PLLLOCK_CMP1, PLLLOCK_CMP(43)},
	{EMAC_QSERDES_COM_PLLLOCK_CMP2, PLLLOCK_CMP(104)},
	{EMAC_QSERDES_COM_PLLLOCK_CMP3, PLLLOCK_CMP(0)},
	{EMAC_QSERDES_COM_PLLLOCK_CMP_EN, PLLLOCK_CMP_EN},
	{EMAC_QSERDES_COM_RESETSM_CNTRL, FRQ_TUNE_MODE},
};

static const struct emac_reg_write cdr_setting[] = {
	{EMAC_QSERDES_RX_CDR_CONTROL,
		SECONDORDERENABLE | FIRSTORDER_THRESH(3) | SECONDORDERGAIN(2)},
	{EMAC_QSERDES_RX_CDR_CONTROL2,
		SECONDORDERENABLE | FIRSTORDER_THRESH(3) | SECONDORDERGAIN(4)},
};

static const struct emac_reg_write tx_rx_setting[] = {
	{EMAC_QSERDES_TX_BIST_MODE_LANENO, 0},
	{EMAC_QSERDES_TX_TX_DRV_LVL, TX_DRV_LVL_MUX | TX_DRV_LVL(15)},
	{EMAC_QSERDES_TX_TRAN_DRVR_EMP_EN, EMP_EN_MUX | EMP_EN},
	{EMAC_QSERDES_TX_TX_EMP_POST1_LVL,
		TX_EMP_POST1_LVL_MUX | TX_EMP_POST1_LVL(1)},
	{EMAC_QSERDES_RX_RX_EQ_GAIN12, RX_EQ_GAIN2(15) | RX_EQ_GAIN1(15)},
	{EMAC_QSERDES_TX_LANE_MODE, LANE_MODE(8)},
};

int emac_sgmii_init_fsm9900(struct emac_adapter *adpt)
{
	struct emac_phy *phy = &adpt->phy;
	unsigned int i;

	emac_reg_write_all(phy->base, physical_coding_sublayer_programming,
			   ARRAY_SIZE(physical_coding_sublayer_programming));
	emac_reg_write_all(phy->base, sysclk_refclk_setting,
			   ARRAY_SIZE(sysclk_refclk_setting));
	emac_reg_write_all(phy->base, pll_setting, ARRAY_SIZE(pll_setting));
	emac_reg_write_all(phy->base, cdr_setting, ARRAY_SIZE(cdr_setting));
	emac_reg_write_all(phy->base, tx_rx_setting, ARRAY_SIZE(tx_rx_setting));

	/* Power up the Ser/Des engine */
	writel(SERDES_START, phy->base + EMAC_SGMII_PHY_SERDES_START);

	for (i = 0; i < SERDES_START_WAIT_TIMES; i++) {
		if (readl(phy->base + EMAC_QSERDES_COM_RESET_SM) & READY)
			break;
		usleep_range(100, 200);
	}

	if (i == SERDES_START_WAIT_TIMES) {
		netdev_err(adpt->netdev, "error: ser/des failed to start\n");
		return -EIO;
	}
	/* Mask out all the SGMII Interrupt */
	writel(0, phy->base + EMAC_SGMII_PHY_INTERRUPT_MASK);

	return 0;
}
