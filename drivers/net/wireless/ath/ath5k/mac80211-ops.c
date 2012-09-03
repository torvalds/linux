/*-
 * Copyright (c) 2002-2005 Sam Leffler, Errno Consulting
 * Copyright (c) 2004-2005 Atheros Communications, Inc.
 * Copyright (c) 2006 Devicescape Software, Inc.
 * Copyright (c) 2007 Jiri Slaby <jirislaby@gmail.com>
 * Copyright (c) 2007 Luis R. Rodriguez <mcgrof@winlab.rutgers.edu>
 * Copyright (c) 2010 Bruno Randolf <br1@einfach.org>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <net/mac80211.h>
#include <asm/unaligned.h>

#include "ath5k.h"
#include "base.h"
#include "reg.h"

/********************\
* Mac80211 functions *
\********************/

static void
ath5k_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct ath5k_hw *ah = hw->priv;
	u16 qnum = skb_get_queue_mapping(skb);

	if (WARN_ON(qnum >= ah->ah_capabilities.cap_queues.q_tx_num)) {
		dev_kfree_skb_any(skb);
		return;
	}

	ath5k_tx_queue(hw, skb, &ah->txqs[qnum]);
}


static int
ath5k_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct ath5k_hw *ah = hw->priv;
	int ret;
	struct ath5k_vif *avf = (void *)vif->drv_priv;

	mutex_lock(&ah->lock);

	if ((vif->type == NL80211_IFTYPE_AP ||
	     vif->type == NL80211_IFTYPE_ADHOC)
	    && (ah->num_ap_vifs + ah->num_adhoc_vifs) >= ATH_BCBUF) {
		ret = -ELNRNG;
		goto end;
	}

	/* Don't allow other interfaces if one ad-hoc is configured.
	 * TODO: Fix the problems with ad-hoc and multiple other interfaces.
	 * We would need to operate the HW in ad-hoc mode to allow TSF updates
	 * for the IBSS, but this breaks with additional AP or STA interfaces
	 * at the moment. */
	if (ah->num_adhoc_vifs ||
	    (ah->nvifs && vif->type == NL80211_IFTYPE_ADHOC)) {
		ATH5K_ERR(ah, "Only one single ad-hoc interface is allowed.\n");
		ret = -ELNRNG;
		goto end;
	}

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_MESH_POINT:
		avf->opmode = vif->type;
		break;
	default:
		ret = -EOPNOTSUPP;
		goto end;
	}

	ah->nvifs++;
	ATH5K_DBG(ah, ATH5K_DEBUG_MODE, "add interface mode %d\n", avf->opmode);

	/* Assign the vap/adhoc to a beacon xmit slot. */
	if ((avf->opmode == NL80211_IFTYPE_AP) ||
	    (avf->opmode == NL80211_IFTYPE_ADHOC) ||
	    (avf->opmode == NL80211_IFTYPE_MESH_POINT)) {
		int slot;

		WARN_ON(list_empty(&ah->bcbuf));
		avf->bbuf = list_first_entry(&ah->bcbuf, struct ath5k_buf,
					     list);
		list_del(&avf->bbuf->list);

		avf->bslot = 0;
		for (slot = 0; slot < ATH_BCBUF; slot++) {
			if (!ah->bslot[slot]) {
				avf->bslot = slot;
				break;
			}
		}
		BUG_ON(ah->bslot[avf->bslot] != NULL);
		ah->bslot[avf->bslot] = vif;
		if (avf->opmode == NL80211_IFTYPE_AP)
			ah->num_ap_vifs++;
		else if (avf->opmode == NL80211_IFTYPE_ADHOC)
			ah->num_adhoc_vifs++;
		else if (avf->opmode == NL80211_IFTYPE_MESH_POINT)
			ah->num_mesh_vifs++;
	}

	/* Any MAC address is fine, all others are included through the
	 * filter.
	 */
	ath5k_hw_set_lladdr(ah, vif->addr);

	ath5k_update_bssid_mask_and_opmode(ah, vif);
	ret = 0;
