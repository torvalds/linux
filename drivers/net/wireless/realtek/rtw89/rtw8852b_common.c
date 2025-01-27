// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2024  Realtek Corporation
 */

#include "coex.h"
#include "debug.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"
#include "rtw8852b_common.h"
#include "util.h"

static const struct rtw89_reg3_def rtw8852bx_pmac_ht20_mcs7_tbl[] = {
	{0x4580, 0x0000ffff, 0x0},
	{0x4580, 0xffff0000, 0x0},
	{0x4584, 0x0000ffff, 0x0},
	{0x4584, 0xffff0000, 0x0},
	{0x4580, 0x0000ffff, 0x1},
	{0x4578, 0x00ffffff, 0x2018b},
	{0x4570, 0x03ffffff, 0x7},
	{0x4574, 0x03ffffff, 0x32407},
	{0x45b8, 0x00000010, 0x0},
	{0x45b8, 0x00000100, 0x0},
	{0x45b8, 0x00000080, 0x0},
	{0x45b8, 0x00000008, 0x0},
	{0x45a0, 0x0000ff00, 0x0},
	{0x45a0, 0xff000000, 0x1},
	{0x45a4, 0x0000ff00, 0x2},
	{0x45a4, 0xff000000, 0x3},
	{0x45b8, 0x00000020, 0x0},
	{0x4568, 0xe0000000, 0x0},
	{0x45b8, 0x00000002, 0x1},
	{0x456c, 0xe0000000, 0x0},
	{0x45b4, 0x00006000, 0x0},
	{0x45b4, 0x00001800, 0x1},
	{0x45b8, 0x00000040, 0x0},
	{0x45b8, 0x00000004, 0x0},
	{0x45b8, 0x00000200, 0x0},
	{0x4598, 0xf8000000, 0x0},
	{0x45b8, 0x00100000, 0x0},
	{0x45a8, 0x00000fc0, 0x0},
	{0x45b8, 0x00200000, 0x0},
	{0x45b0, 0x00000038, 0x0},
	{0x45b0, 0x000001c0, 0x0},
	{0x45a0, 0x000000ff, 0x0},
	{0x45b8, 0x00400000, 0x0},
	{0x4590, 0x000007ff, 0x0},
	{0x45b0, 0x00000e00, 0x0},
	{0x45ac, 0x0000001f, 0x0},
	{0x45b8, 0x00800000, 0x0},
	{0x45a8, 0x0003f000, 0x0},
	{0x45b8, 0x01000000, 0x0},
	{0x45b0, 0x00007000, 0x0},
	{0x45b0, 0x00038000, 0x0},
	{0x45a0, 0x00ff0000, 0x0},
	{0x45b8, 0x02000000, 0x0},
	{0x4590, 0x003ff800, 0x0},
	{0x45b0, 0x001c0000, 0x0},
	{0x45ac, 0x000003e0, 0x0},
	{0x45b8, 0x04000000, 0x0},
	{0x45a8, 0x00fc0000, 0x0},
	{0x45b8, 0x08000000, 0x0},
	{0x45b0, 0x00e00000, 0x0},
	{0x45b0, 0x07000000, 0x0},
	{0x45a4, 0x000000ff, 0x0},
	{0x45b8, 0x10000000, 0x0},
	{0x4594, 0x000007ff, 0x0},
	{0x45b0, 0x38000000, 0x0},
	{0x45ac, 0x00007c00, 0x0},
	{0x45b8, 0x20000000, 0x0},
	{0x45a8, 0x3f000000, 0x0},
	{0x45b8, 0x40000000, 0x0},
	{0x45b4, 0x00000007, 0x0},
	{0x45b4, 0x00000038, 0x0},
	{0x45a4, 0x00ff0000, 0x0},
	{0x45b8, 0x80000000, 0x0},
	{0x4594, 0x003ff800, 0x0},
	{0x45b4, 0x000001c0, 0x0},
	{0x4598, 0xf8000000, 0x0},
	{0x45b8, 0x00100000, 0x0},
	{0x45a8, 0x00000fc0, 0x7},
	{0x45b8, 0x00200000, 0x0},
	{0x45b0, 0x00000038, 0x0},
	{0x45b0, 0x000001c0, 0x0},
	{0x45a0, 0x000000ff, 0x0},
	{0x45b4, 0x06000000, 0x0},
	{0x45b0, 0x00000007, 0x0},
	{0x45b8, 0x00080000, 0x0},
	{0x45a8, 0x0000003f, 0x0},
	{0x457c, 0xffe00000, 0x1},
	{0x4530, 0xffffffff, 0x0},
	{0x4588, 0x00003fff, 0x0},
	{0x4598, 0x000001ff, 0x0},
	{0x4534, 0xffffffff, 0x0},
	{0x4538, 0xffffffff, 0x0},
	{0x453c, 0xffffffff, 0x0},
	{0x4588, 0x0fffc000, 0x0},
	{0x4598, 0x0003fe00, 0x0},
	{0x4540, 0xffffffff, 0x0},
	{0x4544, 0xffffffff, 0x0},
	{0x4548, 0xffffffff, 0x0},
	{0x458c, 0x00003fff, 0x0},
	{0x4598, 0x07fc0000, 0x0},
	{0x454c, 0xffffffff, 0x0},
	{0x4550, 0xffffffff, 0x0},
	{0x4554, 0xffffffff, 0x0},
	{0x458c, 0x0fffc000, 0x0},
	{0x459c, 0x000001ff, 0x0},
	{0x4558, 0xffffffff, 0x0},
	{0x455c, 0xffffffff, 0x0},
	{0x4530, 0xffffffff, 0x4e790001},
	{0x4588, 0x00003fff, 0x0},
	{0x4598, 0x000001ff, 0x1},
	{0x4534, 0xffffffff, 0x0},
	{0x4538, 0xffffffff, 0x4b},
	{0x45ac, 0x38000000, 0x7},
	{0x4588, 0xf0000000, 0x0},
	{0x459c, 0x7e000000, 0x0},
	{0x45b8, 0x00040000, 0x0},
	{0x45b8, 0x00020000, 0x0},
	{0x4590, 0xffc00000, 0x0},
	{0x45b8, 0x00004000, 0x0},
	{0x4578, 0xff000000, 0x0},
	{0x45b8, 0x00000400, 0x0},
	{0x45b8, 0x00000800, 0x0},
	{0x45b8, 0x00001000, 0x0},
	{0x45b8, 0x00002000, 0x0},
	{0x45b4, 0x00018000, 0x0},
	{0x45ac, 0x07800000, 0x0},
	{0x45b4, 0x00000600, 0x2},
	{0x459c, 0x0001fe00, 0x80},
	{0x45ac, 0x00078000, 0x3},
	{0x459c, 0x01fe0000, 0x1},
};

static const struct rtw89_reg3_def rtw8852bx_btc_preagc_en_defs[] = {
	{0x46D0, GENMASK(1, 0), 0x3},
	{0x4790, GENMASK(1, 0), 0x3},
	{0x4AD4, GENMASK(31, 0), 0xf},
	{0x4AE0, GENMASK(31, 0), 0xf},
	{0x4688, GENMASK(31, 24), 0x80},
	{0x476C, GENMASK(31, 24), 0x80},
	{0x4694, GENMASK(7, 0), 0x80},
	{0x4694, GENMASK(15, 8), 0x80},
	{0x4778, GENMASK(7, 0), 0x80},
	{0x4778, GENMASK(15, 8), 0x80},
	{0x4AE4, GENMASK(23, 0), 0x780D1E},
	{0x4AEC, GENMASK(23, 0), 0x780D1E},
	{0x469C, GENMASK(31, 26), 0x34},
	{0x49F0, GENMASK(31, 26), 0x34},
};

static DECLARE_PHY_REG3_TBL(rtw8852bx_btc_preagc_en_defs);

static const struct rtw89_reg3_def rtw8852bx_btc_preagc_dis_defs[] = {
	{0x46D0, GENMASK(1, 0), 0x0},
	{0x4790, GENMASK(1, 0), 0x0},
	{0x4AD4, GENMASK(31, 0), 0x60},
	{0x4AE0, GENMASK(31, 0), 0x60},
	{0x4688, GENMASK(31, 24), 0x1a},
	{0x476C, GENMASK(31, 24), 0x1a},
	{0x4694, GENMASK(7, 0), 0x2a},
	{0x4694, GENMASK(15, 8), 0x2a},
	{0x4778, GENMASK(7, 0), 0x2a},
	{0x4778, GENMASK(15, 8), 0x2a},
	{0x4AE4, GENMASK(23, 0), 0x79E99E},
	{0x4AEC, GENMASK(23, 0), 0x79E99E},
	{0x469C, GENMASK(31, 26), 0x26},
	{0x49F0, GENMASK(31, 26), 0x26},
};

static DECLARE_PHY_REG3_TBL(rtw8852bx_btc_preagc_dis_defs);

static void rtw8852be_efuse_parsing(struct rtw89_efuse *efuse,
				    struct rtw8852bx_efuse *map)
{
	ether_addr_copy(efuse->addr, map->e.mac_addr);
	efuse->rfe_type = map->rfe_type;
	efuse->xtal_cap = map->xtal_k;
}

static void rtw8852bx_efuse_parsing_tssi(struct rtw89_dev *rtwdev,
					 struct rtw8852bx_efuse *map)
{
	struct rtw89_tssi_info *tssi = &rtwdev->tssi;
	struct rtw8852bx_tssi_offset *ofst[] = {&map->path_a_tssi, &map->path_b_tssi};
	u8 i, j;

	tssi->thermal[RF_PATH_A] = map->path_a_therm;
	tssi->thermal[RF_PATH_B] = map->path_b_therm;

	for (i = 0; i < RF_PATH_NUM_8852BX; i++) {
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

static void rtw8852bx_efuse_parsing_gain_offset(struct rtw89_dev *rtwdev,
						struct rtw8852bx_efuse *map)
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

static int __rtw8852bx_read_efuse(struct rtw89_dev *rtwdev, u8 *log_map,
				  enum rtw89_efuse_block block)
{
	struct rtw89_efuse *efuse = &rtwdev->efuse;
	struct rtw8852bx_efuse *map;

	map = (struct rtw8852bx_efuse *)log_map;

	efuse->country_code[0] = map->country_code[0];
	efuse->country_code[1] = map->country_code[1];
	rtw8852bx_efuse_parsing_tssi(rtwdev, map);
	rtw8852bx_efuse_parsing_gain_offset(rtwdev, map);

	switch (rtwdev->hci.type) {
	case RTW89_HCI_TYPE_PCIE:
		rtw8852be_efuse_parsing(efuse, map);
		break;
	default:
		return -EOPNOTSUPP;
	}

	rtw89_info(rtwdev, "chip rfe_type is %d\n", efuse->rfe_type);

	return 0;
}

static void rtw8852bx_phycap_parsing_power_cal(struct rtw89_dev *rtwdev, u8 *phycap_map)
{
#define PWR_K_CHK_OFFSET 0x5E9
#define PWR_K_CHK_VALUE 0xAA
	u32 offset = PWR_K_CHK_OFFSET - rtwdev->chip->phycap_addr;

	if (phycap_map[offset] == PWR_K_CHK_VALUE)
		rtwdev->efuse.power_k_valid = true;
}

static void rtw8852bx_phycap_parsing_tssi(struct rtw89_dev *rtwdev, u8 *phycap_map)
{
	struct rtw89_tssi_info *tssi = &rtwdev->tssi;
	static const u32 tssi_trim_addr[RF_PATH_NUM_8852BX] = {0x5D6, 0x5AB};
	u32 addr = rtwdev->chip->phycap_addr;
	bool pg = false;
	u32 ofst;
	u8 i, j;

	for (i = 0; i < RF_PATH_NUM_8852BX; i++) {
		for (j = 0; j < TSSI_TRIM_CH_GROUP_NUM; j++) {
			/* addrs are in decreasing order */
			ofst = tssi_trim_addr[i] - addr - j;
			tssi->tssi_trim[i][j] = phycap_map[ofst];

			if (phycap_map[ofst] != 0xff)
				pg = true;
		}
	}

	if (!pg) {
		memset(tssi->tssi_trim, 0, sizeof(tssi->tssi_trim));
		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM] no PG, set all trim info to 0\n");
	}

	for (i = 0; i < RF_PATH_NUM_8852BX; i++)
		for (j = 0; j < TSSI_TRIM_CH_GROUP_NUM; j++)
			rtw89_debug(rtwdev, RTW89_DBG_TSSI,
				    "[TSSI] path=%d idx=%d trim=0x%x addr=0x%x\n",
				    i, j, tssi->tssi_trim[i][j],
				    tssi_trim_addr[i] - j);
}

static void rtw8852bx_phycap_parsing_thermal_trim(struct rtw89_dev *rtwdev,
						  u8 *phycap_map)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	static const u32 thm_trim_addr[RF_PATH_NUM_8852BX] = {0x5DF, 0x5DC};
	u32 addr = rtwdev->chip->phycap_addr;
	u8 i;

