/******************************************************************************
 *
 * Copyright(c) 2005 - 2007 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/wireless.h>
#include <net/mac80211.h>
#include <net/ieee80211.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>

#include <linux/workqueue.h>

#define IWL 4965

#include "../net/mac80211/ieee80211_rate.h"

#include "iwlwifi.h"
#include "iwl-helpers.h"

#define RS_NAME "iwl-4965-rs"

#define NUM_TRY_BEFORE_ANTENNA_TOGGLE 1
#define IWL_NUMBER_TRY      1
#define IWL_HT_NUMBER_TRY   3

#define IWL_RATE_MAX_WINDOW		62
#define IWL_RATE_HIGH_TH		10880
#define IWL_RATE_MIN_FAILURE_TH		6
#define IWL_RATE_MIN_SUCCESS_TH		8
#define IWL_RATE_DECREASE_TH		1920
#define IWL_RATE_INCREASE_TH            8960
#define IWL_RATE_SCALE_FLUSH_INTVL   (2*HZ)        /*2 seconds */

static u8 rs_ht_to_legacy[] = {
	IWL_RATE_6M_INDEX, IWL_RATE_6M_INDEX,
	IWL_RATE_6M_INDEX, IWL_RATE_6M_INDEX,
	IWL_RATE_6M_INDEX,
	IWL_RATE_6M_INDEX, IWL_RATE_9M_INDEX,
	IWL_RATE_12M_INDEX, IWL_RATE_18M_INDEX,
	IWL_RATE_24M_INDEX, IWL_RATE_36M_INDEX,
	IWL_RATE_48M_INDEX, IWL_RATE_54M_INDEX
};

struct iwl_rate {
	u32 rate_n_flags;
} __attribute__ ((packed));

struct iwl_rate_scale_data {
	u64 data;
	s32 success_counter;
	s32 success_ratio;
	s32 counter;
	s32 average_tpt;
	unsigned long stamp;
};

struct iwl_scale_tbl_info {
	enum iwl_table_type lq_type;
	enum iwl_antenna_type antenna_type;
	u8 is_SGI;
	u8 is_fat;
	u8 is_dup;
	u8 action;
	s32 *expected_tpt;
	struct iwl_rate current_rate;
	struct iwl_rate_scale_data win[IWL_RATE_COUNT];
};

struct iwl_rate_scale_priv {
	u8 active_tbl;
	u8 enable_counter;
	u8 stay_in_tbl;
	u8 search_better_tbl;
	s32 last_tpt;
	u32 table_count_limit;
	u32 max_failure_limit;
	u32 max_success_limit;
	u32 table_count;
	u32 total_failed;
	u32 total_success;
	u32 flush_timer;
	u8 action_counter;
	u8 antenna;
	u8 valid_antenna;
	u8 is_green;
	u8 is_dup;
	u8 phymode;
	u8 ibss_sta_added;
	u32 supp_rates;
	u16 active_rate;
	u16 active_siso_rate;
	u16 active_mimo_rate;
	u16 active_rate_basic;
	struct iwl_link_quality_cmd lq;
	struct iwl_scale_tbl_info lq_info[LQ_SIZE];
#ifdef CONFIG_MAC80211_DEBUGFS
	struct dentry *rs_sta_dbgfs_scale_table_file;
	struct dentry *rs_sta_dbgfs_stats_table_file;
	struct iwl_rate dbg_fixed;
	struct iwl_priv *drv;
#endif
};

static void rs_rate_scale_perform(struct iwl_priv *priv,
				   struct net_device *dev,
				   struct ieee80211_hdr *hdr,
				   struct sta_info *sta);
static void rs_fill_link_cmd(struct iwl_rate_scale_priv *lq_data,
			     struct iwl_rate *tx_mcs,
			     struct iwl_link_quality_cmd *tbl);


#ifdef CONFIG_MAC80211_DEBUGFS
static void rs_dbgfs_set_mcs(struct iwl_rate_scale_priv *rs_priv,
				struct iwl_rate *mcs, int index);
#else
static void rs_dbgfs_set_mcs(struct iwl_rate_scale_priv *rs_priv,
				struct iwl_rate *mcs, int index)
{}
#endif
static s32 expected_tpt_A[IWL_RATE_COUNT] = {
	0, 0, 0, 0, 40, 57, 72, 98, 121, 154, 177, 186, 186
};

static s32 expected_tpt_G[IWL_RATE_COUNT] = {
	7, 13, 35, 58, 40, 57, 72, 98, 121, 154, 177, 186, 186
};

static s32 expected_tpt_siso20MHz[IWL_RATE_COUNT] = {
	0, 0, 0, 0, 42, 42, 76, 102, 124, 159, 183, 193, 202
};

static s32 expected_tpt_siso20MHzSGI[IWL_RATE_COUNT] = {
	0, 0, 0, 0, 46, 46, 82, 110, 132, 168, 192, 202, 211
};

static s32 expected_tpt_mimo20MHz[IWL_RATE_COUNT] = {
	0, 0, 0, 0, 74, 74, 123, 155, 179, 214, 236, 244, 251
};

static s32 expected_tpt_mimo20MHzSGI[IWL_RATE_COUNT] = {
	0, 0, 0, 0, 81, 81, 131, 164, 188, 222, 243, 251, 257
};

static s32 expected_tpt_siso40MHz[IWL_RATE_COUNT] = {
	0, 0, 0, 0, 77, 77, 127, 160, 184, 220, 242, 250, 257
};

static s32 expected_tpt_siso40MHzSGI[IWL_RATE_COUNT] = {
	0, 0, 0, 0, 83, 83, 135, 169, 193, 229, 250, 257, 264
};

static s32 expected_tpt_mimo40MHz[IWL_RATE_COUNT] = {
	0, 0, 0, 0, 123, 123, 182, 214, 235, 264, 279, 285, 289
};

static s32 expected_tpt_mimo40MHzSGI[IWL_RATE_COUNT] = {
	0, 0, 0, 0, 131, 131, 191, 222, 242, 270, 284, 289, 293
};

static int iwl_lq_sync_callback(struct iwl_priv *priv,
				struct iwl_cmd *cmd, struct sk_buff *skb)
{
	/*We didn't cache the SKB; let the caller free it */
	return 1;
}

static inline u8 iwl_rate_get_rate(u32 rate_n_flags)
{
	return (u8)(rate_n_flags & 0xFF);
}

static int rs_send_lq_cmd(struct iwl_priv *priv,
			  struct iwl_link_quality_cmd *lq, u8 flags)
{
#ifdef CONFIG_IWLWIFI_DEBUG
	int i;
#endif
	int rc = -1;

	struct iwl_host_cmd cmd = {
		.id = REPLY_TX_LINK_QUALITY_CMD,
		.len = sizeof(struct iwl_link_quality_cmd),
		.meta.flags = flags,
		.data = lq,
	};

	if ((lq->sta_id == 0xFF) &&
	    (priv->iw_mode == IEEE80211_IF_TYPE_IBSS))
		return rc;

	if (lq->sta_id == 0xFF)
		lq->sta_id = IWL_AP_ID;

	IWL_DEBUG_RATE("lq station id 0x%x\n", lq->sta_id);
	IWL_DEBUG_RATE("lq dta 0x%X 0x%X\n",
		       lq->general_params.single_stream_ant_msk,
		       lq->general_params.dual_stream_ant_msk);
#ifdef CONFIG_IWLWIFI_DEBUG
	for (i = 0; i < LINK_QUAL_MAX_RETRY_NUM; i++)
		IWL_DEBUG_RATE("lq index %d 0x%X\n",
				i, lq->rs_table[i].rate_n_flags);
#endif

	if (flags & CMD_ASYNC)
		cmd.meta.u.callback = iwl_lq_sync_callback;

	if (iwl_is_associated(priv) && priv->assoc_station_added &&
	    priv->lq_mngr.lq_ready)
		rc = iwl_send_cmd(priv, &cmd);

	return rc;
}

static int rs_rate_scale_clear_window(struct iwl_rate_scale_data *window)
{
	window->data = 0;
	window->success_counter = 0;
	window->success_ratio = IWL_INVALID_VALUE;
	window->counter = 0;
	window->average_tpt = IWL_INVALID_VALUE;
	window->stamp = 0;

	return 0;
}

static int rs_collect_tx_data(struct iwl_rate_scale_data *windows,
			      int scale_index, s32 tpt, u32 status)
{
	int rc = 0;
	struct iwl_rate_scale_data *window = NULL;
	u64 mask;
	u8 win_size = IWL_RATE_MAX_WINDOW;
	s32 fail_count;

	if (scale_index < 0)
		return -1;

	if (scale_index >= IWL_RATE_COUNT)
		return -1;

	window = &(windows[scale_index]);

	if (window->counter >= win_size) {

		window->counter = win_size - 1;
		mask = 1;
		mask = (mask << (win_size - 1));
		if ((window->data & mask)) {
			window->data &= ~mask;
			window->success_counter = window->success_counter - 1;
		}
	}

	window->counter = window->counter + 1;
	mask = window->data;
	window->data = (mask << 1);
	if (status != 0) {
		window->success_counter = window->success_counter + 1;
		window->data |= 0x1;
	}

	if (window->counter > 0)
		window->success_ratio = 128 * (100 * window->success_counter)
					/ window->counter;
	else
		window->success_ratio = IWL_INVALID_VALUE;

	fail_count = window->counter - window->success_counter;

	if ((fail_count >= IWL_RATE_MIN_FAILURE_TH) ||
	    (window->success_counter >= IWL_RATE_MIN_SUCCESS_TH))
		window->average_tpt = (window->success_ratio * tpt + 64) / 128;
	else
		window->average_tpt = IWL_INVALID_VALUE;

	window->stamp = jiffies;

	return rc;
}

