// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of mac80211 API.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
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
	// WFx only support 2GHz
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

static void __wfx_free_event_queue(struct list_head *list)
{
	struct wfx_hif_event *event, *tmp;

	list_for_each_entry_safe(event, tmp, list, link) {
		list_del(&event->link);
		kfree(event);
	}
}

static void wfx_free_event_queue(struct wfx_vif *wvif)
{
	LIST_HEAD(list);

	spin_lock(&wvif->event_queue_lock);
	list_splice_init(&wvif->event_queue, &list);
	spin_unlock(&wvif->event_queue_lock);

	__wfx_free_event_queue(&list);
}

void wfx_cqm_bssloss_sm(struct wfx_vif *wvif, int init, int good, int bad)
{
	int tx = 0;

	mutex_lock(&wvif->bss_loss_lock);
	cancel_work_sync(&wvif->bss_params_work);

	if (init) {
		schedule_delayed_work(&wvif->bss_loss_work, HZ);
		wvif->bss_loss_state = 0;

		if (!atomic_read(&wvif->wdev->tx_lock))
			tx = 1;
	} else if (good) {
		cancel_delayed_work_sync(&wvif->bss_loss_work);
		wvif->bss_loss_state = 0;
		schedule_work(&wvif->bss_params_work);
	} else if (bad) {
		/* FIXME Should we just keep going until we time out? */
		if (wvif->bss_loss_state < 3)
			tx = 1;
	} else {
		cancel_delayed_work_sync(&wvif->bss_loss_work);
		wvif->bss_loss_state = 0;
	}

	/* Spit out a NULL packet to our AP if necessary */
	// FIXME: call ieee80211_beacon_loss/ieee80211_connection_loss instead
	if (tx) {
		struct sk_buff *skb;
		struct ieee80211_hdr *hdr;
		struct ieee80211_tx_control control = { };

		wvif->bss_loss_state++;

		skb = ieee80211_nullfunc_get(wvif->wdev->hw, wvif->vif, false);
		if (!skb)
			goto end;
		hdr = (struct ieee80211_hdr *)skb->data;
		memset(IEEE80211_SKB_CB(skb), 0,
		       sizeof(*IEEE80211_SKB_CB(skb)));
		IEEE80211_SKB_CB(skb)->control.vif = wvif->vif;
		IEEE80211_SKB_CB(skb)->driver_rates[0].idx = 0;
		IEEE80211_SKB_CB(skb)->driver_rates[0].count = 1;
		IEEE80211_SKB_CB(skb)->driver_rates[1].idx = -1;
		rcu_read_lock(); // protect control.sta
		control.sta = ieee80211_find_sta(wvif->vif, hdr->addr1);
		wfx_tx(wvif->wdev->hw, &control, skb);
		rcu_read_unlock();
	}
end:
	mutex_unlock(&wvif->bss_loss_lock);
}

int wfx_fwd_probe_req(struct wfx_vif *wvif, bool enable)
{
	wvif->fwd_probe_req = enable;
	return hif_set_rx_filter(wvif, wvif->filter_bssid,
				 wvif->fwd_probe_req);
}

static int wfx_set_mcast_filter(struct wfx_vif *wvif,
				    struct wfx_grp_addr_table *fp)
{
	int i;

	// Temporary workaround for filters
	return hif_set_data_filtering(wvif, false, true);

	if (!fp->enable)
		return hif_set_data_filtering(wvif, false, true);

	for (i = 0; i < fp->num_addresses; i++)
		hif_set_mac_addr_condition(wvif, i, fp->address_list[i]);
	hif_set_uc_mc_bc_condition(wvif, 0,
				   HIF_FILTER_UNICAST | HIF_FILTER_BROADCAST);
	hif_set_config_data_filter(wvif, true, 0, BIT(1),
				   BIT(fp->num_addresses) - 1);
	hif_set_data_filtering(wvif, true, true);

	return 0;
}

void wfx_update_filtering(struct wfx_vif *wvif)
{
	int ret;
	int bf_enable;
	int bf_count;
	int n_filter_ies;
	struct hif_ie_table_entry filter_ies[] = {
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
		}
	};

	if (wvif->state == WFX_STATE_PASSIVE)
		return;

	if (wvif->disable_beacon_filter) {
		bf_enable = 0;
		bf_count = 1;
		n_filter_ies = 0;
	} else if (wvif->vif->type != NL80211_IFTYPE_STATION) {
		bf_enable = HIF_BEACON_FILTER_ENABLE | HIF_BEACON_FILTER_AUTO_ERP;
		bf_count = 0;
		n_filter_ies = 2;
	} else {
		bf_enable = HIF_BEACON_FILTER_ENABLE;
		bf_count = 0;
		n_filter_ies = 3;
	}

	ret = hif_set_rx_filter(wvif, wvif->filter_bssid, wvif->fwd_probe_req);
	if (!ret)
		ret = hif_set_beacon_filter_table(wvif, n_filter_ies, filter_ies);
	if (!ret)
		ret = hif_beacon_filter_control(wvif, bf_enable, bf_count);
	if (!ret)
		ret = wfx_set_mcast_filter(wvif, &wvif->mcast_filter);
	if (ret)
		dev_err(wvif->wdev->dev, "update filtering failed: %d\n", ret);
}

