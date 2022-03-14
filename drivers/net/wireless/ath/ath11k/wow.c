// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 */

#include <linux/delay.h>

#include "mac.h"

#include <net/mac80211.h>
#include "core.h"
#include "hif.h"
#include "debug.h"
#include "wmi.h"
#include "wow.h"

static const struct wiphy_wowlan_support ath11k_wowlan_support = {
	.flags = WIPHY_WOWLAN_DISCONNECT |
		 WIPHY_WOWLAN_MAGIC_PKT,
	.pattern_min_len = WOW_MIN_PATTERN_SIZE,
	.pattern_max_len = WOW_MAX_PATTERN_SIZE,
	.max_pkt_offset = WOW_MAX_PKT_OFFSET,
};

int ath11k_wow_enable(struct ath11k_base *ab)
{
	struct ath11k *ar = ath11k_ab_to_ar(ab, 0);
	int i, ret;

	clear_bit(ATH11K_FLAG_HTC_SUSPEND_COMPLETE, &ab->dev_flags);

	for (i = 0; i < ATH11K_WOW_RETRY_NUM; i++) {
		reinit_completion(&ab->htc_suspend);

		ret = ath11k_wmi_wow_enable(ar);
		if (ret) {
			ath11k_warn(ab, "failed to issue wow enable: %d\n", ret);
			return ret;
		}

		ret = wait_for_completion_timeout(&ab->htc_suspend, 3 * HZ);
		if (ret == 0) {
			ath11k_warn(ab,
				    "timed out while waiting for htc suspend completion\n");
			return -ETIMEDOUT;
		}

		if (test_bit(ATH11K_FLAG_HTC_SUSPEND_COMPLETE, &ab->dev_flags))
			/* success, suspend complete received */
			return 0;

		ath11k_warn(ab, "htc suspend not complete, retrying (try %d)\n",
			    i);
		msleep(ATH11K_WOW_RETRY_WAIT_MS);
	}

	ath11k_warn(ab, "htc suspend not complete, failing after %d tries\n", i);

	return -ETIMEDOUT;
}

