// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved. */
/*
 * MSM PCIe PHY endpoint mode
 */

#include "ep_pcie_com.h"
#include "ep_pcie_phy.h"

void ep_pcie_phy_init(struct ep_pcie_dev_t *dev)
{

	if (dev->rumi)
		return;

	switch (dev->phy_rev) {
	case 3:
		EP_PCIE_DBG(dev,
			"PCIe V%d: PHY V%d: Initializing 20nm QMP phy - 100MHz\n",
			dev->rev, dev->phy_rev);
		break;
	case 4:
		EP_PCIE_DBG(dev,
			"PCIe V%d: PHY V%d: Initializing 14nm QMP phy - 100MHz\n",
			dev->rev, dev->phy_rev);
		break;
	case 5:
		EP_PCIE_DBG(dev,
			"PCIe V%d: PHY V%d: Initializing 10nm QMP phy - 100MHz\n",
			dev->rev, dev->phy_rev);
		break;
	case 6:
		EP_PCIE_DBG(dev,
			"PCIe V%d: PHY V%d: Initializing 7nm QMP phy - 100MHz\n",
			dev->rev, dev->phy_rev);
		break;
	case 7:
		EP_PCIE_DBG(dev,
			"PCIe V%d: PHY V%d: Initializing 5nm QMP phy - 100MHz\n",
			dev->rev, dev->phy_rev);
		break;
	case 8:
		EP_PCIE_DBG(dev,
			"PCIe V%d: PHY V%d: Initializing 4nm QMP phy - 100MHz\n",
			dev->rev, dev->phy_rev);
		break;
	default:
		EP_PCIE_ERR(dev,
			"PCIe V%d: Unexpected phy version %d is caught\n",
			dev->rev, dev->phy_rev);
	}

	if (dev->phy_init_len && dev->phy_init) {
		int i;
		struct ep_pcie_phy_info_t *phy_init;

		EP_PCIE_DBG(dev,
			"PCIe V%d: PHY V%d: process the sequence specified by DT\n",
			dev->rev, dev->phy_rev);

		i =  dev->phy_init_len;
		phy_init = dev->phy_init;
		while (i--) {
			ep_pcie_write_reg(dev->phy,
				phy_init->offset,
				phy_init->val);
			if (phy_init->delay)
				usleep_range(phy_init->delay,
					phy_init->delay + 1);
			phy_init++;
		}
		return;
	}

	ep_pcie_write_reg(dev->phy, PCIE_PHY_SW_RESET, 0x01);
	ep_pcie_write_reg(dev->phy, PCIE_PHY_POWER_DOWN_CONTROL, 0x01);

	/* Common block settings */
	ep_pcie_write_reg(dev->phy, QSERDES_COM_BIAS_EN_CLKBUFLR_EN, 0x18);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_CLK_ENABLE1, 0x00);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_BG_TRIM, 0x0F);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_LOCK_CMP_EN, 0x01);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_VCO_TUNE_MAP, 0x00);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_VCO_TUNE_TIMER1, 0xFF);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_VCO_TUNE_TIMER2, 0x1F);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_CMN_CONFIG, 0x06);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_PLL_IVCO, 0x0F);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_HSCLK_SEL, 0x00);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_SVS_MODE_CLK_SEL, 0x01);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_CORE_CLK_EN, 0x20);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_CORECLK_DIV, 0x0A);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_RESETSM_CNTRL, 0x20);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_BG_TIMER, 0x01);

	/* PLL Config Settings */
	ep_pcie_write_reg(dev->phy, QSERDES_COM_SYSCLK_EN_SEL, 0x00);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_DEC_START_MODE0, 0x19);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_DIV_FRAC_START3_MODE0, 0x00);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_DIV_FRAC_START2_MODE0, 0x00);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_DIV_FRAC_START1_MODE0, 0x00);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_LOCK_CMP3_MODE0, 0x00);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_LOCK_CMP2_MODE0, 0x02);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_LOCK_CMP1_MODE0, 0x7F);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_CLK_SELECT, 0x30);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_SYS_CLK_CTRL, 0x06);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_SYSCLK_BUF_ENABLE, 0x1E);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_CP_CTRL_MODE0, 0x3F);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_PLL_RCTRL_MODE0, 0x1A);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_PLL_CCTRL_MODE0, 0x00);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_INTEGLOOP_GAIN1_MODE0, 0x03);
	ep_pcie_write_reg(dev->phy, QSERDES_COM_INTEGLOOP_GAIN0_MODE0, 0xFF);

	/* TX settings */
	ep_pcie_write_reg(dev->phy, QSERDES_TX_HIGHZ_TRANSCEIVEREN_BIAS_DRVR_EN,
				0x45);
	ep_pcie_write_reg(dev->phy, QSERDES_TX_LANE_MODE, 0x06);
	ep_pcie_write_reg(dev->phy, QSERDES_TX_RES_CODE_LANE_OFFSET, 0x02);
	ep_pcie_write_reg(dev->phy, QSERDES_TX_RCV_DETECT_LVL_2, 0x12);

	/* RX settings */
	ep_pcie_write_reg(dev->phy, QSERDES_RX_SIGDET_ENABLES, 0x1C);
	ep_pcie_write_reg(dev->phy, QSERDES_RX_SIGDET_DEGLITCH_CNTRL, 0x14);
	ep_pcie_write_reg(dev->phy, QSERDES_RX_RX_EQU_ADAPTOR_CNTRL2, 0x01);
	ep_pcie_write_reg(dev->phy, QSERDES_RX_RX_EQU_ADAPTOR_CNTRL3, 0x00);
	ep_pcie_write_reg(dev->phy, QSERDES_RX_RX_EQU_ADAPTOR_CNTRL4, 0xDB);
	ep_pcie_write_reg(dev->phy, QSERDES_RX_UCDR_SO_SATURATION_AND_ENABLE,
				0x4B);
	ep_pcie_write_reg(dev->phy, QSERDES_RX_UCDR_SO_GAIN, 0x04);
	ep_pcie_write_reg(dev->phy, QSERDES_RX_UCDR_SO_GAIN_HALF, 0x04);

	/* EP_REF_CLK settings */
	ep_pcie_write_reg(dev->phy, QSERDES_COM_CLK_EP_DIV, 0x19);
	ep_pcie_write_reg(dev->phy, PCIE_PHY_ENDPOINT_REFCLK_DRIVE, 0x00);

	/* PCIE L1SS settings */
	ep_pcie_write_reg(dev->phy, PCIE_PHY_PWRUP_RESET_DLY_TIME_AUXCLK, 0x40);
	ep_pcie_write_reg(dev->phy, PCIE_PHY_L1SS_WAKEUP_DLY_TIME_AUXCLK_MSB,
				0x00);
	ep_pcie_write_reg(dev->phy, PCIE_PHY_L1SS_WAKEUP_DLY_TIME_AUXCLK_LSB,
				0x40);
	ep_pcie_write_reg(dev->phy, PCIE_PHY_LP_WAKEUP_DLY_TIME_AUXCLK_MSB,
				0x00);
	ep_pcie_write_reg(dev->phy, PCIE_PHY_LP_WAKEUP_DLY_TIME_AUXCLK,	0x40);
	ep_pcie_write_reg(dev->phy, PCIE_PHY_PLL_LOCK_CHK_DLY_TIME, 0x73);

	/* PCS settings */
	ep_pcie_write_reg(dev->phy, PCIE_PHY_SIGDET_CNTRL, 0x07);
	ep_pcie_write_reg(dev->phy, PCIE_PHY_RX_SIGDET_LVL, 0x99);
	ep_pcie_write_reg(dev->phy, PCIE_PHY_TXDEEMPH_M6DB_V0, 0x15);
	ep_pcie_write_reg(dev->phy, PCIE_PHY_TXDEEMPH_M3P5DB_V0, 0x0E);

	ep_pcie_write_reg(dev->phy, PCIE_PHY_SW_RESET, 0x00);
	ep_pcie_write_reg(dev->phy, PCIE_PHY_START_CONTROL, 0x03);
}

bool ep_pcie_phy_is_ready(struct ep_pcie_dev_t *dev)
{
	u32 offset;

	if (dev->rumi)
		return true;

	if (dev->phy_status_reg)
		offset = dev->phy_status_reg;
	else
		offset = PCIE_PHY_PCS_STATUS;

	if (readl_relaxed(dev->phy + offset) & dev->phy_status_bit_mask_bit)
		return false;
	else
		return true;
}
