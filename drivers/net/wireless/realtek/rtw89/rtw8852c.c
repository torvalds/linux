// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2022  Realtek Corporation
 */

#include "coex.h"
#include "debug.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"
#include "rtw8852c.h"
#include "rtw8852c_table.h"

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

	rtw89_write32(rtwdev, R_AX_WLLPS_CTRL, 0x0001A0B0);
	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_XTAL_OFF_A_DIE);
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

	gain->offset_valid = valid;
}

static int rtw8852c_read_efuse(struct rtw89_dev *rtwdev, u8 *log_map)
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
				     struct rtw89_channel_params *param,
				     u8 mac_idx)
{
	u32 rf_mod = rtw89_mac_reg_by_idx(R_AX_WMAC_RFMOD, mac_idx);
	u32 sub_carr = rtw89_mac_reg_by_idx(R_AX_TX_SUB_CARRIER_VALUE,
					     mac_idx);
	u32 chk_rate = rtw89_mac_reg_by_idx(R_AX_TXRATE_CHK, mac_idx);
	u8 txsc20 = 0, txsc40 = 0, txsc80 = 0;
	u8 rf_mod_val = 0, chk_rate_mask = 0;
	u32 txsc;

	switch (param->bandwidth) {
	case RTW89_CHANNEL_WIDTH_160:
		txsc80 = rtw89_phy_get_txsc(rtwdev, param,
					    RTW89_CHANNEL_WIDTH_80);
		fallthrough;
	case RTW89_CHANNEL_WIDTH_80:
		txsc40 = rtw89_phy_get_txsc(rtwdev, param,
					    RTW89_CHANNEL_WIDTH_40);
		fallthrough;
	case RTW89_CHANNEL_WIDTH_40:
		txsc20 = rtw89_phy_get_txsc(rtwdev, param,
					    RTW89_CHANNEL_WIDTH_20);
		break;
	default:
		break;
	}

	switch (param->bandwidth) {
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

	switch (param->band_type) {
	case RTW89_BAND_2G:
		chk_rate_mask = B_AX_BAND_MODE;
		break;
	case RTW89_BAND_5G:
	case RTW89_BAND_6G:
		chk_rate_mask = B_AX_CHECK_CCK_EN | B_AX_RTS_LIMIT_IN_OFDM6;
		break;
	default:
		rtw89_warn(rtwdev, "Invalid band_type:%d\n", param->band_type);
		return;
	}
	rtw89_write8_clr(rtwdev, chk_rate, B_AX_BAND_MODE | B_AX_CHECK_CCK_EN |
					   B_AX_RTS_LIMIT_IN_OFDM6);
	rtw89_write8_set(rtwdev, chk_rate, chk_rate_mask);
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

static enum rtw89_phy_bb_gain_band
rtw8852c_mapping_gain_band(enum rtw89_subband subband)
{
	switch (subband) {
	default:
	case RTW89_CH_2G:
		return RTW89_BB_GAIN_BAND_2G;
	case RTW89_CH_5G_BAND_1:
		return RTW89_BB_GAIN_BAND_5G_L;
	case RTW89_CH_5G_BAND_3:
		return RTW89_BB_GAIN_BAND_5G_M;
	case RTW89_CH_5G_BAND_4:
		return RTW89_BB_GAIN_BAND_5G_H;
	case RTW89_CH_6G_BAND_IDX0:
	case RTW89_CH_6G_BAND_IDX1:
		return RTW89_BB_GAIN_BAND_6G_L;
	case RTW89_CH_6G_BAND_IDX2:
	case RTW89_CH_6G_BAND_IDX3:
		return RTW89_BB_GAIN_BAND_6G_M;
	case RTW89_CH_6G_BAND_IDX4:
	case RTW89_CH_6G_BAND_IDX5:
		return RTW89_BB_GAIN_BAND_6G_H;
	case RTW89_CH_6G_BAND_IDX6:
	case RTW89_CH_6G_BAND_IDX7:
		return RTW89_BB_GAIN_BAND_6G_UH;
	}
}

static void rtw8852c_set_gain_error(struct rtw89_dev *rtwdev,
				    enum rtw89_subband subband,
				    enum rtw89_rf_path path)
{
	const struct rtw89_phy_bb_gain_info *gain = &rtwdev->bb_gain;
	u8 gain_band = rtw8852c_mapping_gain_band(subband);
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
				     const struct rtw89_channel_params *param,
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

	if (param->band_type == RTW89_BAND_2G) {
		offset_q0 = efuse_gain->offset[path][RTW89_GAIN_OFFSET_2G_CCK];
		offset_base_q4 = efuse_gain->offset_base[phy_idx];

		tmp = clamp_t(s32, (-offset_q0 << 3) + (offset_base_q4 >> 1),
			      S8_MIN >> 1, S8_MAX >> 1);
		rtw89_phy_write32_mask(rtwdev, R_RPL_OFST, B_RPL_OFST_MASK, tmp & 0x7f);
	}

	switch (param->subband_type) {
	default:
	case RTW89_CH_2G:
		gain_band = RTW89_GAIN_OFFSET_2G_OFDM;
		break;
	case RTW89_CH_5G_BAND_1:
		gain_band = RTW89_GAIN_OFFSET_5G_LOW;
		break;
	case RTW89_CH_5G_BAND_3:
		gain_band = RTW89_GAIN_OFFSET_5G_MID;
		break;
	case RTW89_CH_5G_BAND_4:
		gain_band = RTW89_GAIN_OFFSET_5G_HIGH;
		break;
	}

	offset_q0 = -efuse_gain->offset[path][gain_band];
	offset_base_q4 = efuse_gain->offset_base[phy_idx];

	tmp = (offset_q0 << 2) + (offset_base_q4 >> 2);
	tmp = clamp_t(s32, -tmp, S8_MIN, S8_MAX);
	rtw89_phy_write32_mask(rtwdev, rssi_ofst_addr[path], B_PATH0_R_G_OFST_MASK, tmp & 0xff);

	tmp = clamp_t(s32, offset_q0 << 4, S8_MIN, S8_MAX);
	rtw89_phy_write32_idx(rtwdev, R_RPL_PATHAB, rpl_mask[path], tmp & 0xff, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_RSSI_M_PATHAB, rpl_tb_mask[path], tmp & 0xff, phy_idx);
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

static void rtw8852c_bb_reset_en(struct rtw89_dev *rtwdev,
				 enum rtw89_phy_idx phy_idx, bool en)
{
	struct rtw89_hal *hal = &rtwdev->hal;

	if (en) {
		rtw89_phy_write32_idx(rtwdev, R_S0_HW_SI_DIS,
				      B_S0_HW_SI_DIS_W_R_TRIG, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_S1_HW_SI_DIS,
				      B_S1_HW_SI_DIS_W_R_TRIG, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1,
				      phy_idx);
		if (hal->current_band_type == RTW89_BAND_2G)
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
		reg = rtw89_mac_reg_by_idx(R_AX_PWR_UL_TB_1T, mac_idx);
		rtw89_write32_mask(rtwdev, reg,
				   B_AX_PWR_UL_TB_1T_V1_MASK << (8 * i),
				   val_1t);
		/* 2TX */
		reg = rtw89_mac_reg_by_idx(R_AX_PWR_UL_TB_2T, mac_idx);
		rtw89_write32_mask(rtwdev, reg,
				   B_AX_PWR_UL_TB_2T_V1_MASK << (8 * i),
				   val_2t);
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
	struct rtw89_btc_module *module = &btc->mdinfo;
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
	if (module->ant.type == BTC_ANT_SHARED) {
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

static void rtw8852c_mac_disable_bb_rf(struct rtw89_dev *rtwdev)
{
	rtw89_write8_clr(rtwdev, R_AX_SYS_FUNC_EN,
			 B_AX_FEN_BBRSTB | B_AX_FEN_BB_GLB_RSTN);
}

static const struct rtw89_chip_ops rtw8852c_chip_ops = {
	.enable_bb_rf		= rtw8852c_mac_enable_bb_rf,
	.disable_bb_rf		= rtw8852c_mac_disable_bb_rf,
	.bb_reset		= rtw8852c_bb_reset,
	.bb_sethw		= rtw8852c_bb_sethw,
	.read_efuse		= rtw8852c_read_efuse,
	.read_phycap		= rtw8852c_read_phycap,
	.power_trim		= rtw8852c_power_trim,
	.read_rf		= rtw89_phy_read_rf_v1,
	.write_rf		= rtw89_phy_write_rf_v1,
	.set_txpwr_ul_tb_offset	= rtw8852c_set_txpwr_ul_tb_offset,
	.pwr_on_func		= rtw8852c_pwr_on_func,
	.pwr_off_func		= rtw8852c_pwr_off_func,
	.fill_txdesc		= rtw89_core_fill_txdesc_v1,
	.fill_txdesc_fwcmd	= rtw89_core_fill_txdesc_fwcmd_v1,
	.cfg_ctrl_path		= rtw89_mac_cfg_ctrl_path_v1,
	.mac_cfg_gnt		= rtw89_mac_cfg_gnt_v1,
	.stop_sch_tx		= rtw89_mac_stop_sch_tx_v1,
	.resume_sch_tx		= rtw89_mac_resume_sch_tx_v1,
	.h2c_dctl_sec_cam	= rtw89_fw_h2c_dctl_sec_cam_v1,

	.btc_init_cfg		= rtw8852c_btc_init_cfg,
};

const struct rtw89_chip_info rtw8852c_chip_info = {
	.chip_id		= RTL8852C,
	.ops			= &rtw8852c_chip_ops,
	.fw_name		= "rtw89/rtw8852c_fw.bin",
	.hfc_param_ini		= rtw8852c_hfc_param_ini_pcie,
	.dle_mem		= rtw8852c_dle_mem_pcie,
	.rf_base_addr		= {0xe000, 0xf000},
	.pwr_on_seq		= NULL,
	.pwr_off_seq		= NULL,
	.bb_table		= &rtw89_8852c_phy_bb_table,
	.bb_gain_table		= &rtw89_8852c_phy_bb_gain_table,
	.rf_table		= {&rtw89_8852c_phy_radiob_table,
				   &rtw89_8852c_phy_radioa_table,},
	.nctl_table		= &rtw89_8852c_phy_nctl_table,
	.byr_table		= &rtw89_8852c_byr_table,
	.txpwr_lmt_2g		= &rtw89_8852c_txpwr_lmt_2g,
	.txpwr_lmt_5g		= &rtw89_8852c_txpwr_lmt_5g,
	.txpwr_lmt_6g		= &rtw89_8852c_txpwr_lmt_6g,
	.txpwr_lmt_ru_2g	= &rtw89_8852c_txpwr_lmt_ru_2g,
	.txpwr_lmt_ru_5g	= &rtw89_8852c_txpwr_lmt_ru_5g,
	.txpwr_lmt_ru_6g	= &rtw89_8852c_txpwr_lmt_ru_6g,
	.txpwr_factor_rf	= 2,
	.txpwr_factor_mac	= 1,
	.dig_table		= NULL,
	.tssi_dbw_table		= &rtw89_8852c_tssi_dbw_table,
	.hw_sec_hdr		= true,
	.sec_ctrl_efuse_size	= 4,
	.physical_efuse_size	= 1216,
	.logical_efuse_size	= 2048,
	.limit_efuse_size	= 1280,
	.dav_phy_efuse_size	= 96,
	.dav_log_efuse_size	= 16,
	.phycap_addr		= 0x590,
	.phycap_size		= 0x60,
	.h2c_cctl_func_id	= H2C_FUNC_MAC_CCTLINFO_UD_V1,
	.hci_func_en_addr	= R_AX_HCI_FUNC_EN_V1,
	.h2c_desc_size		= sizeof(struct rtw89_rxdesc_short),
	.txwd_body_size		= sizeof(struct rtw89_txwd_body_v1),
	.h2c_ctrl_reg		= R_AX_H2CREG_CTRL_V1,
	.h2c_regs		= rtw8852c_h2c_regs,
	.c2h_ctrl_reg		= R_AX_C2HREG_CTRL_V1,
	.c2h_regs		= rtw8852c_c2h_regs,
	.page_regs		= &rtw8852c_page_regs,
	.dcfo_comp		= &rtw8852c_dcfo_comp,
	.dcfo_comp_sft		= 5,
	.imr_info		= &rtw8852c_imr_info
};
EXPORT_SYMBOL(rtw8852c_chip_info);

MODULE_FIRMWARE("rtw89/rtw8852c_fw.bin");
MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ax wireless 8852C driver");
MODULE_LICENSE("Dual BSD/GPL");
