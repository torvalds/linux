// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2023  Realtek Corporation
 */

#include "debug.h"
#include "efuse.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"
#include "rtw8922a.h"

#define RTW8922A_FW_FORMAT_MAX 0
#define RTW8922A_FW_BASENAME "rtw89/rtw8922a_fw"
#define RTW8922A_MODULE_FIRMWARE \
	RTW8922A_FW_BASENAME ".bin"

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
	[RTW89_QTA_DLFW] = {RTW89_QTA_DLFW, &rtw89_mac_size.wde_size4_v1,
			    &rtw89_mac_size.ple_size3_v1, &rtw89_mac_size.wde_qt4,
			    &rtw89_mac_size.wde_qt4, &rtw89_mac_size.ple_qt9,
			    &rtw89_mac_size.ple_qt9, &rtw89_mac_size.ple_rsvd_qt1,
			    &rtw89_mac_size.rsvd0_size0, &rtw89_mac_size.rsvd1_size0},
	[RTW89_QTA_INVALID] = {RTW89_QTA_INVALID, NULL, NULL, NULL, NULL, NULL,
			       NULL},
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

static int rtw8922a_read_phycap(struct rtw89_dev *rtwdev, u8 *phycap_map)
{
	rtw8922a_phycap_parsing_thermal_trim(rtwdev, phycap_map);
	rtw8922a_phycap_parsing_pa_bias_trim(rtwdev, phycap_map);
	rtw8922a_phycap_parsing_pad_bias_trim(rtwdev, phycap_map);

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
	.read_efuse		= rtw8922a_read_efuse,
	.read_phycap		= rtw8922a_read_phycap,
	.pwr_on_func		= rtw8922a_pwr_on_func,
	.pwr_off_func		= rtw8922a_pwr_off_func,
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
	.tssi_dbw_table		= NULL,
	.support_chanctx_num	= 1,
	.support_bands		= BIT(NL80211_BAND_2GHZ) |
				  BIT(NL80211_BAND_5GHZ) |
				  BIT(NL80211_BAND_6GHZ),
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
	.bacam_num		= 8,
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
	.hci_func_en_addr	= R_BE_HCI_FUNC_EN,
	.h2c_desc_size		= sizeof(struct rtw89_rxdesc_short_v2),
	.txwd_body_size		= sizeof(struct rtw89_txwd_body_v2),
	.txwd_info_size		= sizeof(struct rtw89_txwd_info_v2),
	.cfo_src_fd		= true,
	.cfo_hw_comp            = true,
	.dcfo_comp		= NULL,
	.dcfo_comp_sft		= 0,
	.imr_info		= NULL,
	.imr_dmac_table		= &rtw8922a_imr_dmac_table,
	.imr_cmac_table		= &rtw8922a_imr_cmac_table,
	.bss_clr_vld		= {R_BSS_CLR_VLD_V2, B_BSS_CLR_VLD0_V2},
	.bss_clr_map_reg	= R_BSS_CLR_MAP_V2,
	.dma_ch_mask		= 0,
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
