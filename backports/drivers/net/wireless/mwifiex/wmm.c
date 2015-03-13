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

#include "decl.h"
#include "ioctl.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "wmm.h"
#include "11n.h"


/* Maximum value FW can accept for driver delay in packet transmission */
#define DRV_PKT_DELAY_TO_FW_MAX   512


#define WMM_QUEUED_PACKET_LOWER_LIMIT   180

#define WMM_QUEUED_PACKET_UPPER_LIMIT   200

/* Offset for TOS field in the IP header */
#define IPTOS_OFFSET 5

static bool disable_tx_amsdu;
module_param(disable_tx_amsdu, bool, 0644);

/* WMM information IE */
static const u8 wmm_info_ie[] = { WLAN_EID_VENDOR_SPECIFIC, 0x07,
	0x00, 0x50, 0xf2, 0x02,
	0x00, 0x01, 0x00
};

static const u8 wmm_aci_to_qidx_map[] = { WMM_AC_BE,
	WMM_AC_BK,
	WMM_AC_VI,
	WMM_AC_VO
};

static u8 tos_to_tid[] = {
	/* TID DSCP_P2 DSCP_P1 DSCP_P0 WMM_AC */
	0x01,			/* 0 1 0 AC_BK */
	0x02,			/* 0 0 0 AC_BK */
	0x00,			/* 0 0 1 AC_BE */
	0x03,			/* 0 1 1 AC_BE */
	0x04,			/* 1 0 0 AC_VI */
	0x05,			/* 1 0 1 AC_VI */
	0x06,			/* 1 1 0 AC_VO */
	0x07			/* 1 1 1 AC_VO */
};

static u8 ac_to_tid[4][2] = { {1, 2}, {0, 3}, {4, 5}, {6, 7} };

/*
 * This function debug prints the priority parameters for a WMM AC.
 */
static void
mwifiex_wmm_ac_debug_print(const struct ieee_types_wmm_ac_parameters *ac_param)
{
	const char *ac_str[] = { "BK", "BE", "VI", "VO" };

	pr_debug("info: WMM AC_%s: ACI=%d, ACM=%d, Aifsn=%d, "
		 "EcwMin=%d, EcwMax=%d, TxopLimit=%d\n",
		 ac_str[wmm_aci_to_qidx_map[(ac_param->aci_aifsn_bitmap
					     & MWIFIEX_ACI) >> 5]],
		 (ac_param->aci_aifsn_bitmap & MWIFIEX_ACI) >> 5,
		 (ac_param->aci_aifsn_bitmap & MWIFIEX_ACM) >> 4,
		 ac_param->aci_aifsn_bitmap & MWIFIEX_AIFSN,
		 ac_param->ecw_bitmap & MWIFIEX_ECW_MIN,
		 (ac_param->ecw_bitmap & MWIFIEX_ECW_MAX) >> 4,
		 le16_to_cpu(ac_param->tx_op_limit));
}

/*
 * This function allocates a route address list.
 *
 * The function also initializes the list with the provided RA.
 */
static struct mwifiex_ra_list_tbl *
mwifiex_wmm_allocate_ralist_node(struct mwifiex_adapter *adapter, const u8 *ra)
{
	struct mwifiex_ra_list_tbl *ra_list;

	ra_list = kzalloc(sizeof(struct mwifiex_ra_list_tbl), GFP_ATOMIC);
	if (!ra_list)
		return NULL;

	INIT_LIST_HEAD(&ra_list->list);
	skb_queue_head_init(&ra_list->skb_head);

	memcpy(ra_list->ra, ra, ETH_ALEN);

	ra_list->total_pkt_count = 0;

	dev_dbg(adapter->dev, "info: allocated ra_list %p\n", ra_list);

	return ra_list;
}

/* This function returns random no between 16 and 32 to be used as threshold
 * for no of packets after which BA setup is initiated.
 */
static u8 mwifiex_get_random_ba_threshold(void)
{
	u32 sec, usec;
	struct timeval ba_tstamp;
	u8 ba_threshold;

	/* setup ba_packet_threshold here random number between
	 * [BA_SETUP_PACKET_OFFSET,
	 * BA_SETUP_PACKET_OFFSET+BA_SETUP_MAX_PACKET_THRESHOLD-1]
	 */

	do_gettimeofday(&ba_tstamp);
	sec = (ba_tstamp.tv_sec & 0xFFFF) + (ba_tstamp.tv_sec >> 16);
	usec = (ba_tstamp.tv_usec & 0xFFFF) + (ba_tstamp.tv_usec >> 16);
	ba_threshold = (((sec << 16) + usec) % BA_SETUP_MAX_PACKET_THRESHOLD)
						      + BA_SETUP_PACKET_OFFSET;

	return ba_threshold;
}

/*
 * This function allocates and adds a RA list for all TIDs
 * with the given RA.
 */
void mwifiex_ralist_add(struct mwifiex_private *priv, const u8 *ra)
{
	int i;
	struct mwifiex_ra_list_tbl *ra_list;
	struct mwifiex_adapter *adapter = priv->adapter;
	struct mwifiex_sta_node *node;
	unsigned long flags;


	for (i = 0; i < MAX_NUM_TID; ++i) {
		ra_list = mwifiex_wmm_allocate_ralist_node(adapter, ra);
		dev_dbg(adapter->dev, "info: created ra_list %p\n", ra_list);

		if (!ra_list)
			break;

		ra_list->is_11n_enabled = 0;
		ra_list->tdls_link = false;
		if (!mwifiex_queuing_ra_based(priv)) {
			if (mwifiex_get_tdls_link_status(priv, ra) ==
			    TDLS_SETUP_COMPLETE) {
				ra_list->tdls_link = true;
				ra_list->is_11n_enabled =
					mwifiex_tdls_peer_11n_enabled(priv, ra);
			} else {
				ra_list->is_11n_enabled = IS_11N_ENABLED(priv);
			}
		} else {
			spin_lock_irqsave(&priv->sta_list_spinlock, flags);
			node = mwifiex_get_sta_entry(priv, ra);
			ra_list->is_11n_enabled =
				      mwifiex_is_sta_11n_enabled(priv, node);
			if (ra_list->is_11n_enabled)
				ra_list->max_amsdu = node->max_amsdu;
			spin_unlock_irqrestore(&priv->sta_list_spinlock, flags);
		}

		dev_dbg(adapter->dev, "data: ralist %p: is_11n_enabled=%d\n",
			ra_list, ra_list->is_11n_enabled);

		if (ra_list->is_11n_enabled) {
			ra_list->ba_pkt_count = 0;
			ra_list->ba_packet_thr =
					      mwifiex_get_random_ba_threshold();
		}
		list_add_tail(&ra_list->list,
			      &priv->wmm.tid_tbl_ptr[i].ra_list);
	}
}