	for (i = 0; i < RF_PATH_NUM_8852BX; i++) {
		info->thermal_trim[i] = phycap_map[thm_trim_addr[i] - addr];

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[THERMAL][TRIM] path=%d thermal_trim=0x%x\n",
			    i, info->thermal_trim[i]);

		if (info->thermal_trim[i] != 0xff)
			info->pg_thermal_trim = true;
	}
}

static void rtw8852bx_thermal_trim(struct rtw89_dev *rtwdev)
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

	for (i = 0; i < RF_PATH_NUM_8852BX; i++) {
		val = __thm_setting(info->thermal_trim[i]);
		rtw89_write_rf(rtwdev, i, RR_TM2, RR_TM2_OFF, val);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[THERMAL][TRIM] path=%d thermal_setting=0x%x\n",
			    i, val);
	}
#undef __thm_setting
}

static void rtw8852bx_phycap_parsing_pa_bias_trim(struct rtw89_dev *rtwdev,
						  u8 *phycap_map)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	static const u32 pabias_trim_addr[RF_PATH_NUM_8852BX] = {0x5DE, 0x5DB};
	u32 addr = rtwdev->chip->phycap_addr;
	u8 i;

	for (i = 0; i < RF_PATH_NUM_8852BX; i++) {
		info->pa_bias_trim[i] = phycap_map[pabias_trim_addr[i] - addr];

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PA_BIAS][TRIM] path=%d pa_bias_trim=0x%x\n",
			    i, info->pa_bias_trim[i]);

		if (info->pa_bias_trim[i] != 0xff)
			info->pg_pa_bias_trim = true;
	}
}

static void rtw8852bx_pa_bias_trim(struct rtw89_dev *rtwdev)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	u8 pabias_2g, pabias_5g;
	u8 i;

	if (!info->pg_pa_bias_trim) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PA_BIAS][TRIM] no PG, do nothing\n");

		return;
	}

	for (i = 0; i < RF_PATH_NUM_8852BX; i++) {
		pabias_2g = FIELD_GET(GENMASK(3, 0), info->pa_bias_trim[i]);
		pabias_5g = FIELD_GET(GENMASK(7, 4), info->pa_bias_trim[i]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PA_BIAS][TRIM] path=%d 2G=0x%x 5G=0x%x\n",
			    i, pabias_2g, pabias_5g);

		rtw89_write_rf(rtwdev, i, RR_BIASA, RR_BIASA_TXG, pabias_2g);
		rtw89_write_rf(rtwdev, i, RR_BIASA, RR_BIASA_TXA, pabias_5g);
	}
}

static void rtw8852bx_phycap_parsing_gain_comp(struct rtw89_dev *rtwdev, u8 *phycap_map)
{
	static const u32 comp_addrs[][RTW89_SUBBAND_2GHZ_5GHZ_NR] = {
		{0x5BB, 0x5BA, 0, 0x5B9, 0x5B8},
		{0x590, 0x58F, 0, 0x58E, 0x58D},
	};
	struct rtw89_phy_efuse_gain *gain = &rtwdev->efuse_gain;
	u32 phycap_addr = rtwdev->chip->phycap_addr;
	bool valid = false;
	int path, i;
	u8 data;

	for (path = 0; path < 2; path++)
		for (i = 0; i < RTW89_SUBBAND_2GHZ_5GHZ_NR; i++) {
			if (comp_addrs[path][i] == 0)
				continue;

			data = phycap_map[comp_addrs[path][i] - phycap_addr];
			valid |= _decode_efuse_gain(data, NULL,
						    &gain->comp[path][i]);
		}

	gain->comp_valid = valid;
}

static int __rtw8852bx_read_phycap(struct rtw89_dev *rtwdev, u8 *phycap_map)
{
	rtw8852bx_phycap_parsing_power_cal(rtwdev, phycap_map);
	rtw8852bx_phycap_parsing_tssi(rtwdev, phycap_map);
	rtw8852bx_phycap_parsing_thermal_trim(rtwdev, phycap_map);
	rtw8852bx_phycap_parsing_pa_bias_trim(rtwdev, phycap_map);
	rtw8852bx_phycap_parsing_gain_comp(rtwdev, phycap_map);

	return 0;
}

static void __rtw8852bx_power_trim(struct rtw89_dev *rtwdev)
{
	rtw8852bx_thermal_trim(rtwdev);
	rtw8852bx_pa_bias_trim(rtwdev);
}

static void __rtw8852bx_set_channel_mac(struct rtw89_dev *rtwdev,
					const struct rtw89_chan *chan,
					u8 mac_idx)
{
	u32 rf_mod = rtw89_mac_reg_by_idx(rtwdev, R_AX_WMAC_RFMOD, mac_idx);
	u32 sub_carr = rtw89_mac_reg_by_idx(rtwdev, R_AX_TX_SUB_CARRIER_VALUE, mac_idx);
	u32 chk_rate = rtw89_mac_reg_by_idx(rtwdev, R_AX_TXRATE_CHK, mac_idx);
	u8 txsc20 = 0, txsc40 = 0;

	switch (chan->band_width) {
	case RTW89_CHANNEL_WIDTH_80:
		txsc40 = rtw89_phy_get_txsc(rtwdev, chan, RTW89_CHANNEL_WIDTH_40);
		fallthrough;
	case RTW89_CHANNEL_WIDTH_40:
		txsc20 = rtw89_phy_get_txsc(rtwdev, chan, RTW89_CHANNEL_WIDTH_20);
		break;
	default:
		break;
	}

	switch (chan->band_width) {
	case RTW89_CHANNEL_WIDTH_80:
		rtw89_write8_mask(rtwdev, rf_mod, B_AX_WMAC_RFMOD_MASK, BIT(1));
		rtw89_write32(rtwdev, sub_carr, txsc20 | (txsc40 << 4));
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rtw89_write8_mask(rtwdev, rf_mod, B_AX_WMAC_RFMOD_MASK, BIT(0));
		rtw89_write32(rtwdev, sub_carr, txsc20);
		break;
	case RTW89_CHANNEL_WIDTH_20:
		rtw89_write8_clr(rtwdev, rf_mod, B_AX_WMAC_RFMOD_MASK);
		rtw89_write32(rtwdev, sub_carr, 0);
		break;
	default:
		break;
	}

	if (chan->channel > 14) {
		rtw89_write8_clr(rtwdev, chk_rate, B_AX_BAND_MODE);
		rtw89_write8_set(rtwdev, chk_rate,
				 B_AX_CHECK_CCK_EN | B_AX_RTS_LIMIT_IN_OFDM6);
	} else {
		rtw89_write8_set(rtwdev, chk_rate, B_AX_BAND_MODE);
		rtw89_write8_clr(rtwdev, chk_rate,
				 B_AX_CHECK_CCK_EN | B_AX_RTS_LIMIT_IN_OFDM6);
	}
}

static const u32 rtw8852bx_sco_barker_threshold[14] = {
	0x1cfea, 0x1d0e1, 0x1d1d7, 0x1d2cd, 0x1d3c3, 0x1d4b9, 0x1d5b0, 0x1d6a6,
	0x1d79c, 0x1d892, 0x1d988, 0x1da7f, 0x1db75, 0x1ddc4
};

static const u32 rtw8852bx_sco_cck_threshold[14] = {
	0x27de3, 0x27f35, 0x28088, 0x281da, 0x2832d, 0x2847f, 0x285d2, 0x28724,
	0x28877, 0x289c9, 0x28b1c, 0x28c6e, 0x28dc1, 0x290ed
};

static void rtw8852bx_ctrl_sco_cck(struct rtw89_dev *rtwdev, u8 primary_ch)
{
	u8 ch_element = primary_ch - 1;

	rtw89_phy_write32_mask(rtwdev, R_RXSCOBC, B_RXSCOBC_TH,
			       rtw8852bx_sco_barker_threshold[ch_element]);
	rtw89_phy_write32_mask(rtwdev, R_RXSCOCCK, B_RXSCOCCK_TH,
			       rtw8852bx_sco_cck_threshold[ch_element]);
}

static u8 rtw8852bx_sco_mapping(u8 central_ch)
{
	if (central_ch == 1)
		return 109;
	else if (central_ch >= 2 && central_ch <= 6)
		return 108;
	else if (central_ch >= 7 && central_ch <= 10)
		return 107;
	else if (central_ch >= 11 && central_ch <= 14)
		return 106;
	else if (central_ch == 36 || central_ch == 38)
		return 51;
	else if (central_ch >= 40 && central_ch <= 58)
		return 50;
	else if (central_ch >= 60 && central_ch <= 64)
		return 49;
	else if (central_ch == 100 || central_ch == 102)
		return 48;
	else if (central_ch >= 104 && central_ch <= 126)
		return 47;
	else if (central_ch >= 128 && central_ch <= 151)
		return 46;
	else if (central_ch >= 153 && central_ch <= 177)
		return 45;
	else
		return 0;
}

struct rtw8852bx_bb_gain {
	u32 gain_g[BB_PATH_NUM_8852BX];
	u32 gain_a[BB_PATH_NUM_8852BX];
	u32 gain_mask;
};

