// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2022-2023  Realtek Corporation
 */

#include "coex.h"
#include "efuse.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"
#include "rtw8851b.h"
#include "rtw8851b_rfk.h"
#include "rtw8851b_rfk_table.h"
#include "rtw8851b_table.h"
#include "txrx.h"
#include "util.h"

#define RTW8851B_FW_FORMAT_MAX 0
#define RTW8851B_FW_BASENAME "rtw89/rtw8851b_fw"
#define RTW8851B_MODULE_FIRMWARE \
	RTW8851B_FW_BASENAME ".bin"

static const struct rtw89_hfc_ch_cfg rtw8851b_hfc_chcfg_pcie[] = {
	{5, 343, grp_0}, /* ACH 0 */
	{5, 343, grp_0}, /* ACH 1 */
	{5, 343, grp_0}, /* ACH 2 */
	{5, 343, grp_0}, /* ACH 3 */
	{0, 0, grp_0}, /* ACH 4 */
	{0, 0, grp_0}, /* ACH 5 */
	{0, 0, grp_0}, /* ACH 6 */
	{0, 0, grp_0}, /* ACH 7 */
	{4, 344, grp_0}, /* B0MGQ */
	{4, 344, grp_0}, /* B0HIQ */
	{0, 0, grp_0}, /* B1MGQ */
	{0, 0, grp_0}, /* B1HIQ */
	{40, 0, 0} /* FWCMDQ */
};

static const struct rtw89_hfc_pub_cfg rtw8851b_hfc_pubcfg_pcie = {
	448, /* Group 0 */
	0, /* Group 1 */
	448, /* Public Max */
	0 /* WP threshold */
};

static const struct rtw89_hfc_param_ini rtw8851b_hfc_param_ini_pcie[] = {
	[RTW89_QTA_SCC] = {rtw8851b_hfc_chcfg_pcie, &rtw8851b_hfc_pubcfg_pcie,
			   &rtw89_mac_size.hfc_preccfg_pcie, RTW89_HCIFC_POH},
	[RTW89_QTA_DLFW] = {NULL, NULL, &rtw89_mac_size.hfc_preccfg_pcie,
			    RTW89_HCIFC_POH},
	[RTW89_QTA_INVALID] = {NULL},
};

static const struct rtw89_dle_mem rtw8851b_dle_mem_pcie[] = {
	[RTW89_QTA_SCC] = {RTW89_QTA_SCC, &rtw89_mac_size.wde_size6,
			   &rtw89_mac_size.ple_size6, &rtw89_mac_size.wde_qt6,
			   &rtw89_mac_size.wde_qt6, &rtw89_mac_size.ple_qt18,
			   &rtw89_mac_size.ple_qt58},
	[RTW89_QTA_WOW] = {RTW89_QTA_WOW, &rtw89_mac_size.wde_size6,
			   &rtw89_mac_size.ple_size6, &rtw89_mac_size.wde_qt6,
			   &rtw89_mac_size.wde_qt6, &rtw89_mac_size.ple_qt18,
			   &rtw89_mac_size.ple_qt_51b_wow},
	[RTW89_QTA_DLFW] = {RTW89_QTA_DLFW, &rtw89_mac_size.wde_size9,
			    &rtw89_mac_size.ple_size8, &rtw89_mac_size.wde_qt4,
			    &rtw89_mac_size.wde_qt4, &rtw89_mac_size.ple_qt13,
			    &rtw89_mac_size.ple_qt13},
	[RTW89_QTA_INVALID] = {RTW89_QTA_INVALID, NULL, NULL, NULL, NULL, NULL,
			       NULL},
};

static const struct rtw89_reg3_def rtw8851b_btc_preagc_en_defs[] = {
	{0x46D0, GENMASK(1, 0), 0x3},
	{0x4AD4, GENMASK(31, 0), 0xf},
	{0x4688, GENMASK(23, 16), 0x80},
	{0x4688, GENMASK(31, 24), 0x80},
	{0x4694, GENMASK(7, 0), 0x80},
	{0x4694, GENMASK(15, 8), 0x80},
	{0x4AE4, GENMASK(11, 6), 0x34},
	{0x4AE4, GENMASK(17, 12), 0x0},
	{0x469C, GENMASK(31, 26), 0x34},
};

static DECLARE_PHY_REG3_TBL(rtw8851b_btc_preagc_en_defs);

static const struct rtw89_reg3_def rtw8851b_btc_preagc_dis_defs[] = {
	{0x46D0, GENMASK(1, 0), 0x0},
	{0x4AD4, GENMASK(31, 0), 0x60},
	{0x4688, GENMASK(23, 16), 0x10},
	{0x4690, GENMASK(31, 24), 0x2a},
	{0x4694, GENMASK(15, 8), 0x2a},
	{0x4AE4, GENMASK(11, 6), 0x26},
	{0x4AE4, GENMASK(17, 12), 0x1e},
	{0x469C, GENMASK(31, 26), 0x26},
};

static DECLARE_PHY_REG3_TBL(rtw8851b_btc_preagc_dis_defs);

static const u32 rtw8851b_h2c_regs[RTW89_H2CREG_MAX] = {
	R_AX_H2CREG_DATA0, R_AX_H2CREG_DATA1,  R_AX_H2CREG_DATA2,
	R_AX_H2CREG_DATA3
};

static const u32 rtw8851b_c2h_regs[RTW89_C2HREG_MAX] = {
	R_AX_C2HREG_DATA0, R_AX_C2HREG_DATA1, R_AX_C2HREG_DATA2,
	R_AX_C2HREG_DATA3
};

static const u32 rtw8851b_wow_wakeup_regs[RTW89_WOW_REASON_NUM] = {
	R_AX_C2HREG_DATA3 + 3, R_AX_C2HREG_DATA3 + 3,
};

static const struct rtw89_page_regs rtw8851b_page_regs = {
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

static const struct rtw89_reg_def rtw8851b_dcfo_comp = {
	R_DCFO_COMP_S0_V2, B_DCFO_COMP_S0_MSK_V2
};

static const struct rtw89_imr_info rtw8851b_imr_info = {
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

static const struct rtw89_xtal_info rtw8851b_xtal_info = {
	.xcap_reg		= R_AX_XTAL_ON_CTRL3,
	.sc_xo_mask		= B_AX_XTAL_SC_XO_A_BLOCK_MASK,
	.sc_xi_mask		= B_AX_XTAL_SC_XI_A_BLOCK_MASK,
};

static const struct rtw89_rrsr_cfgs rtw8851b_rrsr_cfgs = {
	.ref_rate = {R_AX_TRXPTCL_RRSR_CTL_0, B_AX_WMAC_RESP_REF_RATE_SEL, 0},
	.rsc = {R_AX_TRXPTCL_RRSR_CTL_0, B_AX_WMAC_RESP_RSC_MASK, 2},
};

static const struct rtw89_rfkill_regs rtw8851b_rfkill_regs = {
	.pinmux = {R_AX_GPIO8_15_FUNC_SEL,
		   B_AX_PINMUX_GPIO9_FUNC_SEL_MASK,
		   0xf},
	.mode = {R_AX_GPIO_EXT_CTRL + 2,
		 (B_AX_GPIO_MOD_9 | B_AX_GPIO_IO_SEL_9) >> 16,
		 0x0},
};

static const struct rtw89_dig_regs rtw8851b_dig_regs = {
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

static const struct rtw89_edcca_regs rtw8851b_edcca_regs = {
	.edcca_level			= R_SEG0R_EDCCA_LVL_V1,
	.edcca_mask			= B_EDCCA_LVL_MSK0,
	.edcca_p_mask			= B_EDCCA_LVL_MSK1,
	.ppdu_level			= R_SEG0R_EDCCA_LVL_V1,
	.ppdu_mask			= B_EDCCA_LVL_MSK3,
	.rpt_a				= R_EDCCA_RPT_A,
	.rpt_b				= R_EDCCA_RPT_B,
	.rpt_sel			= R_EDCCA_RPT_SEL,
	.rpt_sel_mask			= B_EDCCA_RPT_SEL_MSK,
	.tx_collision_t2r_st		= R_TX_COLLISION_T2R_ST,
	.tx_collision_t2r_st_mask	= B_TX_COLLISION_T2R_ST_M,
};

static const struct rtw89_btc_rf_trx_para rtw89_btc_8851b_rf_ul[] = {
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

static const struct rtw89_btc_rf_trx_para rtw89_btc_8851b_rf_dl[] = {
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

static const struct rtw89_btc_fbtc_mreg rtw89_btc_8851b_mon_reg[] = {
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

static const u8 rtw89_btc_8851b_wl_rssi_thres[BTC_WL_RSSI_THMAX] = {70, 60, 50, 40};
static const u8 rtw89_btc_8851b_bt_rssi_thres[BTC_BT_RSSI_THMAX] = {50, 40, 30, 20};

static int rtw8851b_pwr_on_func(struct rtw89_dev *rtwdev)
{
	u32 val32;
	u8 val8;
	u32 ret;

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
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_XTAL_DRV, 0, XTAL_SI_DRV_LATCH);
	if (ret)
		return ret;

	rtw89_write32_set(rtwdev, R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);
	rtw89_write32_set(rtwdev, R_AX_SYS_ISO_CTRL, B_AX_ISO_EB2CORE);
	rtw89_write32_clr(rtwdev, R_AX_SYS_ISO_CTRL, B_AX_PWC_EV2EF_B15);

	fsleep(1000);

	rtw89_write32_clr(rtwdev, R_AX_SYS_ISO_CTRL, B_AX_PWC_EV2EF_B14);
	rtw89_write32_clr(rtwdev, R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);
	rtw89_write32_set(rtwdev, R_AX_GPIO0_16_EECS_EESK_LED1_PULL_LOW_EN,
			  B_AX_GPIO10_PULL_LOW_EN | B_AX_GPIO16_PULL_LOW_EN_V1);

	if (rtwdev->hal.cv == CHIP_CAV) {
		ret = rtw89_read_efuse_ver(rtwdev, &val8);
		if (!ret)
			rtwdev->hal.cv = val8;
	}

	rtw89_write32_clr(rtwdev, R_AX_WLAN_XTAL_SI_CONFIG,
			  B_AX_XTAL_SI_ADDR_NOT_CHK);
	if (rtwdev->hal.cv != CHIP_CAV) {
		rtw89_write32_set(rtwdev, R_AX_SPSLDO_ON_CTRL1, B_AX_FPWMDELAY);
		rtw89_write32_set(rtwdev, R_AX_SPSANA_ON_CTRL1, B_AX_FPWMDELAY);
	}

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

static void rtw8851b_patch_swr_pfm2pwm(struct rtw89_dev *rtwdev)
{
	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_SOP_PWMM_DSWR);
	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_SOP_ASWRM);
	rtw89_write32_set(rtwdev, R_AX_WLLPS_CTRL, B_AX_LPSOP_DSWRM);
	rtw89_write32_set(rtwdev, R_AX_WLLPS_CTRL, B_AX_LPSOP_ASWRM);
}

static int rtw8851b_pwr_off_func(struct rtw89_dev *rtwdev)
{
	u32 val32;
	u32 ret;

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

	rtw89_write32_set(rtwdev, R_AX_WLAN_XTAL_SI_CONFIG,
			  B_AX_XTAL_SI_ADDR_NOT_CHK);
	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_EN_WLON);
	rtw89_write32_clr(rtwdev, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
	rtw89_write8_clr(rtwdev, R_AX_SYS_FUNC_EN, B_AX_FEN_BB_GLB_RSTN | B_AX_FEN_BBRSTB);

	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_APFM_OFFMAC);

	ret = read_poll_timeout(rtw89_read32, val32, !(val32 & B_AX_APFM_OFFMAC),
				1000, 20000, false, rtwdev, R_AX_SYS_PW_CTRL);
	if (ret)
		return ret;

	rtw89_write32(rtwdev, R_AX_WLLPS_CTRL, SW_LPS_OPTION);

	if (rtwdev->hal.cv == CHIP_CAV) {
		rtw8851b_patch_swr_pfm2pwm(rtwdev);
	} else {
		rtw89_write32_set(rtwdev, R_AX_SPSLDO_ON_CTRL1, B_AX_FPWMDELAY);
		rtw89_write32_set(rtwdev, R_AX_SPSANA_ON_CTRL1, B_AX_FPWMDELAY);
	}

	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_APFM_SWLPS);

	return 0;
}

static void rtw8851b_efuse_parsing(struct rtw89_efuse *efuse,
				   struct rtw8851b_efuse *map)
{
	ether_addr_copy(efuse->addr, map->e.mac_addr);
	efuse->rfe_type = map->rfe_type;
	efuse->xtal_cap = map->xtal_k;
}

static void rtw8851b_efuse_parsing_tssi(struct rtw89_dev *rtwdev,
					struct rtw8851b_efuse *map)
{
	struct rtw89_tssi_info *tssi = &rtwdev->tssi;
	struct rtw8851b_tssi_offset *ofst[] = {&map->path_a_tssi};
	u8 i, j;

	tssi->thermal[RF_PATH_A] = map->path_a_therm;

	for (i = 0; i < RF_PATH_NUM_8851B; i++) {
		memcpy(tssi->tssi_cck[i], ofst[i]->cck_tssi,
		       sizeof(ofst[i]->cck_tssi));

		for (j = 0; j < TSSI_CCK_CH_GROUP_NUM; j++)
			rtw89_debug(rtwdev, RTW89_DBG_TSSI,
				    "[TSSI][EFUSE] path=%d cck[%d]=0x%x\n",
				    i, j, tssi->tssi_cck[i][j]);

		memcpy(tssi->tssi_mcs[i], ofst[i]->bw40_tssi,
		       sizeof(ofst[i]->bw40_tssi));
		memcpy(tssi->tssi_mcs[i] + TSSI_MCS_2G_CH_GROUP_NUM,
		       ofst[i]->bw40_1s_tssi_5g, sizeof(ofst[i]->bw40_1s_tssi_5g));

		for (j = 0; j < TSSI_MCS_CH_GROUP_NUM; j++)
			rtw89_debug(rtwdev, RTW89_DBG_TSSI,
				    "[TSSI][EFUSE] path=%d mcs[%d]=0x%x\n",
				    i, j, tssi->tssi_mcs[i][j]);
	}
}

static bool _decode_efuse_gain(u8 data, s8 *high, s8 *low)
{
	if (high)
		*high = sign_extend32(u8_get_bits(data, GENMASK(7,  4)), 3);
	if (low)
		*low = sign_extend32(u8_get_bits(data, GENMASK(3,  0)), 3);

	return data != 0xff;
}

static void rtw8851b_efuse_parsing_gain_offset(struct rtw89_dev *rtwdev,
					       struct rtw8851b_efuse *map)
{
	struct rtw89_phy_efuse_gain *gain = &rtwdev->efuse_gain;
	bool valid = false;

	valid |= _decode_efuse_gain(map->rx_gain_2g_cck,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_2G_CCK],
				    NULL);
	valid |= _decode_efuse_gain(map->rx_gain_2g_ofdm,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_2G_OFDM],
				    NULL);
	valid |= _decode_efuse_gain(map->rx_gain_5g_low,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_5G_LOW],
				    NULL);
	valid |= _decode_efuse_gain(map->rx_gain_5g_mid,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_5G_MID],
				   NULL);
	valid |= _decode_efuse_gain(map->rx_gain_5g_high,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_5G_HIGH],
				    NULL);

	gain->offset_valid = valid;
}