/*
 * This function sets the WMM queue priorities to their default values.
 */
static void mwifiex_wmm_default_queue_priorities(struct mwifiex_private *priv)
{
	/* Default queue priorities: VO->VI->BE->BK */
	priv->wmm.queue_priority[0] = WMM_AC_VO;
	priv->wmm.queue_priority[1] = WMM_AC_VI;
	priv->wmm.queue_priority[2] = WMM_AC_BE;
	priv->wmm.queue_priority[3] = WMM_AC_BK;
}

/*
 * This function map ACs to TIDs.
 */
static void
mwifiex_wmm_queue_priorities_tid(struct mwifiex_private *priv)
{
	struct mwifiex_wmm_desc *wmm = &priv->wmm;
	u8 *queue_priority = wmm->queue_priority;
	int i;

	for (i = 0; i < 4; ++i) {
		tos_to_tid[7 - (i * 2)] = ac_to_tid[queue_priority[i]][1];
		tos_to_tid[6 - (i * 2)] = ac_to_tid[queue_priority[i]][0];
	}

	for (i = 0; i < MAX_NUM_TID; ++i)
		priv->tos_to_tid_inv[tos_to_tid[i]] = (u8)i;

	atomic_set(&wmm->highest_queued_prio, HIGH_PRIO_TID);
}

/*
 * This function initializes WMM priority queues.
 */
void
mwifiex_wmm_setup_queue_priorities(struct mwifiex_private *priv,
				   struct ieee_types_wmm_parameter *wmm_ie)
{
	u16 cw_min, avg_back_off, tmp[4];
	u32 i, j, num_ac;
	u8 ac_idx;

	if (!wmm_ie || !priv->wmm_enabled) {
		/* WMM is not enabled, just set the defaults and return */
		mwifiex_wmm_default_queue_priorities(priv);
		return;
	}

	dev_dbg(priv->adapter->dev, "info: WMM Parameter IE: version=%d, "
		"qos_info Parameter Set Count=%d, Reserved=%#x\n",
		wmm_ie->vend_hdr.version, wmm_ie->qos_info_bitmap &
		IEEE80211_WMM_IE_AP_QOSINFO_PARAM_SET_CNT_MASK,
		wmm_ie->reserved);

	for (num_ac = 0; num_ac < ARRAY_SIZE(wmm_ie->ac_params); num_ac++) {
		u8 ecw = wmm_ie->ac_params[num_ac].ecw_bitmap;
		u8 aci_aifsn = wmm_ie->ac_params[num_ac].aci_aifsn_bitmap;
		cw_min = (1 << (ecw & MWIFIEX_ECW_MIN)) - 1;
		avg_back_off = (cw_min >> 1) + (aci_aifsn & MWIFIEX_AIFSN);

		ac_idx = wmm_aci_to_qidx_map[(aci_aifsn & MWIFIEX_ACI) >> 5];
		priv->wmm.queue_priority[ac_idx] = ac_idx;
		tmp[ac_idx] = avg_back_off;

		dev_dbg(priv->adapter->dev,
			"info: WMM: CWmax=%d CWmin=%d Avg Back-off=%d\n",
			(1 << ((ecw & MWIFIEX_ECW_MAX) >> 4)) - 1,
			cw_min, avg_back_off);
		mwifiex_wmm_ac_debug_print(&wmm_ie->ac_params[num_ac]);
	}

	/* Bubble sort */
	for (i = 0; i < num_ac; i++) {
		for (j = 1; j < num_ac - i; j++) {
			if (tmp[j - 1] > tmp[j]) {
				swap(tmp[j - 1], tmp[j]);
				swap(priv->wmm.queue_priority[j - 1],
				     priv->wmm.queue_priority[j]);
			} else if (tmp[j - 1] == tmp[j]) {
				if (priv->wmm.queue_priority[j - 1]
				    < priv->wmm.queue_priority[j])
					swap(priv->wmm.queue_priority[j - 1],
					     priv->wmm.queue_priority[j]);
			}
		}
	}

	mwifiex_wmm_queue_priorities_tid(priv);
}

/*
 * This function evaluates whether or not an AC is to be downgraded.
 *
 * In case the AC is not enabled, the highest AC is returned that is
 * enabled and does not require admission control.
 */
static enum mwifiex_wmm_ac_e
mwifiex_wmm_eval_downgrade_ac(struct mwifiex_private *priv,
			      enum mwifiex_wmm_ac_e eval_ac)
{
	int down_ac;
	enum mwifiex_wmm_ac_e ret_ac;
	struct mwifiex_wmm_ac_status *ac_status;

	ac_status = &priv->wmm.ac_status[eval_ac];

	if (!ac_status->disabled)
		/* Okay to use this AC, its enabled */
		return eval_ac;

	/* Setup a default return value of the lowest priority */
	ret_ac = WMM_AC_BK;

	/*
	 *  Find the highest AC that is enabled and does not require
	 *  admission control. The spec disallows downgrading to an AC,
	 *  which is enabled due to a completed admission control.
	 *  Unadmitted traffic is not to be sent on an AC with admitted
	 *  traffic.
	 */
	for (down_ac = WMM_AC_BK; down_ac < eval_ac; down_ac++) {
		ac_status = &priv->wmm.ac_status[down_ac];

		if (!ac_status->disabled && !ac_status->flow_required)
			/* AC is enabled and does not require admission
			   control */
			ret_ac = (enum mwifiex_wmm_ac_e) down_ac;
	}

	return ret_ac;
}

/*
 * This function downgrades WMM priority queue.
 */
