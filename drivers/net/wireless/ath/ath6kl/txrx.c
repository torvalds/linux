/*
 * Copyright (c) 2004-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2012 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "core.h"
#include "debug.h"
#include "htc-ops.h"

/*
 * tid - tid_mux0..tid_mux3
 * aid - tid_mux4..tid_mux7
 */
#define ATH6KL_TID_MASK 0xf
#define ATH6KL_AID_SHIFT 4

static inline u8 ath6kl_get_tid(u8 tid_mux)
{
	return tid_mux & ATH6KL_TID_MASK;
}

static inline u8 ath6kl_get_aid(u8 tid_mux)
{
	return tid_mux >> ATH6KL_AID_SHIFT;
}

static u8 ath6kl_ibss_map_epid(struct sk_buff *skb, struct net_device *dev,
			       u32 *map_no)
{
	struct ath6kl *ar = ath6kl_priv(dev);
	struct ethhdr *eth_hdr;
	u32 i, ep_map = -1;
	u8 *datap;

	*map_no = 0;
	datap = skb->data;
	eth_hdr = (struct ethhdr *) (datap + sizeof(struct wmi_data_hdr));

	if (is_multicast_ether_addr(eth_hdr->h_dest))
		return ENDPOINT_2;

	for (i = 0; i < ar->node_num; i++) {
		if (memcmp(eth_hdr->h_dest, ar->node_map[i].mac_addr,
			   ETH_ALEN) == 0) {
			*map_no = i + 1;
			ar->node_map[i].tx_pend++;
			return ar->node_map[i].ep_id;
		}

		if ((ep_map == -1) && !ar->node_map[i].tx_pend)
			ep_map = i;
	}

	if (ep_map == -1) {
		ep_map = ar->node_num;
		ar->node_num++;
		if (ar->node_num > MAX_NODE_NUM)
			return ENDPOINT_UNUSED;
	}

	memcpy(ar->node_map[ep_map].mac_addr, eth_hdr->h_dest, ETH_ALEN);

	for (i = ENDPOINT_2; i <= ENDPOINT_5; i++) {
		if (!ar->tx_pending[i]) {
			ar->node_map[ep_map].ep_id = i;
			break;
		}

		/*
		 * No free endpoint is available, start redistribution on
		 * the inuse endpoints.
		 */
		if (i == ENDPOINT_5) {
			ar->node_map[ep_map].ep_id = ar->next_ep_id;
			ar->next_ep_id++;
			if (ar->next_ep_id > ENDPOINT_5)
				ar->next_ep_id = ENDPOINT_2;
		}
	}

	*map_no = ep_map + 1;
	ar->node_map[ep_map].tx_pend++;

	return ar->node_map[ep_map].ep_id;
}

static bool ath6kl_process_uapsdq(struct ath6kl_sta *conn,
				struct ath6kl_vif *vif,
				struct sk_buff *skb,
				u32 *flags)
{
	struct ath6kl *ar = vif->ar;
	bool is_apsdq_empty = false;
	struct ethhdr *datap = (struct ethhdr *) skb->data;
	u8 up = 0, traffic_class, *ip_hdr;
	u16 ether_type;
	struct ath6kl_llc_snap_hdr *llc_hdr;

	if (conn->sta_flags & STA_PS_APSD_TRIGGER) {
		/*
		 * This tx is because of a uAPSD trigger, determine
		 * more and EOSP bit. Set EOSP if queue is empty
		 * or sufficient frames are delivered for this trigger.
		 */
		spin_lock_bh(&conn->psq_lock);
		if (!skb_queue_empty(&conn->apsdq))
			*flags |= WMI_DATA_HDR_FLAGS_MORE;
		else if (conn->sta_flags & STA_PS_APSD_EOSP)
			*flags |= WMI_DATA_HDR_FLAGS_EOSP;
		*flags |= WMI_DATA_HDR_FLAGS_UAPSD;
		spin_unlock_bh(&conn->psq_lock);
		return false;
	} else if (!conn->apsd_info)
		return false;

	if (test_bit(WMM_ENABLED, &vif->flags)) {
		ether_type = be16_to_cpu(datap->h_proto);
		if (is_ethertype(ether_type)) {
			/* packet is in DIX format  */
			ip_hdr = (u8 *)(datap + 1);
		} else {
			/* packet is in 802.3 format */
			llc_hdr = (struct ath6kl_llc_snap_hdr *)
							(datap + 1);
			ether_type = be16_to_cpu(llc_hdr->eth_type);
			ip_hdr = (u8 *)(llc_hdr + 1);
		}

		if (ether_type == IP_ETHERTYPE)
			up = ath6kl_wmi_determine_user_priority(
							ip_hdr, 0);
	}

	traffic_class = ath6kl_wmi_get_traffic_class(up);

	if ((conn->apsd_info & (1 << traffic_class)) == 0)
		return false;

	/* Queue the frames if the STA is sleeping */
	spin_lock_bh(&conn->psq_lock);
	is_apsdq_empty = skb_queue_empty(&conn->apsdq);
	skb_queue_tail(&conn->apsdq, skb);
	spin_unlock_bh(&conn->psq_lock);

	/*
	 * If this is the first pkt getting queued
	 * for this STA, update the PVB for this STA
	 */
	if (is_apsdq_empty) {
		ath6kl_wmi_set_apsd_bfrd_traf(ar->wmi,
					      vif->fw_vif_idx,
					      conn->aid, 1, 0);
	}
	*flags |= WMI_DATA_HDR_FLAGS_UAPSD;

	return true;
}

static bool ath6kl_process_psq(struct ath6kl_sta *conn,
				struct ath6kl_vif *vif,
				struct sk_buff *skb,
				u32 *flags)
{
	bool is_psq_empty = false;
	struct ath6kl *ar = vif->ar;

	if (conn->sta_flags & STA_PS_POLLED) {
		spin_lock_bh(&conn->psq_lock);
		if (!skb_queue_empty(&conn->psq))
			*flags |= WMI_DATA_HDR_FLAGS_MORE;
		spin_unlock_bh(&conn->psq_lock);
		return false;
	}

	/* Queue the frames if the STA is sleeping */
	spin_lock_bh(&conn->psq_lock);
	is_psq_empty = skb_queue_empty(&conn->psq);
	skb_queue_tail(&conn->psq, skb);
	spin_unlock_bh(&conn->psq_lock);

	/*
	 * If this is the first pkt getting queued
	 * for this STA, update the PVB for this
	 * STA.
	 */
	if (is_psq_empty)
		ath6kl_wmi_set_pvb_cmd(ar->wmi,
				       vif->fw_vif_idx,
				       conn->aid, 1);
	return true;
}

static bool ath6kl_powersave_ap(struct ath6kl_vif *vif, struct sk_buff *skb,
				u32 *flags)
{
	struct ethhdr *datap = (struct ethhdr *) skb->data;
	struct ath6kl_sta *conn = NULL;
	bool ps_queued = false;
	struct ath6kl *ar = vif->ar;

	if (is_multicast_ether_addr(datap->h_dest)) {
		u8 ctr = 0;
		bool q_mcast = false;

		for (ctr = 0; ctr < AP_MAX_NUM_STA; ctr++) {
			if (ar->sta_list[ctr].sta_flags & STA_PS_SLEEP) {
				q_mcast = true;
				break;
			}
		}

		if (q_mcast) {
			/*
			 * If this transmit is not because of a Dtim Expiry
			 * q it.
			 */
			if (!test_bit(DTIM_EXPIRED, &vif->flags)) {
				bool is_mcastq_empty = false;

				spin_lock_bh(&ar->mcastpsq_lock);
				is_mcastq_empty =
					skb_queue_empty(&ar->mcastpsq);
				skb_queue_tail(&ar->mcastpsq, skb);
				spin_unlock_bh(&ar->mcastpsq_lock);

				/*
				 * If this is the first Mcast pkt getting
				 * queued indicate to the target to set the
				 * BitmapControl LSB of the TIM IE.
				 */
				if (is_mcastq_empty)
					ath6kl_wmi_set_pvb_cmd(ar->wmi,
							       vif->fw_vif_idx,
							       MCAST_AID, 1);

				ps_queued = true;
			} else {
				/*
				 * This transmit is because of Dtim expiry.
				 * Determine if MoreData bit has to be set.
				 */
				spin_lock_bh(&ar->mcastpsq_lock);
				if (!skb_queue_empty(&ar->mcastpsq))
					*flags |= WMI_DATA_HDR_FLAGS_MORE;
				spin_unlock_bh(&ar->mcastpsq_lock);
			}
		}
	} else {
		conn = ath6kl_find_sta(vif, datap->h_dest);
		if (!conn) {
			dev_kfree_skb(skb);

			/* Inform the caller that the skb is consumed */
			return true;
		}

		if (conn->sta_flags & STA_PS_SLEEP) {
			ps_queued = ath6kl_process_uapsdq(conn,
						vif, skb, flags);
			if (!(*flags & WMI_DATA_HDR_FLAGS_UAPSD))
				ps_queued = ath6kl_process_psq(conn,
						vif, skb, flags);
		}
	}
	return ps_queued;
}

