// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2023  Realtek Corporation
 */

#include "coex.h"
#include "debug.h"
#include "efuse.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"
#include "rtw8922a.h"
#include "rtw8922a_rfk.h"

#define RTW8922A_FW_FORMAT_MAX 0
#define RTW8922A_FW_BASENAME "rtw89/rtw8922a_fw"
#define RTW8922A_MODULE_FIRMWARE \
	RTW8922A_FW_BASENAME ".bin"

#define HE_N_USER_MAX_8922A 4

static const struct rtw89_hfc_ch_cfg rtw8922a_hfc_chcfg_pcie[] = {
	{2, 1641, grp_0}, /* ACH 0 */
	{2, 1641, grp_0}, /* ACH 1 */
	{2, 1641, grp_0}, /* ACH 2 */
	{2, 1641, grp_0}, /* ACH 3 */
	{2, 1641, grp_1}, /* ACH 4 */
	{2, 1641, grp_1}, /* ACH 5 */
	{2, 1641, grp_1}, /* ACH 6 */
	{2, 1641, grp_1}, /* ACH 7 */
	{2, 1641, grp_0}, /* B0MGQ */
	{2, 1641, grp_0}, /* B0HIQ */
	{2, 1641, grp_1}, /* B1MGQ */
	{2, 1641, grp_1}, /* B1HIQ */
	{0, 0, 0}, /* FWCMDQ */
	{0, 0, 0}, /* BMC */
	{0, 0, 0}, /* H2D */
};

static const struct rtw89_hfc_pub_cfg rtw8922a_hfc_pubcfg_pcie = {
	1651, /* Group 0 */
	1651, /* Group 1 */
	3302, /* Public Max */
	0, /* WP threshold */
};

static const struct rtw89_hfc_param_ini rtw8922a_hfc_param_ini_pcie[] = {
	[RTW89_QTA_SCC] = {rtw8922a_hfc_chcfg_pcie, &rtw8922a_hfc_pubcfg_pcie,
			   &rtw89_mac_size.hfc_prec_cfg_c0, RTW89_HCIFC_POH},
	[RTW89_QTA_DBCC] = {rtw8922a_hfc_chcfg_pcie, &rtw8922a_hfc_pubcfg_pcie,
			   &rtw89_mac_size.hfc_prec_cfg_c0, RTW89_HCIFC_POH},
	[RTW89_QTA_DLFW] = {NULL, NULL, &rtw89_mac_size.hfc_prec_cfg_c2,
			    RTW89_HCIFC_POH},
	[RTW89_QTA_INVALID] = {NULL},
};

static const struct rtw89_dle_mem rtw8922a_dle_mem_pcie[] = {
	[RTW89_QTA_SCC] = {RTW89_QTA_SCC, &rtw89_mac_size.wde_size0_v1,
			   &rtw89_mac_size.ple_size0_v1, &rtw89_mac_size.wde_qt0_v1,
			   &rtw89_mac_size.wde_qt0_v1, &rtw89_mac_size.ple_qt0,
			   &rtw89_mac_size.ple_qt1, &rtw89_mac_size.ple_rsvd_qt0,
			   &rtw89_mac_size.rsvd0_size0, &rtw89_mac_size.rsvd1_size0},
	[RTW89_QTA_DBCC] = {RTW89_QTA_DBCC, &rtw89_mac_size.wde_size0_v1,
			   &rtw89_mac_size.ple_size0_v1, &rtw89_mac_size.wde_qt0_v1,
			   &rtw89_mac_size.wde_qt0_v1, &rtw89_mac_size.ple_qt0,
			   &rtw89_mac_size.ple_qt1, &rtw89_mac_size.ple_rsvd_qt0,
			   &rtw89_mac_size.rsvd0_size0, &rtw89_mac_size.rsvd1_size0},
	[RTW89_QTA_DLFW] = {RTW89_QTA_DLFW, &rtw89_mac_size.wde_size4_v1,
			    &rtw89_mac_size.ple_size3_v1, &rtw89_mac_size.wde_qt4,
			    &rtw89_mac_size.wde_qt4, &rtw89_mac_size.ple_qt9,
			    &rtw89_mac_size.ple_qt9, &rtw89_mac_size.ple_rsvd_qt1,
			    &rtw89_mac_size.rsvd0_size0, &rtw89_mac_size.rsvd1_size0},
	[RTW89_QTA_INVALID] = {RTW89_QTA_INVALID, NULL, NULL, NULL, NULL, NULL,
			       NULL},
};

static const u32 rtw8922a_h2c_regs[RTW89_H2CREG_MAX] = {
	R_BE_H2CREG_DATA0, R_BE_H2CREG_DATA1, R_BE_H2CREG_DATA2,
	R_BE_H2CREG_DATA3
};

static const u32 rtw8922a_c2h_regs[RTW89_H2CREG_MAX] = {
	R_BE_C2HREG_DATA0, R_BE_C2HREG_DATA1, R_BE_C2HREG_DATA2,
	R_BE_C2HREG_DATA3
};