void
mwifiex_wmm_setup_ac_downgrade(struct mwifiex_private *priv)
{
	int ac_val;

	dev_dbg(priv->adapter->dev, "info: WMM: AC Priorities:"
			"BK(0), BE(1), VI(2), VO(3)\n");

	if (!priv->wmm_enabled) {
		/* WMM is not enabled, default priorities */
		for (ac_val = WMM_AC_BK; ac_val <= WMM_AC_VO; ac_val++)
			priv->wmm.ac_down_graded_vals[ac_val] =
						(enum mwifiex_wmm_ac_e) ac_val;
	} else {
		for (ac_val = WMM_AC_BK; ac_val <= WMM_AC_VO; ac_val++) {
			priv->wmm.ac_down_graded_vals[ac_val]
				= mwifiex_wmm_eval_downgrade_ac(priv,
						(enum mwifiex_wmm_ac_e) ac_val);
			dev_dbg(priv->adapter->dev,
				"info: WMM: AC PRIO %d maps to %d\n",
				ac_val, priv->wmm.ac_down_graded_vals[ac_val]);
		}
	}
}

/*
 * This function converts the IP TOS field to an WMM AC
 * Queue assignment.
 */
static enum mwifiex_wmm_ac_e
mwifiex_wmm_convert_tos_to_ac(struct mwifiex_adapter *adapter, u32 tos)
{
	/* Map of TOS UP values to WMM AC */
	const enum mwifiex_wmm_ac_e tos_to_ac[] = { WMM_AC_BE,
		WMM_AC_BK,
		WMM_AC_BK,
		WMM_AC_BE,
		WMM_AC_VI,
		WMM_AC_VI,
		WMM_AC_VO,
		WMM_AC_VO
	};

	if (tos >= ARRAY_SIZE(tos_to_ac))
		return WMM_AC_BE;

	return tos_to_ac[tos];
}

/*
 * This function evaluates a given TID and downgrades it to a lower
 * TID if the WMM Parameter IE received from the AP indicates that the
 * AP is disabled (due to call admission control (ACM bit). Mapping
 * of TID to AC is taken care of internally.
 */
u8 mwifiex_wmm_downgrade_tid(struct mwifiex_private *priv, u32 tid)
{
	enum mwifiex_wmm_ac_e ac, ac_down;
	u8 new_tid;

	ac = mwifiex_wmm_convert_tos_to_ac(priv->adapter, tid);
	ac_down = priv->wmm.ac_down_graded_vals[ac];

	/* Send the index to tid array, picking from the array will be
	 * taken care by dequeuing function
	 */
	new_tid = ac_to_tid[ac_down][tid % 2];

	return new_tid;
}

/*
 * This function initializes the WMM state information and the
 * WMM data path queues.
 */
void
mwifiex_wmm_init(struct mwifiex_adapter *adapter)
{
	int i, j;
	struct mwifiex_private *priv;

	for (j = 0; j < adapter->priv_num; ++j) {
		priv = adapter->priv[j];
		if (!priv)
			continue;

		for (i = 0; i < MAX_NUM_TID; ++i) {
			if (!disable_tx_amsdu &&
			    adapter->tx_buf_size > MWIFIEX_TX_DATA_BUF_SIZE_2K)
				priv->aggr_prio_tbl[i].amsdu =
							priv->tos_to_tid_inv[i];
			else
				priv->aggr_prio_tbl[i].amsdu =
							BA_STREAM_NOT_ALLOWED;
			priv->aggr_prio_tbl[i].ampdu_ap =
							priv->tos_to_tid_inv[i];
			priv->aggr_prio_tbl[i].ampdu_user =
							priv->tos_to_tid_inv[i];
		}

		mwifiex_set_ba_params(priv);
		mwifiex_reset_11n_rx_seq_num(priv);

		atomic_set(&priv->wmm.tx_pkts_queued, 0);
		atomic_set(&priv->wmm.highest_queued_prio, HIGH_PRIO_TID);
	}
}

/*
 * This function checks if WMM Tx queue is empty.
 */
int
mwifiex_wmm_lists_empty(struct mwifiex_adapter *adapter)
{
	int i;
	struct mwifiex_private *priv;

	for (i = 0; i < adapter->priv_num; ++i) {
		priv = adapter->priv[i];
		if (priv && atomic_read(&priv->wmm.tx_pkts_queued))
			return false;
	}

	return true;
}

/*
 * This function deletes all packets in an RA list node.
 *
 * The packet sent completion callback handler are called with
 * status failure, after they are dequeued to ensure proper
 * cleanup. The RA list node itself is freed at the end.
 */
static void
mwifiex_wmm_del_pkts_in_ralist_node(struct mwifiex_private *priv,
				    struct mwifiex_ra_list_tbl *ra_list)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	struct sk_buff *skb, *tmp;

	skb_queue_walk_safe(&ra_list->skb_head, skb, tmp)
		mwifiex_write_data_complete(adapter, skb, 0, -1);
}

/*
 * This function deletes all packets in an RA list.
 *
 * Each nodes in the RA list are freed individually first, and then
 * the RA list itself is freed.
 */
static void
mwifiex_wmm_del_pkts_in_ralist(struct mwifiex_private *priv,
			       struct list_head *ra_list_head)
{
	struct mwifiex_ra_list_tbl *ra_list;

	list_for_each_entry(ra_list, ra_list_head, list)
		mwifiex_wmm_del_pkts_in_ralist_node(priv, ra_list);
}

/*
 * This function deletes all packets in all RA lists.
 */
static void mwifiex_wmm_cleanup_queues(struct mwifiex_private *priv)
{
	int i;

	for (i = 0; i < MAX_NUM_TID; i++)
		mwifiex_wmm_del_pkts_in_ralist(priv, &priv->wmm.tid_tbl_ptr[i].
								       ra_list);

	atomic_set(&priv->wmm.tx_pkts_queued, 0);
	atomic_set(&priv->wmm.highest_queued_prio, HIGH_PRIO_TID);
}

/*
 * This function deletes all route addresses from all RA lists.
 */