static void wfx_update_filtering_work(struct work_struct *work)
{
	struct wfx_vif *wvif = container_of(work, struct wfx_vif,
					    update_filtering_work);

	wfx_update_filtering(wvif);
}

u64 wfx_prepare_multicast(struct ieee80211_hw *hw,
			  struct netdev_hw_addr_list *mc_list)
{
	int i;
	struct netdev_hw_addr *ha;
	struct wfx_vif *wvif = NULL;
	struct wfx_dev *wdev = hw->priv;
	int count = netdev_hw_addr_list_count(mc_list);

	while ((wvif = wvif_iterate(wdev, wvif)) != NULL) {
		memset(&wvif->mcast_filter, 0x00, sizeof(wvif->mcast_filter));
		if (!count ||
		    count > ARRAY_SIZE(wvif->mcast_filter.address_list))
			continue;

		i = 0;
		netdev_hw_addr_list_for_each(ha, mc_list) {
			ether_addr_copy(wvif->mcast_filter.address_list[i],
					ha->addr);
			i++;
		}
		wvif->mcast_filter.enable = true;
		wvif->mcast_filter.num_addresses = count;
	}

	return 0;
}

void wfx_configure_filter(struct ieee80211_hw *hw,
			     unsigned int changed_flags,
			     unsigned int *total_flags,
			     u64 unused)
{
	struct wfx_vif *wvif = NULL;
	struct wfx_dev *wdev = hw->priv;

	*total_flags &= FIF_OTHER_BSS | FIF_FCSFAIL | FIF_PROBE_REQ;

	while ((wvif = wvif_iterate(wdev, wvif)) != NULL) {
		mutex_lock(&wvif->scan_lock);
		wvif->filter_bssid = (*total_flags &
				      (FIF_OTHER_BSS | FIF_PROBE_REQ)) ? 0 : 1;
		wvif->disable_beacon_filter = !(*total_flags & FIF_PROBE_REQ);
		wfx_fwd_probe_req(wvif, true);
		wfx_update_filtering(wvif);
		mutex_unlock(&wvif->scan_lock);
	}
}

static int wfx_update_pm(struct wfx_vif *wvif)
{
	struct ieee80211_conf *conf = &wvif->wdev->hw->conf;
	bool ps = conf->flags & IEEE80211_CONF_PS;
	int ps_timeout = conf->dynamic_ps_timeout;
	struct ieee80211_channel *chan0 = NULL, *chan1 = NULL;

	WARN_ON(conf->dynamic_ps_timeout < 0);
	if (wvif->state != WFX_STATE_STA || !wvif->bss_params.aid)
		return 0;
	if (!ps)
		ps_timeout = 0;
	if (wvif->uapsd_mask)
		ps_timeout = 0;

	// Kernel disable powersave when an AP is in use. In contrary, it is
	// absolutely necessary to enable legacy powersave for WF200 if channels
	// are differents.
	if (wdev_to_wvif(wvif->wdev, 0))
		chan0 = wdev_to_wvif(wvif->wdev, 0)->vif->bss_conf.chandef.chan;
	if (wdev_to_wvif(wvif->wdev, 1))
		chan1 = wdev_to_wvif(wvif->wdev, 1)->vif->bss_conf.chandef.chan;
	if (chan0 && chan1 && chan0->hw_value != chan1->hw_value &&
	    wvif->vif->type != NL80211_IFTYPE_AP) {
		ps = true;
		ps_timeout = 0;
	}

	if (!wait_for_completion_timeout(&wvif->set_pm_mode_complete,
					 TU_TO_JIFFIES(512)))
		dev_warn(wvif->wdev->dev,
			 "timeout while waiting of set_pm_mode_complete\n");
	return hif_set_pm(wvif, ps, ps_timeout);
}

