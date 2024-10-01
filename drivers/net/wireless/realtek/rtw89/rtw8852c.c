// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2022  Realtek Corporation
 */

#include "chan.h"
#include "coex.h"
#include "debug.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"
#include "rtw8852c.h"
#include "rtw8852c_rfk.h"
#include "rtw8852c_table.h"
#include "util.h"

#define RTW8852C_FW_FORMAT_MAX 1
#define RTW8852C_FW_BASENAME "rtw89/rtw8852c_fw"
#define RTW8852C_MODULE_FIRMWARE \
	RTW8852C_FW_BASENAME "-" __stringify(RTW8852C_FW_FORMAT_MAX) ".bin"

static const struct rtw89_hfc_ch_cfg rtw8852c_hfc_chcfg_pcie[] = {
	{13, 1614, grp_0}, /* ACH 0 */
	{13, 1614, grp_0}, /* ACH 1 */
	{13, 1614, grp_0}, /* ACH 2 */
	{13, 1614, grp_0}, /* ACH 3 */
	{13, 1614, grp_1}, /* ACH 4 */
	{13, 1614, grp_1}, /* ACH 5 */
	{13, 1614, grp_1}, /* ACH 6 */
	{13, 1614, grp_1}, /* ACH 7 */
	{13, 1614, grp_0}, /* B0MGQ */
	{13, 1614, grp_0}, /* B0HIQ */
	{13, 1614, grp_1}, /* B1MGQ */
	{13, 1614, grp_1}, /* B1HIQ */
	{40, 0, 0} /* FWCMDQ */
};

static const struct rtw89_hfc_pub_cfg rtw8852c_hfc_pubcfg_pcie = {
	1614, /* Group 0 */
	1614, /* Group 1 */
	3228, /* Public Max */
	0 /* WP threshold */
};

static const struct rtw89_hfc_param_ini rtw8852c_hfc_param_ini_pcie[] = {
	[RTW89_QTA_SCC] = {rtw8852c_hfc_chcfg_pcie, &rtw8852c_hfc_pubcfg_pcie,
			   &rtw89_mac_size.hfc_preccfg_pcie, RTW89_HCIFC_POH},
	[RTW89_QTA_DLFW] = {NULL, NULL, &rtw89_mac_size.hfc_preccfg_pcie,
			    RTW89_HCIFC_POH},
	[RTW89_QTA_INVALID] = {NULL},
};

static const struct rtw89_dle_mem rtw8852c_dle_mem_pcie[] = {
	[RTW89_QTA_SCC] = {RTW89_QTA_SCC, &rtw89_mac_size.wde_size19,
			   &rtw89_mac_size.ple_size19, &rtw89_mac_size.wde_qt18,
			   &rtw89_mac_size.wde_qt18, &rtw89_mac_size.ple_qt46,
			   &rtw89_mac_size.ple_qt47},
	[RTW89_QTA_DLFW] = {RTW89_QTA_DLFW, &rtw89_mac_size.wde_size18,
			    &rtw89_mac_size.ple_size18, &rtw89_mac_size.wde_qt17,
			    &rtw89_mac_size.wde_qt17, &rtw89_mac_size.ple_qt44,
			    &rtw89_mac_size.ple_qt45},
	[RTW89_QTA_INVALID] = {RTW89_QTA_INVALID, NULL, NULL, NULL, NULL, NULL,
			       NULL},
};

static const u32 rtw8852c_h2c_regs[RTW89_H2CREG_MAX] = {
	R_AX_H2CREG_DATA0_V1, R_AX_H2CREG_DATA1_V1, R_AX_H2CREG_DATA2_V1,
	R_AX_H2CREG_DATA3_V1
};

static const u32 rtw8852c_c2h_regs[RTW89_H2CREG_MAX] = {
	R_AX_C2HREG_DATA0_V1, R_AX_C2HREG_DATA1_V1, R_AX_C2HREG_DATA2_V1,
	R_AX_C2HREG_DATA3_V1
};

static const u32 rtw8852c_wow_wakeup_regs[RTW89_WOW_REASON_NUM] = {
	R_AX_C2HREG_DATA3_V1 + 3, R_AX_DBG_WOW,
};

static const struct rtw89_page_regs rtw8852c_page_regs = {
	.hci_fc_ctrl	= R_AX_HCI_FC_CTRL_V1,
	.ch_page_ctrl	= R_AX_CH_PAGE_CTRL_V1,
	.ach_page_ctrl	= R_AX_ACH0_PAGE_CTRL_V1,
	.ach_page_info	= R_AX_ACH0_PAGE_INFO_V1,
	.pub_page_info3	= R_AX_PUB_PAGE_INFO3_V1,
	.pub_page_ctrl1	= R_AX_PUB_PAGE_CTRL1_V1,
	.pub_page_ctrl2	= R_AX_PUB_PAGE_CTRL2_V1,
	.pub_page_info1	= R_AX_PUB_PAGE_INFO1_V1,
	.pub_page_info2 = R_AX_PUB_PAGE_INFO2_V1,
	.wp_page_ctrl1	= R_AX_WP_PAGE_CTRL1_V1,
	.wp_page_ctrl2	= R_AX_WP_PAGE_CTRL2_V1,
	.wp_page_info1	= R_AX_WP_PAGE_INFO1_V1,
};

static const struct rtw89_reg_def rtw8852c_dcfo_comp = {
	R_DCFO_COMP_S0_V1, B_DCFO_COMP_S0_V1_MSK
};

static const struct rtw89_imr_info rtw8852c_imr_info = {
	.wdrls_imr_set		= B_AX_WDRLS_IMR_SET_V1,
	.wsec_imr_reg		= R_AX_SEC_ERROR_FLAG_IMR,
	.wsec_imr_set		= B_AX_TX_HANG_IMR | B_AX_RX_HANG_IMR,
	.mpdu_tx_imr_set	= B_AX_MPDU_TX_IMR_SET_V1,
	.mpdu_rx_imr_set	= B_AX_MPDU_RX_IMR_SET_V1,
	.sta_sch_imr_set	= B_AX_STA_SCHEDULER_IMR_SET,
	.txpktctl_imr_b0_reg	= R_AX_TXPKTCTL_B0_ERRFLAG_IMR,
	.txpktctl_imr_b0_clr	= B_AX_TXPKTCTL_IMR_B0_CLR_V1,
	.txpktctl_imr_b0_set	= B_AX_TXPKTCTL_IMR_B0_SET_V1,
	.txpktctl_imr_b1_reg	= R_AX_TXPKTCTL_B1_ERRFLAG_IMR,
	.txpktctl_imr_b1_clr	= B_AX_TXPKTCTL_IMR_B1_CLR_V1,
	.txpktctl_imr_b1_set	= B_AX_TXPKTCTL_IMR_B1_SET_V1,
	.wde_imr_clr		= B_AX_WDE_IMR_CLR_V1,
	.wde_imr_set		= B_AX_WDE_IMR_SET_V1,
	.ple_imr_clr		= B_AX_PLE_IMR_CLR_V1,
	.ple_imr_set		= B_AX_PLE_IMR_SET_V1,
	.host_disp_imr_clr	= B_AX_HOST_DISP_IMR_CLR_V1,
	.host_disp_imr_set	= B_AX_HOST_DISP_IMR_SET_V1,
	.cpu_disp_imr_clr	= B_AX_CPU_DISP_IMR_CLR_V1,
	.cpu_disp_imr_set	= B_AX_CPU_DISP_IMR_SET_V1,
	.other_disp_imr_clr	= B_AX_OTHER_DISP_IMR_CLR_V1,
	.other_disp_imr_set	= B_AX_OTHER_DISP_IMR_SET_V1,
	.bbrpt_com_err_imr_reg	= R_AX_BBRPT_COM_ERR_IMR,
	.bbrpt_chinfo_err_imr_reg = R_AX_BBRPT_CHINFO_ERR_IMR,
	.bbrpt_err_imr_set	= R_AX_BBRPT_CHINFO_IMR_SET_V1,
	.bbrpt_dfs_err_imr_reg	= R_AX_BBRPT_DFS_ERR_IMR,
	.ptcl_imr_clr		= B_AX_PTCL_IMR_CLR_V1,
	.ptcl_imr_set		= B_AX_PTCL_IMR_SET_V1,
	.cdma_imr_0_reg		= R_AX_RX_ERR_FLAG_IMR,
	.cdma_imr_0_clr		= B_AX_RX_ERR_IMR_CLR_V1,
	.cdma_imr_0_set		= B_AX_RX_ERR_IMR_SET_V1,
	.cdma_imr_1_reg		= R_AX_TX_ERR_FLAG_IMR,
	.cdma_imr_1_clr		= B_AX_TX_ERR_IMR_CLR_V1,
	.cdma_imr_1_set		= B_AX_TX_ERR_IMR_SET_V1,
	.phy_intf_imr_reg	= R_AX_PHYINFO_ERR_IMR_V1,
	.phy_intf_imr_clr	= B_AX_PHYINFO_IMR_CLR_V1,
	.phy_intf_imr_set	= B_AX_PHYINFO_IMR_SET_V1,
	.rmac_imr_reg		= R_AX_RX_ERR_IMR,
	.rmac_imr_clr		= B_AX_RMAC_IMR_CLR_V1,
	.rmac_imr_set		= B_AX_RMAC_IMR_SET_V1,
	.tmac_imr_reg		= R_AX_TRXPTCL_ERROR_INDICA_MASK,
	.tmac_imr_clr		= B_AX_TMAC_IMR_CLR_V1,
	.tmac_imr_set		= B_AX_TMAC_IMR_SET_V1,
};

static const struct rtw89_rrsr_cfgs rtw8852c_rrsr_cfgs = {
	.ref_rate = {R_AX_TRXPTCL_RRSR_CTL_0, B_AX_WMAC_RESP_REF_RATE_SEL, 0},
	.rsc = {R_AX_PTCL_RRSR1, B_AX_RSC_MASK, 2},
};

static const struct rtw89_rfkill_regs rtw8852c_rfkill_regs = {
	.pinmux = {R_AX_GPIO8_15_FUNC_SEL,
		   B_AX_PINMUX_GPIO9_FUNC_SEL_MASK,
		   0xf},
	.mode = {R_AX_GPIO_EXT_CTRL + 2,
		 (B_AX_GPIO_MOD_9 | B_AX_GPIO_IO_SEL_9) >> 16,
		 0x0},
};

static const struct rtw89_dig_regs rtw8852c_dig_regs = {
	.seg0_pd_reg = R_SEG0R_PD,
	.pd_lower_bound_mask = B_SEG0R_PD_LOWER_BOUND_MSK,
	.pd_spatial_reuse_en = B_SEG0R_PD_SPATIAL_REUSE_EN_MSK,
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
	.p0_p20_pagcugc_en = {R_PATH0_P20_FOLLOW_BY_PAGCUGC_V1,
			      B_PATH0_P20_FOLLOW_BY_PAGCUGC_EN_MSK},
	.p0_s20_pagcugc_en = {R_PATH0_S20_FOLLOW_BY_PAGCUGC_V1,
			      B_PATH0_S20_FOLLOW_BY_PAGCUGC_EN_MSK},
	.p1_p20_pagcugc_en = {R_PATH1_P20_FOLLOW_BY_PAGCUGC_V1,
			      B_PATH1_P20_FOLLOW_BY_PAGCUGC_EN_MSK},
	.p1_s20_pagcugc_en = {R_PATH1_S20_FOLLOW_BY_PAGCUGC_V1,
			      B_PATH1_S20_FOLLOW_BY_PAGCUGC_EN_MSK},
};

static const struct rtw89_edcca_regs rtw8852c_edcca_regs = {
	.edcca_level			= R_SEG0R_EDCCA_LVL,
	.edcca_mask			= B_EDCCA_LVL_MSK0,
	.edcca_p_mask			= B_EDCCA_LVL_MSK1,
	.ppdu_level			= R_SEG0R_EDCCA_LVL,
	.ppdu_mask			= B_EDCCA_LVL_MSK3,
	.rpt_a				= R_EDCCA_RPT_A,
	.rpt_b				= R_EDCCA_RPT_B,
	.rpt_sel			= R_EDCCA_RPT_SEL,
	.rpt_sel_mask			= B_EDCCA_RPT_SEL_MSK,
	.tx_collision_t2r_st		= R_TX_COLLISION_T2R_ST,
	.tx_collision_t2r_st_mask	= B_TX_COLLISION_T2R_ST_M,
};

static void rtw8852c_ctrl_btg_bt_rx(struct rtw89_dev *rtwdev, bool en,
				    enum rtw89_phy_idx phy_idx);

static void rtw8852c_ctrl_tx_path_tmac(struct rtw89_dev *rtwdev, u8 tx_path,
				       enum rtw89_mac_idx mac_idx);

static int rtw8852c_pwr_on_func(struct rtw89_dev *rtwdev)
{
	u32 val32;
	u32 ret;

	val32 = rtw89_read32_mask(rtwdev, R_AX_SYS_STATUS1, B_AX_PAD_HCI_SEL_V2_MASK);
	if (val32 == MAC_AX_HCI_SEL_PCIE_USB)
		rtw89_write32_set(rtwdev, R_AX_LDO_AON_CTRL0, B_AX_PD_REGU_L);

	rtw89_write32_clr(rtwdev, R_AX_SYS_PW_CTRL, B_AX_AFSM_WLSUS_EN |
						    B_AX_AFSM_PCIE_SUS_EN);
	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_DIS_WLBT_PDNSUSEN_SOPC);
	rtw89_write32_set(rtwdev, R_AX_WLLPS_CTRL, B_AX_DIS_WLBT_LPSEN_LOPC);
	rtw89_write32_clr(rtwdev, R_AX_SYS_PW_CTRL, B_AX_APDM_HPDN);
	rtw89_write32_clr(rtwdev, R_AX_SYS_PW_CTRL, B_AX_APFM_SWLPS);

	rtw89_write32_mask(rtwdev, R_AX_SPS_DIG_ON_CTRL0,
			   B_AX_OCP_L1_MASK, 0x7);

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

	rtw89_write32_clr(rtwdev, R_AX_SYS_ISO_CTRL_EXTEND, B_AX_CMAC1_FEN);
	rtw89_write32_set(rtwdev, R_AX_SYS_ISO_CTRL_EXTEND, B_AX_R_SYM_ISO_CMAC12PP);
	rtw89_write32_clr(rtwdev, R_AX_AFE_CTRL1, B_AX_R_SYM_WLCMAC1_P4_PC_EN |
						  B_AX_R_SYM_WLCMAC1_P3_PC_EN |
						  B_AX_R_SYM_WLCMAC1_P2_PC_EN |
						  B_AX_R_SYM_WLCMAC1_P1_PC_EN |
						  B_AX_R_SYM_WLCMAC1_PC_EN);
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
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_XTAL_XMD_2, 0x10, XTAL_SI_LDO_LPS);
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
	rtw89_write32_set(rtwdev, R_AX_GPIO0_15_EECS_EESK_LED1_PULL_LOW_EN,
			  B_AX_EECS_PULL_LOW_EN | B_AX_EESK_PULL_LOW_EN |
			  B_AX_LED1_PULL_LOW_EN);

	rtw89_write32_set(rtwdev, R_AX_DMAC_FUNC_EN,
			  B_AX_MAC_FUNC_EN | B_AX_DMAC_FUNC_EN | B_AX_MPDU_PROC_EN |
			  B_AX_WD_RLS_EN | B_AX_DLE_WDE_EN | B_AX_TXPKT_CTRL_EN |
			  B_AX_STA_SCH_EN | B_AX_DLE_PLE_EN | B_AX_PKT_BUF_EN |
			  B_AX_DMAC_TBL_EN | B_AX_PKT_IN_EN | B_AX_DLE_CPUIO_EN |
			  B_AX_DISPATCHER_EN | B_AX_BBRPT_EN | B_AX_MAC_SEC_EN |
			  B_AX_MAC_UN_EN | B_AX_H_AXIDMA_EN);

	rtw89_write32_set(rtwdev, R_AX_CMAC_FUNC_EN,
			  B_AX_CMAC_EN | B_AX_CMAC_TXEN | B_AX_CMAC_RXEN |
			  B_AX_FORCE_CMACREG_GCKEN | B_AX_PHYINTF_EN |
			  B_AX_CMAC_DMA_EN | B_AX_PTCLTOP_EN | B_AX_SCHEDULER_EN |
			  B_AX_TMAC_EN | B_AX_RMAC_EN);

	rtw89_write32_mask(rtwdev, R_AX_LED1_FUNC_SEL, B_AX_PINMUX_EESK_FUNC_SEL_V1_MASK,
			   PINMUX_EESK_FUNC_SEL_BT_LOG);

	return 0;
}