static void mwifiex_wmm_delete_all_ralist(struct mwifiex_private *priv)
{
	struct mwifiex_ra_list_tbl *ra_list, *tmp_node;
	int i;

	for (i = 0; i < MAX_NUM_TID; ++i) {
		dev_dbg(priv->adapter->dev,
			"info: ra_list: freeing buf for tid %d\n", i);
		list_for_each_entry_safe(ra_list, tmp_node,
					 &priv->wmm.tid_tbl_ptr[i].ra_list,
					 list) {
			list_del(&ra_list->list);
			kfree(ra_list);
		}

		INIT_LIST_HEAD(&priv->wmm.tid_tbl_ptr[i].ra_list);
	}
}

static int mwifiex_free_ack_frame(int id, void *p, void *data)
{
	pr_warn("Have pending ack frames!\n");
	kfree_skb(p);
	return 0;
}

/*
 * This function cleans up the Tx and Rx queues.
 *
 * Cleanup includes -
 *      - All packets in RA lists
 *      - All entries in Rx reorder table
 *      - All entries in Tx BA stream table
 *      - MPA buffer (if required)
 *      - All RA lists
 */
void
mwifiex_clean_txrx(struct mwifiex_private *priv)
{
	unsigned long flags;
	struct sk_buff *skb, *tmp;

	mwifiex_11n_cleanup_reorder_tbl(priv);
	spin_lock_irqsave(&priv->wmm.ra_list_spinlock, flags);

	mwifiex_wmm_cleanup_queues(priv);
	mwifiex_11n_delete_all_tx_ba_stream_tbl(priv);

	if (priv->adapter->if_ops.cleanup_mpa_buf)
		priv->adapter->if_ops.cleanup_mpa_buf(priv->adapter);

	mwifiex_wmm_delete_all_ralist(priv);
	memcpy(tos_to_tid, ac_to_tid, sizeof(tos_to_tid));

	if (priv->adapter->if_ops.clean_pcie_ring &&
	    !priv->adapter->surprise_removed)
		priv->adapter->if_ops.clean_pcie_ring(priv->adapter);
	spin_unlock_irqrestore(&priv->wmm.ra_list_spinlock, flags);

	skb_queue_walk_safe(&priv->tdls_txq, skb, tmp)
		mwifiex_write_data_complete(priv->adapter, skb, 0, -1);

	idr_for_each(&priv->ack_status_frames, mwifiex_free_ack_frame, NULL);
	idr_destroy(&priv->ack_status_frames);
}

/*
 * This function retrieves a particular RA list node, matching with the
 * given TID and RA address.
 */
static struct mwifiex_ra_list_tbl *
mwifiex_wmm_get_ralist_node(struct mwifiex_private *priv, u8 tid,
			    const u8 *ra_addr)
{
	struct mwifiex_ra_list_tbl *ra_list;

	list_for_each_entry(ra_list, &priv->wmm.tid_tbl_ptr[tid].ra_list,
			    list) {
		if (!memcmp(ra_list->ra, ra_addr, ETH_ALEN))
			return ra_list;
	}

	return NULL;
}

/*
 * This function retrieves an RA list node for a given TID and
 * RA address pair.
 *
 * If no such node is found, a new node is added first and then
 * retrieved.
 */
struct mwifiex_ra_list_tbl *
mwifiex_wmm_get_queue_raptr(struct mwifiex_private *priv, u8 tid,
			    const u8 *ra_addr)
{
	struct mwifiex_ra_list_tbl *ra_list;

	ra_list = mwifiex_wmm_get_ralist_node(priv, tid, ra_addr);
	if (ra_list)
		return ra_list;
	mwifiex_ralist_add(priv, ra_addr);

	return mwifiex_wmm_get_ralist_node(priv, tid, ra_addr);
}

/*
 * This function deletes RA list nodes for given mac for all TIDs.
 * Function also decrements TX pending count accordingly.
 */
void
mwifiex_wmm_del_peer_ra_list(struct mwifiex_private *priv, const u8 *ra_addr)
{
	struct mwifiex_ra_list_tbl *ra_list;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&priv->wmm.ra_list_spinlock, flags);

	for (i = 0; i < MAX_NUM_TID; ++i) {
		ra_list = mwifiex_wmm_get_ralist_node(priv, i, ra_addr);

		if (!ra_list)
			continue;
		mwifiex_wmm_del_pkts_in_ralist_node(priv, ra_list);
		atomic_sub(ra_list->total_pkt_count, &priv->wmm.tx_pkts_queued);
		list_del(&ra_list->list);
		kfree(ra_list);
	}
	spin_unlock_irqrestore(&priv->wmm.ra_list_spinlock, flags);
}

/*
 * This function checks if a particular RA list node exists in a given TID
 * table index.
 */
int
mwifiex_is_ralist_valid(struct mwifiex_private *priv,
			struct mwifiex_ra_list_tbl *ra_list, int ptr_index)
{
	struct mwifiex_ra_list_tbl *rlist;

	list_for_each_entry(rlist, &priv->wmm.tid_tbl_ptr[ptr_index].ra_list,
			    list) {
		if (rlist == ra_list)
			return true;
	}

	return false;
}

/*
 * This function adds a packet to WMM queue.
 *
 * In disconnected state the packet is immediately dropped and the
 * packet send completion callback is called with status failure.
 *
 * Otherwise, the correct RA list node is located and the packet
 * is queued at the list tail.
 */