static const struct rtw8852bx_bb_gain bb_gain_lna[LNA_GAIN_NUM] = {
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

static const struct rtw8852bx_bb_gain bb_gain_tia[TIA_GAIN_NUM] = {
	{ .gain_g = {0x4680, 0x4764}, .gain_a = {0x4664, 0x4748},
	  .gain_mask = 0x00ff0000 },
	{ .gain_g = {0x4680, 0x4764}, .gain_a = {0x4664, 0x4748},
	  .gain_mask = 0xff000000 },
};

static void rtw8852bx_set_gain_error(struct rtw89_dev *rtwdev,
				     enum rtw89_subband subband,
				     enum rtw89_rf_path path)
{
	const struct rtw89_phy_bb_gain_info *gain = &rtwdev->bb_gain.ax;
	u8 gain_band = rtw89_subband_to_bb_gain_band(subband);
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

static void rtw8852bt_ext_loss_avg_update(struct rtw89_dev *rtwdev,
					  s8 ext_loss_a, s8 ext_loss_b)
{
	s8 ext_loss_avg;
	u64 linear;
	u8 pwrofst;

	if (ext_loss_a == ext_loss_b) {
		ext_loss_avg = ext_loss_a;
	} else {
		linear = rtw89_db_2_linear(abs(ext_loss_a - ext_loss_b)) + 1;
		linear = DIV_ROUND_CLOSEST_ULL(linear / 2, 1 << RTW89_LINEAR_FRAC_BITS);
		ext_loss_avg = rtw89_linear_2_db(linear);
		ext_loss_avg += min(ext_loss_a, ext_loss_b);
	}

	pwrofst = max(DIV_ROUND_CLOSEST(ext_loss_avg, 4) + 16, EDCCA_PWROFST_DEFAULT);

	rtw89_phy_write32_mask(rtwdev, R_PWOFST, B_PWOFST, pwrofst);
}

static void rtw8852bx_set_gain_offset(struct rtw89_dev *rtwdev,
				      enum rtw89_subband subband,
				      enum rtw89_phy_idx phy_idx)
{
	static const u32 gain_err_addr[2] = {R_P0_AGC_RSVD, R_P1_AGC_RSVD};
	static const u32 rssi_ofst_addr[2] = {R_PATH0_G_TIA1_LNA6_OP1DB_V1,
					      R_PATH1_G_TIA1_LNA6_OP1DB_V1};
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_phy_efuse_gain *efuse_gain = &rtwdev->efuse_gain;
	enum rtw89_gain_offset gain_ofdm_band;
	s8 ext_loss_a = 0, ext_loss_b = 0;
	s32 offset_a, offset_b;
	s32 offset_ofdm, offset_cck;
	s32 tmp;
	u8 path;

	if (!efuse_gain->comp_valid)
		goto next;

	for (path = RF_PATH_A; path < BB_PATH_NUM_8852BX; path++) {
		tmp = efuse_gain->comp[path][subband];
		tmp = clamp_t(s32, tmp << 2, S8_MIN, S8_MAX);
		rtw89_phy_write32_mask(rtwdev, gain_err_addr[path], MASKBYTE0, tmp);
	}

next:
	if (!efuse_gain->offset_valid)
		goto ext_loss;

	gain_ofdm_band = rtw89_subband_to_gain_offset_band_of_ofdm(subband);

	offset_a = -efuse_gain->offset[RF_PATH_A][gain_ofdm_band];
	offset_b = -efuse_gain->offset[RF_PATH_B][gain_ofdm_band];

	tmp = -((offset_a << 2) + (efuse_gain->offset_base[RTW89_PHY_0] >> 2));
	tmp = clamp_t(s32, tmp, S8_MIN, S8_MAX);
	rtw89_phy_write32_mask(rtwdev, rssi_ofst_addr[RF_PATH_A], B_PATH0_R_G_OFST_MASK, tmp);

	tmp = -((offset_b << 2) + (efuse_gain->offset_base[RTW89_PHY_0] >> 2));
	tmp = clamp_t(s32, tmp, S8_MIN, S8_MAX);
	rtw89_phy_write32_mask(rtwdev, rssi_ofst_addr[RF_PATH_B], B_PATH0_R_G_OFST_MASK, tmp);

	if (hal->antenna_rx == RF_B) {
		offset_ofdm = -efuse_gain->offset[RF_PATH_B][gain_ofdm_band];
		offset_cck = -efuse_gain->offset[RF_PATH_B][0];
	} else {
		offset_ofdm = -efuse_gain->offset[RF_PATH_A][gain_ofdm_band];
		offset_cck = -efuse_gain->offset[RF_PATH_A][0];
	}

	tmp = (offset_ofdm << 4) + efuse_gain->offset_base[RTW89_PHY_0];
	tmp = clamp_t(s32, tmp, S8_MIN, S8_MAX);
	rtw89_phy_write32_idx(rtwdev, R_P0_RPL1, B_P0_RPL1_BIAS_MASK, tmp, phy_idx);

	tmp = (offset_ofdm << 4) + efuse_gain->rssi_base[RTW89_PHY_0];
	tmp = clamp_t(s32, tmp, S8_MIN, S8_MAX);
	rtw89_phy_write32_idx(rtwdev, R_P1_RPL1, B_P0_RPL1_BIAS_MASK, tmp, phy_idx);

	if (subband == RTW89_CH_2G) {
		tmp = (offset_cck << 3) + (efuse_gain->offset_base[RTW89_PHY_0] >> 1);
		tmp = clamp_t(s32, tmp, S8_MIN >> 1, S8_MAX >> 1);
		rtw89_phy_write32_mask(rtwdev, R_RX_RPL_OFST,
				       B_RX_RPL_OFST_CCK_MASK, tmp);
	}

	ext_loss_a = (offset_a << 2) + (efuse_gain->offset_base[RTW89_PHY_0] >> 2);
	ext_loss_b = (offset_b << 2) + (efuse_gain->offset_base[RTW89_PHY_0] >> 2);

ext_loss:
	if (rtwdev->chip->chip_id == RTL8852BT)
		rtw8852bt_ext_loss_avg_update(rtwdev, ext_loss_a, ext_loss_b);
}

static
void rtw8852bx_set_rxsc_rpl_comp(struct rtw89_dev *rtwdev, enum rtw89_subband subband)
{
	const struct rtw89_phy_bb_gain_info *gain = &rtwdev->bb_gain.ax;
	u8 band = rtw89_subband_to_bb_gain_band(subband);
	u32 val;

	val = u32_encode_bits((gain->rpl_ofst_20[band][RF_PATH_A] +
			       gain->rpl_ofst_20[band][RF_PATH_B]) >> 1, B_P0_RPL1_20_MASK) |
	      u32_encode_bits((gain->rpl_ofst_40[band][RF_PATH_A][0] +
			       gain->rpl_ofst_40[band][RF_PATH_B][0]) >> 1, B_P0_RPL1_40_MASK) |
	      u32_encode_bits((gain->rpl_ofst_40[band][RF_PATH_A][1] +
			       gain->rpl_ofst_40[band][RF_PATH_B][1]) >> 1, B_P0_RPL1_41_MASK);
	val >>= B_P0_RPL1_SHIFT;
	rtw89_phy_write32_mask(rtwdev, R_P0_RPL1, B_P0_RPL1_MASK, val);
	rtw89_phy_write32_mask(rtwdev, R_P1_RPL1, B_P0_RPL1_MASK, val);

	val = u32_encode_bits((gain->rpl_ofst_40[band][RF_PATH_A][2] +
			       gain->rpl_ofst_40[band][RF_PATH_B][2]) >> 1, B_P0_RTL2_42_MASK) |
	      u32_encode_bits((gain->rpl_ofst_80[band][RF_PATH_A][0] +
			       gain->rpl_ofst_80[band][RF_PATH_B][0]) >> 1, B_P0_RTL2_80_MASK) |
	      u32_encode_bits((gain->rpl_ofst_80[band][RF_PATH_A][1] +
			       gain->rpl_ofst_80[band][RF_PATH_B][1]) >> 1, B_P0_RTL2_81_MASK) |
	      u32_encode_bits((gain->rpl_ofst_80[band][RF_PATH_A][10] +
			       gain->rpl_ofst_80[band][RF_PATH_B][10]) >> 1, B_P0_RTL2_8A_MASK);
	rtw89_phy_write32(rtwdev, R_P0_RPL2, val);
	rtw89_phy_write32(rtwdev, R_P1_RPL2, val);

	val = u32_encode_bits((gain->rpl_ofst_80[band][RF_PATH_A][2] +
			       gain->rpl_ofst_80[band][RF_PATH_B][2]) >> 1, B_P0_RTL3_82_MASK) |
	      u32_encode_bits((gain->rpl_ofst_80[band][RF_PATH_A][3] +
			       gain->rpl_ofst_80[band][RF_PATH_B][3]) >> 1, B_P0_RTL3_83_MASK) |
	      u32_encode_bits((gain->rpl_ofst_80[band][RF_PATH_A][4] +
			       gain->rpl_ofst_80[band][RF_PATH_B][4]) >> 1, B_P0_RTL3_84_MASK) |
	      u32_encode_bits((gain->rpl_ofst_80[band][RF_PATH_A][9] +
			       gain->rpl_ofst_80[band][RF_PATH_B][9]) >> 1, B_P0_RTL3_89_MASK);
	rtw89_phy_write32(rtwdev, R_P0_RPL3, val);
	rtw89_phy_write32(rtwdev, R_P1_RPL3, val);
}

static void rtw8852bx_ctrl_ch(struct rtw89_dev *rtwdev,
			      const struct rtw89_chan *chan,
			      enum rtw89_phy_idx phy_idx)
{
	u8 central_ch = chan->channel;
	u8 subband = chan->subband_type;
	u8 sco_comp;
	bool is_2g = central_ch <= 14;

	/* Path A */
	if (is_2g)
		rtw89_phy_write32_idx(rtwdev, R_PATH0_BAND_SEL_V1,
				      B_PATH0_BAND_SEL_MSK_V1, 1, phy_idx);
	else
		rtw89_phy_write32_idx(rtwdev, R_PATH0_BAND_SEL_V1,
				      B_PATH0_BAND_SEL_MSK_V1, 0, phy_idx);

	/* Path B */
	if (is_2g)
		rtw89_phy_write32_idx(rtwdev, R_PATH1_BAND_SEL_V1,
				      B_PATH1_BAND_SEL_MSK_V1, 1, phy_idx);
	else
		rtw89_phy_write32_idx(rtwdev, R_PATH1_BAND_SEL_V1,
				      B_PATH1_BAND_SEL_MSK_V1, 0, phy_idx);

	/* SCO compensate FC setting */
	sco_comp = rtw8852bx_sco_mapping(central_ch);
	rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_INV, sco_comp, phy_idx);

	if (chan->band_type == RTW89_BAND_6G)
		return;

	/* CCK parameters */
	if (central_ch == 14) {
		rtw89_phy_write32_mask(rtwdev, R_TXFIR0, B_TXFIR_C01, 0x3b13ff);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR2, B_TXFIR_C23, 0x1c42de);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR4, B_TXFIR_C45, 0xfdb0ad);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR6, B_TXFIR_C67, 0xf60f6e);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR8, B_TXFIR_C89, 0xfd8f92);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRA, B_TXFIR_CAB, 0x2d011);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRC, B_TXFIR_CCD, 0x1c02c);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRE, B_TXFIR_CEF, 0xfff00a);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_TXFIR0, B_TXFIR_C01, 0x3d23ff);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR2, B_TXFIR_C23, 0x29b354);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR4, B_TXFIR_C45, 0xfc1c8);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR6, B_TXFIR_C67, 0xfdb053);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR8, B_TXFIR_C89, 0xf86f9a);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRA, B_TXFIR_CAB, 0xfaef92);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRC, B_TXFIR_CCD, 0xfe5fcc);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRE, B_TXFIR_CEF, 0xffdff5);
	}

	rtw8852bx_set_gain_error(rtwdev, subband, RF_PATH_A);
	rtw8852bx_set_gain_error(rtwdev, subband, RF_PATH_B);
	rtw8852bx_set_gain_offset(rtwdev, subband, phy_idx);
	rtw8852bx_set_rxsc_rpl_comp(rtwdev, subband);
}

static void rtw8852b_bw_setting(struct rtw89_dev *rtwdev, u8 bw, u8 path)
{
	static const u32 adc_sel[2] = {0xC0EC, 0xC1EC};
	static const u32 wbadc_sel[2] = {0xC0E4, 0xC1E4};

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_5:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x1);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x0);
		break;
	case RTW89_CHANNEL_WIDTH_10:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x2);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x1);
		break;
	case RTW89_CHANNEL_WIDTH_20:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x0);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x2);
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x0);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x2);
		break;
	case RTW89_CHANNEL_WIDTH_80:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x0);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x2);
		break;
	default:
		rtw89_warn(rtwdev, "Fail to set ADC\n");
	}
}

