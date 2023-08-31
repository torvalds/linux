// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2020-2022  Realtek Corporation
 */

#include "chan.h"
#include "coex.h"
#include "debug.h"
#include "fw.h"
#include "mac.h"
#include "ps.h"
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
	rcd->band_changed = band_changed;

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
	const struct cfg80211_chan_def *chandef;
	enum rtw89_entity_mode mode;
	struct rtw89_chan chan;
	u8 weight;
	u8 last;
	u8 idx;

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
		last = RTW89_SUB_ENTITY_0;
		mode = RTW89_ENTITY_MODE_SCC;
		break;
	case 2:
		last = RTW89_SUB_ENTITY_1;
		mode = rtw89_get_entity_mode(rtwdev);
		if (mode == RTW89_ENTITY_MODE_MCC)
			break;

		mode = RTW89_ENTITY_MODE_MCC_PREPARE;
		break;
	}

	for (idx = 0; idx <= last; idx++) {
		chandef = rtw89_chandef_get(rtwdev, idx);
		rtw89_get_channel_params(chandef, &chan);
		if (chan.channel == 0) {
			WARN(1, "Invalid channel on chanctx %d\n", idx);
			return RTW89_ENTITY_MODE_INVALID;
		}

		rtw89_assign_entity_chan(rtwdev, idx, &chan);
	}

	rtw89_set_entity_mode(rtwdev, mode);
	return mode;
}

static void rtw89_chanctx_notify(struct rtw89_dev *rtwdev,
				 enum rtw89_chanctx_state state)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_chanctx_listener *listener = chip->chanctx_listener;
	int i;

	if (!listener)
		return;

	for (i = 0; i < NUM_OF_RTW89_CHANCTX_CALLBACKS; i++) {
		if (!listener->callbacks[i])
			continue;

		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "chanctx notify listener: cb %d, state %d\n",
			    i, state);

		listener->callbacks[i](rtwdev, state);
	}
}

/* This function centrally manages how MCC roles are sorted and iterated.
 * And, it guarantees that ordered_idx is less than NUM_OF_RTW89_MCC_ROLES.
 * So, if data needs to pass an array for ordered_idx, the array can declare
 * with NUM_OF_RTW89_MCC_ROLES. Besides, the entire iteration will stop
 * immediately as long as iterator returns a non-zero value.
 */
static
int rtw89_iterate_mcc_roles(struct rtw89_dev *rtwdev,
			    int (*iterator)(struct rtw89_dev *rtwdev,
					    struct rtw89_mcc_role *mcc_role,
					    unsigned int ordered_idx,
					    void *data),
			    void *data)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role * const roles[] = {
		&mcc->role_ref,
		&mcc->role_aux,
	};
	unsigned int idx;
	int ret;

	BUILD_BUG_ON(ARRAY_SIZE(roles) != NUM_OF_RTW89_MCC_ROLES);

	for (idx = 0; idx < NUM_OF_RTW89_MCC_ROLES; idx++) {
		ret = iterator(rtwdev, roles[idx], idx, data);
		if (ret)
			return ret;
	}

	return 0;
}

/* For now, IEEE80211_HW_TIMING_BEACON_ONLY can make things simple to ensure
 * correctness of MCC calculation logic below. We have noticed that once driver
 * declares WIPHY_FLAG_SUPPORTS_MLO, the use of IEEE80211_HW_TIMING_BEACON_ONLY
 * will be restricted. We will make an alternative in driver when it is ready
 * for MLO.
 */
static u32 rtw89_mcc_get_tbtt_ofst(struct rtw89_dev *rtwdev,
				   struct rtw89_mcc_role *role, u64 tsf)
{
	struct rtw89_vif *rtwvif = role->rtwvif;
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwvif);
	u32 bcn_intvl_us = ieee80211_tu_to_usec(role->beacon_interval);
	u64 sync_tsf = vif->bss_conf.sync_tsf;
	u32 remainder;

	if (tsf < sync_tsf) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC get tbtt ofst: tsf might not update yet\n");
		sync_tsf = 0;
	}

	div_u64_rem(tsf - sync_tsf, bcn_intvl_us, &remainder);

	return remainder;
}

