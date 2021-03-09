// SPDX-License-Identifier: GPL-2.0-only
/*
 * Datapath implementation.
 *
 * Copyright (c) 2017-2020, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <linux/etherdevice.h>
#include <net/mac80211.h>

#include "data_rx.h"
#include "wfx.h"
#include "bh.h"
#include "sta.h"

static void wfx_rx_handle_ba(struct wfx_vif *wvif, struct ieee80211_mgmt *mgmt)
{
	int params, tid;

	if (wfx_api_older_than(wvif->wdev, 3, 6))
		return;

	switch (mgmt->u.action.u.addba_req.action_code) {
	case WLAN_ACTION_ADDBA_REQ:
		params = le16_to_cpu(mgmt->u.action.u.addba_req.capab);
		tid = (params & IEEE80211_ADDBA_PARAM_TID_MASK) >> 2;
		ieee80211_start_rx_ba_session_offl(wvif->vif, mgmt->sa, tid);
		break;
	case WLAN_ACTION_DELBA:
		params = le16_to_cpu(mgmt->u.action.u.delba.params);
		tid = (params &  IEEE80211_DELBA_PARAM_TID_MASK) >> 12;
		ieee80211_stop_rx_ba_session_offl(wvif->vif, mgmt->sa, tid);
		break;
	}
}

void wfx_rx_cb(struct wfx_vif *wvif,
	       const struct hif_ind_rx *arg, struct sk_buff *skb)
{
	struct ieee80211_rx_status *hdr = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_hdr *frame = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;

	memset(hdr, 0, sizeof(*hdr));

	if (arg->status == HIF_STATUS_RX_FAIL_MIC)
		hdr->flag |= RX_FLAG_MMIC_ERROR | RX_FLAG_IV_STRIPPED;
	else if (arg->status)
		goto drop;

	if (skb->len < sizeof(struct ieee80211_pspoll)) {
		dev_warn(wvif->wdev->dev, "malformed SDU received\n");
		goto drop;
	}

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

	if (!arg->rcpi_rssi) {
		hdr->flag |= RX_FLAG_NO_SIGNAL_VAL;
		dev_info(wvif->wdev->dev, "received frame without RSSI data\n");
	}
	hdr->signal = arg->rcpi_rssi / 2 - 110;
	hdr->antenna = 0;

	if (arg->encryp)
		hdr->flag |= RX_FLAG_DECRYPTED;

	// Block ack negotiation is offloaded by the firmware. However,
	// re-ordering must be done by the mac80211.
	if (ieee80211_is_action(frame->frame_control) &&
	    mgmt->u.action.category == WLAN_CATEGORY_BACK &&
	    skb->len > IEEE80211_MIN_ACTION_SIZE) {
		wfx_rx_handle_ba(wvif, mgmt);
		goto drop;
	}

	ieee80211_rx_irqsafe(wvif->wdev->hw, skb);
	return;

drop:
	dev_kfree_skb(skb);
}
