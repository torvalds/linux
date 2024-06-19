// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/delay.h>

#include "mac.h"

#include <net/mac80211.h>
#include "core.h"
#include "hif.h"
#include "debug.h"
#include "wmi.h"
#include "wow.h"

static const struct wiphy_wowlan_support ath12k_wowlan_support = {
	.flags = WIPHY_WOWLAN_DISCONNECT |
		 WIPHY_WOWLAN_MAGIC_PKT,
	.pattern_min_len = WOW_MIN_PATTERN_SIZE,
	.pattern_max_len = WOW_MAX_PATTERN_SIZE,
	.max_pkt_offset = WOW_MAX_PKT_OFFSET,
};

int ath12k_wow_enable(struct ath12k *ar)
{
	struct ath12k_base *ab = ar->ab;
	int i, ret;

	clear_bit(ATH12K_FLAG_HTC_SUSPEND_COMPLETE, &ab->dev_flags);

	/* The firmware might be busy and it can not enter WoW immediately.
	 * In that case firmware notifies host with
	 * ATH12K_HTC_MSG_NACK_SUSPEND message, asking host to try again
	 * later. Per the firmware team there could be up to 10 loops.
	 */
	for (i = 0; i < ATH12K_WOW_RETRY_NUM; i++) {
		reinit_completion(&ab->htc_suspend);

		ret = ath12k_wmi_wow_enable(ar);
		if (ret) {
			ath12k_warn(ab, "failed to issue wow enable: %d\n", ret);
			return ret;
		}

		ret = wait_for_completion_timeout(&ab->htc_suspend, 3 * HZ);
		if (ret == 0) {
			ath12k_warn(ab,
				    "timed out while waiting for htc suspend completion\n");
			return -ETIMEDOUT;
		}

		if (test_bit(ATH12K_FLAG_HTC_SUSPEND_COMPLETE, &ab->dev_flags))
			/* success, suspend complete received */
			return 0;

		ath12k_warn(ab, "htc suspend not complete, retrying (try %d)\n",
			    i);
		msleep(ATH12K_WOW_RETRY_WAIT_MS);
	}

	ath12k_warn(ab, "htc suspend not complete, failing after %d tries\n", i);

	return -ETIMEDOUT;
}