void
mwifiex_wmm_add_buf_txqueue(struct mwifiex_private *priv,
			    struct sk_buff *skb)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	u32 tid;
	struct mwifiex_ra_list_tbl *ra_list;
	u8 ra[ETH_ALEN], tid_down;
	unsigned long flags;
	struct list_head list_head;
	int tdls_status = TDLS_NOT_SETUP;
	struct ethhdr *eth_hdr = (struct ethhdr *)skb->data;
	struct mwifiex_txinfo *tx_info = MWIFIEX_SKB_TXCB(skb);

	memcpy(ra, eth_hdr->h_dest, ETH_ALEN);

	if (GET_BSS_ROLE(priv) == MWIFIEX_BSS_ROLE_STA &&
	    ISSUPP_TDLS_ENABLED(adapter->fw_cap_info)) {
		if (ntohs(eth_hdr->h_proto) == ETH_P_TDLS)
			dev_dbg(adapter->dev,
				"TDLS setup packet for %pM. Don't block\n", ra);
		else if (memcmp(priv->cfg_bssid, ra, ETH_ALEN))
			tdls_status = mwifiex_get_tdls_link_status(priv, ra);
	}

	if (!priv->media_connected && !mwifiex_is_skb_mgmt_frame(skb)) {
		dev_dbg(adapter->dev, "data: drop packet in disconnect\n");
		mwifiex_write_data_complete(adapter, skb, 0, -1);
		return;
	}

	tid = skb->priority;

	spin_lock_irqsave(&priv->wmm.ra_list_spinlock, flags);

	tid_down = mwifiex_wmm_downgrade_tid(priv, tid);

	/* In case of infra as we have already created the list during
	   association we just don't have to call get_queue_raptr, we will
	   have only 1 raptr for a tid in case of infra */
	if (!mwifiex_queuing_ra_based(priv) &&
	    !mwifiex_is_skb_mgmt_frame(skb)) {
		switch (tdls_status) {
		case TDLS_SETUP_COMPLETE:
			ra_list = mwifiex_wmm_get_queue_raptr(priv, tid_down,
							      ra);
			tx_info->flags |= MWIFIEX_BUF_FLAG_TDLS_PKT;
			break;
		case TDLS_SETUP_INPROGRESS:
			skb_queue_tail(&priv->tdls_txq, skb);
			spin_unlock_irqrestore(&priv->wmm.ra_list_spinlock,
					       flags);
			return;
		default:
			list_head = priv->wmm.tid_tbl_ptr[tid_down].ra_list;
			if (!list_empty(&list_head))
				ra_list = list_first_entry(
					&list_head, struct mwifiex_ra_list_tbl,
					list);
			else
				ra_list = NULL;
			break;
		}
	} else {
		memcpy(ra, skb->data, ETH_ALEN);
		if (ra[0] & 0x01 || mwifiex_is_skb_mgmt_frame(skb))
			memset(ra, 0xff, ETH_ALEN);
		ra_list = mwifiex_wmm_get_queue_raptr(priv, tid_down, ra);
	}

	if (!ra_list) {
		spin_unlock_irqrestore(&priv->wmm.ra_list_spinlock, flags);
		mwifiex_write_data_complete(adapter, skb, 0, -1);
		return;
	}

	skb_queue_tail(&ra_list->skb_head, skb);

	ra_list->ba_pkt_count++;
	ra_list->total_pkt_count++;

	if (atomic_read(&priv->wmm.highest_queued_prio) <
						priv->tos_to_tid_inv[tid_down])
		atomic_set(&priv->wmm.highest_queued_prio,
			   priv->tos_to_tid_inv[tid_down]);

	atomic_inc(&priv->wmm.tx_pkts_queued);

	spin_unlock_irqrestore(&priv->wmm.ra_list_spinlock, flags);
}

/*
 * This function processes the get WMM status command response from firmware.
 *
 * The response may contain multiple TLVs -
 *      - AC Queue status TLVs
 *      - Current WMM Parameter IE TLV
 *      - Admission Control action frame TLVs
 *
 * This function parses the TLVs and then calls further specific functions
 * to process any changes in the queue prioritize or state.
 */
int mwifiex_ret_wmm_get_status(struct mwifiex_private *priv,
			       const struct host_cmd_ds_command *resp)
{
	u8 *curr = (u8 *) &resp->params.get_wmm_status;
	uint16_t resp_len = le16_to_cpu(resp->size), tlv_len;
	bool valid = true;

	struct mwifiex_ie_types_data *tlv_hdr;
	struct mwifiex_ie_types_wmm_queue_status *tlv_wmm_qstatus;
	struct ieee_types_wmm_parameter *wmm_param_ie = NULL;
	struct mwifiex_wmm_ac_status *ac_status;

	dev_dbg(priv->adapter->dev, "info: WMM: WMM_GET_STATUS cmdresp received: %d\n",
		resp_len);

	while ((resp_len >= sizeof(tlv_hdr->header)) && valid) {
		tlv_hdr = (struct mwifiex_ie_types_data *) curr;
		tlv_len = le16_to_cpu(tlv_hdr->header.len);

		if (resp_len < tlv_len + sizeof(tlv_hdr->header))
			break;

		switch (le16_to_cpu(tlv_hdr->header.type)) {
		case TLV_TYPE_WMMQSTATUS:
			tlv_wmm_qstatus =
				(struct mwifiex_ie_types_wmm_queue_status *)
				tlv_hdr;
			dev_dbg(priv->adapter->dev,
				"info: CMD_RESP: WMM_GET_STATUS:"
				" QSTATUS TLV: %d, %d, %d\n",
				tlv_wmm_qstatus->queue_index,
				tlv_wmm_qstatus->flow_required,
				tlv_wmm_qstatus->disabled);

			ac_status = &priv->wmm.ac_status[tlv_wmm_qstatus->
							 queue_index];
			ac_status->disabled = tlv_wmm_qstatus->disabled;
			ac_status->flow_required =
						tlv_wmm_qstatus->flow_required;
			ac_status->flow_created = tlv_wmm_qstatus->flow_created;
			break;

		case WLAN_EID_VENDOR_SPECIFIC:
			/*
			 * Point the regular IEEE IE 2 bytes into the Marvell IE
			 *   and setup the IEEE IE type and length byte fields
			 */

			wmm_param_ie =
				(struct ieee_types_wmm_parameter *) (curr +
								    2);
			wmm_param_ie->vend_hdr.len = (u8) tlv_len;
			wmm_param_ie->vend_hdr.element_id =
						WLAN_EID_VENDOR_SPECIFIC;

			dev_dbg(priv->adapter->dev,
				"info: CMD_RESP: WMM_GET_STATUS:"
				" WMM Parameter Set Count: %d\n",
				wmm_param_ie->qos_info_bitmap &
				IEEE80211_WMM_IE_AP_QOSINFO_PARAM_SET_CNT_MASK);

			memcpy((u8 *) &priv->curr_bss_params.bss_descriptor.
			       wmm_ie, wmm_param_ie,
			       wmm_param_ie->vend_hdr.len + 2);

			break;

		default:
			valid = false;
			break;
		}

		curr += (tlv_len + sizeof(tlv_hdr->header));
		resp_len -= (tlv_len + sizeof(tlv_hdr->header));
	}

	mwifiex_wmm_setup_queue_priorities(priv, wmm_param_ie);
	mwifiex_wmm_setup_ac_downgrade(priv);

	return 0;
}

