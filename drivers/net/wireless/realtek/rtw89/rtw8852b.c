// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2022  Realtek Corporation
 */

#include "coex.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"
#include "rtw8852b.h"
#include "rtw8852b_common.h"
#include "rtw8852b_rfk.h"
#include "rtw8852b_table.h"
#include "txrx.h"

#define RTW8852B_FW_FORMAT_MAX 1
#define RTW8852B_FW_BASENAME "rtw89/rtw8852b_fw"
#define RTW8852B_MODULE_FIRMWARE \
	RTW8852B_FW_BASENAME "-" __stringify(RTW8852B_FW_FORMAT_MAX) ".bin"

static const struct rtw89_hfc_ch_cfg rtw8852b_hfc_chcfg_pcie[] = {
	{5, 341, grp_0}, /* ACH 0 */
	{5, 341, grp_0}, /* ACH 1 */
	{4, 342, grp_0}, /* ACH 2 */
	{4, 342, grp_0}, /* ACH 3 */
	{0, 0, grp_0}, /* ACH 4 */
	{0, 0, grp_0}, /* ACH 5 */
	{0, 0, grp_0}, /* ACH 6 */
	{0, 0, grp_0}, /* ACH 7 */
	{4, 342, grp_0}, /* B0MGQ */
	{4, 342, grp_0}, /* B0HIQ */
	{0, 0, grp_0}, /* B1MGQ */
	{0, 0, grp_0}, /* B1HIQ */
	{40, 0, 0} /* FWCMDQ */
};

static const struct rtw89_hfc_pub_cfg rtw8852b_hfc_pubcfg_pcie = {
	446, /* Group 0 */
	0, /* Group 1 */
	446, /* Public Max */
	0 /* WP threshold */
};

static const struct rtw89_hfc_param_ini rtw8852b_hfc_param_ini_pcie[] = {
	[RTW89_QTA_SCC] = {rtw8852b_hfc_chcfg_pcie, &rtw8852b_hfc_pubcfg_pcie,
			   &rtw89_mac_size.hfc_preccfg_pcie, RTW89_HCIFC_POH},
	[RTW89_QTA_DLFW] = {NULL, NULL, &rtw89_mac_size.hfc_preccfg_pcie,
			    RTW89_HCIFC_POH},
	[RTW89_QTA_INVALID] = {NULL},
};

static const struct rtw89_dle_mem rtw8852b_dle_mem_pcie[] = {
	[RTW89_QTA_SCC] = {RTW89_QTA_SCC, &rtw89_mac_size.wde_size7,
			   &rtw89_mac_size.ple_size6, &rtw89_mac_size.wde_qt7,
			   &rtw89_mac_size.wde_qt7, &rtw89_mac_size.ple_qt18,
			   &rtw89_mac_size.ple_qt58},
	[RTW89_QTA_WOW] = {RTW89_QTA_WOW, &rtw89_mac_size.wde_size7,
			   &rtw89_mac_size.ple_size6, &rtw89_mac_size.wde_qt7,
			   &rtw89_mac_size.wde_qt7, &rtw89_mac_size.ple_qt18,
			   &rtw89_mac_size.ple_qt_52b_wow},
	[RTW89_QTA_DLFW] = {RTW89_QTA_DLFW, &rtw89_mac_size.wde_size9,
			    &rtw89_mac_size.ple_size8, &rtw89_mac_size.wde_qt4,
			    &rtw89_mac_size.wde_qt4, &rtw89_mac_size.ple_qt13,
			    &rtw89_mac_size.ple_qt13},
	[RTW89_QTA_INVALID] = {RTW89_QTA_INVALID, NULL, NULL, NULL, NULL, NULL,
			       NULL},
};

static const u32 rtw8852b_h2c_regs[RTW89_H2CREG_MAX] = {
	R_AX_H2CREG_DATA0, R_AX_H2CREG_DATA1,  R_AX_H2CREG_DATA2,
	R_AX_H2CREG_DATA3
};

static const u32 rtw8852b_c2h_regs[RTW89_C2HREG_MAX] = {
	R_AX_C2HREG_DATA0, R_AX_C2HREG_DATA1, R_AX_C2HREG_DATA2,
	R_AX_C2HREG_DATA3
};

static const u32 rtw8852b_wow_wakeup_regs[RTW89_WOW_REASON_NUM] = {
	R_AX_C2HREG_DATA3 + 3, R_AX_C2HREG_DATA3 + 3,
};

static const struct rtw89_page_regs rtw8852b_page_regs = {
	.hci_fc_ctrl	= R_AX_HCI_FC_CTRL,
	.ch_page_ctrl	= R_AX_CH_PAGE_CTRL,
	.ach_page_ctrl	= R_AX_ACH0_PAGE_CTRL,
	.ach_page_info	= R_AX_ACH0_PAGE_INFO,
	.pub_page_info3	= R_AX_PUB_PAGE_INFO3,
	.pub_page_ctrl1	= R_AX_PUB_PAGE_CTRL1,
	.pub_page_ctrl2	= R_AX_PUB_PAGE_CTRL2,
	.pub_page_info1	= R_AX_PUB_PAGE_INFO1,
	.pub_page_info2 = R_AX_PUB_PAGE_INFO2,
	.wp_page_ctrl1	= R_AX_WP_PAGE_CTRL1,
	.wp_page_ctrl2	= R_AX_WP_PAGE_CTRL2,
	.wp_page_info1	= R_AX_WP_PAGE_INFO1,
};

static const struct rtw89_reg_def rtw8852b_dcfo_comp = {
	R_DCFO_COMP_S0, B_DCFO_COMP_S0_MSK
};

