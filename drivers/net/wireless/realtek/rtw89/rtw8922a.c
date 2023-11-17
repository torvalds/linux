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
