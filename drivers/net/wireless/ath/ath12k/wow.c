// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/inetdevice.h>
#include <net/addrconf.h>
#include <net/if_inet6.h>
#include <net/ipv6.h>

#include "mac.h"

#include <net/mac80211.h>
#include "core.h"
#include "hif.h"
#include "debug.h"
#include "wmi.h"
#include "wow.h"

static const struct wiphy_wowlan_support ath12k_wowlan_support = {
	.flags = WIPHY_WOWLAN_DISCONNECT |
		 WIPHY_WOWLAN_MAGIC_PKT |
		 WIPHY_WOWLAN_SUPPORTS_GTK_REKEY |
		 WIPHY_WOWLAN_GTK_REKEY_FAILURE,
	.pattern_min_len = WOW_MIN_PATTERN_SIZE,
	.pattern_max_len = WOW_MAX_PATTERN_SIZE,
	.max_pkt_offset = WOW_MAX_PKT_OFFSET,
};

static inline bool ath12k_wow_is_p2p_vdev(struct ath12k_vif *arvif)
{
	return (arvif->vdev_subtype == WMI_VDEV_SUBTYPE_P2P_DEVICE ||
		arvif->vdev_subtype == WMI_VDEV_SUBTYPE_P2P_CLIENT ||
		arvif->vdev_subtype == WMI_VDEV_SUBTYPE_P2P_GO);
}

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

		if (size_add(eth_pkt_ofs, eth_pat_len) < ETH_ALEN) {
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

		if (size_add(eth_pkt_ofs, eth_pat_len) < prot_ofs) {
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

static int
ath12k_wow_pno_check_and_convert(struct ath12k *ar, u32 vdev_id,
				 const struct cfg80211_sched_scan_request *nd_config,
				 struct wmi_pno_scan_req_arg *pno)
{
	int i, j;
	u8 ssid_len;

	pno->enable = 1;
	pno->vdev_id = vdev_id;
	pno->uc_networks_count = nd_config->n_match_sets;

	if (!pno->uc_networks_count ||
	    pno->uc_networks_count > WMI_PNO_MAX_SUPP_NETWORKS)
		return -EINVAL;

	if (nd_config->n_channels > WMI_PNO_MAX_NETW_CHANNELS_EX)
		return -EINVAL;

	/* Filling per profile params */
	for (i = 0; i < pno->uc_networks_count; i++) {
		ssid_len = nd_config->match_sets[i].ssid.ssid_len;

		if (ssid_len == 0 || ssid_len > 32)
			return -EINVAL;

		pno->a_networks[i].ssid.ssid_len = ssid_len;

		memcpy(pno->a_networks[i].ssid.ssid,
		       nd_config->match_sets[i].ssid.ssid,
		       ssid_len);
		pno->a_networks[i].authentication = 0;
		pno->a_networks[i].encryption     = 0;
		pno->a_networks[i].bcast_nw_type  = 0;

		/* Copying list of valid channel into request */
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
		for (j = 0; j < pno->uc_networks_count; j++) {
			if (pno->a_networks[j].ssid.ssid_len ==
				nd_config->ssids[i].ssid_len &&
			    !memcmp(pno->a_networks[j].ssid.ssid,
				    nd_config->ssids[i].ssid,
				    pno->a_networks[j].ssid.ssid_len)) {
				pno->a_networks[j].bcast_nw_type = BCAST_HIDDEN;
				break;
			}
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
		ath12k_warn(ar->ab, "Invalid number of PNO scan plans: %d",
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

	return 0;
}

static int ath12k_wow_vif_set_wakeups(struct ath12k_vif *arvif,
				      struct cfg80211_wowlan *wowlan)
{
	const struct cfg80211_pkt_pattern *patterns = wowlan->patterns;
	struct ath12k *ar = arvif->ar;
	unsigned long wow_mask = 0;
	int pattern_id = 0;
	int ret, i, j;

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

		if (wowlan->nd_config) {
			struct wmi_pno_scan_req_arg *pno;
			int ret;

			pno = kzalloc(sizeof(*pno), GFP_KERNEL);
			if (!pno)
				return -ENOMEM;

			ar->nlo_enabled = true;

			ret = ath12k_wow_pno_check_and_convert(ar, arvif->vdev_id,
							       wowlan->nd_config, pno);
			if (!ret) {
				ath12k_wmi_wow_config_pno(ar, arvif->vdev_id, pno);
				__set_bit(WOW_NLO_DETECTED_EVENT, &wow_mask);
			}

			kfree(pno);
		}
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
			for (j = 0; j < eth_pattern->pattern_len; j++)
				if (eth_pattern->mask[j / 8] & BIT(j % 8))
					new_pattern.bytemask[j] = 0xff;

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
		if (ath12k_wow_is_p2p_vdev(arvif))
			continue;
		ret = ath12k_wow_vif_set_wakeups(arvif, wowlan);
		if (ret) {
			ath12k_warn(ar->ab, "failed to set wow wakeups on vdev %i: %d\n",
				    arvif->vdev_id, ret);
			return ret;
		}
	}

	return 0;
}

static int ath12k_wow_vdev_clean_nlo(struct ath12k *ar, u32 vdev_id)
{
	struct wmi_pno_scan_req_arg *pno;
	int ret;

	if (!ar->nlo_enabled)
		return 0;

	pno = kzalloc(sizeof(*pno), GFP_KERNEL);
	if (!pno)
		return -ENOMEM;

	pno->enable = 0;
	ret = ath12k_wmi_wow_config_pno(ar, vdev_id, pno);
	if (ret) {
		ath12k_warn(ar->ab, "failed to disable PNO: %d", ret);
		goto out;
	}

	ar->nlo_enabled = false;

out:
	kfree(pno);
	return ret;
}

static int ath12k_wow_vif_clean_nlo(struct ath12k_vif *arvif)
{
	struct ath12k *ar = arvif->ar;

	switch (arvif->vdev_type) {
	case WMI_VDEV_TYPE_STA:
		return ath12k_wow_vdev_clean_nlo(ar, arvif->vdev_id);
	default:
		return 0;
	}
}

static int ath12k_wow_nlo_cleanup(struct ath12k *ar)
{
	struct ath12k_vif *arvif;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	list_for_each_entry(arvif, &ar->arvifs, list) {
		if (ath12k_wow_is_p2p_vdev(arvif))
			continue;

		ret = ath12k_wow_vif_clean_nlo(arvif);
		if (ret) {
			ath12k_warn(ar->ab, "failed to clean nlo settings on vdev %i: %d\n",
				    arvif->vdev_id, ret);
			return ret;
		}
	}

	return 0;
}

static int ath12k_wow_set_hw_filter(struct ath12k *ar)
{
	struct wmi_hw_data_filter_arg arg;
	struct ath12k_vif *arvif;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	list_for_each_entry(arvif, &ar->arvifs, list) {
		if (arvif->vdev_type != WMI_VDEV_TYPE_STA)
			continue;

		arg.vdev_id = arvif->vdev_id;
		arg.enable = true;
		arg.hw_filter_bitmap = WMI_HW_DATA_FILTER_DROP_NON_ICMPV6_MC;
		ret = ath12k_wmi_hw_data_filter_cmd(ar, &arg);
		if (ret) {
			ath12k_warn(ar->ab, "failed to set hw data filter on vdev %i: %d\n",
				    arvif->vdev_id, ret);
			return ret;
		}
	}

	return 0;
}

static int ath12k_wow_clear_hw_filter(struct ath12k *ar)
{
	struct wmi_hw_data_filter_arg arg;
	struct ath12k_vif *arvif;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	list_for_each_entry(arvif, &ar->arvifs, list) {
		if (arvif->vdev_type != WMI_VDEV_TYPE_STA)
			continue;

		arg.vdev_id = arvif->vdev_id;
		arg.enable = false;
		arg.hw_filter_bitmap = 0;
		ret = ath12k_wmi_hw_data_filter_cmd(ar, &arg);

		if (ret) {
			ath12k_warn(ar->ab, "failed to clear hw data filter on vdev %i: %d\n",
				    arvif->vdev_id, ret);
			return ret;
		}
	}

	return 0;
}

static void ath12k_wow_generate_ns_mc_addr(struct ath12k_base *ab,
					   struct wmi_arp_ns_offload_arg *offload)
{
	int i;

	for (i = 0; i < offload->ipv6_count; i++) {
		offload->self_ipv6_addr[i][0] = 0xff;
		offload->self_ipv6_addr[i][1] = 0x02;
		offload->self_ipv6_addr[i][11] = 0x01;
		offload->self_ipv6_addr[i][12] = 0xff;
		offload->self_ipv6_addr[i][13] =
					offload->ipv6_addr[i][13];
		offload->self_ipv6_addr[i][14] =
					offload->ipv6_addr[i][14];
		offload->self_ipv6_addr[i][15] =
					offload->ipv6_addr[i][15];
		ath12k_dbg(ab, ATH12K_DBG_WOW, "NS solicited addr %pI6\n",
			   offload->self_ipv6_addr[i]);
	}
}

static void ath12k_wow_prepare_ns_offload(struct ath12k_vif *arvif,
					  struct wmi_arp_ns_offload_arg *offload)
{
	struct net_device *ndev = ieee80211_vif_to_wdev(arvif->vif)->netdev;
	struct ath12k_base *ab = arvif->ar->ab;
	struct inet6_ifaddr *ifa6;
	struct ifacaddr6 *ifaca6;
	struct inet6_dev *idev;
	u32 count = 0, scope;

	if (!ndev)
		return;

	idev = in6_dev_get(ndev);
	if (!idev)
		return;

	ath12k_dbg(ab, ATH12K_DBG_WOW, "wow prepare ns offload\n");

	read_lock_bh(&idev->lock);

	/* get unicast address */
	list_for_each_entry(ifa6, &idev->addr_list, if_list) {
		if (count >= WMI_IPV6_MAX_COUNT)
			goto unlock;

		if (ifa6->flags & IFA_F_DADFAILED)
			continue;

		scope = ipv6_addr_src_scope(&ifa6->addr);
		if (scope != IPV6_ADDR_SCOPE_LINKLOCAL &&
		    scope != IPV6_ADDR_SCOPE_GLOBAL) {
			ath12k_dbg(ab, ATH12K_DBG_WOW,
				   "Unsupported ipv6 scope: %d\n", scope);
			continue;
		}

		memcpy(offload->ipv6_addr[count], &ifa6->addr.s6_addr,
		       sizeof(ifa6->addr.s6_addr));
		offload->ipv6_type[count] = WMI_IPV6_UC_TYPE;
		ath12k_dbg(ab, ATH12K_DBG_WOW, "mac count %d ipv6 uc %pI6 scope %d\n",
			   count, offload->ipv6_addr[count],
			   scope);
		count++;
	}

	/* get anycast address */
	rcu_read_lock();

	for (ifaca6 = rcu_dereference(idev->ac_list); ifaca6;
	     ifaca6 = rcu_dereference(ifaca6->aca_next)) {
		if (count >= WMI_IPV6_MAX_COUNT) {
			rcu_read_unlock();
			goto unlock;
		}

		scope = ipv6_addr_src_scope(&ifaca6->aca_addr);
		if (scope != IPV6_ADDR_SCOPE_LINKLOCAL &&
		    scope != IPV6_ADDR_SCOPE_GLOBAL) {
			ath12k_dbg(ab, ATH12K_DBG_WOW,
				   "Unsupported ipv scope: %d\n", scope);
			continue;
		}

		memcpy(offload->ipv6_addr[count], &ifaca6->aca_addr,
		       sizeof(ifaca6->aca_addr));
		offload->ipv6_type[count] = WMI_IPV6_AC_TYPE;
		ath12k_dbg(ab, ATH12K_DBG_WOW, "mac count %d ipv6 ac %pI6 scope %d\n",
			   count, offload->ipv6_addr[count],
			   scope);
		count++;
	}

	rcu_read_unlock();

unlock:
	read_unlock_bh(&idev->lock);

	in6_dev_put(idev);

	offload->ipv6_count = count;
	ath12k_wow_generate_ns_mc_addr(ab, offload);
}

static void ath12k_wow_prepare_arp_offload(struct ath12k_vif *arvif,
					   struct wmi_arp_ns_offload_arg *offload)
{
	struct ieee80211_vif *vif = arvif->vif;
	struct ieee80211_vif_cfg vif_cfg = vif->cfg;
	struct ath12k_base *ab = arvif->ar->ab;
	u32 ipv4_cnt;

	ath12k_dbg(ab, ATH12K_DBG_WOW, "wow prepare arp offload\n");

	ipv4_cnt = min(vif_cfg.arp_addr_cnt, WMI_IPV4_MAX_COUNT);
	memcpy(offload->ipv4_addr, vif_cfg.arp_addr_list, ipv4_cnt * sizeof(u32));
	offload->ipv4_count = ipv4_cnt;

	ath12k_dbg(ab, ATH12K_DBG_WOW,
		   "wow arp_addr_cnt %d vif->addr %pM, offload_addr %pI4\n",
		   vif_cfg.arp_addr_cnt, vif->addr, offload->ipv4_addr);
}

static int ath12k_wow_arp_ns_offload(struct ath12k *ar, bool enable)
{
	struct wmi_arp_ns_offload_arg *offload;
	struct ath12k_vif *arvif;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	offload = kmalloc(sizeof(*offload), GFP_KERNEL);
	if (!offload)
		return -ENOMEM;

	list_for_each_entry(arvif, &ar->arvifs, list) {
		if (arvif->vdev_type != WMI_VDEV_TYPE_STA)
			continue;

		memset(offload, 0, sizeof(*offload));

		memcpy(offload->mac_addr, arvif->vif->addr, ETH_ALEN);
		ath12k_wow_prepare_ns_offload(arvif, offload);
		ath12k_wow_prepare_arp_offload(arvif, offload);

		ret = ath12k_wmi_arp_ns_offload(ar, arvif, offload, enable);
		if (ret) {
			ath12k_warn(ar->ab, "failed to set arp ns offload vdev %i: enable %d, ret %d\n",
				    arvif->vdev_id, enable, ret);
			return ret;
		}
	}

	kfree(offload);

	return 0;
}

static int ath12k_gtk_rekey_offload(struct ath12k *ar, bool enable)
{
	struct ath12k_vif *arvif;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	list_for_each_entry(arvif, &ar->arvifs, list) {
		if (arvif->vdev_type != WMI_VDEV_TYPE_STA ||
		    !arvif->is_up ||
		    !arvif->rekey_data.enable_offload)
			continue;

		/* get rekey info before disable rekey offload */
		if (!enable) {
			ret = ath12k_wmi_gtk_rekey_getinfo(ar, arvif);
			if (ret) {
				ath12k_warn(ar->ab, "failed to request rekey info vdev %i, ret %d\n",
					    arvif->vdev_id, ret);
				return ret;
			}
		}

		ret = ath12k_wmi_gtk_rekey_offload(ar, arvif, enable);

		if (ret) {
			ath12k_warn(ar->ab, "failed to offload gtk reky vdev %i: enable %d, ret %d\n",
				    arvif->vdev_id, enable, ret);
			return ret;
		}
	}

	return 0;
}

static int ath12k_wow_protocol_offload(struct ath12k *ar, bool enable)
{
	int ret;

	ret = ath12k_wow_arp_ns_offload(ar, enable);
	if (ret) {
		ath12k_warn(ar->ab, "failed to offload ARP and NS %d %d\n",
			    enable, ret);
		return ret;
	}

	ret = ath12k_gtk_rekey_offload(ar, enable);
	if (ret) {
		ath12k_warn(ar->ab, "failed to offload gtk rekey %d %d\n",
			    enable, ret);
		return ret;
	}

	return 0;
}

static int ath12k_wow_set_keepalive(struct ath12k *ar,
				    enum wmi_sta_keepalive_method method,
				    u32 interval)
{
	struct ath12k_vif *arvif;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	list_for_each_entry(arvif, &ar->arvifs, list) {
		ret = ath12k_mac_vif_set_keepalive(arvif, method, interval);
		if (ret)
			return ret;
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

	ret = ath12k_wow_protocol_offload(ar, true);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set wow protocol offload events: %d\n",
			    ret);
		goto cleanup;
	}

	ret = ath12k_mac_wait_tx_complete(ar);
	if (ret) {
		ath12k_warn(ar->ab, "failed to wait tx complete: %d\n", ret);
		goto cleanup;
	}

	ret = ath12k_wow_set_hw_filter(ar);
	if (ret) {
		ath12k_warn(ar->ab, "failed to set hw filter: %d\n",
			    ret);
		goto cleanup;
	}

	ret = ath12k_wow_set_keepalive(ar,
				       WMI_STA_KEEPALIVE_METHOD_NULL_FRAME,
				       WMI_STA_KEEPALIVE_INTERVAL_DEFAULT);
	if (ret) {
		ath12k_warn(ar->ab, "failed to enable wow keepalive: %d\n", ret);
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
	if (ret) {
		ath12k_warn(ar->ab, "failed to wakeup from wow: %d\n", ret);
		goto exit;
	}

	ret = ath12k_wow_nlo_cleanup(ar);
	if (ret) {
		ath12k_warn(ar->ab, "failed to cleanup nlo: %d\n", ret);
		goto exit;
	}

	ret = ath12k_wow_clear_hw_filter(ar);
	if (ret) {
		ath12k_warn(ar->ab, "failed to clear hw filter: %d\n", ret);
		goto exit;
	}

	ret = ath12k_wow_protocol_offload(ar, false);
	if (ret) {
		ath12k_warn(ar->ab, "failed to clear wow protocol offload events: %d\n",
			    ret);
		goto exit;
	}

	ret = ath12k_wow_set_keepalive(ar,
				       WMI_STA_KEEPALIVE_METHOD_NULL_FRAME,
				       WMI_STA_KEEPALIVE_INTERVAL_DISABLE);
	if (ret) {
		ath12k_warn(ar->ab, "failed to disable wow keepalive: %d\n", ret);
		goto exit;
	}

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
	if (!test_bit(WMI_TLV_SERVICE_WOW, ar->wmi->wmi_ab->svc_map))
		return 0;

	ar->wow.wowlan_support = ath12k_wowlan_support;

	if (ar->ab->wow.wmi_conf_rx_decap_mode == ATH12K_HW_TXRX_NATIVE_WIFI) {
		ar->wow.wowlan_support.pattern_max_len -= WOW_MAX_REDUCE;
		ar->wow.wowlan_support.max_pkt_offset -= WOW_MAX_REDUCE;
	}

	if (test_bit(WMI_TLV_SERVICE_NLO, ar->wmi->wmi_ab->svc_map)) {
		ar->wow.wowlan_support.flags |= WIPHY_WOWLAN_NET_DETECT;
		ar->wow.wowlan_support.max_nd_match_sets = WMI_PNO_MAX_SUPP_NETWORKS;
	}

	ar->wow.max_num_patterns = ATH12K_WOW_PATTERNS;
	ar->wow.wowlan_support.n_patterns = ar->wow.max_num_patterns;
	ar->ah->hw->wiphy->wowlan = &ar->wow.wowlan_support;

	device_set_wakeup_capable(ar->ab->dev, true);

	return 0;
}
