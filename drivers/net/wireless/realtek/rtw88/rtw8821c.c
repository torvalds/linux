// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include "main.h"
#include "coex.h"
#include "fw.h"
#include "tx.h"
#include "rx.h"
#include "phy.h"
#include "rtw8821c.h"
#include "rtw8821c_table.h"
#include "mac.h"
#include "reg.h"
#include "debug.h"
#include "bf.h"
#include "regd.h"

static const s8 lna_gain_table_0[8] = {22, 8, -6, -22, -31, -40, -46, -52};
static const s8 lna_gain_table_1[16] = {10, 6, 2, -2, -6, -10, -14, -17,
					-20, -24, -28, -31, -34, -37, -40, -44};

static void rtw8821ce_efuse_parsing(struct rtw_efuse *efuse,
				    struct rtw8821c_efuse *map)
{
	ether_addr_copy(efuse->addr, map->e.mac_addr);
}

static void rtw8821cu_efuse_parsing(struct rtw_efuse *efuse,
				    struct rtw8821c_efuse *map)
{
	ether_addr_copy(efuse->addr, map->u.mac_addr);
}

static void rtw8821cs_efuse_parsing(struct rtw_efuse *efuse,
				    struct rtw8821c_efuse *map)
{
	ether_addr_copy(efuse->addr, map->s.mac_addr);
}

enum rtw8821ce_rf_set {
	SWITCH_TO_BTG,
	SWITCH_TO_WLG,
	SWITCH_TO_WLA,
	SWITCH_TO_BT,
};

static int rtw8821c_read_efuse(struct rtw_dev *rtwdev, u8 *log_map)
{
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw8821c_efuse *map;
	int i;

	map = (struct rtw8821c_efuse *)log_map;

	efuse->rfe_option = map->rfe_option & 0x1f;
	efuse->rf_board_option = map->rf_board_option;
	efuse->crystal_cap = map->xtal_k;
	efuse->pa_type_2g = map->pa_type;
	efuse->pa_type_5g = map->pa_type;
	efuse->lna_type_2g = map->lna_type_2g[0];
	efuse->lna_type_5g = map->lna_type_5g[0];
	efuse->channel_plan = map->channel_plan;
	efuse->country_code[0] = map->country_code[0];
	efuse->country_code[1] = map->country_code[1];
	efuse->bt_setting = map->rf_bt_setting;
	efuse->regd = map->rf_board_option & 0x7;
	efuse->thermal_meter[0] = map->thermal_meter;
	efuse->thermal_meter_k = map->thermal_meter;
	efuse->tx_bb_swing_setting_2g = map->tx_bb_swing_setting_2g;
	efuse->tx_bb_swing_setting_5g = map->tx_bb_swing_setting_5g;

	hal->pkg_type = map->rfe_option & BIT(5) ? 1 : 0;

	switch (efuse->rfe_option) {
	case 0x2:
	case 0x4:
	case 0x7:
	case 0xa:
	case 0xc:
	case 0xf:
		hal->rfe_btg = true;
		break;
	}

	for (i = 0; i < 4; i++)
		efuse->txpwr_idx_table[i] = map->txpwr_idx_table[i];

	if (rtwdev->efuse.rfe_option == 2 || rtwdev->efuse.rfe_option == 4)
		efuse->txpwr_idx_table[0].pwr_idx_2g = map->txpwr_idx_table[1].pwr_idx_2g;

	switch (rtw_hci_type(rtwdev)) {
	case RTW_HCI_TYPE_PCIE:
		rtw8821ce_efuse_parsing(efuse, map);
		break;
	case RTW_HCI_TYPE_USB:
		rtw8821cu_efuse_parsing(efuse, map);
		break;
	case RTW_HCI_TYPE_SDIO:
		rtw8821cs_efuse_parsing(efuse, map);
		break;
	default:
		/* unsupported now */
		return -ENOTSUPP;
	}

	return 0;
}

static const u32 rtw8821c_txscale_tbl[] = {
	0x081, 0x088, 0x090, 0x099, 0x0a2, 0x0ac, 0x0b6, 0x0c0, 0x0cc, 0x0d8,
	0x0e5, 0x0f2, 0x101, 0x110, 0x120, 0x131, 0x143, 0x156, 0x16a, 0x180,
	0x197, 0x1af, 0x1c8, 0x1e3, 0x200, 0x21e, 0x23e, 0x261, 0x285, 0x2ab,
	0x2d3, 0x2fe, 0x32b, 0x35c, 0x38e, 0x3c4, 0x3fe
};

static u8 rtw8821c_get_swing_index(struct rtw_dev *rtwdev)
{
	u8 i = 0;
	u32 swing, table_value;

	swing = rtw_read32_mask(rtwdev, REG_TXSCALE_A, 0xffe00000);
	for (i = 0; i < ARRAY_SIZE(rtw8821c_txscale_tbl); i++) {
		table_value = rtw8821c_txscale_tbl[i];
		if (swing == table_value)
			break;
	}

	return i;
}

static void rtw8821c_pwrtrack_init(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 swing_idx = rtw8821c_get_swing_index(rtwdev);

	if (swing_idx >= ARRAY_SIZE(rtw8821c_txscale_tbl))
		dm_info->default_ofdm_index = 24;
	else
		dm_info->default_ofdm_index = swing_idx;

	ewma_thermal_init(&dm_info->avg_thermal[RF_PATH_A]);
	dm_info->delta_power_index[RF_PATH_A] = 0;
	dm_info->delta_power_index_last[RF_PATH_A] = 0;
	dm_info->pwr_trk_triggered = false;
	dm_info->pwr_trk_init_trigger = true;
	dm_info->thermal_meter_k = rtwdev->efuse.thermal_meter_k;
}

static void rtw8821c_phy_bf_init(struct rtw_dev *rtwdev)
{
	rtw_bf_phy_init(rtwdev);
	/* Grouping bitmap parameters */
	rtw_write32(rtwdev, 0x1C94, 0xAFFFAFFF);
}

static void rtw8821c_phy_set_param(struct rtw_dev *rtwdev)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u8 crystal_cap, val;

	/* power on BB/RF domain */
	val = rtw_read8(rtwdev, REG_SYS_FUNC_EN);
	val |= BIT_FEN_PCIEA;
	rtw_write8(rtwdev, REG_SYS_FUNC_EN, val);

	/* toggle BB reset */
	val |= BIT_FEN_BB_RSTB | BIT_FEN_BB_GLB_RST;
	rtw_write8(rtwdev, REG_SYS_FUNC_EN, val);
	val &= ~(BIT_FEN_BB_RSTB | BIT_FEN_BB_GLB_RST);
	rtw_write8(rtwdev, REG_SYS_FUNC_EN, val);
	val |= BIT_FEN_BB_RSTB | BIT_FEN_BB_GLB_RST;
	rtw_write8(rtwdev, REG_SYS_FUNC_EN, val);

	rtw_write8(rtwdev, REG_RF_CTRL,
		   BIT_RF_EN | BIT_RF_RSTB | BIT_RF_SDM_RSTB);
	usleep_range(10, 11);
	rtw_write8(rtwdev, REG_WLRF1 + 3,
		   BIT_RF_EN | BIT_RF_RSTB | BIT_RF_SDM_RSTB);
	usleep_range(10, 11);

	/* pre init before header files config */
	rtw_write32_clr(rtwdev, REG_RXPSEL, BIT_RX_PSEL_RST);

	rtw_phy_load_tables(rtwdev);

	crystal_cap = rtwdev->efuse.crystal_cap & 0x3F;
	rtw_write32_mask(rtwdev, REG_AFE_XTAL_CTRL, 0x7e000000, crystal_cap);
	rtw_write32_mask(rtwdev, REG_AFE_PLL_CTRL, 0x7e, crystal_cap);
	rtw_write32_mask(rtwdev, REG_CCK0_FAREPORT, BIT(18) | BIT(22), 0);

	/* post init after header files config */
	rtw_write32_set(rtwdev, REG_RXPSEL, BIT_RX_PSEL_RST);
	hal->ch_param[0] = rtw_read32_mask(rtwdev, REG_TXSF2, MASKDWORD);
	hal->ch_param[1] = rtw_read32_mask(rtwdev, REG_TXSF6, MASKDWORD);
	hal->ch_param[2] = rtw_read32_mask(rtwdev, REG_TXFILTER, MASKDWORD);

	rtw_phy_init(rtwdev);
	rtwdev->dm_info.cck_pd_default = rtw_read8(rtwdev, REG_CSRATIO) & 0x1f;

	rtw8821c_pwrtrack_init(rtwdev);

	rtw8821c_phy_bf_init(rtwdev);
}

static int rtw8821c_mac_init(struct rtw_dev *rtwdev)
{
	u32 value32;
	u16 pre_txcnt;

	/* protocol configuration */
	rtw_write8(rtwdev, REG_AMPDU_MAX_TIME_V1, WLAN_AMPDU_MAX_TIME);
	rtw_write8_set(rtwdev, REG_TX_HANG_CTRL, BIT_EN_EOF_V1);
	pre_txcnt = WLAN_PRE_TXCNT_TIME_TH | BIT_EN_PRECNT;
	rtw_write8(rtwdev, REG_PRECNT_CTRL, (u8)(pre_txcnt & 0xFF));
	rtw_write8(rtwdev, REG_PRECNT_CTRL + 1, (u8)(pre_txcnt >> 8));
	value32 = WLAN_RTS_LEN_TH | (WLAN_RTS_TX_TIME_TH << 8) |
		  (WLAN_MAX_AGG_PKT_LIMIT << 16) |
		  (WLAN_RTS_MAX_AGG_PKT_LIMIT << 24);
	rtw_write32(rtwdev, REG_PROT_MODE_CTRL, value32);
	rtw_write16(rtwdev, REG_BAR_MODE_CTRL + 2,
		    WLAN_BAR_RETRY_LIMIT | WLAN_RA_TRY_RATE_AGG_LIMIT << 8);
	rtw_write8(rtwdev, REG_FAST_EDCA_VOVI_SETTING, FAST_EDCA_VO_TH);
	rtw_write8(rtwdev, REG_FAST_EDCA_VOVI_SETTING + 2, FAST_EDCA_VI_TH);
	rtw_write8(rtwdev, REG_FAST_EDCA_BEBK_SETTING, FAST_EDCA_BE_TH);
	rtw_write8(rtwdev, REG_FAST_EDCA_BEBK_SETTING + 2, FAST_EDCA_BK_TH);
	rtw_write8_set(rtwdev, REG_INIRTS_RATE_SEL, BIT(5));

	/* EDCA configuration */
	rtw_write8_clr(rtwdev, REG_TIMER0_SRC_SEL, BIT_TSFT_SEL_TIMER0);
	rtw_write16(rtwdev, REG_TXPAUSE, 0);
	rtw_write8(rtwdev, REG_SLOT, WLAN_SLOT_TIME);
	rtw_write8(rtwdev, REG_PIFS, WLAN_PIFS_TIME);
	rtw_write32(rtwdev, REG_SIFS, WLAN_SIFS_CFG);
	rtw_write16(rtwdev, REG_EDCA_VO_PARAM + 2, WLAN_VO_TXOP_LIMIT);
	rtw_write16(rtwdev, REG_EDCA_VI_PARAM + 2, WLAN_VI_TXOP_LIMIT);
	rtw_write32(rtwdev, REG_RD_NAV_NXT, WLAN_NAV_CFG);
	rtw_write16(rtwdev, REG_RXTSF_OFFSET_CCK, WLAN_RX_TSF_CFG);

	/* Set beacon cotnrol - enable TSF and other related functions */
	rtw_write8_set(rtwdev, REG_BCN_CTRL, BIT_EN_BCN_FUNCTION);

	/* Set send beacon related registers */
	rtw_write32(rtwdev, REG_TBTT_PROHIBIT, WLAN_TBTT_TIME);
	rtw_write8(rtwdev, REG_DRVERLYINT, WLAN_DRV_EARLY_INT);
	rtw_write8(rtwdev, REG_BCNDMATIM, WLAN_BCN_DMA_TIME);
	rtw_write8_clr(rtwdev, REG_TX_PTCL_CTRL + 1, BIT_SIFS_BK_EN >> 8);

	/* WMAC configuration */
	rtw_write32(rtwdev, REG_RXFLTMAP0, WLAN_RX_FILTER0);
	rtw_write16(rtwdev, REG_RXFLTMAP2, WLAN_RX_FILTER2);
	rtw_write32(rtwdev, REG_RCR, WLAN_RCR_CFG);
	rtw_write8(rtwdev, REG_RX_PKT_LIMIT, WLAN_RXPKT_MAX_SZ_512);
	rtw_write8(rtwdev, REG_TCR + 2, WLAN_TX_FUNC_CFG2);
	rtw_write8(rtwdev, REG_TCR + 1, WLAN_TX_FUNC_CFG1);
	rtw_write8(rtwdev, REG_ACKTO_CCK, 0x40);
	rtw_write8_set(rtwdev, REG_WMAC_TRXPTCL_CTL_H, BIT(1));
	rtw_write8_set(rtwdev, REG_SND_PTCL_CTRL,
		       BIT_DIS_CHK_VHTSIGB_CRC);
	rtw_write32(rtwdev, REG_WMAC_OPTION_FUNCTION + 8, WLAN_MAC_OPT_FUNC2);
	rtw_write8(rtwdev, REG_WMAC_OPTION_FUNCTION + 4, WLAN_MAC_OPT_NORM_FUNC1);

	return 0;
}

