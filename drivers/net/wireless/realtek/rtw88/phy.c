// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include <linux/bcd.h>

#include "main.h"
#include "reg.h"
#include "fw.h"
#include "phy.h"
#include "debug.h"
#include "regd.h"

struct phy_cfg_pair {
	u32 addr;
	u32 data;
};

union phy_table_tile {
	struct rtw_phy_cond cond;
	struct phy_cfg_pair cfg;
};

static const u32 db_invert_table[12][8] = {
	{10,		13,		16,		20,
	 25,		32,		40,		50},
	{64,		80,		101,		128,
	 160,		201,		256,		318},
	{401,		505,		635,		800,
	 1007,		1268,		1596,		2010},
	{316,		398,		501,		631,
	 794,		1000,		1259,		1585},
	{1995,		2512,		3162,		3981,
	 5012,		6310,		7943,		10000},
	{12589,		15849,		19953,		25119,
	 31623,		39811,		50119,		63098},
	{79433,		100000,		125893,		158489,
	 199526,	251189,		316228,		398107},
	{501187,	630957,		794328,		1000000,
	 1258925,	1584893,	1995262,	2511886},
	{3162278,	3981072,	5011872,	6309573,
	 7943282,	1000000,	12589254,	15848932},
	{19952623,	25118864,	31622777,	39810717,
	 50118723,	63095734,	79432823,	100000000},
	{125892541,	158489319,	199526232,	251188643,
	 316227766,	398107171,	501187234,	630957345},
	{794328235,	1000000000,	1258925412,	1584893192,
	 1995262315,	2511886432U,	3162277660U,	3981071706U}
};

u8 rtw_cck_rates[] = { DESC_RATE1M, DESC_RATE2M, DESC_RATE5_5M, DESC_RATE11M };
u8 rtw_ofdm_rates[] = {
	DESC_RATE6M,  DESC_RATE9M,  DESC_RATE12M,
	DESC_RATE18M, DESC_RATE24M, DESC_RATE36M,
	DESC_RATE48M, DESC_RATE54M
};
u8 rtw_ht_1s_rates[] = {
	DESC_RATEMCS0, DESC_RATEMCS1, DESC_RATEMCS2,
	DESC_RATEMCS3, DESC_RATEMCS4, DESC_RATEMCS5,
	DESC_RATEMCS6, DESC_RATEMCS7
};
u8 rtw_ht_2s_rates[] = {
	DESC_RATEMCS8,  DESC_RATEMCS9,  DESC_RATEMCS10,
	DESC_RATEMCS11, DESC_RATEMCS12, DESC_RATEMCS13,
	DESC_RATEMCS14, DESC_RATEMCS15
};
u8 rtw_vht_1s_rates[] = {
	DESC_RATEVHT1SS_MCS0, DESC_RATEVHT1SS_MCS1,
	DESC_RATEVHT1SS_MCS2, DESC_RATEVHT1SS_MCS3,
	DESC_RATEVHT1SS_MCS4, DESC_RATEVHT1SS_MCS5,
	DESC_RATEVHT1SS_MCS6, DESC_RATEVHT1SS_MCS7,
	DESC_RATEVHT1SS_MCS8, DESC_RATEVHT1SS_MCS9
};
u8 rtw_vht_2s_rates[] = {
	DESC_RATEVHT2SS_MCS0, DESC_RATEVHT2SS_MCS1,
	DESC_RATEVHT2SS_MCS2, DESC_RATEVHT2SS_MCS3,
	DESC_RATEVHT2SS_MCS4, DESC_RATEVHT2SS_MCS5,
	DESC_RATEVHT2SS_MCS6, DESC_RATEVHT2SS_MCS7,
	DESC_RATEVHT2SS_MCS8, DESC_RATEVHT2SS_MCS9
};
u8 *rtw_rate_section[RTW_RATE_SECTION_MAX] = {
	rtw_cck_rates, rtw_ofdm_rates,
	rtw_ht_1s_rates, rtw_ht_2s_rates,
	rtw_vht_1s_rates, rtw_vht_2s_rates
};
EXPORT_SYMBOL(rtw_rate_section);

u8 rtw_rate_size[RTW_RATE_SECTION_MAX] = {
	ARRAY_SIZE(rtw_cck_rates),
	ARRAY_SIZE(rtw_ofdm_rates),
	ARRAY_SIZE(rtw_ht_1s_rates),
	ARRAY_SIZE(rtw_ht_2s_rates),
	ARRAY_SIZE(rtw_vht_1s_rates),
	ARRAY_SIZE(rtw_vht_2s_rates)
};
EXPORT_SYMBOL(rtw_rate_size);

static const u8 rtw_cck_size = ARRAY_SIZE(rtw_cck_rates);
static const u8 rtw_ofdm_size = ARRAY_SIZE(rtw_ofdm_rates);
static const u8 rtw_ht_1s_size = ARRAY_SIZE(rtw_ht_1s_rates);
static const u8 rtw_ht_2s_size = ARRAY_SIZE(rtw_ht_2s_rates);
static const u8 rtw_vht_1s_size = ARRAY_SIZE(rtw_vht_1s_rates);
static const u8 rtw_vht_2s_size = ARRAY_SIZE(rtw_vht_2s_rates);

enum rtw_phy_band_type {
	PHY_BAND_2G	= 0,
	PHY_BAND_5G	= 1,
};

static void rtw_phy_cck_pd_init(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 i, j;

	for (i = 0; i <= RTW_CHANNEL_WIDTH_40; i++) {
		for (j = 0; j < RTW_RF_PATH_MAX; j++)
			dm_info->cck_pd_lv[i][j] = CCK_PD_LV0;
	}

	dm_info->cck_fa_avg = CCK_FA_AVG_RESET;
}

void rtw_phy_set_edcca_th(struct rtw_dev *rtwdev, u8 l2h, u8 h2l)
{
	struct rtw_hw_reg_offset *edcca_th = rtwdev->chip->edcca_th;

	rtw_write32_mask(rtwdev,
			 edcca_th[EDCCA_TH_L2H_IDX].hw_reg.addr,
			 edcca_th[EDCCA_TH_L2H_IDX].hw_reg.mask,
			 l2h + edcca_th[EDCCA_TH_L2H_IDX].offset);
	rtw_write32_mask(rtwdev,
			 edcca_th[EDCCA_TH_H2L_IDX].hw_reg.addr,
			 edcca_th[EDCCA_TH_H2L_IDX].hw_reg.mask,
			 h2l + edcca_th[EDCCA_TH_H2L_IDX].offset);
}
EXPORT_SYMBOL(rtw_phy_set_edcca_th);

void rtw_phy_adaptivity_set_mode(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;

	/* turn off in debugfs for debug usage */
	if (!rtw_edcca_enabled) {
		dm_info->edcca_mode = RTW_EDCCA_NORMAL;
		rtw_dbg(rtwdev, RTW_DBG_PHY, "EDCCA disabled, cannot be set\n");
		return;
	}

	switch (rtwdev->regd.dfs_region) {
	case NL80211_DFS_ETSI:
		dm_info->edcca_mode = RTW_EDCCA_ADAPTIVITY;
		dm_info->l2h_th_ini = chip->l2h_th_ini_ad;
		break;
	case NL80211_DFS_JP:
		dm_info->edcca_mode = RTW_EDCCA_ADAPTIVITY;
		dm_info->l2h_th_ini = chip->l2h_th_ini_cs;
		break;
	default:
		dm_info->edcca_mode = RTW_EDCCA_NORMAL;
		break;
	}
}

static void rtw_phy_adaptivity_init(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;

	rtw_phy_adaptivity_set_mode(rtwdev);
	if (chip->ops->adaptivity_init)
		chip->ops->adaptivity_init(rtwdev);
}

static void rtw_phy_adaptivity(struct rtw_dev *rtwdev)
{
	if (rtwdev->chip->ops->adaptivity)
		rtwdev->chip->ops->adaptivity(rtwdev);
}

static void rtw_phy_cfo_init(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;

	if (chip->ops->cfo_init)
		chip->ops->cfo_init(rtwdev);
}

static void rtw_phy_tx_path_div_init(struct rtw_dev *rtwdev)
{
	struct rtw_path_div *path_div = &rtwdev->dm_path_div;

	path_div->current_tx_path = rtwdev->chip->default_1ss_tx_path;
	path_div->path_a_cnt = 0;
	path_div->path_a_sum = 0;
	path_div->path_b_cnt = 0;
	path_div->path_b_sum = 0;
}

void rtw_phy_init(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u32 addr, mask;

	dm_info->fa_history[3] = 0;
	dm_info->fa_history[2] = 0;
	dm_info->fa_history[1] = 0;
	dm_info->fa_history[0] = 0;
	dm_info->igi_bitmap = 0;
	dm_info->igi_history[3] = 0;
	dm_info->igi_history[2] = 0;
	dm_info->igi_history[1] = 0;

	addr = chip->dig[0].addr;
	mask = chip->dig[0].mask;
	dm_info->igi_history[0] = rtw_read32_mask(rtwdev, addr, mask);
	rtw_phy_cck_pd_init(rtwdev);

	dm_info->iqk.done = false;
	rtw_phy_adaptivity_init(rtwdev);
	rtw_phy_cfo_init(rtwdev);
	rtw_phy_tx_path_div_init(rtwdev);
}
EXPORT_SYMBOL(rtw_phy_init);

void rtw_phy_dig_write(struct rtw_dev *rtwdev, u8 igi)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_hal *hal = &rtwdev->hal;
	u32 addr, mask;
	u8 path;

	if (chip->dig_cck) {
		const struct rtw_hw_reg *dig_cck = &chip->dig_cck[0];
		rtw_write32_mask(rtwdev, dig_cck->addr, dig_cck->mask, igi >> 1);
	}

	for (path = 0; path < hal->rf_path_num; path++) {
		addr = chip->dig[path].addr;
		mask = chip->dig[path].mask;
		rtw_write32_mask(rtwdev, addr, mask, igi);
	}
}

static void rtw_phy_stat_false_alarm(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;

	chip->ops->false_alarm_statistics(rtwdev);
}

#define RA_FLOOR_TABLE_SIZE	7
#define RA_FLOOR_UP_GAP		3

static u8 rtw_phy_get_rssi_level(u8 old_level, u8 rssi)
{
	u8 table[RA_FLOOR_TABLE_SIZE] = {20, 34, 38, 42, 46, 50, 100};
	u8 new_level = 0;
	int i;

	for (i = 0; i < RA_FLOOR_TABLE_SIZE; i++)
		if (i >= old_level)
			table[i] += RA_FLOOR_UP_GAP;

	for (i = 0; i < RA_FLOOR_TABLE_SIZE; i++) {
		if (rssi < table[i]) {
			new_level = i;
			break;
		}
	}

	return new_level;
}

struct rtw_phy_stat_iter_data {
	struct rtw_dev *rtwdev;
	u8 min_rssi;
};

static void rtw_phy_stat_rssi_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw_phy_stat_iter_data *iter_data = data;
	struct rtw_dev *rtwdev = iter_data->rtwdev;
	struct rtw_sta_info *si = (struct rtw_sta_info *)sta->drv_priv;
	u8 rssi;

	rssi = ewma_rssi_read(&si->avg_rssi);
	si->rssi_level = rtw_phy_get_rssi_level(si->rssi_level, rssi);

	rtw_fw_send_rssi_info(rtwdev, si);

	iter_data->min_rssi = min_t(u8, rssi, iter_data->min_rssi);
}

