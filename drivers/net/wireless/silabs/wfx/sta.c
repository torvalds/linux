// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of mac80211 API.
 *
 * Copyright (c) 2017-2020, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <linux/etherdevice.h>
#include <net/mac80211.h>

#include "sta.h"
#include "wfx.h"
#include "fwio.h"
#include "bh.h"
#include "key.h"
#include "scan.h"
#include "debug.h"
#include "hif_tx.h"
#include "hif_tx_mib.h"

#define HIF_MAX_ARP_IP_ADDRTABLE_ENTRIES 2

u32 wfx_rate_mask_to_hw(struct wfx_dev *wdev, u32 rates)
{
	int i;
	u32 ret = 0;
	/* The device only supports 2GHz */
	struct ieee80211_supported_band *sband = wdev->hw->wiphy->bands[NL80211_BAND_2GHZ];

	for (i = 0; i < sband->n_bitrates; i++) {
		if (rates & BIT(i)) {
			if (i >= sband->n_bitrates)
				dev_warn(wdev->dev, "unsupported basic rate\n");
			else
				ret |= BIT(sband->bitrates[i].hw_value);
		}
	}
	return ret;
}

void wfx_cooling_timeout_work(struct work_struct *work)
{
	struct wfx_dev *wdev = container_of(to_delayed_work(work), struct wfx_dev,
					    cooling_timeout_work);

	wdev->chip_frozen = true;
	wfx_tx_unlock(wdev);
}

void wfx_suspend_hot_dev(struct wfx_dev *wdev, enum sta_notify_cmd cmd)
{
	if (cmd == STA_NOTIFY_AWAKE) {
		/* Device recover normal temperature */
		if (cancel_delayed_work(&wdev->cooling_timeout_work))
			wfx_tx_unlock(wdev);
	} else {
		/* Device is too hot */
		schedule_delayed_work(&wdev->cooling_timeout_work, 10 * HZ);
		wfx_tx_lock(wdev);
	}
}

static void wfx_filter_beacon(struct wfx_vif *wvif, bool filter_beacon)
{
	static const struct wfx_hif_ie_table_entry filter_ies[] = {
		{
			.ie_id        = WLAN_EID_VENDOR_SPECIFIC,
			.has_changed  = 1,
			.no_longer    = 1,
			.has_appeared = 1,
			.oui          = { 0x50, 0x6F, 0x9A },
		}, {
			.ie_id        = WLAN_EID_HT_OPERATION,
			.has_changed  = 1,
			.no_longer    = 1,
			.has_appeared = 1,
		}, {
			.ie_id        = WLAN_EID_ERP_INFO,
			.has_changed  = 1,
			.no_longer    = 1,
			.has_appeared = 1,
		}, {
			.ie_id        = WLAN_EID_CHANNEL_SWITCH,
			.has_changed  = 1,
			.no_longer    = 1,
			.has_appeared = 1,
		}
	};

	if (!filter_beacon) {
		wfx_hif_beacon_filter_control(wvif, 0, 1);
	} else {
		wfx_hif_set_beacon_filter_table(wvif, ARRAY_SIZE(filter_ies), filter_ies);
		wfx_hif_beacon_filter_control(wvif, HIF_BEACON_FILTER_ENABLE, 0);
	}
}