static void rtw8821c_cfg_ldo25(struct rtw_dev *rtwdev, bool enable)
{
	u8 ldo_pwr;

	ldo_pwr = rtw_read8(rtwdev, REG_LDO_EFUSE_CTRL + 3);
	ldo_pwr = enable ? ldo_pwr | BIT(7) : ldo_pwr & ~BIT(7);
	rtw_write8(rtwdev, REG_LDO_EFUSE_CTRL + 3, ldo_pwr);
}

static void rtw8821c_switch_rf_set(struct rtw_dev *rtwdev, u8 rf_set)
{
	u32 reg;

	rtw_write32_set(rtwdev, REG_DMEM_CTRL, BIT_WL_RST);
	rtw_write32_set(rtwdev, REG_SYS_CTRL, BIT_FEN_EN);

	reg = rtw_read32(rtwdev, REG_RFECTL);
	switch (rf_set) {
	case SWITCH_TO_BTG:
		reg |= B_BTG_SWITCH;
		reg &= ~(B_CTRL_SWITCH | B_WL_SWITCH | B_WLG_SWITCH |
			 B_WLA_SWITCH);
		rtw_write32_mask(rtwdev, REG_ENRXCCA, MASKBYTE2, BTG_CCA);
		rtw_write32_mask(rtwdev, REG_ENTXCCK, MASKLWORD, BTG_LNA);
		break;
	case SWITCH_TO_WLG:
		reg |= B_WL_SWITCH | B_WLG_SWITCH;
		reg &= ~(B_BTG_SWITCH | B_CTRL_SWITCH | B_WLA_SWITCH);
		rtw_write32_mask(rtwdev, REG_ENRXCCA, MASKBYTE2, WLG_CCA);
		rtw_write32_mask(rtwdev, REG_ENTXCCK, MASKLWORD, WLG_LNA);
		break;
	case SWITCH_TO_WLA:
		reg |= B_WL_SWITCH | B_WLA_SWITCH;
		reg &= ~(B_BTG_SWITCH | B_CTRL_SWITCH | B_WLG_SWITCH);
		break;
	case SWITCH_TO_BT:
	default:
		break;
	}

	rtw_write32(rtwdev, REG_RFECTL, reg);
}

static void rtw8821c_set_channel_rf(struct rtw_dev *rtwdev, u8 channel, u8 bw)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u32 rf_reg18;

	rf_reg18 = rtw_read_rf(rtwdev, RF_PATH_A, 0x18, RFREG_MASK);

	rf_reg18 &= ~(RF18_BAND_MASK | RF18_CHANNEL_MASK | RF18_RFSI_MASK |
		      RF18_BW_MASK);

	rf_reg18 |= (channel <= 14 ? RF18_BAND_2G : RF18_BAND_5G);
	rf_reg18 |= (channel & RF18_CHANNEL_MASK);

	if (channel >= 100 && channel <= 140)
		rf_reg18 |= RF18_RFSI_GE;
	else if (channel > 140)
		rf_reg18 |= RF18_RFSI_GT;

	switch (bw) {
	case RTW_CHANNEL_WIDTH_5:
	case RTW_CHANNEL_WIDTH_10:
	case RTW_CHANNEL_WIDTH_20:
	default:
		rf_reg18 |= RF18_BW_20M;
		break;
	case RTW_CHANNEL_WIDTH_40:
		rf_reg18 |= RF18_BW_40M;
		break;
	case RTW_CHANNEL_WIDTH_80:
		rf_reg18 |= RF18_BW_80M;
		break;
	}

	if (channel <= 14) {
		if (hal->rfe_btg)
			rtw8821c_switch_rf_set(rtwdev, SWITCH_TO_BTG);
		else
			rtw8821c_switch_rf_set(rtwdev, SWITCH_TO_WLG);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTDBG, BIT(6), 0x1);
		rtw_write_rf(rtwdev, RF_PATH_A, 0x64, 0xf, 0xf);
	} else {
		rtw8821c_switch_rf_set(rtwdev, SWITCH_TO_WLA);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTDBG, BIT(6), 0x0);
	}

	rtw_write_rf(rtwdev, RF_PATH_A, 0x18, RFREG_MASK, rf_reg18);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_XTALX2, BIT(19), 0);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_XTALX2, BIT(19), 1);
}

static void rtw8821c_set_channel_rxdfir(struct rtw_dev *rtwdev, u8 bw)
{
	if (bw == RTW_CHANNEL_WIDTH_40) {
		/* RX DFIR for BW40 */
		rtw_write32_mask(rtwdev, REG_ACBB0, BIT(29) | BIT(28), 0x2);
		rtw_write32_mask(rtwdev, REG_ACBBRXFIR, BIT(29) | BIT(28), 0x2);
		rtw_write32_mask(rtwdev, REG_TXDFIR, BIT(31), 0x0);
		rtw_write32_mask(rtwdev, REG_CHFIR, BIT(31), 0x0);
	} else if (bw == RTW_CHANNEL_WIDTH_80) {
		/* RX DFIR for BW80 */
		rtw_write32_mask(rtwdev, REG_ACBB0, BIT(29) | BIT(28), 0x2);
		rtw_write32_mask(rtwdev, REG_ACBBRXFIR, BIT(29) | BIT(28), 0x1);
		rtw_write32_mask(rtwdev, REG_TXDFIR, BIT(31), 0x0);
		rtw_write32_mask(rtwdev, REG_CHFIR, BIT(31), 0x1);
	} else {
		/* RX DFIR for BW20, BW10 and BW5 */
		rtw_write32_mask(rtwdev, REG_ACBB0, BIT(29) | BIT(28), 0x2);
		rtw_write32_mask(rtwdev, REG_ACBBRXFIR, BIT(29) | BIT(28), 0x2);
		rtw_write32_mask(rtwdev, REG_TXDFIR, BIT(31), 0x1);
		rtw_write32_mask(rtwdev, REG_CHFIR, BIT(31), 0x0);
	}
}

static void rtw8821c_cck_tx_filter_srrc(struct rtw_dev *rtwdev, u8 channel, u8 bw)
{
	struct rtw_hal *hal = &rtwdev->hal;

	if (channel == 14) {
		rtw_write32_mask(rtwdev, REG_CCA_FLTR, MASKHWORD, 0xe82c);
		rtw_write32_mask(rtwdev, REG_TXSF2, MASKDWORD, 0x0000b81c);
		rtw_write32_mask(rtwdev, REG_TXSF6, MASKLWORD, 0x0000);
		rtw_write32_mask(rtwdev, REG_TXFILTER, MASKDWORD, 0x00003667);

		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE2, RFREG_MASK, 0x00002);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0001e);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0001c);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0000e);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0000c);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE2, RFREG_MASK, 0x00000);
	} else if (channel == 13 ||
		   (channel == 11 && bw == RTW_CHANNEL_WIDTH_40)) {
		rtw_write32_mask(rtwdev, REG_CCA_FLTR, MASKHWORD, 0xf8fe);
		rtw_write32_mask(rtwdev, REG_TXSF2, MASKDWORD, 0x64b80c1c);
		rtw_write32_mask(rtwdev, REG_TXSF6, MASKLWORD, 0x8810);
		rtw_write32_mask(rtwdev, REG_TXFILTER, MASKDWORD, 0x01235667);

		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE2, RFREG_MASK, 0x00002);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0001e);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00027);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0001c);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00027);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0000e);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00029);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0000c);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00026);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE2, RFREG_MASK, 0x00000);
	} else {
		rtw_write32_mask(rtwdev, REG_CCA_FLTR, MASKHWORD, 0xe82c);
		rtw_write32_mask(rtwdev, REG_TXSF2, MASKDWORD,
				 hal->ch_param[0]);
		rtw_write32_mask(rtwdev, REG_TXSF6, MASKLWORD,
				 hal->ch_param[1] & MASKLWORD);
		rtw_write32_mask(rtwdev, REG_TXFILTER, MASKDWORD,
				 hal->ch_param[2]);

		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE2, RFREG_MASK, 0x00002);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0001e);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0001c);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0000e);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0000c);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE2, RFREG_MASK, 0x00000);
	}
}

