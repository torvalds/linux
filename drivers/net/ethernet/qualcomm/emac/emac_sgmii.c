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

/* Qualcomm Technologies, Inc. EMAC SGMII Controller driver.
 */
#include <linux/iopoll.h>
#include <linux/acpi.h>
#include <linux/of_device.h>

#include "emac_sgmii.h"
#include "emac_hw.h"

#define PCS_MAX_REG_CNT		10
#define PLL_MAX_REG_CNT		18

void emac_reg_write_all(void __iomem *base, const struct emac_reg_write *itr)
{
	for (; itr->offset != END_MARKER; ++itr)
		writel_relaxed(itr->val, base + itr->offset);
}

static const struct emac_reg_write
		physical_coding_sublayer_programming[][PCS_MAX_REG_CNT] = {
	/* EMAC_PHY_MAP_DEFAULT */
	{
		{EMAC_SGMII_PHY_CDR_CTRL0, CDR_MAX_CNT(15)},
		{EMAC_SGMII_PHY_POW_DWN_CTRL0, PWRDN_B},
		{EMAC_SGMII_PHY_CMN_PWR_CTRL,
			BIAS_EN | SYSCLK_EN | CLKBUF_L_EN | PLL_TXCLK_EN
			| PLL_RXCLK_EN},
		{EMAC_SGMII_PHY_TX_PWR_CTRL, L0_TX_EN | L0_CLKBUF_EN
			| L0_TRAN_BIAS_EN},
		{EMAC_SGMII_PHY_RX_PWR_CTRL,
			L0_RX_SIGDET_EN | L0_RX_TERM_MODE(1) | L0_RX_I_EN},
		{EMAC_SGMII_PHY_CMN_PWR_CTRL,
			BIAS_EN | PLL_EN | SYSCLK_EN | CLKBUF_L_EN
			| PLL_TXCLK_EN | PLL_RXCLK_EN},
		{EMAC_SGMII_PHY_LANE_CTRL1,
			L0_RX_EQUALIZE_ENABLE | L0_RESET_TSYNC_EN
			| L0_DRV_LVL(15)},
		{END_MARKER,			END_MARKER},
	},
	/* EMAC_PHY_MAP_MDM9607 */
	{
		{EMAC_SGMII_PHY_CDR_CTRL0, CDR_MAX_CNT(15)},
		{EMAC_SGMII_PHY_POW_DWN_CTRL0, PWRDN_B},
		{EMAC_SGMII_PHY_CMN_PWR_CTRL,
			BIAS_EN | SYSCLK_EN | CLKBUF_L_EN | PLL_TXCLK_EN
			| PLL_RXCLK_EN},
		{EMAC_SGMII_PHY_TX_PWR_CTRL, L0_TX_EN | L0_CLKBUF_EN
			| L0_TRAN_BIAS_EN},
		{EMAC_SGMII_PHY_RX_PWR_CTRL,
			L0_RX_SIGDET_EN | L0_RX_TERM_MODE(1) | L0_RX_I_EN},
		{EMAC_SGMII_PHY_CMN_PWR_CTRL,
			BIAS_EN | PLL_EN | SYSCLK_EN | CLKBUF_L_EN
			| PLL_TXCLK_EN | PLL_RXCLK_EN},
		{EMAC_QSERDES_COM_PLL_VCOTAIL_EN,	PLL_VCO_TAIL_MUX |
				PLL_VCO_TAIL(124) | PLL_EN_VCOTAIL_EN},
		{EMAC_QSERDES_COM_PLL_CNTRL, OCP_EN | PLL_DIV_FFEN
			| PLL_DIV_ORD},
		{EMAC_SGMII_PHY_LANE_CTRL1,
			L0_RX_EQUALIZE_ENABLE | L0_RESET_TSYNC_EN
			| L0_DRV_LVL(15)},
		{END_MARKER,			END_MARKER}
	},
	/* EMAC_PHY_MAP_V2 */
	{
		{EMAC_SGMII_PHY_POW_DWN_CTRL0, PWRDN_B},
		{EMAC_SGMII_PHY_CDR_CTRL0, CDR_MAX_CNT(15)},
		{EMAC_SGMII_PHY_TX_PWR_CTRL, 0},
		{EMAC_SGMII_PHY_LANE_CTRL1, L0_RX_EQUALIZE_ENABLE},
		{END_MARKER,			END_MARKER}
	}
};