void wfx_configure_filter(struct ieee80211_hw *hw, unsigned int changed_flags,
			  unsigned int *total_flags, u64 unused)
{
	bool filter_bssid, filter_prbreq, filter_beacon;
	struct ieee80211_vif *vif = NULL;
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif = NULL;

	/* Notes:
	 *   - Probe responses (FIF_BCN_PRBRESP_PROMISC) are never filtered
	 *   - PS-Poll (FIF_PSPOLL) are never filtered
	 *   - RTS, CTS and Ack (FIF_CONTROL) are always filtered
	 *   - Broken frames (FIF_FCSFAIL and FIF_PLCPFAIL) are always filtered
	 *   - Firmware does (yet) allow to forward unicast traffic sent to other stations (aka.
	 *     promiscuous mode)
	 */
	*total_flags &= FIF_BCN_PRBRESP_PROMISC | FIF_ALLMULTI | FIF_OTHER_BSS |
			FIF_PROBE_REQ | FIF_PSPOLL;

	mutex_lock(&wdev->conf_mutex);
	while ((wvif = wvif_iterate(wdev, wvif)) != NULL) {
		mutex_lock(&wvif->scan_lock);

		/* Note: FIF_BCN_PRBRESP_PROMISC covers probe response and
		 * beacons from other BSS
		 */
		if (*total_flags & FIF_BCN_PRBRESP_PROMISC)
			filter_beacon = false;
		else
			filter_beacon = true;
		wfx_filter_beacon(wvif, filter_beacon);

		if (*total_flags & FIF_OTHER_BSS)
			filter_bssid = false;
		else
			filter_bssid = true;

		vif = wvif_to_vif(wvif);
		/* In AP mode, chip can reply to probe request itself */
		if (*total_flags & FIF_PROBE_REQ && vif->type == NL80211_IFTYPE_AP) {
			dev_dbg(wdev->dev, "do not forward probe request in AP mode\n");
			*total_flags &= ~FIF_PROBE_REQ;
		}

		if (*total_flags & FIF_PROBE_REQ)
			filter_prbreq = false;
		else
			filter_prbreq = true;
		wfx_hif_set_rx_filter(wvif, filter_bssid, filter_prbreq);

		mutex_unlock(&wvif->scan_lock);
	}
	mutex_unlock(&wdev->conf_mutex);
}

static int wfx_get_ps_timeout(struct wfx_vif *wvif, bool *enable_ps)
{
	struct ieee80211_channel *chan0 = NULL, *chan1 = NULL;
	struct ieee80211_conf *conf = &wvif->wdev->hw->conf;
	struct ieee80211_vif *vif = wvif_to_vif(wvif);

	WARN(!vif->cfg.assoc && enable_ps,
	     "enable_ps is reliable only if associated");
	if (wdev_to_wvif(wvif->wdev, 0)) {
		struct wfx_vif *wvif_ch0 = wdev_to_wvif(wvif->wdev, 0);
		struct ieee80211_vif *vif_ch0 = wvif_to_vif(wvif_ch0);

		chan0 = vif_ch0->bss_conf.chandef.chan;
	}
	if (wdev_to_wvif(wvif->wdev, 1)) {
		struct wfx_vif *wvif_ch1 = wdev_to_wvif(wvif->wdev, 1);
		struct ieee80211_vif *vif_ch1 = wvif_to_vif(wvif_ch1);

		chan1 = vif_ch1->bss_conf.chandef.chan;
	}
	if (chan0 && chan1 && vif->type != NL80211_IFTYPE_AP) {
		if (chan0->hw_value == chan1->hw_value) {
			/* It is useless to enable PS if channels are the same. */
			if (enable_ps)
				*enable_ps = false;
			if (vif->cfg.assoc && vif->cfg.ps)
				dev_info(wvif->wdev->dev, "ignoring requested PS mode");
			return -1;
		}
		/* It is necessary to enable PS if channels are different. */
		if (enable_ps)
			*enable_ps = true;
		if (wfx_api_older_than(wvif->wdev, 3, 2))
			return 0;
		else
			return 30;
	}
	if (enable_ps)
		*enable_ps = vif->cfg.ps;
	if (vif->cfg.assoc && vif->cfg.ps)
		return conf->dynamic_ps_timeout;
	else
		return -1;
}