static void rtw8821c_set_channel_bb(struct rtw_dev *rtwdev, u8 channel, u8 bw,
				    u8 primary_ch_idx)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u32 val32;

	if (channel <= 14) {
		rtw_write32_mask(rtwdev, REG_RXPSEL, BIT(28), 0x1);
		rtw_write32_mask(rtwdev, REG_CCK_CHECK, BIT(7), 0x0);
		rtw_write32_mask(rtwdev, REG_ENTXCCK, BIT(18), 0x0);
		rtw_write32_mask(rtwdev, REG_RXCCAMSK, 0x0000FC00, 15);

		rtw_write32_mask(rtwdev, REG_TXSCALE_A, 0xf00, 0x0);
		rtw_write32_mask(rtwdev, REG_CLKTRK, 0x1ffe0000, 0x96a);

		if (rtw_regd_srrc(rtwdev)) {
			rtw8821c_cck_tx_filter_srrc(rtwdev, channel, bw);
			goto set_bw;
		}

		/* CCK TX filter parameters for default case */
		if (channel == 14) {
			rtw_write32_mask(rtwdev, REG_TXSF2, MASKDWORD, 0x0000b81c);
			rtw_write32_mask(rtwdev, REG_TXSF6, MASKLWORD, 0x0000);
			rtw_write32_mask(rtwdev, REG_TXFILTER, MASKDWORD, 0x00003667);
		} else {
			rtw_write32_mask(rtwdev, REG_TXSF2, MASKDWORD,
					 hal->ch_param[0]);
			rtw_write32_mask(rtwdev, REG_TXSF6, MASKLWORD,
					 hal->ch_param[1] & MASKLWORD);
			rtw_write32_mask(rtwdev, REG_TXFILTER, MASKDWORD,
					 hal->ch_param[2]);
		}
	} else if (channel > 35) {
		rtw_write32_mask(rtwdev, REG_ENTXCCK, BIT(18), 0x1);
		rtw_write32_mask(rtwdev, REG_CCK_CHECK, BIT(7), 0x1);
		rtw_write32_mask(rtwdev, REG_RXPSEL, BIT(28), 0x0);
		rtw_write32_mask(rtwdev, REG_RXCCAMSK, 0x0000FC00, 15);

		if (channel >= 36 && channel <= 64)
			rtw_write32_mask(rtwdev, REG_TXSCALE_A, 0xf00, 0x1);
		else if (channel >= 100 && channel <= 144)
			rtw_write32_mask(rtwdev, REG_TXSCALE_A, 0xf00, 0x2);
		else if (channel >= 149)
			rtw_write32_mask(rtwdev, REG_TXSCALE_A, 0xf00, 0x3);

		if (channel >= 36 && channel <= 48)
			rtw_write32_mask(rtwdev, REG_CLKTRK, 0x1ffe0000, 0x494);
		else if (channel >= 52 && channel <= 64)
			rtw_write32_mask(rtwdev, REG_CLKTRK, 0x1ffe0000, 0x453);
		else if (channel >= 100 && channel <= 116)
			rtw_write32_mask(rtwdev, REG_CLKTRK, 0x1ffe0000, 0x452);
		else if (channel >= 118 && channel <= 177)
			rtw_write32_mask(rtwdev, REG_CLKTRK, 0x1ffe0000, 0x412);
	}

set_bw:
	switch (bw) {
	case RTW_CHANNEL_WIDTH_20:
	default:
		val32 = rtw_read32_mask(rtwdev, REG_ADCCLK, MASKDWORD);
		val32 &= 0xffcffc00;
		val32 |= 0x10010000;
		rtw_write32_mask(rtwdev, REG_ADCCLK, MASKDWORD, val32);

		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0x1);
		break;
	case RTW_CHANNEL_WIDTH_40:
		if (primary_ch_idx == 1)
			rtw_write32_set(rtwdev, REG_RXSB, BIT(4));
		else
			rtw_write32_clr(rtwdev, REG_RXSB, BIT(4));

		val32 = rtw_read32_mask(rtwdev, REG_ADCCLK, MASKDWORD);
		val32 &= 0xff3ff300;
		val32 |= 0x20020000 | ((primary_ch_idx & 0xf) << 2) |
			 RTW_CHANNEL_WIDTH_40;
		rtw_write32_mask(rtwdev, REG_ADCCLK, MASKDWORD, val32);

		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0x1);
		break;
	case RTW_CHANNEL_WIDTH_80:
		val32 = rtw_read32_mask(rtwdev, REG_ADCCLK, MASKDWORD);
		val32 &= 0xfcffcf00;
		val32 |= 0x40040000 | ((primary_ch_idx & 0xf) << 2) |
			 RTW_CHANNEL_WIDTH_80;
		rtw_write32_mask(rtwdev, REG_ADCCLK, MASKDWORD, val32);

		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0x1);
		break;
	case RTW_CHANNEL_WIDTH_5:
		val32 = rtw_read32_mask(rtwdev, REG_ADCCLK, MASKDWORD);
		val32 &= 0xefcefc00;
		val32 |= 0x200240;
		rtw_write32_mask(rtwdev, REG_ADCCLK, MASKDWORD, val32);

		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0x0);
		rtw_write32_mask(rtwdev, REG_ADC40, BIT(31), 0x1);
		break;
	case RTW_CHANNEL_WIDTH_10:
		val32 = rtw_read32_mask(rtwdev, REG_ADCCLK, MASKDWORD);
		val32 &= 0xefcefc00;
		val32 |= 0x300380;
		rtw_write32_mask(rtwdev, REG_ADCCLK, MASKDWORD, val32);

		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0x0);
		rtw_write32_mask(rtwdev, REG_ADC40, BIT(31), 0x1);
		break;
	}
}

static u32 rtw8821c_get_bb_swing(struct rtw_dev *rtwdev, u8 channel)
{
	struct rtw_efuse efuse = rtwdev->efuse;
	u8 tx_bb_swing;
	u32 swing2setting[4] = {0x200, 0x16a, 0x101, 0x0b6};

	tx_bb_swing = channel <= 14 ? efuse.tx_bb_swing_setting_2g :
				      efuse.tx_bb_swing_setting_5g;
	if (tx_bb_swing > 9)
		tx_bb_swing = 0;

	return swing2setting[(tx_bb_swing / 3)];
}

static void rtw8821c_set_channel_bb_swing(struct rtw_dev *rtwdev, u8 channel,
					  u8 bw, u8 primary_ch_idx)
{
	rtw_write32_mask(rtwdev, REG_TXSCALE_A, GENMASK(31, 21),
			 rtw8821c_get_bb_swing(rtwdev, channel));
	rtw8821c_pwrtrack_init(rtwdev);
}

static void rtw8821c_set_channel(struct rtw_dev *rtwdev, u8 channel, u8 bw,
				 u8 primary_chan_idx)
{
	rtw8821c_set_channel_bb(rtwdev, channel, bw, primary_chan_idx);
	rtw8821c_set_channel_bb_swing(rtwdev, channel, bw, primary_chan_idx);
	rtw_set_channel_mac(rtwdev, channel, bw, primary_chan_idx);
	rtw8821c_set_channel_rf(rtwdev, channel, bw);
	rtw8821c_set_channel_rxdfir(rtwdev, bw);
}

static s8 get_cck_rx_pwr(struct rtw_dev *rtwdev, u8 lna_idx, u8 vga_idx)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	const s8 *lna_gain_table;
	int lna_gain_table_size;
	s8 rx_pwr_all = 0;
	s8 lna_gain = 0;

	if (efuse->rfe_option == 0) {
		lna_gain_table = lna_gain_table_0;
		lna_gain_table_size = ARRAY_SIZE(lna_gain_table_0);
	} else {
		lna_gain_table = lna_gain_table_1;
		lna_gain_table_size = ARRAY_SIZE(lna_gain_table_1);
	}

	if (lna_idx >= lna_gain_table_size) {
		rtw_warn(rtwdev, "incorrect lna index (%d)\n", lna_idx);
		return -120;
	}

	lna_gain = lna_gain_table[lna_idx];
	rx_pwr_all = lna_gain - 2 * vga_idx;

	return rx_pwr_all;
}

static void query_phy_status_page0(struct rtw_dev *rtwdev, u8 *phy_status,
				   struct rtw_rx_pkt_stat *pkt_stat)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	s8 rx_power;
	u8 lna_idx = 0;
	u8 vga_idx = 0;

	vga_idx = GET_PHY_STAT_P0_VGA(phy_status);
	lna_idx = FIELD_PREP(BIT_LNA_H_MASK, GET_PHY_STAT_P0_LNA_H(phy_status)) |
		  FIELD_PREP(BIT_LNA_L_MASK, GET_PHY_STAT_P0_LNA_L(phy_status));
	rx_power = get_cck_rx_pwr(rtwdev, lna_idx, vga_idx);

	pkt_stat->rx_power[RF_PATH_A] = rx_power;
	pkt_stat->rssi = rtw_phy_rf_power_2_rssi(pkt_stat->rx_power, 1);
	dm_info->rssi[RF_PATH_A] = pkt_stat->rssi;
	pkt_stat->bw = RTW_CHANNEL_WIDTH_20;
	pkt_stat->signal_power = rx_power;
}

static void query_phy_status_page1(struct rtw_dev *rtwdev, u8 *phy_status,
				   struct rtw_rx_pkt_stat *pkt_stat)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 rxsc, bw;
	s8 min_rx_power = -120;

	if (pkt_stat->rate > DESC_RATE11M && pkt_stat->rate < DESC_RATEMCS0)
		rxsc = GET_PHY_STAT_P1_L_RXSC(phy_status);
	else
		rxsc = GET_PHY_STAT_P1_HT_RXSC(phy_status);

	if (rxsc >= 1 && rxsc <= 8)
		bw = RTW_CHANNEL_WIDTH_20;
	else if (rxsc >= 9 && rxsc <= 12)
		bw = RTW_CHANNEL_WIDTH_40;
	else if (rxsc >= 13)
		bw = RTW_CHANNEL_WIDTH_80;
	else
		bw = GET_PHY_STAT_P1_RF_MODE(phy_status);

	pkt_stat->rx_power[RF_PATH_A] = GET_PHY_STAT_P1_PWDB_A(phy_status) - 110;
	pkt_stat->rssi = rtw_phy_rf_power_2_rssi(pkt_stat->rx_power, 1);
	dm_info->rssi[RF_PATH_A] = pkt_stat->rssi;
	pkt_stat->bw = bw;
	pkt_stat->signal_power = max(pkt_stat->rx_power[RF_PATH_A],
				     min_rx_power);
}

static void query_phy_status(struct rtw_dev *rtwdev, u8 *phy_status,
			     struct rtw_rx_pkt_stat *pkt_stat)
{
	u8 page;

	page = *phy_status & 0xf;

	switch (page) {
	case 0:
		query_phy_status_page0(rtwdev, phy_status, pkt_stat);
		break;
	case 1:
		query_phy_status_page1(rtwdev, phy_status, pkt_stat);
		break;
	default:
		rtw_warn(rtwdev, "unused phy status page (%d)\n", page);
		return;
	}
}