static const struct rtw89_page_regs rtw8922a_page_regs = {
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

static const struct rtw89_reg_imr rtw8922a_imr_dmac_regs[] = {
	{R_BE_DISP_HOST_IMR, B_BE_DISP_HOST_IMR_CLR, B_BE_DISP_HOST_IMR_SET},
	{R_BE_DISP_CPU_IMR, B_BE_DISP_CPU_IMR_CLR, B_BE_DISP_CPU_IMR_SET},
	{R_BE_DISP_OTHER_IMR, B_BE_DISP_OTHER_IMR_CLR, B_BE_DISP_OTHER_IMR_SET},
	{R_BE_PKTIN_ERR_IMR, B_BE_PKTIN_ERR_IMR_CLR, B_BE_PKTIN_ERR_IMR_SET},
	{R_BE_INTERRUPT_MASK_REG, B_BE_INTERRUPT_MASK_REG_CLR, B_BE_INTERRUPT_MASK_REG_SET},
	{R_BE_MLO_ERR_IDCT_IMR, B_BE_MLO_ERR_IDCT_IMR_CLR, B_BE_MLO_ERR_IDCT_IMR_SET},
	{R_BE_MPDU_TX_ERR_IMR, B_BE_MPDU_TX_ERR_IMR_CLR, B_BE_MPDU_TX_ERR_IMR_SET},
	{R_BE_MPDU_RX_ERR_IMR, B_BE_MPDU_RX_ERR_IMR_CLR, B_BE_MPDU_RX_ERR_IMR_SET},
	{R_BE_SEC_ERROR_IMR, B_BE_SEC_ERROR_IMR_CLR, B_BE_SEC_ERROR_IMR_SET},
	{R_BE_CPUIO_ERR_IMR, B_BE_CPUIO_ERR_IMR_CLR, B_BE_CPUIO_ERR_IMR_SET},
	{R_BE_WDE_ERR_IMR, B_BE_WDE_ERR_IMR_CLR, B_BE_WDE_ERR_IMR_SET},
	{R_BE_WDE_ERR1_IMR, B_BE_WDE_ERR1_IMR_CLR, B_BE_WDE_ERR1_IMR_SET},
	{R_BE_PLE_ERR_IMR, B_BE_PLE_ERR_IMR_CLR, B_BE_PLE_ERR_IMR_SET},
	{R_BE_PLE_ERRFLAG1_IMR, B_BE_PLE_ERRFLAG1_IMR_CLR, B_BE_PLE_ERRFLAG1_IMR_SET},
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
	{R_BE_PLRLS_ERR_IMR, B_BE_PLRLS_ERR_IMR_CLR, B_BE_PLRLS_ERR_IMR_SET},
	{R_BE_HAXI_IDCT_MSK, B_BE_HAXI_IDCT_MSK_CLR, B_BE_HAXI_IDCT_MSK_SET},
};

static const struct rtw89_imr_table rtw8922a_imr_dmac_table = {
	.regs = rtw8922a_imr_dmac_regs,
	.n_regs = ARRAY_SIZE(rtw8922a_imr_dmac_regs),
};

static const struct rtw89_reg_imr rtw8922a_imr_cmac_regs[] = {
	{R_BE_RESP_IMR, B_BE_RESP_IMR_CLR, B_BE_RESP_IMR_SET},
	{R_BE_RX_ERROR_FLAG_IMR, B_BE_RX_ERROR_FLAG_IMR_CLR, B_BE_RX_ERROR_FLAG_IMR_SET},
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

static const struct rtw89_imr_table rtw8922a_imr_cmac_table = {
	.regs = rtw8922a_imr_cmac_regs,
	.n_regs = ARRAY_SIZE(rtw8922a_imr_cmac_regs),
};

static const struct rtw89_rrsr_cfgs rtw8922a_rrsr_cfgs = {
	.ref_rate = {R_BE_TRXPTCL_RESP_1, B_BE_WMAC_RESP_REF_RATE_SEL, 0},
	.rsc = {R_BE_PTCL_RRSR1, B_BE_RSC_MASK, 2},
};

static const struct rtw89_dig_regs rtw8922a_dig_regs = {
	.seg0_pd_reg = R_SEG0R_PD_V2,
	.pd_lower_bound_mask = B_SEG0R_PD_LOWER_BOUND_MSK,
	.pd_spatial_reuse_en = B_SEG0R_PD_SPATIAL_REUSE_EN_MSK_V1,
	.bmode_pd_reg = R_BMODE_PDTH_EN_V2,
	.bmode_cca_rssi_limit_en = B_BMODE_PDTH_LIMIT_EN_MSK_V1,
	.bmode_pd_lower_bound_reg = R_BMODE_PDTH_V2,
	.bmode_rssi_nocca_low_th_mask = B_BMODE_PDTH_LOWER_BOUND_MSK_V1,
	.p0_lna_init = {R_PATH0_LNA_INIT_V1, B_PATH0_LNA_INIT_IDX_MSK},
	.p1_lna_init = {R_PATH1_LNA_INIT_V1, B_PATH1_LNA_INIT_IDX_MSK},
	.p0_tia_init = {R_PATH0_TIA_INIT_V1, B_PATH0_TIA_INIT_IDX_MSK_V1},
	.p1_tia_init = {R_PATH1_TIA_INIT_V1, B_PATH1_TIA_INIT_IDX_MSK_V1},
	.p0_rxb_init = {R_PATH0_RXB_INIT_V1, B_PATH0_RXB_INIT_IDX_MSK_V1},
	.p1_rxb_init = {R_PATH1_RXB_INIT_V1, B_PATH1_RXB_INIT_IDX_MSK_V1},
	.p0_p20_pagcugc_en = {R_PATH0_P20_FOLLOW_BY_PAGCUGC_V3,
			      B_PATH0_P20_FOLLOW_BY_PAGCUGC_EN_MSK},
	.p0_s20_pagcugc_en = {R_PATH0_S20_FOLLOW_BY_PAGCUGC_V3,
			      B_PATH0_S20_FOLLOW_BY_PAGCUGC_EN_MSK},
	.p1_p20_pagcugc_en = {R_PATH1_P20_FOLLOW_BY_PAGCUGC_V3,
			      B_PATH1_P20_FOLLOW_BY_PAGCUGC_EN_MSK},
	.p1_s20_pagcugc_en = {R_PATH1_S20_FOLLOW_BY_PAGCUGC_V3,
			      B_PATH1_S20_FOLLOW_BY_PAGCUGC_EN_MSK},
};

static const struct rtw89_edcca_regs rtw8922a_edcca_regs = {
	.edcca_level			= R_SEG0R_EDCCA_LVL_BE,
	.edcca_mask			= B_EDCCA_LVL_MSK0,
	.edcca_p_mask			= B_EDCCA_LVL_MSK1,
	.ppdu_level			= R_SEG0R_PPDU_LVL_BE,
	.ppdu_mask			= B_EDCCA_LVL_MSK1,
	.rpt_a				= R_EDCCA_RPT_A_BE,
	.rpt_b				= R_EDCCA_RPT_B_BE,
	.rpt_sel			= R_EDCCA_RPT_SEL_BE,
	.rpt_sel_mask			= B_EDCCA_RPT_SEL_MSK,
	.rpt_sel_be			= R_EDCCA_RPTREG_SEL_BE,
	.rpt_sel_be_mask		= B_EDCCA_RPTREG_SEL_BE_MSK,
	.tx_collision_t2r_st		= R_TX_COLLISION_T2R_ST_BE,
	.tx_collision_t2r_st_mask	= B_TX_COLLISION_T2R_ST_BE_M,
};

static const struct rtw89_efuse_block_cfg rtw8922a_efuse_blocks[] = {
	[RTW89_EFUSE_BLOCK_SYS]			= {.offset = 0x00000, .size = 0x310},
	[RTW89_EFUSE_BLOCK_RF]			= {.offset = 0x10000, .size = 0x240},
	[RTW89_EFUSE_BLOCK_HCI_DIG_PCIE_SDIO]	= {.offset = 0x20000, .size = 0x4800},
	[RTW89_EFUSE_BLOCK_HCI_DIG_USB]		= {.offset = 0x30000, .size = 0x890},
	[RTW89_EFUSE_BLOCK_HCI_PHY_PCIE]	= {.offset = 0x40000, .size = 0x200},
	[RTW89_EFUSE_BLOCK_HCI_PHY_USB3]	= {.offset = 0x50000, .size = 0x80},
	[RTW89_EFUSE_BLOCK_HCI_PHY_USB2]	= {.offset = 0x60000, .size = 0x0},
	[RTW89_EFUSE_BLOCK_ADIE]		= {.offset = 0x70000, .size = 0x10},
};

static void rtw8922a_ctrl_btg_bt_rx(struct rtw89_dev *rtwdev, bool en,
				    enum rtw89_phy_idx phy_idx)
{
	if (en) {
		rtw89_phy_write32_idx(rtwdev, R_BT_SHARE_A, B_BT_SHARE_A, 0x1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BT_SHARE_A, B_BTG_PATH_A, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BT_SHARE_B, B_BT_SHARE_B, 0x1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BT_SHARE_B, B_BTG_PATH_B, 0x1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_LNA_OP, B_LNA6, 0x20, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_LNA_TIA, B_TIA0_B, 0x30, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PMAC_GNT, B_PMAC_GNT_P1, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_ANT_CHBW, B_ANT_BT_SHARE, 0x1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_FC0INV_SBW, B_RX_BT_SG0, 0x2, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_GNT_BT_WGT_EN, B_GNT_BT_WGT_EN,
				      0x1, phy_idx);
	} else {
		rtw89_phy_write32_idx(rtwdev, R_BT_SHARE_A, B_BT_SHARE_A, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BT_SHARE_A, B_BTG_PATH_A, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BT_SHARE_B, B_BT_SHARE_B, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BT_SHARE_B, B_BTG_PATH_B, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_LNA_OP, B_LNA6, 0x1a, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_LNA_TIA, B_TIA0_B, 0x2a, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PMAC_GNT, B_PMAC_GNT_P1, 0xc, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_ANT_CHBW, B_ANT_BT_SHARE, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_FC0INV_SBW, B_RX_BT_SG0, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_GNT_BT_WGT_EN, B_GNT_BT_WGT_EN,
				      0x0, phy_idx);
	}
}

static int rtw8922a_pwr_on_func(struct rtw89_dev *rtwdev)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	u32 val32;
	int ret;

	rtw89_write32_clr(rtwdev, R_BE_SYS_PW_CTRL, B_BE_AFSM_WLSUS_EN |
						    B_BE_AFSM_PCIE_SUS_EN);
	rtw89_write32_set(rtwdev, R_BE_SYS_PW_CTRL, B_BE_DIS_WLBT_PDNSUSEN_SOPC);
	rtw89_write32_set(rtwdev, R_BE_WLLPS_CTRL, B_BE_DIS_WLBT_LPSEN_LOPC);
	rtw89_write32_clr(rtwdev, R_BE_SYS_PW_CTRL, B_BE_APDM_HPDN);
	rtw89_write32_clr(rtwdev, R_BE_SYS_PW_CTRL, B_BE_APFM_SWLPS);

	ret = read_poll_timeout(rtw89_read32, val32, val32 & B_BE_RDY_SYSPWR,
				1000, 3000000, false, rtwdev, R_BE_SYS_PW_CTRL);
	if (ret)
		return ret;

	rtw89_write32_set(rtwdev, R_BE_SYS_PW_CTRL, B_BE_EN_WLON);
	rtw89_write32_set(rtwdev, R_BE_WLRESUME_CTRL, B_BE_LPSROP_CMAC0 |
						      B_BE_LPSROP_CMAC1);
	rtw89_write32_set(rtwdev, R_BE_SYS_PW_CTRL, B_BE_APFN_ONMAC);

	ret = read_poll_timeout(rtw89_read32, val32, !(val32 & B_BE_APFN_ONMAC),
				1000, 3000000, false, rtwdev, R_BE_SYS_PW_CTRL);
	if (ret)
		return ret;

	rtw89_write32_clr(rtwdev, R_BE_AFE_ON_CTRL1, B_BE_REG_CK_MON_CK960M_EN);
	rtw89_write8_set(rtwdev, R_BE_ANAPAR_POW_MAC, B_BE_POW_PC_LDO_PORT0 |
						      B_BE_POW_PC_LDO_PORT1);
	rtw89_write32_clr(rtwdev, R_BE_FEN_RST_ENABLE, B_BE_R_SYM_ISO_ADDA_P02PP |
						       B_BE_R_SYM_ISO_ADDA_P12PP);
	rtw89_write8_set(rtwdev, R_BE_PLATFORM_ENABLE, B_BE_PLATFORM_EN);
	rtw89_write32_set(rtwdev, R_BE_HCI_OPT_CTRL, B_BE_HAXIDMA_IO_EN);

	ret = read_poll_timeout(rtw89_read32, val32, val32 & B_BE_HAXIDMA_IO_ST,
				1000, 3000000, false, rtwdev, R_BE_HCI_OPT_CTRL);
	if (ret)
		return ret;

	ret = read_poll_timeout(rtw89_read32, val32, !(val32 & B_BE_HAXIDMA_BACKUP_RESTORE_ST),
				1000, 3000000, false, rtwdev, R_BE_HCI_OPT_CTRL);
	if (ret)
		return ret;

	rtw89_write32_set(rtwdev, R_BE_HCI_OPT_CTRL, B_BE_HCI_WLAN_IO_EN);

	ret = read_poll_timeout(rtw89_read32, val32, val32 & B_BE_HCI_WLAN_IO_ST,
				1000, 3000000, false, rtwdev, R_BE_HCI_OPT_CTRL);
	if (ret)
		return ret;

	rtw89_write32_clr(rtwdev, R_BE_SYS_SDIO_CTRL, B_BE_PCIE_FORCE_IBX_EN);

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_PLL, 0x02, 0x02);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_PLL, 0x01, 0x01);
	if (ret)
		return ret;

	rtw89_write32_set(rtwdev, R_BE_SYS_ADIE_PAD_PWR_CTRL, B_BE_SYM_PADPDN_WL_RFC1_1P3);

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0x40, 0x40);
	if (ret)
		return ret;

	rtw89_write32_set(rtwdev, R_BE_SYS_ADIE_PAD_PWR_CTRL, B_BE_SYM_PADPDN_WL_RFC0_1P3);

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0x20, 0x20);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0x04, 0x04);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0x08, 0x08);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, 0x10);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S0, 0xEB, 0xFF);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S1, 0xEB, 0xFF);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0x01, 0x01);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0x02, 0x02);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, 0x80);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_XREF_RF1, 0, 0x40);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_XREF_RF2, 0, 0x40);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_PLL_1, 0x40, 0x60);
	if (ret)
		return ret;

	if (hal->cv != CHIP_CAV) {
		rtw89_write32_set(rtwdev, R_BE_PMC_DBG_CTRL2, B_BE_SYSON_DIS_PMCR_BE_WRMSK);
		rtw89_write32_set(rtwdev, R_BE_SYS_ISO_CTRL, B_BE_ISO_EB2CORE);
		rtw89_write32_clr(rtwdev, R_BE_SYS_ISO_CTRL, B_BE_PWC_EV2EF_B);

		mdelay(1);

		rtw89_write32_clr(rtwdev, R_BE_SYS_ISO_CTRL, B_BE_PWC_EV2EF_S);
		rtw89_write32_clr(rtwdev, R_BE_PMC_DBG_CTRL2, B_BE_SYSON_DIS_PMCR_BE_WRMSK);
	}

	rtw89_write32_set(rtwdev, R_BE_DMAC_FUNC_EN,
			  B_BE_MAC_FUNC_EN | B_BE_DMAC_FUNC_EN | B_BE_MPDU_PROC_EN |
			  B_BE_WD_RLS_EN | B_BE_DLE_WDE_EN | B_BE_TXPKT_CTRL_EN |
			  B_BE_STA_SCH_EN | B_BE_DLE_PLE_EN | B_BE_PKT_BUF_EN |
			  B_BE_DMAC_TBL_EN | B_BE_PKT_IN_EN | B_BE_DLE_CPUIO_EN |
			  B_BE_DISPATCHER_EN | B_BE_BBRPT_EN | B_BE_MAC_SEC_EN |
			  B_BE_H_AXIDMA_EN | B_BE_DMAC_MLO_EN | B_BE_PLRLS_EN |
			  B_BE_P_AXIDMA_EN | B_BE_DLE_DATACPUIO_EN | B_BE_LTR_CTL_EN);

	set_bit(RTW89_FLAG_DMAC_FUNC, rtwdev->flags);

	rtw89_write32_set(rtwdev, R_BE_CMAC_SHARE_FUNC_EN,
			  B_BE_CMAC_SHARE_EN | B_BE_RESPBA_EN | B_BE_ADDRSRCH_EN |
			  B_BE_BTCOEX_EN);
	rtw89_write32_set(rtwdev, R_BE_CMAC_FUNC_EN,
			  B_BE_CMAC_EN | B_BE_CMAC_TXEN |  B_BE_CMAC_RXEN |
			  B_BE_SIGB_EN | B_BE_PHYINTF_EN | B_BE_CMAC_DMA_EN |
			  B_BE_PTCLTOP_EN | B_BE_SCHEDULER_EN | B_BE_TMAC_EN |
			  B_BE_RMAC_EN | B_BE_TXTIME_EN | B_BE_RESP_PKTCTL_EN);

	set_bit(RTW89_FLAG_CMAC0_FUNC, rtwdev->flags);

	rtw89_write32_set(rtwdev, R_BE_FEN_RST_ENABLE, B_BE_FEN_BB_IP_RSTN |
						       B_BE_FEN_BBPLAT_RSTB);

	if (!test_bit(RTW89_FLAG_PROBE_DONE, rtwdev->flags))
		rtw89_efuse_read_fw_secure_be(rtwdev);

	return 0;
}