static int rtw8852c_pwr_off_func(struct rtw89_dev *rtwdev)
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
	rtw89_write32_clr(rtwdev, R_AX_SYS_ISO_CTRL_EXTEND,
			  B_AX_R_SYM_FEN_WLBBGLB_1 | B_AX_R_SYM_FEN_WLBBFUN_1);
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
	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_XTAL_OFF_A_DIE);
	rtw89_write32_set(rtwdev, R_AX_SYS_SWR_CTRL1, B_AX_SYM_CTRL_SPS_PWMFREQ);
	rtw89_write32_mask(rtwdev, R_AX_SPS_DIG_ON_CTRL0,
			   B_AX_REG_ZCDC_H_MASK, 0x3);
	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_APFM_SWLPS);

	return 0;
}

static void rtw8852c_e_efuse_parsing(struct rtw89_efuse *efuse,
				     struct rtw8852c_efuse *map)
{
	ether_addr_copy(efuse->addr, map->e.mac_addr);
	efuse->rfe_type = map->rfe_type;
	efuse->xtal_cap = map->xtal_k;
}

static void rtw8852c_efuse_parsing_tssi(struct rtw89_dev *rtwdev,
					struct rtw8852c_efuse *map)
{
	struct rtw89_tssi_info *tssi = &rtwdev->tssi;
	struct rtw8852c_tssi_offset *ofst[] = {&map->path_a_tssi, &map->path_b_tssi};
	u8 *bw40_1s_tssi_6g_ofst[] = {map->bw40_1s_tssi_6g_a, map->bw40_1s_tssi_6g_b};
	u8 i, j;

	tssi->thermal[RF_PATH_A] = map->path_a_therm;
	tssi->thermal[RF_PATH_B] = map->path_b_therm;

	for (i = 0; i < RF_PATH_NUM_8852C; i++) {
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

static bool _decode_efuse_gain(u8 data, s8 *high, s8 *low)
{
	if (high)
		*high = sign_extend32(FIELD_GET(GENMASK(7,  4), data), 3);
	if (low)
		*low = sign_extend32(FIELD_GET(GENMASK(3,  0), data), 3);

	return data != 0xff;
}

static void rtw8852c_efuse_parsing_gain_offset(struct rtw89_dev *rtwdev,
					       struct rtw8852c_efuse *map)
{
	struct rtw89_phy_efuse_gain *gain = &rtwdev->efuse_gain;
	bool valid = false;

	valid |= _decode_efuse_gain(map->rx_gain_2g_cck,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_2G_CCK],
				    &gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_2G_CCK]);
	valid |= _decode_efuse_gain(map->rx_gain_2g_ofdm,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_2G_OFDM],
				    &gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_2G_OFDM]);
	valid |= _decode_efuse_gain(map->rx_gain_5g_low,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_5G_LOW],
				    &gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_5G_LOW]);
	valid |= _decode_efuse_gain(map->rx_gain_5g_mid,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_5G_MID],
				    &gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_5G_MID]);
	valid |= _decode_efuse_gain(map->rx_gain_5g_high,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_5G_HIGH],
				    &gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_5G_HIGH]);
	valid |= _decode_efuse_gain(map->rx_gain_6g_l0,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_6G_L0],
				    &gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_6G_L0]);
	valid |= _decode_efuse_gain(map->rx_gain_6g_l1,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_6G_L1],
				    &gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_6G_L1]);
	valid |= _decode_efuse_gain(map->rx_gain_6g_m0,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_6G_M0],
				    &gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_6G_M0]);
	valid |= _decode_efuse_gain(map->rx_gain_6g_m1,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_6G_M1],
				    &gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_6G_M1]);
	valid |= _decode_efuse_gain(map->rx_gain_6g_h0,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_6G_H0],
				    &gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_6G_H0]);
	valid |= _decode_efuse_gain(map->rx_gain_6g_h1,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_6G_H1],
				    &gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_6G_H1]);
	valid |= _decode_efuse_gain(map->rx_gain_6g_uh0,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_6G_UH0],
				    &gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_6G_UH0]);
	valid |= _decode_efuse_gain(map->rx_gain_6g_uh1,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_6G_UH1],
				    &gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_6G_UH1]);

	gain->offset_valid = valid;
}

static int rtw8852c_read_efuse(struct rtw89_dev *rtwdev, u8 *log_map,
			       enum rtw89_efuse_block block)
{
	struct rtw89_efuse *efuse = &rtwdev->efuse;
	struct rtw8852c_efuse *map;

	map = (struct rtw8852c_efuse *)log_map;

	efuse->country_code[0] = map->country_code[0];
	efuse->country_code[1] = map->country_code[1];
	rtw8852c_efuse_parsing_tssi(rtwdev, map);
	rtw8852c_efuse_parsing_gain_offset(rtwdev, map);

	switch (rtwdev->hci.type) {
	case RTW89_HCI_TYPE_PCIE:
		rtw8852c_e_efuse_parsing(efuse, map);
		break;
	default:
		return -ENOTSUPP;
	}

	rtw89_info(rtwdev, "chip rfe_type is %d\n", efuse->rfe_type);

	return 0;
}

static void rtw8852c_phycap_parsing_tssi(struct rtw89_dev *rtwdev, u8 *phycap_map)
{
	struct rtw89_tssi_info *tssi = &rtwdev->tssi;
	static const u32 tssi_trim_addr[RF_PATH_NUM_8852C] = {0x5D6, 0x5AB};
	static const u32 tssi_trim_addr_6g[RF_PATH_NUM_8852C] = {0x5CE, 0x5A3};
	u32 addr = rtwdev->chip->phycap_addr;
	bool pg = false;
	u32 ofst;
	u8 i, j;

	for (i = 0; i < RF_PATH_NUM_8852C; i++) {
		for (j = 0; j < TSSI_TRIM_CH_GROUP_NUM; j++) {
			/* addrs are in decreasing order */
			ofst = tssi_trim_addr[i] - addr - j;
			tssi->tssi_trim[i][j] = phycap_map[ofst];

			if (phycap_map[ofst] != 0xff)
				pg = true;
		}

		for (j = 0; j < TSSI_TRIM_CH_GROUP_NUM_6G; j++) {
			/* addrs are in decreasing order */
			ofst = tssi_trim_addr_6g[i] - addr - j;
			tssi->tssi_trim_6g[i][j] = phycap_map[ofst];

			if (phycap_map[ofst] != 0xff)
				pg = true;
		}
	}

	if (!pg) {
		memset(tssi->tssi_trim, 0, sizeof(tssi->tssi_trim));
		memset(tssi->tssi_trim_6g, 0, sizeof(tssi->tssi_trim_6g));
		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM] no PG, set all trim info to 0\n");
	}

	for (i = 0; i < RF_PATH_NUM_8852C; i++)
		for (j = 0; j < TSSI_TRIM_CH_GROUP_NUM; j++)
			rtw89_debug(rtwdev, RTW89_DBG_TSSI,
				    "[TSSI] path=%d idx=%d trim=0x%x addr=0x%x\n",
				    i, j, tssi->tssi_trim[i][j],
				    tssi_trim_addr[i] - j);
}

static void rtw8852c_phycap_parsing_thermal_trim(struct rtw89_dev *rtwdev,
						 u8 *phycap_map)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	static const u32 thm_trim_addr[RF_PATH_NUM_8852C] = {0x5DF, 0x5DC};
	u32 addr = rtwdev->chip->phycap_addr;
	u8 i;

	for (i = 0; i < RF_PATH_NUM_8852C; i++) {
		info->thermal_trim[i] = phycap_map[thm_trim_addr[i] - addr];

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[THERMAL][TRIM] path=%d thermal_trim=0x%x\n",
			    i, info->thermal_trim[i]);

		if (info->thermal_trim[i] != 0xff)
			info->pg_thermal_trim = true;
	}
}

static void rtw8852c_thermal_trim(struct rtw89_dev *rtwdev)
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

	for (i = 0; i < RF_PATH_NUM_8852C; i++) {
		val = __thm_setting(info->thermal_trim[i]);
		rtw89_write_rf(rtwdev, i, RR_TM2, RR_TM2_OFF, val);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[THERMAL][TRIM] path=%d thermal_setting=0x%x\n",
			    i, val);
	}
#undef __thm_setting
}

static void rtw8852c_phycap_parsing_pa_bias_trim(struct rtw89_dev *rtwdev,
						 u8 *phycap_map)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	static const u32 pabias_trim_addr[RF_PATH_NUM_8852C] = {0x5DE, 0x5DB};
	u32 addr = rtwdev->chip->phycap_addr;
	u8 i;

	for (i = 0; i < RF_PATH_NUM_8852C; i++) {
		info->pa_bias_trim[i] = phycap_map[pabias_trim_addr[i] - addr];

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PA_BIAS][TRIM] path=%d pa_bias_trim=0x%x\n",
			    i, info->pa_bias_trim[i]);

		if (info->pa_bias_trim[i] != 0xff)
			info->pg_pa_bias_trim = true;
	}
}

static void rtw8852c_pa_bias_trim(struct rtw89_dev *rtwdev)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	u8 pabias_2g, pabias_5g;
	u8 i;

	if (!info->pg_pa_bias_trim) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PA_BIAS][TRIM] no PG, do nothing\n");

		return;
	}

	for (i = 0; i < RF_PATH_NUM_8852C; i++) {
		pabias_2g = FIELD_GET(GENMASK(3, 0), info->pa_bias_trim[i]);
		pabias_5g = FIELD_GET(GENMASK(7, 4), info->pa_bias_trim[i]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PA_BIAS][TRIM] path=%d 2G=0x%x 5G=0x%x\n",
			    i, pabias_2g, pabias_5g);

		rtw89_write_rf(rtwdev, i, RR_BIASA, RR_BIASA_TXG, pabias_2g);
		rtw89_write_rf(rtwdev, i, RR_BIASA, RR_BIASA_TXA, pabias_5g);
	}
}

static int rtw8852c_read_phycap(struct rtw89_dev *rtwdev, u8 *phycap_map)
{
	rtw8852c_phycap_parsing_tssi(rtwdev, phycap_map);
	rtw8852c_phycap_parsing_thermal_trim(rtwdev, phycap_map);
	rtw8852c_phycap_parsing_pa_bias_trim(rtwdev, phycap_map);

	return 0;
}

static void rtw8852c_power_trim(struct rtw89_dev *rtwdev)
{
	rtw8852c_thermal_trim(rtwdev);
	rtw8852c_pa_bias_trim(rtwdev);
}

static void rtw8852c_set_channel_mac(struct rtw89_dev *rtwdev,
				     const struct rtw89_chan *chan,
				     u8 mac_idx)
{
	u32 rf_mod = rtw89_mac_reg_by_idx(rtwdev, R_AX_WMAC_RFMOD, mac_idx);
	u32 sub_carr = rtw89_mac_reg_by_idx(rtwdev, R_AX_TX_SUB_CARRIER_VALUE, mac_idx);
	u32 chk_rate = rtw89_mac_reg_by_idx(rtwdev, R_AX_TXRATE_CHK, mac_idx);
	u8 txsc20 = 0, txsc40 = 0, txsc80 = 0;
	u8 rf_mod_val = 0, chk_rate_mask = 0;
	u32 txsc;

	switch (chan->band_width) {
	case RTW89_CHANNEL_WIDTH_160:
		txsc80 = rtw89_phy_get_txsc(rtwdev, chan,
					    RTW89_CHANNEL_WIDTH_80);
		fallthrough;
	case RTW89_CHANNEL_WIDTH_80:
		txsc40 = rtw89_phy_get_txsc(rtwdev, chan,
					    RTW89_CHANNEL_WIDTH_40);
		fallthrough;
	case RTW89_CHANNEL_WIDTH_40:
		txsc20 = rtw89_phy_get_txsc(rtwdev, chan,
					    RTW89_CHANNEL_WIDTH_20);
		break;
	default:
		break;
	}

	switch (chan->band_width) {
	case RTW89_CHANNEL_WIDTH_160:
		rf_mod_val = AX_WMAC_RFMOD_160M;
		txsc = FIELD_PREP(B_AX_TXSC_20M_MASK, txsc20) |
		       FIELD_PREP(B_AX_TXSC_40M_MASK, txsc40) |
		       FIELD_PREP(B_AX_TXSC_80M_MASK, txsc80);
		break;
	case RTW89_CHANNEL_WIDTH_80:
		rf_mod_val = AX_WMAC_RFMOD_80M;
		txsc = FIELD_PREP(B_AX_TXSC_20M_MASK, txsc20) |
		       FIELD_PREP(B_AX_TXSC_40M_MASK, txsc40);
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rf_mod_val = AX_WMAC_RFMOD_40M;
		txsc = FIELD_PREP(B_AX_TXSC_20M_MASK, txsc20);
		break;
	case RTW89_CHANNEL_WIDTH_20:
	default:
		rf_mod_val = AX_WMAC_RFMOD_20M;
		txsc = 0;
		break;
	}
	rtw89_write8_mask(rtwdev, rf_mod, B_AX_WMAC_RFMOD_MASK, rf_mod_val);
	rtw89_write32(rtwdev, sub_carr, txsc);

	switch (chan->band_type) {
	case RTW89_BAND_2G:
		chk_rate_mask = B_AX_BAND_MODE;
		break;
	case RTW89_BAND_5G:
	case RTW89_BAND_6G:
		chk_rate_mask = B_AX_CHECK_CCK_EN | B_AX_RTS_LIMIT_IN_OFDM6;
		break;
	default:
		rtw89_warn(rtwdev, "Invalid band_type:%d\n", chan->band_type);
		return;
	}
	rtw89_write8_clr(rtwdev, chk_rate, B_AX_BAND_MODE | B_AX_CHECK_CCK_EN |
					   B_AX_RTS_LIMIT_IN_OFDM6);
	rtw89_write8_set(rtwdev, chk_rate, chk_rate_mask);
}

static const u32 rtw8852c_sco_barker_threshold[14] = {
	0x1fe4f, 0x1ff5e, 0x2006c, 0x2017b, 0x2028a, 0x20399, 0x204a8, 0x205b6,
	0x206c5, 0x207d4, 0x208e3, 0x209f2, 0x20b00, 0x20d8a
};

static const u32 rtw8852c_sco_cck_threshold[14] = {
	0x2bdac, 0x2bf21, 0x2c095, 0x2c209, 0x2c37e, 0x2c4f2, 0x2c666, 0x2c7db,
	0x2c94f, 0x2cac3, 0x2cc38, 0x2cdac, 0x2cf21, 0x2d29e
};

static int rtw8852c_ctrl_sco_cck(struct rtw89_dev *rtwdev, u8 central_ch,
				 u8 primary_ch, enum rtw89_bandwidth bw)
{
	u8 ch_element;

	if (bw == RTW89_CHANNEL_WIDTH_20) {
		ch_element = central_ch - 1;
	} else if (bw == RTW89_CHANNEL_WIDTH_40) {
		if (primary_ch == 1)
			ch_element = central_ch - 1 + 2;
		else
			ch_element = central_ch - 1 - 2;
	} else {
		rtw89_warn(rtwdev, "Invalid BW:%d for CCK\n", bw);
		return -EINVAL;
	}
	rtw89_phy_write32_mask(rtwdev, R_BK_FC0_INV_V1, B_BK_FC0_INV_MSK_V1,
			       rtw8852c_sco_barker_threshold[ch_element]);
	rtw89_phy_write32_mask(rtwdev, R_CCK_FC0_INV_V1, B_CCK_FC0_INV_MSK_V1,
			       rtw8852c_sco_cck_threshold[ch_element]);

	return 0;
}

struct rtw8852c_bb_gain {
	u32 gain_g[BB_PATH_NUM_8852C];
	u32 gain_a[BB_PATH_NUM_8852C];
	u32 gain_mask;
};

static const struct rtw8852c_bb_gain bb_gain_lna[LNA_GAIN_NUM] = {
	{ .gain_g = {0x4678, 0x475C}, .gain_a = {0x45DC, 0x4740},
	  .gain_mask = 0x00ff0000 },
	{ .gain_g = {0x4678, 0x475C}, .gain_a = {0x45DC, 0x4740},
	  .gain_mask = 0xff000000 },
	{ .gain_g = {0x467C, 0x4760}, .gain_a = {0x4660, 0x4744},
	  .gain_mask = 0x000000ff },
	{ .gain_g = {0x467C, 0x4760}, .gain_a = {0x4660, 0x4744},
	  .gain_mask = 0x0000ff00 },
	{ .gain_g = {0x467C, 0x4760}, .gain_a = {0x4660, 0x4744},
	  .gain_mask = 0x00ff0000 },
	{ .gain_g = {0x467C, 0x4760}, .gain_a = {0x4660, 0x4744},
	  .gain_mask = 0xff000000 },
	{ .gain_g = {0x4680, 0x4764}, .gain_a = {0x4664, 0x4748},
	  .gain_mask = 0x000000ff },
};

static const struct rtw8852c_bb_gain bb_gain_tia[TIA_GAIN_NUM] = {
	{ .gain_g = {0x4680, 0x4764}, .gain_a = {0x4664, 0x4748},
	  .gain_mask = 0x00ff0000 },
	{ .gain_g = {0x4680, 0x4764}, .gain_a = {0x4664, 0x4748},
	  .gain_mask = 0xff000000 },
};