int wfx_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   u16 queue, const struct ieee80211_tx_queue_params *params)
{
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;
	int old_uapsd = wvif->uapsd_mask;

	WARN_ON(queue >= hw->queues);

	mutex_lock(&wdev->conf_mutex);
	assign_bit(queue, &wvif->uapsd_mask, params->uapsd);
	memcpy(&wvif->edca_params[queue], params, sizeof(*params));
	hif_set_edca_queue_params(wvif, queue, params);
	if (wvif->vif->type == NL80211_IFTYPE_STATION &&
	    old_uapsd != wvif->uapsd_mask) {
		hif_set_uapsd_info(wvif, wvif->uapsd_mask);
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
		hif_rts_threshold(wvif, value);
	return 0;
}

static int __wfx_flush(struct wfx_dev *wdev, bool drop)
{
	for (;;) {
		if (drop)
			wfx_tx_queues_clear(wdev);
		if (wait_event_timeout(wdev->tx_queue_stats.wait_link_id_empty,
				       wfx_tx_queues_empty(wdev),
				       2 * HZ) <= 0)
			return -ETIMEDOUT;
		wfx_tx_flush(wdev);
		if (wfx_tx_queues_empty(wdev))
			return 0;
		dev_warn(wdev->dev, "frames queued while flushing tx queues");
	}
}

void wfx_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  u32 queues, bool drop)
{
	// FIXME: only flush requested vif and queues
	__wfx_flush(hw->priv, drop);
}

/* WSM callbacks */

static void wfx_event_report_rssi(struct wfx_vif *wvif, u8 raw_rcpi_rssi)
{
	/* RSSI: signed Q8.0, RCPI: unsigned Q7.1
	 * RSSI = RCPI / 2 - 110
	 */
	int rcpi_rssi;
	int cqm_evt;

	rcpi_rssi = raw_rcpi_rssi / 2 - 110;
	if (rcpi_rssi <= wvif->vif->bss_conf.cqm_rssi_thold)
		cqm_evt = NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW;
	else
		cqm_evt = NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH;
	ieee80211_cqm_rssi_notify(wvif->vif, cqm_evt, rcpi_rssi, GFP_KERNEL);
}

static void wfx_event_handler_work(struct work_struct *work)
{
	struct wfx_vif *wvif =
		container_of(work, struct wfx_vif, event_handler_work);
	struct wfx_hif_event *event;

	LIST_HEAD(list);

	spin_lock(&wvif->event_queue_lock);
	list_splice_init(&wvif->event_queue, &list);
	spin_unlock(&wvif->event_queue_lock);

	list_for_each_entry(event, &list, link) {
		switch (event->evt.event_id) {
		case HIF_EVENT_IND_BSSLOST:
			cancel_work_sync(&wvif->unjoin_work);
			mutex_lock(&wvif->scan_lock);
			wfx_cqm_bssloss_sm(wvif, 1, 0, 0);
			mutex_unlock(&wvif->scan_lock);
			break;
		case HIF_EVENT_IND_BSSREGAINED:
			wfx_cqm_bssloss_sm(wvif, 0, 0, 0);
			cancel_work_sync(&wvif->unjoin_work);
			break;
		case HIF_EVENT_IND_RCPI_RSSI:
			wfx_event_report_rssi(wvif,
					      event->evt.event_data.rcpi_rssi);
			break;
		case HIF_EVENT_IND_PS_MODE_ERROR:
			dev_warn(wvif->wdev->dev,
				 "error while processing power save request\n");
			break;
		default:
			dev_warn(wvif->wdev->dev,
				 "unhandled event indication: %.2x\n",
				 event->evt.event_id);
			break;
		}
	}
	__wfx_free_event_queue(&list);
}

static void wfx_bss_loss_work(struct work_struct *work)
{
	struct wfx_vif *wvif = container_of(work, struct wfx_vif,
					    bss_loss_work.work);

	ieee80211_connection_loss(wvif->vif);
}

static void wfx_bss_params_work(struct work_struct *work)
{
	struct wfx_vif *wvif = container_of(work, struct wfx_vif,
					    bss_params_work);

	mutex_lock(&wvif->wdev->conf_mutex);
	wvif->bss_params.bss_flags.lost_count_only = 1;
	hif_set_bss_params(wvif, &wvif->bss_params);
	wvif->bss_params.bss_flags.lost_count_only = 0;
	mutex_unlock(&wvif->wdev->conf_mutex);
}

static void wfx_do_unjoin(struct wfx_vif *wvif)
{
	mutex_lock(&wvif->wdev->conf_mutex);

	if (!wvif->state)
		goto done;

	if (wvif->state == WFX_STATE_AP)
		goto done;

	cancel_work_sync(&wvif->update_filtering_work);
	wvif->state = WFX_STATE_PASSIVE;

	/* Unjoin is a reset. */
	wfx_tx_flush(wvif->wdev);
	hif_keep_alive_period(wvif, 0);
	hif_reset(wvif, false);
	wfx_tx_policy_init(wvif);
	hif_set_macaddr(wvif, wvif->vif->addr);
	wfx_free_event_queue(wvif);
	cancel_work_sync(&wvif->event_handler_work);
	wfx_cqm_bssloss_sm(wvif, 0, 0, 0);

	/* Disable Block ACKs */
	hif_set_block_ack_policy(wvif, 0, 0);

	wvif->disable_beacon_filter = false;
	wfx_update_filtering(wvif);
	memset(&wvif->bss_params, 0, sizeof(wvif->bss_params));

done:
	mutex_unlock(&wvif->wdev->conf_mutex);
}