static
void rtw89_mcc_role_fw_macid_bitmap_set_bit(struct rtw89_mcc_role *mcc_role,
					    unsigned int bit)
{
	unsigned int idx = bit / 8;
	unsigned int pos = bit % 8;

	if (idx >= ARRAY_SIZE(mcc_role->macid_bitmap))
		return;

	mcc_role->macid_bitmap[idx] |= BIT(pos);
}

static void rtw89_mcc_role_macid_sta_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw89_sta *rtwsta = (struct rtw89_sta *)sta->drv_priv;
	struct rtw89_vif *rtwvif = rtwsta->rtwvif;
	struct rtw89_mcc_role *mcc_role = data;
	struct rtw89_vif *target = mcc_role->rtwvif;

	if (rtwvif != target)
		return;

	rtw89_mcc_role_fw_macid_bitmap_set_bit(mcc_role, rtwsta->mac_id);
}

static void rtw89_mcc_fill_role_macid_bitmap(struct rtw89_dev *rtwdev,
					     struct rtw89_mcc_role *mcc_role)
{
	struct rtw89_vif *rtwvif = mcc_role->rtwvif;

	rtw89_mcc_role_fw_macid_bitmap_set_bit(mcc_role, rtwvif->mac_id);
	ieee80211_iterate_stations_atomic(rtwdev->hw,
					  rtw89_mcc_role_macid_sta_iter,
					  mcc_role);
}

static void rtw89_mcc_fill_role_policy(struct rtw89_dev *rtwdev,
				       struct rtw89_mcc_role *mcc_role)
{
	struct rtw89_mcc_policy *policy = &mcc_role->policy;

	policy->c2h_rpt = RTW89_FW_MCC_C2H_RPT_ALL;
	policy->tx_null_early = RTW89_MCC_DFLT_TX_NULL_EARLY;
	policy->in_curr_ch = false;
	policy->dis_sw_retry = true;
	policy->sw_retry_count = false;

	if (mcc_role->is_go)
		policy->dis_tx_null = true;
	else
		policy->dis_tx_null = false;
}

static void rtw89_mcc_fill_role_limit(struct rtw89_dev *rtwdev,
				      struct rtw89_mcc_role *mcc_role)
{
	struct ieee80211_vif *vif = rtwvif_to_vif(mcc_role->rtwvif);
	struct ieee80211_p2p_noa_desc *noa_desc;
	u32 bcn_intvl_us = ieee80211_tu_to_usec(mcc_role->beacon_interval);
	u32 max_toa_us, max_tob_us, max_dur_us;
	u32 start_time, interval, duration;
	u64 tsf, tsf_lmt;
	int ret;
	int i;

	if (!mcc_role->is_go && !mcc_role->is_gc)
		return;

	/* find the first periodic NoA */
	for (i = 0; i < RTW89_P2P_MAX_NOA_NUM; i++) {
		noa_desc = &vif->bss_conf.p2p_noa_attr.desc[i];
		if (noa_desc->count == 255)
			goto fill;
	}

	return;

fill:
	start_time = le32_to_cpu(noa_desc->start_time);
	interval = le32_to_cpu(noa_desc->interval);
	duration = le32_to_cpu(noa_desc->duration);