static void rtw_phy_stat_rssi(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_phy_stat_iter_data data = {};

	data.rtwdev = rtwdev;
	data.min_rssi = U8_MAX;
	rtw_iterate_stas_atomic(rtwdev, rtw_phy_stat_rssi_iter, &data);

	dm_info->pre_min_rssi = dm_info->min_rssi;
	dm_info->min_rssi = data.min_rssi;
}

static void rtw_phy_stat_rate_cnt(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;

	dm_info->last_pkt_count = dm_info->cur_pkt_count;
	memset(&dm_info->cur_pkt_count, 0, sizeof(dm_info->cur_pkt_count));
}

static void rtw_phy_statistics(struct rtw_dev *rtwdev)
{
	rtw_phy_stat_rssi(rtwdev);
	rtw_phy_stat_false_alarm(rtwdev);
	rtw_phy_stat_rate_cnt(rtwdev);
}

#define DIG_PERF_FA_TH_LOW			250
#define DIG_PERF_FA_TH_HIGH			500
#define DIG_PERF_FA_TH_EXTRA_HIGH		750
#define DIG_PERF_MAX				0x5a
#define DIG_PERF_MID				0x40
#define DIG_CVRG_FA_TH_LOW			2000
#define DIG_CVRG_FA_TH_HIGH			4000
#define DIG_CVRG_FA_TH_EXTRA_HIGH		5000
#define DIG_CVRG_MAX				0x2a
#define DIG_CVRG_MID				0x26
#define DIG_CVRG_MIN				0x1c
#define DIG_RSSI_GAIN_OFFSET			15

static bool
rtw_phy_dig_check_damping(struct rtw_dm_info *dm_info)
{
	u16 fa_lo = DIG_PERF_FA_TH_LOW;
	u16 fa_hi = DIG_PERF_FA_TH_HIGH;
	u16 *fa_history;
	u8 *igi_history;
	u8 damping_rssi;
	u8 min_rssi;
	u8 diff;
	u8 igi_bitmap;
	bool damping = false;

	min_rssi = dm_info->min_rssi;
	if (dm_info->damping) {
		damping_rssi = dm_info->damping_rssi;
		diff = min_rssi > damping_rssi ? min_rssi - damping_rssi :
						 damping_rssi - min_rssi;
		if (diff > 3 || dm_info->damping_cnt++ > 20) {
			dm_info->damping = false;
			return false;
		}

		return true;
	}

	igi_history = dm_info->igi_history;
	fa_history = dm_info->fa_history;
	igi_bitmap = dm_info->igi_bitmap & 0xf;
	switch (igi_bitmap) {
	case 5:
		/* down -> up -> down -> up */
		if (igi_history[0] > igi_history[1] &&
		    igi_history[2] > igi_history[3] &&
		    igi_history[0] - igi_history[1] >= 2 &&
		    igi_history[2] - igi_history[3] >= 2 &&
		    fa_history[0] > fa_hi && fa_history[1] < fa_lo &&
		    fa_history[2] > fa_hi && fa_history[3] < fa_lo)
			damping = true;
		break;
	case 9:
		/* up -> down -> down -> up */
		if (igi_history[0] > igi_history[1] &&
		    igi_history[3] > igi_history[2] &&
		    igi_history[0] - igi_history[1] >= 4 &&
		    igi_history[3] - igi_history[2] >= 2 &&
		    fa_history[0] > fa_hi && fa_history[1] < fa_lo &&
		    fa_history[2] < fa_lo && fa_history[3] > fa_hi)
			damping = true;
		break;
	default:
		return false;
	}

	if (damping) {
		dm_info->damping = true;
		dm_info->damping_cnt = 0;
		dm_info->damping_rssi = min_rssi;
	}

	return damping;
}

static void rtw_phy_dig_get_boundary(struct rtw_dev *rtwdev,
				     struct rtw_dm_info *dm_info,
				     u8 *upper, u8 *lower, bool linked)
{
	u8 dig_max, dig_min, dig_mid;
	u8 min_rssi;

	if (linked) {
		dig_max = DIG_PERF_MAX;
		dig_mid = DIG_PERF_MID;
		dig_min = rtwdev->chip->dig_min;
		min_rssi = max_t(u8, dm_info->min_rssi, dig_min);
	} else {
		dig_max = DIG_CVRG_MAX;
		dig_mid = DIG_CVRG_MID;
		dig_min = DIG_CVRG_MIN;
		min_rssi = dig_min;
	}

	/* DIG MAX should be bounded by minimum RSSI with offset +15 */
	dig_max = min_t(u8, dig_max, min_rssi + DIG_RSSI_GAIN_OFFSET);

	*lower = clamp_t(u8, min_rssi, dig_min, dig_mid);
	*upper = clamp_t(u8, *lower + DIG_RSSI_GAIN_OFFSET, dig_min, dig_max);
}

static void rtw_phy_dig_get_threshold(struct rtw_dm_info *dm_info,
				      u16 *fa_th, u8 *step, bool linked)
{
	u8 min_rssi, pre_min_rssi;

	min_rssi = dm_info->min_rssi;
	pre_min_rssi = dm_info->pre_min_rssi;
	step[0] = 4;
	step[1] = 3;
	step[2] = 2;

	if (linked) {
		fa_th[0] = DIG_PERF_FA_TH_EXTRA_HIGH;
		fa_th[1] = DIG_PERF_FA_TH_HIGH;
		fa_th[2] = DIG_PERF_FA_TH_LOW;
		if (pre_min_rssi > min_rssi) {
			step[0] = 6;
			step[1] = 4;
			step[2] = 2;
		}
	} else {
		fa_th[0] = DIG_CVRG_FA_TH_EXTRA_HIGH;
		fa_th[1] = DIG_CVRG_FA_TH_HIGH;
		fa_th[2] = DIG_CVRG_FA_TH_LOW;
	}
}

static void rtw_phy_dig_recorder(struct rtw_dm_info *dm_info, u8 igi, u16 fa)
{
	u8 *igi_history;
	u16 *fa_history;
	u8 igi_bitmap;
	bool up;

	igi_bitmap = dm_info->igi_bitmap << 1 & 0xfe;
	igi_history = dm_info->igi_history;
	fa_history = dm_info->fa_history;

	up = igi > igi_history[0];
	igi_bitmap |= up;

	igi_history[3] = igi_history[2];
	igi_history[2] = igi_history[1];
	igi_history[1] = igi_history[0];
	igi_history[0] = igi;

	fa_history[3] = fa_history[2];
	fa_history[2] = fa_history[1];
	fa_history[1] = fa_history[0];
	fa_history[0] = fa;

	dm_info->igi_bitmap = igi_bitmap;
}

static void rtw_phy_dig(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 upper_bound, lower_bound;
	u8 pre_igi, cur_igi;
	u16 fa_th[3], fa_cnt;
	u8 level;
	u8 step[3];
	bool linked;

	if (test_bit(RTW_FLAG_DIG_DISABLE, rtwdev->flags))
		return;

	if (rtw_phy_dig_check_damping(dm_info))
		return;

	linked = !!rtwdev->sta_cnt;

	fa_cnt = dm_info->total_fa_cnt;
	pre_igi = dm_info->igi_history[0];

	rtw_phy_dig_get_threshold(dm_info, fa_th, step, linked);

	/* test the false alarm count from the highest threshold level first,
	 * and increase it by corresponding step size
	 *
	 * note that the step size is offset by -2, compensate it afterall
	 */
	cur_igi = pre_igi;
	for (level = 0; level < 3; level++) {
		if (fa_cnt > fa_th[level]) {
			cur_igi += step[level];
			break;
		}
	}
	cur_igi -= 2;

	/* calculate the upper/lower bound by the minimum rssi we have among
	 * the peers connected with us, meanwhile make sure the igi value does
	 * not beyond the hardware limitation
	 */
	rtw_phy_dig_get_boundary(rtwdev, dm_info, &upper_bound, &lower_bound,
				 linked);
	cur_igi = clamp_t(u8, cur_igi, lower_bound, upper_bound);

	/* record current igi value and false alarm statistics for further
	 * damping checks, and record the trend of igi values
	 */
	rtw_phy_dig_recorder(dm_info, cur_igi, fa_cnt);

	if (cur_igi != pre_igi)
		rtw_phy_dig_write(rtwdev, cur_igi);
}

static void rtw_phy_ra_info_update_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw_dev *rtwdev = data;
	struct rtw_sta_info *si = (struct rtw_sta_info *)sta->drv_priv;

	rtw_update_sta_info(rtwdev, si);
}

static void rtw_phy_ra_info_update(struct rtw_dev *rtwdev)
{
	if (rtwdev->watch_dog_cnt & 0x3)
		return;

	rtw_iterate_stas_atomic(rtwdev, rtw_phy_ra_info_update_iter, rtwdev);
}

static u32 rtw_phy_get_rrsr_mask(struct rtw_dev *rtwdev, u8 rate_idx)
{
	u8 rate_order;

	rate_order = rate_idx;

	if (rate_idx >= DESC_RATEVHT4SS_MCS0)
		rate_order -= DESC_RATEVHT4SS_MCS0;
	else if (rate_idx >= DESC_RATEVHT3SS_MCS0)
		rate_order -= DESC_RATEVHT3SS_MCS0;
	else if (rate_idx >= DESC_RATEVHT2SS_MCS0)
		rate_order -= DESC_RATEVHT2SS_MCS0;
	else if (rate_idx >= DESC_RATEVHT1SS_MCS0)
		rate_order -= DESC_RATEVHT1SS_MCS0;
	else if (rate_idx >= DESC_RATEMCS24)
		rate_order -= DESC_RATEMCS24;
	else if (rate_idx >= DESC_RATEMCS16)
		rate_order -= DESC_RATEMCS16;
	else if (rate_idx >= DESC_RATEMCS8)
		rate_order -= DESC_RATEMCS8;
	else if (rate_idx >= DESC_RATEMCS0)
		rate_order -= DESC_RATEMCS0;
	else if (rate_idx >= DESC_RATE6M)
		rate_order -= DESC_RATE6M;
	else
		rate_order -= DESC_RATE1M;

	if (rate_idx >= DESC_RATEMCS0 || rate_order == 0)
		rate_order++;

	return GENMASK(rate_order + RRSR_RATE_ORDER_CCK_LEN - 1, 0);
}

static void rtw_phy_rrsr_mask_min_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw_dev *rtwdev = (struct rtw_dev *)data;
	struct rtw_sta_info *si = (struct rtw_sta_info *)sta->drv_priv;
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u32 mask = 0;

	mask = rtw_phy_get_rrsr_mask(rtwdev, si->ra_report.desc_rate);
	if (mask < dm_info->rrsr_mask_min)
		dm_info->rrsr_mask_min = mask;
}

static void rtw_phy_rrsr_update(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;

	dm_info->rrsr_mask_min = RRSR_RATE_ORDER_MAX;
	rtw_iterate_stas_atomic(rtwdev, rtw_phy_rrsr_mask_min_iter, rtwdev);
	rtw_write32(rtwdev, REG_RRSR, dm_info->rrsr_val_init & dm_info->rrsr_mask_min);
}

static void rtw_phy_dpk_track(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;

	if (chip->ops->dpk_track)
		chip->ops->dpk_track(rtwdev);
}