static int rtw8922a_pwr_off_func(struct rtw89_dev *rtwdev)
{
	u32 val32;
	int ret;

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0x10, 0x10);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, 0x08);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, 0x04);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S0, 0xC6, 0xFF);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S1, 0xC6, 0xFF);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0x80, 0x80);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, 0x02);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, 0x01);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_PLL, 0x02, 0xFF);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_PLL, 0x00, 0xFF);
	if (ret)
		return ret;

	rtw89_write32_set(rtwdev, R_BE_FEN_RST_ENABLE, B_BE_R_SYM_ISO_ADDA_P02PP |
						       B_BE_R_SYM_ISO_ADDA_P12PP);
	rtw89_write8_clr(rtwdev, R_BE_ANAPAR_POW_MAC, B_BE_POW_PC_LDO_PORT0 |
						      B_BE_POW_PC_LDO_PORT1);
	rtw89_write32_set(rtwdev, R_BE_SYS_PW_CTRL, B_BE_EN_WLON);
	rtw89_write8_clr(rtwdev, R_BE_FEN_RST_ENABLE, B_BE_FEN_BB_IP_RSTN |
						      B_BE_FEN_BBPLAT_RSTB);
	rtw89_write32_clr(rtwdev, R_BE_SYS_ADIE_PAD_PWR_CTRL, B_BE_SYM_PADPDN_WL_RFC0_1P3);

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, 0x20);
	if (ret)
		return ret;

	rtw89_write32_clr(rtwdev, R_BE_SYS_ADIE_PAD_PWR_CTRL, B_BE_SYM_PADPDN_WL_RFC1_1P3);

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, 0x40);
	if (ret)
		return ret;

	rtw89_write32_clr(rtwdev, R_BE_HCI_OPT_CTRL, B_BE_HAXIDMA_IO_EN);

	ret = read_poll_timeout(rtw89_read32, val32, !(val32 & B_BE_HAXIDMA_IO_ST),
				1000, 3000000, false, rtwdev, R_BE_HCI_OPT_CTRL);
	if (ret)
		return ret;

	ret = read_poll_timeout(rtw89_read32, val32, !(val32 & B_BE_HAXIDMA_BACKUP_RESTORE_ST),
				1000, 3000000, false, rtwdev, R_BE_HCI_OPT_CTRL);
	if (ret)
		return ret;

	rtw89_write32_clr(rtwdev, R_BE_HCI_OPT_CTRL, B_BE_HCI_WLAN_IO_EN);

	ret = read_poll_timeout(rtw89_read32, val32, !(val32 & B_BE_HCI_WLAN_IO_ST),
				1000, 3000000, false, rtwdev, R_BE_HCI_OPT_CTRL);
	if (ret)
		return ret;

	rtw89_write32_set(rtwdev, R_BE_SYS_PW_CTRL, B_BE_APFM_OFFMAC);

	ret = read_poll_timeout(rtw89_read32, val32, !(val32 & B_BE_APFM_OFFMAC),
				1000, 3000000, false, rtwdev, R_BE_SYS_PW_CTRL);
	if (ret)
		return ret;

	rtw89_write32(rtwdev, R_BE_WLLPS_CTRL, 0x0000A1B2);
	rtw89_write32_set(rtwdev, R_BE_SYS_PW_CTRL, B_BE_XTAL_OFF_A_DIE);
	rtw89_write32_set(rtwdev, R_BE_SYS_PW_CTRL, B_BE_APFM_SWLPS);
	rtw89_write32(rtwdev, R_BE_UDM1, 0);

	return 0;
}

static void rtw8922a_efuse_parsing_tssi(struct rtw89_dev *rtwdev,
					struct rtw8922a_efuse *map)
{
	struct rtw8922a_tssi_offset *ofst[] = {&map->path_a_tssi, &map->path_b_tssi};
	u8 *bw40_1s_tssi_6g_ofst[] = {map->bw40_1s_tssi_6g_a, map->bw40_1s_tssi_6g_b};
	struct rtw89_tssi_info *tssi = &rtwdev->tssi;
	u8 i, j;

	tssi->thermal[RF_PATH_A] = map->path_a_therm;
	tssi->thermal[RF_PATH_B] = map->path_b_therm;

	for (i = 0; i < RF_PATH_NUM_8922A; i++) {
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
		memcpy(tssi->tssi_6g_mcs[i], bw40_1s_tssi_6g_ofst[i],
		       sizeof(tssi->tssi_6g_mcs[i]));

		for (j = 0; j < TSSI_MCS_CH_GROUP_NUM; j++)
			rtw89_debug(rtwdev, RTW89_DBG_TSSI,
				    "[TSSI][EFUSE] path=%d mcs[%d]=0x%x\n",
				    i, j, tssi->tssi_mcs[i][j]);
	}
}

static void rtw8922a_efuse_parsing_gain_offset(struct rtw89_dev *rtwdev,
					       struct rtw8922a_efuse *map)
{
	struct rtw89_phy_efuse_gain *gain = &rtwdev->efuse_gain;
	bool all_0xff = true, all_0x00 = true;
	int i, j;
	u8 t;

	gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_2G_CCK] = map->rx_gain_a._2g_cck;
	gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_2G_CCK] = map->rx_gain_b._2g_cck;
	gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_2G_OFDM] = map->rx_gain_a._2g_ofdm;
	gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_2G_OFDM] = map->rx_gain_b._2g_ofdm;
	gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_5G_LOW] = map->rx_gain_a._5g_low;
	gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_5G_LOW] = map->rx_gain_b._5g_low;
	gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_5G_MID] = map->rx_gain_a._5g_mid;
	gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_5G_MID] = map->rx_gain_b._5g_mid;
	gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_5G_HIGH] = map->rx_gain_a._5g_high;
	gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_5G_HIGH] = map->rx_gain_b._5g_high;
	gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_6G_L0] = map->rx_gain_6g_a._6g_l0;
	gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_6G_L0] = map->rx_gain_6g_b._6g_l0;
	gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_6G_L1] = map->rx_gain_6g_a._6g_l1;
	gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_6G_L1] = map->rx_gain_6g_b._6g_l1;
	gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_6G_M0] = map->rx_gain_6g_a._6g_m0;
	gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_6G_M0] = map->rx_gain_6g_b._6g_m0;
	gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_6G_M1] = map->rx_gain_6g_a._6g_m1;
	gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_6G_M1] = map->rx_gain_6g_b._6g_m1;
	gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_6G_H0] = map->rx_gain_6g_a._6g_h0;
	gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_6G_H0] = map->rx_gain_6g_b._6g_h0;
	gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_6G_H1] = map->rx_gain_6g_a._6g_h1;
	gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_6G_H1] = map->rx_gain_6g_b._6g_h1;
	gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_6G_UH0] = map->rx_gain_6g_a._6g_uh0;
	gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_6G_UH0] = map->rx_gain_6g_b._6g_uh0;
	gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_6G_UH1] = map->rx_gain_6g_a._6g_uh1;
	gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_6G_UH1] = map->rx_gain_6g_b._6g_uh1;

	for (i = RF_PATH_A; i <= RF_PATH_B; i++)
		for (j = 0; j < RTW89_GAIN_OFFSET_NR; j++) {
			t = gain->offset[i][j];
			if (t != 0xff)
				all_0xff = false;
			if (t != 0x0)
				all_0x00 = false;

			/* transform: sign-bit + U(7,2) to S(8,2) */
			if (t & 0x80)
				gain->offset[i][j] = (t ^ 0x7f) + 1;
		}

	gain->offset_valid = !all_0xff && !all_0x00;
}

static void rtw8922a_read_efuse_mac_addr(struct rtw89_dev *rtwdev, u32 addr)
{
	struct rtw89_efuse *efuse = &rtwdev->efuse;
	u16 val;
	int i;

	for (i = 0; i < ETH_ALEN; i += 2, addr += 2) {
		val = rtw89_read16(rtwdev, addr);
		efuse->addr[i] = val & 0xff;
		efuse->addr[i + 1] = val >> 8;
	}
}

static int rtw8922a_read_efuse_pci_sdio(struct rtw89_dev *rtwdev, u8 *log_map)
{
	struct rtw89_efuse *efuse = &rtwdev->efuse;

	if (rtwdev->hci.type == RTW89_HCI_TYPE_PCIE)
		rtw8922a_read_efuse_mac_addr(rtwdev, 0x3104);
	else
		ether_addr_copy(efuse->addr, log_map + 0x001A);

	return 0;
}

static int rtw8922a_read_efuse_usb(struct rtw89_dev *rtwdev, u8 *log_map)
{
	rtw8922a_read_efuse_mac_addr(rtwdev, 0x4078);

	return 0;
}

static int rtw8922a_read_efuse_rf(struct rtw89_dev *rtwdev, u8 *log_map)
{
	struct rtw8922a_efuse *map = (struct rtw8922a_efuse *)log_map;
	struct rtw89_efuse *efuse = &rtwdev->efuse;

	efuse->rfe_type = map->rfe_type;
	efuse->xtal_cap = map->xtal_k;
	efuse->country_code[0] = map->country_code[0];
	efuse->country_code[1] = map->country_code[1];
	rtw8922a_efuse_parsing_tssi(rtwdev, map);
	rtw8922a_efuse_parsing_gain_offset(rtwdev, map);

	rtw89_info(rtwdev, "chip rfe_type is %d\n", efuse->rfe_type);

	return 0;
}

static int rtw8922a_read_efuse(struct rtw89_dev *rtwdev, u8 *log_map,
			       enum rtw89_efuse_block block)
{
	switch (block) {
	case RTW89_EFUSE_BLOCK_HCI_DIG_PCIE_SDIO:
		return rtw8922a_read_efuse_pci_sdio(rtwdev, log_map);
	case RTW89_EFUSE_BLOCK_HCI_DIG_USB:
		return rtw8922a_read_efuse_usb(rtwdev, log_map);
	case RTW89_EFUSE_BLOCK_RF:
		return rtw8922a_read_efuse_rf(rtwdev, log_map);
	default:
		return 0;
	}
}

#define THM_TRIM_POSITIVE_MASK BIT(6)
#define THM_TRIM_MAGNITUDE_MASK GENMASK(5, 0)

static void rtw8922a_phycap_parsing_thermal_trim(struct rtw89_dev *rtwdev,
						 u8 *phycap_map)
{
	static const u32 thm_trim_addr[RF_PATH_NUM_8922A] = {0x1706, 0x1733};
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	u32 addr = rtwdev->chip->phycap_addr;
	bool pg = true;
	u8 pg_th;
	s8 val;
	u8 i;

	for (i = 0; i < RF_PATH_NUM_8922A; i++) {
		pg_th = phycap_map[thm_trim_addr[i] - addr];
		if (pg_th == 0xff) {
			info->thermal_trim[i] = 0;
			pg = false;
			break;
		}

		val = u8_get_bits(pg_th, THM_TRIM_MAGNITUDE_MASK);

		if (!(pg_th & THM_TRIM_POSITIVE_MASK))
			val *= -1;

		info->thermal_trim[i] = val;

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[THERMAL][TRIM] path=%d thermal_trim=0x%x (%d)\n",
			    i, pg_th, val);
	}

	info->pg_thermal_trim = pg;
}