struct rtw8852c_bb_gain_bypass {
	u32 gain_g[BB_PATH_NUM_8852C];
	u32 gain_a[BB_PATH_NUM_8852C];
	u32 gain_mask_g;
	u32 gain_mask_a;
};

static
const struct rtw8852c_bb_gain_bypass bb_gain_bypass_lna[LNA_GAIN_NUM] = {
	{ .gain_g = {0x4BB8, 0x4C7C}, .gain_a = {0x4BB4, 0x4C78},
	  .gain_mask_g = 0xff000000, .gain_mask_a = 0xff},
	{ .gain_g = {0x4BBC, 0x4C80}, .gain_a = {0x4BB4, 0x4C78},
	  .gain_mask_g = 0xff, .gain_mask_a = 0xff00},
	{ .gain_g = {0x4BBC, 0x4C80}, .gain_a = {0x4BB4, 0x4C78},
	  .gain_mask_g = 0xff00, .gain_mask_a = 0xff0000},
	{ .gain_g = {0x4BBC, 0x4C80}, .gain_a = {0x4BB4, 0x4C78},
	  .gain_mask_g = 0xff0000, .gain_mask_a = 0xff000000},
	{ .gain_g = {0x4BBC, 0x4C80}, .gain_a = {0x4BB8, 0x4C7C},
	  .gain_mask_g = 0xff000000, .gain_mask_a = 0xff},
	{ .gain_g = {0x4BC0, 0x4C84}, .gain_a = {0x4BB8, 0x4C7C},
	  .gain_mask_g = 0xff, .gain_mask_a = 0xff00},
	{ .gain_g = {0x4BC0, 0x4C84}, .gain_a = {0x4BB8, 0x4C7C},
	  .gain_mask_g = 0xff00, .gain_mask_a = 0xff0000},
};

struct rtw8852c_bb_gain_op1db {
	struct {
		u32 lna[BB_PATH_NUM_8852C];
		u32 tia_lna[BB_PATH_NUM_8852C];
		u32 mask;
	} reg[LNA_GAIN_NUM];
	u32 reg_tia0_lna6[BB_PATH_NUM_8852C];
	u32 mask_tia0_lna6;
};

static const struct rtw8852c_bb_gain_op1db bb_gain_op1db_a = {
	.reg = {
		{ .lna = {0x4668, 0x474c}, .tia_lna = {0x4670, 0x4754},
		  .mask = 0xff},
		{ .lna = {0x4668, 0x474c}, .tia_lna = {0x4670, 0x4754},
		  .mask = 0xff00},
		{ .lna = {0x4668, 0x474c}, .tia_lna = {0x4670, 0x4754},
		  .mask = 0xff0000},
		{ .lna = {0x4668, 0x474c}, .tia_lna = {0x4670, 0x4754},
		  .mask = 0xff000000},
		{ .lna = {0x466c, 0x4750}, .tia_lna = {0x4674, 0x4758},
		  .mask = 0xff},
		{ .lna = {0x466c, 0x4750}, .tia_lna = {0x4674, 0x4758},
		  .mask = 0xff00},
		{ .lna = {0x466c, 0x4750}, .tia_lna = {0x4674, 0x4758},
		  .mask = 0xff0000},
	},
	.reg_tia0_lna6 = {0x4674, 0x4758},
	.mask_tia0_lna6 = 0xff000000,
};

static void rtw8852c_set_gain_error(struct rtw89_dev *rtwdev,
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

		if (subband == RTW89_CH_2G) {
			reg = bb_gain_bypass_lna[i].gain_g[path];
			mask = bb_gain_bypass_lna[i].gain_mask_g;
		} else {
			reg = bb_gain_bypass_lna[i].gain_a[path];
			mask = bb_gain_bypass_lna[i].gain_mask_a;
		}

		val = gain->lna_gain_bypass[gain_band][path][i];
		rtw89_phy_write32_mask(rtwdev, reg, mask, val);

		if (subband != RTW89_CH_2G) {
			reg = bb_gain_op1db_a.reg[i].lna[path];
			mask = bb_gain_op1db_a.reg[i].mask;
			val = gain->lna_op1db[gain_band][path][i];
			rtw89_phy_write32_mask(rtwdev, reg, mask, val);

			reg = bb_gain_op1db_a.reg[i].tia_lna[path];
			mask = bb_gain_op1db_a.reg[i].mask;
			val = gain->tia_lna_op1db[gain_band][path][i];
			rtw89_phy_write32_mask(rtwdev, reg, mask, val);
		}
	}

	if (subband != RTW89_CH_2G) {
		reg = bb_gain_op1db_a.reg_tia0_lna6[path];
		mask = bb_gain_op1db_a.mask_tia0_lna6;
		val = gain->tia_lna_op1db[gain_band][path][7];
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

static void rtw8852c_set_gain_offset(struct rtw89_dev *rtwdev,
				     const struct rtw89_chan *chan,
				     enum rtw89_phy_idx phy_idx,
				     enum rtw89_rf_path path)
{
	static const u32 rssi_ofst_addr[2] = {R_PATH0_G_TIA0_LNA6_OP1DB_V1,
					      R_PATH1_G_TIA0_LNA6_OP1DB_V1};
	static const u32 rpl_mask[2] = {B_RPL_PATHA_MASK, B_RPL_PATHB_MASK};
	static const u32 rpl_tb_mask[2] = {B_RSSI_M_PATHA_MASK, B_RSSI_M_PATHB_MASK};
	struct rtw89_phy_efuse_gain *efuse_gain = &rtwdev->efuse_gain;
	enum rtw89_gain_offset gain_band;
	s32 offset_q0, offset_base_q4;
	s32 tmp = 0;

	if (!efuse_gain->offset_valid)
		return;

	if (rtwdev->dbcc_en && path == RF_PATH_B)
		phy_idx = RTW89_PHY_1;

	if (chan->band_type == RTW89_BAND_2G) {
		offset_q0 = efuse_gain->offset[path][RTW89_GAIN_OFFSET_2G_CCK];
		offset_base_q4 = efuse_gain->offset_base[phy_idx];

		tmp = clamp_t(s32, (-offset_q0 << 3) + (offset_base_q4 >> 1),
			      S8_MIN >> 1, S8_MAX >> 1);
		rtw89_phy_write32_mask(rtwdev, R_RPL_OFST, B_RPL_OFST_MASK, tmp & 0x7f);
	}

	gain_band = rtw89_subband_to_gain_offset_band_of_ofdm(chan->subband_type);

	offset_q0 = -efuse_gain->offset[path][gain_band];
	offset_base_q4 = efuse_gain->offset_base[phy_idx];

	tmp = (offset_q0 << 2) + (offset_base_q4 >> 2);
	tmp = clamp_t(s32, -tmp, S8_MIN, S8_MAX);
	rtw89_phy_write32_mask(rtwdev, rssi_ofst_addr[path], B_PATH0_R_G_OFST_MASK, tmp & 0xff);

	tmp = clamp_t(s32, offset_q0 << 4, S8_MIN, S8_MAX);
	rtw89_phy_write32_idx(rtwdev, R_RPL_PATHAB, rpl_mask[path], tmp & 0xff, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_RSSI_M_PATHAB, rpl_tb_mask[path], tmp & 0xff, phy_idx);
}

static void rtw8852c_ctrl_ch(struct rtw89_dev *rtwdev,
			     const struct rtw89_chan *chan,
			     enum rtw89_phy_idx phy_idx)
{
	u8 sco;
	u16 central_freq = chan->freq;
	u8 central_ch = chan->channel;
	u8 band = chan->band_type;
	u8 subband = chan->subband_type;
	bool is_2g = band == RTW89_BAND_2G;
	u8 chan_idx;

	if (!central_freq) {
		rtw89_warn(rtwdev, "Invalid central_freq\n");
		return;
	}

	if (phy_idx == RTW89_PHY_0) {
		/* Path A */
		rtw8852c_set_gain_error(rtwdev, subband, RF_PATH_A);
		rtw8852c_set_gain_offset(rtwdev, chan, phy_idx, RF_PATH_A);

		if (is_2g)
			rtw89_phy_write32_idx(rtwdev, R_PATH0_BAND_SEL_V1,
					      B_PATH0_BAND_SEL_MSK_V1, 1,
					      phy_idx);
		else
			rtw89_phy_write32_idx(rtwdev, R_PATH0_BAND_SEL_V1,
					      B_PATH0_BAND_SEL_MSK_V1, 0,
					      phy_idx);
		/* Path B */
		if (!rtwdev->dbcc_en) {
			rtw8852c_set_gain_error(rtwdev, subband, RF_PATH_B);
			rtw8852c_set_gain_offset(rtwdev, chan, phy_idx, RF_PATH_B);

			if (is_2g)
				rtw89_phy_write32_idx(rtwdev,
						      R_PATH1_BAND_SEL_V1,
						      B_PATH1_BAND_SEL_MSK_V1,
						      1, phy_idx);
			else
				rtw89_phy_write32_idx(rtwdev,
						      R_PATH1_BAND_SEL_V1,
						      B_PATH1_BAND_SEL_MSK_V1,
						      0, phy_idx);
			rtw89_phy_write32_clr(rtwdev, R_2P4G_BAND, B_2P4G_BAND_SEL);
		} else {
			if (is_2g)
				rtw89_phy_write32_clr(rtwdev, R_2P4G_BAND, B_2P4G_BAND_SEL);
			else
				rtw89_phy_write32_set(rtwdev, R_2P4G_BAND, B_2P4G_BAND_SEL);
		}
		/* SCO compensate FC setting */
		rtw89_phy_write32_idx(rtwdev, R_FC0_V1, B_FC0_MSK_V1,
				      central_freq, phy_idx);
		/* round_up((1/fc0)*pow(2,18)) */
		sco = DIV_ROUND_CLOSEST(1 << 18, central_freq);
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW, B_FC0_BW_INV, sco,
				      phy_idx);
	} else {
		/* Path B */
		rtw8852c_set_gain_error(rtwdev, subband, RF_PATH_B);
		rtw8852c_set_gain_offset(rtwdev, chan, phy_idx, RF_PATH_B);

		if (is_2g)
			rtw89_phy_write32_idx(rtwdev, R_PATH1_BAND_SEL_V1,
					      B_PATH1_BAND_SEL_MSK_V1,
					      1, phy_idx);
		else
			rtw89_phy_write32_idx(rtwdev, R_PATH1_BAND_SEL_V1,
					      B_PATH1_BAND_SEL_MSK_V1,
					      0, phy_idx);
		/* SCO compensate FC setting */
		rtw89_phy_write32_idx(rtwdev, R_FC0_V1, B_FC0_MSK_V1,
				      central_freq, phy_idx);
		/* round_up((1/fc0)*pow(2,18)) */
		sco = DIV_ROUND_CLOSEST(1 << 18, central_freq);
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW, B_FC0_BW_INV, sco,
				      phy_idx);
	}
	/* CCK parameters */
	if (band == RTW89_BAND_2G) {
		if (central_ch == 14) {
			rtw89_phy_write32_mask(rtwdev, R_PCOEFF0_V1,
					       B_PCOEFF01_MSK_V1, 0x3b13ff);
			rtw89_phy_write32_mask(rtwdev, R_PCOEFF2_V1,
					       B_PCOEFF23_MSK_V1, 0x1c42de);
			rtw89_phy_write32_mask(rtwdev, R_PCOEFF4_V1,
					       B_PCOEFF45_MSK_V1, 0xfdb0ad);
			rtw89_phy_write32_mask(rtwdev, R_PCOEFF6_V1,
					       B_PCOEFF67_MSK_V1, 0xf60f6e);
			rtw89_phy_write32_mask(rtwdev, R_PCOEFF8_V1,
					       B_PCOEFF89_MSK_V1, 0xfd8f92);
			rtw89_phy_write32_mask(rtwdev, R_PCOEFFA_V1,
					       B_PCOEFFAB_MSK_V1, 0x2d011);
			rtw89_phy_write32_mask(rtwdev, R_PCOEFFC_V1,
					       B_PCOEFFCD_MSK_V1, 0x1c02c);
			rtw89_phy_write32_mask(rtwdev, R_PCOEFFE_V1,
					       B_PCOEFFEF_MSK_V1, 0xfff00a);
		} else {
			rtw89_phy_write32_mask(rtwdev, R_PCOEFF0_V1,
					       B_PCOEFF01_MSK_V1, 0x3d23ff);
			rtw89_phy_write32_mask(rtwdev, R_PCOEFF2_V1,
					       B_PCOEFF23_MSK_V1, 0x29b354);
			rtw89_phy_write32_mask(rtwdev, R_PCOEFF4_V1,
					       B_PCOEFF45_MSK_V1, 0xfc1c8);
			rtw89_phy_write32_mask(rtwdev, R_PCOEFF6_V1,
					       B_PCOEFF67_MSK_V1, 0xfdb053);
			rtw89_phy_write32_mask(rtwdev, R_PCOEFF8_V1,
					       B_PCOEFF89_MSK_V1, 0xf86f9a);
			rtw89_phy_write32_mask(rtwdev, R_PCOEFFA_V1,
					       B_PCOEFFAB_MSK_V1, 0xfaef92);
			rtw89_phy_write32_mask(rtwdev, R_PCOEFFC_V1,
					       B_PCOEFFCD_MSK_V1, 0xfe5fcc);
			rtw89_phy_write32_mask(rtwdev, R_PCOEFFE_V1,
					       B_PCOEFFEF_MSK_V1, 0xffdff5);
		}
	}

	chan_idx = rtw89_encode_chan_idx(rtwdev, chan->primary_channel, band);
	rtw89_phy_write32_idx(rtwdev, R_MAC_PIN_SEL, B_CH_IDX_SEG0, chan_idx, phy_idx);
}

static void rtw8852c_bw_setting(struct rtw89_dev *rtwdev, u8 bw, u8 path)
{
	static const u32 adc_sel[2] = {0xC0EC, 0xC1EC};
	static const u32 wbadc_sel[2] = {0xC0E4, 0xC1E4};

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_5:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x1);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x0);
		break;
	case RTW89_CHANNEL_WIDTH_10:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x2);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x1);
		break;
	case RTW89_CHANNEL_WIDTH_20:
	case RTW89_CHANNEL_WIDTH_40:
	case RTW89_CHANNEL_WIDTH_80:
	case RTW89_CHANNEL_WIDTH_160:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x0);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x2);
		break;
	default:
		rtw89_warn(rtwdev, "Fail to set ADC\n");
	}
}

static void rtw8852c_edcca_per20_bitmap_sifs(struct rtw89_dev *rtwdev, u8 bw,
					     enum rtw89_phy_idx phy_idx)
{
	if (bw == RTW89_CHANNEL_WIDTH_20) {
		rtw89_phy_write32_idx(rtwdev, R_SNDCCA_A1, B_SNDCCA_A1_EN, 0xff, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_SNDCCA_A2, B_SNDCCA_A2_VAL, 0, phy_idx);
	} else {
		rtw89_phy_write32_idx(rtwdev, R_SNDCCA_A1, B_SNDCCA_A1_EN, 0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_SNDCCA_A2, B_SNDCCA_A2_VAL, 0, phy_idx);
	}
}

