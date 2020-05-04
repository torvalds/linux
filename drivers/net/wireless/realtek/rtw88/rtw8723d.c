// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include "main.h"
#include "coex.h"
#include "fw.h"
#include "tx.h"
#include "rx.h"
#include "phy.h"
#include "rtw8723d.h"
#include "rtw8723d_table.h"
#include "mac.h"
#include "reg.h"
#include "debug.h"

static const struct rtw_hw_reg rtw8723d_txagc[] = {
	[DESC_RATE1M]	= { .addr = 0xe08, .mask = 0x0000ff00 },
	[DESC_RATE2M]	= { .addr = 0x86c, .mask = 0x0000ff00 },
	[DESC_RATE5_5M]	= { .addr = 0x86c, .mask = 0x00ff0000 },
	[DESC_RATE11M]	= { .addr = 0x86c, .mask = 0xff000000 },
	[DESC_RATE6M]	= { .addr = 0xe00, .mask = 0x000000ff },
	[DESC_RATE9M]	= { .addr = 0xe00, .mask = 0x0000ff00 },
	[DESC_RATE12M]	= { .addr = 0xe00, .mask = 0x00ff0000 },
	[DESC_RATE18M]	= { .addr = 0xe00, .mask = 0xff000000 },
	[DESC_RATE24M]	= { .addr = 0xe04, .mask = 0x000000ff },
	[DESC_RATE36M]	= { .addr = 0xe04, .mask = 0x0000ff00 },
	[DESC_RATE48M]	= { .addr = 0xe04, .mask = 0x00ff0000 },
	[DESC_RATE54M]	= { .addr = 0xe04, .mask = 0xff000000 },
	[DESC_RATEMCS0]	= { .addr = 0xe10, .mask = 0x000000ff },
	[DESC_RATEMCS1]	= { .addr = 0xe10, .mask = 0x0000ff00 },
	[DESC_RATEMCS2]	= { .addr = 0xe10, .mask = 0x00ff0000 },
	[DESC_RATEMCS3]	= { .addr = 0xe10, .mask = 0xff000000 },
	[DESC_RATEMCS4]	= { .addr = 0xe14, .mask = 0x000000ff },
	[DESC_RATEMCS5]	= { .addr = 0xe14, .mask = 0x0000ff00 },
	[DESC_RATEMCS6]	= { .addr = 0xe14, .mask = 0x00ff0000 },
	[DESC_RATEMCS7]	= { .addr = 0xe14, .mask = 0xff000000 },
};

#define WLAN_TXQ_RPT_EN		0x1F
#define WLAN_SLOT_TIME		0x09
#define WLAN_RL_VAL		0x3030
#define WLAN_BAR_VAL		0x0201ffff
#define BIT_MASK_TBTT_HOLD	0x00000fff
#define BIT_SHIFT_TBTT_HOLD	8
#define BIT_MASK_TBTT_SETUP	0x000000ff
#define BIT_SHIFT_TBTT_SETUP	0
#define BIT_MASK_TBTT_MASK	((BIT_MASK_TBTT_HOLD << BIT_SHIFT_TBTT_HOLD) | \
				 (BIT_MASK_TBTT_SETUP << BIT_SHIFT_TBTT_SETUP))
#define TBTT_TIME(s, h)((((s) & BIT_MASK_TBTT_SETUP) << BIT_SHIFT_TBTT_SETUP) |\
			(((h) & BIT_MASK_TBTT_HOLD) << BIT_SHIFT_TBTT_HOLD))
#define WLAN_TBTT_TIME_NORMAL	TBTT_TIME(0x04, 0x80)
#define WLAN_TBTT_TIME_STOP_BCN	TBTT_TIME(0x04, 0x64)
#define WLAN_PIFS_VAL		0
#define WLAN_AGG_BRK_TIME	0x16
#define WLAN_NAV_PROT_LEN	0x0040
#define WLAN_SPEC_SIFS		0x100a
#define WLAN_RX_PKT_LIMIT	0x17
#define WLAN_MAX_AGG_NR		0x0A
#define WLAN_AMPDU_MAX_TIME	0x1C
#define WLAN_ANT_SEL		0x82
#define WLAN_LTR_IDLE_LAT	0x883C883C
#define WLAN_LTR_ACT_LAT	0x880B880B
#define WLAN_LTR_CTRL1		0xCB004010
#define WLAN_LTR_CTRL2		0x01233425