static void wfx_set_mfp(struct wfx_vif *wvif,
			struct cfg80211_bss *bss)
{
	const int pairwise_cipher_suite_count_offset = 8 / sizeof(u16);
	const int pairwise_cipher_suite_size = 4 / sizeof(u16);
	const int akm_suite_size = 4 / sizeof(u16);
	const u16 *ptr = NULL;
	bool mfpc = false;
	bool mfpr = false;

	/* 802.11w protected mgmt frames */

	/* retrieve MFPC and MFPR flags from beacon or PBRSP */

	rcu_read_lock();
	if (bss)
		ptr = (const u16 *) ieee80211_bss_get_ie(bss,
							      WLAN_EID_RSN);

	if (ptr) {
		ptr += pairwise_cipher_suite_count_offset;
		ptr += 1 + pairwise_cipher_suite_size * *ptr;
		ptr += 1 + akm_suite_size * *ptr;
		mfpr = *ptr & BIT(6);
		mfpc = *ptr & BIT(7);
	}
	rcu_read_unlock();

	hif_set_mfp(wvif, mfpc, mfpr);
}

static void wfx_do_join(struct wfx_vif *wvif)
{
	int ret;
	struct ieee80211_bss_conf *conf = &wvif->vif->bss_conf;
	struct cfg80211_bss *bss = NULL;
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	const u8 *ssidie = NULL;
	int ssidlen = 0;

	wfx_tx_lock_flush(wvif->wdev);

	if (wvif->state)
		wfx_do_unjoin(wvif);

	bss = cfg80211_get_bss(wvif->wdev->hw->wiphy, wvif->channel,
			       conf->bssid, NULL, 0,
			       IEEE80211_BSS_TYPE_ANY, IEEE80211_PRIVACY_ANY);
	if (!bss && !conf->ibss_joined) {
		wfx_tx_unlock(wvif->wdev);
		return;
	}

	mutex_lock(&wvif->wdev->conf_mutex);

	/* Sanity check beacon interval */
	if (!wvif->beacon_int)
		wvif->beacon_int = 1;

	rcu_read_lock(); // protect ssidie
	if (!conf->ibss_joined)
		ssidie = ieee80211_bss_get_ie(bss, WLAN_EID_SSID);
	if (ssidie) {
		ssidlen = ssidie[1];
		memcpy(ssid, &ssidie[2], ssidie[1]);
	}
	rcu_read_unlock();

	wfx_tx_flush(wvif->wdev);

	if (wvif_count(wvif->wdev) <= 1)
		hif_set_block_ack_policy(wvif, 0xFF, 0xFF);

	wfx_set_mfp(wvif, bss);

	ret = hif_join(wvif, conf, wvif->channel, ssid, ssidlen);
	if (ret) {
		ieee80211_connection_loss(wvif->vif);
		wvif->join_complete_status = -1;
		/* Tx lock still held, unjoin will clear it. */
		if (!schedule_work(&wvif->unjoin_work))
			wfx_tx_unlock(wvif->wdev);
	} else {
		wvif->join_complete_status = 0;
		if (wvif->vif->type == NL80211_IFTYPE_ADHOC)
			wvif->state = WFX_STATE_IBSS;
		else
			wvif->state = WFX_STATE_PRE_STA;
		wfx_tx_unlock(wvif->wdev);

		/* Upload keys */
		wfx_upload_keys(wvif);

		/* Due to beacon filtering it is possible that the
		 * AP's beacon is not known for the mac80211 stack.
		 * Disable filtering temporary to make sure the stack
		 * receives at least one
		 */
		wvif->disable_beacon_filter = true;
	}
	wfx_update_filtering(wvif);

	mutex_unlock(&wvif->wdev->conf_mutex);
	if (bss)
		cfg80211_put_bss(wvif->wdev->hw->wiphy, bss);
}

static void wfx_unjoin_work(struct work_struct *work)
{
	struct wfx_vif *wvif = container_of(work, struct wfx_vif, unjoin_work);

	wfx_do_unjoin(wvif);
	wfx_tx_unlock(wvif->wdev);
}