static void rtw8922a_phycap_parsing_pa_bias_trim(struct rtw89_dev *rtwdev,
						 u8 *phycap_map)
{
	static const u32 pabias_trim_addr[RF_PATH_NUM_8922A] = {0x1707, 0x1734};
	static const u32 check_pa_pad_trim_addr = 0x1700;
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	u32 addr = rtwdev->chip->phycap_addr;
	u8 val;
	u8 i;

	val = phycap_map[check_pa_pad_trim_addr - addr];
	if (val != 0xff)
		info->pg_pa_bias_trim = true;

	for (i = 0; i < RF_PATH_NUM_8922A; i++) {
		info->pa_bias_trim[i] = phycap_map[pabias_trim_addr[i] - addr];

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PA_BIAS][TRIM] path=%d pa_bias_trim=0x%x\n",
			    i, info->pa_bias_trim[i]);
	}
}

static void rtw8922a_pa_bias_trim(struct rtw89_dev *rtwdev)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	u8 pabias_2g, pabias_5g;
	u8 i;

	if (!info->pg_pa_bias_trim) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PA_BIAS][TRIM] no PG, do nothing\n");

		return;
	}

	for (i = 0; i < RF_PATH_NUM_8922A; i++) {
		pabias_2g = FIELD_GET(GENMASK(3, 0), info->pa_bias_trim[i]);
		pabias_5g = FIELD_GET(GENMASK(7, 4), info->pa_bias_trim[i]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PA_BIAS][TRIM] path=%d 2G=0x%x 5G=0x%x\n",
			    i, pabias_2g, pabias_5g);

		rtw89_write_rf(rtwdev, i, RR_BIASA, RR_BIASA_TXG_V1, pabias_2g);
		rtw89_write_rf(rtwdev, i, RR_BIASA, RR_BIASA_TXA_V1, pabias_5g);
	}
}

static void rtw8922a_phycap_parsing_pad_bias_trim(struct rtw89_dev *rtwdev,
						  u8 *phycap_map)
{
	static const u32 pad_bias_trim_addr[RF_PATH_NUM_8922A] = {0x1708, 0x1735};
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	u32 addr = rtwdev->chip->phycap_addr;
	u8 i;

	for (i = 0; i < RF_PATH_NUM_8922A; i++) {
		info->pad_bias_trim[i] = phycap_map[pad_bias_trim_addr[i] - addr];

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PAD_BIAS][TRIM] path=%d pad_bias_trim=0x%x\n",
			    i, info->pad_bias_trim[i]);
	}
}

static void rtw8922a_pad_bias_trim(struct rtw89_dev *rtwdev)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	u8 pad_bias_2g, pad_bias_5g;
	u8 i;

	if (!info->pg_pa_bias_trim) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PAD_BIAS][TRIM] no PG, do nothing\n");
		return;
	}

	for (i = 0; i < RF_PATH_NUM_8922A; i++) {
		pad_bias_2g = u8_get_bits(info->pad_bias_trim[i], GENMASK(3, 0));
		pad_bias_5g = u8_get_bits(info->pad_bias_trim[i], GENMASK(7, 4));

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PAD_BIAS][TRIM] path=%d 2G=0x%x 5G=0x%x\n",
			    i, pad_bias_2g, pad_bias_5g);

		rtw89_write_rf(rtwdev, i, RR_BIASA, RR_BIASD_TXG_V1, pad_bias_2g);
		rtw89_write_rf(rtwdev, i, RR_BIASA, RR_BIASD_TXA_V1, pad_bias_5g);
	}
}

static int rtw8922a_read_phycap(struct rtw89_dev *rtwdev, u8 *phycap_map)
{
	rtw8922a_phycap_parsing_thermal_trim(rtwdev, phycap_map);
	rtw8922a_phycap_parsing_pa_bias_trim(rtwdev, phycap_map);
	rtw8922a_phycap_parsing_pad_bias_trim(rtwdev, phycap_map);

	return 0;
}

static void rtw8922a_power_trim(struct rtw89_dev *rtwdev)
{
	rtw8922a_pa_bias_trim(rtwdev);
	rtw8922a_pad_bias_trim(rtwdev);
}

struct rtw8922a_bb_gain {
	u32 gain_g[BB_PATH_NUM_8922A];
	u32 gain_a[BB_PATH_NUM_8922A];
	u32 gain_g_mask;
	u32 gain_a_mask;
};

static const struct rtw89_reg_def rpl_comp_bw160[RTW89_BW20_SC_160M] = {
	{ .addr = 0x41E8, .mask = 0xFF00},
	{ .addr = 0x41E8, .mask = 0xFF0000},
	{ .addr = 0x41E8, .mask = 0xFF000000},
	{ .addr = 0x41EC, .mask = 0xFF},
	{ .addr = 0x41EC, .mask = 0xFF00},
	{ .addr = 0x41EC, .mask = 0xFF0000},
	{ .addr = 0x41EC, .mask = 0xFF000000},
	{ .addr = 0x41F0, .mask = 0xFF}
};

static const struct rtw89_reg_def rpl_comp_bw80[RTW89_BW20_SC_80M] = {
	{ .addr = 0x41F4, .mask = 0xFF},
	{ .addr = 0x41F4, .mask = 0xFF00},
	{ .addr = 0x41F4, .mask = 0xFF0000},
	{ .addr = 0x41F4, .mask = 0xFF000000}
};

static const struct rtw89_reg_def rpl_comp_bw40[RTW89_BW20_SC_40M] = {
	{ .addr = 0x41F0, .mask = 0xFF0000},
	{ .addr = 0x41F0, .mask = 0xFF000000}
};

static const struct rtw89_reg_def rpl_comp_bw20[RTW89_BW20_SC_20M] = {
	{ .addr = 0x41F0, .mask = 0xFF00}
};

static const struct rtw8922a_bb_gain bb_gain_lna[LNA_GAIN_NUM] = {
	{ .gain_g = {0x409c, 0x449c}, .gain_a = {0x406C, 0x446C},
	  .gain_g_mask = 0xFF00, .gain_a_mask = 0xFF},
	{ .gain_g = {0x409c, 0x449c}, .gain_a = {0x406C, 0x446C},
	  .gain_g_mask = 0xFF000000, .gain_a_mask = 0xFF0000},
	{ .gain_g = {0x40a0, 0x44a0}, .gain_a = {0x4070, 0x4470},
	  .gain_g_mask = 0xFF00, .gain_a_mask = 0xFF},
	{ .gain_g = {0x40a0, 0x44a0}, .gain_a = {0x4070, 0x4470},
	  .gain_g_mask = 0xFF000000, .gain_a_mask = 0xFF0000},
	{ .gain_g = {0x40a4, 0x44a4}, .gain_a = {0x4074, 0x4474},
	  .gain_g_mask = 0xFF00, .gain_a_mask = 0xFF},
	{ .gain_g = {0x40a4, 0x44a4}, .gain_a = {0x4074, 0x4474},
	  .gain_g_mask = 0xFF000000, .gain_a_mask = 0xFF0000},
	{ .gain_g = {0x40a8, 0x44a8}, .gain_a = {0x4078, 0x4478},
	  .gain_g_mask = 0xFF00, .gain_a_mask = 0xFF},
};

static const struct rtw8922a_bb_gain bb_gain_tia[TIA_GAIN_NUM] = {
	{ .gain_g = {0x4054, 0x4454}, .gain_a = {0x4054, 0x4454},
	  .gain_g_mask = 0x7FC0000, .gain_a_mask = 0x1FF},
	{ .gain_g = {0x4058, 0x4458}, .gain_a = {0x4054, 0x4454},
	  .gain_g_mask = 0x1FF, .gain_a_mask = 0x3FE00 },
};

struct rtw8922a_bb_gain_bypass {
	u32 gain_g[BB_PATH_NUM_8922A];
	u32 gain_a[BB_PATH_NUM_8922A];
	u32 gain_mask_g;
	u32 gain_mask_a;
};

static void rtw8922a_set_rpl_gain(struct rtw89_dev *rtwdev,
				  const struct rtw89_chan *chan,
				  enum rtw89_rf_path path,
				  enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_phy_bb_gain_info_be *gain = &rtwdev->bb_gain.be;
	u8 gain_band = rtw89_subband_to_gain_band_be(chan->subband_type);
	u32 reg_path_ofst = 0;
	u32 mask;
	s32 val;
	u32 reg;
	int i;

	if (path == RF_PATH_B)
		reg_path_ofst = 0x400;

	for (i = 0; i < RTW89_BW20_SC_160M; i++) {
		reg = rpl_comp_bw160[i].addr | reg_path_ofst;
		mask = rpl_comp_bw160[i].mask;
		val = gain->rpl_ofst_160[gain_band][path][i];
		rtw89_phy_write32_idx(rtwdev, reg, mask, val, phy_idx);
	}

	for (i = 0; i < RTW89_BW20_SC_80M; i++) {
		reg = rpl_comp_bw80[i].addr | reg_path_ofst;
		mask = rpl_comp_bw80[i].mask;
		val = gain->rpl_ofst_80[gain_band][path][i];
		rtw89_phy_write32_idx(rtwdev, reg, mask, val, phy_idx);
	}

	for (i = 0; i < RTW89_BW20_SC_40M; i++) {
		reg = rpl_comp_bw40[i].addr | reg_path_ofst;
		mask = rpl_comp_bw40[i].mask;
		val = gain->rpl_ofst_40[gain_band][path][i];
		rtw89_phy_write32_idx(rtwdev, reg, mask, val, phy_idx);
	}

	for (i = 0; i < RTW89_BW20_SC_20M; i++) {
		reg = rpl_comp_bw20[i].addr | reg_path_ofst;
		mask = rpl_comp_bw20[i].mask;
		val = gain->rpl_ofst_20[gain_band][path][i];
		rtw89_phy_write32_idx(rtwdev, reg, mask, val, phy_idx);
	}
}

static void rtw8922a_set_lna_tia_gain(struct rtw89_dev *rtwdev,
				      const struct rtw89_chan *chan,
				      enum rtw89_rf_path path,
				      enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_phy_bb_gain_info_be *gain = &rtwdev->bb_gain.be;
	u8 gain_band = rtw89_subband_to_gain_band_be(chan->subband_type);
	enum rtw89_phy_bb_bw_be bw_type;
	s32 val;
	u32 reg;
	u32 mask;
	int i;

	bw_type = chan->band_width <= RTW89_CHANNEL_WIDTH_40 ?
		  RTW89_BB_BW_20_40 : RTW89_BB_BW_80_160_320;

	for (i = 0; i < LNA_GAIN_NUM; i++) {
		if (chan->band_type == RTW89_BAND_2G) {
			reg = bb_gain_lna[i].gain_g[path];
			mask = bb_gain_lna[i].gain_g_mask;
		} else {
			reg = bb_gain_lna[i].gain_a[path];
			mask = bb_gain_lna[i].gain_a_mask;
		}
		val = gain->lna_gain[gain_band][bw_type][path][i];
		rtw89_phy_write32_idx(rtwdev, reg, mask, val, phy_idx);
	}

	for (i = 0; i < TIA_GAIN_NUM; i++) {
		if (chan->band_type == RTW89_BAND_2G) {
			reg = bb_gain_tia[i].gain_g[path];
			mask = bb_gain_tia[i].gain_g_mask;
		} else {
			reg = bb_gain_tia[i].gain_a[path];
			mask = bb_gain_tia[i].gain_a_mask;
		}
		val = gain->tia_gain[gain_band][bw_type][path][i];
		rtw89_phy_write32_idx(rtwdev, reg, mask, val, phy_idx);
	}
}