int wfx_update_pm(struct wfx_vif *wvif)
{
	struct ieee80211_vif *vif = wvif_to_vif(wvif);
	int ps_timeout;
	bool ps;

	if (!vif->cfg.assoc)
		return 0;
	ps_timeout = wfx_get_ps_timeout(wvif, &ps);
	if (!ps)
		ps_timeout = 0;
	WARN_ON(ps_timeout < 0);
	if (wvif->uapsd_mask)
		ps_timeout = 0;

	if (!wait_for_completion_timeout(&wvif->set_pm_mode_complete, TU_TO_JIFFIES(512)))
		dev_warn(wvif->wdev->dev, "timeout while waiting of set_pm_mode_complete\n");
	return wfx_hif_set_pm(wvif, ps, ps_timeout);
}

int wfx_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		unsigned int link_id, u16 queue,
		const struct ieee80211_tx_queue_params *params)
{
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif = (struct wfx_vif *)vif->drv_priv;
	int old_uapsd = wvif->uapsd_mask;

	WARN_ON(queue >= hw->queues);

	mutex_lock(&wdev->conf_mutex);
	assign_bit(queue, &wvif->uapsd_mask, params->uapsd);
	wfx_hif_set_edca_queue_params(wvif, queue, params);
	if (vif->type == NL80211_IFTYPE_STATION &&
	    old_uapsd != wvif->uapsd_mask) {
		wfx_hif_set_uapsd_info(wvif, wvif->uapsd_mask);
		wfx_update_pm(wvif);
	}
	mutex_unlock(&wdev->conf_mutex);
	return 0;
}

int wfx_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif = NULL;

	while ((wvif = wvif_iterate(wdev, wvif)) != NULL)
		wfx_hif_rts_threshold(wvif, value);
	return 0;
}

void wfx_event_report_rssi(struct wfx_vif *wvif, u8 raw_rcpi_rssi)
{
	/* RSSI: signed Q8.0, RCPI: unsigned Q7.1
	 * RSSI = RCPI / 2 - 110
	 */
	struct ieee80211_vif *vif = wvif_to_vif(wvif);
	int rcpi_rssi;
	int cqm_evt;

	rcpi_rssi = raw_rcpi_rssi / 2 - 110;
	if (rcpi_rssi <= vif->bss_conf.cqm_rssi_thold)
		cqm_evt = NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW;
	else
		cqm_evt = NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH;
	ieee80211_cqm_rssi_notify(vif, cqm_evt, rcpi_rssi, GFP_KERNEL);
}

static void wfx_beacon_loss_work(struct work_struct *work)
{
	struct wfx_vif *wvif = container_of(to_delayed_work(work), struct wfx_vif,
					    beacon_loss_work);
	struct ieee80211_vif *vif = wvif_to_vif(wvif);
	struct ieee80211_bss_conf *bss_conf = &vif->bss_conf;

	ieee80211_beacon_loss(vif);
	schedule_delayed_work(to_delayed_work(work), msecs_to_jiffies(bss_conf->beacon_int));
}

void wfx_set_default_unicast_key(struct ieee80211_hw *hw, struct ieee80211_vif *vif, int idx)
{
	struct wfx_vif *wvif = (struct wfx_vif *)vif->drv_priv;

	wfx_hif_wep_default_key_id(wvif, idx);
}

void wfx_reset(struct wfx_vif *wvif)
{
	struct wfx_dev *wdev = wvif->wdev;

	wfx_tx_lock_flush(wdev);
	wfx_hif_reset(wvif, false);
	wfx_tx_policy_init(wvif);
	if (wvif_count(wdev) <= 1)
		wfx_hif_set_block_ack_policy(wvif, 0xFF, 0xFF);
	wfx_tx_unlock(wdev);
	wvif->join_in_progress = false;
	cancel_delayed_work_sync(&wvif->beacon_loss_work);
	wvif =  NULL;
	while ((wvif = wvif_iterate(wdev, wvif)) != NULL)
		wfx_update_pm(wvif);
}