static void rtw8723d_phy_set_param(struct rtw_dev *rtwdev)
{
	u8 xtal_cap;
	u32 val32;

	/* power on BB/RF domain */
	rtw_write16_set(rtwdev, REG_SYS_FUNC_EN,
			BIT_FEN_EN_25_1 | BIT_FEN_BB_GLB_RST | BIT_FEN_BB_RSTB);
	rtw_write8_set(rtwdev, REG_RF_CTRL,
		       BIT_RF_EN | BIT_RF_RSTB | BIT_RF_SDM_RSTB);
	rtw_write8(rtwdev, REG_AFE_CTRL1 + 1, 0x80);

	rtw_phy_load_tables(rtwdev);

	/* post init after header files config */
	rtw_write32_clr(rtwdev, REG_RCR, BIT_RCR_ADF);
	rtw_write8_set(rtwdev, REG_HIQ_NO_LMT_EN, BIT_HIQ_NO_LMT_EN_ROOT);
	rtw_write16_set(rtwdev, REG_AFE_CTRL_4, BIT_CK320M_AFE_EN | BIT_EN_SYN);

	xtal_cap = rtwdev->efuse.crystal_cap & 0x3F;
	rtw_write32_mask(rtwdev, REG_AFE_CTRL3, BIT_MASK_XTAL,
			 xtal_cap | (xtal_cap << 6));
	rtw_write32_set(rtwdev, REG_FPGA0_RFMOD, BIT_CCKEN | BIT_OFDMEN);
	if ((rtwdev->efuse.afe >> 4) == 14) {
		rtw_write32_set(rtwdev, REG_AFE_CTRL3, BIT_XTAL_GMP_BIT4);
		rtw_write32_clr(rtwdev, REG_AFE_CTRL1, BITS_PLL);
		rtw_write32_set(rtwdev, REG_LDO_SWR_CTRL, BIT_XTA1);
		rtw_write32_clr(rtwdev, REG_LDO_SWR_CTRL, BIT_XTA0);
	}

	rtw_write8(rtwdev, REG_SLOT, WLAN_SLOT_TIME);
	rtw_write8(rtwdev, REG_FWHW_TXQ_CTRL + 1, WLAN_TXQ_RPT_EN);
	rtw_write16(rtwdev, REG_RETRY_LIMIT, WLAN_RL_VAL);
	rtw_write32(rtwdev, REG_BAR_MODE_CTRL, WLAN_BAR_VAL);
	rtw_write8(rtwdev, REG_ATIMWND, 0x2);
	rtw_write8(rtwdev, REG_BCN_CTRL,
		   BIT_DIS_TSF_UDT | BIT_EN_BCN_FUNCTION | BIT_EN_TXBCN_RPT);
	val32 = rtw_read32(rtwdev, REG_TBTT_PROHIBIT);
	val32 &= ~BIT_MASK_TBTT_MASK;
	val32 |= WLAN_TBTT_TIME_STOP_BCN;
	rtw_write8(rtwdev, REG_TBTT_PROHIBIT, val32);
	rtw_write8(rtwdev, REG_PIFS, WLAN_PIFS_VAL);
	rtw_write8(rtwdev, REG_AGGR_BREAK_TIME, WLAN_AGG_BRK_TIME);
	rtw_write16(rtwdev, REG_NAV_PROT_LEN, WLAN_NAV_PROT_LEN);
	rtw_write16(rtwdev, REG_MAC_SPEC_SIFS, WLAN_SPEC_SIFS);
	rtw_write16(rtwdev, REG_SIFS, WLAN_SPEC_SIFS);
	rtw_write16(rtwdev, REG_SIFS + 2, WLAN_SPEC_SIFS);
	rtw_write8(rtwdev, REG_SINGLE_AMPDU_CTRL, BIT_EN_SINGLE_APMDU);
	rtw_write8(rtwdev, REG_RX_PKT_LIMIT, WLAN_RX_PKT_LIMIT);
	rtw_write8(rtwdev, REG_MAX_AGGR_NUM, WLAN_MAX_AGG_NR);
	rtw_write8(rtwdev, REG_AMPDU_MAX_TIME, WLAN_AMPDU_MAX_TIME);
	rtw_write8(rtwdev, REG_LEDCFG2, WLAN_ANT_SEL);

	rtw_write32(rtwdev, REG_LTR_IDLE_LATENCY, WLAN_LTR_IDLE_LAT);
	rtw_write32(rtwdev, REG_LTR_ACTIVE_LATENCY, WLAN_LTR_ACT_LAT);
	rtw_write32(rtwdev, REG_LTR_CTRL_BASIC, WLAN_LTR_CTRL1);
	rtw_write32(rtwdev, REG_LTR_CTRL_BASIC + 4, WLAN_LTR_CTRL2);

	rtw_phy_init(rtwdev);

	rtw_write16_set(rtwdev, REG_TXDMA_OFFSET_CHK, BIT_DROP_DATA_EN);
	rtw_write32_mask(rtwdev, REG_OFDM0_XAAGC1, MASKBYTE0, 0x50);
	rtw_write32_mask(rtwdev, REG_OFDM0_XAAGC1, MASKBYTE0, 0x20);
}

static void rtw8723de_efuse_parsing(struct rtw_efuse *efuse,
				    struct rtw8723d_efuse *map)
{
	ether_addr_copy(efuse->addr, map->e.mac_addr);
}

