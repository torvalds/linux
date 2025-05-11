// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2020-2022  Realtek Corporation
 */

#include "chan.h"
#include "coex.h"
#include "debug.h"
#include "fw.h"
#include "mac.h"
#include "ps.h"
#include "sar.h"
#include "util.h"

static void rtw89_swap_chanctx(struct rtw89_dev *rtwdev,
			       enum rtw89_chanctx_idx idx1,
			       enum rtw89_chanctx_idx idx2);

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

static u8 rtw89_get_primary_sb_idx(u8 central_ch, u8 pri_ch,
				   enum rtw89_bandwidth bw)
{
	static const u8 prisb_cal_ofst[RTW89_CHANNEL_WIDTH_ORDINARY_NUM] = {
		0, 2, 6, 14, 30
	};

	if (bw >= RTW89_CHANNEL_WIDTH_ORDINARY_NUM)
		return 0;

	return (prisb_cal_ofst[bw] + pri_ch - central_ch) / 4;
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
	chan->pri_sb_idx = rtw89_get_primary_sb_idx(center_chan, primary_chan,
						    bandwidth);
}

bool rtw89_assign_entity_chan(struct rtw89_dev *rtwdev,
			      enum rtw89_chanctx_idx idx,
			      const struct rtw89_chan *new)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_chan *chan = &hal->chanctx[idx].chan;
	struct rtw89_chan_rcd *rcd = &hal->chanctx[idx].rcd;
	bool band_changed;

	rcd->prev_primary_channel = chan->primary_channel;
	rcd->prev_band_type = chan->band_type;
	band_changed = new->band_type != chan->band_type;
	rcd->band_changed = band_changed;

	*chan = *new;
	return band_changed;
}

int rtw89_iterate_entity_chan(struct rtw89_dev *rtwdev,
			      int (*iterator)(const struct rtw89_chan *chan,
					      void *data),
			      void *data)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	const struct rtw89_chan *chan;
	int ret;
	u8 idx;

	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	for_each_set_bit(idx,  hal->entity_map, NUM_OF_RTW89_CHANCTX) {
		chan = rtw89_chan_get(rtwdev, idx);
		ret = iterator(chan, data);
		if (ret)
			return ret;
	}

	return 0;
}

static void __rtw89_config_entity_chandef(struct rtw89_dev *rtwdev,
					  enum rtw89_chanctx_idx idx,
					  const struct cfg80211_chan_def *chandef,
					  bool from_stack)
{
	struct rtw89_hal *hal = &rtwdev->hal;

	hal->chanctx[idx].chandef = *chandef;

	if (from_stack)
		set_bit(idx, hal->entity_map);
}

void rtw89_config_entity_chandef(struct rtw89_dev *rtwdev,
				 enum rtw89_chanctx_idx idx,
				 const struct cfg80211_chan_def *chandef)
{
	__rtw89_config_entity_chandef(rtwdev, idx, chandef, true);
}

void rtw89_config_roc_chandef(struct rtw89_dev *rtwdev,
			      struct rtw89_vif_link *rtwvif_link,
			      const struct cfg80211_chan_def *chandef)
{
	enum rtw89_chanctx_idx idx = rtwvif_link->chanctx_idx;
	struct rtw89_hal *hal = &rtwdev->hal;
	enum rtw89_chanctx_idx cur;

	if (chandef) {
		cur = atomic_cmpxchg(&hal->roc_chanctx_idx,
				     RTW89_CHANCTX_IDLE, idx);
		if (cur != RTW89_CHANCTX_IDLE) {
			rtw89_debug(rtwdev, RTW89_DBG_TXRX,
				    "ROC still processing on entity %d\n", idx);
			return;
		}

		hal->roc_chandef = *chandef;
		hal->roc_link_index = rtw89_vif_link_inst_get_index(rtwvif_link);
	} else {
		cur = atomic_cmpxchg(&hal->roc_chanctx_idx, idx,
				     RTW89_CHANCTX_IDLE);
		if (cur == idx)
			return;

		if (cur == RTW89_CHANCTX_IDLE)
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
	__rtw89_config_entity_chandef(rtwdev, RTW89_CHANCTX_0, &chandef, false);
}

void rtw89_entity_init(struct rtw89_dev *rtwdev)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_entity_mgnt *mgnt = &hal->entity_mgnt;

	hal->entity_pause = false;
	bitmap_zero(hal->entity_map, NUM_OF_RTW89_CHANCTX);
	bitmap_zero(hal->changes, NUM_OF_RTW89_CHANCTX_CHANGES);
	atomic_set(&hal->roc_chanctx_idx, RTW89_CHANCTX_IDLE);

	INIT_LIST_HEAD(&mgnt->active_list);

	rtw89_config_default_chandef(rtwdev);
}

static bool rtw89_vif_is_active_role(struct rtw89_vif *rtwvif)
{
	struct rtw89_vif_link *rtwvif_link;
	unsigned int link_id;

	rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id)
		if (rtwvif_link->chanctx_assigned)
			return true;

	return false;
}

static void rtw89_entity_calculate_weight(struct rtw89_dev *rtwdev,
					  struct rtw89_entity_weight *w)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	const struct rtw89_chanctx_cfg *cfg;
	struct rtw89_vif *rtwvif;
	int idx;

	for_each_set_bit(idx, hal->entity_map, NUM_OF_RTW89_CHANCTX) {
		cfg = hal->chanctx[idx].cfg;
		if (!cfg) {
			/* doesn't run with chanctx ops; one channel at most */
			w->active_chanctxs = 1;
			break;
		}

		if (cfg->ref_count > 0)
			w->active_chanctxs++;
	}

	rtw89_for_each_rtwvif(rtwdev, rtwvif) {
		if (rtw89_vif_is_active_role(rtwvif))
			w->active_roles++;
	}
}

static void rtw89_normalize_link_chanctx(struct rtw89_dev *rtwdev,
					 struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_vif *rtwvif = rtwvif_link->rtwvif;
	struct rtw89_vif_link *cur;

	if (unlikely(!rtwvif_link->chanctx_assigned))
		return;

	cur = rtw89_vif_get_link_inst(rtwvif, 0);
	if (!cur || !cur->chanctx_assigned)
		return;

	if (cur == rtwvif_link)
		return;

	rtw89_swap_chanctx(rtwdev, rtwvif_link->chanctx_idx, cur->chanctx_idx);
}

const struct rtw89_chan *__rtw89_mgnt_chan_get(struct rtw89_dev *rtwdev,
					       const char *caller_message,
					       u8 link_index)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_entity_mgnt *mgnt = &hal->entity_mgnt;
	enum rtw89_chanctx_idx chanctx_idx;
	enum rtw89_chanctx_idx roc_idx;
	enum rtw89_entity_mode mode;
	u8 role_index;

	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	if (unlikely(link_index >= __RTW89_MLD_MAX_LINK_NUM)) {
		WARN(1, "link index %u is invalid (max link inst num: %d)\n",
		     link_index, __RTW89_MLD_MAX_LINK_NUM);
		goto dflt;
	}

	mode = rtw89_get_entity_mode(rtwdev);
	switch (mode) {
	case RTW89_ENTITY_MODE_SCC_OR_SMLD:
	case RTW89_ENTITY_MODE_MCC:
		role_index = 0;
		break;
	case RTW89_ENTITY_MODE_MCC_PREPARE:
		role_index = 1;
		break;
	default:
		WARN(1, "Invalid ent mode: %d\n", mode);
		goto dflt;
	}

	chanctx_idx = mgnt->chanctx_tbl[role_index][link_index];
	if (chanctx_idx == RTW89_CHANCTX_IDLE)
		goto dflt;

	roc_idx = atomic_read(&hal->roc_chanctx_idx);
	if (roc_idx != RTW89_CHANCTX_IDLE) {
		/* ROC is ongoing (given ROC runs on @hal->roc_link_index).
		 * If @link_index is the same, get the ongoing ROC chanctx.
		 */
		if (link_index == hal->roc_link_index)
			chanctx_idx = roc_idx;
	}

	return rtw89_chan_get(rtwdev, chanctx_idx);

dflt:
	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "%s (%s): prefetch NULL on link index %u\n",
		    __func__, caller_message ?: "", link_index);

	return rtw89_chan_get(rtwdev, RTW89_CHANCTX_0);
}
EXPORT_SYMBOL(__rtw89_mgnt_chan_get);

static enum rtw89_mlo_dbcc_mode
rtw89_entity_sel_mlo_dbcc_mode(struct rtw89_dev *rtwdev, u8 active_hws)
{
	if (rtwdev->chip->chip_gen != RTW89_CHIP_BE)
		return MLO_DBCC_NOT_SUPPORT;

	switch (active_hws) {
	case BIT(0):
		return MLO_2_PLUS_0_1RF;
	case BIT(1):
		return MLO_0_PLUS_2_1RF;
	case BIT(0) | BIT(1):
	default:
		return MLO_1_PLUS_1_1RF;
	}
}

static
void rtw89_entity_recalc_mlo_dbcc_mode(struct rtw89_dev *rtwdev, u8 active_hws)
{
	enum rtw89_mlo_dbcc_mode mode;

	mode = rtw89_entity_sel_mlo_dbcc_mode(rtwdev, active_hws);
	rtwdev->mlo_dbcc_mode = mode;

	rtw89_debug(rtwdev, RTW89_DBG_STATE, "recalc mlo dbcc mode to %d\n", mode);
}

static void rtw89_entity_recalc_mgnt_roles(struct rtw89_dev *rtwdev)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_entity_mgnt *mgnt = &hal->entity_mgnt;
	struct rtw89_vif_link *link;
	struct rtw89_vif *role;
	u8 active_hws = 0;
	u8 pos = 0;
	int i, j;

	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	for (i = 0; i < RTW89_MAX_INTERFACE_NUM; i++)
		mgnt->active_roles[i] = NULL;

	for (i = 0; i < RTW89_MAX_INTERFACE_NUM; i++) {
		for (j = 0; j < __RTW89_MLD_MAX_LINK_NUM; j++)
			mgnt->chanctx_tbl[i][j] = RTW89_CHANCTX_IDLE;
	}

	/* To be consistent with legacy behavior, expect the first active role
	 * which uses RTW89_CHANCTX_0 to put at position 0, and make its first
	 * link instance take RTW89_CHANCTX_0. (normalizing)
	 */
	list_for_each_entry(role, &mgnt->active_list, mgnt_entry) {
		for (i = 0; i < role->links_inst_valid_num; i++) {
			link = rtw89_vif_get_link_inst(role, i);
			if (!link || !link->chanctx_assigned)
				continue;

			if (link->chanctx_idx == RTW89_CHANCTX_0) {
				rtw89_normalize_link_chanctx(rtwdev, link);

				list_del(&role->mgnt_entry);
				list_add(&role->mgnt_entry, &mgnt->active_list);
				goto fill;
			}
		}
	}