int wfx_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_sta *sta)
{
	struct wfx_vif *wvif = (struct wfx_vif *)vif->drv_priv;
	struct wfx_sta_priv *sta_priv = (struct wfx_sta_priv *)&sta->drv_priv;

	sta_priv->vif_id = wvif->id;

	if (vif->type == NL80211_IFTYPE_STATION)
		wfx_hif_set_mfp(wvif, sta->mfp, sta->mfp);

	/* In station mode, the firmware interprets new link-id as a TDLS peer */
	if (vif->type == NL80211_IFTYPE_STATION && !sta->tdls)
		return 0;
	sta_priv->link_id = ffz(wvif->link_id_map);
	wvif->link_id_map |= BIT(sta_priv->link_id);
	WARN_ON(!sta_priv->link_id);
	WARN_ON(sta_priv->link_id >= HIF_LINK_ID_MAX);
	wfx_hif_map_link(wvif, false, sta->addr, sta_priv->link_id, sta->mfp);

	return 0;
}

int wfx_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_sta *sta)
{
	struct wfx_vif *wvif = (struct wfx_vif *)vif->drv_priv;
	struct wfx_sta_priv *sta_priv = (struct wfx_sta_priv *)&sta->drv_priv;

	/* See note in wfx_sta_add() */
	if (!sta_priv->link_id)
		return 0;
	/* FIXME add a mutex? */
	wfx_hif_map_link(wvif, true, sta->addr, sta_priv->link_id, false);
	wvif->link_id_map &= ~BIT(sta_priv->link_id);
	return 0;
}

static int wfx_upload_ap_templates(struct wfx_vif *wvif)
{
	struct ieee80211_vif *vif = wvif_to_vif(wvif);
	struct sk_buff *skb;

	skb = ieee80211_beacon_get(wvif->wdev->hw, vif, 0);
	if (!skb)
		return -ENOMEM;
	wfx_hif_set_template_frame(wvif, skb, HIF_TMPLT_BCN, API_RATE_INDEX_B_1MBPS);
	dev_kfree_skb(skb);

	skb = ieee80211_proberesp_get(wvif->wdev->hw, vif);
	if (!skb)
		return -ENOMEM;
	wfx_hif_set_template_frame(wvif, skb, HIF_TMPLT_PRBRES, API_RATE_INDEX_B_1MBPS);
	dev_kfree_skb(skb);
	return 0;
}

static int wfx_set_mfp_ap(struct wfx_vif *wvif)
{
	struct ieee80211_vif *vif = wvif_to_vif(wvif);
	struct sk_buff *skb = ieee80211_beacon_get(wvif->wdev->hw, vif, 0);
	const int ieoffset = offsetof(struct ieee80211_mgmt, u.beacon.variable);
	const int pairwise_cipher_suite_count_offset = 8 / sizeof(u16);
	const int pairwise_cipher_suite_size = 4 / sizeof(u16);
	const int akm_suite_size = 4 / sizeof(u16);
	int ret = -EINVAL;
	const u16 *ptr;

	if (unlikely(!skb))
		return -ENOMEM;

	ptr = (u16 *)cfg80211_find_ie(WLAN_EID_RSN, skb->data + ieoffset,
				      skb->len - ieoffset);
	if (unlikely(!ptr))
		goto free_skb;

	ptr += pairwise_cipher_suite_count_offset;
	if (WARN_ON(ptr > (u16 *)skb_tail_pointer(skb)))
		goto free_skb;

	ptr += 1 + pairwise_cipher_suite_size * *ptr;
	if (WARN_ON(ptr > (u16 *)skb_tail_pointer(skb)))
		goto free_skb;

	ptr += 1 + akm_suite_size * *ptr;
	if (WARN_ON(ptr > (u16 *)skb_tail_pointer(skb)))
		goto free_skb;

	wfx_hif_set_mfp(wvif, *ptr & BIT(7), *ptr & BIT(6));
	ret = 0;

free_skb:
	dev_kfree_skb(skb);
	return ret;
}