/* Tx functions */

int ath6kl_control_tx(void *devt, struct sk_buff *skb,
		      enum htc_endpoint_id eid)
{
	struct ath6kl *ar = devt;
	int status = 0;
	struct ath6kl_cookie *cookie = NULL;

	if (WARN_ON_ONCE(ar->state == ATH6KL_STATE_WOW)) {
		dev_kfree_skb(skb);
		return -EACCES;
	}

	if (WARN_ON_ONCE(eid == ENDPOINT_UNUSED ||
			 eid >= ENDPOINT_MAX)) {
		status = -EINVAL;
		goto fail_ctrl_tx;
	}

	spin_lock_bh(&ar->lock);

	ath6kl_dbg(ATH6KL_DBG_WLAN_TX,
		   "%s: skb=0x%p, len=0x%x eid =%d\n", __func__,
		   skb, skb->len, eid);

	if (test_bit(WMI_CTRL_EP_FULL, &ar->flag) && (eid == ar->ctrl_ep)) {
		/*
		 * Control endpoint is full, don't allocate resources, we
		 * are just going to drop this packet.
		 */
		cookie = NULL;
		ath6kl_err("wmi ctrl ep full, dropping pkt : 0x%p, len:%d\n",
			   skb, skb->len);
	} else
		cookie = ath6kl_alloc_cookie(ar);

	if (cookie == NULL) {
		spin_unlock_bh(&ar->lock);
		status = -ENOMEM;
		goto fail_ctrl_tx;
	}

	ar->tx_pending[eid]++;

	if (eid != ar->ctrl_ep)
		ar->total_tx_data_pend++;

	spin_unlock_bh(&ar->lock);

	cookie->skb = skb;
	cookie->map_no = 0;
	set_htc_pkt_info(&cookie->htc_pkt, cookie, skb->data, skb->len,
			 eid, ATH6KL_CONTROL_PKT_TAG);
	cookie->htc_pkt.skb = skb;

	/*
	 * This interface is asynchronous, if there is an error, cleanup
	 * will happen in the TX completion callback.
	 */
	ath6kl_htc_tx(ar->htc_target, &cookie->htc_pkt);

	return 0;

fail_ctrl_tx:
	dev_kfree_skb(skb);
	return status;
}

int ath6kl_data_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct ath6kl *ar = ath6kl_priv(dev);
	struct ath6kl_cookie *cookie = NULL;
	enum htc_endpoint_id eid = ENDPOINT_UNUSED;
	struct ath6kl_vif *vif = netdev_priv(dev);
	u32 map_no = 0;
	u16 htc_tag = ATH6KL_DATA_PKT_TAG;
	u8 ac = 99 ; /* initialize to unmapped ac */
	bool chk_adhoc_ps_mapping = false;
	int ret;
	struct wmi_tx_meta_v2 meta_v2;
	void *meta;
	u8 csum_start = 0, csum_dest = 0, csum = skb->ip_summed;
	u8 meta_ver = 0;
	u32 flags = 0;

	ath6kl_dbg(ATH6KL_DBG_WLAN_TX,
		   "%s: skb=0x%p, data=0x%p, len=0x%x\n", __func__,
		   skb, skb->data, skb->len);

	/* If target is not associated */
	if (!test_bit(CONNECTED, &vif->flags))
		goto fail_tx;

	if (WARN_ON_ONCE(ar->state != ATH6KL_STATE_ON))
		goto fail_tx;

	if (!test_bit(WMI_READY, &ar->flag))
		goto fail_tx;

	/* AP mode Power saving processing */
	if (vif->nw_type == AP_NETWORK) {
		if (ath6kl_powersave_ap(vif, skb, &flags))
			return 0;
	}

	if (test_bit(WMI_ENABLED, &ar->flag)) {
		if ((dev->features & NETIF_F_IP_CSUM) &&
		    (csum == CHECKSUM_PARTIAL)) {
			csum_start = skb->csum_start -
					(skb_network_header(skb) - skb->head) +
					sizeof(struct ath6kl_llc_snap_hdr);
			csum_dest = skb->csum_offset + csum_start;
		}

		if (skb_headroom(skb) < dev->needed_headroom) {
			struct sk_buff *tmp_skb = skb;

			skb = skb_realloc_headroom(skb, dev->needed_headroom);
			kfree_skb(tmp_skb);
			if (skb == NULL) {
				vif->net_stats.tx_dropped++;
				return 0;
			}
		}

		if (ath6kl_wmi_dix_2_dot3(ar->wmi, skb)) {
			ath6kl_err("ath6kl_wmi_dix_2_dot3 failed\n");
			goto fail_tx;
		}

		if ((dev->features & NETIF_F_IP_CSUM) &&
		    (csum == CHECKSUM_PARTIAL)) {
			meta_v2.csum_start = csum_start;
			meta_v2.csum_dest = csum_dest;

			/* instruct target to calculate checksum */
			meta_v2.csum_flags = WMI_META_V2_FLAG_CSUM_OFFLOAD;
			meta_ver = WMI_META_VERSION_2;
			meta = &meta_v2;
		} else {
			meta_ver = 0;
			meta = NULL;
		}

		ret = ath6kl_wmi_data_hdr_add(ar->wmi, skb,
				DATA_MSGTYPE, flags, 0,
				meta_ver,
				meta, vif->fw_vif_idx);

		if (ret) {
			ath6kl_warn("failed to add wmi data header:%d\n"
				, ret);
			goto fail_tx;
		}

		if ((vif->nw_type == ADHOC_NETWORK) &&
		    ar->ibss_ps_enable && test_bit(CONNECTED, &vif->flags))
			chk_adhoc_ps_mapping = true;
		else {
			/* get the stream mapping */
			ret = ath6kl_wmi_implicit_create_pstream(ar->wmi,
				    vif->fw_vif_idx, skb,
				    0, test_bit(WMM_ENABLED, &vif->flags), &ac);
			if (ret)
				goto fail_tx;
		}
	} else
		goto fail_tx;

	spin_lock_bh(&ar->lock);

	if (chk_adhoc_ps_mapping)
		eid = ath6kl_ibss_map_epid(skb, dev, &map_no);
	else
		eid = ar->ac2ep_map[ac];

	if (eid == 0 || eid == ENDPOINT_UNUSED) {
		ath6kl_err("eid %d is not mapped!\n", eid);
		spin_unlock_bh(&ar->lock);
		goto fail_tx;
	}

	/* allocate resource for this packet */
	cookie = ath6kl_alloc_cookie(ar);

	if (!cookie) {
		spin_unlock_bh(&ar->lock);
		goto fail_tx;
	}

	/* update counts while the lock is held */
	ar->tx_pending[eid]++;
	ar->total_tx_data_pend++;

	spin_unlock_bh(&ar->lock);

	if (!IS_ALIGNED((unsigned long) skb->data - HTC_HDR_LENGTH, 4) &&
	    skb_cloned(skb)) {
		/*
		 * We will touch (move the buffer data to align it. Since the
		 * skb buffer is cloned and not only the header is changed, we
		 * have to copy it to allow the changes. Since we are copying
		 * the data here, we may as well align it by reserving suitable
		 * headroom to avoid the memmove in ath6kl_htc_tx_buf_align().
		 */
		struct sk_buff *nskb;

		nskb = skb_copy_expand(skb, HTC_HDR_LENGTH, 0, GFP_ATOMIC);
		if (nskb == NULL)
			goto fail_tx;
		kfree_skb(skb);
		skb = nskb;
	}

	cookie->skb = skb;
	cookie->map_no = map_no;
	set_htc_pkt_info(&cookie->htc_pkt, cookie, skb->data, skb->len,
			 eid, htc_tag);
	cookie->htc_pkt.skb = skb;

	ath6kl_dbg_dump(ATH6KL_DBG_RAW_BYTES, __func__, "tx ",
			skb->data, skb->len);

	/*
	 * HTC interface is asynchronous, if this fails, cleanup will
	 * happen in the ath6kl_tx_complete callback.
	 */
	ath6kl_htc_tx(ar->htc_target, &cookie->htc_pkt);

	return 0;