static
void rtw8852bt_adc_cfg(struct rtw89_dev *rtwdev, u8 bw, u8 path)
{
	static const u32 rck_reset_count[2] = {0xC0E8, 0xC1E8};
	static const u32 adc_op5_bw_sel[2] = {0xC0D8, 0xC1D8};
	static const u32 adc_sample_td[2] = {0xC0D4, 0xC1D4};
	static const u32 adc_rst_cycle[2] = {0xC0EC, 0xC1EC};
	static const u32 decim_filter[2] = {0xC0EC, 0xC1EC};
	static const u32 rck_offset[2] = {0xC0C4, 0xC1C4};
	static const u32 rx_adc_clk[2] = {0x12A0, 0x32A0};
	static const u32 wbadc_sel[2] = {0xC0E4, 0xC1E4};
	static const u32 idac2_1[2] = {0xC0D4, 0xC1D4};
	static const u32 idac2[2] = {0xC0D4, 0xC1D4};
	static const u32 upd_clk_adc = {0x704};

	if (rtwdev->chip->chip_id != RTL8852BT)
		return;

	rtw89_phy_write32_mask(rtwdev, idac2[path], B_P0_CFCH_CTL, 0x8);
	rtw89_phy_write32_mask(rtwdev, rck_reset_count[path], B_ADCMOD_LP, 0x9);
	rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], B_WDADC_SEL, 0x2);
	rtw89_phy_write32_mask(rtwdev, rx_adc_clk[path], B_P0_RXCK_ADJ, 0x49);
	rtw89_phy_write32_mask(rtwdev, decim_filter[path], B_DCIM_FR, 0x0);

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_5:
	case RTW89_CHANNEL_WIDTH_10:
	case RTW89_CHANNEL_WIDTH_20:
	case RTW89_CHANNEL_WIDTH_40:
		rtw89_phy_write32_mask(rtwdev, idac2_1[path], B_P0_CFCH_EN, 0x2);
		rtw89_phy_write32_mask(rtwdev, adc_sample_td[path], B_P0_CFCH_BW0, 0x3);
		rtw89_phy_write32_mask(rtwdev, adc_op5_bw_sel[path], B_P0_CFCH_BW1, 0xf);
		rtw89_phy_write32_mask(rtwdev, rck_offset[path], B_DRCK_MUL, 0x0);
		/* Tx TSSI ADC update */
		rtw89_phy_write32_mask(rtwdev, upd_clk_adc, B_RSTB_ASYNC_BW80, 0);

		if (rtwdev->efuse.rfe_type >= 51)
			rtw89_phy_write32_mask(rtwdev, adc_rst_cycle[path], B_DCIM_RC, 0x2);
		else
			rtw89_phy_write32_mask(rtwdev, adc_rst_cycle[path], B_DCIM_RC, 0x3);
		break;
	case RTW89_CHANNEL_WIDTH_80:
		rtw89_phy_write32_mask(rtwdev, idac2_1[path], B_P0_CFCH_EN, 0x2);
		rtw89_phy_write32_mask(rtwdev, adc_sample_td[path], B_P0_CFCH_BW0, 0x2);
		rtw89_phy_write32_mask(rtwdev, adc_op5_bw_sel[path], B_P0_CFCH_BW1, 0x8);
		rtw89_phy_write32_mask(rtwdev, rck_offset[path], B_DRCK_MUL, 0x0);
		rtw89_phy_write32_mask(rtwdev, adc_rst_cycle[path], B_DCIM_RC, 0x3);
		/* Tx TSSI ADC update */
		rtw89_phy_write32_mask(rtwdev, upd_clk_adc, B_RSTB_ASYNC_BW80, 1);
		break;
	case RTW89_CHANNEL_WIDTH_160:
		rtw89_phy_write32_mask(rtwdev, idac2_1[path], B_P0_CFCH_EN, 0x0);
		rtw89_phy_write32_mask(rtwdev, adc_sample_td[path], B_P0_CFCH_BW0, 0x2);
		rtw89_phy_write32_mask(rtwdev, adc_op5_bw_sel[path], B_P0_CFCH_BW1, 0x4);
		rtw89_phy_write32_mask(rtwdev, rck_offset[path], B_DRCK_MUL, 0x6);
		rtw89_phy_write32_mask(rtwdev, adc_rst_cycle[path], B_DCIM_RC, 0x3);
		/* Tx TSSI ADC update */
		rtw89_phy_write32_mask(rtwdev, upd_clk_adc, B_RSTB_ASYNC_BW80, 2);
		break;
	default:
		rtw89_warn(rtwdev, "Fail to set ADC\n");
		break;
	}
}

static void rtw8852bx_ctrl_bw(struct rtw89_dev *rtwdev, u8 pri_ch, u8 bw,
			      enum rtw89_phy_idx phy_idx)
{
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;
	u32 rx_path_0;

	rx_path_0 = rtw89_phy_read32_idx(rtwdev, R_CHBW_MOD_V1, B_ANT_RX_SEG0, phy_idx);

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_5:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_SET, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_SBW, 0x1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_PRICH, 0x0, phy_idx);

		/*Set RF mode at 3 */
		rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE_ORI_RX,
				      B_P0_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE_ORI_RX,
				      B_P1_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		if (chip_id == RTL8852BT) {
			rtw89_phy_write32_idx(rtwdev, R_PATH0_BAND_SEL_V1,
					      B_PATH0_BAND_NRBW_EN_V1, 0x0, phy_idx);
			rtw89_phy_write32_idx(rtwdev, R_PATH1_BAND_SEL_V1,
					      B_PATH1_BAND_NRBW_EN_V1, 0x0, phy_idx);
		}
		break;
	case RTW89_CHANNEL_WIDTH_10:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_SET, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_SBW, 0x2, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_PRICH, 0x0, phy_idx);

		/*Set RF mode at 3 */
		rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE_ORI_RX,
				      B_P0_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE_ORI_RX,
				      B_P1_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		if (chip_id == RTL8852BT) {
			rtw89_phy_write32_idx(rtwdev, R_PATH0_BAND_SEL_V1,
					      B_PATH0_BAND_NRBW_EN_V1, 0x0, phy_idx);
			rtw89_phy_write32_idx(rtwdev, R_PATH1_BAND_SEL_V1,
					      B_PATH1_BAND_NRBW_EN_V1, 0x0, phy_idx);
		}
		break;
	case RTW89_CHANNEL_WIDTH_20:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_SET, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_SBW, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_PRICH, 0x0, phy_idx);

		/*Set RF mode at 3 */
		rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE_ORI_RX,
				      B_P0_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE_ORI_RX,
				      B_P1_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		if (chip_id == RTL8852BT) {
			rtw89_phy_write32_idx(rtwdev, R_PATH0_BAND_SEL_V1,
					      B_PATH0_BAND_NRBW_EN_V1, 0x1, phy_idx);
			rtw89_phy_write32_idx(rtwdev, R_PATH1_BAND_SEL_V1,
					      B_PATH1_BAND_NRBW_EN_V1, 0x1, phy_idx);
		}
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_SET, 0x1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_SBW, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_PRICH,
				      pri_ch, phy_idx);

		/*Set RF mode at 3 */
		rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE_ORI_RX,
				      B_P0_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE_ORI_RX,
				      B_P1_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		/*CCK primary channel */
		if (pri_ch == RTW89_SC_20_UPPER)
			rtw89_phy_write32_mask(rtwdev, R_RXSC, B_RXSC_EN, 1);
		else
			rtw89_phy_write32_mask(rtwdev, R_RXSC, B_RXSC_EN, 0);

		break;
	case RTW89_CHANNEL_WIDTH_80:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_SET, 0x2, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_SBW, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_PRICH,
				      pri_ch, phy_idx);

		/*Set RF mode at 3 */
		rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE_ORI_RX,
				      B_P0_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE_ORI_RX,
				      B_P1_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		break;
	default:
		rtw89_warn(rtwdev, "Fail to switch bw (bw:%d, pri ch:%d)\n", bw,
			   pri_ch);
	}

	if (chip_id == RTL8852B) {
		rtw8852b_bw_setting(rtwdev, bw, RF_PATH_A);
		rtw8852b_bw_setting(rtwdev, bw, RF_PATH_B);
	} else if (chip_id == RTL8852BT) {
		rtw8852bt_adc_cfg(rtwdev, bw, RF_PATH_A);
		rtw8852bt_adc_cfg(rtwdev, bw, RF_PATH_B);
	}

	if (rx_path_0 == 0x1)
		rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE_ORI_RX,
				      B_P1_RFMODE_ORI_RX_ALL, 0x111, phy_idx);
	else if (rx_path_0 == 0x2)
		rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE_ORI_RX,
				      B_P0_RFMODE_ORI_RX_ALL, 0x111, phy_idx);
}

static void rtw8852bx_ctrl_cck_en(struct rtw89_dev *rtwdev, bool cck_en)
{
	if (cck_en) {
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK_ADC, B_ENABLE_CCK, 1);
		rtw89_phy_write32_mask(rtwdev, R_RXCCA, B_RXCCA_DIS, 0);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK_ADC, B_ENABLE_CCK, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXCCA, B_RXCCA_DIS, 1);
	}
}

static void rtw8852bx_5m_mask(struct rtw89_dev *rtwdev, const struct rtw89_chan *chan,
			      enum rtw89_phy_idx phy_idx)
{
	u8 pri_ch = chan->pri_ch_idx;
	bool mask_5m_low;
	bool mask_5m_en;

	switch (chan->band_width) {
	case RTW89_CHANNEL_WIDTH_40:
		/* Prich=1: Mask 5M High, Prich=2: Mask 5M Low */
		mask_5m_en = true;
		mask_5m_low = pri_ch == RTW89_SC_20_LOWER;
		break;
	case RTW89_CHANNEL_WIDTH_80:
		/* Prich=3: Mask 5M High, Prich=4: Mask 5M Low, Else: Disable */
		mask_5m_en = pri_ch == RTW89_SC_20_UPMOST ||
			     pri_ch == RTW89_SC_20_LOWEST;
		mask_5m_low = pri_ch == RTW89_SC_20_LOWEST;
		break;
	default:
		mask_5m_en = false;
		break;
	}

	if (!mask_5m_en) {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_EN, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_EN, 0x0);
		rtw89_phy_write32_idx(rtwdev, R_ASSIGN_SBD_OPT_V1,
				      B_ASSIGN_SBD_OPT_EN_V1, 0x0, phy_idx);
		return;
	}

	if (mask_5m_low) {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_TH, 0x4);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_SB2, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_SB0, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_TH, 0x4);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_SB2, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_SB0, 0x1);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_TH, 0x4);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_SB2, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_SB0, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_TH, 0x4);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_SB2, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_SB0, 0x0);
	}
	rtw89_phy_write32_idx(rtwdev, R_ASSIGN_SBD_OPT_V1,
			      B_ASSIGN_SBD_OPT_EN_V1, 0x1, phy_idx);
}

static void __rtw8852bx_bb_reset_all(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	rtw89_phy_write32_idx(rtwdev, R_S0_HW_SI_DIS, B_S0_HW_SI_DIS_W_R_TRIG, 0x7, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_S1_HW_SI_DIS, B_S1_HW_SI_DIS_W_R_TRIG, 0x7, phy_idx);
	fsleep(1);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 0, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_S0_HW_SI_DIS, B_S0_HW_SI_DIS_W_R_TRIG, 0x0, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_S1_HW_SI_DIS, B_S1_HW_SI_DIS_W_R_TRIG, 0x0, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1, phy_idx);
}

static void rtw8852bx_bb_macid_ctrl_init(struct rtw89_dev *rtwdev,
					 enum rtw89_phy_idx phy_idx)
{
	u32 addr;

	for (addr = R_AX_PWR_MACID_LMT_TABLE0;
	     addr <= R_AX_PWR_MACID_LMT_TABLE127; addr += 4)
		rtw89_mac_txpwr_write32(rtwdev, phy_idx, addr, 0);
}