struct rtw_rx_addr_match_data {
	struct rtw_dev *rtwdev;
	struct ieee80211_hdr *hdr;
	struct rtw_rx_pkt_stat *pkt_stat;
	u8 *bssid;
};

static void rtw_phy_parsing_cfo_iter(void *data, u8 *mac,
				     struct ieee80211_vif *vif)
{
	struct rtw_rx_addr_match_data *iter_data = data;
	struct rtw_dev *rtwdev = iter_data->rtwdev;
	struct rtw_rx_pkt_stat *pkt_stat = iter_data->pkt_stat;
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_cfo_track *cfo = &dm_info->cfo_track;
	u8 *bssid = iter_data->bssid;
	u8 i;

	if (!ether_addr_equal(vif->bss_conf.bssid, bssid))
		return;

	for (i = 0; i < rtwdev->hal.rf_path_num; i++) {
		cfo->cfo_tail[i] += pkt_stat->cfo_tail[i];
		cfo->cfo_cnt[i]++;
	}

	cfo->packet_count++;
}

void rtw_phy_parsing_cfo(struct rtw_dev *rtwdev,
			 struct rtw_rx_pkt_stat *pkt_stat)
{
	struct ieee80211_hdr *hdr = pkt_stat->hdr;
	struct rtw_rx_addr_match_data data = {};

	if (pkt_stat->crc_err || pkt_stat->icv_err || !pkt_stat->phy_status ||
	    ieee80211_is_ctl(hdr->frame_control))
		return;

	data.rtwdev = rtwdev;
	data.hdr = hdr;
	data.pkt_stat = pkt_stat;
	data.bssid = get_hdr_bssid(hdr);

	rtw_iterate_vifs_atomic(rtwdev, rtw_phy_parsing_cfo_iter, &data);
}
EXPORT_SYMBOL(rtw_phy_parsing_cfo);

static void rtw_phy_cfo_track(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;

	if (chip->ops->cfo_track)
		chip->ops->cfo_track(rtwdev);
}

#define CCK_PD_FA_LV1_MIN	1000
#define CCK_PD_FA_LV0_MAX	500

static u8 rtw_phy_cck_pd_lv_unlink(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u32 cck_fa_avg = dm_info->cck_fa_avg;

	if (cck_fa_avg > CCK_PD_FA_LV1_MIN)
		return CCK_PD_LV1;

	if (cck_fa_avg < CCK_PD_FA_LV0_MAX)
		return CCK_PD_LV0;

	return CCK_PD_LV_MAX;
}

#define CCK_PD_IGI_LV4_VAL 0x38
#define CCK_PD_IGI_LV3_VAL 0x2a
#define CCK_PD_IGI_LV2_VAL 0x24
#define CCK_PD_RSSI_LV4_VAL 32
#define CCK_PD_RSSI_LV3_VAL 32
#define CCK_PD_RSSI_LV2_VAL 24

static u8 rtw_phy_cck_pd_lv_link(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 igi = dm_info->igi_history[0];
	u8 rssi = dm_info->min_rssi;
	u32 cck_fa_avg = dm_info->cck_fa_avg;

	if (igi > CCK_PD_IGI_LV4_VAL && rssi > CCK_PD_RSSI_LV4_VAL)
		return CCK_PD_LV4;
	if (igi > CCK_PD_IGI_LV3_VAL && rssi > CCK_PD_RSSI_LV3_VAL)
		return CCK_PD_LV3;
	if (igi > CCK_PD_IGI_LV2_VAL || rssi > CCK_PD_RSSI_LV2_VAL)
		return CCK_PD_LV2;
	if (cck_fa_avg > CCK_PD_FA_LV1_MIN)
		return CCK_PD_LV1;
	if (cck_fa_avg < CCK_PD_FA_LV0_MAX)
		return CCK_PD_LV0;

	return CCK_PD_LV_MAX;
}

static u8 rtw_phy_cck_pd_lv(struct rtw_dev *rtwdev)
{
	if (!rtw_is_assoc(rtwdev))
		return rtw_phy_cck_pd_lv_unlink(rtwdev);
	else
		return rtw_phy_cck_pd_lv_link(rtwdev);
}

static void rtw_phy_cck_pd(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_chip_info *chip = rtwdev->chip;
	u32 cck_fa = dm_info->cck_fa_cnt;
	u8 level;

	if (rtwdev->hal.current_band_type != RTW_BAND_2G)
		return;

	if (dm_info->cck_fa_avg == CCK_FA_AVG_RESET)
		dm_info->cck_fa_avg = cck_fa;
	else
		dm_info->cck_fa_avg = (dm_info->cck_fa_avg * 3 + cck_fa) >> 2;

	rtw_dbg(rtwdev, RTW_DBG_PHY, "IGI=0x%x, rssi_min=%d, cck_fa=%d\n",
		dm_info->igi_history[0], dm_info->min_rssi,
		dm_info->fa_history[0]);
	rtw_dbg(rtwdev, RTW_DBG_PHY, "cck_fa_avg=%d, cck_pd_default=%d\n",
		dm_info->cck_fa_avg, dm_info->cck_pd_default);

	level = rtw_phy_cck_pd_lv(rtwdev);

	if (level >= CCK_PD_LV_MAX)
		return;

	if (chip->ops->cck_pd_set)
		chip->ops->cck_pd_set(rtwdev, level);
}

static void rtw_phy_pwr_track(struct rtw_dev *rtwdev)
{
	rtwdev->chip->ops->pwr_track(rtwdev);
}

static void rtw_phy_ra_track(struct rtw_dev *rtwdev)
{
	rtw_fw_update_wl_phy_info(rtwdev);
	rtw_phy_ra_info_update(rtwdev);
	rtw_phy_rrsr_update(rtwdev);
}

void rtw_phy_dynamic_mechanism(struct rtw_dev *rtwdev)
{
	/* for further calculation */
	rtw_phy_statistics(rtwdev);
	rtw_phy_dig(rtwdev);
	rtw_phy_cck_pd(rtwdev);
	rtw_phy_ra_track(rtwdev);
	rtw_phy_tx_path_diversity(rtwdev);
	rtw_phy_cfo_track(rtwdev);
	rtw_phy_dpk_track(rtwdev);
	rtw_phy_pwr_track(rtwdev);

	if (rtw_fw_feature_check(&rtwdev->fw, FW_FEATURE_ADAPTIVITY))
		rtw_fw_adaptivity(rtwdev);
	else
		rtw_phy_adaptivity(rtwdev);
}

#define FRAC_BITS 3

static u8 rtw_phy_power_2_db(s8 power)
{
	if (power <= -100 || power >= 20)
		return 0;
	else if (power >= 0)
		return 100;
	else
		return 100 + power;
}

static u64 rtw_phy_db_2_linear(u8 power_db)
{
	u8 i, j;
	u64 linear;

	if (power_db > 96)
		power_db = 96;
	else if (power_db < 1)
		return 1;

	/* 1dB ~ 96dB */
	i = (power_db - 1) >> 3;
	j = (power_db - 1) - (i << 3);

	linear = db_invert_table[i][j];
	linear = i > 2 ? linear << FRAC_BITS : linear;

	return linear;
}

static u8 rtw_phy_linear_2_db(u64 linear)
{
	u8 i;
	u8 j;
	u32 dB;

	if (linear >= db_invert_table[11][7])
		return 96; /* maximum 96 dB */

	for (i = 0; i < 12; i++) {
		if (i <= 2 && (linear << FRAC_BITS) <= db_invert_table[i][7])
			break;
		else if (i > 2 && linear <= db_invert_table[i][7])
			break;
	}

	for (j = 0; j < 8; j++) {
		if (i <= 2 && (linear << FRAC_BITS) <= db_invert_table[i][j])
			break;
		else if (i > 2 && linear <= db_invert_table[i][j])
			break;
	}

	if (j == 0 && i == 0)
		goto end;

	if (j == 0) {
		if (i != 3) {
			if (db_invert_table[i][0] - linear >
			    linear - db_invert_table[i - 1][7]) {
				i = i - 1;
				j = 7;
			}
		} else {
			if (db_invert_table[3][0] - linear >
			    linear - db_invert_table[2][7]) {
				i = 2;
				j = 7;
			}
		}
	} else {
		if (db_invert_table[i][j] - linear >
		    linear - db_invert_table[i][j - 1]) {
			j = j - 1;
		}
	}
end:
	dB = (i << 3) + j + 1;

	return dB;
}

u8 rtw_phy_rf_power_2_rssi(s8 *rf_power, u8 path_num)
{
	s8 power;
	u8 power_db;
	u64 linear;
	u64 sum = 0;
	u8 path;

	for (path = 0; path < path_num; path++) {
		power = rf_power[path];
		power_db = rtw_phy_power_2_db(power);
		linear = rtw_phy_db_2_linear(power_db);
		sum += linear;
	}

	sum = (sum + (1 << (FRAC_BITS - 1))) >> FRAC_BITS;
	switch (path_num) {
	case 2:
		sum >>= 1;
		break;
	case 3:
		sum = ((sum) + ((sum) << 1) + ((sum) << 3)) >> 5;
		break;
	case 4:
		sum >>= 2;
		break;
	default:
		break;
	}

	return rtw_phy_linear_2_db(sum);
}
EXPORT_SYMBOL(rtw_phy_rf_power_2_rssi);

u32 rtw_phy_read_rf(struct rtw_dev *rtwdev, enum rtw_rf_path rf_path,
		    u32 addr, u32 mask)
{
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_chip_info *chip = rtwdev->chip;
	const u32 *base_addr = chip->rf_base_addr;
	u32 val, direct_addr;

	if (rf_path >= hal->rf_phy_num) {
		rtw_err(rtwdev, "unsupported rf path (%d)\n", rf_path);
		return INV_RF_DATA;
	}

	addr &= 0xff;
	direct_addr = base_addr[rf_path] + (addr << 2);
	mask &= RFREG_MASK;

	val = rtw_read32_mask(rtwdev, direct_addr, mask);

	return val;
}
EXPORT_SYMBOL(rtw_phy_read_rf);

u32 rtw_phy_read_rf_sipi(struct rtw_dev *rtwdev, enum rtw_rf_path rf_path,
			 u32 addr, u32 mask)
{
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_chip_info *chip = rtwdev->chip;
	const struct rtw_rf_sipi_addr *rf_sipi_addr;
	const struct rtw_rf_sipi_addr *rf_sipi_addr_a;
	u32 val32;
	u32 en_pi;
	u32 r_addr;
	u32 shift;

	if (rf_path >= hal->rf_phy_num) {
		rtw_err(rtwdev, "unsupported rf path (%d)\n", rf_path);
		return INV_RF_DATA;
	}

	if (!chip->rf_sipi_read_addr) {
		rtw_err(rtwdev, "rf_sipi_read_addr isn't defined\n");
		return INV_RF_DATA;
	}

	rf_sipi_addr = &chip->rf_sipi_read_addr[rf_path];
	rf_sipi_addr_a = &chip->rf_sipi_read_addr[RF_PATH_A];

	addr &= 0xff;

	val32 = rtw_read32(rtwdev, rf_sipi_addr->hssi_2);
	val32 = (val32 & ~LSSI_READ_ADDR_MASK) | (addr << 23);
	rtw_write32(rtwdev, rf_sipi_addr->hssi_2, val32);

	/* toggle read edge of path A */
	val32 = rtw_read32(rtwdev, rf_sipi_addr_a->hssi_2);
	rtw_write32(rtwdev, rf_sipi_addr_a->hssi_2, val32 & ~LSSI_READ_EDGE_MASK);
	rtw_write32(rtwdev, rf_sipi_addr_a->hssi_2, val32 | LSSI_READ_EDGE_MASK);

	udelay(120);

	en_pi = rtw_read32_mask(rtwdev, rf_sipi_addr->hssi_1, BIT(8));
	r_addr = en_pi ? rf_sipi_addr->lssi_read_pi : rf_sipi_addr->lssi_read;

	val32 = rtw_read32_mask(rtwdev, r_addr, LSSI_READ_DATA_MASK);

	shift = __ffs(mask);

	return (val32 & mask) >> shift;
}
EXPORT_SYMBOL(rtw_phy_read_rf_sipi);