static int rtw8851b_read_efuse(struct rtw89_dev *rtwdev, u8 *log_map,
			       enum rtw89_efuse_block block)
{
	struct rtw89_efuse *efuse = &rtwdev->efuse;
	struct rtw8851b_efuse *map;

	map = (struct rtw8851b_efuse *)log_map;

	efuse->country_code[0] = map->country_code[0];
	efuse->country_code[1] = map->country_code[1];
	rtw8851b_efuse_parsing_tssi(rtwdev, map);
	rtw8851b_efuse_parsing_gain_offset(rtwdev, map);

	switch (rtwdev->hci.type) {
	case RTW89_HCI_TYPE_PCIE:
		rtw8851b_efuse_parsing(efuse, map);
		break;
	default:
		return -EOPNOTSUPP;
	}

	rtw89_info(rtwdev, "chip rfe_type is %d\n", efuse->rfe_type);

	return 0;
}

static void rtw8851b_phycap_parsing_tssi(struct rtw89_dev *rtwdev, u8 *phycap_map)
{
	struct rtw89_tssi_info *tssi = &rtwdev->tssi;
	static const u32 tssi_trim_addr[RF_PATH_NUM_8851B] = {0x5D6};
	u32 addr = rtwdev->chip->phycap_addr;
	bool pg = false;
	u32 ofst;
	u8 i, j;

	for (i = 0; i < RF_PATH_NUM_8851B; i++) {
		for (j = 0; j < TSSI_TRIM_CH_GROUP_NUM; j++) {
			/* addrs are in decreasing order */
			ofst = tssi_trim_addr[i] - addr - j;
			tssi->tssi_trim[i][j] = phycap_map[ofst];

			if (phycap_map[ofst] != 0xff)
				pg = true;
		}
	}

	if (!pg) {
		memset(tssi->tssi_trim, 0, sizeof(tssi->tssi_trim));
		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM] no PG, set all trim info to 0\n");
	}

	for (i = 0; i < RF_PATH_NUM_8851B; i++)
		for (j = 0; j < TSSI_TRIM_CH_GROUP_NUM; j++)
			rtw89_debug(rtwdev, RTW89_DBG_TSSI,
				    "[TSSI] path=%d idx=%d trim=0x%x addr=0x%x\n",
				    i, j, tssi->tssi_trim[i][j],
				    tssi_trim_addr[i] - j);
}

static void rtw8851b_phycap_parsing_thermal_trim(struct rtw89_dev *rtwdev,
						 u8 *phycap_map)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	static const u32 thm_trim_addr[RF_PATH_NUM_8851B] = {0x5DF};
	u32 addr = rtwdev->chip->phycap_addr;
	u8 i;

	for (i = 0; i < RF_PATH_NUM_8851B; i++) {
		info->thermal_trim[i] = phycap_map[thm_trim_addr[i] - addr];

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[THERMAL][TRIM] path=%d thermal_trim=0x%x\n",
			    i, info->thermal_trim[i]);

		if (info->thermal_trim[i] != 0xff)
			info->pg_thermal_trim = true;
	}
}

static void rtw8851b_thermal_trim(struct rtw89_dev *rtwdev)
{
#define __thm_setting(raw)				\
({							\
	u8 __v = (raw);					\
	((__v & 0x1) << 3) | ((__v & 0x1f) >> 1);	\
})
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	u8 i, val;

	if (!info->pg_thermal_trim) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[THERMAL][TRIM] no PG, do nothing\n");

		return;
	}

	for (i = 0; i < RF_PATH_NUM_8851B; i++) {
		val = __thm_setting(info->thermal_trim[i]);
		rtw89_write_rf(rtwdev, i, RR_TM2, RR_TM2_OFF, val);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[THERMAL][TRIM] path=%d thermal_setting=0x%x\n",
			    i, val);
	}
#undef __thm_setting
}

static void rtw8851b_phycap_parsing_pa_bias_trim(struct rtw89_dev *rtwdev,
						 u8 *phycap_map)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	static const u32 pabias_trim_addr[] = {0x5DE};
	u32 addr = rtwdev->chip->phycap_addr;
	u8 i;

	for (i = 0; i < RF_PATH_NUM_8851B; i++) {
		info->pa_bias_trim[i] = phycap_map[pabias_trim_addr[i] - addr];

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PA_BIAS][TRIM] path=%d pa_bias_trim=0x%x\n",
			    i, info->pa_bias_trim[i]);

		if (info->pa_bias_trim[i] != 0xff)
			info->pg_pa_bias_trim = true;
	}
}

static void rtw8851b_pa_bias_trim(struct rtw89_dev *rtwdev)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	u8 pabias_2g, pabias_5g;
	u8 i;

	if (!info->pg_pa_bias_trim) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PA_BIAS][TRIM] no PG, do nothing\n");

		return;
	}

	for (i = 0; i < RF_PATH_NUM_8851B; i++) {
		pabias_2g = u8_get_bits(info->pa_bias_trim[i], GENMASK(3, 0));
		pabias_5g = u8_get_bits(info->pa_bias_trim[i], GENMASK(7, 4));

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PA_BIAS][TRIM] path=%d 2G=0x%x 5G=0x%x\n",
			    i, pabias_2g, pabias_5g);

		rtw89_write_rf(rtwdev, i, RR_BIASA, RR_BIASA_TXG, pabias_2g);
		rtw89_write_rf(rtwdev, i, RR_BIASA, RR_BIASA_TXA, pabias_5g);
	}
}

static void rtw8851b_phycap_parsing_gain_comp(struct rtw89_dev *rtwdev, u8 *phycap_map)
{
	static const u32 comp_addrs[][RTW89_SUBBAND_2GHZ_5GHZ_NR] = {
		{0x5BB, 0x5BA, 0, 0x5B9, 0x5B8},
	};
	struct rtw89_phy_efuse_gain *gain = &rtwdev->efuse_gain;
	u32 phycap_addr = rtwdev->chip->phycap_addr;
	bool valid = false;
	int path, i;
	u8 data;

	for (path = 0; path < BB_PATH_NUM_8851B; path++)
		for (i = 0; i < RTW89_SUBBAND_2GHZ_5GHZ_NR; i++) {
			if (comp_addrs[path][i] == 0)
				continue;

			data = phycap_map[comp_addrs[path][i] - phycap_addr];
			valid |= _decode_efuse_gain(data, NULL,
						    &gain->comp[path][i]);
		}

	gain->comp_valid = valid;
}

static int rtw8851b_read_phycap(struct rtw89_dev *rtwdev, u8 *phycap_map)
{
	rtw8851b_phycap_parsing_tssi(rtwdev, phycap_map);
	rtw8851b_phycap_parsing_thermal_trim(rtwdev, phycap_map);
	rtw8851b_phycap_parsing_pa_bias_trim(rtwdev, phycap_map);
	rtw8851b_phycap_parsing_gain_comp(rtwdev, phycap_map);

	return 0;
}

static void rtw8851b_set_bb_gpio(struct rtw89_dev *rtwdev, u8 gpio_idx, bool inv,
				 u8 src_sel)
{
	u32 addr, mask;

	if (gpio_idx >= 32)
		return;

	/* 2 continual 32-bit registers for 32 GPIOs, and each GPIO occupies 2 bits */
	addr = R_RFE_SEL0_A2 + (gpio_idx / 16) * sizeof(u32);
	mask = B_RFE_SEL0_MASK << (gpio_idx % 16) * 2;

	rtw89_phy_write32_mask(rtwdev, addr, mask, RF_PATH_A);
	rtw89_phy_write32_mask(rtwdev, R_RFE_INV0, BIT(gpio_idx), inv);

	/* 4 continual 32-bit registers for 32 GPIOs, and each GPIO occupies 4 bits */
	addr = R_RFE_SEL0_BASE + (gpio_idx / 8) * sizeof(u32);
	mask = B_RFE_SEL0_SRC_MASK << (gpio_idx % 8) * 4;

	rtw89_phy_write32_mask(rtwdev, addr, mask, src_sel);
}

static void rtw8851b_set_mac_gpio(struct rtw89_dev *rtwdev, u8 func)
{
	static const struct rtw89_reg3_def func16 = {
		R_AX_GPIO16_23_FUNC_SEL, B_AX_PINMUX_GPIO16_FUNC_SEL_MASK, BIT(3)
	};
	static const struct rtw89_reg3_def func17 = {
		R_AX_GPIO16_23_FUNC_SEL, B_AX_PINMUX_GPIO17_FUNC_SEL_MASK, BIT(7) >> 4,
	};
	const struct rtw89_reg3_def *def;

	switch (func) {
	case 16:
		def = &func16;
		break;
	case 17:
		def = &func17;
		break;
	default:
		rtw89_warn(rtwdev, "undefined gpio func %d\n", func);
		return;
	}

	rtw89_write8_mask(rtwdev, def->addr, def->mask, def->data);
}

static void rtw8851b_rfe_gpio(struct rtw89_dev *rtwdev)
{
	u8 rfe_type = rtwdev->efuse.rfe_type;

	if (rfe_type > 50)
		return;

	if (rfe_type % 3 == 2) {
		rtw8851b_set_bb_gpio(rtwdev, 16, true, RFE_SEL0_SRC_ANTSEL_0);
		rtw8851b_set_bb_gpio(rtwdev, 17, false, RFE_SEL0_SRC_ANTSEL_0);

		rtw8851b_set_mac_gpio(rtwdev, 16);
		rtw8851b_set_mac_gpio(rtwdev, 17);
	}
}

static void rtw8851b_power_trim(struct rtw89_dev *rtwdev)
{
	rtw8851b_thermal_trim(rtwdev);
	rtw8851b_pa_bias_trim(rtwdev);
}

static void rtw8851b_set_channel_mac(struct rtw89_dev *rtwdev,
				     const struct rtw89_chan *chan,
				     u8 mac_idx)
{
	u32 sub_carr = rtw89_mac_reg_by_idx(rtwdev, R_AX_TX_SUB_CARRIER_VALUE, mac_idx);
	u32 chk_rate = rtw89_mac_reg_by_idx(rtwdev, R_AX_TXRATE_CHK, mac_idx);
	u32 rf_mod = rtw89_mac_reg_by_idx(rtwdev, R_AX_WMAC_RFMOD, mac_idx);
	u8 txsc20 = 0, txsc40 = 0;

	switch (chan->band_width) {
	case RTW89_CHANNEL_WIDTH_80:
		txsc40 = rtw89_phy_get_txsc(rtwdev, chan, RTW89_CHANNEL_WIDTH_40);
		fallthrough;
	case RTW89_CHANNEL_WIDTH_40:
		txsc20 = rtw89_phy_get_txsc(rtwdev, chan, RTW89_CHANNEL_WIDTH_20);
		break;
	default:
		break;
	}

	switch (chan->band_width) {
	case RTW89_CHANNEL_WIDTH_80:
		rtw89_write8_mask(rtwdev, rf_mod, B_AX_WMAC_RFMOD_MASK, BIT(1));
		rtw89_write32(rtwdev, sub_carr, txsc20 | (txsc40 << 4));
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rtw89_write8_mask(rtwdev, rf_mod, B_AX_WMAC_RFMOD_MASK, BIT(0));
		rtw89_write32(rtwdev, sub_carr, txsc20);
		break;
	case RTW89_CHANNEL_WIDTH_20:
		rtw89_write8_clr(rtwdev, rf_mod, B_AX_WMAC_RFMOD_MASK);
		rtw89_write32(rtwdev, sub_carr, 0);
		break;
	default:
		break;
	}

	if (chan->channel > 14) {
		rtw89_write8_clr(rtwdev, chk_rate, B_AX_BAND_MODE);
		rtw89_write8_set(rtwdev, chk_rate,
				 B_AX_CHECK_CCK_EN | B_AX_RTS_LIMIT_IN_OFDM6);
	} else {
		rtw89_write8_set(rtwdev, chk_rate, B_AX_BAND_MODE);
		rtw89_write8_clr(rtwdev, chk_rate,
				 B_AX_CHECK_CCK_EN | B_AX_RTS_LIMIT_IN_OFDM6);
	}
}