int wfx_start_ap(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		 struct ieee80211_bss_conf *link_conf)
{
	struct wfx_vif *wvif = (struct wfx_vif *)vif->drv_priv;
	struct wfx_dev *wdev = wvif->wdev;
	int ret;

	wvif =  NULL;
	while ((wvif = wvif_iterate(wdev, wvif)) != NULL)
		wfx_update_pm(wvif);
	wvif = (struct wfx_vif *)vif->drv_priv;
	wfx_upload_ap_templates(wvif);
	ret = wfx_hif_start(wvif, &vif->bss_conf, wvif->channel);
	if (ret > 0)
		return -EIO;
	return wfx_set_mfp_ap(wvif);
}

void wfx_stop_ap(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		 struct ieee80211_bss_conf *link_conf)
{
	struct wfx_vif *wvif = (struct wfx_vif *)vif->drv_priv;

	wfx_reset(wvif);
}

static void wfx_join(struct wfx_vif *wvif)
{
	struct ieee80211_vif *vif = wvif_to_vif(wvif);
	struct ieee80211_bss_conf *conf = &vif->bss_conf;
	struct cfg80211_bss *bss = NULL;
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	const u8 *ssid_ie = NULL;
	int ssid_len = 0;
	int ret;

	wfx_tx_lock_flush(wvif->wdev);

	bss = cfg80211_get_bss(wvif->wdev->hw->wiphy, wvif->channel, conf->bssid, NULL, 0,
			       IEEE80211_BSS_TYPE_ANY, IEEE80211_PRIVACY_ANY);
	if (!bss && !vif->cfg.ibss_joined) {
		wfx_tx_unlock(wvif->wdev);
		return;
	}

	rcu_read_lock(); /* protect ssid_ie */
	if (bss)
		ssid_ie = ieee80211_bss_get_ie(bss, WLAN_EID_SSID);
	if (ssid_ie) {
		ssid_len = ssid_ie[1];
		if (ssid_len > IEEE80211_MAX_SSID_LEN)
			ssid_len = IEEE80211_MAX_SSID_LEN;
		memcpy(ssid, &ssid_ie[2], ssid_len);
	}
	rcu_read_unlock();

	cfg80211_put_bss(wvif->wdev->hw->wiphy, bss);

	wvif->join_in_progress = true;
	ret = wfx_hif_join(wvif, conf, wvif->channel, ssid, ssid_len);
	if (ret) {
		ieee80211_connection_loss(vif);
		wfx_reset(wvif);
	} else {
		/* Due to beacon filtering it is possible that the AP's beacon is not known for the
		 * mac80211 stack.  Disable filtering temporary to make sure the stack receives at
		 * least one
		 */
		wfx_filter_beacon(wvif, false);
	}
	wfx_tx_unlock(wvif->wdev);
}

static void wfx_join_finalize(struct wfx_vif *wvif, struct ieee80211_bss_conf *info)
{
	struct ieee80211_vif *vif = wvif_to_vif(wvif);
	struct ieee80211_sta *sta = NULL;
	int ampdu_density = 0;
	bool greenfield = false;

	rcu_read_lock(); /* protect sta */
	if (info->bssid && !vif->cfg.ibss_joined)
		sta = ieee80211_find_sta(vif, info->bssid);
	if (sta && sta->deflink.ht_cap.ht_supported)
		ampdu_density = sta->deflink.ht_cap.ampdu_density;
	if (sta && sta->deflink.ht_cap.ht_supported &&
	    !(info->ht_operation_mode & IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT))
		greenfield = !!(sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_GRN_FLD);
	rcu_read_unlock();

	wvif->join_in_progress = false;
	wfx_hif_set_association_mode(wvif, ampdu_density, greenfield, info->use_short_preamble);
	wfx_hif_keep_alive_period(wvif, 0);
	/* beacon_loss_count is defined to 7 in net/mac80211/mlme.c. Let's use the same value. */
	wfx_hif_set_bss_params(wvif, vif->cfg.aid, 7);
	wfx_hif_set_beacon_wakeup_period(wvif, 1, 1);
	wfx_update_pm(wvif);
}

int wfx_join_ibss(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct wfx_vif *wvif = (struct wfx_vif *)vif->drv_priv;

	wfx_upload_ap_templates(wvif);
	wfx_join(wvif);
	return 0;
}

