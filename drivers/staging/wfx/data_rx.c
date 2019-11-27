// SPDX-License-Identifier: GPL-2.0-only
/*
 * Datapath implementation.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <linux/etherdevice.h>
#include <net/mac80211.h>

#include "data_rx.h"
#include "wfx.h"
#include "bh.h"
#include "sta.h"

static int wfx_handle_pspoll(struct wfx_vif *wvif, struct sk_buff *skb)
{
	struct ieee80211_sta *sta;
	struct ieee80211_pspoll *pspoll = (struct ieee80211_pspoll *)skb->data;
	int link_id = 0;
	u32 pspoll_mask = 0;
	int i;

	if (wvif->state != WFX_STATE_AP)
		return 1;
	if (!ether_addr_equal(wvif->vif->addr, pspoll->bssid))
		return 1;

	rcu_read_lock();
	sta = ieee80211_find_sta(wvif->vif, pspoll->ta);
	if (sta)
		link_id = ((struct wfx_sta_priv *)&sta->drv_priv)->link_id;
	rcu_read_unlock();
	if (link_id)
		pspoll_mask = BIT(link_id);
	else
		return 1;

	wvif->pspoll_mask |= pspoll_mask;
	/* Do not report pspols if data for given link id is queued already. */
	for (i = 0; i < IEEE80211_NUM_ACS; ++i) {
		if (wfx_tx_queue_get_num_queued(&wvif->wdev->tx_queue[i],
						pspoll_mask)) {
			wfx_bh_request_tx(wvif->wdev);
			return 1;
		}
	}
	return 0;
}

static int wfx_drop_encrypt_data(struct wfx_dev *wdev, struct hif_ind_rx *arg, struct sk_buff *skb)
{
	struct ieee80211_hdr *frame = (struct ieee80211_hdr *) skb->data;
	size_t hdrlen = ieee80211_hdrlen(frame->frame_control);
	size_t iv_len, icv_len;

	/* Oops... There is no fast way to ask mac80211 about
	 * IV/ICV lengths. Even defineas are not exposed.
	 */
	switch (arg->rx_flags.encryp) {
	case HIF_RI_FLAGS_WEP_ENCRYPTED:
		iv_len = 4 /* WEP_IV_LEN */;
		icv_len = 4 /* WEP_ICV_LEN */;
		break;
	case HIF_RI_FLAGS_TKIP_ENCRYPTED:
		iv_len = 8 /* TKIP_IV_LEN */;
		icv_len = 4 /* TKIP_ICV_LEN */
			+ 8 /*MICHAEL_MIC_LEN*/;
		break;
	case HIF_RI_FLAGS_AES_ENCRYPTED:
		iv_len = 8 /* CCMP_HDR_LEN */;
		icv_len = 8 /* CCMP_MIC_LEN */;
		break;
	case HIF_RI_FLAGS_WAPI_ENCRYPTED:
		iv_len = 18 /* WAPI_HDR_LEN */;
		icv_len = 16 /* WAPI_MIC_LEN */;
		break;
	default:
		dev_err(wdev->dev, "unknown encryption type %d\n",
			arg->rx_flags.encryp);
		return -EIO;
	}

	/* Firmware strips ICV in case of MIC failure. */
	if (arg->status == HIF_STATUS_MICFAILURE)
		icv_len = 0;

	if (skb->len < hdrlen + iv_len + icv_len) {
		dev_warn(wdev->dev, "malformed SDU received\n");
		return -EIO;
	}

	/* Remove IV, ICV and MIC */
	skb_trim(skb, skb->len - icv_len);
	memmove(skb->data + iv_len, skb->data, hdrlen);
	skb_pull(skb, iv_len);
	return 0;

}

void wfx_rx_cb(struct wfx_vif *wvif, struct hif_ind_rx *arg,
	       struct sk_buff *skb)
{
	int link_id = arg->rx_flags.peer_sta_id;
	struct ieee80211_rx_status *hdr = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_hdr *frame = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
	struct wfx_link_entry *entry = NULL;
	bool early_data = false;

	memset(hdr, 0, sizeof(*hdr));