static const struct rtw89_imr_info rtw8852b_imr_info = {
	.wdrls_imr_set		= B_AX_WDRLS_IMR_SET,
	.wsec_imr_reg		= R_AX_SEC_DEBUG,
	.wsec_imr_set		= B_AX_IMR_ERROR,
	.mpdu_tx_imr_set	= 0,
	.mpdu_rx_imr_set	= 0,
	.sta_sch_imr_set	= B_AX_STA_SCHEDULER_IMR_SET,
	.txpktctl_imr_b0_reg	= R_AX_TXPKTCTL_ERR_IMR_ISR,
	.txpktctl_imr_b0_clr	= B_AX_TXPKTCTL_IMR_B0_CLR,
	.txpktctl_imr_b0_set	= B_AX_TXPKTCTL_IMR_B0_SET,
	.txpktctl_imr_b1_reg	= R_AX_TXPKTCTL_ERR_IMR_ISR_B1,
	.txpktctl_imr_b1_clr	= B_AX_TXPKTCTL_IMR_B1_CLR,
	.txpktctl_imr_b1_set	= B_AX_TXPKTCTL_IMR_B1_SET,
	.wde_imr_clr		= B_AX_WDE_IMR_CLR,
	.wde_imr_set		= B_AX_WDE_IMR_SET,
	.ple_imr_clr		= B_AX_PLE_IMR_CLR,
	.ple_imr_set		= B_AX_PLE_IMR_SET,
	.host_disp_imr_clr	= B_AX_HOST_DISP_IMR_CLR,
	.host_disp_imr_set	= B_AX_HOST_DISP_IMR_SET,
	.cpu_disp_imr_clr	= B_AX_CPU_DISP_IMR_CLR,
	.cpu_disp_imr_set	= B_AX_CPU_DISP_IMR_SET,
	.other_disp_imr_clr	= B_AX_OTHER_DISP_IMR_CLR,
	.other_disp_imr_set	= 0,
	.bbrpt_com_err_imr_reg	= R_AX_BBRPT_COM_ERR_IMR_ISR,
	.bbrpt_chinfo_err_imr_reg = R_AX_BBRPT_CHINFO_ERR_IMR_ISR,
	.bbrpt_err_imr_set	= 0,
	.bbrpt_dfs_err_imr_reg	= R_AX_BBRPT_DFS_ERR_IMR_ISR,
	.ptcl_imr_clr		= B_AX_PTCL_IMR_CLR_ALL,
	.ptcl_imr_set		= B_AX_PTCL_IMR_SET,
	.cdma_imr_0_reg		= R_AX_DLE_CTRL,
	.cdma_imr_0_clr		= B_AX_DLE_IMR_CLR,
	.cdma_imr_0_set		= B_AX_DLE_IMR_SET,
	.cdma_imr_1_reg		= 0,
	.cdma_imr_1_clr		= 0,
	.cdma_imr_1_set		= 0,
	.phy_intf_imr_reg	= R_AX_PHYINFO_ERR_IMR,
	.phy_intf_imr_clr	= 0,
	.phy_intf_imr_set	= 0,
	.rmac_imr_reg		= R_AX_RMAC_ERR_ISR,
	.rmac_imr_clr		= B_AX_RMAC_IMR_CLR,
	.rmac_imr_set		= B_AX_RMAC_IMR_SET,
	.tmac_imr_reg		= R_AX_TMAC_ERR_IMR_ISR,
	.tmac_imr_clr		= B_AX_TMAC_IMR_CLR,
	.tmac_imr_set		= B_AX_TMAC_IMR_SET,
};

static const struct rtw89_rrsr_cfgs rtw8852b_rrsr_cfgs = {
	.ref_rate = {R_AX_TRXPTCL_RRSR_CTL_0, B_AX_WMAC_RESP_REF_RATE_SEL, 0},
	.rsc = {R_AX_TRXPTCL_RRSR_CTL_0, B_AX_WMAC_RESP_RSC_MASK, 2},
};

static const struct rtw89_rfkill_regs rtw8852b_rfkill_regs = {
	.pinmux = {R_AX_GPIO8_15_FUNC_SEL,
		   B_AX_PINMUX_GPIO9_FUNC_SEL_MASK,
		   0xf},
	.mode = {R_AX_GPIO_EXT_CTRL + 2,
		 (B_AX_GPIO_MOD_9 | B_AX_GPIO_IO_SEL_9) >> 16,
		 0x0},
};

static const struct rtw89_dig_regs rtw8852b_dig_regs = {
	.seg0_pd_reg = R_SEG0R_PD_V1,
	.pd_lower_bound_mask = B_SEG0R_PD_LOWER_BOUND_MSK,
	.pd_spatial_reuse_en = B_SEG0R_PD_SPATIAL_REUSE_EN_MSK_V1,
	.bmode_pd_reg = R_BMODE_PDTH_EN_V1,
	.bmode_cca_rssi_limit_en = B_BMODE_PDTH_LIMIT_EN_MSK_V1,
	.bmode_pd_lower_bound_reg = R_BMODE_PDTH_V1,
	.bmode_rssi_nocca_low_th_mask = B_BMODE_PDTH_LOWER_BOUND_MSK_V1,
	.p0_lna_init = {R_PATH0_LNA_INIT_V1, B_PATH0_LNA_INIT_IDX_MSK},
	.p1_lna_init = {R_PATH1_LNA_INIT_V1, B_PATH1_LNA_INIT_IDX_MSK},
	.p0_tia_init = {R_PATH0_TIA_INIT_V1, B_PATH0_TIA_INIT_IDX_MSK_V1},
	.p1_tia_init = {R_PATH1_TIA_INIT_V1, B_PATH1_TIA_INIT_IDX_MSK_V1},
	.p0_rxb_init = {R_PATH0_RXB_INIT_V1, B_PATH0_RXB_INIT_IDX_MSK_V1},
	.p1_rxb_init = {R_PATH1_RXB_INIT_V1, B_PATH1_RXB_INIT_IDX_MSK_V1},
	.p0_p20_pagcugc_en = {R_PATH0_P20_FOLLOW_BY_PAGCUGC_V2,
			      B_PATH0_P20_FOLLOW_BY_PAGCUGC_EN_MSK},
	.p0_s20_pagcugc_en = {R_PATH0_S20_FOLLOW_BY_PAGCUGC_V2,
			      B_PATH0_S20_FOLLOW_BY_PAGCUGC_EN_MSK},
	.p1_p20_pagcugc_en = {R_PATH1_P20_FOLLOW_BY_PAGCUGC_V2,
			      B_PATH1_P20_FOLLOW_BY_PAGCUGC_EN_MSK},
	.p1_s20_pagcugc_en = {R_PATH1_S20_FOLLOW_BY_PAGCUGC_V2,
			      B_PATH1_S20_FOLLOW_BY_PAGCUGC_EN_MSK},
};