end:
	mutex_unlock(&ah->lock);
	return ret;
}


static void
ath5k_remove_interface(struct ieee80211_hw *hw,
		       struct ieee80211_vif *vif)
{
	struct ath5k_hw *ah = hw->priv;
	struct ath5k_vif *avf = (void *)vif->drv_priv;
	unsigned int i;

	mutex_lock(&ah->lock);
	ah->nvifs--;

	if (avf->bbuf) {
		ath5k_txbuf_free_skb(ah, avf->bbuf);
		list_add_tail(&avf->bbuf->list, &ah->bcbuf);
		for (i = 0; i < ATH_BCBUF; i++) {
			if (ah->bslot[i] == vif) {
				ah->bslot[i] = NULL;
				break;
			}
		}
		avf->bbuf = NULL;
	}
	if (avf->opmode == NL80211_IFTYPE_AP)
		ah->num_ap_vifs--;
	else if (avf->opmode == NL80211_IFTYPE_ADHOC)
		ah->num_adhoc_vifs--;
	else if (avf->opmode == NL80211_IFTYPE_MESH_POINT)
		ah->num_mesh_vifs--;

	ath5k_update_bssid_mask_and_opmode(ah, NULL);
	mutex_unlock(&ah->lock);
}


/*
 * TODO: Phy disable/diversity etc
 */
static int
ath5k_config(struct ieee80211_hw *hw, u32 changed)
{
	struct ath5k_hw *ah = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;
	int ret = 0;
	int i;

	mutex_lock(&ah->lock);

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		ret = ath5k_chan_set(ah, conf->channel);
		if (ret < 0)
			goto unlock;
	}

	if ((changed & IEEE80211_CONF_CHANGE_POWER) &&
	(ah->power_level != conf->power_level)) {
		ah->power_level = conf->power_level;

		/* Half dB steps */
		ath5k_hw_set_txpower_limit(ah, (conf->power_level * 2));
	}

	if (changed & IEEE80211_CONF_CHANGE_RETRY_LIMITS) {
		ah->ah_retry_long = conf->long_frame_max_tx_count;
		ah->ah_retry_short = conf->short_frame_max_tx_count;

		for (i = 0; i < ah->ah_capabilities.cap_queues.q_tx_num; i++)
			ath5k_hw_set_tx_retry_limits(ah, i);
	}

	/* TODO:
	 * 1) Move this on config_interface and handle each case
	 * separately eg. when we have only one STA vif, use
	 * AR5K_ANTMODE_SINGLE_AP
	 *
	 * 2) Allow the user to change antenna mode eg. when only
	 * one antenna is present
	 *
	 * 3) Allow the user to set default/tx antenna when possible
	 *
	 * 4) Default mode should handle 90% of the cases, together
	 * with fixed a/b and single AP modes we should be able to
	 * handle 99%. Sectored modes are extreme cases and i still
	 * haven't found a usage for them. If we decide to support them,
	 * then we must allow the user to set how many tx antennas we
	 * have available
	 */
	ath5k_hw_set_antenna_mode(ah, ah->ah_ant_mode);

unlock:
	mutex_unlock(&ah->lock);
	return ret;
}