static void rtw8821c_query_rx_desc(struct rtw_dev *rtwdev, u8 *rx_desc,
				   struct rtw_rx_pkt_stat *pkt_stat,
				   struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_hdr *hdr;
	u32 desc_sz = rtwdev->chip->rx_pkt_desc_sz;
	u8 *phy_status = NULL;

	memset(pkt_stat, 0, sizeof(*pkt_stat));

	pkt_stat->phy_status = GET_RX_DESC_PHYST(rx_desc);
	pkt_stat->icv_err = GET_RX_DESC_ICV_ERR(rx_desc);
	pkt_stat->crc_err = GET_RX_DESC_CRC32(rx_desc);
	pkt_stat->decrypted = !GET_RX_DESC_SWDEC(rx_desc) &&
			      GET_RX_DESC_ENC_TYPE(rx_desc) != RX_DESC_ENC_NONE;
	pkt_stat->is_c2h = GET_RX_DESC_C2H(rx_desc);
	pkt_stat->pkt_len = GET_RX_DESC_PKT_LEN(rx_desc);
	pkt_stat->drv_info_sz = GET_RX_DESC_DRV_INFO_SIZE(rx_desc);
	pkt_stat->shift = GET_RX_DESC_SHIFT(rx_desc);
	pkt_stat->rate = GET_RX_DESC_RX_RATE(rx_desc);
	pkt_stat->cam_id = GET_RX_DESC_MACID(rx_desc);
	pkt_stat->ppdu_cnt = GET_RX_DESC_PPDU_CNT(rx_desc);
	pkt_stat->tsf_low = GET_RX_DESC_TSFL(rx_desc);

	/* drv_info_sz is in unit of 8-bytes */
	pkt_stat->drv_info_sz *= 8;

	/* c2h cmd pkt's rx/phy status is not interested */
	if (pkt_stat->is_c2h)
		return;

	hdr = (struct ieee80211_hdr *)(rx_desc + desc_sz + pkt_stat->shift +
				       pkt_stat->drv_info_sz);
	if (pkt_stat->phy_status) {
		phy_status = rx_desc + desc_sz + pkt_stat->shift;
		query_phy_status(rtwdev, phy_status, pkt_stat);
	}

	rtw_rx_fill_rx_status(rtwdev, pkt_stat, hdr, rx_status, phy_status);
}

static void
rtw8821c_set_tx_power_index_by_rate(struct rtw_dev *rtwdev, u8 path, u8 rs)
{
	struct rtw_hal *hal = &rtwdev->hal;
	static const u32 offset_txagc[2] = {0x1d00, 0x1d80};
	static u32 phy_pwr_idx;
	u8 rate, rate_idx, pwr_index, shift;
	int j;

	for (j = 0; j < rtw_rate_size[rs]; j++) {
		rate = rtw_rate_section[rs][j];
		pwr_index = hal->tx_pwr_tbl[path][rate];
		shift = rate & 0x3;
		phy_pwr_idx |= ((u32)pwr_index << (shift * 8));
		if (shift == 0x3 || rate == DESC_RATEVHT1SS_MCS9) {
			rate_idx = rate & 0xfc;
			rtw_write32(rtwdev, offset_txagc[path] + rate_idx,
				    phy_pwr_idx);
			phy_pwr_idx = 0;
		}
	}
}

static void rtw8821c_set_tx_power_index(struct rtw_dev *rtwdev)
{
	struct rtw_hal *hal = &rtwdev->hal;
	int rs, path;

	for (path = 0; path < hal->rf_path_num; path++) {
		for (rs = 0; rs < RTW_RATE_SECTION_MAX; rs++) {
			if (rs == RTW_RATE_SECTION_HT_2S ||
			    rs == RTW_RATE_SECTION_VHT_2S)
				continue;
			rtw8821c_set_tx_power_index_by_rate(rtwdev, path, rs);
		}
	}
}

static void rtw8821c_false_alarm_statistics(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u32 cck_enable;
	u32 cck_fa_cnt;
	u32 ofdm_fa_cnt;
	u32 crc32_cnt;
	u32 cca32_cnt;

	cck_enable = rtw_read32(rtwdev, REG_RXPSEL) & BIT(28);
	cck_fa_cnt = rtw_read16(rtwdev, REG_FA_CCK);
	ofdm_fa_cnt = rtw_read16(rtwdev, REG_FA_OFDM);

	dm_info->cck_fa_cnt = cck_fa_cnt;
	dm_info->ofdm_fa_cnt = ofdm_fa_cnt;
	if (cck_enable)
		dm_info->total_fa_cnt += cck_fa_cnt;
	dm_info->total_fa_cnt = ofdm_fa_cnt;

	crc32_cnt = rtw_read32(rtwdev, REG_CRC_CCK);
	dm_info->cck_ok_cnt = FIELD_GET(GENMASK(15, 0), crc32_cnt);
	dm_info->cck_err_cnt = FIELD_GET(GENMASK(31, 16), crc32_cnt);

	crc32_cnt = rtw_read32(rtwdev, REG_CRC_OFDM);
	dm_info->ofdm_ok_cnt = FIELD_GET(GENMASK(15, 0), crc32_cnt);
	dm_info->ofdm_err_cnt = FIELD_GET(GENMASK(31, 16), crc32_cnt);

	crc32_cnt = rtw_read32(rtwdev, REG_CRC_HT);
	dm_info->ht_ok_cnt = FIELD_GET(GENMASK(15, 0), crc32_cnt);
	dm_info->ht_err_cnt = FIELD_GET(GENMASK(31, 16), crc32_cnt);

	crc32_cnt = rtw_read32(rtwdev, REG_CRC_VHT);
	dm_info->vht_ok_cnt = FIELD_GET(GENMASK(15, 0), crc32_cnt);
	dm_info->vht_err_cnt = FIELD_GET(GENMASK(31, 16), crc32_cnt);

	cca32_cnt = rtw_read32(rtwdev, REG_CCA_OFDM);
	dm_info->ofdm_cca_cnt = FIELD_GET(GENMASK(31, 16), cca32_cnt);
	dm_info->total_cca_cnt = dm_info->ofdm_cca_cnt;
	if (cck_enable) {
		cca32_cnt = rtw_read32(rtwdev, REG_CCA_CCK);
		dm_info->cck_cca_cnt = FIELD_GET(GENMASK(15, 0), cca32_cnt);
		dm_info->total_cca_cnt += dm_info->cck_cca_cnt;
	}

	rtw_write32_set(rtwdev, REG_FAS, BIT(17));
	rtw_write32_clr(rtwdev, REG_FAS, BIT(17));
	rtw_write32_clr(rtwdev, REG_RXDESC, BIT(15));
	rtw_write32_set(rtwdev, REG_RXDESC, BIT(15));
	rtw_write32_set(rtwdev, REG_CNTRST, BIT(0));
	rtw_write32_clr(rtwdev, REG_CNTRST, BIT(0));
}

static void rtw8821c_do_iqk(struct rtw_dev *rtwdev)
{
	static int do_iqk_cnt;
	struct rtw_iqk_para para = {.clear = 0, .segment_iqk = 0};
	u32 rf_reg, iqk_fail_mask;
	int counter;
	bool reload;

	if (rtw_is_assoc(rtwdev))
		para.segment_iqk = 1;

	rtw_fw_do_iqk(rtwdev, &para);

	for (counter = 0; counter < 300; counter++) {
		rf_reg = rtw_read_rf(rtwdev, RF_PATH_A, RF_DTXLOK, RFREG_MASK);
		if (rf_reg == 0xabcde)
			break;
		msleep(20);
	}
	rtw_write_rf(rtwdev, RF_PATH_A, RF_DTXLOK, RFREG_MASK, 0x0);

	reload = !!rtw_read32_mask(rtwdev, REG_IQKFAILMSK, BIT(16));
	iqk_fail_mask = rtw_read32_mask(rtwdev, REG_IQKFAILMSK, GENMASK(7, 0));
	rtw_dbg(rtwdev, RTW_DBG_PHY,
		"iqk counter=%d reload=%d do_iqk_cnt=%d n_iqk_fail(mask)=0x%02x\n",
		counter, reload, ++do_iqk_cnt, iqk_fail_mask);
}

static void rtw8821c_phy_calibration(struct rtw_dev *rtwdev)
{
	rtw8821c_do_iqk(rtwdev);
}

/* for coex */
static void rtw8821c_coex_cfg_init(struct rtw_dev *rtwdev)
{
	/* enable TBTT nterrupt */
	rtw_write8_set(rtwdev, REG_BCN_CTRL, BIT_EN_BCN_FUNCTION);

	/* BT report packet sample rate */
	rtw_write8_mask(rtwdev, REG_BT_TDMA_TIME, BIT_MASK_SAMPLE_RATE, 0x5);

	/* enable BT counter statistics */
	rtw_write8(rtwdev, REG_BT_STAT_CTRL, BT_CNT_ENABLE);

	/* enable PTA (3-wire function form BT side) */
	rtw_write32_set(rtwdev, REG_GPIO_MUXCFG, BIT_BT_PTA_EN);
	rtw_write32_set(rtwdev, REG_GPIO_MUXCFG, BIT_PO_BT_PTA_PINS);

	/* enable PTA (tx/rx signal form WiFi side) */
	rtw_write8_set(rtwdev, REG_QUEUE_CTRL, BIT_PTA_WL_TX_EN);
	/* wl tx signal to PTA not case EDCCA */
	rtw_write8_clr(rtwdev, REG_QUEUE_CTRL, BIT_PTA_EDCCA_EN);
	/* GNT_BT=1 while select both */
	rtw_write16_set(rtwdev, REG_BT_COEX_V2, BIT_GNT_BT_POLARITY);

	/* beacon queue always hi-pri  */
	rtw_write8_mask(rtwdev, REG_BT_COEX_TABLE_H + 3, BIT_BCN_QUEUE,
			BCN_PRI_EN);
}

