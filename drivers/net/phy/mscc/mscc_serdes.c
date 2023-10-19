// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Driver for Microsemi VSC85xx PHYs
 *
 * Author: Bjarni Jonasson <bjarni.jonassoni@microchip.com>
 * License: Dual MIT/GPL
 * Copyright (c) 2021 Microsemi Corporation
 */

#include <linux/phy.h>
#include "mscc_serdes.h"
#include "mscc.h"

static int pll5g_detune(struct phy_device *phydev)
{
	u32 rd_dat;
	int ret;

	rd_dat = vsc85xx_csr_read(phydev, MACRO_CTRL, PHY_S6G_PLL5G_CFG2);
	rd_dat &= ~PHY_S6G_PLL5G_CFG2_GAIN_MASK;
	rd_dat |= PHY_S6G_PLL5G_CFG2_ENA_GAIN;
	ret = vsc85xx_csr_write(phydev, MACRO_CTRL,
				PHY_S6G_PLL5G_CFG2, rd_dat);
	if (ret)
		dev_err(&phydev->mdio.dev, "%s: write error\n", __func__);
	return ret;
}

static int pll5g_tune(struct phy_device *phydev)
{
	u32 rd_dat;
	int ret;

	rd_dat = vsc85xx_csr_read(phydev, MACRO_CTRL, PHY_S6G_PLL5G_CFG2);
	rd_dat &= ~PHY_S6G_PLL5G_CFG2_ENA_GAIN;
	ret = vsc85xx_csr_write(phydev, MACRO_CTRL,
				PHY_S6G_PLL5G_CFG2, rd_dat);
	if (ret)
		dev_err(&phydev->mdio.dev, "%s: write error\n", __func__);
	return ret;
}

static int vsc85xx_sd6g_pll_cfg_wr(struct phy_device *phydev,
				   const u32 pll_ena_offs,
				   const u32 pll_fsm_ctrl_data,
				   const u32 pll_fsm_ena)
{
	int ret;

	ret = vsc85xx_csr_write(phydev, MACRO_CTRL,
				PHY_S6G_PLL_CFG,
				(pll_fsm_ena << PHY_S6G_PLL_ENA_OFFS_POS) |
				(pll_fsm_ctrl_data << PHY_S6G_PLL_FSM_CTRL_DATA_POS) |
				(pll_ena_offs << PHY_S6G_PLL_FSM_ENA_POS));
	if (ret)
		dev_err(&phydev->mdio.dev, "%s: write error\n", __func__);
	return ret;
}

static int vsc85xx_sd6g_common_cfg_wr(struct phy_device *phydev,
				      const u32 sys_rst,
				      const u32 ena_lane,
				      const u32 ena_loop,
				      const u32 qrate,
				      const u32 if_mode,
				      const u32 pwd_tx)
{
	/* ena_loop = 8 for eloop */
	/*          = 4 for floop */
	/*          = 2 for iloop */
	/*          = 1 for ploop */
	/* qrate    = 1 for SGMII, 0 for QSGMII */
	/* if_mode  = 1 for SGMII, 3 for QSGMII */

	int ret;

	ret = vsc85xx_csr_write(phydev, MACRO_CTRL,
				PHY_S6G_COMMON_CFG,
				(sys_rst << PHY_S6G_SYS_RST_POS) |
				(ena_lane << PHY_S6G_ENA_LANE_POS) |
				(ena_loop << PHY_S6G_ENA_LOOP_POS) |
				(qrate << PHY_S6G_QRATE_POS) |
				(if_mode << PHY_S6G_IF_MODE_POS));
	if (ret)
		dev_err(&phydev->mdio.dev, "%s: write error\n", __func__);
	return ret;
}