static const struct rtw89_edcca_regs rtw8852b_edcca_regs = {
	.edcca_level			= R_SEG0R_EDCCA_LVL_V1,
	.edcca_mask			= B_EDCCA_LVL_MSK0,
	.edcca_p_mask			= B_EDCCA_LVL_MSK1,
	.ppdu_level			= R_SEG0R_EDCCA_LVL_V1,
	.ppdu_mask			= B_EDCCA_LVL_MSK3,
	.p = {{
		.rpt_a			= R_EDCCA_RPT_A,
		.rpt_b			= R_EDCCA_RPT_B,
		.rpt_sel		= R_EDCCA_RPT_SEL,
		.rpt_sel_mask		= B_EDCCA_RPT_SEL_MSK,
	}, {
		.rpt_a			= R_EDCCA_RPT_P1_A,
		.rpt_b			= R_EDCCA_RPT_P1_B,
		.rpt_sel		= R_EDCCA_RPT_SEL,
		.rpt_sel_mask		= B_EDCCA_RPT_SEL_P1_MSK,
	}},
	.tx_collision_t2r_st		= R_TX_COLLISION_T2R_ST,
	.tx_collision_t2r_st_mask	= B_TX_COLLISION_T2R_ST_M,
};

static const struct rtw89_btc_rf_trx_para rtw89_btc_8852b_rf_ul[] = {
	{255, 0, 0, 7}, /* 0 -> original */
	{255, 2, 0, 7}, /* 1 -> for BT-connected ACI issue && BTG co-rx */
	{255, 0, 0, 7}, /* 2 ->reserved for shared-antenna */
	{255, 0, 0, 7}, /* 3- >reserved for shared-antenna */
	{255, 0, 0, 7}, /* 4 ->reserved for shared-antenna */
	{255, 1, 0, 7}, /* the below id is for non-shared-antenna free-run */
	{6, 1, 0, 7},
	{13, 1, 0, 7},
	{13, 1, 0, 7}
};

static const struct rtw89_btc_rf_trx_para rtw89_btc_8852b_rf_dl[] = {
	{255, 0, 0, 7}, /* 0 -> original */
	{255, 2, 0, 7}, /* 1 -> reserved for shared-antenna */
	{255, 0, 0, 7}, /* 2 ->reserved for shared-antenna */
	{255, 0, 0, 7}, /* 3- >reserved for shared-antenna */
	{255, 0, 0, 7}, /* 4 ->reserved for shared-antenna */
	{255, 1, 0, 7}, /* the below id is for non-shared-antenna free-run */
	{255, 1, 0, 7},
	{255, 1, 0, 7},
	{255, 1, 0, 7}
};

static const struct rtw89_btc_fbtc_mreg rtw89_btc_8852b_mon_reg[] = {
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda24),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda28),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda2c),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda30),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda4c),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda10),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda20),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda34),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xcef4),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0x8424),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xd200),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xd220),
	RTW89_DEF_FBTC_MREG(REG_BB, 4, 0x980),
	RTW89_DEF_FBTC_MREG(REG_BB, 4, 0x4738),
	RTW89_DEF_FBTC_MREG(REG_BB, 4, 0x4688),
	RTW89_DEF_FBTC_MREG(REG_BB, 4, 0x4694),
};

static const u8 rtw89_btc_8852b_wl_rssi_thres[BTC_WL_RSSI_THMAX] = {70, 60, 50, 40};
static const u8 rtw89_btc_8852b_bt_rssi_thres[BTC_BT_RSSI_THMAX] = {50, 40, 30, 20};

static void rtw8852b_pwr_sps_ana(struct rtw89_dev *rtwdev)
{
	struct rtw89_efuse *efuse = &rtwdev->efuse;

	if (efuse->rfe_type == 0x5)
		rtw89_write16(rtwdev, R_AX_SPS_ANA_ON_CTRL2, RTL8852B_RFE_05_SPS_ANA);
}

