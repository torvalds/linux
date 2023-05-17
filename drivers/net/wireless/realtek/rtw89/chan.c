// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2020-2022  Realtek Corporation
 */

#include "chan.h"
#include "debug.h"
#include "util.h"

static enum rtw89_subband rtw89_get_subband_type(enum rtw89_band band,
						 u8 center_chan)
{
	switch (band) {
	default:
	case RTW89_BAND_2G:
		switch (center_chan) {
		default:
		case 1 ... 14:
			return RTW89_CH_2G;
		}
	case RTW89_BAND_5G:
		switch (center_chan) {
		default:
		case 36 ... 64:
			return RTW89_CH_5G_BAND_1;
		case 100 ... 144:
			return RTW89_CH_5G_BAND_3;
		case 149 ... 177:
			return RTW89_CH_5G_BAND_4;
		}
	case RTW89_BAND_6G:
		switch (center_chan) {
		default:
		case 1 ... 29:
			return RTW89_CH_6G_BAND_IDX0;
		case 33 ... 61:
			return RTW89_CH_6G_BAND_IDX1;
		case 65 ... 93:
			return RTW89_CH_6G_BAND_IDX2;
		case 97 ... 125:
			return RTW89_CH_6G_BAND_IDX3;
		case 129 ... 157:
			return RTW89_CH_6G_BAND_IDX4;
		case 161 ... 189:
			return RTW89_CH_6G_BAND_IDX5;
		case 193 ... 221:
			return RTW89_CH_6G_BAND_IDX6;
		case 225 ... 253:
			return RTW89_CH_6G_BAND_IDX7;
		}
	}
}

static enum rtw89_sc_offset rtw89_get_primary_chan_idx(enum rtw89_bandwidth bw,
						       u32 center_freq,
						       u32 primary_freq)
{
	u8 primary_chan_idx;
	u32 offset;

	switch (bw) {
	default:
	case RTW89_CHANNEL_WIDTH_20:
		primary_chan_idx = RTW89_SC_DONT_CARE;
		break;
	case RTW89_CHANNEL_WIDTH_40:
		if (primary_freq > center_freq)
			primary_chan_idx = RTW89_SC_20_UPPER;
		else
			primary_chan_idx = RTW89_SC_20_LOWER;
		break;
	case RTW89_CHANNEL_WIDTH_80:
	case RTW89_CHANNEL_WIDTH_160:
		if (primary_freq > center_freq) {
			offset = (primary_freq - center_freq - 10) / 20;
			primary_chan_idx = RTW89_SC_20_UPPER + offset * 2;
		} else {
			offset = (center_freq - primary_freq - 10) / 20;
			primary_chan_idx = RTW89_SC_20_LOWER + offset * 2;
		}
		break;
	}

	return primary_chan_idx;
}

void rtw89_chan_create(struct rtw89_chan *chan, u8 center_chan, u8 primary_chan,
		       enum rtw89_band band, enum rtw89_bandwidth bandwidth)
{
	enum nl80211_band nl_band = rtw89_hw_to_nl80211_band(band);
	u32 center_freq, primary_freq;

	memset(chan, 0, sizeof(*chan));
	chan->channel = center_chan;
	chan->primary_channel = primary_chan;
	chan->band_type = band;
	chan->band_width = bandwidth;

	center_freq = ieee80211_channel_to_frequency(center_chan, nl_band);
	primary_freq = ieee80211_channel_to_frequency(primary_chan, nl_band);

	chan->freq = center_freq;
	chan->subband_type = rtw89_get_subband_type(band, center_chan);
	chan->pri_ch_idx = rtw89_get_primary_chan_idx(bandwidth, center_freq,
						      primary_freq);
}

bool rtw89_assign_entity_chan(struct rtw89_dev *rtwdev,
			      enum rtw89_sub_entity_idx idx,
			      const struct rtw89_chan *new)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_chan *chan = &hal->sub[idx].chan;
	struct rtw89_chan_rcd *rcd = &hal->sub[idx].rcd;
	bool band_changed;

	rcd->prev_primary_channel = chan->primary_channel;
	rcd->prev_band_type = chan->band_type;
	band_changed = new->band_type != chan->band_type;

	*chan = *new;
	return band_changed;
}

static void __rtw89_config_entity_chandef(struct rtw89_dev *rtwdev,
					  enum rtw89_sub_entity_idx idx,
					  const struct cfg80211_chan_def *chandef,
					  bool from_stack)
{
	struct rtw89_hal *hal = &rtwdev->hal;

	hal->sub[idx].chandef = *chandef;

	if (from_stack)
		set_bit(idx, hal->entity_map);
}

void rtw89_config_entity_chandef(struct rtw89_dev *rtwdev,
				 enum rtw89_sub_entity_idx idx,
				 const struct cfg80211_chan_def *chandef)
{
	__rtw89_config_entity_chandef(rtwdev, idx, chandef, true);
}

void rtw89_config_roc_chandef(struct rtw89_dev *rtwdev,
			      enum rtw89_sub_entity_idx idx,
			      const struct cfg80211_chan_def *chandef)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	enum rtw89_sub_entity_idx cur;

	if (chandef) {
		cur = atomic_cmpxchg(&hal->roc_entity_idx,
				     RTW89_SUB_ENTITY_IDLE, idx);
		if (cur != RTW89_SUB_ENTITY_IDLE) {
			rtw89_debug(rtwdev, RTW89_DBG_TXRX,
				    "ROC still processing on entity %d\n", idx);
			return;
		}

		hal->roc_chandef = *chandef;
	} else {
		cur = atomic_cmpxchg(&hal->roc_entity_idx, idx,
				     RTW89_SUB_ENTITY_IDLE);
		if (cur == idx)
			return;

		if (cur == RTW89_SUB_ENTITY_IDLE)
			rtw89_debug(rtwdev, RTW89_DBG_TXRX,
				    "ROC already finished on entity %d\n", idx);
		else
			rtw89_debug(rtwdev, RTW89_DBG_TXRX,
				    "ROC is processing on entity %d\n", cur);
	}
}