bool rtw_phy_write_rf_reg_sipi(struct rtw_dev *rtwdev, enum rtw_rf_path rf_path,
			       u32 addr, u32 mask, u32 data)
{
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_chip_info *chip = rtwdev->chip;
	u32 *sipi_addr = chip->rf_sipi_addr;
	u32 data_and_addr;
	u32 old_data = 0;
	u32 shift;

	if (rf_path >= hal->rf_phy_num) {
		rtw_err(rtwdev, "unsupported rf path (%d)\n", rf_path);
		return false;
	}

	addr &= 0xff;
	mask &= RFREG_MASK;

	if (mask != RFREG_MASK) {
		old_data = chip->ops->read_rf(rtwdev, rf_path, addr, RFREG_MASK);

		if (old_data == INV_RF_DATA) {
			rtw_err(rtwdev, "Write fail, rf is disabled\n");
			return false;
		}

		shift = __ffs(mask);
		data = ((old_data) & (~mask)) | (data << shift);
	}

	data_and_addr = ((addr << 20) | (data & 0x000fffff)) & 0x0fffffff;

	rtw_write32(rtwdev, sipi_addr[rf_path], data_and_addr);

	udelay(13);

	return true;
}
EXPORT_SYMBOL(rtw_phy_write_rf_reg_sipi);

bool rtw_phy_write_rf_reg(struct rtw_dev *rtwdev, enum rtw_rf_path rf_path,
			  u32 addr, u32 mask, u32 data)
{
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_chip_info *chip = rtwdev->chip;
	const u32 *base_addr = chip->rf_base_addr;
	u32 direct_addr;

	if (rf_path >= hal->rf_phy_num) {
		rtw_err(rtwdev, "unsupported rf path (%d)\n", rf_path);
		return false;
	}

	addr &= 0xff;
	direct_addr = base_addr[rf_path] + (addr << 2);
	mask &= RFREG_MASK;

	rtw_write32_mask(rtwdev, direct_addr, mask, data);

	udelay(1);

	return true;
}

bool rtw_phy_write_rf_reg_mix(struct rtw_dev *rtwdev, enum rtw_rf_path rf_path,
			      u32 addr, u32 mask, u32 data)
{
	if (addr != 0x00)
		return rtw_phy_write_rf_reg(rtwdev, rf_path, addr, mask, data);

	return rtw_phy_write_rf_reg_sipi(rtwdev, rf_path, addr, mask, data);
}
EXPORT_SYMBOL(rtw_phy_write_rf_reg_mix);

void rtw_phy_setup_phy_cond(struct rtw_dev *rtwdev, u32 pkg)
{
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	struct rtw_phy_cond cond = {0};

	cond.cut = hal->cut_version ? hal->cut_version : 15;
	cond.pkg = pkg ? pkg : 15;
	cond.plat = 0x04;
	cond.rfe = efuse->rfe_option;

	switch (rtw_hci_type(rtwdev)) {
	case RTW_HCI_TYPE_USB:
		cond.intf = INTF_USB;
		break;
	case RTW_HCI_TYPE_SDIO:
		cond.intf = INTF_SDIO;
		break;
	case RTW_HCI_TYPE_PCIE:
	default:
		cond.intf = INTF_PCIE;
		break;
	}

	hal->phy_cond = cond;

	rtw_dbg(rtwdev, RTW_DBG_PHY, "phy cond=0x%08x\n", *((u32 *)&hal->phy_cond));
}

static bool check_positive(struct rtw_dev *rtwdev, struct rtw_phy_cond cond)
{
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_phy_cond drv_cond = hal->phy_cond;

	if (cond.cut && cond.cut != drv_cond.cut)
		return false;

	if (cond.pkg && cond.pkg != drv_cond.pkg)
		return false;

	if (cond.intf && cond.intf != drv_cond.intf)
		return false;

	if (cond.rfe != drv_cond.rfe)
		return false;

	return true;
}

void rtw_parse_tbl_phy_cond(struct rtw_dev *rtwdev, const struct rtw_table *tbl)
{
	const union phy_table_tile *p = tbl->data;
	const union phy_table_tile *end = p + tbl->size / 2;
	struct rtw_phy_cond pos_cond = {0};
	bool is_matched = true, is_skipped = false;

	BUILD_BUG_ON(sizeof(union phy_table_tile) != sizeof(struct phy_cfg_pair));

	for (; p < end; p++) {
		if (p->cond.pos) {
			switch (p->cond.branch) {
			case BRANCH_ENDIF:
				is_matched = true;
				is_skipped = false;
				break;
			case BRANCH_ELSE:
				is_matched = is_skipped ? false : true;
				break;
			case BRANCH_IF:
			case BRANCH_ELIF:
			default:
				pos_cond = p->cond;
				break;
			}
		} else if (p->cond.neg) {
			if (!is_skipped) {
				if (check_positive(rtwdev, pos_cond)) {
					is_matched = true;
					is_skipped = true;
				} else {
					is_matched = false;
					is_skipped = false;
				}
			} else {
				is_matched = false;
			}
		} else if (is_matched) {
			(*tbl->do_cfg)(rtwdev, tbl, p->cfg.addr, p->cfg.data);
		}
	}
}
EXPORT_SYMBOL(rtw_parse_tbl_phy_cond);

#define bcd_to_dec_pwr_by_rate(val, i) bcd2bin(val >> (i * 8))

static u8 tbl_to_dec_pwr_by_rate(struct rtw_dev *rtwdev, u32 hex, u8 i)
{
	if (rtwdev->chip->is_pwr_by_rate_dec)
		return bcd_to_dec_pwr_by_rate(hex, i);

	return (hex >> (i * 8)) & 0xFF;
}

static void
rtw_phy_get_rate_values_of_txpwr_by_rate(struct rtw_dev *rtwdev,
					 u32 addr, u32 mask, u32 val, u8 *rate,
					 u8 *pwr_by_rate, u8 *rate_num)
{
	int i;

	switch (addr) {
	case 0xE00:
	case 0x830:
		rate[0] = DESC_RATE6M;
		rate[1] = DESC_RATE9M;
		rate[2] = DESC_RATE12M;
		rate[3] = DESC_RATE18M;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0xE04:
	case 0x834:
		rate[0] = DESC_RATE24M;
		rate[1] = DESC_RATE36M;
		rate[2] = DESC_RATE48M;
		rate[3] = DESC_RATE54M;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0xE08:
		rate[0] = DESC_RATE1M;
		pwr_by_rate[0] = bcd_to_dec_pwr_by_rate(val, 1);
		*rate_num = 1;
		break;
	case 0x86C:
		if (mask == 0xffffff00) {
			rate[0] = DESC_RATE2M;
			rate[1] = DESC_RATE5_5M;
			rate[2] = DESC_RATE11M;
			for (i = 1; i < 4; ++i)
				pwr_by_rate[i - 1] =
					tbl_to_dec_pwr_by_rate(rtwdev, val, i);
			*rate_num = 3;
		} else if (mask == 0x000000ff) {
			rate[0] = DESC_RATE11M;
			pwr_by_rate[0] = bcd_to_dec_pwr_by_rate(val, 0);
			*rate_num = 1;
		}
		break;
	case 0xE10:
	case 0x83C:
		rate[0] = DESC_RATEMCS0;
		rate[1] = DESC_RATEMCS1;
		rate[2] = DESC_RATEMCS2;
		rate[3] = DESC_RATEMCS3;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0xE14:
	case 0x848:
		rate[0] = DESC_RATEMCS4;
		rate[1] = DESC_RATEMCS5;
		rate[2] = DESC_RATEMCS6;
		rate[3] = DESC_RATEMCS7;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0xE18:
	case 0x84C:
		rate[0] = DESC_RATEMCS8;
		rate[1] = DESC_RATEMCS9;
		rate[2] = DESC_RATEMCS10;
		rate[3] = DESC_RATEMCS11;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0xE1C:
	case 0x868:
		rate[0] = DESC_RATEMCS12;
		rate[1] = DESC_RATEMCS13;
		rate[2] = DESC_RATEMCS14;
		rate[3] = DESC_RATEMCS15;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0x838:
		rate[0] = DESC_RATE1M;
		rate[1] = DESC_RATE2M;
		rate[2] = DESC_RATE5_5M;
		for (i = 1; i < 4; ++i)
			pwr_by_rate[i - 1] = tbl_to_dec_pwr_by_rate(rtwdev,
								    val, i);
		*rate_num = 3;
		break;
	case 0xC20:
	case 0xE20:
	case 0x1820:
	case 0x1A20:
		rate[0] = DESC_RATE1M;
		rate[1] = DESC_RATE2M;
		rate[2] = DESC_RATE5_5M;
		rate[3] = DESC_RATE11M;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0xC24:
	case 0xE24:
	case 0x1824:
	case 0x1A24:
		rate[0] = DESC_RATE6M;
		rate[1] = DESC_RATE9M;
		rate[2] = DESC_RATE12M;
		rate[3] = DESC_RATE18M;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0xC28:
	case 0xE28:
	case 0x1828:
	case 0x1A28:
		rate[0] = DESC_RATE24M;
		rate[1] = DESC_RATE36M;
		rate[2] = DESC_RATE48M;
		rate[3] = DESC_RATE54M;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0xC2C:
	case 0xE2C:
	case 0x182C:
	case 0x1A2C:
		rate[0] = DESC_RATEMCS0;
		rate[1] = DESC_RATEMCS1;
		rate[2] = DESC_RATEMCS2;
		rate[3] = DESC_RATEMCS3;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0xC30:
	case 0xE30:
	case 0x1830:
	case 0x1A30:
		rate[0] = DESC_RATEMCS4;
		rate[1] = DESC_RATEMCS5;
		rate[2] = DESC_RATEMCS6;
		rate[3] = DESC_RATEMCS7;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0xC34:
	case 0xE34:
	case 0x1834:
	case 0x1A34:
		rate[0] = DESC_RATEMCS8;
		rate[1] = DESC_RATEMCS9;
		rate[2] = DESC_RATEMCS10;
		rate[3] = DESC_RATEMCS11;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0xC38:
	case 0xE38:
	case 0x1838:
	case 0x1A38:
		rate[0] = DESC_RATEMCS12;
		rate[1] = DESC_RATEMCS13;
		rate[2] = DESC_RATEMCS14;
		rate[3] = DESC_RATEMCS15;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0xC3C:
	case 0xE3C:
	case 0x183C:
	case 0x1A3C:
		rate[0] = DESC_RATEVHT1SS_MCS0;
		rate[1] = DESC_RATEVHT1SS_MCS1;
		rate[2] = DESC_RATEVHT1SS_MCS2;
		rate[3] = DESC_RATEVHT1SS_MCS3;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0xC40:
	case 0xE40:
	case 0x1840:
	case 0x1A40:
		rate[0] = DESC_RATEVHT1SS_MCS4;
		rate[1] = DESC_RATEVHT1SS_MCS5;
		rate[2] = DESC_RATEVHT1SS_MCS6;
		rate[3] = DESC_RATEVHT1SS_MCS7;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0xC44:
	case 0xE44:
	case 0x1844:
	case 0x1A44:
		rate[0] = DESC_RATEVHT1SS_MCS8;
		rate[1] = DESC_RATEVHT1SS_MCS9;
		rate[2] = DESC_RATEVHT2SS_MCS0;
		rate[3] = DESC_RATEVHT2SS_MCS1;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0xC48:
	case 0xE48:
	case 0x1848:
	case 0x1A48:
		rate[0] = DESC_RATEVHT2SS_MCS2;
		rate[1] = DESC_RATEVHT2SS_MCS3;
		rate[2] = DESC_RATEVHT2SS_MCS4;
		rate[3] = DESC_RATEVHT2SS_MCS5;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0xC4C:
	case 0xE4C:
	case 0x184C:
	case 0x1A4C:
		rate[0] = DESC_RATEVHT2SS_MCS6;
		rate[1] = DESC_RATEVHT2SS_MCS7;
		rate[2] = DESC_RATEVHT2SS_MCS8;
		rate[3] = DESC_RATEVHT2SS_MCS9;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0xCD8:
	case 0xED8:
	case 0x18D8:
	case 0x1AD8:
		rate[0] = DESC_RATEMCS16;
		rate[1] = DESC_RATEMCS17;
		rate[2] = DESC_RATEMCS18;
		rate[3] = DESC_RATEMCS19;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0xCDC:
	case 0xEDC:
	case 0x18DC:
	case 0x1ADC:
		rate[0] = DESC_RATEMCS20;
		rate[1] = DESC_RATEMCS21;
		rate[2] = DESC_RATEMCS22;
		rate[3] = DESC_RATEMCS23;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0xCE0:
	case 0xEE0:
	case 0x18E0:
	case 0x1AE0:
		rate[0] = DESC_RATEVHT3SS_MCS0;
		rate[1] = DESC_RATEVHT3SS_MCS1;
		rate[2] = DESC_RATEVHT3SS_MCS2;
		rate[3] = DESC_RATEVHT3SS_MCS3;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0xCE4:
	case 0xEE4:
	case 0x18E4:
	case 0x1AE4:
		rate[0] = DESC_RATEVHT3SS_MCS4;
		rate[1] = DESC_RATEVHT3SS_MCS5;
		rate[2] = DESC_RATEVHT3SS_MCS6;
		rate[3] = DESC_RATEVHT3SS_MCS7;
		for (i = 0; i < 4; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 4;
		break;
	case 0xCE8:
	case 0xEE8:
	case 0x18E8:
	case 0x1AE8:
		rate[0] = DESC_RATEVHT3SS_MCS8;
		rate[1] = DESC_RATEVHT3SS_MCS9;
		for (i = 0; i < 2; ++i)
			pwr_by_rate[i] = tbl_to_dec_pwr_by_rate(rtwdev, val, i);
		*rate_num = 2;
		break;
	default:
		rtw_warn(rtwdev, "invalid tx power index addr 0x%08x\n", addr);
		break;
	}
}

static void rtw_phy_store_tx_power_by_rate(struct rtw_dev *rtwdev,
					   u32 band, u32 rfpath, u32 txnum,
					   u32 regaddr, u32 bitmask, u32 data)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u8 rate_num = 0;
	u8 rate;
	u8 rates[RTW_RF_PATH_MAX] = {0};
	s8 offset;
	s8 pwr_by_rate[RTW_RF_PATH_MAX] = {0};
	int i;

	rtw_phy_get_rate_values_of_txpwr_by_rate(rtwdev, regaddr, bitmask, data,
						 rates, pwr_by_rate, &rate_num);

	if (WARN_ON(rfpath >= RTW_RF_PATH_MAX ||
		    (band != PHY_BAND_2G && band != PHY_BAND_5G) ||
		    rate_num > RTW_RF_PATH_MAX))
		return;

	for (i = 0; i < rate_num; i++) {
		offset = pwr_by_rate[i];
		rate = rates[i];
		if (band == PHY_BAND_2G)
			hal->tx_pwr_by_rate_offset_2g[rfpath][rate] = offset;
		else if (band == PHY_BAND_5G)
			hal->tx_pwr_by_rate_offset_5g[rfpath][rate] = offset;
		else
			continue;
	}
}

