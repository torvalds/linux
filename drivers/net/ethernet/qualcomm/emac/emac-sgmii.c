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

/* Qualcomm Technologies, Inc. EMAC SGMII Controller driver.
 */

#include <linux/iopoll.h>
#include <linux/of_device.h>
#include "emac.h"
#include "emac-mac.h"
#include "emac-sgmii.h"

/* EMAC_QSERDES register offsets */
#define EMAC_QSERDES_COM_SYS_CLK_CTRL		0x000000
#define EMAC_QSERDES_COM_PLL_CNTRL		0x000014
#define EMAC_QSERDES_COM_PLL_IP_SETI		0x000018
#define EMAC_QSERDES_COM_PLL_CP_SETI		0x000024
#define EMAC_QSERDES_COM_PLL_IP_SETP		0x000028
#define EMAC_QSERDES_COM_PLL_CP_SETP		0x00002c
#define EMAC_QSERDES_COM_SYSCLK_EN_SEL		0x000038
#define EMAC_QSERDES_COM_RESETSM_CNTRL		0x000040
#define EMAC_QSERDES_COM_PLLLOCK_CMP1		0x000044
#define EMAC_QSERDES_COM_PLLLOCK_CMP2		0x000048
#define EMAC_QSERDES_COM_PLLLOCK_CMP3		0x00004c
#define EMAC_QSERDES_COM_PLLLOCK_CMP_EN		0x000050
#define EMAC_QSERDES_COM_DEC_START1		0x000064
#define EMAC_QSERDES_COM_DIV_FRAC_START1	0x000098
#define EMAC_QSERDES_COM_DIV_FRAC_START2	0x00009c
#define EMAC_QSERDES_COM_DIV_FRAC_START3	0x0000a0
#define EMAC_QSERDES_COM_DEC_START2		0x0000a4
#define EMAC_QSERDES_COM_PLL_CRCTRL		0x0000ac
#define EMAC_QSERDES_COM_RESET_SM		0x0000bc
#define EMAC_QSERDES_TX_BIST_MODE_LANENO	0x000100
#define EMAC_QSERDES_TX_TX_EMP_POST1_LVL	0x000108
#define EMAC_QSERDES_TX_TX_DRV_LVL		0x00010c
#define EMAC_QSERDES_TX_LANE_MODE		0x000150
#define EMAC_QSERDES_TX_TRAN_DRVR_EMP_EN	0x000170
#define EMAC_QSERDES_RX_CDR_CONTROL		0x000200
#define EMAC_QSERDES_RX_CDR_CONTROL2		0x000210
#define EMAC_QSERDES_RX_RX_EQ_GAIN12		0x000230

/* EMAC_SGMII register offsets */
#define EMAC_SGMII_PHY_SERDES_START		0x000000
#define EMAC_SGMII_PHY_CMN_PWR_CTRL		0x000004
#define EMAC_SGMII_PHY_RX_PWR_CTRL		0x000008
#define EMAC_SGMII_PHY_TX_PWR_CTRL		0x00000C
#define EMAC_SGMII_PHY_LANE_CTRL1		0x000018
#define EMAC_SGMII_PHY_AUTONEG_CFG2		0x000048
#define EMAC_SGMII_PHY_CDR_CTRL0		0x000058
#define EMAC_SGMII_PHY_SPEED_CFG1		0x000074
#define EMAC_SGMII_PHY_POW_DWN_CTRL0		0x000080
#define EMAC_SGMII_PHY_RESET_CTRL		0x0000a8
#define EMAC_SGMII_PHY_IRQ_CMD			0x0000ac
#define EMAC_SGMII_PHY_INTERRUPT_CLEAR		0x0000b0
#define EMAC_SGMII_PHY_INTERRUPT_MASK		0x0000b4
#define EMAC_SGMII_PHY_INTERRUPT_STATUS		0x0000b8
#define EMAC_SGMII_PHY_RX_CHK_STATUS		0x0000d4
#define EMAC_SGMII_PHY_AUTONEG0_STATUS		0x0000e0
#define EMAC_SGMII_PHY_AUTONEG1_STATUS		0x0000e4