static void
ath5k_bss_info_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       struct ieee80211_bss_conf *bss_conf, u32 changes)
{
	struct ath5k_vif *avf = (void *)vif->drv_priv;
	struct ath5k_hw *ah = hw->priv;
	struct ath_common *common = ath5k_hw_common(ah);

	mutex_lock(&ah->lock);

	if (changes & BSS_CHANGED_BSSID) {
		/* Cache for later use during resets */
		memcpy(common->curbssid, bss_conf->bssid, ETH_ALEN);
		common->curaid = 0;
		ath5k_hw_set_bssid(ah);
		mmiowb();
	}

	if (changes & BSS_CHANGED_BEACON_INT)
		ah->bintval = bss_conf->beacon_int;

	if (changes & BSS_CHANGED_ERP_SLOT) {
		int slot_time;

		ah->ah_short_slot = bss_conf->use_short_slot;
		slot_time = ath5k_hw_get_default_slottime(ah) +
			    3 * ah->ah_coverage_class;
		ath5k_hw_set_ifs_intervals(ah, slot_time);
	}

	if (changes & BSS_CHANGED_ASSOC) {
		avf->assoc = bss_conf->assoc;
		if (bss_conf->assoc)
			ah->assoc = bss_conf->assoc;
		else
			ah->assoc = ath5k_any_vif_assoc(ah);

		if (ah->opmode == NL80211_IFTYPE_STATION)
			ath5k_set_beacon_filter(hw, ah->assoc);
		ath5k_hw_set_ledstate(ah, ah->assoc ?
			AR5K_LED_ASSOC : AR5K_LED_INIT);
		if (bss_conf->assoc) {
			ATH5K_DBG(ah, ATH5K_DEBUG_ANY,
				  "Bss Info ASSOC %d, bssid: %pM\n",
				  bss_conf->aid, common->curbssid);
			common->curaid = bss_conf->aid;
			ath5k_hw_set_bssid(ah);
			/* Once ANI is available you would start it here */
		}
	}

	if (changes & BSS_CHANGED_BEACON) {
		spin_lock_bh(&ah->block);
		ath5k_beacon_update(hw, vif);
		spin_unlock_bh(&ah->block);
	}

	if (changes & BSS_CHANGED_BEACON_ENABLED)
		ah->enable_beacon = bss_conf->enable_beacon;

	if (changes & (BSS_CHANGED_BEACON | BSS_CHANGED_BEACON_ENABLED |
		       BSS_CHANGED_BEACON_INT))
		ath5k_beacon_config(ah);

	mutex_unlock(&ah->lock);
}


static u64
ath5k_prepare_multicast(struct ieee80211_hw *hw,
			struct netdev_hw_addr_list *mc_list)
{
	u32 mfilt[2], val;
	u8 pos;
	struct netdev_hw_addr *ha;

	mfilt[0] = 0;
	mfilt[1] = 1;

	netdev_hw_addr_list_for_each(ha, mc_list) {
		/* calculate XOR of eight 6-bit values */
		val = get_unaligned_le32(ha->addr + 0);
		pos = (val >> 18) ^ (val >> 12) ^ (val >> 6) ^ val;
		val = get_unaligned_le32(ha->addr + 3);
		pos ^= (val >> 18) ^ (val >> 12) ^ (val >> 6) ^ val;
		pos &= 0x3f;
		mfilt[pos / 32] |= (1 << (pos % 32));
		/* XXX: we might be able to just do this instead,
		* but not sure, needs testing, if we do use this we'd
		* need to inform below not to reset the mcast */
		/* ath5k_hw_set_mcast_filterindex(ah,
		 *      ha->addr[5]); */
	}

	return ((u64)(mfilt[1]) << 32) | mfilt[0];
}


/*
 * o always accept unicast, broadcast, and multicast traffic
 * o multicast traffic for all BSSIDs will be enabled if mac80211
 *   says it should be
 * o maintain current state of phy ofdm or phy cck error reception.
 *   If the hardware detects any of these type of errors then
 *   ath5k_hw_get_rx_filter() will pass to us the respective
 *   hardware filters to be able to receive these type of frames.
 * o probe request frames are accepted only when operating in
 *   hostap, adhoc, or monitor modes
 * o enable promiscuous mode according to the interface state
 * o accept beacons:
 *   - when operating in adhoc mode so the 802.11 layer creates
 *     node table entries for peers,
 *   - when operating in station mode for collecting rssi data when
 *     the station is otherwise quiet, or
 *   - when scanning
 */
