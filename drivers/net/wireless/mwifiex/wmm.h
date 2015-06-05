/*
 * Marvell Wireless LAN device driver: WMM
 *
 * Copyright (C) 2011-2014, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
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

static const u16 mwifiex_1d_to_wmm_queue[8] = { 1, 0, 0, 1, 2, 2, 3, 3 };

/*
 * This table inverses the tos_to_tid operation to get a priority
 * which is in sequential order, and can be compared.
 * Use this to compare the priority of two different TIDs.
 */
static const u8 tos_to_tid_inv[] = {
	0x02,  /* from tos_to_tid[2] = 0 */
	0x00,  /* from tos_to_tid[0] = 1 */
	0x01,  /* from tos_to_tid[1] = 2 */
	0x03,
	0x04,
	0x05,
	0x06,
	0x07};

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
 * This function gets the length of a list.
 */
static inline int
mwifiex_wmm_list_len(struct list_head *head)
{
	struct list_head *pos;
	int count = 0;

	list_for_each(pos, head)
		++count;

	return count;
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
void mwifiex_ralist_add(struct mwifiex_private *priv, const u8 *ra);
void mwifiex_rotate_priolists(struct mwifiex_private *priv,
			      struct mwifiex_ra_list_tbl *ra, int tid);

int mwifiex_wmm_lists_empty(struct mwifiex_adapter *adapter);
void mwifiex_wmm_process_tx(struct mwifiex_adapter *adapter);
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

struct mwifiex_ra_list_tbl *mwifiex_wmm_get_ralist_node(struct mwifiex_private
					*priv, u8 tid, const u8 *ra_addr);
#endif /* !_MWIFIEX_WMM_H_ */