/* EMAC_QSERDES_COM_PLL_IP_SETI */
#define PLL_IPSETI(x)				((x) & 0x3f)

/* EMAC_QSERDES_COM_PLL_CP_SETI */
#define PLL_CPSETI(x)				((x) & 0xff)

/* EMAC_QSERDES_COM_PLL_IP_SETP */
#define PLL_IPSETP(x)				((x) & 0x3f)

/* EMAC_QSERDES_COM_PLL_CP_SETP */
#define PLL_CPSETP(x)				((x) & 0x1f)

/* EMAC_QSERDES_COM_PLL_CRCTRL */
#define PLL_RCTRL(x)				(((x) & 0xf) << 4)
#define PLL_CCTRL(x)				((x) & 0xf)

/* SGMII v2 PHY registers per lane */
#define EMAC_SGMII_PHY_LN_OFFSET		0x0400

/* SGMII v2 digital lane registers */
#define EMAC_SGMII_LN_DRVR_CTRL0		0x00C
#define EMAC_SGMII_LN_DRVR_TAP_EN		0x018
#define EMAC_SGMII_LN_TX_MARGINING		0x01C
#define EMAC_SGMII_LN_TX_PRE			0x020
#define EMAC_SGMII_LN_TX_POST			0x024
#define EMAC_SGMII_LN_TX_BAND_MODE		0x060
#define EMAC_SGMII_LN_LANE_MODE			0x064
#define EMAC_SGMII_LN_PARALLEL_RATE		0x078
#define EMAC_SGMII_LN_CML_CTRL_MODE0		0x0B8
#define EMAC_SGMII_LN_MIXER_CTRL_MODE0		0x0D0
#define EMAC_SGMII_LN_VGA_INITVAL		0x134
#define EMAC_SGMII_LN_UCDR_FO_GAIN_MODE0	0x17C
#define EMAC_SGMII_LN_UCDR_SO_GAIN_MODE0	0x188
#define EMAC_SGMII_LN_UCDR_SO_CONFIG		0x194
#define EMAC_SGMII_LN_RX_BAND			0x19C
#define EMAC_SGMII_LN_RX_RCVR_PATH1_MODE0	0x1B8
#define EMAC_SGMII_LN_RSM_CONFIG		0x1F0
#define EMAC_SGMII_LN_SIGDET_ENABLES		0x224
#define EMAC_SGMII_LN_SIGDET_CNTRL		0x228
#define EMAC_SGMII_LN_SIGDET_DEGLITCH_CNTRL	0x22C
#define EMAC_SGMII_LN_RX_EN_SIGNAL		0x2A0
#define EMAC_SGMII_LN_RX_MISC_CNTRL0		0x2AC
#define EMAC_SGMII_LN_DRVR_LOGIC_CLKDIV		0x2BC