fill:
	list_for_each_entry(role, &mgnt->active_list, mgnt_entry) {
		if (unlikely(pos >= RTW89_MAX_INTERFACE_NUM)) {
			rtw89_warn(rtwdev,
				   "%s: active roles are over max iface num\n",
				   __func__);
			break;
		}

		for (i = 0; i < role->links_inst_valid_num; i++) {
			link = rtw89_vif_get_link_inst(role, i);
			if (!link || !link->chanctx_assigned)
				continue;

			mgnt->chanctx_tbl[pos][i] = link->chanctx_idx;
			active_hws |= BIT(i);
		}

		mgnt->active_roles[pos++] = role;
	}

	rtw89_entity_recalc_mlo_dbcc_mode(rtwdev, active_hws);
}

enum rtw89_entity_mode rtw89_entity_recalc(struct rtw89_dev *rtwdev)
{
	DECLARE_BITMAP(recalc_map, NUM_OF_RTW89_CHANCTX) = {};
	struct rtw89_hal *hal = &rtwdev->hal;
	const struct cfg80211_chan_def *chandef;
	struct rtw89_entity_weight w = {};
	enum rtw89_entity_mode mode;
	struct rtw89_chan chan;
	u8 idx;

	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	bitmap_copy(recalc_map, hal->entity_map, NUM_OF_RTW89_CHANCTX);

	rtw89_entity_calculate_weight(rtwdev, &w);
	switch (w.active_chanctxs) {
	default:
		rtw89_warn(rtwdev, "unknown ent chanctxs weight: %d\n",
			   w.active_chanctxs);
		bitmap_zero(recalc_map, NUM_OF_RTW89_CHANCTX);
		fallthrough;
	case 0:
		rtw89_config_default_chandef(rtwdev);
		set_bit(RTW89_CHANCTX_0, recalc_map);
		fallthrough;
	case 1:
		mode = RTW89_ENTITY_MODE_SCC_OR_SMLD;
		break;
	case 2 ... NUM_OF_RTW89_CHANCTX:
		if (w.active_roles == 1) {
			mode = RTW89_ENTITY_MODE_SCC_OR_SMLD;
			break;
		}

		if (w.active_roles != NUM_OF_RTW89_MCC_ROLES) {
			rtw89_debug(rtwdev, RTW89_DBG_CHAN,
				    "unhandled ent: %d chanctxs %d roles\n",
				    w.active_chanctxs, w.active_roles);
			return RTW89_ENTITY_MODE_UNHANDLED;
		}

		mode = rtw89_get_entity_mode(rtwdev);
		if (mode == RTW89_ENTITY_MODE_MCC)
			break;

		mode = RTW89_ENTITY_MODE_MCC_PREPARE;
		break;
	}

	for_each_set_bit(idx, recalc_map, NUM_OF_RTW89_CHANCTX) {
		chandef = rtw89_chandef_get(rtwdev, idx);
		rtw89_get_channel_params(chandef, &chan);
		if (chan.channel == 0) {
			WARN(1, "Invalid channel on chanctx %d\n", idx);
			return RTW89_ENTITY_MODE_INVALID;
		}

		rtw89_assign_entity_chan(rtwdev, idx, &chan);
	}

	rtw89_entity_recalc_mgnt_roles(rtwdev);

	if (hal->entity_pause)
		return rtw89_get_entity_mode(rtwdev);

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

static bool rtw89_concurrent_via_mrc(struct rtw89_dev *rtwdev)
{
	enum rtw89_chip_gen chip_gen = rtwdev->chip->chip_gen;

	return chip_gen == RTW89_CHIP_BE;
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

static u32 rtw89_mcc_get_tbtt_ofst(struct rtw89_dev *rtwdev,
				   struct rtw89_mcc_role *role, u64 tsf)
{
	struct rtw89_vif_link *rtwvif_link = role->rtwvif_link;
	u32 bcn_intvl_us = ieee80211_tu_to_usec(role->beacon_interval);
	u64 sync_tsf = READ_ONCE(rtwvif_link->sync_bcn_tsf);
	u32 remainder;

	if (role->is_go) {
		sync_tsf = 0;
	} else if (tsf < sync_tsf) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC get tbtt ofst: tsf might not update yet\n");
		sync_tsf = 0;
	}

	div_u64_rem(tsf - sync_tsf, bcn_intvl_us, &remainder);

	return remainder;
}

static int __mcc_fw_req_tsf(struct rtw89_dev *rtwdev, u64 *tsf_ref, u64 *tsf_aux)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;
	struct rtw89_mac_mcc_tsf_rpt rpt = {};
	struct rtw89_fw_mcc_tsf_req req = {};
	int ret;

	req.group = mcc->group;
	req.macid_x = ref->rtwvif_link->mac_id;
	req.macid_y = aux->rtwvif_link->mac_id;
	ret = rtw89_fw_h2c_mcc_req_tsf(rtwdev, &req, &rpt);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC h2c failed to request tsf: %d\n", ret);
		return ret;
	}

	*tsf_ref = (u64)rpt.tsf_x_high << 32 | rpt.tsf_x_low;
	*tsf_aux = (u64)rpt.tsf_y_high << 32 | rpt.tsf_y_low;

	return 0;
}

static int __mrc_fw_req_tsf(struct rtw89_dev *rtwdev, u64 *tsf_ref, u64 *tsf_aux)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;
	struct rtw89_fw_mrc_req_tsf_arg arg = {};
	struct rtw89_mac_mrc_tsf_rpt rpt = {};
	int ret;

	BUILD_BUG_ON(RTW89_MAC_MRC_MAX_REQ_TSF_NUM < NUM_OF_RTW89_MCC_ROLES);

	arg.num = 2;
	arg.infos[0].band = ref->rtwvif_link->mac_idx;
	arg.infos[0].port = ref->rtwvif_link->port;
	arg.infos[1].band = aux->rtwvif_link->mac_idx;
	arg.infos[1].port = aux->rtwvif_link->port;

	ret = rtw89_fw_h2c_mrc_req_tsf(rtwdev, &arg, &rpt);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MRC h2c failed to request tsf: %d\n", ret);
		return ret;
	}

	*tsf_ref = rpt.tsfs[0];
	*tsf_aux = rpt.tsfs[1];

	return 0;
}

static u16 rtw89_mcc_get_bcn_ofst(struct rtw89_dev *rtwdev)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;
	u32 bcn_intvl_ref_us = ieee80211_tu_to_usec(ref->beacon_interval);
	u32 tbtt_ofst_ref, tbtt_ofst_aux;
	u64 tsf_ref, tsf_aux;
	int ret;

	if (rtw89_concurrent_via_mrc(rtwdev))
		ret = __mrc_fw_req_tsf(rtwdev, &tsf_ref, &tsf_aux);
	else
		ret = __mcc_fw_req_tsf(rtwdev, &tsf_ref, &tsf_aux);

	if (ret)
		return RTW89_MCC_DFLT_BCN_OFST_TIME;

	tbtt_ofst_ref = rtw89_mcc_get_tbtt_ofst(rtwdev, ref, tsf_ref);
	tbtt_ofst_aux = rtw89_mcc_get_tbtt_ofst(rtwdev, aux, tsf_aux);

	while (tbtt_ofst_ref < tbtt_ofst_aux)
		tbtt_ofst_ref += bcn_intvl_ref_us;

	return (tbtt_ofst_ref - tbtt_ofst_aux) / 1024;
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

static
u32 rtw89_mcc_role_fw_macid_bitmap_to_u32(struct rtw89_mcc_role *mcc_role)
{
	unsigned int macid;
	unsigned int i, j;
	u32 bitmap = 0;

	for (i = 0; i < ARRAY_SIZE(mcc_role->macid_bitmap); i++) {
		for (j = 0; j < 8; j++) {
			macid = i * 8 + j;
			if (macid >= 32)
				goto out;

			if (mcc_role->macid_bitmap[i] & BIT(j))
				bitmap |= BIT(macid);
		}
	}

out:
	return bitmap;
}

static void rtw89_mcc_role_macid_sta_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw89_mcc_role *mcc_role = data;
	struct rtw89_vif *target = mcc_role->rtwvif_link->rtwvif;
	struct rtw89_sta *rtwsta = sta_to_rtwsta(sta);
	struct rtw89_vif *rtwvif = rtwsta->rtwvif;
	u8 macid;

	if (rtwvif != target)
		return;

	macid = rtw89_sta_get_main_macid(rtwsta);
	rtw89_mcc_role_fw_macid_bitmap_set_bit(mcc_role, macid);
}

static void rtw89_mcc_fill_role_macid_bitmap(struct rtw89_dev *rtwdev,
					     struct rtw89_mcc_role *mcc_role)
{
	struct rtw89_vif_link *rtwvif_link = mcc_role->rtwvif_link;

	rtw89_mcc_role_fw_macid_bitmap_set_bit(mcc_role, rtwvif_link->mac_id);
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
	struct rtw89_vif_link *rtwvif_link = mcc_role->rtwvif_link;
	struct ieee80211_p2p_noa_desc *noa_desc;
	struct ieee80211_bss_conf *bss_conf;
	u32 bcn_intvl_us = ieee80211_tu_to_usec(mcc_role->beacon_interval);
	u32 max_toa_us, max_tob_us, max_dur_us;
	u32 start_time, interval, duration;
	u64 tsf, tsf_lmt;
	int ret;
	int i;

	if (!mcc_role->is_go && !mcc_role->is_gc)
		return;

	rcu_read_lock();

	bss_conf = rtw89_vif_rcu_dereference_link(rtwvif_link, true);

	/* find the first periodic NoA */
	for (i = 0; i < RTW89_P2P_MAX_NOA_NUM; i++) {
		noa_desc = &bss_conf->p2p_noa_attr.desc[i];
		if (noa_desc->count == 255)
			goto fill;
	}

	rcu_read_unlock();
	return;

fill:
	start_time = le32_to_cpu(noa_desc->start_time);
	interval = le32_to_cpu(noa_desc->interval);
	duration = le32_to_cpu(noa_desc->duration);

	rcu_read_unlock();