int static rs_mcs_from_tbl(struct iwl_rate *mcs_rate,
			   struct iwl_scale_tbl_info *tbl,
			   int index, u8 use_green)
{
	int rc = 0;

	if (is_legacy(tbl->lq_type)) {
		mcs_rate->rate_n_flags = iwl_rates[index].plcp;
		if (index >= IWL_FIRST_CCK_RATE && index <= IWL_LAST_CCK_RATE)
			mcs_rate->rate_n_flags |= RATE_MCS_CCK_MSK;

	} else if (is_siso(tbl->lq_type)) {
		if (index > IWL_LAST_OFDM_RATE)
			index = IWL_LAST_OFDM_RATE;
		 mcs_rate->rate_n_flags = iwl_rates[index].plcp_siso |
					  RATE_MCS_HT_MSK;
	} else {
		if (index > IWL_LAST_OFDM_RATE)
			index = IWL_LAST_OFDM_RATE;
		mcs_rate->rate_n_flags = iwl_rates[index].plcp_mimo |
					 RATE_MCS_HT_MSK;
	}

	switch (tbl->antenna_type) {
	case ANT_BOTH:
		mcs_rate->rate_n_flags |= RATE_MCS_ANT_AB_MSK;
		break;
	case ANT_MAIN:
		mcs_rate->rate_n_flags |= RATE_MCS_ANT_A_MSK;
		break;
	case ANT_AUX:
		mcs_rate->rate_n_flags |= RATE_MCS_ANT_B_MSK;
		break;
	case ANT_NONE:
		break;
	}

	if (is_legacy(tbl->lq_type))
		return rc;

	if (tbl->is_fat) {
		if (tbl->is_dup)
			mcs_rate->rate_n_flags |= RATE_MCS_DUP_MSK;
		else
			mcs_rate->rate_n_flags |= RATE_MCS_FAT_MSK;
	}
	if (tbl->is_SGI)
		mcs_rate->rate_n_flags |= RATE_MCS_SGI_MSK;

	if (use_green) {
		mcs_rate->rate_n_flags |= RATE_MCS_GF_MSK;
		if (is_siso(tbl->lq_type))
			mcs_rate->rate_n_flags &= ~RATE_MCS_SGI_MSK;
	}
	return rc;
}

static int rs_get_tbl_info_from_mcs(const struct iwl_rate *mcs_rate,
				    int phymode, struct iwl_scale_tbl_info *tbl,
				    int *rate_idx)
{
	int index;
	u32 ant_msk;

	index = iwl_rate_index_from_plcp(mcs_rate->rate_n_flags);

	if (index  == IWL_RATE_INVALID) {
		*rate_idx = -1;
		return -1;
	}
	tbl->is_SGI = 0;
	tbl->is_fat = 0;
	tbl->is_dup = 0;
	tbl->antenna_type = ANT_BOTH;

	if (!(mcs_rate->rate_n_flags & RATE_MCS_HT_MSK)) {
		ant_msk = (mcs_rate->rate_n_flags & RATE_MCS_ANT_AB_MSK);

		if (ant_msk == RATE_MCS_ANT_AB_MSK)
			tbl->lq_type = LQ_NONE;
		else {

			if (phymode == MODE_IEEE80211A)
				tbl->lq_type = LQ_A;
			else
				tbl->lq_type = LQ_G;

			if (mcs_rate->rate_n_flags & RATE_MCS_ANT_A_MSK)
				tbl->antenna_type = ANT_MAIN;
			else
				tbl->antenna_type = ANT_AUX;
		}
		*rate_idx = index;

	} else if (iwl_rate_get_rate(mcs_rate->rate_n_flags)
					<= IWL_RATE_SISO_60M_PLCP) {
		tbl->lq_type = LQ_SISO;

		ant_msk = (mcs_rate->rate_n_flags & RATE_MCS_ANT_AB_MSK);
		if (ant_msk == RATE_MCS_ANT_AB_MSK)
			tbl->lq_type = LQ_NONE;
		else {
			if (mcs_rate->rate_n_flags & RATE_MCS_ANT_A_MSK)
				tbl->antenna_type = ANT_MAIN;
			else
				tbl->antenna_type = ANT_AUX;
		}
		if (mcs_rate->rate_n_flags & RATE_MCS_SGI_MSK)
			tbl->is_SGI = 1;

		if ((mcs_rate->rate_n_flags & RATE_MCS_FAT_MSK) ||
		    (mcs_rate->rate_n_flags & RATE_MCS_DUP_MSK))
			tbl->is_fat = 1;

		if (mcs_rate->rate_n_flags & RATE_MCS_DUP_MSK)
			tbl->is_dup = 1;

		*rate_idx = index;
	} else {
		tbl->lq_type = LQ_MIMO;
		if (mcs_rate->rate_n_flags & RATE_MCS_SGI_MSK)
			tbl->is_SGI = 1;

		if ((mcs_rate->rate_n_flags & RATE_MCS_FAT_MSK) ||
		    (mcs_rate->rate_n_flags & RATE_MCS_DUP_MSK))
			tbl->is_fat = 1;

		if (mcs_rate->rate_n_flags & RATE_MCS_DUP_MSK)
			tbl->is_dup = 1;
		*rate_idx = index;
	}
	return 0;
}

static inline void rs_toggle_antenna(struct iwl_rate *new_rate,
				     struct iwl_scale_tbl_info *tbl)
{
	if (tbl->antenna_type == ANT_AUX) {
		tbl->antenna_type = ANT_MAIN;
		new_rate->rate_n_flags &= ~RATE_MCS_ANT_B_MSK;
		new_rate->rate_n_flags |= RATE_MCS_ANT_A_MSK;
	} else {
		tbl->antenna_type = ANT_AUX;
		new_rate->rate_n_flags &= ~RATE_MCS_ANT_A_MSK;
		new_rate->rate_n_flags |= RATE_MCS_ANT_B_MSK;
	}
}

static inline s8 rs_use_green(struct iwl_priv *priv)
{
	s8 rc = 0;
#ifdef CONFIG_IWLWIFI_HT
	if (!priv->is_ht_enabled || !priv->current_assoc_ht.is_ht)
		return 0;

	if ((priv->current_assoc_ht.is_green_field) &&
	    !(priv->current_assoc_ht.operating_mode & 0x4))
		rc = 1;
#endif	/*CONFIG_IWLWIFI_HT */
	return rc;
}

/**
 * rs_get_supported_rates - get the available rates
 *
 * if management frame or broadcast frame only return
 * basic available rates.
 *
 */
static void rs_get_supported_rates(struct iwl_rate_scale_priv *lq_data,
				   struct ieee80211_hdr *hdr,
				   enum iwl_table_type rate_type,
				   u16 *data_rate)
{
	if (is_legacy(rate_type))
		*data_rate = lq_data->active_rate;
	else {
		if (is_siso(rate_type))
			*data_rate = lq_data->active_siso_rate;
		else
			*data_rate = lq_data->active_mimo_rate;
	}

	if (hdr && is_multicast_ether_addr(hdr->addr1) &&
	    lq_data->active_rate_basic)
		*data_rate = lq_data->active_rate_basic;
}

static u16 rs_get_adjacent_rate(u8 index, u16 rate_mask, int rate_type)
{
	u8 high = IWL_RATE_INVALID;
	u8 low = IWL_RATE_INVALID;

	/* 802.11A or ht walks to the next literal adjascent rate in
	 * the rate table */
	if (is_a_band(rate_type) || !is_legacy(rate_type)) {
		int i;
		u32 mask;

		/* Find the previous rate that is in the rate mask */
		i = index - 1;
		for (mask = (1 << i); i >= 0; i--, mask >>= 1) {
			if (rate_mask & mask) {
				low = i;
				break;
			}
		}

		/* Find the next rate that is in the rate mask */
		i = index + 1;
		for (mask = (1 << i); i < IWL_RATE_COUNT; i++, mask <<= 1) {
			if (rate_mask & mask) {
				high = i;
				break;
			}
		}

		return (high << 8) | low;
	}

	low = index;
	while (low != IWL_RATE_INVALID) {
		low = iwl_rates[low].prev_rs;
		if (low == IWL_RATE_INVALID)
			break;
		if (rate_mask & (1 << low))
			break;
		IWL_DEBUG_RATE("Skipping masked lower rate: %d\n", low);
	}

	high = index;
	while (high != IWL_RATE_INVALID) {
		high = iwl_rates[high].next_rs;
		if (high == IWL_RATE_INVALID)
			break;
		if (rate_mask & (1 << high))
			break;
		IWL_DEBUG_RATE("Skipping masked higher rate: %d\n", high);
	}

	return (high << 8) | low;
}

static int rs_get_lower_rate(struct iwl_rate_scale_priv *lq_data,
			     struct iwl_scale_tbl_info *tbl, u8 scale_index,
			     u8 ht_possible, struct iwl_rate *mcs_rate)
{
	s32 low;
	u16 rate_mask;
	u16 high_low;
	u8 switch_to_legacy = 0;
	u8 is_green = lq_data->is_green;

	/* check if we need to switch from HT to legacy rates.
	 * assumption is that mandatory rates (1Mbps or 6Mbps)
	 * are always supported (spec demand) */
	if (!is_legacy(tbl->lq_type) && (!ht_possible || !scale_index)) {
		switch_to_legacy = 1;
		scale_index = rs_ht_to_legacy[scale_index];
		if (lq_data->phymode == MODE_IEEE80211A)
			tbl->lq_type = LQ_A;
		else
			tbl->lq_type = LQ_G;

		if ((tbl->antenna_type == ANT_BOTH) ||
		    (tbl->antenna_type == ANT_NONE))
			tbl->antenna_type = ANT_MAIN;

		tbl->is_fat = 0;
		tbl->is_SGI = 0;
	}

	rs_get_supported_rates(lq_data, NULL, tbl->lq_type, &rate_mask);

	/* mask with station rate restriction */
	if (is_legacy(tbl->lq_type)) {
		if (lq_data->phymode == (u8) MODE_IEEE80211A)
			rate_mask  = (u16)(rate_mask &
			   (lq_data->supp_rates << IWL_FIRST_OFDM_RATE));
		else
			rate_mask = (u16)(rate_mask & lq_data->supp_rates);
	}

	/* if we did switched from HT to legacy check current rate */
	if ((switch_to_legacy) &&
	    (rate_mask & (1 << scale_index))) {
		rs_mcs_from_tbl(mcs_rate, tbl, scale_index, is_green);
		return 0;
	}

	high_low = rs_get_adjacent_rate(scale_index, rate_mask, tbl->lq_type);
	low = high_low & 0xff;

	if (low != IWL_RATE_INVALID)
		rs_mcs_from_tbl(mcs_rate, tbl, low, is_green);
	else
		rs_mcs_from_tbl(mcs_rate, tbl, scale_index, is_green);

	return 0;
}