static int rtw8852b_pwr_on_func(struct rtw89_dev *rtwdev)
{
	u32 val32;
	int ret;

	rtw8852b_pwr_sps_ana(rtwdev);

	rtw89_write32_clr(rtwdev, R_AX_SYS_PW_CTRL, B_AX_AFSM_WLSUS_EN |
						    B_AX_AFSM_PCIE_SUS_EN);
	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_DIS_WLBT_PDNSUSEN_SOPC);
	rtw89_write32_set(rtwdev, R_AX_WLLPS_CTRL, B_AX_DIS_WLBT_LPSEN_LOPC);
	rtw89_write32_clr(rtwdev, R_AX_SYS_PW_CTRL, B_AX_APDM_HPDN);
	rtw89_write32_clr(rtwdev, R_AX_SYS_PW_CTRL, B_AX_APFM_SWLPS);

	ret = read_poll_timeout(rtw89_read32, val32, val32 & B_AX_RDY_SYSPWR,
				1000, 20000, false, rtwdev, R_AX_SYS_PW_CTRL);
	if (ret)
		return ret;

	rtw89_write32_set(rtwdev, R_AX_AFE_LDO_CTRL, B_AX_AON_OFF_PC_EN);
	ret = read_poll_timeout(rtw89_read32, val32, val32 & B_AX_AON_OFF_PC_EN,
				1000, 20000, false, rtwdev, R_AX_AFE_LDO_CTRL);
	if (ret)
		return ret;

	rtw89_write32_mask(rtwdev, R_AX_SPS_DIG_OFF_CTRL0, B_AX_C1_L1_MASK, 0x1);
	rtw89_write32_mask(rtwdev, R_AX_SPS_DIG_OFF_CTRL0, B_AX_C3_L1_MASK, 0x3);
	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_EN_WLON);
	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_APFN_ONMAC);

	ret = read_poll_timeout(rtw89_read32, val32, !(val32 & B_AX_APFN_ONMAC),
				1000, 20000, false, rtwdev, R_AX_SYS_PW_CTRL);
	if (ret)
		return ret;

	rtw89_write8_set(rtwdev, R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
	rtw89_write8_clr(rtwdev, R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
	rtw89_write8_set(rtwdev, R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
	rtw89_write8_clr(rtwdev, R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);

	rtw89_write8_set(rtwdev, R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
	rtw89_write32_clr(rtwdev, R_AX_SYS_SDIO_CTRL, B_AX_PCIE_CALIB_EN_V1);

	rtw89_write32_set(rtwdev, R_AX_SYS_ADIE_PAD_PWR_CTRL, B_AX_SYM_PADPDN_WL_PTA_1P3);

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL,
				      XTAL_SI_GND_SHDN_WL, XTAL_SI_GND_SHDN_WL);
	if (ret)
		return ret;

	rtw89_write32_set(rtwdev, R_AX_SYS_ADIE_PAD_PWR_CTRL, B_AX_SYM_PADPDN_WL_RFC_1P3);

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL,
				      XTAL_SI_SHDN_WL, XTAL_SI_SHDN_WL);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, XTAL_SI_OFF_WEI,
				      XTAL_SI_OFF_WEI);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, XTAL_SI_OFF_EI,
				      XTAL_SI_OFF_EI);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, XTAL_SI_RFC2RF);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, XTAL_SI_PON_WEI,
				      XTAL_SI_PON_WEI);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, XTAL_SI_PON_EI,
				      XTAL_SI_PON_EI);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, XTAL_SI_SRAM2RFC);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_SRAM_CTRL, 0, XTAL_SI_SRAM_DIS);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_XTAL_XMD_2, 0, XTAL_SI_LDO_LPS);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_XTAL_XMD_4, 0, XTAL_SI_LPS_CAP);
	if (ret)
		return ret;

	rtw89_write32_set(rtwdev, R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);
	rtw89_write32_set(rtwdev, R_AX_SYS_ISO_CTRL, B_AX_ISO_EB2CORE);
	rtw89_write32_clr(rtwdev, R_AX_SYS_ISO_CTRL, B_AX_PWC_EV2EF_B15);

	fsleep(1000);

	rtw89_write32_clr(rtwdev, R_AX_SYS_ISO_CTRL, B_AX_PWC_EV2EF_B14);
	rtw89_write32_clr(rtwdev, R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);

	if (!rtwdev->efuse.valid || rtwdev->efuse.power_k_valid)
		goto func_en;

	rtw89_write32_mask(rtwdev, R_AX_SPS_DIG_ON_CTRL0, B_AX_VOL_L1_MASK, 0x9);
	rtw89_write32_mask(rtwdev, R_AX_SPS_DIG_ON_CTRL0, B_AX_VREFPFM_L_MASK, 0xA);

	if (rtwdev->hal.cv == CHIP_CBV) {
		rtw89_write32_set(rtwdev, R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);
		rtw89_write16_mask(rtwdev, R_AX_HCI_LDO_CTRL, B_AX_R_AX_VADJ_MASK, 0xA);
		rtw89_write32_clr(rtwdev, R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);
	}

func_en:
	rtw89_write32_set(rtwdev, R_AX_DMAC_FUNC_EN,
			  B_AX_MAC_FUNC_EN | B_AX_DMAC_FUNC_EN | B_AX_MPDU_PROC_EN |
			  B_AX_WD_RLS_EN | B_AX_DLE_WDE_EN | B_AX_TXPKT_CTRL_EN |
			  B_AX_STA_SCH_EN | B_AX_DLE_PLE_EN | B_AX_PKT_BUF_EN |
			  B_AX_DMAC_TBL_EN | B_AX_PKT_IN_EN | B_AX_DLE_CPUIO_EN |
			  B_AX_DISPATCHER_EN | B_AX_BBRPT_EN | B_AX_MAC_SEC_EN |
			  B_AX_DMACREG_GCKEN);
	rtw89_write32_set(rtwdev, R_AX_CMAC_FUNC_EN,
			  B_AX_CMAC_EN | B_AX_CMAC_TXEN | B_AX_CMAC_RXEN |
			  B_AX_FORCE_CMACREG_GCKEN | B_AX_PHYINTF_EN | B_AX_CMAC_DMA_EN |
			  B_AX_PTCLTOP_EN | B_AX_SCHEDULER_EN | B_AX_TMAC_EN |
			  B_AX_RMAC_EN);

	rtw89_write32_mask(rtwdev, R_AX_EECS_EESK_FUNC_SEL, B_AX_PINMUX_EESK_FUNC_SEL_MASK,
			   PINMUX_EESK_FUNC_SEL_BT_LOG);

	return 0;
}

static int rtw8852b_pwr_off_func(struct rtw89_dev *rtwdev)
{
	u32 val32;
	int ret;

	rtw8852b_pwr_sps_ana(rtwdev);

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, XTAL_SI_RFC2RF,
				      XTAL_SI_RFC2RF);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, XTAL_SI_OFF_EI);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, XTAL_SI_OFF_WEI);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S0, 0, XTAL_SI_RF00);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S1, 0, XTAL_SI_RF10);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, XTAL_SI_SRAM2RFC,
				      XTAL_SI_SRAM2RFC);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, XTAL_SI_PON_EI);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, XTAL_SI_PON_WEI);
	if (ret)
		return ret;

	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_EN_WLON);
	rtw89_write32_clr(rtwdev, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
	rtw89_write8_clr(rtwdev, R_AX_SYS_FUNC_EN, B_AX_FEN_BB_GLB_RSTN | B_AX_FEN_BBRSTB);
	rtw89_write32_clr(rtwdev, R_AX_SYS_ADIE_PAD_PWR_CTRL, B_AX_SYM_PADPDN_WL_RFC_1P3);

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, XTAL_SI_SHDN_WL);
	if (ret)
		return ret;

	rtw89_write32_clr(rtwdev, R_AX_SYS_ADIE_PAD_PWR_CTRL, B_AX_SYM_PADPDN_WL_PTA_1P3);

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, XTAL_SI_GND_SHDN_WL);
	if (ret)
		return ret;

	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_APFM_OFFMAC);

	ret = read_poll_timeout(rtw89_read32, val32, !(val32 & B_AX_APFM_OFFMAC),
				1000, 20000, false, rtwdev, R_AX_SYS_PW_CTRL);
	if (ret)
		return ret;

	rtw89_write32(rtwdev, R_AX_WLLPS_CTRL, SW_LPS_OPTION);
	rtw89_write32_set(rtwdev, R_AX_SYS_SWR_CTRL1, B_AX_SYM_CTRL_SPS_PWMFREQ);
	rtw89_write32_mask(rtwdev, R_AX_SPS_DIG_ON_CTRL0, B_AX_REG_ZCDC_H_MASK, 0x3);
	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_APFM_SWLPS);

	return 0;
}

