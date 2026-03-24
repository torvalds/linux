// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2026  Realtek Corporation
 */

#include "efuse.h"
#include "mac.h"
#include "reg.h"
#include "rtw8922d.h"

#define RTW8922D_FW_FORMAT_MAX 0
#define RTW8922D_FW_BASENAME "rtw89/rtw8922d_fw"
#define RTW8922D_MODULE_FIRMWARE \
	RTW89_GEN_MODULE_FWNAME(RTW8922D_FW_BASENAME, RTW8922D_FW_FORMAT_MAX)

#define RTW8922DS_FW_FORMAT_MAX 0
#define RTW8922DS_FW_BASENAME "rtw89/rtw8922ds_fw"
#define RTW8922DS_MODULE_FIRMWARE \
	RTW89_GEN_MODULE_FWNAME(RTW8922DS_FW_BASENAME, RTW8922DS_FW_FORMAT_MAX)

static const struct rtw89_hfc_ch_cfg rtw8922d_hfc_chcfg_pcie[] = {
	{2, 603, 0}, /* ACH 0 */
	{0, 601, 0}, /* ACH 1 */
	{2, 603, 0}, /* ACH 2 */
	{0, 601, 0}, /* ACH 3 */
	{2, 603, 0}, /* ACH 4 */
	{0, 601, 0}, /* ACH 5 */
	{2, 603, 0}, /* ACH 6 */
	{0, 601, 0}, /* ACH 7 */
	{2, 603, 0}, /* B0MGQ */
	{0, 601, 0}, /* B0HIQ */
	{2, 603, 0}, /* B1MGQ */
	{0, 601, 0}, /* B1HIQ */
	{0, 0, 0}, /* FWCMDQ */
	{0, 0, 0}, /* BMC */
	{0, 0, 0}, /* H2D */
};

static const struct rtw89_hfc_pub_cfg rtw8922d_hfc_pubcfg_pcie = {
	613, /* Group 0 */
	0, /* Group 1 */
	613, /* Public Max */
	0, /* WP threshold */
};

static const struct rtw89_hfc_param_ini rtw8922d_hfc_param_ini_pcie[] = {
	[RTW89_QTA_SCC] = {rtw8922d_hfc_chcfg_pcie, &rtw8922d_hfc_pubcfg_pcie,
			   &rtw89_mac_size.hfc_prec_cfg_c0, RTW89_HCIFC_POH},
	[RTW89_QTA_DBCC] = {rtw8922d_hfc_chcfg_pcie, &rtw8922d_hfc_pubcfg_pcie,
			   &rtw89_mac_size.hfc_prec_cfg_c0, RTW89_HCIFC_POH},
	[RTW89_QTA_DLFW] = {NULL, NULL, &rtw89_mac_size.hfc_prec_cfg_c2,
			    RTW89_HCIFC_POH},
	[RTW89_QTA_INVALID] = {NULL},
};

static const struct rtw89_dle_mem rtw8922d_dle_mem_pcie[] = {
	[RTW89_QTA_SCC] = {RTW89_QTA_SCC, &rtw89_mac_size.wde_size16_v1,
			   &rtw89_mac_size.ple_size20_v1, &rtw89_mac_size.wde_qt19_v1,
			   &rtw89_mac_size.wde_qt19_v1, &rtw89_mac_size.ple_qt42_v2,
			   &rtw89_mac_size.ple_qt43_v2, &rtw89_mac_size.ple_rsvd_qt9,
			   &rtw89_mac_size.rsvd0_size6, &rtw89_mac_size.rsvd1_size2,
			   &rtw89_mac_size.dle_input18},
	[RTW89_QTA_DBCC] = {RTW89_QTA_DBCC, &rtw89_mac_size.wde_size16_v1,
			   &rtw89_mac_size.ple_size20_v1, &rtw89_mac_size.wde_qt19_v1,
			   &rtw89_mac_size.wde_qt19_v1, &rtw89_mac_size.ple_qt42_v2,
			   &rtw89_mac_size.ple_qt43_v2, &rtw89_mac_size.ple_rsvd_qt9,
			   &rtw89_mac_size.rsvd0_size6, &rtw89_mac_size.rsvd1_size2,
			   &rtw89_mac_size.dle_input18},
	[RTW89_QTA_DLFW] = {RTW89_QTA_DLFW, &rtw89_mac_size.wde_size18_v1,
			    &rtw89_mac_size.ple_size22_v1, &rtw89_mac_size.wde_qt3,
			    &rtw89_mac_size.wde_qt3, &rtw89_mac_size.ple_qt5_v2,
			    &rtw89_mac_size.ple_qt5_v2, &rtw89_mac_size.ple_rsvd_qt1,
			    &rtw89_mac_size.rsvd0_size6, &rtw89_mac_size.rsvd1_size2,
			    &rtw89_mac_size.dle_input3},
	[RTW89_QTA_INVALID] = {RTW89_QTA_INVALID, NULL, NULL, NULL, NULL, NULL,
			       NULL},
};