void wfx_leave_ibss(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct wfx_vif *wvif = (struct wfx_vif *)vif->drv_priv;

	wfx_reset(wvif);
}

static void wfx_enable_beacon(struct wfx_vif *wvif, bool enable)
{
	/* Driver has Content After DTIM Beacon in queue. Driver is waiting for a signal from the
	 * firmware. Since we are going to stop to send beacons, this signal will never happens. See
	 * also wfx_suspend_resume_mc()
	 */
	if (!enable && wfx_tx_queues_has_cab(wvif)) {
		wvif->after_dtim_tx_allowed = true;
		wfx_bh_request_tx(wvif->wdev);
	}
	wfx_hif_beacon_transmit(wvif, enable);
}

void wfx_bss_info_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *info, u64 changed)
{
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif = (struct wfx_vif *)vif->drv_priv;
	int i;

	mutex_lock(&wdev->conf_mutex);

	if (changed & BSS_CHANGED_BASIC_RATES ||
	    changed & BSS_CHANGED_BEACON_INT ||
	    changed & BSS_CHANGED_BSSID) {
		if (vif->type == NL80211_IFTYPE_STATION)
			wfx_join(wvif);
	}

	if (changed & BSS_CHANGED_ASSOC) {
		if (vif->cfg.assoc || vif->cfg.ibss_joined)
			wfx_join_finalize(wvif, info);
		else if (!vif->cfg.assoc && vif->type == NL80211_IFTYPE_STATION)
			wfx_reset(wvif);
		else
			dev_warn(wdev->dev, "misunderstood change: ASSOC\n");
	}

	if (changed & BSS_CHANGED_BEACON_INFO) {
		if (vif->type != NL80211_IFTYPE_STATION)
			dev_warn(wdev->dev, "misunderstood change: BEACON_INFO\n");
		wfx_hif_set_beacon_wakeup_period(wvif, info->dtim_period, info->dtim_period);
		/* We temporary forwarded beacon for join process. It is now no more necessary. */
		wfx_filter_beacon(wvif, true);
	}

	if (changed & BSS_CHANGED_ARP_FILTER) {
		for (i = 0; i < HIF_MAX_ARP_IP_ADDRTABLE_ENTRIES; i++) {
			__be32 *arp_addr = &vif->cfg.arp_addr_list[i];

			if (vif->cfg.arp_addr_cnt > HIF_MAX_ARP_IP_ADDRTABLE_ENTRIES)
				arp_addr = NULL;
			if (i >= vif->cfg.arp_addr_cnt)
				arp_addr = NULL;
			wfx_hif_set_arp_ipv4_filter(wvif, i, arp_addr);
		}
	}

	if (changed & BSS_CHANGED_AP_PROBE_RESP || changed & BSS_CHANGED_BEACON)
		wfx_upload_ap_templates(wvif);

	if (changed & BSS_CHANGED_BEACON_ENABLED)
		wfx_enable_beacon(wvif, info->enable_beacon);

	if (changed & BSS_CHANGED_KEEP_ALIVE)
		wfx_hif_keep_alive_period(wvif,
					  info->max_idle_period * USEC_PER_TU / USEC_PER_MSEC);

	if (changed & BSS_CHANGED_ERP_CTS_PROT)
		wfx_hif_erp_use_protection(wvif, info->use_cts_prot);

	if (changed & BSS_CHANGED_ERP_SLOT)
		wfx_hif_slot_time(wvif, info->use_short_slot ? 9 : 20);

	if (changed & BSS_CHANGED_CQM)
		wfx_hif_set_rcpi_rssi_threshold(wvif, info->cqm_rssi_thold, info->cqm_rssi_hyst);

	if (changed & BSS_CHANGED_TXPOWER)
		wfx_hif_set_output_power(wvif, info->txpower);

	if (changed & BSS_CHANGED_PS)
		wfx_update_pm(wvif);

	mutex_unlock(&wdev->conf_mutex);
}