static void rs_tx_status(void *priv_rate,
			 struct net_device *dev,
			 struct sk_buff *skb,
			 struct ieee80211_tx_status *tx_resp)
{
	int status;
	u8 retries;
	int rs_index, index = 0;
	struct iwl_rate_scale_priv *lq;
	struct iwl_link_quality_cmd *table;
	struct sta_info *sta;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct iwl_priv *priv = (struct iwl_priv *)priv_rate;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct iwl_rate_scale_data *window = NULL;
	struct iwl_rate_scale_data *search_win = NULL;
	struct iwl_rate tx_mcs;
	struct iwl_scale_tbl_info tbl_type;
	struct iwl_scale_tbl_info *curr_tbl, *search_tbl;
	u8 active_index = 0;
	u16 fc = le16_to_cpu(hdr->frame_control);
	s32 tpt = 0;

	IWL_DEBUG_RATE_LIMIT("get frame ack response, update rate scale window\n");

	if (!ieee80211_is_data(fc) || is_multicast_ether_addr(hdr->addr1))
		return;

	retries = tx_resp->retry_count;

	if (retries > 15)
		retries = 15;


	sta = sta_info_get(local, hdr->addr1);

	if (!sta || !sta->rate_ctrl_priv) {
		if (sta)
			sta_info_put(sta);
		return;
	}

	lq = (struct iwl_rate_scale_priv *)sta->rate_ctrl_priv;

	if (!priv->lq_mngr.lq_ready)
		return;

	if ((priv->iw_mode == IEEE80211_IF_TYPE_IBSS) && !lq->ibss_sta_added)
		return;

	table = &lq->lq;
	active_index = lq->active_tbl;

	lq->antenna = (lq->valid_antenna & local->hw.conf.antenna_sel_tx);
	if (!lq->antenna)
		lq->antenna = lq->valid_antenna;

	lq->antenna = lq->valid_antenna;
	curr_tbl = &(lq->lq_info[active_index]);
	search_tbl = &(lq->lq_info[(1 - active_index)]);
	window = (struct iwl_rate_scale_data *)
	    &(curr_tbl->win[0]);
	search_win = (struct iwl_rate_scale_data *)
	    &(search_tbl->win[0]);

	tx_mcs.rate_n_flags = tx_resp->control.tx_rate;

	rs_get_tbl_info_from_mcs(&tx_mcs, priv->phymode,
				  &tbl_type, &rs_index);
	if ((rs_index < 0) || (rs_index >= IWL_RATE_COUNT)) {
		IWL_DEBUG_RATE("bad rate index at: %d rate 0x%X\n",
			     rs_index, tx_mcs.rate_n_flags);
		sta_info_put(sta);
		return;
	}

	if (retries &&
	    (tx_mcs.rate_n_flags !=
				le32_to_cpu(table->rs_table[0].rate_n_flags))) {
		IWL_DEBUG_RATE("initial rate does not match 0x%x 0x%x\n",
				tx_mcs.rate_n_flags,
				le32_to_cpu(table->rs_table[0].rate_n_flags));
		sta_info_put(sta);
		return;
	}

	while (retries) {
		tx_mcs.rate_n_flags =
		    le32_to_cpu(table->rs_table[index].rate_n_flags);
		rs_get_tbl_info_from_mcs(&tx_mcs, priv->phymode,
					  &tbl_type, &rs_index);

		if ((tbl_type.lq_type == search_tbl->lq_type) &&
		    (tbl_type.antenna_type == search_tbl->antenna_type) &&
		    (tbl_type.is_SGI == search_tbl->is_SGI)) {
			if (search_tbl->expected_tpt)
				tpt = search_tbl->expected_tpt[rs_index];
			else
				tpt = 0;
			rs_collect_tx_data(search_win,
					    rs_index, tpt, 0);
		} else if ((tbl_type.lq_type == curr_tbl->lq_type) &&
			   (tbl_type.antenna_type == curr_tbl->antenna_type) &&
			   (tbl_type.is_SGI == curr_tbl->is_SGI)) {
			if (curr_tbl->expected_tpt)
				tpt = curr_tbl->expected_tpt[rs_index];
			else
				tpt = 0;
			rs_collect_tx_data(window, rs_index, tpt, 0);
		}
		if (lq->stay_in_tbl)
			lq->total_failed++;
		--retries;
		index++;

	}

	if (!tx_resp->retry_count)
		tx_mcs.rate_n_flags = tx_resp->control.tx_rate;
	else
		tx_mcs.rate_n_flags =
			le32_to_cpu(table->rs_table[index].rate_n_flags);

	rs_get_tbl_info_from_mcs(&tx_mcs, priv->phymode,
				  &tbl_type, &rs_index);

	if (tx_resp->flags & IEEE80211_TX_STATUS_ACK)
		status = 1;
	else
		status = 0;

	if ((tbl_type.lq_type == search_tbl->lq_type) &&
	    (tbl_type.antenna_type == search_tbl->antenna_type) &&
	    (tbl_type.is_SGI == search_tbl->is_SGI)) {
		if (search_tbl->expected_tpt)
			tpt = search_tbl->expected_tpt[rs_index];
		else
			tpt = 0;
		rs_collect_tx_data(search_win,
				    rs_index, tpt, status);
	} else if ((tbl_type.lq_type == curr_tbl->lq_type) &&
		   (tbl_type.antenna_type == curr_tbl->antenna_type) &&
		   (tbl_type.is_SGI == curr_tbl->is_SGI)) {
		if (curr_tbl->expected_tpt)
			tpt = curr_tbl->expected_tpt[rs_index];
		else
			tpt = 0;
		rs_collect_tx_data(window, rs_index, tpt, status);
	}

	if (lq->stay_in_tbl) {
		if (status)
			lq->total_success++;
		else
			lq->total_failed++;
	}

	rs_rate_scale_perform(priv, dev, hdr, sta);
	sta_info_put(sta);
	return;
}

static u8 rs_is_ant_connected(u8 valid_antenna,
			      enum iwl_antenna_type antenna_type)
{
	if (antenna_type == ANT_AUX)
		return ((valid_antenna & 0x2) ? 1:0);
	else if (antenna_type == ANT_MAIN)
		return ((valid_antenna & 0x1) ? 1:0);
	else if (antenna_type == ANT_BOTH) {
		if ((valid_antenna & 0x3) == 0x3)
			return 1;
		else
			return 0;
	}

	return 1;
}

static u8 rs_is_other_ant_connected(u8 valid_antenna,
				    enum iwl_antenna_type antenna_type)
{
	if (antenna_type == ANT_AUX)
		return (rs_is_ant_connected(valid_antenna, ANT_MAIN));
	else
		return (rs_is_ant_connected(valid_antenna, ANT_AUX));

	return 0;
}

static void rs_set_stay_in_table(u8 is_legacy,
				 struct iwl_rate_scale_priv *lq_data)
{
	IWL_DEBUG_HT("we are staying in the same table\n");
	lq_data->stay_in_tbl = 1;
	if (is_legacy) {
		lq_data->table_count_limit = IWL_LEGACY_TABLE_COUNT;
		lq_data->max_failure_limit = IWL_LEGACY_FAILURE_LIMIT;
		lq_data->max_success_limit = IWL_LEGACY_TABLE_COUNT;
	} else {
		lq_data->table_count_limit = IWL_NONE_LEGACY_TABLE_COUNT;
		lq_data->max_failure_limit = IWL_NONE_LEGACY_FAILURE_LIMIT;
		lq_data->max_success_limit = IWL_NONE_LEGACY_SUCCESS_LIMIT;
	}
	lq_data->table_count = 0;
	lq_data->total_failed = 0;
	lq_data->total_success = 0;
}

static void rs_get_expected_tpt_table(struct iwl_rate_scale_priv *lq_data,
				      struct iwl_scale_tbl_info *tbl)
{
	if (is_legacy(tbl->lq_type)) {
		if (!is_a_band(tbl->lq_type))
			tbl->expected_tpt = expected_tpt_G;
		else
			tbl->expected_tpt = expected_tpt_A;
	} else if (is_siso(tbl->lq_type)) {
		if (tbl->is_fat && !lq_data->is_dup)
			if (tbl->is_SGI)
				tbl->expected_tpt = expected_tpt_siso40MHzSGI;
			else
				tbl->expected_tpt = expected_tpt_siso40MHz;
		else if (tbl->is_SGI)
			tbl->expected_tpt = expected_tpt_siso20MHzSGI;
		else
			tbl->expected_tpt = expected_tpt_siso20MHz;

	} else if (is_mimo(tbl->lq_type)) {
		if (tbl->is_fat && !lq_data->is_dup)
			if (tbl->is_SGI)
				tbl->expected_tpt = expected_tpt_mimo40MHzSGI;
			else
				tbl->expected_tpt = expected_tpt_mimo40MHz;
		else if (tbl->is_SGI)
			tbl->expected_tpt = expected_tpt_mimo20MHzSGI;
		else
			tbl->expected_tpt = expected_tpt_mimo20MHz;
	} else
		tbl->expected_tpt = expected_tpt_G;
}

#ifdef CONFIG_IWLWIFI_HT
static s32 rs_get_best_rate(struct iwl_priv *priv,
			    struct iwl_rate_scale_priv *lq_data,
			    struct iwl_scale_tbl_info *tbl,
			    u16 rate_mask, s8 index, s8 rate)
{
	struct iwl_scale_tbl_info *active_tbl =
	    &(lq_data->lq_info[lq_data->active_tbl]);
	s32 new_rate, high, low, start_hi;
	s32 active_sr = active_tbl->win[index].success_ratio;
	s32 *tpt_tbl = tbl->expected_tpt;
	s32 active_tpt = active_tbl->expected_tpt[index];
	u16 high_low;

	new_rate = high = low = start_hi = IWL_RATE_INVALID;

	for (; ;) {
		high_low = rs_get_adjacent_rate(rate, rate_mask, tbl->lq_type);

		low = high_low & 0xff;
		high = (high_low >> 8) & 0xff;

		if ((((100 * tpt_tbl[rate]) > lq_data->last_tpt) &&
		     ((active_sr > IWL_RATE_DECREASE_TH) &&
		      (active_sr <= IWL_RATE_HIGH_TH) &&
		      (tpt_tbl[rate] <= active_tpt))) ||
		    ((active_sr >= IWL_RATE_SCALE_SWITCH) &&
		     (tpt_tbl[rate] > active_tpt))) {

			if (start_hi != IWL_RATE_INVALID) {
				new_rate = start_hi;
				break;
			}
			new_rate = rate;
			if (low != IWL_RATE_INVALID)
				rate = low;
			else
				break;
		} else {
			if (new_rate != IWL_RATE_INVALID)
				break;
			else if (high != IWL_RATE_INVALID) {
				start_hi = high;
				rate = high;
			} else {
				new_rate = rate;
				break;
			}
		}
	}

	return new_rate;
}
#endif				/* CONFIG_IWLWIFI_HT */

