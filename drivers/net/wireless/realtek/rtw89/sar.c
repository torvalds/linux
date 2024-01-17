// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "acpi.h"
#include "debug.h"
#include "phy.h"
#include "reg.h"
#include "sar.h"

#define RTW89_TAS_FACTOR 2 /* unit: 0.25 dBm */
#define RTW89_TAS_DPR_GAP (1 << RTW89_TAS_FACTOR)
#define RTW89_TAS_DELTA (2 << RTW89_TAS_FACTOR)

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

static int rtw89_query_sar_config_common(struct rtw89_dev *rtwdev,
					 u32 center_freq, s32 *cfg)
{
	struct rtw89_sar_cfg_common *rtwsar = &rtwdev->sar.cfg_common;
	const struct rtw89_sar_span *span = NULL;
	enum rtw89_sar_subband subband_l, subband_h;
	int idx;

	if (center_freq >= RTW89_SAR_6GHZ_SPAN_HEAD) {
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
		    "center_freq %u: SAR subband {%u, %u}\n",
		    center_freq, subband_l, subband_h);

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

static s8 rtw89_txpwr_tas_to_sar(const struct rtw89_sar_handler *sar_hdl,
				 s8 cfg)
{
	const u8 fct = sar_hdl->txpwr_factor_sar;

	if (fct > RTW89_TAS_FACTOR)
		return cfg << (fct - RTW89_TAS_FACTOR);
	else
		return cfg >> (RTW89_TAS_FACTOR - fct);
}

static s8 rtw89_txpwr_sar_to_tas(const struct rtw89_sar_handler *sar_hdl,
				 s8 cfg)
{
	const u8 fct = sar_hdl->txpwr_factor_sar;

	if (fct > RTW89_TAS_FACTOR)
		return cfg >> (fct - RTW89_TAS_FACTOR);
	else
		return cfg << (RTW89_TAS_FACTOR - fct);
}

s8 rtw89_query_sar(struct rtw89_dev *rtwdev, u32 center_freq)
{
	const enum rtw89_sar_sources src = rtwdev->sar.src;
	/* its members are protected by rtw89_sar_set_src() */
	const struct rtw89_sar_handler *sar_hdl = &rtw89_sar_handlers[src];
	struct rtw89_tas_info *tas = &rtwdev->tas;
	s8 delta;
	int ret;
	s32 cfg;
	u8 fct;

	lockdep_assert_held(&rtwdev->mutex);

	if (src == RTW89_SAR_SOURCE_NONE)
		return RTW89_SAR_TXPWR_MAC_MAX;

	ret = sar_hdl->query_sar_config(rtwdev, center_freq, &cfg);
	if (ret)
		return RTW89_SAR_TXPWR_MAC_MAX;

	if (tas->enable) {
		switch (tas->state) {
		case RTW89_TAS_STATE_DPR_OFF:
			return RTW89_SAR_TXPWR_MAC_MAX;
		case RTW89_TAS_STATE_DPR_ON:
			delta = rtw89_txpwr_tas_to_sar(sar_hdl, tas->delta);
			cfg -= delta;
			break;
		case RTW89_TAS_STATE_DPR_FORBID:
		default:
			break;
		}
	}

	fct = sar_hdl->txpwr_factor_sar;

	return rtw89_txpwr_sar_to_mac(rtwdev, fct, cfg);
}

void rtw89_print_sar(struct seq_file *m, struct rtw89_dev *rtwdev, u32 center_freq)
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

	ret = sar_hdl->query_sar_config(rtwdev, center_freq, &cfg);
	if (ret) {
		seq_printf(m, "config: return code: %d\n", ret);
		seq_printf(m, "assign: max setting: %d (unit: 1/%lu dBm)\n",
			   RTW89_SAR_TXPWR_MAC_MAX, BIT(fct_mac));
		return;
	}

	fct = sar_hdl->txpwr_factor_sar;

	seq_printf(m, "config: %d (unit: 1/%lu dBm)\n", cfg, BIT(fct));
}