static void __rtw8852bx_bb_sethw(struct rtw89_dev *rtwdev)
{
	struct rtw89_phy_efuse_gain *gain = &rtwdev->efuse_gain;

	rtw89_phy_write32_clr(rtwdev, R_P0_EN_SOUND_WO_NDP, B_P0_EN_SOUND_WO_NDP);
	rtw89_phy_write32_clr(rtwdev, R_P1_EN_SOUND_WO_NDP, B_P1_EN_SOUND_WO_NDP);

	rtw8852bx_bb_macid_ctrl_init(rtwdev, RTW89_PHY_0);

	/* read these registers after loading BB parameters */
	gain->offset_base[RTW89_PHY_0] =
		rtw89_phy_read32_mask(rtwdev, R_P0_RPL1, B_P0_RPL1_BIAS_MASK);
	gain->rssi_base[RTW89_PHY_0] =
		rtw89_phy_read32_mask(rtwdev, R_P1_RPL1, B_P0_RPL1_BIAS_MASK);
}

static void rtw8852bx_bb_set_pop(struct rtw89_dev *rtwdev)
{
	if (rtwdev->hw->conf.flags & IEEE80211_CONF_MONITOR)
		rtw89_phy_write32_clr(rtwdev, R_PKT_CTRL, B_PKT_POP_EN);
}

static u32 rtw8852bt_spur_freq(struct rtw89_dev *rtwdev,
			       const struct rtw89_chan *chan)
{
	u8 center_chan = chan->channel;

	switch (chan->band_type) {
	case RTW89_BAND_5G:
		if (center_chan == 151 || center_chan == 153 ||
		    center_chan == 155 || center_chan == 163)
			return 5760;
		break;
	default:
		break;
	}

	return 0;
}

#define CARRIER_SPACING_312_5 312500 /* 312.5 kHz */
#define CARRIER_SPACING_78_125 78125 /* 78.125 kHz */
#define MAX_TONE_NUM 2048

static void rtw8852bt_set_csi_tone_idx(struct rtw89_dev *rtwdev,
				       const struct rtw89_chan *chan,
				       enum rtw89_phy_idx phy_idx)
{
	s32 freq_diff, csi_idx, csi_tone_idx;
	u32 spur_freq;

	spur_freq = rtw8852bt_spur_freq(rtwdev, chan);
	if (spur_freq == 0) {
		rtw89_phy_write32_idx(rtwdev, R_SEG0CSI_EN_V1, B_SEG0CSI_EN,
				      0, phy_idx);
		return;
	}

	freq_diff = (spur_freq - chan->freq) * 1000000;
	csi_idx = s32_div_u32_round_closest(freq_diff, CARRIER_SPACING_78_125);
	s32_div_u32_round_down(csi_idx, MAX_TONE_NUM, &csi_tone_idx);

	rtw89_phy_write32_idx(rtwdev, R_SEG0CSI_V1, B_SEG0CSI_IDX,
			      csi_tone_idx, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_SEG0CSI_EN_V1, B_SEG0CSI_EN, 1, phy_idx);
}

static
void __rtw8852bx_set_channel_bb(struct rtw89_dev *rtwdev, const struct rtw89_chan *chan,
				enum rtw89_phy_idx phy_idx)
{
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;
	bool cck_en = chan->channel <= 14;
	u8 pri_ch_idx = chan->pri_ch_idx;
	u8 band = chan->band_type, chan_idx;

	if (cck_en)
		rtw8852bx_ctrl_sco_cck(rtwdev,  chan->primary_channel);

	rtw8852bx_ctrl_ch(rtwdev, chan, phy_idx);
	rtw8852bx_ctrl_bw(rtwdev, pri_ch_idx, chan->band_width, phy_idx);
	rtw8852bx_ctrl_cck_en(rtwdev, cck_en);
	if (chip_id == RTL8852BT)
		rtw8852bt_set_csi_tone_idx(rtwdev, chan, phy_idx);
	if (chip_id == RTL8852B && chan->band_type == RTW89_BAND_5G) {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BT_SHARE_V1,
				       B_PATH0_BT_SHARE_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BTG_PATH_V1,
				       B_PATH0_BTG_PATH_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BT_SHARE_V1,
				       B_PATH1_BT_SHARE_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BTG_PATH_V1,
				       B_PATH1_BTG_PATH_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD_V1, B_BT_SHARE, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_BT_SEG0, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_BT_DYN_DC_EST_EN_V1,
				       B_BT_DYN_DC_EST_EN_MSK, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_GNT_BT_WGT_EN, B_GNT_BT_WGT_EN, 0x0);
	}
	chan_idx = rtw89_encode_chan_idx(rtwdev, chan->primary_channel, band);
	rtw89_phy_write32_mask(rtwdev, R_MAC_PIN_SEL, B_CH_IDX_SEG0, chan_idx);
	rtw8852bx_5m_mask(rtwdev, chan, phy_idx);
	rtw8852bx_bb_set_pop(rtwdev);
	__rtw8852bx_bb_reset_all(rtwdev, phy_idx);
}

static u32 rtw8852bx_bb_cal_txpwr_ref(struct rtw89_dev *rtwdev,
				      enum rtw89_phy_idx phy_idx, s16 ref)
{
	const u16 tssi_16dbm_cw = 0x12c;
	const u8 base_cw_0db = 0x27;
	const s8 ofst_int = 0;
	s16 pwr_s10_3;
	s16 rf_pwr_cw;
	u16 bb_pwr_cw;
	u32 pwr_cw;
	u32 tssi_ofst_cw;

	pwr_s10_3 = (ref << 1) + (s16)(ofst_int) + (s16)(base_cw_0db << 3);
	bb_pwr_cw = u16_get_bits(pwr_s10_3, GENMASK(2, 0));
	rf_pwr_cw = u16_get_bits(pwr_s10_3, GENMASK(8, 3));
	rf_pwr_cw = clamp_t(s16, rf_pwr_cw, 15, 63);
	pwr_cw = (rf_pwr_cw << 3) | bb_pwr_cw;

	tssi_ofst_cw = (u32)((s16)tssi_16dbm_cw + (ref << 1) - (16 << 3));
	rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
		    "[TXPWR] tssi_ofst_cw=%d rf_cw=0x%x bb_cw=0x%x\n",
		    tssi_ofst_cw, rf_pwr_cw, bb_pwr_cw);

	return u32_encode_bits(tssi_ofst_cw, B_DPD_TSSI_CW) |
	       u32_encode_bits(pwr_cw, B_DPD_PWR_CW) |
	       u32_encode_bits(ref, B_DPD_REF);
}

static void rtw8852bx_set_txpwr_ref(struct rtw89_dev *rtwdev,
				    enum rtw89_phy_idx phy_idx)
{
	static const u32 addr[RF_PATH_NUM_8852BX] = {0x5800, 0x7800};
	const u32 mask = B_DPD_TSSI_CW | B_DPD_PWR_CW | B_DPD_REF;
	const u8 ofst_ofdm = 0x4;
	const u8 ofst_cck = 0x8;
	const s16 ref_ofdm = 0;
	const s16 ref_cck = 0;
	u32 val;
	u8 i;

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[TXPWR] set txpwr reference\n");

	rtw89_mac_txpwr_write32_mask(rtwdev, phy_idx, R_AX_PWR_RATE_CTRL,
				     B_AX_PWR_REF, 0x0);

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[TXPWR] set bb ofdm txpwr ref\n");
	val = rtw8852bx_bb_cal_txpwr_ref(rtwdev, phy_idx, ref_ofdm);

	for (i = 0; i < RF_PATH_NUM_8852BX; i++)
		rtw89_phy_write32_idx(rtwdev, addr[i] + ofst_ofdm, mask, val,
				      phy_idx);

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[TXPWR] set bb cck txpwr ref\n");
	val = rtw8852bx_bb_cal_txpwr_ref(rtwdev, phy_idx, ref_cck);

	for (i = 0; i < RF_PATH_NUM_8852BX; i++)
		rtw89_phy_write32_idx(rtwdev, addr[i] + ofst_cck, mask, val,
				      phy_idx);
}

static void rtw8852bx_bb_set_tx_shape_dfir(struct rtw89_dev *rtwdev,
					   const struct rtw89_chan *chan,
					   u8 tx_shape_idx,
					   enum rtw89_phy_idx phy_idx)
{
#define __DFIR_CFG_ADDR(i) (R_TXFIR0 + ((i) << 2))
#define __DFIR_CFG_MASK 0xffffffff
#define __DFIR_CFG_NR 8
#define __DECL_DFIR_PARAM(_name, _val...) \
	static const u32 param_ ## _name[] = {_val}; \
	static_assert(ARRAY_SIZE(param_ ## _name) == __DFIR_CFG_NR)

	__DECL_DFIR_PARAM(flat,
			  0x023D23FF, 0x0029B354, 0x000FC1C8, 0x00FDB053,
			  0x00F86F9A, 0x06FAEF92, 0x00FE5FCC, 0x00FFDFF5);
	__DECL_DFIR_PARAM(sharp,
			  0x023D83FF, 0x002C636A, 0x0013F204, 0x00008090,
			  0x00F87FB0, 0x06F99F83, 0x00FDBFBA, 0x00003FF5);
	__DECL_DFIR_PARAM(sharp_14,
			  0x023B13FF, 0x001C42DE, 0x00FDB0AD, 0x00F60F6E,
			  0x00FD8F92, 0x0602D011, 0x0001C02C, 0x00FFF00A);
	u8 ch = chan->channel;
	const u32 *param;
	u32 addr;
	int i;

	if (ch > 14) {
		rtw89_warn(rtwdev,
			   "set tx shape dfir by unknown ch: %d on 2G\n", ch);
		return;
	}

	if (ch == 14)
		param = param_sharp_14;
	else
		param = tx_shape_idx == 0 ? param_flat : param_sharp;

	for (i = 0; i < __DFIR_CFG_NR; i++) {
		addr = __DFIR_CFG_ADDR(i);
		rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
			    "set tx shape dfir: 0x%x: 0x%x\n", addr, param[i]);
		rtw89_phy_write32_idx(rtwdev, addr, __DFIR_CFG_MASK, param[i],
				      phy_idx);
	}

#undef __DECL_DFIR_PARAM
#undef __DFIR_CFG_NR
#undef __DFIR_CFG_MASK
#undef __DECL_CFG_ADDR
}

static void rtw8852bx_set_tx_shape(struct rtw89_dev *rtwdev,
				   const struct rtw89_chan *chan,
				   enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_rfe_parms *rfe_parms = rtwdev->rfe_parms;
	u8 band = chan->band_type;
	u8 regd = rtw89_regd_get(rtwdev, band);
	u8 tx_shape_cck = (*rfe_parms->tx_shape.lmt)[band][RTW89_RS_CCK][regd];
	u8 tx_shape_ofdm = (*rfe_parms->tx_shape.lmt)[band][RTW89_RS_OFDM][regd];

	if (band == RTW89_BAND_2G)
		rtw8852bx_bb_set_tx_shape_dfir(rtwdev, chan, tx_shape_cck, phy_idx);

	rtw89_phy_write32_mask(rtwdev, R_DCFO_OPT, B_TXSHAPE_TRIANGULAR_CFG,
			       tx_shape_ofdm);
}

static void __rtw8852bx_set_txpwr(struct rtw89_dev *rtwdev,
				  const struct rtw89_chan *chan,
				  enum rtw89_phy_idx phy_idx)
{
	rtw89_phy_set_txpwr_byrate(rtwdev, chan, phy_idx);
	rtw89_phy_set_txpwr_offset(rtwdev, chan, phy_idx);
	rtw8852bx_set_tx_shape(rtwdev, chan, phy_idx);
	rtw89_phy_set_txpwr_limit(rtwdev, chan, phy_idx);
	rtw89_phy_set_txpwr_limit_ru(rtwdev, chan, phy_idx);
}

static void __rtw8852bx_set_txpwr_ctrl(struct rtw89_dev *rtwdev,
				       enum rtw89_phy_idx phy_idx)
{
	rtw8852bx_set_txpwr_ref(rtwdev, phy_idx);
}

static
void __rtw8852bx_set_txpwr_ul_tb_offset(struct rtw89_dev *rtwdev,
					s8 pw_ofst, enum rtw89_mac_idx mac_idx)
{
	u32 reg;

	if (pw_ofst < -16 || pw_ofst > 15) {
		rtw89_warn(rtwdev, "[ULTB] Err pwr_offset=%d\n", pw_ofst);
		return;
	}

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_PWR_UL_TB_CTRL, mac_idx);
	rtw89_write32_set(rtwdev, reg, B_AX_PWR_UL_TB_CTRL_EN);

	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_PWR_UL_TB_1T, mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_AX_PWR_UL_TB_1T_MASK, pw_ofst);

	pw_ofst = max_t(s8, pw_ofst - 3, -16);
	reg = rtw89_mac_reg_by_idx(rtwdev, R_AX_PWR_UL_TB_2T, mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_AX_PWR_UL_TB_2T_MASK, pw_ofst);
}