	// FIXME: Why do we drop these frames?
	if (!arg->rcpi_rssi &&
	    (ieee80211_is_probe_resp(frame->frame_control) ||
	     ieee80211_is_beacon(frame->frame_control)))
		goto drop;

	if (link_id && link_id <= WFX_MAX_STA_IN_AP_MODE) {
		entry = &wvif->link_id_db[link_id - 1];
		entry->timestamp = jiffies;
		if (entry->status == WFX_LINK_SOFT &&
		    ieee80211_is_data(frame->frame_control))
			early_data = true;
	}

	if (arg->status == HIF_STATUS_MICFAILURE)
		hdr->flag |= RX_FLAG_MMIC_ERROR;
	else if (arg->status)
		goto drop;

	if (skb->len < sizeof(struct ieee80211_pspoll)) {
		dev_warn(wvif->wdev->dev, "malformed SDU received\n");
		goto drop;
	}

	if (ieee80211_is_pspoll(frame->frame_control))
		if (wfx_handle_pspoll(wvif, skb))
			goto drop;

	hdr->band = NL80211_BAND_2GHZ;
	hdr->freq = ieee80211_channel_to_frequency(arg->channel_number,
						   hdr->band);

	if (arg->rxed_rate >= 14) {
		hdr->encoding = RX_ENC_HT;
		hdr->rate_idx = arg->rxed_rate - 14;
	} else if (arg->rxed_rate >= 4) {
		hdr->rate_idx = arg->rxed_rate - 2;
	} else {
		hdr->rate_idx = arg->rxed_rate;
	}

	hdr->signal = arg->rcpi_rssi / 2 - 110;
	hdr->antenna = 0;

	if (arg->rx_flags.encryp) {
		if (wfx_drop_encrypt_data(wvif->wdev, arg, skb))
			goto drop;
		hdr->flag |= RX_FLAG_DECRYPTED | RX_FLAG_IV_STRIPPED;
		if (arg->rx_flags.encryp == HIF_RI_FLAGS_TKIP_ENCRYPTED)
			hdr->flag |= RX_FLAG_MMIC_STRIPPED;
	}

	/* Filter block ACK negotiation: fully controlled by firmware */
	if (ieee80211_is_action(frame->frame_control) &&
	    arg->rx_flags.match_uc_addr &&
	    mgmt->u.action.category == WLAN_CATEGORY_BACK)
		goto drop;
	if (ieee80211_is_beacon(frame->frame_control) &&
	    !arg->status && wvif->vif &&
	    ether_addr_equal(ieee80211_get_SA(frame),
			     wvif->vif->bss_conf.bssid)) {
		const u8 *tim_ie;
		u8 *ies = mgmt->u.beacon.variable;
		size_t ies_len = skb->len - (ies - skb->data);

		tim_ie = cfg80211_find_ie(WLAN_EID_TIM, ies, ies_len);
		if (tim_ie) {
			struct ieee80211_tim_ie *tim = (struct ieee80211_tim_ie *)&tim_ie[2];

			if (wvif->dtim_period != tim->dtim_period) {
				wvif->dtim_period = tim->dtim_period;
				schedule_work(&wvif->set_beacon_wakeup_period_work);
			}
		}

		/* Disable beacon filter once we're associated... */
		if (wvif->disable_beacon_filter &&
		    (wvif->vif->bss_conf.assoc ||
		     wvif->vif->bss_conf.ibss_joined)) {
			wvif->disable_beacon_filter = false;
			schedule_work(&wvif->update_filtering_work);
		}
	}

	if (early_data) {
		spin_lock_bh(&wvif->ps_state_lock);
		/* Double-check status with lock held */
		if (entry->status == WFX_LINK_SOFT)
			skb_queue_tail(&entry->rx_queue, skb);
		else
			ieee80211_rx_irqsafe(wvif->wdev->hw, skb);
		spin_unlock_bh(&wvif->ps_state_lock);
	} else {
		ieee80211_rx_irqsafe(wvif->wdev->hw, skb);
	}

	return;

drop:
	dev_kfree_skb(skb);
}