static inline u8 rs_is_both_ant_supp(u8 valid_antenna)
{
	return (rs_is_ant_connected(valid_antenna, ANT_BOTH));
}

static int rs_switch_to_mimo(struct iwl_priv *priv,
			     struct iwl_rate_scale_priv *lq_data,
			     struct iwl_scale_tbl_info *tbl, int index)
{
	int rc = -1;
#ifdef CONFIG_IWLWIFI_HT
	u16 rate_mask;
	s32 rate;
	s8 is_green = lq_data->is_green;

	if (!priv->is_ht_enabled || !priv->current_assoc_ht.is_ht)
		return -1;

	IWL_DEBUG_HT("LQ: try to switch to MIMO\n");
	tbl->lq_type = LQ_MIMO;
	rs_get_supported_rates(lq_data, NULL, tbl->lq_type,
				&rate_mask);

	if (priv->current_assoc_ht.tx_mimo_ps_mode == IWL_MIMO_PS_STATIC)
		return -1;

	if (!rs_is_both_ant_supp(lq_data->antenna))
		return -1;

	rc = 0;
	tbl->is_dup = lq_data->is_dup;
	tbl->action = 0;
	if (priv->current_channel_width == IWL_CHANNEL_WIDTH_40MHZ)
		tbl->is_fat = 1;
	else
		tbl->is_fat = 0;

	if (tbl->is_fat) {
		if (priv->current_assoc_ht.sgf & HT_SHORT_GI_40MHZ_ONLY)
			tbl->is_SGI = 1;
		else
			tbl->is_SGI = 0;
	} else if (priv->current_assoc_ht.sgf & HT_SHORT_GI_20MHZ_ONLY)
		tbl->is_SGI = 1;
	else
		tbl->is_SGI = 0;

	rs_get_expected_tpt_table(lq_data, tbl);

	rate = rs_get_best_rate(priv, lq_data, tbl, rate_mask, index, index);

	IWL_DEBUG_HT("LQ: MIMO best rate %d mask %X\n", rate, rate_mask);
	if ((rate == IWL_RATE_INVALID) || !((1 << rate) & rate_mask))
		return -1;
	rs_mcs_from_tbl(&tbl->current_rate, tbl, rate, is_green);

	IWL_DEBUG_HT("LQ: Switch to new mcs %X index is green %X\n",
		     tbl->current_rate.rate_n_flags, is_green);

#endif				/*CONFIG_IWLWIFI_HT */
	return rc;
}

static int rs_switch_to_siso(struct iwl_priv *priv,
			     struct iwl_rate_scale_priv *lq_data,
			     struct iwl_scale_tbl_info *tbl, int index)
{
	int rc = -1;
#ifdef CONFIG_IWLWIFI_HT
	u16 rate_mask;
	u8 is_green = lq_data->is_green;
	s32 rate;

	IWL_DEBUG_HT("LQ: try to switch to SISO\n");
	if (!priv->is_ht_enabled || !priv->current_assoc_ht.is_ht)
		return -1;

	rc = 0;
	tbl->is_dup = lq_data->is_dup;
	tbl->lq_type = LQ_SISO;
	tbl->action = 0;
	rs_get_supported_rates(lq_data, NULL, tbl->lq_type,
				&rate_mask);

	if (priv->current_channel_width == IWL_CHANNEL_WIDTH_40MHZ)
		tbl->is_fat = 1;
	else
		tbl->is_fat = 0;

	if (tbl->is_fat) {
		if (priv->current_assoc_ht.sgf & HT_SHORT_GI_40MHZ_ONLY)
			tbl->is_SGI = 1;
		else
			tbl->is_SGI = 0;
	} else if (priv->current_assoc_ht.sgf & HT_SHORT_GI_20MHZ_ONLY)
		tbl->is_SGI = 1;
	else
		tbl->is_SGI = 0;

	if (is_green)
		tbl->is_SGI = 0;

	rs_get_expected_tpt_table(lq_data, tbl);
	rate = rs_get_best_rate(priv, lq_data, tbl, rate_mask, index, index);

	IWL_DEBUG_HT("LQ: get best rate %d mask %X\n", rate, rate_mask);
	if ((rate == IWL_RATE_INVALID) || !((1 << rate) & rate_mask)) {
		IWL_DEBUG_HT("can not switch with index %d rate mask %x\n",
			     rate, rate_mask);
		return -1;
	}
	rs_mcs_from_tbl(&tbl->current_rate, tbl, rate, is_green);
	IWL_DEBUG_HT("LQ: Switch to new mcs %X index is green %X\n",
		     tbl->current_rate.rate_n_flags, is_green);

#endif				/*CONFIG_IWLWIFI_HT */
	return rc;
}

static int rs_move_legacy_other(struct iwl_priv *priv,
				struct iwl_rate_scale_priv *lq_data,
				int index)
{
	int rc = 0;
	struct iwl_scale_tbl_info *tbl =
	    &(lq_data->lq_info[lq_data->active_tbl]);
	struct iwl_scale_tbl_info *search_tbl =
	    &(lq_data->lq_info[(1 - lq_data->active_tbl)]);
	struct iwl_rate_scale_data *window = &(tbl->win[index]);
	u32 sz = (sizeof(struct iwl_scale_tbl_info) -
		  (sizeof(struct iwl_rate_scale_data) * IWL_RATE_COUNT));
	u8 start_action = tbl->action;

	for (; ;) {
		switch (tbl->action) {
		case IWL_LEGACY_SWITCH_ANTENNA:
			IWL_DEBUG_HT("LQ Legacy switch Antenna\n");

			search_tbl->lq_type = LQ_NONE;
			lq_data->action_counter++;
			if (window->success_ratio >= IWL_RS_GOOD_RATIO)
				break;
			if (!rs_is_other_ant_connected(lq_data->antenna,
							tbl->antenna_type))
				break;

			memcpy(search_tbl, tbl, sz);

			rs_toggle_antenna(&(search_tbl->current_rate),
					   search_tbl);
			rs_get_expected_tpt_table(lq_data, search_tbl);
			lq_data->search_better_tbl = 1;
			goto out;

		case IWL_LEGACY_SWITCH_SISO:
			IWL_DEBUG_HT("LQ: Legacy switch to SISO\n");
			memcpy(search_tbl, tbl, sz);
			search_tbl->lq_type = LQ_SISO;
			search_tbl->is_SGI = 0;
			search_tbl->is_fat = 0;
			rc = rs_switch_to_siso(priv, lq_data, search_tbl,
					       index);
			if (!rc) {
				lq_data->search_better_tbl = 1;
				lq_data->action_counter = 0;
			}
			if (!rc)
				goto out;

			break;
		case IWL_LEGACY_SWITCH_MIMO:
			IWL_DEBUG_HT("LQ: Legacy switch MIMO\n");
			memcpy(search_tbl, tbl, sz);
			search_tbl->lq_type = LQ_MIMO;
			search_tbl->is_SGI = 0;
			search_tbl->is_fat = 0;
			search_tbl->antenna_type = ANT_BOTH;
			rc = rs_switch_to_mimo(priv, lq_data, search_tbl,
					       index);
			if (!rc) {
				lq_data->search_better_tbl = 1;
				lq_data->action_counter = 0;
			}
			if (!rc)
				goto out;
			break;
		}
		tbl->action++;
		if (tbl->action > IWL_LEGACY_SWITCH_MIMO)
			tbl->action = IWL_LEGACY_SWITCH_ANTENNA;

		if (tbl->action == start_action)
			break;

	}
	return 0;

 out:
	tbl->action++;
	if (tbl->action > IWL_LEGACY_SWITCH_MIMO)
		tbl->action = IWL_LEGACY_SWITCH_ANTENNA;
	return 0;

}

static int rs_move_siso_to_other(struct iwl_priv *priv,
				 struct iwl_rate_scale_priv *lq_data,
				 int index)
{
	int rc = -1;
	u8 is_green = lq_data->is_green;
	struct iwl_scale_tbl_info *tbl =
	    &(lq_data->lq_info[lq_data->active_tbl]);
	struct iwl_scale_tbl_info *search_tbl =
	    &(lq_data->lq_info[(1 - lq_data->active_tbl)]);
	struct iwl_rate_scale_data *window = &(tbl->win[index]);
	u32 sz = (sizeof(struct iwl_scale_tbl_info) -
		  (sizeof(struct iwl_rate_scale_data) * IWL_RATE_COUNT));
	u8 start_action = tbl->action;

	for (;;) {
		lq_data->action_counter++;
		switch (tbl->action) {
		case IWL_SISO_SWITCH_ANTENNA:
			IWL_DEBUG_HT("LQ: SISO SWITCH ANTENNA SISO\n");
			search_tbl->lq_type = LQ_NONE;
			if (window->success_ratio >= IWL_RS_GOOD_RATIO)
				break;
			if (!rs_is_other_ant_connected(lq_data->antenna,
						       tbl->antenna_type))
				break;

			memcpy(search_tbl, tbl, sz);
			search_tbl->action = IWL_SISO_SWITCH_MIMO;
			rs_toggle_antenna(&(search_tbl->current_rate),
					   search_tbl);
			lq_data->search_better_tbl = 1;

			goto out;

		case IWL_SISO_SWITCH_MIMO:
			IWL_DEBUG_HT("LQ: SISO SWITCH TO MIMO FROM SISO\n");
			memcpy(search_tbl, tbl, sz);
			search_tbl->lq_type = LQ_MIMO;
			search_tbl->is_SGI = 0;
			search_tbl->is_fat = 0;
			search_tbl->antenna_type = ANT_BOTH;
			rc = rs_switch_to_mimo(priv, lq_data, search_tbl,
					       index);
			if (!rc)
				lq_data->search_better_tbl = 1;

			if (!rc)
				goto out;
			break;
		case IWL_SISO_SWITCH_GI:
			IWL_DEBUG_HT("LQ: SISO SWITCH TO GI\n");
			memcpy(search_tbl, tbl, sz);
			search_tbl->action = 0;
			if (search_tbl->is_SGI)
				search_tbl->is_SGI = 0;
			else if (!is_green)
				search_tbl->is_SGI = 1;
			else
				break;
			lq_data->search_better_tbl = 1;
			if ((tbl->lq_type == LQ_SISO) &&
			    (tbl->is_SGI)) {
				s32 tpt = lq_data->last_tpt / 100;
				if (((!tbl->is_fat) &&
				     (tpt >= expected_tpt_siso20MHz[index])) ||
				    ((tbl->is_fat) &&
				     (tpt >= expected_tpt_siso40MHz[index])))
					lq_data->search_better_tbl = 0;
			}
			rs_get_expected_tpt_table(lq_data, search_tbl);
			rs_mcs_from_tbl(&search_tbl->current_rate,
					     search_tbl, index, is_green);
			goto out;
		}
		tbl->action++;
		if (tbl->action > IWL_SISO_SWITCH_GI)
			tbl->action = IWL_SISO_SWITCH_ANTENNA;

		if (tbl->action == start_action)
			break;
	}
	return 0;

 out:
	tbl->action++;
	if (tbl->action > IWL_SISO_SWITCH_GI)
		tbl->action = IWL_SISO_SWITCH_ANTENNA;
	return 0;
}