int ath12k_wow_wakeup(struct ath12k *ar)
{
	struct ath12k_base *ab = ar->ab;
	int ret;

	reinit_completion(&ab->wow.wakeup_completed);

	ret = ath12k_wmi_wow_host_wakeup_ind(ar);
	if (ret) {
		ath12k_warn(ab, "failed to send wow wakeup indication: %d\n",
			    ret);
		return ret;
	}

	ret = wait_for_completion_timeout(&ab->wow.wakeup_completed, 3 * HZ);
	if (ret == 0) {
		ath12k_warn(ab, "timed out while waiting for wow wakeup completion\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int ath12k_wow_vif_cleanup(struct ath12k_vif *arvif)
{
	struct ath12k *ar = arvif->ar;
	int i, ret;

	for (i = 0; i < WOW_EVENT_MAX; i++) {
		ret = ath12k_wmi_wow_add_wakeup_event(ar, arvif->vdev_id, i, 0);
		if (ret) {
			ath12k_warn(ar->ab, "failed to issue wow wakeup for event %s on vdev %i: %d\n",
				    wow_wakeup_event(i), arvif->vdev_id, ret);
			return ret;
		}
	}

	for (i = 0; i < ar->wow.max_num_patterns; i++) {
		ret = ath12k_wmi_wow_del_pattern(ar, arvif->vdev_id, i);
		if (ret) {
			ath12k_warn(ar->ab, "failed to delete wow pattern %d for vdev %i: %d\n",
				    i, arvif->vdev_id, ret);
			return ret;
		}
	}

	return 0;
}

static int ath12k_wow_cleanup(struct ath12k *ar)
{
	struct ath12k_vif *arvif;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	list_for_each_entry(arvif, &ar->arvifs, list) {
		ret = ath12k_wow_vif_cleanup(arvif);
		if (ret) {
			ath12k_warn(ar->ab, "failed to clean wow wakeups on vdev %i: %d\n",
				    arvif->vdev_id, ret);
			return ret;
		}
	}

	return 0;
}

/* Convert a 802.3 format to a 802.11 format.
 *         +------------+-----------+--------+----------------+
 * 802.3:  |dest mac(6B)|src mac(6B)|type(2B)|     body...    |
 *         +------------+-----------+--------+----------------+
 *                |__         |_______    |____________  |________
 *                   |                |                |          |
 *         +--+------------+----+-----------+---------------+-----------+
 * 802.11: |4B|dest mac(6B)| 6B |src mac(6B)|  8B  |type(2B)|  body...  |
 *         +--+------------+----+-----------+---------------+-----------+
 */
static void
ath12k_wow_convert_8023_to_80211(struct ath12k *ar,
				 const struct cfg80211_pkt_pattern *eth_pattern,
				 struct ath12k_pkt_pattern *i80211_pattern)
{
	size_t r1042_eth_ofs = offsetof(struct rfc1042_hdr, eth_type);
	size_t a1_ofs = offsetof(struct ieee80211_hdr_3addr, addr1);
	size_t a3_ofs = offsetof(struct ieee80211_hdr_3addr, addr3);
	size_t i80211_hdr_len = sizeof(struct ieee80211_hdr_3addr);
	size_t prot_ofs = offsetof(struct ethhdr, h_proto);
	size_t src_ofs = offsetof(struct ethhdr, h_source);
	u8 eth_bytemask[WOW_MAX_PATTERN_SIZE] = {};
	const u8 *eth_pat = eth_pattern->pattern;
	size_t eth_pat_len = eth_pattern->pattern_len;
	size_t eth_pkt_ofs = eth_pattern->pkt_offset;
	u8 *bytemask = i80211_pattern->bytemask;
	u8 *pat = i80211_pattern->pattern;
	size_t pat_len = 0;
	size_t pkt_ofs = 0;
	size_t delta;
	int i;

	/* convert bitmask to bytemask */
	for (i = 0; i < eth_pat_len; i++)
		if (eth_pattern->mask[i / 8] & BIT(i % 8))
			eth_bytemask[i] = 0xff;

	if (eth_pkt_ofs < ETH_ALEN) {
		pkt_ofs = eth_pkt_ofs + a1_ofs;

		if (eth_pkt_ofs + eth_pat_len < ETH_ALEN) {
			memcpy(pat, eth_pat, eth_pat_len);
			memcpy(bytemask, eth_bytemask, eth_pat_len);

			pat_len = eth_pat_len;
		} else if (eth_pkt_ofs + eth_pat_len < prot_ofs) {
			memcpy(pat, eth_pat, ETH_ALEN - eth_pkt_ofs);
			memcpy(bytemask, eth_bytemask, ETH_ALEN - eth_pkt_ofs);

			delta = eth_pkt_ofs + eth_pat_len - src_ofs;
			memcpy(pat + a3_ofs - pkt_ofs,
			       eth_pat + ETH_ALEN - eth_pkt_ofs,
			       delta);
			memcpy(bytemask + a3_ofs - pkt_ofs,
			       eth_bytemask + ETH_ALEN - eth_pkt_ofs,
			       delta);

			pat_len = a3_ofs - pkt_ofs + delta;
		} else {
			memcpy(pat, eth_pat, ETH_ALEN - eth_pkt_ofs);
			memcpy(bytemask, eth_bytemask, ETH_ALEN - eth_pkt_ofs);

			memcpy(pat + a3_ofs - pkt_ofs,
			       eth_pat + ETH_ALEN - eth_pkt_ofs,
			       ETH_ALEN);
			memcpy(bytemask + a3_ofs - pkt_ofs,
			       eth_bytemask + ETH_ALEN - eth_pkt_ofs,
			       ETH_ALEN);

			delta = eth_pkt_ofs + eth_pat_len - prot_ofs;
			memcpy(pat + i80211_hdr_len + r1042_eth_ofs - pkt_ofs,
			       eth_pat + prot_ofs - eth_pkt_ofs,
			       delta);
			memcpy(bytemask + i80211_hdr_len + r1042_eth_ofs - pkt_ofs,
			       eth_bytemask + prot_ofs - eth_pkt_ofs,
			       delta);

			pat_len = i80211_hdr_len + r1042_eth_ofs - pkt_ofs + delta;
		}
	} else if (eth_pkt_ofs < prot_ofs) {
		pkt_ofs = eth_pkt_ofs - ETH_ALEN + a3_ofs;

		if (eth_pkt_ofs + eth_pat_len < prot_ofs) {
			memcpy(pat, eth_pat, eth_pat_len);
			memcpy(bytemask, eth_bytemask, eth_pat_len);

			pat_len = eth_pat_len;
		} else {
			memcpy(pat, eth_pat, prot_ofs - eth_pkt_ofs);
			memcpy(bytemask, eth_bytemask, prot_ofs - eth_pkt_ofs);

			delta = eth_pkt_ofs + eth_pat_len - prot_ofs;
			memcpy(pat + i80211_hdr_len + r1042_eth_ofs - pkt_ofs,
			       eth_pat +  prot_ofs - eth_pkt_ofs,
			       delta);
			memcpy(bytemask + i80211_hdr_len + r1042_eth_ofs - pkt_ofs,
			       eth_bytemask + prot_ofs - eth_pkt_ofs,
			       delta);

			pat_len =  i80211_hdr_len + r1042_eth_ofs - pkt_ofs + delta;
		}
	} else {
		pkt_ofs = eth_pkt_ofs - prot_ofs + i80211_hdr_len + r1042_eth_ofs;

		memcpy(pat, eth_pat, eth_pat_len);
		memcpy(bytemask, eth_bytemask, eth_pat_len);

		pat_len = eth_pat_len;
	}

	i80211_pattern->pattern_len = pat_len;
	i80211_pattern->pkt_offset = pkt_ofs;
}

static int ath12k_vif_wow_set_wakeups(struct ath12k_vif *arvif,
				      struct cfg80211_wowlan *wowlan)
{
	const struct cfg80211_pkt_pattern *patterns = wowlan->patterns;
	struct ath12k *ar = arvif->ar;
	unsigned long wow_mask = 0;
	int pattern_id = 0;
	int ret, i;

	/* Setup requested WOW features */
	switch (arvif->vdev_type) {
	case WMI_VDEV_TYPE_IBSS:
		__set_bit(WOW_BEACON_EVENT, &wow_mask);
		fallthrough;
	case WMI_VDEV_TYPE_AP:
		__set_bit(WOW_DEAUTH_RECVD_EVENT, &wow_mask);
		__set_bit(WOW_DISASSOC_RECVD_EVENT, &wow_mask);
		__set_bit(WOW_PROBE_REQ_WPS_IE_EVENT, &wow_mask);
		__set_bit(WOW_AUTH_REQ_EVENT, &wow_mask);
		__set_bit(WOW_ASSOC_REQ_EVENT, &wow_mask);
		__set_bit(WOW_HTT_EVENT, &wow_mask);
		__set_bit(WOW_RA_MATCH_EVENT, &wow_mask);
		break;
	case WMI_VDEV_TYPE_STA:
		if (wowlan->disconnect) {
			__set_bit(WOW_DEAUTH_RECVD_EVENT, &wow_mask);
			__set_bit(WOW_DISASSOC_RECVD_EVENT, &wow_mask);
			__set_bit(WOW_BMISS_EVENT, &wow_mask);
			__set_bit(WOW_CSA_IE_EVENT, &wow_mask);
		}

		if (wowlan->magic_pkt)
			__set_bit(WOW_MAGIC_PKT_RECVD_EVENT, &wow_mask);
		break;
	default:
		break;
	}

	for (i = 0; i < wowlan->n_patterns; i++) {
		const struct cfg80211_pkt_pattern *eth_pattern = &patterns[i];
		struct ath12k_pkt_pattern new_pattern = {};

		if (WARN_ON(eth_pattern->pattern_len > WOW_MAX_PATTERN_SIZE))
			return -EINVAL;

		if (ar->ab->wow.wmi_conf_rx_decap_mode ==
		    ATH12K_HW_TXRX_NATIVE_WIFI) {
			ath12k_wow_convert_8023_to_80211(ar, eth_pattern,
							 &new_pattern);

			if (WARN_ON(new_pattern.pattern_len > WOW_MAX_PATTERN_SIZE))
				return -EINVAL;
		} else {
			memcpy(new_pattern.pattern, eth_pattern->pattern,
			       eth_pattern->pattern_len);

			/* convert bitmask to bytemask */
			for (i = 0; i < eth_pattern->pattern_len; i++)
				if (eth_pattern->mask[i / 8] & BIT(i % 8))
					new_pattern.bytemask[i] = 0xff;

			new_pattern.pattern_len = eth_pattern->pattern_len;
			new_pattern.pkt_offset = eth_pattern->pkt_offset;
		}

		ret = ath12k_wmi_wow_add_pattern(ar, arvif->vdev_id,
						 pattern_id,
						 new_pattern.pattern,
						 new_pattern.bytemask,
						 new_pattern.pattern_len,
						 new_pattern.pkt_offset);
		if (ret) {
			ath12k_warn(ar->ab, "failed to add pattern %i to vdev %i: %d\n",
				    pattern_id,
				    arvif->vdev_id, ret);
			return ret;
		}

		pattern_id++;
		__set_bit(WOW_PATTERN_MATCH_EVENT, &wow_mask);
	}

	for (i = 0; i < WOW_EVENT_MAX; i++) {
		if (!test_bit(i, &wow_mask))
			continue;
		ret = ath12k_wmi_wow_add_wakeup_event(ar, arvif->vdev_id, i, 1);
		if (ret) {
			ath12k_warn(ar->ab, "failed to enable wakeup event %s on vdev %i: %d\n",
				    wow_wakeup_event(i), arvif->vdev_id, ret);
			return ret;
		}
	}

	return 0;
}

static int ath12k_wow_set_wakeups(struct ath12k *ar,
				  struct cfg80211_wowlan *wowlan)
{
	struct ath12k_vif *arvif;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	list_for_each_entry(arvif, &ar->arvifs, list) {
		ret = ath12k_vif_wow_set_wakeups(arvif, wowlan);
		if (ret) {
			ath12k_warn(ar->ab, "failed to set wow wakeups on vdev %i: %d\n",
				    arvif->vdev_id, ret);
			return ret;
		}
	}

	return 0;
}

int ath12k_wow_op_suspend(struct ieee80211_hw *hw,
			  struct cfg80211_wowlan *wowlan)
{
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k *ar = ath12k_ah_to_ar(ah, 0);
	int ret;

	mutex_lock(&ar->conf_mutex);

	ret =  ath12k_wow_cleanup(ar);
	if (ret) {
		ath12k_warn(ar->ab, "failed to clear wow wakeup events: %d\n",
			    ret);
		goto exit;
	}

	ret = ath12k_wow_set_wakeups(ar, wowlan);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set wow wakeup events: %d\n",
			    ret);
		goto cleanup;
	}

	ret = ath12k_mac_wait_tx_complete(ar);
	if (ret) {
		ath12k_warn(ar->ab, "failed to wait tx complete: %d\n", ret);
		goto cleanup;
	}

	ret = ath12k_wow_enable(ar);
	if (ret) {
		ath12k_warn(ar->ab, "failed to start wow: %d\n", ret);
		goto cleanup;
	}

	ath12k_hif_irq_disable(ar->ab);
	ath12k_hif_ce_irq_disable(ar->ab);

	ret = ath12k_hif_suspend(ar->ab);
	if (ret) {
		ath12k_warn(ar->ab, "failed to suspend hif: %d\n", ret);
		goto wakeup;
	}

	goto exit;

wakeup:
	ath12k_wow_wakeup(ar);

cleanup:
	ath12k_wow_cleanup(ar);

exit:
	mutex_unlock(&ar->conf_mutex);
	return ret ? 1 : 0;
}

void ath12k_wow_op_set_wakeup(struct ieee80211_hw *hw, bool enabled)
{
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k *ar = ath12k_ah_to_ar(ah, 0);

	mutex_lock(&ar->conf_mutex);
	device_set_wakeup_enable(ar->ab->dev, enabled);
	mutex_unlock(&ar->conf_mutex);
}

int ath12k_wow_op_resume(struct ieee80211_hw *hw)
{
	struct ath12k_hw *ah = ath12k_hw_to_ah(hw);
	struct ath12k *ar = ath12k_ah_to_ar(ah, 0);
	int ret;

	mutex_lock(&ar->conf_mutex);

	ret = ath12k_hif_resume(ar->ab);
	if (ret) {
		ath12k_warn(ar->ab, "failed to resume hif: %d\n", ret);
		goto exit;
	}

	ath12k_hif_ce_irq_enable(ar->ab);
	ath12k_hif_irq_enable(ar->ab);

	ret = ath12k_wow_wakeup(ar);
	if (ret)
		ath12k_warn(ar->ab, "failed to wakeup from wow: %d\n", ret);

exit:
	if (ret) {
		switch (ah->state) {
		case ATH12K_HW_STATE_ON:
			ah->state = ATH12K_HW_STATE_RESTARTING;
			ret = 1;
			break;
		case ATH12K_HW_STATE_OFF:
		case ATH12K_HW_STATE_RESTARTING:
		case ATH12K_HW_STATE_RESTARTED:
		case ATH12K_HW_STATE_WEDGED:
			ath12k_warn(ar->ab, "encountered unexpected device state %d on resume, cannot recover\n",
				    ah->state);
			ret = -EIO;
			break;
		}
	}

	mutex_unlock(&ar->conf_mutex);
	return ret;
}

int ath12k_wow_init(struct ath12k *ar)
{
	if (WARN_ON(!test_bit(WMI_TLV_SERVICE_WOW, ar->wmi->wmi_ab->svc_map)))
		return -EINVAL;

	ar->wow.wowlan_support = ath12k_wowlan_support;

	if (ar->ab->wow.wmi_conf_rx_decap_mode == ATH12K_HW_TXRX_NATIVE_WIFI) {
		ar->wow.wowlan_support.pattern_max_len -= WOW_MAX_REDUCE;
		ar->wow.wowlan_support.max_pkt_offset -= WOW_MAX_REDUCE;
	}

	ar->wow.max_num_patterns = ATH12K_WOW_PATTERNS;
	ar->wow.wowlan_support.n_patterns = ar->wow.max_num_patterns;
	ar->ah->hw->wiphy->wowlan = &ar->wow.wowlan_support;

	device_set_wakeup_capable(ar->ab->dev, true);

	return 0;
}