fail_tx:
	dev_kfree_skb(skb);

	vif->net_stats.tx_dropped++;
	vif->net_stats.tx_aborted_errors++;

	return 0;
}

/* indicate tx activity or inactivity on a WMI stream */
void ath6kl_indicate_tx_activity(void *devt, u8 traffic_class, bool active)
{
	struct ath6kl *ar = devt;
	enum htc_endpoint_id eid;
	int i;

	eid = ar->ac2ep_map[traffic_class];

	if (!test_bit(WMI_ENABLED, &ar->flag))
		goto notify_htc;

	spin_lock_bh(&ar->lock);

	ar->ac_stream_active[traffic_class] = active;

	if (active) {
		/*
		 * Keep track of the active stream with the highest
		 * priority.
		 */
		if (ar->ac_stream_pri_map[traffic_class] >
		    ar->hiac_stream_active_pri)
			/* set the new highest active priority */
			ar->hiac_stream_active_pri =
					ar->ac_stream_pri_map[traffic_class];

	} else {
		/*
		 * We may have to search for the next active stream
		 * that is the highest priority.
		 */
		if (ar->hiac_stream_active_pri ==
			ar->ac_stream_pri_map[traffic_class]) {
			/*
			 * The highest priority stream just went inactive
			 * reset and search for the "next" highest "active"
			 * priority stream.
			 */
			ar->hiac_stream_active_pri = 0;

			for (i = 0; i < WMM_NUM_AC; i++) {
				if (ar->ac_stream_active[i] &&
				    (ar->ac_stream_pri_map[i] >
				     ar->hiac_stream_active_pri))
					/*
					 * Set the new highest active
					 * priority.
					 */
					ar->hiac_stream_active_pri =
						ar->ac_stream_pri_map[i];
			}
		}
	}

	spin_unlock_bh(&ar->lock);

notify_htc:
	/* notify HTC, this may cause credit distribution changes */
	ath6kl_htc_activity_changed(ar->htc_target, eid, active);
}

enum htc_send_full_action ath6kl_tx_queue_full(struct htc_target *target,
					       struct htc_packet *packet)
{
	struct ath6kl *ar = target->dev->ar;
	struct ath6kl_vif *vif;
	enum htc_endpoint_id endpoint = packet->endpoint;
	enum htc_send_full_action action = HTC_SEND_FULL_KEEP;

	if (endpoint == ar->ctrl_ep) {
		/*
		 * Under normal WMI if this is getting full, then something
		 * is running rampant the host should not be exhausting the
		 * WMI queue with too many commands the only exception to
		 * this is during testing using endpointping.
		 */
		set_bit(WMI_CTRL_EP_FULL, &ar->flag);
		ath6kl_err("wmi ctrl ep is full\n");
		ath6kl_recovery_err_notify(ar, ATH6KL_FW_EP_FULL);
		return action;
	}

	if (packet->info.tx.tag == ATH6KL_CONTROL_PKT_TAG)
		return action;

	/*
	 * The last MAX_HI_COOKIE_NUM "batch" of cookies are reserved for
	 * the highest active stream.
	 */
	if (ar->ac_stream_pri_map[ar->ep2ac_map[endpoint]] <
	    ar->hiac_stream_active_pri &&
	    ar->cookie_count <=
			target->endpoint[endpoint].tx_drop_packet_threshold)
		/*
		 * Give preference to the highest priority stream by
		 * dropping the packets which overflowed.
		 */
		action = HTC_SEND_FULL_DROP;

	/* FIXME: Locking */
	spin_lock_bh(&ar->list_lock);
	list_for_each_entry(vif, &ar->vif_list, list) {
		if (vif->nw_type == ADHOC_NETWORK ||
		    action != HTC_SEND_FULL_DROP) {
			spin_unlock_bh(&ar->list_lock);

			set_bit(NETQ_STOPPED, &vif->flags);
			netif_stop_queue(vif->ndev);

			return action;
		}
	}
	spin_unlock_bh(&ar->list_lock);

	return action;
}

/* TODO this needs to be looked at */
static void ath6kl_tx_clear_node_map(struct ath6kl_vif *vif,
				     enum htc_endpoint_id eid, u32 map_no)
{
	struct ath6kl *ar = vif->ar;
	u32 i;

	if (vif->nw_type != ADHOC_NETWORK)
		return;

	if (!ar->ibss_ps_enable)
		return;

	if (eid == ar->ctrl_ep)
		return;

	if (map_no == 0)
		return;

	map_no--;
	ar->node_map[map_no].tx_pend--;

	if (ar->node_map[map_no].tx_pend)
		return;

	if (map_no != (ar->node_num - 1))
		return;

	for (i = ar->node_num; i > 0; i--) {
		if (ar->node_map[i - 1].tx_pend)
			break;

		memset(&ar->node_map[i - 1], 0,
		       sizeof(struct ath6kl_node_mapping));
		ar->node_num--;
	}
}

void ath6kl_tx_complete(struct htc_target *target,
			struct list_head *packet_queue)
{
	struct ath6kl *ar = target->dev->ar;
	struct sk_buff_head skb_queue;
	struct htc_packet *packet;
	struct sk_buff *skb;
	struct ath6kl_cookie *ath6kl_cookie;
	u32 map_no = 0;
	int status;
	enum htc_endpoint_id eid;
	bool wake_event = false;
	bool flushing[ATH6KL_VIF_MAX] = {false};
	u8 if_idx;
	struct ath6kl_vif *vif;

	skb_queue_head_init(&skb_queue);

	/* lock the driver as we update internal state */
	spin_lock_bh(&ar->lock);

