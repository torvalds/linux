// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "debug.h"
#include "sar.h"

static int rtw89_query_sar_config_common(struct rtw89_dev *rtwdev, s32 *cfg)
{
	struct rtw89_sar_cfg_common *rtwsar = &rtwdev->sar.cfg_common;
	enum rtw89_subband subband = rtwdev->hal.current_subband;

	if (!rtwsar->set[subband])
		return -ENODATA;

	*cfg = rtwsar->cfg[subband];
	return 0;
}

static const
struct rtw89_sar_handler rtw89_sar_handlers[RTW89_SAR_SOURCE_NR] = {
	[RTW89_SAR_SOURCE_COMMON] = {
		.descr_sar_source = "RTW89_SAR_SOURCE_COMMON",
		.txpwr_factor_sar = 2,
		.query_sar_config = rtw89_query_sar_config_common,
	},
};

#define rtw89_sar_set_src(_dev, _src, _cfg_name, _cfg_data)		\
	do {								\
		typeof(_src) _s = (_src);				\
		typeof(_dev) _d = (_dev);				\
		BUILD_BUG_ON(!rtw89_sar_handlers[_s].descr_sar_source);	\
		BUILD_BUG_ON(!rtw89_sar_handlers[_s].query_sar_config);	\
		lockdep_assert_held(&_d->mutex);			\
		_d->sar._cfg_name = *(_cfg_data);			\
		_d->sar.src = _s;					\
	} while (0)

static s8 rtw89_txpwr_sar_to_mac(struct rtw89_dev *rtwdev, u8 fct, s32 cfg)
{
	const u8 fct_mac = rtwdev->chip->txpwr_factor_mac;
	s32 cfg_mac;

	cfg_mac = fct > fct_mac ?
		  cfg >> (fct - fct_mac) : cfg << (fct_mac - fct);

	return (s8)clamp_t(s32, cfg_mac,
			   RTW89_SAR_TXPWR_MAC_MIN,
			   RTW89_SAR_TXPWR_MAC_MAX);
}

s8 rtw89_query_sar(struct rtw89_dev *rtwdev)
{
	const enum rtw89_sar_sources src = rtwdev->sar.src;
	/* its members are protected by rtw89_sar_set_src() */
	const struct rtw89_sar_handler *sar_hdl = &rtw89_sar_handlers[src];
	int ret;
	s32 cfg;
	u8 fct;

	lockdep_assert_held(&rtwdev->mutex);

	if (src == RTW89_SAR_SOURCE_NONE)
		return RTW89_SAR_TXPWR_MAC_MAX;

	ret = sar_hdl->query_sar_config(rtwdev, &cfg);
	if (ret)
		return RTW89_SAR_TXPWR_MAC_MAX;

	fct = sar_hdl->txpwr_factor_sar;

	return rtw89_txpwr_sar_to_mac(rtwdev, fct, cfg);
}

void rtw89_print_sar(struct seq_file *m, struct rtw89_dev *rtwdev)
{
	const enum rtw89_sar_sources src = rtwdev->sar.src;
	/* its members are protected by rtw89_sar_set_src() */
	const struct rtw89_sar_handler *sar_hdl = &rtw89_sar_handlers[src];
	const u8 fct_mac = rtwdev->chip->txpwr_factor_mac;
	int ret;
	s32 cfg;
	u8 fct;

	lockdep_assert_held(&rtwdev->mutex);

	if (src == RTW89_SAR_SOURCE_NONE) {
		seq_puts(m, "no SAR is applied\n");
		return;
	}

	seq_printf(m, "source: %d (%s)\n", src, sar_hdl->descr_sar_source);

	ret = sar_hdl->query_sar_config(rtwdev, &cfg);
	if (ret) {
		seq_printf(m, "config: return code: %d\n", ret);
		seq_printf(m, "assign: max setting: %d (unit: 1/%lu dBm)\n",
			   RTW89_SAR_TXPWR_MAC_MAX, BIT(fct_mac));
		return;
	}

	fct = sar_hdl->txpwr_factor_sar;

	seq_printf(m, "config: %d (unit: 1/%lu dBm)\n", cfg, BIT(fct));
}

static int rtw89_apply_sar_common(struct rtw89_dev *rtwdev,
				  const struct rtw89_sar_cfg_common *sar)
{
	enum rtw89_sar_sources src;
	int ret = 0;

	mutex_lock(&rtwdev->mutex);

	src = rtwdev->sar.src;
	if (src != RTW89_SAR_SOURCE_NONE && src != RTW89_SAR_SOURCE_COMMON) {
		rtw89_warn(rtwdev, "SAR source: %d is in use", src);
		ret = -EBUSY;
		goto exit;
	}

	rtw89_sar_set_src(rtwdev, RTW89_SAR_SOURCE_COMMON, cfg_common, sar);
	rtw89_chip_set_txpwr(rtwdev);

exit:
	mutex_unlock(&rtwdev->mutex);
	return ret;
}

static const u8 rtw89_common_sar_subband_map[] = {
	RTW89_CH_2G,
	RTW89_CH_5G_BAND_1,
	RTW89_CH_5G_BAND_3,
	RTW89_CH_5G_BAND_4,
};

static const struct cfg80211_sar_freq_ranges rtw89_common_sar_freq_ranges[] = {
	{ .start_freq = 2412, .end_freq = 2484, },
	{ .start_freq = 5180, .end_freq = 5320, },
	{ .start_freq = 5500, .end_freq = 5720, },
	{ .start_freq = 5745, .end_freq = 5825, },
};

static_assert(ARRAY_SIZE(rtw89_common_sar_subband_map) ==
	      ARRAY_SIZE(rtw89_common_sar_freq_ranges));

const struct cfg80211_sar_capa rtw89_sar_capa = {
	.type = NL80211_SAR_TYPE_POWER,
	.num_freq_ranges = ARRAY_SIZE(rtw89_common_sar_freq_ranges),
	.freq_ranges = rtw89_common_sar_freq_ranges,
};

int rtw89_ops_set_sar_specs(struct ieee80211_hw *hw,
			    const struct cfg80211_sar_specs *sar)
{
	struct rtw89_dev *rtwdev = hw->priv;
	struct rtw89_sar_cfg_common sar_common = {0};
	u8 fct;
	u32 freq_start;
	u32 freq_end;
	u32 band;
	s32 power;
	u32 i, idx;

	if (sar->type != NL80211_SAR_TYPE_POWER)
		return -EINVAL;

	fct = rtw89_sar_handlers[RTW89_SAR_SOURCE_COMMON].txpwr_factor_sar;

	for (i = 0; i < sar->num_sub_specs; i++) {
		idx = sar->sub_specs[i].freq_range_index;
		if (idx >= ARRAY_SIZE(rtw89_common_sar_freq_ranges))
			return -EINVAL;

		freq_start = rtw89_common_sar_freq_ranges[idx].start_freq;
		freq_end = rtw89_common_sar_freq_ranges[idx].end_freq;
		band = rtw89_common_sar_subband_map[idx];
		power = sar->sub_specs[i].power;

		rtw89_info(rtwdev, "On freq %u to %u, ", freq_start, freq_end);
		rtw89_info(rtwdev, "set SAR power limit %d (unit: 1/%lu dBm)\n",
			   power, BIT(fct));

		sar_common.set[band] = true;
		sar_common.cfg[band] = power;
	}

	return rtw89_apply_sar_common(rtwdev, &sar_common);
}