static void rtw8852b_bb_reset_en(struct rtw89_dev *rtwdev, enum rtw89_band band,
				 enum rtw89_phy_idx phy_idx, bool en)
{
	if (en) {
		rtw89_phy_write32_idx(rtwdev, R_S0_HW_SI_DIS,
				      B_S0_HW_SI_DIS_W_R_TRIG, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_S1_HW_SI_DIS,
				      B_S1_HW_SI_DIS_W_R_TRIG, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1, phy_idx);
		if (band == RTW89_BAND_2G)
			rtw89_phy_write32_mask(rtwdev, R_RXCCA, B_RXCCA_DIS, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PD_CTRL, B_PD_HIT_DIS, 0x0);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_RXCCA, B_RXCCA_DIS, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PD_CTRL, B_PD_HIT_DIS, 0x1);
		rtw89_phy_write32_idx(rtwdev, R_S0_HW_SI_DIS,
				      B_S0_HW_SI_DIS_W_R_TRIG, 0x7, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_S1_HW_SI_DIS,
				      B_S1_HW_SI_DIS_W_R_TRIG, 0x7, phy_idx);
		fsleep(1);
		rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 0, phy_idx);
	}
}

static void rtw8852b_bb_reset(struct rtw89_dev *rtwdev,
			      enum rtw89_phy_idx phy_idx)
{
	rtw89_phy_write32_set(rtwdev, R_P0_TXPW_RSTB, B_P0_TXPW_RSTB_MANON);
	rtw89_phy_write32_set(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_TRK_EN);
	rtw89_phy_write32_set(rtwdev, R_P1_TXPW_RSTB, B_P1_TXPW_RSTB_MANON);
	rtw89_phy_write32_set(rtwdev, R_P1_TSSI_TRK, B_P1_TSSI_TRK_EN);
	rtw8852bx_bb_reset_all(rtwdev, phy_idx);
	rtw89_phy_write32_clr(rtwdev, R_P0_TXPW_RSTB, B_P0_TXPW_RSTB_MANON);
	rtw89_phy_write32_clr(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_TRK_EN);
	rtw89_phy_write32_clr(rtwdev, R_P1_TXPW_RSTB, B_P1_TXPW_RSTB_MANON);
	rtw89_phy_write32_clr(rtwdev, R_P1_TSSI_TRK, B_P1_TSSI_TRK_EN);
}

static void rtw8852b_set_channel(struct rtw89_dev *rtwdev,
				 const struct rtw89_chan *chan,
				 enum rtw89_mac_idx mac_idx,
				 enum rtw89_phy_idx phy_idx)
{
	rtw8852bx_set_channel_mac(rtwdev, chan, mac_idx);
	rtw8852bx_set_channel_bb(rtwdev, chan, phy_idx);
	rtw8852b_set_channel_rf(rtwdev, chan, phy_idx);
}

static void rtw8852b_tssi_cont_en(struct rtw89_dev *rtwdev, bool en,
				  enum rtw89_rf_path path)
{
	static const u32 tssi_trk[2] = {R_P0_TSSI_TRK, R_P1_TSSI_TRK};
	static const u32 ctrl_bbrst[2] = {R_P0_TXPW_RSTB, R_P1_TXPW_RSTB};

	if (en) {
		rtw89_phy_write32_mask(rtwdev, ctrl_bbrst[path], B_P0_TXPW_RSTB_MANON, 0x0);
		rtw89_phy_write32_mask(rtwdev, tssi_trk[path], B_P0_TSSI_TRK_EN, 0x0);
	} else {
		rtw89_phy_write32_mask(rtwdev, ctrl_bbrst[path], B_P0_TXPW_RSTB_MANON, 0x1);
		rtw89_phy_write32_mask(rtwdev, tssi_trk[path], B_P0_TSSI_TRK_EN, 0x1);
	}
}

static void rtw8852b_tssi_cont_en_phyidx(struct rtw89_dev *rtwdev, bool en,
					 u8 phy_idx)
{
	if (!rtwdev->dbcc_en) {
		rtw8852b_tssi_cont_en(rtwdev, en, RF_PATH_A);
		rtw8852b_tssi_cont_en(rtwdev, en, RF_PATH_B);
	} else {
		if (phy_idx == RTW89_PHY_0)
			rtw8852b_tssi_cont_en(rtwdev, en, RF_PATH_A);
		else
			rtw8852b_tssi_cont_en(rtwdev, en, RF_PATH_B);
	}
}

static void rtw8852b_adc_en(struct rtw89_dev *rtwdev, bool en)
{
	if (en)
		rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RST, 0x0);
	else
		rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RST, 0xf);
}

static void rtw8852b_set_channel_help(struct rtw89_dev *rtwdev, bool enter,
				      struct rtw89_channel_help_params *p,
				      const struct rtw89_chan *chan,
				      enum rtw89_mac_idx mac_idx,
				      enum rtw89_phy_idx phy_idx)
{
	if (enter) {
		rtw89_chip_stop_sch_tx(rtwdev, RTW89_MAC_0, &p->tx_en, RTW89_SCH_TX_SEL_ALL);
		rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, false);
		rtw8852b_tssi_cont_en_phyidx(rtwdev, false, RTW89_PHY_0);
		rtw8852b_adc_en(rtwdev, false);
		fsleep(40);
		rtw8852b_bb_reset_en(rtwdev, chan->band_type, phy_idx, false);
	} else {
		rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, true);
		rtw8852b_adc_en(rtwdev, true);
		rtw8852b_tssi_cont_en_phyidx(rtwdev, true, RTW89_PHY_0);
		rtw8852b_bb_reset_en(rtwdev, chan->band_type, phy_idx, true);
		rtw89_chip_resume_sch_tx(rtwdev, RTW89_MAC_0, p->tx_en);
	}
}

static void rtw8852b_rfk_init(struct rtw89_dev *rtwdev)
{
	rtwdev->is_tssi_mode[RF_PATH_A] = false;
	rtwdev->is_tssi_mode[RF_PATH_B] = false;

	rtw8852b_dpk_init(rtwdev);
	rtw8852b_rck(rtwdev);
	rtw8852b_dack(rtwdev, RTW89_CHANCTX_0);
	rtw8852b_rx_dck(rtwdev, RTW89_PHY_0, RTW89_CHANCTX_0);
}

