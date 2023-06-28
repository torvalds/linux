/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NXP Wireless LAN device driver: WMM
 *
 * Copyright 2011-2020 NXP
 */

#ifndef _MWIFIEX_WMM_H_
#define _MWIFIEX_WMM_H_

enum ieee_types_wmm_aciaifsn_bitmasks {
	MWIFIEX_AIFSN = (BIT(0) | BIT(1) | BIT(2) | BIT(3)),
	MWIFIEX_ACM = BIT(4),
	MWIFIEX_ACI = (BIT(5) | BIT(6)),
};

enum ieee_types_wmm_ecw_bitmasks {
	MWIFIEX_ECW_MIN = (BIT(0) | BIT(1) | BIT(2) | BIT(3)),
	MWIFIEX_ECW_MAX = (BIT(4) | BIT(5) | BIT(6) | BIT(7)),
};

extern const u16 mwifiex_1d_to_wmm_queue[];
extern const u8 tos_to_tid_inv[];

/*
 * This function retrieves the TID of the given RA list.
 */
static inline int
mwifiex_get_tid(struct mwifiex_ra_list_tbl *ptr)
{
	struct sk_buff *skb;

	if (skb_queue_empty(&ptr->skb_head))
		return 0;

	skb = skb_peek(&ptr->skb_head);

	return skb->priority;
}

/*
 * This function checks if a RA list is empty or not.
 */
static inline u8
mwifiex_wmm_is_ra_list_empty(struct list_head *ra_list_hhead)
{
	struct mwifiex_ra_list_tbl *ra_list;
	int is_list_empty;

	list_for_each_entry(ra_list, ra_list_hhead, list) {
		is_list_empty = skb_queue_empty(&ra_list->skb_head);
		if (!is_list_empty)
			return false;
	}

	return true;
}

void mwifiex_wmm_add_buf_txqueue(struct mwifiex_private *priv,
				 struct sk_buff *skb);
void mwifiex_wmm_add_buf_bypass_txqueue(struct mwifiex_private *priv,
					struct sk_buff *skb);
void mwifiex_ralist_add(struct mwifiex_private *priv, const u8 *ra);
void mwifiex_rotate_priolists(struct mwifiex_private *priv,
			      struct mwifiex_ra_list_tbl *ra, int tid);

int mwifiex_wmm_lists_empty(struct mwifiex_adapter *adapter);
int mwifiex_bypass_txlist_empty(struct mwifiex_adapter *adapter);
void mwifiex_wmm_process_tx(struct mwifiex_adapter *adapter);
void mwifiex_process_bypass_tx(struct mwifiex_adapter *adapter);
int mwifiex_is_ralist_valid(struct mwifiex_private *priv,
			    struct mwifiex_ra_list_tbl *ra_list, int tid);

u8 mwifiex_wmm_compute_drv_pkt_delay(struct mwifiex_private *priv,
				     const struct sk_buff *skb);
void mwifiex_wmm_init(struct mwifiex_adapter *adapter);

u32 mwifiex_wmm_process_association_req(struct mwifiex_private *priv,
					u8 **assoc_buf,
					struct ieee_types_wmm_parameter *wmmie,
					struct ieee80211_ht_cap *htcap);

void mwifiex_wmm_setup_queue_priorities(struct mwifiex_private *priv,
					struct ieee_types_wmm_parameter *wmm_ie);
void mwifiex_wmm_setup_ac_downgrade(struct mwifiex_private *priv);
int mwifiex_ret_wmm_get_status(struct mwifiex_private *priv,
			       const struct host_cmd_ds_command *resp);
struct mwifiex_ra_list_tbl *
mwifiex_wmm_get_queue_raptr(struct mwifiex_private *priv, u8 tid,
			    const u8 *ra_addr);
u8 mwifiex_wmm_downgrade_tid(struct mwifiex_private *priv, u32 tid);
void mwifiex_update_ralist_tx_pause(struct mwifiex_private *priv, u8 *mac,
				    u8 tx_pause);
void mwifiex_update_ralist_tx_pause_in_tdls_cs(struct mwifiex_private *priv,
					       u8 *mac, u8 tx_pause);

struct mwifiex_ra_list_tbl *mwifiex_wmm_get_ralist_node(struct mwifiex_private
					*priv, u8 tid, const u8 *ra_addr);
#endif /* !_MWIFIEX_WMM_H_ */
