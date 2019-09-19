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

#define TXOP_UNIT 32

static int wfx_set_tim_impl(struct wfx_vif *wvif, bool aid0_bit_set)
{
	struct sk_buff *skb;
	struct hif_ie_flags target_frame = {
		.beacon = 1,
	};
	u16 tim_offset, tim_length;
	u8 *tim_ptr;

	skb = ieee80211_beacon_get_tim(wvif->wdev->hw, wvif->vif,
				       &tim_offset, &tim_length);
	if (!skb)
		return -ENOENT;
	tim_ptr = skb->data + tim_offset;

	if (tim_offset && tim_length >= 6) {
		/* Ignore DTIM count from mac80211:
		 * firmware handles DTIM internally.
		 */
		tim_ptr[2] = 0;

		/* Set/reset aid0 bit */
		if (aid0_bit_set)
			tim_ptr[4] |= 1;
		else
			tim_ptr[4] &= ~1;
	}

	hif_update_ie(wvif, &target_frame, tim_ptr, tim_length);
	dev_kfree_skb(skb);

	return 0;
}

static void wfx_mcast_start_work(struct work_struct *work)
{
	struct wfx_vif *wvif = container_of(work, struct wfx_vif, mcast_start_work);

	cancel_work_sync(&wvif->mcast_stop_work);
	if (!wvif->aid0_bit_set) {
		wfx_tx_lock_flush(wvif->wdev);
		wfx_set_tim_impl(wvif, true);
		wvif->aid0_bit_set = true;
		mod_timer(&wvif->mcast_timeout, TU_TO_JIFFIES(1000));
		wfx_tx_unlock(wvif->wdev);
	}
}

static void wfx_mcast_stop_work(struct work_struct *work)
{
	struct wfx_vif *wvif = container_of(work, struct wfx_vif, mcast_stop_work);

	if (wvif->aid0_bit_set) {
		del_timer_sync(&wvif->mcast_timeout);
		wfx_tx_lock_flush(wvif->wdev);
		wvif->aid0_bit_set = false;
		wfx_set_tim_impl(wvif, false);
		wfx_tx_unlock(wvif->wdev);
	}
}

static void wfx_mcast_timeout(struct timer_list *t)
{
	struct wfx_vif *wvif = from_timer(wvif, t, mcast_timeout);

	dev_warn(wvif->wdev->dev, "multicast delivery timeout\n");
	spin_lock_bh(&wvif->ps_state_lock);
	wvif->mcast_tx = wvif->aid0_bit_set && wvif->mcast_buffered;
	if (wvif->mcast_tx)
		wfx_bh_request_tx(wvif->wdev);
	spin_unlock_bh(&wvif->ps_state_lock);
}

int wfx_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	int i;
	struct wfx_dev *wdev = hw->priv;
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;
	// FIXME: parameters are set by kernel juste after interface_add.
	// Keep struct hif_req_edca_queue_params blank?
	struct hif_req_edca_queue_params default_edca_params[] = {
		[IEEE80211_AC_VO] = {
			.queue_id = HIF_QUEUE_ID_VOICE,
			.aifsn = 2,
			.cw_min = 3,
			.cw_max = 7,
			.tx_op_limit = TXOP_UNIT * 47,
		},
		[IEEE80211_AC_VI] = {
			.queue_id = HIF_QUEUE_ID_VIDEO,
			.aifsn = 2,
			.cw_min = 7,
			.cw_max = 15,
			.tx_op_limit = TXOP_UNIT * 94,
		},
		[IEEE80211_AC_BE] = {
			.queue_id = HIF_QUEUE_ID_BESTEFFORT,
			.aifsn = 3,
			.cw_min = 15,
			.cw_max = 1023,
			.tx_op_limit = TXOP_UNIT * 0,
		},
		[IEEE80211_AC_BK] = {
			.queue_id = HIF_QUEUE_ID_BACKGROUND,
			.aifsn = 7,
			.cw_min = 15,
			.cw_max = 1023,
			.tx_op_limit = TXOP_UNIT * 0,
		},
	};

	if (wfx_api_older_than(wdev, 2, 0)) {
		default_edca_params[IEEE80211_AC_BE].queue_id = HIF_QUEUE_ID_BACKGROUND;
		default_edca_params[IEEE80211_AC_BK].queue_id = HIF_QUEUE_ID_BESTEFFORT;
	}

	for (i = 0; i < ARRAY_SIZE(wdev->vif); i++) {
		if (!wdev->vif[i]) {
			wdev->vif[i] = vif;
			wvif->id = i;
			break;
		}
	}
	if (i == ARRAY_SIZE(wdev->vif))
		return -EOPNOTSUPP;
	wvif->vif = vif;
	wvif->wdev = wdev;

	INIT_WORK(&wvif->link_id_work, wfx_link_id_work);
	INIT_DELAYED_WORK(&wvif->link_id_gc_work, wfx_link_id_gc_work);

	spin_lock_init(&wvif->ps_state_lock);

	INIT_WORK(&wvif->mcast_start_work, wfx_mcast_start_work);
	INIT_WORK(&wvif->mcast_stop_work, wfx_mcast_stop_work);
	timer_setup(&wvif->mcast_timeout, wfx_mcast_timeout, 0);
	BUG_ON(ARRAY_SIZE(default_edca_params) != ARRAY_SIZE(wvif->edca.params));
	for (i = 0; i < IEEE80211_NUM_ACS; i++)
		memcpy(&wvif->edca.params[i], &default_edca_params[i], sizeof(default_edca_params[i]));
	tx_policy_init(wvif);
	return 0;
}

void wfx_remove_interface(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif)
{
	struct wfx_vif *wvif = (struct wfx_vif *) vif->drv_priv;

	wfx_tx_queues_wait_empty_vif(wvif);
	cancel_delayed_work_sync(&wvif->link_id_gc_work);
	del_timer_sync(&wvif->mcast_timeout);
}

int wfx_start(struct ieee80211_hw *hw)
{
	return 0;
}

void wfx_stop(struct ieee80211_hw *hw)
{
	struct wfx_dev *wdev = hw->priv;

	wfx_tx_lock_flush(wdev);
	wfx_tx_queues_clear(wdev);
	wfx_tx_unlock(wdev);
	WARN(atomic_read(&wdev->tx_lock), "tx_lock is locked");
}