static void rtw8852b_rfk_channel(struct rtw89_dev *rtwdev,
				 struct rtw89_vif_link *rtwvif_link)
{
	enum rtw89_chanctx_idx chanctx_idx = rtwvif_link->chanctx_idx;
	enum rtw89_phy_idx phy_idx = rtwvif_link->phy_idx;

	rtw89_btc_ntfy_conn_rfk(rtwdev, true);

	rtw8852b_rx_dck(rtwdev, phy_idx, chanctx_idx);
	rtw8852b_iqk(rtwdev, phy_idx, chanctx_idx);
	rtw89_btc_ntfy_preserve_bt_time(rtwdev, 30);
	rtw8852b_tssi(rtwdev, phy_idx, true, chanctx_idx);
	rtw89_btc_ntfy_preserve_bt_time(rtwdev, 30);
	rtw8852b_dpk(rtwdev, phy_idx, chanctx_idx);

	rtw89_btc_ntfy_conn_rfk(rtwdev, false);
}

static void rtw8852b_rfk_band_changed(struct rtw89_dev *rtwdev,
				      enum rtw89_phy_idx phy_idx,
				      const struct rtw89_chan *chan)
{
	rtw8852b_tssi_scan(rtwdev, phy_idx, chan);
}

static void rtw8852b_rfk_scan(struct rtw89_dev *rtwdev,
			      struct rtw89_vif_link *rtwvif_link,
			      bool start)
{
	rtw8852b_wifi_scan_notify(rtwdev, start, rtwvif_link->phy_idx,
				  rtwvif_link->chanctx_idx);
}

static void rtw8852b_rfk_track(struct rtw89_dev *rtwdev)
{
	rtw8852b_dpk_track(rtwdev);
}

static void rtw8852b_btc_set_rfe(struct rtw89_dev *rtwdev)
{
	const struct rtw89_btc_ver *ver = rtwdev->btc.ver;
	union rtw89_btc_module_info *md = &rtwdev->btc.mdinfo;

	if (ver->fcxinit == 7) {
		md->md_v7.rfe_type = rtwdev->efuse.rfe_type;
		md->md_v7.kt_ver = rtwdev->hal.cv;
		md->md_v7.bt_solo = 0;
		md->md_v7.switch_type = BTC_SWITCH_INTERNAL;

		if (md->md_v7.rfe_type > 0)
			md->md_v7.ant.num = (md->md_v7.rfe_type % 2 ? 2 : 3);
		else
			md->md_v7.ant.num = 2;

		md->md_v7.ant.diversity = 0;
		md->md_v7.ant.isolation = 10;

		if (md->md_v7.ant.num == 3) {
			md->md_v7.ant.type = BTC_ANT_DEDICATED;
			md->md_v7.bt_pos = BTC_BT_ALONE;
		} else {
			md->md_v7.ant.type = BTC_ANT_SHARED;
			md->md_v7.bt_pos = BTC_BT_BTG;
		}
		rtwdev->btc.btg_pos = md->md_v7.ant.btg_pos;
		rtwdev->btc.ant_type = md->md_v7.ant.type;
	} else {
		md->md.rfe_type = rtwdev->efuse.rfe_type;
		md->md.cv = rtwdev->hal.cv;
		md->md.bt_solo = 0;
		md->md.switch_type = BTC_SWITCH_INTERNAL;

		if (md->md.rfe_type > 0)
			md->md.ant.num = (md->md.rfe_type % 2 ? 2 : 3);
		else
			md->md.ant.num = 2;

		md->md.ant.diversity = 0;
		md->md.ant.isolation = 10;

		if (md->md.ant.num == 3) {
			md->md.ant.type = BTC_ANT_DEDICATED;
			md->md.bt_pos = BTC_BT_ALONE;
		} else {
			md->md.ant.type = BTC_ANT_SHARED;
			md->md.bt_pos = BTC_BT_BTG;
		}
		rtwdev->btc.btg_pos = md->md.ant.btg_pos;
		rtwdev->btc.ant_type = md->md.ant.type;
	}
}

union rtw8852b_btc_wl_txpwr_ctrl {
	u32 txpwr_val;
	struct {
		union {
			u16 ctrl_all_time;
			struct {
				s16 data:9;
				u16 rsvd:6;
				u16 flag:1;
			} all_time;
		};
		union {
			u16 ctrl_gnt_bt;
			struct {
				s16 data:9;
				u16 rsvd:7;
			} gnt_bt;
		};
	};
} __packed;

static void
rtw8852b_btc_set_wl_txpwr_ctrl(struct rtw89_dev *rtwdev, u32 txpwr_val)
{
	union rtw8852b_btc_wl_txpwr_ctrl arg = { .txpwr_val = txpwr_val };
	s32 val;

#define __write_ctrl(_reg, _msk, _val, _en, _cond)		\
do {								\
	u32 _wrt = FIELD_PREP(_msk, _val);			\
	BUILD_BUG_ON(!!(_msk & _en));				\
	if (_cond)						\
		_wrt |= _en;					\
	else							\
		_wrt &= ~_en;					\
	rtw89_mac_txpwr_write32_mask(rtwdev, RTW89_PHY_0, _reg,	\
				     _msk | _en, _wrt);		\
} while (0)

	switch (arg.ctrl_all_time) {
	case 0xffff:
		val = 0;
		break;
	default:
		val = arg.all_time.data;
		break;
	}

	__write_ctrl(R_AX_PWR_RATE_CTRL, B_AX_FORCE_PWR_BY_RATE_VALUE_MASK,
		     val, B_AX_FORCE_PWR_BY_RATE_EN,
		     arg.ctrl_all_time != 0xffff);

	switch (arg.ctrl_gnt_bt) {
	case 0xffff:
		val = 0;
		break;
	default:
		val = arg.gnt_bt.data;
		break;
	}

	__write_ctrl(R_AX_PWR_COEXT_CTRL, B_AX_TXAGC_BT_MASK, val,
		     B_AX_TXAGC_BT_EN, arg.ctrl_gnt_bt != 0xffff);

#undef __write_ctrl
}

