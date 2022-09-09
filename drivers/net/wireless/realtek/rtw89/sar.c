// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "debug.h"
#include "sar.h"

static enum rtw89_sar_subband rtw89_sar_get_subband(struct rtw89_dev *rtwdev,
						    u32 center_freq)
{
	switch (center_freq) {
	default:
		rtw89_debug(rtwdev, RTW89_DBG_SAR,
			    "center freq: %u to SAR subband is unhandled\n",
			    center_freq);
		fallthrough;
	case 2412 ... 2484:
		return RTW89_SAR_2GHZ_SUBBAND;
	case 5180 ... 5320:
		return RTW89_SAR_5GHZ_SUBBAND_1_2;
	case 5500 ... 5720:
		return RTW89_SAR_5GHZ_SUBBAND_2_E;
	case 5745 ... 5825:
		return RTW89_SAR_5GHZ_SUBBAND_3;
	case 5955 ... 6155:
		return RTW89_SAR_6GHZ_SUBBAND_5_L;
	case 6175 ... 6415:
		return RTW89_SAR_6GHZ_SUBBAND_5_H;
	case 6435 ... 6515:
		return RTW89_SAR_6GHZ_SUBBAND_6;
	case 6535 ... 6695:
		return RTW89_SAR_6GHZ_SUBBAND_7_L;
	case 6715 ... 6855:
		return RTW89_SAR_6GHZ_SUBBAND_7_H;

	/* freq 6875 (ch 185, 20MHz) spans RTW89_SAR_6GHZ_SUBBAND_7_H
	 * and RTW89_SAR_6GHZ_SUBBAND_8, so directly describe it with
	 * struct rtw89_sar_span in the following.
	 */

	case 6895 ... 7115:
		return RTW89_SAR_6GHZ_SUBBAND_8;
	}
}

struct rtw89_sar_span {
	enum rtw89_sar_subband subband_low;
	enum rtw89_sar_subband subband_high;
};

#define RTW89_SAR_SPAN_VALID(span) ((span)->subband_high)

#define RTW89_SAR_6GHZ_SPAN_HEAD 6145
#define RTW89_SAR_6GHZ_SPAN_IDX(center_freq) \
	((((int)(center_freq) - RTW89_SAR_6GHZ_SPAN_HEAD) / 5) / 2)

#define RTW89_DECL_SAR_6GHZ_SPAN(center_freq, subband_l, subband_h) \
	[RTW89_SAR_6GHZ_SPAN_IDX(center_freq)] = { \
		.subband_low = RTW89_SAR_6GHZ_ ## subband_l, \
		.subband_high = RTW89_SAR_6GHZ_ ## subband_h, \
	}

/* Since 6GHz SAR subbands are not edge aligned, some cases span two SAR
 * subbands. In the following, we describe each of them with rtw89_sar_span.
 */
static const struct rtw89_sar_span rtw89_sar_overlapping_6ghz[] = {
	RTW89_DECL_SAR_6GHZ_SPAN(6145, SUBBAND_5_L, SUBBAND_5_H),
	RTW89_DECL_SAR_6GHZ_SPAN(6165, SUBBAND_5_L, SUBBAND_5_H),
	RTW89_DECL_SAR_6GHZ_SPAN(6185, SUBBAND_5_L, SUBBAND_5_H),
	RTW89_DECL_SAR_6GHZ_SPAN(6505, SUBBAND_6, SUBBAND_7_L),
	RTW89_DECL_SAR_6GHZ_SPAN(6525, SUBBAND_6, SUBBAND_7_L),
	RTW89_DECL_SAR_6GHZ_SPAN(6545, SUBBAND_6, SUBBAND_7_L),
	RTW89_DECL_SAR_6GHZ_SPAN(6665, SUBBAND_7_L, SUBBAND_7_H),
	RTW89_DECL_SAR_6GHZ_SPAN(6705, SUBBAND_7_L, SUBBAND_7_H),
	RTW89_DECL_SAR_6GHZ_SPAN(6825, SUBBAND_7_H, SUBBAND_8),
	RTW89_DECL_SAR_6GHZ_SPAN(6865, SUBBAND_7_H, SUBBAND_8),
	RTW89_DECL_SAR_6GHZ_SPAN(6875, SUBBAND_7_H, SUBBAND_8),
	RTW89_DECL_SAR_6GHZ_SPAN(6885, SUBBAND_7_H, SUBBAND_8),
};

static int rtw89_query_sar_config_common(struct rtw89_dev *rtwdev, s32 *cfg)
{
	struct rtw89_sar_cfg_common *rtwsar = &rtwdev->sar.cfg_common;
	struct rtw89_hal *hal = &rtwdev->hal;
	enum rtw89_band band = hal->current_band_type;
	u32 center_freq = hal->current_freq;
	const struct rtw89_sar_span *span = NULL;
	enum rtw89_sar_subband subband_l, subband_h;
	int idx;

	if (band == RTW89_BAND_6G) {
		idx = RTW89_SAR_6GHZ_SPAN_IDX(center_freq);
		/* To decrease size of rtw89_sar_overlapping_6ghz[],
		 * RTW89_SAR_6GHZ_SPAN_IDX() truncates the leading NULLs
		 * to make first span as index 0 of the table. So, if center
		 * frequency is less than the first one, it will get netative.
		 */
		if (idx >= 0 && idx < ARRAY_SIZE(rtw89_sar_overlapping_6ghz))
			span = &rtw89_sar_overlapping_6ghz[idx];
	}

	if (span && RTW89_SAR_SPAN_VALID(span)) {
		subband_l = span->subband_low;
		subband_h = span->subband_high;
	} else {
		subband_l = rtw89_sar_get_subband(rtwdev, center_freq);
		subband_h = subband_l;
	}

	rtw89_debug(rtwdev, RTW89_DBG_SAR,
		    "for {band %u, center_freq %u}, SAR subband: {%u, %u}\n",
		    band, center_freq, subband_l, subband_h);

	if (!rtwsar->set[subband_l] && !rtwsar->set[subband_h])
		return -ENODATA;

	if (!rtwsar->set[subband_l])
		*cfg = rtwsar->cfg[subband_h];
	else if (!rtwsar->set[subband_h])
		*cfg = rtwsar->cfg[subband_l];
	else
		*cfg = min(rtwsar->cfg[subband_l], rtwsar->cfg[subband_h]);

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

static const struct cfg80211_sar_freq_ranges rtw89_common_sar_freq_ranges[] = {
	{ .start_freq = 2412, .end_freq = 2484, },
	{ .start_freq = 5180, .end_freq = 5320, },
	{ .start_freq = 5500, .end_freq = 5720, },
	{ .start_freq = 5745, .end_freq = 5825, },
	{ .start_freq = 5955, .end_freq = 6155, },
	{ .start_freq = 6175, .end_freq = 6415, },
	{ .start_freq = 6435, .end_freq = 6515, },
	{ .start_freq = 6535, .end_freq = 6695, },
	{ .start_freq = 6715, .end_freq = 6875, },
	{ .start_freq = 6875, .end_freq = 7115, },
};

static_assert(RTW89_SAR_SUBBAND_NR ==
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
		power = sar->sub_specs[i].power;

		rtw89_debug(rtwdev, RTW89_DBG_SAR,
			    "On freq %u to %u, set SAR limit %d (unit: 1/%lu dBm)\n",
			    freq_start, freq_end, power, BIT(fct));

		sar_common.set[idx] = true;
		sar_common.cfg[idx] = power;
	}

	return rtw89_apply_sar_common(rtwdev, &sar_common);
}