	if (interval != bcn_intvl_us) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC role limit: mismatch interval: %d vs. %d\n",
			    interval, bcn_intvl_us);
		return;
	}

	ret = rtw89_mac_port_get_tsf(rtwdev, mcc_role->rtwvif, &tsf);
	if (ret) {
		rtw89_warn(rtwdev, "MCC failed to get port tsf: %d\n", ret);
		return;
	}

	tsf_lmt = (tsf & GENMASK_ULL(63, 32)) | start_time;
	max_toa_us = rtw89_mcc_get_tbtt_ofst(rtwdev, mcc_role, tsf_lmt);
	max_dur_us = interval - duration;
	max_tob_us = max_dur_us - max_toa_us;

	if (!max_toa_us || !max_tob_us) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC role limit: hit boundary\n");
		return;
	}

	if (max_dur_us < max_toa_us) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC role limit: insufficient duration\n");
		return;
	}

	mcc_role->limit.max_toa = max_toa_us / 1024;
	mcc_role->limit.max_tob = max_tob_us / 1024;
	mcc_role->limit.max_dur = max_dur_us / 1024;
	mcc_role->limit.enable = true;

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC role limit: max_toa %d, max_tob %d, max_dur %d\n",
		    mcc_role->limit.max_toa, mcc_role->limit.max_tob,
		    mcc_role->limit.max_dur);
}

static int rtw89_mcc_fill_role(struct rtw89_dev *rtwdev,
			       struct rtw89_vif *rtwvif,
			       struct rtw89_mcc_role *role)
{
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwvif);
	const struct rtw89_chan *chan;

	memset(role, 0, sizeof(*role));
	role->rtwvif = rtwvif;
	role->beacon_interval = vif->bss_conf.beacon_int;

	if (!role->beacon_interval) {
		rtw89_warn(rtwdev,
			   "cannot handle MCC role without beacon interval\n");
		return -EINVAL;
	}

	role->duration = role->beacon_interval / 2;

	chan = rtw89_chan_get(rtwdev, rtwvif->sub_entity_idx);
	role->is_2ghz = chan->band_type == RTW89_BAND_2G;
	role->is_go = rtwvif->wifi_role == RTW89_WIFI_ROLE_P2P_GO;
	role->is_gc = rtwvif->wifi_role == RTW89_WIFI_ROLE_P2P_CLIENT;

	rtw89_mcc_fill_role_macid_bitmap(rtwdev, role);
	rtw89_mcc_fill_role_policy(rtwdev, role);
	rtw89_mcc_fill_role_limit(rtwdev, role);

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC role: bcn_intvl %d, is_2ghz %d, is_go %d, is_gc %d\n",
		    role->beacon_interval, role->is_2ghz, role->is_go, role->is_gc);
	return 0;
}

static void rtw89_mcc_fill_bt_role(struct rtw89_dev *rtwdev)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_bt_role *bt_role = &mcc->bt_role;

	memset(bt_role, 0, sizeof(*bt_role));
	bt_role->duration = rtw89_coex_query_bt_req_len(rtwdev, RTW89_PHY_0);

	rtw89_debug(rtwdev, RTW89_DBG_CHAN, "MCC bt role: dur %d\n",
		    bt_role->duration);
}

struct rtw89_mcc_fill_role_selector {
	struct rtw89_vif *bind_vif[NUM_OF_RTW89_SUB_ENTITY];
};

static_assert((u8)NUM_OF_RTW89_SUB_ENTITY >= NUM_OF_RTW89_MCC_ROLES);

static int rtw89_mcc_fill_role_iterator(struct rtw89_dev *rtwdev,
					struct rtw89_mcc_role *mcc_role,
					unsigned int ordered_idx,
					void *data)
{
	struct rtw89_mcc_fill_role_selector *sel = data;
	struct rtw89_vif *role_vif = sel->bind_vif[ordered_idx];
	int ret;

	if (!role_vif) {
		rtw89_warn(rtwdev, "cannot handle MCC without role[%d]\n",
			   ordered_idx);
		return -EINVAL;
	}

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC fill role[%d] with vif <macid %d>\n",
		    ordered_idx, role_vif->mac_id);

	ret = rtw89_mcc_fill_role(rtwdev, role_vif, mcc_role);
	if (ret)
		return ret;

	return 0;
}