static const struct rtw89_chip_ops rtw8852b_chip_ops = {
	.enable_bb_rf		= rtw8852bx_mac_enable_bb_rf,
	.disable_bb_rf		= rtw8852bx_mac_disable_bb_rf,
	.bb_preinit		= NULL,
	.bb_postinit		= NULL,
	.bb_reset		= rtw8852b_bb_reset,
	.bb_sethw		= rtw8852bx_bb_sethw,
	.read_rf		= rtw89_phy_read_rf_v1,
	.write_rf		= rtw89_phy_write_rf_v1,
	.set_channel		= rtw8852b_set_channel,
	.set_channel_help	= rtw8852b_set_channel_help,
	.read_efuse		= rtw8852bx_read_efuse,
	.read_phycap		= rtw8852bx_read_phycap,
	.fem_setup		= NULL,
	.rfe_gpio		= NULL,
	.rfk_hw_init		= NULL,
	.rfk_init		= rtw8852b_rfk_init,
	.rfk_init_late		= NULL,
	.rfk_channel		= rtw8852b_rfk_channel,
	.rfk_band_changed	= rtw8852b_rfk_band_changed,
	.rfk_scan		= rtw8852b_rfk_scan,
	.rfk_track		= rtw8852b_rfk_track,
	.power_trim		= rtw8852bx_power_trim,
	.set_txpwr		= rtw8852bx_set_txpwr,
	.set_txpwr_ctrl		= rtw8852bx_set_txpwr_ctrl,
	.init_txpwr_unit	= rtw8852bx_init_txpwr_unit,
	.get_thermal		= rtw8852bx_get_thermal,
	.chan_to_rf18_val	= NULL,
	.ctrl_btg_bt_rx		= rtw8852bx_ctrl_btg_bt_rx,
	.query_ppdu		= rtw8852bx_query_ppdu,
	.convert_rpl_to_rssi	= rtw8852bx_convert_rpl_to_rssi,
	.phy_rpt_to_rssi	= NULL,
	.ctrl_nbtg_bt_tx	= rtw8852bx_ctrl_nbtg_bt_tx,
	.cfg_txrx_path		= rtw8852bx_bb_cfg_txrx_path,
	.set_txpwr_ul_tb_offset	= rtw8852bx_set_txpwr_ul_tb_offset,
	.digital_pwr_comp	= NULL,
	.pwr_on_func		= rtw8852b_pwr_on_func,
	.pwr_off_func		= rtw8852b_pwr_off_func,
	.query_rxdesc		= rtw89_core_query_rxdesc,
	.fill_txdesc		= rtw89_core_fill_txdesc,
	.fill_txdesc_fwcmd	= rtw89_core_fill_txdesc,
	.cfg_ctrl_path		= rtw89_mac_cfg_ctrl_path,
	.mac_cfg_gnt		= rtw89_mac_cfg_gnt,
	.stop_sch_tx		= rtw89_mac_stop_sch_tx,
	.resume_sch_tx		= rtw89_mac_resume_sch_tx,
	.h2c_dctl_sec_cam	= NULL,
	.h2c_default_cmac_tbl	= rtw89_fw_h2c_default_cmac_tbl,
	.h2c_assoc_cmac_tbl	= rtw89_fw_h2c_assoc_cmac_tbl,
	.h2c_ampdu_cmac_tbl	= NULL,
	.h2c_txtime_cmac_tbl	= rtw89_fw_h2c_txtime_cmac_tbl,
	.h2c_default_dmac_tbl	= NULL,
	.h2c_update_beacon	= rtw89_fw_h2c_update_beacon,
	.h2c_ba_cam		= rtw89_fw_h2c_ba_cam,

	.btc_set_rfe		= rtw8852b_btc_set_rfe,
	.btc_init_cfg		= rtw8852bx_btc_init_cfg,
	.btc_set_wl_pri		= rtw8852bx_btc_set_wl_pri,
	.btc_set_wl_txpwr_ctrl	= rtw8852b_btc_set_wl_txpwr_ctrl,
	.btc_get_bt_rssi	= rtw8852bx_btc_get_bt_rssi,
	.btc_update_bt_cnt	= rtw8852bx_btc_update_bt_cnt,
	.btc_wl_s1_standby	= rtw8852bx_btc_wl_s1_standby,
	.btc_set_wl_rx_gain	= rtw8852bx_btc_set_wl_rx_gain,
	.btc_set_policy		= rtw89_btc_set_policy_v1,
};

#ifdef CONFIG_PM
static const struct wiphy_wowlan_support rtw_wowlan_stub_8852b = {
	.flags = WIPHY_WOWLAN_MAGIC_PKT | WIPHY_WOWLAN_DISCONNECT,
	.n_patterns = RTW89_MAX_PATTERN_NUM,
	.pattern_max_len = RTW89_MAX_PATTERN_SIZE,
	.pattern_min_len = 1,
};
#endif