static void
rtw8852c_ctrl_bw(struct rtw89_dev *rtwdev, u8 pri_ch, u8 bw,
		 enum rtw89_phy_idx phy_idx)
{
	u8 mod_sbw = 0;

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_5:
	case RTW89_CHANNEL_WIDTH_10:
	case RTW89_CHANNEL_WIDTH_20:
		if (bw == RTW89_CHANNEL_WIDTH_5)
			mod_sbw = 0x1;
		else if (bw == RTW89_CHANNEL_WIDTH_10)
			mod_sbw = 0x2;
		else if (bw == RTW89_CHANNEL_WIDTH_20)
			mod_sbw = 0x0;
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW, B_FC0_BW_SET, 0x0,
				      phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD, B_CHBW_MOD_SBW,
				      mod_sbw, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD, B_CHBW_MOD_PRICH, 0x0,
				      phy_idx);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_SAMPL_DLY_T_V1,
				       B_PATH0_SAMPL_DLY_T_MSK_V1, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_SAMPL_DLY_T_V1,
				       B_PATH1_SAMPL_DLY_T_MSK_V1, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BW_SEL_V1,
				       B_PATH0_BW_SEL_MSK_V1, 0xf);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BW_SEL_V1,
				       B_PATH1_BW_SEL_MSK_V1, 0xf);
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW, B_FC0_BW_SET, 0x1,
				      phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD, B_CHBW_MOD_SBW, 0x0,
				      phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD, B_CHBW_MOD_PRICH,
				      pri_ch,
				      phy_idx);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_SAMPL_DLY_T_V1,
				       B_PATH0_SAMPL_DLY_T_MSK_V1, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_SAMPL_DLY_T_V1,
				       B_PATH1_SAMPL_DLY_T_MSK_V1, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BW_SEL_V1,
				       B_PATH0_BW_SEL_MSK_V1, 0xf);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BW_SEL_V1,
				       B_PATH1_BW_SEL_MSK_V1, 0xf);
		break;
	case RTW89_CHANNEL_WIDTH_80:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW, B_FC0_BW_SET, 0x2,
				      phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD, B_CHBW_MOD_SBW, 0x0,
				      phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD, B_CHBW_MOD_PRICH,
				      pri_ch,
				      phy_idx);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_SAMPL_DLY_T_V1,
				       B_PATH0_SAMPL_DLY_T_MSK_V1, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_SAMPL_DLY_T_V1,
				       B_PATH1_SAMPL_DLY_T_MSK_V1, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BW_SEL_V1,
				       B_PATH0_BW_SEL_MSK_V1, 0xd);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BW_SEL_V1,
				       B_PATH1_BW_SEL_MSK_V1, 0xd);
		break;
	case RTW89_CHANNEL_WIDTH_160:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW, B_FC0_BW_SET, 0x3,
				      phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD, B_CHBW_MOD_SBW, 0x0,
				      phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD, B_CHBW_MOD_PRICH,
				      pri_ch,
				      phy_idx);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_SAMPL_DLY_T_V1,
				       B_PATH0_SAMPL_DLY_T_MSK_V1, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_SAMPL_DLY_T_V1,
				       B_PATH1_SAMPL_DLY_T_MSK_V1, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BW_SEL_V1,
				       B_PATH0_BW_SEL_MSK_V1, 0xb);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BW_SEL_V1,
				       B_PATH1_BW_SEL_MSK_V1, 0xb);
		break;
	default:
		rtw89_warn(rtwdev, "Fail to switch bw (bw:%d, pri ch:%d)\n", bw,
			   pri_ch);
	}

	if (bw == RTW89_CHANNEL_WIDTH_40) {
		rtw89_phy_write32_idx(rtwdev, R_RX_BW40_2XFFT_EN_V1,
				      B_RX_BW40_2XFFT_EN_MSK_V1, 0x1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_T2F_GI_COMB, B_T2F_GI_COMB_EN, 1, phy_idx);
	} else {
		rtw89_phy_write32_idx(rtwdev, R_RX_BW40_2XFFT_EN_V1,
				      B_RX_BW40_2XFFT_EN_MSK_V1, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_T2F_GI_COMB, B_T2F_GI_COMB_EN, 0, phy_idx);
	}

	if (phy_idx == RTW89_PHY_0) {
		rtw8852c_bw_setting(rtwdev, bw, RF_PATH_A);
		if (!rtwdev->dbcc_en)
			rtw8852c_bw_setting(rtwdev, bw, RF_PATH_B);
	} else {
		rtw8852c_bw_setting(rtwdev, bw, RF_PATH_B);
	}

	rtw8852c_edcca_per20_bitmap_sifs(rtwdev, bw, phy_idx);
}

static u32 rtw8852c_spur_freq(struct rtw89_dev *rtwdev,
			      const struct rtw89_chan *chan)
{
	u8 center_chan = chan->channel;
	u8 bw = chan->band_width;

	switch (chan->band_type) {
	case RTW89_BAND_2G:
		if (bw == RTW89_CHANNEL_WIDTH_20) {
			if (center_chan >= 5 && center_chan <= 8)
				return 2440;
			if (center_chan == 13)
				return 2480;
		} else if (bw == RTW89_CHANNEL_WIDTH_40) {
			if (center_chan >= 3 && center_chan <= 10)
				return 2440;
		}
		break;
	case RTW89_BAND_5G:
		if (center_chan == 151 || center_chan == 153 ||
		    center_chan == 155 || center_chan == 163)
			return 5760;
		break;
	case RTW89_BAND_6G:
		if (center_chan == 195 || center_chan == 197 ||
		    center_chan == 199 || center_chan == 207)
			return 6920;
		break;
	default:
		break;
	}

	return 0;
}

#define CARRIER_SPACING_312_5 312500 /* 312.5 kHz */
#define CARRIER_SPACING_78_125 78125 /* 78.125 kHz */
#define MAX_TONE_NUM 2048

static void rtw8852c_set_csi_tone_idx(struct rtw89_dev *rtwdev,
				      const struct rtw89_chan *chan,
				      enum rtw89_phy_idx phy_idx)
{
	u32 spur_freq;
	s32 freq_diff, csi_idx, csi_tone_idx;

	spur_freq = rtw8852c_spur_freq(rtwdev, chan);
	if (spur_freq == 0) {
		rtw89_phy_write32_idx(rtwdev, R_SEG0CSI_EN, B_SEG0CSI_EN, 0, phy_idx);
		return;
	}

	freq_diff = (spur_freq - chan->freq) * 1000000;
	csi_idx = s32_div_u32_round_closest(freq_diff, CARRIER_SPACING_78_125);
	s32_div_u32_round_down(csi_idx, MAX_TONE_NUM, &csi_tone_idx);

	rtw89_phy_write32_idx(rtwdev, R_SEG0CSI, B_SEG0CSI_IDX, csi_tone_idx, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_SEG0CSI_EN, B_SEG0CSI_EN, 1, phy_idx);
}

static const struct rtw89_nbi_reg_def rtw8852c_nbi_reg_def[] = {
	[RF_PATH_A] = {
		.notch1_idx = {0x4C14, 0xFF},
		.notch1_frac_idx = {0x4C14, 0xC00},
		.notch1_en = {0x4C14, 0x1000},
		.notch2_idx = {0x4C20, 0xFF},
		.notch2_frac_idx = {0x4C20, 0xC00},
		.notch2_en = {0x4C20, 0x1000},
	},
	[RF_PATH_B] = {
		.notch1_idx = {0x4CD8, 0xFF},
		.notch1_frac_idx = {0x4CD8, 0xC00},
		.notch1_en = {0x4CD8, 0x1000},
		.notch2_idx = {0x4CE4, 0xFF},
		.notch2_frac_idx = {0x4CE4, 0xC00},
		.notch2_en = {0x4CE4, 0x1000},
	},
};

static void rtw8852c_set_nbi_tone_idx(struct rtw89_dev *rtwdev,
				      const struct rtw89_chan *chan,
				      enum rtw89_rf_path path)
{
	const struct rtw89_nbi_reg_def *nbi = &rtw8852c_nbi_reg_def[path];
	u32 spur_freq, fc;
	s32 freq_diff;
	s32 nbi_idx, nbi_tone_idx;
	s32 nbi_frac_idx, nbi_frac_tone_idx;
	bool notch2_chk = false;

	spur_freq = rtw8852c_spur_freq(rtwdev, chan);
	if (spur_freq == 0) {
		rtw89_phy_write32_mask(rtwdev, nbi->notch1_en.addr, nbi->notch1_en.mask, 0);
		rtw89_phy_write32_mask(rtwdev, nbi->notch1_en.addr, nbi->notch1_en.mask, 0);
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
	nbi_idx = s32_div_u32_round_down(freq_diff, CARRIER_SPACING_312_5, &nbi_frac_idx);

	if (chan->band_width == RTW89_CHANNEL_WIDTH_20) {
		s32_div_u32_round_down(nbi_idx + 32, 64, &nbi_tone_idx);
	} else {
		u16 tone_para = (chan->band_width == RTW89_CHANNEL_WIDTH_40) ?
				128 : 256;

		s32_div_u32_round_down(nbi_idx, tone_para, &nbi_tone_idx);
	}
	nbi_frac_tone_idx = s32_div_u32_round_closest(nbi_frac_idx, CARRIER_SPACING_78_125);

	if (chan->band_width == RTW89_CHANNEL_WIDTH_160 && notch2_chk) {
		rtw89_phy_write32_mask(rtwdev, nbi->notch2_idx.addr,
				       nbi->notch2_idx.mask, nbi_tone_idx);
		rtw89_phy_write32_mask(rtwdev, nbi->notch2_frac_idx.addr,
				       nbi->notch2_frac_idx.mask, nbi_frac_tone_idx);
		rtw89_phy_write32_mask(rtwdev, nbi->notch2_en.addr, nbi->notch2_en.mask, 0);
		rtw89_phy_write32_mask(rtwdev, nbi->notch2_en.addr, nbi->notch2_en.mask, 1);
		rtw89_phy_write32_mask(rtwdev, nbi->notch1_en.addr, nbi->notch1_en.mask, 0);
	} else {
		rtw89_phy_write32_mask(rtwdev, nbi->notch1_idx.addr,
				       nbi->notch1_idx.mask, nbi_tone_idx);
		rtw89_phy_write32_mask(rtwdev, nbi->notch1_frac_idx.addr,
				       nbi->notch1_frac_idx.mask, nbi_frac_tone_idx);
		rtw89_phy_write32_mask(rtwdev, nbi->notch1_en.addr, nbi->notch1_en.mask, 0);
		rtw89_phy_write32_mask(rtwdev, nbi->notch1_en.addr, nbi->notch1_en.mask, 1);
		rtw89_phy_write32_mask(rtwdev, nbi->notch2_en.addr, nbi->notch2_en.mask, 0);
	}
}

static void rtw8852c_spur_notch(struct rtw89_dev *rtwdev, u32 val,
				enum rtw89_phy_idx phy_idx)
{
	u32 notch;
	u32 notch2;

	if (phy_idx == RTW89_PHY_0) {
		notch = R_PATH0_NOTCH;
		notch2 = R_PATH0_NOTCH2;
	} else {
		notch = R_PATH1_NOTCH;
		notch2 = R_PATH1_NOTCH2;
	}

	rtw89_phy_write32_mask(rtwdev, notch,
			       B_PATH0_NOTCH_VAL | B_PATH0_NOTCH_EN, val);
	rtw89_phy_write32_set(rtwdev, notch, B_PATH0_NOTCH_EN);
	rtw89_phy_write32_mask(rtwdev, notch2,
			       B_PATH0_NOTCH2_VAL | B_PATH0_NOTCH2_EN, val);
	rtw89_phy_write32_set(rtwdev, notch2, B_PATH0_NOTCH2_EN);
}

static void rtw8852c_spur_elimination(struct rtw89_dev *rtwdev,
				      const struct rtw89_chan *chan,
				      u8 pri_ch_idx,
				      enum rtw89_phy_idx phy_idx)
{
	rtw8852c_set_csi_tone_idx(rtwdev, chan, phy_idx);

	if (phy_idx == RTW89_PHY_0) {
		if (chan->band_width == RTW89_CHANNEL_WIDTH_160 &&
		    (pri_ch_idx == RTW89_SC_20_LOWER ||
		     pri_ch_idx == RTW89_SC_20_UP3X)) {
			rtw8852c_spur_notch(rtwdev, 0xe7f, RTW89_PHY_0);
			if (!rtwdev->dbcc_en)
				rtw8852c_spur_notch(rtwdev, 0xe7f, RTW89_PHY_1);
		} else if (chan->band_width == RTW89_CHANNEL_WIDTH_160 &&
			   (pri_ch_idx == RTW89_SC_20_UPPER ||
			    pri_ch_idx == RTW89_SC_20_LOW3X)) {
			rtw8852c_spur_notch(rtwdev, 0x280, RTW89_PHY_0);
			if (!rtwdev->dbcc_en)
				rtw8852c_spur_notch(rtwdev, 0x280, RTW89_PHY_1);
		} else {
			rtw8852c_set_nbi_tone_idx(rtwdev, chan, RF_PATH_A);
			if (!rtwdev->dbcc_en)
				rtw8852c_set_nbi_tone_idx(rtwdev, chan,
							  RF_PATH_B);
		}
	} else {
		if (chan->band_width == RTW89_CHANNEL_WIDTH_160 &&
		    (pri_ch_idx == RTW89_SC_20_LOWER ||
		     pri_ch_idx == RTW89_SC_20_UP3X)) {
			rtw8852c_spur_notch(rtwdev, 0xe7f, RTW89_PHY_1);
		} else if (chan->band_width == RTW89_CHANNEL_WIDTH_160 &&
			   (pri_ch_idx == RTW89_SC_20_UPPER ||
			    pri_ch_idx == RTW89_SC_20_LOW3X)) {
			rtw8852c_spur_notch(rtwdev, 0x280, RTW89_PHY_1);
		} else {
			rtw8852c_set_nbi_tone_idx(rtwdev, chan, RF_PATH_B);
		}
	}

	if (pri_ch_idx == RTW89_SC_20_UP3X || pri_ch_idx == RTW89_SC_20_LOW3X)
		rtw89_phy_write32_idx(rtwdev, R_PD_BOOST_EN, B_PD_BOOST_EN, 0, phy_idx);
	else
		rtw89_phy_write32_idx(rtwdev, R_PD_BOOST_EN, B_PD_BOOST_EN, 1, phy_idx);
}

static void rtw8852c_5m_mask(struct rtw89_dev *rtwdev,
			     const struct rtw89_chan *chan,
			     enum rtw89_phy_idx phy_idx)
{
	u8 pri_ch = chan->pri_ch_idx;
	bool mask_5m_low;
	bool mask_5m_en;

	switch (chan->band_width) {
	case RTW89_CHANNEL_WIDTH_40:
		mask_5m_en = true;
		mask_5m_low = pri_ch == RTW89_SC_20_LOWER;
		break;
	case RTW89_CHANNEL_WIDTH_80:
		mask_5m_en = pri_ch == RTW89_SC_20_UPMOST ||
			     pri_ch == RTW89_SC_20_LOWEST;
		mask_5m_low = pri_ch == RTW89_SC_20_LOWEST;
		break;
	default:
		mask_5m_en = false;
		mask_5m_low = false;
		break;
	}

	if (!mask_5m_en) {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET, B_PATH0_5MDET_EN, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET, B_PATH1_5MDET_EN, 0x0);
		rtw89_phy_write32_idx(rtwdev, R_ASSIGN_SBD_OPT,
				      B_ASSIGN_SBD_OPT_EN, 0x0, phy_idx);
	} else {
		if (mask_5m_low) {
			rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET, B_PATH0_5MDET_TH, 0x4);
			rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET, B_PATH0_5MDET_EN, 0x1);
			rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET, B_PATH0_5MDET_SB2, 0x0);
			rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET, B_PATH0_5MDET_SB0, 0x1);
			rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET, B_PATH1_5MDET_TH, 0x4);
			rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET, B_PATH1_5MDET_EN, 0x1);
			rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET, B_PATH1_5MDET_SB2, 0x0);
			rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET, B_PATH1_5MDET_SB0, 0x1);
		} else {
			rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET, B_PATH0_5MDET_TH, 0x4);
			rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET, B_PATH0_5MDET_EN, 0x1);
			rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET, B_PATH0_5MDET_SB2, 0x1);
			rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET, B_PATH0_5MDET_SB0, 0x0);
			rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET, B_PATH1_5MDET_TH, 0x4);
			rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET, B_PATH1_5MDET_EN, 0x1);
			rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET, B_PATH1_5MDET_SB2, 0x1);
			rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET, B_PATH1_5MDET_SB0, 0x0);
		}
		rtw89_phy_write32_idx(rtwdev, R_ASSIGN_SBD_OPT, B_ASSIGN_SBD_OPT_EN, 0x1, phy_idx);
	}
}

static void rtw8852c_bb_reset_all(struct rtw89_dev *rtwdev,
				  enum rtw89_phy_idx phy_idx)
{
	/*HW SI reset*/
	rtw89_phy_write32_mask(rtwdev, R_S0_HW_SI_DIS, B_S0_HW_SI_DIS_W_R_TRIG,
			       0x7);
	rtw89_phy_write32_mask(rtwdev, R_S1_HW_SI_DIS, B_S1_HW_SI_DIS_W_R_TRIG,
			       0x7);

	udelay(1);

	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1,
			      phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 0,
			      phy_idx);
	/*HW SI reset*/
	rtw89_phy_write32_mask(rtwdev, R_S0_HW_SI_DIS, B_S0_HW_SI_DIS_W_R_TRIG,
			       0x0);
	rtw89_phy_write32_mask(rtwdev, R_S1_HW_SI_DIS, B_S1_HW_SI_DIS_W_R_TRIG,
			       0x0);

	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1,
			      phy_idx);
}

static void rtw8852c_bb_reset_en(struct rtw89_dev *rtwdev, enum rtw89_band band,
				 enum rtw89_phy_idx phy_idx, bool en)
{
	if (en) {
		rtw89_phy_write32_idx(rtwdev, R_S0_HW_SI_DIS,
				      B_S0_HW_SI_DIS_W_R_TRIG, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_S1_HW_SI_DIS,
				      B_S1_HW_SI_DIS_W_R_TRIG, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1,
				      phy_idx);
		if (band == RTW89_BAND_2G)
			rtw89_phy_write32_mask(rtwdev, R_RXCCA_V1, B_RXCCA_DIS_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PD_CTRL, B_PD_HIT_DIS, 0x0);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_RXCCA_V1, B_RXCCA_DIS_V1, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PD_CTRL, B_PD_HIT_DIS, 0x1);
		rtw89_phy_write32_idx(rtwdev, R_S0_HW_SI_DIS,
				      B_S0_HW_SI_DIS_W_R_TRIG, 0x7, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_S1_HW_SI_DIS,
				      B_S1_HW_SI_DIS_W_R_TRIG, 0x7, phy_idx);
		fsleep(1);
		rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 0,
				      phy_idx);
	}
}