static const struct emac_reg_write sysclk_refclk_setting[] = {
	{EMAC_QSERDES_COM_SYSCLK_EN_SEL,	SYSCLK_SEL_CMOS},
	{EMAC_QSERDES_COM_SYS_CLK_CTRL,		SYSCLK_CM | SYSCLK_AC_COUPLE},
	{END_MARKER,				END_MARKER},
};

static const struct emac_reg_write pll_setting[][PLL_MAX_REG_CNT] = {
	/* EMAC_PHY_MAP_DEFAULT */
	{
		{EMAC_QSERDES_COM_PLL_IP_SETI, PLL_IPSETI(1)},
		{EMAC_QSERDES_COM_PLL_CP_SETI, PLL_CPSETI(59)},
		{EMAC_QSERDES_COM_PLL_IP_SETP, PLL_IPSETP(10)},
		{EMAC_QSERDES_COM_PLL_CP_SETP, PLL_CPSETP(9)},
		{EMAC_QSERDES_COM_PLL_CRCTRL, PLL_RCTRL(15) | PLL_CCTRL(11)},
		{EMAC_QSERDES_COM_PLL_CNTRL, OCP_EN | PLL_DIV_FFEN
			| PLL_DIV_ORD},
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
		{END_MARKER,				END_MARKER}
	},
	/* EMAC_PHY_MAP_MDM9607 */
	{
		{EMAC_QSERDES_COM_PLL_IP_SETI, PLL_IPSETI(3)},
		{EMAC_QSERDES_COM_PLL_CP_SETI, PLL_CPSETI(59)},
		{EMAC_QSERDES_COM_PLL_IP_SETP, PLL_IPSETP(10)},
		{EMAC_QSERDES_COM_PLL_CP_SETP, PLL_CPSETP(9)},
		{EMAC_QSERDES_COM_PLL_CRCTRL, PLL_RCTRL(15) | PLL_CCTRL(11)},
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
		{EMAC_QSERDES_COM_RES_TRIM_SEARCH,	RESTRIM_SEARCH(0)},
		{EMAC_QSERDES_COM_BGTC,			BGTC(7)},
		{END_MARKER,				END_MARKER},
	}
};

static const struct emac_reg_write cdr_setting[] = {
	{EMAC_QSERDES_RX_CDR_CONTROL,
		SECONDORDERENABLE | FIRSTORDER_THRESH(3) | SECONDORDERGAIN(2)},
	{EMAC_QSERDES_RX_CDR_CONTROL2,
		SECONDORDERENABLE | FIRSTORDER_THRESH(3) | SECONDORDERGAIN(4)},
	{END_MARKER,				END_MARKER},
};

static const struct emac_reg_write tx_rx_setting[] = {
	{EMAC_QSERDES_TX_BIST_MODE_LANENO, 0},
	{EMAC_QSERDES_TX_TX_DRV_LVL, TX_DRV_LVL_MUX | TX_DRV_LVL(15)},
	{EMAC_QSERDES_TX_TRAN_DRVR_EMP_EN, EMP_EN_MUX | EMP_EN},
	{EMAC_QSERDES_TX_TX_EMP_POST1_LVL,
		TX_EMP_POST1_LVL_MUX | TX_EMP_POST1_LVL(1)},
	{EMAC_QSERDES_RX_RX_EQ_GAIN12, RX_EQ_GAIN2(15) | RX_EQ_GAIN1(15)},
	{EMAC_QSERDES_TX_LANE_MODE, LANE_MODE(8)},
	{END_MARKER,				END_MARKER}
};