static void
ath5k_configure_filter(struct ieee80211_hw *hw, unsigned int changed_flags,
		       unsigned int *new_flags, u64 multicast)
{
#define SUPPORTED_FIF_FLAGS \
	(FIF_PROMISC_IN_BSS |  FIF_ALLMULTI | FIF_FCSFAIL | \
	FIF_PLCPFAIL | FIF_CONTROL | FIF_OTHER_BSS | \
	FIF_BCN_PRBRESP_PROMISC)

	struct ath5k_hw *ah = hw->priv;
	u32 mfilt[2], rfilt;
	struct ath5k_vif_iter_data iter_data; /* to count STA interfaces */

	mutex_lock(&ah->lock);

	mfilt[0] = multicast;
	mfilt[1] = multicast >> 32;

	/* Only deal with supported flags */
	changed_flags &= SUPPORTED_FIF_FLAGS;
	*new_flags &= SUPPORTED_FIF_FLAGS;

	/* If HW detects any phy or radar errors, leave those filters on.
	 * Also, always enable Unicast, Broadcasts and Multicast
	 * XXX: move unicast, bssid broadcasts and multicast to mac80211 */
	rfilt = (ath5k_hw_get_rx_filter(ah) & (AR5K_RX_FILTER_PHYERR)) |
		(AR5K_RX_FILTER_UCAST | AR5K_RX_FILTER_BCAST |
		AR5K_RX_FILTER_MCAST);

	if (changed_flags & (FIF_PROMISC_IN_BSS | FIF_OTHER_BSS)) {
		if (*new_flags & FIF_PROMISC_IN_BSS)
			__set_bit(ATH_STAT_PROMISC, ah->status);
		else
			__clear_bit(ATH_STAT_PROMISC, ah->status);
	}

	if (test_bit(ATH_STAT_PROMISC, ah->status))
		rfilt |= AR5K_RX_FILTER_PROM;

	/* Note, AR5K_RX_FILTER_MCAST is already enabled */
	if (*new_flags & FIF_ALLMULTI) {
		mfilt[0] =  ~0;
		mfilt[1] =  ~0;
	}

	/* This is the best we can do */
	if (*new_flags & (FIF_FCSFAIL | FIF_PLCPFAIL))
		rfilt |= AR5K_RX_FILTER_PHYERR;

	/* FIF_BCN_PRBRESP_PROMISC really means to enable beacons
	* and probes for any BSSID */
	if ((*new_flags & FIF_BCN_PRBRESP_PROMISC) || (ah->nvifs > 1))
		rfilt |= AR5K_RX_FILTER_BEACON;

	/* FIF_CONTROL doc says that if FIF_PROMISC_IN_BSS is not
	 * set we should only pass on control frames for this
	 * station. This needs testing. I believe right now this
	 * enables *all* control frames, which is OK.. but
	 * but we should see if we can improve on granularity */
	if (*new_flags & FIF_CONTROL)
		rfilt |= AR5K_RX_FILTER_CONTROL;

	/* Additional settings per mode -- this is per ath5k */

	/* XXX move these to mac80211, and add a beacon IFF flag to mac80211 */

	switch (ah->opmode) {
	case NL80211_IFTYPE_MESH_POINT:
		rfilt |= AR5K_RX_FILTER_CONTROL |
			 AR5K_RX_FILTER_BEACON |
			 AR5K_RX_FILTER_PROBEREQ |
			 AR5K_RX_FILTER_PROM;
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_ADHOC:
		rfilt |= AR5K_RX_FILTER_PROBEREQ |
			 AR5K_RX_FILTER_BEACON;
		break;
	case NL80211_IFTYPE_STATION:
		if (ah->assoc)
			rfilt |= AR5K_RX_FILTER_BEACON;
	default:
		break;
	}

	iter_data.hw_macaddr = NULL;
	iter_data.n_stas = 0;
	iter_data.need_set_hw_addr = false;
	ieee80211_iterate_active_interfaces_atomic(ah->hw, ath5k_vif_iter,
						   &iter_data);

	/* Set up RX Filter */
	if (iter_data.n_stas > 1) {
		/* If you have multiple STA interfaces connected to
		 * different APs, ARPs are not received (most of the time?)
		 * Enabling PROMISC appears to fix that problem.
		 */
		rfilt |= AR5K_RX_FILTER_PROM;
	}

	/* Set filters */
	ath5k_hw_set_rx_filter(ah, rfilt);

	/* Set multicast bits */
	ath5k_hw_set_mcast_filter(ah, mfilt[0], mfilt[1]);
	/* Set the cached hw filter flags, this will later actually
	 * be set in HW */
	ah->filter_flags = rfilt;

	mutex_unlock(&ah->lock);
}