static const u32 rtw8851b_sco_barker_threshold[14] = {
	0x1cfea, 0x1d0e1, 0x1d1d7, 0x1d2cd, 0x1d3c3, 0x1d4b9, 0x1d5b0, 0x1d6a6,
	0x1d79c, 0x1d892, 0x1d988, 0x1da7f, 0x1db75, 0x1ddc4
};

static const u32 rtw8851b_sco_cck_threshold[14] = {
	0x27de3, 0x27f35, 0x28088, 0x281da, 0x2832d, 0x2847f, 0x285d2, 0x28724,
	0x28877, 0x289c9, 0x28b1c, 0x28c6e, 0x28dc1, 0x290ed
};

static void rtw8851b_ctrl_sco_cck(struct rtw89_dev *rtwdev, u8 primary_ch)
{
	u8 ch_element = primary_ch - 1;

	rtw89_phy_write32_mask(rtwdev, R_RXSCOBC, B_RXSCOBC_TH,
			       rtw8851b_sco_barker_threshold[ch_element]);
	rtw89_phy_write32_mask(rtwdev, R_RXSCOCCK, B_RXSCOCCK_TH,
			       rtw8851b_sco_cck_threshold[ch_element]);
}

static u8 rtw8851b_sco_mapping(u8 central_ch)
{
	if (central_ch == 1)
		return 109;
	else if (central_ch >= 2 && central_ch <= 6)
		return 108;
	else if (central_ch >= 7 && central_ch <= 10)
		return 107;
	else if (central_ch >= 11 && central_ch <= 14)
		return 106;
	else if (central_ch == 36 || central_ch == 38)
		return 51;
	else if (central_ch >= 40 && central_ch <= 58)
		return 50;
	else if (central_ch >= 60 && central_ch <= 64)
		return 49;
	else if (central_ch == 100 || central_ch == 102)
		return 48;
	else if (central_ch >= 104 && central_ch <= 126)
		return 47;
	else if (central_ch >= 128 && central_ch <= 151)
		return 46;
	else if (central_ch >= 153 && central_ch <= 177)
		return 45;
	else
		return 0;
}

struct rtw8851b_bb_gain {
	u32 gain_g[BB_PATH_NUM_8851B];
	u32 gain_a[BB_PATH_NUM_8851B];
	u32 gain_mask;
};

static const struct rtw8851b_bb_gain bb_gain_lna[LNA_GAIN_NUM] = {
	{ .gain_g = {0x4678}, .gain_a = {0x45DC},
	  .gain_mask = 0x00ff0000 },
	{ .gain_g = {0x4678}, .gain_a = {0x45DC},
	  .gain_mask = 0xff000000 },
	{ .gain_g = {0x467C}, .gain_a = {0x4660},
	  .gain_mask = 0x000000ff },
	{ .gain_g = {0x467C}, .gain_a = {0x4660},
	  .gain_mask = 0x0000ff00 },
	{ .gain_g = {0x467C}, .gain_a = {0x4660},
	  .gain_mask = 0x00ff0000 },
	{ .gain_g = {0x467C}, .gain_a = {0x4660},
	  .gain_mask = 0xff000000 },
	{ .gain_g = {0x4680}, .gain_a = {0x4664},
	  .gain_mask = 0x000000ff },
};

static const struct rtw8851b_bb_gain bb_gain_tia[TIA_GAIN_NUM] = {
	{ .gain_g = {0x4680}, .gain_a = {0x4664},
	  .gain_mask = 0x00ff0000 },
	{ .gain_g = {0x4680}, .gain_a = {0x4664},
	  .gain_mask = 0xff000000 },
};

static void rtw8851b_set_gain_error(struct rtw89_dev *rtwdev,
				    enum rtw89_subband subband,
				    enum rtw89_rf_path path)
{
	const struct rtw89_phy_bb_gain_info *gain = &rtwdev->bb_gain.ax;
	u8 gain_band = rtw89_subband_to_bb_gain_band(subband);
	s32 val;
	u32 reg;
	u32 mask;
	int i;

	for (i = 0; i < LNA_GAIN_NUM; i++) {
		if (subband == RTW89_CH_2G)
			reg = bb_gain_lna[i].gain_g[path];
		else
			reg = bb_gain_lna[i].gain_a[path];

		mask = bb_gain_lna[i].gain_mask;
		val = gain->lna_gain[gain_band][path][i];
		rtw89_phy_write32_mask(rtwdev, reg, mask, val);
	}

	for (i = 0; i < TIA_GAIN_NUM; i++) {
		if (subband == RTW89_CH_2G)
			reg = bb_gain_tia[i].gain_g[path];
		else
			reg = bb_gain_tia[i].gain_a[path];

		mask = bb_gain_tia[i].gain_mask;
		val = gain->tia_gain[gain_band][path][i];
		rtw89_phy_write32_mask(rtwdev, reg, mask, val);
	}
}

static void rtw8851b_set_gain_offset(struct rtw89_dev *rtwdev,
				     enum rtw89_subband subband,
				     enum rtw89_phy_idx phy_idx)
{
	static const u32 rssi_ofst_addr[] = {R_PATH0_G_TIA1_LNA6_OP1DB_V1};
	static const u32 gain_err_addr[] = {R_P0_AGC_RSVD};
	struct rtw89_phy_efuse_gain *efuse_gain = &rtwdev->efuse_gain;
	enum rtw89_gain_offset gain_ofdm_band;
	s32 offset_ofdm, offset_cck;
	s32 offset_a;
	s32 tmp;
	u8 path;

	if (!efuse_gain->comp_valid)
		goto next;

	for (path = RF_PATH_A; path < BB_PATH_NUM_8851B; path++) {
		tmp = efuse_gain->comp[path][subband];
		tmp = clamp_t(s32, tmp << 2, S8_MIN, S8_MAX);
		rtw89_phy_write32_mask(rtwdev, gain_err_addr[path], MASKBYTE0, tmp);
	}

next:
	if (!efuse_gain->offset_valid)
		return;

	gain_ofdm_band = rtw89_subband_to_gain_offset_band_of_ofdm(subband);

	offset_a = -efuse_gain->offset[RF_PATH_A][gain_ofdm_band];

	tmp = -((offset_a << 2) + (efuse_gain->offset_base[RTW89_PHY_0] >> 2));
	tmp = clamp_t(s32, tmp, S8_MIN, S8_MAX);
	rtw89_phy_write32_mask(rtwdev, rssi_ofst_addr[RF_PATH_A], B_PATH0_R_G_OFST_MASK, tmp);

	offset_ofdm = -efuse_gain->offset[RF_PATH_A][gain_ofdm_band];
	offset_cck = -efuse_gain->offset[RF_PATH_A][0];

	tmp = (offset_ofdm << 4) + efuse_gain->offset_base[RTW89_PHY_0];
	tmp = clamp_t(s32, tmp, S8_MIN, S8_MAX);
	rtw89_phy_write32_idx(rtwdev, R_P0_RPL1, B_P0_RPL1_BIAS_MASK, tmp, phy_idx);

	tmp = (offset_ofdm << 4) + efuse_gain->rssi_base[RTW89_PHY_0];
	tmp = clamp_t(s32, tmp, S8_MIN, S8_MAX);
	rtw89_phy_write32_idx(rtwdev, R_P1_RPL1, B_P0_RPL1_BIAS_MASK, tmp, phy_idx);

	if (subband == RTW89_CH_2G) {
		tmp = (offset_cck << 3) + (efuse_gain->offset_base[RTW89_PHY_0] >> 1);
		tmp = clamp_t(s32, tmp, S8_MIN >> 1, S8_MAX >> 1);
		rtw89_phy_write32_mask(rtwdev, R_RX_RPL_OFST,
				       B_RX_RPL_OFST_CCK_MASK, tmp);
	}
}

static
void rtw8851b_set_rxsc_rpl_comp(struct rtw89_dev *rtwdev, enum rtw89_subband subband)
{
	const struct rtw89_phy_bb_gain_info *gain = &rtwdev->bb_gain.ax;
	u8 band = rtw89_subband_to_bb_gain_band(subband);
	u32 val;

	val = u32_encode_bits(gain->rpl_ofst_20[band][RF_PATH_A], B_P0_RPL1_20_MASK) |
	      u32_encode_bits(gain->rpl_ofst_40[band][RF_PATH_A][0], B_P0_RPL1_40_MASK) |
	      u32_encode_bits(gain->rpl_ofst_40[band][RF_PATH_A][1], B_P0_RPL1_41_MASK);
	val >>= B_P0_RPL1_SHIFT;
	rtw89_phy_write32_mask(rtwdev, R_P0_RPL1, B_P0_RPL1_MASK, val);
	rtw89_phy_write32_mask(rtwdev, R_P1_RPL1, B_P0_RPL1_MASK, val);

	val = u32_encode_bits(gain->rpl_ofst_40[band][RF_PATH_A][2], B_P0_RTL2_42_MASK) |
	      u32_encode_bits(gain->rpl_ofst_80[band][RF_PATH_A][0], B_P0_RTL2_80_MASK) |
	      u32_encode_bits(gain->rpl_ofst_80[band][RF_PATH_A][1], B_P0_RTL2_81_MASK) |
	      u32_encode_bits(gain->rpl_ofst_80[band][RF_PATH_A][10], B_P0_RTL2_8A_MASK);
	rtw89_phy_write32(rtwdev, R_P0_RPL2, val);
	rtw89_phy_write32(rtwdev, R_P1_RPL2, val);

	val = u32_encode_bits(gain->rpl_ofst_80[band][RF_PATH_A][2], B_P0_RTL3_82_MASK) |
	      u32_encode_bits(gain->rpl_ofst_80[band][RF_PATH_A][3], B_P0_RTL3_83_MASK) |
	      u32_encode_bits(gain->rpl_ofst_80[band][RF_PATH_A][4], B_P0_RTL3_84_MASK) |
	      u32_encode_bits(gain->rpl_ofst_80[band][RF_PATH_A][9], B_P0_RTL3_89_MASK);
	rtw89_phy_write32(rtwdev, R_P0_RPL3, val);
	rtw89_phy_write32(rtwdev, R_P1_RPL3, val);
}

static void rtw8851b_ctrl_ch(struct rtw89_dev *rtwdev,
			     const struct rtw89_chan *chan,
			     enum rtw89_phy_idx phy_idx)
{
	u8 subband = chan->subband_type;
	u8 central_ch = chan->channel;
	bool is_2g = central_ch <= 14;
	u8 sco_comp;

	if (is_2g)
		rtw89_phy_write32_idx(rtwdev, R_PATH0_BAND_SEL_V1,
				      B_PATH0_BAND_SEL_MSK_V1, 1, phy_idx);
	else
		rtw89_phy_write32_idx(rtwdev, R_PATH0_BAND_SEL_V1,
				      B_PATH0_BAND_SEL_MSK_V1, 0, phy_idx);
	/* SCO compensate FC setting */
	sco_comp = rtw8851b_sco_mapping(central_ch);
	rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_INV, sco_comp, phy_idx);

	if (chan->band_type == RTW89_BAND_6G)
		return;

	/* CCK parameters */
	if (central_ch == 14) {
		rtw89_phy_write32_mask(rtwdev, R_TXFIR0, B_TXFIR_C01, 0x3b13ff);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR2, B_TXFIR_C23, 0x1c42de);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR4, B_TXFIR_C45, 0xfdb0ad);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR6, B_TXFIR_C67, 0xf60f6e);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR8, B_TXFIR_C89, 0xfd8f92);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRA, B_TXFIR_CAB, 0x2d011);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRC, B_TXFIR_CCD, 0x1c02c);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRE, B_TXFIR_CEF, 0xfff00a);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_TXFIR0, B_TXFIR_C01, 0x3d23ff);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR2, B_TXFIR_C23, 0x29b354);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR4, B_TXFIR_C45, 0xfc1c8);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR6, B_TXFIR_C67, 0xfdb053);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR8, B_TXFIR_C89, 0xf86f9a);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRA, B_TXFIR_CAB, 0xfaef92);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRC, B_TXFIR_CCD, 0xfe5fcc);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRE, B_TXFIR_CEF, 0xffdff5);
	}

	rtw8851b_set_gain_error(rtwdev, subband, RF_PATH_A);
	rtw8851b_set_gain_offset(rtwdev, subband, phy_idx);
	rtw8851b_set_rxsc_rpl_comp(rtwdev, subband);
}