static void rtw8922a_set_gain(struct rtw89_dev *rtwdev,
			      const struct rtw89_chan *chan,
			      enum rtw89_rf_path path,
			      enum rtw89_phy_idx phy_idx)
{
	rtw8922a_set_lna_tia_gain(rtwdev, chan, path, phy_idx);
	rtw8922a_set_rpl_gain(rtwdev, chan, path, phy_idx);
}

static void rtw8922a_ctrl_ch(struct rtw89_dev *rtwdev,
			     const struct rtw89_chan *chan,
			     enum rtw89_phy_idx phy_idx)
{
	rtw8922a_set_gain(rtwdev, chan, RF_PATH_A, phy_idx);
	rtw8922a_set_gain(rtwdev, chan, RF_PATH_B, phy_idx);
}

static void rtw8922a_ctrl_afe_dac(struct rtw89_dev *rtwdev, enum rtw89_bandwidth bw,
				  enum rtw89_rf_path path)
{
	u32 cr_ofst = 0x0;

	if (path == RF_PATH_B)
		cr_ofst = 0x100;

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_5:
	case RTW89_CHANNEL_WIDTH_10:
	case RTW89_CHANNEL_WIDTH_20:
	case RTW89_CHANNEL_WIDTH_40:
	case RTW89_CHANNEL_WIDTH_80:
		rtw89_phy_write32_mask(rtwdev, R_AFEDAC0 + cr_ofst, B_AFEDAC0, 0xE);
		rtw89_phy_write32_mask(rtwdev, R_AFEDAC1 + cr_ofst, B_AFEDAC1, 0x7);
		break;
	case RTW89_CHANNEL_WIDTH_160:
		rtw89_phy_write32_mask(rtwdev, R_AFEDAC0 + cr_ofst, B_AFEDAC0, 0xD);
		rtw89_phy_write32_mask(rtwdev, R_AFEDAC1 + cr_ofst, B_AFEDAC1, 0x6);
		break;
	default:
		break;
	}
}

static const struct rtw89_reg2_def bb_mcu0_init_reg[] = {
	{0x6990, 0x00000000},
	{0x6994, 0x00000000},
	{0x6998, 0x00000000},
	{0x6820, 0xFFFFFFFE},
	{0x6800, 0xC0000FFE},
	{0x6808, 0x76543210},
	{0x6814, 0xBFBFB000},
	{0x6818, 0x0478C009},
	{0x6800, 0xC0000FFF},
	{0x6820, 0xFFFFFFFF},
};

static const struct rtw89_reg2_def bb_mcu1_init_reg[] = {
	{0x6990, 0x00000000},
	{0x6994, 0x00000000},
	{0x6998, 0x00000000},
	{0x6820, 0xFFFFFFFE},
	{0x6800, 0xC0000FFE},
	{0x6808, 0x76543210},
	{0x6814, 0xBFBFB000},
	{0x6818, 0x0478C009},
	{0x6800, 0xC0000FFF},
	{0x6820, 0xFFFFFFFF},
};

static void rtw8922a_bbmcu_cr_init(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_reg2_def *reg;
	int size;
	int i;

	if (phy_idx == RTW89_PHY_0) {
		reg = bb_mcu0_init_reg;
		size = ARRAY_SIZE(bb_mcu0_init_reg);
	} else {
		reg = bb_mcu1_init_reg;
		size = ARRAY_SIZE(bb_mcu1_init_reg);
	}

	for (i = 0; i < size; i++, reg++)
		rtw89_bbmcu_write32(rtwdev, reg->addr, reg->data, phy_idx);
}

static const u32 dmac_sys_mask[2] = {B_BE_DMAC_BB_PHY0_MASK, B_BE_DMAC_BB_PHY1_MASK};
static const u32 bbrst_mask[2] = {B_BE_FEN_BBPLAT_RSTB, B_BE_FEN_BB1PLAT_RSTB};
static const u32 glbrst_mask[2] = {B_BE_FEN_BB_IP_RSTN, B_BE_FEN_BB1_IP_RSTN};
static const u32 mcu_bootrdy_mask[2] = {B_BE_BOOT_RDY0, B_BE_BOOT_RDY1};

static void rtw8922a_bb_preinit(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	u32 rdy = 0;

	if (phy_idx == RTW89_PHY_1)
		rdy = 1;

	rtw89_write32_mask(rtwdev, R_BE_DMAC_SYS_CR32B, dmac_sys_mask[phy_idx], 0x7FF9);
	rtw89_write32_mask(rtwdev, R_BE_FEN_RST_ENABLE, glbrst_mask[phy_idx], 0x0);
	rtw89_write32_mask(rtwdev, R_BE_FEN_RST_ENABLE, bbrst_mask[phy_idx], 0x0);
	rtw89_write32_mask(rtwdev, R_BE_FEN_RST_ENABLE, glbrst_mask[phy_idx], 0x1);
	rtw89_write32_mask(rtwdev, R_BE_FEN_RST_ENABLE, mcu_bootrdy_mask[phy_idx], rdy);
	rtw89_write32_mask(rtwdev, R_BE_MEM_PWR_CTRL, B_BE_MEM_BBMCU0_DS_V1, 0);

	fsleep(1);
	rtw8922a_bbmcu_cr_init(rtwdev, phy_idx);
}

static void rtw8922a_bb_postinit(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	if (phy_idx == RTW89_PHY_0)
		rtw89_write32_set(rtwdev, R_BE_FEN_RST_ENABLE, mcu_bootrdy_mask[phy_idx]);
	rtw89_write32_set(rtwdev, R_BE_FEN_RST_ENABLE, bbrst_mask[phy_idx]);

	rtw89_phy_write32_set(rtwdev, R_BBCLK, B_CLK_640M);
	rtw89_phy_write32_clr(rtwdev, R_TXSCALE, B_TXFCTR_EN);
	rtw89_phy_set_phy_regs(rtwdev, R_TXFCTR, B_TXFCTR_THD, 0x200);
	rtw89_phy_set_phy_regs(rtwdev, R_SLOPE, B_EHT_RATE_TH, 0xA);
	rtw89_phy_set_phy_regs(rtwdev, R_BEDGE, B_HE_RATE_TH, 0xA);
	rtw89_phy_set_phy_regs(rtwdev, R_BEDGE2, B_HT_VHT_TH, 0xAAA);
	rtw89_phy_set_phy_regs(rtwdev, R_BEDGE, B_EHT_MCS14, 0x1);
	rtw89_phy_set_phy_regs(rtwdev, R_BEDGE2, B_EHT_MCS15, 0x1);
	rtw89_phy_set_phy_regs(rtwdev, R_BEDGE3, B_EHTTB_EN, 0x0);
	rtw89_phy_set_phy_regs(rtwdev, R_BEDGE3, B_HEERSU_EN, 0x0);
	rtw89_phy_set_phy_regs(rtwdev, R_BEDGE3, B_HEMU_EN, 0x0);
	rtw89_phy_set_phy_regs(rtwdev, R_BEDGE3, B_TB_EN, 0x0);
	rtw89_phy_set_phy_regs(rtwdev, R_SU_PUNC, B_SU_PUNC_EN, 0x1);
	rtw89_phy_set_phy_regs(rtwdev, R_BEDGE5, B_HWGEN_EN, 0x1);
	rtw89_phy_set_phy_regs(rtwdev, R_BEDGE5, B_PWROFST_COMP, 0x1);
	rtw89_phy_set_phy_regs(rtwdev, R_MAG_AB, B_BY_SLOPE, 0x1);
	rtw89_phy_set_phy_regs(rtwdev, R_MAG_A, B_MGA_AEND, 0xe0);
	rtw89_phy_set_phy_regs(rtwdev, R_MAG_AB, B_MAG_AB, 0xe0c000);
	rtw89_phy_set_phy_regs(rtwdev, R_SLOPE, B_SLOPE_A, 0x3FE0);
	rtw89_phy_set_phy_regs(rtwdev, R_SLOPE, B_SLOPE_B, 0x3FE0);
	rtw89_phy_set_phy_regs(rtwdev, R_SC_CORNER, B_SC_CORNER, 0x200);
	rtw89_phy_write32_idx(rtwdev, R_UDP_COEEF, B_UDP_COEEF, 0x0, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_UDP_COEEF, B_UDP_COEEF, 0x1, phy_idx);
}

static void rtw8922a_bb_reset_en(struct rtw89_dev *rtwdev, enum rtw89_band band,
				 bool en, enum rtw89_phy_idx phy_idx)
{
	if (en) {
		rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1, phy_idx);
		if (band == RTW89_BAND_2G)
			rtw89_phy_write32_idx(rtwdev, R_RXCCA_BE1,
					      B_RXCCA_BE1_DIS, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PD_CTRL, B_PD_HIT_DIS, 0x0, phy_idx);
	} else {
		rtw89_phy_write32_idx(rtwdev, R_RXCCA_BE1, B_RXCCA_BE1_DIS, 0x1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PD_CTRL, B_PD_HIT_DIS, 0x1, phy_idx);
		fsleep(1);
		rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 0, phy_idx);
	}
}

static int rtw8922a_ctrl_tx_path_tmac(struct rtw89_dev *rtwdev,
				      enum rtw89_rf_path tx_path,
				      enum rtw89_phy_idx phy_idx)
{
	struct rtw89_reg2_def path_com_cr[] = {
		{0x11A00, 0x21C86900},
		{0x11A04, 0x00E4E433},
		{0x11A08, 0x39390CC9},
		{0x11A0C, 0x4E433240},
		{0x11A10, 0x90CC900E},
		{0x11A14, 0x00240393},
		{0x11A18, 0x201C8600},
	};
	int ret = 0;
	u32 reg;
	int i;

	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL, 0x0, phy_idx);

	if (phy_idx == RTW89_PHY_1 && !rtwdev->dbcc_en)
		return 0;

	if (tx_path == RF_PATH_A) {
		path_com_cr[0].data = 0x21C82900;
		path_com_cr[1].data = 0x00E4E431;
		path_com_cr[2].data = 0x39390C49;
		path_com_cr[3].data = 0x4E431240;
		path_com_cr[4].data = 0x90C4900E;
		path_com_cr[6].data = 0x201C8200;
	} else if (tx_path == RF_PATH_B) {
		path_com_cr[0].data = 0x21C04900;
		path_com_cr[1].data = 0x00E4E032;
		path_com_cr[2].data = 0x39380C89;
		path_com_cr[3].data = 0x4E032240;
		path_com_cr[4].data = 0x80C8900E;
		path_com_cr[6].data = 0x201C0400;
	} else if (tx_path == RF_PATH_AB) {
		path_com_cr[0].data = 0x21C86900;
		path_com_cr[1].data = 0x00E4E433;
		path_com_cr[2].data = 0x39390CC9;
		path_com_cr[3].data = 0x4E433240;
		path_com_cr[4].data = 0x90CC900E;
		path_com_cr[6].data = 0x201C8600;
	} else {
		ret = -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(path_com_cr); i++) {
		reg = rtw89_mac_reg_by_idx(rtwdev, path_com_cr[i].addr, phy_idx);
		rtw89_write32(rtwdev, reg, path_com_cr[i].data);
	}

	return ret;
}

static void rtw8922a_bb_reset(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
}

