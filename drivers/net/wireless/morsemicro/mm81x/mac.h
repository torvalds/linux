/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2026 Morse Micro
 */

#ifndef _MM81X_MAC_H_
#define _MM81X_MAC_H_

#include "core.h"
#include "command.h"

struct mm81x_queue_params {
	u8 uapsd;
	u8 aci;
	u8 aifs;
	u16 cw_min;
	u16 cw_max;
	u32 txop;
};

static inline u32 mm81x_vif_generate_cssid(struct ieee80211_vif *vif)
{
	return mm81x_generate_cssid(vif->cfg.ssid, vif->cfg.ssid_len);
}

/*
 * Build a little-endian word from the last four octets of a MAC address;
 * the first two octets are dropped.
 */
static inline __le32 mac2le32(const unsigned char *addr)
{
	return cpu_to_le32(((u32)(addr[2]) << 24) | ((u32)(addr[3]) << 16) |
			   ((u32)(addr[4]) << 8) | ((u32)(addr[5])));
}

static inline struct ieee80211_vif *
mm81x_rcu_dereference_vif_id(struct mm81x *mors, u8 vif_id, bool rcu)
{
	if (WARN_ON(vif_id >= ARRAY_SIZE(mors->vifs)))
		return NULL;

	if (rcu)
		return rcu_dereference(mors->vifs[vif_id]);

	return rcu_dereference_protected(mors->vifs[vif_id],
					 lockdep_is_held(&mors->hw->wiphy->mtx));
}

int mm81x_tx_h_get_attempts(struct mm81x *mors,
			    struct mm81x_skb_tx_status *tx_sts);
struct mm81x *mm81x_mac_alloc(size_t priv_size, struct device *dev);
int mm81x_mac_register(struct mm81x *mors);
void mm81x_mac_free(struct mm81x *mors);
void mm81x_mac_unregister(struct mm81x *mors);
int mm81x_mac_event_recv(struct mm81x *mors, struct sk_buff *skb);
void mm81x_mac_rx_skb(struct mm81x *mors, struct sk_buff *skb,
		      struct mm81x_skb_rx_status *hdr_rx_status);
void mm81x_mac_beacon_irq_handle(struct mm81x *mors, u32 status);

u8 *mm81x_hw_scan_h_insert_tlvs(struct mm81x_hw_scan_params *params, u8 *buf);
size_t mm81x_hw_scan_h_get_cmd_size(struct mm81x_hw_scan_params *params);
void mm81x_tx_h_check_aggr(struct ieee80211_sta *pubsta, struct sk_buff *skb);
#endif /* !_MM81X_MAC_H_ */
