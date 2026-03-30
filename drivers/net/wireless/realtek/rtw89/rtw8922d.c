// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2026  Realtek Corporation
 */

#include "chan.h"
#include "debug.h"
#include "efuse.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"
#include "rtw8922d.h"
#include "rtw8922d_rfk.h"
#include "util.h"

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

static int rtw8922d_pwr_on_func(struct rtw89_dev *rtwdev)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	u32 val32;
	int ret;

	if (hal->cid != RTL8922D_CID7025)
		goto begin;

	switch (hal->cv) {
	case CHIP_CAV:
	case CHIP_CBV:
		rtw89_write32_set(rtwdev, R_BE_SPS_DIG_ON_CTRL1, B_BE_PWM_FORCE);
		rtw89_write32_set(rtwdev, R_BE_SPS_ANA_ON_CTRL1, B_BE_PWM_FORCE_ANA);
		break;
	default:
		break;
	}

begin:
	rtw89_write32_clr(rtwdev, R_BE_SYS_PW_CTRL, B_BE_AFSM_WLSUS_EN |
						    B_BE_AFSM_PCIE_SUS_EN);
	rtw89_write32_set(rtwdev, R_BE_SYS_PW_CTRL, B_BE_DIS_WLBT_PDNSUSEN_SOPC);
	rtw89_write32_set(rtwdev, R_BE_WLLPS_CTRL, B_BE_DIS_WLBT_LPSEN_LOPC);
	if (hal->cid != RTL8922D_CID7090)
		rtw89_write32_clr(rtwdev, R_BE_SYS_PW_CTRL, B_BE_APDM_HPDN);
	rtw89_write32_clr(rtwdev, R_BE_FWS1ISR, B_BE_FS_WL_HW_RADIO_OFF_INT);
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

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_NORMAL_WRITE, 0x10, 0x10);
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
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_XTAL_XMD_2, 0, 0x70);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_SRAM_CTRL, 0, 0x02);
	if (ret)
		return ret;

	rtw89_write32_set(rtwdev, R_BE_PMC_DBG_CTRL2, B_BE_SYSON_DIS_PMCR_BE_WRMSK);
	rtw89_write32_set(rtwdev, R_BE_SYS_ISO_CTRL, B_BE_ISO_EB2CORE);
	rtw89_write32_clr(rtwdev, R_BE_SYS_ISO_CTRL, B_BE_PWC_EV2EF_B);

	mdelay(1);

	rtw89_write32_clr(rtwdev, R_BE_SYS_ISO_CTRL, B_BE_PWC_EV2EF_S);
	rtw89_write32_clr(rtwdev, R_BE_PMC_DBG_CTRL2, B_BE_SYSON_DIS_PMCR_BE_WRMSK);

	rtw89_write32_set(rtwdev, R_BE_DMAC_FUNC_EN,
			  B_BE_MAC_FUNC_EN | B_BE_DMAC_FUNC_EN |
			  B_BE_MPDU_PROC_EN | B_BE_WD_RLS_EN |
			  B_BE_DLE_WDE_EN | B_BE_TXPKT_CTRL_EN |
			  B_BE_STA_SCH_EN | B_BE_DLE_PLE_EN |
			  B_BE_PKT_BUF_EN | B_BE_DMAC_TBL_EN |
			  B_BE_PKT_IN_EN | B_BE_DLE_CPUIO_EN |
			  B_BE_DISPATCHER_EN | B_BE_BBRPT_EN |
			  B_BE_MAC_SEC_EN | B_BE_H_AXIDMA_EN |
			  B_BE_DMAC_MLO_EN | B_BE_PLRLS_EN |
			  B_BE_P_AXIDMA_EN | B_BE_DLE_DATACPUIO_EN |
			  B_BE_LTR_CTL_EN);

	set_bit(RTW89_FLAG_DMAC_FUNC, rtwdev->flags);

	rtw89_write32_set(rtwdev, R_BE_CMAC_SHARE_FUNC_EN,
			  B_BE_CMAC_SHARE_EN | B_BE_RESPBA_EN |
			  B_BE_ADDRSRCH_EN | B_BE_BTCOEX_EN);

	rtw89_write32_set(rtwdev, R_BE_CMAC_FUNC_EN,
			  B_BE_CMAC_EN | B_BE_CMAC_TXEN |
			  B_BE_CMAC_RXEN | B_BE_SIGB_EN |
			  B_BE_PHYINTF_EN | B_BE_CMAC_DMA_EN |
			  B_BE_PTCLTOP_EN | B_BE_SCHEDULER_EN |
			  B_BE_TMAC_EN | B_BE_RMAC_EN |
			  B_BE_TXTIME_EN | B_BE_RESP_PKTCTL_EN);

	set_bit(RTW89_FLAG_CMAC0_FUNC, rtwdev->flags);

	rtw89_write32_set(rtwdev, R_BE_FEN_RST_ENABLE,
			  B_BE_FEN_BB_IP_RSTN | B_BE_FEN_BBPLAT_RSTB);

	return 0;
}

static int rtw8922d_pwr_off_func(struct rtw89_dev *rtwdev)
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
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S0, 0, 0x01);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S1, 0, 0x01);
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

	rtw89_write32_set(rtwdev, R_BE_SYS_PW_CTRL, B_BE_EN_WLON);
	rtw89_write8_clr(rtwdev, R_BE_FEN_RST_ENABLE, B_BE_FEN_BB_IP_RSTN |
						      B_BE_FEN_BBPLAT_RSTB);
	rtw89_write32_clr(rtwdev, R_BE_SYS_ADIE_PAD_PWR_CTRL,
			  B_BE_SYM_PADPDN_WL_RFC0_1P3);

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, 0x20);
	if (ret)
		return ret;

	rtw89_write32_clr(rtwdev, R_BE_SYS_ADIE_PAD_PWR_CTRL,
			  B_BE_SYM_PADPDN_WL_RFC1_1P3);

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, 0x40);
	if (ret)
		return ret;

	rtw89_write32_clr(rtwdev, R_BE_HCI_OPT_CTRL, B_BE_HAXIDMA_IO_EN);

	ret = read_poll_timeout(rtw89_read32, val32, !(val32 & B_BE_HAXIDMA_IO_ST),
				1000, 3000000, false, rtwdev, R_BE_HCI_OPT_CTRL);
	if (ret)
		return ret;
	ret = read_poll_timeout(rtw89_read32, val32,
				!(val32 & B_BE_HAXIDMA_BACKUP_RESTORE_ST),
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

	rtw89_write32(rtwdev, R_BE_WLLPS_CTRL, 0x00015002);
	rtw89_write32_clr(rtwdev, R_BE_SYS_PW_CTRL, B_BE_XTAL_OFF_A_DIE);
	rtw89_write32_set(rtwdev, R_BE_SYS_PW_CTRL, B_BE_APFM_SWLPS);
	rtw89_write32(rtwdev, R_BE_UDM1, 0);

	return 0;
}

static void rtw8922d_efuse_parsing_tssi(struct rtw89_dev *rtwdev,
					struct rtw8922d_efuse *map)
{
	const struct rtw8922d_tssi_offset_6g * const ofst_6g[] = {
		&map->path_a_tssi_6g,
		&map->path_b_tssi_6g,
	};
	const struct rtw8922d_tssi_offset * const ofst[] = {
		&map->path_a_tssi,
		&map->path_b_tssi,
	};
	struct rtw89_tssi_info *tssi = &rtwdev->tssi;
	u8 i, j;

	tssi->thermal[RF_PATH_A] = map->path_a_therm;
	tssi->thermal[RF_PATH_B] = map->path_b_therm;

	for (i = 0; i < RF_PATH_NUM_8922D; i++) {
		memcpy(tssi->tssi_cck[i], ofst[i]->cck_tssi, TSSI_CCK_CH_GROUP_NUM);

		for (j = 0; j < TSSI_CCK_CH_GROUP_NUM; j++)
			rtw89_debug(rtwdev, RTW89_DBG_TSSI,
				    "[TSSI][EFUSE] path=%d cck[%d]=0x%x\n",
				    i, j, tssi->tssi_cck[i][j]);

		memcpy(tssi->tssi_mcs[i], ofst[i]->bw40_tssi,
		       TSSI_MCS_2G_CH_GROUP_NUM);
		memcpy(tssi->tssi_mcs[i] + TSSI_MCS_2G_CH_GROUP_NUM,
		       ofst[i]->bw40_1s_tssi_5g, TSSI_MCS_5G_CH_GROUP_NUM);
		memcpy(tssi->tssi_6g_mcs[i], ofst_6g[i]->bw40_1s_tssi_6g,
		       TSSI_MCS_6G_CH_GROUP_NUM);

		for (j = 0; j < TSSI_MCS_CH_GROUP_NUM; j++)
			rtw89_debug(rtwdev, RTW89_DBG_TSSI,
				    "[TSSI][EFUSE] path=%d mcs[%d]=0x%x\n",
				    i, j, tssi->tssi_mcs[i][j]);

		for (j = 0; j < TSSI_MCS_6G_CH_GROUP_NUM; j++)
			rtw89_debug(rtwdev, RTW89_DBG_TSSI,
				    "[TSSI][EFUSE] path=%d mcs_6g[%d]=0x%x\n",
				    i, j, tssi->tssi_6g_mcs[i][j]);
	}
}