	if (interval != bcn_intvl_us) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC role limit: mismatch interval: %d vs. %d\n",
			    interval, bcn_intvl_us);
		return;
	}

	ret = rtw89_mac_port_get_tsf(rtwdev, rtwvif_link, &tsf);
	if (ret) {
		rtw89_warn(rtwdev, "MCC failed to get port tsf: %d\n", ret);
		return;
	}

	tsf_lmt = (tsf & GENMASK_ULL(63, 32)) | start_time;
	if (tsf_lmt < tsf)
		tsf_lmt += roundup_u64(tsf - tsf_lmt, interval);

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
	mcc_role->limit.max_dur = mcc_role->limit.max_toa + mcc_role->limit.max_tob;
	mcc_role->limit.enable = true;

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC role limit: max_toa %d, max_tob %d, max_dur %d\n",
		    mcc_role->limit.max_toa, mcc_role->limit.max_tob,
		    mcc_role->limit.max_dur);
}

static int rtw89_mcc_fill_role(struct rtw89_dev *rtwdev,
			       struct rtw89_vif_link *rtwvif_link,
			       struct rtw89_mcc_role *role)
{
	struct ieee80211_bss_conf *bss_conf;
	const struct rtw89_chan *chan;

	memset(role, 0, sizeof(*role));
	role->rtwvif_link = rtwvif_link;

	rcu_read_lock();

	bss_conf = rtw89_vif_rcu_dereference_link(rtwvif_link, true);
	role->beacon_interval = bss_conf->beacon_int;

	rcu_read_unlock();

	if (!role->beacon_interval) {
		rtw89_warn(rtwdev,
			   "cannot handle MCC role without beacon interval\n");
		return -EINVAL;
	}

	role->duration = role->beacon_interval / 2;

	chan = rtw89_chan_get(rtwdev, rtwvif_link->chanctx_idx);
	role->is_2ghz = chan->band_type == RTW89_BAND_2G;
	role->is_go = rtwvif_link->wifi_role == RTW89_WIFI_ROLE_P2P_GO;
	role->is_gc = rtwvif_link->wifi_role == RTW89_WIFI_ROLE_P2P_CLIENT;

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
	struct rtw89_vif_link *bind_vif[NUM_OF_RTW89_CHANCTX];
};

static_assert((u8)NUM_OF_RTW89_CHANCTX >= NUM_OF_RTW89_MCC_ROLES);
static_assert(RTW89_MAX_INTERFACE_NUM >= NUM_OF_RTW89_MCC_ROLES);

static int rtw89_mcc_fill_role_iterator(struct rtw89_dev *rtwdev,
					struct rtw89_mcc_role *mcc_role,
					unsigned int ordered_idx,
					void *data)
{
	struct rtw89_mcc_fill_role_selector *sel = data;
	struct rtw89_vif_link *role_vif = sel->bind_vif[ordered_idx];
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
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_entity_mgnt *mgnt = &hal->entity_mgnt;
	struct rtw89_mcc_fill_role_selector sel = {};
	struct rtw89_vif_link *rtwvif_link;
	struct rtw89_vif *rtwvif;
	int ret;
	int i;

	for (i = 0; i < NUM_OF_RTW89_MCC_ROLES; i++) {
		rtwvif = mgnt->active_roles[i];
		if (!rtwvif)
			break;

		rtwvif_link = rtw89_vif_get_link_inst(rtwvif, 0);
		if (unlikely(!rtwvif_link)) {
			rtw89_err(rtwdev, "mcc fill roles: find no link on HW-0\n");
			continue;
		}

		sel.bind_vif[i] = rtwvif_link;
	}

	ret = rtw89_iterate_mcc_roles(rtwdev, rtw89_mcc_fill_role_iterator, &sel);
	if (ret)
		return ret;

	rtw89_mcc_fill_bt_role(rtwdev);
	return 0;
}

static bool rtw89_mcc_can_courtesy(const struct rtw89_mcc_role *provider,
				   const struct rtw89_mcc_role *receiver)
{
	if (provider->is_go || receiver->is_gc)
		return false;

	return true;
}

static void rtw89_mcc_assign_pattern(struct rtw89_dev *rtwdev,
				     const struct rtw89_mcc_pattern *new)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;
	struct rtw89_mcc_config *config = &mcc->config;
	struct rtw89_mcc_pattern *pattern = &config->pattern;
	struct rtw89_mcc_courtesy_cfg *crtz;

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC assign pattern: ref {%d | %d}, aux {%d | %d}\n",
		    new->tob_ref, new->toa_ref, new->tob_aux, new->toa_aux);

	rtw89_debug(rtwdev, RTW89_DBG_CHAN, "MCC pattern plan: %d\n", new->plan);

	*pattern = *new;
	memset(&pattern->courtesy, 0, sizeof(pattern->courtesy));

	if (RTW89_MCC_REQ_COURTESY(pattern, aux) && rtw89_mcc_can_courtesy(ref, aux)) {
		crtz = &pattern->courtesy.ref;
		ref->crtz = crtz;

		crtz->macid_tgt = aux->rtwvif_link->mac_id;
		crtz->slot_num = RTW89_MCC_DFLT_COURTESY_SLOT;

		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC courtesy ref: tgt %d, slot %d\n",
			    crtz->macid_tgt, crtz->slot_num);
	} else {
		ref->crtz = NULL;
	}

	if (RTW89_MCC_REQ_COURTESY(pattern, ref) && rtw89_mcc_can_courtesy(aux, ref)) {
		crtz = &pattern->courtesy.aux;
		aux->crtz = crtz;

		crtz->macid_tgt = ref->rtwvif_link->mac_id;
		crtz->slot_num = RTW89_MCC_DFLT_COURTESY_SLOT;

		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC courtesy aux: tgt %d, slot %d\n",
			    crtz->macid_tgt, crtz->slot_num);
	} else {
		aux->crtz = NULL;
	}
}

/* The follow-up roughly shows the relationship between the parameters
 * for pattern calculation.
 *
 * |<    duration ref     >| (if mid bt) |<    duration aux     >|
 * |< tob ref >|< toa ref >|     ...     |< tob aux >|< toa aux >|
 *             V                                     V
 *         tbtt ref                              tbtt aux
 *             |<           beacon offset           >|
 *
 * In loose pattern calculation, we only ensure at least tob_ref and
 * toa_ref have positive results. If tob_aux or toa_aux is negative
 * unfortunately, FW will be notified to handle it with courtesy
 * mechanism.
 */
static void __rtw89_mcc_calc_pattern_loose(struct rtw89_dev *rtwdev,
					   struct rtw89_mcc_pattern *ptrn,
					   bool hdl_bt)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;
	struct rtw89_mcc_config *config = &mcc->config;
	u16 mcc_intvl = config->mcc_interval;
	u16 bcn_ofst = config->beacon_offset;
	u16 bt_dur_in_mid = 0;
	u16 max_bcn_ofst;
	s16 upper, lower;
	u16 res;

	*ptrn = (typeof(*ptrn)){
		.plan = hdl_bt ? RTW89_MCC_PLAN_TAIL_BT : RTW89_MCC_PLAN_NO_BT,
	};

	if (!hdl_bt)
		goto calc;

	max_bcn_ofst = ref->duration + aux->duration;
	if (ref->limit.enable)
		max_bcn_ofst = min_t(u16, max_bcn_ofst,
				     ref->limit.max_toa + aux->duration);
	else if (aux->limit.enable)
		max_bcn_ofst = min_t(u16, max_bcn_ofst,
				     ref->duration + aux->limit.max_tob);

	if (bcn_ofst > max_bcn_ofst && bcn_ofst >= mcc->bt_role.duration) {
		bt_dur_in_mid = mcc->bt_role.duration;
		ptrn->plan = RTW89_MCC_PLAN_MID_BT;
	}

calc:
	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC calc ptrn_ls: plan %d, bcn_ofst %d\n",
		    ptrn->plan, bcn_ofst);

	res = bcn_ofst - bt_dur_in_mid;
	upper = min_t(s16, ref->duration, res);
	lower = max_t(s16, 0, ref->duration - (mcc_intvl - bcn_ofst));

	if (ref->limit.enable) {
		upper = min_t(s16, upper, ref->limit.max_toa);
		lower = max_t(s16, lower, ref->duration - ref->limit.max_tob);
	} else if (aux->limit.enable) {
		upper = min_t(s16, upper,
			      res - (aux->duration - aux->limit.max_toa));
		lower = max_t(s16, lower, res - aux->limit.max_tob);
	}

	if (lower < upper)
		ptrn->toa_ref = (upper + lower) / 2;
	else
		ptrn->toa_ref = lower;

	ptrn->tob_ref = ref->duration - ptrn->toa_ref;
	ptrn->tob_aux = res - ptrn->toa_ref;
	ptrn->toa_aux = aux->duration - ptrn->tob_aux;
}

/* In strict pattern calculation, we consider timing that might need
 * for HW stuffs, i.e. min_tob and min_toa.
 */
static int __rtw89_mcc_calc_pattern_strict(struct rtw89_dev *rtwdev,
					   struct rtw89_mcc_pattern *ptrn)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;
	struct rtw89_mcc_config *config = &mcc->config;
	u16 min_tob = RTW89_MCC_EARLY_RX_BCN_TIME;
	u16 min_toa = RTW89_MCC_MIN_RX_BCN_TIME;
	u16 bcn_ofst = config->beacon_offset;
	s16 upper_toa_ref, lower_toa_ref;
	s16 upper_tob_aux, lower_tob_aux;
	u16 bt_dur_in_mid;
	s16 res;

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC calc ptrn_st: plan %d, bcn_ofst %d\n",
		    ptrn->plan, bcn_ofst);

	if (ptrn->plan == RTW89_MCC_PLAN_MID_BT)
		bt_dur_in_mid = mcc->bt_role.duration;
	else
		bt_dur_in_mid = 0;

	if (ref->duration < min_tob + min_toa) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC calc ptrn_st: not meet ref dur cond\n");
		return -EINVAL;
	}

	if (aux->duration < min_tob + min_toa) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC calc ptrn_st: not meet aux dur cond\n");
		return -EINVAL;
	}

	res = bcn_ofst - min_toa - min_tob - bt_dur_in_mid;
	if (res < 0) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC calc ptrn_st: not meet bcn_ofst cond\n");
		return -EINVAL;
	}

	upper_toa_ref = min_t(s16, min_toa + res, ref->duration - min_tob);
	lower_toa_ref = min_toa;
	upper_tob_aux = min_t(s16, min_tob + res, aux->duration - min_toa);
	lower_tob_aux = min_tob;

	if (ref->limit.enable) {
		if (min_tob > ref->limit.max_tob || min_toa > ref->limit.max_toa) {
			rtw89_debug(rtwdev, RTW89_DBG_CHAN,
				    "MCC calc ptrn_st: conflict ref limit\n");
			return -EINVAL;
		}

		upper_toa_ref = min_t(s16, upper_toa_ref, ref->limit.max_toa);
		lower_toa_ref = max_t(s16, lower_toa_ref,
				      ref->duration - ref->limit.max_tob);
	} else if (aux->limit.enable) {
		if (min_tob > aux->limit.max_tob || min_toa > aux->limit.max_toa) {
			rtw89_debug(rtwdev, RTW89_DBG_CHAN,
				    "MCC calc ptrn_st: conflict aux limit\n");
			return -EINVAL;
		}

		upper_tob_aux = min_t(s16, upper_tob_aux, aux->limit.max_tob);
		lower_tob_aux = max_t(s16, lower_tob_aux,
				      aux->duration - aux->limit.max_toa);
	}

	upper_toa_ref = min_t(s16, upper_toa_ref,
			      bcn_ofst - bt_dur_in_mid - lower_tob_aux);
	lower_toa_ref = max_t(s16, lower_toa_ref,
			      bcn_ofst - bt_dur_in_mid - upper_tob_aux);
	if (lower_toa_ref > upper_toa_ref) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC calc ptrn_st: conflict boundary\n");
		return -EINVAL;
	}

	ptrn->toa_ref = (upper_toa_ref + lower_toa_ref) / 2;
	ptrn->tob_ref = ref->duration - ptrn->toa_ref;
	ptrn->tob_aux = bcn_ofst - ptrn->toa_ref - bt_dur_in_mid;
	ptrn->toa_aux = aux->duration - ptrn->tob_aux;
	return 0;
}