static void rtw8821c_coex_cfg_ant_switch(struct rtw_dev *rtwdev, u8 ctrl_type,
					 u8 pos_type)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	struct rtw_coex_rfe *coex_rfe = &coex->rfe;
	u32 switch_status = FIELD_PREP(CTRL_TYPE_MASK, ctrl_type) | pos_type;
	bool polarity_inverse;
	u8 regval = 0;

	if (switch_status == coex_dm->cur_switch_status)
		return;

	if (coex_rfe->wlg_at_btg) {
		ctrl_type = COEX_SWITCH_CTRL_BY_BBSW;

		if (coex_rfe->ant_switch_polarity)
			pos_type = COEX_SWITCH_TO_WLA;
		else
			pos_type = COEX_SWITCH_TO_WLG_BT;
	}

	coex_dm->cur_switch_status = switch_status;

	if (coex_rfe->ant_switch_diversity &&
	    ctrl_type == COEX_SWITCH_CTRL_BY_BBSW)
		ctrl_type = COEX_SWITCH_CTRL_BY_ANTDIV;

	polarity_inverse = (coex_rfe->ant_switch_polarity == 1);

	switch (ctrl_type) {
	default:
	case COEX_SWITCH_CTRL_BY_BBSW:
		rtw_write32_clr(rtwdev, REG_LED_CFG, BIT_DPDT_SEL_EN);
		rtw_write32_set(rtwdev, REG_LED_CFG, BIT_DPDT_WL_SEL);
		/* BB SW, DPDT use RFE_ctrl8 and RFE_ctrl9 as ctrl pin */
		rtw_write8_mask(rtwdev, REG_RFE_CTRL8, BIT_MASK_RFE_SEL89,
				DPDT_CTRL_PIN);

		if (pos_type == COEX_SWITCH_TO_WLG_BT) {
			if (coex_rfe->rfe_module_type != 0x4 &&
			    coex_rfe->rfe_module_type != 0x2)
				regval = 0x3;
			else
				regval = (!polarity_inverse ? 0x2 : 0x1);
		} else if (pos_type == COEX_SWITCH_TO_WLG) {
			regval = (!polarity_inverse ? 0x2 : 0x1);
		} else {
			regval = (!polarity_inverse ? 0x1 : 0x2);
		}

		rtw_write32_mask(rtwdev, REG_RFE_CTRL8, BIT_MASK_R_RFE_SEL_15,
				 regval);
		break;
	case COEX_SWITCH_CTRL_BY_PTA:
		rtw_write32_clr(rtwdev, REG_LED_CFG, BIT_DPDT_SEL_EN);
		rtw_write32_set(rtwdev, REG_LED_CFG, BIT_DPDT_WL_SEL);
		/* PTA,  DPDT use RFE_ctrl8 and RFE_ctrl9 as ctrl pin */
		rtw_write8_mask(rtwdev, REG_RFE_CTRL8, BIT_MASK_RFE_SEL89,
				PTA_CTRL_PIN);

		regval = (!polarity_inverse ? 0x2 : 0x1);
		rtw_write32_mask(rtwdev, REG_RFE_CTRL8, BIT_MASK_R_RFE_SEL_15,
				 regval);
		break;
	case COEX_SWITCH_CTRL_BY_ANTDIV:
		rtw_write32_clr(rtwdev, REG_LED_CFG, BIT_DPDT_SEL_EN);
		rtw_write32_set(rtwdev, REG_LED_CFG, BIT_DPDT_WL_SEL);
		rtw_write8_mask(rtwdev, REG_RFE_CTRL8, BIT_MASK_RFE_SEL89,
				ANTDIC_CTRL_PIN);
		break;
	case COEX_SWITCH_CTRL_BY_MAC:
		rtw_write32_set(rtwdev, REG_LED_CFG, BIT_DPDT_SEL_EN);

		regval = (!polarity_inverse ? 0x0 : 0x1);
		rtw_write8_mask(rtwdev, REG_PAD_CTRL1, BIT_SW_DPDT_SEL_DATA,
				regval);
		break;
	case COEX_SWITCH_CTRL_BY_FW:
		rtw_write32_clr(rtwdev, REG_LED_CFG, BIT_DPDT_SEL_EN);
		rtw_write32_set(rtwdev, REG_LED_CFG, BIT_DPDT_WL_SEL);
		break;
	case COEX_SWITCH_CTRL_BY_BT:
		rtw_write32_clr(rtwdev, REG_LED_CFG, BIT_DPDT_SEL_EN);
		rtw_write32_clr(rtwdev, REG_LED_CFG, BIT_DPDT_WL_SEL);
		break;
	}

	if (ctrl_type == COEX_SWITCH_CTRL_BY_BT) {
		rtw_write8_clr(rtwdev, REG_CTRL_TYPE, BIT_CTRL_TYPE1);
		rtw_write8_clr(rtwdev, REG_CTRL_TYPE, BIT_CTRL_TYPE2);
	} else {
		rtw_write8_set(rtwdev, REG_CTRL_TYPE, BIT_CTRL_TYPE1);
		rtw_write8_set(rtwdev, REG_CTRL_TYPE, BIT_CTRL_TYPE2);
	}
}

static void rtw8821c_coex_cfg_gnt_fix(struct rtw_dev *rtwdev)
{}

static void rtw8821c_coex_cfg_gnt_debug(struct rtw_dev *rtwdev)
{
	rtw_write32_clr(rtwdev, REG_PAD_CTRL1, BIT_BTGP_SPI_EN);
	rtw_write32_clr(rtwdev, REG_PAD_CTRL1, BIT_BTGP_JTAG_EN);
	rtw_write32_clr(rtwdev, REG_GPIO_MUXCFG, BIT_FSPI_EN);
	rtw_write32_clr(rtwdev, REG_PAD_CTRL1, BIT_LED1DIS);
	rtw_write32_clr(rtwdev, REG_SYS_SDIO_CTRL, BIT_SDIO_INT);
	rtw_write32_clr(rtwdev, REG_SYS_SDIO_CTRL, BIT_DBG_GNT_WL_BT);
}

static void rtw8821c_coex_cfg_rfe_type(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_rfe *coex_rfe = &coex->rfe;
	struct rtw_efuse *efuse = &rtwdev->efuse;

	coex_rfe->rfe_module_type = efuse->rfe_option;
	coex_rfe->ant_switch_polarity = 0;
	coex_rfe->ant_switch_exist = true;
	coex_rfe->wlg_at_btg = false;

	switch (coex_rfe->rfe_module_type) {
	case 0:
	case 8:
	case 1:
	case 9:  /* 1-Ant, Main, WLG */
	default: /* 2-Ant, DPDT, WLG */
		break;
	case 2:
	case 10: /* 1-Ant, Main, BTG */
	case 7:
	case 15: /* 2-Ant, DPDT, BTG */
		coex_rfe->wlg_at_btg = true;
		break;
	case 3:
	case 11: /* 1-Ant, Aux, WLG */
		coex_rfe->ant_switch_polarity = 1;
		break;
	case 4:
	case 12: /* 1-Ant, Aux, BTG */
		coex_rfe->wlg_at_btg = true;
		coex_rfe->ant_switch_polarity = 1;
		break;
	case 5:
	case 13: /* 2-Ant, no switch, WLG */
	case 6:
	case 14: /* 2-Ant, no antenna switch, WLG */
		coex_rfe->ant_switch_exist = false;
		break;
	}
}

static void rtw8821c_coex_cfg_wl_tx_power(struct rtw_dev *rtwdev, u8 wl_pwr)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	bool share_ant = efuse->share_ant;

	if (share_ant)
		return;

	if (wl_pwr == coex_dm->cur_wl_pwr_lvl)
		return;

	coex_dm->cur_wl_pwr_lvl = wl_pwr;
}

static void rtw8821c_coex_cfg_wl_rx_gain(struct rtw_dev *rtwdev, bool low_gain)
{}

static void
rtw8821c_txagc_swing_offset(struct rtw_dev *rtwdev, u8 pwr_idx_offset,
			    s8 pwr_idx_offset_lower,
			    s8 *txagc_idx, u8 *swing_idx)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	s8 delta_pwr_idx = dm_info->delta_power_index[RF_PATH_A];
	u8 swing_upper_bound = dm_info->default_ofdm_index + 10;
	u8 swing_lower_bound = 0;
	u8 max_pwr_idx_offset = 0xf;
	s8 agc_index = 0;
	u8 swing_index = dm_info->default_ofdm_index;

	pwr_idx_offset = min_t(u8, pwr_idx_offset, max_pwr_idx_offset);
	pwr_idx_offset_lower = max_t(s8, pwr_idx_offset_lower, -15);

	if (delta_pwr_idx >= 0) {
		if (delta_pwr_idx <= pwr_idx_offset) {
			agc_index = delta_pwr_idx;
			swing_index = dm_info->default_ofdm_index;
		} else if (delta_pwr_idx > pwr_idx_offset) {
			agc_index = pwr_idx_offset;
			swing_index = dm_info->default_ofdm_index +
					delta_pwr_idx - pwr_idx_offset;
			swing_index = min_t(u8, swing_index, swing_upper_bound);
		}
	} else if (delta_pwr_idx < 0) {
		if (delta_pwr_idx >= pwr_idx_offset_lower) {
			agc_index = delta_pwr_idx;
			swing_index = dm_info->default_ofdm_index;
		} else if (delta_pwr_idx < pwr_idx_offset_lower) {
			if (dm_info->default_ofdm_index >
				(pwr_idx_offset_lower - delta_pwr_idx))
				swing_index = dm_info->default_ofdm_index +
					delta_pwr_idx - pwr_idx_offset_lower;
			else
				swing_index = swing_lower_bound;

			agc_index = pwr_idx_offset_lower;
		}
	}

	if (swing_index >= ARRAY_SIZE(rtw8821c_txscale_tbl)) {
		rtw_warn(rtwdev, "swing index overflow\n");
		swing_index = ARRAY_SIZE(rtw8821c_txscale_tbl) - 1;
	}

	*txagc_idx = agc_index;
	*swing_idx = swing_index;
}

static void rtw8821c_pwrtrack_set_pwr(struct rtw_dev *rtwdev, u8 pwr_idx_offset,
				      s8 pwr_idx_offset_lower)
{
	s8 txagc_idx;
	u8 swing_idx;

	rtw8821c_txagc_swing_offset(rtwdev, pwr_idx_offset, pwr_idx_offset_lower,
				    &txagc_idx, &swing_idx);
	rtw_write32_mask(rtwdev, REG_TXAGCIDX, GENMASK(6, 1), txagc_idx);
	rtw_write32_mask(rtwdev, REG_TXSCALE_A, GENMASK(31, 21),
			 rtw8821c_txscale_tbl[swing_idx]);
}

static void rtw8821c_pwrtrack_set(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 pwr_idx_offset, tx_pwr_idx;
	s8 pwr_idx_offset_lower;
	u8 channel = rtwdev->hal.current_channel;
	u8 band_width = rtwdev->hal.current_band_width;
	u8 regd = rtw_regd_get(rtwdev);
	u8 tx_rate = dm_info->tx_rate;
	u8 max_pwr_idx = rtwdev->chip->max_power_index;

	tx_pwr_idx = rtw_phy_get_tx_power_index(rtwdev, RF_PATH_A, tx_rate,
						band_width, channel, regd);

	tx_pwr_idx = min_t(u8, tx_pwr_idx, max_pwr_idx);

	pwr_idx_offset = max_pwr_idx - tx_pwr_idx;
	pwr_idx_offset_lower = 0 - tx_pwr_idx;

	rtw8821c_pwrtrack_set_pwr(rtwdev, pwr_idx_offset, pwr_idx_offset_lower);
}

static void rtw8821c_phy_pwrtrack(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_swing_table swing_table;
	u8 thermal_value, delta;

	rtw_phy_config_swing_table(rtwdev, &swing_table);

	if (rtwdev->efuse.thermal_meter[0] == 0xff)
		return;

	thermal_value = rtw_read_rf(rtwdev, RF_PATH_A, RF_T_METER, 0xfc00);

	rtw_phy_pwrtrack_avg(rtwdev, thermal_value, RF_PATH_A);

	if (dm_info->pwr_trk_init_trigger)
		dm_info->pwr_trk_init_trigger = false;
	else if (!rtw_phy_pwrtrack_thermal_changed(rtwdev, thermal_value,
						   RF_PATH_A))
		goto iqk;

	delta = rtw_phy_pwrtrack_get_delta(rtwdev, RF_PATH_A);

	delta = min_t(u8, delta, RTW_PWR_TRK_TBL_SZ - 1);

	dm_info->delta_power_index[RF_PATH_A] =
		rtw_phy_pwrtrack_get_pwridx(rtwdev, &swing_table, RF_PATH_A,
					    RF_PATH_A, delta);
	if (dm_info->delta_power_index[RF_PATH_A] ==
			dm_info->delta_power_index_last[RF_PATH_A])
		goto iqk;
	else
		dm_info->delta_power_index_last[RF_PATH_A] =
			dm_info->delta_power_index[RF_PATH_A];
	rtw8821c_pwrtrack_set(rtwdev);

iqk:
	if (rtw_phy_pwrtrack_need_iqk(rtwdev))
		rtw8821c_do_iqk(rtwdev);
}

