// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2022-2023  Realtek Corporation
 */

#include "coex.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"
#include "rtw8851b.h"
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

static const struct rtw89_xtal_info rtw8851b_xtal_info = {
	.xcap_reg		= R_AX_XTAL_ON_CTRL3,
	.sc_xo_mask		= B_AX_XTAL_SC_XO_A_BLOCK_MASK,
	.sc_xi_mask		= B_AX_XTAL_SC_XI_A_BLOCK_MASK,
};

static const struct rtw89_chip_ops rtw8851b_chip_ops = {
	.fem_setup		= NULL,
	.fill_txdesc		= rtw89_core_fill_txdesc,
	.fill_txdesc_fwcmd	= rtw89_core_fill_txdesc,
	.h2c_dctl_sec_cam	= NULL,
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
	.ops			= &rtw8851b_chip_ops,
	.fw_basename		= RTW8851B_FW_BASENAME,
	.fw_format_max		= RTW8851B_FW_FORMAT_MAX,
	.try_ce_fw		= true,
	.fifo_size		= 196608,
	.small_fifo_size	= true,
	.dle_scc_rsvd_size	= 98304,
	.max_amsdu_limit	= 3500,
	.dis_2g_40m_ul_ofdma	= true,
	.rsvd_ple_ofst		= 0x2f800,
	.hfc_param_ini		= rtw8851b_hfc_param_ini_pcie,
	.dle_mem		= rtw8851b_dle_mem_pcie,
	.wde_qempty_acq_num     = 4,
	.wde_qempty_mgq_sel     = 4,
	.rf_base_addr		= {0xe000},
	.pwr_on_seq		= NULL,
	.pwr_off_seq		= NULL,
	.bb_table		= &rtw89_8851b_phy_bb_table,
	.bb_gain_table		= &rtw89_8851b_phy_bb_gain_table,
	.rf_table		= {&rtw89_8851b_phy_radioa_table,},
	.nctl_table		= &rtw89_8851b_phy_nctl_table,
	.nctl_post_table	= &rtw8851b_nctl_post_defs_tbl,
	.byr_table		= &rtw89_8851b_byr_table,
	.dflt_parms		= &rtw89_8851b_dflt_parms,
	.rfe_parms_conf		= rtw89_8851b_rfe_parms_conf,
	.txpwr_factor_rf	= 2,
	.txpwr_factor_mac	= 1,
	.dig_table		= NULL,
	.tssi_dbw_table		= NULL,
	.support_chanctx_num	= 0,
	.support_bands		= BIT(NL80211_BAND_2GHZ) |
				  BIT(NL80211_BAND_5GHZ),
	.support_bw160		= false,
	.support_unii4		= true,
	.support_ul_tb_ctrl	= true,
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
	.sec_ctrl_efuse_size	= 4,
	.physical_efuse_size	= 1216,
	.logical_efuse_size	= 2048,
	.limit_efuse_size	= 1280,
	.dav_phy_efuse_size	= 0,
	.dav_log_efuse_size	= 0,
	.phycap_addr		= 0x580,
	.phycap_size		= 128,
	.para_ver		= 0,
	.wlcx_desired		= 0x06000000,
	.btcx_desired		= 0x7,
	.scbd			= 0x1,
	.mailbox		= 0x1,

	.ps_mode_supported	= BIT(RTW89_PS_MODE_RFOFF) |
				  BIT(RTW89_PS_MODE_CLK_GATED),
	.low_power_hci_modes	= 0,
	.h2c_cctl_func_id	= H2C_FUNC_MAC_CCTLINFO_UD,
	.hci_func_en_addr	= R_AX_HCI_FUNC_EN,
	.h2c_desc_size		= sizeof(struct rtw89_txwd_body),
	.txwd_body_size		= sizeof(struct rtw89_txwd_body),
	.bss_clr_map_reg	= R_BSS_CLR_MAP_V1,
	.dma_ch_mask		= BIT(RTW89_DMA_ACH4) | BIT(RTW89_DMA_ACH5) |
				  BIT(RTW89_DMA_ACH6) | BIT(RTW89_DMA_ACH7) |
				  BIT(RTW89_DMA_B1MG) | BIT(RTW89_DMA_B1HI),
	.edcca_lvl_reg		= R_SEG0R_EDCCA_LVL_V1,
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