static int wfx_update_tim(struct wfx_vif *wvif)
{
	struct ieee80211_vif *vif = wvif_to_vif(wvif);
	struct sk_buff *skb;
	u16 tim_offset, tim_length;
	u8 *tim_ptr;

	skb = ieee80211_beacon_get_tim(wvif->wdev->hw, vif, &tim_offset,
				       &tim_length, 0);
	if (!skb)
		return -ENOENT;
	tim_ptr = skb->data + tim_offset;

	if (tim_offset && tim_length >= 6) {
		/* Firmware handles DTIM counter internally */
		tim_ptr[2] = 0;

		/* Set/reset aid0 bit */
		if (wfx_tx_queues_has_cab(wvif))
			tim_ptr[4] |= 1;
		else
			tim_ptr[4] &= ~1;
	}

	wfx_hif_update_ie_beacon(wvif, tim_ptr, tim_length);
	dev_kfree_skb(skb);

	return 0;
}

static void wfx_update_tim_work(struct work_struct *work)
{
	struct wfx_vif *wvif = container_of(work, struct wfx_vif, update_tim_work);

	wfx_update_tim(wvif);
}

int wfx_set_tim(struct ieee80211_hw *hw, struct ieee80211_sta *sta, bool set)
{
	struct wfx_dev *wdev = hw->priv;
	struct wfx_sta_priv *sta_dev = (struct wfx_sta_priv *)&sta->drv_priv;
	struct wfx_vif *wvif = wdev_to_wvif(wdev, sta_dev->vif_id);

	if (!wvif) {
		dev_warn(wdev->dev, "%s: received event for non-existent vif\n", __func__);
		return -EIO;
	}
	schedule_work(&wvif->update_tim_work);
	return 0;
}

void wfx_suspend_resume_mc(struct wfx_vif *wvif, enum sta_notify_cmd notify_cmd)
{
	struct wfx_vif *wvif_it;

	if (notify_cmd != STA_NOTIFY_AWAKE)
		return;

	/* Device won't be able to honor CAB if a scan is in progress on any interface. Prefer to
	 * skip this DTIM and wait for the next one.
	 */
	wvif_it = NULL;
	while ((wvif_it = wvif_iterate(wvif->wdev, wvif_it)) != NULL)
		if (mutex_is_locked(&wvif_it->scan_lock))
			return;

	if (!wfx_tx_queues_has_cab(wvif) || wvif->after_dtim_tx_allowed)
		dev_warn(wvif->wdev->dev, "incorrect sequence (%d CAB in queue)",
			 wfx_tx_queues_has_cab(wvif));
	wvif->after_dtim_tx_allowed = true;
	wfx_bh_request_tx(wvif->wdev);
}

int wfx_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		     struct ieee80211_ampdu_params *params)
{
	/* Aggregation is implemented fully in firmware */
	switch (params->action) {
	case IEEE80211_AMPDU_RX_START:
	case IEEE80211_AMPDU_RX_STOP:
		/* Just acknowledge it to enable frame re-ordering */
		return 0;
	default:
		/* Leave the firmware doing its business for tx aggregation */
		return -EOPNOTSUPP;
	}
}

int wfx_add_chanctx(struct ieee80211_hw *hw, struct ieee80211_chanctx_conf *conf)
{
	return 0;
}

void wfx_remove_chanctx(struct ieee80211_hw *hw, struct ieee80211_chanctx_conf *conf)
{
}

void wfx_change_chanctx(struct ieee80211_hw *hw, struct ieee80211_chanctx_conf *conf, u32 changed)
{
}

int wfx_assign_vif_chanctx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   struct ieee80211_bss_conf *link_conf,
			   struct ieee80211_chanctx_conf *conf)
{
	struct wfx_vif *wvif = (struct wfx_vif *)vif->drv_priv;
	struct ieee80211_channel *ch = conf->def.chan;

	WARN(wvif->channel, "channel overwrite");
	wvif->channel = ch;

	return 0;
}