static int
__rtw8852bx_init_txpwr_unit(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	int ret;

	ret = rtw89_mac_txpwr_write32(rtwdev, phy_idx, R_AX_PWR_UL_CTRL2, 0x07763333);
	if (ret)
		return ret;

	ret = rtw89_mac_txpwr_write32(rtwdev, phy_idx, R_AX_PWR_COEXT_CTRL, 0x01ebf000);
	if (ret)
		return ret;

	ret = rtw89_mac_txpwr_write32(rtwdev, phy_idx, R_AX_PWR_UL_CTRL0, 0x0002f8ff);
	if (ret)
		return ret;

	rtw8852bx_set_txpwr_ul_tb_offset(rtwdev, 0, phy_idx == RTW89_PHY_1 ?
						   RTW89_MAC_1 : RTW89_MAC_0);

	return 0;
}

static
void __rtw8852bx_bb_set_plcp_tx(struct rtw89_dev *rtwdev)
{
	const struct rtw89_reg3_def *def = rtw8852bx_pmac_ht20_mcs7_tbl;
	u8 i;

	for (i = 0; i < ARRAY_SIZE(rtw8852bx_pmac_ht20_mcs7_tbl); i++, def++)
		rtw89_phy_write32_mask(rtwdev, def->addr, def->mask, def->data);
}

static void rtw8852bx_stop_pmac_tx(struct rtw89_dev *rtwdev,
				   struct rtw8852bx_bb_pmac_info *tx_info,
				   enum rtw89_phy_idx idx)
{
	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "PMAC Stop Tx");
	if (tx_info->mode == CONT_TX)
		rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_PRD, B_PMAC_CTX_EN, 0, idx);
	else if (tx_info->mode == PKTS_TX)
		rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_PRD, B_PMAC_PTX_EN, 0, idx);
}

static void rtw8852bx_start_pmac_tx(struct rtw89_dev *rtwdev,
				    struct rtw8852bx_bb_pmac_info *tx_info,
				    enum rtw89_phy_idx idx)
{
	enum rtw8852bx_pmac_mode mode = tx_info->mode;
	u32 pkt_cnt = tx_info->tx_cnt;
	u16 period = tx_info->period;

	if (mode == CONT_TX && !tx_info->is_cck) {
		rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_PRD, B_PMAC_CTX_EN, 1, idx);
		rtw89_debug(rtwdev, RTW89_DBG_TSSI, "PMAC CTx Start");
	} else if (mode == PKTS_TX) {
		rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_PRD, B_PMAC_PTX_EN, 1, idx);
		rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_PRD,
				      B_PMAC_TX_PRD_MSK, period, idx);
		rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_CNT, B_PMAC_TX_CNT_MSK,
				      pkt_cnt, idx);
		rtw89_debug(rtwdev, RTW89_DBG_TSSI, "PMAC PTx Start");
	}

	rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_CTRL, B_PMAC_TXEN_DIS, 1, idx);
	rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_CTRL, B_PMAC_TXEN_DIS, 0, idx);
}

static
void rtw8852bx_bb_set_pmac_tx(struct rtw89_dev *rtwdev,
			      struct rtw8852bx_bb_pmac_info *tx_info,
			      enum rtw89_phy_idx idx, const struct rtw89_chan *chan)
{
	if (!tx_info->en_pmac_tx) {
		rtw8852bx_stop_pmac_tx(rtwdev, tx_info, idx);
		rtw89_phy_write32_idx(rtwdev, R_PD_CTRL, B_PD_HIT_DIS, 0, idx);
		if (chan->band_type == RTW89_BAND_2G)
			rtw89_phy_write32_clr(rtwdev, R_RXCCA, B_RXCCA_DIS);
		return;
	}

	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "PMAC Tx Enable");

	rtw89_phy_write32_idx(rtwdev, R_PMAC_GNT, B_PMAC_GNT_TXEN, 1, idx);
	rtw89_phy_write32_idx(rtwdev, R_PMAC_GNT, B_PMAC_GNT_RXEN, 1, idx);
	rtw89_phy_write32_idx(rtwdev, R_PMAC_RX_CFG1, B_PMAC_OPT1_MSK, 0x3f, idx);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 0, idx);
	rtw89_phy_write32_idx(rtwdev, R_PD_CTRL, B_PD_HIT_DIS, 1, idx);
	rtw89_phy_write32_set(rtwdev, R_RXCCA, B_RXCCA_DIS);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1, idx);

	rtw8852bx_start_pmac_tx(rtwdev, tx_info, idx);
}

static
void __rtw8852bx_bb_set_pmac_pkt_tx(struct rtw89_dev *rtwdev, u8 enable,
				    u16 tx_cnt, u16 period, u16 tx_time,
				    enum rtw89_phy_idx idx, const struct rtw89_chan *chan)
{
	struct rtw8852bx_bb_pmac_info tx_info = {0};

	tx_info.en_pmac_tx = enable;
	tx_info.is_cck = 0;
	tx_info.mode = PKTS_TX;
	tx_info.tx_cnt = tx_cnt;
	tx_info.period = period;
	tx_info.tx_time = tx_time;

	rtw8852bx_bb_set_pmac_tx(rtwdev, &tx_info, idx, chan);
}

static
void __rtw8852bx_bb_set_power(struct rtw89_dev *rtwdev, s16 pwr_dbm,
			      enum rtw89_phy_idx idx)
{
	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "PMAC CFG Tx PWR = %d", pwr_dbm);

	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_PWR_EN, 1, idx);
	rtw89_phy_write32_idx(rtwdev, R_TXPWR, B_TXPWR_MSK, pwr_dbm, idx);
}

static
void __rtw8852bx_bb_cfg_tx_path(struct rtw89_dev *rtwdev, u8 tx_path)
{
	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_MOD, 7, RTW89_PHY_0);

	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "PMAC CFG Tx Path = %d", tx_path);

	if (tx_path == RF_PATH_A) {
		rtw89_phy_write32_mask(rtwdev, R_TXPATH_SEL, B_TXPATH_SEL_MSK, 1);
		rtw89_phy_write32_mask(rtwdev, R_TXNSS_MAP, B_TXNSS_MAP_MSK, 0);
	} else if (tx_path == RF_PATH_B) {
		rtw89_phy_write32_mask(rtwdev, R_TXPATH_SEL, B_TXPATH_SEL_MSK, 2);
		rtw89_phy_write32_mask(rtwdev, R_TXNSS_MAP, B_TXNSS_MAP_MSK, 0);
	} else if (tx_path == RF_PATH_AB) {
		rtw89_phy_write32_mask(rtwdev, R_TXPATH_SEL, B_TXPATH_SEL_MSK, 3);
		rtw89_phy_write32_mask(rtwdev, R_TXNSS_MAP, B_TXNSS_MAP_MSK, 4);
	} else {
		rtw89_debug(rtwdev, RTW89_DBG_TSSI, "Error Tx Path");
	}
}

static
void __rtw8852bx_bb_tx_mode_switch(struct rtw89_dev *rtwdev,
				   enum rtw89_phy_idx idx, u8 mode)
{
	if (mode != 0)
		return;

	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "Tx mode switch");

	rtw89_phy_write32_idx(rtwdev, R_PMAC_GNT, B_PMAC_GNT_TXEN, 0, idx);
	rtw89_phy_write32_idx(rtwdev, R_PMAC_GNT, B_PMAC_GNT_RXEN, 0, idx);
	rtw89_phy_write32_idx(rtwdev, R_PMAC_RX_CFG1, B_PMAC_OPT1_MSK, 0, idx);
	rtw89_phy_write32_idx(rtwdev, R_PMAC_RXMOD, B_PMAC_RXMOD_MSK, 0, idx);
	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_DPD_EN, 0, idx);
	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_MOD, 0, idx);
	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_PWR_EN, 0, idx);
}

static
void __rtw8852bx_bb_backup_tssi(struct rtw89_dev *rtwdev, enum rtw89_phy_idx idx,
				struct rtw8852bx_bb_tssi_bak *bak)
{
	s32 tmp;

	bak->tx_path = rtw89_phy_read32_idx(rtwdev, R_TXPATH_SEL, B_TXPATH_SEL_MSK, idx);
	bak->rx_path = rtw89_phy_read32_idx(rtwdev, R_CHBW_MOD_V1, B_ANT_RX_SEG0, idx);
	bak->p0_rfmode = rtw89_phy_read32_idx(rtwdev, R_P0_RFMODE, MASKDWORD, idx);
	bak->p0_rfmode_ftm = rtw89_phy_read32_idx(rtwdev, R_P0_RFMODE_FTM_RX, MASKDWORD, idx);
	bak->p1_rfmode = rtw89_phy_read32_idx(rtwdev, R_P1_RFMODE, MASKDWORD, idx);
	bak->p1_rfmode_ftm = rtw89_phy_read32_idx(rtwdev, R_P1_RFMODE_FTM_RX, MASKDWORD, idx);
	tmp = rtw89_phy_read32_idx(rtwdev, R_TXPWR, B_TXPWR_MSK, idx);
	bak->tx_pwr = sign_extend32(tmp, 8);
}

static
void __rtw8852bx_bb_restore_tssi(struct rtw89_dev *rtwdev, enum rtw89_phy_idx idx,
				 const struct rtw8852bx_bb_tssi_bak *bak)
{
	rtw89_phy_write32_idx(rtwdev, R_TXPATH_SEL, B_TXPATH_SEL_MSK, bak->tx_path, idx);
	if (bak->tx_path == RF_AB)
		rtw89_phy_write32_mask(rtwdev, R_TXNSS_MAP, B_TXNSS_MAP_MSK, 0x4);
	else
		rtw89_phy_write32_mask(rtwdev, R_TXNSS_MAP, B_TXNSS_MAP_MSK, 0x0);
	rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_ANT_RX_SEG0, bak->rx_path, idx);
	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_PWR_EN, 1, idx);
	rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE, MASKDWORD, bak->p0_rfmode, idx);
	rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE_FTM_RX, MASKDWORD, bak->p0_rfmode_ftm, idx);
	rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE, MASKDWORD, bak->p1_rfmode, idx);
	rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE_FTM_RX, MASKDWORD, bak->p1_rfmode_ftm, idx);
	rtw89_phy_write32_idx(rtwdev, R_TXPWR, B_TXPWR_MSK, bak->tx_pwr, idx);
}

static void __rtw8852bx_ctrl_nbtg_bt_tx(struct rtw89_dev *rtwdev, bool en,
					enum rtw89_phy_idx phy_idx)
{
	rtw89_phy_write_reg3_tbl(rtwdev, en ? &rtw8852bx_btc_preagc_en_defs_tbl :
						 &rtw8852bx_btc_preagc_dis_defs_tbl);
}