/* SGMII v2 digital lane register values */
#define UCDR_STEP_BY_TWO_MODE0			BIT(7)
#define UCDR_xO_GAIN_MODE(x)			((x) & 0x7f)
#define UCDR_ENABLE				BIT(6)
#define UCDR_SO_SATURATION(x)			((x) & 0x3f)
#define SIGDET_LP_BYP_PS4			BIT(7)
#define SIGDET_EN_PS0_TO_PS2			BIT(6)
#define EN_ACCOUPLEVCM_SW_MUX			BIT(5)
#define EN_ACCOUPLEVCM_SW			BIT(4)
#define RX_SYNC_EN				BIT(3)
#define RXTERM_HIGHZ_PS5			BIT(2)
#define SIGDET_EN_PS3				BIT(1)
#define EN_ACCOUPLE_VCM_PS3			BIT(0)
#define UFS_MODE				BIT(5)
#define TXVAL_VALID_INIT			BIT(4)
#define TXVAL_VALID_MUX				BIT(3)
#define TXVAL_VALID				BIT(2)
#define USB3P1_MODE				BIT(1)
#define KR_PCIGEN3_MODE				BIT(0)
#define PRE_EN					BIT(3)
#define POST_EN					BIT(2)
#define MAIN_EN_MUX				BIT(1)
#define MAIN_EN					BIT(0)
#define TX_MARGINING_MUX			BIT(6)
#define TX_MARGINING(x)				((x) & 0x3f)
#define TX_PRE_MUX				BIT(6)
#define TX_PRE(x)				((x) & 0x3f)
#define TX_POST_MUX				BIT(6)
#define TX_POST(x)				((x) & 0x3f)
#define CML_GEAR_MODE(x)			(((x) & 7) << 3)
#define CML2CMOS_IBOOST_MODE(x)			((x) & 7)
#define MIXER_LOADB_MODE(x)			(((x) & 0xf) << 2)
#define MIXER_DATARATE_MODE(x)			((x) & 3)
#define VGA_THRESH_DFE(x)			((x) & 0x3f)
#define SIGDET_LP_BYP_PS0_TO_PS2		BIT(5)
#define SIGDET_LP_BYP_MUX			BIT(4)
#define SIGDET_LP_BYP				BIT(3)
#define SIGDET_EN_MUX				BIT(2)
#define SIGDET_EN				BIT(1)
#define SIGDET_FLT_BYP				BIT(0)
#define SIGDET_LVL(x)				(((x) & 0xf) << 4)
#define SIGDET_BW_CTRL(x)			((x) & 0xf)
#define SIGDET_DEGLITCH_CTRL(x)			(((x) & 0xf) << 1)
#define SIGDET_DEGLITCH_BYP			BIT(0)
#define INVERT_PCS_RX_CLK			BIT(7)
#define PWM_EN					BIT(6)
#define RXBIAS_SEL(x)				(((x) & 0x3) << 4)
#define EBDAC_SIGN				BIT(3)
#define EDAC_SIGN				BIT(2)
#define EN_AUXTAP1SIGN_INVERT			BIT(1)
#define EN_DAC_CHOPPING				BIT(0)
#define DRVR_LOGIC_CLK_EN			BIT(4)
#define DRVR_LOGIC_CLK_DIV(x)			((x) & 0xf)
#define PARALLEL_RATE_MODE2(x)			(((x) & 0x3) << 4)
#define PARALLEL_RATE_MODE1(x)			(((x) & 0x3) << 2)
#define PARALLEL_RATE_MODE0(x)			((x) & 0x3)
#define BAND_MODE2(x)				(((x) & 0x3) << 4)
#define BAND_MODE1(x)				(((x) & 0x3) << 2)
#define BAND_MODE0(x)				((x) & 0x3)
#define LANE_SYNC_MODE				BIT(5)
#define LANE_MODE(x)				((x) & 0x1f)
#define CDR_PD_SEL_MODE0(x)			(((x) & 0x3) << 5)
#define EN_DLL_MODE0				BIT(4)
#define EN_IQ_DCC_MODE0				BIT(3)
#define EN_IQCAL_MODE0				BIT(2)
#define EN_QPATH_MODE0				BIT(1)
#define EN_EPATH_MODE0				BIT(0)
#define FORCE_TSYNC_ACK				BIT(7)
#define FORCE_CMN_ACK				BIT(6)
#define FORCE_CMN_READY				BIT(5)
#define EN_RCLK_DEGLITCH			BIT(4)
#define BYPASS_RSM_CDR_RESET			BIT(3)
#define BYPASS_RSM_TSYNC			BIT(2)
#define BYPASS_RSM_SAMP_CAL			BIT(1)
#define BYPASS_RSM_DLL_CAL			BIT(0)

/* EMAC_QSERDES_COM_SYS_CLK_CTRL */
#define SYSCLK_CM				BIT(4)
#define SYSCLK_AC_COUPLE			BIT(3)

/* EMAC_QSERDES_COM_PLL_CNTRL */
#define OCP_EN					BIT(5)
#define PLL_DIV_FFEN				BIT(2)
#define PLL_DIV_ORD				BIT(1)