static const u32 rtw8922d_h2c_regs[RTW89_H2CREG_MAX] = {
	R_BE_H2CREG_DATA0, R_BE_H2CREG_DATA1, R_BE_H2CREG_DATA2,
	R_BE_H2CREG_DATA3
};

static const u32 rtw8922d_c2h_regs[RTW89_H2CREG_MAX] = {
	R_BE_C2HREG_DATA0, R_BE_C2HREG_DATA1, R_BE_C2HREG_DATA2,
	R_BE_C2HREG_DATA3
};

static const u32 rtw8922d_wow_wakeup_regs[RTW89_WOW_REASON_NUM] = {
	R_BE_DBG_WOW, R_BE_DBG_WOW,
};

static const struct rtw89_page_regs rtw8922d_page_regs = {
	.hci_fc_ctrl	= R_BE_HCI_FC_CTRL,
	.ch_page_ctrl	= R_BE_CH_PAGE_CTRL,
	.ach_page_ctrl	= R_BE_CH0_PAGE_CTRL,
	.ach_page_info	= R_BE_CH0_PAGE_INFO,
	.pub_page_info3	= R_BE_PUB_PAGE_INFO3,
	.pub_page_ctrl1	= R_BE_PUB_PAGE_CTRL1,
	.pub_page_ctrl2	= R_BE_PUB_PAGE_CTRL2,
	.pub_page_info1	= R_BE_PUB_PAGE_INFO1,
	.pub_page_info2 = R_BE_PUB_PAGE_INFO2,
	.wp_page_ctrl1	= R_BE_WP_PAGE_CTRL1,
	.wp_page_ctrl2	= R_BE_WP_PAGE_CTRL2,
	.wp_page_info1	= R_BE_WP_PAGE_INFO1,
};