static const struct emac_reg_write sgmii_v2_laned[] = {
	/* CDR Settings */
	{EMAC_SGMII_LN_UCDR_FO_GAIN_MODE0,
		UCDR_STEP_BY_TWO_MODE0 | UCDR_XO_GAIN_MODE(10)},
	{EMAC_SGMII_LN_UCDR_SO_GAIN_MODE0, UCDR_XO_GAIN_MODE(0)},
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
	{END_MARKER,				END_MARKER}
};

void emac_sgmii_reset_prepare(struct emac_adapter *adpt)
{
	struct emac_sgmii *sgmii = adpt->phy.private;
	u32 val;

	/* Reset PHY */
	val = readl_relaxed(sgmii->base + EMAC_EMAC_WRAPPER_CSR2);
	writel_relaxed(((val & ~PHY_RESET) | PHY_RESET),
		       sgmii->base + EMAC_EMAC_WRAPPER_CSR2);
	/* Ensure phy-reset command is written to HW before the release cmd */
	wmb();
	msleep(50);
	val = readl_relaxed(sgmii->base + EMAC_EMAC_WRAPPER_CSR2);
	writel_relaxed((val & ~PHY_RESET),
		       sgmii->base + EMAC_EMAC_WRAPPER_CSR2);
	/* Ensure phy-reset release command is written to HW before initializing
	 * SGMII
	 */
	wmb();
	msleep(50);
}

static void emac_sgmii_reset(struct emac_adapter *adpt)
{
	struct emac_sgmii *sgmii = adpt->phy.private;
	int ret;

	emac_clk_set_rate(adpt, EMAC_CLK_HIGH_SPEED, EMC_CLK_RATE_19_2MHZ);
	emac_sgmii_reset_prepare(adpt);

	ret = sgmii->initialize(adpt);
	if (ret)
		emac_err(adpt,
			 "could not reinitialize internal PHY (error=%i)\n",
			 ret);

	emac_clk_set_rate(adpt, EMAC_CLK_HIGH_SPEED, EMC_CLK_RATE_125MHZ);
}

/* LINK */
int emac_sgmii_link_init(struct emac_adapter *adpt)
{
	struct phy_device *phydev = adpt->phydev;
	struct emac_sgmii *sgmii = adpt->phy.private;
	u32 val;
	int autoneg, speed, duplex;

	autoneg = (adpt->phydev) ? phydev->autoneg : AUTONEG_ENABLE;
	speed = (adpt->phydev) ? phydev->speed : SPEED_UNKNOWN;
	duplex = (adpt->phydev) ? phydev->duplex : DUPLEX_UNKNOWN;

	val = readl_relaxed(sgmii->base + EMAC_SGMII_PHY_AUTONEG_CFG2);

	if (autoneg == AUTONEG_ENABLE) {
		val &= ~(FORCE_AN_RX_CFG | FORCE_AN_TX_CFG);
		val |= AN_ENABLE;
		writel_relaxed(val,
			       sgmii->base + EMAC_SGMII_PHY_AUTONEG_CFG2);
	} else {
		u32 speed_cfg = 0;

		switch (speed) {
		case SPEED_10:
			speed_cfg = SPDMODE_10;
			break;
		case SPEED_100:
			speed_cfg = SPDMODE_100;
			break;
		case SPEED_1000:
			speed_cfg = SPDMODE_1000;
			break;
		default:
			return -EINVAL;
		}

		if (duplex == DUPLEX_FULL)
			speed_cfg |= DUPLEX_MODE;

		val &= ~AN_ENABLE;
		writel_relaxed(speed_cfg,
			       sgmii->base + EMAC_SGMII_PHY_SPEED_CFG1);
		writel_relaxed(val, sgmii->base + EMAC_SGMII_PHY_AUTONEG_CFG2);
	}
	/* Ensure Auto-Neg setting are written to HW before leaving */
	wmb();

	return 0;
}