static void rtw8851b_bw_setting(struct rtw89_dev *rtwdev, u8 bw)
{
	rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW0, B_P0_CFCH_CTL, 0x8);
	rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW0, B_P0_CFCH_EN, 0x2);
	rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW0, B_P0_CFCH_BW0, 0x2);
	rtw89_phy_write32_mask(rtwdev, R_P0_CFCH_BW1, B_P0_CFCH_BW1, 0x4);
	rtw89_phy_write32_mask(rtwdev, R_DRCK, B_DRCK_MUL, 0xf);
	rtw89_phy_write32_mask(rtwdev, R_ADCMOD, B_ADCMOD_LP, 0xa);
	rtw89_phy_write32_mask(rtwdev, R_P0_RXCK, B_P0_RXCK_ADJ, 0x92);

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_5:
		rtw89_phy_write32_mask(rtwdev, R_DCIM, B_DCIM_FR, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_WDADC, B_WDADC_SEL, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_ADDCK0D, B_ADDCK_DS, 0x1);
		break;
	case RTW89_CHANNEL_WIDTH_10:
		rtw89_phy_write32_mask(rtwdev, R_DCIM, B_DCIM_FR, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_WDADC, B_WDADC_SEL, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_ADDCK0D, B_ADDCK_DS, 0x0);
		break;
	case RTW89_CHANNEL_WIDTH_20:
		rtw89_phy_write32_mask(rtwdev, R_DCIM, B_DCIM_FR, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_WDADC, B_WDADC_SEL, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_ADDCK0D, B_ADDCK_DS, 0x0);
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rtw89_phy_write32_mask(rtwdev, R_DCIM, B_DCIM_FR, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_WDADC, B_WDADC_SEL, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_ADDCK0D, B_ADDCK_DS, 0x0);
		break;
	case RTW89_CHANNEL_WIDTH_80:
		rtw89_phy_write32_mask(rtwdev, R_DCIM, B_DCIM_FR, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_WDADC, B_WDADC_SEL, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_ADDCK0D, B_ADDCK_DS, 0x0);
		break;
	default:
		rtw89_warn(rtwdev, "Fail to set ADC\n");
	}
}

static void rtw8851b_ctrl_bw(struct rtw89_dev *rtwdev, u8 pri_ch, u8 bw,
			     enum rtw89_phy_idx phy_idx)
{
	switch (bw) {
	case RTW89_CHANNEL_WIDTH_5:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_SET, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_SBW, 0x1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_PRICH, 0x0, phy_idx);
		break;
	case RTW89_CHANNEL_WIDTH_10:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_SET, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_SBW, 0x2, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_PRICH, 0x0, phy_idx);
		break;
	case RTW89_CHANNEL_WIDTH_20:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_SET, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_SBW, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_PRICH, 0x0, phy_idx);
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_SET, 0x1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_SBW, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_PRICH,
				      pri_ch, phy_idx);
		/* CCK primary channel */
		if (pri_ch == RTW89_SC_20_UPPER)
			rtw89_phy_write32_mask(rtwdev, R_RXSC, B_RXSC_EN, 1);
		else
			rtw89_phy_write32_mask(rtwdev, R_RXSC, B_RXSC_EN, 0);

		break;
	case RTW89_CHANNEL_WIDTH_80:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_SET, 0x2, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_SBW, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_PRICH,
				      pri_ch, phy_idx);
		break;
	default:
		rtw89_warn(rtwdev, "Fail to switch bw (bw:%d, pri ch:%d)\n", bw,
			   pri_ch);
	}

	rtw8851b_bw_setting(rtwdev, bw);
}

static void rtw8851b_ctrl_cck_en(struct rtw89_dev *rtwdev, bool cck_en)
{
	if (cck_en) {
		rtw89_phy_write32_mask(rtwdev, R_RXCCA, B_RXCCA_DIS, 0);
		rtw89_phy_write32_mask(rtwdev, R_PD_ARBITER_OFF,
				       B_PD_ARBITER_OFF, 0);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK_ADC, B_ENABLE_CCK, 1);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_RXCCA, B_RXCCA_DIS, 1);
		rtw89_phy_write32_mask(rtwdev, R_PD_ARBITER_OFF,
				       B_PD_ARBITER_OFF, 1);
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK_ADC, B_ENABLE_CCK, 0);
	}
}

static u32 rtw8851b_spur_freq(struct rtw89_dev *rtwdev,
			      const struct rtw89_chan *chan)
{
	u8 center_chan = chan->channel;

	switch (chan->band_type) {
	case RTW89_BAND_5G:
		if (center_chan == 151 || center_chan == 153 ||
		    center_chan == 155 || center_chan == 163)
			return 5760;
		else if (center_chan == 54 || center_chan == 58)
			return 5280;
		break;
	default:
		break;
	}

	return 0;
}

#define CARRIER_SPACING_312_5 312500 /* 312.5 kHz */
#define CARRIER_SPACING_78_125 78125 /* 78.125 kHz */
#define MAX_TONE_NUM 2048

static void rtw8851b_set_csi_tone_idx(struct rtw89_dev *rtwdev,
				      const struct rtw89_chan *chan,
				      enum rtw89_phy_idx phy_idx)
{
	u32 spur_freq;
	s32 freq_diff, csi_idx, csi_tone_idx;

	spur_freq = rtw8851b_spur_freq(rtwdev, chan);
	if (spur_freq == 0) {
		rtw89_phy_write32_idx(rtwdev, R_SEG0CSI_EN_V1, B_SEG0CSI_EN,
				      0, phy_idx);
		return;
	}

	freq_diff = (spur_freq - chan->freq) * 1000000;
	csi_idx = s32_div_u32_round_closest(freq_diff, CARRIER_SPACING_78_125);
	s32_div_u32_round_down(csi_idx, MAX_TONE_NUM, &csi_tone_idx);

	rtw89_phy_write32_idx(rtwdev, R_SEG0CSI_V1, B_SEG0CSI_IDX,
			      csi_tone_idx, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_SEG0CSI_EN_V1, B_SEG0CSI_EN, 1, phy_idx);
}

static const struct rtw89_nbi_reg_def rtw8851b_nbi_reg_def = {
	.notch1_idx = {0x46E4, 0xFF},
	.notch1_frac_idx = {0x46E4, 0xC00},
	.notch1_en = {0x46E4, 0x1000},
	.notch2_idx = {0x47A4, 0xFF},
	.notch2_frac_idx = {0x47A4, 0xC00},
	.notch2_en = {0x47A4, 0x1000},
};

static void rtw8851b_set_nbi_tone_idx(struct rtw89_dev *rtwdev,
				      const struct rtw89_chan *chan)
{
	const struct rtw89_nbi_reg_def *nbi = &rtw8851b_nbi_reg_def;
	s32 nbi_frac_idx, nbi_frac_tone_idx;
	s32 nbi_idx, nbi_tone_idx;
	bool notch2_chk = false;
	u32 spur_freq, fc;
	s32 freq_diff;

	spur_freq = rtw8851b_spur_freq(rtwdev, chan);
	if (spur_freq == 0) {
		rtw89_phy_write32_mask(rtwdev, nbi->notch1_en.addr,
				       nbi->notch1_en.mask, 0);
		rtw89_phy_write32_mask(rtwdev, nbi->notch2_en.addr,
				       nbi->notch2_en.mask, 0);
		return;
	}

	fc = chan->freq;
	if (chan->band_width == RTW89_CHANNEL_WIDTH_160) {
		fc = (spur_freq > fc) ? fc + 40 : fc - 40;
		if ((fc > spur_freq &&
		     chan->channel < chan->primary_channel) ||
		    (fc < spur_freq &&
		     chan->channel > chan->primary_channel))
			notch2_chk = true;
	}

	freq_diff = (spur_freq - fc) * 1000000;
	nbi_idx = s32_div_u32_round_down(freq_diff, CARRIER_SPACING_312_5,
					 &nbi_frac_idx);

	if (chan->band_width == RTW89_CHANNEL_WIDTH_20) {
		s32_div_u32_round_down(nbi_idx + 32, 64, &nbi_tone_idx);
	} else {
		u16 tone_para = (chan->band_width == RTW89_CHANNEL_WIDTH_40) ?
				128 : 256;

		s32_div_u32_round_down(nbi_idx, tone_para, &nbi_tone_idx);
	}
	nbi_frac_tone_idx = s32_div_u32_round_closest(nbi_frac_idx,
						      CARRIER_SPACING_78_125);

	if (chan->band_width == RTW89_CHANNEL_WIDTH_160 && notch2_chk) {
		rtw89_phy_write32_mask(rtwdev, nbi->notch2_idx.addr,
				       nbi->notch2_idx.mask, nbi_tone_idx);
		rtw89_phy_write32_mask(rtwdev, nbi->notch2_frac_idx.addr,
				       nbi->notch2_frac_idx.mask, nbi_frac_tone_idx);
		rtw89_phy_write32_mask(rtwdev, nbi->notch2_en.addr,
				       nbi->notch2_en.mask, 0);
		rtw89_phy_write32_mask(rtwdev, nbi->notch2_en.addr,
				       nbi->notch2_en.mask, 1);
		rtw89_phy_write32_mask(rtwdev, nbi->notch1_en.addr,
				       nbi->notch1_en.mask, 0);
	} else {
		rtw89_phy_write32_mask(rtwdev, nbi->notch1_idx.addr,
				       nbi->notch1_idx.mask, nbi_tone_idx);
		rtw89_phy_write32_mask(rtwdev, nbi->notch1_frac_idx.addr,
				       nbi->notch1_frac_idx.mask, nbi_frac_tone_idx);
		rtw89_phy_write32_mask(rtwdev, nbi->notch1_en.addr,
				       nbi->notch1_en.mask, 0);
		rtw89_phy_write32_mask(rtwdev, nbi->notch1_en.addr,
				       nbi->notch1_en.mask, 1);
		rtw89_phy_write32_mask(rtwdev, nbi->notch2_en.addr,
				       nbi->notch2_en.mask, 0);
	}
}

static void rtw8851b_set_cfr(struct rtw89_dev *rtwdev, const struct rtw89_chan *chan)
{
	if (chan->band_type == RTW89_BAND_2G &&
	    chan->band_width == RTW89_CHANNEL_WIDTH_20 &&
	    (chan->channel == 1 || chan->channel == 13)) {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_TX_CFR,
				       B_PATH0_TX_CFR_LGC0, 0xf8);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_TX_CFR,
				       B_PATH0_TX_CFR_LGC1, 0x120);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_TX_POLAR_CLIPPING,
				       B_PATH0_TX_POLAR_CLIPPING_LGC0, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_TX_POLAR_CLIPPING,
				       B_PATH0_TX_POLAR_CLIPPING_LGC1, 0x3);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_TX_CFR,
				       B_PATH0_TX_CFR_LGC0, 0x120);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_TX_CFR,
				       B_PATH0_TX_CFR_LGC1, 0x3ff);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_TX_POLAR_CLIPPING,
				       B_PATH0_TX_POLAR_CLIPPING_LGC0, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_TX_POLAR_CLIPPING,
				       B_PATH0_TX_POLAR_CLIPPING_LGC1, 0x7);
	}
}

static void rtw8851b_5m_mask(struct rtw89_dev *rtwdev, const struct rtw89_chan *chan,
			     enum rtw89_phy_idx phy_idx)
{
	u8 pri_ch = chan->pri_ch_idx;
	bool mask_5m_low;
	bool mask_5m_en;

	switch (chan->band_width) {
	case RTW89_CHANNEL_WIDTH_40:
		/* Prich=1: Mask 5M High, Prich=2: Mask 5M Low */
		mask_5m_en = true;
		mask_5m_low = pri_ch == RTW89_SC_20_LOWER;
		break;
	case RTW89_CHANNEL_WIDTH_80:
		/* Prich=3: Mask 5M High, Prich=4: Mask 5M Low, Else: Disable */
		mask_5m_en = pri_ch == RTW89_SC_20_UPMOST ||
			     pri_ch == RTW89_SC_20_LOWEST;
		mask_5m_low = pri_ch == RTW89_SC_20_LOWEST;
		break;
	default:
		mask_5m_en = false;
		break;
	}

	if (!mask_5m_en) {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_EN, 0x0);
		rtw89_phy_write32_idx(rtwdev, R_ASSIGN_SBD_OPT_V1,
				      B_ASSIGN_SBD_OPT_EN_V1, 0x0, phy_idx);
		return;
	}

	if (mask_5m_low) {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_TH, 0x5);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_SB2, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_SB0, 0x1);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_TH, 0x5);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_SB2, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_SB0, 0x0);
	}
	rtw89_phy_write32_idx(rtwdev, R_ASSIGN_SBD_OPT_V1,
			      B_ASSIGN_SBD_OPT_EN_V1, 0x1, phy_idx);
}