static void rtw8821c_pwr_track(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;

	if (efuse->power_track_type != 0)
		return;

	if (!dm_info->pwr_trk_triggered) {
		rtw_write_rf(rtwdev, RF_PATH_A, RF_T_METER,
			     GENMASK(17, 16), 0x03);
		dm_info->pwr_trk_triggered = true;
		return;
	}

	rtw8821c_phy_pwrtrack(rtwdev);
	dm_info->pwr_trk_triggered = false;
}

static void rtw8821c_bf_config_bfee_su(struct rtw_dev *rtwdev,
				       struct rtw_vif *vif,
				       struct rtw_bfee *bfee, bool enable)
{
	if (enable)
		rtw_bf_enable_bfee_su(rtwdev, vif, bfee);
	else
		rtw_bf_remove_bfee_su(rtwdev, bfee);
}

static void rtw8821c_bf_config_bfee_mu(struct rtw_dev *rtwdev,
				       struct rtw_vif *vif,
				       struct rtw_bfee *bfee, bool enable)
{
	if (enable)
		rtw_bf_enable_bfee_mu(rtwdev, vif, bfee);
	else
		rtw_bf_remove_bfee_mu(rtwdev, bfee);
}

static void rtw8821c_bf_config_bfee(struct rtw_dev *rtwdev, struct rtw_vif *vif,
				    struct rtw_bfee *bfee, bool enable)
{
	if (bfee->role == RTW_BFEE_SU)
		rtw8821c_bf_config_bfee_su(rtwdev, vif, bfee, enable);
	else if (bfee->role == RTW_BFEE_MU)
		rtw8821c_bf_config_bfee_mu(rtwdev, vif, bfee, enable);
	else
		rtw_warn(rtwdev, "wrong bfee role\n");
}

static void rtw8821c_phy_cck_pd_set(struct rtw_dev *rtwdev, u8 new_lvl)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 pd[CCK_PD_LV_MAX] = {3, 7, 13, 13, 13};
	u8 cck_n_rx;

	rtw_dbg(rtwdev, RTW_DBG_PHY, "lv: (%d) -> (%d)\n",
		dm_info->cck_pd_lv[RTW_CHANNEL_WIDTH_20][RF_PATH_A], new_lvl);

	if (dm_info->cck_pd_lv[RTW_CHANNEL_WIDTH_20][RF_PATH_A] == new_lvl)
		return;

	cck_n_rx = (rtw_read8_mask(rtwdev, REG_CCK0_FAREPORT, BIT_CCK0_2RX) &&
		    rtw_read8_mask(rtwdev, REG_CCK0_FAREPORT, BIT_CCK0_MRC)) ? 2 : 1;
	rtw_dbg(rtwdev, RTW_DBG_PHY,
		"is_linked=%d, lv=%d, n_rx=%d, cs_ratio=0x%x, pd_th=0x%x, cck_fa_avg=%d\n",
		rtw_is_assoc(rtwdev), new_lvl, cck_n_rx,
		dm_info->cck_pd_default + new_lvl * 2,
		pd[new_lvl], dm_info->cck_fa_avg);

	dm_info->cck_fa_avg = CCK_FA_AVG_RESET;

	dm_info->cck_pd_lv[RTW_CHANNEL_WIDTH_20][RF_PATH_A] = new_lvl;
	rtw_write32_mask(rtwdev, REG_PWRTH, 0x3f0000, pd[new_lvl]);
	rtw_write32_mask(rtwdev, REG_PWRTH2, 0x1f0000,
			 dm_info->cck_pd_default + new_lvl * 2);
}

static void rtw8821c_fill_txdesc_checksum(struct rtw_dev *rtwdev,
					  struct rtw_tx_pkt_info *pkt_info,
					  u8 *txdesc)
{
	fill_txdesc_checksum_common(txdesc, 16);
}

static struct rtw_pwr_seq_cmd trans_carddis_to_cardemu_8821c[] = {
	{0x0086,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0086,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_POLLING, BIT(1), BIT(1)},
	{0x004A,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(4) | BIT(7), 0},
	{0x0300,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0x0301,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static struct rtw_pwr_seq_cmd trans_cardemu_to_act_8821c[] = {
	{0x0020,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0001,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_DELAY, 1, RTW_PWR_DELAY_MS},
	{0x0000,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, (BIT(4) | BIT(3) | BIT(2)), 0},
	{0x0075,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0006,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, BIT(1), BIT(1)},
	{0x0075,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0006,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(7), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, (BIT(4) | BIT(3)), 0},
	{0x10C3,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, BIT(0), 0},
	{0x0020,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3), BIT(3)},
	{0x0074,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), BIT(5)},
	{0x0022,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0062,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, (BIT(7) | BIT(6) | BIT(5)),
	 (BIT(7) | BIT(6) | BIT(5))},
	{0x0061,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, (BIT(7) | BIT(6) | BIT(5)), 0},
	{0x007C,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static struct rtw_pwr_seq_cmd trans_act_to_cardemu_8821c[] = {
	{0x0093,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3), 0},
	{0x001F,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0x0049,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0006,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0002,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x10C3,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), BIT(1)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, BIT(1), 0},
	{0x0020,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3), 0},
	{0x0000,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), BIT(5)},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static struct rtw_pwr_seq_cmd trans_cardemu_to_carddis_8821c[] = {
	{0x0007,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x20},
	{0x0067,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(2), BIT(2)},
	{0x004A,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0067,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), 0},
	{0x0067,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(4), 0},
	{0x004F,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0067,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0046,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(6), BIT(6)},
	{0x0067,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(2), 0},
	{0x0046,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(7), BIT(7)},
	{0x0062,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(4), BIT(4)},
	{0x0081,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(7) | BIT(6), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(4), BIT(3)},
	{0x0086,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0086,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_POLLING, BIT(1), 0},
	{0x0090,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0044,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0x0040,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x90},
	{0x0041,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x00},
	{0x0042,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x04},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd *card_enable_flow_8821c[] = {
	trans_carddis_to_cardemu_8821c,
	trans_cardemu_to_act_8821c,
	NULL
};

static const struct rtw_pwr_seq_cmd *card_disable_flow_8821c[] = {
	trans_act_to_cardemu_8821c,
	trans_cardemu_to_carddis_8821c,
	NULL
};

static const struct rtw_intf_phy_para usb2_param_8821c[] = {
	{0xFFFF, 0x00,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_ALL,
	 RTW_INTF_PHY_PLATFORM_ALL},
};

static const struct rtw_intf_phy_para usb3_param_8821c[] = {
	{0xFFFF, 0x0000,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_ALL,
	 RTW_INTF_PHY_PLATFORM_ALL},
};

static const struct rtw_intf_phy_para pcie_gen1_param_8821c[] = {
	{0x0009, 0x6380,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_ALL,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0xFFFF, 0x0000,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_ALL,
	 RTW_INTF_PHY_PLATFORM_ALL},
};

static const struct rtw_intf_phy_para pcie_gen2_param_8821c[] = {
	{0xFFFF, 0x0000,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_ALL,
	 RTW_INTF_PHY_PLATFORM_ALL},
};

static const struct rtw_intf_phy_para_table phy_para_table_8821c = {
	.usb2_para	= usb2_param_8821c,
	.usb3_para	= usb3_param_8821c,
	.gen1_para	= pcie_gen1_param_8821c,
	.gen2_para	= pcie_gen2_param_8821c,
	.n_usb2_para	= ARRAY_SIZE(usb2_param_8821c),
	.n_usb3_para	= ARRAY_SIZE(usb2_param_8821c),
	.n_gen1_para	= ARRAY_SIZE(pcie_gen1_param_8821c),
	.n_gen2_para	= ARRAY_SIZE(pcie_gen2_param_8821c),
};

static const struct rtw_rfe_def rtw8821c_rfe_defs[] = {
	[0] = RTW_DEF_RFE(8821c, 0, 0),
	[2] = RTW_DEF_RFE_EXT(8821c, 0, 0, 2),
	[4] = RTW_DEF_RFE_EXT(8821c, 0, 0, 2),
	[6] = RTW_DEF_RFE(8821c, 0, 0),
};

static struct rtw_hw_reg rtw8821c_dig[] = {
	[0] = { .addr = 0xc50, .mask = 0x7f },
};

static const struct rtw_ltecoex_addr rtw8821c_ltecoex_addr = {
	.ctrl = LTECOEX_ACCESS_CTRL,
	.wdata = LTECOEX_WRITE_DATA,
	.rdata = LTECOEX_READ_DATA,
};

static struct rtw_page_table page_table_8821c[] = {
	/* not sure what [0] stands for */
	{16, 16, 16, 14, 1},
	{16, 16, 16, 14, 1},
	{16, 16, 0, 0, 1},
	{16, 16, 16, 0, 1},
	{16, 16, 16, 14, 1},
};

static struct rtw_rqpn rqpn_table_8821c[] = {
	/* not sure what [0] stands for */
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_EXTRA, RTW_DMA_MAPPING_HIGH},
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_EXTRA, RTW_DMA_MAPPING_HIGH},
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_HIGH,
	 RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_HIGH},
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_HIGH},
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_EXTRA, RTW_DMA_MAPPING_HIGH},
};

static struct rtw_prioq_addrs prioq_addrs_8821c = {
	.prio[RTW_DMA_MAPPING_EXTRA] = {
		.rsvd = REG_FIFOPAGE_INFO_4, .avail = REG_FIFOPAGE_INFO_4 + 2,
	},
	.prio[RTW_DMA_MAPPING_LOW] = {
		.rsvd = REG_FIFOPAGE_INFO_2, .avail = REG_FIFOPAGE_INFO_2 + 2,
	},
	.prio[RTW_DMA_MAPPING_NORMAL] = {
		.rsvd = REG_FIFOPAGE_INFO_3, .avail = REG_FIFOPAGE_INFO_3 + 2,
	},
	.prio[RTW_DMA_MAPPING_HIGH] = {
		.rsvd = REG_FIFOPAGE_INFO_1, .avail = REG_FIFOPAGE_INFO_1 + 2,
	},
	.wsize = true,
};

static struct rtw_chip_ops rtw8821c_ops = {
	.phy_set_param		= rtw8821c_phy_set_param,
	.read_efuse		= rtw8821c_read_efuse,
	.query_rx_desc		= rtw8821c_query_rx_desc,
	.set_channel		= rtw8821c_set_channel,
	.mac_init		= rtw8821c_mac_init,
	.read_rf		= rtw_phy_read_rf,
	.write_rf		= rtw_phy_write_rf_reg_sipi,
	.set_antenna		= NULL,
	.set_tx_power_index	= rtw8821c_set_tx_power_index,
	.cfg_ldo25		= rtw8821c_cfg_ldo25,
	.false_alarm_statistics	= rtw8821c_false_alarm_statistics,
	.phy_calibration	= rtw8821c_phy_calibration,
	.cck_pd_set		= rtw8821c_phy_cck_pd_set,
	.pwr_track		= rtw8821c_pwr_track,
	.config_bfee		= rtw8821c_bf_config_bfee,
	.set_gid_table		= rtw_bf_set_gid_table,
	.cfg_csi_rate		= rtw_bf_cfg_csi_rate,
	.fill_txdesc_checksum	= rtw8821c_fill_txdesc_checksum,