static int rtw8723d_read_efuse(struct rtw_dev *rtwdev, u8 *log_map)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw8723d_efuse *map;
	int i;

	map = (struct rtw8723d_efuse *)log_map;

	efuse->rfe_option = 0;
	efuse->rf_board_option = map->rf_board_option;
	efuse->crystal_cap = map->xtal_k;
	efuse->pa_type_2g = map->pa_type;
	efuse->lna_type_2g = map->lna_type_2g[0];
	efuse->channel_plan = map->channel_plan;
	efuse->country_code[0] = map->country_code[0];
	efuse->country_code[1] = map->country_code[1];
	efuse->bt_setting = map->rf_bt_setting;
	efuse->regd = map->rf_board_option & 0x7;
	efuse->thermal_meter[0] = map->thermal_meter;
	efuse->thermal_meter_k = map->thermal_meter;
	efuse->afe = map->afe;

	for (i = 0; i < 4; i++)
		efuse->txpwr_idx_table[i] = map->txpwr_idx_table[i];

	switch (rtw_hci_type(rtwdev)) {
	case RTW_HCI_TYPE_PCIE:
		rtw8723de_efuse_parsing(efuse, map);
		break;
	default:
		/* unsupported now */
		return -ENOTSUPP;
	}

	return 0;
}

static void query_phy_status_page0(struct rtw_dev *rtwdev, u8 *phy_status,
				   struct rtw_rx_pkt_stat *pkt_stat)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	s8 min_rx_power = -120;
	u8 pwdb = GET_PHY_STAT_P0_PWDB(phy_status);

	pkt_stat->rx_power[RF_PATH_A] = pwdb - 97;
	pkt_stat->rssi = rtw_phy_rf_power_2_rssi(pkt_stat->rx_power, 1);
	pkt_stat->bw = RTW_CHANNEL_WIDTH_20;
	pkt_stat->signal_power = max(pkt_stat->rx_power[RF_PATH_A],
				     min_rx_power);
	dm_info->rssi[RF_PATH_A] = pkt_stat->rssi;
}

static void query_phy_status_page1(struct rtw_dev *rtwdev, u8 *phy_status,
				   struct rtw_rx_pkt_stat *pkt_stat)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 rxsc, bw;
	s8 min_rx_power = -120;
	s8 rx_evm;

	if (pkt_stat->rate > DESC_RATE11M && pkt_stat->rate < DESC_RATEMCS0)
		rxsc = GET_PHY_STAT_P1_L_RXSC(phy_status);
	else
		rxsc = GET_PHY_STAT_P1_HT_RXSC(phy_status);

	if (GET_PHY_STAT_P1_RF_MODE(phy_status) == 0)
		bw = RTW_CHANNEL_WIDTH_20;
	else if ((rxsc == 1) || (rxsc == 2))
		bw = RTW_CHANNEL_WIDTH_20;
	else
		bw = RTW_CHANNEL_WIDTH_40;

	pkt_stat->rx_power[RF_PATH_A] = GET_PHY_STAT_P1_PWDB_A(phy_status) - 110;
	pkt_stat->rssi = rtw_phy_rf_power_2_rssi(pkt_stat->rx_power, 1);
	pkt_stat->bw = bw;
	pkt_stat->signal_power = max(pkt_stat->rx_power[RF_PATH_A],
				     min_rx_power);
	pkt_stat->rx_evm[RF_PATH_A] = GET_PHY_STAT_P1_RXEVM_A(phy_status);
	pkt_stat->rx_snr[RF_PATH_A] = GET_PHY_STAT_P1_RXSNR_A(phy_status);
	pkt_stat->cfo_tail[RF_PATH_A] = GET_PHY_STAT_P1_CFO_TAIL_A(phy_status);

	dm_info->curr_rx_rate = pkt_stat->rate;
	dm_info->rssi[RF_PATH_A] = pkt_stat->rssi;
	dm_info->rx_snr[RF_PATH_A] = pkt_stat->rx_snr[RF_PATH_A] >> 1;
	dm_info->cfo_tail[RF_PATH_A] = (pkt_stat->cfo_tail[RF_PATH_A] * 5) >> 1;

	rx_evm = clamp_t(s8, -pkt_stat->rx_evm[RF_PATH_A] >> 1, 0, 64);
	rx_evm &= 0x3F;	/* 64->0: second path of 1SS rate is 64 */
	dm_info->rx_evm_dbm[RF_PATH_A] = rx_evm;
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