int emac_sgmii_irq_clear(struct emac_adapter *adpt, u32 irq_bits)
{
	struct emac_sgmii *sgmii = adpt->phy.private;
	u32 status;

	writel_relaxed(irq_bits, sgmii->base + EMAC_SGMII_PHY_INTERRUPT_CLEAR);
	writel_relaxed(IRQ_GLOBAL_CLEAR, sgmii->base + EMAC_SGMII_PHY_IRQ_CMD);
	/* Ensure interrupt clear command is written to HW */
	wmb();

	/* After set the IRQ_GLOBAL_CLEAR bit, the status clearing must
	 * be confirmed before clearing the bits in other registers.
	 * It takes a few cycles for hw to clear the interrupt status.
	 */
	if (readl_poll_timeout_atomic(sgmii->base +
				      EMAC_SGMII_PHY_INTERRUPT_STATUS,
				      status, !(status & irq_bits), 1,
				      SGMII_PHY_IRQ_CLR_WAIT_TIME)) {
		emac_err(adpt,
			 "error: failed clear SGMII irq: status:0x%x bits:0x%x\n",
			 status, irq_bits);
		return -EIO;
	}

	/* Finalize clearing procedure */
	writel_relaxed(0, sgmii->base + EMAC_SGMII_PHY_IRQ_CMD);
	writel_relaxed(0, sgmii->base + EMAC_SGMII_PHY_INTERRUPT_CLEAR);
	/* Ensure that clearing procedure finalization is written to HW */
	wmb();

	return 0;
}

int emac_sgmii_init_ephy_nop(struct emac_adapter *adpt)
{
	return 0;
}

int emac_sgmii_autoneg_check(struct emac_adapter *adpt,
			     struct phy_device *phydev)
{
	struct emac_sgmii *sgmii = adpt->phy.private;
	u32 autoneg0, autoneg1, status;

	autoneg0 = readl_relaxed(sgmii->base + EMAC_SGMII_PHY_AUTONEG0_STATUS);
	autoneg1 = readl_relaxed(sgmii->base + EMAC_SGMII_PHY_AUTONEG1_STATUS);
	status   = ((autoneg1 & 0xff) << 8) | (autoneg0 & 0xff);

	if (!(status & TXCFG_LINK)) {
		phydev->link = false;
		phydev->speed = SPEED_UNKNOWN;
		phydev->duplex = DUPLEX_UNKNOWN;
		return 0;
	}

	phydev->link = true;

	switch (status & TXCFG_MODE_BMSK) {
	case TXCFG_1000_FULL:
		phydev->speed = SPEED_1000;
		phydev->duplex = DUPLEX_FULL;
		break;
	case TXCFG_100_FULL:
		phydev->speed = SPEED_100;
		phydev->duplex = DUPLEX_FULL;
		break;
	case TXCFG_100_HALF:
		phydev->speed = SPEED_100;
		phydev->duplex = DUPLEX_HALF;
		break;
	case TXCFG_10_FULL:
		phydev->speed = SPEED_10;
		phydev->duplex = DUPLEX_FULL;
		break;
	case TXCFG_10_HALF:
		phydev->speed = SPEED_10;
		phydev->duplex = DUPLEX_HALF;
		break;
	default:
		phydev->speed = SPEED_UNKNOWN;
		phydev->duplex = DUPLEX_UNKNOWN;
		break;
	}
	return 0;
}

int emac_sgmii_link_check_no_ephy(struct emac_adapter *adpt,
				  struct phy_device *phydev)
{
	struct emac_sgmii *sgmii = adpt->phy.private;
	u32 val;

	val = readl_relaxed(sgmii->base + EMAC_SGMII_PHY_AUTONEG_CFG2);
	if (val & AN_ENABLE)
		return emac_sgmii_autoneg_check(adpt, phydev);