static int rtw89_mcc_fill_all_roles(struct rtw89_dev *rtwdev)
{
	struct rtw89_mcc_fill_role_selector sel = {};
	struct rtw89_vif *rtwvif;
	int ret;

	rtw89_for_each_rtwvif(rtwdev, rtwvif) {
		if (sel.bind_vif[rtwvif->sub_entity_idx]) {
			rtw89_warn(rtwdev,
				   "MCC skip extra vif <macid %d> on chanctx[%d]\n",
				   rtwvif->mac_id, rtwvif->sub_entity_idx);
			continue;
		}

		sel.bind_vif[rtwvif->sub_entity_idx] = rtwvif;
	}

	ret = rtw89_iterate_mcc_roles(rtwdev, rtw89_mcc_fill_role_iterator, &sel);
	if (ret)
		return ret;

	rtw89_mcc_fill_bt_role(rtwdev);
	return 0;
}

static void rtw89_mcc_assign_pattern(struct rtw89_dev *rtwdev,
				     const struct rtw89_mcc_pattern *new)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;
	struct rtw89_mcc_config *config = &mcc->config;
	struct rtw89_mcc_pattern *pattern = &config->pattern;

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC assign pattern: ref {%d | %d}, aux {%d | %d}\n",
		    new->tob_ref, new->toa_ref, new->tob_aux, new->toa_aux);

	*pattern = *new;
	memset(&pattern->courtesy, 0, sizeof(pattern->courtesy));

	if (pattern->tob_aux <= 0 || pattern->toa_aux <= 0) {
		pattern->courtesy.macid_tgt = aux->rtwvif->mac_id;
		pattern->courtesy.macid_src = ref->rtwvif->mac_id;
		pattern->courtesy.slot_num = RTW89_MCC_DFLT_COURTESY_SLOT;
		pattern->courtesy.enable = true;
	} else if (pattern->tob_ref <= 0 || pattern->toa_ref <= 0) {
		pattern->courtesy.macid_tgt = ref->rtwvif->mac_id;
		pattern->courtesy.macid_src = aux->rtwvif->mac_id;
		pattern->courtesy.slot_num = RTW89_MCC_DFLT_COURTESY_SLOT;
		pattern->courtesy.enable = true;
	}

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC pattern flags: plan %d, courtesy_en %d\n",
		    pattern->plan, pattern->courtesy.enable);

	if (!pattern->courtesy.enable)
		return;

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC pattern courtesy: tgt %d, src %d, slot %d\n",
		    pattern->courtesy.macid_tgt, pattern->courtesy.macid_src,
		    pattern->courtesy.slot_num);
}

static void rtw89_mcc_set_default_pattern(struct rtw89_dev *rtwdev)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;
	struct rtw89_mcc_pattern tmp = {};

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC use default pattern unexpectedly\n");

	tmp.plan = RTW89_MCC_PLAN_NO_BT;
	tmp.tob_ref = ref->duration / 2;
	tmp.toa_ref = ref->duration - tmp.tob_ref;
	tmp.tob_aux = aux->duration / 2;
	tmp.toa_aux = aux->duration - tmp.tob_aux;

	rtw89_mcc_assign_pattern(rtwdev, &tmp);
}

static int rtw89_mcc_fill_start_tsf(struct rtw89_dev *rtwdev)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_config *config = &mcc->config;
	u32 bcn_intvl_ref_us = ieee80211_tu_to_usec(ref->beacon_interval);
	u32 tob_ref_us = ieee80211_tu_to_usec(config->pattern.tob_ref);
	struct rtw89_vif *rtwvif = ref->rtwvif;
	u64 tsf, start_tsf;
	u32 cur_tbtt_ofst;
	u64 min_time;
	int ret;

	ret = rtw89_mac_port_get_tsf(rtwdev, rtwvif, &tsf);
	if (ret) {
		rtw89_warn(rtwdev, "MCC failed to get port tsf: %d\n", ret);
		return ret;
	}

	min_time = tsf;
	if (ref->is_go)
		min_time += ieee80211_tu_to_usec(RTW89_MCC_SHORT_TRIGGER_TIME);
	else
		min_time += ieee80211_tu_to_usec(RTW89_MCC_LONG_TRIGGER_TIME);

	cur_tbtt_ofst = rtw89_mcc_get_tbtt_ofst(rtwdev, ref, tsf);
	start_tsf = tsf - cur_tbtt_ofst + bcn_intvl_ref_us - tob_ref_us;
	while (start_tsf < min_time)
		start_tsf += bcn_intvl_ref_us;

	config->start_tsf = start_tsf;
	return 0;
}