static const struct rtw89_reg_imr rtw8922d_imr_dmac_regs[] = {
	{R_BE_HCI_BUF_IMR, B_BE_HCI_BUF_IMR_CLR, B_BE_HCI_BUF_IMR_SET},
	{R_BE_DISP_HOST_IMR, B_BE_DISP_HOST_IMR_CLR_V1, B_BE_DISP_HOST_IMR_SET_V1},
	{R_BE_DISP_CPU_IMR, B_BE_DISP_CPU_IMR_CLR_V1, B_BE_DISP_CPU_IMR_SET_V1},
	{R_BE_DISP_OTHER_IMR, B_BE_DISP_OTHER_IMR_CLR_V1, B_BE_DISP_OTHER_IMR_SET_V1},
	{R_BE_PKTIN_ERR_IMR, B_BE_PKTIN_ERR_IMR_CLR, B_BE_PKTIN_ERR_IMR_SET},
	{R_BE_MLO_ERR_IDCT_IMR, B_BE_MLO_ERR_IDCT_IMR_CLR, B_BE_MLO_ERR_IDCT_IMR_SET},
	{R_BE_MPDU_TX_ERR_IMR, B_BE_MPDU_TX_ERR_IMR_CLR, B_BE_MPDU_TX_ERR_IMR_SET},
	{R_BE_MPDU_RX_ERR_IMR, B_BE_MPDU_RX_ERR_IMR_CLR, B_BE_MPDU_RX_ERR_IMR_SET},
	{R_BE_SEC_ERROR_IMR, B_BE_SEC_ERROR_IMR_CLR, B_BE_SEC_ERROR_IMR_SET},
	{R_BE_CPUIO_ERR_IMR, B_BE_CPUIO_ERR_IMR_CLR, B_BE_CPUIO_ERR_IMR_SET},
	{R_BE_WDE_ERR_IMR, B_BE_WDE_ERR_IMR_CLR, B_BE_WDE_ERR_IMR_SET},
	{R_BE_PLE_ERR_IMR, B_BE_PLE_ERR_IMR_CLR, B_BE_PLE_ERR_IMR_SET},
	{R_BE_WDRLS_ERR_IMR, B_BE_WDRLS_ERR_IMR_CLR, B_BE_WDRLS_ERR_IMR_SET},
	{R_BE_TXPKTCTL_B0_ERRFLAG_IMR, B_BE_TXPKTCTL_B0_ERRFLAG_IMR_CLR,
	 B_BE_TXPKTCTL_B0_ERRFLAG_IMR_SET},
	{R_BE_TXPKTCTL_B1_ERRFLAG_IMR, B_BE_TXPKTCTL_B1_ERRFLAG_IMR_CLR,
	 B_BE_TXPKTCTL_B1_ERRFLAG_IMR_SET},
	{R_BE_BBRPT_COM_ERR_IMR, B_BE_BBRPT_COM_ERR_IMR_CLR, B_BE_BBRPT_COM_ERR_IMR_SET},
	{R_BE_BBRPT_CHINFO_ERR_IMR, B_BE_BBRPT_CHINFO_ERR_IMR_CLR,
	 B_BE_BBRPT_CHINFO_ERR_IMR_SET},
	{R_BE_BBRPT_DFS_ERR_IMR, B_BE_BBRPT_DFS_ERR_IMR_CLR, B_BE_BBRPT_DFS_ERR_IMR_SET},
	{R_BE_LA_ERRFLAG_IMR, B_BE_LA_ERRFLAG_IMR_CLR, B_BE_LA_ERRFLAG_IMR_SET},
	{R_BE_CH_INFO_DBGFLAG_IMR, B_BE_CH_INFO_DBGFLAG_IMR_CLR, B_BE_CH_INFO_DBGFLAG_IMR_SET},
	{R_BE_PLRLS_ERR_IMR_V1, B_BE_PLRLS_ERR_IMR_V1_CLR, B_BE_PLRLS_ERR_IMR_V1_SET},
	{R_BE_HAXI_IDCT_MSK, B_BE_HAXI_IDCT_MSK_CLR, B_BE_HAXI_IDCT_MSK_SET},
};

static const struct rtw89_imr_table rtw8922d_imr_dmac_table = {
	.regs = rtw8922d_imr_dmac_regs,
	.n_regs = ARRAY_SIZE(rtw8922d_imr_dmac_regs),
};

