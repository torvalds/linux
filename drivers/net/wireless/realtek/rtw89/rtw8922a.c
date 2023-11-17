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

#ifdef CONFIG_PM
static const struct wiphy_wowlan_support rtw_wowlan_stub_8922a = {
	.flags = WIPHY_WOWLAN_MAGIC_PKT | WIPHY_WOWLAN_DISCONNECT,
	.n_patterns = RTW89_MAX_PATTERN_NUM,
	.pattern_max_len = RTW89_MAX_PATTERN_SIZE,
	.pattern_min_len = 1,
};
#endif

static const struct rtw89_chip_ops rtw8922a_chip_ops = {
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