int wfx_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		struct ieee80211_sta *sta)
{
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;
	struct wfx_sta_priv *sta_priv = (struct wfx_sta_priv *) &sta->drv_priv;

	spin_lock_init(&sta_priv->lock);
	sta_priv->vif_id = wvif->id;

	// FIXME: in station mode, the current API interprets new link-id as a
	// tdls peer.
	if (vif->type == NL80211_IFTYPE_STATION)
		return 0;
	sta_priv->link_id = ffz(wvif->link_id_map);
	wvif->link_id_map |= BIT(sta_priv->link_id);
	WARN_ON(!sta_priv->link_id);
	WARN_ON(sta_priv->link_id >= WFX_MAX_STA_IN_AP_MODE);
	hif_map_link(wvif, sta->addr, 0, sta_priv->link_id);

	spin_lock_bh(&wvif->ps_state_lock);
	if ((sta->uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_MASK) ==
					IEEE80211_WMM_IE_STA_QOSINFO_AC_MASK)
		wvif->sta_asleep_mask |= BIT(sta_priv->link_id);
	spin_unlock_bh(&wvif->ps_state_lock);
	return 0;
}

int wfx_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta)
{
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;
	struct wfx_sta_priv *sta_priv = (struct wfx_sta_priv *) &sta->drv_priv;
	int i;

	for (i = 0; i < ARRAY_SIZE(sta_priv->buffered); i++)
		if (sta_priv->buffered[i])
			dev_warn(wvif->wdev->dev, "release station while %d pending frame on queue %d",
				 sta_priv->buffered[i], i);
	// FIXME: see note in wfx_sta_add()
	if (vif->type == NL80211_IFTYPE_STATION)
		return 0;
	// FIXME add a mutex?
	hif_map_link(wvif, sta->addr, 1, sta_priv->link_id);
	wvif->link_id_map &= ~BIT(sta_priv->link_id);
	return 0;
}

static int wfx_start_ap(struct wfx_vif *wvif)
{
	int ret;

	wvif->beacon_int = wvif->vif->bss_conf.beacon_int;
	ret = hif_start(wvif, &wvif->vif->bss_conf, wvif->channel);
	if (ret)
		return ret;
	ret = wfx_upload_keys(wvif);
	if (ret)
		return ret;
	if (wvif_count(wvif->wdev) <= 1)
		hif_set_block_ack_policy(wvif, 0xFF, 0xFF);
	wvif->state = WFX_STATE_AP;
	wfx_update_filtering(wvif);
	return 0;
}

static int wfx_update_beaconing(struct wfx_vif *wvif)
{
	if (wvif->vif->type != NL80211_IFTYPE_AP)
		return 0;
	if (wvif->state == WFX_STATE_AP &&
	    wvif->beacon_int == wvif->vif->bss_conf.beacon_int)
		return 0;
	wfx_tx_lock_flush(wvif->wdev);
	hif_reset(wvif, false);
	wfx_tx_policy_init(wvif);
	wvif->state = WFX_STATE_PASSIVE;
	wfx_start_ap(wvif);
	wfx_tx_unlock(wvif->wdev);
	return 0;
}

static int wfx_upload_ap_templates(struct wfx_vif *wvif)
{
	struct sk_buff *skb;

	if (wvif->vif->type == NL80211_IFTYPE_STATION ||
	    wvif->vif->type == NL80211_IFTYPE_MONITOR ||
	    wvif->vif->type == NL80211_IFTYPE_UNSPECIFIED)
		return 0;

	skb = ieee80211_beacon_get(wvif->wdev->hw, wvif->vif);
	if (!skb)
		return -ENOMEM;
	hif_set_template_frame(wvif, skb, HIF_TMPLT_BCN,
			       API_RATE_INDEX_B_1MBPS);
	dev_kfree_skb(skb);

	skb = ieee80211_proberesp_get(wvif->wdev->hw, wvif->vif);
	if (!skb)
		return -ENOMEM;
	hif_set_template_frame(wvif, skb, HIF_TMPLT_PRBRES,
			       API_RATE_INDEX_B_1MBPS);
	dev_kfree_skb(skb);
	return 0;
}

static void wfx_join_finalize(struct wfx_vif *wvif,
			      struct ieee80211_bss_conf *info)
{
	struct ieee80211_sta *sta = NULL;

	wvif->beacon_int = info->beacon_int;
	rcu_read_lock(); // protect sta
	if (info->bssid && !info->ibss_joined)
		sta = ieee80211_find_sta(wvif->vif, info->bssid);
	if (sta)
		wvif->bss_params.operational_rate_set =
			wfx_rate_mask_to_hw(wvif->wdev, sta->supp_rates[wvif->channel->band]);
	else
		wvif->bss_params.operational_rate_set = -1;
	rcu_read_unlock();
	if (sta &&
	    info->ht_operation_mode & IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT)
		hif_dual_cts_protection(wvif, true);
	else
		hif_dual_cts_protection(wvif, false);