static int rs_move_mimo_to_other(struct iwl_priv *priv,
				 struct iwl_rate_scale_priv *lq_data,
				 int index)
{
	int rc = -1;
	s8 is_green = lq_data->is_green;
	struct iwl_scale_tbl_info *tbl =
	    &(lq_data->lq_info[lq_data->active_tbl]);
	struct iwl_scale_tbl_info *search_tbl =
	    &(lq_data->lq_info[(1 - lq_data->active_tbl)]);
	u32 sz = (sizeof(struct iwl_scale_tbl_info) -
		  (sizeof(struct iwl_rate_scale_data) * IWL_RATE_COUNT));
	u8 start_action = tbl->action;

	for (;;) {
		lq_data->action_counter++;
		switch (tbl->action) {
		case IWL_MIMO_SWITCH_ANTENNA_A:
		case IWL_MIMO_SWITCH_ANTENNA_B:
			IWL_DEBUG_HT("LQ: MIMO SWITCH TO SISO\n");
			memcpy(search_tbl, tbl, sz);
			search_tbl->lq_type = LQ_SISO;
			search_tbl->is_SGI = 0;
			search_tbl->is_fat = 0;
			if (tbl->action == IWL_MIMO_SWITCH_ANTENNA_A)
				search_tbl->antenna_type = ANT_MAIN;
			else
				search_tbl->antenna_type = ANT_AUX;

			rc = rs_switch_to_siso(priv, lq_data, search_tbl,
					       index);
			if (!rc) {
				lq_data->search_better_tbl = 1;
				goto out;
			}
			break;

		case IWL_MIMO_SWITCH_GI:
			IWL_DEBUG_HT("LQ: MIMO SWITCH TO GI\n");
			memcpy(search_tbl, tbl, sz);
			search_tbl->lq_type = LQ_MIMO;
			search_tbl->antenna_type = ANT_BOTH;
			search_tbl->action = 0;
			if (search_tbl->is_SGI)
				search_tbl->is_SGI = 0;
			else
				search_tbl->is_SGI = 1;
			lq_data->search_better_tbl = 1;
			if ((tbl->lq_type == LQ_MIMO) &&
			    (tbl->is_SGI)) {
				s32 tpt = lq_data->last_tpt / 100;
				if (((!tbl->is_fat) &&
				     (tpt >= expected_tpt_mimo20MHz[index])) ||
				    ((tbl->is_fat) &&
				     (tpt >= expected_tpt_mimo40MHz[index])))
					lq_data->search_better_tbl = 0;
			}
			rs_get_expected_tpt_table(lq_data, search_tbl);
			rs_mcs_from_tbl(&search_tbl->current_rate,
					     search_tbl, index, is_green);
			goto out;

		}
		tbl->action++;
		if (tbl->action > IWL_MIMO_SWITCH_GI)
			tbl->action = IWL_MIMO_SWITCH_ANTENNA_A;

		if (tbl->action == start_action)
			break;
	}

	return 0;
 out:
	tbl->action++;
	if (tbl->action > IWL_MIMO_SWITCH_GI)
		tbl->action = IWL_MIMO_SWITCH_ANTENNA_A;
	return 0;

}

static void rs_stay_in_table(struct iwl_rate_scale_priv *lq_data)
{
	struct iwl_scale_tbl_info *tbl;
	int i;
	int active_tbl;
	int flush_interval_passed = 0;

	active_tbl = lq_data->active_tbl;

	tbl = &(lq_data->lq_info[active_tbl]);

	if (lq_data->stay_in_tbl) {

		if (lq_data->flush_timer)
			flush_interval_passed =
			    time_after(jiffies,
				       (unsigned long)(lq_data->flush_timer +
					IWL_RATE_SCALE_FLUSH_INTVL));

		flush_interval_passed = 0;
		if ((lq_data->total_failed > lq_data->max_failure_limit) ||
		    (lq_data->total_success > lq_data->max_success_limit) ||
		    ((!lq_data->search_better_tbl) && (lq_data->flush_timer)
		     && (flush_interval_passed))) {
			IWL_DEBUG_HT("LQ: stay is expired %d %d %d\n:",
				     lq_data->total_failed,
				     lq_data->total_success,
				     flush_interval_passed);
			lq_data->stay_in_tbl = 0;
			lq_data->total_failed = 0;
			lq_data->total_success = 0;
			lq_data->flush_timer = 0;
		} else if (lq_data->table_count > 0) {
			lq_data->table_count++;
			if (lq_data->table_count >=
			    lq_data->table_count_limit) {
				lq_data->table_count = 0;

				IWL_DEBUG_HT("LQ: stay in table clear win\n");
				for (i = 0; i < IWL_RATE_COUNT; i++)
					rs_rate_scale_clear_window(
						&(tbl->win[i]));
			}
		}

		if (!lq_data->stay_in_tbl) {
			for (i = 0; i < IWL_RATE_COUNT; i++)
				rs_rate_scale_clear_window(&(tbl->win[i]));
		}
	}
}

static void rs_rate_scale_perform(struct iwl_priv *priv,
				  struct net_device *dev,
				  struct ieee80211_hdr *hdr,
				  struct sta_info *sta)
{
	int low = IWL_RATE_INVALID;
	int high = IWL_RATE_INVALID;
	int index;
	int i;
	struct iwl_rate_scale_data *window = NULL;
	int current_tpt = IWL_INVALID_VALUE;
	int low_tpt = IWL_INVALID_VALUE;
	int high_tpt = IWL_INVALID_VALUE;
	u32 fail_count;
	s8 scale_action = 0;
	u16 fc, rate_mask;
	u8 update_lq = 0;
	struct iwl_rate_scale_priv *lq_data;
	struct iwl_scale_tbl_info *tbl, *tbl1;
	u16 rate_scale_index_msk = 0;
	struct iwl_rate mcs_rate;
	u8 is_green = 0;
	u8 active_tbl = 0;
	u8 done_search = 0;
	u16 high_low;

	IWL_DEBUG_RATE("rate scale calculate new rate for skb\n");

	fc = le16_to_cpu(hdr->frame_control);
	if (!ieee80211_is_data(fc) || is_multicast_ether_addr(hdr->addr1)) {
		/* Send management frames and broadcast/multicast data using
		 * lowest rate. */
		/* TODO: this could probably be improved.. */
		return;
	}

	if (!sta || !sta->rate_ctrl_priv)
		return;

	if (!priv->lq_mngr.lq_ready) {
		IWL_DEBUG_RATE("still rate scaling not ready\n");
		return;
	}
	lq_data = (struct iwl_rate_scale_priv *)sta->rate_ctrl_priv;

	if (!lq_data->search_better_tbl)
		active_tbl = lq_data->active_tbl;
	else
		active_tbl = 1 - lq_data->active_tbl;

	tbl = &(lq_data->lq_info[active_tbl]);
	is_green = lq_data->is_green;

	index = sta->last_txrate;

	IWL_DEBUG_RATE("Rate scale index %d for type %d\n", index,
		       tbl->lq_type);

	rs_get_supported_rates(lq_data, hdr, tbl->lq_type,
				&rate_mask);

	IWL_DEBUG_RATE("mask 0x%04X \n", rate_mask);

	/* mask with station rate restriction */
	if (is_legacy(tbl->lq_type)) {
		if (lq_data->phymode == (u8) MODE_IEEE80211A)
			rate_scale_index_msk = (u16) (rate_mask &
				(lq_data->supp_rates << IWL_FIRST_OFDM_RATE));
		else
			rate_scale_index_msk = (u16) (rate_mask &
						      lq_data->supp_rates);

	} else
		rate_scale_index_msk = rate_mask;

	if (!rate_scale_index_msk)
		rate_scale_index_msk = rate_mask;

	if (index < 0 || !((1 << index) & rate_scale_index_msk)) {
		index = IWL_INVALID_VALUE;
		update_lq = 1;

		/* get the lowest availabe rate */
		for (i = 0; i <= IWL_RATE_COUNT; i++) {
			if ((1 << i) & rate_scale_index_msk)
				index = i;
		}

		if (index == IWL_INVALID_VALUE) {
			IWL_WARNING("Can not find a suitable rate\n");
			return;
		}
	}

	if (!tbl->expected_tpt)
		rs_get_expected_tpt_table(lq_data, tbl);

	window = &(tbl->win[index]);

	fail_count = window->counter - window->success_counter;
	if (((fail_count < IWL_RATE_MIN_FAILURE_TH) &&
	     (window->success_counter < IWL_RATE_MIN_SUCCESS_TH))
	    || (tbl->expected_tpt == NULL)) {
		IWL_DEBUG_RATE("LQ: still below TH succ %d total %d "
			       "for index %d\n",
			       window->success_counter, window->counter, index);
		window->average_tpt = IWL_INVALID_VALUE;
		rs_stay_in_table(lq_data);
		if (update_lq) {
			rs_mcs_from_tbl(&mcs_rate, tbl, index, is_green);
			rs_fill_link_cmd(lq_data, &mcs_rate, &lq_data->lq);
			rs_send_lq_cmd(priv, &lq_data->lq, CMD_ASYNC);
		}
		goto out;

	} else
		window->average_tpt = ((window->success_ratio *
					tbl->expected_tpt[index] + 64) / 128);