	val = readl_relaxed(sgmii->base + EMAC_SGMII_PHY_SPEED_CFG1);
	val &= DUPLEX_MODE | SPDMODE_BMSK;
	switch (val) {
	case DUPLEX_MODE | SPDMODE_1000:
		phydev->speed = SPEED_1000;
		phydev->duplex = DUPLEX_FULL;
		break;
	case DUPLEX_MODE | SPDMODE_100:
		phydev->speed = SPEED_100;
		phydev->duplex = DUPLEX_FULL;
		break;
	case SPDMODE_100:
		phydev->speed = SPEED_100;
		phydev->duplex = DUPLEX_HALF;
		break;
	case DUPLEX_MODE | SPDMODE_10:
		phydev->speed = SPEED_10;
		phydev->duplex = DUPLEX_FULL;
		break;
	case SPDMODE_10:
		phydev->speed = SPEED_10;
		phydev->duplex = DUPLEX_HALF;
		break;
	default:
		phydev->speed = SPEED_UNKNOWN;
		phydev->duplex = DUPLEX_UNKNOWN;
		break;
	}
	phydev->link = true;
	return 0;
}

irqreturn_t emac_sgmii_isr(int _irq, void *data)
{
	struct emac_adapter *adpt = data;
	struct emac_sgmii *sgmii = adpt->phy.private;
	u32 status;

	emac_dbg(adpt, intr, adpt->netdev, "receive sgmii interrupt\n");

	do {
		status = readl_relaxed(sgmii->base +
				       EMAC_SGMII_PHY_INTERRUPT_STATUS) &
				       SGMII_ISR_MASK;
		if (!status)
			break;

		if (status & SGMII_PHY_INTERRUPT_ERR) {
			SET_FLAG(adpt, ADPT_TASK_CHK_SGMII_REQ);
			if (!TEST_FLAG(adpt, ADPT_STATE_DOWN))
				emac_task_schedule(adpt);
		}

		if (status & SGMII_ISR_AN_MASK)
			emac_check_lsc(adpt);

		if (emac_sgmii_irq_clear(adpt, status) != 0) {
			/* reset */
			SET_FLAG(adpt, ADPT_TASK_REINIT_REQ);
			emac_task_schedule(adpt);
			break;
		}
	} while (1);

	return IRQ_HANDLED;
}

int emac_sgmii_up(struct emac_adapter *adpt)
{
	struct emac_sgmii *sgmii = adpt->phy.private;
	int ret;

	ret = request_irq(sgmii->irq, emac_sgmii_isr, IRQF_TRIGGER_RISING,
			  "sgmii_irq", adpt);
	if (ret)
		emac_err(adpt,
			 "error:%d on request_irq(%d:sgmii_irq)\n", ret,
			 sgmii->irq);

	/* enable sgmii irq */
	writel_relaxed(SGMII_ISR_MASK,
		       sgmii->base + EMAC_SGMII_PHY_INTERRUPT_MASK);

	return ret;
}

void emac_sgmii_down(struct emac_adapter *adpt)
{
	struct emac_sgmii *sgmii = adpt->phy.private;

	writel_relaxed(0, sgmii->base + EMAC_SGMII_PHY_INTERRUPT_MASK);
	synchronize_irq(sgmii->irq);
	free_irq(sgmii->irq, adpt);
}

int emac_sgmii_link_setup_no_ephy(struct emac_adapter *adpt)
{
	struct emac_sgmii *sgmii = adpt->phy.private;

	/* The AN_ENABLE and SPEED_CFG can't change on fly. The SGMII_PHY has
	 * to be re-initialized.
	 */
	emac_sgmii_reset_prepare(adpt);
	return sgmii->initialize(adpt);
}

void emac_sgmii_tx_clk_set_rate_nop(struct emac_adapter *adpt)
{
}

/* Check SGMII for error */
void emac_sgmii_periodic_check(struct emac_adapter *adpt)
{
	struct emac_sgmii *sgmii = adpt->phy.private;

	if (!TEST_FLAG(adpt, ADPT_TASK_CHK_SGMII_REQ))
		return;
	CLR_FLAG(adpt, ADPT_TASK_CHK_SGMII_REQ);

	/* ensure that no reset is in progress while link task is running */
	while (TEST_N_SET_FLAG(adpt, ADPT_STATE_RESETTING))
		msleep(20); /* Reset might take few 10s of ms */

	if (TEST_FLAG(adpt, ADPT_STATE_DOWN))
		goto sgmii_task_done;

	if (readl_relaxed(sgmii->base + EMAC_SGMII_PHY_RX_CHK_STATUS) & 0x40)
		goto sgmii_task_done;

	emac_err(adpt, "SGMII CDR not locked\n");

sgmii_task_done:
	CLR_FLAG(adpt, ADPT_STATE_RESETTING);
}

