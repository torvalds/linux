/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Datapath implementation.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#ifndef WFX_DATA_TX_H
#define WFX_DATA_TX_H

#include <linux/list.h>
#include <net/mac80211.h>

#include "hif_api_cmd.h"
#include "hif_api_mib.h"

// FIXME: use IEEE80211_NUM_TIDS
#define WFX_MAX_TID               8

struct wfx_tx_priv;
struct wfx_dev;
struct wfx_vif;

enum wfx_link_status {
	WFX_LINK_OFF,
	WFX_LINK_RESERVE,
	WFX_LINK_SOFT,
	WFX_LINK_HARD,
};

struct wfx_link_entry {
	unsigned long		timestamp;
	enum wfx_link_status	status;
	uint8_t			mac[ETH_ALEN];
	uint8_t			old_mac[ETH_ALEN];
	uint8_t			buffered[WFX_MAX_TID];
	struct sk_buff_head	rx_queue;
};

struct tx_policy {
	struct list_head link;
	uint8_t rates[12];
	uint8_t usage_count;
	uint8_t uploaded;
};

struct tx_policy_cache {
	struct tx_policy cache[HIF_MIB_NUM_TX_RATE_RETRY_POLICIES];
	// FIXME: use a trees and drop hash from tx_policy
	struct list_head used;
	struct list_head free;
	spinlock_t lock;
};

struct wfx_tx_priv {
	ktime_t xmit_timestamp;
	struct ieee80211_key_conf *hw_key;
	uint8_t link_id;
	uint8_t raw_link_id;
	uint8_t tid;
} __packed;

void wfx_tx_policy_init(struct wfx_vif *wvif);

void wfx_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
	    struct sk_buff *skb);
void wfx_tx_confirm_cb(struct wfx_vif *wvif, struct hif_cnf_tx *arg);
void wfx_skb_dtor(struct wfx_dev *wdev, struct sk_buff *skb);

int wfx_unmap_link(struct wfx_vif *wvif, int link_id);
void wfx_link_id_work(struct work_struct *work);
void wfx_link_id_gc_work(struct work_struct *work);
int wfx_find_link_id(struct wfx_vif *wvif, const u8 *mac);

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
	struct hif_req_tx *req = (struct hif_req_tx *) hif->body;

	return req;
}

#endif /* WFX_DATA_TX_H */