	wfx_cqm_bssloss_sm(wvif, 0, 0, 0);
	cancel_work_sync(&wvif->unjoin_work);

	wvif->bss_params.beacon_lost_count = 20;
	wvif->bss_params.aid = info->aid;

	hif_set_association_mode(wvif, info);

	if (!info->ibss_joined) {
		hif_keep_alive_period(wvif, 30 /* sec */);
		hif_set_bss_params(wvif, &wvif->bss_params);
		hif_set_beacon_wakeup_period(wvif, info->dtim_period,
					     info->dtim_period);
		wfx_update_pm(wvif);
	}
}

void wfx_bss_info_changed(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *info,
			     u32 changed)
{
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;
	bool do_join = false;
	int i;

	mutex_lock(&wdev->conf_mutex);

	/* TODO: BSS_CHANGED_QOS */
	if (changed & BSS_CHANGED_ARP_FILTER) {
		for (i = 0; i < HIF_MAX_ARP_IP_ADDRTABLE_ENTRIES; i++) {
			__be32 *arp_addr = &info->arp_addr_list[i];

			if (info->arp_addr_cnt > HIF_MAX_ARP_IP_ADDRTABLE_ENTRIES)
				arp_addr = NULL;
			if (i >= info->arp_addr_cnt)
				arp_addr = NULL;
			hif_set_arp_ipv4_filter(wvif, i, arp_addr);
		}
	}

	if (changed & BSS_CHANGED_BEACON ||
	    changed & BSS_CHANGED_AP_PROBE_RESP ||
	    changed & BSS_CHANGED_BSSID ||
	    changed & BSS_CHANGED_SSID ||
	    changed & BSS_CHANGED_IBSS) {
		wvif->beacon_int = info->beacon_int;
		wfx_update_beaconing(wvif);
		wfx_upload_ap_templates(wvif);
		wfx_fwd_probe_req(wvif, false);
	}

	if (changed & BSS_CHANGED_BEACON_ENABLED &&
	    wvif->state != WFX_STATE_IBSS)
		hif_beacon_transmit(wvif, info->enable_beacon);

	if (changed & BSS_CHANGED_BEACON_INFO)
		hif_set_beacon_wakeup_period(wvif, info->dtim_period,
					     info->dtim_period);

	/* assoc/disassoc, or maybe AID changed */
	if (changed & BSS_CHANGED_ASSOC) {
		wfx_tx_lock_flush(wdev);
		wvif->wep_default_key_id = -1;
		wfx_tx_unlock(wdev);
	}

	if (changed & BSS_CHANGED_ASSOC && !info->assoc &&
	    (wvif->state == WFX_STATE_STA || wvif->state == WFX_STATE_IBSS)) {
		/* Shedule unjoin work */
		wfx_tx_lock(wdev);
		if (!schedule_work(&wvif->unjoin_work))
			wfx_tx_unlock(wdev);
	} else {
		if (changed & BSS_CHANGED_BEACON_INT) {
			if (info->ibss_joined)
				do_join = true;
			else if (wvif->state == WFX_STATE_AP)
				wfx_update_beaconing(wvif);
		}

		if (changed & BSS_CHANGED_BSSID)
			do_join = true;

		if (changed & BSS_CHANGED_ASSOC ||
		    changed & BSS_CHANGED_BSSID ||
		    changed & BSS_CHANGED_IBSS ||
		    changed & BSS_CHANGED_BASIC_RATES ||
		    changed & BSS_CHANGED_HT) {
			if (info->assoc) {
				if (wvif->state < WFX_STATE_PRE_STA) {
					ieee80211_connection_loss(vif);
					mutex_unlock(&wdev->conf_mutex);
					return;
				} else if (wvif->state == WFX_STATE_PRE_STA) {
					wvif->state = WFX_STATE_STA;
				}
			} else {
				do_join = true;
			}

			if (info->assoc || info->ibss_joined)
				wfx_join_finalize(wvif, info);
			else
				memset(&wvif->bss_params, 0,
				       sizeof(wvif->bss_params));
		}
	}

	if (changed & BSS_CHANGED_ASSOC ||
	    changed & BSS_CHANGED_ERP_CTS_PROT ||
	    changed & BSS_CHANGED_ERP_PREAMBLE) {
		u8 erp_ie[3] = { WLAN_EID_ERP_INFO, 1, 0 };

		hif_erp_use_protection(wvif, info->use_cts_prot);
		if (info->use_cts_prot)
			erp_ie[2] |= WLAN_ERP_USE_PROTECTION;
		if (info->use_short_preamble)
			erp_ie[2] |= WLAN_ERP_BARKER_PREAMBLE;
		if (wvif->vif->type != NL80211_IFTYPE_STATION)
			hif_update_ie_beacon(wvif, erp_ie, sizeof(erp_ie));
	}

	if (changed & BSS_CHANGED_ASSOC || changed & BSS_CHANGED_ERP_SLOT)
		hif_slot_time(wvif, info->use_short_slot ? 9 : 20);

	if (changed & BSS_CHANGED_ASSOC || changed & BSS_CHANGED_CQM)
		hif_set_rcpi_rssi_threshold(wvif, info->cqm_rssi_thold,
					    info->cqm_rssi_hyst);

	if (changed & BSS_CHANGED_TXPOWER)
		hif_set_output_power(wvif, info->txpower);

	if (changed & BSS_CHANGED_PS)
		wfx_update_pm(wvif);

	mutex_unlock(&wdev->conf_mutex);

	if (do_join)
		wfx_do_join(wvif);
}