	if (lq_data->search_better_tbl) {
		int success_limit = IWL_RATE_SCALE_SWITCH;

		if ((window->success_ratio > success_limit) ||
		    (window->average_tpt > lq_data->last_tpt)) {
			if (!is_legacy(tbl->lq_type)) {
				IWL_DEBUG_HT("LQ: we are switching to HT"
					     " rate suc %d current tpt %d"
					     " old tpt %d\n",
					     window->success_ratio,
					     window->average_tpt,
					     lq_data->last_tpt);
				lq_data->enable_counter = 1;
			}
			lq_data->active_tbl = active_tbl;
			current_tpt = window->average_tpt;
		} else {
			tbl->lq_type = LQ_NONE;
			active_tbl = lq_data->active_tbl;
			tbl = &(lq_data->lq_info[active_tbl]);

			index = iwl_rate_index_from_plcp(
				tbl->current_rate.rate_n_flags);

			update_lq = 1;
			current_tpt = lq_data->last_tpt;
			IWL_DEBUG_HT("XXY GO BACK TO OLD TABLE\n");
		}
		lq_data->search_better_tbl = 0;
		done_search = 1;
		goto lq_update;
	}

	high_low = rs_get_adjacent_rate(index, rate_scale_index_msk,
					tbl->lq_type);
	low = high_low & 0xff;
	high = (high_low >> 8) & 0xff;

	current_tpt = window->average_tpt;

	if (low != IWL_RATE_INVALID)
		low_tpt = tbl->win[low].average_tpt;

	if (high != IWL_RATE_INVALID)
		high_tpt = tbl->win[high].average_tpt;


	scale_action = 1;

	if ((window->success_ratio <= IWL_RATE_DECREASE_TH) ||
	    (current_tpt == 0)) {
		IWL_DEBUG_RATE("decrease rate because of low success_ratio\n");
		scale_action = -1;
	} else if ((low_tpt == IWL_INVALID_VALUE) &&
		   (high_tpt == IWL_INVALID_VALUE))
		scale_action = 1;
	else if ((low_tpt != IWL_INVALID_VALUE) &&
		 (high_tpt != IWL_INVALID_VALUE) &&
		 (low_tpt < current_tpt) &&
		 (high_tpt < current_tpt))
		scale_action = 0;
	else {
		if (high_tpt != IWL_INVALID_VALUE) {
			if (high_tpt > current_tpt)
				scale_action = 1;
			else {
				IWL_DEBUG_RATE
				    ("decrease rate because of high tpt\n");
				scale_action = -1;
			}
		} else if (low_tpt != IWL_INVALID_VALUE) {
			if (low_tpt > current_tpt) {
				IWL_DEBUG_RATE
				    ("decrease rate because of low tpt\n");
				scale_action = -1;
			} else
				scale_action = 1;
		}
	}

	if (scale_action == -1) {
		if ((low != IWL_RATE_INVALID) &&
		    ((window->success_ratio > IWL_RATE_HIGH_TH) ||
		     (current_tpt > (100 * tbl->expected_tpt[low]))))
			scale_action = 0;
	} else if ((scale_action == 1) &&
		   (window->success_ratio < IWL_RATE_INCREASE_TH))
		scale_action = 0;

	switch (scale_action) {
	case -1:
		if (low != IWL_RATE_INVALID) {
			update_lq = 1;
			index = low;
		}
		break;
	case 1:
		if (high != IWL_RATE_INVALID) {
			update_lq = 1;
			index = high;
		}

		break;
	case 0:
	default:
		break;
	}

	IWL_DEBUG_HT("choose rate scale index %d action %d low %d "
		    "high %d type %d\n",
		     index, scale_action, low, high, tbl->lq_type);

 lq_update:
	if (update_lq) {
		rs_mcs_from_tbl(&mcs_rate, tbl, index, is_green);
		rs_fill_link_cmd(lq_data, &mcs_rate, &lq_data->lq);
		rs_send_lq_cmd(priv, &lq_data->lq, CMD_ASYNC);
	}
	rs_stay_in_table(lq_data);

	if (!update_lq && !done_search && !lq_data->stay_in_tbl) {
		lq_data->last_tpt = current_tpt;

		if (is_legacy(tbl->lq_type))
			rs_move_legacy_other(priv, lq_data, index);
		else if (is_siso(tbl->lq_type))
			rs_move_siso_to_other(priv, lq_data, index);
		else
			rs_move_mimo_to_other(priv, lq_data, index);

		if (lq_data->search_better_tbl) {
			tbl = &(lq_data->lq_info[(1 - lq_data->active_tbl)]);
			for (i = 0; i < IWL_RATE_COUNT; i++)
				rs_rate_scale_clear_window(&(tbl->win[i]));

			index = iwl_rate_index_from_plcp(
					tbl->current_rate.rate_n_flags);

			IWL_DEBUG_HT("Switch current  mcs: %X index: %d\n",
				     tbl->current_rate.rate_n_flags, index);
			rs_fill_link_cmd(lq_data, &tbl->current_rate,
					 &lq_data->lq);
			rs_send_lq_cmd(priv, &lq_data->lq, CMD_ASYNC);
		}
		tbl1 = &(lq_data->lq_info[lq_data->active_tbl]);

		if (is_legacy(tbl1->lq_type) &&
#ifdef CONFIG_IWLWIFI_HT
		    !priv->current_assoc_ht.is_ht &&
#endif
		    (lq_data->action_counter >= 1)) {
			lq_data->action_counter = 0;
			IWL_DEBUG_HT("LQ: STAY in legacy table\n");
			rs_set_stay_in_table(1, lq_data);
		}

		if (lq_data->enable_counter &&
		    (lq_data->action_counter >= IWL_ACTION_LIMIT)) {
#ifdef CONFIG_IWLWIFI_HT_AGG
			if ((lq_data->last_tpt > TID_AGG_TPT_THREHOLD) &&
			    (priv->lq_mngr.agg_ctrl.auto_agg)) {
				priv->lq_mngr.agg_ctrl.tid_retry =
				    TID_ALL_SPECIFIED;
				schedule_work(&priv->agg_work);
			}
#endif /*CONFIG_IWLWIFI_HT_AGG */
			lq_data->action_counter = 0;
			rs_set_stay_in_table(0, lq_data);
		}
	} else {
		if ((!update_lq) && (!done_search) && (!lq_data->flush_timer))
			lq_data->flush_timer = jiffies;
	}

out:
	rs_mcs_from_tbl(&tbl->current_rate, tbl, index, is_green);
	i = index;
	sta->last_txrate = i;

	/* sta->txrate is an index to A mode rates which start
	 * at IWL_FIRST_OFDM_RATE
	 */
	if (lq_data->phymode == (u8) MODE_IEEE80211A)
		sta->txrate = i - IWL_FIRST_OFDM_RATE;
	else
		sta->txrate = i;

	return;
}


static void rs_initialize_lq(struct iwl_priv *priv,
			     struct sta_info *sta)
{
	int i;
	struct iwl_rate_scale_priv *lq;
	struct iwl_scale_tbl_info *tbl;
	u8 active_tbl = 0;
	int rate_idx;
	u8 use_green = rs_use_green(priv);
	struct iwl_rate mcs_rate;

	if (!sta || !sta->rate_ctrl_priv)
		goto out;

	lq = (struct iwl_rate_scale_priv *)sta->rate_ctrl_priv;
	i = sta->last_txrate;

	if ((lq->lq.sta_id == 0xff) &&
	    (priv->iw_mode == IEEE80211_IF_TYPE_IBSS))
		goto out;

	if (!lq->search_better_tbl)
		active_tbl = lq->active_tbl;
	else
		active_tbl = 1 - lq->active_tbl;

	tbl = &(lq->lq_info[active_tbl]);

	if ((i < 0) || (i >= IWL_RATE_COUNT))
		i = 0;

	mcs_rate.rate_n_flags = iwl_rates[i].plcp ;
	mcs_rate.rate_n_flags |= RATE_MCS_ANT_B_MSK;
	mcs_rate.rate_n_flags &= ~RATE_MCS_ANT_A_MSK;

	if (i >= IWL_FIRST_CCK_RATE && i <= IWL_LAST_CCK_RATE)
		mcs_rate.rate_n_flags |= RATE_MCS_CCK_MSK;

	tbl->antenna_type = ANT_AUX;
	rs_get_tbl_info_from_mcs(&mcs_rate, priv->phymode, tbl, &rate_idx);
	if (!rs_is_ant_connected(priv->valid_antenna, tbl->antenna_type))
	    rs_toggle_antenna(&mcs_rate, tbl);

	rs_mcs_from_tbl(&mcs_rate, tbl, rate_idx, use_green);
	tbl->current_rate.rate_n_flags = mcs_rate.rate_n_flags;
	rs_get_expected_tpt_table(lq, tbl);
	rs_fill_link_cmd(lq, &mcs_rate, &lq->lq);
	rs_send_lq_cmd(priv, &lq->lq, CMD_ASYNC);
 out:
	return;
}

static struct ieee80211_rate *rs_get_lowest_rate(struct ieee80211_local
						 *local)
{
	struct ieee80211_hw_mode *mode = local->oper_hw_mode;
	int i;

	for (i = 0; i < mode->num_rates; i++) {
		struct ieee80211_rate *rate = &mode->rates[i];

		if (rate->flags & IEEE80211_RATE_SUPPORTED)
			return rate;
	}

	return &mode->rates[0];
}

static struct ieee80211_rate *rs_get_rate(void *priv_rate,
					       struct net_device *dev,
					       struct sk_buff *skb,
					       struct rate_control_extra
					       *extra)
{

	int i;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct sta_info *sta;
	u16 fc;
	struct iwl_priv *priv = (struct iwl_priv *)priv_rate;
	struct iwl_rate_scale_priv *lq;

	IWL_DEBUG_RATE_LIMIT("rate scale calculate new rate for skb\n");

	memset(extra, 0, sizeof(*extra));

	fc = le16_to_cpu(hdr->frame_control);
	if (!ieee80211_is_data(fc) || is_multicast_ether_addr(hdr->addr1)) {
		/* Send management frames and broadcast/multicast data using
		 * lowest rate. */
		/* TODO: this could probably be improved.. */
		return rs_get_lowest_rate(local);
	}

	sta = sta_info_get(local, hdr->addr1);

	if (!sta || !sta->rate_ctrl_priv) {
		if (sta)
			sta_info_put(sta);
		return rs_get_lowest_rate(local);
	}

	lq = (struct iwl_rate_scale_priv *)sta->rate_ctrl_priv;
	i = sta->last_txrate;

	if ((priv->iw_mode == IEEE80211_IF_TYPE_IBSS) && !lq->ibss_sta_added) {
		u8 sta_id = iwl_hw_find_station(priv, hdr->addr1);
		DECLARE_MAC_BUF(mac);

		if (sta_id == IWL_INVALID_STATION) {
			IWL_DEBUG_RATE("LQ: ADD station %s\n",
				       print_mac(mac, hdr->addr1));
			sta_id = iwl_add_station(priv,
						 hdr->addr1, 0, CMD_ASYNC);
		}
		if ((sta_id != IWL_INVALID_STATION)) {
			lq->lq.sta_id = sta_id;
			lq->lq.rs_table[0].rate_n_flags = 0;
			lq->ibss_sta_added = 1;
			rs_initialize_lq(priv, sta);
		}
		if (!lq->ibss_sta_added)
			goto done;
	}

 done:
	sta_info_put(sta);
	if ((i < 0) || (i > IWL_RATE_COUNT))
		return rs_get_lowest_rate(local);

	return &priv->ieee_rates[i];
}