/*
 * Callback handler from the command module to allow insertion of a WMM TLV.
 *
 * If the BSS we are associating to supports WMM, this function adds the
 * required WMM Information IE to the association request command buffer in
 * the form of a Marvell extended IEEE IE.
 */
u32
mwifiex_wmm_process_association_req(struct mwifiex_private *priv,
				    u8 **assoc_buf,
				    struct ieee_types_wmm_parameter *wmm_ie,
				    struct ieee80211_ht_cap *ht_cap)
{
	struct mwifiex_ie_types_wmm_param_set *wmm_tlv;
	u32 ret_len = 0;

	/* Null checks */
	if (!assoc_buf)
		return 0;
	if (!(*assoc_buf))
		return 0;

	if (!wmm_ie)
		return 0;

	dev_dbg(priv->adapter->dev,
		"info: WMM: process assoc req: bss->wmm_ie=%#x\n",
		wmm_ie->vend_hdr.element_id);

	if ((priv->wmm_required ||
	     (ht_cap && (priv->adapter->config_bands & BAND_GN ||
	     priv->adapter->config_bands & BAND_AN))) &&
	    wmm_ie->vend_hdr.element_id == WLAN_EID_VENDOR_SPECIFIC) {
		wmm_tlv = (struct mwifiex_ie_types_wmm_param_set *) *assoc_buf;
		wmm_tlv->header.type = cpu_to_le16((u16) wmm_info_ie[0]);
		wmm_tlv->header.len = cpu_to_le16((u16) wmm_info_ie[1]);
		memcpy(wmm_tlv->wmm_ie, &wmm_info_ie[2],
		       le16_to_cpu(wmm_tlv->header.len));
		if (wmm_ie->qos_info_bitmap & IEEE80211_WMM_IE_AP_QOSINFO_UAPSD)
			memcpy((u8 *) (wmm_tlv->wmm_ie
				       + le16_to_cpu(wmm_tlv->header.len)
				       - sizeof(priv->wmm_qosinfo)),
			       &priv->wmm_qosinfo, sizeof(priv->wmm_qosinfo));

		ret_len = sizeof(wmm_tlv->header)
			  + le16_to_cpu(wmm_tlv->header.len);

		*assoc_buf += ret_len;
	}

	return ret_len;
}

/*
 * This function computes the time delay in the driver queues for a
 * given packet.
 *
 * When the packet is received at the OS/Driver interface, the current
 * time is set in the packet structure. The difference between the present
 * time and that received time is computed in this function and limited
 * based on pre-compiled limits in the driver.
 */
u8
mwifiex_wmm_compute_drv_pkt_delay(struct mwifiex_private *priv,
				  const struct sk_buff *skb)
{
	u32 queue_delay = ktime_to_ms(net_timedelta(skb->tstamp));
	u8 ret_val;

	/*
	 * Queue delay is passed as a uint8 in units of 2ms (ms shifted
	 *  by 1). Min value (other than 0) is therefore 2ms, max is 510ms.
	 *
	 * Pass max value if queue_delay is beyond the uint8 range
	 */
	ret_val = (u8) (min(queue_delay, priv->wmm.drv_pkt_delay_max) >> 1);

	dev_dbg(priv->adapter->dev, "data: WMM: Pkt Delay: %d ms,"
				" %d ms sent to FW\n", queue_delay, ret_val);

	return ret_val;
}

/*
 * This function retrieves the highest priority RA list table pointer.
 */
static struct mwifiex_ra_list_tbl *
mwifiex_wmm_get_highest_priolist_ptr(struct mwifiex_adapter *adapter,
				     struct mwifiex_private **priv, int *tid)
{
	struct mwifiex_private *priv_tmp;
	struct mwifiex_ra_list_tbl *ptr;
	struct mwifiex_tid_tbl *tid_ptr;
	atomic_t *hqp;
	unsigned long flags_bss, flags_ra;
	int i, j;

	/* check the BSS with highest priority first */
	for (j = adapter->priv_num - 1; j >= 0; --j) {
		spin_lock_irqsave(&adapter->bss_prio_tbl[j].bss_prio_lock,
				  flags_bss);

		/* iterate over BSS with the equal priority */
		list_for_each_entry(adapter->bss_prio_tbl[j].bss_prio_cur,
				    &adapter->bss_prio_tbl[j].bss_prio_head,
				    list) {

			priv_tmp = adapter->bss_prio_tbl[j].bss_prio_cur->priv;

			if (atomic_read(&priv_tmp->wmm.tx_pkts_queued) == 0)
				continue;

			/* iterate over the WMM queues of the BSS */
			hqp = &priv_tmp->wmm.highest_queued_prio;
			for (i = atomic_read(hqp); i >= LOW_PRIO_TID; --i) {

				spin_lock_irqsave(&priv_tmp->wmm.
						  ra_list_spinlock, flags_ra);

				tid_ptr = &(priv_tmp)->wmm.
					tid_tbl_ptr[tos_to_tid[i]];

				/* iterate over receiver addresses */
				list_for_each_entry(ptr, &tid_ptr->ra_list,
						    list) {

					if (!skb_queue_empty(&ptr->skb_head))
						/* holds both locks */
						goto found;
				}

				spin_unlock_irqrestore(&priv_tmp->wmm.
						       ra_list_spinlock,
						       flags_ra);
			}
		}

		spin_unlock_irqrestore(&adapter->bss_prio_tbl[j].bss_prio_lock,
				       flags_bss);
	}

	return NULL;

found:
	/* holds bss_prio_lock / ra_list_spinlock */
	if (atomic_read(hqp) > i)
		atomic_set(hqp, i);
	spin_unlock_irqrestore(&priv_tmp->wmm.ra_list_spinlock, flags_ra);
	spin_unlock_irqrestore(&adapter->bss_prio_tbl[j].bss_prio_lock,
			       flags_bss);

	*priv = priv_tmp;
	*tid = tos_to_tid[i];

	return ptr;
}

/* This functions rotates ra and bss lists so packets are picked round robin.
 *
 * After a packet is successfully transmitted, rotate the ra list, so the ra
 * next to the one transmitted, will come first in the list. This way we pick
 * the ra' in a round robin fashion. Same applies to bss nodes of equal
 * priority.
 *
 * Function also increments wmm.packets_out counter.
 */