static void wfx_ps_notify_sta(struct wfx_vif *wvif,
			      enum sta_notify_cmd notify_cmd, int link_id)
{
	spin_lock_bh(&wvif->ps_state_lock);
	if (notify_cmd == STA_NOTIFY_SLEEP)
		wvif->sta_asleep_mask |= BIT(link_id);
	else // notify_cmd == STA_NOTIFY_AWAKE
		wvif->sta_asleep_mask &= ~BIT(link_id);
	spin_unlock_bh(&wvif->ps_state_lock);
	if (notify_cmd == STA_NOTIFY_AWAKE)
		wfx_bh_request_tx(wvif->wdev);
}

void wfx_sta_notify(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    enum sta_notify_cmd notify_cmd, struct ieee80211_sta *sta)
{
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;
	struct wfx_sta_priv *sta_priv = (struct wfx_sta_priv *) &sta->drv_priv;

	wfx_ps_notify_sta(wvif, notify_cmd, sta_priv->link_id);
}

static int wfx_update_tim(struct wfx_vif *wvif)
{
	struct sk_buff *skb;
	u16 tim_offset, tim_length;
	u8 *tim_ptr;

	skb = ieee80211_beacon_get_tim(wvif->wdev->hw, wvif->vif,
				       &tim_offset, &tim_length);
	if (!skb) {
		__wfx_flush(wvif->wdev, true);
		return -ENOENT;
	}
	tim_ptr = skb->data + tim_offset;

	if (tim_offset && tim_length >= 6) {
		/* Ignore DTIM count from mac80211:
		 * firmware handles DTIM internally.
		 */
		tim_ptr[2] = 0;

		/* Set/reset aid0 bit */
		if (wfx_tx_queues_get_after_dtim(wvif))
			tim_ptr[4] |= 1;
		else
			tim_ptr[4] &= ~1;
	}

	hif_update_ie_beacon(wvif, tim_ptr, tim_length);
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

	schedule_work(&wvif->update_tim_work);
	return 0;
}

void wfx_suspend_resume_mc(struct wfx_vif *wvif, enum sta_notify_cmd notify_cmd)
{
	WARN(!wfx_tx_queues_get_after_dtim(wvif), "incorrect sequence");
	WARN(wvif->after_dtim_tx_allowed, "incorrect sequence");
	wvif->after_dtim_tx_allowed = true;
	wfx_bh_request_tx(wvif->wdev);
}

int wfx_ampdu_action(struct ieee80211_hw *hw,
		     struct ieee80211_vif *vif,
		     struct ieee80211_ampdu_params *params)
{
	/* Aggregation is implemented fully in firmware,
	 * including block ack negotiation. Do not allow
	 * mac80211 stack to do anything: it interferes with
	 * the firmware.
	 */

	/* Note that we still need this function stubbed. */

	return -ENOTSUPP;
}

int wfx_add_chanctx(struct ieee80211_hw *hw,
		    struct ieee80211_chanctx_conf *conf)
{
	return 0;
}

void wfx_remove_chanctx(struct ieee80211_hw *hw,
			struct ieee80211_chanctx_conf *conf)
{
}

void wfx_change_chanctx(struct ieee80211_hw *hw,
			struct ieee80211_chanctx_conf *conf,
			u32 changed)
{
}

int wfx_assign_vif_chanctx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   struct ieee80211_chanctx_conf *conf)
{
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;
	struct ieee80211_channel *ch = conf->def.chan;

	WARN(wvif->channel, "channel overwrite");
	wvif->channel = ch;

	return 0;
}