static int rtw89_mcc_calc_pattern(struct rtw89_dev *rtwdev, bool hdl_bt)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;
	bool sel_plan[NUM_OF_RTW89_MCC_PLAN] = {};
	struct rtw89_mcc_pattern ptrn;
	int ret;
	int i;

	if (ref->limit.enable && aux->limit.enable) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC calc ptrn: not support dual limited roles\n");
		return -EINVAL;
	}

	if (ref->limit.enable &&
	    ref->duration > ref->limit.max_tob + ref->limit.max_toa) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC calc ptrn: not fit ref limit\n");
		return -EINVAL;
	}

	if (aux->limit.enable &&
	    aux->duration > aux->limit.max_tob + aux->limit.max_toa) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC calc ptrn: not fit aux limit\n");
		return -EINVAL;
	}

	if (hdl_bt) {
		sel_plan[RTW89_MCC_PLAN_TAIL_BT] = true;
		sel_plan[RTW89_MCC_PLAN_MID_BT] = true;
	} else {
		sel_plan[RTW89_MCC_PLAN_NO_BT] = true;
	}

	for (i = 0; i < NUM_OF_RTW89_MCC_PLAN; i++) {
		if (!sel_plan[i])
			continue;

		ptrn = (typeof(ptrn)){
			.plan = i,
		};

		ret = __rtw89_mcc_calc_pattern_strict(rtwdev, &ptrn);
		if (ret)
			rtw89_debug(rtwdev, RTW89_DBG_CHAN,
				    "MCC calc ptrn_st with plan %d: fail\n", i);
		else
			goto done;
	}

	__rtw89_mcc_calc_pattern_loose(rtwdev, &ptrn, hdl_bt);

done:
	rtw89_mcc_assign_pattern(rtwdev, &ptrn);
	return 0;
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

static void rtw89_mcc_set_duration_go_sta(struct rtw89_dev *rtwdev,
					  struct rtw89_mcc_role *role_go,
					  struct rtw89_mcc_role *role_sta)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_config *config = &mcc->config;
	u16 mcc_intvl = config->mcc_interval;
	u16 dur_go, dur_sta;

	dur_go = clamp_t(u16, role_go->duration, RTW89_MCC_MIN_GO_DURATION,
			 mcc_intvl - RTW89_MCC_MIN_STA_DURATION);
	if (role_go->limit.enable)
		dur_go = min(dur_go, role_go->limit.max_dur);
	dur_sta = mcc_intvl - dur_go;

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC set dur: (go, sta) {%d, %d} -> {%d, %d}\n",
		    role_go->duration, role_sta->duration, dur_go, dur_sta);

	role_go->duration = dur_go;
	role_sta->duration = dur_sta;
}

static void rtw89_mcc_set_duration_gc_sta(struct rtw89_dev *rtwdev)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;
	struct rtw89_mcc_config *config = &mcc->config;
	u16 mcc_intvl = config->mcc_interval;
	u16 dur_ref, dur_aux;

	if (ref->duration < RTW89_MCC_MIN_STA_DURATION) {
		dur_ref = RTW89_MCC_MIN_STA_DURATION;
		dur_aux = mcc_intvl - dur_ref;
	} else if (aux->duration < RTW89_MCC_MIN_STA_DURATION) {
		dur_aux = RTW89_MCC_MIN_STA_DURATION;
		dur_ref = mcc_intvl - dur_aux;
	} else {
		dur_ref = ref->duration;
		dur_aux = mcc_intvl - dur_ref;
	}

	if (ref->limit.enable) {
		dur_ref = min(dur_ref, ref->limit.max_dur);
		dur_aux = mcc_intvl - dur_ref;
	} else if (aux->limit.enable) {
		dur_aux = min(dur_aux, aux->limit.max_dur);
		dur_ref = mcc_intvl - dur_aux;
	}

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC set dur: (ref, aux) {%d ~ %d} -> {%d ~ %d}\n",
		    ref->duration, aux->duration, dur_ref, dur_aux);

	ref->duration = dur_ref;
	aux->duration = dur_aux;
}

struct rtw89_mcc_mod_dur_data {
	u16 available;
	struct {
		u16 dur;
		u16 room;
	} parm[NUM_OF_RTW89_MCC_ROLES];
};

static int rtw89_mcc_mod_dur_get_iterator(struct rtw89_dev *rtwdev,
					  struct rtw89_mcc_role *mcc_role,
					  unsigned int ordered_idx,
					  void *data)
{
	struct rtw89_mcc_mod_dur_data *p = data;
	u16 min;

	p->parm[ordered_idx].dur = mcc_role->duration;

	if (mcc_role->is_go)
		min = RTW89_MCC_MIN_GO_DURATION;
	else
		min = RTW89_MCC_MIN_STA_DURATION;

	p->parm[ordered_idx].room = max_t(s32, p->parm[ordered_idx].dur - min, 0);

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC mod dur: chk role[%u]: dur %u, min %u, room %u\n",
		    ordered_idx, p->parm[ordered_idx].dur, min,
		    p->parm[ordered_idx].room);

	p->available += p->parm[ordered_idx].room;
	return 0;
}

static int rtw89_mcc_mod_dur_put_iterator(struct rtw89_dev *rtwdev,
					  struct rtw89_mcc_role *mcc_role,
					  unsigned int ordered_idx,
					  void *data)
{
	struct rtw89_mcc_mod_dur_data *p = data;

	mcc_role->duration = p->parm[ordered_idx].dur;

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC mod dur: set role[%u]: dur %u\n",
		    ordered_idx, p->parm[ordered_idx].dur);
	return 0;
}

static void rtw89_mcc_mod_duration_dual_2ghz_with_bt(struct rtw89_dev *rtwdev)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_config *config = &mcc->config;
	struct rtw89_mcc_mod_dur_data data = {};
	u16 mcc_intvl = config->mcc_interval;
	u16 bt_dur = mcc->bt_role.duration;
	u16 wifi_dur;

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC mod dur (dual 2ghz): mcc_intvl %u, raw bt_dur %u\n",
		    mcc_intvl, bt_dur);

	rtw89_iterate_mcc_roles(rtwdev, rtw89_mcc_mod_dur_get_iterator, &data);

	bt_dur = clamp_t(u16, bt_dur, 1, data.available / 3);
	wifi_dur = mcc_intvl - bt_dur;

	if (data.parm[0].room <= data.parm[1].room) {
		data.parm[0].dur -= min_t(u16, bt_dur / 2, data.parm[0].room);
		data.parm[1].dur = wifi_dur - data.parm[0].dur;
	} else {
		data.parm[1].dur -= min_t(u16, bt_dur / 2, data.parm[1].room);
		data.parm[0].dur = wifi_dur - data.parm[1].dur;
	}

	rtw89_iterate_mcc_roles(rtwdev, rtw89_mcc_mod_dur_put_iterator, &data);

	rtw89_debug(rtwdev, RTW89_DBG_CHAN, "MCC mod dur: set bt: dur %u\n", bt_dur);
	mcc->bt_role.duration = bt_dur;
}

static
void rtw89_mcc_mod_duration_diff_band_with_bt(struct rtw89_dev *rtwdev,
					      struct rtw89_mcc_role *role_2ghz,
					      struct rtw89_mcc_role *role_non_2ghz)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_config *config = &mcc->config;
	u16 dur_2ghz, dur_non_2ghz;
	u16 bt_dur, mcc_intvl;

	dur_2ghz = role_2ghz->duration;
	dur_non_2ghz = role_non_2ghz->duration;
	mcc_intvl = config->mcc_interval;
	bt_dur = mcc->bt_role.duration;

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC mod dur (diff band): mcc_intvl %u, bt_dur %u\n",
		    mcc_intvl, bt_dur);

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC mod dur: check dur_2ghz %u, dur_non_2ghz %u\n",
		    dur_2ghz, dur_non_2ghz);

	if (dur_non_2ghz >= bt_dur) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC mod dur: dur_non_2ghz is enough for bt\n");
		return;
	}

	dur_non_2ghz = bt_dur;
	dur_2ghz = mcc_intvl - dur_non_2ghz;

	if (role_non_2ghz->limit.enable) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC mod dur: dur_non_2ghz is limited with max %u\n",
			    role_non_2ghz->limit.max_dur);

		dur_non_2ghz = min(dur_non_2ghz, role_non_2ghz->limit.max_dur);
		dur_2ghz = mcc_intvl - dur_non_2ghz;
	}

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC mod dur: set dur_2ghz %u, dur_non_2ghz %u\n",
		    dur_2ghz, dur_non_2ghz);

	role_2ghz->duration = dur_2ghz;
	role_non_2ghz->duration = dur_non_2ghz;
}

static bool rtw89_mcc_duration_decision_on_bt(struct rtw89_dev *rtwdev)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;
	struct rtw89_mcc_bt_role *bt_role = &mcc->bt_role;

	if (!bt_role->duration)
		return false;

	if (ref->is_2ghz && aux->is_2ghz) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC dual roles are on 2GHz; consider BT duration\n");

		rtw89_mcc_mod_duration_dual_2ghz_with_bt(rtwdev);
		return true;
	}

	if (!ref->is_2ghz && !aux->is_2ghz) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC dual roles are not on 2GHz; ignore BT duration\n");
		return false;
	}

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MCC one role is on 2GHz; modify another for BT duration\n");

	if (ref->is_2ghz)
		rtw89_mcc_mod_duration_diff_band_with_bt(rtwdev, ref, aux);
	else
		rtw89_mcc_mod_duration_diff_band_with_bt(rtwdev, aux, ref);

	return false;
}