static void
__rtw8922d_efuse_parsing_gain_offset(struct rtw89_dev *rtwdev,
				     s8 offset[RTW89_GAIN_OFFSET_NR],
				     const s8 *offset_default,
				     const struct rtw8922d_rx_gain *rx_gain,
				     const struct rtw8922d_rx_gain_6g *rx_gain_6g)
{
	int i;
	u8 t;

	offset[RTW89_GAIN_OFFSET_2G_CCK] = rx_gain->_2g_cck;
	offset[RTW89_GAIN_OFFSET_2G_OFDM] = rx_gain->_2g_ofdm;
	offset[RTW89_GAIN_OFFSET_5G_LOW] = rx_gain->_5g_low;
	offset[RTW89_GAIN_OFFSET_5G_MID] = rx_gain->_5g_mid;
	offset[RTW89_GAIN_OFFSET_5G_HIGH] = rx_gain->_5g_high;
	offset[RTW89_GAIN_OFFSET_6G_L0] = rx_gain_6g->_6g_l0;
	offset[RTW89_GAIN_OFFSET_6G_L1] = rx_gain_6g->_6g_l1;
	offset[RTW89_GAIN_OFFSET_6G_M0] = rx_gain_6g->_6g_m0;
	offset[RTW89_GAIN_OFFSET_6G_M1] = rx_gain_6g->_6g_m1;
	offset[RTW89_GAIN_OFFSET_6G_H0] = rx_gain_6g->_6g_h0;
	offset[RTW89_GAIN_OFFSET_6G_H1] = rx_gain_6g->_6g_h1;
	offset[RTW89_GAIN_OFFSET_6G_UH0] = rx_gain_6g->_6g_uh0;
	offset[RTW89_GAIN_OFFSET_6G_UH1] = rx_gain_6g->_6g_uh1;

	for (i = 0; i < RTW89_GAIN_OFFSET_NR; i++) {
		t = offset[i];
		if (t == 0xff) {
			if (offset_default) {
				offset[i] = offset_default[i];
				continue;
			}
			t = 0;
		}

		/* transform: sign-bit + U(7,2) to S(8,2) */
		if (t & 0x80)
			offset[i] = (t ^ 0x7f) + 1;
		else
			offset[i] = t;
	}
}

static void rtw8922d_efuse_parsing_gain_offset(struct rtw89_dev *rtwdev,
					       struct rtw8922d_efuse *map)
{
	struct rtw89_phy_efuse_gain *gain = &rtwdev->efuse_gain;

	__rtw8922d_efuse_parsing_gain_offset(rtwdev, gain->offset[RF_PATH_A],
					     NULL,
					     &map->rx_gain_a, &map->rx_gain_6g_a);
	__rtw8922d_efuse_parsing_gain_offset(rtwdev, gain->offset[RF_PATH_B],
					     NULL,
					     &map->rx_gain_b, &map->rx_gain_6g_b);

	__rtw8922d_efuse_parsing_gain_offset(rtwdev, gain->offset2[RF_PATH_A],
					     gain->offset[RF_PATH_A],
					     &map->rx_gain_a_2, &map->rx_gain_6g_a_2);
	__rtw8922d_efuse_parsing_gain_offset(rtwdev, gain->offset2[RF_PATH_B],
					     gain->offset[RF_PATH_B],
					     &map->rx_gain_b_2, &map->rx_gain_6g_b_2);

	gain->offset_valid = true;
}

static int rtw8922d_read_efuse_pci_sdio(struct rtw89_dev *rtwdev, u8 *log_map)
{
	struct rtw89_efuse *efuse = &rtwdev->efuse;

	if (rtwdev->hci.type == RTW89_HCI_TYPE_PCIE)
		ether_addr_copy(efuse->addr, log_map + 0x4104);
	else
		ether_addr_copy(efuse->addr, log_map + 0x001A);

	return 0;
}

static int rtw8922d_read_efuse_usb(struct rtw89_dev *rtwdev, u8 *log_map)
{
	struct rtw89_efuse *efuse = &rtwdev->efuse;

	ether_addr_copy(efuse->addr, log_map + 0x0078);

	return 0;
}

static int rtw8922d_read_efuse_rf(struct rtw89_dev *rtwdev, u8 *log_map)
{
	struct rtw8922d_efuse *map = (struct rtw8922d_efuse *)log_map;
	struct rtw89_efuse *efuse = &rtwdev->efuse;

	efuse->rfe_type = map->rfe_type;
	efuse->xtal_cap = map->xtal_k;
	efuse->country_code[0] = map->country_code[0];
	efuse->country_code[1] = map->country_code[1];
	efuse->bt_setting_2 = map->bt_setting_2;
	efuse->bt_setting_3 = map->bt_setting_3;
	rtw8922d_efuse_parsing_tssi(rtwdev, map);
	rtw8922d_efuse_parsing_gain_offset(rtwdev, map);

	return 0;
}

static int rtw8922d_read_efuse(struct rtw89_dev *rtwdev, u8 *log_map,
			       enum rtw89_efuse_block block)
{
	switch (block) {
	case RTW89_EFUSE_BLOCK_HCI_DIG_PCIE_SDIO:
		return rtw8922d_read_efuse_pci_sdio(rtwdev, log_map);
	case RTW89_EFUSE_BLOCK_HCI_DIG_USB:
		return rtw8922d_read_efuse_usb(rtwdev, log_map);
	case RTW89_EFUSE_BLOCK_RF:
		return rtw8922d_read_efuse_rf(rtwdev, log_map);
	default:
		return 0;
	}
}

static void rtw8922d_phycap_parsing_vco_trim(struct rtw89_dev *rtwdev,
					     u8 *phycap_map)
{
	static const u32 vco_trim_addr[RF_PATH_NUM_8922D] = {0x175E, 0x175F};
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	u32 addr = rtwdev->chip->phycap_addr;
	const u32 vco_check_addr = 0x1700;
	u8 val;

	val = phycap_map[vco_check_addr - addr];
	if (val & BIT(1))
		return;

	info->pg_vco_trim = true;

	info->vco_trim[0] = u8_get_bits(phycap_map[vco_trim_addr[0] - addr], GENMASK(4, 0));
	info->vco_trim[1] = u8_get_bits(phycap_map[vco_trim_addr[1] - addr], GENMASK(4, 0));
}

static void rtw8922d_vco_trim(struct rtw89_dev *rtwdev)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;

	if (!info->pg_vco_trim)
		return;

	rtw89_write_rf(rtwdev, RF_PATH_A, RR_VCO, RR_VCO_VAL, info->vco_trim[0]);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_VCO, RR_VCO_VAL, info->vco_trim[1]);
}

#define THM_TRIM_POSITIVE_MASK BIT(6)
#define THM_TRIM_MAGNITUDE_MASK GENMASK(5, 0)
#define THM_TRIM_MAX (15)
#define THM_TRIM_MIN (-15)

static void rtw8922d_phycap_parsing_thermal_trim(struct rtw89_dev *rtwdev,
						 u8 *phycap_map)
{
	static const u32 thm_trim_addr[RF_PATH_NUM_8922D] = {0x1706, 0x1732};
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	u32 addr = rtwdev->chip->phycap_addr;
	bool pg = true;
	u8 pg_th;
	s8 val;
	u8 i;

	for (i = 0; i < RF_PATH_NUM_8922D; i++) {
		pg_th = phycap_map[thm_trim_addr[i] - addr];
		if (pg_th == 0xff) {
			memset(info->thermal_trim, 0, sizeof(info->thermal_trim));
			pg = false;
			goto out;
		}

		val = u8_get_bits(pg_th, THM_TRIM_MAGNITUDE_MASK);

		if (!(pg_th & THM_TRIM_POSITIVE_MASK))
			val *= -1;

		if (val <= THM_TRIM_MIN || val >= THM_TRIM_MAX) {
			val = 0;
			info->thermal_trim[i] = 0;
		} else {
			info->thermal_trim[i] = pg_th;
		}

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[THERMAL][TRIM] path=%d thermal_trim=0x%x (%d)\n",
			    i, pg_th, val);
	}

out:
	info->pg_thermal_trim = pg;
}

static void rtw8922d_thermal_trim(struct rtw89_dev *rtwdev)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	u8 thermal;
	int i;

	for (i = 0; i < RF_PATH_NUM_8922D; i++) {
		thermal = info->pg_thermal_trim ? info->thermal_trim[i] : 0;
		rtw89_write_rf(rtwdev, i, RR_TM, RR_TM_TRM, thermal & 0x7f);
	}
}

static void rtw8922d_phycap_parsing_pa_bias_trim(struct rtw89_dev *rtwdev,
						 u8 *phycap_map)
{
	static const u32 pabias_trim_addr[RF_PATH_NUM_8922D] = {0x1707, 0x1733};
	static const u32 check_pa_pad_trim_addr = 0x1700;
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	u32 addr = rtwdev->chip->phycap_addr;
	bool pg = true;
	u8 val;
	u8 i;

	val = phycap_map[check_pa_pad_trim_addr - addr];
	if (val == 0xff) {
		pg = false;
		goto out;
	}

	for (i = 0; i < RF_PATH_NUM_8922D; i++) {
		info->pa_bias_trim[i] = phycap_map[pabias_trim_addr[i] - addr];

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PA_BIAS][TRIM] path=%d pa_bias_trim=0x%x\n",
			    i, info->pa_bias_trim[i]);
	}

out:
	info->pg_pa_bias_trim = pg;
}

static void rtw8922d_pa_bias_trim(struct rtw89_dev *rtwdev)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	u8 pabias_2g, pabias_5g;
	u8 i;

	if (!info->pg_pa_bias_trim) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PA_BIAS][TRIM] no PG, do nothing\n");

		return;
	}

	for (i = 0; i < RF_PATH_NUM_8922D; i++) {
		pabias_2g = FIELD_GET(GENMASK(3, 0), info->pa_bias_trim[i]);
		pabias_5g = FIELD_GET(GENMASK(7, 4), info->pa_bias_trim[i]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PA_BIAS][TRIM] path=%d 2G=0x%x 5G=0x%x\n",
			    i, pabias_2g, pabias_5g);

		rtw89_write_rf(rtwdev, i, RR_BIASA, RR_BIASA_TXG_V1, pabias_2g);
		rtw89_write_rf(rtwdev, i, RR_BIASA, RR_BIASA_TXA_V1, pabias_5g);
	}
}

static void rtw8922d_phycap_parsing_pad_bias_trim(struct rtw89_dev *rtwdev,
						  u8 *phycap_map)
{
	static const u32 pad_bias_trim_addr[RF_PATH_NUM_8922D] = {0x1708, 0x1734};
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	u32 addr = rtwdev->chip->phycap_addr;
	u8 i;

	if (!info->pg_pa_bias_trim)
		return;

	for (i = 0; i < RF_PATH_NUM_8922D; i++) {
		info->pad_bias_trim[i] = phycap_map[pad_bias_trim_addr[i] - addr];

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PAD_BIAS][TRIM] path=%d pad_bias_trim=0x%x\n",
			    i, info->pad_bias_trim[i]);
	}
}