static void rtw8851b_bb_reset_all(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	rtw89_phy_write32_idx(rtwdev, R_S0_HW_SI_DIS, B_S0_HW_SI_DIS_W_R_TRIG, 0x7, phy_idx);
	fsleep(1);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 0, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_S0_HW_SI_DIS, B_S0_HW_SI_DIS_W_R_TRIG, 0x0, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1, phy_idx);
}

static void rtw8851b_bb_reset_en(struct rtw89_dev *rtwdev, enum rtw89_band band,
				 enum rtw89_phy_idx phy_idx, bool en)
{
	if (en) {
		rtw89_phy_write32_idx(rtwdev, R_S0_HW_SI_DIS,
				      B_S0_HW_SI_DIS_W_R_TRIG, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1, phy_idx);
		if (band == RTW89_BAND_2G)
			rtw89_phy_write32_mask(rtwdev, R_RXCCA, B_RXCCA_DIS, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PD_CTRL, B_PD_HIT_DIS, 0x0);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_RXCCA, B_RXCCA_DIS, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PD_CTRL, B_PD_HIT_DIS, 0x1);
		rtw89_phy_write32_idx(rtwdev, R_S0_HW_SI_DIS,
				      B_S0_HW_SI_DIS_W_R_TRIG, 0x7, phy_idx);
		fsleep(1);
		rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 0, phy_idx);
	}
}

static void rtw8851b_bb_reset(struct rtw89_dev *rtwdev,
			      enum rtw89_phy_idx phy_idx)
{
	rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB,
			       B_P0_TXPW_RSTB_MANON | B_P0_TXPW_RSTB_TSSI, 0x1);
	rtw89_phy_write32_set(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_TRK_EN);
	rtw8851b_bb_reset_all(rtwdev, phy_idx);
	rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB,
			       B_P0_TXPW_RSTB_MANON | B_P0_TXPW_RSTB_TSSI, 0x3);
	rtw89_phy_write32_clr(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_TRK_EN);
}

static
void rtw8851b_bb_gpio_trsw(struct rtw89_dev *rtwdev, enum rtw89_rf_path path,
			   u8 tx_path_en, u8 trsw_tx,
			   u8 trsw_rx, u8 trsw_a, u8 trsw_b)
{
	u32 mask_ofst = 16;
	u32 val;

	if (path != RF_PATH_A)
		return;

	mask_ofst += (tx_path_en * 4 + trsw_tx * 2 + trsw_rx) * 2;
	val = u32_encode_bits(trsw_a, B_P0_TRSW_A) |
	      u32_encode_bits(trsw_b, B_P0_TRSW_B);

	rtw89_phy_write32_mask(rtwdev, R_P0_TRSW,
			       (B_P0_TRSW_A | B_P0_TRSW_B) << mask_ofst, val);
}

static void rtw8851b_bb_gpio_init(struct rtw89_dev *rtwdev)
{
	rtw89_phy_write32_set(rtwdev, R_P0_TRSW, B_P0_TRSW_A);
	rtw89_phy_write32_clr(rtwdev, R_P0_TRSW, B_P0_TRSW_X);
	rtw89_phy_write32_clr(rtwdev, R_P0_TRSW, B_P0_TRSW_SO_A2);
	rtw89_phy_write32(rtwdev, R_RFE_SEL0_BASE, 0x77777777);
	rtw89_phy_write32(rtwdev, R_RFE_SEL32_BASE, 0x77777777);

	rtw89_phy_write32(rtwdev, R_RFE_E_A2, 0xffffffff);
	rtw89_phy_write32(rtwdev, R_RFE_O_SEL_A2, 0);
	rtw89_phy_write32(rtwdev, R_RFE_SEL0_A2, 0);
	rtw89_phy_write32(rtwdev, R_RFE_SEL32_A2, 0);

	rtw8851b_bb_gpio_trsw(rtwdev, RF_PATH_A, 0, 0, 0, 0, 1);
	rtw8851b_bb_gpio_trsw(rtwdev, RF_PATH_A, 0, 0, 1, 1, 0);
	rtw8851b_bb_gpio_trsw(rtwdev, RF_PATH_A, 0, 1, 0, 1, 0);
	rtw8851b_bb_gpio_trsw(rtwdev, RF_PATH_A, 0, 1, 1, 1, 0);
	rtw8851b_bb_gpio_trsw(rtwdev, RF_PATH_A, 1, 0, 0, 0, 1);
	rtw8851b_bb_gpio_trsw(rtwdev, RF_PATH_A, 1, 0, 1, 1, 0);
	rtw8851b_bb_gpio_trsw(rtwdev, RF_PATH_A, 1, 1, 0, 1, 0);
	rtw8851b_bb_gpio_trsw(rtwdev, RF_PATH_A, 1, 1, 1, 1, 0);
}

static void rtw8851b_bb_macid_ctrl_init(struct rtw89_dev *rtwdev,
					enum rtw89_phy_idx phy_idx)
{
	u32 addr;

	for (addr = R_AX_PWR_MACID_LMT_TABLE0;
	     addr <= R_AX_PWR_MACID_LMT_TABLE127; addr += 4)
		rtw89_mac_txpwr_write32(rtwdev, phy_idx, addr, 0);
}

static void rtw8851b_bb_sethw(struct rtw89_dev *rtwdev)
{
	struct rtw89_phy_efuse_gain *gain = &rtwdev->efuse_gain;

	rtw89_phy_write32_clr(rtwdev, R_P0_EN_SOUND_WO_NDP, B_P0_EN_SOUND_WO_NDP);

	rtw8851b_bb_macid_ctrl_init(rtwdev, RTW89_PHY_0);
	rtw8851b_bb_gpio_init(rtwdev);

	rtw89_write32_clr(rtwdev, R_AX_PWR_NORM_FORCE1, B_AX_FORCE_NTX_VALUE);
	rtw89_write32_set(rtwdev, R_AX_PWR_NORM_FORCE1, B_AX_FORCE_NTX_EN);

	/* read these registers after loading BB parameters */
	gain->offset_base[RTW89_PHY_0] =
		rtw89_phy_read32_mask(rtwdev, R_P0_RPL1, B_P0_RPL1_BIAS_MASK);
	gain->rssi_base[RTW89_PHY_0] =
		rtw89_phy_read32_mask(rtwdev, R_P1_RPL1, B_P0_RPL1_BIAS_MASK);
}

static void rtw8851b_set_channel_bb(struct rtw89_dev *rtwdev, const struct rtw89_chan *chan,
				    enum rtw89_phy_idx phy_idx)
{
	u8 band = chan->band_type, chan_idx;
	bool cck_en = chan->channel <= 14;
	u8 pri_ch_idx = chan->pri_ch_idx;

	if (cck_en)
		rtw8851b_ctrl_sco_cck(rtwdev,  chan->primary_channel);

	rtw8851b_ctrl_ch(rtwdev, chan, phy_idx);
	rtw8851b_ctrl_bw(rtwdev, pri_ch_idx, chan->band_width, phy_idx);
	rtw8851b_ctrl_cck_en(rtwdev, cck_en);
	rtw8851b_set_nbi_tone_idx(rtwdev, chan);
	rtw8851b_set_csi_tone_idx(rtwdev, chan, phy_idx);

	if (chan->band_type == RTW89_BAND_5G) {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BT_SHARE_V1,
				       B_PATH0_BT_SHARE_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BTG_PATH_V1,
				       B_PATH0_BTG_PATH_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD_V1, B_BT_SHARE, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_BT_SEG0, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_BT_DYN_DC_EST_EN_V1,
				       B_BT_DYN_DC_EST_EN_MSK, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_GNT_BT_WGT_EN, B_GNT_BT_WGT_EN, 0x0);
	}

	chan_idx = rtw89_encode_chan_idx(rtwdev, chan->primary_channel, band);
	rtw89_phy_write32_mask(rtwdev, R_MAC_PIN_SEL, B_CH_IDX_SEG0, chan_idx);
	rtw8851b_5m_mask(rtwdev, chan, phy_idx);
	rtw8851b_set_cfr(rtwdev, chan);
	rtw8851b_bb_reset_all(rtwdev, phy_idx);
}

static void rtw8851b_set_channel(struct rtw89_dev *rtwdev,
				 const struct rtw89_chan *chan,
				 enum rtw89_mac_idx mac_idx,
				 enum rtw89_phy_idx phy_idx)
{
	rtw8851b_set_channel_mac(rtwdev, chan, mac_idx);
	rtw8851b_set_channel_bb(rtwdev, chan, phy_idx);
	rtw8851b_set_channel_rf(rtwdev, chan, phy_idx);
}

static void rtw8851b_tssi_cont_en(struct rtw89_dev *rtwdev, bool en,
				  enum rtw89_rf_path path)
{
	if (en) {
		rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB, B_P0_TXPW_RSTB_MANON, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_TRK_EN, 0x0);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB, B_P0_TXPW_RSTB_MANON, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_TRK_EN, 0x1);
	}
}

static void rtw8851b_tssi_cont_en_phyidx(struct rtw89_dev *rtwdev, bool en,
					 u8 phy_idx)
{
	rtw8851b_tssi_cont_en(rtwdev, en, RF_PATH_A);
}

static void rtw8851b_adc_en(struct rtw89_dev *rtwdev, bool en)
{
	if (en)
		rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RST, 0x0);
	else
		rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RST, 0xf);
}

static void rtw8851b_set_channel_help(struct rtw89_dev *rtwdev, bool enter,
				      struct rtw89_channel_help_params *p,
				      const struct rtw89_chan *chan,
				      enum rtw89_mac_idx mac_idx,
				      enum rtw89_phy_idx phy_idx)
{
	if (enter) {
		rtw89_chip_stop_sch_tx(rtwdev, RTW89_MAC_0, &p->tx_en, RTW89_SCH_TX_SEL_ALL);
		rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, false);
		rtw8851b_tssi_cont_en_phyidx(rtwdev, false, RTW89_PHY_0);
		rtw8851b_adc_en(rtwdev, false);
		fsleep(40);
		rtw8851b_bb_reset_en(rtwdev, chan->band_type, phy_idx, false);
	} else {
		rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, true);
		rtw8851b_adc_en(rtwdev, true);
		rtw8851b_tssi_cont_en_phyidx(rtwdev, true, RTW89_PHY_0);
		rtw8851b_bb_reset_en(rtwdev, chan->band_type, phy_idx, true);
		rtw89_chip_resume_sch_tx(rtwdev, RTW89_MAC_0, p->tx_en);
	}
}

static void rtw8851b_rfk_init(struct rtw89_dev *rtwdev)
{
	rtwdev->is_tssi_mode[RF_PATH_A] = false;
	rtwdev->is_tssi_mode[RF_PATH_B] = false;
	rtw8851b_lck_init(rtwdev);

	rtw8851b_dpk_init(rtwdev);
	rtw8851b_aack(rtwdev);
	rtw8851b_rck(rtwdev);
	rtw8851b_dack(rtwdev);
	rtw8851b_rx_dck(rtwdev, RTW89_PHY_0);
}

static void rtw8851b_rfk_channel(struct rtw89_dev *rtwdev)
{
	enum rtw89_phy_idx phy_idx = RTW89_PHY_0;

	rtw8851b_rx_dck(rtwdev, phy_idx);
	rtw8851b_iqk(rtwdev, phy_idx);
	rtw8851b_tssi(rtwdev, phy_idx, true);
	rtw8851b_dpk(rtwdev, phy_idx);
}

static void rtw8851b_rfk_band_changed(struct rtw89_dev *rtwdev,
				      enum rtw89_phy_idx phy_idx)
{
	rtw8851b_tssi_scan(rtwdev, phy_idx);
}

static void rtw8851b_rfk_scan(struct rtw89_dev *rtwdev, bool start)
{
	rtw8851b_wifi_scan_notify(rtwdev, start, RTW89_PHY_0);
}

static void rtw8851b_rfk_track(struct rtw89_dev *rtwdev)
{
	rtw8851b_dpk_track(rtwdev);
	rtw8851b_lck_track(rtwdev);
}

static u32 rtw8851b_bb_cal_txpwr_ref(struct rtw89_dev *rtwdev,
				     enum rtw89_phy_idx phy_idx, s16 ref)
{
	const u16 tssi_16dbm_cw = 0x12c;
	const u8 base_cw_0db = 0x27;
	const s8 ofst_int = 0;
	s16 pwr_s10_3;
	s16 rf_pwr_cw;
	u16 bb_pwr_cw;
	u32 pwr_cw;
	u32 tssi_ofst_cw;

	pwr_s10_3 = (ref << 1) + (s16)(ofst_int) + (s16)(base_cw_0db << 3);
	bb_pwr_cw = u16_get_bits(pwr_s10_3, GENMASK(2, 0));
	rf_pwr_cw = u16_get_bits(pwr_s10_3, GENMASK(8, 3));
	rf_pwr_cw = clamp_t(s16, rf_pwr_cw, 15, 63);
	pwr_cw = (rf_pwr_cw << 3) | bb_pwr_cw;

	tssi_ofst_cw = (u32)((s16)tssi_16dbm_cw + (ref << 1) - (16 << 3));
	rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
		    "[TXPWR] tssi_ofst_cw=%d rf_cw=0x%x bb_cw=0x%x\n",
		    tssi_ofst_cw, rf_pwr_cw, bb_pwr_cw);

	return u32_encode_bits(tssi_ofst_cw, B_DPD_TSSI_CW) |
	       u32_encode_bits(pwr_cw, B_DPD_PWR_CW) |
	       u32_encode_bits(ref, B_DPD_REF);
}

