/*
 * This file is part of wl18xx
 *
 * Copyright (C) 2011 Texas Instruments Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include "../wlcore/wlcore.h"
#include "../wlcore/cmd.h"
#include "../wlcore/debug.h"
#include "../wlcore/acx.h"
#include "../wlcore/tx.h"

#include "wl18xx.h"
#include "tx.h"

static void wl18xx_tx_complete_packet(struct wl1271 *wl, u8 tx_stat_byte)
{
	struct ieee80211_tx_info *info;
	struct sk_buff *skb;
	int id = tx_stat_byte & WL18XX_TX_STATUS_DESC_ID_MASK;
	bool tx_success;

	/* check for id legality */
	if (unlikely(id >= wl->num_tx_desc || wl->tx_frames[id] == NULL)) {
		wl1271_warning("illegal id in tx completion: %d", id);
		return;
	}

	/* a zero bit indicates Tx success */
	tx_success = !(tx_stat_byte & BIT(WL18XX_TX_STATUS_STAT_BIT_IDX));


	skb = wl->tx_frames[id];
	info = IEEE80211_SKB_CB(skb);

	if (wl12xx_is_dummy_packet(wl, skb)) {
		wl1271_free_tx_id(wl, id);
		return;
	}

	/* update the TX status info */
	if (tx_success && !(info->flags & IEEE80211_TX_CTL_NO_ACK))
		info->flags |= IEEE80211_TX_STAT_ACK;

	/* no real data about Tx completion */
	info->status.rates[0].idx = -1;
	info->status.rates[0].count = 0;
	info->status.rates[0].flags = 0;
	info->status.ack_signal = -1;

	if (!tx_success)
		wl->stats.retry_count++;

	/*
	 * TODO: update sequence number for encryption? seems to be
	 * unsupported for now. needed for recovery with encryption.
	 */

	/* remove private header from packet */
	skb_pull(skb, sizeof(struct wl1271_tx_hw_descr));

	/* remove TKIP header space if present */
	if (info->control.hw_key &&
	    info->control.hw_key->cipher == WLAN_CIPHER_SUITE_TKIP) {
		int hdrlen = ieee80211_get_hdrlen_from_skb(skb);
		memmove(skb->data + WL1271_EXTRA_SPACE_TKIP, skb->data, hdrlen);
		skb_pull(skb, WL1271_EXTRA_SPACE_TKIP);
	}

	wl1271_debug(DEBUG_TX, "tx status id %u skb 0x%p success %d",
		     id, skb, tx_success);

	/* return the packet to the stack */
	skb_queue_tail(&wl->deferred_tx_queue, skb);
	queue_work(wl->freezable_wq, &wl->netstack_work);
	wl1271_free_tx_id(wl, id);
}

void wl18xx_tx_immediate_complete(struct wl1271 *wl)
{
	struct wl18xx_fw_status_priv *status_priv =
		(struct wl18xx_fw_status_priv *)wl->fw_status_2->priv;
	struct wl18xx_priv *priv = wl->priv;
	u8 i;

	/* nothing to do here */
	if (priv->last_fw_rls_idx == status_priv->fw_release_idx)
		return;

	/* freed Tx descriptors */
	wl1271_debug(DEBUG_TX, "last released desc = %d, current idx = %d",
		     priv->last_fw_rls_idx, status_priv->fw_release_idx);

	if (status_priv->fw_release_idx >= WL18XX_FW_MAX_TX_STATUS_DESC) {
		wl1271_error("invalid desc release index %d",
			     status_priv->fw_release_idx);
		WARN_ON(1);
		return;
	}

	for (i = priv->last_fw_rls_idx;
	     i != status_priv->fw_release_idx;
	     i = (i + 1) % WL18XX_FW_MAX_TX_STATUS_DESC) {
		wl18xx_tx_complete_packet(wl,
			status_priv->released_tx_desc[i]);

		wl->tx_results_count++;
	}

	priv->last_fw_rls_idx = status_priv->fw_release_idx;
}