static int rtw89_mcc_fill_start_tsf(struct rtw89_dev *rtwdev)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;
	struct rtw89_mcc_config *config = &mcc->config;
	u32 bcn_intvl_ref_us = ieee80211_tu_to_usec(ref->beacon_interval);
	s32 tob_ref_us = ieee80211_tu_to_usec(config->pattern.tob_ref);
	u64 tsf, start_tsf;
	u32 cur_tbtt_ofst;
	u64 min_time;
	u64 tsf_aux;
	int ret;

	if (rtw89_concurrent_via_mrc(rtwdev))
		ret = __mrc_fw_req_tsf(rtwdev, &tsf, &tsf_aux);
	else
		ret = __mcc_fw_req_tsf(rtwdev, &tsf, &tsf_aux);

	if (ret)
		return ret;

	min_time = tsf;
	if (ref->is_go || aux->is_go)
		min_time += ieee80211_tu_to_usec(RTW89_MCC_SHORT_TRIGGER_TIME);
	else
		min_time += ieee80211_tu_to_usec(RTW89_MCC_LONG_TRIGGER_TIME);

	cur_tbtt_ofst = rtw89_mcc_get_tbtt_ofst(rtwdev, ref, tsf);
	start_tsf = tsf - cur_tbtt_ofst + bcn_intvl_ref_us - tob_ref_us;
	if (start_tsf < min_time)
		start_tsf += roundup_u64(min_time - start_tsf, bcn_intvl_ref_us);

	config->start_tsf = start_tsf;
	config->start_tsf_in_aux_domain = tsf_aux + start_tsf - tsf;
	return 0;
}

static int rtw89_mcc_fill_config(struct rtw89_dev *rtwdev)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;
	struct rtw89_mcc_config *config = &mcc->config;
	bool hdl_bt;
	int ret;

	memset(config, 0, sizeof(*config));

	switch (mcc->mode) {
	case RTW89_MCC_MODE_GO_STA:
		config->beacon_offset = rtw89_mcc_get_bcn_ofst(rtwdev);
		if (ref->is_go) {
			config->mcc_interval = ref->beacon_interval;
			rtw89_mcc_set_duration_go_sta(rtwdev, ref, aux);
		} else {
			config->mcc_interval = aux->beacon_interval;
			rtw89_mcc_set_duration_go_sta(rtwdev, aux, ref);
		}
		break;
	case RTW89_MCC_MODE_GC_STA:
		config->beacon_offset = rtw89_mcc_get_bcn_ofst(rtwdev);
		config->mcc_interval = ref->beacon_interval;
		rtw89_mcc_set_duration_gc_sta(rtwdev);
		break;
	default:
		rtw89_warn(rtwdev, "MCC unknown mode: %d\n", mcc->mode);
		return -EFAULT;
	}

	hdl_bt = rtw89_mcc_duration_decision_on_bt(rtwdev);
	rtw89_debug(rtwdev, RTW89_DBG_CHAN, "MCC handle bt: %d\n", hdl_bt);

	ret = rtw89_mcc_calc_pattern(rtwdev, hdl_bt);
	if (!ret)
		goto bottom;

	rtw89_mcc_set_default_pattern(rtwdev);

bottom:
	return rtw89_mcc_fill_start_tsf(rtwdev);
}

static int __mcc_fw_add_role(struct rtw89_dev *rtwdev, struct rtw89_mcc_role *role)
{
	const struct rtw89_mcc_courtesy_cfg *crtz = role->crtz;
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_policy *policy = &role->policy;
	struct rtw89_fw_mcc_add_req req = {};
	const struct rtw89_chan *chan;
	int ret;

	chan = rtw89_chan_get(rtwdev, role->rtwvif_link->chanctx_idx);
	req.central_ch_seg0 = chan->channel;
	req.primary_ch = chan->primary_channel;
	req.bandwidth = chan->band_width;
	req.ch_band_type = chan->band_type;

	req.macid = role->rtwvif_link->mac_id;
	req.group = mcc->group;
	req.c2h_rpt = policy->c2h_rpt;
	req.tx_null_early = policy->tx_null_early;
	req.dis_tx_null = policy->dis_tx_null;
	req.in_curr_ch = policy->in_curr_ch;
	req.sw_retry_count = policy->sw_retry_count;
	req.dis_sw_retry = policy->dis_sw_retry;
	req.duration = role->duration;
	req.btc_in_2g = false;

	if (crtz) {
		req.courtesy_target = crtz->macid_tgt;
		req.courtesy_num = crtz->slot_num;
		req.courtesy_en = true;
	}

	ret = rtw89_fw_h2c_add_mcc(rtwdev, &req);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC h2c failed to add wifi role: %d\n", ret);
		return ret;
	}

	ret = rtw89_fw_h2c_mcc_macid_bitmap(rtwdev, mcc->group,
					    role->rtwvif_link->mac_id,
					    role->macid_bitmap);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC h2c failed to set macid bitmap: %d\n", ret);
		return ret;
	}

	return 0;
}

static
void __mrc_fw_add_role(struct rtw89_dev *rtwdev, struct rtw89_mcc_role *role,
		       struct rtw89_fw_mrc_add_arg *arg, u8 slot_idx)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_policy *policy = &role->policy;
	struct rtw89_fw_mrc_add_slot_arg *slot_arg;
	const struct rtw89_chan *chan;

	slot_arg = &arg->slots[slot_idx];
	role->slot_idx = slot_idx;

	slot_arg->duration = role->duration;
	slot_arg->role_num = 1;

	chan = rtw89_chan_get(rtwdev, role->rtwvif_link->chanctx_idx);

	slot_arg->roles[0].role_type = RTW89_H2C_MRC_ROLE_WIFI;
	slot_arg->roles[0].is_master = role == ref;
	slot_arg->roles[0].band = chan->band_type;
	slot_arg->roles[0].bw = chan->band_width;
	slot_arg->roles[0].central_ch = chan->channel;
	slot_arg->roles[0].primary_ch = chan->primary_channel;
	slot_arg->roles[0].en_tx_null = !policy->dis_tx_null;
	slot_arg->roles[0].null_early = policy->tx_null_early;
	slot_arg->roles[0].macid = role->rtwvif_link->mac_id;
	slot_arg->roles[0].macid_main_bitmap =
		rtw89_mcc_role_fw_macid_bitmap_to_u32(role);
}

static int __mcc_fw_add_bt_role(struct rtw89_dev *rtwdev)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_bt_role *bt_role = &mcc->bt_role;
	struct rtw89_fw_mcc_add_req req = {};
	int ret;

	req.group = mcc->group;
	req.duration = bt_role->duration;
	req.btc_in_2g = true;

	ret = rtw89_fw_h2c_add_mcc(rtwdev, &req);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC h2c failed to add bt role: %d\n", ret);
		return ret;
	}

	return 0;
}

static
void __mrc_fw_add_bt_role(struct rtw89_dev *rtwdev,
			  struct rtw89_fw_mrc_add_arg *arg, u8 slot_idx)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_bt_role *bt_role = &mcc->bt_role;
	struct rtw89_fw_mrc_add_slot_arg *slot_arg = &arg->slots[slot_idx];

	slot_arg->duration = bt_role->duration;
	slot_arg->role_num = 1;

	slot_arg->roles[0].role_type = RTW89_H2C_MRC_ROLE_BT;
}

static int __mcc_fw_start(struct rtw89_dev *rtwdev, bool replace)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;
	struct rtw89_mcc_config *config = &mcc->config;
	struct rtw89_mcc_pattern *pattern = &config->pattern;
	struct rtw89_mcc_sync *sync = &config->sync;
	struct rtw89_fw_mcc_start_req req = {};
	int ret;

	if (replace) {
		req.old_group = mcc->group;
		req.old_group_action = RTW89_FW_MCC_OLD_GROUP_ACT_REPLACE;
		mcc->group = RTW89_MCC_NEXT_GROUP(mcc->group);
	}

	req.group = mcc->group;

	switch (pattern->plan) {
	case RTW89_MCC_PLAN_TAIL_BT:
		ret = __mcc_fw_add_role(rtwdev, ref);
		if (ret)
			return ret;
		ret = __mcc_fw_add_role(rtwdev, aux);
		if (ret)
			return ret;
		ret = __mcc_fw_add_bt_role(rtwdev);
		if (ret)
			return ret;

		req.btc_in_group = true;
		break;
	case RTW89_MCC_PLAN_MID_BT:
		ret = __mcc_fw_add_role(rtwdev, ref);
		if (ret)
			return ret;
		ret = __mcc_fw_add_bt_role(rtwdev);
		if (ret)
			return ret;
		ret = __mcc_fw_add_role(rtwdev, aux);
		if (ret)
			return ret;

		req.btc_in_group = true;
		break;
	case RTW89_MCC_PLAN_NO_BT:
		ret = __mcc_fw_add_role(rtwdev, ref);
		if (ret)
			return ret;
		ret = __mcc_fw_add_role(rtwdev, aux);
		if (ret)
			return ret;

		req.btc_in_group = false;
		break;
	default:
		rtw89_warn(rtwdev, "MCC unknown plan: %d\n", pattern->plan);
		return -EFAULT;
	}

	if (sync->enable) {
		ret = rtw89_fw_h2c_mcc_sync(rtwdev, req.group, sync->macid_src,
					    sync->macid_tgt, sync->offset);
		if (ret) {
			rtw89_debug(rtwdev, RTW89_DBG_CHAN,
				    "MCC h2c failed to trigger sync: %d\n", ret);
			return ret;
		}
	}

	req.macid = ref->rtwvif_link->mac_id;
	req.tsf_high = config->start_tsf >> 32;
	req.tsf_low = config->start_tsf;

	ret = rtw89_fw_h2c_start_mcc(rtwdev, &req);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC h2c failed to trigger start: %d\n", ret);
		return ret;
	}

	return 0;
}

static void __mrc_fw_add_courtesy(struct rtw89_dev *rtwdev,
				  struct rtw89_fw_mrc_add_arg *arg)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;
	struct rtw89_fw_mrc_add_slot_arg *slot_arg_src;

	if (ref->crtz) {
		slot_arg_src = &arg->slots[ref->slot_idx];

		slot_arg_src->courtesy_target = aux->slot_idx;
		slot_arg_src->courtesy_period = ref->crtz->slot_num;
		slot_arg_src->courtesy_en = true;
	}

	if (aux->crtz) {
		slot_arg_src = &arg->slots[aux->slot_idx];

		slot_arg_src->courtesy_target = ref->slot_idx;
		slot_arg_src->courtesy_period = aux->crtz->slot_num;
		slot_arg_src->courtesy_en = true;
	}
}