static void rtw8922d_pad_bias_trim(struct rtw89_dev *rtwdev)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	u8 pad_bias_2g, pad_bias_5g;
	u8 i;

	if (!info->pg_pa_bias_trim) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PAD_BIAS][TRIM] no PG, do nothing\n");
		return;
	}

	for (i = 0; i < RF_PATH_NUM_8922D; i++) {
		pad_bias_2g = u8_get_bits(info->pad_bias_trim[i], GENMASK(3, 0));
		pad_bias_5g = u8_get_bits(info->pad_bias_trim[i], GENMASK(7, 4));

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PAD_BIAS][TRIM] path=%d 2G=0x%x 5G=0x%x\n",
			    i, pad_bias_2g, pad_bias_5g);

		rtw89_write_rf(rtwdev, i, RR_BIASA, RR_BIASD_TXG_V1, pad_bias_2g);
		rtw89_write_rf(rtwdev, i, RR_BIASA, RR_BIASD_TXA_V1, pad_bias_5g);
	}
}

static int rtw8922d_read_phycap(struct rtw89_dev *rtwdev, u8 *phycap_map)
{
	rtw8922d_phycap_parsing_vco_trim(rtwdev, phycap_map);
	rtw8922d_phycap_parsing_thermal_trim(rtwdev, phycap_map);
	rtw8922d_phycap_parsing_pa_bias_trim(rtwdev, phycap_map);
	rtw8922d_phycap_parsing_pad_bias_trim(rtwdev, phycap_map);

	return 0;
}

static void rtw8922d_power_trim(struct rtw89_dev *rtwdev)
{
	rtw8922d_vco_trim(rtwdev);
	rtw8922d_thermal_trim(rtwdev);
	rtw8922d_pa_bias_trim(rtwdev);
	rtw8922d_pad_bias_trim(rtwdev);
}

static void rtw8922d_set_channel_mac(struct rtw89_dev *rtwdev,
				     const struct rtw89_chan *chan,
				     u8 mac_idx)
{
	u32 sub_carr = rtw89_mac_reg_by_idx(rtwdev, R_BE_TX_SUB_BAND_VALUE, mac_idx);
	u32 chk_rate = rtw89_mac_reg_by_idx(rtwdev, R_BE_TXRATE_CHK, mac_idx);
	u32 rf_mod = rtw89_mac_reg_by_idx(rtwdev, R_BE_WMAC_RFMOD, mac_idx);
	u8 txsb20 = 0, txsb40 = 0, txsb80 = 0;
	u8 rf_mod_val, chk_rate_mask, sifs;
	u32 txsb;
	u32 reg;

	switch (chan->band_width) {
	case RTW89_CHANNEL_WIDTH_160:
		txsb80 = rtw89_phy_get_txsb(rtwdev, chan, RTW89_CHANNEL_WIDTH_80);
		fallthrough;
	case RTW89_CHANNEL_WIDTH_80:
		txsb40 = rtw89_phy_get_txsb(rtwdev, chan, RTW89_CHANNEL_WIDTH_40);
		fallthrough;
	case RTW89_CHANNEL_WIDTH_40:
		txsb20 = rtw89_phy_get_txsb(rtwdev, chan, RTW89_CHANNEL_WIDTH_20);
		break;
	default:
		break;
	}

	switch (chan->band_width) {
	case RTW89_CHANNEL_WIDTH_160:
		rf_mod_val = BE_WMAC_RFMOD_160M;
		txsb = u32_encode_bits(txsb20, B_BE_TXSB_20M_MASK) |
		       u32_encode_bits(txsb40, B_BE_TXSB_40M_MASK) |
		       u32_encode_bits(txsb80, B_BE_TXSB_80M_MASK);
		break;
	case RTW89_CHANNEL_WIDTH_80:
		rf_mod_val = BE_WMAC_RFMOD_80M;
		txsb = u32_encode_bits(txsb20, B_BE_TXSB_20M_MASK) |
		       u32_encode_bits(txsb40, B_BE_TXSB_40M_MASK);
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rf_mod_val = BE_WMAC_RFMOD_40M;
		txsb = u32_encode_bits(txsb20, B_BE_TXSB_20M_MASK);
		break;
	case RTW89_CHANNEL_WIDTH_20:
	default:
		rf_mod_val = BE_WMAC_RFMOD_20M;
		txsb = 0;
		break;
	}

	if (txsb20 <= BE_PRI20_BITMAP_MAX)
		txsb |= u32_encode_bits(BIT(txsb20), B_BE_PRI20_BITMAP_MASK);

	rtw89_write8_mask(rtwdev, rf_mod, B_BE_WMAC_RFMOD_MASK, rf_mod_val);
	rtw89_write32(rtwdev, sub_carr, txsb);

	switch (chan->band_type) {
	case RTW89_BAND_2G:
		chk_rate_mask = B_BE_BAND_MODE;
		break;
	case RTW89_BAND_5G:
	case RTW89_BAND_6G:
		chk_rate_mask = B_BE_CHECK_CCK_EN | B_BE_RTS_LIMIT_IN_OFDM6;
		break;
	default:
		rtw89_warn(rtwdev, "Invalid band_type:%d\n", chan->band_type);
		return;
	}

	rtw89_write8_clr(rtwdev, chk_rate, B_BE_BAND_MODE | B_BE_CHECK_CCK_EN |
					   B_BE_RTS_LIMIT_IN_OFDM6);
	rtw89_write8_set(rtwdev, chk_rate, chk_rate_mask);

	switch (chan->band_width) {
	case RTW89_CHANNEL_WIDTH_160:
		sifs = 0x8C;
		break;
	case RTW89_CHANNEL_WIDTH_80:
		sifs = 0x8A;
		break;
	case RTW89_CHANNEL_WIDTH_40:
		sifs = 0x84;
		break;
	case RTW89_CHANNEL_WIDTH_20:
	default:
		sifs = 0x82;
	}

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_MUEDCA_EN, mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_BE_SIFS_MACTXEN_TB_T1_DOT05US_MASK, sifs);
}

static const u32 rtw8922d_sco_barker_threshold[14] = {
	0x1fe4f, 0x1ff5e, 0x2006c, 0x2017b, 0x2028a, 0x20399, 0x204a8, 0x205b6,
	0x206c5, 0x207d4, 0x208e3, 0x209f2, 0x20b00, 0x20d8a
};

static const u32 rtw8922d_sco_cck_threshold[14] = {
	0x2bdac, 0x2bf21, 0x2c095, 0x2c209, 0x2c37e, 0x2c4f2, 0x2c666, 0x2c7db,
	0x2c94f, 0x2cac3, 0x2cc38, 0x2cdac, 0x2cf21, 0x2d29e
};

static int rtw8922d_ctrl_sco_cck(struct rtw89_dev *rtwdev,
				 u8 primary_ch, enum rtw89_bandwidth bw,
				 enum rtw89_phy_idx phy_idx)
{
	u8 ch_element;

	if (primary_ch >= 14)
		return -EINVAL;

	ch_element = primary_ch - 1;

	rtw89_phy_write32_idx(rtwdev, R_BK_FC0_INV_BE4, B_BK_FC0_INV_BE4,
			      rtw8922d_sco_barker_threshold[ch_element],
			      phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_CCK_FC0_INV_BE4, B_CCK_FC0_INV_BE4,
			      rtw8922d_sco_cck_threshold[ch_element],
			      phy_idx);

	return 0;
}

static void rtw8922d_ctrl_ch_core(struct rtw89_dev *rtwdev,
				  const struct rtw89_chan *chan,
				  enum rtw89_phy_idx phy_idx)
{
	u16 central_freq = chan->freq;
	u16 sco;

	if (chan->band_type == RTW89_BAND_2G) {
		rtw89_phy_write32_idx(rtwdev, R_BAND_SEL0_BE4, B_BAND_SEL0_BE4,
				      1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BAND_SEL1_BE4, B_BAND_SEL1_BE4,
				      1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_ENABLE_CCK0_BE4, B_ENABLE_CCK0_BE4,
				      1, phy_idx);
	} else {
		rtw89_phy_write32_idx(rtwdev, R_BAND_SEL0_BE4, B_BAND_SEL0_BE4,
				      0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BAND_SEL1_BE4, B_BAND_SEL1_BE4,
				      0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_ENABLE_CCK0_BE4, B_ENABLE_CCK0_BE4,
				      0, phy_idx);
	}

	rtw89_phy_write32_idx(rtwdev, R_FC0_BE4, B_FC0_BE4, central_freq, phy_idx);

	sco = phy_div((BIT(0) << 27) + (central_freq / 2), central_freq);
	rtw89_phy_write32_idx(rtwdev, R_FC0_INV_BE4, B_FC0_INV_BE4, sco, phy_idx);
}

struct rtw8922d_bb_gain {
	u32 gain_g[BB_PATH_NUM_8922D];
	u32 gain_a[BB_PATH_NUM_8922D];
	u32 gain_g_mask;
	u32 gain_a_mask;
};

static const struct rtw89_reg_def rpl_comp_bw160[RTW89_BW20_SC_160M] = {
	{ .addr = 0x241E8, .mask = 0xFF00},
	{ .addr = 0x241E8, .mask = 0xFF0000},
	{ .addr = 0x241E8, .mask = 0xFF000000},
	{ .addr = 0x241EC, .mask = 0xFF},
	{ .addr = 0x241EC, .mask = 0xFF00},
	{ .addr = 0x241EC, .mask = 0xFF0000},
	{ .addr = 0x241EC, .mask = 0xFF000000},
	{ .addr = 0x241F0, .mask = 0xFF}
};

static const struct rtw89_reg_def rpl_comp_bw80[RTW89_BW20_SC_80M] = {
	{ .addr = 0x241F4, .mask = 0xFF},
	{ .addr = 0x241F4, .mask = 0xFF00},
	{ .addr = 0x241F4, .mask = 0xFF0000},
	{ .addr = 0x241F4, .mask = 0xFF000000}
};

static const struct rtw89_reg_def rpl_comp_bw40[RTW89_BW20_SC_40M] = {
	{ .addr = 0x241F0, .mask = 0xFF0000},
	{ .addr = 0x241F0, .mask = 0xFF000000}
};

static const struct rtw89_reg_def rpl_comp_bw20[RTW89_BW20_SC_20M] = {
	{ .addr = 0x241F0, .mask = 0xFF00}
};

