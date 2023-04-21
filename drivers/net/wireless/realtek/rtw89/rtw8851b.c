// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2022-2023  Realtek Corporation
 */

#include "coex.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"
#include "rtw8851b.h"
#include "rtw8851b_table.h"
#include "txrx.h"
#include "util.h"

#define RTW8851B_FW_FORMAT_MAX 0
#define RTW8851B_FW_BASENAME "rtw89/rtw8851b_fw"
#define RTW8851B_MODULE_FIRMWARE \
	RTW8851B_FW_BASENAME ".bin"

static const struct rtw89_chip_ops rtw8851b_chip_ops = {
	.fem_setup		= NULL,
	.fill_txdesc		= rtw89_core_fill_txdesc,
	.fill_txdesc_fwcmd	= rtw89_core_fill_txdesc,
	.h2c_dctl_sec_cam	= NULL,
};

const struct rtw89_chip_info rtw8851b_chip_info = {
	.chip_id		= RTL8851B,
	.ops			= &rtw8851b_chip_ops,
	.fw_basename		= RTW8851B_FW_BASENAME,
	.fw_format_max		= RTW8851B_FW_FORMAT_MAX,
	.try_ce_fw		= true,
	.fifo_size		= 196608,
	.dle_scc_rsvd_size	= 98304,
	.max_amsdu_limit	= 3500,
	.dis_2g_40m_ul_ofdma	= true,
	.rsvd_ple_ofst		= 0x2f800,
	.wde_qempty_acq_num     = 4,
	.wde_qempty_mgq_sel     = 4,
	.rf_base_addr		= {0xe000},
	.pwr_on_seq		= NULL,
	.pwr_off_seq		= NULL,
	.bb_table		= &rtw89_8851b_phy_bb_table,
	.bb_gain_table		= &rtw89_8851b_phy_bb_gain_table,
	.rf_table		= {&rtw89_8851b_phy_radioa_table,},
	.nctl_table		= &rtw89_8851b_phy_nctl_table,
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
	.bacam_v1		= false,
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
};
EXPORT_SYMBOL(rtw8851b_chip_info);

MODULE_FIRMWARE(RTW8851B_MODULE_FIRMWARE);
MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ax wireless 8851B driver");
MODULE_LICENSE("Dual BSD/GPL");