static int __mrc_fw_start(struct rtw89_dev *rtwdev, bool replace)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;
	struct rtw89_mcc_config *config = &mcc->config;
	struct rtw89_mcc_pattern *pattern = &config->pattern;
	struct rtw89_mcc_sync *sync = &config->sync;
	struct rtw89_fw_mrc_start_arg start_arg = {};
	struct rtw89_fw_mrc_add_arg add_arg = {};
	int ret;

	BUILD_BUG_ON(RTW89_MAC_MRC_MAX_ADD_SLOT_NUM <
		     NUM_OF_RTW89_MCC_ROLES + 1 /* bt role */);

	if (replace) {
		start_arg.old_sch_idx = mcc->group;
		start_arg.action = RTW89_H2C_MRC_START_ACTION_REPLACE_OLD;
		mcc->group = RTW89_MCC_NEXT_GROUP(mcc->group);
	}

	add_arg.sch_idx = mcc->group;
	add_arg.sch_type = RTW89_H2C_MRC_SCH_BAND0_ONLY;

	switch (pattern->plan) {
	case RTW89_MCC_PLAN_TAIL_BT:
		__mrc_fw_add_role(rtwdev, ref, &add_arg, 0);
		__mrc_fw_add_role(rtwdev, aux, &add_arg, 1);
		__mrc_fw_add_bt_role(rtwdev, &add_arg, 2);

		add_arg.slot_num = 3;
		add_arg.btc_in_sch = true;
		break;
	case RTW89_MCC_PLAN_MID_BT:
		__mrc_fw_add_role(rtwdev, ref, &add_arg, 0);
		__mrc_fw_add_bt_role(rtwdev, &add_arg, 1);
		__mrc_fw_add_role(rtwdev, aux, &add_arg, 2);

		add_arg.slot_num = 3;
		add_arg.btc_in_sch = true;
		break;
	case RTW89_MCC_PLAN_NO_BT:
		__mrc_fw_add_role(rtwdev, ref, &add_arg, 0);
		__mrc_fw_add_role(rtwdev, aux, &add_arg, 1);

		add_arg.slot_num = 2;
		add_arg.btc_in_sch = false;
		break;
	default:
		rtw89_warn(rtwdev, "MCC unknown plan: %d\n", pattern->plan);
		return -EFAULT;
	}

	__mrc_fw_add_courtesy(rtwdev, &add_arg);

	ret = rtw89_fw_h2c_mrc_add(rtwdev, &add_arg);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MRC h2c failed to trigger add: %d\n", ret);
		return ret;
	}

	if (sync->enable) {
		struct rtw89_fw_mrc_sync_arg sync_arg = {
			.offset = sync->offset,
			.src = {
				.band = sync->band_src,
				.port = sync->port_src,
			},
			.dest = {
				.band = sync->band_tgt,
				.port = sync->port_tgt,
			},
		};

		ret = rtw89_fw_h2c_mrc_sync(rtwdev, &sync_arg);
		if (ret) {
			rtw89_debug(rtwdev, RTW89_DBG_CHAN,
				    "MRC h2c failed to trigger sync: %d\n", ret);
			return ret;
		}
	}

	start_arg.sch_idx = mcc->group;
	start_arg.start_tsf = config->start_tsf;

	ret = rtw89_fw_h2c_mrc_start(rtwdev, &start_arg);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MRC h2c failed to trigger start: %d\n", ret);
		return ret;
	}

	return 0;
}

static int __mcc_fw_set_duration_no_bt(struct rtw89_dev *rtwdev, bool sync_changed)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_config *config = &mcc->config;
	struct rtw89_mcc_sync *sync = &config->sync;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;
	struct rtw89_fw_mcc_duration req = {
		.group = mcc->group,
		.btc_in_group = false,
		.start_macid = ref->rtwvif_link->mac_id,
		.macid_x = ref->rtwvif_link->mac_id,
		.macid_y = aux->rtwvif_link->mac_id,
		.duration_x = ref->duration,
		.duration_y = aux->duration,
		.start_tsf_high = config->start_tsf >> 32,
		.start_tsf_low = config->start_tsf,
	};
	int ret;

	ret = rtw89_fw_h2c_mcc_set_duration(rtwdev, &req);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC h2c failed to set duration: %d\n", ret);
		return ret;
	}

	if (!sync->enable || !sync_changed)
		return 0;

	ret = rtw89_fw_h2c_mcc_sync(rtwdev, mcc->group, sync->macid_src,
				    sync->macid_tgt, sync->offset);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC h2c failed to trigger sync: %d\n", ret);
		return ret;
	}

	return 0;
}

static int __mrc_fw_set_duration_no_bt(struct rtw89_dev *rtwdev, bool sync_changed)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_config *config = &mcc->config;
	struct rtw89_mcc_sync *sync = &config->sync;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;
	struct rtw89_fw_mrc_upd_duration_arg dur_arg = {
		.sch_idx = mcc->group,
		.start_tsf = config->start_tsf,
		.slot_num = 2,
		.slots[0] = {
			.slot_idx = ref->slot_idx,
			.duration = ref->duration,
		},
		.slots[1] = {
			.slot_idx = aux->slot_idx,
			.duration = aux->duration,
		},
	};
	struct rtw89_fw_mrc_sync_arg sync_arg = {
		.offset = sync->offset,
		.src = {
			.band = sync->band_src,
			.port = sync->port_src,
		},
		.dest = {
			.band = sync->band_tgt,
			.port = sync->port_tgt,
		},

	};
	int ret;

	ret = rtw89_fw_h2c_mrc_upd_duration(rtwdev, &dur_arg);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MRC h2c failed to update duration: %d\n", ret);
		return ret;
	}

	if (!sync->enable || !sync_changed)
		return 0;

	ret = rtw89_fw_h2c_mrc_sync(rtwdev, &sync_arg);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MRC h2c failed to trigger sync: %d\n", ret);
		return ret;
	}

	return 0;
}

static void rtw89_mcc_handle_beacon_noa(struct rtw89_dev *rtwdev, bool enable)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;
	struct rtw89_mcc_config *config = &mcc->config;
	struct ieee80211_p2p_noa_desc noa_desc = {};
	u32 interval = config->mcc_interval;
	struct rtw89_vif_link *rtwvif_go;
	u64 start_time;
	u32 duration;

	if (mcc->mode != RTW89_MCC_MODE_GO_STA)
		return;

	if (ref->is_go) {
		start_time = config->start_tsf;
		rtwvif_go = ref->rtwvif_link;
		start_time += ieee80211_tu_to_usec(ref->duration);
		duration = config->mcc_interval - ref->duration;
	} else if (aux->is_go) {
		start_time = config->start_tsf_in_aux_domain;
		rtwvif_go = aux->rtwvif_link;
		duration = config->mcc_interval - aux->duration;
	} else {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC find no GO: skip updating beacon NoA\n");
		return;
	}

	rtw89_p2p_noa_renew(rtwvif_go);

	if (enable) {
		noa_desc.start_time = cpu_to_le32(start_time);
		noa_desc.interval = cpu_to_le32(ieee80211_tu_to_usec(interval));
		noa_desc.duration = cpu_to_le32(ieee80211_tu_to_usec(duration));
		noa_desc.count = 255;
		rtw89_p2p_noa_append(rtwvif_go, &noa_desc);
	}

	/* without chanctx, we cannot get beacon from mac80211 stack */
	if (!rtwvif_go->chanctx_assigned)
		return;

	rtw89_chip_h2c_update_beacon(rtwdev, rtwvif_go);
}

static void rtw89_mcc_start_beacon_noa(struct rtw89_dev *rtwdev)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;

	if (mcc->mode != RTW89_MCC_MODE_GO_STA)
		return;

	if (ref->is_go)
		rtw89_fw_h2c_tsf32_toggle(rtwdev, ref->rtwvif_link, true);
	else if (aux->is_go)
		rtw89_fw_h2c_tsf32_toggle(rtwdev, aux->rtwvif_link, true);

	rtw89_mcc_handle_beacon_noa(rtwdev, true);
}

static void rtw89_mcc_stop_beacon_noa(struct rtw89_dev *rtwdev)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_role *aux = &mcc->role_aux;

	if (mcc->mode != RTW89_MCC_MODE_GO_STA)
		return;

	if (ref->is_go)
		rtw89_fw_h2c_tsf32_toggle(rtwdev, ref->rtwvif_link, false);
	else if (aux->is_go)
		rtw89_fw_h2c_tsf32_toggle(rtwdev, aux->rtwvif_link, false);

	rtw89_mcc_handle_beacon_noa(rtwdev, false);
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

	mcc->group = RTW89_MCC_DFLT_GROUP;

	ret = rtw89_mcc_fill_config(rtwdev);
	if (ret)
		return ret;

	if (rtw89_concurrent_via_mrc(rtwdev))
		ret = __mrc_fw_start(rtwdev, false);
	else
		ret = __mcc_fw_start(rtwdev, false);

	if (ret)
		return ret;

	rtw89_chanctx_notify(rtwdev, RTW89_CHANCTX_STATE_MCC_START);

	rtw89_mcc_start_beacon_noa(rtwdev);
	return 0;
}

struct rtw89_mcc_stop_sel {
	struct {
		const struct rtw89_vif_link *target;
	} hint;

	/* selection content */
	bool filled;
	u8 mac_id;
	u8 slot_idx;
};

static void rtw89_mcc_stop_sel_fill(struct rtw89_mcc_stop_sel *sel,
				    const struct rtw89_mcc_role *mcc_role)
{
	sel->mac_id = mcc_role->rtwvif_link->mac_id;
	sel->slot_idx = mcc_role->slot_idx;
	sel->filled = true;
}

static int rtw89_mcc_stop_sel_iterator(struct rtw89_dev *rtwdev,
				       struct rtw89_mcc_role *mcc_role,
				       unsigned int ordered_idx,
				       void *data)
{
	struct rtw89_mcc_stop_sel *sel = data;

	if (mcc_role->rtwvif_link == sel->hint.target) {
		rtw89_mcc_stop_sel_fill(sel, mcc_role);
		return 1; /* break iteration */
	}

	if (sel->filled)
		return 0;

	if (!mcc_role->rtwvif_link->chanctx_assigned)
		return 0;

	rtw89_mcc_stop_sel_fill(sel, mcc_role);
	return 0;
}