static int rtw8922a_cfg_rx_nss_limit(struct rtw89_dev *rtwdev, u8 rx_nss,
				     enum rtw89_phy_idx phy_idx)
{
	if (rx_nss == 1) {
		rtw89_phy_write32_idx(rtwdev, R_BRK_R, B_HTMCS_LMT, 0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BRK_R, B_VHTMCS_LMT, 0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BRK_HE, B_N_USR_MAX,
				      HE_N_USER_MAX_8922A, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BRK_HE, B_NSS_MAX, 0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BRK_HE, B_TB_NSS_MAX, 0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BRK_EHT, B_RXEHT_NSS_MAX, 0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BRK_RXEHT, B_RXEHTTB_NSS_MAX, 0,
				      phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BRK_RXEHT, B_RXEHT_N_USER_MAX,
				      HE_N_USER_MAX_8922A, phy_idx);
	} else if (rx_nss == 2) {
		rtw89_phy_write32_idx(rtwdev, R_BRK_R, B_HTMCS_LMT, 1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BRK_R, B_VHTMCS_LMT, 1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BRK_HE, B_N_USR_MAX,
				      HE_N_USER_MAX_8922A, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BRK_HE, B_NSS_MAX, 1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BRK_HE, B_TB_NSS_MAX, 1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BRK_EHT, B_RXEHT_NSS_MAX, 1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BRK_RXEHT, B_RXEHTTB_NSS_MAX, 1,
				      phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BRK_RXEHT, B_RXEHT_N_USER_MAX,
				      HE_N_USER_MAX_8922A, phy_idx);
	} else {
		return -EINVAL;
	}

	return 0;
}

static void rtw8922a_tssi_reset(struct rtw89_dev *rtwdev,
				enum rtw89_rf_path path,
				enum rtw89_phy_idx phy_idx)
{
	if (rtwdev->mlo_dbcc_mode == MLO_1_PLUS_1_1RF) {
		if (phy_idx == RTW89_PHY_0) {
			rtw89_phy_write32_mask(rtwdev, R_TXPWR_RSTA, B_TXPWR_RSTA, 0x0);
			rtw89_phy_write32_mask(rtwdev, R_TXPWR_RSTA, B_TXPWR_RSTA, 0x1);
		} else {
			rtw89_phy_write32_mask(rtwdev, R_TXPWR_RSTB, B_TXPWR_RSTB, 0x0);
			rtw89_phy_write32_mask(rtwdev, R_TXPWR_RSTB, B_TXPWR_RSTB, 0x1);
		}
	} else {
		rtw89_phy_write32_mask(rtwdev, R_TXPWR_RSTA, B_TXPWR_RSTA, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_TXPWR_RSTA, B_TXPWR_RSTA, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_TXPWR_RSTB, B_TXPWR_RSTB, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_TXPWR_RSTB, B_TXPWR_RSTB, 0x1);
	}
}

static int rtw8922a_ctrl_rx_path_tmac(struct rtw89_dev *rtwdev,
				      enum rtw89_rf_path rx_path,
				      enum rtw89_phy_idx phy_idx)
{
	u8 rx_nss = (rx_path == RF_PATH_AB) ? 2 : 1;

	/* Set to 0 first to avoid abnormal EDCCA report */
	rtw89_phy_write32_idx(rtwdev, R_ANT_CHBW, B_ANT_RX_SG0, 0x0, phy_idx);

	if (rx_path == RF_PATH_A) {
		rtw89_phy_write32_idx(rtwdev, R_ANT_CHBW, B_ANT_RX_SG0, 0x1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_FC0INV_SBW, B_RX_1RCCA, 1, phy_idx);
		rtw8922a_cfg_rx_nss_limit(rtwdev, rx_nss, phy_idx);
		rtw8922a_tssi_reset(rtwdev, rx_path, phy_idx);
	} else if (rx_path == RF_PATH_B) {
		rtw89_phy_write32_idx(rtwdev, R_ANT_CHBW, B_ANT_RX_SG0, 0x2, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_FC0INV_SBW, B_RX_1RCCA, 2, phy_idx);
		rtw8922a_cfg_rx_nss_limit(rtwdev, rx_nss, phy_idx);
		rtw8922a_tssi_reset(rtwdev, rx_path, phy_idx);
	} else if (rx_path == RF_PATH_AB) {
		rtw89_phy_write32_idx(rtwdev, R_ANT_CHBW, B_ANT_RX_SG0, 0x3, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_FC0INV_SBW, B_RX_1RCCA, 3, phy_idx);
		rtw8922a_cfg_rx_nss_limit(rtwdev, rx_nss, phy_idx);
		rtw8922a_tssi_reset(rtwdev, rx_path, phy_idx);
	} else {
		return -EINVAL;
	}

	return 0;
}

static int rtw8922a_ctrl_mlo(struct rtw89_dev *rtwdev, enum rtw89_mlo_dbcc_mode mode)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);

	if (mode == MLO_1_PLUS_1_1RF || mode == DBCC_LEGACY) {
		rtw89_phy_write32_mask(rtwdev, R_DBCC, B_DBCC_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_DBCC_FA, B_DBCC_FA, 0x0);
	} else if (mode == MLO_2_PLUS_0_1RF || mode == MLO_0_PLUS_2_1RF ||
		   mode == MLO_DBCC_NOT_SUPPORT) {
		rtw89_phy_write32_mask(rtwdev, R_DBCC, B_DBCC_EN, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_DBCC_FA, B_DBCC_FA, 0x1);
	} else {
		return -EOPNOTSUPP;
	}

	if (mode == MLO_2_PLUS_0_1RF) {
		rtw8922a_ctrl_afe_dac(rtwdev, chan->band_width, RF_PATH_A);
		rtw8922a_ctrl_afe_dac(rtwdev, chan->band_width, RF_PATH_B);
	} else {
		rtw89_warn(rtwdev, "unsupported MLO mode %d\n", mode);
	}

	rtw89_phy_write32_mask(rtwdev, R_EMLSR, B_EMLSR_PARM, 0x6180);

	if (mode == MLO_2_PLUS_0_1RF) {
		rtw89_phy_write32_mask(rtwdev, R_EMLSR, B_EMLSR_PARM, 0xBBAB);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR, B_EMLSR_PARM, 0xABA9);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR, B_EMLSR_PARM, 0xEBA9);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR, B_EMLSR_PARM, 0xEAA9);
	} else if (mode == MLO_0_PLUS_2_1RF) {
		rtw89_phy_write32_mask(rtwdev, R_EMLSR, B_EMLSR_PARM, 0xBBAB);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR, B_EMLSR_PARM, 0xAFFF);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR, B_EMLSR_PARM, 0xEFFF);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR, B_EMLSR_PARM, 0xEEFF);
	} else if ((mode == MLO_1_PLUS_1_1RF) || (mode == DBCC_LEGACY)) {
		rtw89_phy_write32_mask(rtwdev, R_EMLSR, B_EMLSR_PARM, 0x7BAB);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR, B_EMLSR_PARM, 0x3BAB);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR, B_EMLSR_PARM, 0x3AAB);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_EMLSR, B_EMLSR_PARM, 0x180);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR, B_EMLSR_PARM, 0x0);
	}

	return 0;
}

static void rtw8922a_bb_sethw(struct rtw89_dev *rtwdev)
{
	u32 reg;

	rtw89_phy_write32_clr(rtwdev, R_EN_SND_WO_NDP, B_EN_SND_WO_NDP);
	rtw89_phy_write32_clr(rtwdev, R_EN_SND_WO_NDP_C1, B_EN_SND_WO_NDP);

	rtw89_write32_mask(rtwdev, R_BE_PWR_BOOST, B_BE_PWR_CTRL_SEL, 0);
	if (rtwdev->dbcc_en) {
		reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_BOOST, RTW89_MAC_1);
		rtw89_write32_mask(rtwdev, reg, B_BE_PWR_CTRL_SEL, 0);
	}

	rtw8922a_ctrl_mlo(rtwdev, rtwdev->mlo_dbcc_mode);
}

static void rtw8922a_set_channel_bb(struct rtw89_dev *rtwdev,
				    const struct rtw89_chan *chan,
				    enum rtw89_phy_idx phy_idx)
{
	rtw8922a_ctrl_ch(rtwdev, chan, phy_idx);
}

static void rtw8922a_set_channel(struct rtw89_dev *rtwdev,
				 const struct rtw89_chan *chan,
				 enum rtw89_mac_idx mac_idx,
				 enum rtw89_phy_idx phy_idx)
{
	rtw8922a_set_channel_bb(rtwdev, chan, phy_idx);
}

static void rtw8922a_dfs_en_idx(struct rtw89_dev *rtwdev,
				enum rtw89_phy_idx phy_idx, enum rtw89_rf_path path,
				bool en)
{
	u32 path_ofst = (path == RF_PATH_B) ? 0x100 : 0x0;

	if (en)
		rtw89_phy_write32_idx(rtwdev, 0x2800 + path_ofst, BIT(1), 1,
				      phy_idx);
	else
		rtw89_phy_write32_idx(rtwdev, 0x2800 + path_ofst, BIT(1), 0,
				      phy_idx);
}

static void rtw8922a_dfs_en(struct rtw89_dev *rtwdev, bool en,
			    enum rtw89_phy_idx phy_idx)
{
	rtw8922a_dfs_en_idx(rtwdev, phy_idx, RF_PATH_A, en);
	rtw8922a_dfs_en_idx(rtwdev, phy_idx, RF_PATH_B, en);
}

static void rtw8922a_adc_en_path(struct rtw89_dev *rtwdev,
				 enum rtw89_rf_path path, bool en)
{
	u32 val;

	val = rtw89_phy_read32_mask(rtwdev, R_ADC_FIFO_V1, B_ADC_FIFO_EN_V1);

	if (en) {
		if (path == RF_PATH_A)
			val &= ~0x1;
		else
			val &= ~0x2;
	} else {
		if (path == RF_PATH_A)
			val |= 0x1;
		else
			val |= 0x2;
	}

	rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO_V1, B_ADC_FIFO_EN_V1, val);
}

static void rtw8922a_adc_en(struct rtw89_dev *rtwdev, bool en, u8 phy_idx)
{
	if (rtwdev->mlo_dbcc_mode == MLO_1_PLUS_1_1RF) {
		if (phy_idx == RTW89_PHY_0)
			rtw8922a_adc_en_path(rtwdev, RF_PATH_A, en);
		else
			rtw8922a_adc_en_path(rtwdev, RF_PATH_B, en);
	} else {
		rtw8922a_adc_en_path(rtwdev, RF_PATH_A, en);
		rtw8922a_adc_en_path(rtwdev, RF_PATH_B, en);
	}
}

static
void rtw8922a_hal_reset(struct rtw89_dev *rtwdev,
			enum rtw89_phy_idx phy_idx, enum rtw89_mac_idx mac_idx,
			enum rtw89_band band, u32 *tx_en, bool enter)
{
	if (enter) {
		rtw89_chip_stop_sch_tx(rtwdev, mac_idx, tx_en, RTW89_SCH_TX_SEL_ALL);
		rtw89_mac_cfg_ppdu_status(rtwdev, mac_idx, false);
		rtw8922a_dfs_en(rtwdev, false, phy_idx);
		rtw8922a_tssi_cont_en_phyidx(rtwdev, false, phy_idx);
		rtw8922a_adc_en(rtwdev, false, phy_idx);
		fsleep(40);
		rtw8922a_bb_reset_en(rtwdev, band, false, phy_idx);
	} else {
		rtw89_mac_cfg_ppdu_status(rtwdev, mac_idx, true);
		rtw8922a_adc_en(rtwdev, true, phy_idx);
		rtw8922a_dfs_en(rtwdev, true, phy_idx);
		rtw8922a_tssi_cont_en_phyidx(rtwdev, true, phy_idx);
		rtw8922a_bb_reset_en(rtwdev, band, true, phy_idx);
		rtw89_chip_resume_sch_tx(rtwdev, mac_idx, *tx_en);
	}
}