static void rtw8723d_query_rx_desc(struct rtw_dev *rtwdev, u8 *rx_desc,
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
	pkt_stat->ppdu_cnt = 0;
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

static bool rtw8723d_check_spur_ov_thres(struct rtw_dev *rtwdev,
					 u8 channel, u32 thres)
{
	u32 freq;
	bool ret = false;

	if (channel == 13)
		freq = FREQ_CH13;
	else if (channel == 14)
		freq = FREQ_CH14;
	else
		return false;

	rtw_write32(rtwdev, REG_ANALOG_P4, DIS_3WIRE);
	rtw_write32(rtwdev, REG_PSDFN, freq);
	rtw_write32(rtwdev, REG_PSDFN, START_PSD | freq);

	msleep(30);
	if (rtw_read32(rtwdev, REG_PSDRPT) >= thres)
		ret = true;

	rtw_write32(rtwdev, REG_PSDFN, freq);
	rtw_write32(rtwdev, REG_ANALOG_P4, EN_3WIRE);

	return ret;
}

static void rtw8723d_cfg_notch(struct rtw_dev *rtwdev, u8 channel, bool notch)
{
	if (!notch) {
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_MASK_RXDSP, 0x1f);
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_EN_RXDSP, 0x0);
		rtw_write32(rtwdev, REG_OFDM1_CSI1, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI2, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI3, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI4, 0x00000000);
		rtw_write32_mask(rtwdev, REG_OFDM1_CFOTRK, BIT_EN_CFOTRK, 0x0);
		return;
	}

	switch (channel) {
	case 13:
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_MASK_RXDSP, 0xb);
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_EN_RXDSP, 0x1);
		rtw_write32(rtwdev, REG_OFDM1_CSI1, 0x04000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI2, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI3, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI4, 0x00000000);
		rtw_write32_mask(rtwdev, REG_OFDM1_CFOTRK, BIT_EN_CFOTRK, 0x1);
		break;
	case 14:
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_MASK_RXDSP, 0x5);
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_EN_RXDSP, 0x1);
		rtw_write32(rtwdev, REG_OFDM1_CSI1, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI2, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI3, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI4, 0x00080000);
		rtw_write32_mask(rtwdev, REG_OFDM1_CFOTRK, BIT_EN_CFOTRK, 0x1);
		break;
	default:
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_EN_RXDSP, 0x0);
		rtw_write32_mask(rtwdev, REG_OFDM1_CFOTRK, BIT_EN_CFOTRK, 0x0);
		break;
	}
}

static void rtw8723d_spur_cal(struct rtw_dev *rtwdev, u8 channel)
{
	bool notch;

	if (channel < 13) {
		rtw8723d_cfg_notch(rtwdev, channel, false);
		return;
	}

	notch = rtw8723d_check_spur_ov_thres(rtwdev, channel, SPUR_THRES);
	rtw8723d_cfg_notch(rtwdev, channel, notch);
}

static void rtw8723d_set_channel_rf(struct rtw_dev *rtwdev, u8 channel, u8 bw)
{
	u32 rf_cfgch_a, rf_cfgch_b;

	rf_cfgch_a = rtw_read_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK);
	rf_cfgch_b = rtw_read_rf(rtwdev, RF_PATH_B, RF_CFGCH, RFREG_MASK);

	rf_cfgch_a &= ~RFCFGCH_CHANNEL_MASK;
	rf_cfgch_b &= ~RFCFGCH_CHANNEL_MASK;
	rf_cfgch_a |= (channel & RFCFGCH_CHANNEL_MASK);
	rf_cfgch_b |= (channel & RFCFGCH_CHANNEL_MASK);

	rf_cfgch_a &= ~RFCFGCH_BW_MASK;
	switch (bw) {
	case RTW_CHANNEL_WIDTH_20:
		rf_cfgch_a |= RFCFGCH_BW_20M;
		break;
	case RTW_CHANNEL_WIDTH_40:
		rf_cfgch_a |= RFCFGCH_BW_40M;
		break;
	default:
		break;
	}

	rtw_write_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK, rf_cfgch_a);
	rtw_write_rf(rtwdev, RF_PATH_B, RF_CFGCH, RFREG_MASK, rf_cfgch_b);

	rtw8723d_spur_cal(rtwdev, channel);
}

static const struct rtw_backup_info cck_dfir_cfg[][CCK_DFIR_NR] = {
	[0] = {
		{ .len = 4, .reg = 0xA24, .val = 0x64B80C1C },
		{ .len = 4, .reg = 0xA28, .val = 0x00008810 },
		{ .len = 4, .reg = 0xAAC, .val = 0x01235667 },
	},
	[1] = {
		{ .len = 4, .reg = 0xA24, .val = 0x0000B81C },
		{ .len = 4, .reg = 0xA28, .val = 0x00000000 },
		{ .len = 4, .reg = 0xAAC, .val = 0x00003667 },
	},
};