static int
ath5k_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
	      struct ieee80211_vif *vif, struct ieee80211_sta *sta,
	      struct ieee80211_key_conf *key)
{
	struct ath5k_hw *ah = hw->priv;
	struct ath_common *common = ath5k_hw_common(ah);
	int ret = 0;

	if (ath5k_modparam_nohwcrypt)
		return -EOPNOTSUPP;

	if (vif->type == NL80211_IFTYPE_ADHOC &&
	    (key->cipher == WLAN_CIPHER_SUITE_TKIP ||
	     key->cipher == WLAN_CIPHER_SUITE_CCMP) &&
	    !(key->flags & IEEE80211_KEY_FLAG_PAIRWISE)) {
		/* don't program group keys when using IBSS_RSN */
		return -EOPNOTSUPP;
	}

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
	case WLAN_CIPHER_SUITE_TKIP:
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		if (common->crypt_caps & ATH_CRYPT_CAP_CIPHER_AESCCM)
			break;
		return -EOPNOTSUPP;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	mutex_lock(&ah->lock);

	switch (cmd) {
	case SET_KEY:
		ret = ath_key_config(common, vif, sta, key);
		if (ret >= 0) {
			key->hw_key_idx = ret;
			/* push IV and Michael MIC generation to stack */
			key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
			if (key->cipher == WLAN_CIPHER_SUITE_TKIP)
				key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
			if (key->cipher == WLAN_CIPHER_SUITE_CCMP)
				key->flags |= IEEE80211_KEY_FLAG_SW_MGMT;
			ret = 0;
		}
		break;
	case DISABLE_KEY:
		ath_key_delete(common, key);
		break;
	default:
		ret = -EINVAL;
	}

	mmiowb();
	mutex_unlock(&ah->lock);
	return ret;
}


static void
ath5k_sw_scan_start(struct ieee80211_hw *hw)
{
	struct ath5k_hw *ah = hw->priv;
	if (!ah->assoc)
		ath5k_hw_set_ledstate(ah, AR5K_LED_SCAN);
}


static void
ath5k_sw_scan_complete(struct ieee80211_hw *hw)
{
	struct ath5k_hw *ah = hw->priv;
	ath5k_hw_set_ledstate(ah, ah->assoc ?
		AR5K_LED_ASSOC : AR5K_LED_INIT);
}


static int
ath5k_get_stats(struct ieee80211_hw *hw,
		struct ieee80211_low_level_stats *stats)
{
	struct ath5k_hw *ah = hw->priv;

	/* Force update */
	ath5k_hw_update_mib_counters(ah);

	stats->dot11ACKFailureCount = ah->stats.ack_fail;
	stats->dot11RTSFailureCount = ah->stats.rts_fail;
	stats->dot11RTSSuccessCount = ah->stats.rts_ok;
	stats->dot11FCSErrorCount = ah->stats.fcs_error;

	return 0;
}


static int
ath5k_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif, u16 queue,
	      const struct ieee80211_tx_queue_params *params)
{
	struct ath5k_hw *ah = hw->priv;
	struct ath5k_txq_info qi;
	int ret = 0;

	if (queue >= ah->ah_capabilities.cap_queues.q_tx_num)
		return 0;

	mutex_lock(&ah->lock);

	ath5k_hw_get_tx_queueprops(ah, queue, &qi);

	qi.tqi_aifs = params->aifs;
	qi.tqi_cw_min = params->cw_min;
	qi.tqi_cw_max = params->cw_max;
	qi.tqi_burst_time = params->txop * 32;

	ATH5K_DBG(ah, ATH5K_DEBUG_ANY,
		  "Configure tx [queue %d],  "
		  "aifs: %d, cw_min: %d, cw_max: %d, txop: %d\n",
		  queue, params->aifs, params->cw_min,
		  params->cw_max, params->txop);

	if (ath5k_hw_set_tx_queueprops(ah, queue, &qi)) {
		ATH5K_ERR(ah,
			  "Unable to update hardware queue %u!\n", queue);
		ret = -EIO;
	} else
		ath5k_hw_reset_tx_queue(ah, queue);

	mutex_unlock(&ah->lock);

	return ret;
}