static void rtw8852c_bb_reset(struct rtw89_dev *rtwdev,
			      enum rtw89_phy_idx phy_idx)
{
	rtw8852c_bb_reset_all(rtwdev, phy_idx);
}

static
void rtw8852c_bb_gpio_trsw(struct rtw89_dev *rtwdev, enum rtw89_rf_path path,
			   u8 tx_path_en, u8 trsw_tx,
			   u8 trsw_rx, u8 trsw, u8 trsw_b)
{
	static const u32 path_cr_bases[] = {0x5868, 0x7868};
	u32 mask_ofst = 16;
	u32 cr;
	u32 val;

	if (path >= ARRAY_SIZE(path_cr_bases))
		return;

	cr = path_cr_bases[path];

	mask_ofst += (tx_path_en * 4 + trsw_tx * 2 + trsw_rx) * 2;
	val = FIELD_PREP(B_P0_TRSW_A, trsw) | FIELD_PREP(B_P0_TRSW_B, trsw_b);

	rtw89_phy_write32_mask(rtwdev, cr, (B_P0_TRSW_A | B_P0_TRSW_B) << mask_ofst, val);
}

enum rtw8852c_rfe_src {
	PAPE_RFM,
	TRSW_RFM,
	LNAON_RFM,
};

static
void rtw8852c_bb_gpio_rfm(struct rtw89_dev *rtwdev, enum rtw89_rf_path path,
			  enum rtw8852c_rfe_src src, u8 dis_tx_gnt_wl,
			  u8 active_tx_opt, u8 act_bt_en, u8 rfm_output_val)
{
	static const u32 path_cr_bases[] = {0x5894, 0x7894};
	static const u32 masks[] = {0, 8, 16};
	u32 mask, mask_ofst;
	u32 cr;
	u32 val;

	if (src >= ARRAY_SIZE(masks) || path >= ARRAY_SIZE(path_cr_bases))
		return;

	mask_ofst = masks[src];
	cr = path_cr_bases[path];

	val = FIELD_PREP(B_P0_RFM_DIS_WL, dis_tx_gnt_wl) |
	      FIELD_PREP(B_P0_RFM_TX_OPT, active_tx_opt) |
	      FIELD_PREP(B_P0_RFM_BT_EN, act_bt_en) |
	      FIELD_PREP(B_P0_RFM_OUT, rfm_output_val);
	mask = 0xff << mask_ofst;

	rtw89_phy_write32_mask(rtwdev, cr, mask, val);
}

static void rtw8852c_bb_gpio_init(struct rtw89_dev *rtwdev)
{
	static const u32 cr_bases[] = {0x5800, 0x7800};
	u32 addr;
	u8 i;

	for (i = 0; i < ARRAY_SIZE(cr_bases); i++) {
		addr = cr_bases[i];
		rtw89_phy_write32_set(rtwdev, (addr | 0x68), B_P0_TRSW_A);
		rtw89_phy_write32_clr(rtwdev, (addr | 0x68), B_P0_TRSW_X);
		rtw89_phy_write32_clr(rtwdev, (addr | 0x68), B_P0_TRSW_SO_A2);
		rtw89_phy_write32(rtwdev, (addr | 0x80), 0x77777777);
		rtw89_phy_write32(rtwdev, (addr | 0x84), 0x77777777);
	}

	rtw89_phy_write32(rtwdev, R_RFE_E_A2, 0xffffffff);
	rtw89_phy_write32(rtwdev, R_RFE_O_SEL_A2, 0);
	rtw89_phy_write32(rtwdev, R_RFE_SEL0_A2, 0);
	rtw89_phy_write32(rtwdev, R_RFE_SEL32_A2, 0);

	rtw8852c_bb_gpio_trsw(rtwdev, RF_PATH_A, 0, 0, 0, 0, 1);
	rtw8852c_bb_gpio_trsw(rtwdev, RF_PATH_A, 0, 0, 1, 1, 0);
	rtw8852c_bb_gpio_trsw(rtwdev, RF_PATH_A, 0, 1, 0, 1, 0);
	rtw8852c_bb_gpio_trsw(rtwdev, RF_PATH_A, 0, 1, 1, 1, 0);
	rtw8852c_bb_gpio_trsw(rtwdev, RF_PATH_A, 1, 0, 0, 0, 1);
	rtw8852c_bb_gpio_trsw(rtwdev, RF_PATH_A, 1, 0, 1, 1, 0);
	rtw8852c_bb_gpio_trsw(rtwdev, RF_PATH_A, 1, 1, 0, 1, 0);
	rtw8852c_bb_gpio_trsw(rtwdev, RF_PATH_A, 1, 1, 1, 1, 0);

	rtw8852c_bb_gpio_trsw(rtwdev, RF_PATH_B, 0, 0, 0, 0, 1);
	rtw8852c_bb_gpio_trsw(rtwdev, RF_PATH_B, 0, 0, 1, 1, 0);
	rtw8852c_bb_gpio_trsw(rtwdev, RF_PATH_B, 0, 1, 0, 1, 0);
	rtw8852c_bb_gpio_trsw(rtwdev, RF_PATH_B, 0, 1, 1, 1, 0);
	rtw8852c_bb_gpio_trsw(rtwdev, RF_PATH_B, 1, 0, 0, 0, 1);
	rtw8852c_bb_gpio_trsw(rtwdev, RF_PATH_B, 1, 0, 1, 1, 0);
	rtw8852c_bb_gpio_trsw(rtwdev, RF_PATH_B, 1, 1, 0, 1, 0);
	rtw8852c_bb_gpio_trsw(rtwdev, RF_PATH_B, 1, 1, 1, 1, 0);

	rtw8852c_bb_gpio_rfm(rtwdev, RF_PATH_A, PAPE_RFM, 0, 0, 0, 0x0);
	rtw8852c_bb_gpio_rfm(rtwdev, RF_PATH_A, TRSW_RFM, 0, 0, 0, 0x4);
	rtw8852c_bb_gpio_rfm(rtwdev, RF_PATH_A, LNAON_RFM, 0, 0, 0, 0x8);

	rtw8852c_bb_gpio_rfm(rtwdev, RF_PATH_B, PAPE_RFM, 0, 0, 0, 0x0);
	rtw8852c_bb_gpio_rfm(rtwdev, RF_PATH_B, TRSW_RFM, 0, 0, 0, 0x4);
	rtw8852c_bb_gpio_rfm(rtwdev, RF_PATH_B, LNAON_RFM, 0, 0, 0, 0x8);
}

static void rtw8852c_bb_macid_ctrl_init(struct rtw89_dev *rtwdev,
					enum rtw89_phy_idx phy_idx)
{
	u32 addr;

	for (addr = R_AX_PWR_MACID_LMT_TABLE0;
	     addr <= R_AX_PWR_MACID_LMT_TABLE127; addr += 4)
		rtw89_mac_txpwr_write32(rtwdev, phy_idx, addr, 0);
}

static void rtw8852c_bb_sethw(struct rtw89_dev *rtwdev)
{
	struct rtw89_phy_efuse_gain *gain = &rtwdev->efuse_gain;

	rtw89_phy_write32_set(rtwdev, R_DBCC_80P80_SEL_EVM_RPT,
			      B_DBCC_80P80_SEL_EVM_RPT_EN);
	rtw89_phy_write32_set(rtwdev, R_DBCC_80P80_SEL_EVM_RPT2,
			      B_DBCC_80P80_SEL_EVM_RPT2_EN);

	rtw8852c_bb_macid_ctrl_init(rtwdev, RTW89_PHY_0);
	rtw8852c_bb_gpio_init(rtwdev);

	/* read these registers after loading BB parameters */
	gain->offset_base[RTW89_PHY_0] =
		rtw89_phy_read32_mask(rtwdev, R_RPL_BIAS_COMP, B_RPL_BIAS_COMP_MASK);
	gain->offset_base[RTW89_PHY_1] =
		rtw89_phy_read32_mask(rtwdev, R_RPL_BIAS_COMP1, B_RPL_BIAS_COMP1_MASK);
}

static void rtw8852c_set_channel_bb(struct rtw89_dev *rtwdev,
				    const struct rtw89_chan *chan,
				    enum rtw89_phy_idx phy_idx)
{
	static const u32 ru_alloc_msk[2] = {B_P80_AT_HIGH_FREQ_RU_ALLOC_PHY0,
					    B_P80_AT_HIGH_FREQ_RU_ALLOC_PHY1};
	struct rtw89_hal *hal = &rtwdev->hal;
	bool cck_en = chan->band_type == RTW89_BAND_2G;
	u8 pri_ch_idx = chan->pri_ch_idx;
	u32 mask, reg;
	u8 ntx_path;

	if (chan->band_type == RTW89_BAND_2G)
		rtw8852c_ctrl_sco_cck(rtwdev, chan->channel,
				      chan->primary_channel,
				      chan->band_width);

	rtw8852c_ctrl_ch(rtwdev, chan, phy_idx);
	rtw8852c_ctrl_bw(rtwdev, pri_ch_idx, chan->band_width, phy_idx);
	if (cck_en) {
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK_ADC, B_ENABLE_CCK, 1);
		rtw89_phy_write32_mask(rtwdev, R_RXCCA_V1, B_RXCCA_DIS_V1, 0);
		rtw89_phy_write32_idx(rtwdev, R_PD_ARBITER_OFF,
				      B_PD_ARBITER_OFF, 0x0, phy_idx);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK_ADC, B_ENABLE_CCK, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXCCA_V1, B_RXCCA_DIS_V1, 1);
		rtw89_phy_write32_idx(rtwdev, R_PD_ARBITER_OFF,
				      B_PD_ARBITER_OFF, 0x1, phy_idx);
	}

	rtw8852c_spur_elimination(rtwdev, chan, pri_ch_idx, phy_idx);
	rtw8852c_ctrl_btg_bt_rx(rtwdev, chan->band_type == RTW89_BAND_2G,
				RTW89_PHY_0);
	rtw8852c_5m_mask(rtwdev, chan, phy_idx);

	if (chan->band_width == RTW89_CHANNEL_WIDTH_160 &&
	    rtwdev->hal.cv != CHIP_CAV) {
		rtw89_phy_write32_idx(rtwdev, R_P80_AT_HIGH_FREQ,
				      B_P80_AT_HIGH_FREQ, 0x0, phy_idx);
		reg = rtw89_mac_reg_by_idx(rtwdev, R_P80_AT_HIGH_FREQ_BB_WRP, phy_idx);
		if (chan->primary_channel > chan->channel) {
			rtw89_phy_write32_mask(rtwdev,
					       R_P80_AT_HIGH_FREQ_RU_ALLOC,
					       ru_alloc_msk[phy_idx], 1);
			rtw89_write32_mask(rtwdev, reg,
					   B_P80_AT_HIGH_FREQ_BB_WRP, 1);
		} else {
			rtw89_phy_write32_mask(rtwdev,
					       R_P80_AT_HIGH_FREQ_RU_ALLOC,
					       ru_alloc_msk[phy_idx], 0);
			rtw89_write32_mask(rtwdev, reg,
					   B_P80_AT_HIGH_FREQ_BB_WRP, 0);
		}
	}

	if (chan->band_type == RTW89_BAND_6G &&
	    chan->band_width == RTW89_CHANNEL_WIDTH_160)
		rtw89_phy_write32_idx(rtwdev, R_CDD_EVM_CHK_EN,
				      B_CDD_EVM_CHK_EN, 0, phy_idx);
	else
		rtw89_phy_write32_idx(rtwdev, R_CDD_EVM_CHK_EN,
				      B_CDD_EVM_CHK_EN, 1, phy_idx);

	if (!rtwdev->dbcc_en) {
		mask = B_P0_TXPW_RSTB_TSSI | B_P0_TXPW_RSTB_MANON;
		rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB, mask, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB, mask, 0x3);
		mask = B_P1_TXPW_RSTB_TSSI | B_P1_TXPW_RSTB_MANON;
		rtw89_phy_write32_mask(rtwdev, R_P1_TXPW_RSTB, mask, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P1_TXPW_RSTB, mask, 0x3);
	} else {
		if (phy_idx == RTW89_PHY_0) {
			mask = B_P0_TXPW_RSTB_TSSI | B_P0_TXPW_RSTB_MANON;
			rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB, mask, 0x1);
			rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB, mask, 0x3);
		} else {
			mask = B_P1_TXPW_RSTB_TSSI | B_P1_TXPW_RSTB_MANON;
			rtw89_phy_write32_mask(rtwdev, R_P1_TXPW_RSTB, mask, 0x1);
			rtw89_phy_write32_mask(rtwdev, R_P1_TXPW_RSTB, mask, 0x3);
		}
	}

	if (chan->band_type == RTW89_BAND_6G)
		rtw89_phy_write32_set(rtwdev, R_MUIC, B_MUIC_EN);
	else
		rtw89_phy_write32_clr(rtwdev, R_MUIC, B_MUIC_EN);

	if (hal->antenna_tx)
		ntx_path = hal->antenna_tx;
	else
		ntx_path = chan->band_type == RTW89_BAND_6G ? RF_B : RF_AB;

	rtw8852c_ctrl_tx_path_tmac(rtwdev, ntx_path, (enum rtw89_mac_idx)phy_idx);

	rtw8852c_bb_reset_all(rtwdev, phy_idx);
}

static void rtw8852c_set_channel(struct rtw89_dev *rtwdev,
				 const struct rtw89_chan *chan,
				 enum rtw89_mac_idx mac_idx,
				 enum rtw89_phy_idx phy_idx)
{
	rtw8852c_set_channel_mac(rtwdev, chan, mac_idx);
	rtw8852c_set_channel_bb(rtwdev, chan, phy_idx);
	rtw8852c_set_channel_rf(rtwdev, chan, phy_idx);
}

static void rtw8852c_dfs_en(struct rtw89_dev *rtwdev, bool en)
{
	if (en)
		rtw89_phy_write32_mask(rtwdev, R_UPD_P0, B_UPD_P0_EN, 1);
	else
		rtw89_phy_write32_mask(rtwdev, R_UPD_P0, B_UPD_P0_EN, 0);
}

static void rtw8852c_adc_en(struct rtw89_dev *rtwdev, bool en)
{
	if (en)
		rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RST,
				       0x0);
	else
		rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RST,
				       0xf);
}

static void rtw8852c_set_channel_help(struct rtw89_dev *rtwdev, bool enter,
				      struct rtw89_channel_help_params *p,
				      const struct rtw89_chan *chan,
				      enum rtw89_mac_idx mac_idx,
				      enum rtw89_phy_idx phy_idx)
{
	if (enter) {
		rtw89_chip_stop_sch_tx(rtwdev, mac_idx, &p->tx_en,
				       RTW89_SCH_TX_SEL_ALL);
		rtw89_mac_cfg_ppdu_status(rtwdev, mac_idx, false);
		rtw8852c_dfs_en(rtwdev, false);
		rtw8852c_tssi_cont_en_phyidx(rtwdev, false, phy_idx, chan);
		rtw8852c_adc_en(rtwdev, false);
		fsleep(40);
		rtw8852c_bb_reset_en(rtwdev, chan->band_type, phy_idx, false);
	} else {
		rtw89_mac_cfg_ppdu_status(rtwdev, mac_idx, true);
		rtw8852c_adc_en(rtwdev, true);
		rtw8852c_dfs_en(rtwdev, true);
		rtw8852c_tssi_cont_en_phyidx(rtwdev, true, phy_idx, chan);
		rtw8852c_bb_reset_en(rtwdev, chan->band_type, phy_idx, true);
		rtw89_chip_resume_sch_tx(rtwdev, mac_idx, p->tx_en);
	}
}

static void rtw8852c_rfk_init(struct rtw89_dev *rtwdev)
{
	struct rtw89_rfk_mcc_info *rfk_mcc = &rtwdev->rfk_mcc;

	rtwdev->is_tssi_mode[RF_PATH_A] = false;
	rtwdev->is_tssi_mode[RF_PATH_B] = false;
	memset(rfk_mcc, 0, sizeof(*rfk_mcc));
	rtw8852c_lck_init(rtwdev);
	rtw8852c_dpk_init(rtwdev);

	rtw8852c_rck(rtwdev);
	rtw8852c_dack(rtwdev, RTW89_CHANCTX_0);
	rtw8852c_rx_dck(rtwdev, RTW89_PHY_0, false);
}