	.coex_set_init		= rtw8821c_coex_cfg_init,
	.coex_set_ant_switch	= rtw8821c_coex_cfg_ant_switch,
	.coex_set_gnt_fix	= rtw8821c_coex_cfg_gnt_fix,
	.coex_set_gnt_debug	= rtw8821c_coex_cfg_gnt_debug,
	.coex_set_rfe_type	= rtw8821c_coex_cfg_rfe_type,
	.coex_set_wl_tx_power	= rtw8821c_coex_cfg_wl_tx_power,
	.coex_set_wl_rx_gain	= rtw8821c_coex_cfg_wl_rx_gain,
};

/* rssi in percentage % (dbm = % - 100) */
static const u8 wl_rssi_step_8821c[] = {101, 45, 101, 40};
static const u8 bt_rssi_step_8821c[] = {101, 101, 101, 101};

/* Shared-Antenna Coex Table */
static const struct coex_table_para table_sant_8821c[] = {
	{0x55555555, 0x55555555}, /* case-0 */
	{0x55555555, 0x55555555},
	{0x66555555, 0x66555555},
	{0xaaaaaaaa, 0xaaaaaaaa},
	{0x5a5a5a5a, 0x5a5a5a5a},
	{0xfafafafa, 0xfafafafa}, /* case-5 */
	{0x6a5a5555, 0xaaaaaaaa},
	{0x6a5a56aa, 0x6a5a56aa},
	{0x6a5a5a5a, 0x6a5a5a5a},
	{0x66555555, 0x5a5a5a5a},
	{0x66555555, 0x6a5a5a5a}, /* case-10 */
	{0x66555555, 0xaaaaaaaa},
	{0x66555555, 0x6a5a5aaa},
	{0x66555555, 0x6aaa6aaa},
	{0x66555555, 0x6a5a5aaa},
	{0x66555555, 0xaaaaaaaa}, /* case-15 */
	{0xffff55ff, 0xfafafafa},
	{0xffff55ff, 0x6afa5afa},
	{0xaaffffaa, 0xfafafafa},
	{0xaa5555aa, 0x5a5a5a5a},
	{0xaa5555aa, 0x6a5a5a5a}, /* case-20 */
	{0xaa5555aa, 0xaaaaaaaa},
	{0xffffffff, 0x55555555},
	{0xffffffff, 0x5a5a5a5a},
	{0xffffffff, 0x5a5a5a5a},
	{0xffffffff, 0x5a5a5aaa}, /* case-25 */
	{0x55555555, 0x5a5a5a5a},
	{0x55555555, 0xaaaaaaaa},
	{0x66555555, 0x6a5a6a5a},
	{0x66556655, 0x66556655},
	{0x66556aaa, 0x6a5a6aaa}, /* case-30 */
	{0xffffffff, 0x5aaa5aaa},
	{0x56555555, 0x5a5a5aaa}
};

/* Non-Shared-Antenna Coex Table */
static const struct coex_table_para table_nsant_8821c[] = {
	{0xffffffff, 0xffffffff}, /* case-100 */
	{0xffff55ff, 0xfafafafa},
	{0x66555555, 0x66555555},
	{0xaaaaaaaa, 0xaaaaaaaa},
	{0x5a5a5a5a, 0x5a5a5a5a},
	{0xffffffff, 0xffffffff}, /* case-105 */
	{0x5afa5afa, 0x5afa5afa},
	{0x55555555, 0xfafafafa},
	{0x66555555, 0xfafafafa},
	{0x66555555, 0x5a5a5a5a},
	{0x66555555, 0x6a5a5a5a}, /* case-110 */
	{0x66555555, 0xaaaaaaaa},
	{0xffff55ff, 0xfafafafa},
	{0xffff55ff, 0x5afa5afa},
	{0xffff55ff, 0xaaaaaaaa},
	{0xffff55ff, 0xffff55ff}, /* case-115 */
	{0xaaffffaa, 0x5afa5afa},
	{0xaaffffaa, 0xaaaaaaaa},
	{0xffffffff, 0xfafafafa},
	{0xffff55ff, 0xfafafafa},
	{0xffffffff, 0xaaaaaaaa}, /* case-120 */
	{0xffff55ff, 0x5afa5afa},
	{0xffff55ff, 0x5afa5afa},
	{0x55ff55ff, 0x55ff55ff}
};

/* Shared-Antenna TDMA */
static const struct coex_tdma_para tdma_sant_8821c[] = {
	{ {0x00, 0x00, 0x00, 0x00, 0x00} }, /* case-0 */
	{ {0x61, 0x45, 0x03, 0x11, 0x11} }, /* case-1 */
	{ {0x61, 0x3a, 0x03, 0x11, 0x11} },
	{ {0x61, 0x35, 0x03, 0x11, 0x11} },
	{ {0x61, 0x20, 0x03, 0x11, 0x11} },
	{ {0x61, 0x3a, 0x03, 0x11, 0x11} }, /* case-5 */
	{ {0x61, 0x45, 0x03, 0x11, 0x10} },
	{ {0x61, 0x35, 0x03, 0x11, 0x10} },
	{ {0x61, 0x30, 0x03, 0x11, 0x10} },
	{ {0x61, 0x20, 0x03, 0x11, 0x10} },
	{ {0x61, 0x10, 0x03, 0x11, 0x10} }, /* case-10 */
	{ {0x61, 0x08, 0x03, 0x11, 0x15} },
	{ {0x61, 0x08, 0x03, 0x10, 0x14} },
	{ {0x51, 0x08, 0x03, 0x10, 0x54} },
	{ {0x51, 0x08, 0x03, 0x10, 0x55} },
	{ {0x51, 0x08, 0x07, 0x10, 0x54} }, /* case-15 */
	{ {0x51, 0x45, 0x03, 0x10, 0x50} },
	{ {0x51, 0x3a, 0x03, 0x11, 0x50} },
	{ {0x51, 0x30, 0x03, 0x10, 0x50} },
	{ {0x51, 0x21, 0x03, 0x10, 0x50} },
	{ {0x51, 0x10, 0x03, 0x10, 0x50} }, /* case-20 */
	{ {0x51, 0x4a, 0x03, 0x10, 0x50} },
	{ {0x51, 0x08, 0x03, 0x30, 0x54} },
	{ {0x55, 0x08, 0x03, 0x10, 0x54} },
	{ {0x65, 0x10, 0x03, 0x11, 0x10} },
	{ {0x51, 0x10, 0x03, 0x10, 0x51} }, /* case-25 */
	{ {0x51, 0x21, 0x03, 0x10, 0x50} },
	{ {0x61, 0x08, 0x03, 0x11, 0x11} }
};

/* Non-Shared-Antenna TDMA */
static const struct coex_tdma_para tdma_nsant_8821c[] = {
	{ {0x00, 0x00, 0x00, 0x40, 0x00} }, /* case-100 */
	{ {0x61, 0x45, 0x03, 0x11, 0x11} },
	{ {0x61, 0x25, 0x03, 0x11, 0x11} },
	{ {0x61, 0x35, 0x03, 0x11, 0x11} },
	{ {0x61, 0x20, 0x03, 0x11, 0x11} },
	{ {0x61, 0x10, 0x03, 0x11, 0x11} }, /* case-105 */
	{ {0x61, 0x45, 0x03, 0x11, 0x10} },
	{ {0x61, 0x30, 0x03, 0x11, 0x10} },
	{ {0x61, 0x30, 0x03, 0x11, 0x10} },
	{ {0x61, 0x20, 0x03, 0x11, 0x10} },
	{ {0x61, 0x10, 0x03, 0x11, 0x10} }, /* case-110 */
	{ {0x61, 0x10, 0x03, 0x11, 0x11} },
	{ {0x61, 0x08, 0x03, 0x10, 0x14} },
	{ {0x51, 0x08, 0x03, 0x10, 0x54} },
	{ {0x51, 0x08, 0x03, 0x10, 0x55} },
	{ {0x51, 0x08, 0x07, 0x10, 0x54} }, /* case-115 */
	{ {0x51, 0x45, 0x03, 0x10, 0x50} },
	{ {0x51, 0x3a, 0x03, 0x10, 0x50} },
	{ {0x51, 0x30, 0x03, 0x10, 0x50} },
	{ {0x51, 0x21, 0x03, 0x10, 0x50} },
	{ {0x51, 0x21, 0x03, 0x10, 0x50} }, /* case-120 */
	{ {0x51, 0x10, 0x03, 0x10, 0x50} }
};

static const struct coex_5g_afh_map afh_5g_8821c[] = { {0, 0, 0} };

/* wl_tx_dec_power, bt_tx_dec_power, wl_rx_gain, bt_rx_lna_constrain */
static const struct coex_rf_para rf_para_tx_8821c[] = {
	{0, 0, false, 7},  /* for normal */
	{0, 20, false, 7}, /* for WL-CPT */
	{8, 17, true, 4},
	{7, 18, true, 4},
	{6, 19, true, 4},
	{5, 20, true, 4}
};

static const struct coex_rf_para rf_para_rx_8821c[] = {
	{0, 0, false, 7},  /* for normal */
	{0, 20, false, 7}, /* for WL-CPT */
	{3, 24, true, 5},
	{2, 26, true, 5},
	{1, 27, true, 5},
	{0, 28, true, 5}
};

static_assert(ARRAY_SIZE(rf_para_tx_8821c) == ARRAY_SIZE(rf_para_rx_8821c));

static const u8 rtw8821c_pwrtrk_5gb_n[][RTW_PWR_TRK_TBL_SZ] = {
	{0, 1, 1, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 8, 8, 8, 9, 9, 9, 10, 10,
	 11, 11, 12, 12, 12, 12, 12},
	{0, 1, 1, 1, 2, 3, 3, 4, 4, 5, 5, 5, 6, 6, 7, 8, 8, 9, 9, 10, 10, 11,
	 11, 12, 12, 12, 12, 12, 12, 12},
	{0, 1, 2, 2, 3, 4, 4, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9, 10, 10, 11,
	 11, 12, 12, 12, 12, 12, 12},
};

static const u8 rtw8821c_pwrtrk_5gb_p[][RTW_PWR_TRK_TBL_SZ] = {
	{0, 1, 1, 2, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 11, 11,
	 12, 12, 12, 12, 12, 12, 12},
	{0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 5, 6, 7, 7, 8, 8, 9, 10, 10, 11, 11,
	 12, 12, 12, 12, 12, 12, 12, 12},
	{0, 1, 1, 1, 2, 3, 3, 3, 4, 4, 4, 5, 6, 6, 7, 7, 8, 8, 9, 10, 10, 11,
	 11, 12, 12, 12, 12, 12, 12, 12},
};