void wfx_unassign_vif_chanctx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			      struct ieee80211_bss_conf *link_conf,
			      struct ieee80211_chanctx_conf *conf)
{
	struct wfx_vif *wvif = (struct wfx_vif *)vif->drv_priv;
	struct ieee80211_channel *ch = conf->def.chan;

	WARN(wvif->channel != ch, "channel mismatch");
	wvif->channel = NULL;
}

int wfx_config(struct ieee80211_hw *hw, u32 changed)
{
	return 0;
}

int wfx_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	int i;
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif = (struct wfx_vif *)vif->drv_priv;

	vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER |
			     IEEE80211_VIF_SUPPORTS_UAPSD |
			     IEEE80211_VIF_SUPPORTS_CQM_RSSI;

	mutex_lock(&wdev->conf_mutex);

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP:
		break;
	default:
		mutex_unlock(&wdev->conf_mutex);
		return -EOPNOTSUPP;
	}

	wvif->wdev = wdev;

	wvif->link_id_map = 1; /* link-id 0 is reserved for multicast */
	INIT_WORK(&wvif->update_tim_work, wfx_update_tim_work);
	INIT_DELAYED_WORK(&wvif->beacon_loss_work, wfx_beacon_loss_work);

	init_completion(&wvif->set_pm_mode_complete);
	complete(&wvif->set_pm_mode_complete);
	INIT_WORK(&wvif->tx_policy_upload_work, wfx_tx_policy_upload_work);

	mutex_init(&wvif->scan_lock);
	init_completion(&wvif->scan_complete);
	INIT_WORK(&wvif->scan_work, wfx_hw_scan_work);

	wfx_tx_queues_init(wvif);
	wfx_tx_policy_init(wvif);

	for (i = 0; i < ARRAY_SIZE(wdev->vif); i++) {
		if (!wdev->vif[i]) {
			wdev->vif[i] = vif;
			wvif->id = i;
			break;
		}
	}
	WARN(i == ARRAY_SIZE(wdev->vif), "try to instantiate more vif than supported");

	wfx_hif_set_macaddr(wvif, vif->addr);

	mutex_unlock(&wdev->conf_mutex);

	wvif = NULL;
	while ((wvif = wvif_iterate(wdev, wvif)) != NULL) {
		/* Combo mode does not support Block Acks. We can re-enable them */
		if (wvif_count(wdev) == 1)
			wfx_hif_set_block_ack_policy(wvif, 0xFF, 0xFF);
		else
			wfx_hif_set_block_ack_policy(wvif, 0x00, 0x00);
	}
	return 0;
}

void wfx_remove_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif = (struct wfx_vif *)vif->drv_priv;

	wait_for_completion_timeout(&wvif->set_pm_mode_complete, msecs_to_jiffies(300));
	wfx_tx_queues_check_empty(wvif);

	mutex_lock(&wdev->conf_mutex);
	WARN(wvif->link_id_map != 1, "corrupted state");

	wfx_hif_reset(wvif, false);
	wfx_hif_set_macaddr(wvif, NULL);
	wfx_tx_policy_init(wvif);

	cancel_delayed_work_sync(&wvif->beacon_loss_work);
	wdev->vif[wvif->id] = NULL;

	mutex_unlock(&wdev->conf_mutex);

	wvif = NULL;
	while ((wvif = wvif_iterate(wdev, wvif)) != NULL) {
		/* Combo mode does not support Block Acks. We can re-enable them */
		if (wvif_count(wdev) == 1)
			wfx_hif_set_block_ack_policy(wvif, 0xFF, 0xFF);
		else
			wfx_hif_set_block_ack_policy(wvif, 0x00, 0x00);
	}
}

int wfx_start(struct ieee80211_hw *hw)
{
	return 0;
}

void wfx_stop(struct ieee80211_hw *hw)
{
	struct wfx_dev *wdev = hw->priv;

	WARN_ON(!skb_queue_empty_lockless(&wdev->tx_pending));
}