	/* reap completed packets */
	while (!list_empty(packet_queue)) {

		packet = list_first_entry(packet_queue, struct htc_packet,
					  list);
		list_del(&packet->list);

		if (WARN_ON_ONCE(packet->endpoint == ENDPOINT_UNUSED ||
				 packet->endpoint >= ENDPOINT_MAX))
			continue;

		ath6kl_cookie = (struct ath6kl_cookie *)packet->pkt_cntxt;
		if (WARN_ON_ONCE(!ath6kl_cookie))
			continue;

		status = packet->status;
		skb = ath6kl_cookie->skb;
		eid = packet->endpoint;
		map_no = ath6kl_cookie->map_no;

		if (WARN_ON_ONCE(!skb || !skb->data)) {
			dev_kfree_skb(skb);
			ath6kl_free_cookie(ar, ath6kl_cookie);
			continue;
		}

		__skb_queue_tail(&skb_queue, skb);

		if (WARN_ON_ONCE(!status && (packet->act_len != skb->len))) {
			ath6kl_free_cookie(ar, ath6kl_cookie);
			continue;
		}

		ar->tx_pending[eid]--;

		if (eid != ar->ctrl_ep)
			ar->total_tx_data_pend--;

		if (eid == ar->ctrl_ep) {
			if (test_bit(WMI_CTRL_EP_FULL, &ar->flag))
				clear_bit(WMI_CTRL_EP_FULL, &ar->flag);

			if (ar->tx_pending[eid] == 0)
				wake_event = true;
		}

		if (eid == ar->ctrl_ep) {
			if_idx = wmi_cmd_hdr_get_if_idx(
				(struct wmi_cmd_hdr *) packet->buf);
		} else {
			if_idx = wmi_data_hdr_get_if_idx(
				(struct wmi_data_hdr *) packet->buf);
		}

		vif = ath6kl_get_vif_by_index(ar, if_idx);
		if (!vif) {
			ath6kl_free_cookie(ar, ath6kl_cookie);
			continue;
		}

		if (status) {
			if (status == -ECANCELED)
				/* a packet was flushed  */
				flushing[if_idx] = true;

			vif->net_stats.tx_errors++;

			if (status != -ENOSPC && status != -ECANCELED)
				ath6kl_warn("tx complete error: %d\n", status);

			ath6kl_dbg(ATH6KL_DBG_WLAN_TX,
				   "%s: skb=0x%p data=0x%p len=0x%x eid=%d %s\n",
				   __func__, skb, packet->buf, packet->act_len,
				   eid, "error!");
		} else {
			ath6kl_dbg(ATH6KL_DBG_WLAN_TX,
				   "%s: skb=0x%p data=0x%p len=0x%x eid=%d %s\n",
				   __func__, skb, packet->buf, packet->act_len,
				   eid, "OK");

			flushing[if_idx] = false;
			vif->net_stats.tx_packets++;
			vif->net_stats.tx_bytes += skb->len;
		}

		ath6kl_tx_clear_node_map(vif, eid, map_no);

		ath6kl_free_cookie(ar, ath6kl_cookie);

		if (test_bit(NETQ_STOPPED, &vif->flags))
			clear_bit(NETQ_STOPPED, &vif->flags);
	}

	spin_unlock_bh(&ar->lock);

	__skb_queue_purge(&skb_queue);

	/* FIXME: Locking */
	spin_lock_bh(&ar->list_lock);
	list_for_each_entry(vif, &ar->vif_list, list) {
		if (test_bit(CONNECTED, &vif->flags) &&
		    !flushing[vif->fw_vif_idx]) {
			spin_unlock_bh(&ar->list_lock);
			netif_wake_queue(vif->ndev);
			spin_lock_bh(&ar->list_lock);
		}
	}
	spin_unlock_bh(&ar->list_lock);

	if (wake_event)
		wake_up(&ar->event_wq);

	return;
}

void ath6kl_tx_data_cleanup(struct ath6kl *ar)
{
	int i;

	/* flush all the data (non-control) streams */
	for (i = 0; i < WMM_NUM_AC; i++)
		ath6kl_htc_flush_txep(ar->htc_target, ar->ac2ep_map[i],
				      ATH6KL_DATA_PKT_TAG);
}

/* Rx functions */

static void ath6kl_deliver_frames_to_nw_stack(struct net_device *dev,
					      struct sk_buff *skb)
{
	if (!skb)
		return;

	skb->dev = dev;

	if (!(skb->dev->flags & IFF_UP)) {
		dev_kfree_skb(skb);
		return;
	}

	skb->protocol = eth_type_trans(skb, skb->dev);

	netif_rx_ni(skb);
}

static void ath6kl_alloc_netbufs(struct sk_buff_head *q, u16 num)
{
	struct sk_buff *skb;

	while (num) {
		skb = ath6kl_buf_alloc(ATH6KL_BUFFER_SIZE);
		if (!skb) {
			ath6kl_err("netbuf allocation failed\n");
			return;
		}
		skb_queue_tail(q, skb);
		num--;
	}
}

static struct sk_buff *aggr_get_free_skb(struct aggr_info *p_aggr)
{
	struct sk_buff *skb = NULL;

	if (skb_queue_len(&p_aggr->rx_amsdu_freeq) <
	    (AGGR_NUM_OF_FREE_NETBUFS >> 2))
		ath6kl_alloc_netbufs(&p_aggr->rx_amsdu_freeq,
				     AGGR_NUM_OF_FREE_NETBUFS);

	skb = skb_dequeue(&p_aggr->rx_amsdu_freeq);

	return skb;
}

void ath6kl_rx_refill(struct htc_target *target, enum htc_endpoint_id endpoint)
{
	struct ath6kl *ar = target->dev->ar;
	struct sk_buff *skb;
	int rx_buf;
	int n_buf_refill;
	struct htc_packet *packet;
	struct list_head queue;

	n_buf_refill = ATH6KL_MAX_RX_BUFFERS -
			  ath6kl_htc_get_rxbuf_num(ar->htc_target, endpoint);

	if (n_buf_refill <= 0)
		return;

	INIT_LIST_HEAD(&queue);

	ath6kl_dbg(ATH6KL_DBG_WLAN_RX,
		   "%s: providing htc with %d buffers at eid=%d\n",
		   __func__, n_buf_refill, endpoint);

	for (rx_buf = 0; rx_buf < n_buf_refill; rx_buf++) {
		skb = ath6kl_buf_alloc(ATH6KL_BUFFER_SIZE);
		if (!skb)
			break;

		packet = (struct htc_packet *) skb->head;
		if (!IS_ALIGNED((unsigned long) skb->data, 4)) {
			size_t len = skb_headlen(skb);
			skb->data = PTR_ALIGN(skb->data - 4, 4);
			skb_set_tail_pointer(skb, len);
		}
		set_htc_rxpkt_info(packet, skb, skb->data,
				   ATH6KL_BUFFER_SIZE, endpoint);
		packet->skb = skb;
		list_add_tail(&packet->list, &queue);
	}

	if (!list_empty(&queue))
		ath6kl_htc_add_rxbuf_multiple(ar->htc_target, &queue);
}

void ath6kl_refill_amsdu_rxbufs(struct ath6kl *ar, int count)
{
	struct htc_packet *packet;
	struct sk_buff *skb;

	while (count) {
		skb = ath6kl_buf_alloc(ATH6KL_AMSDU_BUFFER_SIZE);
		if (!skb)
			return;

		packet = (struct htc_packet *) skb->head;
		if (!IS_ALIGNED((unsigned long) skb->data, 4)) {
			size_t len = skb_headlen(skb);
			skb->data = PTR_ALIGN(skb->data - 4, 4);
			skb_set_tail_pointer(skb, len);
		}
		set_htc_rxpkt_info(packet, skb, skb->data,
				   ATH6KL_AMSDU_BUFFER_SIZE, 0);
		packet->skb = skb;

		spin_lock_bh(&ar->lock);
		list_add_tail(&packet->list, &ar->amsdu_rx_buffer_queue);
		spin_unlock_bh(&ar->lock);
		count--;
	}
}

/*
 * Callback to allocate a receive buffer for a pending packet. We use a
 * pre-allocated list of buffers of maximum AMSDU size (4K).
 */
struct htc_packet *ath6kl_alloc_amsdu_rxbuf(struct htc_target *target,
					    enum htc_endpoint_id endpoint,
					    int len)
{
	struct ath6kl *ar = target->dev->ar;
	struct htc_packet *packet = NULL;
	struct list_head *pkt_pos;
	int refill_cnt = 0, depth = 0;

	ath6kl_dbg(ATH6KL_DBG_WLAN_RX, "%s: eid=%d, len:%d\n",
		   __func__, endpoint, len);

	if ((len <= ATH6KL_BUFFER_SIZE) ||
	    (len > ATH6KL_AMSDU_BUFFER_SIZE))
		return NULL;

	spin_lock_bh(&ar->lock);

	if (list_empty(&ar->amsdu_rx_buffer_queue)) {
		spin_unlock_bh(&ar->lock);
		refill_cnt = ATH6KL_MAX_AMSDU_RX_BUFFERS;
		goto refill_buf;
	}

	packet = list_first_entry(&ar->amsdu_rx_buffer_queue,
				  struct htc_packet, list);
	list_del(&packet->list);
	list_for_each(pkt_pos, &ar->amsdu_rx_buffer_queue)
		depth++;

	refill_cnt = ATH6KL_MAX_AMSDU_RX_BUFFERS - depth;
	spin_unlock_bh(&ar->lock);

	/* set actual endpoint ID */
	packet->endpoint = endpoint;

refill_buf:
	if (refill_cnt >= ATH6KL_AMSDU_REFILL_THRESHOLD)
		ath6kl_refill_amsdu_rxbufs(ar, refill_cnt);

	return packet;
}

