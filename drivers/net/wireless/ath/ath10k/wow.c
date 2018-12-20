/*
 * Copyright (c) 2015-2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "mac.h"

#include <net/mac80211.h>
#include "hif.h"
#include "core.h"
#include "debug.h"
#include "wmi.h"
#include "wmi-ops.h"

static const struct wiphy_wowlan_support ath10k_wowlan_support = {
	.flags = WIPHY_WOWLAN_DISCONNECT |
		 WIPHY_WOWLAN_MAGIC_PKT,
	.pattern_min_len = WOW_MIN_PATTERN_SIZE,
	.pattern_max_len = WOW_MAX_PATTERN_SIZE,
	.max_pkt_offset = WOW_MAX_PKT_OFFSET,
};

static int ath10k_wow_vif_cleanup(struct ath10k_vif *arvif)
{
	struct ath10k *ar = arvif->ar;
	int i, ret;

	for (i = 0; i < WOW_EVENT_MAX; i++) {
		ret = ath10k_wmi_wow_add_wakeup_event(ar, arvif->vdev_id, i, 0);
		if (ret) {
			ath10k_warn(ar, "failed to issue wow wakeup for event %s on vdev %i: %d\n",
				    wow_wakeup_event(i), arvif->vdev_id, ret);
			return ret;
		}
	}

	for (i = 0; i < ar->wow.max_num_patterns; i++) {
		ret = ath10k_wmi_wow_del_pattern(ar, arvif->vdev_id, i);
		if (ret) {
			ath10k_warn(ar, "failed to delete wow pattern %d for vdev %i: %d\n",
				    i, arvif->vdev_id, ret);
			return ret;
		}
	}

	return 0;
}

static int ath10k_wow_cleanup(struct ath10k *ar)
{
	struct ath10k_vif *arvif;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	list_for_each_entry(arvif, &ar->arvifs, list) {
		ret = ath10k_wow_vif_cleanup(arvif);
		if (ret) {
			ath10k_warn(ar, "failed to clean wow wakeups on vdev %i: %d\n",
				    arvif->vdev_id, ret);
			return ret;
		}
	}

	return 0;
}

/**
 * Convert a 802.3 format to a 802.11 format.
 *         +------------+-----------+--------+----------------+
 * 802.3:  |dest mac(6B)|src mac(6B)|type(2B)|     body...    |
 *         +------------+-----------+--------+----------------+
 *                |__         |_______    |____________  |________
 *                   |                |                |          |
 *         +--+------------+----+-----------+---------------+-----------+
 * 802.11: |4B|dest mac(6B)| 6B |src mac(6B)|  8B  |type(2B)|  body...  |
 *         +--+------------+----+-----------+---------------+-----------+
 */