static const struct rtw89_reg_imr rtw8922d_imr_cmac_regs[] = {
	{R_BE_RESP_IMR, B_BE_RESP_IMR_CLR_V1, B_BE_RESP_IMR_SET_V1},
	{R_BE_RESP_IMR1, B_BE_RESP_IMR1_CLR, B_BE_RESP_IMR1_SET},
	{R_BE_RX_ERROR_FLAG_IMR, B_BE_RX_ERROR_FLAG_IMR_CLR_V1, B_BE_RX_ERROR_FLAG_IMR_SET_V1},
	{R_BE_TX_ERROR_FLAG_IMR, B_BE_TX_ERROR_FLAG_IMR_CLR, B_BE_TX_ERROR_FLAG_IMR_SET},
	{R_BE_RX_ERROR_FLAG_IMR_1, B_BE_TX_ERROR_FLAG_IMR_1_CLR, B_BE_TX_ERROR_FLAG_IMR_1_SET},
	{R_BE_PTCL_IMR1, B_BE_PTCL_IMR1_CLR, B_BE_PTCL_IMR1_SET},
	{R_BE_PTCL_IMR0, B_BE_PTCL_IMR0_CLR, B_BE_PTCL_IMR0_SET},
	{R_BE_PTCL_IMR_2, B_BE_PTCL_IMR_2_CLR, B_BE_PTCL_IMR_2_SET},
	{R_BE_SCHEDULE_ERR_IMR, B_BE_SCHEDULE_ERR_IMR_CLR, B_BE_SCHEDULE_ERR_IMR_SET},
	{R_BE_C0_TXPWR_IMR, B_BE_C0_TXPWR_IMR_CLR, B_BE_C0_TXPWR_IMR_SET},
	{R_BE_TRXPTCL_ERROR_INDICA_MASK, B_BE_TRXPTCL_ERROR_INDICA_MASK_CLR,
	 B_BE_TRXPTCL_ERROR_INDICA_MASK_SET},
	{R_BE_RX_ERR_IMR, B_BE_RX_ERR_IMR_CLR, B_BE_RX_ERR_IMR_SET},
	{R_BE_PHYINFO_ERR_IMR_V1, B_BE_PHYINFO_ERR_IMR_V1_CLR, B_BE_PHYINFO_ERR_IMR_V1_SET},
};

static const struct rtw89_imr_table rtw8922d_imr_cmac_table = {
	.regs = rtw8922d_imr_cmac_regs,
	.n_regs = ARRAY_SIZE(rtw8922d_imr_cmac_regs),
};

static const struct rtw89_rrsr_cfgs rtw8922d_rrsr_cfgs = {
	.ref_rate = {R_BE_TRXPTCL_RESP_1, B_BE_WMAC_RESP_REF_RATE_SEL, 0},
	.rsc = {R_BE_PTCL_RRSR1, B_BE_RSC_MASK, 2},
};

static const struct rtw89_rfkill_regs rtw8922d_rfkill_regs = {
	.pinmux = {R_BE_GPIO8_15_FUNC_SEL,
		   B_BE_PINMUX_GPIO9_FUNC_SEL_MASK,
		   0xf},
	.mode = {R_BE_GPIO_EXT_CTRL + 2,
		 (B_BE_GPIO_MOD_9 | B_BE_GPIO_IO_SEL_9) >> 16,
		 0x0},
};

static const struct rtw89_dig_regs rtw8922d_dig_regs = {
	.seg0_pd_reg = R_SEG0R_PD_BE4,
	.pd_lower_bound_mask = B_SEG0R_PD_LOWER_BOUND_MSK,
	.pd_spatial_reuse_en = B_SEG0R_PD_SPATIAL_REUSE_EN_MSK_V1,
	.bmode_pd_reg = R_BMODE_PDTH_EN_BE4,
	.bmode_cca_rssi_limit_en = B_BMODE_PDTH_LIMIT_EN_MSK_V1,
	.bmode_pd_lower_bound_reg = R_BMODE_PDTH_BE4,
	.bmode_rssi_nocca_low_th_mask = B_BMODE_PDTH_LOWER_BOUND_MSK_V1,
	.p0_lna_init = {R_PATH0_LNA_INIT_BE4, B_PATH0_LNA_INIT_IDX_BE4},
	.p1_lna_init = {R_PATH1_LNA_INIT_BE4, B_PATH1_LNA_INIT_IDX_BE4},
	.p0_tia_init = {R_PATH0_TIA_INIT_BE4, B_PATH0_TIA_INIT_IDX_BE4},
	.p1_tia_init = {R_PATH1_TIA_INIT_BE4, B_PATH1_TIA_INIT_IDX_BE4},
	.p0_rxb_init = {R_PATH0_RXIDX_INIT_BE4, B_PATH0_RXIDX_INIT_BE4},
	.p1_rxb_init = {R_PATH1_RXIDX_INIT_BE4, B_PATH1_RXIDX_INIT_BE4},
	.p0_p20_pagcugc_en = {R_PATH0_P20_FOLLOW_BY_PAGCUGC_BE4,
			      B_PATH0_P20_FOLLOW_BY_PAGCUGC_EN_MSK},
	.p0_s20_pagcugc_en = {R_PATH0_S20_FOLLOW_BY_PAGCUGC_BE4,
			      B_PATH0_S20_FOLLOW_BY_PAGCUGC_EN_MSK},
	.p1_p20_pagcugc_en = {R_PATH1_P20_FOLLOW_BY_PAGCUGC_BE4,
			      B_PATH1_P20_FOLLOW_BY_PAGCUGC_EN_MSK},
	.p1_s20_pagcugc_en = {R_PATH1_S20_FOLLOW_BY_PAGCUGC_BE4,
			      B_PATH1_S20_FOLLOW_BY_PAGCUGC_EN_MSK},
};