static void aggr_slice_amsdu(struct aggr_info *p_aggr,
			     struct rxtid *rxtid, struct sk_buff *skb)
{
	struct sk_buff *new_skb;
	struct ethhdr *hdr;
	u16 frame_8023_len, payload_8023_len, mac_hdr_len, amsdu_len;
	u8 *framep;

	mac_hdr_len = sizeof(struct ethhdr);
	framep = skb->data + mac_hdr_len;
	amsdu_len = skb->len - mac_hdr_len;

	while (amsdu_len > mac_hdr_len) {
		hdr = (struct ethhdr *) framep;
		payload_8023_len = ntohs(hdr->h_proto);

		if (payload_8023_len < MIN_MSDU_SUBFRAME_PAYLOAD_LEN ||
		    payload_8023_len > MAX_MSDU_SUBFRAME_PAYLOAD_LEN) {
			ath6kl_err("802.3 AMSDU frame bound check failed. len %d\n",
				   payload_8023_len);
			break;
		}

		frame_8023_len = payload_8023_len + mac_hdr_len;
		new_skb = aggr_get_free_skb(p_aggr);
		if (!new_skb) {
			ath6kl_err("no buffer available\n");
			break;
		}

		memcpy(new_skb->data, framep, frame_8023_len);
		skb_put(new_skb, frame_8023_len);
		if (ath6kl_wmi_dot3_2_dix(new_skb)) {
			ath6kl_err("dot3_2_dix error\n");
			dev_kfree_skb(new_skb);
			break;
		}

		skb_queue_tail(&rxtid->q, new_skb);

		/* Is this the last subframe within this aggregate ? */
		if ((amsdu_len - frame_8023_len) == 0)
			break;

		/* Add the length of A-MSDU subframe padding bytes -
		 * Round to nearest word.
		 */
		frame_8023_len = ALIGN(frame_8023_len, 4);

		framep += frame_8023_len;
		amsdu_len -= frame_8023_len;
	}

	dev_kfree_skb(skb);
}

static void aggr_deque_frms(struct aggr_info_conn *agg_conn, u8 tid,
			    u16 seq_no, u8 order)
{
	struct sk_buff *skb;
	struct rxtid *rxtid;
	struct skb_hold_q *node;
	u16 idx, idx_end, seq_end;
	struct rxtid_stats *stats;

	rxtid = &agg_conn->rx_tid[tid];
	stats = &agg_conn->stat[tid];

	spin_lock_bh(&rxtid->lock);
	idx = AGGR_WIN_IDX(rxtid->seq_next, rxtid->hold_q_sz);

	/*
	 * idx_end is typically the last possible frame in the window,
	 * but changes to 'the' seq_no, when BAR comes. If seq_no
	 * is non-zero, we will go up to that and stop.
	 * Note: last seq no in current window will occupy the same
	 * index position as index that is just previous to start.
	 * An imp point : if win_sz is 7, for seq_no space of 4095,
	 * then, there would be holes when sequence wrap around occurs.
	 * Target should judiciously choose the win_sz, based on
	 * this condition. For 4095, (TID_WINDOW_SZ = 2 x win_sz
	 * 2, 4, 8, 16 win_sz works fine).
	 * We must deque from "idx" to "idx_end", including both.
	 */
	seq_end = seq_no ? seq_no : rxtid->seq_next;
	idx_end = AGGR_WIN_IDX(seq_end, rxtid->hold_q_sz);

	do {
		node = &rxtid->hold_q[idx];
		if ((order == 1) && (!node->skb))
			break;

		if (node->skb) {
			if (node->is_amsdu)
				aggr_slice_amsdu(agg_conn->aggr_info, rxtid,
						 node->skb);
			else
				skb_queue_tail(&rxtid->q, node->skb);
			node->skb = NULL;
		} else
			stats->num_hole++;

		rxtid->seq_next = ATH6KL_NEXT_SEQ_NO(rxtid->seq_next);
		idx = AGGR_WIN_IDX(rxtid->seq_next, rxtid->hold_q_sz);
	} while (idx != idx_end);

	spin_unlock_bh(&rxtid->lock);

	stats->num_delivered += skb_queue_len(&rxtid->q);

	while ((skb = skb_dequeue(&rxtid->q)))
		ath6kl_deliver_frames_to_nw_stack(agg_conn->dev, skb);
}

static bool aggr_process_recv_frm(struct aggr_info_conn *agg_conn, u8 tid,
				  u16 seq_no,
				  bool is_amsdu, struct sk_buff *frame)
{
	struct rxtid *rxtid;
	struct rxtid_stats *stats;
	struct sk_buff *skb;
	struct skb_hold_q *node;
	u16 idx, st, cur, end;
	bool is_queued = false;
	u16 extended_end;

	rxtid = &agg_conn->rx_tid[tid];
	stats = &agg_conn->stat[tid];

	stats->num_into_aggr++;

	if (!rxtid->aggr) {
		if (is_amsdu) {
			aggr_slice_amsdu(agg_conn->aggr_info, rxtid, frame);
			is_queued = true;
			stats->num_amsdu++;
			while ((skb = skb_dequeue(&rxtid->q)))
				ath6kl_deliver_frames_to_nw_stack(agg_conn->dev,
								  skb);
		}
		return is_queued;
	}

	/* Check the incoming sequence no, if it's in the window */
	st = rxtid->seq_next;
	cur = seq_no;
	end = (st + rxtid->hold_q_sz-1) & ATH6KL_MAX_SEQ_NO;

	if (((st < end) && (cur < st || cur > end)) ||
	    ((st > end) && (cur > end) && (cur < st))) {
		extended_end = (end + rxtid->hold_q_sz - 1) &
			ATH6KL_MAX_SEQ_NO;

		if (((end < extended_end) &&
		     (cur < end || cur > extended_end)) ||
		    ((end > extended_end) && (cur > extended_end) &&
		     (cur < end))) {
			aggr_deque_frms(agg_conn, tid, 0, 0);
			spin_lock_bh(&rxtid->lock);
			if (cur >= rxtid->hold_q_sz - 1)
				rxtid->seq_next = cur - (rxtid->hold_q_sz - 1);
			else
				rxtid->seq_next = ATH6KL_MAX_SEQ_NO -
						  (rxtid->hold_q_sz - 2 - cur);
			spin_unlock_bh(&rxtid->lock);
		} else {
			/*
			 * Dequeue only those frames that are outside the
			 * new shifted window.
			 */
			if (cur >= rxtid->hold_q_sz - 1)
				st = cur - (rxtid->hold_q_sz - 1);
			else
				st = ATH6KL_MAX_SEQ_NO -
					(rxtid->hold_q_sz - 2 - cur);

			aggr_deque_frms(agg_conn, tid, st, 0);
		}

		stats->num_oow++;
	}

	idx = AGGR_WIN_IDX(seq_no, rxtid->hold_q_sz);

	node = &rxtid->hold_q[idx];

	spin_lock_bh(&rxtid->lock);

	/*
	 * Is the cur frame duplicate or something beyond our window(hold_q
	 * -> which is 2x, already)?
	 *
	 * 1. Duplicate is easy - drop incoming frame.
	 * 2. Not falling in current sliding window.
	 *  2a. is the frame_seq_no preceding current tid_seq_no?
	 *      -> drop the frame. perhaps sender did not get our ACK.
	 *         this is taken care of above.
	 *  2b. is the frame_seq_no beyond window(st, TID_WINDOW_SZ);
	 *      -> Taken care of it above, by moving window forward.
	 */
	dev_kfree_skb(node->skb);
	stats->num_dups++;

	node->skb = frame;
	is_queued = true;
	node->is_amsdu = is_amsdu;
	node->seq_no = seq_no;

	if (node->is_amsdu)
		stats->num_amsdu++;
	else
		stats->num_mpdu++;

	spin_unlock_bh(&rxtid->lock);

	aggr_deque_frms(agg_conn, tid, 0, 1);

	if (agg_conn->timer_scheduled)
		return is_queued;

	spin_lock_bh(&rxtid->lock);
	for (idx = 0 ; idx < rxtid->hold_q_sz; idx++) {
		if (rxtid->hold_q[idx].skb) {
			/*
			 * There is a frame in the queue and no
			 * timer so start a timer to ensure that
			 * the frame doesn't remain stuck
			 * forever.
			 */
			agg_conn->timer_scheduled = true;
			mod_timer(&agg_conn->timer,
				  (jiffies + (HZ * AGGR_RX_TIMEOUT) / 1000));
			rxtid->timer_mon = true;
			break;
		}
	}
	spin_unlock_bh(&rxtid->lock);

	return is_queued;
}