static void ath10k_wow_convert_8023_to_80211
					(struct cfg80211_pkt_pattern *new,
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

	/* Caculate new pkt_offset */
	if (old->pkt_offset < ETH_ALEN)
		new->pkt_offset = old->pkt_offset +
			offsetof(struct ieee80211_hdr_3addr, addr1);
	else if (old->pkt_offset < offsetof(struct ethhdr, h_proto))
		new->pkt_offset = old->pkt_offset +
			offsetof(struct ieee80211_hdr_3addr, addr3) -
			offsetof(struct ethhdr, h_source);
	else
		new->pkt_offset = old->pkt_offset + hdr_len + rfc_len - ETH_HLEN;

	/* Caculate new hdr end offset */
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

static int ath10k_wmi_pno_check(struct ath10k *ar, u32 vdev_id,
				struct cfg80211_sched_scan_request *nd_config,
				struct wmi_pno_scan_req *pno)
{
	int i, j, ret = 0;
	u8 ssid_len;

	pno->enable = 1;
	pno->vdev_id = vdev_id;
	pno->uc_networks_count = nd_config->n_match_sets;

	if (!pno->uc_networks_count ||
	    pno->uc_networks_count > WMI_PNO_MAX_SUPP_NETWORKS)
		return -EINVAL;

	if (nd_config->n_channels > WMI_PNO_MAX_NETW_CHANNELS_EX)
		return -EINVAL;

	/* Filling per profile  params */
	for (i = 0; i < pno->uc_networks_count; i++) {
		ssid_len = nd_config->match_sets[i].ssid.ssid_len;

		if (ssid_len == 0 || ssid_len > 32)
			return -EINVAL;

		pno->a_networks[i].ssid.ssid_len = __cpu_to_le32(ssid_len);

		memcpy(pno->a_networks[i].ssid.ssid,
		       nd_config->match_sets[i].ssid.ssid,
		       nd_config->match_sets[i].ssid.ssid_len);
		pno->a_networks[i].authentication = 0;
		pno->a_networks[i].encryption     = 0;
		pno->a_networks[i].bcast_nw_type  = 0;

		/*Copying list of valid channel into request */
		pno->a_networks[i].channel_count = nd_config->n_channels;
		pno->a_networks[i].rssi_threshold = nd_config->match_sets[i].rssi_thold;

		for (j = 0; j < nd_config->n_channels; j++) {
			pno->a_networks[i].channels[j] =
					nd_config->channels[j]->center_freq;
		}
	}

	/* set scan to passive if no SSIDs are specified in the request */
	if (nd_config->n_ssids == 0)
		pno->do_passive_scan = true;
	else
		pno->do_passive_scan = false;

	for (i = 0; i < nd_config->n_ssids; i++) {
		j = 0;
		while (j < pno->uc_networks_count) {
			if (__le32_to_cpu(pno->a_networks[j].ssid.ssid_len) ==
				nd_config->ssids[i].ssid_len &&
			(memcmp(pno->a_networks[j].ssid.ssid,
				nd_config->ssids[i].ssid,
				__le32_to_cpu(pno->a_networks[j].ssid.ssid_len)) == 0)) {
				pno->a_networks[j].bcast_nw_type = BCAST_HIDDEN;
				break;
			}
			j++;
		}
	}

	if (nd_config->n_scan_plans == 2) {
		pno->fast_scan_period = nd_config->scan_plans[0].interval * MSEC_PER_SEC;
		pno->fast_scan_max_cycles = nd_config->scan_plans[0].iterations;
		pno->slow_scan_period =
			nd_config->scan_plans[1].interval * MSEC_PER_SEC;
	} else if (nd_config->n_scan_plans == 1) {
		pno->fast_scan_period = nd_config->scan_plans[0].interval * MSEC_PER_SEC;
		pno->fast_scan_max_cycles = 1;
		pno->slow_scan_period = nd_config->scan_plans[0].interval * MSEC_PER_SEC;
	} else {
		ath10k_warn(ar, "Invalid number of scan plans %d !!",
			    nd_config->n_scan_plans);
	}

	if (nd_config->flags & NL80211_SCAN_FLAG_RANDOM_ADDR) {
		/* enable mac randomization */
		pno->enable_pno_scan_randomization = 1;
		memcpy(pno->mac_addr, nd_config->mac_addr, ETH_ALEN);
		memcpy(pno->mac_addr_mask, nd_config->mac_addr_mask, ETH_ALEN);
	}

	pno->delay_start_time = nd_config->delay;

	/* Current FW does not support min-max range for dwell time */
	pno->active_max_time = WMI_ACTIVE_MAX_CHANNEL_TIME;
	pno->passive_max_time = WMI_PASSIVE_MAX_CHANNEL_TIME;
	return ret;
}

static int ath10k_vif_wow_set_wakeups(struct ath10k_vif *arvif,
				      struct cfg80211_wowlan *wowlan)
{
	int ret, i;
	unsigned long wow_mask = 0;
	struct ath10k *ar = arvif->ar;
	const struct cfg80211_pkt_pattern *patterns = wowlan->patterns;
	int pattern_id = 0;

	/* Setup requested WOW features */
	switch (arvif->vdev_type) {
	case WMI_VDEV_TYPE_IBSS:
		__set_bit(WOW_BEACON_EVENT, &wow_mask);
		 /* fall through */
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

		if (wowlan->nd_config) {
			struct wmi_pno_scan_req *pno;
			int ret;

			pno = kzalloc(sizeof(*pno), GFP_KERNEL);
			if (!pno)
				return -ENOMEM;

			ar->nlo_enabled = true;

			ret = ath10k_wmi_pno_check(ar, arvif->vdev_id,
						   wowlan->nd_config, pno);
			if (!ret) {
				ath10k_wmi_wow_config_pno(ar, arvif->vdev_id, pno);
				__set_bit(WOW_NLO_DETECTED_EVENT, &wow_mask);
			}

			kfree(pno);
		}
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
		new_pattern = old_pattern;

		if (ar->wmi.rx_decap_mode == ATH10K_HW_TXRX_NATIVE_WIFI) {
			if (patterns[i].pkt_offset < ETH_HLEN)
				ath10k_wow_convert_8023_to_80211(&new_pattern,
								 &old_pattern);
			else
				new_pattern.pkt_offset += WOW_HDR_LEN - ETH_HLEN;
		}

		if (WARN_ON(new_pattern.pattern_len > WOW_MAX_PATTERN_SIZE))
			return -EINVAL;

		ret = ath10k_wmi_wow_add_pattern(ar, arvif->vdev_id,
						 pattern_id,
						 new_pattern.pattern,
						 new_pattern.mask,
						 new_pattern.pattern_len,
						 new_pattern.pkt_offset);
		if (ret) {
			ath10k_warn(ar, "failed to add pattern %i to vdev %i: %d\n",
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
		ret = ath10k_wmi_wow_add_wakeup_event(ar, arvif->vdev_id, i, 1);
		if (ret) {
			ath10k_warn(ar, "failed to enable wakeup event %s on vdev %i: %d\n",
				    wow_wakeup_event(i), arvif->vdev_id, ret);
			return ret;
		}
	}

	return 0;
}

static int ath10k_wow_set_wakeups(struct ath10k *ar,
				  struct cfg80211_wowlan *wowlan)
{
	struct ath10k_vif *arvif;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	list_for_each_entry(arvif, &ar->arvifs, list) {
		ret = ath10k_vif_wow_set_wakeups(arvif, wowlan);
		if (ret) {
			ath10k_warn(ar, "failed to set wow wakeups on vdev %i: %d\n",
				    arvif->vdev_id, ret);
			return ret;
		}
	}

	return 0;
}

static int ath10k_vif_wow_clean_nlo(struct ath10k_vif *arvif)
{
	int ret = 0;
	struct ath10k *ar = arvif->ar;

	switch (arvif->vdev_type) {
	case WMI_VDEV_TYPE_STA:
		if (ar->nlo_enabled) {
			struct wmi_pno_scan_req *pno;

			pno = kzalloc(sizeof(*pno), GFP_KERNEL);
			if (!pno)
				return -ENOMEM;

			pno->enable = 0;
			ar->nlo_enabled = false;
			ret = ath10k_wmi_wow_config_pno(ar, arvif->vdev_id, pno);
			kfree(pno);
		}
		break;
	default:
		break;
	}
	return ret;
}

static int ath10k_wow_nlo_cleanup(struct ath10k *ar)
{
	struct ath10k_vif *arvif;
	int ret = 0;

	lockdep_assert_held(&ar->conf_mutex);

	list_for_each_entry(arvif, &ar->arvifs, list) {
		ret = ath10k_vif_wow_clean_nlo(arvif);
		if (ret) {
			ath10k_warn(ar, "failed to clean nlo settings on vdev %i: %d\n",
				    arvif->vdev_id, ret);
			return ret;
		}
	}

	return 0;
}

static int ath10k_wow_enable(struct ath10k *ar)
{
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	reinit_completion(&ar->target_suspend);

	ret = ath10k_wmi_wow_enable(ar);
	if (ret) {
		ath10k_warn(ar, "failed to issue wow enable: %d\n", ret);
		return ret;
	}

	ret = wait_for_completion_timeout(&ar->target_suspend, 3 * HZ);
	if (ret == 0) {
		ath10k_warn(ar, "timed out while waiting for suspend completion\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int ath10k_wow_wakeup(struct ath10k *ar)
{
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	reinit_completion(&ar->wow.wakeup_completed);

	ret = ath10k_wmi_wow_host_wakeup_ind(ar);
	if (ret) {
		ath10k_warn(ar, "failed to send wow wakeup indication: %d\n",
			    ret);
		return ret;
	}

	ret = wait_for_completion_timeout(&ar->wow.wakeup_completed, 3 * HZ);
	if (ret == 0) {
		ath10k_warn(ar, "timed out while waiting for wow wakeup completion\n");
		return -ETIMEDOUT;
	}

	return 0;
}

int ath10k_wow_op_suspend(struct ieee80211_hw *hw,
			  struct cfg80211_wowlan *wowlan)
{
	struct ath10k *ar = hw->priv;
	int ret;

	mutex_lock(&ar->conf_mutex);

	if (WARN_ON(!test_bit(ATH10K_FW_FEATURE_WOWLAN_SUPPORT,
			      ar->running_fw->fw_file.fw_features))) {
		ret = 1;
		goto exit;
	}

	ret =  ath10k_wow_cleanup(ar);
	if (ret) {
		ath10k_warn(ar, "failed to clear wow wakeup events: %d\n",
			    ret);
		goto exit;
	}

	ret = ath10k_wow_set_wakeups(ar, wowlan);
	if (ret) {
		ath10k_warn(ar, "failed to set wow wakeup events: %d\n",
			    ret);
		goto cleanup;
	}

	ath10k_mac_wait_tx_complete(ar);

	ret = ath10k_wow_enable(ar);
	if (ret) {
		ath10k_warn(ar, "failed to start wow: %d\n", ret);
		goto cleanup;
	}

	ret = ath10k_hif_suspend(ar);
	if (ret) {
		ath10k_warn(ar, "failed to suspend hif: %d\n", ret);
		goto wakeup;
	}

	goto exit;

wakeup:
	ath10k_wow_wakeup(ar);

cleanup:
	ath10k_wow_cleanup(ar);

exit:
	mutex_unlock(&ar->conf_mutex);
	return ret ? 1 : 0;
}

void ath10k_wow_op_set_wakeup(struct ieee80211_hw *hw, bool enabled)
{
	struct ath10k *ar = hw->priv;

	mutex_lock(&ar->conf_mutex);
	if (test_bit(ATH10K_FW_FEATURE_WOWLAN_SUPPORT,
		     ar->running_fw->fw_file.fw_features)) {
		device_set_wakeup_enable(ar->dev, enabled);
	}
	mutex_unlock(&ar->conf_mutex);
}

int ath10k_wow_op_resume(struct ieee80211_hw *hw)
{
	struct ath10k *ar = hw->priv;
	int ret;

	mutex_lock(&ar->conf_mutex);

	if (WARN_ON(!test_bit(ATH10K_FW_FEATURE_WOWLAN_SUPPORT,
			      ar->running_fw->fw_file.fw_features))) {
		ret = 1;
		goto exit;
	}

	ret = ath10k_hif_resume(ar);
	if (ret) {
		ath10k_warn(ar, "failed to resume hif: %d\n", ret);
		goto exit;
	}

	ret = ath10k_wow_wakeup(ar);
	if (ret)
		ath10k_warn(ar, "failed to wakeup from wow: %d\n", ret);

	ret = ath10k_wow_nlo_cleanup(ar);
	if (ret)
		ath10k_warn(ar, "failed to cleanup nlo: %d\n", ret);

exit:
	if (ret) {
		switch (ar->state) {
		case ATH10K_STATE_ON:
			ar->state = ATH10K_STATE_RESTARTING;
			ret = 1;
			break;
		case ATH10K_STATE_OFF:
		case ATH10K_STATE_RESTARTING:
		case ATH10K_STATE_RESTARTED:
		case ATH10K_STATE_UTF:
		case ATH10K_STATE_WEDGED:
			ath10k_warn(ar, "encountered unexpected device state %d on resume, cannot recover\n",
				    ar->state);
			ret = -EIO;
			break;
		}
	}

	mutex_unlock(&ar->conf_mutex);
	return ret;
}

int ath10k_wow_init(struct ath10k *ar)
{
	if (!test_bit(ATH10K_FW_FEATURE_WOWLAN_SUPPORT,
		      ar->running_fw->fw_file.fw_features))
		return 0;

	if (WARN_ON(!test_bit(WMI_SERVICE_WOW, ar->wmi.svc_map)))
		return -EINVAL;

	ar->wow.wowlan_support = ath10k_wowlan_support;

	if (ar->wmi.rx_decap_mode == ATH10K_HW_TXRX_NATIVE_WIFI) {
		ar->wow.wowlan_support.pattern_max_len -= WOW_MAX_REDUCE;
		ar->wow.wowlan_support.max_pkt_offset -= WOW_MAX_REDUCE;
	}

	if (test_bit(WMI_SERVICE_NLO, ar->wmi.svc_map)) {
		ar->wow.wowlan_support.flags |= WIPHY_WOWLAN_NET_DETECT;
		ar->wow.wowlan_support.max_nd_match_sets = WMI_PNO_MAX_SUPP_NETWORKS;
	}

	ar->wow.wowlan_support.n_patterns = ar->wow.max_num_patterns;
	ar->hw->wiphy->wowlan = &ar->wow.wowlan_support;

	device_set_wakeup_capable(ar->dev, true);

	return 0;
}