static int emac_sgmii_init_v1_0(struct emac_adapter *adpt)
{
	struct emac_phy *phy = &adpt->phy;
	struct emac_sgmii *sgmii = phy->private;
	unsigned int i;
	int ret;

	ret = emac_sgmii_link_init(adpt);
	if (ret)
		return ret;

	emac_reg_write_all(sgmii->base,
			   (const struct emac_reg_write *)
			   &physical_coding_sublayer_programming[EMAC_PHY_MAP_DEFAULT]);

	/* Ensure Rx/Tx lanes power configuration is written to hw before
	 * configuring the SerDes engine's clocks
	 */
	wmb();

	emac_reg_write_all(sgmii->base, sysclk_refclk_setting);
	emac_reg_write_all(sgmii->base,
			   (const struct emac_reg_write *)
			   &pll_setting[EMAC_PHY_MAP_DEFAULT]);
	emac_reg_write_all(sgmii->base, cdr_setting);
	emac_reg_write_all(sgmii->base, tx_rx_setting);

	/* Ensure SerDes engine configuration is written to hw before powering
	 * it up
	 */
	wmb();

	writel_relaxed(SERDES_START, sgmii->base + EMAC_SGMII_PHY_SERDES_START);

	/* Ensure Rx/Tx SerDes engine power-up command is written to HW */
	wmb();

	for (i = 0; i < SERDES_START_WAIT_TIMES; i++) {
		if (readl_relaxed(sgmii->base + EMAC_QSERDES_COM_RESET_SM)
				 & READY)
			break;
		usleep_range(100, 200);
	}

	if (i == SERDES_START_WAIT_TIMES) {
		emac_err(adpt, "serdes failed to start\n");
		return -EIO;
	}
	/* Mask out all the SGMII Interrupt */
	writel_relaxed(0, sgmii->base + EMAC_SGMII_PHY_INTERRUPT_MASK);
	/* Ensure SGMII interrupts are masked out before clearing them */
	wmb();

	emac_sgmii_irq_clear(adpt, SGMII_PHY_INTERRUPT_ERR);

	return 0;
}

static int emac_sgmii_init_v1_1(struct emac_adapter *adpt)
{
	struct emac_phy *phy = &adpt->phy;
	struct emac_sgmii *sgmii = phy->private;
	unsigned int i;
	int ret;

	ret = emac_sgmii_link_init(adpt);
	if (ret)
		return ret;

	emac_reg_write_all(sgmii->base,
			   (const struct emac_reg_write *)
			   &physical_coding_sublayer_programming[EMAC_PHY_MAP_MDM9607]);

	/* Ensure Rx/Tx lanes power configuration is written to hw before
	 * configuring the SerDes engine's clocks
	 */
	wmb();

	emac_reg_write_all(sgmii->base, sysclk_refclk_setting);
	emac_reg_write_all(sgmii->base,
			   (const struct emac_reg_write *)
			   &pll_setting[EMAC_PHY_MAP_MDM9607]);
	emac_reg_write_all(sgmii->base, cdr_setting);
	emac_reg_write_all(sgmii->base, tx_rx_setting);

	/* Ensure SerDes engine configuration is written to hw before powering
	 * it up
	 */
	wmb();

	/* Power up the Ser/Des engine */
	writel_relaxed(SERDES_START, sgmii->base + EMAC_SGMII_PHY_SERDES_START);

	/* Ensure Rx/Tx SerDes engine power-up command is written to HW */
	wmb();

	for (i = 0; i < SERDES_START_WAIT_TIMES; i++) {
		if (readl_relaxed(sgmii->base + EMAC_QSERDES_COM_RESET_SM)
				 & READY)
			break;
		usleep_range(100, 200);
	}

	if (i == SERDES_START_WAIT_TIMES) {
		emac_err(adpt, "serdes failed to start\n");
		return -EIO;
	}
	/* Mask out all the SGMII Interrupt */
	writel_relaxed(0, sgmii->base + EMAC_SGMII_PHY_INTERRUPT_MASK);
	/* Ensure SGMII interrupts are masked out before clearing them */
	wmb();

	emac_sgmii_irq_clear(adpt, SGMII_PHY_INTERRUPT_ERR);

	return 0;
}