static const struct rtw8922d_bb_gain bb_gain_lna[LNA_GAIN_NUM] = {
	{ .gain_g = {0x2409C, 0x2449C}, .gain_a = {0x2406C, 0x2446C},
	  .gain_g_mask = 0xFF00, .gain_a_mask = 0xFF},
	{ .gain_g = {0x2409C, 0x2449C}, .gain_a = {0x2406C, 0x2446C},
	  .gain_g_mask = 0xFF000000, .gain_a_mask = 0xFF0000},
	{ .gain_g = {0x240A0, 0x244A0}, .gain_a = {0x24070, 0x24470},
	  .gain_g_mask = 0xFF00, .gain_a_mask = 0xFF},
	{ .gain_g = {0x240A0, 0x244A0}, .gain_a = {0x24070, 0x24470},
	  .gain_g_mask = 0xFF000000, .gain_a_mask = 0xFF0000},
	{ .gain_g = {0x240A4, 0x244A4}, .gain_a = {0x24074, 0x24474},
	  .gain_g_mask = 0xFF00, .gain_a_mask = 0xFF},
	{ .gain_g = {0x240A4, 0x244A4}, .gain_a = {0x24074, 0x24474},
	  .gain_g_mask = 0xFF000000, .gain_a_mask = 0xFF0000},
	{ .gain_g = {0x240A8, 0x244A8}, .gain_a = {0x24078, 0x24478},
	  .gain_g_mask = 0xFF00, .gain_a_mask = 0xFF},
};

static const struct rtw8922d_bb_gain bb_gain_tia[TIA_GAIN_NUM] = {
	{ .gain_g = {0x24054, 0x24454}, .gain_a = {0x24054, 0x24454},
	  .gain_g_mask = 0x7FC0000, .gain_a_mask = 0x1FF},
	{ .gain_g = {0x24058, 0x24458}, .gain_a = {0x24054, 0x24454},
	  .gain_g_mask = 0x1FF, .gain_a_mask = 0x3FE00 },
};

static const struct rtw8922d_bb_gain bb_op1db_lna[LNA_GAIN_NUM] = {
	{ .gain_g = {0x240AC, 0x244AC}, .gain_a = {0x24078, 0x24478},
	  .gain_g_mask = 0xFF00, .gain_a_mask = 0xFF000000},
	{ .gain_g = {0x240AC, 0x244AC}, .gain_a = {0x2407C, 0x2447C},
	  .gain_g_mask = 0xFF0000, .gain_a_mask = 0xFF},
	{ .gain_g = {0x240AC, 0x244AC}, .gain_a = {0x2407C, 0x2447C},
	  .gain_g_mask = 0xFF000000, .gain_a_mask = 0xFF00},
	{ .gain_g = {0x240B0, 0x244B0}, .gain_a = {0x2407C, 0x2447C},
	  .gain_g_mask = 0xFF, .gain_a_mask = 0xFF0000},
	{ .gain_g = {0x240B0, 0x244B0}, .gain_a = {0x2407C, 0x2447C},
	  .gain_g_mask = 0xFF00, .gain_a_mask = 0xFF000000},
	{ .gain_g = {0x240B0, 0x244B0}, .gain_a = {0x24080, 0x24480},
	  .gain_g_mask = 0xFF0000, .gain_a_mask = 0xFF},
	{ .gain_g = {0x240B0, 0x244B0}, .gain_a = {0x24080, 0x24480},
	  .gain_g_mask = 0xFF000000, .gain_a_mask = 0xFF00},
};

static const struct rtw8922d_bb_gain bb_op1db_tia_lna[TIA_LNA_OP1DB_NUM] = {
	{ .gain_g = {0x240B4, 0x244B4}, .gain_a = {0x24080, 0x24480},
	  .gain_g_mask = 0xFF0000, .gain_a_mask = 0xFF000000},
	{ .gain_g = {0x240B4, 0x244B4}, .gain_a = {0x24084, 0x24484},
	  .gain_g_mask = 0xFF000000, .gain_a_mask = 0xFF},
	{ .gain_g = {0x240B8, 0x244B8}, .gain_a = {0x24084, 0x24484},
	  .gain_g_mask = 0xFF, .gain_a_mask = 0xFF00},
	{ .gain_g = {0x240B8, 0x244B8}, .gain_a = {0x24084, 0x24484},
	  .gain_g_mask = 0xFF00, .gain_a_mask = 0xFF0000},
	{ .gain_g = {0x240B8, 0x244B8}, .gain_a = {0x24084, 0x24484},
	  .gain_g_mask = 0xFF0000, .gain_a_mask = 0xFF000000},
	{ .gain_g = {0x240B8, 0x244B8}, .gain_a = {0x24088, 0x24488},
	  .gain_g_mask = 0xFF000000, .gain_a_mask = 0xFF},
	{ .gain_g = {0x240BC, 0x244BC}, .gain_a = {0x24088, 0x24488},
	  .gain_g_mask = 0xFF, .gain_a_mask = 0xFF00},
	{ .gain_g = {0x240BC, 0x244BC}, .gain_a = {0x24088, 0x24488},
	  .gain_g_mask = 0xFF00, .gain_a_mask = 0xFF0000},
};