static int vsc85xx_sd6g_des_cfg_wr(struct phy_device *phydev,
				   const u32 des_phy_ctrl,
				   const u32 des_mbtr_ctrl,
				   const u32 des_bw_hyst,
				   const u32 des_bw_ana,
				   const u32 des_cpmd_sel)
{
	u32 reg_val;
	int ret;

	/* configurable terms */
	reg_val = (des_phy_ctrl << PHY_S6G_DES_PHY_CTRL_POS) |
		  (des_mbtr_ctrl << PHY_S6G_DES_MBTR_CTRL_POS) |
		  (des_cpmd_sel << PHY_S6G_DES_CPMD_SEL_POS) |
		  (des_bw_hyst << PHY_S6G_DES_BW_HYST_POS) |
		  (des_bw_ana << PHY_S6G_DES_BW_ANA_POS);
	ret = vsc85xx_csr_write(phydev, MACRO_CTRL,
				PHY_S6G_DES_CFG,
				reg_val);
	if (ret)
		dev_err(&phydev->mdio.dev, "%s: write error\n", __func__);
	return ret;
}

static int vsc85xx_sd6g_ib_cfg0_wr(struct phy_device *phydev,
				   const u32 ib_rtrm_adj,
				   const u32 ib_sig_det_clk_sel,
				   const u32 ib_reg_pat_sel_offset,
				   const u32 ib_cal_ena)
{
	u32 base_val;
	u32 reg_val;
	int ret;

	/* constant terms */
	base_val = 0x60a85837;
	/* configurable terms */
	reg_val = base_val | (ib_rtrm_adj << 25) |
		  (ib_sig_det_clk_sel << 16) |
		  (ib_reg_pat_sel_offset << 8) |
		  (ib_cal_ena << 3);
	ret = vsc85xx_csr_write(phydev, MACRO_CTRL,
				PHY_S6G_IB_CFG0,
				reg_val);
	if (ret)
		dev_err(&phydev->mdio.dev, "%s: write error\n", __func__);
	return ret;
}

static int vsc85xx_sd6g_ib_cfg1_wr(struct phy_device *phydev,
				   const u32 ib_tjtag,
				   const u32 ib_tsdet,
				   const u32 ib_scaly,
				   const u32 ib_frc_offset,
				   const u32 ib_filt_offset)
{
	u32 ib_filt_val;
	u32 reg_val = 0;
	int ret;

	/* constant terms */
	ib_filt_val = 0xe0;
	/* configurable terms */
	reg_val  = (ib_tjtag << 17) + (ib_tsdet << 12) + (ib_scaly << 8) +
		   ib_filt_val + (ib_filt_offset << 4) + (ib_frc_offset << 0);
	ret = vsc85xx_csr_write(phydev, MACRO_CTRL,
				PHY_S6G_IB_CFG1,
				reg_val);
	if (ret)
		dev_err(&phydev->mdio.dev, "%s: write error\n", __func__);
	return ret;
}

static int vsc85xx_sd6g_ib_cfg2_wr(struct phy_device *phydev,
				   const u32 ib_tinfv,
				   const u32 ib_tcalv,
				   const u32 ib_ureg)
{
	u32 ib_cfg2_val;
	u32 base_val;
	int ret;

	/* constant terms */
	base_val = 0x0f878010;
	/* configurable terms */
	ib_cfg2_val = base_val | ((ib_tinfv) << 28) | ((ib_tcalv) << 5) |
		      (ib_ureg << 0);
	ret = vsc85xx_csr_write(phydev, MACRO_CTRL,
				PHY_S6G_IB_CFG2,
				ib_cfg2_val);
	if (ret)
		dev_err(&phydev->mdio.dev, "%s: write error\n", __func__);
	return ret;
}

static int vsc85xx_sd6g_ib_cfg3_wr(struct phy_device *phydev,
				   const u32 ib_ini_hp,
				   const u32 ib_ini_mid,
				   const u32 ib_ini_lp,
				   const u32 ib_ini_offset)
{
	u32 reg_val;
	int ret;

	reg_val  = (ib_ini_hp << 24) + (ib_ini_mid << 16) +
		   (ib_ini_lp << 8) + (ib_ini_offset << 0);
	ret = vsc85xx_csr_write(phydev, MACRO_CTRL,
				PHY_S6G_IB_CFG3,
				reg_val);
	if (ret)
		dev_err(&phydev->mdio.dev, "%s: write error\n", __func__);
	return ret;
}