static void rtw89_mcc_stop(struct rtw89_dev *rtwdev,
			   const struct rtw89_chanctx_pause_parm *pause)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_role *ref = &mcc->role_ref;
	struct rtw89_mcc_stop_sel sel = {
		.hint.target = pause ? pause->trigger : NULL,
	};
	int ret;

	if (!pause) {
		wiphy_delayed_work_cancel(rtwdev->hw->wiphy, &rtwdev->chanctx_work);
		bitmap_zero(hal->changes, NUM_OF_RTW89_CHANCTX_CHANGES);
	}

	/* by default, stop at ref */
	rtw89_iterate_mcc_roles(rtwdev, rtw89_mcc_stop_sel_iterator, &sel);
	if (!sel.filled)
		rtw89_mcc_stop_sel_fill(&sel, ref);

	rtw89_debug(rtwdev, RTW89_DBG_CHAN, "MCC stop at <macid %d>\n", sel.mac_id);

	if (rtw89_concurrent_via_mrc(rtwdev)) {
		ret = rtw89_fw_h2c_mrc_del(rtwdev, mcc->group, sel.slot_idx);
		if (ret)
			rtw89_debug(rtwdev, RTW89_DBG_CHAN,
				    "MRC h2c failed to trigger del: %d\n", ret);
	} else {
		ret = rtw89_fw_h2c_stop_mcc(rtwdev, mcc->group,
					    sel.mac_id, true);
		if (ret)
			rtw89_debug(rtwdev, RTW89_DBG_CHAN,
				    "MCC h2c failed to trigger stop: %d\n", ret);

		ret = rtw89_fw_h2c_del_mcc_group(rtwdev, mcc->group, true);
		if (ret)
			rtw89_debug(rtwdev, RTW89_DBG_CHAN,
				    "MCC h2c failed to delete group: %d\n", ret);
	}

	rtw89_chanctx_notify(rtwdev, RTW89_CHANCTX_STATE_MCC_STOP);

	rtw89_mcc_stop_beacon_noa(rtwdev);
}

static int rtw89_mcc_update(struct rtw89_dev *rtwdev)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_config *config = &mcc->config;
	struct rtw89_mcc_config old_cfg = *config;
	bool courtesy_changed;
	bool sync_changed;
	int ret;

	if (rtwdev->scanning)
		rtw89_hw_scan_abort(rtwdev, rtwdev->scan_info.scanning_vif);

	rtw89_debug(rtwdev, RTW89_DBG_CHAN, "MCC update\n");

	ret = rtw89_mcc_fill_config(rtwdev);
	if (ret)
		return ret;

	if (memcmp(&old_cfg.pattern.courtesy, &config->pattern.courtesy,
		   sizeof(old_cfg.pattern.courtesy)) == 0)
		courtesy_changed = false;
	else
		courtesy_changed = true;

	if (old_cfg.pattern.plan != RTW89_MCC_PLAN_NO_BT ||
	    config->pattern.plan != RTW89_MCC_PLAN_NO_BT ||
	    courtesy_changed) {
		if (rtw89_concurrent_via_mrc(rtwdev))
			ret = __mrc_fw_start(rtwdev, true);
		else
			ret = __mcc_fw_start(rtwdev, true);

		if (ret)
			return ret;
	} else {
		if (memcmp(&old_cfg.sync, &config->sync, sizeof(old_cfg.sync)) == 0)
			sync_changed = false;
		else
			sync_changed = true;

		if (rtw89_concurrent_via_mrc(rtwdev))
			ret = __mrc_fw_set_duration_no_bt(rtwdev, sync_changed);
		else
			ret = __mcc_fw_set_duration_no_bt(rtwdev, sync_changed);

		if (ret)
			return ret;
	}

	rtw89_mcc_handle_beacon_noa(rtwdev, true);
	return 0;
}

static void rtw89_mcc_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_mcc_config *config = &mcc->config;
	struct rtw89_mcc_pattern *pattern = &config->pattern;
	s16 tolerance;
	u16 bcn_ofst;
	u16 diff;

	if (mcc->mode != RTW89_MCC_MODE_GC_STA)
		return;

	bcn_ofst = rtw89_mcc_get_bcn_ofst(rtwdev);
	if (bcn_ofst > config->beacon_offset) {
		diff = bcn_ofst - config->beacon_offset;
		if (pattern->tob_aux < 0)
			tolerance = -pattern->tob_aux;
		else
			tolerance = pattern->toa_aux;
	} else {
		diff = config->beacon_offset - bcn_ofst;
		if (pattern->toa_aux < 0)
			tolerance = -pattern->toa_aux;
		else
			tolerance = pattern->tob_aux;
	}

	if (diff <= tolerance)
		return;

	rtw89_queue_chanctx_change(rtwdev, RTW89_CHANCTX_BCN_OFFSET_CHANGE);
}

static int __mcc_fw_upd_macid_bitmap(struct rtw89_dev *rtwdev,
				     struct rtw89_mcc_role *upd)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	int ret;

	ret = rtw89_fw_h2c_mcc_macid_bitmap(rtwdev, mcc->group,
					    upd->rtwvif_link->mac_id,
					    upd->macid_bitmap);
	if (ret) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN,
			    "MCC h2c failed to update macid bitmap: %d\n", ret);
		return ret;
	}

	return 0;
}

static int __mrc_fw_upd_macid_bitmap(struct rtw89_dev *rtwdev,
				     struct rtw89_mcc_role *cur,
				     struct rtw89_mcc_role *upd)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;
	struct rtw89_fw_mrc_upd_bitmap_arg arg = {};
	u32 old = rtw89_mcc_role_fw_macid_bitmap_to_u32(cur);
	u32 new = rtw89_mcc_role_fw_macid_bitmap_to_u32(upd);
	u32 add = new & ~old;
	u32 del = old & ~new;
	int ret;
	int i;

	arg.sch_idx = mcc->group;
	arg.macid = upd->rtwvif_link->mac_id;

	for (i = 0; i < 32; i++) {
		if (add & BIT(i)) {
			arg.client_macid = i;
			arg.action = RTW89_H2C_MRC_UPD_BITMAP_ACTION_ADD;

			ret = rtw89_fw_h2c_mrc_upd_bitmap(rtwdev, &arg);
			if (ret)
				goto err;
		}
	}

	for (i = 0; i < 32; i++) {
		if (del & BIT(i)) {
			arg.client_macid = i;
			arg.action = RTW89_H2C_MRC_UPD_BITMAP_ACTION_DEL;

			ret = rtw89_fw_h2c_mrc_upd_bitmap(rtwdev, &arg);
			if (ret)
				goto err;
		}
	}

	return 0;

err:
	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "MRC h2c failed to update bitmap: %d\n", ret);
	return ret;
}

static int rtw89_mcc_upd_map_iterator(struct rtw89_dev *rtwdev,
				      struct rtw89_mcc_role *mcc_role,
				      unsigned int ordered_idx,
				      void *data)
{
	struct rtw89_mcc_role upd = {
		.rtwvif_link = mcc_role->rtwvif_link,
	};
	int ret;

	if (!mcc_role->is_go)
		return 0;

	rtw89_mcc_fill_role_macid_bitmap(rtwdev, &upd);
	if (memcmp(mcc_role->macid_bitmap, upd.macid_bitmap,
		   sizeof(mcc_role->macid_bitmap)) == 0)
		return 0;

	if (rtw89_concurrent_via_mrc(rtwdev))
		ret = __mrc_fw_upd_macid_bitmap(rtwdev, mcc_role, &upd);
	else
		ret = __mcc_fw_upd_macid_bitmap(rtwdev, &upd);

	if (ret)
		return ret;

	memcpy(mcc_role->macid_bitmap, upd.macid_bitmap,
	       sizeof(mcc_role->macid_bitmap));
	return 0;
}

static void rtw89_mcc_update_macid_bitmap(struct rtw89_dev *rtwdev)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;

	if (mcc->mode != RTW89_MCC_MODE_GO_STA)
		return;

	rtw89_iterate_mcc_roles(rtwdev, rtw89_mcc_upd_map_iterator, NULL);
}

static int rtw89_mcc_upd_lmt_iterator(struct rtw89_dev *rtwdev,
				      struct rtw89_mcc_role *mcc_role,
				      unsigned int ordered_idx,
				      void *data)
{
	memset(&mcc_role->limit, 0, sizeof(mcc_role->limit));
	rtw89_mcc_fill_role_limit(rtwdev, mcc_role);
	return 0;
}

static void rtw89_mcc_update_limit(struct rtw89_dev *rtwdev)
{
	struct rtw89_mcc_info *mcc = &rtwdev->mcc;

	if (mcc->mode != RTW89_MCC_MODE_GC_STA)
		return;

	rtw89_iterate_mcc_roles(rtwdev, rtw89_mcc_upd_lmt_iterator, NULL);
}

void rtw89_chanctx_work(struct wiphy *wiphy, struct wiphy_work *work)
{
	struct rtw89_dev *rtwdev = container_of(work, struct rtw89_dev,
						chanctx_work.work);
	struct rtw89_hal *hal = &rtwdev->hal;
	bool update_mcc_pattern = false;
	enum rtw89_entity_mode mode;
	u32 changed = 0;
	int ret;
	int i;

	lockdep_assert_wiphy(wiphy);

	if (hal->entity_pause)
		return;

	for (i = 0; i < NUM_OF_RTW89_CHANCTX_CHANGES; i++) {
		if (test_and_clear_bit(i, hal->changes))
			changed |= BIT(i);
	}

	mode = rtw89_get_entity_mode(rtwdev);
	switch (mode) {
	case RTW89_ENTITY_MODE_MCC_PREPARE:
		rtw89_set_entity_mode(rtwdev, RTW89_ENTITY_MODE_MCC);
		rtw89_set_channel(rtwdev);

		ret = rtw89_mcc_start(rtwdev);
		if (ret)
			rtw89_warn(rtwdev, "failed to start MCC: %d\n", ret);
		break;
	case RTW89_ENTITY_MODE_MCC:
		if (changed & BIT(RTW89_CHANCTX_BCN_OFFSET_CHANGE) ||
		    changed & BIT(RTW89_CHANCTX_P2P_PS_CHANGE) ||
		    changed & BIT(RTW89_CHANCTX_BT_SLOT_CHANGE) ||
		    changed & BIT(RTW89_CHANCTX_TSF32_TOGGLE_CHANGE))
			update_mcc_pattern = true;
		if (changed & BIT(RTW89_CHANCTX_REMOTE_STA_CHANGE))
			rtw89_mcc_update_macid_bitmap(rtwdev);
		if (changed & BIT(RTW89_CHANCTX_P2P_PS_CHANGE))
			rtw89_mcc_update_limit(rtwdev);
		if (changed & BIT(RTW89_CHANCTX_BT_SLOT_CHANGE))
			rtw89_mcc_fill_bt_role(rtwdev);
		if (update_mcc_pattern) {
			ret = rtw89_mcc_update(rtwdev);
			if (ret)
				rtw89_warn(rtwdev, "failed to update MCC: %d\n",
					   ret);
		}
		break;
	default:
		break;
	}
}