static void rtw8851b_set_txpwr_ref(struct rtw89_dev *rtwdev,
				   enum rtw89_phy_idx phy_idx)
{
	static const u32 addr[RF_PATH_NUM_8851B] = {0x5800};
	const u32 mask = B_DPD_TSSI_CW | B_DPD_PWR_CW | B_DPD_REF;
	const u8 ofst_ofdm = 0x4;
	const u8 ofst_cck = 0x8;
	const s16 ref_ofdm = 0;
	const s16 ref_cck = 0;
	u32 val;
	u8 i;

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[TXPWR] set txpwr reference\n");

	rtw89_mac_txpwr_write32_mask(rtwdev, phy_idx, R_AX_PWR_RATE_CTRL,
				     B_AX_PWR_REF, 0x0);

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[TXPWR] set bb ofdm txpwr ref\n");
	val = rtw8851b_bb_cal_txpwr_ref(rtwdev, phy_idx, ref_ofdm);

	for (i = 0; i < RF_PATH_NUM_8851B; i++)
		rtw89_phy_write32_idx(rtwdev, addr[i] + ofst_ofdm, mask, val,
				      phy_idx);

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[TXPWR] set bb cck txpwr ref\n");
	val = rtw8851b_bb_cal_txpwr_ref(rtwdev, phy_idx, ref_cck);

	for (i = 0; i < RF_PATH_NUM_8851B; i++)
		rtw89_phy_write32_idx(rtwdev, addr[i] + ofst_cck, mask, val,
				      phy_idx);
}

static void rtw8851b_bb_set_tx_shape_dfir(struct rtw89_dev *rtwdev,
					  const struct rtw89_chan *chan,
					  u8 tx_shape_idx,
					  enum rtw89_phy_idx phy_idx)
{
#define __DFIR_CFG_ADDR(i) (R_TXFIR0 + ((i) << 2))
#define __DFIR_CFG_MASK 0xffffffff
#define __DFIR_CFG_NR 8
#define __DECL_DFIR_PARAM(_name, _val...) \
	static const u32 param_ ## _name[] = {_val}; \
	static_assert(ARRAY_SIZE(param_ ## _name) == __DFIR_CFG_NR)

	__DECL_DFIR_PARAM(flat,
			  0x023D23FF, 0x0029B354, 0x000FC1C8, 0x00FDB053,
			  0x00F86F9A, 0x06FAEF92, 0x00FE5FCC, 0x00FFDFF5);
	__DECL_DFIR_PARAM(sharp,
			  0x023D83FF, 0x002C636A, 0x0013F204, 0x00008090,
			  0x00F87FB0, 0x06F99F83, 0x00FDBFBA, 0x00003FF5);
	__DECL_DFIR_PARAM(sharp_14,
			  0x023B13FF, 0x001C42DE, 0x00FDB0AD, 0x00F60F6E,
			  0x00FD8F92, 0x0602D011, 0x0001C02C, 0x00FFF00A);
	u8 ch = chan->channel;
	const u32 *param;
	u32 addr;
	int i;

	if (ch > 14) {
		rtw89_warn(rtwdev,
			   "set tx shape dfir by unknown ch: %d on 2G\n", ch);
		return;
	}

	if (ch == 14)
		param = param_sharp_14;
	else
		param = tx_shape_idx == 0 ? param_flat : param_sharp;

	for (i = 0; i < __DFIR_CFG_NR; i++) {
		addr = __DFIR_CFG_ADDR(i);
		rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
			    "set tx shape dfir: 0x%x: 0x%x\n", addr, param[i]);
		rtw89_phy_write32_idx(rtwdev, addr, __DFIR_CFG_MASK, param[i],
				      phy_idx);
	}

#undef __DECL_DFIR_PARAM
#undef __DFIR_CFG_NR
#undef __DFIR_CFG_MASK
#undef __DECL_CFG_ADDR
}

static void rtw8851b_set_tx_shape(struct rtw89_dev *rtwdev,
				  const struct rtw89_chan *chan,
				  enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_rfe_parms *rfe_parms = rtwdev->rfe_parms;
	u8 band = chan->band_type;
	u8 regd = rtw89_regd_get(rtwdev, band);
	u8 tx_shape_cck = (*rfe_parms->tx_shape.lmt)[band][RTW89_RS_CCK][regd];
	u8 tx_shape_ofdm = (*rfe_parms->tx_shape.lmt)[band][RTW89_RS_OFDM][regd];

	if (band == RTW89_BAND_2G)
		rtw8851b_bb_set_tx_shape_dfir(rtwdev, chan, tx_shape_cck, phy_idx);

	rtw89_phy_write32_mask(rtwdev, R_DCFO_OPT, B_TXSHAPE_TRIANGULAR_CFG,
			       tx_shape_ofdm);
}

static void rtw8851b_set_txpwr(struct rtw89_dev *rtwdev,
			       const struct rtw89_chan *chan,
			       enum rtw89_phy_idx phy_idx)
{
	rtw89_phy_set_txpwr_byrate(rtwdev, chan, phy_idx);
	rtw89_phy_set_txpwr_offset(rtwdev, chan, phy_idx);
	rtw8851b_set_tx_shape(rtwdev, chan, phy_idx);
	rtw89_phy_set_txpwr_limit(rtwdev, chan, phy_idx);
	rtw89_phy_set_txpwr_limit_ru(rtwdev, chan, phy_idx);
}

static void rtw8851b_set_txpwr_ctrl(struct rtw89_dev *rtwdev,
				    enum rtw89_phy_idx phy_idx)
{
	rtw8851b_set_txpwr_ref(rtwdev, phy_idx);
}

static
void rtw8851b_set_txpwr_ul_tb_offset(struct rtw89_dev *rtwdev,
				     s8 pw_ofst, enum rtw89_mac_idx mac_idx)
{
	u32 reg;

	if (pw_ofst < -16 || pw_ofst > 15) {
		rtw89_warn(rtwdev, "[ULTB] Err pwr_offset=%d\n", pw_ofst);
		return;
	}

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_PWR_UL_TB_CTRL, mac_idx);
	rtw89_write32_set(rtwdev, reg, B_AX_PWR_UL_TB_CTRL_EN);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_PWR_UL_TB_1T, mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_AX_PWR_UL_TB_1T_MASK, pw_ofst);

	pw_ofst = max_t(s8, pw_ofst - 3, -16);
	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_PWR_UL_TB_2T, mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_AX_PWR_UL_TB_2T_MASK, pw_ofst);
}

static int
rtw8851b_init_txpwr_unit(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	int ret;

	ret = rtw89_mac_txpwr_write32(rtwdev, phy_idx, R_AX_PWR_UL_CTRL2, 0x07763333);
	if (ret)
		return ret;

	ret = rtw89_mac_txpwr_write32(rtwdev, phy_idx, R_AX_PWR_COEXT_CTRL, 0x01ebf000);
	if (ret)
		return ret;

	ret = rtw89_mac_txpwr_write32(rtwdev, phy_idx, R_AX_PWR_UL_CTRL0, 0x0002f8ff);
	if (ret)
		return ret;

	rtw8851b_set_txpwr_ul_tb_offset(rtwdev, 0, phy_idx == RTW89_PHY_1 ?
						   RTW89_MAC_1 : RTW89_MAC_0);

	return 0;
}

static void rtw8851b_ctrl_nbtg_bt_tx(struct rtw89_dev *rtwdev, bool en,
				     enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);

	rtw89_phy_write_reg3_tbl(rtwdev, en ? &rtw8851b_btc_preagc_en_defs_tbl :
						 &rtw8851b_btc_preagc_dis_defs_tbl);

	if (!en) {
		if (chan->band_type == RTW89_BAND_2G) {
			rtw89_phy_write32_mask(rtwdev, R_PATH0_G_LNA6_OP1DB_V1,
					       B_PATH0_G_LNA6_OP1DB_V1, 0x20);
			rtw89_phy_write32_mask(rtwdev, R_PATH0_G_TIA0_LNA6_OP1DB_V1,
					       B_PATH0_G_TIA0_LNA6_OP1DB_V1, 0x30);
		} else {
			rtw89_phy_write32_mask(rtwdev, R_PATH0_G_LNA6_OP1DB_V1,
					       B_PATH0_G_LNA6_OP1DB_V1, 0x1a);
			rtw89_phy_write32_mask(rtwdev, R_PATH0_G_TIA0_LNA6_OP1DB_V1,
					       B_PATH0_G_TIA0_LNA6_OP1DB_V1, 0x2a);
		}
	}
}

static void rtw8851b_ctrl_btg_bt_rx(struct rtw89_dev *rtwdev, bool en,
				    enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);

	if (en) {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BT_SHARE_V1,
				       B_PATH0_BT_SHARE_V1, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BTG_PATH_V1,
				       B_PATH0_BTG_PATH_V1, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_G_LNA6_OP1DB_V1,
				       B_PATH0_G_LNA6_OP1DB_V1, 0x20);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_G_TIA0_LNA6_OP1DB_V1,
				       B_PATH0_G_TIA0_LNA6_OP1DB_V1, 0x30);
		rtw89_phy_write32_mask(rtwdev, R_PMAC_GNT, B_PMAC_GNT_P1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD_V1, B_BT_SHARE, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_BT_SEG0, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_BT_DYN_DC_EST_EN_V1,
				       B_BT_DYN_DC_EST_EN_MSK, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_GNT_BT_WGT_EN, B_GNT_BT_WGT_EN, 0x1);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BT_SHARE_V1,
				       B_PATH0_BT_SHARE_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BTG_PATH_V1,
				       B_PATH0_BTG_PATH_V1, 0x0);
		if (chan->band_type == RTW89_BAND_2G) {
			rtw89_phy_write32_mask(rtwdev, R_PATH0_G_LNA6_OP1DB_V1,
					       B_PATH0_G_LNA6_OP1DB_V1, 0x80);
			rtw89_phy_write32_mask(rtwdev, R_PATH0_G_TIA0_LNA6_OP1DB_V1,
					       B_PATH0_G_TIA0_LNA6_OP1DB_V1, 0x80);
		} else {
			rtw89_phy_write32_mask(rtwdev, R_PATH0_G_LNA6_OP1DB_V1,
					       B_PATH0_G_LNA6_OP1DB_V1, 0x1a);
			rtw89_phy_write32_mask(rtwdev, R_PATH0_G_TIA0_LNA6_OP1DB_V1,
					       B_PATH0_G_TIA0_LNA6_OP1DB_V1, 0x2a);
		}
		rtw89_phy_write32_mask(rtwdev, R_PMAC_GNT, B_PMAC_GNT_P1, 0xc);
		rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD_V1, B_BT_SHARE, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_BT_SEG0, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_BT_DYN_DC_EST_EN_V1,
				       B_BT_DYN_DC_EST_EN_MSK, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_GNT_BT_WGT_EN, B_GNT_BT_WGT_EN, 0x0);
	}
}

static void rtw8851b_bb_ctrl_rx_path(struct rtw89_dev *rtwdev,
				     enum rtw89_rf_path_bit rx_path)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	u32 rst_mask0;

	if (rx_path == RF_A) {
		rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD_V1, B_ANT_RX_SEG0, 1);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_1RCCA_SEG0, 1);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_1RCCA_SEG1, 1);
		rtw89_phy_write32_mask(rtwdev, R_RXHT_MCS_LIMIT, B_RXHT_MCS_LIMIT, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXVHT_MCS_LIMIT, B_RXVHT_MCS_LIMIT, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_USER_MAX, 4);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_MAX_NSS, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHETB_MAX_NSS, 0);
	}

	rtw8851b_set_gain_offset(rtwdev, chan->subband_type, RTW89_PHY_0);

	rst_mask0 = B_P0_TXPW_RSTB_MANON | B_P0_TXPW_RSTB_TSSI;
	if (rx_path == RF_A) {
		rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB, rst_mask0, 1);
		rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB, rst_mask0, 3);
	}
}

static void rtw8851b_bb_cfg_txrx_path(struct rtw89_dev *rtwdev)
{
	rtw8851b_bb_ctrl_rx_path(rtwdev, RF_A);

	if (rtwdev->hal.rx_nss == 1) {
		rtw89_phy_write32_mask(rtwdev, R_RXHT_MCS_LIMIT, B_RXHT_MCS_LIMIT, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXVHT_MCS_LIMIT, B_RXVHT_MCS_LIMIT, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_MAX_NSS, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHETB_MAX_NSS, 0);
	}

	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_MOD, 0x0, RTW89_PHY_0);
}

static u8 rtw8851b_get_thermal(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path)
{
	if (rtwdev->is_tssi_mode[rf_path]) {
		u32 addr = R_TSSI_THER + (rf_path << 13);

		return rtw89_phy_read32_mask(rtwdev, addr, B_TSSI_THER);
	}

	rtw89_write_rf(rtwdev, rf_path, RR_TM, RR_TM_TRI, 0x1);
	rtw89_write_rf(rtwdev, rf_path, RR_TM, RR_TM_TRI, 0x0);
	rtw89_write_rf(rtwdev, rf_path, RR_TM, RR_TM_TRI, 0x1);

	fsleep(200);

	return rtw89_read_rf(rtwdev, rf_path, RR_TM, RR_TM_VAL);
}