static int vsc85xx_sd6g_ib_cfg4_wr(struct phy_device *phydev,
				   const u32 ib_max_hp,
				   const u32 ib_max_mid,
				   const u32 ib_max_lp,
				   const u32 ib_max_offset)
{
	u32 reg_val;
	int ret;

	reg_val  = (ib_max_hp << 24) + (ib_max_mid << 16) +
		   (ib_max_lp << 8) + (ib_max_offset << 0);
	ret = vsc85xx_csr_write(phydev, MACRO_CTRL,
				PHY_S6G_IB_CFG4,
				reg_val);
	if (ret)
		dev_err(&phydev->mdio.dev, "%s: write error\n", __func__);
	return ret;
}

static int vsc85xx_sd6g_misc_cfg_wr(struct phy_device *phydev,
				    const u32 lane_rst)
{
	int ret;

	ret = vsc85xx_csr_write(phydev, MACRO_CTRL,
				PHY_S6G_MISC_CFG,
				lane_rst);
	if (ret)
		dev_err(&phydev->mdio.dev, "%s: write error\n", __func__);
	return ret;
}

static int vsc85xx_sd6g_gp_cfg_wr(struct phy_device *phydev, const u32 gp_cfg_val)
{
	int ret;

	ret = vsc85xx_csr_write(phydev, MACRO_CTRL,
				PHY_S6G_GP_CFG,
				gp_cfg_val);
	if (ret)
		dev_err(&phydev->mdio.dev, "%s: write error\n", __func__);
	return ret;
}

static int vsc85xx_sd6g_dft_cfg2_wr(struct phy_device *phydev,
				    const u32 rx_ji_ampl,
				    const u32 rx_step_freq,
				    const u32 rx_ji_ena,
				    const u32 rx_waveform_sel,
				    const u32 rx_freqoff_dir,
				    const u32 rx_freqoff_ena)
{
	u32 reg_val;
	int ret;

	/* configurable terms */
	reg_val = (rx_ji_ampl << 8) | (rx_step_freq << 4) |
		  (rx_ji_ena << 3) | (rx_waveform_sel << 2) |
		  (rx_freqoff_dir << 1) | rx_freqoff_ena;
	ret = vsc85xx_csr_write(phydev, MACRO_CTRL,
				PHY_S6G_IB_DFT_CFG2,
				reg_val);
	if (ret)
		dev_err(&phydev->mdio.dev, "%s: write error\n", __func__);
	return ret;
}

static int vsc85xx_sd6g_dft_cfg0_wr(struct phy_device *phydev,
				    const u32 prbs_sel,
				    const u32 test_mode,
				    const u32 rx_dft_ena)
{
	u32 reg_val;
	int ret;

	/* configurable terms */
	reg_val = (prbs_sel << 20) | (test_mode << 16) | (rx_dft_ena << 2);
	ret = vsc85xx_csr_write(phydev, MACRO_CTRL,
				PHY_S6G_DFT_CFG0,
				reg_val);
	if (ret)
		dev_err(&phydev->mdio.dev, "%s: write error\n", __func__);
	return ret;
}

/* Access LCPLL Cfg_0 */
static int vsc85xx_pll5g_cfg0_wr(struct phy_device *phydev,
				 const u32 selbgv820)
{
	u32 base_val;
	u32 reg_val;
	int ret;

	/* constant terms */
	base_val = 0x7036f145;
	/* configurable terms */
	reg_val = base_val | (selbgv820 << 23);
	ret = vsc85xx_csr_write(phydev, MACRO_CTRL,
				PHY_S6G_PLL5G_CFG0, reg_val);
	if (ret)
		dev_err(&phydev->mdio.dev, "%s: write error\n", __func__);
	return ret;
}