void rtw89_print_tas(struct seq_file *m, struct rtw89_dev *rtwdev)
{
	struct rtw89_tas_info *tas = &rtwdev->tas;

	if (!tas->enable) {
		seq_puts(m, "no TAS is applied\n");
		return;
	}

	seq_printf(m, "DPR gap: %d\n", tas->dpr_gap);
	seq_printf(m, "TAS delta: %d\n", tas->delta);
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
	rtw89_core_set_chip_txpwr(rtwdev);

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

static void rtw89_tas_state_update(struct rtw89_dev *rtwdev)
{
	const enum rtw89_sar_sources src = rtwdev->sar.src;
	/* its members are protected by rtw89_sar_set_src() */
	const struct rtw89_sar_handler *sar_hdl = &rtw89_sar_handlers[src];
	struct rtw89_tas_info *tas = &rtwdev->tas;
	s32 txpwr_avg = tas->total_txpwr / RTW89_TAS_MAX_WINDOW / PERCENT;
	s32 dpr_on_threshold, dpr_off_threshold, cfg;
	enum rtw89_tas_state state = tas->state;
	const struct rtw89_chan *chan;
	int ret;

	lockdep_assert_held(&rtwdev->mutex);

	if (src == RTW89_SAR_SOURCE_NONE)
		return;

	chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	ret = sar_hdl->query_sar_config(rtwdev, chan->freq, &cfg);
	if (ret)
		return;

	cfg = rtw89_txpwr_sar_to_tas(sar_hdl, cfg);

	if (tas->delta >= cfg) {
		rtw89_debug(rtwdev, RTW89_DBG_SAR,
			    "TAS delta exceed SAR limit\n");
		state = RTW89_TAS_STATE_DPR_FORBID;
		goto out;
	}

	dpr_on_threshold = cfg;
	dpr_off_threshold = cfg - tas->dpr_gap;
	rtw89_debug(rtwdev, RTW89_DBG_SAR,
		    "DPR_ON thold: %d, DPR_OFF thold: %d, txpwr_avg: %d\n",
		    dpr_on_threshold, dpr_off_threshold, txpwr_avg);

	if (txpwr_avg >= dpr_on_threshold)
		state = RTW89_TAS_STATE_DPR_ON;
	else if (txpwr_avg < dpr_off_threshold)
		state = RTW89_TAS_STATE_DPR_OFF;

out:
	if (tas->state == state)
		return;

	rtw89_debug(rtwdev, RTW89_DBG_SAR,
		    "TAS old state: %d, new state: %d\n", tas->state, state);
	tas->state = state;
	rtw89_core_set_chip_txpwr(rtwdev);
}

void rtw89_tas_init(struct rtw89_dev *rtwdev)
{
	struct rtw89_tas_info *tas = &rtwdev->tas;
	struct rtw89_acpi_dsm_result res = {};
	int ret;
	u8 val;

	ret = rtw89_acpi_evaluate_dsm(rtwdev, RTW89_ACPI_DSM_FUNC_TAS_EN, &res);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_SAR,
			    "acpi: cannot get TAS: %d\n", ret);
		return;
	}

	val = res.u.value;
	switch (val) {
	case 0:
		tas->enable = false;
		break;
	case 1:
		tas->enable = true;
		break;
	default:
		break;
	}

	if (!tas->enable) {
		rtw89_debug(rtwdev, RTW89_DBG_SAR, "TAS not enable\n");
		return;
	}

	tas->dpr_gap = RTW89_TAS_DPR_GAP;
	tas->delta = RTW89_TAS_DELTA;
}

void rtw89_tas_reset(struct rtw89_dev *rtwdev)
{
	struct rtw89_tas_info *tas = &rtwdev->tas;

	if (!tas->enable)
		return;

	memset(&tas->txpwr_history, 0, sizeof(tas->txpwr_history));
	tas->total_txpwr = 0;
	tas->cur_idx = 0;
	tas->state = RTW89_TAS_STATE_DPR_OFF;
}

static const struct rtw89_reg_def txpwr_regs[] = {
	{R_PATH0_TXPWR, B_PATH0_TXPWR},
	{R_PATH1_TXPWR, B_PATH1_TXPWR},
};

void rtw89_tas_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_env_monitor_info *env = &rtwdev->env_monitor;
	const enum rtw89_sar_sources src = rtwdev->sar.src;
	u8 max_nss_num = rtwdev->chip->rf_path_num;
	struct rtw89_tas_info *tas = &rtwdev->tas;
	s16 tmp, txpwr, instant_txpwr = 0;
	u32 val;
	int i;

	if (!tas->enable || src == RTW89_SAR_SOURCE_NONE)
		return;

	if (env->ccx_watchdog_result != RTW89_PHY_ENV_MON_IFS_CLM)
		return;

	for (i = 0; i < max_nss_num; i++) {
		val = rtw89_phy_read32_mask(rtwdev, txpwr_regs[i].addr,
					    txpwr_regs[i].mask);
		tmp = sign_extend32(val, 8);
		if (tmp <= 0)
			return;
		instant_txpwr += tmp;
	}

	instant_txpwr /= max_nss_num;
	/* in unit of 0.25 dBm multiply by percentage */
	txpwr = instant_txpwr * env->ifs_clm_tx_ratio;
	tas->total_txpwr += txpwr - tas->txpwr_history[tas->cur_idx];
	tas->txpwr_history[tas->cur_idx] = txpwr;
	rtw89_debug(rtwdev, RTW89_DBG_SAR,
		    "instant_txpwr: %d, tx_ratio: %d, txpwr: %d\n",
		    instant_txpwr, env->ifs_clm_tx_ratio, txpwr);

	tas->cur_idx = (tas->cur_idx + 1) % RTW89_TAS_MAX_WINDOW;

	rtw89_tas_state_update(rtwdev);
}