static void ath6kl_uapsd_trigger_frame_rx(struct ath6kl_vif *vif,
						 struct ath6kl_sta *conn)
{
	struct ath6kl *ar = vif->ar;
	bool is_apsdq_empty, is_apsdq_empty_at_start;
	u32 num_frames_to_deliver, flags;
	struct sk_buff *skb = NULL;

	/*
	 * If the APSD q for this STA is not empty, dequeue and
	 * send a pkt from the head of the q. Also update the
	 * More data bit in the WMI_DATA_HDR if there are
	 * more pkts for this STA in the APSD q.
	 * If there are no more pkts for this STA,
	 * update the APSD bitmap for this STA.
	 */

	num_frames_to_deliver = (conn->apsd_info >> ATH6KL_APSD_NUM_OF_AC) &
						    ATH6KL_APSD_FRAME_MASK;
	/*
	 * Number of frames to send in a service period is
	 * indicated by the station
	 * in the QOS_INFO of the association request
	 * If it is zero, send all frames
	 */
	if (!num_frames_to_deliver)
		num_frames_to_deliver = ATH6KL_APSD_ALL_FRAME;

	spin_lock_bh(&conn->psq_lock);
	is_apsdq_empty = skb_queue_empty(&conn->apsdq);
	spin_unlock_bh(&conn->psq_lock);
	is_apsdq_empty_at_start = is_apsdq_empty;

	while ((!is_apsdq_empty) && (num_frames_to_deliver)) {

		spin_lock_bh(&conn->psq_lock);
		skb = skb_dequeue(&conn->apsdq);
		is_apsdq_empty = skb_queue_empty(&conn->apsdq);
		spin_unlock_bh(&conn->psq_lock);

		/*
		 * Set the STA flag to Trigger delivery,
		 * so that the frame will go out
		 */
		conn->sta_flags |= STA_PS_APSD_TRIGGER;
		num_frames_to_deliver--;

		/* Last frame in the service period, set EOSP or queue empty */
		if ((is_apsdq_empty) || (!num_frames_to_deliver))
			conn->sta_flags |= STA_PS_APSD_EOSP;

		ath6kl_data_tx(skb, vif->ndev);
		conn->sta_flags &= ~(STA_PS_APSD_TRIGGER);
		conn->sta_flags &= ~(STA_PS_APSD_EOSP);
	}

	if (is_apsdq_empty) {
		if (is_apsdq_empty_at_start)
			flags = WMI_AP_APSD_NO_DELIVERY_FRAMES;
		else
			flags = 0;

		ath6kl_wmi_set_apsd_bfrd_traf(ar->wmi,
					      vif->fw_vif_idx,
					      conn->aid, 0, flags);
	}

	return;
}