const struct rtw89_chip_info rtw8852b_chip_info = {
	.chip_id		= RTL8852B,
	.chip_gen		= RTW89_CHIP_AX,
	.ops			= &rtw8852b_chip_ops,
	.mac_def		= &rtw89_mac_gen_ax,
	.phy_def		= &rtw89_phy_gen_ax,
	.fw_basename		= RTW8852B_FW_BASENAME,
	.fw_format_max		= RTW8852B_FW_FORMAT_MAX,
	.try_ce_fw		= true,
	.bbmcu_nr		= 0,
	.needed_fw_elms		= 0,
	.fw_blacklist		= &rtw89_fw_blacklist_default,
	.fifo_size		= 196608,
	.small_fifo_size	= true,
	.dle_scc_rsvd_size	= 98304,
	.max_amsdu_limit	= 5000,
	.dis_2g_40m_ul_ofdma	= true,
	.rsvd_ple_ofst		= 0x2f800,
	.hfc_param_ini		= {rtw8852b_hfc_param_ini_pcie, NULL, NULL},
	.dle_mem		= {rtw8852b_dle_mem_pcie, NULL, NULL, NULL},
	.wde_qempty_acq_grpnum	= 4,
	.wde_qempty_mgq_grpsel	= 4,
	.rf_base_addr		= {0xe000, 0xf000},
	.thermal_th		= {0x32, 0x35},
	.pwr_on_seq		= NULL,
	.pwr_off_seq		= NULL,
	.bb_table		= &rtw89_8852b_phy_bb_table,
	.bb_gain_table		= &rtw89_8852b_phy_bb_gain_table,
	.rf_table		= {&rtw89_8852b_phy_radioa_table,
				   &rtw89_8852b_phy_radiob_table,},
	.nctl_table		= &rtw89_8852b_phy_nctl_table,
	.nctl_post_table	= NULL,
	.dflt_parms		= &rtw89_8852b_dflt_parms,
	.rfe_parms_conf		= NULL,
	.txpwr_factor_bb	= 3,
	.txpwr_factor_rf	= 2,
	.txpwr_factor_mac	= 1,
	.dig_table		= NULL,
	.dig_regs		= &rtw8852b_dig_regs,
	.tssi_dbw_table		= NULL,
	.support_macid_num	= RTW89_MAX_MAC_ID_NUM,
	.support_link_num	= 0,
	.support_chanctx_num	= 0,
	.support_rnr		= false,
	.support_bands		= BIT(NL80211_BAND_2GHZ) |
				  BIT(NL80211_BAND_5GHZ),
	.support_bandwidths	= BIT(NL80211_CHAN_WIDTH_20) |
				  BIT(NL80211_CHAN_WIDTH_40) |
				  BIT(NL80211_CHAN_WIDTH_80),
	.support_unii4		= true,
	.support_ant_gain	= true,
	.support_tas		= false,
	.support_sar_by_ant	= true,
	.ul_tb_waveform_ctrl	= true,
	.ul_tb_pwr_diff		= false,
	.rx_freq_frome_ie	= true,
	.hw_sec_hdr		= false,
	.hw_mgmt_tx_encrypt	= false,
	.hw_tkip_crypto		= false,
	.hw_mlo_bmc_crypto	= false,
	.rf_path_num		= 2,
	.tx_nss			= 2,
	.rx_nss			= 2,
	.acam_num		= 128,
	.bcam_num		= 10,
	.scam_num		= 128,
	.bacam_num		= 2,
	.bacam_dynamic_num	= 4,
	.bacam_ver		= RTW89_BACAM_V0,
	.ppdu_max_usr		= 4,
	.sec_ctrl_efuse_size	= 4,
	.physical_efuse_size	= 1216,
	.logical_efuse_size	= 2048,
	.limit_efuse_size	= 1280,
	.dav_phy_efuse_size	= 96,
	.dav_log_efuse_size	= 16,
	.efuse_blocks		= NULL,
	.phycap_addr		= 0x580,
	.phycap_size		= 128,
	.para_ver		= 0,
	.wlcx_desired		= 0x05050000,
	.scbd			= 0x1,
	.mailbox		= 0x1,

	.afh_guard_ch		= 6,
	.wl_rssi_thres		= rtw89_btc_8852b_wl_rssi_thres,
	.bt_rssi_thres		= rtw89_btc_8852b_bt_rssi_thres,
	.rssi_tol		= 2,
	.mon_reg_num		= ARRAY_SIZE(rtw89_btc_8852b_mon_reg),
	.mon_reg		= rtw89_btc_8852b_mon_reg,
	.rf_para_ulink_num	= ARRAY_SIZE(rtw89_btc_8852b_rf_ul),
	.rf_para_ulink		= rtw89_btc_8852b_rf_ul,
	.rf_para_dlink_num	= ARRAY_SIZE(rtw89_btc_8852b_rf_dl),
	.rf_para_dlink		= rtw89_btc_8852b_rf_dl,
	.ps_mode_supported	= BIT(RTW89_PS_MODE_RFOFF) |
				  BIT(RTW89_PS_MODE_CLK_GATED) |
				  BIT(RTW89_PS_MODE_PWR_GATED),
	.low_power_hci_modes	= 0,
	.h2c_cctl_func_id	= H2C_FUNC_MAC_CCTLINFO_UD,
	.hci_func_en_addr	= R_AX_HCI_FUNC_EN,
	.h2c_desc_size		= sizeof(struct rtw89_txwd_body),
	.txwd_body_size		= sizeof(struct rtw89_txwd_body),
	.txwd_info_size		= sizeof(struct rtw89_txwd_info),
	.h2c_ctrl_reg		= R_AX_H2CREG_CTRL,
	.h2c_counter_reg	= {R_AX_UDM1 + 1, B_AX_UDM1_HALMAC_H2C_DEQ_CNT_MASK >> 8},
	.h2c_regs		= rtw8852b_h2c_regs,
	.c2h_ctrl_reg		= R_AX_C2HREG_CTRL,
	.c2h_counter_reg	= {R_AX_UDM1 + 1, B_AX_UDM1_HALMAC_C2H_ENQ_CNT_MASK >> 8},
	.c2h_regs		= rtw8852b_c2h_regs,
	.page_regs		= &rtw8852b_page_regs,
	.wow_reason_reg		= rtw8852b_wow_wakeup_regs,
	.cfo_src_fd		= true,
	.cfo_hw_comp		= true,
	.dcfo_comp		= &rtw8852b_dcfo_comp,
	.dcfo_comp_sft		= 10,
	.imr_info		= &rtw8852b_imr_info,
	.imr_dmac_table		= NULL,
	.imr_cmac_table		= NULL,
	.rrsr_cfgs		= &rtw8852b_rrsr_cfgs,
	.bss_clr_vld		= {R_BSS_CLR_MAP_V1, B_BSS_CLR_MAP_VLD0},
	.bss_clr_map_reg	= R_BSS_CLR_MAP_V1,
	.rfkill_init		= &rtw8852b_rfkill_regs,
	.rfkill_get		= {R_AX_GPIO_EXT_CTRL, B_AX_GPIO_IN_9},
	.dma_ch_mask		= BIT(RTW89_DMA_ACH4) | BIT(RTW89_DMA_ACH5) |
				  BIT(RTW89_DMA_ACH6) | BIT(RTW89_DMA_ACH7) |
				  BIT(RTW89_DMA_B1MG) | BIT(RTW89_DMA_B1HI),
	.edcca_regs		= &rtw8852b_edcca_regs,
#ifdef CONFIG_PM
	.wowlan_stub		= &rtw_wowlan_stub_8852b,
#endif
	.xtal_info		= NULL,
};
EXPORT_SYMBOL(rtw8852b_chip_info);

MODULE_FIRMWARE(RTW8852B_MODULE_FIRMWARE);
MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ax wireless 8852B driver");
MODULE_LICENSE("Dual BSD/GPL");