static void rtw8852c_rfk_channel(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif)
{
	enum rtw89_chanctx_idx chanctx_idx = rtwvif->chanctx_idx;
	enum rtw89_phy_idx phy_idx = rtwvif->phy_idx;

	rtw8852c_mcc_get_ch_info(rtwdev, phy_idx);
	rtw8852c_rx_dck(rtwdev, phy_idx, false);
	rtw8852c_iqk(rtwdev, phy_idx, chanctx_idx);
	rtw8852c_tssi(rtwdev, phy_idx, chanctx_idx);
	rtw8852c_dpk(rtwdev, phy_idx, chanctx_idx);
	rtw89_fw_h2c_rf_ntfy_mcc(rtwdev);
}

static void rtw8852c_rfk_band_changed(struct rtw89_dev *rtwdev,
				      enum rtw89_phy_idx phy_idx,
				      const struct rtw89_chan *chan)
{
	rtw8852c_tssi_scan(rtwdev, phy_idx, chan);
}

static void rtw8852c_rfk_scan(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
			      bool start)
{
	rtw8852c_wifi_scan_notify(rtwdev, start, rtwvif->phy_idx);
}

static void rtw8852c_rfk_track(struct rtw89_dev *rtwdev)
{
	rtw8852c_dpk_track(rtwdev);
	rtw8852c_lck_track(rtwdev);
	rtw8852c_rx_dck_track(rtwdev);
}

static u32 rtw8852c_bb_cal_txpwr_ref(struct rtw89_dev *rtwdev,
				     enum rtw89_phy_idx phy_idx, s16 ref)
{
	s8 ofst_int = 0;
	u8 base_cw_0db = 0x27;
	u16 tssi_16dbm_cw = 0x12c;
	s16 pwr_s10_3 = 0;
	s16 rf_pwr_cw = 0;
	u16 bb_pwr_cw = 0;
	u32 pwr_cw = 0;
	u32 tssi_ofst_cw = 0;

	pwr_s10_3 = (ref << 1) + (s16)(ofst_int) + (s16)(base_cw_0db << 3);
	bb_pwr_cw = FIELD_GET(GENMASK(2, 0), pwr_s10_3);
	rf_pwr_cw = FIELD_GET(GENMASK(8, 3), pwr_s10_3);
	rf_pwr_cw = clamp_t(s16, rf_pwr_cw, 15, 63);
	pwr_cw = (rf_pwr_cw << 3) | bb_pwr_cw;

	tssi_ofst_cw = (u32)((s16)tssi_16dbm_cw + (ref << 1) - (16 << 3));
	rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
		    "[TXPWR] tssi_ofst_cw=%d rf_cw=0x%x bb_cw=0x%x\n",
		    tssi_ofst_cw, rf_pwr_cw, bb_pwr_cw);

	return (tssi_ofst_cw << 18) | (pwr_cw << 9) | (ref & GENMASK(8, 0));
}

static
void rtw8852c_set_txpwr_ul_tb_offset(struct rtw89_dev *rtwdev,
				     s8 pw_ofst, enum rtw89_mac_idx mac_idx)
{
	s8 pw_ofst_2tx;
	s8 val_1t;
	s8 val_2t;
	u32 reg;
	u8 i;

	if (pw_ofst < -32 || pw_ofst > 31) {
		rtw89_warn(rtwdev, "[ULTB] Err pwr_offset=%d\n", pw_ofst);
		return;
	}
	val_1t = pw_ofst << 2;
	pw_ofst_2tx = max(pw_ofst - 3, -32);
	val_2t = pw_ofst_2tx << 2;

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[ULTB] val_1tx=0x%x\n", val_1t);
	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[ULTB] val_2tx=0x%x\n", val_2t);

	for (i = 0; i < 4; i++) {
		/* 1TX */
		reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_PWR_UL_TB_1T, mac_idx);
		rtw89_write32_mask(rtwdev, reg,
				   B_AX_PWR_UL_TB_1T_V1_MASK << (8 * i),
				   val_1t);
		/* 2TX */
		reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_PWR_UL_TB_2T, mac_idx);
		rtw89_write32_mask(rtwdev, reg,
				   B_AX_PWR_UL_TB_2T_V1_MASK << (8 * i),
				   val_2t);
	}
}

static void rtw8852c_set_txpwr_ref(struct rtw89_dev *rtwdev,
				   enum rtw89_phy_idx phy_idx)
{
	static const u32 addr[RF_PATH_NUM_8852C] = {0x5800, 0x7800};
	const u32 mask = 0x7FFFFFF;
	const u8 ofst_ofdm = 0x4;
	const u8 ofst_cck = 0x8;
	s16 ref_ofdm = 0;
	s16 ref_cck = 0;
	u32 val;
	u8 i;

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[TXPWR] set txpwr reference\n");

	rtw89_mac_txpwr_write32_mask(rtwdev, phy_idx, R_AX_PWR_RATE_CTRL,
				     GENMASK(27, 10), 0x0);

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[TXPWR] set bb ofdm txpwr ref\n");
	val = rtw8852c_bb_cal_txpwr_ref(rtwdev, phy_idx, ref_ofdm);

	for (i = 0; i < RF_PATH_NUM_8852C; i++)
		rtw89_phy_write32_idx(rtwdev, addr[i] + ofst_ofdm, mask, val,
				      phy_idx);

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[TXPWR] set bb cck txpwr ref\n");
	val = rtw8852c_bb_cal_txpwr_ref(rtwdev, phy_idx, ref_cck);

	for (i = 0; i < RF_PATH_NUM_8852C; i++)
		rtw89_phy_write32_idx(rtwdev, addr[i] + ofst_cck, mask, val,
				      phy_idx);
}

static void rtw8852c_bb_set_tx_shape_dfir(struct rtw89_dev *rtwdev,
					  const struct rtw89_chan *chan,
					  u8 tx_shape_idx,
					  enum rtw89_phy_idx phy_idx)
{
#define __DFIR_CFG_MASK 0xffffff
#define __DFIR_CFG_NR 8
#define __DECL_DFIR_VAR(_prefix, _name, _val...) \
	static const u32 _prefix ## _ ## _name[] = {_val}; \
	static_assert(ARRAY_SIZE(_prefix ## _ ## _name) == __DFIR_CFG_NR)
#define __DECL_DFIR_PARAM(_name, _val...) __DECL_DFIR_VAR(param, _name, _val)
#define __DECL_DFIR_ADDR(_name, _val...) __DECL_DFIR_VAR(addr, _name, _val)

	__DECL_DFIR_PARAM(flat,
			  0x003D23FF, 0x0029B354, 0x000FC1C8, 0x00FDB053,
			  0x00F86F9A, 0x00FAEF92, 0x00FE5FCC, 0x00FFDFF5);
	__DECL_DFIR_PARAM(sharp,
			  0x003D83FF, 0x002C636A, 0x0013F204, 0x00008090,
			  0x00F87FB0, 0x00F99F83, 0x00FDBFBA, 0x00003FF5);
	__DECL_DFIR_PARAM(sharp_14,
			  0x003B13FF, 0x001C42DE, 0x00FDB0AD, 0x00F60F6E,
			  0x00FD8F92, 0x0002D011, 0x0001C02C, 0x00FFF00A);
	__DECL_DFIR_ADDR(filter,
			 0x45BC, 0x45CC, 0x45D0, 0x45D4, 0x45D8, 0x45C0,
			 0x45C4, 0x45C8);
	u8 ch = chan->channel;
	const u32 *param;
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
		rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
			    "set tx shape dfir: 0x%x: 0x%x\n", addr_filter[i],
			    param[i]);
		rtw89_phy_write32_idx(rtwdev, addr_filter[i], __DFIR_CFG_MASK,
				      param[i], phy_idx);
	}

#undef __DECL_DFIR_ADDR
#undef __DECL_DFIR_PARAM
#undef __DECL_DFIR_VAR
#undef __DFIR_CFG_NR
#undef __DFIR_CFG_MASK
}

static void rtw8852c_set_tx_shape(struct rtw89_dev *rtwdev,
				  const struct rtw89_chan *chan,
				  enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_rfe_parms *rfe_parms = rtwdev->rfe_parms;
	u8 band = chan->band_type;
	u8 regd = rtw89_regd_get(rtwdev, band);
	u8 tx_shape_cck = (*rfe_parms->tx_shape.lmt)[band][RTW89_RS_CCK][regd];
	u8 tx_shape_ofdm = (*rfe_parms->tx_shape.lmt)[band][RTW89_RS_OFDM][regd];

	if (band == RTW89_BAND_2G)
		rtw8852c_bb_set_tx_shape_dfir(rtwdev, chan, tx_shape_cck, phy_idx);

	rtw89_phy_tssi_ctrl_set_bandedge_cfg(rtwdev,
					     (enum rtw89_mac_idx)phy_idx,
					     tx_shape_ofdm);

	rtw89_phy_write32_set(rtwdev, R_P0_DAC_COMP_POST_DPD_EN,
			      B_P0_DAC_COMP_POST_DPD_EN);
	rtw89_phy_write32_set(rtwdev, R_P1_DAC_COMP_POST_DPD_EN,
			      B_P1_DAC_COMP_POST_DPD_EN);
}

static void rtw8852c_set_txpwr(struct rtw89_dev *rtwdev,
			       const struct rtw89_chan *chan,
			       enum rtw89_phy_idx phy_idx)
{
	rtw89_phy_set_txpwr_byrate(rtwdev, chan, phy_idx);
	rtw89_phy_set_txpwr_offset(rtwdev, chan, phy_idx);
	rtw8852c_set_tx_shape(rtwdev, chan, phy_idx);
	rtw89_phy_set_txpwr_limit(rtwdev, chan, phy_idx);
	rtw89_phy_set_txpwr_limit_ru(rtwdev, chan, phy_idx);
}

static void rtw8852c_set_txpwr_ctrl(struct rtw89_dev *rtwdev,
				    enum rtw89_phy_idx phy_idx)
{
	rtw8852c_set_txpwr_ref(rtwdev, phy_idx);
}

static void
rtw8852c_init_tssi_ctrl(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	static const struct rtw89_reg2_def ctrl_ini[] = {
		{0xD938, 0x00010100},
		{0xD93C, 0x0500D500},
		{0xD940, 0x00000500},
		{0xD944, 0x00000005},
		{0xD94C, 0x00220000},
		{0xD950, 0x00030000},
	};
	u32 addr;
	int i;

	for (addr = R_AX_TSSI_CTRL_HEAD; addr <= R_AX_TSSI_CTRL_TAIL; addr += 4)
		rtw89_mac_txpwr_write32(rtwdev, phy_idx, addr, 0);

	for (i = 0; i < ARRAY_SIZE(ctrl_ini); i++)
		rtw89_mac_txpwr_write32(rtwdev, phy_idx, ctrl_ini[i].addr,
					ctrl_ini[i].data);

	rtw89_phy_tssi_ctrl_set_bandedge_cfg(rtwdev,
					     (enum rtw89_mac_idx)phy_idx,
					     RTW89_TSSI_BANDEDGE_FLAT);
}

static int
rtw8852c_init_txpwr_unit(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
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

	rtw8852c_set_txpwr_ul_tb_offset(rtwdev, 0, phy_idx == RTW89_PHY_1 ?
							      RTW89_MAC_1 :
							      RTW89_MAC_0);
	rtw8852c_init_tssi_ctrl(rtwdev, phy_idx);

	return 0;
}

static void rtw8852c_bb_cfg_rx_path(struct rtw89_dev *rtwdev, u8 rx_path)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_CHANCTX_0);
	u8 band = chan->band_type;
	u32 rst_mask0 = B_P0_TXPW_RSTB_MANON | B_P0_TXPW_RSTB_TSSI;
	u32 rst_mask1 = B_P1_TXPW_RSTB_MANON | B_P1_TXPW_RSTB_TSSI;

	if (rtwdev->dbcc_en) {
		rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD, B_ANT_RX_SEG0, 1);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD, B_ANT_RX_SEG0, 2,
				      RTW89_PHY_1);

		rtw89_phy_write32_mask(rtwdev, R_FC0_BW, B_ANT_RX_1RCCA_SEG0,
				       1);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW, B_ANT_RX_1RCCA_SEG1,
				       1);
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW, B_ANT_RX_1RCCA_SEG0, 2,
				      RTW89_PHY_1);
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW, B_ANT_RX_1RCCA_SEG1, 2,
				      RTW89_PHY_1);

		rtw89_phy_write32_mask(rtwdev, R_RXHT_MCS_LIMIT,
				       B_RXHT_MCS_LIMIT, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXVHT_MCS_LIMIT,
				       B_RXVHT_MCS_LIMIT, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_USER_MAX, 8);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_MAX_NSS, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHETB_MAX_NSS, 0);

		rtw89_phy_write32_idx(rtwdev, R_RXHT_MCS_LIMIT,
				      B_RXHT_MCS_LIMIT, 0, RTW89_PHY_1);
		rtw89_phy_write32_idx(rtwdev, R_RXVHT_MCS_LIMIT,
				      B_RXVHT_MCS_LIMIT, 0, RTW89_PHY_1);
		rtw89_phy_write32_idx(rtwdev, R_RXHE, B_RXHE_USER_MAX, 1,
				      RTW89_PHY_1);
		rtw89_phy_write32_idx(rtwdev, R_RXHE, B_RXHE_MAX_NSS, 0,
				      RTW89_PHY_1);
		rtw89_phy_write32_idx(rtwdev, R_RXHE, B_RXHETB_MAX_NSS, 0,
				      RTW89_PHY_1);
		rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB, rst_mask0, 1);
		rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB, rst_mask0, 3);
		rtw89_phy_write32_mask(rtwdev, R_P1_TXPW_RSTB, rst_mask1, 1);
		rtw89_phy_write32_mask(rtwdev, R_P1_TXPW_RSTB, rst_mask1, 3);
	} else {
		if (rx_path == RF_PATH_A) {
			rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD,
					       B_ANT_RX_SEG0, 1);
			rtw89_phy_write32_mask(rtwdev, R_FC0_BW,
					       B_ANT_RX_1RCCA_SEG0, 1);
			rtw89_phy_write32_mask(rtwdev, R_FC0_BW,
					       B_ANT_RX_1RCCA_SEG1, 1);
			rtw89_phy_write32_mask(rtwdev, R_RXHT_MCS_LIMIT,
					       B_RXHT_MCS_LIMIT, 0);
			rtw89_phy_write32_mask(rtwdev, R_RXVHT_MCS_LIMIT,
					       B_RXVHT_MCS_LIMIT, 0);
			rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_MAX_NSS,
					       0);
			rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHETB_MAX_NSS,
					       0);
			rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB,
					       rst_mask0, 1);
			rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB,
					       rst_mask0, 3);
		} else if (rx_path == RF_PATH_B) {
			rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD,
					       B_ANT_RX_SEG0, 2);
			rtw89_phy_write32_mask(rtwdev, R_FC0_BW,
					       B_ANT_RX_1RCCA_SEG0, 2);
			rtw89_phy_write32_mask(rtwdev, R_FC0_BW,
					       B_ANT_RX_1RCCA_SEG1, 2);
			rtw89_phy_write32_mask(rtwdev, R_RXHT_MCS_LIMIT,
					       B_RXHT_MCS_LIMIT, 0);
			rtw89_phy_write32_mask(rtwdev, R_RXVHT_MCS_LIMIT,
					       B_RXVHT_MCS_LIMIT, 0);
			rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_MAX_NSS,
					       0);
			rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHETB_MAX_NSS,
					       0);
			rtw89_phy_write32_mask(rtwdev, R_P1_TXPW_RSTB,
					       rst_mask1, 1);
			rtw89_phy_write32_mask(rtwdev, R_P1_TXPW_RSTB,
					       rst_mask1, 3);
		} else {
			rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD,
					       B_ANT_RX_SEG0, 3);
			rtw89_phy_write32_mask(rtwdev, R_FC0_BW,
					       B_ANT_RX_1RCCA_SEG0, 3);
			rtw89_phy_write32_mask(rtwdev, R_FC0_BW,
					       B_ANT_RX_1RCCA_SEG1, 3);
			rtw89_phy_write32_mask(rtwdev, R_RXHT_MCS_LIMIT,
					       B_RXHT_MCS_LIMIT, 1);
			rtw89_phy_write32_mask(rtwdev, R_RXVHT_MCS_LIMIT,
					       B_RXVHT_MCS_LIMIT, 1);
			rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_MAX_NSS,
					       1);
			rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHETB_MAX_NSS,
					       1);
			rtw8852c_ctrl_btg_bt_rx(rtwdev, band == RTW89_BAND_2G,
						RTW89_PHY_0);
			rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB,
					       rst_mask0, 1);
			rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB,
					       rst_mask0, 3);
			rtw89_phy_write32_mask(rtwdev, R_P1_TXPW_RSTB,
					       rst_mask1, 1);
			rtw89_phy_write32_mask(rtwdev, R_P1_TXPW_RSTB,
					       rst_mask1, 3);
		}
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_USER_MAX, 8);
	}
}