static void __rtw8852bx_ctrl_btg_bt_rx(struct rtw89_dev *rtwdev, bool en,
				       enum rtw89_phy_idx phy_idx)
{
	if (en) {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BT_SHARE_V1,
				       B_PATH0_BT_SHARE_V1, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BTG_PATH_V1,
				       B_PATH0_BTG_PATH_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_G_LNA6_OP1DB_V1,
				       B_PATH1_G_LNA6_OP1DB_V1, 0x20);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_G_TIA0_LNA6_OP1DB_V1,
				       B_PATH1_G_TIA0_LNA6_OP1DB_V1, 0x30);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BT_SHARE_V1,
				       B_PATH1_BT_SHARE_V1, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BTG_PATH_V1,
				       B_PATH1_BTG_PATH_V1, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PMAC_GNT, B_PMAC_GNT_P1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD_V1, B_BT_SHARE, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_BT_SEG0, 0x2);
		rtw89_phy_write32_mask(rtwdev, R_BT_DYN_DC_EST_EN_V1,
				       B_BT_DYN_DC_EST_EN_MSK, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_GNT_BT_WGT_EN, B_GNT_BT_WGT_EN, 0x1);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BT_SHARE_V1,
				       B_PATH0_BT_SHARE_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BTG_PATH_V1,
				       B_PATH0_BTG_PATH_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_G_LNA6_OP1DB_V1,
				       B_PATH1_G_LNA6_OP1DB_V1, 0x1a);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_G_TIA0_LNA6_OP1DB_V1,
				       B_PATH1_G_TIA0_LNA6_OP1DB_V1, 0x2a);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BT_SHARE_V1,
				       B_PATH1_BT_SHARE_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BTG_PATH_V1,
				       B_PATH1_BTG_PATH_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PMAC_GNT, B_PMAC_GNT_P1, 0xc);
		rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD_V1, B_BT_SHARE, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_BT_SEG0, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_BT_DYN_DC_EST_EN_V1,
				       B_BT_DYN_DC_EST_EN_MSK, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_GNT_BT_WGT_EN, B_GNT_BT_WGT_EN, 0x0);
	}
}

static
void __rtw8852bx_bb_ctrl_rx_path(struct rtw89_dev *rtwdev,
				 enum rtw89_rf_path_bit rx_path,
				 const struct rtw89_chan *chan)
{
	u32 rst_mask0;
	u32 rst_mask1;

	if (rx_path == RF_A) {
		rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD_V1, B_ANT_RX_SEG0, 1);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_1RCCA_SEG0, 1);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_1RCCA_SEG1, 1);
		rtw89_phy_write32_mask(rtwdev, R_RXHT_MCS_LIMIT, B_RXHT_MCS_LIMIT, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXVHT_MCS_LIMIT, B_RXVHT_MCS_LIMIT, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_USER_MAX, 4);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_MAX_NSS, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHETB_MAX_NSS, 0);
	} else if (rx_path == RF_B) {
		rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD_V1, B_ANT_RX_SEG0, 2);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_1RCCA_SEG0, 2);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_1RCCA_SEG1, 2);
		rtw89_phy_write32_mask(rtwdev, R_RXHT_MCS_LIMIT, B_RXHT_MCS_LIMIT, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXVHT_MCS_LIMIT, B_RXVHT_MCS_LIMIT, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_USER_MAX, 4);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_MAX_NSS, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHETB_MAX_NSS, 0);
	} else if (rx_path == RF_AB) {
		rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD_V1, B_ANT_RX_SEG0, 3);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_1RCCA_SEG0, 3);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_1RCCA_SEG1, 3);
		rtw89_phy_write32_mask(rtwdev, R_RXHT_MCS_LIMIT, B_RXHT_MCS_LIMIT, 1);
		rtw89_phy_write32_mask(rtwdev, R_RXVHT_MCS_LIMIT, B_RXVHT_MCS_LIMIT, 1);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_USER_MAX, 4);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_MAX_NSS, 1);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHETB_MAX_NSS, 1);
	}

	rtw8852bx_set_gain_offset(rtwdev, chan->subband_type, RTW89_PHY_0);

	if (chan->band_type == RTW89_BAND_2G &&
	    (rx_path == RF_B || rx_path == RF_AB))
		rtw8852bx_ctrl_btg_bt_rx(rtwdev, true, RTW89_PHY_0);
	else
		rtw8852bx_ctrl_btg_bt_rx(rtwdev, false, RTW89_PHY_0);

	rst_mask0 = B_P0_TXPW_RSTB_MANON | B_P0_TXPW_RSTB_TSSI;
	rst_mask1 = B_P1_TXPW_RSTB_MANON | B_P1_TXPW_RSTB_TSSI;
	if (rx_path == RF_A) {
		rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB, rst_mask0, 1);
		rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB, rst_mask0, 3);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_P1_TXPW_RSTB, rst_mask1, 1);
		rtw89_phy_write32_mask(rtwdev, R_P1_TXPW_RSTB, rst_mask1, 3);
	}
}

static void rtw8852bx_bb_ctrl_rf_mode_rx_path(struct rtw89_dev *rtwdev,
					      enum rtw89_rf_path_bit rx_path)
{
	if (rx_path == RF_A) {
		rtw89_phy_write32_mask(rtwdev, R_P0_RFMODE,
				       B_P0_RFMODE_ORI_TXRX_FTM_TX, 0x1233312);
		rtw89_phy_write32_mask(rtwdev, R_P0_RFMODE_FTM_RX,
				       B_P0_RFMODE_FTM_RX, 0x333);
		rtw89_phy_write32_mask(rtwdev, R_P1_RFMODE,
				       B_P1_RFMODE_ORI_TXRX_FTM_TX, 0x1111111);
		rtw89_phy_write32_mask(rtwdev, R_P1_RFMODE_FTM_RX,
				       B_P1_RFMODE_FTM_RX, 0x111);
	} else if (rx_path == RF_B) {
		rtw89_phy_write32_mask(rtwdev, R_P0_RFMODE,
				       B_P0_RFMODE_ORI_TXRX_FTM_TX, 0x1111111);
		rtw89_phy_write32_mask(rtwdev, R_P0_RFMODE_FTM_RX,
				       B_P0_RFMODE_FTM_RX, 0x111);
		rtw89_phy_write32_mask(rtwdev, R_P1_RFMODE,
				       B_P1_RFMODE_ORI_TXRX_FTM_TX, 0x1233312);
		rtw89_phy_write32_mask(rtwdev, R_P1_RFMODE_FTM_RX,
				       B_P1_RFMODE_FTM_RX, 0x333);
	} else if (rx_path == RF_AB) {
		rtw89_phy_write32_mask(rtwdev, R_P0_RFMODE,
				       B_P0_RFMODE_ORI_TXRX_FTM_TX, 0x1233312);
		rtw89_phy_write32_mask(rtwdev, R_P0_RFMODE_FTM_RX,
				       B_P0_RFMODE_FTM_RX, 0x333);
		rtw89_phy_write32_mask(rtwdev, R_P1_RFMODE,
				       B_P1_RFMODE_ORI_TXRX_FTM_TX, 0x1233312);
		rtw89_phy_write32_mask(rtwdev, R_P1_RFMODE_FTM_RX,
				       B_P1_RFMODE_FTM_RX, 0x333);
	}
}

static void __rtw8852bx_bb_cfg_txrx_path(struct rtw89_dev *rtwdev)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_CHANCTX_0);
	enum rtw89_rf_path_bit rx_path = hal->antenna_rx ? hal->antenna_rx : RF_AB;

	rtw8852bx_bb_ctrl_rx_path(rtwdev, rx_path, chan);
	rtw8852bx_bb_ctrl_rf_mode_rx_path(rtwdev, rx_path);

	if (rtwdev->hal.rx_nss == 1) {
		rtw89_phy_write32_mask(rtwdev, R_RXHT_MCS_LIMIT, B_RXHT_MCS_LIMIT, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXVHT_MCS_LIMIT, B_RXVHT_MCS_LIMIT, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_MAX_NSS, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHETB_MAX_NSS, 0);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_RXHT_MCS_LIMIT, B_RXHT_MCS_LIMIT, 1);
		rtw89_phy_write32_mask(rtwdev, R_RXVHT_MCS_LIMIT, B_RXVHT_MCS_LIMIT, 1);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHE_MAX_NSS, 1);
		rtw89_phy_write32_mask(rtwdev, R_RXHE, B_RXHETB_MAX_NSS, 1);
	}

	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_MOD, 0x0, RTW89_PHY_0);
}

static u8 __rtw8852bx_get_thermal(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path)
{
	if (rtwdev->is_tssi_mode[rf_path]) {
		u32 addr = 0x1c10 + (rf_path << 13);

		return rtw89_phy_read32_mask(rtwdev, addr, 0x3F000000);
	}

	rtw89_write_rf(rtwdev, rf_path, RR_TM, RR_TM_TRI, 0x1);
	rtw89_write_rf(rtwdev, rf_path, RR_TM, RR_TM_TRI, 0x0);
	rtw89_write_rf(rtwdev, rf_path, RR_TM, RR_TM_TRI, 0x1);

	fsleep(200);

	return rtw89_read_rf(rtwdev, rf_path, RR_TM, RR_TM_VAL);
}

static
void rtw8852bx_set_trx_mask(struct rtw89_dev *rtwdev, u8 path, u8 group, u32 val)
{
	rtw89_write_rf(rtwdev, path, RR_LUTWE, RFREG_MASK, 0x20000);
	rtw89_write_rf(rtwdev, path, RR_LUTWA, RFREG_MASK, group);
	rtw89_write_rf(rtwdev, path, RR_LUTWD0, RFREG_MASK, val);
	rtw89_write_rf(rtwdev, path, RR_LUTWE, RFREG_MASK, 0x0);
}

static void __rtw8852bx_btc_init_cfg(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_mac_ax_coex coex_params = {
		.pta_mode = RTW89_MAC_AX_COEX_RTK_MODE,
		.direction = RTW89_MAC_AX_COEX_INNER,
	};

	/* PTA init  */
	rtw89_mac_coex_init(rtwdev, &coex_params);

	/* set WL Tx response = Hi-Pri */
	chip->ops->btc_set_wl_pri(rtwdev, BTC_PRI_MASK_TX_RESP, true);
	chip->ops->btc_set_wl_pri(rtwdev, BTC_PRI_MASK_BEACON, true);

	/* set rf gnt debug off */
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_WLSEL, RFREG_MASK, 0x0);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_WLSEL, RFREG_MASK, 0x0);

	/* set WL Tx thru in TRX mask table if GNT_WL = 0 && BT_S1 = ss group */
	if (btc->ant_type == BTC_ANT_SHARED) {
		rtw8852bx_set_trx_mask(rtwdev, RF_PATH_A, BTC_BT_SS_GROUP, 0x5ff);
		rtw8852bx_set_trx_mask(rtwdev, RF_PATH_B, BTC_BT_SS_GROUP, 0x5ff);
		/* set path-A(S0) Tx/Rx no-mask if GNT_WL=0 && BT_S1=tx group */
		rtw8852bx_set_trx_mask(rtwdev, RF_PATH_A, BTC_BT_TX_GROUP, 0x5ff);
		rtw8852bx_set_trx_mask(rtwdev, RF_PATH_B, BTC_BT_TX_GROUP, 0x55f);
	} else { /* set WL Tx stb if GNT_WL = 0 && BT_S1 = ss group for 3-ant */
		rtw8852bx_set_trx_mask(rtwdev, RF_PATH_A, BTC_BT_SS_GROUP, 0x5df);
		rtw8852bx_set_trx_mask(rtwdev, RF_PATH_B, BTC_BT_SS_GROUP, 0x5df);
		rtw8852bx_set_trx_mask(rtwdev, RF_PATH_A, BTC_BT_TX_GROUP, 0x5ff);
		rtw8852bx_set_trx_mask(rtwdev, RF_PATH_B, BTC_BT_TX_GROUP, 0x5ff);
	}

	if (rtwdev->chip->chip_id == RTL8852BT) {
		rtw8852bx_set_trx_mask(rtwdev, RF_PATH_A, BTC_BT_RX_GROUP, 0x5df);
		rtw8852bx_set_trx_mask(rtwdev, RF_PATH_B, BTC_BT_RX_GROUP, 0x5df);
	}

	/* set PTA break table */
	rtw89_write32(rtwdev, R_BTC_BREAK_TABLE, BTC_BREAK_PARAM);

	 /* enable BT counter 0xda40[16,2] = 2b'11 */
	rtw89_write32_set(rtwdev, R_AX_CSR_MODE, B_AX_BT_CNT_RST | B_AX_STATIS_BT_EN);
	btc->cx.wl.status.map.init_ok = true;
}

