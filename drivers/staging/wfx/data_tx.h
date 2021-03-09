/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Datapath implementation.
 *
 * Copyright (c) 2017-2020, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#ifndef WFX_DATA_TX_H
#define WFX_DATA_TX_H

#include <linux/list.h>
#include <net/mac80211.h>

#include "hif_api_cmd.h"
#include "hif_api_mib.h"

struct wfx_tx_priv;
struct wfx_dev;
struct wfx_vif;

struct tx_policy {
	struct list_head link;
	int usage_count;
	u8 rates[12];
	bool uploaded;
};

struct tx_policy_cache {
	struct tx_policy cache[HIF_TX_RETRY_POLICY_MAX];
	// FIXME: use a trees and drop hash from tx_policy
	struct list_head used;
	struct list_head free;
	spinlock_t lock;
};

struct wfx_tx_priv {
	ktime_t xmit_timestamp;
	unsigned char icv_size;
};

void wfx_tx_policy_init(struct wfx_vif *wvif);
void wfx_tx_policy_upload_work(struct work_struct *work);

void wfx_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
	    struct sk_buff *skb);
void wfx_tx_confirm_cb(struct wfx_dev *wdev, const struct hif_cnf_tx *arg);
void wfx_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       u32 queues, bool drop);

static inline struct wfx_tx_priv *wfx_skb_tx_priv(struct sk_buff *skb)
{
	struct ieee80211_tx_info *tx_info;

	if (!skb)
		return NULL;
	tx_info = IEEE80211_SKB_CB(skb);
	return (struct wfx_tx_priv *)tx_info->rate_driver_data;
}

static inline struct hif_req_tx *wfx_skb_txreq(struct sk_buff *skb)
{
	struct hif_msg *hif = (struct hif_msg *)skb->data;
	struct hif_req_tx *req = (struct hif_req_tx *)hif->body;

	return req;
}

#endif /* WFX_DATA_TX_H */