static void rtw8922a_rfk_init(struct rtw89_dev *rtwdev)
{
	struct rtw89_rfk_mcc_info *rfk_mcc = &rtwdev->rfk_mcc;

	rtwdev->is_tssi_mode[RF_PATH_A] = false;
	rtwdev->is_tssi_mode[RF_PATH_B] = false;
	memset(rfk_mcc, 0, sizeof(*rfk_mcc));
}

static void rtw8922a_rfk_init_late(struct rtw89_dev *rtwdev)
{
	rtw89_phy_rfk_pre_ntfy_and_wait(rtwdev, RTW89_PHY_0, 5);

	rtw89_phy_rfk_dack_and_wait(rtwdev, RTW89_PHY_0, 58);
	rtw89_phy_rfk_rxdck_and_wait(rtwdev, RTW89_PHY_0, 32);
}

static void _wait_rx_mode(struct rtw89_dev *rtwdev, u8 kpath)
{
	u32 rf_mode;
	u8 path;
	int ret;

	for (path = 0; path < RF_PATH_NUM_8922A; path++) {
		if (!(kpath & BIT(path)))
			continue;

		ret = read_poll_timeout_atomic(rtw89_read_rf, rf_mode, rf_mode != 2,
					       2, 5000, false, rtwdev, path, 0x00,
					       RR_MOD_MASK);
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[RFK] Wait S%d to Rx mode!! (ret = %d)\n",
			    path, ret);
	}
}

static void rtw8922a_rfk_channel(struct rtw89_dev *rtwdev)
{
	enum rtw89_phy_idx phy_idx = RTW89_PHY_0;
	u8 phy_map = rtw89_btc_phymap(rtwdev, phy_idx, RF_AB);
	u32 tx_en;

	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_CHLK, BTC_WRFK_START);
	rtw89_chip_stop_sch_tx(rtwdev, phy_idx, &tx_en, RTW89_SCH_TX_SEL_ALL);
	_wait_rx_mode(rtwdev, RF_AB);

	rtw89_phy_rfk_pre_ntfy_and_wait(rtwdev, phy_idx, 5);
	rtw89_phy_rfk_txgapk_and_wait(rtwdev, phy_idx, 54);
	rtw89_phy_rfk_iqk_and_wait(rtwdev, phy_idx, 84);
	rtw89_phy_rfk_tssi_and_wait(rtwdev, phy_idx, RTW89_TSSI_NORMAL, 6);
	rtw89_phy_rfk_dpk_and_wait(rtwdev, phy_idx, 34);
	rtw89_phy_rfk_rxdck_and_wait(rtwdev, RTW89_PHY_0, 32);

	rtw89_chip_resume_sch_tx(rtwdev, phy_idx, tx_en);
	rtw89_btc_ntfy_wl_rfk(rtwdev, phy_map, BTC_WRFKT_CHLK, BTC_WRFK_STOP);
}

static void rtw8922a_rfk_band_changed(struct rtw89_dev *rtwdev,
				      enum rtw89_phy_idx phy_idx)
{
	rtw89_phy_rfk_tssi_and_wait(rtwdev, phy_idx, RTW89_TSSI_SCAN, 6);
}

static void rtw8922a_rfk_scan(struct rtw89_dev *rtwdev, bool start)
{
}

static void rtw8922a_rfk_track(struct rtw89_dev *rtwdev)
{
}

static void rtw8922a_set_txpwr_ref(struct rtw89_dev *rtwdev,
				   enum rtw89_phy_idx phy_idx)
{
	s16 ref_ofdm = 0;
	s16 ref_cck = 0;

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[TXPWR] set txpwr reference\n");

	rtw89_mac_txpwr_write32_mask(rtwdev, phy_idx, R_BE_PWR_REF_CTRL,
				     B_BE_PWR_REF_CTRL_OFDM, ref_ofdm);
	rtw89_mac_txpwr_write32_mask(rtwdev, phy_idx, R_BE_PWR_REF_CTRL,
				     B_BE_PWR_REF_CTRL_CCK, ref_cck);
}

static void rtw8922a_bb_tx_triangular(struct rtw89_dev *rtwdev, bool en,
				      enum rtw89_phy_idx phy_idx)
{
	u8 ctrl = en ? 0x1 : 0x0;

	rtw89_phy_write32_idx(rtwdev, R_BEDGE3, B_BEDGE_CFG, ctrl, phy_idx);
}

static void rtw8922a_set_tx_shape(struct rtw89_dev *rtwdev,
				  const struct rtw89_chan *chan,
				  enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_rfe_parms *rfe_parms = rtwdev->rfe_parms;
	const struct rtw89_tx_shape *tx_shape = &rfe_parms->tx_shape;
	u8 tx_shape_idx;
	u8 band, regd;

	band = chan->band_type;
	regd = rtw89_regd_get(rtwdev, band);
	tx_shape_idx = (*tx_shape->lmt)[band][RTW89_RS_OFDM][regd];

	if (tx_shape_idx == 0)
		rtw8922a_bb_tx_triangular(rtwdev, false, phy_idx);
	else
		rtw8922a_bb_tx_triangular(rtwdev, true, phy_idx);
}

static void rtw8922a_set_txpwr(struct rtw89_dev *rtwdev,
			       const struct rtw89_chan *chan,
			       enum rtw89_phy_idx phy_idx)
{
	rtw89_phy_set_txpwr_byrate(rtwdev, chan, phy_idx);
	rtw89_phy_set_txpwr_offset(rtwdev, chan, phy_idx);
	rtw8922a_set_tx_shape(rtwdev, chan, phy_idx);
	rtw89_phy_set_txpwr_limit(rtwdev, chan, phy_idx);
	rtw89_phy_set_txpwr_limit_ru(rtwdev, chan, phy_idx);
}

static void rtw8922a_set_txpwr_ctrl(struct rtw89_dev *rtwdev,
				    enum rtw89_phy_idx phy_idx)
{
	rtw8922a_set_txpwr_ref(rtwdev, phy_idx);
}

static void rtw8922a_ctrl_trx_path(struct rtw89_dev *rtwdev,
				   enum rtw89_rf_path tx_path, u8 tx_nss,
				   enum rtw89_rf_path rx_path, u8 rx_nss)
{
	enum rtw89_phy_idx phy_idx;

	for (phy_idx = RTW89_PHY_0; phy_idx <= RTW89_PHY_1; phy_idx++) {
		rtw8922a_ctrl_tx_path_tmac(rtwdev, tx_path, phy_idx);
		rtw8922a_ctrl_rx_path_tmac(rtwdev, rx_path, phy_idx);
		rtw8922a_cfg_rx_nss_limit(rtwdev, rx_nss, phy_idx);
	}
}

static void rtw8922a_ctrl_nbtg_bt_tx(struct rtw89_dev *rtwdev, bool en,
				     enum rtw89_phy_idx phy_idx)
{
	if (en) {
		rtw89_phy_write32_idx(rtwdev, R_FORCE_FIR_A, B_FORCE_FIR_A, 0x3, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXBY_WBADC_A, B_RXBY_WBADC_A,
				      0xf, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BT_RXBY_WBADC_A, B_BT_RXBY_WBADC_A,
				      0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BT_SHARE_A, B_BT_TRK_OFF_A, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_OP1DB_A, B_OP1DB_A, 0x80, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_OP1DB1_A, B_TIA10_A, 0x8080, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BACKOFF_A, B_LNA_IBADC_A, 0x34, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BKOFF_A, B_BKOFF_IBADC_A, 0x34, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_FORCE_FIR_B, B_FORCE_FIR_B, 0x3, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXBY_WBADC_B, B_RXBY_WBADC_B,
				      0xf, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BT_RXBY_WBADC_B, B_BT_RXBY_WBADC_B,
				      0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BT_SHARE_B, B_BT_TRK_OFF_B, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_LNA_OP, B_LNA6, 0x80, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_LNA_TIA, B_TIA10_B, 0x8080, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BACKOFF_B, B_LNA_IBADC_B, 0x34, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BKOFF_B, B_BKOFF_IBADC_B, 0x34, phy_idx);
	} else {
		rtw89_phy_write32_idx(rtwdev, R_FORCE_FIR_A, B_FORCE_FIR_A, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXBY_WBADC_A, B_RXBY_WBADC_A,
				      0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BT_RXBY_WBADC_A, B_BT_RXBY_WBADC_A,
				      0x1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BT_SHARE_A, B_BT_TRK_OFF_A, 0x1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_OP1DB_A, B_OP1DB_A, 0x1a, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_OP1DB1_A, B_TIA10_A, 0x2a2a, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BACKOFF_A, B_LNA_IBADC_A, 0x7a6, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BKOFF_A, B_BKOFF_IBADC_A, 0x26, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_FORCE_FIR_B, B_FORCE_FIR_B, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXBY_WBADC_B, B_RXBY_WBADC_B,
				      0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BT_RXBY_WBADC_B, B_BT_RXBY_WBADC_B,
				      0x1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BT_SHARE_B, B_BT_TRK_OFF_B, 0x1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_LNA_OP, B_LNA6, 0x20, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_LNA_TIA, B_TIA10_B, 0x2a30, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BACKOFF_B, B_LNA_IBADC_B, 0x7a6, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BKOFF_B, B_BKOFF_IBADC_B, 0x26, phy_idx);
	}
}

static void rtw8922a_bb_cfg_txrx_path(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	enum rtw89_band band = chan->band_type;
	struct rtw89_hal *hal = &rtwdev->hal;
	u8 ntx_path = RF_PATH_AB;
	u32 tx_en0, tx_en1;

	if (hal->antenna_tx == RF_A)
		ntx_path = RF_PATH_A;
	else if (hal->antenna_tx == RF_B)
		ntx_path = RF_PATH_B;

	rtw8922a_hal_reset(rtwdev, RTW89_PHY_0, RTW89_MAC_0, band, &tx_en0, true);
	if (rtwdev->dbcc_en)
		rtw8922a_hal_reset(rtwdev, RTW89_PHY_1, RTW89_MAC_1, band,
				   &tx_en1, true);

	rtw8922a_ctrl_trx_path(rtwdev, ntx_path, 2, RF_PATH_AB, 2);

	rtw8922a_hal_reset(rtwdev, RTW89_PHY_0, RTW89_MAC_0, band, &tx_en0, false);
	if (rtwdev->dbcc_en)
		rtw8922a_hal_reset(rtwdev, RTW89_PHY_1, RTW89_MAC_1, band,
				   &tx_en0, false);
}

static u8 rtw8922a_get_thermal(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	int th;

	/* read thermal only if debugging */
	if (!rtw89_debug_is_enabled(rtwdev, RTW89_DBG_CFO | RTW89_DBG_RFK_TRACK))
		return 80;

	rtw89_write_rf(rtwdev, rf_path, RR_TM, RR_TM_TRI, 0x1);
	rtw89_write_rf(rtwdev, rf_path, RR_TM, RR_TM_TRI, 0x0);
	rtw89_write_rf(rtwdev, rf_path, RR_TM, RR_TM_TRI, 0x1);

	fsleep(200);

	th = rtw89_read_rf(rtwdev, rf_path, RR_TM, RR_TM_VAL_V1);
	th += (s8)info->thermal_trim[rf_path];

	return clamp_t(int, th, 0, U8_MAX);
}