void rtw_parse_tbl_bb_pg(struct rtw_dev *rtwdev, const struct rtw_table *tbl)
{
	const struct rtw_phy_pg_cfg_pair *p = tbl->data;
	const struct rtw_phy_pg_cfg_pair *end = p + tbl->size;

	for (; p < end; p++) {
		if (p->addr == 0xfe || p->addr == 0xffe) {
			msleep(50);
			continue;
		}
		rtw_phy_store_tx_power_by_rate(rtwdev, p->band, p->rf_path,
					       p->tx_num, p->addr, p->bitmask,
					       p->data);
	}
}
EXPORT_SYMBOL(rtw_parse_tbl_bb_pg);

static const u8 rtw_channel_idx_5g[RTW_MAX_CHANNEL_NUM_5G] = {
	36,  38,  40,  42,  44,  46,  48, /* Band 1 */
	52,  54,  56,  58,  60,  62,  64, /* Band 2 */
	100, 102, 104, 106, 108, 110, 112, /* Band 3 */
	116, 118, 120, 122, 124, 126, 128, /* Band 3 */
	132, 134, 136, 138, 140, 142, 144, /* Band 3 */
	149, 151, 153, 155, 157, 159, 161, /* Band 4 */
	165, 167, 169, 171, 173, 175, 177}; /* Band 4 */

static int rtw_channel_to_idx(u8 band, u8 channel)
{
	int ch_idx;
	u8 n_channel;

	if (band == PHY_BAND_2G) {
		ch_idx = channel - 1;
		n_channel = RTW_MAX_CHANNEL_NUM_2G;
	} else if (band == PHY_BAND_5G) {
		n_channel = RTW_MAX_CHANNEL_NUM_5G;
		for (ch_idx = 0; ch_idx < n_channel; ch_idx++)
			if (rtw_channel_idx_5g[ch_idx] == channel)
				break;
	} else {
		return -1;
	}

	if (ch_idx >= n_channel)
		return -1;

	return ch_idx;
}

static void rtw_phy_set_tx_power_limit(struct rtw_dev *rtwdev, u8 regd, u8 band,
				       u8 bw, u8 rs, u8 ch, s8 pwr_limit)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u8 max_power_index = rtwdev->chip->max_power_index;
	s8 ww;
	int ch_idx;

	pwr_limit = clamp_t(s8, pwr_limit,
			    -max_power_index, max_power_index);
	ch_idx = rtw_channel_to_idx(band, ch);

	if (regd >= RTW_REGD_MAX || bw >= RTW_CHANNEL_WIDTH_MAX ||
	    rs >= RTW_RATE_SECTION_MAX || ch_idx < 0) {
		WARN(1,
		     "wrong txpwr_lmt regd=%u, band=%u bw=%u, rs=%u, ch_idx=%u, pwr_limit=%d\n",
		     regd, band, bw, rs, ch_idx, pwr_limit);
		return;
	}

	if (band == PHY_BAND_2G) {
		hal->tx_pwr_limit_2g[regd][bw][rs][ch_idx] = pwr_limit;
		ww = hal->tx_pwr_limit_2g[RTW_REGD_WW][bw][rs][ch_idx];
		ww = min_t(s8, ww, pwr_limit);
		hal->tx_pwr_limit_2g[RTW_REGD_WW][bw][rs][ch_idx] = ww;
	} else if (band == PHY_BAND_5G) {
		hal->tx_pwr_limit_5g[regd][bw][rs][ch_idx] = pwr_limit;
		ww = hal->tx_pwr_limit_5g[RTW_REGD_WW][bw][rs][ch_idx];
		ww = min_t(s8, ww, pwr_limit);
		hal->tx_pwr_limit_5g[RTW_REGD_WW][bw][rs][ch_idx] = ww;
	}
}

/* cross-reference 5G power limits if values are not assigned */
static void
rtw_xref_5g_txpwr_lmt(struct rtw_dev *rtwdev, u8 regd,
		      u8 bw, u8 ch_idx, u8 rs_ht, u8 rs_vht)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u8 max_power_index = rtwdev->chip->max_power_index;
	s8 lmt_ht = hal->tx_pwr_limit_5g[regd][bw][rs_ht][ch_idx];
	s8 lmt_vht = hal->tx_pwr_limit_5g[regd][bw][rs_vht][ch_idx];

	if (lmt_ht == lmt_vht)
		return;

	if (lmt_ht == max_power_index)
		hal->tx_pwr_limit_5g[regd][bw][rs_ht][ch_idx] = lmt_vht;

	else if (lmt_vht == max_power_index)
		hal->tx_pwr_limit_5g[regd][bw][rs_vht][ch_idx] = lmt_ht;
}

/* cross-reference power limits for ht and vht */
static void
rtw_xref_txpwr_lmt_by_rs(struct rtw_dev *rtwdev, u8 regd, u8 bw, u8 ch_idx)
{
	u8 rs_idx, rs_ht, rs_vht;
	u8 rs_cmp[2][2] = {{RTW_RATE_SECTION_HT_1S, RTW_RATE_SECTION_VHT_1S},
			   {RTW_RATE_SECTION_HT_2S, RTW_RATE_SECTION_VHT_2S} };

	for (rs_idx = 0; rs_idx < 2; rs_idx++) {
		rs_ht = rs_cmp[rs_idx][0];
		rs_vht = rs_cmp[rs_idx][1];

		rtw_xref_5g_txpwr_lmt(rtwdev, regd, bw, ch_idx, rs_ht, rs_vht);
	}
}

/* cross-reference power limits for 5G channels */
static void
rtw_xref_5g_txpwr_lmt_by_ch(struct rtw_dev *rtwdev, u8 regd, u8 bw)
{
	u8 ch_idx;

	for (ch_idx = 0; ch_idx < RTW_MAX_CHANNEL_NUM_5G; ch_idx++)
		rtw_xref_txpwr_lmt_by_rs(rtwdev, regd, bw, ch_idx);
}

/* cross-reference power limits for 20/40M bandwidth */
static void
rtw_xref_txpwr_lmt_by_bw(struct rtw_dev *rtwdev, u8 regd)
{
	u8 bw;

	for (bw = RTW_CHANNEL_WIDTH_20; bw <= RTW_CHANNEL_WIDTH_40; bw++)
		rtw_xref_5g_txpwr_lmt_by_ch(rtwdev, regd, bw);
}

/* cross-reference power limits */
static void rtw_xref_txpwr_lmt(struct rtw_dev *rtwdev)
{
	u8 regd;

	for (regd = 0; regd < RTW_REGD_MAX; regd++)
		rtw_xref_txpwr_lmt_by_bw(rtwdev, regd);
}