static
void __rtw8852bx_btc_set_wl_pri(struct rtw89_dev *rtwdev, u8 map, bool state)
{
	u32 bitmap;
	u32 reg;

	switch (map) {
	case BTC_PRI_MASK_TX_RESP:
		reg = R_BTC_BT_COEX_MSK_TABLE;
		bitmap = B_BTC_PRI_MASK_TX_RESP_V1;
		break;
	case BTC_PRI_MASK_BEACON:
		reg = R_AX_WL_PRI_MSK;
		bitmap = B_AX_PTA_WL_PRI_MASK_BCNQ;
		break;
	case BTC_PRI_MASK_RX_CCK:
		reg = R_BTC_BT_COEX_MSK_TABLE;
		bitmap = B_BTC_PRI_MASK_RXCCK_V1;
		break;
	default:
		return;
	}

	if (state)
		rtw89_write32_set(rtwdev, reg, bitmap);
	else
		rtw89_write32_clr(rtwdev, reg, bitmap);
}

static
s8 __rtw8852bx_btc_get_bt_rssi(struct rtw89_dev *rtwdev, s8 val)
{
	/* +6 for compensate offset */
	return clamp_t(s8, val + 6, -100, 0) + 100;
}

static
void __rtw8852bx_btc_update_bt_cnt(struct rtw89_dev *rtwdev)
{
	/* Feature move to firmware */
}

static void __rtw8852bx_btc_wl_s1_standby(struct rtw89_dev *rtwdev, bool state)
{
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWE, RFREG_MASK, 0x80000);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWA, RFREG_MASK, 0x1);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD1, RFREG_MASK, 0x31);

	/* set WL standby = Rx for GNT_BT_Tx = 1->0 settle issue */
	if (state)
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0, RFREG_MASK, 0x179);
	else
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0, RFREG_MASK, 0x20);

	rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWE, RFREG_MASK, 0x0);
}

static void rtw8852bx_btc_set_wl_lna2(struct rtw89_dev *rtwdev, u8 level)
{
	switch (level) {
	case 0: /* default */
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWE, RFREG_MASK, 0x1000);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWA, RFREG_MASK, 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0, RFREG_MASK, 0x15);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWA, RFREG_MASK, 0x1);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0, RFREG_MASK, 0x17);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWA, RFREG_MASK, 0x2);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0, RFREG_MASK, 0x15);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWA, RFREG_MASK, 0x3);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0, RFREG_MASK, 0x17);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWE, RFREG_MASK, 0x0);
		break;
	case 1: /* Fix LNA2=5  */
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWE, RFREG_MASK, 0x1000);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWA, RFREG_MASK, 0x0);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0, RFREG_MASK, 0x15);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWA, RFREG_MASK, 0x1);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0, RFREG_MASK, 0x5);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWA, RFREG_MASK, 0x2);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0, RFREG_MASK, 0x15);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWA, RFREG_MASK, 0x3);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0, RFREG_MASK, 0x5);
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWE, RFREG_MASK, 0x0);
		break;
	}
}

static void __rtw8852bx_btc_set_wl_rx_gain(struct rtw89_dev *rtwdev, u32 level)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	switch (level) {
	case 0: /* original */
	default:
		rtw8852bx_ctrl_nbtg_bt_tx(rtwdev, false, RTW89_PHY_0);
		btc->dm.wl_lna2 = 0;
		break;
	case 1: /* for FDD free-run */
		rtw8852bx_ctrl_nbtg_bt_tx(rtwdev, true, RTW89_PHY_0);
		btc->dm.wl_lna2 = 0;
		break;
	case 2: /* for BTG Co-Rx*/
		rtw8852bx_ctrl_nbtg_bt_tx(rtwdev, false, RTW89_PHY_0);
		btc->dm.wl_lna2 = 1;
		break;
	}

	rtw8852bx_btc_set_wl_lna2(rtwdev, btc->dm.wl_lna2);
}

static void rtw8852bx_fill_freq_with_ppdu(struct rtw89_dev *rtwdev,
					  struct rtw89_rx_phy_ppdu *phy_ppdu,
					  struct ieee80211_rx_status *status)
{
	u16 chan = phy_ppdu->chan_idx;
	enum nl80211_band band;
	u8 ch;

	if (chan == 0)
		return;

	rtw89_decode_chan_idx(rtwdev, chan, &ch, &band);
	status->freq = ieee80211_channel_to_frequency(ch, band);
	status->band = band;
}

static void __rtw8852bx_query_ppdu(struct rtw89_dev *rtwdev,
				   struct rtw89_rx_phy_ppdu *phy_ppdu,
				   struct ieee80211_rx_status *status)
{
	u8 path;
	u8 *rx_power = phy_ppdu->rssi;

	status->signal = RTW89_RSSI_RAW_TO_DBM(max(rx_power[RF_PATH_A], rx_power[RF_PATH_B]));
	for (path = 0; path < rtwdev->chip->rf_path_num; path++) {
		status->chains |= BIT(path);
		status->chain_signal[path] = RTW89_RSSI_RAW_TO_DBM(rx_power[path]);
	}
	if (phy_ppdu->valid)
		rtw8852bx_fill_freq_with_ppdu(rtwdev, phy_ppdu, status);
}

static void __rtw8852bx_convert_rpl_to_rssi(struct rtw89_dev *rtwdev,
					    struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	u8 delta = phy_ppdu->rpl_avg - phy_ppdu->rssi_avg;
	u8 *rssi = phy_ppdu->rssi;
	u8 i;

	for (i = 0; i < RF_PATH_NUM_8852BX; i++)
		rssi[i] += delta;

	phy_ppdu->rssi_avg = phy_ppdu->rpl_avg;
}

static int __rtw8852bx_mac_enable_bb_rf(struct rtw89_dev *rtwdev)
{
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;
	u32 val32;
	int ret;

	rtw89_write8_set(rtwdev, R_AX_SYS_FUNC_EN,
			 B_AX_FEN_BBRSTB | B_AX_FEN_BB_GLB_RSTN);
	rtw89_write32_mask(rtwdev, R_AX_SPS_DIG_ON_CTRL0, B_AX_REG_ZCDC_H_MASK, 0x1);
	rtw89_write32_set(rtwdev, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
	rtw89_write32_clr(rtwdev, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
	rtw89_write32_set(rtwdev, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);

	if (chip_id == RTL8852BT) {
		val32 = rtw89_read32(rtwdev, R_AX_AFE_OFF_CTRL1);
		val32 = u32_replace_bits(val32, 0x1, B_AX_S0_LDO_VSEL_F_MASK);
		val32 = u32_replace_bits(val32, 0x1, B_AX_S1_LDO_VSEL_F_MASK);
		rtw89_write32(rtwdev, R_AX_AFE_OFF_CTRL1, val32);
	}

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S0, 0xC7,
				      FULL_BIT_MASK);
	if (ret)
		return ret;

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S1, 0xC7,
				      FULL_BIT_MASK);
	if (ret)
		return ret;

	rtw89_write8(rtwdev, R_AX_PHYREG_SET, PHYREG_SET_XYN_CYCLE);

	return 0;
}

static int __rtw8852bx_mac_disable_bb_rf(struct rtw89_dev *rtwdev)
{
	u8 wl_rfc_s0;
	u8 wl_rfc_s1;
	int ret;

	rtw89_write32_clr(rtwdev, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
	rtw89_write8_clr(rtwdev, R_AX_SYS_FUNC_EN,
			 B_AX_FEN_BBRSTB | B_AX_FEN_BB_GLB_RSTN);

	ret = rtw89_mac_read_xtal_si(rtwdev, XTAL_SI_WL_RFC_S0, &wl_rfc_s0);
	if (ret)
		return ret;
	wl_rfc_s0 &= ~XTAL_SI_RF00S_EN;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S0, wl_rfc_s0,
				      FULL_BIT_MASK);
	if (ret)
		return ret;

	ret = rtw89_mac_read_xtal_si(rtwdev, XTAL_SI_WL_RFC_S1, &wl_rfc_s1);
	if (ret)
		return ret;
	wl_rfc_s1 &= ~XTAL_SI_RF10S_EN;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S1, wl_rfc_s1,
				      FULL_BIT_MASK);
	return ret;
}

const struct rtw8852bx_info rtw8852bx_info = {
	.mac_enable_bb_rf = __rtw8852bx_mac_enable_bb_rf,
	.mac_disable_bb_rf = __rtw8852bx_mac_disable_bb_rf,
	.bb_sethw = __rtw8852bx_bb_sethw,
	.bb_reset_all = __rtw8852bx_bb_reset_all,
	.bb_cfg_txrx_path = __rtw8852bx_bb_cfg_txrx_path,
	.bb_cfg_tx_path = __rtw8852bx_bb_cfg_tx_path,
	.bb_ctrl_rx_path = __rtw8852bx_bb_ctrl_rx_path,
	.bb_set_plcp_tx = __rtw8852bx_bb_set_plcp_tx,
	.bb_set_power = __rtw8852bx_bb_set_power,
	.bb_set_pmac_pkt_tx = __rtw8852bx_bb_set_pmac_pkt_tx,
	.bb_backup_tssi = __rtw8852bx_bb_backup_tssi,
	.bb_restore_tssi = __rtw8852bx_bb_restore_tssi,
	.bb_tx_mode_switch = __rtw8852bx_bb_tx_mode_switch,
	.set_channel_mac = __rtw8852bx_set_channel_mac,
	.set_channel_bb = __rtw8852bx_set_channel_bb,
	.ctrl_nbtg_bt_tx = __rtw8852bx_ctrl_nbtg_bt_tx,
	.ctrl_btg_bt_rx = __rtw8852bx_ctrl_btg_bt_rx,
	.query_ppdu = __rtw8852bx_query_ppdu,
	.convert_rpl_to_rssi = __rtw8852bx_convert_rpl_to_rssi,
	.read_efuse = __rtw8852bx_read_efuse,
	.read_phycap = __rtw8852bx_read_phycap,
	.power_trim = __rtw8852bx_power_trim,
	.set_txpwr = __rtw8852bx_set_txpwr,
	.set_txpwr_ctrl = __rtw8852bx_set_txpwr_ctrl,
	.init_txpwr_unit = __rtw8852bx_init_txpwr_unit,
	.set_txpwr_ul_tb_offset = __rtw8852bx_set_txpwr_ul_tb_offset,
	.get_thermal = __rtw8852bx_get_thermal,
	.adc_cfg = rtw8852bt_adc_cfg,
	.btc_init_cfg = __rtw8852bx_btc_init_cfg,
	.btc_set_wl_pri = __rtw8852bx_btc_set_wl_pri,
	.btc_get_bt_rssi = __rtw8852bx_btc_get_bt_rssi,
	.btc_update_bt_cnt = __rtw8852bx_btc_update_bt_cnt,
	.btc_wl_s1_standby = __rtw8852bx_btc_wl_s1_standby,
	.btc_set_wl_rx_gain = __rtw8852bx_btc_set_wl_rx_gain,
};
EXPORT_SYMBOL(rtw8852bx_info);

MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ax wireless 8852B common routines");
MODULE_LICENSE("Dual BSD/GPL");
