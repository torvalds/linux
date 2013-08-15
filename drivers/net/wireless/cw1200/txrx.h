/*
 * Datapath interface for ST-Ericsson CW1200 mac80211 drivers
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@lockless.no>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CW1200_TXRX_H
#define CW1200_TXRX_H

#include <linux/list.h>

/* extern */ struct ieee80211_hw;
/* extern */ struct sk_buff;
/* extern */ struct wsm_tx;
/* extern */ struct wsm_rx;
/* extern */ struct wsm_tx_confirm;
/* extern */ struct cw1200_txpriv;

struct tx_policy {
	union {
		__le32 tbl[3];
		u8 raw[12];
	};
	u8  defined;
	u8  usage_count;
	u8  retry_count;
	u8  uploaded;
};

struct tx_policy_cache_entry {
	struct tx_policy policy;
	struct list_head link;
};

#define TX_POLICY_CACHE_SIZE	(8)
struct tx_policy_cache {
	struct tx_policy_cache_entry cache[TX_POLICY_CACHE_SIZE];
	struct list_head used;
	struct list_head free;
	spinlock_t lock; /* Protect policy cache */
};

/* ******************************************************************** */
/* TX policy cache							*/
/* Intention of TX policy cache is an overcomplicated WSM API.
 * Device does not accept per-PDU tx retry sequence.
 * It uses "tx retry policy id" instead, so driver code has to sync
 * linux tx retry sequences with a retry policy table in the device.
 */
void tx_policy_init(struct cw1200_common *priv);
void tx_policy_upload_work(struct work_struct *work);
void tx_policy_clean(struct cw1200_common *priv);

/* ******************************************************************** */
/* TX implementation							*/

u32 cw1200_rate_mask_to_wsm(struct cw1200_common *priv,
			       u32 rates);
void cw1200_tx(struct ieee80211_hw *dev,
	       struct ieee80211_tx_control *control,
	       struct sk_buff *skb);
void cw1200_skb_dtor(struct cw1200_common *priv,
		     struct sk_buff *skb,
		     const struct cw1200_txpriv *txpriv);

/* ******************************************************************** */
/* WSM callbacks							*/

void cw1200_tx_confirm_cb(struct cw1200_common *priv,
			  int link_id,
			  struct wsm_tx_confirm *arg);
void cw1200_rx_cb(struct cw1200_common *priv,
		  struct wsm_rx *arg,
		  int link_id,
		  struct sk_buff **skb_p);

/* ******************************************************************** */
/* Timeout								*/

void cw1200_tx_timeout(struct work_struct *work);

/* ******************************************************************** */
/* Security								*/
int cw1200_alloc_key(struct cw1200_common *priv);
void cw1200_free_key(struct cw1200_common *priv, int idx);
void cw1200_free_keys(struct cw1200_common *priv);
int cw1200_upload_keys(struct cw1200_common *priv);

/* ******************************************************************** */
/* Workaround for WFD test case 6.1.10					*/
void cw1200_link_id_reset(struct work_struct *work);

#define CW1200_LINK_ID_GC_TIMEOUT ((unsigned long)(10 * HZ))

int cw1200_find_link_id(struct cw1200_common *priv, const u8 *mac);
int cw1200_alloc_link_id(struct cw1200_common *priv, const u8 *mac);
void cw1200_link_id_work(struct work_struct *work);
void cw1200_link_id_gc_work(struct work_struct *work);


#endif /* CW1200_TXRX_H */