static void
__cfg_txpwr_lmt_by_alt(struct rtw_hal *hal, u8 regd, u8 regd_alt, u8 bw, u8 rs)
{
	u8 ch;

	for (ch = 0; ch < RTW_MAX_CHANNEL_NUM_2G; ch++)
		hal->tx_pwr_limit_2g[regd][bw][rs][ch] =
			hal->tx_pwr_limit_2g[regd_alt][bw][rs][ch];

	for (ch = 0; ch < RTW_MAX_CHANNEL_NUM_5G; ch++)
		hal->tx_pwr_limit_5g[regd][bw][rs][ch] =
			hal->tx_pwr_limit_5g[regd_alt][bw][rs][ch];
}

static void
rtw_cfg_txpwr_lmt_by_alt(struct rtw_dev *rtwdev, u8 regd, u8 regd_alt)
{
	u8 bw, rs;

	for (bw = 0; bw < RTW_CHANNEL_WIDTH_MAX; bw++)
		for (rs = 0; rs < RTW_RATE_SECTION_MAX; rs++)
			__cfg_txpwr_lmt_by_alt(&rtwdev->hal, regd, regd_alt,
					       bw, rs);
}

void rtw_parse_tbl_txpwr_lmt(struct rtw_dev *rtwdev,
			     const struct rtw_table *tbl)
{
	const struct rtw_txpwr_lmt_cfg_pair *p = tbl->data;
	const struct rtw_txpwr_lmt_cfg_pair *end = p + tbl->size;
	u32 regd_cfg_flag = 0;
	u8 regd_alt;
	u8 i;

	for (; p < end; p++) {
		regd_cfg_flag |= BIT(p->regd);
		rtw_phy_set_tx_power_limit(rtwdev, p->regd, p->band,
					   p->bw, p->rs, p->ch, p->txpwr_lmt);
	}

	for (i = 0; i < RTW_REGD_MAX; i++) {
		if (i == RTW_REGD_WW)
			continue;

		if (regd_cfg_flag & BIT(i))
			continue;

		rtw_dbg(rtwdev, RTW_DBG_REGD,
			"txpwr regd %d does not be configured\n", i);

		if (rtw_regd_has_alt(i, &regd_alt) &&
		    regd_cfg_flag & BIT(regd_alt)) {
			rtw_dbg(rtwdev, RTW_DBG_REGD,
				"cfg txpwr regd %d by regd %d as alternative\n",
				i, regd_alt);

			rtw_cfg_txpwr_lmt_by_alt(rtwdev, i, regd_alt);
			continue;
		}

		rtw_dbg(rtwdev, RTW_DBG_REGD, "cfg txpwr regd %d by WW\n", i);
		rtw_cfg_txpwr_lmt_by_alt(rtwdev, i, RTW_REGD_WW);
	}

	rtw_xref_txpwr_lmt(rtwdev);
}
EXPORT_SYMBOL(rtw_parse_tbl_txpwr_lmt);

void rtw_phy_cfg_mac(struct rtw_dev *rtwdev, const struct rtw_table *tbl,
		     u32 addr, u32 data)
{
	rtw_write8(rtwdev, addr, data);
}
EXPORT_SYMBOL(rtw_phy_cfg_mac);

void rtw_phy_cfg_agc(struct rtw_dev *rtwdev, const struct rtw_table *tbl,
		     u32 addr, u32 data)
{
	rtw_write32(rtwdev, addr, data);
}
EXPORT_SYMBOL(rtw_phy_cfg_agc);

void rtw_phy_cfg_bb(struct rtw_dev *rtwdev, const struct rtw_table *tbl,
		    u32 addr, u32 data)
{
	if (addr == 0xfe)
		msleep(50);
	else if (addr == 0xfd)
		mdelay(5);
	else if (addr == 0xfc)
		mdelay(1);
	else if (addr == 0xfb)
		usleep_range(50, 60);
	else if (addr == 0xfa)
		udelay(5);
	else if (addr == 0xf9)
		udelay(1);
	else
		rtw_write32(rtwdev, addr, data);
}
EXPORT_SYMBOL(rtw_phy_cfg_bb);

void rtw_phy_cfg_rf(struct rtw_dev *rtwdev, const struct rtw_table *tbl,
		    u32 addr, u32 data)
{
	if (addr == 0xffe) {
		msleep(50);
	} else if (addr == 0xfe) {
		usleep_range(100, 110);
	} else {
		rtw_write_rf(rtwdev, tbl->rf_path, addr, RFREG_MASK, data);
		udelay(1);
	}
}
EXPORT_SYMBOL(rtw_phy_cfg_rf);

static void rtw_load_rfk_table(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;

	if (!chip->rfk_init_tbl)
		return;

	rtw_write32_mask(rtwdev, 0x1e24, BIT(17), 0x1);
	rtw_write32_mask(rtwdev, 0x1cd0, BIT(28), 0x1);
	rtw_write32_mask(rtwdev, 0x1cd0, BIT(29), 0x1);
	rtw_write32_mask(rtwdev, 0x1cd0, BIT(30), 0x1);
	rtw_write32_mask(rtwdev, 0x1cd0, BIT(31), 0x0);

	rtw_load_table(rtwdev, chip->rfk_init_tbl);

	dpk_info->is_dpk_pwr_on = true;
}

void rtw_phy_load_tables(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 rf_path;

	rtw_load_table(rtwdev, chip->mac_tbl);
	rtw_load_table(rtwdev, chip->bb_tbl);
	rtw_load_table(rtwdev, chip->agc_tbl);
	rtw_load_rfk_table(rtwdev);

	for (rf_path = 0; rf_path < rtwdev->hal.rf_path_num; rf_path++) {
		const struct rtw_table *tbl;

		tbl = chip->rf_tbl[rf_path];
		rtw_load_table(rtwdev, tbl);
	}
}
EXPORT_SYMBOL(rtw_phy_load_tables);

static u8 rtw_get_channel_group(u8 channel, u8 rate)
{
	switch (channel) {
	default:
		WARN_ON(1);
		fallthrough;
	case 1:
	case 2:
	case 36:
	case 38:
	case 40:
	case 42:
		return 0;
	case 3:
	case 4:
	case 5:
	case 44:
	case 46:
	case 48:
	case 50:
		return 1;
	case 6:
	case 7:
	case 8:
	case 52:
	case 54:
	case 56:
	case 58:
		return 2;
	case 9:
	case 10:
	case 11:
	case 60:
	case 62:
	case 64:
		return 3;
	case 12:
	case 13:
	case 100:
	case 102:
	case 104:
	case 106:
		return 4;
	case 14:
		return rate <= DESC_RATE11M ? 5 : 4;
	case 108:
	case 110:
	case 112:
	case 114:
		return 5;
	case 116:
	case 118:
	case 120:
	case 122:
		return 6;
	case 124:
	case 126:
	case 128:
	case 130:
		return 7;
	case 132:
	case 134:
	case 136:
	case 138:
		return 8;
	case 140:
	case 142:
	case 144:
		return 9;
	case 149:
	case 151:
	case 153:
	case 155:
		return 10;
	case 157:
	case 159:
	case 161:
		return 11;
	case 165:
	case 167:
	case 169:
	case 171:
		return 12;
	case 173:
	case 175:
	case 177:
		return 13;
	}
}

static s8 rtw_phy_get_dis_dpd_by_rate_diff(struct rtw_dev *rtwdev, u16 rate)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	s8 dpd_diff = 0;

	if (!chip->en_dis_dpd)
		return 0;

#define RTW_DPD_RATE_CHECK(_rate)					\
	case DESC_RATE ## _rate:					\
	if (DIS_DPD_RATE ## _rate & chip->dpd_ratemask)			\
		dpd_diff = -6 * chip->txgi_factor;			\
	break

	switch (rate) {
	RTW_DPD_RATE_CHECK(6M);
	RTW_DPD_RATE_CHECK(9M);
	RTW_DPD_RATE_CHECK(MCS0);
	RTW_DPD_RATE_CHECK(MCS1);
	RTW_DPD_RATE_CHECK(MCS8);
	RTW_DPD_RATE_CHECK(MCS9);
	RTW_DPD_RATE_CHECK(VHT1SS_MCS0);
	RTW_DPD_RATE_CHECK(VHT1SS_MCS1);
	RTW_DPD_RATE_CHECK(VHT2SS_MCS0);
	RTW_DPD_RATE_CHECK(VHT2SS_MCS1);
	}
#undef RTW_DPD_RATE_CHECK

	return dpd_diff;
}

static u8 rtw_phy_get_2g_tx_power_index(struct rtw_dev *rtwdev,
					struct rtw_2g_txpwr_idx *pwr_idx_2g,
					enum rtw_bandwidth bandwidth,
					u8 rate, u8 group)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 tx_power;
	bool mcs_rate;
	bool above_2ss;
	u8 factor = chip->txgi_factor;

	if (rate <= DESC_RATE11M)
		tx_power = pwr_idx_2g->cck_base[group];
	else
		tx_power = pwr_idx_2g->bw40_base[group];

	if (rate >= DESC_RATE6M && rate <= DESC_RATE54M)
		tx_power += pwr_idx_2g->ht_1s_diff.ofdm * factor;

	mcs_rate = (rate >= DESC_RATEMCS0 && rate <= DESC_RATEMCS15) ||
		   (rate >= DESC_RATEVHT1SS_MCS0 &&
		    rate <= DESC_RATEVHT2SS_MCS9);
	above_2ss = (rate >= DESC_RATEMCS8 && rate <= DESC_RATEMCS15) ||
		    (rate >= DESC_RATEVHT2SS_MCS0);

	if (!mcs_rate)
		return tx_power;

	switch (bandwidth) {
	default:
		WARN_ON(1);
		fallthrough;
	case RTW_CHANNEL_WIDTH_20:
		tx_power += pwr_idx_2g->ht_1s_diff.bw20 * factor;
		if (above_2ss)
			tx_power += pwr_idx_2g->ht_2s_diff.bw20 * factor;
		break;
	case RTW_CHANNEL_WIDTH_40:
		/* bw40 is the base power */
		if (above_2ss)
			tx_power += pwr_idx_2g->ht_2s_diff.bw40 * factor;
		break;
	}

	return tx_power;
}

static u8 rtw_phy_get_5g_tx_power_index(struct rtw_dev *rtwdev,
					struct rtw_5g_txpwr_idx *pwr_idx_5g,
					enum rtw_bandwidth bandwidth,
					u8 rate, u8 group)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	u8 tx_power;
	u8 upper, lower;
	bool mcs_rate;
	bool above_2ss;
	u8 factor = chip->txgi_factor;

	tx_power = pwr_idx_5g->bw40_base[group];

	mcs_rate = (rate >= DESC_RATEMCS0 && rate <= DESC_RATEMCS15) ||
		   (rate >= DESC_RATEVHT1SS_MCS0 &&
		    rate <= DESC_RATEVHT2SS_MCS9);
	above_2ss = (rate >= DESC_RATEMCS8 && rate <= DESC_RATEMCS15) ||
		    (rate >= DESC_RATEVHT2SS_MCS0);

	if (!mcs_rate) {
		tx_power += pwr_idx_5g->ht_1s_diff.ofdm * factor;
		return tx_power;
	}

	switch (bandwidth) {
	default:
		WARN_ON(1);
		fallthrough;
	case RTW_CHANNEL_WIDTH_20:
		tx_power += pwr_idx_5g->ht_1s_diff.bw20 * factor;
		if (above_2ss)
			tx_power += pwr_idx_5g->ht_2s_diff.bw20 * factor;
		break;
	case RTW_CHANNEL_WIDTH_40:
		/* bw40 is the base power */
		if (above_2ss)
			tx_power += pwr_idx_5g->ht_2s_diff.bw40 * factor;
		break;
	case RTW_CHANNEL_WIDTH_80:
		/* the base idx of bw80 is the average of bw40+/bw40- */
		lower = pwr_idx_5g->bw40_base[group];
		upper = pwr_idx_5g->bw40_base[group + 1];

		tx_power = (lower + upper) / 2;
		tx_power += pwr_idx_5g->vht_1s_diff.bw80 * factor;
		if (above_2ss)
			tx_power += pwr_idx_5g->vht_2s_diff.bw80 * factor;
		break;
	}

	return tx_power;
}