void wfx_unassign_vif_chanctx(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_chanctx_conf *conf)
{
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;
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
	int i, ret = 0;
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;

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

	for (i = 0; i < ARRAY_SIZE(wdev->vif); i++) {
		if (!wdev->vif[i]) {
			wdev->vif[i] = vif;
			wvif->id = i;
			break;
		}
	}
	if (i == ARRAY_SIZE(wdev->vif)) {
		mutex_unlock(&wdev->conf_mutex);
		return -EOPNOTSUPP;
	}
	// FIXME: prefer use of container_of() to get vif
	wvif->vif = vif;
	wvif->wdev = wdev;

	wvif->link_id_map = 1; // link-id 0 is reserved for multicast
	spin_lock_init(&wvif->ps_state_lock);
	INIT_WORK(&wvif->update_tim_work, wfx_update_tim_work);

	memset(&wvif->bss_params, 0, sizeof(wvif->bss_params));

	mutex_init(&wvif->bss_loss_lock);
	INIT_DELAYED_WORK(&wvif->bss_loss_work, wfx_bss_loss_work);

	wvif->wep_default_key_id = -1;
	INIT_WORK(&wvif->wep_key_work, wfx_wep_key_work);

	spin_lock_init(&wvif->event_queue_lock);
	INIT_LIST_HEAD(&wvif->event_queue);
	INIT_WORK(&wvif->event_handler_work, wfx_event_handler_work);

	init_completion(&wvif->set_pm_mode_complete);
	complete(&wvif->set_pm_mode_complete);
	INIT_WORK(&wvif->update_filtering_work, wfx_update_filtering_work);
	INIT_WORK(&wvif->bss_params_work, wfx_bss_params_work);
	INIT_WORK(&wvif->unjoin_work, wfx_unjoin_work);
	INIT_WORK(&wvif->tx_policy_upload_work, wfx_tx_policy_upload_work);

	mutex_init(&wvif->scan_lock);
	init_completion(&wvif->scan_complete);
	INIT_WORK(&wvif->scan_work, wfx_hw_scan_work);

	INIT_WORK(&wvif->tx_policy_upload_work, wfx_tx_policy_upload_work);
	mutex_unlock(&wdev->conf_mutex);

	hif_set_macaddr(wvif, vif->addr);

	wfx_tx_policy_init(wvif);
	wvif = NULL;
	while ((wvif = wvif_iterate(wdev, wvif)) != NULL) {
		// Combo mode does not support Block Acks. We can re-enable them
		if (wvif_count(wdev) == 1)
			hif_set_block_ack_policy(wvif, 0xFF, 0xFF);
		else
			hif_set_block_ack_policy(wvif, 0x00, 0x00);
		// Combo force powersave mode. We can re-enable it now
		ret = wfx_update_pm(wvif);
	}
	return ret;
}

void wfx_remove_interface(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif)
{
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;

	wait_for_completion_timeout(&wvif->set_pm_mode_complete, msecs_to_jiffies(300));

	mutex_lock(&wdev->conf_mutex);
	WARN(wvif->link_id_map != 1, "corrupted state");
	switch (wvif->state) {
	case WFX_STATE_PRE_STA:
	case WFX_STATE_STA:
	case WFX_STATE_IBSS:
		wfx_tx_lock_flush(wdev);
		if (!schedule_work(&wvif->unjoin_work))
			wfx_tx_unlock(wdev);
		break;
	case WFX_STATE_AP:
		wvif->sta_asleep_mask = 0;
		/* reset.link_id = 0; */
		hif_reset(wvif, false);
		break;
	default:
		break;
	}

	wvif->state = WFX_STATE_PASSIVE;
	wfx_tx_queues_wait_empty_vif(wvif);
	wfx_tx_unlock(wdev);

	/* FIXME: In add to reset MAC address, try to reset interface */
	hif_set_macaddr(wvif, NULL);

	wfx_cqm_bssloss_sm(wvif, 0, 0, 0);
	cancel_work_sync(&wvif->unjoin_work);
	wfx_free_event_queue(wvif);

	wdev->vif[wvif->id] = NULL;
	wvif->vif = NULL;

	mutex_unlock(&wdev->conf_mutex);
	wvif = NULL;
	while ((wvif = wvif_iterate(wdev, wvif)) != NULL) {
		// Combo mode does not support Block Acks. We can re-enable them
		if (wvif_count(wdev) == 1)
			hif_set_block_ack_policy(wvif, 0xFF, 0xFF);
		else
			hif_set_block_ack_policy(wvif, 0x00, 0x00);
		// Combo force powersave mode. We can re-enable it now
		wfx_update_pm(wvif);
	}
}

int wfx_start(struct ieee80211_hw *hw)
{
	return 0;
}

void wfx_stop(struct ieee80211_hw *hw)
{
	struct wfx_dev *wdev = hw->priv;

	wfx_tx_lock_flush(wdev);
	mutex_lock(&wdev->conf_mutex);
	wfx_tx_queues_clear(wdev);
	mutex_unlock(&wdev->conf_mutex);
	wfx_tx_unlock(wdev);
	WARN(atomic_read(&wdev->tx_lock), "tx_lock is locked");
}