static void *rs_alloc_sta(void *priv, gfp_t gfp)
{
	struct iwl_rate_scale_priv *crl;
	int i, j;

	IWL_DEBUG_RATE("create station rate scale window\n");

	crl = kzalloc(sizeof(struct iwl_rate_scale_priv), gfp);

	if (crl == NULL)
		return NULL;
	crl->lq.sta_id = 0xff;


	for (j = 0; j < LQ_SIZE; j++)
		for (i = 0; i < IWL_RATE_COUNT; i++)
			rs_rate_scale_clear_window(&(crl->lq_info[j].win[i]));

	return crl;
}

static void rs_rate_init(void *priv_rate, void *priv_sta,
			 struct ieee80211_local *local,
			 struct sta_info *sta)
{
	int i, j;
	struct ieee80211_hw_mode *mode = local->oper_hw_mode;
	struct iwl_priv *priv = (struct iwl_priv *)priv_rate;
	struct iwl_rate_scale_priv *crl = priv_sta;

	crl->flush_timer = 0;
	crl->supp_rates = sta->supp_rates;
	sta->txrate = 3;
	for (j = 0; j < LQ_SIZE; j++)
		for (i = 0; i < IWL_RATE_COUNT; i++)
			rs_rate_scale_clear_window(&(crl->lq_info[j].win[i]));

	IWL_DEBUG_RATE("rate scale global init\n");
	/* TODO: what is a good starting rate for STA? About middle? Maybe not
	 * the lowest or the highest rate.. Could consider using RSSI from
	 * previous packets? Need to have IEEE 802.1X auth succeed immediately
	 * after assoc.. */

	crl->ibss_sta_added = 0;
	if (priv->iw_mode == IEEE80211_IF_TYPE_AP) {
		u8 sta_id = iwl_hw_find_station(priv, sta->addr);
		DECLARE_MAC_BUF(mac);

		/* for IBSS the call are from tasklet */
		IWL_DEBUG_HT("LQ: ADD station %s\n",
			     print_mac(mac, sta->addr));

		if (sta_id == IWL_INVALID_STATION) {
			IWL_DEBUG_RATE("LQ: ADD station %s\n",
				       print_mac(mac, sta->addr));
			sta_id = iwl_add_station(priv,
						 sta->addr, 0, CMD_ASYNC);
		}
		if ((sta_id != IWL_INVALID_STATION)) {
			crl->lq.sta_id = sta_id;
			crl->lq.rs_table[0].rate_n_flags = 0;
		}
		/* FIXME: this is w/a remove it later */
		priv->assoc_station_added = 1;
	}

	for (i = 0; i < mode->num_rates; i++) {
		if ((sta->supp_rates & BIT(i)) &&
		    (mode->rates[i].flags & IEEE80211_RATE_SUPPORTED))
			sta->txrate = i;
	}
	sta->last_txrate = sta->txrate;
	/* For MODE_IEEE80211A mode cck rate are at end
	 * rate table
	 */
	if (local->hw.conf.phymode == MODE_IEEE80211A)
		sta->last_txrate += IWL_FIRST_OFDM_RATE;

	crl->is_dup = priv->is_dup;
	crl->valid_antenna = priv->valid_antenna;
	crl->antenna = priv->antenna;
	crl->is_green = rs_use_green(priv);
	crl->active_rate = priv->active_rate;
	crl->active_rate &= ~(0x1000);
	crl->active_rate_basic = priv->active_rate_basic;
	crl->phymode = priv->phymode;
#ifdef CONFIG_IWLWIFI_HT
	crl->active_siso_rate = (priv->current_assoc_ht.supp_rates[0] << 1);
	crl->active_siso_rate |= (priv->current_assoc_ht.supp_rates[0] & 0x1);
	crl->active_siso_rate &= ~((u16)0x2);
	crl->active_siso_rate = crl->active_siso_rate << IWL_FIRST_OFDM_RATE;

	crl->active_mimo_rate = (priv->current_assoc_ht.supp_rates[1] << 1);
	crl->active_mimo_rate |= (priv->current_assoc_ht.supp_rates[1] & 0x1);
	crl->active_mimo_rate &= ~((u16)0x2);
	crl->active_mimo_rate = crl->active_mimo_rate << IWL_FIRST_OFDM_RATE;
	IWL_DEBUG_HT("MIMO RATE 0x%X SISO MASK 0x%X\n", crl->active_siso_rate,
		     crl->active_mimo_rate);
#endif /*CONFIG_IWLWIFI_HT*/
#ifdef CONFIG_MAC80211_DEBUGFS
	crl->drv = priv;
#endif

	if (priv->assoc_station_added)
		priv->lq_mngr.lq_ready = 1;

	rs_initialize_lq(priv, sta);
}

static void rs_fill_link_cmd(struct iwl_rate_scale_priv *lq_data,
			    struct iwl_rate *tx_mcs,
			    struct iwl_link_quality_cmd *lq_cmd)
{
	int index = 0;
	int rate_idx;
	int repeat_rate = 0;
	u8 ant_toggle_count = 0;
	u8 use_ht_possible = 1;
	struct iwl_rate new_rate;
	struct iwl_scale_tbl_info tbl_type = { 0 };

	rs_dbgfs_set_mcs(lq_data, tx_mcs, index);

	rs_get_tbl_info_from_mcs(tx_mcs, lq_data->phymode,
				  &tbl_type, &rate_idx);

	if (is_legacy(tbl_type.lq_type)) {
		ant_toggle_count = 1;
		repeat_rate = IWL_NUMBER_TRY;
	} else
		repeat_rate = IWL_HT_NUMBER_TRY;

	lq_cmd->general_params.mimo_delimiter =
			is_mimo(tbl_type.lq_type) ? 1 : 0;
	lq_cmd->rs_table[index].rate_n_flags =
			cpu_to_le32(tx_mcs->rate_n_flags);
	new_rate.rate_n_flags = tx_mcs->rate_n_flags;

	if (is_mimo(tbl_type.lq_type) || (tbl_type.antenna_type == ANT_MAIN))
		lq_cmd->general_params.single_stream_ant_msk = 1;
	else
		lq_cmd->general_params.single_stream_ant_msk = 2;

	index++;
	repeat_rate--;

	while (index < LINK_QUAL_MAX_RETRY_NUM) {
		while (repeat_rate > 0 && (index < LINK_QUAL_MAX_RETRY_NUM)) {
			if (is_legacy(tbl_type.lq_type)) {
				if (ant_toggle_count <
				    NUM_TRY_BEFORE_ANTENNA_TOGGLE)
					ant_toggle_count++;
				else {
					rs_toggle_antenna(&new_rate, &tbl_type);
					ant_toggle_count = 1;
				}
			}

			rs_dbgfs_set_mcs(lq_data, &new_rate, index);
			lq_cmd->rs_table[index].rate_n_flags =
					cpu_to_le32(new_rate.rate_n_flags);
			repeat_rate--;
			index++;
		}

		rs_get_tbl_info_from_mcs(&new_rate, lq_data->phymode, &tbl_type,
						&rate_idx);

		if (is_mimo(tbl_type.lq_type))
			lq_cmd->general_params.mimo_delimiter = index;

		rs_get_lower_rate(lq_data, &tbl_type, rate_idx,
				  use_ht_possible, &new_rate);

		if (is_legacy(tbl_type.lq_type)) {
			if (ant_toggle_count < NUM_TRY_BEFORE_ANTENNA_TOGGLE)
				ant_toggle_count++;
			else {
				rs_toggle_antenna(&new_rate, &tbl_type);
				ant_toggle_count = 1;
			}
			repeat_rate = IWL_NUMBER_TRY;
		} else
			repeat_rate = IWL_HT_NUMBER_TRY;

		use_ht_possible = 0;

		rs_dbgfs_set_mcs(lq_data, &new_rate, index);
		lq_cmd->rs_table[index].rate_n_flags =
				cpu_to_le32(new_rate.rate_n_flags);

		index++;
		repeat_rate--;
	}

	lq_cmd->general_params.dual_stream_ant_msk = 3;
	lq_cmd->agg_params.agg_dis_start_th = 3;
	lq_cmd->agg_params.agg_time_limit = cpu_to_le16(4000);
}

static void *rs_alloc(struct ieee80211_local *local)
{
	return local->hw.priv;
}
/* rate scale requires free function to be implemented */
static void rs_free(void *priv_rate)
{
	return;
}

static void rs_clear(void *priv_rate)
{
	struct iwl_priv *priv = (struct iwl_priv *) priv_rate;

	IWL_DEBUG_RATE("enter\n");

	priv->lq_mngr.lq_ready = 0;
#ifdef CONFIG_IWLWIFI_HT
#ifdef CONFIG_IWLWIFI_HT_AGG
	if (priv->lq_mngr.agg_ctrl.granted_ba)
		iwl4965_turn_off_agg(priv, TID_ALL_SPECIFIED);
#endif /*CONFIG_IWLWIFI_HT_AGG */
#endif /* CONFIG_IWLWIFI_HT */

	IWL_DEBUG_RATE("leave\n");
}

static void rs_free_sta(void *priv, void *priv_sta)
{
	struct iwl_rate_scale_priv *rs_priv = priv_sta;

	IWL_DEBUG_RATE("enter\n");
	kfree(rs_priv);
	IWL_DEBUG_RATE("leave\n");
}


#ifdef CONFIG_MAC80211_DEBUGFS
static int open_file_generic(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}
static void rs_dbgfs_set_mcs(struct iwl_rate_scale_priv *rs_priv,
				struct iwl_rate *mcs, int index)
{
	u32 base_rate;

	if (rs_priv->phymode == (u8) MODE_IEEE80211A)
		base_rate = 0x800D;
	else
		base_rate = 0x820A;