static int emac_sgmii_init_v2(struct emac_adapter *adpt)
{
	struct emac_phy *phy = &adpt->phy;
	struct emac_sgmii *sgmii = phy->private;
	void __iomem *phy_regs = sgmii->base;
	void __iomem *laned = sgmii->digital;
	unsigned int i;
	u32 lnstatus;
	int ret;

	ret = emac_sgmii_link_init(adpt);
	if (ret)
		return ret;

	/* PCS lane-x init */
	emac_reg_write_all(sgmii->base,
			   (const struct emac_reg_write *)
			   &physical_coding_sublayer_programming[EMAC_PHY_MAP_V2]);

	/* Ensure Rx/Tx lanes power configuration is written to hw before
	 * configuring the SerDes engine's clocks
	 */
	wmb();

	/* SGMII lane-x init */
	emac_reg_write_all(sgmii->digital, sgmii_v2_laned);

	/* Power up PCS and start reset lane state machine */
	writel_relaxed(0, phy_regs + EMAC_SGMII_PHY_RESET_CTRL);
	writel_relaxed(1, laned + SGMII_LN_RSM_START);
	wmb(); /* ensure power up is written before checking lane status */

	/* Wait for c_ready assertion */
	for (i = 0; i < SERDES_START_WAIT_TIMES; i++) {
		lnstatus = readl_relaxed(phy_regs + SGMII_PHY_LN_LANE_STATUS);
		rmb(); /* ensure status read is complete before testing it */
		if (lnstatus & BIT(1))
			break;
		usleep_range(100, 200);
	}

	if (i == SERDES_START_WAIT_TIMES) {
		emac_err(adpt, "SGMII failed to start\n");
		return -EIO;
	}

	/* Disable digital and SERDES loopback */
	writel_relaxed(0, phy_regs + SGMII_PHY_LN_BIST_GEN0);
	writel_relaxed(0, phy_regs + SGMII_PHY_LN_BIST_GEN2);
	writel_relaxed(0, phy_regs + SGMII_PHY_LN_CDR_CTRL1);

	/* Mask out all the SGMII Interrupt */
	writel_relaxed(0, phy_regs + EMAC_SGMII_PHY_INTERRUPT_MASK);
	wmb(); /* ensure writes are flushed to hw */

	emac_sgmii_irq_clear(adpt, SGMII_PHY_INTERRUPT_ERR);

	return 0;
}

static int emac_sgmii_acpi_match(struct device *dev, void *data)
{
	static const struct acpi_device_id match_table[] = {
		{
			.id = "QCOM8071",
			.driver_data = (kernel_ulong_t)emac_sgmii_init_v2,
		},
		{}
	};
	const struct acpi_device_id *id = acpi_match_device(match_table, dev);
	emac_sgmii_initialize *initialize = data;

	if (id)
		*initialize = (emac_sgmii_initialize)id->driver_data;

	return !!id;
}

static const struct of_device_id emac_sgmii_dt_match[] = {
	{
		.compatible = "qcom,fsm9900-emac-sgmii",
		.data = emac_sgmii_init_v1_0,
	},
	{
		.compatible = "qcom,qdf2432-emac-sgmii",
		.data = emac_sgmii_init_v2,
	},
	{
		.compatible = "qcom,mdm9607-emac-sgmii",
		.data = emac_sgmii_init_v1_1,
	},
	{}
};

