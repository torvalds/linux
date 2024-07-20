// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2024 Realtek Corporation
 */

#include "coex.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"
#include "rtw8852bt.h"

#define RTW8852BT_FW_FORMAT_MAX 0
#define RTW8852BT_FW_BASENAME "rtw89/rtw8852bt_fw"
#define RTW8852BT_MODULE_FIRMWARE \
	RTW8852BT_FW_BASENAME ".bin"

static const struct rtw89_hfc_ch_cfg rtw8852bt_hfc_chcfg_pcie[] = {
	{16, 742, grp_0}, /* ACH 0 */
	{16, 742, grp_0}, /* ACH 1 */
	{16, 742, grp_0}, /* ACH 2 */
	{16, 742, grp_0}, /* ACH 3 */
	{0, 0, grp_0}, /* ACH 4 */
	{0, 0, grp_0}, /* ACH 5 */
	{0, 0, grp_0}, /* ACH 6 */
	{0, 0, grp_0}, /* ACH 7 */
	{15, 743, grp_0}, /* B0MGQ */
	{15, 743, grp_0}, /* B0HIQ */
	{0, 0, grp_0}, /* B1MGQ */
	{0, 0, grp_0}, /* B1HIQ */
	{40, 0, 0} /* FWCMDQ */
};

static const struct rtw89_hfc_pub_cfg rtw8852bt_hfc_pubcfg_pcie = {
	958, /* Group 0 */
	0, /* Group 1 */
	958, /* Public Max */
	0 /* WP threshold */
};

static const struct rtw89_hfc_param_ini rtw8852bt_hfc_param_ini_pcie[] = {
	[RTW89_QTA_SCC] = {rtw8852bt_hfc_chcfg_pcie, &rtw8852bt_hfc_pubcfg_pcie,
			   &rtw89_mac_size.hfc_preccfg_pcie, RTW89_HCIFC_POH},
	[RTW89_QTA_DLFW] = {NULL, NULL, &rtw89_mac_size.hfc_preccfg_pcie,
			    RTW89_HCIFC_POH},
	[RTW89_QTA_INVALID] = {NULL},
};

static const struct rtw89_dle_mem rtw8852bt_dle_mem_pcie[] = {
	[RTW89_QTA_SCC] = {RTW89_QTA_SCC, &rtw89_mac_size.wde_size23,
			   &rtw89_mac_size.ple_size9, &rtw89_mac_size.wde_qt23,
			   &rtw89_mac_size.wde_qt23, &rtw89_mac_size.ple_qt57,
			   &rtw89_mac_size.ple_qt59},
	[RTW89_QTA_WOW] = {RTW89_QTA_WOW, &rtw89_mac_size.wde_size23,
			   &rtw89_mac_size.ple_size9, &rtw89_mac_size.wde_qt23,
			   &rtw89_mac_size.wde_qt23, &rtw89_mac_size.ple_qt57,
			   &rtw89_mac_size.ple_qt_52bt_wow},
	[RTW89_QTA_DLFW] = {RTW89_QTA_DLFW, &rtw89_mac_size.wde_size4,
			    &rtw89_mac_size.ple_size4, &rtw89_mac_size.wde_qt4,
			    &rtw89_mac_size.wde_qt4, &rtw89_mac_size.ple_qt13,
			    &rtw89_mac_size.ple_qt13},
	[RTW89_QTA_INVALID] = {RTW89_QTA_INVALID, NULL, NULL, NULL, NULL, NULL,
			       NULL},
};

static const u32 rtw8852bt_h2c_regs[RTW89_H2CREG_MAX] = {
	R_AX_H2CREG_DATA0, R_AX_H2CREG_DATA1,  R_AX_H2CREG_DATA2,
	R_AX_H2CREG_DATA3
};

static const u32 rtw8852bt_c2h_regs[RTW89_C2HREG_MAX] = {
	R_AX_C2HREG_DATA0, R_AX_C2HREG_DATA1, R_AX_C2HREG_DATA2,
	R_AX_C2HREG_DATA3
};

static const u32 rtw8852bt_wow_wakeup_regs[RTW89_WOW_REASON_NUM] = {
	R_AX_C2HREG_DATA3 + 3, R_AX_C2HREG_DATA3 + 3,
};