static void rtw89_config_default_chandef(struct rtw89_dev *rtwdev)
{
	struct cfg80211_chan_def chandef = {0};

	rtw89_get_default_chandef(&chandef);
	__rtw89_config_entity_chandef(rtwdev, RTW89_SUB_ENTITY_0, &chandef, false);
}

void rtw89_entity_init(struct rtw89_dev *rtwdev)
{
	struct rtw89_hal *hal = &rtwdev->hal;

	bitmap_zero(hal->entity_map, NUM_OF_RTW89_SUB_ENTITY);
	atomic_set(&hal->roc_entity_idx, RTW89_SUB_ENTITY_IDLE);
	rtw89_config_default_chandef(rtwdev);
}

enum rtw89_entity_mode rtw89_entity_recalc(struct rtw89_dev *rtwdev)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	enum rtw89_entity_mode mode;
	u8 weight;

	weight = bitmap_weight(hal->entity_map, NUM_OF_RTW89_SUB_ENTITY);
	switch (weight) {
	default:
		rtw89_warn(rtwdev, "unknown ent chan weight: %d\n", weight);
		bitmap_zero(hal->entity_map, NUM_OF_RTW89_SUB_ENTITY);
		fallthrough;
	case 0:
		rtw89_config_default_chandef(rtwdev);
		fallthrough;
	case 1:
		mode = RTW89_ENTITY_MODE_SCC;
		break;
	}

	rtw89_set_entity_mode(rtwdev, mode);
	return mode;
}

int rtw89_chanctx_ops_add(struct rtw89_dev *rtwdev,
			  struct ieee80211_chanctx_conf *ctx)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_chanctx_cfg *cfg = (struct rtw89_chanctx_cfg *)ctx->drv_priv;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u8 idx;

	idx = find_first_zero_bit(hal->entity_map, NUM_OF_RTW89_SUB_ENTITY);
	if (idx >= chip->support_chanctx_num)
		return -ENOENT;

	rtw89_config_entity_chandef(rtwdev, idx, &ctx->def);
	rtw89_set_channel(rtwdev);
	cfg->idx = idx;
	hal->sub[idx].cfg = cfg;
	return 0;
}

void rtw89_chanctx_ops_remove(struct rtw89_dev *rtwdev,
			      struct ieee80211_chanctx_conf *ctx)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_chanctx_cfg *cfg = (struct rtw89_chanctx_cfg *)ctx->drv_priv;
	struct rtw89_vif *rtwvif;
	u8 drop, roll;

	drop = cfg->idx;
	if (drop != RTW89_SUB_ENTITY_0)
		goto out;

	roll = find_next_bit(hal->entity_map, NUM_OF_RTW89_SUB_ENTITY, drop + 1);

	/* Follow rtw89_config_default_chandef() when rtw89_entity_recalc(). */
	if (roll == NUM_OF_RTW89_SUB_ENTITY)
		goto out;

	/* RTW89_SUB_ENTITY_0 is going to release, and another exists.
	 * Make another roll down to RTW89_SUB_ENTITY_0 to replace.
	 */
	hal->sub[roll].cfg->idx = RTW89_SUB_ENTITY_0;
	hal->sub[RTW89_SUB_ENTITY_0] = hal->sub[roll];

	rtw89_for_each_rtwvif(rtwdev, rtwvif) {
		if (rtwvif->sub_entity_idx == roll)
			rtwvif->sub_entity_idx = RTW89_SUB_ENTITY_0;
	}

	atomic_cmpxchg(&hal->roc_entity_idx, roll, RTW89_SUB_ENTITY_0);

	drop = roll;

out:
	clear_bit(drop, hal->entity_map);
	rtw89_set_channel(rtwdev);
}

void rtw89_chanctx_ops_change(struct rtw89_dev *rtwdev,
			      struct ieee80211_chanctx_conf *ctx,
			      u32 changed)
{
	struct rtw89_chanctx_cfg *cfg = (struct rtw89_chanctx_cfg *)ctx->drv_priv;
	u8 idx = cfg->idx;

	if (changed & IEEE80211_CHANCTX_CHANGE_WIDTH) {
		rtw89_config_entity_chandef(rtwdev, idx, &ctx->def);
		rtw89_set_channel(rtwdev);
	}
}

int rtw89_chanctx_ops_assign_vif(struct rtw89_dev *rtwdev,
				 struct rtw89_vif *rtwvif,
				 struct ieee80211_chanctx_conf *ctx)
{
	struct rtw89_chanctx_cfg *cfg = (struct rtw89_chanctx_cfg *)ctx->drv_priv;

	rtwvif->sub_entity_idx = cfg->idx;
	return 0;
}

void rtw89_chanctx_ops_unassign_vif(struct rtw89_dev *rtwdev,
				    struct rtw89_vif *rtwvif,
				    struct ieee80211_chanctx_conf *ctx)
{
	rtwvif->sub_entity_idx = RTW89_SUB_ENTITY_0;
}
