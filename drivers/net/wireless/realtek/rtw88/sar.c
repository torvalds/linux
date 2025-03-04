// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2021  Realtek Corporation
 */

#include "sar.h"
#include "phy.h"
#include "debug.h"

s8 rtw_query_sar(struct rtw_dev *rtwdev, const struct rtw_sar_arg *arg)
{
	const struct rtw_hal *hal = &rtwdev->hal;
	const struct rtw_sar *sar = &hal->sar;

	switch (sar->src) {
	default:
		rtw_warn(rtwdev, "unknown SAR source: %d\n", sar->src);
		fallthrough;
	case RTW_SAR_SOURCE_NONE:
		return (s8)rtwdev->chip->max_power_index;
	case RTW_SAR_SOURCE_COMMON:
		return sar->cfg[arg->path][arg->rs].common[arg->sar_band];
	}
}

static int rtw_apply_sar(struct rtw_dev *rtwdev, const struct rtw_sar *new)
{
	struct rtw_hal *hal = &rtwdev->hal;
	struct rtw_sar *sar = &hal->sar;

	if (sar->src != RTW_SAR_SOURCE_NONE && new->src != sar->src) {
		rtw_warn(rtwdev, "SAR source: %d is in use\n", sar->src);
		return -EBUSY;
	}

	*sar = *new;
	rtw_phy_set_tx_power_level(rtwdev, hal->current_channel);

	return 0;
}

static s8 rtw_sar_to_phy(struct rtw_dev *rtwdev, u8 fct, s32 sar,
			 const struct rtw_sar_arg *arg)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u8 txgi = rtwdev->chip->txgi_factor;
	u8 max = rtwdev->chip->max_power_index;
	s32 tmp;
	s8 base;

	tmp = fct > txgi ? sar >> (fct - txgi) : sar << (txgi - fct);
	base = arg->sar_band == RTW_SAR_BAND_0 ?
	       hal->tx_pwr_by_rate_base_2g[arg->path][arg->rs] :
	       hal->tx_pwr_by_rate_base_5g[arg->path][arg->rs];

	return (s8)clamp_t(s32, tmp, -max - 1, max) - base;
}

static const struct cfg80211_sar_freq_ranges rtw_common_sar_freq_ranges[] = {
	[RTW_SAR_BAND_0] = { .start_freq = 2412, .end_freq = 2484, },
	[RTW_SAR_BAND_1] = { .start_freq = 5180, .end_freq = 5320, },
	[RTW_SAR_BAND_3] = { .start_freq = 5500, .end_freq = 5720, },
	[RTW_SAR_BAND_4] = { .start_freq = 5745, .end_freq = 5825, },
};

static_assert(ARRAY_SIZE(rtw_common_sar_freq_ranges) == RTW_SAR_BAND_NR);

const struct cfg80211_sar_capa rtw_sar_capa = {
	.type = NL80211_SAR_TYPE_POWER,
	.num_freq_ranges = RTW_SAR_BAND_NR,
	.freq_ranges = rtw_common_sar_freq_ranges,
};

int rtw_set_sar_specs(struct rtw_dev *rtwdev,
		      const struct cfg80211_sar_specs *sar)
{
	struct rtw_sar_arg arg = {0};
	struct rtw_sar new = {0};
	u32 idx, i, j, k;
	s32 power;
	s8 val;

	if (sar->type != NL80211_SAR_TYPE_POWER)
		return -EINVAL;

	memset(&new, rtwdev->chip->max_power_index, sizeof(new));
	new.src = RTW_SAR_SOURCE_COMMON;

	for (i = 0; i < sar->num_sub_specs; i++) {
		idx = sar->sub_specs[i].freq_range_index;
		if (idx >= RTW_SAR_BAND_NR)
			return -EINVAL;

		power = sar->sub_specs[i].power;
		rtw_dbg(rtwdev, RTW_DBG_REGD, "On freq %u to %u, set SAR %d in 1/%lu dBm\n",
			rtw_common_sar_freq_ranges[idx].start_freq,
			rtw_common_sar_freq_ranges[idx].end_freq,
			power, BIT(RTW_COMMON_SAR_FCT));

		for (j = 0; j < RTW_RF_PATH_MAX; j++) {
			for (k = 0; k < RTW_RATE_SECTION_NUM; k++) {
				arg = (struct rtw_sar_arg){
					.sar_band = idx,
					.path = j,
					.rs = k,
				};
				val = rtw_sar_to_phy(rtwdev, RTW_COMMON_SAR_FCT,
						     power, &arg);
				new.cfg[j][k].common[idx] = val;
			}
		}
	}

	return rtw_apply_sar(rtwdev, &new);
}