static void rtw8723d_set_channel_bb(struct rtw_dev *rtwdev, u8 channel, u8 bw,
				    u8 primary_ch_idx)
{
	const struct rtw_backup_info *cck_dfir;
	int i;

	cck_dfir = channel <= 13 ? cck_dfir_cfg[0] : cck_dfir_cfg[1];

	for (i = 0; i < CCK_DFIR_NR; i++, cck_dfir++)
		rtw_write32(rtwdev, cck_dfir->reg, cck_dfir->val);

	switch (bw) {
	case RTW_CHANNEL_WIDTH_20:
		rtw_write32_mask(rtwdev, REG_FPGA0_RFMOD, BIT_MASK_RFMOD, 0x0);
		rtw_write32_mask(rtwdev, REG_FPGA1_RFMOD, BIT_MASK_RFMOD, 0x0);
		rtw_write32_mask(rtwdev, REG_BBRX_DFIR, BIT_RXBB_DFIR_EN, 1);
		rtw_write32_mask(rtwdev, REG_BBRX_DFIR, BIT_MASK_RXBB_DFIR, 0xa);
		break;
	case RTW_CHANNEL_WIDTH_40:
		rtw_write32_mask(rtwdev, REG_FPGA0_RFMOD, BIT_MASK_RFMOD, 0x1);
		rtw_write32_mask(rtwdev, REG_FPGA1_RFMOD, BIT_MASK_RFMOD, 0x1);
		rtw_write32_mask(rtwdev, REG_BBRX_DFIR, BIT_RXBB_DFIR_EN, 0);
		rtw_write32_mask(rtwdev, REG_CCK0_SYS, BIT_CCK_SIDE_BAND,
				 (primary_ch_idx == RTW_SC_20_UPPER ? 1 : 0));
		break;
	default:
		break;
	}
}

static void rtw8723d_set_channel(struct rtw_dev *rtwdev, u8 channel, u8 bw,
				 u8 primary_chan_idx)
{
	rtw8723d_set_channel_rf(rtwdev, channel, bw);
	rtw_set_channel_mac(rtwdev, channel, bw, primary_chan_idx);
	rtw8723d_set_channel_bb(rtwdev, channel, bw, primary_chan_idx);
}

#define BIT_CFENDFORM		BIT(9)
#define BIT_WMAC_TCR_ERR0	BIT(12)
#define BIT_WMAC_TCR_ERR1	BIT(13)
#define BIT_TCR_CFG		(BIT_CFENDFORM | BIT_WMAC_TCR_ERR0 |	       \
				 BIT_WMAC_TCR_ERR1)
#define WLAN_RX_FILTER0		0xFFFF
#define WLAN_RX_FILTER1		0x400
#define WLAN_RX_FILTER2		0xFFFF
#define WLAN_RCR_CFG		0x700060CE

static int rtw8723d_mac_init(struct rtw_dev *rtwdev)
{
	rtw_write8(rtwdev, REG_FWHW_TXQ_CTRL + 1, WLAN_TXQ_RPT_EN);
	rtw_write32(rtwdev, REG_TCR, BIT_TCR_CFG);

	rtw_write16(rtwdev, REG_RXFLTMAP0, WLAN_RX_FILTER0);
	rtw_write16(rtwdev, REG_RXFLTMAP1, WLAN_RX_FILTER1);
	rtw_write16(rtwdev, REG_RXFLTMAP2, WLAN_RX_FILTER2);
	rtw_write32(rtwdev, REG_RCR, WLAN_RCR_CFG);

	rtw_write32(rtwdev, REG_INT_MIG, 0);
	rtw_write32(rtwdev, REG_MCUTST_1, 0x0);

	rtw_write8(rtwdev, REG_MISC_CTRL, BIT_DIS_SECOND_CCA);
	rtw_write8(rtwdev, REG_2ND_CCA_CTRL, 0);

	return 0;
}

static void rtw8723d_cfg_ldo25(struct rtw_dev *rtwdev, bool enable)
{
	u8 ldo_pwr;

	ldo_pwr = rtw_read8(rtwdev, REG_LDO_EFUSE_CTRL + 3);
	if (enable) {
		ldo_pwr &= ~BIT_MASK_LDO25_VOLTAGE;
		ldo_pwr = (BIT_LDO25_VOLTAGE_V25 << 4) | BIT_LDO25_EN;
	} else {
		ldo_pwr &= ~BIT_LDO25_EN;
	}
	rtw_write8(rtwdev, REG_LDO_EFUSE_CTRL + 3, ldo_pwr);
}

static void
rtw8723d_set_tx_power_index_by_rate(struct rtw_dev *rtwdev, u8 path, u8 rs)
{
	struct rtw_hal *hal = &rtwdev->hal;
	const struct rtw_hw_reg *txagc;
	u8 rate, pwr_index;
	int j;

	for (j = 0; j < rtw_rate_size[rs]; j++) {
		rate = rtw_rate_section[rs][j];
		pwr_index = hal->tx_pwr_tbl[path][rate];

		if (rate >= ARRAY_SIZE(rtw8723d_txagc)) {
			rtw_warn(rtwdev, "rate 0x%x isn't supported\n", rate);
			continue;
		}
		txagc = &rtw8723d_txagc[rate];
		if (!txagc->addr) {
			rtw_warn(rtwdev, "rate 0x%x isn't defined\n", rate);
			continue;
		}

		rtw_write32_mask(rtwdev, txagc->addr, txagc->mask, pwr_index);
	}
}