static void rtw8922d_set_rpl_gain(struct rtw89_dev *rtwdev,
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

static void rtw8922d_set_lna_tia_gain(struct rtw89_dev *rtwdev,
				      const struct rtw89_chan *chan,
				      enum rtw89_rf_path path,
				      enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_phy_bb_gain_info_be *gain = &rtwdev->bb_gain.be;
	u8 gain_band = rtw89_subband_to_gain_band_be(chan->subband_type);
	enum rtw89_phy_bb_bw_be bw_type;
	u32 mask;
	s32 val;
	u32 reg;
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

static void rtw8922d_set_op1db(struct rtw89_dev *rtwdev,
			       const struct rtw89_chan *chan,
			       enum rtw89_rf_path path,
			       enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_phy_bb_gain_info_be *gain = &rtwdev->bb_gain.be;
	u8 gain_band = rtw89_subband_to_gain_band_be(chan->subband_type);
	enum rtw89_phy_bb_bw_be bw_type;
	u32 mask;
	s32 val;
	u32 reg;
	int i;

	bw_type = chan->band_width <= RTW89_CHANNEL_WIDTH_40 ?
		  RTW89_BB_BW_20_40 : RTW89_BB_BW_80_160_320;

	for (i = 0; i < LNA_GAIN_NUM; i++) {
		if (chan->band_type == RTW89_BAND_2G) {
			reg = bb_op1db_lna[i].gain_g[path];
			mask = bb_op1db_lna[i].gain_g_mask;
		} else {
			reg = bb_op1db_lna[i].gain_a[path];
			mask = bb_op1db_lna[i].gain_a_mask;
		}
		val = gain->lna_op1db[gain_band][bw_type][path][i];
		rtw89_phy_write32_idx(rtwdev, reg, mask, val, phy_idx);
	}

	for (i = 0; i < TIA_LNA_OP1DB_NUM; i++) {
		if (chan->band_type == RTW89_BAND_2G) {
			reg = bb_op1db_tia_lna[i].gain_g[path];
			mask = bb_op1db_tia_lna[i].gain_g_mask;
		} else {
			reg = bb_op1db_tia_lna[i].gain_a[path];
			mask = bb_op1db_tia_lna[i].gain_a_mask;
		}
		val = gain->tia_lna_op1db[gain_band][bw_type][path][i];
		rtw89_phy_write32_idx(rtwdev, reg, mask, val, phy_idx);
	}
}

static void rtw8922d_set_gain(struct rtw89_dev *rtwdev,
			      const struct rtw89_chan *chan,
			      enum rtw89_rf_path path,
			      enum rtw89_phy_idx phy_idx)
{
	rtw8922d_set_rpl_gain(rtwdev, chan, path, phy_idx);
	rtw8922d_set_lna_tia_gain(rtwdev, chan, path, phy_idx);
	rtw8922d_set_op1db(rtwdev, chan, path, phy_idx);
}

static s8 rtw8922d_get_rx_gain_by_chan(struct rtw89_dev *rtwdev,
				       const struct rtw89_chan *chan,
				       enum rtw89_rf_path path, bool is_cck)
{
	struct rtw89_phy_efuse_gain *gain = &rtwdev->efuse_gain;
	enum rtw89_gain_offset band;
	u8 fc_ch = chan->channel;
	s8 normal_efuse = 0;

	if (path > RF_PATH_B)
		return 0;

	if (is_cck) {
		if (fc_ch >= 1 && fc_ch <= 7)
			return gain->offset[path][RTW89_GAIN_OFFSET_2G_CCK];
		else if (fc_ch >= 8 && fc_ch <= 14)
			return gain->offset2[path][RTW89_GAIN_OFFSET_2G_CCK];

		return 0;
	}

	band = rtw89_subband_to_gain_offset_band_of_ofdm(chan->subband_type);

	if (band == RTW89_GAIN_OFFSET_2G_OFDM) {
		if (fc_ch >= 1 && fc_ch <= 7)
			normal_efuse = gain->offset[path][band];
		else if (fc_ch >= 8 && fc_ch <= 14)
			normal_efuse = gain->offset2[path][band];
	} else if (band == RTW89_GAIN_OFFSET_5G_LOW) {
		if (fc_ch == 50)
			normal_efuse = (gain->offset[path][band] + gain->offset2[path][band]) >> 1;
		else if (fc_ch >= 36 && fc_ch <= 48)
			normal_efuse = gain->offset[path][band];
		else if (fc_ch >= 52 && fc_ch <= 64)
			normal_efuse = gain->offset2[path][band];

	} else if (band == RTW89_GAIN_OFFSET_5G_MID) {
		if (fc_ch == 122)
			normal_efuse = (gain->offset[path][band] + gain->offset2[path][band]) >> 1;
		else if (fc_ch >= 100 && fc_ch <= 120)
			normal_efuse = gain->offset[path][band];
		else if (fc_ch >= 124 && fc_ch <= 144)
			normal_efuse = gain->offset2[path][band];
	} else if (band == RTW89_GAIN_OFFSET_5G_HIGH) {
		if (fc_ch == 163)
			normal_efuse = (gain->offset[path][band] + gain->offset2[path][band]) >> 1;
		else if (fc_ch >= 149 && fc_ch <= 161)
			normal_efuse = gain->offset[path][band];
		else if (fc_ch >= 165 && fc_ch <= 177)
			normal_efuse = gain->offset2[path][band];
	} else if (band == RTW89_GAIN_OFFSET_6G_L0) {
		if (fc_ch == 15)
			normal_efuse = (gain->offset[path][band] + gain->offset2[path][band]) >> 1;
		else if (fc_ch >= 1 && fc_ch <= 13)
			normal_efuse = gain->offset[path][band];
		else if (fc_ch >= 17 && fc_ch <= 29)
			normal_efuse = gain->offset2[path][band];
	} else if (band == RTW89_GAIN_OFFSET_6G_L1) {
		if (fc_ch == 47)
			normal_efuse = (gain->offset[path][band] + gain->offset2[path][band]) >> 1;
		else if (fc_ch >= 33 && fc_ch <= 45)
			normal_efuse = gain->offset[path][band];
		else if (fc_ch >= 49 && fc_ch <= 61)
			normal_efuse = gain->offset2[path][band];
	} else if (band == RTW89_GAIN_OFFSET_6G_M0) {
		if (fc_ch == 79)
			normal_efuse = (gain->offset[path][band] + gain->offset2[path][band]) >> 1;
		else if (fc_ch >= 65 && fc_ch <= 77)
			normal_efuse = gain->offset[path][band];
		else if (fc_ch >= 81 && fc_ch <= 93)
			normal_efuse = gain->offset2[path][band];
	} else if (band == RTW89_GAIN_OFFSET_6G_M1) {
		if (fc_ch == 111)
			normal_efuse = (gain->offset[path][band] + gain->offset2[path][band]) >> 1;
		else if (fc_ch >= 97 && fc_ch <= 109)
			normal_efuse = gain->offset[path][band];
		else if (fc_ch >= 113 && fc_ch <= 125)
			normal_efuse = gain->offset2[path][band];
	} else if (band == RTW89_GAIN_OFFSET_6G_H0) {
		if (fc_ch == 143)
			normal_efuse = (gain->offset[path][band] + gain->offset2[path][band]) >> 1;
		else if (fc_ch >= 129 && fc_ch <= 141)
			normal_efuse = gain->offset[path][band];
		else if (fc_ch >= 145 && fc_ch <= 157)
			normal_efuse = gain->offset2[path][band];
	} else if (band == RTW89_GAIN_OFFSET_6G_H1) {
		if (fc_ch == 175)
			normal_efuse = (gain->offset[path][band] + gain->offset2[path][band]) >> 1;
		else if (fc_ch >= 161 && fc_ch <= 173)
			normal_efuse = gain->offset[path][band];
		else if (fc_ch >= 177 && fc_ch <= 189)
			normal_efuse = gain->offset2[path][band];
	} else if (band == RTW89_GAIN_OFFSET_6G_UH0) {
		if (fc_ch == 207)
			normal_efuse = (gain->offset[path][band] + gain->offset2[path][band]) >> 1;
		else if (fc_ch >= 193 && fc_ch <= 205)
			normal_efuse = gain->offset[path][band];
		else if (fc_ch >= 209 && fc_ch <= 221)
			normal_efuse = gain->offset2[path][band];
	} else if (band == RTW89_GAIN_OFFSET_6G_UH1) {
		if (fc_ch == 239)
			normal_efuse = (gain->offset[path][band] + gain->offset2[path][band]) >> 1;
		else if (fc_ch >= 225 && fc_ch <= 237)
			normal_efuse = gain->offset[path][band];
		else if (fc_ch >= 241 && fc_ch <= 253)
			normal_efuse = gain->offset2[path][band];
	} else {
		normal_efuse = gain->offset[path][band];
	}

	return normal_efuse;
}

static void rtw8922d_calc_rx_gain_normal_cck(struct rtw89_dev *rtwdev,
					     const struct rtw89_chan *chan,
					     enum rtw89_rf_path path,
					     enum rtw89_phy_idx phy_idx,
					     struct rtw89_phy_calc_efuse_gain *calc)
{
	struct rtw89_phy_efuse_gain *gain = &rtwdev->efuse_gain;
	s8 rx_gain_offset;

	rx_gain_offset = -rtw8922d_get_rx_gain_by_chan(rtwdev, chan, path, true);

	if (chan->band_width == RTW89_CHANNEL_WIDTH_40)
		rx_gain_offset += (3 << 2); /* compensate RPL loss of 3dB */

	calc->cck_mean_gain_bias = (rx_gain_offset & 0x3) << 1;
	calc->cck_rpl_ofst = (rx_gain_offset >> 2) + gain->cck_rpl_base[phy_idx];
}

static void rtw8922d_set_rx_gain_normal_cck(struct rtw89_dev *rtwdev,
					    const struct rtw89_chan *chan,
					    enum rtw89_rf_path path,
					    enum rtw89_phy_idx phy_idx)
{
	struct rtw89_phy_calc_efuse_gain calc = {};

	rtw8922d_calc_rx_gain_normal_cck(rtwdev, chan, path, phy_idx, &calc);

	rtw89_phy_write32_idx(rtwdev, R_GAIN_BIAS_BE4, B_GAIN_BIAS_BW20_BE4,
			      calc.cck_mean_gain_bias, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_GAIN_BIAS_BE4, B_GAIN_BIAS_BW40_BE4,
			      calc.cck_mean_gain_bias, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_CCK_RPL_OFST_BE4, B_CCK_RPL_OFST_BE4,
			      calc.cck_rpl_ofst, phy_idx);
}

static void rtw8922d_calc_rx_gain_normal_ofdm(struct rtw89_dev *rtwdev,
					      const struct rtw89_chan *chan,
					      enum rtw89_rf_path path,
					      enum rtw89_phy_idx phy_idx,
					      struct rtw89_phy_calc_efuse_gain *calc)
{
	struct rtw89_phy_efuse_gain *gain = &rtwdev->efuse_gain;
	s8 rx_gain_offset;

	rx_gain_offset = rtw8922d_get_rx_gain_by_chan(rtwdev, chan, path, false);
	calc->rssi_ofst = (rx_gain_offset + gain->ref_gain_base[phy_idx]) & 0xff;
}

static void rtw8922d_set_rx_gain_normal_ofdm(struct rtw89_dev *rtwdev,
					     const struct rtw89_chan *chan,
					     enum rtw89_rf_path path,
					     enum rtw89_phy_idx phy_idx)
{
	static const u32 rssi_ofst_addr[2] = {R_OFDM_OFST_P0_BE4, R_OFDM_OFST_P1_BE4};
	static const u32 rssi_ofst_addr_m[2] = {B_OFDM_OFST_P0_BE4, B_OFDM_OFST_P1_BE4};
	static const u32 rpl_bias_comp[2] = {R_OFDM_RPL_BIAS_P0_BE4, R_OFDM_RPL_BIAS_P1_BE4};
	static const u32 rpl_bias_comp_m[2] = {B_OFDM_RPL_BIAS_P0_BE4, B_OFDM_RPL_BIAS_P1_BE4};
	struct rtw89_phy_calc_efuse_gain calc = {};

	rtw8922d_calc_rx_gain_normal_ofdm(rtwdev, chan, path, phy_idx, &calc);

	rtw89_phy_write32_idx(rtwdev, rssi_ofst_addr[path], rssi_ofst_addr_m[path],
			      calc.rssi_ofst, phy_idx);
	rtw89_phy_write32_idx(rtwdev, rpl_bias_comp[path], rpl_bias_comp_m[path], 0, phy_idx);
}

static void rtw8922d_set_rx_gain_normal(struct rtw89_dev *rtwdev,
					const struct rtw89_chan *chan,
					enum rtw89_rf_path path,
					enum rtw89_phy_idx phy_idx)
{
	struct rtw89_phy_efuse_gain *gain = &rtwdev->efuse_gain;

	if (!gain->offset_valid)
		return;

	if (chan->band_type == RTW89_BAND_2G)
		rtw8922d_set_rx_gain_normal_cck(rtwdev, chan, path, phy_idx);

	rtw8922d_set_rx_gain_normal_ofdm(rtwdev, chan, path, phy_idx);
}

static void rtw8922d_set_cck_parameters(struct rtw89_dev *rtwdev,
					const struct rtw89_chan *chan,
					enum rtw89_phy_idx phy_idx)
{
	u8 regd = rtw89_regd_get(rtwdev, chan->band_type);
	u8 central_ch = chan->channel;

	if (central_ch == 14) {
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF0_BE4, B_PCOEFF01_BE4, 0x3b13ff, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF2_BE4, B_PCOEFF23_BE4, 0x1c42de, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF4_BE4, B_PCOEFF45_BE4, 0xfdb0ad, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF6_BE4, B_PCOEFF67_BE4, 0xf60f6e, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF8_BE4, B_PCOEFF89_BE4, 0xfd8f92, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF10_BE4, B_PCOEFF10_BE4, 0x2d011, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF12_BE4, B_PCOEFF12_BE4, 0x1c02c, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF14_BE4, B_PCOEFF14_BE4, 0xfff00a, phy_idx);

		return;
	}

	if (regd == RTW89_FCC) {
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF0_BE4, B_PCOEFF01_BE4, 0x39A3BC, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF2_BE4, B_PCOEFF23_BE4, 0x2AA339, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF4_BE4, B_PCOEFF45_BE4, 0x15B202, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF6_BE4, B_PCOEFF67_BE4, 0x0550C7, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF8_BE4, B_PCOEFF89_BE4, 0xfe0009, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF10_BE4, B_PCOEFF10_BE4, 0xfd7fd3, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF12_BE4, B_PCOEFF12_BE4, 0xfeffe2, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF14_BE4, B_PCOEFF14_BE4, 0xffeff8, phy_idx);
	} else {
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF0_BE4, B_PCOEFF01_BE4, 0x3d23ff, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF2_BE4, B_PCOEFF23_BE4, 0x29b354, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF4_BE4, B_PCOEFF45_BE4, 0xfc1c8, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF6_BE4, B_PCOEFF67_BE4, 0xfdb053, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF8_BE4, B_PCOEFF89_BE4, 0xf86f9a, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF10_BE4, B_PCOEFF10_BE4, 0xfaef92, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF12_BE4, B_PCOEFF12_BE4, 0xfe5fcc, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_PCOEFF14_BE4, B_PCOEFF14_BE4, 0xffdff5, phy_idx);
	}
}