int vsc85xx_sd6g_config_v2(struct phy_device *phydev)
{
	u32 ib_sig_det_clk_sel_cal = 0;
	u32 ib_sig_det_clk_sel_mm  = 7;
	u32 pll_fsm_ctrl_data = 60;
	unsigned long deadline;
	u32 des_bw_ana_val = 3;
	u32 ib_tsdet_cal = 16;
	u32 ib_tsdet_mm  = 5;
	u32 ib_rtrm_adj;
	u32 if_mode = 1;
	u32 gp_iter = 5;
	u32 val32 = 0;
	u32 qrate = 1;
	u32 iter;
	int val = 0;
	int ret;

	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);

	/* Detune/Unlock LCPLL */
	ret = pll5g_detune(phydev);
	if (ret)
		return ret;

	/* 0. Reset RCPLL */
	ret = vsc85xx_sd6g_pll_cfg_wr(phydev, 3, pll_fsm_ctrl_data, 0);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_common_cfg_wr(phydev, 0, 0, 0, qrate, if_mode, 0);
	if (ret)
		return ret;
	ret = phy_commit_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_des_cfg_wr(phydev, 6, 2, 5, des_bw_ana_val, 0);
	if (ret)
		return ret;

	/* 1. Configure sd6g for SGMII prior to sd6g_IB_CAL */
	ib_rtrm_adj = 13;
	ret = vsc85xx_sd6g_ib_cfg0_wr(phydev, ib_rtrm_adj, ib_sig_det_clk_sel_mm, 0, 0);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_ib_cfg1_wr(phydev, 8, ib_tsdet_mm, 15, 0, 1);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_ib_cfg2_wr(phydev, 3, 13, 5);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_ib_cfg3_wr(phydev,  0, 31, 1, 31);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_ib_cfg4_wr(phydev, 63, 63, 2, 63);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_common_cfg_wr(phydev, 1, 1, 0, qrate, if_mode, 0);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_misc_cfg_wr(phydev, 1);
	if (ret)
		return ret;
	ret = phy_commit_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
	if (ret)
		return ret;

	/* 2. Start rcpll_fsm */
	ret = vsc85xx_sd6g_pll_cfg_wr(phydev, 3, pll_fsm_ctrl_data, 1);
	if (ret)
		return ret;
	ret = phy_commit_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
	if (ret)
		return ret;

	deadline = jiffies + msecs_to_jiffies(PROC_CMD_NCOMPLETED_TIMEOUT_MS);
	do {
		usleep_range(500, 1000);
		ret = phy_update_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
		if (ret)
			return ret;
		val32 = vsc85xx_csr_read(phydev, MACRO_CTRL,
					 PHY_S6G_PLL_STATUS);
		/* wait for bit 12 to clear */
	} while (time_before(jiffies, deadline) && (val32 & BIT(12)));

	if (val32 & BIT(12))
		return -ETIMEDOUT;

	/* 4. Release digital reset and disable transmitter */
	ret = vsc85xx_sd6g_misc_cfg_wr(phydev, 0);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_common_cfg_wr(phydev, 1, 1, 0, qrate, if_mode, 1);
	if (ret)
		return ret;
	ret = phy_commit_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
	if (ret)
		return ret;

	/* 5. Apply a frequency offset on RX-side (using internal FoJi logic) */
	ret = vsc85xx_sd6g_gp_cfg_wr(phydev, 768);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_dft_cfg2_wr(phydev, 0, 2, 0, 0, 0, 1);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_dft_cfg0_wr(phydev, 0, 0, 1);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_des_cfg_wr(phydev, 6, 2, 5, des_bw_ana_val, 2);
	if (ret)
		return ret;
	ret = phy_commit_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
	if (ret)
		return ret;

	/* 6. Prepare required settings for IBCAL */
	ret = vsc85xx_sd6g_ib_cfg1_wr(phydev, 8, ib_tsdet_cal, 15, 1, 0);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_ib_cfg0_wr(phydev, ib_rtrm_adj, ib_sig_det_clk_sel_cal, 0, 0);
	if (ret)
		return ret;
	ret = phy_commit_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
	if (ret)
		return ret;

	/* 7. Start IB_CAL */
	ret = vsc85xx_sd6g_ib_cfg0_wr(phydev, ib_rtrm_adj,
				      ib_sig_det_clk_sel_cal, 0, 1);
	if (ret)
		return ret;
	ret = phy_commit_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
	if (ret)
		return ret;
	/* 11 cycles (for ViperA) or 5 cycles (for ViperB & Elise) w/ SW clock */
	for (iter = 0; iter < gp_iter; iter++) {
		/* set gp(0) */
		ret = vsc85xx_sd6g_gp_cfg_wr(phydev, 769);
		if (ret)
			return ret;
		ret = phy_commit_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
		if (ret)
			return ret;
		/* clear gp(0) */
		ret = vsc85xx_sd6g_gp_cfg_wr(phydev, 768);
		if (ret)
			return ret;
		ret = phy_commit_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
		if (ret)
			return ret;
	}

	ret = vsc85xx_sd6g_ib_cfg1_wr(phydev, 8, ib_tsdet_cal, 15, 1, 1);
	if (ret)
		return ret;
	ret = phy_commit_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_ib_cfg1_wr(phydev, 8, ib_tsdet_cal, 15, 0, 1);
	if (ret)
		return ret;
	ret = phy_commit_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
	if (ret)
		return ret;

	/* 8. Wait for IB cal to complete */
	deadline = jiffies + msecs_to_jiffies(PROC_CMD_NCOMPLETED_TIMEOUT_MS);
	do {
		usleep_range(500, 1000);
		ret = phy_update_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
		if (ret)
			return ret;
		val32 = vsc85xx_csr_read(phydev, MACRO_CTRL,
					 PHY_S6G_IB_STATUS0);
		/* wait for bit 8 to set */
	} while (time_before(jiffies, deadline) && (~val32 & BIT(8)));

	if (~val32 & BIT(8))
		return -ETIMEDOUT;

	/* 9. Restore cfg values for mission mode */
	ret = vsc85xx_sd6g_ib_cfg0_wr(phydev, ib_rtrm_adj, ib_sig_det_clk_sel_mm, 0, 1);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_ib_cfg1_wr(phydev, 8, ib_tsdet_mm, 15, 0, 1);
	if (ret)
		return ret;
	ret = phy_commit_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
	if (ret)
		return ret;

	/* 10. Re-enable transmitter */
	ret = vsc85xx_sd6g_common_cfg_wr(phydev, 1, 1, 0, qrate, if_mode, 0);
	if (ret)
		return ret;
	ret = phy_commit_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
	if (ret)
		return ret;

	/* 11. Disable frequency offset generation (using internal FoJi logic) */
	ret = vsc85xx_sd6g_dft_cfg2_wr(phydev, 0, 0, 0, 0, 0, 0);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_dft_cfg0_wr(phydev, 0, 0, 0);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_des_cfg_wr(phydev, 6, 2, 5, des_bw_ana_val, 0);
	if (ret)
		return ret;
	ret = phy_commit_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
	if (ret)
		return ret;

	/* Tune/Re-lock LCPLL */
	ret = pll5g_tune(phydev);
	if (ret)
		return ret;

	/* 12. Configure for Final Configuration and Settings */
	/* a. Reset RCPLL */
	ret = vsc85xx_sd6g_pll_cfg_wr(phydev, 3, pll_fsm_ctrl_data, 0);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_common_cfg_wr(phydev, 0, 1, 0, qrate, if_mode, 0);
	if (ret)
		return ret;
	ret = phy_commit_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
	if (ret)
		return ret;

	/* b. Configure sd6g for desired operating mode */
	phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_EXTENDED_GPIO);
	ret = phy_base_read(phydev, MSCC_PHY_MAC_CFG_FASTLINK);
	if ((ret & MAC_CFG_MASK) == MAC_CFG_QSGMII) {
		/* QSGMII */
		pll_fsm_ctrl_data = 120;
		qrate   = 0;
		if_mode = 3;
		des_bw_ana_val = 5;
		val = PROC_CMD_MCB_ACCESS_MAC_CONF | PROC_CMD_RST_CONF_PORT |
			PROC_CMD_READ_MOD_WRITE_PORT | PROC_CMD_QSGMII_MAC;

		ret = vsc8584_cmd(phydev, val);
		if (ret) {
			dev_err(&phydev->mdio.dev, "%s: QSGMII error: %d\n",
				__func__, ret);
			return ret;
		}

		phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);
	} else if ((ret & MAC_CFG_MASK) == MAC_CFG_SGMII) {
		/* SGMII */
		pll_fsm_ctrl_data = 60;
		qrate   = 1;
		if_mode = 1;
		des_bw_ana_val = 3;

		val = PROC_CMD_MCB_ACCESS_MAC_CONF | PROC_CMD_RST_CONF_PORT |
			PROC_CMD_READ_MOD_WRITE_PORT | PROC_CMD_SGMII_MAC;

		ret = vsc8584_cmd(phydev, val);
		if (ret) {
			dev_err(&phydev->mdio.dev, "%s: SGMII error: %d\n",
				__func__, ret);
			return ret;
		}

		phy_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);
	} else {
		dev_err(&phydev->mdio.dev, "%s: invalid mac_if: %x\n",
			__func__, ret);
	}

	ret = phy_update_mcb_s6g(phydev, PHY_S6G_LCPLL_CFG, 0);
	if (ret)
		return ret;
	ret = phy_update_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
	if (ret)
		return ret;
	ret = vsc85xx_pll5g_cfg0_wr(phydev, 4);
	if (ret)
		return ret;
	ret = phy_commit_mcb_s6g(phydev, PHY_S6G_LCPLL_CFG, 0);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_des_cfg_wr(phydev, 6, 2, 5, des_bw_ana_val, 0);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_ib_cfg0_wr(phydev, ib_rtrm_adj, ib_sig_det_clk_sel_mm, 0, 1);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_ib_cfg1_wr(phydev, 8, ib_tsdet_mm, 15, 0, 1);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_common_cfg_wr(phydev, 1, 1, 0, qrate, if_mode, 0);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_ib_cfg2_wr(phydev, 3, 13, 5);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_ib_cfg3_wr(phydev,  0, 31, 1, 31);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_ib_cfg4_wr(phydev, 63, 63, 2, 63);
	if (ret)
		return ret;
	ret = vsc85xx_sd6g_misc_cfg_wr(phydev, 1);
	if (ret)
		return ret;
	ret = phy_commit_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
	if (ret)
		return ret;

	/* 13. Start rcpll_fsm */
	ret = vsc85xx_sd6g_pll_cfg_wr(phydev, 3, pll_fsm_ctrl_data, 1);
	if (ret)
		return ret;
	ret = phy_commit_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
	if (ret)
		return ret;

	/* 14. Wait for PLL cal to complete */
	deadline = jiffies + msecs_to_jiffies(PROC_CMD_NCOMPLETED_TIMEOUT_MS);
	do {
		usleep_range(500, 1000);
		ret = phy_update_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
		if (ret)
			return ret;
		val32 = vsc85xx_csr_read(phydev, MACRO_CTRL,
					 PHY_S6G_PLL_STATUS);
		/* wait for bit 12 to clear */
	} while (time_before(jiffies, deadline) && (val32 & BIT(12)));

	if (val32 & BIT(12))
		return -ETIMEDOUT;

	/* release lane reset */
	ret = vsc85xx_sd6g_misc_cfg_wr(phydev, 0);
	if (ret)
		return ret;

	return phy_commit_mcb_s6g(phydev, PHY_MCB_S6G_CFG, 0);
}