void ath6kl_rx(struct htc_target *target, struct htc_packet *packet)
{
	struct ath6kl *ar = target->dev->ar;
	struct sk_buff *skb = packet->pkt_cntxt;
	struct wmi_rx_meta_v2 *meta;
	struct wmi_data_hdr *dhdr;
	int min_hdr_len;
	u8 meta_type, dot11_hdr = 0;
	u8 pad_before_data_start;
	int status = packet->status;
	enum htc_endpoint_id ept = packet->endpoint;
	bool is_amsdu, prev_ps, ps_state = false;
	bool trig_state = false;
	struct ath6kl_sta *conn = NULL;
	struct sk_buff *skb1 = NULL;
	struct ethhdr *datap = NULL;
	struct ath6kl_vif *vif;
	struct aggr_info_conn *aggr_conn;
	u16 seq_no, offset;
	u8 tid, if_idx;

	ath6kl_dbg(ATH6KL_DBG_WLAN_RX,
		   "%s: ar=0x%p eid=%d, skb=0x%p, data=0x%p, len=0x%x status:%d",
		   __func__, ar, ept, skb, packet->buf,
		   packet->act_len, status);

	if (status || !(skb->data + HTC_HDR_LENGTH)) {
		dev_kfree_skb(skb);
		return;
	}

	skb_put(skb, packet->act_len + HTC_HDR_LENGTH);
	skb_pull(skb, HTC_HDR_LENGTH);

	ath6kl_dbg_dump(ATH6KL_DBG_RAW_BYTES, __func__, "rx ",
			skb->data, skb->len);

	if (ept == ar->ctrl_ep) {
		if (test_bit(WMI_ENABLED, &ar->flag)) {
			ath6kl_check_wow_status(ar);
			ath6kl_wmi_control_rx(ar->wmi, skb);
			return;
		}
		if_idx =
		wmi_cmd_hdr_get_if_idx((struct wmi_cmd_hdr *) skb->data);
	} else {
		if_idx =
		wmi_data_hdr_get_if_idx((struct wmi_data_hdr *) skb->data);
	}

	vif = ath6kl_get_vif_by_index(ar, if_idx);
	if (!vif) {
		dev_kfree_skb(skb);
		return;
	}

	/*
	 * Take lock to protect buffer counts and adaptive power throughput
	 * state.
	 */
	spin_lock_bh(&vif->if_lock);

	vif->net_stats.rx_packets++;
	vif->net_stats.rx_bytes += packet->act_len;

	spin_unlock_bh(&vif->if_lock);

	skb->dev = vif->ndev;

	if (!test_bit(WMI_ENABLED, &ar->flag)) {
		if (EPPING_ALIGNMENT_PAD > 0)
			skb_pull(skb, EPPING_ALIGNMENT_PAD);
		ath6kl_deliver_frames_to_nw_stack(vif->ndev, skb);
		return;
	}

	ath6kl_check_wow_status(ar);

	min_hdr_len = sizeof(struct ethhdr) + sizeof(struct wmi_data_hdr) +
		      sizeof(struct ath6kl_llc_snap_hdr);

	dhdr = (struct wmi_data_hdr *) skb->data;

	/*
	 * In the case of AP mode we may receive NULL data frames
	 * that do not have LLC hdr. They are 16 bytes in size.
	 * Allow these frames in the AP mode.
	 */
	if (vif->nw_type != AP_NETWORK &&
	    ((packet->act_len < min_hdr_len) ||
	     (packet->act_len > WMI_MAX_AMSDU_RX_DATA_FRAME_LENGTH))) {
		ath6kl_info("frame len is too short or too long\n");
		vif->net_stats.rx_errors++;
		vif->net_stats.rx_length_errors++;
		dev_kfree_skb(skb);
		return;
	}

	/* Get the Power save state of the STA */
	if (vif->nw_type == AP_NETWORK) {
		meta_type = wmi_data_hdr_get_meta(dhdr);

		ps_state = !!((dhdr->info >> WMI_DATA_HDR_PS_SHIFT) &
			      WMI_DATA_HDR_PS_MASK);

		offset = sizeof(struct wmi_data_hdr);
		trig_state = !!(le16_to_cpu(dhdr->info3) & WMI_DATA_HDR_TRIG);

		switch (meta_type) {
		case 0:
			break;
		case WMI_META_VERSION_1:
			offset += sizeof(struct wmi_rx_meta_v1);
			break;
		case WMI_META_VERSION_2:
			offset += sizeof(struct wmi_rx_meta_v2);
			break;
		default:
			break;
		}

		datap = (struct ethhdr *) (skb->data + offset);
		conn = ath6kl_find_sta(vif, datap->h_source);

		if (!conn) {
			dev_kfree_skb(skb);
			return;
		}

		/*
		 * If there is a change in PS state of the STA,
		 * take appropriate steps:
		 *
		 * 1. If Sleep-->Awake, flush the psq for the STA
		 *    Clear the PVB for the STA.
		 * 2. If Awake-->Sleep, Starting queueing frames
		 *    the STA.
		 */
		prev_ps = !!(conn->sta_flags & STA_PS_SLEEP);

		if (ps_state)
			conn->sta_flags |= STA_PS_SLEEP;
		else
			conn->sta_flags &= ~STA_PS_SLEEP;

		/* Accept trigger only when the station is in sleep */
		if ((conn->sta_flags & STA_PS_SLEEP) && trig_state)
			ath6kl_uapsd_trigger_frame_rx(vif, conn);

		if (prev_ps ^ !!(conn->sta_flags & STA_PS_SLEEP)) {
			if (!(conn->sta_flags & STA_PS_SLEEP)) {
				struct sk_buff *skbuff = NULL;
				bool is_apsdq_empty;
				struct ath6kl_mgmt_buff *mgmt;
				u8 idx;

				spin_lock_bh(&conn->psq_lock);
				while (conn->mgmt_psq_len > 0) {
					mgmt = list_first_entry(
							&conn->mgmt_psq,
							struct ath6kl_mgmt_buff,
							list);
					list_del(&mgmt->list);
					conn->mgmt_psq_len--;
					spin_unlock_bh(&conn->psq_lock);
					idx = vif->fw_vif_idx;

					ath6kl_wmi_send_mgmt_cmd(ar->wmi,
								 idx,
								 mgmt->id,
								 mgmt->freq,
								 mgmt->wait,
								 mgmt->buf,
								 mgmt->len,
								 mgmt->no_cck);

					kfree(mgmt);
					spin_lock_bh(&conn->psq_lock);
				}
				conn->mgmt_psq_len = 0;
				while ((skbuff = skb_dequeue(&conn->psq))) {
					spin_unlock_bh(&conn->psq_lock);
					ath6kl_data_tx(skbuff, vif->ndev);
					spin_lock_bh(&conn->psq_lock);
				}

				is_apsdq_empty = skb_queue_empty(&conn->apsdq);
				while ((skbuff = skb_dequeue(&conn->apsdq))) {
					spin_unlock_bh(&conn->psq_lock);
					ath6kl_data_tx(skbuff, vif->ndev);
					spin_lock_bh(&conn->psq_lock);
				}
				spin_unlock_bh(&conn->psq_lock);

				if (!is_apsdq_empty)
					ath6kl_wmi_set_apsd_bfrd_traf(
							ar->wmi,
							vif->fw_vif_idx,
							conn->aid, 0, 0);

				/* Clear the PVB for this STA */
				ath6kl_wmi_set_pvb_cmd(ar->wmi, vif->fw_vif_idx,
						       conn->aid, 0);
			}
		}

		/* drop NULL data frames here */
		if ((packet->act_len < min_hdr_len) ||
		    (packet->act_len >
		     WMI_MAX_AMSDU_RX_DATA_FRAME_LENGTH)) {
			dev_kfree_skb(skb);
			return;
		}
	}

	is_amsdu = wmi_data_hdr_is_amsdu(dhdr) ? true : false;
	tid = wmi_data_hdr_get_up(dhdr);
	seq_no = wmi_data_hdr_get_seqno(dhdr);
	meta_type = wmi_data_hdr_get_meta(dhdr);
	dot11_hdr = wmi_data_hdr_get_dot11(dhdr);
	pad_before_data_start =
		(le16_to_cpu(dhdr->info3) >> WMI_DATA_HDR_PAD_BEFORE_DATA_SHIFT)
			& WMI_DATA_HDR_PAD_BEFORE_DATA_MASK;

	skb_pull(skb, sizeof(struct wmi_data_hdr));

	switch (meta_type) {
	case WMI_META_VERSION_1:
		skb_pull(skb, sizeof(struct wmi_rx_meta_v1));
		break;
	case WMI_META_VERSION_2:
		meta = (struct wmi_rx_meta_v2 *) skb->data;
		if (meta->csum_flags & 0x1) {
			skb->ip_summed = CHECKSUM_COMPLETE;
			skb->csum = (__force __wsum) meta->csum;
		}
		skb_pull(skb, sizeof(struct wmi_rx_meta_v2));
		break;
	default:
		break;
	}

	skb_pull(skb, pad_before_data_start);

	if (dot11_hdr)
		status = ath6kl_wmi_dot11_hdr_remove(ar->wmi, skb);
	else if (!is_amsdu)
		status = ath6kl_wmi_dot3_2_dix(skb);

	if (status) {
		/*
		 * Drop frames that could not be processed (lack of
		 * memory, etc.)
		 */
		dev_kfree_skb(skb);
		return;
	}

	if (!(vif->ndev->flags & IFF_UP)) {
		dev_kfree_skb(skb);
		return;
	}

	if (vif->nw_type == AP_NETWORK) {
		datap = (struct ethhdr *) skb->data;
		if (is_multicast_ether_addr(datap->h_dest))
			/*
			 * Bcast/Mcast frames should be sent to the
			 * OS stack as well as on the air.
			 */
			skb1 = skb_copy(skb, GFP_ATOMIC);
		else {
			/*
			 * Search for a connected STA with dstMac
			 * as the Mac address. If found send the
			 * frame to it on the air else send the
			 * frame up the stack.
			 */
			conn = ath6kl_find_sta(vif, datap->h_dest);

			if (conn && ar->intra_bss) {
				skb1 = skb;
				skb = NULL;
			} else if (conn && !ar->intra_bss) {
				dev_kfree_skb(skb);
				skb = NULL;
			}
		}
		if (skb1)
			ath6kl_data_tx(skb1, vif->ndev);

		if (skb == NULL) {
			/* nothing to deliver up the stack */
			return;
		}
	}

	datap = (struct ethhdr *) skb->data;

	if (is_unicast_ether_addr(datap->h_dest)) {
		if (vif->nw_type == AP_NETWORK) {
			conn = ath6kl_find_sta(vif, datap->h_source);
			if (!conn)
				return;
			aggr_conn = conn->aggr_conn;
		} else
			aggr_conn = vif->aggr_cntxt->aggr_conn;

		if (aggr_process_recv_frm(aggr_conn, tid, seq_no,
					  is_amsdu, skb)) {
			/* aggregation code will handle the skb */
			return;
		}
	} else if (!is_broadcast_ether_addr(datap->h_dest))
		vif->net_stats.multicast++;

	ath6kl_deliver_frames_to_nw_stack(vif->ndev, skb);
}