/* EMAC_QSERDES_COM_SYSCLK_EN_SEL */
#define SYSCLK_SEL_CMOS				BIT(3)

/* EMAC_QSERDES_COM_RESETSM_CNTRL */
#define FRQ_TUNE_MODE				BIT(4)

/* EMAC_QSERDES_COM_PLLLOCK_CMP_EN */
#define PLLLOCK_CMP_EN				BIT(0)

/* EMAC_QSERDES_COM_DEC_START1 */
#define DEC_START1_MUX				BIT(7)
#define DEC_START1(x)				((x) & 0x7f)

/* EMAC_QSERDES_COM_DIV_FRAC_START1 * EMAC_QSERDES_COM_DIV_FRAC_START2 */
#define DIV_FRAC_START_MUX			BIT(7)
#define DIV_FRAC_START(x)			((x) & 0x7f)

/* EMAC_QSERDES_COM_DIV_FRAC_START3 */
#define DIV_FRAC_START3_MUX			BIT(4)
#define DIV_FRAC_START3(x)			((x) & 0xf)

/* EMAC_QSERDES_COM_DEC_START2 */
#define DEC_START2_MUX				BIT(1)
#define DEC_START2				BIT(0)

/* EMAC_QSERDES_COM_RESET_SM */
#define READY					BIT(5)

/* EMAC_QSERDES_TX_TX_EMP_POST1_LVL */
#define TX_EMP_POST1_LVL_MUX			BIT(5)
#define TX_EMP_POST1_LVL(x)			((x) & 0x1f)
#define TX_EMP_POST1_LVL_BMSK			0x1f
#define TX_EMP_POST1_LVL_SHFT			0

/* EMAC_QSERDES_TX_TX_DRV_LVL */
#define TX_DRV_LVL_MUX				BIT(4)
#define TX_DRV_LVL(x)				((x) & 0xf)

/* EMAC_QSERDES_TX_TRAN_DRVR_EMP_EN */
#define EMP_EN_MUX				BIT(1)
#define EMP_EN					BIT(0)

/* EMAC_QSERDES_RX_CDR_CONTROL & EMAC_QSERDES_RX_CDR_CONTROL2 */
#define HBW_PD_EN				BIT(7)
#define SECONDORDERENABLE			BIT(6)
#define FIRSTORDER_THRESH(x)			(((x) & 0x7) << 3)
#define SECONDORDERGAIN(x)			((x) & 0x7)

/* EMAC_QSERDES_RX_RX_EQ_GAIN12 */
#define RX_EQ_GAIN2(x)				(((x) & 0xf) << 4)
#define RX_EQ_GAIN1(x)				((x) & 0xf)

/* EMAC_SGMII_PHY_SERDES_START */
#define SERDES_START				BIT(0)

/* EMAC_SGMII_PHY_CMN_PWR_CTRL */
#define BIAS_EN					BIT(6)
#define PLL_EN					BIT(5)
#define SYSCLK_EN				BIT(4)
#define CLKBUF_L_EN				BIT(3)
#define PLL_TXCLK_EN				BIT(1)
#define PLL_RXCLK_EN				BIT(0)

/* EMAC_SGMII_PHY_RX_PWR_CTRL */
#define L0_RX_SIGDET_EN				BIT(7)
#define L0_RX_TERM_MODE(x)			(((x) & 3) << 4)
#define L0_RX_I_EN				BIT(1)

/* EMAC_SGMII_PHY_TX_PWR_CTRL */
#define L0_TX_EN				BIT(5)
#define L0_CLKBUF_EN				BIT(4)
#define L0_TRAN_BIAS_EN				BIT(1)

/* EMAC_SGMII_PHY_LANE_CTRL1 */
#define L0_RX_EQUALIZE_ENABLE			BIT(6)
#define L0_RESET_TSYNC_EN			BIT(4)
#define L0_DRV_LVL(x)				((x) & 0xf)

