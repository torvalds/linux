// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include <linux/bcd.h>

#include "main.h"
#include "reg.h"
#include "fw.h"
#include "phy.h"
#include "debug.h"

struct phy_cfg_pair {
	u32 addr;
	u32 data;
};

union phy_table_tile {
	struct rtw_phy_cond cond;
	struct phy_cfg_pair cfg;
};

struct phy_pg_cfg_pair {
	u32 band;
	u32 rf_path;
	u32 tx_num;
	u32 addr;
	u32 bitmask;
	u32 data;
};

struct txpwr_lmt_cfg_pair {
	u8 regd;
	u8 band;
	u8 bw;
	u8 rs;
	u8 ch;
	s8 txpwr_lmt;
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

enum rtw_phy_band_type {
	PHY_BAND_2G	= 0,
	PHY_BAND_5G	= 1,
};

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
}

void rtw_phy_dig_write(struct rtw_dev *rtwdev, u8 igi)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_hal *hal = &rtwdev->hal;
	u32 addr, mask;
	u8 path;

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

static void rtw_phy_statistics(struct rtw_dev *rtwdev)
{
	rtw_phy_stat_rssi(rtwdev);
	rtw_phy_stat_false_alarm(rtwdev);
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

static void rtw_phy_dig_get_boundary(struct rtw_dm_info *dm_info,
				     u8 *upper, u8 *lower, bool linked)
{
	u8 dig_max, dig_min, dig_mid;
	u8 min_rssi;

	if (linked) {
		dig_max = DIG_PERF_MAX;
		dig_mid = DIG_PERF_MID;
		/* 22B=0x1c, 22C=0x20 */
		dig_min = 0x1c;
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

	if (rtw_flag_check(rtwdev, RTW_FLAG_DIG_DISABLE))
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
	rtw_phy_dig_get_boundary(dm_info, &upper_bound, &lower_bound, linked);
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

void rtw_phy_dynamic_mechanism(struct rtw_dev *rtwdev)
{
	/* for further calculation */
	rtw_phy_statistics(rtwdev);
	rtw_phy_dig(rtwdev);
	rtw_phy_ra_info_update(rtwdev);
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

u32 rtw_phy_read_rf(struct rtw_dev *rtwdev, enum rtw_rf_path rf_path,
		    u32 addr, u32 mask)
{
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_chip_info *chip = rtwdev->chip;
	const u32 *base_addr = chip->rf_base_addr;
	u32 val, direct_addr;

	if (rf_path >= hal->rf_path_num) {
		rtw_err(rtwdev, "unsupported rf path (%d)\n", rf_path);
		return INV_RF_DATA;
	}

	addr &= 0xff;
	direct_addr = base_addr[rf_path] + (addr << 2);
	mask &= RFREG_MASK;

	val = rtw_read32_mask(rtwdev, direct_addr, mask);

	return val;
}

bool rtw_phy_write_rf_reg_sipi(struct rtw_dev *rtwdev, enum rtw_rf_path rf_path,
			       u32 addr, u32 mask, u32 data)
{
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_chip_info *chip = rtwdev->chip;
	u32 *sipi_addr = chip->rf_sipi_addr;
	u32 data_and_addr;
	u32 old_data = 0;
	u32 shift;

	if (rf_path >= hal->rf_path_num) {
		rtw_err(rtwdev, "unsupported rf path (%d)\n", rf_path);
		return false;
	}

	addr &= 0xff;
	mask &= RFREG_MASK;

	if (mask != RFREG_MASK) {
		old_data = rtw_phy_read_rf(rtwdev, rf_path, addr, RFREG_MASK);

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

bool rtw_phy_write_rf_reg(struct rtw_dev *rtwdev, enum rtw_rf_path rf_path,
			  u32 addr, u32 mask, u32 data)
{
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_chip_info *chip = rtwdev->chip;
	const u32 *base_addr = chip->rf_base_addr;
	u32 direct_addr;

	if (rf_path >= hal->rf_path_num) {
		rtw_err(rtwdev, "unsupported rf path (%d)\n", rf_path);
		return false;
	}

	addr &= 0xff;
	direct_addr = base_addr[rf_path] + (addr << 2);
	mask &= RFREG_MASK;

	if (addr == RF_CFGCH) {
		rtw_write32_mask(rtwdev, REG_RSV_CTRL, BITS_RFC_DIRECT, DISABLE_PI);
		rtw_write32_mask(rtwdev, REG_WLRF1, BITS_RFC_DIRECT, DISABLE_PI);
	}

	rtw_write32_mask(rtwdev, direct_addr, mask, data);

	udelay(1);

	if (addr == RF_CFGCH) {
		rtw_write32_mask(rtwdev, REG_RSV_CTRL, BITS_RFC_DIRECT, ENABLE_PI);
		rtw_write32_mask(rtwdev, REG_WLRF1, BITS_RFC_DIRECT, ENABLE_PI);
	}

	return true;
}

bool rtw_phy_write_rf_reg_mix(struct rtw_dev *rtwdev, enum rtw_rf_path rf_path,
			      u32 addr, u32 mask, u32 data)
{
	if (addr != 0x00)
		return rtw_phy_write_rf_reg(rtwdev, rf_path, addr, mask, data);

	return rtw_phy_write_rf_reg_sipi(rtwdev, rf_path, addr, mask, data);
}

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

void rtw_parse_tbl_bb_pg(struct rtw_dev *rtwdev, const struct rtw_table *tbl)
{
	const struct phy_pg_cfg_pair *p = tbl->data;
	const struct phy_pg_cfg_pair *end = p + tbl->size / 6;

	BUILD_BUG_ON(sizeof(struct phy_pg_cfg_pair) != sizeof(u32) * 6);

	for (; p < end; p++) {
		if (p->addr == 0xfe || p->addr == 0xffe) {
			msleep(50);
			continue;
		}
		phy_store_tx_power_by_rate(rtwdev, p->band, p->rf_path,
					   p->tx_num, p->addr, p->bitmask,
					   p->data);
	}
}

void rtw_parse_tbl_txpwr_lmt(struct rtw_dev *rtwdev,
			     const struct rtw_table *tbl)
{
	const struct txpwr_lmt_cfg_pair *p = tbl->data;
	const struct txpwr_lmt_cfg_pair *end = p + tbl->size / 6;

	BUILD_BUG_ON(sizeof(struct txpwr_lmt_cfg_pair) != sizeof(u8) * 6);

	for (; p < end; p++) {
		phy_set_tx_power_limit(rtwdev, p->regd, p->band,
				       p->bw, p->rs,
				       p->ch, p->txpwr_lmt);
	}
}

void rtw_phy_cfg_mac(struct rtw_dev *rtwdev, const struct rtw_table *tbl,
		     u32 addr, u32 data)
{
	rtw_write8(rtwdev, addr, data);
}

void rtw_phy_cfg_agc(struct rtw_dev *rtwdev, const struct rtw_table *tbl,
		     u32 addr, u32 data)
{
	rtw_write32(rtwdev, addr, data);
}

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

static void rtw_load_rfk_table(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;

	if (!chip->rfk_init_tbl)
		return;

	rtw_load_table(rtwdev, chip->rfk_init_tbl);
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

#define bcd_to_dec_pwr_by_rate(val, i) bcd2bin(val >> (i * 8))

#define RTW_MAX_POWER_INDEX		0x3F

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

static u8 rtw_cck_size = ARRAY_SIZE(rtw_cck_rates);
static u8 rtw_ofdm_size = ARRAY_SIZE(rtw_ofdm_rates);
static u8 rtw_ht_1s_size = ARRAY_SIZE(rtw_ht_1s_rates);
static u8 rtw_ht_2s_size = ARRAY_SIZE(rtw_ht_2s_rates);
static u8 rtw_vht_1s_size = ARRAY_SIZE(rtw_vht_1s_rates);
static u8 rtw_vht_2s_size = ARRAY_SIZE(rtw_vht_2s_rates);
u8 *rtw_rate_section[RTW_RATE_SECTION_MAX] = {
	rtw_cck_rates, rtw_ofdm_rates,
	rtw_ht_1s_rates, rtw_ht_2s_rates,
	rtw_vht_1s_rates, rtw_vht_2s_rates
};
u8 rtw_rate_size[RTW_RATE_SECTION_MAX] = {
	ARRAY_SIZE(rtw_cck_rates),
	ARRAY_SIZE(rtw_ofdm_rates),
	ARRAY_SIZE(rtw_ht_1s_rates),
	ARRAY_SIZE(rtw_ht_2s_rates),
	ARRAY_SIZE(rtw_vht_1s_rates),
	ARRAY_SIZE(rtw_vht_2s_rates)
};

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

static u8 rtw_get_channel_group(u8 channel)
{
	switch (channel) {
	default:
		WARN_ON(1);
		/* fall through */
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

static u8 phy_get_2g_tx_power_index(struct rtw_dev *rtwdev,
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
		/* fall through */
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

static u8 phy_get_5g_tx_power_index(struct rtw_dev *rtwdev,
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
		/* fall through */
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

/* set tx power level by path for each rates, note that the order of the rates
 * are *very* important, bacause 8822B/8821C combines every four bytes of tx
 * power index into a four-byte power index register, and calls set_tx_agc to
 * write these values into hardware
 */
static
void phy_set_tx_power_level_by_path(struct rtw_dev *rtwdev, u8 ch, u8 path)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u8 rs;

	/* do not need cck rates if we are not in 2.4G */
	if (hal->current_band_type == RTW_BAND_2G)
		rs = RTW_RATE_SECTION_CCK;
	else
		rs = RTW_RATE_SECTION_OFDM;

	for (; rs < RTW_RATE_SECTION_MAX; rs++)
		phy_set_tx_power_index_by_rs(rtwdev, ch, path, rs);
}

void rtw_phy_set_tx_power_level(struct rtw_dev *rtwdev, u8 channel)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_hal *hal = &rtwdev->hal;
	u8 path;

	mutex_lock(&hal->tx_power_mutex);

	for (path = 0; path < hal->rf_path_num; path++)
		phy_set_tx_power_level_by_path(rtwdev, channel, path);

	chip->ops->set_tx_power_index(rtwdev);
	mutex_unlock(&hal->tx_power_mutex);
}

s8 phy_get_tx_power_limit(struct rtw_dev *rtwdev, u8 band,
			  enum rtw_bandwidth bandwidth, u8 rf_path,
			  u8 rate, u8 channel, u8 regd);

static
u8 phy_get_tx_power_index(void *adapter, u8 rf_path, u8 rate,
			  enum rtw_bandwidth bandwidth, u8 channel, u8 regd)
{
	struct rtw_dev *rtwdev = adapter;
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_txpwr_idx *pwr_idx;
	u8 tx_power;
	u8 group;
	u8 band;
	s8 offset, limit;

	pwr_idx = &rtwdev->efuse.txpwr_idx_table[rf_path];
	group = rtw_get_channel_group(channel);

	/* base power index for 2.4G/5G */
	if (channel <= 14) {
		band = PHY_BAND_2G;
		tx_power = phy_get_2g_tx_power_index(rtwdev,
						     &pwr_idx->pwr_idx_2g,
						     bandwidth, rate, group);
		offset = hal->tx_pwr_by_rate_offset_2g[rf_path][rate];
	} else {
		band = PHY_BAND_5G;
		tx_power = phy_get_5g_tx_power_index(rtwdev,
						     &pwr_idx->pwr_idx_5g,
						     bandwidth, rate, group);
		offset = hal->tx_pwr_by_rate_offset_5g[rf_path][rate];
	}

	limit = phy_get_tx_power_limit(rtwdev, band, bandwidth, rf_path,
				       rate, channel, regd);

	if (offset > limit)
		offset = limit;

	tx_power += offset;

	if (tx_power > rtwdev->chip->max_power_index)
		tx_power = rtwdev->chip->max_power_index;

	return tx_power;
}

void phy_set_tx_power_index_by_rs(void *adapter, u8 ch, u8 path, u8 rs)
{
	struct rtw_dev *rtwdev = adapter;
	struct rtw_hal *hal = &rtwdev->hal;
	u8 regd = rtwdev->regd.txpwr_regd;
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
		pwr_idx = phy_get_tx_power_index(adapter, path, rate, bw, ch,
						 regd);
		hal->tx_pwr_tbl[path][rate] = pwr_idx;
	}
}

static u8 tbl_to_dec_pwr_by_rate(struct rtw_dev *rtwdev, u32 hex, u8 i)
{
	if (rtwdev->chip->is_pwr_by_rate_dec)
		return bcd_to_dec_pwr_by_rate(hex, i);
	else
		return (hex >> (i * 8)) & 0xFF;
}

static void phy_get_rate_values_of_txpwr_by_rate(struct rtw_dev *rtwdev,
						 u32 addr, u32 mask,
						 u32 val, u8 *rate,
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

void phy_store_tx_power_by_rate(void *adapter, u32 band, u32 rfpath, u32 txnum,
				u32 regaddr, u32 bitmask, u32 data)
{
	struct rtw_dev *rtwdev = adapter;
	struct rtw_hal *hal = &rtwdev->hal;
	u8 rate_num = 0;
	u8 rate;
	u8 rates[RTW_RF_PATH_MAX] = {0};
	s8 offset;
	s8 pwr_by_rate[RTW_RF_PATH_MAX] = {0};
	int i;

	phy_get_rate_values_of_txpwr_by_rate(rtwdev, regaddr, bitmask, data,
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

static
void phy_tx_power_by_rate_config_by_path(struct rtw_hal *hal, u8 path,
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
		phy_tx_power_by_rate_config_by_path(hal, path,
				RTW_RATE_SECTION_CCK,
				rtw_cck_size, rtw_cck_rates);
		phy_tx_power_by_rate_config_by_path(hal, path,
				RTW_RATE_SECTION_OFDM,
				rtw_ofdm_size, rtw_ofdm_rates);
		phy_tx_power_by_rate_config_by_path(hal, path,
				RTW_RATE_SECTION_HT_1S,
				rtw_ht_1s_size, rtw_ht_1s_rates);
		phy_tx_power_by_rate_config_by_path(hal, path,
				RTW_RATE_SECTION_HT_2S,
				rtw_ht_2s_size, rtw_ht_2s_rates);
		phy_tx_power_by_rate_config_by_path(hal, path,
				RTW_RATE_SECTION_VHT_1S,
				rtw_vht_1s_size, rtw_vht_1s_rates);
		phy_tx_power_by_rate_config_by_path(hal, path,
				RTW_RATE_SECTION_VHT_2S,
				rtw_vht_2s_size, rtw_vht_2s_rates);
	}
}

static void
phy_tx_power_limit_config(struct rtw_hal *hal, u8 regd, u8 bw, u8 rs)
{
	s8 base, orig;
	u8 ch;

	for (ch = 0; ch < RTW_MAX_CHANNEL_NUM_2G; ch++) {
		base = hal->tx_pwr_by_rate_base_2g[0][rs];
		orig = hal->tx_pwr_limit_2g[regd][bw][rs][ch];
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

	for (regd = 0; regd < RTW_REGD_MAX; regd++)
		for (bw = 0; bw < RTW_CHANNEL_WIDTH_MAX; bw++)
			for (rs = 0; rs < RTW_RATE_SECTION_MAX; rs++)
				phy_tx_power_limit_config(hal, regd, bw, rs);
}

static s8 get_tx_power_limit(struct rtw_hal *hal, u8 bw, u8 rs, u8 ch, u8 regd)
{
	if (regd > RTW_REGD_WW)
		return RTW_MAX_POWER_INDEX;

	return hal->tx_pwr_limit_2g[regd][bw][rs][ch];
}

s8 phy_get_tx_power_limit(struct rtw_dev *rtwdev, u8 band,
			  enum rtw_bandwidth bw, u8 rf_path,
			  u8 rate, u8 channel, u8 regd)
{
	struct rtw_hal *hal = &rtwdev->hal;
	s8 power_limit;
	u8 rs;
	int ch_idx;

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

	ch_idx = rtw_channel_to_idx(band, channel);
	if (ch_idx < 0)
		goto err;

	power_limit = get_tx_power_limit(hal, bw, rs, ch_idx, regd);

	return power_limit;

err:
	WARN(1, "invalid arguments, band=%d, bw=%d, path=%d, rate=%d, ch=%d\n",
	     band, bw, rf_path, rate, channel);
	return RTW_MAX_POWER_INDEX;
}

void phy_set_tx_power_limit(struct rtw_dev *rtwdev, u8 regd, u8 band,
			    u8 bw, u8 rs, u8 ch, s8 pwr_limit)
{
	struct rtw_hal *hal = &rtwdev->hal;
	int ch_idx;

	pwr_limit = clamp_t(s8, pwr_limit,
			    -RTW_MAX_POWER_INDEX, RTW_MAX_POWER_INDEX);
	ch_idx = rtw_channel_to_idx(band, ch);

	if (regd >= RTW_REGD_MAX || bw >= RTW_CHANNEL_WIDTH_MAX ||
	    rs >= RTW_RATE_SECTION_MAX || ch_idx < 0) {
		WARN(1,
		     "wrong txpwr_lmt regd=%u, band=%u bw=%u, rs=%u, ch_idx=%u, pwr_limit=%d\n",
		     regd, band, bw, rs, ch_idx, pwr_limit);
		return;
	}

	if (band == PHY_BAND_2G)
		hal->tx_pwr_limit_2g[regd][bw][rs][ch_idx] = pwr_limit;
	else if (band == PHY_BAND_5G)
		hal->tx_pwr_limit_5g[regd][bw][rs][ch_idx] = pwr_limit;
}

static
void rtw_hw_tx_power_limit_init(struct rtw_hal *hal, u8 regd, u8 bw, u8 rs)
{
	u8 ch;

	/* 2.4G channels */
	for (ch = 0; ch < RTW_MAX_CHANNEL_NUM_2G; ch++)
		hal->tx_pwr_limit_2g[regd][bw][rs][ch] = RTW_MAX_POWER_INDEX;

	/* 5G channels */
	for (ch = 0; ch < RTW_MAX_CHANNEL_NUM_5G; ch++)
		hal->tx_pwr_limit_5g[regd][bw][rs][ch] = RTW_MAX_POWER_INDEX;
}

void rtw_hw_init_tx_power(struct rtw_hal *hal)
{
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
				rtw_hw_tx_power_limit_init(hal, regd, bw, rs);
}