static void rtw8851b_btc_set_rfe(struct rtw89_dev *rtwdev)
{
	const struct rtw89_btc_ver *ver = rtwdev->btc.ver;
	union rtw89_btc_module_info *md = &rtwdev->btc.mdinfo;

	if  (ver->fcxinit == 7) {
		md->md_v7.rfe_type = rtwdev->efuse.rfe_type;
		md->md_v7.kt_ver = rtwdev->hal.cv;
		md->md_v7.bt_solo = 0;
		md->md_v7.switch_type = BTC_SWITCH_INTERNAL;
		md->md_v7.ant.isolation = 10;
		md->md_v7.kt_ver_adie = rtwdev->hal.acv;

		if (md->md_v7.rfe_type == 0)
			return;

		/* rfe_type 3*n+1: 1-Ant(shared),
		 *	    3*n+2: 2-Ant+Div(non-shared),
		 *	    3*n+3: 2-Ant+no-Div(non-shared)
		 */
		md->md_v7.ant.num = (md->md_v7.rfe_type % 3 == 1) ? 1 : 2;
		/* WL-1ss at S0, btg at s0 (On 1 WL RF) */
		md->md_v7.ant.single_pos = RF_PATH_A;
		md->md_v7.ant.btg_pos = RF_PATH_A;
		md->md_v7.ant.stream_cnt = 1;

		if (md->md_v7.ant.num == 1) {
			md->md_v7.ant.type = BTC_ANT_SHARED;
			md->md_v7.bt_pos = BTC_BT_BTG;
			md->md_v7.wa_type = 1;
			md->md_v7.ant.diversity = 0;
		} else { /* ant.num == 2 */
			md->md_v7.ant.type = BTC_ANT_DEDICATED;
			md->md_v7.bt_pos = BTC_BT_ALONE;
			md->md_v7.switch_type = BTC_SWITCH_EXTERNAL;
			md->md_v7.wa_type = 0;
			if (md->md_v7.rfe_type % 3 == 2)
				md->md_v7.ant.diversity = 1;
		}
		rtwdev->btc.btg_pos = md->md_v7.ant.btg_pos;
		rtwdev->btc.ant_type = md->md_v7.ant.type;
	} else {
		md->md.rfe_type = rtwdev->efuse.rfe_type;
		md->md.cv = rtwdev->hal.cv;
		md->md.bt_solo = 0;
		md->md.switch_type = BTC_SWITCH_INTERNAL;
		md->md.ant.isolation = 10;
		md->md.kt_ver_adie = rtwdev->hal.acv;

		if (md->md.rfe_type == 0)
			return;

		/* rfe_type 3*n+1: 1-Ant(shared),
		 *	    3*n+2: 2-Ant+Div(non-shared),
		 *	    3*n+3: 2-Ant+no-Div(non-shared)
		 */
		md->md.ant.num = (md->md.rfe_type % 3 == 1) ? 1 : 2;
		/* WL-1ss at S0, btg at s0 (On 1 WL RF) */
		md->md.ant.single_pos = RF_PATH_A;
		md->md.ant.btg_pos = RF_PATH_A;
		md->md.ant.stream_cnt = 1;

		if (md->md.ant.num == 1) {
			md->md.ant.type = BTC_ANT_SHARED;
			md->md.bt_pos = BTC_BT_BTG;
			md->md.wa_type = 1;
			md->md.ant.diversity = 0;
		} else { /* ant.num == 2 */
			md->md.ant.type = BTC_ANT_DEDICATED;
			md->md.bt_pos = BTC_BT_ALONE;
			md->md.switch_type = BTC_SWITCH_EXTERNAL;
			md->md.wa_type = 0;
			if (md->md.rfe_type % 3 == 2)
				md->md.ant.diversity = 1;
		}
		rtwdev->btc.btg_pos = md->md.ant.btg_pos;
		rtwdev->btc.ant_type = md->md.ant.type;
	}
}

static
void rtw8851b_set_trx_mask(struct rtw89_dev *rtwdev, u8 path, u8 group, u32 val)
{
	if (group > BTC_BT_SS_GROUP)
		group--; /* Tx-group=1, Rx-group=2 */

	if (rtwdev->btc.ant_type == BTC_ANT_SHARED) /* 1-Ant */
		group += 3;

	rtw89_write_rf(rtwdev, path, RR_LUTWA, RFREG_MASK, group);
	rtw89_write_rf(rtwdev, path, RR_LUTWD0, RFREG_MASK, val);
}

static void rtw8851b_btc_init_cfg(struct rtw89_dev *rtwdev)
{
	static const struct rtw89_mac_ax_coex coex_params = {
		.pta_mode = RTW89_MAC_AX_COEX_RTK_MODE,
		.direction = RTW89_MAC_AX_COEX_INNER,
	};
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_btc *btc = &rtwdev->btc;
	union rtw89_btc_module_info *md = &btc->mdinfo;
	const struct rtw89_btc_ver *ver = btc->ver;
	u8 path, path_min, path_max, str_cnt, ant_sing_pos;

	/* PTA init  */
	rtw89_mac_coex_init(rtwdev, &coex_params);

	/* set WL Tx response = Hi-Pri */
	chip->ops->btc_set_wl_pri(rtwdev, BTC_PRI_MASK_TX_RESP, true);
	chip->ops->btc_set_wl_pri(rtwdev, BTC_PRI_MASK_BEACON, true);

	if (ver->fcxinit == 7) {
		str_cnt = md->md_v7.ant.stream_cnt;
		ant_sing_pos = md->md_v7.ant.single_pos;
	} else {
		str_cnt = md->md.ant.stream_cnt;
		ant_sing_pos = md->md.ant.single_pos;
	}

	/* for 1-Ant && 1-ss case: only 1-path */
	if (str_cnt == 1) {
		path_min = ant_sing_pos;
		path_max = path_min;
	} else {
		path_min = RF_PATH_A;
		path_max = RF_PATH_B;
	}

	for (path = path_min; path <= path_max; path++) {
		/* set rf gnt-debug off */
		rtw89_write_rf(rtwdev, path, RR_WLSEL, RFREG_MASK, 0x0);

		/* set DEBUG_LUT_RFMODE_MASK = 1 to start trx-mask-setup */
		rtw89_write_rf(rtwdev, path, RR_LUTWE, RFREG_MASK, BIT(17));

		/* if GNT_WL=0 && BT=SS_group --> WL Tx/Rx = THRU  */
		rtw8851b_set_trx_mask(rtwdev, path, BTC_BT_SS_GROUP, 0x5ff);

		/* if GNT_WL=0 && BT=Rx_group --> WL-Rx = THRU + WL-Tx = MASK */
		rtw8851b_set_trx_mask(rtwdev, path, BTC_BT_RX_GROUP, 0x5df);

		/* if GNT_WL = 0 && BT = Tx_group -->
		 * Shared-Ant && BTG-path:WL mask(0x55f), others:WL THRU(0x5ff)
		 */
		if (btc->ant_type == BTC_ANT_SHARED && btc->btg_pos == path)
			rtw8851b_set_trx_mask(rtwdev, path, BTC_BT_TX_GROUP, 0x55f);
		else
			rtw8851b_set_trx_mask(rtwdev, path, BTC_BT_TX_GROUP, 0x5ff);

		/* set DEBUG_LUT_RFMODE_MASK = 0 to stop trx-mask-setup */
		rtw89_write_rf(rtwdev, path, RR_LUTWE, RFREG_MASK, 0);
	}

	/* set PTA break table */
	rtw89_write32(rtwdev, R_BTC_BREAK_TABLE, BTC_BREAK_PARAM);

	/* enable BT counter 0xda40[16,2] = 2b'11 */
	rtw89_write32_set(rtwdev, R_AX_CSR_MODE, B_AX_BT_CNT_RST | B_AX_STATIS_BT_EN);

	btc->cx.wl.status.map.init_ok = true;
}

static
void rtw8851b_btc_set_wl_pri(struct rtw89_dev *rtwdev, u8 map, bool state)
{
	u32 bitmap;
	u32 reg;

	switch (map) {
	case BTC_PRI_MASK_TX_RESP:
		reg = R_BTC_BT_COEX_MSK_TABLE;
		bitmap = B_BTC_PRI_MASK_TX_RESP_V1;
		break;
	case BTC_PRI_MASK_BEACON:
		reg = R_AX_WL_PRI_MSK;
		bitmap = B_AX_PTA_WL_PRI_MASK_BCNQ;
		break;
	case BTC_PRI_MASK_RX_CCK:
		reg = R_BTC_BT_COEX_MSK_TABLE;
		bitmap = B_BTC_PRI_MASK_RXCCK_V1;
		break;
	default:
		return;
	}

	if (state)
		rtw89_write32_set(rtwdev, reg, bitmap);
	else
		rtw89_write32_clr(rtwdev, reg, bitmap);
}

union rtw8851b_btc_wl_txpwr_ctrl {
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
rtw8851b_btc_set_wl_txpwr_ctrl(struct rtw89_dev *rtwdev, u32 txpwr_val)
{
	union rtw8851b_btc_wl_txpwr_ctrl arg = { .txpwr_val = txpwr_val };
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

static
s8 rtw8851b_btc_get_bt_rssi(struct rtw89_dev *rtwdev, s8 val)
{
	val = clamp_t(s8, val, -100, 0) + 100;
	val = min(val + 6, 100); /* compensate offset */

	return val;
}

static
void rtw8851b_btc_update_bt_cnt(struct rtw89_dev *rtwdev)
{
	/* Feature move to firmware */
}

static void rtw8851b_btc_wl_s1_standby(struct rtw89_dev *rtwdev, bool state)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	rtw89_write_rf(rtwdev, btc->btg_pos, RR_LUTWE, RFREG_MASK, 0x80000);
	rtw89_write_rf(rtwdev, btc->btg_pos, RR_LUTWA, RFREG_MASK, 0x1);
	rtw89_write_rf(rtwdev, btc->btg_pos, RR_LUTWD1, RFREG_MASK, 0x110);

	/* set WL standby = Rx for GNT_BT_Tx = 1->0 settle issue */
	if (state)
		rtw89_write_rf(rtwdev, btc->btg_pos, RR_LUTWD0, RFREG_MASK, 0x179c);
	else
		rtw89_write_rf(rtwdev, btc->btg_pos, RR_LUTWD0, RFREG_MASK, 0x208);

	rtw89_write_rf(rtwdev, btc->btg_pos, RR_LUTWE, RFREG_MASK, 0x0);
}

#define LNA2_51B_MA 0x700

static const struct rtw89_reg2_def btc_8851b_rf_0[] = {{0x2, 0x0}};
static const struct rtw89_reg2_def btc_8851b_rf_1[] = {{0x2, 0x1}};

static void rtw8851b_btc_set_wl_rx_gain(struct rtw89_dev *rtwdev, u32 level)
{
	/* To improve BT ACI in co-rx
	 * level=0 Default: TIA 1/0= (LNA2,TIAN6) = (7,1)/(5,1) = 21dB/12dB
	 * level=1 Fix LNA2=5: TIA 1/0= (LNA2,TIAN6) = (5,0)/(5,1) = 18dB/12dB
	 */
	struct rtw89_btc *btc = &rtwdev->btc;
	const struct rtw89_reg2_def *rf;
	u32 n, i, val;

	switch (level) {
	case 0: /* original */
	default:
		btc->dm.wl_lna2 = 0;
		break;
	case 1: /* for FDD free-run */
		btc->dm.wl_lna2 = 0;
		break;
	case 2: /* for BTG Co-Rx*/
		btc->dm.wl_lna2 = 1;
		break;
	}

	if (btc->dm.wl_lna2 == 0) {
		rf = btc_8851b_rf_0;
		n = ARRAY_SIZE(btc_8851b_rf_0);
	} else {
		rf = btc_8851b_rf_1;
		n = ARRAY_SIZE(btc_8851b_rf_1);
	}

	for (i = 0; i < n; i++, rf++) {
		val = rf->data;
		/* bit[10] = 1 if non-shared-ant for 8851b */
		if (btc->ant_type == BTC_ANT_DEDICATED)
			val |= 0x4;

		rtw89_write_rf(rtwdev, btc->btg_pos, rf->addr, LNA2_51B_MA, val);
	}
}

static void rtw8851b_fill_freq_with_ppdu(struct rtw89_dev *rtwdev,
					 struct rtw89_rx_phy_ppdu *phy_ppdu,
					 struct ieee80211_rx_status *status)
{
	u16 chan = phy_ppdu->chan_idx;
	enum nl80211_band band;
	u8 ch;

	if (chan == 0)
		return;

	rtw89_decode_chan_idx(rtwdev, chan, &ch, &band);
	status->freq = ieee80211_channel_to_frequency(ch, band);
	status->band = band;
}

static void rtw8851b_query_ppdu(struct rtw89_dev *rtwdev,
				struct rtw89_rx_phy_ppdu *phy_ppdu,
				struct ieee80211_rx_status *status)
{
	u8 path;
	u8 *rx_power = phy_ppdu->rssi;