/* EMAC_SGMII_PHY_AUTONEG_CFG2 */
#define FORCE_AN_TX_CFG				BIT(5)
#define FORCE_AN_RX_CFG				BIT(4)
#define AN_ENABLE				BIT(0)

/* EMAC_SGMII_PHY_SPEED_CFG1 */
#define DUPLEX_MODE				BIT(4)
#define SPDMODE_1000				BIT(1)
#define SPDMODE_100				BIT(0)
#define SPDMODE_10				0
#define SPDMODE_BMSK				3
#define SPDMODE_SHFT				0

/* EMAC_SGMII_PHY_POW_DWN_CTRL0 */
#define PWRDN_B					BIT(0)
#define CDR_MAX_CNT(x)				((x) & 0xff)

/* EMAC_QSERDES_TX_BIST_MODE_LANENO */
#define BIST_LANE_NUMBER(x)			(((x) & 3) << 5)
#define BISTMODE(x)				((x) & 0x1f)

/* EMAC_QSERDES_COM_PLLLOCK_CMPx */
#define PLLLOCK_CMP(x)				((x) & 0xff)

/* EMAC_SGMII_PHY_RESET_CTRL */
#define PHY_SW_RESET				BIT(0)

/* EMAC_SGMII_PHY_IRQ_CMD */
#define IRQ_GLOBAL_CLEAR			BIT(0)

/* EMAC_SGMII_PHY_INTERRUPT_MASK */
#define DECODE_CODE_ERR				BIT(7)
#define DECODE_DISP_ERR				BIT(6)
#define PLL_UNLOCK				BIT(5)
#define AN_ILLEGAL_TERM				BIT(4)
#define SYNC_FAIL				BIT(3)
#define AN_START				BIT(2)
#define AN_END					BIT(1)
#define AN_REQUEST				BIT(0)

#define SGMII_PHY_IRQ_CLR_WAIT_TIME		10

#define SGMII_PHY_INTERRUPT_ERR (\
	DECODE_CODE_ERR         |\
	DECODE_DISP_ERR)

#define SGMII_ISR_AN_MASK       (\
	AN_REQUEST              |\
	AN_START                |\
	AN_END                  |\
	AN_ILLEGAL_TERM         |\
	PLL_UNLOCK              |\
	SYNC_FAIL)

#define SGMII_ISR_MASK          (\
	SGMII_PHY_INTERRUPT_ERR |\
	SGMII_ISR_AN_MASK)

/* SGMII TX_CONFIG */
#define TXCFG_LINK				0x8000
#define TXCFG_MODE_BMSK				0x1c00
#define TXCFG_1000_FULL				0x1800
#define TXCFG_100_FULL				0x1400
#define TXCFG_100_HALF				0x0400
#define TXCFG_10_FULL				0x1000
#define TXCFG_10_HALF				0x0000

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