static void rtw8922a_fill_freq_with_ppdu(struct rtw89_dev *rtwdev,
					 struct rtw89_rx_phy_ppdu *phy_ppdu,
					 struct ieee80211_rx_status *status)
{
	u8 chan_idx = phy_ppdu->chan_idx;
	enum nl80211_band band;
	u8 ch;

	if (chan_idx == 0)
		return;

	rtw89_decode_chan_idx(rtwdev, chan_idx, &ch, &band);
	status->freq = ieee80211_channel_to_frequency(ch, band);
	status->band = band;
}

static void rtw8922a_query_ppdu(struct rtw89_dev *rtwdev,
				struct rtw89_rx_phy_ppdu *phy_ppdu,
				struct ieee80211_rx_status *status)
{
	u8 path;
	u8 *rx_power = phy_ppdu->rssi;

	status->signal =
		RTW89_RSSI_RAW_TO_DBM(max(rx_power[RF_PATH_A], rx_power[RF_PATH_B]));
	for (path = 0; path < rtwdev->chip->rf_path_num; path++) {
		status->chains |= BIT(path);
		status->chain_signal[path] = RTW89_RSSI_RAW_TO_DBM(rx_power[path]);
	}
	if (phy_ppdu->valid)
		rtw8922a_fill_freq_with_ppdu(rtwdev, phy_ppdu, status);
}

static int rtw8922a_mac_enable_bb_rf(struct rtw89_dev *rtwdev)
{
	rtw89_write8_set(rtwdev, R_BE_FEN_RST_ENABLE,
			 B_BE_FEN_BBPLAT_RSTB | B_BE_FEN_BB_IP_RSTN);
	rtw89_write32(rtwdev, R_BE_DMAC_SYS_CR32B, 0x7FF97FF9);

	return 0;
}

static int rtw8922a_mac_disable_bb_rf(struct rtw89_dev *rtwdev)
{
	rtw89_write8_clr(rtwdev, R_BE_FEN_RST_ENABLE,
			 B_BE_FEN_BBPLAT_RSTB | B_BE_FEN_BB_IP_RSTN);

	return 0;
}

#ifdef CONFIG_PM
static const struct wiphy_wowlan_support rtw_wowlan_stub_8922a = {
	.flags = WIPHY_WOWLAN_MAGIC_PKT | WIPHY_WOWLAN_DISCONNECT,
	.n_patterns = RTW89_MAX_PATTERN_NUM,
	.pattern_max_len = RTW89_MAX_PATTERN_SIZE,
	.pattern_min_len = 1,
};
#endif

static const struct rtw89_chip_ops rtw8922a_chip_ops = {
	.enable_bb_rf		= rtw8922a_mac_enable_bb_rf,
	.disable_bb_rf		= rtw8922a_mac_disable_bb_rf,
	.bb_preinit		= rtw8922a_bb_preinit,
	.bb_postinit		= rtw8922a_bb_postinit,
	.bb_reset		= rtw8922a_bb_reset,
	.bb_sethw		= rtw8922a_bb_sethw,
	.read_rf		= rtw89_phy_read_rf_v2,
	.write_rf		= rtw89_phy_write_rf_v2,
	.set_channel		= rtw8922a_set_channel,
	.read_efuse		= rtw8922a_read_efuse,
	.read_phycap		= rtw8922a_read_phycap,
	.fem_setup		= NULL,
	.rfe_gpio		= NULL,
	.rfk_hw_init		= rtw8922a_rfk_hw_init,
	.rfk_init		= rtw8922a_rfk_init,
	.rfk_init_late		= rtw8922a_rfk_init_late,
	.rfk_channel		= rtw8922a_rfk_channel,
	.rfk_band_changed	= rtw8922a_rfk_band_changed,
	.rfk_scan		= rtw8922a_rfk_scan,
	.rfk_track		= rtw8922a_rfk_track,
	.power_trim		= rtw8922a_power_trim,
	.set_txpwr		= rtw8922a_set_txpwr,
	.set_txpwr_ctrl		= rtw8922a_set_txpwr_ctrl,
	.init_txpwr_unit	= NULL,
	.get_thermal		= rtw8922a_get_thermal,
	.ctrl_btg_bt_rx		= rtw8922a_ctrl_btg_bt_rx,
	.query_ppdu		= rtw8922a_query_ppdu,
	.ctrl_nbtg_bt_tx	= rtw8922a_ctrl_nbtg_bt_tx,
	.cfg_txrx_path		= rtw8922a_bb_cfg_txrx_path,
	.set_txpwr_ul_tb_offset	= NULL,
	.pwr_on_func		= rtw8922a_pwr_on_func,
	.pwr_off_func		= rtw8922a_pwr_off_func,
	.query_rxdesc		= rtw89_core_query_rxdesc_v2,
	.fill_txdesc		= rtw89_core_fill_txdesc_v2,
	.fill_txdesc_fwcmd	= rtw89_core_fill_txdesc_fwcmd_v2,
	.stop_sch_tx		= rtw89_mac_stop_sch_tx_v2,
	.resume_sch_tx		= rtw89_mac_resume_sch_tx_v2,
	.h2c_dctl_sec_cam	= rtw89_fw_h2c_dctl_sec_cam_v2,
	.h2c_default_cmac_tbl	= rtw89_fw_h2c_default_cmac_tbl_g7,
	.h2c_assoc_cmac_tbl	= rtw89_fw_h2c_assoc_cmac_tbl_g7,
	.h2c_ampdu_cmac_tbl	= rtw89_fw_h2c_ampdu_cmac_tbl_g7,
	.h2c_default_dmac_tbl	= rtw89_fw_h2c_default_dmac_tbl_v2,
	.h2c_update_beacon	= rtw89_fw_h2c_update_beacon_be,
	.h2c_ba_cam		= rtw89_fw_h2c_ba_cam_v1,
};

const struct rtw89_chip_info rtw8922a_chip_info = {
	.chip_id		= RTL8922A,
	.chip_gen		= RTW89_CHIP_BE,
	.ops			= &rtw8922a_chip_ops,
	.mac_def		= &rtw89_mac_gen_be,
	.phy_def		= &rtw89_phy_gen_be,
	.fw_basename		= RTW8922A_FW_BASENAME,
	.fw_format_max		= RTW8922A_FW_FORMAT_MAX,
	.try_ce_fw		= false,
	.bbmcu_nr		= 1,
	.needed_fw_elms		= RTW89_BE_GEN_DEF_NEEDED_FW_ELEMENTS,
	.fifo_size		= 589824,
	.small_fifo_size	= false,
	.dle_scc_rsvd_size	= 0,
	.max_amsdu_limit	= 8000,
	.dis_2g_40m_ul_ofdma	= false,
	.rsvd_ple_ofst		= 0x8f800,
	.hfc_param_ini		= rtw8922a_hfc_param_ini_pcie,
	.dle_mem		= rtw8922a_dle_mem_pcie,
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
	.dflt_parms		= NULL, /* load parm from fw */
	.rfe_parms_conf		= NULL, /* load parm from fw */
	.txpwr_factor_rf	= 2,
	.txpwr_factor_mac	= 1,
	.dig_table		= NULL,
	.dig_regs		= &rtw8922a_dig_regs,
	.tssi_dbw_table		= NULL,
	.support_chanctx_num	= 2,
	.support_bands		= BIT(NL80211_BAND_2GHZ) |
				  BIT(NL80211_BAND_5GHZ) |
				  BIT(NL80211_BAND_6GHZ),
	.support_bandwidths	= BIT(NL80211_CHAN_WIDTH_20) |
				  BIT(NL80211_CHAN_WIDTH_40) |
				  BIT(NL80211_CHAN_WIDTH_80) |
				  BIT(NL80211_CHAN_WIDTH_160),
	.support_unii4		= true,
	.ul_tb_waveform_ctrl	= false,
	.ul_tb_pwr_diff		= false,
	.hw_sec_hdr		= true,
	.rf_path_num		= 2,
	.tx_nss			= 2,
	.rx_nss			= 2,
	.acam_num		= 128,
	.bcam_num		= 20,
	.scam_num		= 32,
	.bacam_num		= 24,
	.bacam_dynamic_num	= 8,
	.bacam_ver		= RTW89_BACAM_V1,
	.ppdu_max_usr		= 16,
	.sec_ctrl_efuse_size	= 4,
	.physical_efuse_size	= 0x1300,
	.logical_efuse_size	= 0x70000,
	.limit_efuse_size	= 0x40000,
	.dav_phy_efuse_size	= 0,
	.dav_log_efuse_size	= 0,
	.efuse_blocks		= rtw8922a_efuse_blocks,
	.phycap_addr		= 0x1700,
	.phycap_size		= 0x38,

	.ps_mode_supported	= BIT(RTW89_PS_MODE_RFOFF) |
				  BIT(RTW89_PS_MODE_CLK_GATED) |
				  BIT(RTW89_PS_MODE_PWR_GATED),
	.low_power_hci_modes	= 0,
	.h2c_cctl_func_id	= H2C_FUNC_MAC_CCTLINFO_UD_G7,
	.hci_func_en_addr	= R_BE_HCI_FUNC_EN,
	.h2c_desc_size		= sizeof(struct rtw89_rxdesc_short_v2),
	.txwd_body_size		= sizeof(struct rtw89_txwd_body_v2),
	.txwd_info_size		= sizeof(struct rtw89_txwd_info_v2),
	.h2c_ctrl_reg		= R_BE_H2CREG_CTRL,
	.h2c_counter_reg	= {R_BE_UDM1 + 1, B_BE_UDM1_HALMAC_H2C_DEQ_CNT_MASK >> 8},
	.h2c_regs		= rtw8922a_h2c_regs,
	.c2h_ctrl_reg		= R_BE_C2HREG_CTRL,
	.c2h_counter_reg	= {R_BE_UDM1 + 1, B_BE_UDM1_HALMAC_C2H_ENQ_CNT_MASK >> 8},
	.c2h_regs		= rtw8922a_c2h_regs,
	.page_regs		= &rtw8922a_page_regs,
	.cfo_src_fd		= true,
	.cfo_hw_comp            = true,
	.dcfo_comp		= NULL,
	.dcfo_comp_sft		= 0,
	.imr_info		= NULL,
	.imr_dmac_table		= &rtw8922a_imr_dmac_table,
	.imr_cmac_table		= &rtw8922a_imr_cmac_table,
	.rrsr_cfgs		= &rtw8922a_rrsr_cfgs,
	.bss_clr_vld		= {R_BSS_CLR_VLD_V2, B_BSS_CLR_VLD0_V2},
	.bss_clr_map_reg	= R_BSS_CLR_MAP_V2,
	.dma_ch_mask		= 0,
	.edcca_regs		= &rtw8922a_edcca_regs,
#ifdef CONFIG_PM
	.wowlan_stub		= &rtw_wowlan_stub_8922a,
#endif
	.xtal_info		= NULL,
};
EXPORT_SYMBOL(rtw8922a_chip_info);

MODULE_FIRMWARE(RTW8922A_MODULE_FIRMWARE);
MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11be wireless 8922A driver");
MODULE_LICENSE("Dual BSD/GPL");