static const u8 rtw8821c_pwrtrk_5ga_n[][RTW_PWR_TRK_TBL_SZ] = {
	{0, 1, 1, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 8, 8, 8, 9, 9, 9, 10, 10,
	 11, 11, 12, 12, 12, 12, 12},
	{0, 1, 1, 1, 2, 3, 3, 4, 4, 5, 5, 5, 6, 6, 7, 8, 8, 9, 9, 10, 10, 11,
	 11, 12, 12, 12, 12, 12, 12, 12},
	{0, 1, 2, 2, 3, 4, 4, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9, 10, 10, 11,
	 11, 12, 12, 12, 12, 12, 12},
};

static const u8 rtw8821c_pwrtrk_5ga_p[][RTW_PWR_TRK_TBL_SZ] = {
	{0, 1, 1, 2, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 11, 11,
	 12, 12, 12, 12, 12, 12, 12},
	{0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 5, 6, 7, 7, 8, 8, 9, 10, 10, 11, 11,
	 12, 12, 12, 12, 12, 12, 12, 12},
	{0, 1, 1, 1, 2, 3, 3, 3, 4, 4, 4, 5, 6, 6, 7, 7, 8, 8, 9, 10, 10, 11,
	 11, 12, 12, 12, 12, 12, 12, 12},
};

static const u8 rtw8821c_pwrtrk_2gb_n[] = {
	0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4,
	4, 4, 5, 5, 5, 5, 6, 6, 6, 7, 7, 8, 8, 9
};

static const u8 rtw8821c_pwrtrk_2gb_p[] = {
	0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5,
	5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 9, 9, 9, 9
};

static const u8 rtw8821c_pwrtrk_2ga_n[] = {
	0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4,
	4, 4, 5, 5, 5, 5, 6, 6, 6, 7, 7, 8, 8, 9
};

static const u8 rtw8821c_pwrtrk_2ga_p[] = {
	0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5,
	5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 9, 9, 9, 9
};

static const u8 rtw8821c_pwrtrk_2g_cck_b_n[] = {
	0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4,
	4, 5, 5, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9
};

static const u8 rtw8821c_pwrtrk_2g_cck_b_p[] = {
	0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5,
	5, 6, 6, 7, 7, 7, 8, 8, 9, 9, 9, 9, 9, 9
};

static const u8 rtw8821c_pwrtrk_2g_cck_a_n[] = {
	0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4,
	4, 5, 5, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9
};

static const u8 rtw8821c_pwrtrk_2g_cck_a_p[] = {
	0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5,
	5, 6, 6, 7, 7, 7, 8, 8, 9, 9, 9, 9, 9, 9
};

static const struct rtw_pwr_track_tbl rtw8821c_rtw_pwr_track_tbl = {
	.pwrtrk_5gb_n[0] = rtw8821c_pwrtrk_5gb_n[0],
	.pwrtrk_5gb_n[1] = rtw8821c_pwrtrk_5gb_n[1],
	.pwrtrk_5gb_n[2] = rtw8821c_pwrtrk_5gb_n[2],
	.pwrtrk_5gb_p[0] = rtw8821c_pwrtrk_5gb_p[0],
	.pwrtrk_5gb_p[1] = rtw8821c_pwrtrk_5gb_p[1],
	.pwrtrk_5gb_p[2] = rtw8821c_pwrtrk_5gb_p[2],
	.pwrtrk_5ga_n[0] = rtw8821c_pwrtrk_5ga_n[0],
	.pwrtrk_5ga_n[1] = rtw8821c_pwrtrk_5ga_n[1],
	.pwrtrk_5ga_n[2] = rtw8821c_pwrtrk_5ga_n[2],
	.pwrtrk_5ga_p[0] = rtw8821c_pwrtrk_5ga_p[0],
	.pwrtrk_5ga_p[1] = rtw8821c_pwrtrk_5ga_p[1],
	.pwrtrk_5ga_p[2] = rtw8821c_pwrtrk_5ga_p[2],
	.pwrtrk_2gb_n = rtw8821c_pwrtrk_2gb_n,
	.pwrtrk_2gb_p = rtw8821c_pwrtrk_2gb_p,
	.pwrtrk_2ga_n = rtw8821c_pwrtrk_2ga_n,
	.pwrtrk_2ga_p = rtw8821c_pwrtrk_2ga_p,
	.pwrtrk_2g_cckb_n = rtw8821c_pwrtrk_2g_cck_b_n,
	.pwrtrk_2g_cckb_p = rtw8821c_pwrtrk_2g_cck_b_p,
	.pwrtrk_2g_ccka_n = rtw8821c_pwrtrk_2g_cck_a_n,
	.pwrtrk_2g_ccka_p = rtw8821c_pwrtrk_2g_cck_a_p,
};

static const struct rtw_reg_domain coex_info_hw_regs_8821c[] = {
	{0xCB0, MASKDWORD, RTW_REG_DOMAIN_MAC32},
	{0xCB4, MASKDWORD, RTW_REG_DOMAIN_MAC32},
	{0xCBA, MASKBYTE0, RTW_REG_DOMAIN_MAC8},
	{0, 0, RTW_REG_DOMAIN_NL},
	{0x430, MASKDWORD, RTW_REG_DOMAIN_MAC32},
	{0x434, MASKDWORD, RTW_REG_DOMAIN_MAC32},
	{0x42a, MASKLWORD, RTW_REG_DOMAIN_MAC16},
	{0x426, MASKBYTE0, RTW_REG_DOMAIN_MAC8},
	{0x45e, BIT(3), RTW_REG_DOMAIN_MAC8},
	{0x454, MASKLWORD, RTW_REG_DOMAIN_MAC16},
	{0, 0, RTW_REG_DOMAIN_NL},
	{0x4c, BIT(24) | BIT(23), RTW_REG_DOMAIN_MAC32},
	{0x64, BIT(0), RTW_REG_DOMAIN_MAC8},
	{0x4c6, BIT(4), RTW_REG_DOMAIN_MAC8},
	{0x40, BIT(5), RTW_REG_DOMAIN_MAC8},
	{0x1, RFREG_MASK, RTW_REG_DOMAIN_RF_A},
	{0, 0, RTW_REG_DOMAIN_NL},
	{0x550, MASKDWORD, RTW_REG_DOMAIN_MAC32},
	{0x522, MASKBYTE0, RTW_REG_DOMAIN_MAC8},
	{0x953, BIT(1), RTW_REG_DOMAIN_MAC8},
	{0xc50,  MASKBYTE0, RTW_REG_DOMAIN_MAC8},
	{0x60A, MASKBYTE0, RTW_REG_DOMAIN_MAC8},
};

const struct rtw_chip_info rtw8821c_hw_spec = {
	.ops = &rtw8821c_ops,
	.id = RTW_CHIP_TYPE_8821C,
	.fw_name = "rtw88/rtw8821c_fw.bin",
	.wlan_cpu = RTW_WCPU_11AC,
	.tx_pkt_desc_sz = 48,
	.tx_buf_desc_sz = 16,
	.rx_pkt_desc_sz = 24,
	.rx_buf_desc_sz = 8,
	.phy_efuse_size = 512,
	.log_efuse_size = 512,
	.ptct_efuse_size = 96,
	.txff_size = 65536,
	.rxff_size = 16384,
	.rsvd_drv_pg_num = 8,
	.txgi_factor = 1,
	.is_pwr_by_rate_dec = true,
	.max_power_index = 0x3f,
	.csi_buf_pg_num = 0,
	.band = RTW_BAND_2G | RTW_BAND_5G,
	.page_size = TX_PAGE_SIZE,
	.dig_min = 0x1c,
	.ht_supported = true,
	.vht_supported = true,
	.lps_deep_mode_supported = BIT(LPS_DEEP_MODE_LCLK),
	.sys_func_en = 0xD8,
	.pwr_on_seq = card_enable_flow_8821c,
	.pwr_off_seq = card_disable_flow_8821c,
	.page_table = page_table_8821c,
	.rqpn_table = rqpn_table_8821c,
	.prioq_addrs = &prioq_addrs_8821c,
	.intf_table = &phy_para_table_8821c,
	.dig = rtw8821c_dig,
	.rf_base_addr = {0x2800, 0x2c00},
	.rf_sipi_addr = {0xc90, 0xe90},
	.ltecoex_addr = &rtw8821c_ltecoex_addr,
	.mac_tbl = &rtw8821c_mac_tbl,
	.agc_tbl = &rtw8821c_agc_tbl,
	.bb_tbl = &rtw8821c_bb_tbl,
	.rf_tbl = {&rtw8821c_rf_a_tbl},
	.rfe_defs = rtw8821c_rfe_defs,
	.rfe_defs_size = ARRAY_SIZE(rtw8821c_rfe_defs),
	.rx_ldpc = false,
	.pwr_track_tbl = &rtw8821c_rtw_pwr_track_tbl,
	.iqk_threshold = 8,
	.bfer_su_max_num = 2,
	.bfer_mu_max_num = 1,
	.ampdu_density = IEEE80211_HT_MPDU_DENSITY_2,
	.max_scan_ie_len = IEEE80211_MAX_DATA_LEN,

	.coex_para_ver = 0x19092746,
	.bt_desired_ver = 0x46,
	.scbd_support = true,
	.new_scbd10_def = false,
	.ble_hid_profile_support = false,
	.wl_mimo_ps_support = false,
	.pstdma_type = COEX_PSTDMA_FORCE_LPSOFF,
	.bt_rssi_type = COEX_BTRSSI_RATIO,
	.ant_isolation = 15,
	.rssi_tolerance = 2,
	.wl_rssi_step = wl_rssi_step_8821c,
	.bt_rssi_step = bt_rssi_step_8821c,
	.table_sant_num = ARRAY_SIZE(table_sant_8821c),
	.table_sant = table_sant_8821c,
	.table_nsant_num = ARRAY_SIZE(table_nsant_8821c),
	.table_nsant = table_nsant_8821c,
	.tdma_sant_num = ARRAY_SIZE(tdma_sant_8821c),
	.tdma_sant = tdma_sant_8821c,
	.tdma_nsant_num = ARRAY_SIZE(tdma_nsant_8821c),
	.tdma_nsant = tdma_nsant_8821c,
	.wl_rf_para_num = ARRAY_SIZE(rf_para_tx_8821c),
	.wl_rf_para_tx = rf_para_tx_8821c,
	.wl_rf_para_rx = rf_para_rx_8821c,
	.bt_afh_span_bw20 = 0x24,
	.bt_afh_span_bw40 = 0x36,
	.afh_5g_num = ARRAY_SIZE(afh_5g_8821c),
	.afh_5g = afh_5g_8821c,

	.coex_info_hw_regs_num = ARRAY_SIZE(coex_info_hw_regs_8821c),
	.coex_info_hw_regs = coex_info_hw_regs_8821c,
};
EXPORT_SYMBOL(rtw8821c_hw_spec);

MODULE_FIRMWARE("rtw88/rtw8821c_fw.bin");

MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ac wireless 8821c driver");
MODULE_LICENSE("Dual BSD/GPL");