static const struct emac_reg_write physical_coding_sublayer_programming_v1[] = {
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

static const struct emac_reg_write sgmii_v2_laned[] = {
	/* CDR Settings */
	{EMAC_SGMII_LN_UCDR_FO_GAIN_MODE0,
		UCDR_STEP_BY_TWO_MODE0 | UCDR_xO_GAIN_MODE(10)},
	{EMAC_SGMII_LN_UCDR_SO_GAIN_MODE0, UCDR_xO_GAIN_MODE(6)},
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

static const struct emac_reg_write physical_coding_sublayer_programming_v2[] = {
	{EMAC_SGMII_PHY_POW_DWN_CTRL0, PWRDN_B},
	{EMAC_SGMII_PHY_CDR_CTRL0, CDR_MAX_CNT(15)},
	{EMAC_SGMII_PHY_TX_PWR_CTRL, 0},
	{EMAC_SGMII_PHY_LANE_CTRL1, L0_RX_EQUALIZE_ENABLE},
};

static int emac_sgmii_link_init(struct emac_adapter *adpt)
{
	struct phy_device *phydev = adpt->phydev;
	struct emac_phy *phy = &adpt->phy;
	u32 val;

	val = readl(phy->base + EMAC_SGMII_PHY_AUTONEG_CFG2);

	if (phydev->autoneg == AUTONEG_ENABLE) {
		val &= ~(FORCE_AN_RX_CFG | FORCE_AN_TX_CFG);
		val |= AN_ENABLE;
		writel(val, phy->base + EMAC_SGMII_PHY_AUTONEG_CFG2);
	} else {
		u32 speed_cfg;

		switch (phydev->speed) {
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

		if (phydev->duplex == DUPLEX_FULL)
			speed_cfg |= DUPLEX_MODE;

		val &= ~AN_ENABLE;
		writel(speed_cfg, phy->base + EMAC_SGMII_PHY_SPEED_CFG1);
		writel(val, phy->base + EMAC_SGMII_PHY_AUTONEG_CFG2);
	}

	return 0;
}

static int emac_sgmii_irq_clear(struct emac_adapter *adpt, u32 irq_bits)
{
	struct emac_phy *phy = &adpt->phy;
	u32 status;

	writel_relaxed(irq_bits, phy->base + EMAC_SGMII_PHY_INTERRUPT_CLEAR);
	writel_relaxed(IRQ_GLOBAL_CLEAR, phy->base + EMAC_SGMII_PHY_IRQ_CMD);
	/* Ensure interrupt clear command is written to HW */
	wmb();

	/* After set the IRQ_GLOBAL_CLEAR bit, the status clearing must
	 * be confirmed before clearing the bits in other registers.
	 * It takes a few cycles for hw to clear the interrupt status.
	 */
	if (readl_poll_timeout_atomic(phy->base +
				      EMAC_SGMII_PHY_INTERRUPT_STATUS,
				      status, !(status & irq_bits), 1,
				      SGMII_PHY_IRQ_CLR_WAIT_TIME)) {
		netdev_err(adpt->netdev,
			   "error: failed clear SGMII irq: status:0x%x bits:0x%x\n",
			   status, irq_bits);
		return -EIO;
	}

	/* Finalize clearing procedure */
	writel_relaxed(0, phy->base + EMAC_SGMII_PHY_IRQ_CMD);
	writel_relaxed(0, phy->base + EMAC_SGMII_PHY_INTERRUPT_CLEAR);

	/* Ensure that clearing procedure finalization is written to HW */
	wmb();

	return 0;
}

int emac_sgmii_init_v1(struct emac_adapter *adpt)
{
	struct emac_phy *phy = &adpt->phy;
	unsigned int i;
	int ret;

	ret = emac_sgmii_link_init(adpt);
	if (ret)
		return ret;

	emac_reg_write_all(phy->base, physical_coding_sublayer_programming_v1,
			   ARRAY_SIZE(physical_coding_sublayer_programming_v1));
	emac_reg_write_all(phy->base, sysclk_refclk_setting,
			   ARRAY_SIZE(sysclk_refclk_setting));
	emac_reg_write_all(phy->base, pll_setting, ARRAY_SIZE(pll_setting));
	emac_reg_write_all(phy->base, cdr_setting, ARRAY_SIZE(cdr_setting));
	emac_reg_write_all(phy->base, tx_rx_setting,
			   ARRAY_SIZE(tx_rx_setting));

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

	emac_sgmii_irq_clear(adpt, SGMII_PHY_INTERRUPT_ERR);

	return 0;
}

int emac_sgmii_init_v2(struct emac_adapter *adpt)
{
	struct emac_phy *phy = &adpt->phy;
	void __iomem *phy_regs = phy->base;
	void __iomem *laned = phy->digital;
	unsigned int i;
	u32 lnstatus;
	int ret;

	ret = emac_sgmii_link_init(adpt);
	if (ret)
		return ret;

	/* PCS lane-x init */
	emac_reg_write_all(phy->base, physical_coding_sublayer_programming_v2,
			   ARRAY_SIZE(physical_coding_sublayer_programming_v2));

	/* SGMII lane-x init */
	emac_reg_write_all(phy->digital,
			   sgmii_v2_laned, ARRAY_SIZE(sgmii_v2_laned));

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

	emac_sgmii_irq_clear(adpt, SGMII_PHY_INTERRUPT_ERR);

	return 0;
}

static void emac_sgmii_reset_prepare(struct emac_adapter *adpt)
{
	struct emac_phy *phy = &adpt->phy;
	u32 val;

	/* Reset PHY */
	val = readl(phy->base + EMAC_EMAC_WRAPPER_CSR2);
	writel(((val & ~PHY_RESET) | PHY_RESET), phy->base +
	       EMAC_EMAC_WRAPPER_CSR2);
	/* Ensure phy-reset command is written to HW before the release cmd */
	msleep(50);
	val = readl(phy->base + EMAC_EMAC_WRAPPER_CSR2);
	writel((val & ~PHY_RESET), phy->base + EMAC_EMAC_WRAPPER_CSR2);
	/* Ensure phy-reset release command is written to HW before initializing
	 * SGMII
	 */
	msleep(50);
}

void emac_sgmii_reset(struct emac_adapter *adpt)
{
	int ret;

	clk_set_rate(adpt->clk[EMAC_CLK_HIGH_SPEED], 19200000);
	emac_sgmii_reset_prepare(adpt);

	ret = adpt->phy.initialize(adpt);
	if (ret)
		netdev_err(adpt->netdev,
			   "could not reinitialize internal PHY (error=%i)\n",
			   ret);

	clk_set_rate(adpt->clk[EMAC_CLK_HIGH_SPEED], 125000000);
}

static const struct of_device_id emac_sgmii_dt_match[] = {
	{
		.compatible = "qcom,fsm9900-emac-sgmii",
		.data = emac_sgmii_init_v1,
	},
	{
		.compatible = "qcom,qdf2432-emac-sgmii",
		.data = emac_sgmii_init_v2,
	},
	{}
};

int emac_sgmii_config(struct platform_device *pdev, struct emac_adapter *adpt)
{
	struct platform_device *sgmii_pdev = NULL;
	struct emac_phy *phy = &adpt->phy;
	struct resource *res;
	const struct of_device_id *match;
	struct device_node *np;
	int ret;

	np = of_parse_phandle(pdev->dev.of_node, "internal-phy", 0);
	if (!np) {
		dev_err(&pdev->dev, "missing internal-phy property\n");
		return -ENODEV;
	}

	sgmii_pdev = of_find_device_by_node(np);
	if (!sgmii_pdev) {
		dev_err(&pdev->dev, "invalid internal-phy property\n");
		return -ENODEV;
	}

	match = of_match_device(emac_sgmii_dt_match, &sgmii_pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "unrecognized internal phy node\n");
		ret = -ENODEV;
		goto error_put_device;
	}

	phy->initialize = (emac_sgmii_initialize)match->data;

	/* Base address is the first address */
	res = platform_get_resource(sgmii_pdev, IORESOURCE_MEM, 0);
	phy->base = ioremap(res->start, resource_size(res));
	if (IS_ERR(phy->base)) {
		ret = PTR_ERR(phy->base);
		goto error_put_device;
	}

	/* v2 SGMII has a per-lane digital digital, so parse it if it exists */
	res = platform_get_resource(sgmii_pdev, IORESOURCE_MEM, 1);
	if (res) {
		phy->digital = ioremap(res->start, resource_size(res));
		if (IS_ERR(phy->digital)) {
			ret = PTR_ERR(phy->digital);
			goto error_unmap_base;
		}
	}

	ret = phy->initialize(adpt);
	if (ret)
		goto error;

	/* We've remapped the addresses, so we don't need the device any
	 * more.  of_find_device_by_node() says we should release it.
	 */
	put_device(&sgmii_pdev->dev);

	return 0;

error:
	if (phy->digital)
		iounmap(phy->digital);
error_unmap_base:
	iounmap(phy->base);
error_put_device:
	put_device(&sgmii_pdev->dev);

	return ret;
}