int ath11k_wow_wakeup(struct ath11k_base *ab)
{
	struct ath11k *ar = ath11k_ab_to_ar(ab, 0);
	int ret;

	reinit_completion(&ab->wow.wakeup_completed);

	ret = ath11k_wmi_wow_host_wakeup_ind(ar);
	if (ret) {
		ath11k_warn(ab, "failed to send wow wakeup indication: %d\n",
			    ret);
		return ret;
	}

	ret = wait_for_completion_timeout(&ab->wow.wakeup_completed, 3 * HZ);
	if (ret == 0) {
		ath11k_warn(ab, "timed out while waiting for wow wakeup completion\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int ath11k_wow_vif_cleanup(struct ath11k_vif *arvif)
{
	struct ath11k *ar = arvif->ar;
	int i, ret;

	for (i = 0; i < WOW_EVENT_MAX; i++) {
		ret = ath11k_wmi_wow_add_wakeup_event(ar, arvif->vdev_id, i, 0);
		if (ret) {
			ath11k_warn(ar->ab, "failed to issue wow wakeup for event %s on vdev %i: %d\n",
				    wow_wakeup_event(i), arvif->vdev_id, ret);
			return ret;
		}
	}

	for (i = 0; i < ar->wow.max_num_patterns; i++) {
		ret = ath11k_wmi_wow_del_pattern(ar, arvif->vdev_id, i);
		if (ret) {
			ath11k_warn(ar->ab, "failed to delete wow pattern %d for vdev %i: %d\n",
				    i, arvif->vdev_id, ret);
			return ret;
		}
	}

	return 0;
}

static int ath11k_wow_cleanup(struct ath11k *ar)
{
	struct ath11k_vif *arvif;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	list_for_each_entry(arvif, &ar->arvifs, list) {
		ret = ath11k_wow_vif_cleanup(arvif);
		if (ret) {
			ath11k_warn(ar->ab, "failed to clean wow wakeups on vdev %i: %d\n",
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
static void ath11k_wow_convert_8023_to_80211(struct cfg80211_pkt_pattern *new,
					     const struct cfg80211_pkt_pattern *old)
{
	u8 hdr_8023_pattern[ETH_HLEN] = {};
	u8 hdr_8023_bit_mask[ETH_HLEN] = {};
	u8 hdr_80211_pattern[WOW_HDR_LEN] = {};
	u8 hdr_80211_bit_mask[WOW_HDR_LEN] = {};

	int total_len = old->pkt_offset + old->pattern_len;
	int hdr_80211_end_offset;

	struct ieee80211_hdr_3addr *new_hdr_pattern =
		(struct ieee80211_hdr_3addr *)hdr_80211_pattern;
	struct ieee80211_hdr_3addr *new_hdr_mask =
		(struct ieee80211_hdr_3addr *)hdr_80211_bit_mask;
	struct ethhdr *old_hdr_pattern = (struct ethhdr *)hdr_8023_pattern;
	struct ethhdr *old_hdr_mask = (struct ethhdr *)hdr_8023_bit_mask;
	int hdr_len = sizeof(*new_hdr_pattern);

	struct rfc1042_hdr *new_rfc_pattern =
		(struct rfc1042_hdr *)(hdr_80211_pattern + hdr_len);
	struct rfc1042_hdr *new_rfc_mask =
		(struct rfc1042_hdr *)(hdr_80211_bit_mask + hdr_len);
	int rfc_len = sizeof(*new_rfc_pattern);

	memcpy(hdr_8023_pattern + old->pkt_offset,
	       old->pattern, ETH_HLEN - old->pkt_offset);
	memcpy(hdr_8023_bit_mask + old->pkt_offset,
	       old->mask, ETH_HLEN - old->pkt_offset);

	/* Copy destination address */
	memcpy(new_hdr_pattern->addr1, old_hdr_pattern->h_dest, ETH_ALEN);
	memcpy(new_hdr_mask->addr1, old_hdr_mask->h_dest, ETH_ALEN);

	/* Copy source address */
	memcpy(new_hdr_pattern->addr3, old_hdr_pattern->h_source, ETH_ALEN);
	memcpy(new_hdr_mask->addr3, old_hdr_mask->h_source, ETH_ALEN);

	/* Copy logic link type */
	memcpy(&new_rfc_pattern->snap_type,
	       &old_hdr_pattern->h_proto,
	       sizeof(old_hdr_pattern->h_proto));
	memcpy(&new_rfc_mask->snap_type,
	       &old_hdr_mask->h_proto,
	       sizeof(old_hdr_mask->h_proto));

	/* Compute new pkt_offset */
	if (old->pkt_offset < ETH_ALEN)
		new->pkt_offset = old->pkt_offset +
			offsetof(struct ieee80211_hdr_3addr, addr1);
	else if (old->pkt_offset < offsetof(struct ethhdr, h_proto))
		new->pkt_offset = old->pkt_offset +
			offsetof(struct ieee80211_hdr_3addr, addr3) -
			offsetof(struct ethhdr, h_source);
	else
		new->pkt_offset = old->pkt_offset + hdr_len + rfc_len - ETH_HLEN;

	/* Compute new hdr end offset */
	if (total_len > ETH_HLEN)
		hdr_80211_end_offset = hdr_len + rfc_len;
	else if (total_len > offsetof(struct ethhdr, h_proto))
		hdr_80211_end_offset = hdr_len + rfc_len + total_len - ETH_HLEN;
	else if (total_len > ETH_ALEN)
		hdr_80211_end_offset = total_len - ETH_ALEN +
			offsetof(struct ieee80211_hdr_3addr, addr3);
	else
		hdr_80211_end_offset = total_len +
			offsetof(struct ieee80211_hdr_3addr, addr1);

	new->pattern_len = hdr_80211_end_offset - new->pkt_offset;

	memcpy((u8 *)new->pattern,
	       hdr_80211_pattern + new->pkt_offset,
	       new->pattern_len);
	memcpy((u8 *)new->mask,
	       hdr_80211_bit_mask + new->pkt_offset,
	       new->pattern_len);

	if (total_len > ETH_HLEN) {
		/* Copy frame body */
		memcpy((u8 *)new->pattern + new->pattern_len,
		       (void *)old->pattern + ETH_HLEN - old->pkt_offset,
		       total_len - ETH_HLEN);
		memcpy((u8 *)new->mask + new->pattern_len,
		       (void *)old->mask + ETH_HLEN - old->pkt_offset,
		       total_len - ETH_HLEN);

		new->pattern_len += total_len - ETH_HLEN;
	}
}

static int ath11k_vif_wow_set_wakeups(struct ath11k_vif *arvif,
				      struct cfg80211_wowlan *wowlan)
{
	int ret, i;
	unsigned long wow_mask = 0;
	struct ath11k *ar = arvif->ar;
	const struct cfg80211_pkt_pattern *patterns = wowlan->patterns;
	int pattern_id = 0;

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
		u8 bitmask[WOW_MAX_PATTERN_SIZE] = {};
		u8 ath_pattern[WOW_MAX_PATTERN_SIZE] = {};
		u8 ath_bitmask[WOW_MAX_PATTERN_SIZE] = {};
		struct cfg80211_pkt_pattern new_pattern = {};
		struct cfg80211_pkt_pattern old_pattern = patterns[i];
		int j;

		new_pattern.pattern = ath_pattern;
		new_pattern.mask = ath_bitmask;
		if (patterns[i].pattern_len > WOW_MAX_PATTERN_SIZE)
			continue;
		/* convert bytemask to bitmask */
		for (j = 0; j < patterns[i].pattern_len; j++)
			if (patterns[i].mask[j / 8] & BIT(j % 8))
				bitmask[j] = 0xff;
		old_pattern.mask = bitmask;

		if (ar->wmi->wmi_ab->wlan_resource_config.rx_decap_mode ==
		    ATH11K_HW_TXRX_NATIVE_WIFI) {
			if (patterns[i].pkt_offset < ETH_HLEN) {
				u8 pattern_ext[WOW_MAX_PATTERN_SIZE] = {};

				memcpy(pattern_ext, old_pattern.pattern,
				       old_pattern.pattern_len);
				old_pattern.pattern = pattern_ext;
				ath11k_wow_convert_8023_to_80211(&new_pattern,
								 &old_pattern);
			} else {
				new_pattern = old_pattern;
				new_pattern.pkt_offset += WOW_HDR_LEN - ETH_HLEN;
			}
		}

		if (WARN_ON(new_pattern.pattern_len > WOW_MAX_PATTERN_SIZE))
			return -EINVAL;

		ret = ath11k_wmi_wow_add_pattern(ar, arvif->vdev_id,
						 pattern_id,
						 new_pattern.pattern,
						 new_pattern.mask,
						 new_pattern.pattern_len,
						 new_pattern.pkt_offset);
		if (ret) {
			ath11k_warn(ar->ab, "failed to add pattern %i to vdev %i: %d\n",
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
		ret = ath11k_wmi_wow_add_wakeup_event(ar, arvif->vdev_id, i, 1);
		if (ret) {
			ath11k_warn(ar->ab, "failed to enable wakeup event %s on vdev %i: %d\n",
				    wow_wakeup_event(i), arvif->vdev_id, ret);
			return ret;
		}
	}

	return 0;
}

static int ath11k_wow_set_wakeups(struct ath11k *ar,
				  struct cfg80211_wowlan *wowlan)
{
	struct ath11k_vif *arvif;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	list_for_each_entry(arvif, &ar->arvifs, list) {
		ret = ath11k_vif_wow_set_wakeups(arvif, wowlan);
		if (ret) {
			ath11k_warn(ar->ab, "failed to set wow wakeups on vdev %i: %d\n",
				    arvif->vdev_id, ret);
			return ret;
		}
	}

	return 0;
}

int ath11k_wow_op_suspend(struct ieee80211_hw *hw,
			  struct cfg80211_wowlan *wowlan)
{
	struct ath11k *ar = hw->priv;
	int ret;

	mutex_lock(&ar->conf_mutex);

	ret =  ath11k_wow_cleanup(ar);
	if (ret) {
		ath11k_warn(ar->ab, "failed to clear wow wakeup events: %d\n",
			    ret);
		goto exit;
	}

	ret = ath11k_wow_set_wakeups(ar, wowlan);
	if (ret) {
		ath11k_warn(ar->ab, "failed to set wow wakeup events: %d\n",
			    ret);
		goto cleanup;
	}

	ret = ath11k_mac_wait_tx_complete(ar);
	if (ret) {
		ath11k_warn(ar->ab, "failed to wait tx complete: %d\n", ret);
		goto cleanup;
	}

	ret = ath11k_wow_enable(ar->ab);
	if (ret) {
		ath11k_warn(ar->ab, "failed to start wow: %d\n", ret);
		goto cleanup;
	}

	ath11k_ce_stop_shadow_timers(ar->ab);
	ath11k_dp_stop_shadow_timers(ar->ab);

	ath11k_hif_irq_disable(ar->ab);
	ath11k_hif_ce_irq_disable(ar->ab);

	ret = ath11k_hif_suspend(ar->ab);
	if (ret) {
		ath11k_warn(ar->ab, "failed to suspend hif: %d\n", ret);
		goto wakeup;
	}

	goto exit;

wakeup:
	ath11k_wow_wakeup(ar->ab);

cleanup:
	ath11k_wow_cleanup(ar);

exit:
	mutex_unlock(&ar->conf_mutex);
	return ret ? 1 : 0;
}

void ath11k_wow_op_set_wakeup(struct ieee80211_hw *hw, bool enabled)
{
	struct ath11k *ar = hw->priv;

	mutex_lock(&ar->conf_mutex);
	device_set_wakeup_enable(ar->ab->dev, enabled);
	mutex_unlock(&ar->conf_mutex);
}

int ath11k_wow_op_resume(struct ieee80211_hw *hw)
{
	struct ath11k *ar = hw->priv;
	int ret;

	mutex_lock(&ar->conf_mutex);

	ret = ath11k_hif_resume(ar->ab);
	if (ret) {
		ath11k_warn(ar->ab, "failed to resume hif: %d\n", ret);
		goto exit;
	}

	ath11k_hif_ce_irq_enable(ar->ab);
	ath11k_hif_irq_enable(ar->ab);

	ret = ath11k_wow_wakeup(ar->ab);
	if (ret)
		ath11k_warn(ar->ab, "failed to wakeup from wow: %d\n", ret);

exit:
	if (ret) {
		switch (ar->state) {
		case ATH11K_STATE_ON:
			ar->state = ATH11K_STATE_RESTARTING;
			ret = 1;
			break;
		case ATH11K_STATE_OFF:
		case ATH11K_STATE_RESTARTING:
		case ATH11K_STATE_RESTARTED:
		case ATH11K_STATE_WEDGED:
			ath11k_warn(ar->ab, "encountered unexpected device state %d on resume, cannot recover\n",
				    ar->state);
			ret = -EIO;
			break;
		}
	}

	mutex_unlock(&ar->conf_mutex);
	return ret;
}

int ath11k_wow_init(struct ath11k *ar)
{
	if (WARN_ON(!test_bit(WMI_TLV_SERVICE_WOW, ar->wmi->wmi_ab->svc_map)))
		return -EINVAL;

	ar->wow.wowlan_support = ath11k_wowlan_support;

	if (ar->wmi->wmi_ab->wlan_resource_config.rx_decap_mode ==
	    ATH11K_HW_TXRX_NATIVE_WIFI) {
		ar->wow.wowlan_support.pattern_max_len -= WOW_MAX_REDUCE;
		ar->wow.wowlan_support.max_pkt_offset -= WOW_MAX_REDUCE;
	}

	ar->wow.max_num_patterns = ATH11K_WOW_PATTERNS;
	ar->wow.wowlan_support.n_patterns = ar->wow.max_num_patterns;
	ar->hw->wiphy->wowlan = &ar->wow.wowlan_support;

	device_set_wakeup_capable(ar->ab->dev, true);

	return 0;
}