static void rtw8852c_ctrl_tx_path_tmac(struct rtw89_dev *rtwdev, u8 tx_path,
				       enum rtw89_mac_idx mac_idx)
{
	struct rtw89_reg2_def path_com[] = {
		{R_AX_PATH_COM0, AX_PATH_COM0_DFVAL},
		{R_AX_PATH_COM1, AX_PATH_COM1_DFVAL},
		{R_AX_PATH_COM2, AX_PATH_COM2_DFVAL},
		{R_AX_PATH_COM3, AX_PATH_COM3_DFVAL},
		{R_AX_PATH_COM4, AX_PATH_COM4_DFVAL},
		{R_AX_PATH_COM5, AX_PATH_COM5_DFVAL},
		{R_AX_PATH_COM6, AX_PATH_COM6_DFVAL},
		{R_AX_PATH_COM7, AX_PATH_COM7_DFVAL},
		{R_AX_PATH_COM8, AX_PATH_COM8_DFVAL},
		{R_AX_PATH_COM9, AX_PATH_COM9_DFVAL},
		{R_AX_PATH_COM10, AX_PATH_COM10_DFVAL},
		{R_AX_PATH_COM11, AX_PATH_COM11_DFVAL},
	};
	u32 addr;
	u32 reg;
	u8 cr_size = ARRAY_SIZE(path_com);
	u8 i = 0;

	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_MOD, 0, RTW89_PHY_0);
	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_MOD, 0, RTW89_PHY_1);

	for (addr = R_AX_MACID_ANT_TABLE;
	     addr <= R_AX_MACID_ANT_TABLE_LAST; addr += 4) {
		reg = rtw89_mac_reg_by_idx(rtwdev, addr, mac_idx);
		rtw89_write32(rtwdev, reg, 0);
	}

	if (tx_path == RF_A) {
		path_com[0].data = AX_PATH_COM0_PATHA;
		path_com[1].data = AX_PATH_COM1_PATHA;
		path_com[2].data = AX_PATH_COM2_PATHA;
		path_com[7].data = AX_PATH_COM7_PATHA;
		path_com[8].data = AX_PATH_COM8_PATHA;
	} else if (tx_path == RF_B) {
		path_com[0].data = AX_PATH_COM0_PATHB;
		path_com[1].data = AX_PATH_COM1_PATHB;
		path_com[2].data = AX_PATH_COM2_PATHB;
		path_com[7].data = AX_PATH_COM7_PATHB;
		path_com[8].data = AX_PATH_COM8_PATHB;
	} else if (tx_path == RF_AB) {
		path_com[0].data = AX_PATH_COM0_PATHAB;
		path_com[1].data = AX_PATH_COM1_PATHAB;
		path_com[2].data = AX_PATH_COM2_PATHAB;
		path_com[7].data = AX_PATH_COM7_PATHAB;
		path_com[8].data = AX_PATH_COM8_PATHAB;
	} else {
		rtw89_warn(rtwdev, "[Invalid Tx Path]Tx Path: %d\n", tx_path);
		return;
	}

	for (i = 0; i < cr_size; i++) {
		rtw89_debug(rtwdev, RTW89_DBG_TSSI, "0x%x = 0x%x\n",
			    path_com[i].addr, path_com[i].data);
		reg = rtw89_mac_reg_by_idx(rtwdev, path_com[i].addr, mac_idx);
		rtw89_write32(rtwdev, reg, path_com[i].data);
	}
}

static void rtw8852c_ctrl_nbtg_bt_tx(struct rtw89_dev *rtwdev, bool en,
				     enum rtw89_phy_idx phy_idx)
{
	if (en) {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_FRC_FIR_TYPE_V1,
				       B_PATH0_FRC_FIR_TYPE_MSK_V1, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_FRC_FIR_TYPE_V1,
				       B_PATH1_FRC_FIR_TYPE_MSK_V1, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_RXBB_V1,
				       B_PATH0_RXBB_MSK_V1, 0xf);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_RXBB_V1,
				       B_PATH1_RXBB_MSK_V1, 0xf);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_G_LNA6_OP1DB_V1,
				       B_PATH0_G_LNA6_OP1DB_V1, 0x80);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_G_LNA6_OP1DB_V1,
				       B_PATH1_G_LNA6_OP1DB_V1, 0x80);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_G_TIA0_LNA6_OP1DB_V1,
				       B_PATH0_G_TIA0_LNA6_OP1DB_V1, 0x80);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_G_TIA1_LNA6_OP1DB_V1,
				       B_PATH0_G_TIA1_LNA6_OP1DB_V1, 0x80);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_G_TIA0_LNA6_OP1DB_V1,
				       B_PATH1_G_TIA0_LNA6_OP1DB_V1, 0x80);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_G_TIA1_LNA6_OP1DB_V1,
				       B_PATH1_G_TIA1_LNA6_OP1DB_V1, 0x80);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BT_BACKOFF_V1,
				       B_PATH0_BT_BACKOFF_V1, 0x780D1E);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BT_BACKOFF_V1,
				       B_PATH1_BT_BACKOFF_V1, 0x780D1E);
		rtw89_phy_write32_mask(rtwdev, R_P0_BACKOFF_IBADC_V1,
				       B_P0_BACKOFF_IBADC_V1, 0x34);
		rtw89_phy_write32_mask(rtwdev, R_P1_BACKOFF_IBADC_V1,
				       B_P1_BACKOFF_IBADC_V1, 0x34);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_FRC_FIR_TYPE_V1,
				       B_PATH0_FRC_FIR_TYPE_MSK_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_FRC_FIR_TYPE_V1,
				       B_PATH1_FRC_FIR_TYPE_MSK_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_RXBB_V1,
				       B_PATH0_RXBB_MSK_V1, 0x60);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_RXBB_V1,
				       B_PATH1_RXBB_MSK_V1, 0x60);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_G_LNA6_OP1DB_V1,
				       B_PATH0_G_LNA6_OP1DB_V1, 0x1a);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_G_LNA6_OP1DB_V1,
				       B_PATH1_G_LNA6_OP1DB_V1, 0x1a);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_G_TIA0_LNA6_OP1DB_V1,
				       B_PATH0_G_TIA0_LNA6_OP1DB_V1, 0x2a);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_G_TIA1_LNA6_OP1DB_V1,
				       B_PATH0_G_TIA1_LNA6_OP1DB_V1, 0x2a);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_G_TIA0_LNA6_OP1DB_V1,
				       B_PATH1_G_TIA0_LNA6_OP1DB_V1, 0x2a);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_G_TIA1_LNA6_OP1DB_V1,
				       B_PATH1_G_TIA1_LNA6_OP1DB_V1, 0x2a);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BT_BACKOFF_V1,
				       B_PATH0_BT_BACKOFF_V1, 0x79E99E);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BT_BACKOFF_V1,
				       B_PATH1_BT_BACKOFF_V1, 0x79E99E);
		rtw89_phy_write32_mask(rtwdev, R_P0_BACKOFF_IBADC_V1,
				       B_P0_BACKOFF_IBADC_V1, 0x26);
		rtw89_phy_write32_mask(rtwdev, R_P1_BACKOFF_IBADC_V1,
				       B_P1_BACKOFF_IBADC_V1, 0x26);
	}
}

static void rtw8852c_bb_cfg_txrx_path(struct rtw89_dev *rtwdev)
{
	struct rtw89_hal *hal = &rtwdev->hal;

	rtw8852c_bb_cfg_rx_path(rtwdev, RF_PATH_AB);

	if (hal->rx_nss == 1) {
		rtw89_phy_write32_mask(rtwdev, R_RXHT_MCS_LIMIT, B_RXHT_MCS_LIMIT, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXVHT_MCS_LIMIT, B_RXVHT_MCS_LIMIT, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_MAX_NSS, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHETB_MAX_NSS, 0);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_RXHT_MCS_LIMIT, B_RXHT_MCS_LIMIT, 1);
		rtw89_phy_write32_mask(rtwdev, R_RXVHT_MCS_LIMIT, B_RXVHT_MCS_LIMIT, 1);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_MAX_NSS, 1);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHETB_MAX_NSS, 1);
	}
}

static u8 rtw8852c_get_thermal(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path)
{
	rtw89_write_rf(rtwdev, rf_path, RR_TM, RR_TM_TRI, 0x1);
	rtw89_write_rf(rtwdev, rf_path, RR_TM, RR_TM_TRI, 0x0);
	rtw89_write_rf(rtwdev, rf_path, RR_TM, RR_TM_TRI, 0x1);

	fsleep(200);

	return rtw89_read_rf(rtwdev, rf_path, RR_TM, RR_TM_VAL);
}

static void rtw8852c_btc_set_rfe(struct rtw89_dev *rtwdev)
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

static void rtw8852c_ctrl_btg_bt_rx(struct rtw89_dev *rtwdev, bool en,
				    enum rtw89_phy_idx phy_idx)
{
	if (en) {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BT_SHARE_V1,
				       B_PATH0_BT_SHARE_V1, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BTG_PATH_V1,
				       B_PATH0_BTG_PATH_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_G_LNA6_OP1DB_V1,
				       B_PATH1_G_LNA6_OP1DB_V1, 0x20);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_G_TIA0_LNA6_OP1DB_V1,
				       B_PATH1_G_TIA0_LNA6_OP1DB_V1, 0x30);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BT_SHARE_V1,
				       B_PATH1_BT_SHARE_V1, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BTG_PATH_V1,
				       B_PATH1_BTG_PATH_V1, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PMAC_GNT, B_PMAC_GNT_P1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD, B_BT_SHARE, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW, B_ANT_RX_BT_SEG0, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_BT_DYN_DC_EST_EN,
				       B_BT_DYN_DC_EST_EN_MSK, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_GNT_BT_WGT_EN, B_GNT_BT_WGT_EN,
				       0x1);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BT_SHARE_V1,
				       B_PATH0_BT_SHARE_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BTG_PATH_V1,
				       B_PATH0_BTG_PATH_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_G_LNA6_OP1DB_V1,
				       B_PATH1_G_LNA6_OP1DB_V1, 0x1a);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_G_TIA0_LNA6_OP1DB_V1,
				       B_PATH1_G_TIA0_LNA6_OP1DB_V1, 0x2a);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BT_SHARE_V1,
				       B_PATH1_BT_SHARE_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BTG_PATH_V1,
				       B_PATH1_BTG_PATH_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PMAC_GNT, B_PMAC_GNT_P1, 0xf);
		rtw89_phy_write32_mask(rtwdev, R_PMAC_GNT, B_PMAC_GNT_P2, 0x4);
		rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD, B_BT_SHARE, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW, B_ANT_RX_BT_SEG0, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_BT_DYN_DC_EST_EN,
				       B_BT_DYN_DC_EST_EN_MSK, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_GNT_BT_WGT_EN, B_GNT_BT_WGT_EN,
				       0x0);
	}
}

static
void rtw8852c_set_trx_mask(struct rtw89_dev *rtwdev, u8 path, u8 group, u32 val)
{
	rtw89_write_rf(rtwdev, path, RR_LUTWE, RFREG_MASK, 0x20000);
	rtw89_write_rf(rtwdev, path, RR_LUTWA, RFREG_MASK, group);
	rtw89_write_rf(rtwdev, path, RR_LUTWD0, RFREG_MASK, val);
	rtw89_write_rf(rtwdev, path, RR_LUTWE, RFREG_MASK, 0x0);
}

static void rtw8852c_btc_init_cfg(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_mac_ax_coex coex_params = {
		.pta_mode = RTW89_MAC_AX_COEX_RTK_MODE,
		.direction = RTW89_MAC_AX_COEX_INNER,
	};

	/* PTA init  */
	rtw89_mac_coex_init_v1(rtwdev, &coex_params);

	/* set WL Tx response = Hi-Pri */
	chip->ops->btc_set_wl_pri(rtwdev, BTC_PRI_MASK_TX_RESP, true);
	chip->ops->btc_set_wl_pri(rtwdev, BTC_PRI_MASK_BEACON, true);

	/* set rf gnt debug off */
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_WLSEL, RFREG_MASK, 0x0);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_WLSEL, RFREG_MASK, 0x0);

	/* set WL Tx thru in TRX mask table if GNT_WL = 0 && BT_S1 = ss group */
	if (btc->ant_type == BTC_ANT_SHARED) {
		rtw8852c_set_trx_mask(rtwdev,
				      RF_PATH_A, BTC_BT_SS_GROUP, 0x5ff);
		rtw8852c_set_trx_mask(rtwdev,
				      RF_PATH_B, BTC_BT_SS_GROUP, 0x5ff);
		/* set path-A(S0) Tx/Rx no-mask if GNT_WL=0 && BT_S1=tx group */
		rtw8852c_set_trx_mask(rtwdev,
				      RF_PATH_A, BTC_BT_TX_GROUP, 0x5ff);
	} else { /* set WL Tx stb if GNT_WL = 0 && BT_S1 = ss group for 3-ant */
		rtw8852c_set_trx_mask(rtwdev,
				      RF_PATH_A, BTC_BT_SS_GROUP, 0x5df);
		rtw8852c_set_trx_mask(rtwdev,
				      RF_PATH_B, BTC_BT_SS_GROUP, 0x5df);
	}

	/* set PTA break table */
	rtw89_write32(rtwdev, R_AX_BT_BREAK_TABLE, BTC_BREAK_PARAM);

	 /* enable BT counter 0xda10[1:0] = 2b'11 */
	rtw89_write32_set(rtwdev,
			  R_AX_BT_CNT_CFG, B_AX_BT_CNT_EN |
			  B_AX_BT_CNT_RST_V1);
	btc->cx.wl.status.map.init_ok = true;
}

static
void rtw8852c_btc_set_wl_pri(struct rtw89_dev *rtwdev, u8 map, bool state)
{
	u32 bitmap = 0;
	u32 reg = 0;

	switch (map) {
	case BTC_PRI_MASK_TX_RESP:
		reg = R_BTC_COEX_WL_REQ;
		bitmap = B_BTC_RSP_ACK_HI;
		break;
	case BTC_PRI_MASK_BEACON:
		reg = R_BTC_COEX_WL_REQ;
		bitmap = B_BTC_TX_BCN_HI;
		break;
	default:
		return;
	}

	if (state)
		rtw89_write32_set(rtwdev, reg, bitmap);
	else
		rtw89_write32_clr(rtwdev, reg, bitmap);
}

union rtw8852c_btc_wl_txpwr_ctrl {
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
rtw8852c_btc_set_wl_txpwr_ctrl(struct rtw89_dev *rtwdev, u32 txpwr_val)
{
	union rtw8852c_btc_wl_txpwr_ctrl arg = { .txpwr_val = txpwr_val };
	s32 val;

#define __write_ctrl(_reg, _msk, _val, _en, _cond)		\
do {								\
	u32 _wrt = FIELD_PREP(_msk, _val);			\
	BUILD_BUG_ON((_msk & _en) != 0);			\
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
s8 rtw8852c_btc_get_bt_rssi(struct rtw89_dev *rtwdev, s8 val)
{
	/* +6 for compensate offset */
	return clamp_t(s8, val + 6, -100, 0) + 100;
}

static const struct rtw89_btc_rf_trx_para rtw89_btc_8852c_rf_ul[] = {
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

static const struct rtw89_btc_rf_trx_para rtw89_btc_8852c_rf_dl[] = {
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

static const u8 rtw89_btc_8852c_wl_rssi_thres[BTC_WL_RSSI_THMAX] = {60, 50, 40, 30};
static const u8 rtw89_btc_8852c_bt_rssi_thres[BTC_BT_RSSI_THMAX] = {40, 36, 31, 28};

static const struct rtw89_btc_fbtc_mreg rtw89_btc_8852c_mon_reg[] = {
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda00),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda04),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda24),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda30),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda34),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda38),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda44),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda48),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda4c),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xd200),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xd220),
	RTW89_DEF_FBTC_MREG(REG_BB, 4, 0x980),
	RTW89_DEF_FBTC_MREG(REG_BB, 4, 0x4aa4),
	RTW89_DEF_FBTC_MREG(REG_BB, 4, 0x4778),
	RTW89_DEF_FBTC_MREG(REG_BB, 4, 0x476c),
};

static
void rtw8852c_btc_update_bt_cnt(struct rtw89_dev *rtwdev)
{
	/* Feature move to firmware */
}

static
void rtw8852c_btc_wl_s1_standby(struct rtw89_dev *rtwdev, bool state)
{
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWE, RFREG_MASK, 0x80000);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWA, RFREG_MASK, 0x1);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD1, RFREG_MASK, 0x620);

	/* set WL standby = Rx for GNT_BT_Tx = 1->0 settle issue */
	if (state)
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0,
			       RFREG_MASK, 0x179c);
	else
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0,
			       RFREG_MASK, 0x208);

	rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWE, RFREG_MASK, 0x0);
}