static const struct rtw89_edcca_regs rtw8922d_edcca_regs = {
	.edcca_level			= R_SEG0R_EDCCA_LVL_BE4,
	.edcca_mask			= B_EDCCA_LVL_MSK0,
	.edcca_p_mask			= B_EDCCA_LVL_MSK1,
	.ppdu_level			= R_SEG0R_PPDU_LVL_BE4,
	.ppdu_mask			= B_EDCCA_LVL_MSK1,
	.p = {{
		.rpt_a			= R_EDCCA_RPT_A_BE4,
		.rpt_b			= R_EDCCA_RPT_B_BE4,
		.rpt_sel		= R_EDCCA_RPT_SEL_BE4,
		.rpt_sel_mask		= B_EDCCA_RPT_SEL_BE4_MSK,
	}, {
		.rpt_a			= R_EDCCA_RPT_A_BE4_C1,
		.rpt_b			= R_EDCCA_RPT_A_BE4_C1,
		.rpt_sel		= R_EDCCA_RPT_SEL_BE4_C1,
		.rpt_sel_mask		= B_EDCCA_RPT_SEL_BE4_MSK,
	}},
	.rpt_sel_be			= R_EDCCA_RPTREG_SEL_BE4,
	.rpt_sel_be_mask		= B_EDCCA_RPTREG_SEL_BE_MSK,
	.tx_collision_t2r_st		= R_TX_COLLISION_T2R_ST_BE4,
	.tx_collision_t2r_st_mask	= B_TX_COLLISION_T2R_ST_BE_M,
};

static const struct rtw89_efuse_block_cfg rtw8922d_efuse_blocks[] = {
	[RTW89_EFUSE_BLOCK_SYS]			= {.offset = 0x00000, .size = 0x310},
	[RTW89_EFUSE_BLOCK_RF]			= {.offset = 0x10000, .size = 0x240},
	[RTW89_EFUSE_BLOCK_HCI_DIG_PCIE_SDIO]	= {.offset = 0x20000, .size = 0x4800},
	[RTW89_EFUSE_BLOCK_HCI_DIG_USB]		= {.offset = 0x30000, .size = 0x890},
	[RTW89_EFUSE_BLOCK_HCI_PHY_PCIE]	= {.offset = 0x40000, .size = 0x400},
	[RTW89_EFUSE_BLOCK_HCI_PHY_USB3]	= {.offset = 0x50000, .size = 0x80},
	[RTW89_EFUSE_BLOCK_HCI_PHY_USB2]	= {.offset = 0x60000, .size = 0x50},
	[RTW89_EFUSE_BLOCK_ADIE]		= {.offset = 0x70000, .size = 0x10},
};

MODULE_FIRMWARE(RTW8922D_MODULE_FIRMWARE);
MODULE_FIRMWARE(RTW8922DS_MODULE_FIRMWARE);
MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11be wireless 8922D driver");
MODULE_LICENSE("Dual BSD/GPL");