void mwifiex_rotate_priolists(struct mwifiex_private *priv,
				 struct mwifiex_ra_list_tbl *ra,
				 int tid)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	struct mwifiex_bss_prio_tbl *tbl = adapter->bss_prio_tbl;
	struct mwifiex_tid_tbl *tid_ptr = &priv->wmm.tid_tbl_ptr[tid];
	unsigned long flags;

	spin_lock_irqsave(&tbl[priv->bss_priority].bss_prio_lock, flags);
	/*
	 * dirty trick: we remove 'head' temporarily and reinsert it after
	 * curr bss node. imagine list to stay fixed while head is moved
	 */
	list_move(&tbl[priv->bss_priority].bss_prio_head,
		  &tbl[priv->bss_priority].bss_prio_cur->list);
	spin_unlock_irqrestore(&tbl[priv->bss_priority].bss_prio_lock, flags);

	spin_lock_irqsave(&priv->wmm.ra_list_spinlock, flags);
	if (mwifiex_is_ralist_valid(priv, ra, tid)) {
		priv->wmm.packets_out[tid]++;
		/* same as above */
		list_move(&tid_ptr->ra_list, &ra->list);
	}
	spin_unlock_irqrestore(&priv->wmm.ra_list_spinlock, flags);
}

/*
 * This function checks if 11n aggregation is possible.
 */
static int
mwifiex_is_11n_aggragation_possible(struct mwifiex_private *priv,
				    struct mwifiex_ra_list_tbl *ptr,
				    int max_buf_size)
{
	int count = 0, total_size = 0;
	struct sk_buff *skb, *tmp;
	int max_amsdu_size;

	if (priv->bss_role == MWIFIEX_BSS_ROLE_UAP && priv->ap_11n_enabled &&
	    ptr->is_11n_enabled)
		max_amsdu_size = min_t(int, ptr->max_amsdu, max_buf_size);
	else
		max_amsdu_size = max_buf_size;

	skb_queue_walk_safe(&ptr->skb_head, skb, tmp) {
		total_size += skb->len;
		if (total_size >= max_amsdu_size)
			break;
		if (++count >= MIN_NUM_AMSDU)
			return true;
	}

	return false;
}

/*
 * This function sends a single packet to firmware for transmission.
 */
static void
mwifiex_send_single_packet(struct mwifiex_private *priv,
			   struct mwifiex_ra_list_tbl *ptr, int ptr_index,
			   unsigned long ra_list_flags)
			   __releases(&priv->wmm.ra_list_spinlock)
{
	struct sk_buff *skb, *skb_next;
	struct mwifiex_tx_param tx_param;
	struct mwifiex_adapter *adapter = priv->adapter;
	struct mwifiex_txinfo *tx_info;

	if (skb_queue_empty(&ptr->skb_head)) {
		spin_unlock_irqrestore(&priv->wmm.ra_list_spinlock,
				       ra_list_flags);
		dev_dbg(adapter->dev, "data: nothing to send\n");
		return;
	}

	skb = skb_dequeue(&ptr->skb_head);

	tx_info = MWIFIEX_SKB_TXCB(skb);
	dev_dbg(adapter->dev, "data: dequeuing the packet %p %p\n", ptr, skb);

	ptr->total_pkt_count--;

	if (!skb_queue_empty(&ptr->skb_head))
		skb_next = skb_peek(&ptr->skb_head);
	else
		skb_next = NULL;

	spin_unlock_irqrestore(&priv->wmm.ra_list_spinlock, ra_list_flags);

	tx_param.next_pkt_len = ((skb_next) ? skb_next->len +
				sizeof(struct txpd) : 0);

	if (mwifiex_process_tx(priv, skb, &tx_param) == -EBUSY) {
		/* Queue the packet back at the head */
		spin_lock_irqsave(&priv->wmm.ra_list_spinlock, ra_list_flags);

		if (!mwifiex_is_ralist_valid(priv, ptr, ptr_index)) {
			spin_unlock_irqrestore(&priv->wmm.ra_list_spinlock,
					       ra_list_flags);
			mwifiex_write_data_complete(adapter, skb, 0, -1);
			return;
		}

		skb_queue_tail(&ptr->skb_head, skb);

		ptr->total_pkt_count++;
		ptr->ba_pkt_count++;
		tx_info->flags |= MWIFIEX_BUF_FLAG_REQUEUED_PKT;
		spin_unlock_irqrestore(&priv->wmm.ra_list_spinlock,
				       ra_list_flags);
	} else {
		mwifiex_rotate_priolists(priv, ptr, ptr_index);
		atomic_dec(&priv->wmm.tx_pkts_queued);
	}
}

/*
 * This function checks if the first packet in the given RA list
 * is already processed or not.
 */
static int
mwifiex_is_ptr_processed(struct mwifiex_private *priv,
			 struct mwifiex_ra_list_tbl *ptr)
{
	struct sk_buff *skb;
	struct mwifiex_txinfo *tx_info;

	if (skb_queue_empty(&ptr->skb_head))
		return false;

	skb = skb_peek(&ptr->skb_head);

	tx_info = MWIFIEX_SKB_TXCB(skb);
	if (tx_info->flags & MWIFIEX_BUF_FLAG_REQUEUED_PKT)
		return true;

	return false;
}

/*
 * This function sends a single processed packet to firmware for
 * transmission.
 */