static void rtw8723d_set_tx_power_index(struct rtw_dev *rtwdev)
{
	struct rtw_hal *hal = &rtwdev->hal;
	int rs, path;

	for (path = 0; path < hal->rf_path_num; path++) {
		for (rs = 0; rs <= RTW_RATE_SECTION_HT_1S; rs++)
			rtw8723d_set_tx_power_index_by_rate(rtwdev, path, rs);
	}
}

static void rtw8723d_efuse_grant(struct rtw_dev *rtwdev, bool on)
{
	if (on) {
		rtw_write8(rtwdev, REG_EFUSE_ACCESS, EFUSE_ACCESS_ON);

		rtw_write16_set(rtwdev, REG_SYS_FUNC_EN, BIT_FEN_ELDR);
		rtw_write16_set(rtwdev, REG_SYS_CLKR, BIT_LOADER_CLK_EN | BIT_ANA8M);
	} else {
		rtw_write8(rtwdev, REG_EFUSE_ACCESS, EFUSE_ACCESS_OFF);
	}
}

static void rtw8723d_false_alarm_statistics(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u32 cck_fa_cnt;
	u32 ofdm_fa_cnt;
	u32 crc32_cnt;
	u32 val32;

	/* hold counter */
	rtw_write32_mask(rtwdev, REG_OFDM_FA_HOLDC_11N, BIT_MASK_OFDM_FA_KEEP, 1);
	rtw_write32_mask(rtwdev, REG_OFDM_FA_RSTD_11N, BIT_MASK_OFDM_FA_KEEP1, 1);
	rtw_write32_mask(rtwdev, REG_CCK_FA_RST_11N, BIT_MASK_CCK_CNT_KEEP, 1);
	rtw_write32_mask(rtwdev, REG_CCK_FA_RST_11N, BIT_MASK_CCK_FA_KEEP, 1);

	cck_fa_cnt = rtw_read32_mask(rtwdev, REG_CCK_FA_LSB_11N, MASKBYTE0);
	cck_fa_cnt += rtw_read32_mask(rtwdev, REG_CCK_FA_MSB_11N, MASKBYTE3) << 8;

	val32 = rtw_read32(rtwdev, REG_OFDM_FA_TYPE1_11N);
	ofdm_fa_cnt = u32_get_bits(val32, BIT_MASK_OFDM_FF_CNT);
	ofdm_fa_cnt += u32_get_bits(val32, BIT_MASK_OFDM_SF_CNT);
	val32 = rtw_read32(rtwdev, REG_OFDM_FA_TYPE2_11N);
	dm_info->ofdm_cca_cnt = u32_get_bits(val32, BIT_MASK_OFDM_CCA_CNT);
	ofdm_fa_cnt += u32_get_bits(val32, BIT_MASK_OFDM_PF_CNT);
	val32 = rtw_read32(rtwdev, REG_OFDM_FA_TYPE3_11N);
	ofdm_fa_cnt += u32_get_bits(val32, BIT_MASK_OFDM_RI_CNT);
	ofdm_fa_cnt += u32_get_bits(val32, BIT_MASK_OFDM_CRC_CNT);
	val32 = rtw_read32(rtwdev, REG_OFDM_FA_TYPE4_11N);
	ofdm_fa_cnt += u32_get_bits(val32, BIT_MASK_OFDM_MNS_CNT);

	dm_info->cck_fa_cnt = cck_fa_cnt;
	dm_info->ofdm_fa_cnt = ofdm_fa_cnt;
	dm_info->total_fa_cnt = cck_fa_cnt + ofdm_fa_cnt;

	dm_info->cck_err_cnt = rtw_read32(rtwdev, REG_IGI_C_11N);
	dm_info->cck_ok_cnt = rtw_read32(rtwdev, REG_IGI_D_11N);
	crc32_cnt = rtw_read32(rtwdev, REG_OFDM_CRC32_CNT_11N);
	dm_info->ofdm_err_cnt = u32_get_bits(crc32_cnt, BIT_MASK_OFDM_LCRC_ERR);
	dm_info->ofdm_ok_cnt = u32_get_bits(crc32_cnt, BIT_MASK_OFDM_LCRC_OK);
	crc32_cnt = rtw_read32(rtwdev, REG_HT_CRC32_CNT_11N);
	dm_info->ht_err_cnt = u32_get_bits(crc32_cnt, BIT_MASK_HT_CRC_ERR);
	dm_info->ht_ok_cnt = u32_get_bits(crc32_cnt, BIT_MASK_HT_CRC_OK);
	dm_info->vht_err_cnt = 0;
	dm_info->vht_ok_cnt = 0;

	val32 = rtw_read32(rtwdev, REG_CCK_CCA_CNT_11N);
	dm_info->cck_cca_cnt = (u32_get_bits(val32, BIT_MASK_CCK_FA_MSB) << 8) |
			       u32_get_bits(val32, BIT_MASK_CCK_FA_LSB);
	dm_info->total_cca_cnt = dm_info->cck_cca_cnt + dm_info->ofdm_cca_cnt;

	/* reset counter */
	rtw_write32_mask(rtwdev, REG_OFDM_FA_RSTC_11N, BIT_MASK_OFDM_FA_RST, 1);
	rtw_write32_mask(rtwdev, REG_OFDM_FA_RSTC_11N, BIT_MASK_OFDM_FA_RST, 0);
	rtw_write32_mask(rtwdev, REG_OFDM_FA_RSTD_11N, BIT_MASK_OFDM_FA_RST1, 1);
	rtw_write32_mask(rtwdev, REG_OFDM_FA_RSTD_11N, BIT_MASK_OFDM_FA_RST1, 0);
	rtw_write32_mask(rtwdev, REG_OFDM_FA_HOLDC_11N, BIT_MASK_OFDM_FA_KEEP, 0);
	rtw_write32_mask(rtwdev, REG_OFDM_FA_RSTD_11N, BIT_MASK_OFDM_FA_KEEP1, 0);
	rtw_write32_mask(rtwdev, REG_CCK_FA_RST_11N, BIT_MASK_CCK_CNT_KPEN, 0);
	rtw_write32_mask(rtwdev, REG_CCK_FA_RST_11N, BIT_MASK_CCK_CNT_KPEN, 2);
	rtw_write32_mask(rtwdev, REG_CCK_FA_RST_11N, BIT_MASK_CCK_FA_KPEN, 0);
	rtw_write32_mask(rtwdev, REG_CCK_FA_RST_11N, BIT_MASK_CCK_FA_KPEN, 2);
	rtw_write32_mask(rtwdev, REG_PAGE_F_RST_11N, BIT_MASK_F_RST_ALL, 1);
	rtw_write32_mask(rtwdev, REG_PAGE_F_RST_11N, BIT_MASK_F_RST_ALL, 0);
}