static u64
ath5k_get_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct ath5k_hw *ah = hw->priv;

	return ath5k_hw_get_tsf64(ah);
}


static void
ath5k_set_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif, u64 tsf)
{
	struct ath5k_hw *ah = hw->priv;

	ath5k_hw_set_tsf64(ah, tsf);
}


static void
ath5k_reset_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct ath5k_hw *ah = hw->priv;

	/*
	 * in IBSS mode we need to update the beacon timers too.
	 * this will also reset the TSF if we call it with 0
	 */
	if (ah->opmode == NL80211_IFTYPE_ADHOC)
		ath5k_beacon_update_timers(ah, 0);
	else
		ath5k_hw_reset_tsf(ah);
}


static int
ath5k_get_survey(struct ieee80211_hw *hw, int idx, struct survey_info *survey)
{
	struct ath5k_hw *ah = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;
	struct ath_common *common = ath5k_hw_common(ah);
	struct ath_cycle_counters *cc = &common->cc_survey;
	unsigned int div = common->clockrate * 1000;

	if (idx != 0)
		return -ENOENT;

	spin_lock_bh(&common->cc_lock);
	ath_hw_cycle_counters_update(common);
	if (cc->cycles > 0) {
		ah->survey.channel_time += cc->cycles / div;
		ah->survey.channel_time_busy += cc->rx_busy / div;
		ah->survey.channel_time_rx += cc->rx_frame / div;
		ah->survey.channel_time_tx += cc->tx_frame / div;
	}
	memset(cc, 0, sizeof(*cc));
	spin_unlock_bh(&common->cc_lock);

	memcpy(survey, &ah->survey, sizeof(*survey));

	survey->channel = conf->channel;
	survey->noise = ah->ah_noise_floor;
	survey->filled = SURVEY_INFO_NOISE_DBM |
			SURVEY_INFO_CHANNEL_TIME |
			SURVEY_INFO_CHANNEL_TIME_BUSY |
			SURVEY_INFO_CHANNEL_TIME_RX |
			SURVEY_INFO_CHANNEL_TIME_TX;

	return 0;
}


/**
 * ath5k_set_coverage_class - Set IEEE 802.11 coverage class
 *
 * @hw: struct ieee80211_hw pointer
 * @coverage_class: IEEE 802.11 coverage class number
 *
 * Mac80211 callback. Sets slot time, ACK timeout and CTS timeout for given
 * coverage class. The values are persistent, they are restored after device
 * reset.
 */
static void
ath5k_set_coverage_class(struct ieee80211_hw *hw, u8 coverage_class)
{
	struct ath5k_hw *ah = hw->priv;

	mutex_lock(&ah->lock);
	ath5k_hw_set_coverage_class(ah, coverage_class);
	mutex_unlock(&ah->lock);
}


static int
ath5k_set_antenna(struct ieee80211_hw *hw, u32 tx_ant, u32 rx_ant)
{
	struct ath5k_hw *ah = hw->priv;

	if (tx_ant == 1 && rx_ant == 1)
		ath5k_hw_set_antenna_mode(ah, AR5K_ANTMODE_FIXED_A);
	else if (tx_ant == 2 && rx_ant == 2)
		ath5k_hw_set_antenna_mode(ah, AR5K_ANTMODE_FIXED_B);
	else if ((tx_ant & 3) == 3 && (rx_ant & 3) == 3)
		ath5k_hw_set_antenna_mode(ah, AR5K_ANTMODE_DEFAULT);
	else
		return -EINVAL;
	return 0;
}