static void rtw8922d_ctrl_ch(struct rtw89_dev *rtwdev,
			     const struct rtw89_chan *chan,
			     enum rtw89_phy_idx phy_idx)
{
	u16 central_freq = chan->freq;
	u8 band = chan->band_type;
	u8 chan_idx;

	if (!central_freq) {
		rtw89_warn(rtwdev, "Invalid central_freq\n");
		return;
	}

	rtw8922d_ctrl_ch_core(rtwdev, chan, phy_idx);

	chan_idx = rtw89_encode_chan_idx(rtwdev, chan->primary_channel, band);
	rtw89_phy_write32_idx(rtwdev, R_MAC_PIN_SEL_BE4, B_CH_IDX_SEG0, chan_idx, phy_idx);

	rtw8922d_set_gain(rtwdev, chan, RF_PATH_A, phy_idx);
	rtw8922d_set_gain(rtwdev, chan, RF_PATH_B, phy_idx);

	rtw8922d_set_rx_gain_normal(rtwdev, chan, RF_PATH_A, phy_idx);
	rtw8922d_set_rx_gain_normal(rtwdev, chan, RF_PATH_B, phy_idx);

	if (band == RTW89_BAND_2G)
		rtw8922d_set_cck_parameters(rtwdev, chan, phy_idx);
}

static void rtw8922d_ctrl_bw(struct rtw89_dev *rtwdev, u8 pri_sb, u8 bw,
			     enum rtw89_phy_idx phy_idx)
{
	switch (bw) {
	default:
	case RTW89_CHANNEL_WIDTH_20:
		rtw89_phy_write32_idx(rtwdev, R_BW_BE4, B_BW_BE4, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BW_BE4, B_PRISB_BE4, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXBW_BE4, B_RXBW_BE4, 0x2, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXBW67_BE4, B_RXBW6_BE4, 0x2, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXBW67_BE4, B_RXBW7_BE4, 0x2, phy_idx);
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rtw89_phy_write32_idx(rtwdev, R_BW_BE4, B_BW_BE4, 0x1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BW_BE4, B_PRISB_BE4, pri_sb, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXBW_BE4, B_RXBW_BE4, 0x3, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXBW67_BE4, B_RXBW6_BE4, 0x3, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXBW67_BE4, B_RXBW7_BE4, 0x3, phy_idx);
		break;
	case RTW89_CHANNEL_WIDTH_80:
		rtw89_phy_write32_idx(rtwdev, R_BW_BE4, B_BW_BE4, 0x2, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BW_BE4, B_PRISB_BE4, pri_sb, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXBW_BE4, B_RXBW_BE4, 0x4, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXBW67_BE4, B_RXBW6_BE4, 0x4, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXBW67_BE4, B_RXBW7_BE4, 0x4, phy_idx);
		break;
	case RTW89_CHANNEL_WIDTH_160:
		rtw89_phy_write32_idx(rtwdev, R_BW_BE4, B_BW_BE4, 0x3, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_BW_BE4, B_PRISB_BE4, pri_sb, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXBW_BE4, B_RXBW_BE4, 0x5, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXBW67_BE4, B_RXBW6_BE4, 0x5, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXBW67_BE4, B_RXBW7_BE4, 0x5, phy_idx);
		break;
	}
}

static const u16 spur_nbi_a[] = {6400};
static const u16 spur_csi[] = {6400};

static u32 rtw8922d_spur_freq(struct rtw89_dev *rtwdev, const struct rtw89_chan *chan,
			      bool nbi_or_csi, enum rtw89_rf_path path)
{
	static const u16 cbw[RTW89_CHANNEL_WIDTH_ORDINARY_NUM] = {
		20, 40, 80, 160, 320,
	};
	u16 freq_lower, freq_upper, freq;
	const u16 *spur_freq;
	int spur_freq_nr, i;

	if (rtwdev->hal.aid != RTL8922D_AID7060)
		return 0;

	if (nbi_or_csi && path == RF_PATH_A) {
		spur_freq = spur_nbi_a;
		spur_freq_nr = ARRAY_SIZE(spur_nbi_a);
	} else if (!nbi_or_csi) {
		spur_freq = spur_csi;
		spur_freq_nr = ARRAY_SIZE(spur_csi);
	} else {
		return 0;
	}

	if (chan->band_width >= RTW89_CHANNEL_WIDTH_ORDINARY_NUM)
		return 0;

	freq_lower = chan->freq - cbw[chan->band_width] / 2;
	freq_upper = chan->freq + cbw[chan->band_width] / 2;

	for (i = 0; i < spur_freq_nr; i++) {
		freq = spur_freq[i];

		if (freq >= freq_lower && freq <= freq_upper)
			return freq;
	}

	return 0;
}

#define CARRIER_SPACING_312_5 312500 /* 312.5 kHz */
#define CARRIER_SPACING_78_125 78125 /* 78.125 kHz */
#define MAX_TONE_NUM 2048

static void rtw8922d_set_csi_tone_idx(struct rtw89_dev *rtwdev,
				      const struct rtw89_chan *chan,
				      enum rtw89_phy_idx phy_idx)
{
	s32 freq_diff, csi_idx, csi_tone_idx;
	u32 spur_freq;

	spur_freq = rtw8922d_spur_freq(rtwdev, chan, false, RF_PATH_AB);
	if (spur_freq == 0) {
		rtw89_phy_write32_idx(rtwdev, R_CSI_WGT_BE4, B_CSI_WGT_EN_BE4,
				      0, phy_idx);
		return;
	}

	freq_diff = (spur_freq - chan->freq) * 1000000;
	csi_idx = s32_div_u32_round_closest(freq_diff, CARRIER_SPACING_78_125);
	s32_div_u32_round_down(csi_idx, MAX_TONE_NUM, &csi_tone_idx);

	rtw89_phy_write32_idx(rtwdev, R_CSI_WGT_BE4, B_CSI_WGT_IDX_BE4,
			      csi_tone_idx, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_CSI_WGT_BE4, B_CSI_WGT_EN_BE4, 1, phy_idx);
}

static const struct rtw89_nbi_reg_def rtw8922d_nbi_reg_def[] = {
	[RF_PATH_A] = {
		.notch1_idx = {0x241A0, 0xFF},
		.notch1_frac_idx = {0x241A0, 0xC00},
		.notch1_en = {0x241A0, 0x1000},
		.notch2_idx = {0x241AC, 0xFF},
		.notch2_frac_idx = {0x241AC, 0xC00},
		.notch2_en = {0x241AC, 0x1000},
	},
	[RF_PATH_B] = {
		.notch1_idx = {0x245A0, 0xFF},
		.notch1_frac_idx = {0x245A0, 0xC00},
		.notch1_en = {0x245A0, 0x1000},
		.notch2_idx = {0x245AC, 0xFF},
		.notch2_frac_idx = {0x245AC, 0xC00},
		.notch2_en = {0x245AC, 0x1000},
	},
};

static void rtw8922d_set_nbi_tone_idx(struct rtw89_dev *rtwdev,
				      const struct rtw89_chan *chan,
				      enum rtw89_rf_path path,
				      enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_nbi_reg_def *nbi = &rtw8922d_nbi_reg_def[path];
	s32 nbi_frac_idx, nbi_frac_tone_idx;
	s32 nbi_idx, nbi_tone_idx;
	bool notch2_chk = false;
	u32 spur_freq, fc;
	s32 freq_diff;

	spur_freq = rtw8922d_spur_freq(rtwdev, chan, true, path);
	if (spur_freq == 0) {
		rtw89_phy_write32_idx(rtwdev, nbi->notch1_en.addr,
				      nbi->notch1_en.mask, 0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, nbi->notch2_en.addr,
				      nbi->notch2_en.mask, 0, phy_idx);
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
	nbi_frac_tone_idx =
		s32_div_u32_round_closest(nbi_frac_idx, CARRIER_SPACING_78_125);

	if (chan->band_width == RTW89_CHANNEL_WIDTH_160 && notch2_chk) {
		rtw89_phy_write32_idx(rtwdev, nbi->notch2_idx.addr,
				      nbi->notch2_idx.mask, nbi_tone_idx, phy_idx);
		rtw89_phy_write32_idx(rtwdev, nbi->notch2_frac_idx.addr,
				      nbi->notch2_frac_idx.mask, nbi_frac_tone_idx,
				      phy_idx);
		rtw89_phy_write32_idx(rtwdev, nbi->notch2_en.addr,
				      nbi->notch2_en.mask, 0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, nbi->notch2_en.addr,
				      nbi->notch2_en.mask, 1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, nbi->notch1_en.addr,
				      nbi->notch1_en.mask, 0, phy_idx);
	} else {
		rtw89_phy_write32_idx(rtwdev, nbi->notch1_idx.addr,
				      nbi->notch1_idx.mask, nbi_tone_idx, phy_idx);
		rtw89_phy_write32_idx(rtwdev, nbi->notch1_frac_idx.addr,
				      nbi->notch1_frac_idx.mask, nbi_frac_tone_idx,
				      phy_idx);
		rtw89_phy_write32_idx(rtwdev, nbi->notch1_en.addr,
				      nbi->notch1_en.mask, 0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, nbi->notch1_en.addr,
				      nbi->notch1_en.mask, 1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, nbi->notch2_en.addr,
				      nbi->notch2_en.mask, 0, phy_idx);
	}
}

static void rtw8922d_spur_elimination(struct rtw89_dev *rtwdev,
				      const struct rtw89_chan *chan,
				      enum rtw89_phy_idx phy_idx)
{
	rtw8922d_set_csi_tone_idx(rtwdev, chan, phy_idx);
	rtw8922d_set_nbi_tone_idx(rtwdev, chan, RF_PATH_A, phy_idx);
	rtw8922d_set_nbi_tone_idx(rtwdev, chan, RF_PATH_B, phy_idx);
}

static const u32 bbrst_mask[2] = {B_BE_FEN_BBPLAT_RSTB, B_BE_FEN_BB1PLAT_RSTB};
static const u32 glbrst_mask[2] = {B_BE_FEN_BB_IP_RSTN, B_BE_FEN_BB1_IP_RSTN};
static const u32 chip_top_bitmask[2] = {0xffff, 0xffff0000};

static void rtw8922d_bb_preinit(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	rtw89_write32_mask(rtwdev, R_BE_FEN_RST_ENABLE, glbrst_mask[phy_idx], 0x0);
	rtw89_write32_mask(rtwdev, R_BE_FEN_RST_ENABLE, bbrst_mask[phy_idx], 0x0);
	rtw89_write32_mask(rtwdev, R_BE_DMAC_SYS_CR32B, chip_top_bitmask[phy_idx], 0x74F9);
	rtw89_write32_mask(rtwdev, R_BE_FEN_RST_ENABLE, glbrst_mask[phy_idx], 0x1);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC_BE4,  B_RSTB_ASYNC_BE4, 0, phy_idx);
	rtw89_write32_mask(rtwdev, R_BE_FEN_RST_ENABLE, bbrst_mask[phy_idx], 0x1);
}