	if (rs_priv->dbg_fixed.rate_n_flags) {
		if (index < 12)
			mcs->rate_n_flags = rs_priv->dbg_fixed.rate_n_flags;
		else
			mcs->rate_n_flags = base_rate;
		IWL_DEBUG_RATE("Fixed rate ON\n");
		return;
	}

	IWL_DEBUG_RATE("Fixed rate OFF\n");
}

static ssize_t rs_sta_dbgfs_scale_table_write(struct file *file,
			const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct iwl_rate_scale_priv *rs_priv = file->private_data;
	char buf[64];
	int buf_size;
	u32 parsed_rate;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (sscanf(buf, "%x", &parsed_rate) == 1)
		rs_priv->dbg_fixed.rate_n_flags = parsed_rate;
	else
		rs_priv->dbg_fixed.rate_n_flags = 0;

	rs_priv->active_rate = 0x0FFF;
	rs_priv->active_siso_rate = 0x1FD0;
	rs_priv->active_mimo_rate = 0x1FD0;

	IWL_DEBUG_RATE("sta_id %d rate 0x%X\n",
		rs_priv->lq.sta_id, rs_priv->dbg_fixed.rate_n_flags);

	if (rs_priv->dbg_fixed.rate_n_flags) {
		rs_fill_link_cmd(rs_priv, &rs_priv->dbg_fixed, &rs_priv->lq);
		rs_send_lq_cmd(rs_priv->drv, &rs_priv->lq, CMD_ASYNC);
	}

	return count;
}

static ssize_t rs_sta_dbgfs_scale_table_read(struct file *file,
			char __user *user_buf, size_t count, loff_t *ppos)
{
	char buff[1024];
	int desc = 0;
	int i = 0;

	struct iwl_rate_scale_priv *rs_priv = file->private_data;

	desc += sprintf(buff+desc, "sta_id %d\n", rs_priv->lq.sta_id);
	desc += sprintf(buff+desc, "failed=%d success=%d rate=0%X\n",
			rs_priv->total_failed, rs_priv->total_success,
			rs_priv->active_rate);
	desc += sprintf(buff+desc, "fixed rate 0x%X\n",
			rs_priv->dbg_fixed.rate_n_flags);
	desc += sprintf(buff+desc, "general:"
		"flags=0x%X mimo-d=%d s-ant0x%x d-ant=0x%x\n",
		rs_priv->lq.general_params.flags,
		rs_priv->lq.general_params.mimo_delimiter,
		rs_priv->lq.general_params.single_stream_ant_msk,
		rs_priv->lq.general_params.dual_stream_ant_msk);

	desc += sprintf(buff+desc, "agg:"
			"time_limit=%d dist_start_th=%d frame_cnt_limit=%d\n",
			le16_to_cpu(rs_priv->lq.agg_params.agg_time_limit),
			rs_priv->lq.agg_params.agg_dis_start_th,
			rs_priv->lq.agg_params.agg_frame_cnt_limit);

	desc += sprintf(buff+desc,
			"Start idx [0]=0x%x [1]=0x%x [2]=0x%x [3]=0x%x\n",
			rs_priv->lq.general_params.start_rate_index[0],
			rs_priv->lq.general_params.start_rate_index[1],
			rs_priv->lq.general_params.start_rate_index[2],
			rs_priv->lq.general_params.start_rate_index[3]);


	for (i = 0; i < LINK_QUAL_MAX_RETRY_NUM; i++)
		desc += sprintf(buff+desc, " rate[%d] 0x%X\n",
			i, le32_to_cpu(rs_priv->lq.rs_table[i].rate_n_flags));

	return simple_read_from_buffer(user_buf, count, ppos, buff, desc);
}

static const struct file_operations rs_sta_dbgfs_scale_table_ops = {
	.write = rs_sta_dbgfs_scale_table_write,
	.read = rs_sta_dbgfs_scale_table_read,
	.open = open_file_generic,
};
static ssize_t rs_sta_dbgfs_stats_table_read(struct file *file,
			char __user *user_buf, size_t count, loff_t *ppos)
{
	char buff[1024];
	int desc = 0;
	int i, j;

	struct iwl_rate_scale_priv *rs_priv = file->private_data;
	for (i = 0; i < LQ_SIZE; i++) {
		desc += sprintf(buff+desc, "%s type=%d SGI=%d FAT=%d DUP=%d\n"
				"rate=0x%X\n",
				rs_priv->active_tbl == i?"*":"x",
				rs_priv->lq_info[i].lq_type,
				rs_priv->lq_info[i].is_SGI,
				rs_priv->lq_info[i].is_fat,
				rs_priv->lq_info[i].is_dup,
				rs_priv->lq_info[i].current_rate.rate_n_flags);
		for (j = 0; j < IWL_RATE_COUNT; j++) {
			desc += sprintf(buff+desc,
					"counter=%d success=%d %%=%d\n",
					rs_priv->lq_info[i].win[j].counter,
					rs_priv->lq_info[i].win[j].success_counter,
					rs_priv->lq_info[i].win[j].success_ratio);
		}
	}
	return simple_read_from_buffer(user_buf, count, ppos, buff, desc);
}

static const struct file_operations rs_sta_dbgfs_stats_table_ops = {
	.read = rs_sta_dbgfs_stats_table_read,
	.open = open_file_generic,
};

static void rs_add_debugfs(void *priv, void *priv_sta,
					struct dentry *dir)
{
	struct iwl_rate_scale_priv *rs_priv = priv_sta;
	rs_priv->rs_sta_dbgfs_scale_table_file =
		debugfs_create_file("rate_scale_table", 0600, dir,
				rs_priv, &rs_sta_dbgfs_scale_table_ops);
	rs_priv->rs_sta_dbgfs_stats_table_file =
		debugfs_create_file("rate_stats_table", 0600, dir,
			rs_priv, &rs_sta_dbgfs_stats_table_ops);
}

static void rs_remove_debugfs(void *priv, void *priv_sta)
{
	struct iwl_rate_scale_priv *rs_priv = priv_sta;
	debugfs_remove(rs_priv->rs_sta_dbgfs_scale_table_file);
	debugfs_remove(rs_priv->rs_sta_dbgfs_stats_table_file);
}
#endif

static struct rate_control_ops rs_ops = {
	.module = NULL,
	.name = RS_NAME,
	.tx_status = rs_tx_status,
	.get_rate = rs_get_rate,
	.rate_init = rs_rate_init,
	.clear = rs_clear,
	.alloc = rs_alloc,
	.free = rs_free,
	.alloc_sta = rs_alloc_sta,
	.free_sta = rs_free_sta,
#ifdef CONFIG_MAC80211_DEBUGFS
	.add_sta_debugfs = rs_add_debugfs,
	.remove_sta_debugfs = rs_remove_debugfs,
#endif
};

int iwl_fill_rs_info(struct ieee80211_hw *hw, char *buf, u8 sta_id)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct iwl_priv *priv = hw->priv;
	struct iwl_rate_scale_priv *rs_priv;
	struct sta_info *sta;
	int count = 0, i;
	u32 samples = 0, success = 0, good = 0;
	unsigned long now = jiffies;
	u32 max_time = 0;
	u8 lq_type, antenna;

	sta = sta_info_get(local, priv->stations[sta_id].sta.sta.addr);
	if (!sta || !sta->rate_ctrl_priv) {
		if (sta) {
			sta_info_put(sta);
			IWL_DEBUG_RATE("leave - no private rate data!\n");
		} else
			IWL_DEBUG_RATE("leave - no station!\n");
		return sprintf(buf, "station %d not found\n", sta_id);
	}

	rs_priv = (void *)sta->rate_ctrl_priv;

	lq_type = rs_priv->lq_info[rs_priv->active_tbl].lq_type;
	antenna = rs_priv->lq_info[rs_priv->active_tbl].antenna_type;

	if (is_legacy(lq_type))
		i = IWL_RATE_54M_INDEX;
	else
		i = IWL_RATE_60M_INDEX;
	while (1) {
		u64 mask;
		int j;
		int active = rs_priv->active_tbl;

		count +=
		    sprintf(&buf[count], " %2dMbs: ", iwl_rates[i].ieee / 2);

		mask = (1ULL << (IWL_RATE_MAX_WINDOW - 1));
		for (j = 0; j < IWL_RATE_MAX_WINDOW; j++, mask >>= 1)
			buf[count++] =
				(rs_priv->lq_info[active].win[i].data & mask)
				? '1' : '0';

		samples += rs_priv->lq_info[active].win[i].counter;
		good += rs_priv->lq_info[active].win[i].success_counter;
		success += rs_priv->lq_info[active].win[i].success_counter *
			   iwl_rates[i].ieee;

		if (rs_priv->lq_info[active].win[i].stamp) {
			int delta =
				   jiffies_to_msecs(now -
				   rs_priv->lq_info[active].win[i].stamp);

			if (delta > max_time)
				max_time = delta;

			count += sprintf(&buf[count], "%5dms\n", delta);
		} else
			buf[count++] = '\n';

		j = iwl_get_prev_ieee_rate(i);
		if (j == i)
			break;
		i = j;
	}

	/* Display the average rate of all samples taken.
	 *
	 * NOTE:  We multiple # of samples by 2 since the IEEE measurement
	 * added from iwl_rates is actually 2X the rate */
	if (samples)
		count += sprintf(&buf[count],
			 "\nAverage rate is %3d.%02dMbs over last %4dms\n"
			 "%3d%% success (%d good packets over %d tries)\n",
			 success / (2 * samples), (success * 5 / samples) % 10,
			 max_time, good * 100 / samples, good, samples);
	else
		count += sprintf(&buf[count], "\nAverage rate: 0Mbs\n");
	count += sprintf(&buf[count], "\nrate scale type %d anntena %d "
			 "active_search %d rate index %d\n", lq_type, antenna,
			 rs_priv->search_better_tbl, sta->last_txrate);

	sta_info_put(sta);
	return count;
}

void iwl_rate_scale_init(struct ieee80211_hw *hw, s32 sta_id)
{
	struct iwl_priv *priv = hw->priv;

	priv->lq_mngr.lq_ready = 1;
}

void iwl_rate_control_register(struct ieee80211_hw *hw)
{
	ieee80211_rate_control_register(&rs_ops);
}

void iwl_rate_control_unregister(struct ieee80211_hw *hw)
{
	ieee80211_rate_control_unregister(&rs_ops);
}