static s8 rtw_phy_get_tx_power_limit(struct rtw_dev *rtwdev, u8 band,
				     enum rtw_bandwidth bw, u8 rf_path,
				     u8 rate, u8 channel, u8 regd)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u8 *cch_by_bw = hal->cch_by_bw;
	s8 power_limit = (s8)rtwdev->chip->max_power_index;
	u8 rs;
	int ch_idx;
	u8 cur_bw, cur_ch;
	s8 cur_lmt;

	if (regd > RTW_REGD_WW)
		return power_limit;

	if (rate >= DESC_RATE1M && rate <= DESC_RATE11M)
		rs = RTW_RATE_SECTION_CCK;
	else if (rate >= DESC_RATE6M && rate <= DESC_RATE54M)
		rs = RTW_RATE_SECTION_OFDM;
	else if (rate >= DESC_RATEMCS0 && rate <= DESC_RATEMCS7)
		rs = RTW_RATE_SECTION_HT_1S;
	else if (rate >= DESC_RATEMCS8 && rate <= DESC_RATEMCS15)
		rs = RTW_RATE_SECTION_HT_2S;
	else if (rate >= DESC_RATEVHT1SS_MCS0 && rate <= DESC_RATEVHT1SS_MCS9)
		rs = RTW_RATE_SECTION_VHT_1S;
	else if (rate >= DESC_RATEVHT2SS_MCS0 && rate <= DESC_RATEVHT2SS_MCS9)
		rs = RTW_RATE_SECTION_VHT_2S;
	else
		goto err;

	/* only 20M BW with cck and ofdm */
	if (rs == RTW_RATE_SECTION_CCK || rs == RTW_RATE_SECTION_OFDM)
		bw = RTW_CHANNEL_WIDTH_20;

	/* only 20/40M BW with ht */
	if (rs == RTW_RATE_SECTION_HT_1S || rs == RTW_RATE_SECTION_HT_2S)
		bw = min_t(u8, bw, RTW_CHANNEL_WIDTH_40);

	/* select min power limit among [20M BW ~ current BW] */
	for (cur_bw = RTW_CHANNEL_WIDTH_20; cur_bw <= bw; cur_bw++) {
		cur_ch = cch_by_bw[cur_bw];

		ch_idx = rtw_channel_to_idx(band, cur_ch);
		if (ch_idx < 0)
			goto err;

		cur_lmt = cur_ch <= RTW_MAX_CHANNEL_NUM_2G ?
			hal->tx_pwr_limit_2g[regd][cur_bw][rs][ch_idx] :
			hal->tx_pwr_limit_5g[regd][cur_bw][rs][ch_idx];

		power_limit = min_t(s8, cur_lmt, power_limit);
	}

	return power_limit;

err:
	WARN(1, "invalid arguments, band=%d, bw=%d, path=%d, rate=%d, ch=%d\n",
	     band, bw, rf_path, rate, channel);
	return (s8)rtwdev->chip->max_power_index;
}

void rtw_get_tx_power_params(struct rtw_dev *rtwdev, u8 path, u8 rate, u8 bw,
			     u8 ch, u8 regd, struct rtw_power_params *pwr_param)
{
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	struct rtw_txpwr_idx *pwr_idx;
	u8 group, band;
	u8 *base = &pwr_param->pwr_base;
	s8 *offset = &pwr_param->pwr_offset;
	s8 *limit = &pwr_param->pwr_limit;
	s8 *remnant = &pwr_param->pwr_remnant;

	pwr_idx = &rtwdev->efuse.txpwr_idx_table[path];
	group = rtw_get_channel_group(ch, rate);

	/* base power index for 2.4G/5G */
	if (IS_CH_2G_BAND(ch)) {
		band = PHY_BAND_2G;
		*base = rtw_phy_get_2g_tx_power_index(rtwdev,
						      &pwr_idx->pwr_idx_2g,
						      bw, rate, group);
		*offset = hal->tx_pwr_by_rate_offset_2g[path][rate];
	} else {
		band = PHY_BAND_5G;
		*base = rtw_phy_get_5g_tx_power_index(rtwdev,
						      &pwr_idx->pwr_idx_5g,
						      bw, rate, group);
		*offset = hal->tx_pwr_by_rate_offset_5g[path][rate];
	}

	*limit = rtw_phy_get_tx_power_limit(rtwdev, band, bw, path,
					    rate, ch, regd);
	*remnant = (rate <= DESC_RATE11M ? dm_info->txagc_remnant_cck :
		    dm_info->txagc_remnant_ofdm);
}

u8
rtw_phy_get_tx_power_index(struct rtw_dev *rtwdev, u8 rf_path, u8 rate,
			   enum rtw_bandwidth bandwidth, u8 channel, u8 regd)
{
	struct rtw_power_params pwr_param = {0};
	u8 tx_power;
	s8 offset;

	rtw_get_tx_power_params(rtwdev, rf_path, rate, bandwidth,
				channel, regd, &pwr_param);

	tx_power = pwr_param.pwr_base;
	offset = min_t(s8, pwr_param.pwr_offset, pwr_param.pwr_limit);

	if (rtwdev->chip->en_dis_dpd)
		offset += rtw_phy_get_dis_dpd_by_rate_diff(rtwdev, rate);

	tx_power += offset + pwr_param.pwr_remnant;

	if (tx_power > rtwdev->chip->max_power_index)
		tx_power = rtwdev->chip->max_power_index;

	return tx_power;
}
EXPORT_SYMBOL(rtw_phy_get_tx_power_index);

static void rtw_phy_set_tx_power_index_by_rs(struct rtw_dev *rtwdev,
					     u8 ch, u8 path, u8 rs)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u8 regd = rtw_regd_get(rtwdev);
	u8 *rates;
	u8 size;
	u8 rate;
	u8 pwr_idx;
	u8 bw;
	int i;

	if (rs >= RTW_RATE_SECTION_MAX)
		return;

	rates = rtw_rate_section[rs];
	size = rtw_rate_size[rs];
	bw = hal->current_band_width;
	for (i = 0; i < size; i++) {
		rate = rates[i];
		pwr_idx = rtw_phy_get_tx_power_index(rtwdev, path, rate,
						     bw, ch, regd);
		hal->tx_pwr_tbl[path][rate] = pwr_idx;
	}
}

/* set tx power level by path for each rates, note that the order of the rates
 * are *very* important, bacause 8822B/8821C combines every four bytes of tx
 * power index into a four-byte power index register, and calls set_tx_agc to
 * write these values into hardware
 */
static void rtw_phy_set_tx_power_level_by_path(struct rtw_dev *rtwdev,
					       u8 ch, u8 path)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u8 rs;

	/* do not need cck rates if we are not in 2.4G */
	if (hal->current_band_type == RTW_BAND_2G)
		rs = RTW_RATE_SECTION_CCK;
	else
		rs = RTW_RATE_SECTION_OFDM;

	for (; rs < RTW_RATE_SECTION_MAX; rs++)
		rtw_phy_set_tx_power_index_by_rs(rtwdev, ch, path, rs);
}

void rtw_phy_set_tx_power_level(struct rtw_dev *rtwdev, u8 channel)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_hal *hal = &rtwdev->hal;
	u8 path;

	mutex_lock(&hal->tx_power_mutex);

	for (path = 0; path < hal->rf_path_num; path++)
		rtw_phy_set_tx_power_level_by_path(rtwdev, channel, path);

	chip->ops->set_tx_power_index(rtwdev);
	mutex_unlock(&hal->tx_power_mutex);
}
EXPORT_SYMBOL(rtw_phy_set_tx_power_level);

static void
rtw_phy_tx_power_by_rate_config_by_path(struct rtw_hal *hal, u8 path,
					u8 rs, u8 size, u8 *rates)
{
	u8 rate;
	u8 base_idx, rate_idx;
	s8 base_2g, base_5g;

	if (rs >= RTW_RATE_SECTION_VHT_1S)
		base_idx = rates[size - 3];
	else
		base_idx = rates[size - 1];
	base_2g = hal->tx_pwr_by_rate_offset_2g[path][base_idx];
	base_5g = hal->tx_pwr_by_rate_offset_5g[path][base_idx];
	hal->tx_pwr_by_rate_base_2g[path][rs] = base_2g;
	hal->tx_pwr_by_rate_base_5g[path][rs] = base_5g;
	for (rate = 0; rate < size; rate++) {
		rate_idx = rates[rate];
		hal->tx_pwr_by_rate_offset_2g[path][rate_idx] -= base_2g;
		hal->tx_pwr_by_rate_offset_5g[path][rate_idx] -= base_5g;
	}
}

void rtw_phy_tx_power_by_rate_config(struct rtw_hal *hal)
{
	u8 path;

	for (path = 0; path < RTW_RF_PATH_MAX; path++) {
		rtw_phy_tx_power_by_rate_config_by_path(hal, path,
				RTW_RATE_SECTION_CCK,
				rtw_cck_size, rtw_cck_rates);
		rtw_phy_tx_power_by_rate_config_by_path(hal, path,
				RTW_RATE_SECTION_OFDM,
				rtw_ofdm_size, rtw_ofdm_rates);
		rtw_phy_tx_power_by_rate_config_by_path(hal, path,
				RTW_RATE_SECTION_HT_1S,
				rtw_ht_1s_size, rtw_ht_1s_rates);
		rtw_phy_tx_power_by_rate_config_by_path(hal, path,
				RTW_RATE_SECTION_HT_2S,
				rtw_ht_2s_size, rtw_ht_2s_rates);
		rtw_phy_tx_power_by_rate_config_by_path(hal, path,
				RTW_RATE_SECTION_VHT_1S,
				rtw_vht_1s_size, rtw_vht_1s_rates);
		rtw_phy_tx_power_by_rate_config_by_path(hal, path,
				RTW_RATE_SECTION_VHT_2S,
				rtw_vht_2s_size, rtw_vht_2s_rates);
	}
}

static void
__rtw_phy_tx_power_limit_config(struct rtw_hal *hal, u8 regd, u8 bw, u8 rs)
{
	s8 base;
	u8 ch;

	for (ch = 0; ch < RTW_MAX_CHANNEL_NUM_2G; ch++) {
		base = hal->tx_pwr_by_rate_base_2g[0][rs];
		hal->tx_pwr_limit_2g[regd][bw][rs][ch] -= base;
	}

	for (ch = 0; ch < RTW_MAX_CHANNEL_NUM_5G; ch++) {
		base = hal->tx_pwr_by_rate_base_5g[0][rs];
		hal->tx_pwr_limit_5g[regd][bw][rs][ch] -= base;
	}
}