	status->signal = RTW89_RSSI_RAW_TO_DBM(rx_power[RF_PATH_A]);

	for (path = 0; path < rtwdev->chip->rf_path_num; path++) {
		status->chains |= BIT(path);
		status->chain_signal[path] = RTW89_RSSI_RAW_TO_DBM(rx_power[path]);
	}
	if (phy_ppdu->valid)
		rtw8851b_fill_freq_with_ppdu(rtwdev, phy_ppdu, status);
}

static int rtw8851b_mac_enable_bb_rf(struct rtw89_dev *rtwdev)
{
	int ret;

	rtw89_write8_set(rtwdev, R_AX_SYS_FUNC_EN,
			 B_AX_FEN_BBRSTB | B_AX_FEN_BB_GLB_RSTN);
	rtw89_write32_set(rtwdev, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
	rtw89_write32_clr(rtwdev, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
	rtw89_write32_set(rtwdev, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S0, 0xC7,
				      FULL_BIT_MASK);
	if (ret)
		return ret;

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S1, 0xC7,
				      FULL_BIT_MASK);
	if (ret)
		return ret;

	rtw89_write8(rtwdev, R_AX_PHYREG_SET, PHYREG_SET_XYN_CYCLE);

	return 0;
}

static int rtw8851b_mac_disable_bb_rf(struct rtw89_dev *rtwdev)
{
	u8 wl_rfc_s0;
	u8 wl_rfc_s1;
	int ret;

	rtw89_write32_clr(rtwdev, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
	rtw89_write8_clr(rtwdev, R_AX_SYS_FUNC_EN,
			 B_AX_FEN_BBRSTB | B_AX_FEN_BB_GLB_RSTN);

	ret = rtw89_mac_read_xtal_si(rtwdev, XTAL_SI_WL_RFC_S0, &wl_rfc_s0);
	if (ret)
		return ret;
	wl_rfc_s0 &= ~XTAL_SI_RF00S_EN;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S0, wl_rfc_s0,
				      FULL_BIT_MASK);
	if (ret)
		return ret;

	ret = rtw89_mac_read_xtal_si(rtwdev, XTAL_SI_WL_RFC_S1, &wl_rfc_s1);
	if (ret)
		return ret;
	wl_rfc_s1 &= ~XTAL_SI_RF10S_EN;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S1, wl_rfc_s1,
				      FULL_BIT_MASK);
	return ret;
}

static const struct rtw89_chip_ops rtw8851b_chip_ops = {
	.enable_bb_rf		= rtw8851b_mac_enable_bb_rf,
	.disable_bb_rf		= rtw8851b_mac_disable_bb_rf,
	.bb_preinit		= NULL,
	.bb_postinit		= NULL,
	.bb_reset		= rtw8851b_bb_reset,
	.bb_sethw		= rtw8851b_bb_sethw,
	.read_rf		= rtw89_phy_read_rf_v1,
	.write_rf		= rtw89_phy_write_rf_v1,
	.set_channel		= rtw8851b_set_channel,
	.set_channel_help	= rtw8851b_set_channel_help,
	.read_efuse		= rtw8851b_read_efuse,
	.read_phycap		= rtw8851b_read_phycap,
	.fem_setup		= NULL,
	.rfe_gpio		= rtw8851b_rfe_gpio,
	.rfk_hw_init		= NULL,
	.rfk_init		= rtw8851b_rfk_init,
	.rfk_init_late		= NULL,
	.rfk_channel		= rtw8851b_rfk_channel,
	.rfk_band_changed	= rtw8851b_rfk_band_changed,
	.rfk_scan		= rtw8851b_rfk_scan,
	.rfk_track		= rtw8851b_rfk_track,
	.power_trim		= rtw8851b_power_trim,
	.set_txpwr		= rtw8851b_set_txpwr,
	.set_txpwr_ctrl		= rtw8851b_set_txpwr_ctrl,
	.init_txpwr_unit	= rtw8851b_init_txpwr_unit,
	.get_thermal		= rtw8851b_get_thermal,
	.ctrl_btg_bt_rx		= rtw8851b_ctrl_btg_bt_rx,
	.query_ppdu		= rtw8851b_query_ppdu,
	.ctrl_nbtg_bt_tx	= rtw8851b_ctrl_nbtg_bt_tx,
	.cfg_txrx_path		= rtw8851b_bb_cfg_txrx_path,
	.set_txpwr_ul_tb_offset	= rtw8851b_set_txpwr_ul_tb_offset,
	.pwr_on_func		= rtw8851b_pwr_on_func,
	.pwr_off_func		= rtw8851b_pwr_off_func,
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
	.h2c_default_dmac_tbl	= NULL,
	.h2c_update_beacon	= rtw89_fw_h2c_update_beacon,
	.h2c_ba_cam		= rtw89_fw_h2c_ba_cam,

	.btc_set_rfe		= rtw8851b_btc_set_rfe,
	.btc_init_cfg		= rtw8851b_btc_init_cfg,
	.btc_set_wl_pri		= rtw8851b_btc_set_wl_pri,
	.btc_set_wl_txpwr_ctrl	= rtw8851b_btc_set_wl_txpwr_ctrl,
	.btc_get_bt_rssi	= rtw8851b_btc_get_bt_rssi,
	.btc_update_bt_cnt	= rtw8851b_btc_update_bt_cnt,
	.btc_wl_s1_standby	= rtw8851b_btc_wl_s1_standby,
	.btc_set_wl_rx_gain	= rtw8851b_btc_set_wl_rx_gain,
	.btc_set_policy		= rtw89_btc_set_policy_v1,
};

#ifdef CONFIG_PM
static const struct wiphy_wowlan_support rtw_wowlan_stub_8851b = {
	.flags = WIPHY_WOWLAN_MAGIC_PKT | WIPHY_WOWLAN_DISCONNECT,
	.n_patterns = RTW89_MAX_PATTERN_NUM,
	.pattern_max_len = RTW89_MAX_PATTERN_SIZE,
	.pattern_min_len = 1,
};
#endif

const struct rtw89_chip_info rtw8851b_chip_info = {
	.chip_id		= RTL8851B,
	.chip_gen		= RTW89_CHIP_AX,
	.ops			= &rtw8851b_chip_ops,
	.mac_def		= &rtw89_mac_gen_ax,
	.phy_def		= &rtw89_phy_gen_ax,
	.fw_basename		= RTW8851B_FW_BASENAME,
	.fw_format_max		= RTW8851B_FW_FORMAT_MAX,
	.try_ce_fw		= true,
	.bbmcu_nr		= 0,
	.needed_fw_elms		= 0,
	.fifo_size		= 196608,
	.small_fifo_size	= true,
	.dle_scc_rsvd_size	= 98304,
	.max_amsdu_limit	= 3500,
	.dis_2g_40m_ul_ofdma	= true,
	.rsvd_ple_ofst		= 0x2f800,
	.hfc_param_ini		= rtw8851b_hfc_param_ini_pcie,
	.dle_mem		= rtw8851b_dle_mem_pcie,
	.wde_qempty_acq_grpnum	= 4,
	.wde_qempty_mgq_grpsel	= 4,
	.rf_base_addr		= {0xe000},
	.pwr_on_seq		= NULL,
	.pwr_off_seq		= NULL,
	.bb_table		= &rtw89_8851b_phy_bb_table,
	.bb_gain_table		= &rtw89_8851b_phy_bb_gain_table,
	.rf_table		= {&rtw89_8851b_phy_radioa_table,},
	.nctl_table		= &rtw89_8851b_phy_nctl_table,
	.nctl_post_table	= &rtw8851b_nctl_post_defs_tbl,
	.dflt_parms		= &rtw89_8851b_dflt_parms,
	.rfe_parms_conf		= rtw89_8851b_rfe_parms_conf,
	.txpwr_factor_rf	= 2,
	.txpwr_factor_mac	= 1,
	.dig_table		= NULL,
	.dig_regs		= &rtw8851b_dig_regs,
	.tssi_dbw_table		= NULL,
	.support_macid_num	= RTW89_MAX_MAC_ID_NUM,
	.support_chanctx_num	= 0,
	.support_rnr		= false,
	.support_bands		= BIT(NL80211_BAND_2GHZ) |
				  BIT(NL80211_BAND_5GHZ),
	.support_bandwidths	= BIT(NL80211_CHAN_WIDTH_20) |
				  BIT(NL80211_CHAN_WIDTH_40) |
				  BIT(NL80211_CHAN_WIDTH_80),
	.support_unii4		= true,
	.ul_tb_waveform_ctrl	= true,
	.ul_tb_pwr_diff		= false,
	.hw_sec_hdr		= false,
	.rf_path_num		= 1,
	.tx_nss			= 1,
	.rx_nss			= 1,
	.acam_num		= 32,
	.bcam_num		= 20,
	.scam_num		= 128,
	.bacam_num		= 2,
	.bacam_dynamic_num	= 4,
	.bacam_ver		= RTW89_BACAM_V0,
	.ppdu_max_usr		= 4,
	.sec_ctrl_efuse_size	= 4,
	.physical_efuse_size	= 1216,
	.logical_efuse_size	= 2048,
	.limit_efuse_size	= 1280,
	.dav_phy_efuse_size	= 0,
	.dav_log_efuse_size	= 0,
	.efuse_blocks		= NULL,
	.phycap_addr		= 0x580,
	.phycap_size		= 128,
	.para_ver		= 0,
	.wlcx_desired		= 0x06000000,
	.btcx_desired		= 0x7,
	.scbd			= 0x1,
	.mailbox		= 0x1,

	.afh_guard_ch		= 6,
	.wl_rssi_thres		= rtw89_btc_8851b_wl_rssi_thres,
	.bt_rssi_thres		= rtw89_btc_8851b_bt_rssi_thres,
	.rssi_tol		= 2,
	.mon_reg_num		= ARRAY_SIZE(rtw89_btc_8851b_mon_reg),
	.mon_reg		= rtw89_btc_8851b_mon_reg,
	.rf_para_ulink_num	= ARRAY_SIZE(rtw89_btc_8851b_rf_ul),
	.rf_para_ulink		= rtw89_btc_8851b_rf_ul,
	.rf_para_dlink_num	= ARRAY_SIZE(rtw89_btc_8851b_rf_dl),
	.rf_para_dlink		= rtw89_btc_8851b_rf_dl,
	.ps_mode_supported	= BIT(RTW89_PS_MODE_RFOFF) |
				  BIT(RTW89_PS_MODE_CLK_GATED),
	.low_power_hci_modes	= 0,
	.h2c_cctl_func_id	= H2C_FUNC_MAC_CCTLINFO_UD,
	.hci_func_en_addr	= R_AX_HCI_FUNC_EN,
	.h2c_desc_size		= sizeof(struct rtw89_txwd_body),
	.txwd_body_size		= sizeof(struct rtw89_txwd_body),
	.txwd_info_size		= sizeof(struct rtw89_txwd_info),
	.h2c_ctrl_reg		= R_AX_H2CREG_CTRL,
	.h2c_counter_reg	= {R_AX_UDM1 + 1, B_AX_UDM1_HALMAC_H2C_DEQ_CNT_MASK >> 8},
	.h2c_regs		= rtw8851b_h2c_regs,
	.c2h_ctrl_reg		= R_AX_C2HREG_CTRL,
	.c2h_counter_reg	= {R_AX_UDM1 + 1, B_AX_UDM1_HALMAC_C2H_ENQ_CNT_MASK >> 8},
	.c2h_regs		= rtw8851b_c2h_regs,
	.page_regs		= &rtw8851b_page_regs,
	.wow_reason_reg		= rtw8851b_wow_wakeup_regs,
	.cfo_src_fd		= true,
	.cfo_hw_comp		= true,
	.dcfo_comp		= &rtw8851b_dcfo_comp,
	.dcfo_comp_sft		= 12,
	.imr_info		= &rtw8851b_imr_info,
	.imr_dmac_table		= NULL,
	.imr_cmac_table		= NULL,
	.rrsr_cfgs		= &rtw8851b_rrsr_cfgs,
	.bss_clr_vld		= {R_BSS_CLR_MAP_V1, B_BSS_CLR_MAP_VLD0},
	.bss_clr_map_reg	= R_BSS_CLR_MAP_V1,
	.rfkill_init		= &rtw8851b_rfkill_regs,
	.rfkill_get		= {R_AX_GPIO_EXT_CTRL, B_AX_GPIO_IN_9},
	.dma_ch_mask		= BIT(RTW89_DMA_ACH4) | BIT(RTW89_DMA_ACH5) |
				  BIT(RTW89_DMA_ACH6) | BIT(RTW89_DMA_ACH7) |
				  BIT(RTW89_DMA_B1MG) | BIT(RTW89_DMA_B1HI),
	.edcca_regs		= &rtw8851b_edcca_regs,
#ifdef CONFIG_PM
	.wowlan_stub		= &rtw_wowlan_stub_8851b,
#endif
	.xtal_info		= &rtw8851b_xtal_info,
};
EXPORT_SYMBOL(rtw8851b_chip_info);

MODULE_FIRMWARE(RTW8851B_MODULE_FIRMWARE);
MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ax wireless 8851B driver");
MODULE_LICENSE("Dual BSD/GPL");