static struct rtw_chip_ops rtw8723d_ops = {
	.phy_set_param		= rtw8723d_phy_set_param,
	.read_efuse		= rtw8723d_read_efuse,
	.query_rx_desc		= rtw8723d_query_rx_desc,
	.set_channel		= rtw8723d_set_channel,
	.mac_init		= rtw8723d_mac_init,
	.read_rf		= rtw_phy_read_rf_sipi,
	.write_rf		= rtw_phy_write_rf_reg_sipi,
	.set_tx_power_index	= rtw8723d_set_tx_power_index,
	.set_antenna		= NULL,
	.cfg_ldo25		= rtw8723d_cfg_ldo25,
	.efuse_grant		= rtw8723d_efuse_grant,
	.false_alarm_statistics	= rtw8723d_false_alarm_statistics,
	.config_bfee		= NULL,
	.set_gid_table		= NULL,
	.cfg_csi_rate		= NULL,
};

static const struct rtw_pwr_seq_cmd trans_carddis_to_cardemu_8723d[] = {
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(7), 0},
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
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(4), 0},
	{0x0023,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(4), 0},
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

static const struct rtw_pwr_seq_cmd trans_cardemu_to_act_8723d[] = {
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
	 RTW_PWR_CMD_POLLING, (BIT(1) | BIT(0)), 0},
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
	{0x0010,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(6), BIT(6)},
	{0x0049,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), BIT(1)},
	{0x0063,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), BIT(1)},
	{0x0062,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0058,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x005A,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), BIT(1)},
	{0x0068,
	 RTW_PWR_CUT_TEST_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3), BIT(3)},
	{0x0069,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(6), BIT(6)},
	{0x001f,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x00},
	{0x0077,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x00},
	{0x001f,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x07},
	{0x0077,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x07},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd *card_enable_flow_8723d[] = {
	trans_carddis_to_cardemu_8723d,
	trans_cardemu_to_act_8723d,
	NULL
};