void rtw_phy_tx_power_limit_config(struct rtw_hal *hal)
{
	u8 regd, bw, rs;

	/* default at channel 1 */
	hal->cch_by_bw[RTW_CHANNEL_WIDTH_20] = 1;

	for (regd = 0; regd < RTW_REGD_MAX; regd++)
		for (bw = 0; bw < RTW_CHANNEL_WIDTH_MAX; bw++)
			for (rs = 0; rs < RTW_RATE_SECTION_MAX; rs++)
				__rtw_phy_tx_power_limit_config(hal, regd, bw, rs);
}

static void rtw_phy_init_tx_power_limit(struct rtw_dev *rtwdev,
					u8 regd, u8 bw, u8 rs)
{
	struct rtw_hal *hal = &rtwdev->hal;
	s8 max_power_index = (s8)rtwdev->chip->max_power_index;
	u8 ch;

	/* 2.4G channels */
	for (ch = 0; ch < RTW_MAX_CHANNEL_NUM_2G; ch++)
		hal->tx_pwr_limit_2g[regd][bw][rs][ch] = max_power_index;

	/* 5G channels */
	for (ch = 0; ch < RTW_MAX_CHANNEL_NUM_5G; ch++)
		hal->tx_pwr_limit_5g[regd][bw][rs][ch] = max_power_index;
}

void rtw_phy_init_tx_power(struct rtw_dev *rtwdev)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u8 regd, path, rate, rs, bw;

	/* init tx power by rate offset */
	for (path = 0; path < RTW_RF_PATH_MAX; path++) {
		for (rate = 0; rate < DESC_RATE_MAX; rate++) {
			hal->tx_pwr_by_rate_offset_2g[path][rate] = 0;
			hal->tx_pwr_by_rate_offset_5g[path][rate] = 0;
		}
	}

	/* init tx power limit */
	for (regd = 0; regd < RTW_REGD_MAX; regd++)
		for (bw = 0; bw < RTW_CHANNEL_WIDTH_MAX; bw++)
			for (rs = 0; rs < RTW_RATE_SECTION_MAX; rs++)
				rtw_phy_init_tx_power_limit(rtwdev, regd, bw,
							    rs);
}

void rtw_phy_config_swing_table(struct rtw_dev *rtwdev,
				struct rtw_swing_table *swing_table)
{
	const struct rtw_pwr_track_tbl *tbl = rtwdev->chip->pwr_track_tbl;
	u8 channel = rtwdev->hal.current_channel;

	if (IS_CH_2G_BAND(channel)) {
		if (rtwdev->dm_info.tx_rate <= DESC_RATE11M) {
			swing_table->p[RF_PATH_A] = tbl->pwrtrk_2g_ccka_p;
			swing_table->n[RF_PATH_A] = tbl->pwrtrk_2g_ccka_n;
			swing_table->p[RF_PATH_B] = tbl->pwrtrk_2g_cckb_p;
			swing_table->n[RF_PATH_B] = tbl->pwrtrk_2g_cckb_n;
		} else {
			swing_table->p[RF_PATH_A] = tbl->pwrtrk_2ga_p;
			swing_table->n[RF_PATH_A] = tbl->pwrtrk_2ga_n;
			swing_table->p[RF_PATH_B] = tbl->pwrtrk_2gb_p;
			swing_table->n[RF_PATH_B] = tbl->pwrtrk_2gb_n;
		}
	} else if (IS_CH_5G_BAND_1(channel) || IS_CH_5G_BAND_2(channel)) {
		swing_table->p[RF_PATH_A] = tbl->pwrtrk_5ga_p[RTW_PWR_TRK_5G_1];
		swing_table->n[RF_PATH_A] = tbl->pwrtrk_5ga_n[RTW_PWR_TRK_5G_1];
		swing_table->p[RF_PATH_B] = tbl->pwrtrk_5gb_p[RTW_PWR_TRK_5G_1];
		swing_table->n[RF_PATH_B] = tbl->pwrtrk_5gb_n[RTW_PWR_TRK_5G_1];
	} else if (IS_CH_5G_BAND_3(channel)) {
		swing_table->p[RF_PATH_A] = tbl->pwrtrk_5ga_p[RTW_PWR_TRK_5G_2];
		swing_table->n[RF_PATH_A] = tbl->pwrtrk_5ga_n[RTW_PWR_TRK_5G_2];
		swing_table->p[RF_PATH_B] = tbl->pwrtrk_5gb_p[RTW_PWR_TRK_5G_2];
		swing_table->n[RF_PATH_B] = tbl->pwrtrk_5gb_n[RTW_PWR_TRK_5G_2];
	} else if (IS_CH_5G_BAND_4(channel)) {
		swing_table->p[RF_PATH_A] = tbl->pwrtrk_5ga_p[RTW_PWR_TRK_5G_3];
		swing_table->n[RF_PATH_A] = tbl->pwrtrk_5ga_n[RTW_PWR_TRK_5G_3];
		swing_table->p[RF_PATH_B] = tbl->pwrtrk_5gb_p[RTW_PWR_TRK_5G_3];
		swing_table->n[RF_PATH_B] = tbl->pwrtrk_5gb_n[RTW_PWR_TRK_5G_3];
	} else {
		swing_table->p[RF_PATH_A] = tbl->pwrtrk_2ga_p;
		swing_table->n[RF_PATH_A] = tbl->pwrtrk_2ga_n;
		swing_table->p[RF_PATH_B] = tbl->pwrtrk_2gb_p;
		swing_table->n[RF_PATH_B] = tbl->pwrtrk_2gb_n;
	}
}
EXPORT_SYMBOL(rtw_phy_config_swing_table);

void rtw_phy_pwrtrack_avg(struct rtw_dev *rtwdev, u8 thermal, u8 path)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;

	ewma_thermal_add(&dm_info->avg_thermal[path], thermal);
	dm_info->thermal_avg[path] =
		ewma_thermal_read(&dm_info->avg_thermal[path]);
}
EXPORT_SYMBOL(rtw_phy_pwrtrack_avg);

bool rtw_phy_pwrtrack_thermal_changed(struct rtw_dev *rtwdev, u8 thermal,
				      u8 path)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 avg = ewma_thermal_read(&dm_info->avg_thermal[path]);

	if (avg == thermal)
		return false;

	return true;
}
EXPORT_SYMBOL(rtw_phy_pwrtrack_thermal_changed);

u8 rtw_phy_pwrtrack_get_delta(struct rtw_dev *rtwdev, u8 path)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 therm_avg, therm_efuse, therm_delta;

	therm_avg = dm_info->thermal_avg[path];
	therm_efuse = rtwdev->efuse.thermal_meter[path];
	therm_delta = abs(therm_avg - therm_efuse);

	return min_t(u8, therm_delta, RTW_PWR_TRK_TBL_SZ - 1);
}
EXPORT_SYMBOL(rtw_phy_pwrtrack_get_delta);

s8 rtw_phy_pwrtrack_get_pwridx(struct rtw_dev *rtwdev,
			       struct rtw_swing_table *swing_table,
			       u8 tbl_path, u8 therm_path, u8 delta)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	const u8 *delta_swing_table_idx_pos;
	const u8 *delta_swing_table_idx_neg;

	if (delta >= RTW_PWR_TRK_TBL_SZ) {
		rtw_warn(rtwdev, "power track table overflow\n");
		return 0;
	}

	if (!swing_table) {
		rtw_warn(rtwdev, "swing table not configured\n");
		return 0;
	}

	delta_swing_table_idx_pos = swing_table->p[tbl_path];
	delta_swing_table_idx_neg = swing_table->n[tbl_path];

	if (!delta_swing_table_idx_pos || !delta_swing_table_idx_neg) {
		rtw_warn(rtwdev, "invalid swing table index\n");
		return 0;
	}

	if (dm_info->thermal_avg[therm_path] >
	    rtwdev->efuse.thermal_meter[therm_path])
		return delta_swing_table_idx_pos[delta];
	else
		return -delta_swing_table_idx_neg[delta];
}
EXPORT_SYMBOL(rtw_phy_pwrtrack_get_pwridx);

bool rtw_phy_pwrtrack_need_lck(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 delta_lck;

	delta_lck = abs(dm_info->thermal_avg[0] - dm_info->thermal_meter_lck);
	if (delta_lck >= rtwdev->chip->lck_threshold) {
		dm_info->thermal_meter_lck = dm_info->thermal_avg[0];
		return true;
	}
	return false;
}
EXPORT_SYMBOL(rtw_phy_pwrtrack_need_lck);

bool rtw_phy_pwrtrack_need_iqk(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 delta_iqk;

	delta_iqk = abs(dm_info->thermal_avg[0] - dm_info->thermal_meter_k);
	if (delta_iqk >= rtwdev->chip->iqk_threshold) {
		dm_info->thermal_meter_k = dm_info->thermal_avg[0];
		return true;
	}
	return false;
}
EXPORT_SYMBOL(rtw_phy_pwrtrack_need_iqk);

static void rtw_phy_set_tx_path_by_reg(struct rtw_dev *rtwdev,
				       enum rtw_bb_path tx_path_sel_1ss)
{
	struct rtw_path_div *path_div = &rtwdev->dm_path_div;
	enum rtw_bb_path tx_path_sel_cck = tx_path_sel_1ss;
	struct rtw_chip_info *chip = rtwdev->chip;

	if (tx_path_sel_1ss == path_div->current_tx_path)
		return;

	path_div->current_tx_path = tx_path_sel_1ss;
	rtw_dbg(rtwdev, RTW_DBG_PATH_DIV, "Switch TX path=%s\n",
		tx_path_sel_1ss == BB_PATH_A ? "A" : "B");
	chip->ops->config_tx_path(rtwdev, rtwdev->hal.antenna_tx,
				  tx_path_sel_1ss, tx_path_sel_cck, false);
}

static void rtw_phy_tx_path_div_select(struct rtw_dev *rtwdev)
{
	struct rtw_path_div *path_div = &rtwdev->dm_path_div;
	enum rtw_bb_path path = path_div->current_tx_path;
	s32 rssi_a = 0, rssi_b = 0;

	if (path_div->path_a_cnt)
		rssi_a = path_div->path_a_sum / path_div->path_a_cnt;
	else
		rssi_a = 0;
	if (path_div->path_b_cnt)
		rssi_b = path_div->path_b_sum / path_div->path_b_cnt;
	else
		rssi_b = 0;

	if (rssi_a != rssi_b)
		path = (rssi_a > rssi_b) ? BB_PATH_A : BB_PATH_B;

	path_div->path_a_cnt = 0;
	path_div->path_a_sum = 0;
	path_div->path_b_cnt = 0;
	path_div->path_b_sum = 0;
	rtw_phy_set_tx_path_by_reg(rtwdev, path);
}

static void rtw_phy_tx_path_diversity_2ss(struct rtw_dev *rtwdev)
{
	if (rtwdev->hal.antenna_rx != BB_PATH_AB) {
		rtw_dbg(rtwdev, RTW_DBG_PATH_DIV,
			"[Return] tx_Path_en=%d, rx_Path_en=%d\n",
			rtwdev->hal.antenna_tx, rtwdev->hal.antenna_rx);
		return;
	}
	if (rtwdev->sta_cnt == 0) {
		rtw_dbg(rtwdev, RTW_DBG_PATH_DIV, "No Link\n");
		return;
	}

	rtw_phy_tx_path_div_select(rtwdev);
}

void rtw_phy_tx_path_diversity(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;

	if (!chip->path_div_supported)
		return;

	rtw_phy_tx_path_diversity_2ss(rtwdev);
}