void rtw89_queue_chanctx_change(struct rtw89_dev *rtwdev,
				enum rtw89_chanctx_changes change)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	enum rtw89_entity_mode mode;
	u32 delay;

	mode = rtw89_get_entity_mode(rtwdev);
	switch (mode) {
	default:
		return;
	case RTW89_ENTITY_MODE_MCC_PREPARE:
		delay = ieee80211_tu_to_usec(RTW89_CHANCTX_TIME_MCC_PREPARE);
		break;
	case RTW89_ENTITY_MODE_MCC:
		delay = ieee80211_tu_to_usec(RTW89_CHANCTX_TIME_MCC);
		break;
	}

	if (change != RTW89_CHANCTX_CHANGE_DFLT) {
		rtw89_debug(rtwdev, RTW89_DBG_CHAN, "set chanctx change %d\n",
			    change);
		set_bit(change, hal->changes);
	}

	rtw89_debug(rtwdev, RTW89_DBG_CHAN,
		    "queue chanctx work for mode %d with delay %d us\n",
		    mode, delay);
	wiphy_delayed_work_queue(rtwdev->hw->wiphy, &rtwdev->chanctx_work,
				 usecs_to_jiffies(delay));
}

void rtw89_queue_chanctx_work(struct rtw89_dev *rtwdev)
{
	rtw89_queue_chanctx_change(rtwdev, RTW89_CHANCTX_CHANGE_DFLT);
}

void rtw89_chanctx_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	enum rtw89_entity_mode mode;

	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	if (hal->entity_pause)
		return;

	mode = rtw89_get_entity_mode(rtwdev);
	switch (mode) {
	case RTW89_ENTITY_MODE_MCC:
		rtw89_mcc_track(rtwdev);
		break;
	default:
		break;
	}
}

void rtw89_chanctx_pause(struct rtw89_dev *rtwdev,
			 const struct rtw89_chanctx_pause_parm *pause_parm)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	enum rtw89_entity_mode mode;

	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	if (hal->entity_pause)
		return;

	rtw89_debug(rtwdev, RTW89_DBG_CHAN, "chanctx pause (rsn: %d)\n", pause_parm->rsn);

	mode = rtw89_get_entity_mode(rtwdev);
	switch (mode) {
	case RTW89_ENTITY_MODE_MCC:
		rtw89_mcc_stop(rtwdev, pause_parm);
		break;
	default:
		break;
	}

	hal->entity_pause = true;
}

static void rtw89_chanctx_proceed_cb(struct rtw89_dev *rtwdev,
				     const struct rtw89_chanctx_cb_parm *parm)
{
	int ret;

	if (!parm || !parm->cb)
		return;

	ret = parm->cb(rtwdev, parm->data);
	if (ret)
		rtw89_warn(rtwdev, "%s (%s): cb failed: %d\n", __func__,
			   parm->caller ?: "unknown", ret);
}

/* pass @cb_parm if there is a @cb_parm->cb which needs to invoke right after
 * call rtw89_set_channel() and right before proceed entity according to mode.
 */
void rtw89_chanctx_proceed(struct rtw89_dev *rtwdev,
			   const struct rtw89_chanctx_cb_parm *cb_parm)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	enum rtw89_entity_mode mode;
	int ret;

	lockdep_assert_wiphy(rtwdev->hw->wiphy);

	if (unlikely(!hal->entity_pause)) {
		rtw89_chanctx_proceed_cb(rtwdev, cb_parm);
		return;
	}

	rtw89_debug(rtwdev, RTW89_DBG_CHAN, "chanctx proceed\n");

	hal->entity_pause = false;
	rtw89_set_channel(rtwdev);

	rtw89_chanctx_proceed_cb(rtwdev, cb_parm);

	mode = rtw89_get_entity_mode(rtwdev);
	switch (mode) {
	case RTW89_ENTITY_MODE_MCC:
		ret = rtw89_mcc_start(rtwdev);
		if (ret)
			rtw89_warn(rtwdev, "failed to start MCC: %d\n", ret);
		break;
	default:
		break;
	}

	rtw89_queue_chanctx_work(rtwdev);
}

static void __rtw89_swap_chanctx(struct rtw89_vif *rtwvif,
				 enum rtw89_chanctx_idx idx1,
				 enum rtw89_chanctx_idx idx2)
{
	struct rtw89_vif_link *rtwvif_link;
	unsigned int link_id;

	rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id) {
		if (!rtwvif_link->chanctx_assigned)
			continue;

		if (rtwvif_link->chanctx_idx == idx1)
			rtwvif_link->chanctx_idx = idx2;
		else if (rtwvif_link->chanctx_idx == idx2)
			rtwvif_link->chanctx_idx = idx1;
	}
}

static void rtw89_swap_chanctx(struct rtw89_dev *rtwdev,
			       enum rtw89_chanctx_idx idx1,
			       enum rtw89_chanctx_idx idx2)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_vif *rtwvif;
	u8 cur;

	if (idx1 == idx2)
		return;

	hal->chanctx[idx1].cfg->idx = idx2;
	hal->chanctx[idx2].cfg->idx = idx1;

	swap(hal->chanctx[idx1], hal->chanctx[idx2]);

	rtw89_for_each_rtwvif(rtwdev, rtwvif)
		__rtw89_swap_chanctx(rtwvif, idx1, idx2);

	cur = atomic_read(&hal->roc_chanctx_idx);
	if (cur == idx1)
		atomic_set(&hal->roc_chanctx_idx, idx2);
	else if (cur == idx2)
		atomic_set(&hal->roc_chanctx_idx, idx1);
}

int rtw89_chanctx_ops_add(struct rtw89_dev *rtwdev,
			  struct ieee80211_chanctx_conf *ctx)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_chanctx_cfg *cfg = (struct rtw89_chanctx_cfg *)ctx->drv_priv;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u8 idx;

	idx = find_first_zero_bit(hal->entity_map, NUM_OF_RTW89_CHANCTX);
	if (idx >= chip->support_chanctx_num)
		return -ENOENT;

	rtw89_config_entity_chandef(rtwdev, idx, &ctx->def);
	cfg->idx = idx;
	cfg->ref_count = 0;
	hal->chanctx[idx].cfg = cfg;
	return 0;
}

void rtw89_chanctx_ops_remove(struct rtw89_dev *rtwdev,
			      struct ieee80211_chanctx_conf *ctx)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_chanctx_cfg *cfg = (struct rtw89_chanctx_cfg *)ctx->drv_priv;

	clear_bit(cfg->idx, hal->entity_map);
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
				 struct rtw89_vif_link *rtwvif_link,
				 struct ieee80211_chanctx_conf *ctx)
{
	struct rtw89_chanctx_cfg *cfg = (struct rtw89_chanctx_cfg *)ctx->drv_priv;
	struct rtw89_vif *rtwvif = rtwvif_link->rtwvif;
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_entity_mgnt *mgnt = &hal->entity_mgnt;
	struct rtw89_entity_weight w = {};
	int ret;

	rtwvif_link->chanctx_idx = cfg->idx;
	rtwvif_link->chanctx_assigned = true;
	cfg->ref_count++;

	if (list_empty(&rtwvif->mgnt_entry))
		list_add_tail(&rtwvif->mgnt_entry, &mgnt->active_list);

	if (cfg->idx == RTW89_CHANCTX_0)
		goto out;

	rtw89_entity_calculate_weight(rtwdev, &w);
	if (w.active_chanctxs != 1)
		goto out;

	/* put the first active chanctx at RTW89_CHANCTX_0 */
	rtw89_swap_chanctx(rtwdev, cfg->idx, RTW89_CHANCTX_0);

out:
	ret = rtw89_set_channel(rtwdev);
	if (ret)
		return ret;

	rtw89_tas_reset(rtwdev, true);

	return 0;
}

void rtw89_chanctx_ops_unassign_vif(struct rtw89_dev *rtwdev,
				    struct rtw89_vif_link *rtwvif_link,
				    struct ieee80211_chanctx_conf *ctx)
{
	struct rtw89_chanctx_cfg *cfg = (struct rtw89_chanctx_cfg *)ctx->drv_priv;
	struct rtw89_vif *rtwvif = rtwvif_link->rtwvif;
	struct rtw89_hal *hal = &rtwdev->hal;
	enum rtw89_chanctx_idx roll;
	enum rtw89_entity_mode cur;
	enum rtw89_entity_mode new;
	int ret;

	rtwvif_link->chanctx_idx = RTW89_CHANCTX_0;
	rtwvif_link->chanctx_assigned = false;
	cfg->ref_count--;

	if (!rtw89_vif_is_active_role(rtwvif))
		list_del_init(&rtwvif->mgnt_entry);

	if (cfg->ref_count != 0)
		goto out;

	if (cfg->idx != RTW89_CHANCTX_0)
		goto out;

	roll = find_next_bit(hal->entity_map, NUM_OF_RTW89_CHANCTX,
			     cfg->idx + 1);
	/* Follow rtw89_config_default_chandef() when rtw89_entity_recalc(). */
	if (roll == NUM_OF_RTW89_CHANCTX)
		goto out;

	/* RTW89_CHANCTX_0 is going to release, and another exists.
	 * Make another roll down to RTW89_CHANCTX_0 to replace.
	 */
	rtw89_swap_chanctx(rtwdev, cfg->idx, roll);

out:
	if (!hal->entity_pause) {
		cur = rtw89_get_entity_mode(rtwdev);
		switch (cur) {
		case RTW89_ENTITY_MODE_MCC:
			rtw89_mcc_stop(rtwdev, NULL);
			break;
		default:
			break;
		}
	}

	ret = rtw89_set_channel(rtwdev);
	if (ret)
		return;

	if (hal->entity_pause)
		return;

	new = rtw89_get_entity_mode(rtwdev);
	switch (new) {
	case RTW89_ENTITY_MODE_MCC:
		/* re-plan MCC for chanctx changes. */
		ret = rtw89_mcc_start(rtwdev);
		if (ret)
			rtw89_warn(rtwdev, "failed to start MCC: %d\n", ret);
		break;
	default:
		break;
	}
}