static int
ath5k_get_antenna(struct ieee80211_hw *hw, u32 *tx_ant, u32 *rx_ant)
{
	struct ath5k_hw *ah = hw->priv;

	switch (ah->ah_ant_mode) {
	case AR5K_ANTMODE_FIXED_A:
		*tx_ant = 1; *rx_ant = 1; break;
	case AR5K_ANTMODE_FIXED_B:
		*tx_ant = 2; *rx_ant = 2; break;
	case AR5K_ANTMODE_DEFAULT:
		*tx_ant = 3; *rx_ant = 3; break;
	}
	return 0;
}


static void ath5k_get_ringparam(struct ieee80211_hw *hw,
				u32 *tx, u32 *tx_max, u32 *rx, u32 *rx_max)
{
	struct ath5k_hw *ah = hw->priv;

	*tx = ah->txqs[AR5K_TX_QUEUE_ID_DATA_MIN].txq_max;

	*tx_max = ATH5K_TXQ_LEN_MAX;
	*rx = *rx_max = ATH_RXBUF;
}


static int ath5k_set_ringparam(struct ieee80211_hw *hw, u32 tx, u32 rx)
{
	struct ath5k_hw *ah = hw->priv;
	u16 qnum;

	/* only support setting tx ring size for now */
	if (rx != ATH_RXBUF)
		return -EINVAL;

	/* restrict tx ring size min/max */
	if (!tx || tx > ATH5K_TXQ_LEN_MAX)
		return -EINVAL;

	for (qnum = 0; qnum < ARRAY_SIZE(ah->txqs); qnum++) {
		if (!ah->txqs[qnum].setup)
			continue;
		if (ah->txqs[qnum].qnum < AR5K_TX_QUEUE_ID_DATA_MIN ||
		    ah->txqs[qnum].qnum > AR5K_TX_QUEUE_ID_DATA_MAX)
			continue;

		ah->txqs[qnum].txq_max = tx;
		if (ah->txqs[qnum].txq_len >= ah->txqs[qnum].txq_max)
			ieee80211_stop_queue(hw, ah->txqs[qnum].qnum);
	}

	return 0;
}


const struct ieee80211_ops ath5k_hw_ops = {
	.tx			= ath5k_tx,
	.start			= ath5k_start,
	.stop			= ath5k_stop,
	.add_interface		= ath5k_add_interface,
	/* .change_interface	= not implemented */
	.remove_interface	= ath5k_remove_interface,
	.config			= ath5k_config,
	.bss_info_changed	= ath5k_bss_info_changed,
	.prepare_multicast	= ath5k_prepare_multicast,
	.configure_filter	= ath5k_configure_filter,
	/* .set_tim		= not implemented */
	.set_key		= ath5k_set_key,
	/* .update_tkip_key	= not implemented */
	/* .hw_scan		= not implemented */
	.sw_scan_start		= ath5k_sw_scan_start,
	.sw_scan_complete	= ath5k_sw_scan_complete,
	.get_stats		= ath5k_get_stats,
	/* .get_tkip_seq	= not implemented */
	/* .set_frag_threshold	= not implemented */
	/* .set_rts_threshold	= not implemented */
	/* .sta_add		= not implemented */
	/* .sta_remove		= not implemented */
	/* .sta_notify		= not implemented */
	.conf_tx		= ath5k_conf_tx,
	.get_tsf		= ath5k_get_tsf,
	.set_tsf		= ath5k_set_tsf,
	.reset_tsf		= ath5k_reset_tsf,
	/* .tx_last_beacon	= not implemented */
	/* .ampdu_action	= not needed */
	.get_survey		= ath5k_get_survey,
	.set_coverage_class	= ath5k_set_coverage_class,
	/* .rfkill_poll		= not implemented */
	/* .flush		= not implemented */
	/* .channel_switch	= not implemented */
	/* .napi_poll		= not implemented */
	.set_antenna		= ath5k_set_antenna,
	.get_antenna		= ath5k_get_antenna,
	.set_ringparam		= ath5k_set_ringparam,
	.get_ringparam		= ath5k_get_ringparam,
};