static const struct rtw89_page_regs rtw8852bt_page_regs = {
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

static const struct rtw89_reg_def rtw8852bt_dcfo_comp = {
	R_DCFO_COMP_S0, B_DCFO_COMP_S0_MSK
};

static const struct rtw89_imr_info rtw8852bt_imr_info = {
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
	.wde_imr_clr		= B_AX_WDE_IMR_CLR_V01,
	.wde_imr_set		= B_AX_WDE_IMR_SET_V01,
	.ple_imr_clr		= B_AX_PLE_IMR_CLR,
	.ple_imr_set		= B_AX_PLE_IMR_SET,
	.host_disp_imr_clr	= B_AX_HOST_DISP_IMR_CLR,
	.host_disp_imr_set	= B_AX_HOST_DISP_IMR_SET_V01,
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
	.phy_intf_imr_clr	= B_AX_PHYINFO_IMR_EN_ALL,
	.phy_intf_imr_set	= B_AX_PHYINFO_IMR_SET,
	.rmac_imr_reg		= R_AX_RMAC_ERR_ISR,
	.rmac_imr_clr		= B_AX_RMAC_IMR_CLR,
	.rmac_imr_set		= B_AX_RMAC_IMR_SET,
	.tmac_imr_reg		= R_AX_TMAC_ERR_IMR_ISR,
	.tmac_imr_clr		= B_AX_TMAC_IMR_CLR,
	.tmac_imr_set		= B_AX_TMAC_IMR_SET,
};

static const struct rtw89_rrsr_cfgs rtw8852bt_rrsr_cfgs = {
	.ref_rate = {R_AX_TRXPTCL_RRSR_CTL_0, B_AX_WMAC_RESP_REF_RATE_SEL, 0},
	.rsc = {R_AX_TRXPTCL_RRSR_CTL_0, B_AX_WMAC_RESP_RSC_MASK, 2},
};

static const struct rtw89_dig_regs rtw8852bt_dig_regs = {
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

static const struct rtw89_edcca_regs rtw8852bt_edcca_regs = {
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

static const struct rtw89_btc_rf_trx_para rtw89_btc_8852bt_rf_ul[] = {
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

static const struct rtw89_btc_rf_trx_para rtw89_btc_8852bt_rf_dl[] = {
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

static const struct rtw89_btc_fbtc_mreg rtw89_btc_8852bt_mon_reg[] = {
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
	RTW89_DEF_FBTC_MREG(REG_BB, 4, 0x4aa4),
	RTW89_DEF_FBTC_MREG(REG_BB, 4, 0x4778),
	RTW89_DEF_FBTC_MREG(REG_BB, 4, 0x476c),
};

static const u8 rtw89_btc_8852bt_wl_rssi_thres[BTC_WL_RSSI_THMAX] = {70, 60, 50, 40};
static const u8 rtw89_btc_8852bt_bt_rssi_thres[BTC_BT_RSSI_THMAX] = {50, 40, 30, 20};

static const struct rtw89_chip_ops rtw8852bt_chip_ops = {
};

#ifdef CONFIG_PM
static const struct wiphy_wowlan_support rtw_wowlan_stub_8852bt = {
	.flags = WIPHY_WOWLAN_MAGIC_PKT | WIPHY_WOWLAN_DISCONNECT,
	.n_patterns = RTW89_MAX_PATTERN_NUM,
	.pattern_max_len = RTW89_MAX_PATTERN_SIZE,
	.pattern_min_len = 1,
};
#endif

const struct rtw89_chip_info rtw8852bt_chip_info = {
	.chip_id		= RTL8852BT,
	.chip_gen		= RTW89_CHIP_AX,
	.ops			= &rtw8852bt_chip_ops,
	.mac_def		= &rtw89_mac_gen_ax,
	.phy_def		= &rtw89_phy_gen_ax,
	.fw_basename		= RTW8852BT_FW_BASENAME,
	.fw_format_max		= RTW8852BT_FW_FORMAT_MAX,
	.try_ce_fw		= true,
	.bbmcu_nr		= 0,
	.needed_fw_elms		= RTW89_AX_GEN_DEF_NEEDED_FW_ELEMENTS_NO_6GHZ,
	.fifo_size		= 458752,
	.small_fifo_size	= true,
	.dle_scc_rsvd_size	= 98304,
	.max_amsdu_limit	= 5000,
	.dis_2g_40m_ul_ofdma	= true,
	.rsvd_ple_ofst		= 0x6f800,
	.hfc_param_ini		= rtw8852bt_hfc_param_ini_pcie,
	.dle_mem		= rtw8852bt_dle_mem_pcie,
	.wde_qempty_acq_grpnum	= 4,
	.wde_qempty_mgq_grpsel	= 4,
	.rf_base_addr		= {0xe000, 0xf000},
	.pwr_on_seq		= NULL,
	.pwr_off_seq		= NULL,
	.bb_table		= NULL,
	.bb_gain_table		= NULL,
	.rf_table		= {},
	.nctl_table		= NULL,
	.nctl_post_table	= NULL,
	.dflt_parms		= NULL,
	.rfe_parms_conf		= NULL,
	.txpwr_factor_rf	= 2,
	.txpwr_factor_mac	= 1,
	.dig_table		= NULL,
	.dig_regs		= &rtw8852bt_dig_regs,
	.tssi_dbw_table		= NULL,
	.support_macid_num	= RTW89_MAX_MAC_ID_NUM,
	.support_chanctx_num	= 1,
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
	.wlcx_desired		= 0x070e0000,
	.btcx_desired		= 0x7,
	.scbd			= 0x1,
	.mailbox		= 0x1,

	.afh_guard_ch		= 6,
	.wl_rssi_thres		= rtw89_btc_8852bt_wl_rssi_thres,
	.bt_rssi_thres		= rtw89_btc_8852bt_bt_rssi_thres,
	.rssi_tol		= 2,
	.mon_reg_num		= ARRAY_SIZE(rtw89_btc_8852bt_mon_reg),
	.mon_reg		= rtw89_btc_8852bt_mon_reg,
	.rf_para_ulink_num	= ARRAY_SIZE(rtw89_btc_8852bt_rf_ul),
	.rf_para_ulink		= rtw89_btc_8852bt_rf_ul,
	.rf_para_dlink_num	= ARRAY_SIZE(rtw89_btc_8852bt_rf_dl),
	.rf_para_dlink		= rtw89_btc_8852bt_rf_dl,
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
	.h2c_regs		= rtw8852bt_h2c_regs,
	.c2h_ctrl_reg		= R_AX_C2HREG_CTRL,
	.c2h_counter_reg	= {R_AX_UDM1 + 1, B_AX_UDM1_HALMAC_C2H_ENQ_CNT_MASK >> 8},
	.c2h_regs		= rtw8852bt_c2h_regs,
	.page_regs		= &rtw8852bt_page_regs,
	.wow_reason_reg		= rtw8852bt_wow_wakeup_regs,
	.cfo_src_fd		= true,
	.cfo_hw_comp		= true,
	.dcfo_comp		= &rtw8852bt_dcfo_comp,
	.dcfo_comp_sft		= 10,
	.imr_info		= &rtw8852bt_imr_info,
	.imr_dmac_table		= NULL,
	.imr_cmac_table		= NULL,
	.rrsr_cfgs		= &rtw8852bt_rrsr_cfgs,
	.bss_clr_vld		= {R_BSS_CLR_MAP_V1, B_BSS_CLR_MAP_VLD0},
	.bss_clr_map_reg	= R_BSS_CLR_MAP_V1,
	.dma_ch_mask		= BIT(RTW89_DMA_ACH4) | BIT(RTW89_DMA_ACH5) |
				  BIT(RTW89_DMA_ACH6) | BIT(RTW89_DMA_ACH7) |
				  BIT(RTW89_DMA_B1MG) | BIT(RTW89_DMA_B1HI),
	.edcca_regs		= &rtw8852bt_edcca_regs,
#ifdef CONFIG_PM
	.wowlan_stub		= &rtw_wowlan_stub_8852bt,
#endif
	.xtal_info		= NULL,
};
EXPORT_SYMBOL(rtw8852bt_chip_info);

MODULE_FIRMWARE(RTW8852BT_MODULE_FIRMWARE);
MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ax wireless 8852BT driver");
MODULE_LICENSE("Dual BSD/GPL");