static int rtw89_mcc_fill_config(struct rtw89_dev *rtwdev)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_config *config = &mcc->config;

	memset(config, 0, sizeof(*config));
	rtw89_mcc_set_default_pattern(rtwdev);
	return rtw89_mcc_fill_start_tsf(rtwdev);
}

static int rtw89_mcc_start(struct rtw89_dev *rtwdev)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;
	int ret;

	if (rtwdev->scanning)
		rtw89_hw_scan_abort(rtwdev, rtwdev->scan_info.scanning_vif);

	rtw89_leave_lps(rtwdev);

	rtw89_debug(rtwdev, RTW89_DBG_CHAN, "MCC start\n");

	ret = rtw89_mcc_fill_all_roles(rtwdev);
	if (ret)
		return ret;

	if (ref->is_go || aux->is_go)
		mcc->mode = RTW89_MCC_MODE_GO_STA;
	else
		mcc->mode = RTW89_MCC_MODE_GC_STA;

	rtw89_debug(rtwdev, RTW89_DBG_CHAN, "MCC sel mode: %d\n", mcc->mode);

	ret = rtw89_mcc_fill_config(rtwdev);
	if (ret)
		return ret;

	rtw89_chanctx_notify(rtwdev, RTW89_CHANCTX_STATE_MCC_START);
	return 0;
}

static void rtw89_mcc_stop(struct rtw89_dev *rtwdev)
{
	rtw89_debug(rtwdev, RTW89_DBG_CHAN, "MCC stop\n");
	rtw89_chanctx_notify(rtwdev, RTW89_CHANCTX_STATE_MCC_STOP);
}

void rtw89_chanctx_work(struct work_struct *work)
{
	struct rtw89_dev *rtwdev = container_of(work, struct rtw89_dev,
						chanctx_work.work);
	enum rtw89_entity_mode mode;
	int ret;

	mutex_lock(&rtwdev->mutex);

	mode = rtw89_get_entity_mode(rtwdev);
	switch (mode) {
	case RTW89_ENTITY_MODE_MCC_PREPARE:
		rtw89_set_entity_mode(rtwdev, RTW89_ENTITY_MODE_MCC);
		rtw89_set_channel(rtwdev);

		ret = rtw89_mcc_start(rtwdev);
		if (ret)
			rtw89_warn(rtwdev, "failed to start MCC: %d\n", ret);
		break;
	default:
		break;
	}

	mutex_unlock(&rtwdev->mutex);
}

void rtw89_queue_chanctx_work(struct rtw89_dev *rtwdev)
{
	enum rtw89_entity_mode mode;
	u32 delay;

	mode = rtw89_get_entity_mode(rtwdev);
	switch (mode) {
	default:
		return;
	case RTW89_ENTITY_MODE_MCC_PREPARE:
		delay = ieee80211_tu_to_usec(RTW89_CHANCTX_TIME_MCC_PREPARE);
		break;
	}

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "queue chanctx work for mode %d with delay %d us\n",
		    mode, delay);
	ieee80211_queue_delayed_work(rtwdev->hw, &rtwdev->chanctx_work,
				     usecs_to_jiffies(delay));
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
	enum rtw89_entity_mode mode;
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
	mode = rtw89_get_entity_mode(rtwdev);
	switch (mode) {
	case RTW89_ENTITY_MODE_MCC:
		rtw89_mcc_stop(rtwdev);
		break;
	default:
		break;
	}

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