static void rtw8922d_bb_postinit(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	rtw89_phy_write32_idx_clr(rtwdev, R_SHAPER_COEFF_BE4, B_SHAPER_COEFF_BE4, phy_idx);
	rtw89_phy_write32_idx_set(rtwdev, R_SHAPER_COEFF_BE4, B_SHAPER_COEFF_BE4, phy_idx);
}

static void rtw8922d_bb_reset_en(struct rtw89_dev *rtwdev, enum rtw89_band band,
				 bool en, enum rtw89_phy_idx phy_idx)
{
	if (en)
		rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC_BE4, B_RSTB_ASYNC_BE4, 1, phy_idx);
	else
		rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC_BE4, B_RSTB_ASYNC_BE4, 0, phy_idx);
}

static int rtw8922d_ctrl_tx_path_tmac(struct rtw89_dev *rtwdev,
				      enum rtw89_rf_path tx_path,
				      enum rtw89_phy_idx phy_idx)
{
	struct rtw89_reg2_def path_com_cr[] = {
		{0x11A00, 0x21C86900},
		{0x11A04, 0x00E4E433},
		{0x11A08, 0x39390CC9},
		{0x11A10, 0x10CC0000},
		{0x11A14, 0x00240393},
		{0x11A18, 0x201C8600},
		{0x11B38, 0x39393FDB},
		{0x11B3C, 0x00E4E4FF},
	};
	int ret = 0;
	u32 reg;
	int i;

	rtw89_phy_write32_idx(rtwdev, R_TXINFO_PATH_BE4, B_TXINFO_PATH_EN_BE4, 0x0, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_TXINFO_PATH_BE4, B_TXINFO_PATH_MA_BE4, 0x0, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_TXINFO_PATH_BE4, B_TXINFO_PATH_MB_BE4, 0x0, phy_idx);

	if (phy_idx == RTW89_PHY_1 && !rtwdev->dbcc_en)
		return 0;

	if (tx_path == RF_PATH_A) {
		path_com_cr[1].data = 0x40031;
		path_com_cr[2].data = 0x1000C48;
		path_com_cr[5].data = 0x200;
		path_com_cr[6].data = 0x1000C48;
		path_com_cr[7].data = 0x40031;
	} else if (tx_path == RF_PATH_B) {
		path_com_cr[1].data = 0x40032;
		path_com_cr[2].data = 0x1000C88;
		path_com_cr[5].data = 0x400;
		path_com_cr[6].data = 0x1000C88;
		path_com_cr[7].data = 0x40032;
	} else if (tx_path == RF_PATH_AB) {
		path_com_cr[1].data = 0x00E4E433;
		path_com_cr[2].data = 0x39390CC9;
		path_com_cr[5].data = 0x201C8600;
		path_com_cr[6].data = 0x1010CC9;
		path_com_cr[7].data = 0x40433;
	} else {
		ret = -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(path_com_cr); i++) {
		reg = rtw89_mac_reg_by_idx(rtwdev, path_com_cr[i].addr, phy_idx);
		rtw89_write32(rtwdev, reg, path_com_cr[i].data);
	}

	return ret;
}

static void rtw8922d_bb_reset(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
}

static void rtw8922d_tssi_reset(struct rtw89_dev *rtwdev,
				enum rtw89_rf_path path,
				enum rtw89_phy_idx phy_idx)
{
	if (rtwdev->mlo_dbcc_mode == MLO_1_PLUS_1_1RF) {
		if (phy_idx == RTW89_PHY_0) {
			rtw89_phy_write32_mask(rtwdev, R_TXPWR_RSTB0_BE4,
					       B_TXPWR_RSTB0_BE4, 0x0);
			rtw89_phy_write32_mask(rtwdev, R_TXPWR_RSTB0_BE4,
					       B_TXPWR_RSTB0_BE4, 0x1);
		} else {
			rtw89_phy_write32_mask(rtwdev, R_TXPWR_RSTB1_BE4,
					       B_TXPWR_RSTB1_BE4, 0x0);
			rtw89_phy_write32_mask(rtwdev, R_TXPWR_RSTB1_BE4,
					       B_TXPWR_RSTB1_BE4, 0x1);
		}
	} else {
		rtw89_phy_write32_mask(rtwdev, R_TXPWR_RSTB0_BE4, B_TXPWR_RSTB0_BE4, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_TXPWR_RSTB0_BE4, B_TXPWR_RSTB0_BE4, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_TXPWR_RSTB1_BE4, B_TXPWR_RSTB1_BE4, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_TXPWR_RSTB1_BE4, B_TXPWR_RSTB1_BE4, 0x1);
	}
}

static int rtw8922d_ctrl_rx_path_tmac(struct rtw89_dev *rtwdev,
				      enum rtw89_rf_path rx_path,
				      enum rtw89_phy_idx phy_idx)
{
	enum rtw89_rf_path_bit path;

	if (rx_path == RF_PATH_A)
		path = RF_A;
	else if (rx_path == RF_PATH_B)
		path = RF_B;
	else
		path = RF_AB;

	rtw89_phy_write32_idx(rtwdev, R_ANT_RX_BE4, B_ANT_RX_BE4, path, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_ANT_RX_1RCCA_BE4, B_ANT_RX_1RCCA_BE4,
			      path, phy_idx);

	if (rx_path == RF_PATH_AB) {
		rtw89_phy_write32_idx(rtwdev, R_RXCH_BCC0_BE4, B_RXCH_MCS4_BE4, 8, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXCH_BCC1_BE4, B_RXCH_MCS5_BE4, 4, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXCH_BCC1_BE4, B_RXCH_MCS6_BE4, 3, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXCH_BCC1_BE4, B_RXCH_MCS7_BE4, 7, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXCH_BCC1_BE4, B_RXCH_MCS8_BE4, 2, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXCH_BCC1_BE4, B_RXCH_MCS9_BE4, 2, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_AWGN00_BE4, B_RX_AWGN04_BE4, 4, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_AWGN00_BE4, B_RX_AWGN07_BE4, 2, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_AWGN01_BE4, B_RX_AWGN09_BE4, 0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_AWGN02_BE4, B_RX_AWGN11_BE4, 1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_LDPC00_BE4, B_RX_LDPC04_BE4, 8, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_LDPC00_BE4, B_RX_LDPC05_BE4, 5, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_LDPC00_BE4, B_RX_LDPC06_BE4, 3, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_LDPC00_BE4, B_RX_LDPC07_BE4, 5, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_LDPC00_BE4, B_RX_LDPC08_BE4, 1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_LDPC01_BE4, B_RX_LDPC09_BE4, 2, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_LDPC02_BE4, B_RX_LDPC10_BE4, 4, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_LDPC02_BE4, B_RX_LDPC11_BE4, 2, phy_idx);
	} else {
		rtw89_phy_write32_idx(rtwdev, R_RXCH_BCC0_BE4, B_RXCH_MCS4_BE4, 13, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXCH_BCC1_BE4, B_RXCH_MCS5_BE4, 15, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXCH_BCC1_BE4, B_RXCH_MCS6_BE4, 6, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXCH_BCC1_BE4, B_RXCH_MCS7_BE4, 15, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXCH_BCC1_BE4, B_RXCH_MCS8_BE4, 4, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RXCH_BCC1_BE4, B_RXCH_MCS9_BE4, 15, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_AWGN00_BE4, B_RX_AWGN04_BE4, 9, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_AWGN00_BE4, B_RX_AWGN07_BE4, 3, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_AWGN01_BE4, B_RX_AWGN09_BE4, 1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_AWGN02_BE4, B_RX_AWGN11_BE4, 0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_LDPC00_BE4, B_RX_LDPC04_BE4, 9, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_LDPC00_BE4, B_RX_LDPC05_BE4, 8, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_LDPC00_BE4, B_RX_LDPC06_BE4, 6, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_LDPC00_BE4, B_RX_LDPC07_BE4, 16, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_LDPC00_BE4, B_RX_LDPC08_BE4, 4, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_LDPC01_BE4, B_RX_LDPC09_BE4, 9, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_LDPC02_BE4, B_RX_LDPC10_BE4, 9, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RX_LDPC02_BE4, B_RX_LDPC11_BE4, 7, phy_idx);
	}

	return 0;
}

static void rtw8922d_set_digital_pwr_comp(struct rtw89_dev *rtwdev,
					  const struct rtw89_chan *chan, u8 nss,
					  enum rtw89_rf_path path,
					  enum rtw89_phy_idx phy_idx)
{
#define DIGITAL_PWR_COMP_REG_NUM 22
	static const u32 pw_comp_cr[2] = {R_RX_PATH0_TBL0_BE4, R_RX_PATH1_TBL0_BE4};
	const __le32 (*pwr_comp_val)[2][RTW89_TX_COMP_BAND_NR]
				    [BB_PATH_NUM_8922D][DIGITAL_PWR_COMP_REG_NUM];
	struct rtw89_fw_elm_info *elm_info = &rtwdev->fw.elm_info;
	const struct rtw89_fw_element_hdr *txcomp_elm = elm_info->tx_comp;
	const __le32 *digital_pwr_comp;
	u32 addr, val;
	u32 i;

	if (sizeof(*pwr_comp_val) != le32_to_cpu(txcomp_elm->size)) {
		rtw89_debug(rtwdev, RTW89_DBG_UNEXP,
			    "incorrect power comp size %d\n",
			    le32_to_cpu(txcomp_elm->size));
		return;
	}

	pwr_comp_val = (const void *)txcomp_elm->u.common.contents;
	digital_pwr_comp = (*pwr_comp_val)[nss][chan->tx_comp_band][path];
	addr = pw_comp_cr[path];

