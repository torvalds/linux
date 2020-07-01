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

void wfx_rx_cb(struct wfx_vif *wvif,
	       const struct hif_ind_rx *arg, struct sk_buff *skb)
{
	struct ieee80211_rx_status *hdr = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_hdr *frame = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;

	memset(hdr, 0, sizeof(*hdr));

	if (arg->status == HIF_STATUS_RX_FAIL_MIC)
		hdr->flag |= RX_FLAG_MMIC_ERROR;
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

	if (arg->rx_flags.encryp)
		hdr->flag |= RX_FLAG_DECRYPTED | RX_FLAG_PN_VALIDATED;

	/* Filter block ACK negotiation: fully controlled by firmware */
	if (ieee80211_is_action(frame->frame_control) &&
	    arg->rx_flags.match_uc_addr &&
	    mgmt->u.action.category == WLAN_CATEGORY_BACK)
		goto drop;
	ieee80211_rx_irqsafe(wvif->wdev->hw, skb);

	return;

drop:
	dev_kfree_skb(skb);
}