static const struct rtw_pwr_seq_cmd trans_act_to_lps_8723d[] = {
	{0x0301,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0xFF},
	{0x0522,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0xFF},
	{0x05F8,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, 0xFF, 0},
	{0x05F9,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, 0xFF, 0},
	{0x05FA,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, 0xFF, 0},
	{0x05FB,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, 0xFF, 0},
	{0x0002,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0002,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_DELAY, 0, RTW_PWR_DELAY_US},
	{0x0002,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0100,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x03},
	{0x0101,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0093,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x00},
	{0x0553,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), BIT(5)},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd trans_act_to_pre_carddis_8723d[] = {
	{0x0003,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(2), 0},
	{0x0080,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd trans_act_to_cardemu_8723d[] = {
	{0x0002,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
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
	{0x0010,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(6), 0},
	{0x0000,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), BIT(5)},
	{0x0020,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd trans_cardemu_to_carddis_8723d[] = {
	{0x0007,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x20},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(4), BIT(3)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(2), BIT(2)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(4), BIT(3) | BIT(4)},
	{0x004A,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 1},
	{0x0023,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(4), BIT(4)},
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
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd trans_act_to_post_carddis_8723d[] = {
	{0x001D,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x001D,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x001C,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x0E},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd *card_disable_flow_8723d[] = {
	trans_act_to_lps_8723d,
	trans_act_to_pre_carddis_8723d,
	trans_act_to_cardemu_8723d,
	trans_cardemu_to_carddis_8723d,
	trans_act_to_post_carddis_8723d,
	NULL
};

static const struct rtw_page_table page_table_8723d[] = {
	{12, 2, 2, 0, 1},
	{12, 2, 2, 0, 1},
	{12, 2, 2, 0, 1},
	{12, 2, 2, 0, 1},
	{12, 2, 2, 0, 1},
};

static const struct rtw_rqpn rqpn_table_8723d[] = {
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

static const struct rtw_intf_phy_para pcie_gen1_param_8723d[] = {
	{0x0008, 0x4a22,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_ALL,
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0x0009, 0x1000,
	 RTW_IP_SEL_PHY,
	 ~(RTW_INTF_PHY_CUT_A | RTW_INTF_PHY_CUT_B),
	 RTW_INTF_PHY_PLATFORM_ALL},
	{0xFFFF, 0x0000,
	 RTW_IP_SEL_PHY,
	 RTW_INTF_PHY_CUT_ALL,
	 RTW_INTF_PHY_PLATFORM_ALL},
};

static const struct rtw_intf_phy_para_table phy_para_table_8723d = {
	.gen1_para	= pcie_gen1_param_8723d,
	.n_gen1_para	= ARRAY_SIZE(pcie_gen1_param_8723d),
};

static const struct rtw_hw_reg rtw8723d_dig[] = {
	[0] = { .addr = 0xc50, .mask = 0x7f },
	[1] = { .addr = 0xc50, .mask = 0x7f },
};

static const struct rtw_hw_reg rtw8723d_dig_cck[] = {
	[0] = { .addr = 0xa0c, .mask = 0x3f00 },
};

static const struct rtw_rf_sipi_addr rtw8723d_rf_sipi_addr[] = {
	[RF_PATH_A] = { .hssi_1 = 0x820, .lssi_read    = 0x8a0,
			.hssi_2 = 0x824, .lssi_read_pi = 0x8b8},
	[RF_PATH_B] = { .hssi_1 = 0x828, .lssi_read    = 0x8a4,
			.hssi_2 = 0x82c, .lssi_read_pi = 0x8bc},
};

static const struct rtw_rfe_def rtw8723d_rfe_defs[] = {
	[0] = { .phy_pg_tbl	= &rtw8723d_bb_pg_tbl,
		.txpwr_lmt_tbl	= &rtw8723d_txpwr_lmt_tbl,},
};

struct rtw_chip_info rtw8723d_hw_spec = {
	.ops = &rtw8723d_ops,
	.id = RTW_CHIP_TYPE_8723D,
	.fw_name = "rtw88/rtw8723d_fw.bin",
	.wlan_cpu = RTW_WCPU_11N,
	.tx_pkt_desc_sz = 40,
	.tx_buf_desc_sz = 16,
	.rx_pkt_desc_sz = 24,
	.rx_buf_desc_sz = 8,
	.phy_efuse_size = 512,
	.log_efuse_size = 512,
	.ptct_efuse_size = 96 + 1,
	.txff_size = 32768,
	.rxff_size = 16384,
	.txgi_factor = 1,
	.is_pwr_by_rate_dec = true,
	.max_power_index = 0x3f,
	.csi_buf_pg_num = 0,
	.band = RTW_BAND_2G,
	.page_size = 128,
	.dig_min = 0x20,
	.ht_supported = true,
	.vht_supported = false,
	.lps_deep_mode_supported = 0,
	.sys_func_en = 0xFD,
	.pwr_on_seq = card_enable_flow_8723d,
	.pwr_off_seq = card_disable_flow_8723d,
	.page_table = page_table_8723d,
	.rqpn_table = rqpn_table_8723d,
	.intf_table = &phy_para_table_8723d,
	.dig = rtw8723d_dig,
	.dig_cck = rtw8723d_dig_cck,
	.rf_sipi_addr = {0x840, 0x844},
	.rf_sipi_read_addr = rtw8723d_rf_sipi_addr,
	.fix_rf_phy_num = 2,
	.mac_tbl = &rtw8723d_mac_tbl,
	.agc_tbl = &rtw8723d_agc_tbl,
	.bb_tbl = &rtw8723d_bb_tbl,
	.rf_tbl = {&rtw8723d_rf_a_tbl},
	.rfe_defs = rtw8723d_rfe_defs,
	.rfe_defs_size = ARRAY_SIZE(rtw8723d_rfe_defs),
	.rx_ldpc = false,
};
EXPORT_SYMBOL(rtw8723d_hw_spec);

MODULE_FIRMWARE("rtw88/rtw8723d_fw.bin");