static void
mwifiex_send_processed_packet(struct mwifiex_private *priv,
			      struct mwifiex_ra_list_tbl *ptr, int ptr_index,
			      unsigned long ra_list_flags)
				__releases(&priv->wmm.ra_list_spinlock)
{
	struct mwifiex_tx_param tx_param;
	struct mwifiex_adapter *adapter = priv->adapter;
	int ret = -1;
	struct sk_buff *skb, *skb_next;
	struct mwifiex_txinfo *tx_info;

	if (skb_queue_empty(&ptr->skb_head)) {
		spin_unlock_irqrestore(&priv->wmm.ra_list_spinlock,
				       ra_list_flags);
		return;
	}

	skb = skb_dequeue(&ptr->skb_head);

	if (!skb_queue_empty(&ptr->skb_head))
		skb_next = skb_peek(&ptr->skb_head);
	else
		skb_next = NULL;

	tx_info = MWIFIEX_SKB_TXCB(skb);

	spin_unlock_irqrestore(&priv->wmm.ra_list_spinlock, ra_list_flags);

	if (adapter->iface_type == MWIFIEX_USB) {
		adapter->data_sent = true;
		ret = adapter->if_ops.host_to_card(adapter, MWIFIEX_USB_EP_DATA,
						   skb, NULL);
	} else {
		tx_param.next_pkt_len =
			((skb_next) ? skb_next->len +
			 sizeof(struct txpd) : 0);
		ret = adapter->if_ops.host_to_card(adapter, MWIFIEX_TYPE_DATA,
						   skb, &tx_param);
	}

	switch (ret) {
	case -EBUSY:
		dev_dbg(adapter->dev, "data: -EBUSY is returned\n");
		spin_lock_irqsave(&priv->wmm.ra_list_spinlock, ra_list_flags);

		if (!mwifiex_is_ralist_valid(priv, ptr, ptr_index)) {
			spin_unlock_irqrestore(&priv->wmm.ra_list_spinlock,
					       ra_list_flags);
			mwifiex_write_data_complete(adapter, skb, 0, -1);
			return;
		}

		skb_queue_tail(&ptr->skb_head, skb);

		tx_info->flags |= MWIFIEX_BUF_FLAG_REQUEUED_PKT;
		spin_unlock_irqrestore(&priv->wmm.ra_list_spinlock,
				       ra_list_flags);
		break;
	case -1:
		if (adapter->iface_type != MWIFIEX_PCIE)
			adapter->data_sent = false;
		dev_err(adapter->dev, "host_to_card failed: %#x\n", ret);
		adapter->dbg.num_tx_host_to_card_failure++;
		mwifiex_write_data_complete(adapter, skb, 0, ret);
		break;
	case -EINPROGRESS:
		if (adapter->iface_type != MWIFIEX_PCIE)
			adapter->data_sent = false;
	default:
		break;
	}
	if (ret != -EBUSY) {
		mwifiex_rotate_priolists(priv, ptr, ptr_index);
		atomic_dec(&priv->wmm.tx_pkts_queued);
	}
}

/*
 * This function dequeues a packet from the highest priority list
 * and transmits it.
 */
static int
mwifiex_dequeue_tx_packet(struct mwifiex_adapter *adapter)
{
	struct mwifiex_ra_list_tbl *ptr;
	struct mwifiex_private *priv = NULL;
	int ptr_index = 0;
	u8 ra[ETH_ALEN];
	int tid_del = 0, tid = 0;
	unsigned long flags;

	ptr = mwifiex_wmm_get_highest_priolist_ptr(adapter, &priv, &ptr_index);
	if (!ptr)
		return -1;

	tid = mwifiex_get_tid(ptr);

	dev_dbg(adapter->dev, "data: tid=%d\n", tid);

	spin_lock_irqsave(&priv->wmm.ra_list_spinlock, flags);
	if (!mwifiex_is_ralist_valid(priv, ptr, ptr_index)) {
		spin_unlock_irqrestore(&priv->wmm.ra_list_spinlock, flags);
		return -1;
	}

	if (mwifiex_is_ptr_processed(priv, ptr)) {
		mwifiex_send_processed_packet(priv, ptr, ptr_index, flags);
		/* ra_list_spinlock has been freed in
		   mwifiex_send_processed_packet() */
		return 0;
	}

	if (!ptr->is_11n_enabled ||
	    mwifiex_is_ba_stream_setup(priv, ptr, tid) ||
	    priv->wps.session_enable) {
		if (ptr->is_11n_enabled &&
		    mwifiex_is_ba_stream_setup(priv, ptr, tid) &&
		    mwifiex_is_amsdu_in_ampdu_allowed(priv, ptr, tid) &&
		    mwifiex_is_amsdu_allowed(priv, tid) &&
		    mwifiex_is_11n_aggragation_possible(priv, ptr,
							adapter->tx_buf_size))
			mwifiex_11n_aggregate_pkt(priv, ptr, ptr_index, flags);
			/* ra_list_spinlock has been freed in
			 * mwifiex_11n_aggregate_pkt()
			 */
		else
			mwifiex_send_single_packet(priv, ptr, ptr_index, flags);
			/* ra_list_spinlock has been freed in
			 * mwifiex_send_single_packet()
			 */
	} else {
		if (mwifiex_is_ampdu_allowed(priv, ptr, tid) &&
		    ptr->ba_pkt_count > ptr->ba_packet_thr) {
			if (mwifiex_space_avail_for_new_ba_stream(adapter)) {
				mwifiex_create_ba_tbl(priv, ptr->ra, tid,
						      BA_SETUP_INPROGRESS);
				mwifiex_send_addba(priv, tid, ptr->ra);
			} else if (mwifiex_find_stream_to_delete
				   (priv, tid, &tid_del, ra)) {
				mwifiex_create_ba_tbl(priv, ptr->ra, tid,
						      BA_SETUP_INPROGRESS);
				mwifiex_send_delba(priv, tid_del, ra, 1);
			}
		}
		if (mwifiex_is_amsdu_allowed(priv, tid) &&
		    mwifiex_is_11n_aggragation_possible(priv, ptr,
							adapter->tx_buf_size))
			mwifiex_11n_aggregate_pkt(priv, ptr, ptr_index, flags);
			/* ra_list_spinlock has been freed in
			   mwifiex_11n_aggregate_pkt() */
		else
			mwifiex_send_single_packet(priv, ptr, ptr_index, flags);
			/* ra_list_spinlock has been freed in
			   mwifiex_send_single_packet() */
	}
	return 0;
}

/*
 * This function transmits the highest priority packet awaiting in the
 * WMM Queues.
 */
void
mwifiex_wmm_process_tx(struct mwifiex_adapter *adapter)
{
	do {
		/* Check if busy */
		if (adapter->data_sent || adapter->tx_lock_flag)
			break;

		if (mwifiex_dequeue_tx_packet(adapter))
			break;
	} while (!mwifiex_wmm_lists_empty(adapter));
}