static void rtw8852c_set_wl_lna2(struct rtw89_dev *rtwdev, u8 level)
{
	/* level=0 Default:    TIA 1/0= (LNA2,TIAN6) = (7,1)/(5,1) = 21dB/12dB
	 * level=1 Fix LNA2=5: TIA 1/0= (LNA2,TIAN6) = (5,0)/(5,1) = 18dB/12dB
	 * To improve BT ACI in co-rx
	 */

	switch (level) {
	case 0: /* default */
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWE, RFREG_MASK, 0x1000);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWA, RFREG_MASK, 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0, RFREG_MASK, 0x15);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWA, RFREG_MASK, 0x1);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0, RFREG_MASK, 0x17);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWA, RFREG_MASK, 0x2);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0, RFREG_MASK, 0x15);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWA, RFREG_MASK, 0x3);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0, RFREG_MASK, 0x17);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWE, RFREG_MASK, 0x0);
		break;
	case 1: /* Fix LNA2=5  */
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWE, RFREG_MASK, 0x1000);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWA, RFREG_MASK, 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0, RFREG_MASK, 0x15);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWA, RFREG_MASK, 0x1);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0, RFREG_MASK, 0x5);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWA, RFREG_MASK, 0x2);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0, RFREG_MASK, 0x15);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWA, RFREG_MASK, 0x3);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0, RFREG_MASK, 0x5);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWE, RFREG_MASK, 0x0);
		break;
	}
}

static void rtw8852c_btc_set_wl_rx_gain(struct rtw89_dev *rtwdev, u32 level)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	switch (level) {
	case 0: /* original */
	default:
		rtw8852c_ctrl_nbtg_bt_tx(rtwdev, false, RTW89_PHY_0);
		btc->dm.wl_lna2 = 0;
		break;
	case 1: /* for FDD free-run */
		rtw8852c_ctrl_nbtg_bt_tx(rtwdev, true, RTW89_PHY_0);
		btc->dm.wl_lna2 = 0;
		break;
	case 2: /* for BTG Co-Rx*/
		rtw8852c_ctrl_nbtg_bt_tx(rtwdev, false, RTW89_PHY_0);
		btc->dm.wl_lna2 = 1;
		break;
	}

	rtw8852c_set_wl_lna2(rtwdev, btc->dm.wl_lna2);
}

static void rtw8852c_fill_freq_with_ppdu(struct rtw89_dev *rtwdev,
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

static void rtw8852c_query_ppdu(struct rtw89_dev *rtwdev,
				struct rtw89_rx_phy_ppdu *phy_ppdu,
				struct ieee80211_rx_status *status)
{
	u8 path;
	u8 *rx_power = phy_ppdu->rssi;

	status->signal = RTW89_RSSI_RAW_TO_DBM(max(rx_power[RF_PATH_A], rx_power[RF_PATH_B]));
	for (path = 0; path < rtwdev->chip->rf_path_num; path++) {
		status->chains |= BIT(path);
		status->chain_signal[path] = RTW89_RSSI_RAW_TO_DBM(rx_power[path]);
	}
	if (phy_ppdu->valid)
		rtw8852c_fill_freq_with_ppdu(rtwdev, phy_ppdu, status);
}

static int rtw8852c_mac_enable_bb_rf(struct rtw89_dev *rtwdev)
{
	int ret;

	rtw89_write8_set(rtwdev, R_AX_SYS_FUNC_EN,
			 B_AX_FEN_BBRSTB | B_AX_FEN_BB_GLB_RSTN);

	rtw89_write32_set(rtwdev, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
	rtw89_write32_clr(rtwdev, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
	rtw89_write32_set(rtwdev, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);

	rtw89_write32_mask(rtwdev, R_AX_AFE_OFF_CTRL1, B_AX_S0_LDO_VSEL_F_MASK, 0x1);
	rtw89_write32_mask(rtwdev, R_AX_AFE_OFF_CTRL1, B_AX_S1_LDO_VSEL_F_MASK, 0x1);

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL0, 0x7, FULL_BIT_MASK);
	if (ret)
		return ret;

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0x6c, FULL_BIT_MASK);
	if (ret)
		return ret;

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S0, 0xc7, FULL_BIT_MASK);
	if (ret)
		return ret;

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S1, 0xc7, FULL_BIT_MASK);
	if (ret)
		return ret;

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL3, 0xd, FULL_BIT_MASK);
	if (ret)
		return ret;

	return 0;
}

static int rtw8852c_mac_disable_bb_rf(struct rtw89_dev *rtwdev)
{
	rtw89_write32_clr(rtwdev, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
	rtw89_write8_clr(rtwdev, R_AX_SYS_FUNC_EN,
			 B_AX_FEN_BBRSTB | B_AX_FEN_BB_GLB_RSTN);

	return 0;
}

static const struct rtw89_chanctx_listener rtw8852c_chanctx_listener = {
	.callbacks[RTW89_CHANCTX_CALLBACK_RFK] = rtw8852c_rfk_chanctx_cb,
};

#ifdef CONFIG_PM
static const struct wiphy_wowlan_support rtw_wowlan_stub_8852c = {
	.flags = WIPHY_WOWLAN_MAGIC_PKT | WIPHY_WOWLAN_DISCONNECT |
		 WIPHY_WOWLAN_NET_DETECT,
	.n_patterns = RTW89_MAX_PATTERN_NUM,
	.pattern_max_len = RTW89_MAX_PATTERN_SIZE,
	.pattern_min_len = 1,
	.max_nd_match_sets = RTW89_SCANOFLD_MAX_SSID,
};
#endif

static const struct rtw89_chip_ops rtw8852c_chip_ops = {
	.enable_bb_rf		= rtw8852c_mac_enable_bb_rf,
	.disable_bb_rf		= rtw8852c_mac_disable_bb_rf,
	.bb_preinit		= NULL,
	.bb_postinit		= NULL,
	.bb_reset		= rtw8852c_bb_reset,
	.bb_sethw		= rtw8852c_bb_sethw,
	.read_rf		= rtw89_phy_read_rf_v1,
	.write_rf		= rtw89_phy_write_rf_v1,
	.set_channel		= rtw8852c_set_channel,
	.set_channel_help	= rtw8852c_set_channel_help,
	.read_efuse		= rtw8852c_read_efuse,
	.read_phycap		= rtw8852c_read_phycap,
	.fem_setup		= NULL,
	.rfe_gpio		= NULL,
	.rfk_hw_init		= NULL,
	.rfk_init		= rtw8852c_rfk_init,
	.rfk_init_late		= NULL,
	.rfk_channel		= rtw8852c_rfk_channel,
	.rfk_band_changed	= rtw8852c_rfk_band_changed,
	.rfk_scan		= rtw8852c_rfk_scan,
	.rfk_track		= rtw8852c_rfk_track,
	.power_trim		= rtw8852c_power_trim,
	.set_txpwr		= rtw8852c_set_txpwr,
	.set_txpwr_ctrl		= rtw8852c_set_txpwr_ctrl,
	.init_txpwr_unit	= rtw8852c_init_txpwr_unit,
	.get_thermal		= rtw8852c_get_thermal,
	.ctrl_btg_bt_rx		= rtw8852c_ctrl_btg_bt_rx,
	.query_ppdu		= rtw8852c_query_ppdu,
	.convert_rpl_to_rssi	= NULL,
	.ctrl_nbtg_bt_tx	= rtw8852c_ctrl_nbtg_bt_tx,
	.cfg_txrx_path		= rtw8852c_bb_cfg_txrx_path,
	.set_txpwr_ul_tb_offset	= rtw8852c_set_txpwr_ul_tb_offset,
	.digital_pwr_comp	= NULL,
	.pwr_on_func		= rtw8852c_pwr_on_func,
	.pwr_off_func		= rtw8852c_pwr_off_func,
	.query_rxdesc		= rtw89_core_query_rxdesc,
	.fill_txdesc		= rtw89_core_fill_txdesc_v1,
	.fill_txdesc_fwcmd	= rtw89_core_fill_txdesc_fwcmd_v1,
	.cfg_ctrl_path		= rtw89_mac_cfg_ctrl_path_v1,
	.mac_cfg_gnt		= rtw89_mac_cfg_gnt_v1,
	.stop_sch_tx		= rtw89_mac_stop_sch_tx_v1,
	.resume_sch_tx		= rtw89_mac_resume_sch_tx_v1,
	.h2c_dctl_sec_cam	= rtw89_fw_h2c_dctl_sec_cam_v1,
	.h2c_default_cmac_tbl	= rtw89_fw_h2c_default_cmac_tbl,
	.h2c_assoc_cmac_tbl	= rtw89_fw_h2c_assoc_cmac_tbl,
	.h2c_ampdu_cmac_tbl	= NULL,
	.h2c_default_dmac_tbl	= NULL,
	.h2c_update_beacon	= rtw89_fw_h2c_update_beacon,
	.h2c_ba_cam		= rtw89_fw_h2c_ba_cam,

	.btc_set_rfe		= rtw8852c_btc_set_rfe,
	.btc_init_cfg		= rtw8852c_btc_init_cfg,
	.btc_set_wl_pri		= rtw8852c_btc_set_wl_pri,
	.btc_set_wl_txpwr_ctrl	= rtw8852c_btc_set_wl_txpwr_ctrl,
	.btc_get_bt_rssi	= rtw8852c_btc_get_bt_rssi,
	.btc_update_bt_cnt	= rtw8852c_btc_update_bt_cnt,
	.btc_wl_s1_standby	= rtw8852c_btc_wl_s1_standby,
	.btc_set_wl_rx_gain	= rtw8852c_btc_set_wl_rx_gain,
	.btc_set_policy		= rtw89_btc_set_policy_v1,
};

const struct rtw89_chip_info rtw8852c_chip_info = {
	.chip_id		= RTL8852C,
	.chip_gen		= RTW89_CHIP_AX,
	.ops			= &rtw8852c_chip_ops,
	.mac_def		= &rtw89_mac_gen_ax,
	.phy_def		= &rtw89_phy_gen_ax,
	.fw_basename		= RTW8852C_FW_BASENAME,
	.fw_format_max		= RTW8852C_FW_FORMAT_MAX,
	.try_ce_fw		= false,
	.bbmcu_nr		= 0,
	.needed_fw_elms		= 0,
	.fifo_size		= 458752,
	.small_fifo_size	= false,
	.dle_scc_rsvd_size	= 0,
	.max_amsdu_limit	= 8000,
	.dis_2g_40m_ul_ofdma	= false,
	.rsvd_ple_ofst		= 0x6f800,
	.hfc_param_ini		= rtw8852c_hfc_param_ini_pcie,
	.dle_mem		= rtw8852c_dle_mem_pcie,
	.wde_qempty_acq_grpnum	= 16,
	.wde_qempty_mgq_grpsel	= 16,
	.rf_base_addr		= {0xe000, 0xf000},
	.pwr_on_seq		= NULL,
	.pwr_off_seq		= NULL,
	.bb_table		= &rtw89_8852c_phy_bb_table,
	.bb_gain_table		= &rtw89_8852c_phy_bb_gain_table,
	.rf_table		= {&rtw89_8852c_phy_radiob_table,
				   &rtw89_8852c_phy_radioa_table,},
	.nctl_table		= &rtw89_8852c_phy_nctl_table,
	.nctl_post_table	= NULL,
	.dflt_parms		= &rtw89_8852c_dflt_parms,
	.rfe_parms_conf		= NULL,
	.chanctx_listener	= &rtw8852c_chanctx_listener,
	.txpwr_factor_rf	= 2,
	.txpwr_factor_mac	= 1,
	.dig_table		= NULL,
	.dig_regs		= &rtw8852c_dig_regs,
	.tssi_dbw_table		= &rtw89_8852c_tssi_dbw_table,
	.support_macid_num	= RTW89_MAX_MAC_ID_NUM,
	.support_link_num	= 0,
	.support_chanctx_num	= 2,
	.support_rnr		= false,
	.support_bands		= BIT(NL80211_BAND_2GHZ) |
				  BIT(NL80211_BAND_5GHZ) |
				  BIT(NL80211_BAND_6GHZ),
	.support_bandwidths	= BIT(NL80211_CHAN_WIDTH_20) |
				  BIT(NL80211_CHAN_WIDTH_40) |
				  BIT(NL80211_CHAN_WIDTH_80) |
				  BIT(NL80211_CHAN_WIDTH_160),
	.support_unii4		= true,
	.ul_tb_waveform_ctrl	= false,
	.ul_tb_pwr_diff		= true,
	.hw_sec_hdr		= true,
	.hw_mgmt_tx_encrypt	= true,
	.rf_path_num		= 2,
	.tx_nss			= 2,
	.rx_nss			= 2,
	.acam_num		= 128,
	.bcam_num		= 20,
	.scam_num		= 128,
	.bacam_num		= 8,
	.bacam_dynamic_num	= 8,
	.bacam_ver		= RTW89_BACAM_V0_EXT,
	.ppdu_max_usr		= 8,
	.sec_ctrl_efuse_size	= 4,
	.physical_efuse_size	= 1216,
	.logical_efuse_size	= 2048,
	.limit_efuse_size	= 1280,
	.dav_phy_efuse_size	= 96,
	.dav_log_efuse_size	= 16,
	.efuse_blocks		= NULL,
	.phycap_addr		= 0x590,
	.phycap_size		= 0x60,
	.para_ver		= 0x1,
	.wlcx_desired		= 0x06000000,
	.btcx_desired		= 0x7,
	.scbd			= 0x1,
	.mailbox		= 0x1,

	.afh_guard_ch		= 6,
	.wl_rssi_thres		= rtw89_btc_8852c_wl_rssi_thres,
	.bt_rssi_thres		= rtw89_btc_8852c_bt_rssi_thres,
	.rssi_tol		= 2,
	.mon_reg_num		= ARRAY_SIZE(rtw89_btc_8852c_mon_reg),
	.mon_reg		= rtw89_btc_8852c_mon_reg,
	.rf_para_ulink_num	= ARRAY_SIZE(rtw89_btc_8852c_rf_ul),
	.rf_para_ulink		= rtw89_btc_8852c_rf_ul,
	.rf_para_dlink_num	= ARRAY_SIZE(rtw89_btc_8852c_rf_dl),
	.rf_para_dlink		= rtw89_btc_8852c_rf_dl,
	.ps_mode_supported	= BIT(RTW89_PS_MODE_RFOFF) |
				  BIT(RTW89_PS_MODE_CLK_GATED) |
				  BIT(RTW89_PS_MODE_PWR_GATED),
	.low_power_hci_modes	= BIT(RTW89_PS_MODE_CLK_GATED) |
				  BIT(RTW89_PS_MODE_PWR_GATED),
	.h2c_cctl_func_id	= H2C_FUNC_MAC_CCTLINFO_UD_V1,
	.hci_func_en_addr	= R_AX_HCI_FUNC_EN_V1,
	.h2c_desc_size		= sizeof(struct rtw89_rxdesc_short),
	.txwd_body_size		= sizeof(struct rtw89_txwd_body_v1),
	.txwd_info_size		= sizeof(struct rtw89_txwd_info),
	.h2c_ctrl_reg		= R_AX_H2CREG_CTRL_V1,
	.h2c_counter_reg	= {R_AX_UDM1 + 1, B_AX_UDM1_HALMAC_H2C_DEQ_CNT_MASK >> 8},
	.h2c_regs		= rtw8852c_h2c_regs,
	.c2h_ctrl_reg		= R_AX_C2HREG_CTRL_V1,
	.c2h_counter_reg	= {R_AX_UDM1 + 1, B_AX_UDM1_HALMAC_C2H_ENQ_CNT_MASK >> 8},
	.c2h_regs		= rtw8852c_c2h_regs,
	.page_regs		= &rtw8852c_page_regs,
	.wow_reason_reg		= rtw8852c_wow_wakeup_regs,
	.cfo_src_fd		= false,
	.cfo_hw_comp            = false,
	.dcfo_comp		= &rtw8852c_dcfo_comp,
	.dcfo_comp_sft		= 12,
	.imr_info		= &rtw8852c_imr_info,
	.imr_dmac_table		= NULL,
	.imr_cmac_table		= NULL,
	.rrsr_cfgs		= &rtw8852c_rrsr_cfgs,
	.bss_clr_vld		= {R_BSS_CLR_MAP, B_BSS_CLR_MAP_VLD0},
	.bss_clr_map_reg	= R_BSS_CLR_MAP,
	.rfkill_init		= &rtw8852c_rfkill_regs,
	.rfkill_get		= {R_AX_GPIO_EXT_CTRL, B_AX_GPIO_IN_9},
	.dma_ch_mask		= 0,
	.edcca_regs		= &rtw8852c_edcca_regs,
#ifdef CONFIG_PM
	.wowlan_stub		= &rtw_wowlan_stub_8852c,
#endif
	.xtal_info		= NULL,
};
EXPORT_SYMBOL(rtw8852c_chip_info);

MODULE_FIRMWARE(RTW8852C_MODULE_FIRMWARE);
MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ax wireless 8852C driver");
MODULE_LICENSE("Dual BSD/GPL");