int emac_sgmii_config(struct platform_device *pdev, struct emac_adapter *adpt)
{
	struct platform_device *sgmii_pdev = NULL;
	struct emac_sgmii *sgmii;
	struct resource *res;
	int ret = 0;

	sgmii = devm_kzalloc(&pdev->dev, sizeof(*sgmii), GFP_KERNEL);
	if (!sgmii)
		return -ENOMEM;

	if (ACPI_COMPANION(&pdev->dev)) {
		struct device *dev;

		dev = device_find_child(&pdev->dev, &sgmii->initialize,
					emac_sgmii_acpi_match);

		if (!dev) {
			emac_err(adpt, "cannot find internal phy node\n");
			return -ENODEV;
		}

		sgmii_pdev = to_platform_device(dev);
	} else {
		const struct of_device_id *match;
		struct device_node *np;

		np = of_parse_phandle(pdev->dev.of_node, "internal-phy", 0);
		if (!np) {
			emac_err(adpt, "missing internal-phy property\n");
			return -ENODEV;
		}

		sgmii_pdev = of_find_device_by_node(np);
		if (!sgmii_pdev) {
			emac_err(adpt, "invalid internal-phy property\n");
			return -ENODEV;
		}

		match = of_match_device(emac_sgmii_dt_match, &sgmii_pdev->dev);
		if (!match) {
			emac_err(adpt, "unrecognized internal phy node\n");
			ret = -ENODEV;
			goto error_put_device;
		}

		sgmii->initialize = (emac_sgmii_initialize)match->data;
	}

	/* Base address is the first address */
	res = platform_get_resource_byname(sgmii_pdev, IORESOURCE_MEM,
					   "emac_sgmii");
	if (!res) {
		emac_err(adpt,
			 "error platform_get_resource_byname(emac_sgmii)\n");
		ret = -EINVAL;
		goto error_put_device;
	}

	sgmii->base = ioremap(res->start, resource_size(res));
	if (IS_ERR(sgmii->base)) {
		emac_err(adpt,
			 "error:%ld remap (start:0x%lx size:0x%lx)\n",
			 PTR_ERR(sgmii->base), (ulong)res->start,
			 (ulong)resource_size(res));
		ret = -ENOMEM;
		goto error_put_device;
	}

	/* v2 SGMII has a per-lane digital, so parse it if it exists */
	res = platform_get_resource_byname(sgmii_pdev, IORESOURCE_MEM,
					   "emac_digital");
	if (res) {
		sgmii->digital = devm_ioremap_resource(&sgmii_pdev->dev, res);
		if (!sgmii->digital) {
			ret = -ENOMEM;
			goto error_unmap_base;
		}
	}

	ret = platform_get_irq_byname(sgmii_pdev, "emac_sgmii_irq");
	if (ret < 0)
		goto error;

	sgmii->irq = ret;
	adpt->phy.private = sgmii;

	ret = sgmii->initialize(adpt);
	if (ret)
		goto error;

	/* We've remapped the addresses, so we don't need the device any
	 * more.  of_find_device_by_node() says we should release it.
	 */
	put_device(&sgmii_pdev->dev);

	return 0;

error:
	if (sgmii->digital)
		iounmap(sgmii->digital);
error_unmap_base:
	iounmap(sgmii->base);
error_put_device:
	put_device(&sgmii_pdev->dev);

	return ret;
}

struct emac_phy_ops emac_sgmii_ops = {
	.config			= emac_sgmii_config,
	.up			= emac_sgmii_up,
	.down			= emac_sgmii_down,
	.reset			= emac_sgmii_reset,
	.link_setup_no_ephy	= emac_sgmii_link_setup_no_ephy,
	.link_check_no_ephy	= emac_sgmii_link_check_no_ephy,
	.tx_clk_set_rate	= emac_sgmii_tx_clk_set_rate_nop,
	.periodic_task		= emac_sgmii_periodic_check,
};