	for (i = 0; i < DIGITAL_PWR_COMP_REG_NUM; i++, addr += 4) {
		val = le32_to_cpu(digital_pwr_comp[i]);
		rtw89_phy_write32_idx(rtwdev, addr, MASKDWORD, val, phy_idx);
	}
}

static void rtw8922d_digital_pwr_comp(struct rtw89_dev *rtwdev,
				      enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_chan *chan0 = rtw89_mgnt_chan_get(rtwdev, 0);
	const struct rtw89_chan *chan1 = rtw89_mgnt_chan_get(rtwdev, 1);

	if (rtwdev->mlo_dbcc_mode == MLO_1_PLUS_1_1RF) {
		rtw8922d_set_digital_pwr_comp(rtwdev, chan0, 0, RF_PATH_A, RTW89_PHY_0);
		rtw8922d_set_digital_pwr_comp(rtwdev, chan1, 0, RF_PATH_B, RTW89_PHY_1);
	} else {
		rtw8922d_set_digital_pwr_comp(rtwdev, chan0, 1, RF_PATH_A, phy_idx);
		rtw8922d_set_digital_pwr_comp(rtwdev, chan0, 1, RF_PATH_B, phy_idx);
	}
}

static int rtw8922d_ctrl_mlo(struct rtw89_dev *rtwdev, enum rtw89_mlo_dbcc_mode mode,
			     bool pwr_comp)
{
	const struct rtw89_chan *chan1;
	u32 reg0, reg1;
	u8 cck_phy_idx;

	if (mode == MLO_2_PLUS_0_1RF) {
		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_SWITCH_BE4, 0xBBBB);
		udelay(1);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_BB_CLK_BE4, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_SWITCH_BE4, 0xAFFF);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_SWITCH_BE4, 0xEBAD);
		udelay(1);

		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_BB_CLK_BE4, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_SWITCH_BE4, 0xEAAD);
	} else if (mode == MLO_0_PLUS_2_1RF) {
		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_SWITCH_BE4, 0xBBBB);
		udelay(1);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_BB_CLK_BE4, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_SWITCH_BE4, 0xAFFF);
		udelay(1);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_SWITCH_BE4, 0xEFFF);

		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_BB_CLK_BE4, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_SWITCH_BE4, 0xEEFF);
	} else if ((mode == MLO_1_PLUS_1_1RF) || (mode == DBCC_LEGACY)) {
		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_SWITCH_BE4, 0xBBBB);
		udelay(1);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_BB_CLK_BE4, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_SWITCH_BE4, 0xAFFF);
		udelay(1);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_BB_CLK_BE4, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_SWITCH_BE4, 0x3AAB);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_SWITCH_BE4, 0x6180);
		udelay(1);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_BB_CLK_BE4, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_SWITCH_BE4, 0x180);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_BB_CLK_BE4, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_EMLSR_SWITCH_BE4, B_EMLSR_SWITCH_BE4, 0x0);
	}

	if (pwr_comp)
		rtw8922d_digital_pwr_comp(rtwdev, RTW89_PHY_0);

	reg0 = R_BBWRAP_ELMSR_BE4;
	reg1 = rtw89_mac_reg_by_idx(rtwdev, reg0, 1);

	if (mode == MLO_2_PLUS_0_1RF) {
		rtw89_phy_write32_mask(rtwdev, R_SYS_DBCC_BE4,
				       B_SYS_DBCC_24G_BAND_SEL_BE4, RTW89_PHY_0);
		rtw89_write32_mask(rtwdev, reg0, B_BBWRAP_ELMSR_EN_BE4, 0);
		rtw89_write32_mask(rtwdev, reg1, B_BBWRAP_ELMSR_EN_BE4, 0);
	} else if (mode == MLO_0_PLUS_2_1RF) {
		rtw89_phy_write32_mask(rtwdev, R_SYS_DBCC_BE4,
				       B_SYS_DBCC_24G_BAND_SEL_BE4, RTW89_PHY_0);
		rtw89_write32_mask(rtwdev, reg0, B_BBWRAP_ELMSR_EN_BE4, 0);
		rtw89_write32_mask(rtwdev, reg1, B_BBWRAP_ELMSR_EN_BE4, 0);
	} else if ((mode == MLO_1_PLUS_1_1RF) || (mode == DBCC_LEGACY)) {
		chan1 = rtw89_mgnt_chan_get(rtwdev, 1);
		cck_phy_idx = chan1->band_type == RTW89_BAND_2G ?
			      RTW89_PHY_1 : RTW89_PHY_0;

		rtw89_phy_write32_mask(rtwdev, R_SYS_DBCC_BE4,
				       B_SYS_DBCC_24G_BAND_SEL_BE4, cck_phy_idx);
		rtw89_write32_mask(rtwdev, reg0, B_BBWRAP_ELMSR_EN_BE4, 0x3);
		rtw89_write32_mask(rtwdev, reg1, B_BBWRAP_ELMSR_EN_BE4, 0x3);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_SYS_DBCC_BE4,
				       B_SYS_DBCC_24G_BAND_SEL_BE4, RTW89_PHY_0);
		rtw89_write32_mask(rtwdev, reg0, B_BBWRAP_ELMSR_EN_BE4, 0);
		rtw89_write32_mask(rtwdev, reg1, B_BBWRAP_ELMSR_EN_BE4, 0);
	}

	udelay(1);

	return 0;
}

static void rtw8922d_bb_sethw(struct rtw89_dev *rtwdev)
{
	struct rtw89_phy_efuse_gain *gain = &rtwdev->efuse_gain;
	struct rtw89_hal *hal = &rtwdev->hal;
	enum rtw89_phy_idx phy_idx;
	u32 reg;

	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_BOOST, RTW89_PHY_0);
	rtw89_write32_clr(rtwdev, reg, B_BE_PWR_CTRL_SEL);
	reg = rtw89_mac_reg_by_idx(rtwdev, R_BE_PWR_BOOST, RTW89_PHY_1);
	rtw89_write32_clr(rtwdev, reg, B_BE_PWR_CTRL_SEL);

	if (hal->cid == RTL8922D_CID7090) {
		reg = rtw89_mac_reg_by_idx(rtwdev, R_PWR_BOOST_BE4, RTW89_PHY_0);
		rtw89_write32_set(rtwdev, reg, B_PWR_BOOST_BE4);
		reg = rtw89_mac_reg_by_idx(rtwdev, R_PWR_BOOST_BE4, RTW89_PHY_1);
		rtw89_write32_set(rtwdev, reg, B_PWR_BOOST_BE4);
	}

	rtw89_phy_write32_mask(rtwdev, R_TX_ERROR_SEL_BE4, B_TX_ERROR_PSDU_BE4, 0);
	rtw89_phy_write32_mask(rtwdev, R_TX_ERROR_SEL_BE4, B_TX_ERROR_NSYM_BE4, 1);
	rtw89_phy_write32_mask(rtwdev, R_TX_ERROR_SEL_BE4, B_TX_ERROR_LSIG_BE4, 1);
	rtw89_phy_write32_mask(rtwdev, R_TX_ERROR_SEL_BE4, B_TX_ERROR_TXINFO_BE4, 1);
	rtw89_phy_write32_mask(rtwdev, R_TXERRCT_EN_BE4, B_TXERRCT_EN_BE4, 0);
	rtw89_phy_write32_mask(rtwdev, R_TXERRCT1_EN_BE4, B_TXERRCT1_EN_BE4, 0);
	rtw89_phy_write32_idx(rtwdev, R_IMR_TX_ERROR_BE4, B_IMR_TX_ERROR_BE4, 1, RTW89_PHY_0);
	rtw89_phy_write32_idx(rtwdev, R_IMR_TX_ERROR_BE4, B_IMR_TX_ERROR_BE4, 1, RTW89_PHY_1);

	rtw8922d_ctrl_mlo(rtwdev, rtwdev->mlo_dbcc_mode, false);

	/* read these registers after loading BB parameters */
	for (phy_idx = RTW89_PHY_0; phy_idx < RTW89_PHY_NUM; phy_idx++) {
		gain->ref_gain_base[phy_idx] =
			rtw89_phy_read32_idx(rtwdev, R_OFDM_OFST_P0_BE4,
					     B_OFDM_OFST_P0_BE4, phy_idx);
		gain->cck_rpl_base[phy_idx] =
			rtw89_phy_read32_idx(rtwdev, R_CCK_RPL_OFST_BE4,
					     B_CCK_RPL_OFST_BE4, phy_idx);
	}
}

static void rtw8922d_set_channel_bb(struct rtw89_dev *rtwdev,
				    const struct rtw89_chan *chan,
				    enum rtw89_phy_idx phy_idx)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	bool cck_en = chan->band_type == RTW89_BAND_2G;
	u8 pri_sb = chan->pri_sb_idx;
	u32 val;

	rtw89_phy_bb_wrap_set_rfsi_ct_opt(rtwdev, phy_idx);
	rtw8922d_ctrl_ch(rtwdev, chan, phy_idx);
	rtw8922d_ctrl_bw(rtwdev, pri_sb, chan->band_width, phy_idx);
	rtw89_phy_bb_wrap_set_rfsi_bandedge_ch(rtwdev, chan, phy_idx);

	if (cck_en)
		rtw8922d_ctrl_sco_cck(rtwdev, chan->primary_channel,
				      chan->band_width, phy_idx);

	rtw8922d_spur_elimination(rtwdev, chan, phy_idx);

	if (hal->cid == RTL8922D_CID7025) {
		if (chan->band_width == RTW89_CHANNEL_WIDTH_160)
			val = 0x1f9;
		else if (chan->band_width == RTW89_CHANNEL_WIDTH_80)
			val = 0x1f5;
		else
			val = 0x1e2;

		rtw89_phy_write32_idx(rtwdev, R_AWGN_DET_BE4, B_AWGN_DET_BE4, val, phy_idx);
	}

	rtw8922d_tssi_reset(rtwdev, RF_PATH_AB, phy_idx);
}

MODULE_FIRMWARE(RTW8922D_MODULE_FIRMWARE);
MODULE_FIRMWARE(RTW8922DS_MODULE_FIRMWARE);
MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11be wireless 8922D driver");
MODULE_LICENSE("Dual BSD/GPL");