static void aggr_timeout(unsigned long arg)
{
	u8 i, j;
	struct aggr_info_conn *aggr_conn = (struct aggr_info_conn *) arg;
	struct rxtid *rxtid;
	struct rxtid_stats *stats;

	for (i = 0; i < NUM_OF_TIDS; i++) {
		rxtid = &aggr_conn->rx_tid[i];
		stats = &aggr_conn->stat[i];

		if (!rxtid->aggr || !rxtid->timer_mon)
			continue;

		stats->num_timeouts++;
		ath6kl_dbg(ATH6KL_DBG_AGGR,
			   "aggr timeout (st %d end %d)\n",
			   rxtid->seq_next,
			   ((rxtid->seq_next + rxtid->hold_q_sz-1) &
			    ATH6KL_MAX_SEQ_NO));
		aggr_deque_frms(aggr_conn, i, 0, 0);
	}

	aggr_conn->timer_scheduled = false;

	for (i = 0; i < NUM_OF_TIDS; i++) {
		rxtid = &aggr_conn->rx_tid[i];

		if (rxtid->aggr && rxtid->hold_q) {
			spin_lock_bh(&rxtid->lock);
			for (j = 0; j < rxtid->hold_q_sz; j++) {
				if (rxtid->hold_q[j].skb) {
					aggr_conn->timer_scheduled = true;
					rxtid->timer_mon = true;
					break;
				}
			}
			spin_unlock_bh(&rxtid->lock);

			if (j >= rxtid->hold_q_sz)
				rxtid->timer_mon = false;
		}
	}

	if (aggr_conn->timer_scheduled)
		mod_timer(&aggr_conn->timer,
			  jiffies + msecs_to_jiffies(AGGR_RX_TIMEOUT));
}

static void aggr_delete_tid_state(struct aggr_info_conn *aggr_conn, u8 tid)
{
	struct rxtid *rxtid;
	struct rxtid_stats *stats;

	if (!aggr_conn || tid >= NUM_OF_TIDS)
		return;

	rxtid = &aggr_conn->rx_tid[tid];
	stats = &aggr_conn->stat[tid];

	if (rxtid->aggr)
		aggr_deque_frms(aggr_conn, tid, 0, 0);

	rxtid->aggr = false;
	rxtid->timer_mon = false;
	rxtid->win_sz = 0;
	rxtid->seq_next = 0;
	rxtid->hold_q_sz = 0;

	kfree(rxtid->hold_q);
	rxtid->hold_q = NULL;

	memset(stats, 0, sizeof(struct rxtid_stats));
}

void aggr_recv_addba_req_evt(struct ath6kl_vif *vif, u8 tid_mux, u16 seq_no,
			     u8 win_sz)
{
	struct ath6kl_sta *sta;
	struct aggr_info_conn *aggr_conn = NULL;
	struct rxtid *rxtid;
	struct rxtid_stats *stats;
	u16 hold_q_size;
	u8 tid, aid;

	if (vif->nw_type == AP_NETWORK) {
		aid = ath6kl_get_aid(tid_mux);
		sta = ath6kl_find_sta_by_aid(vif->ar, aid);
		if (sta)
			aggr_conn = sta->aggr_conn;
	} else
		aggr_conn = vif->aggr_cntxt->aggr_conn;

	if (!aggr_conn)
		return;

	tid = ath6kl_get_tid(tid_mux);
	if (tid >= NUM_OF_TIDS)
		return;

	rxtid = &aggr_conn->rx_tid[tid];
	stats = &aggr_conn->stat[tid];

	if (win_sz < AGGR_WIN_SZ_MIN || win_sz > AGGR_WIN_SZ_MAX)
		ath6kl_dbg(ATH6KL_DBG_WLAN_RX, "%s: win_sz %d, tid %d\n",
			   __func__, win_sz, tid);

	if (rxtid->aggr)
		aggr_delete_tid_state(aggr_conn, tid);

	rxtid->seq_next = seq_no;
	hold_q_size = TID_WINDOW_SZ(win_sz) * sizeof(struct skb_hold_q);
	rxtid->hold_q = kzalloc(hold_q_size, GFP_KERNEL);
	if (!rxtid->hold_q)
		return;

	rxtid->win_sz = win_sz;
	rxtid->hold_q_sz = TID_WINDOW_SZ(win_sz);
	if (!skb_queue_empty(&rxtid->q))
		return;

	rxtid->aggr = true;
}

void aggr_conn_init(struct ath6kl_vif *vif, struct aggr_info *aggr_info,
		    struct aggr_info_conn *aggr_conn)
{
	struct rxtid *rxtid;
	u8 i;

	aggr_conn->aggr_sz = AGGR_SZ_DEFAULT;
	aggr_conn->dev = vif->ndev;
	init_timer(&aggr_conn->timer);
	aggr_conn->timer.function = aggr_timeout;
	aggr_conn->timer.data = (unsigned long) aggr_conn;
	aggr_conn->aggr_info = aggr_info;

	aggr_conn->timer_scheduled = false;

	for (i = 0; i < NUM_OF_TIDS; i++) {
		rxtid = &aggr_conn->rx_tid[i];
		rxtid->aggr = false;
		rxtid->timer_mon = false;
		skb_queue_head_init(&rxtid->q);
		spin_lock_init(&rxtid->lock);
	}

}

struct aggr_info *aggr_init(struct ath6kl_vif *vif)
{
	struct aggr_info *p_aggr = NULL;

	p_aggr = kzalloc(sizeof(struct aggr_info), GFP_KERNEL);
	if (!p_aggr) {
		ath6kl_err("failed to alloc memory for aggr_node\n");
		return NULL;
	}

	p_aggr->aggr_conn = kzalloc(sizeof(struct aggr_info_conn), GFP_KERNEL);
	if (!p_aggr->aggr_conn) {
		ath6kl_err("failed to alloc memory for connection specific aggr info\n");
		kfree(p_aggr);
		return NULL;
	}

	aggr_conn_init(vif, p_aggr, p_aggr->aggr_conn);

	skb_queue_head_init(&p_aggr->rx_amsdu_freeq);
	ath6kl_alloc_netbufs(&p_aggr->rx_amsdu_freeq, AGGR_NUM_OF_FREE_NETBUFS);

	return p_aggr;
}

void aggr_recv_delba_req_evt(struct ath6kl_vif *vif, u8 tid_mux)
{
	struct ath6kl_sta *sta;
	struct rxtid *rxtid;
	struct aggr_info_conn *aggr_conn = NULL;
	u8 tid, aid;

	if (vif->nw_type == AP_NETWORK) {
		aid = ath6kl_get_aid(tid_mux);
		sta = ath6kl_find_sta_by_aid(vif->ar, aid);
		if (sta)
			aggr_conn = sta->aggr_conn;
	} else
		aggr_conn = vif->aggr_cntxt->aggr_conn;

	if (!aggr_conn)
		return;

	tid = ath6kl_get_tid(tid_mux);
	if (tid >= NUM_OF_TIDS)
		return;

	rxtid = &aggr_conn->rx_tid[tid];

	if (rxtid->aggr)
		aggr_delete_tid_state(aggr_conn, tid);
}

void aggr_reset_state(struct aggr_info_conn *aggr_conn)
{
	u8 tid;

	if (!aggr_conn)
		return;

	if (aggr_conn->timer_scheduled) {
		del_timer(&aggr_conn->timer);
		aggr_conn->timer_scheduled = false;
	}

	for (tid = 0; tid < NUM_OF_TIDS; tid++)
		aggr_delete_tid_state(aggr_conn, tid);
}

/* clean up our amsdu buffer list */
void ath6kl_cleanup_amsdu_rxbufs(struct ath6kl *ar)
{
	struct htc_packet *packet, *tmp_pkt;

	spin_lock_bh(&ar->lock);
	if (list_empty(&ar->amsdu_rx_buffer_queue)) {
		spin_unlock_bh(&ar->lock);
		return;
	}

	list_for_each_entry_safe(packet, tmp_pkt, &ar->amsdu_rx_buffer_queue,
				 list) {
		list_del(&packet->list);
		spin_unlock_bh(&ar->lock);
		dev_kfree_skb(packet->pkt_cntxt);
		spin_lock_bh(&ar->lock);
	}

	spin_unlock_bh(&ar->lock);
}

void aggr_module_destroy(struct aggr_info *aggr_info)
{
	if (!aggr_info)
		return;

	aggr_reset_state(aggr_info->aggr_conn);
	skb_queue_purge(&aggr_info->rx_amsdu_freeq);
	kfree(aggr_info->aggr_conn);
	kfree(aggr_info);
}
