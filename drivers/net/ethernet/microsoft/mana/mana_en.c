// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2021, Microsoft Corporation. */

#include <uapi/linux/bpf.h>

#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mm.h>

#include <net/checksum.h>
#include <net/ip6_checksum.h>

#include "mana.h"

/* Microsoft Azure Network Adapter (MANA) functions */

static int mana_open(struct net_device *ndev)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	int err;

	err = mana_alloc_queues(ndev);
	if (err)
		return err;

	apc->port_is_up = true;

	/* Ensure port state updated before txq state */
	smp_wmb();

	netif_carrier_on(ndev);
	netif_tx_wake_all_queues(ndev);

	return 0;
}

static int mana_close(struct net_device *ndev)
{
	struct mana_port_context *apc = netdev_priv(ndev);

	if (!apc->port_is_up)
		return 0;

	return mana_detach(ndev, true);
}

static bool mana_can_tx(struct gdma_queue *wq)
{
	return mana_gd_wq_avail_space(wq) >= MAX_TX_WQE_SIZE;
}

static unsigned int mana_checksum_info(struct sk_buff *skb)
{
	if (skb->protocol == htons(ETH_P_IP)) {
		struct iphdr *ip = ip_hdr(skb);

		if (ip->protocol == IPPROTO_TCP)
			return IPPROTO_TCP;

		if (ip->protocol == IPPROTO_UDP)
			return IPPROTO_UDP;
	} else if (skb->protocol == htons(ETH_P_IPV6)) {
		struct ipv6hdr *ip6 = ipv6_hdr(skb);

		if (ip6->nexthdr == IPPROTO_TCP)
			return IPPROTO_TCP;

		if (ip6->nexthdr == IPPROTO_UDP)
			return IPPROTO_UDP;
	}

	/* No csum offloading */
	return 0;
}

static int mana_map_skb(struct sk_buff *skb, struct mana_port_context *apc,
			struct mana_tx_package *tp)
{
	struct mana_skb_head *ash = (struct mana_skb_head *)skb->head;
	struct gdma_dev *gd = apc->ac->gdma_dev;
	struct gdma_context *gc;
	struct device *dev;
	skb_frag_t *frag;
	dma_addr_t da;
	int i;

	gc = gd->gdma_context;
	dev = gc->dev;
	da = dma_map_single(dev, skb->data, skb_headlen(skb), DMA_TO_DEVICE);

	if (dma_mapping_error(dev, da))
		return -ENOMEM;

	ash->dma_handle[0] = da;
	ash->size[0] = skb_headlen(skb);

	tp->wqe_req.sgl[0].address = ash->dma_handle[0];
	tp->wqe_req.sgl[0].mem_key = gd->gpa_mkey;
	tp->wqe_req.sgl[0].size = ash->size[0];

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		frag = &skb_shinfo(skb)->frags[i];
		da = skb_frag_dma_map(dev, frag, 0, skb_frag_size(frag),
				      DMA_TO_DEVICE);

		if (dma_mapping_error(dev, da))
			goto frag_err;

		ash->dma_handle[i + 1] = da;
		ash->size[i + 1] = skb_frag_size(frag);

		tp->wqe_req.sgl[i + 1].address = ash->dma_handle[i + 1];
		tp->wqe_req.sgl[i + 1].mem_key = gd->gpa_mkey;
		tp->wqe_req.sgl[i + 1].size = ash->size[i + 1];
	}

	return 0;

frag_err:
	for (i = i - 1; i >= 0; i--)
		dma_unmap_page(dev, ash->dma_handle[i + 1], ash->size[i + 1],
			       DMA_TO_DEVICE);

	dma_unmap_single(dev, ash->dma_handle[0], ash->size[0], DMA_TO_DEVICE);

	return -ENOMEM;
}

int mana_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	enum mana_tx_pkt_format pkt_fmt = MANA_SHORT_PKT_FMT;
	struct mana_port_context *apc = netdev_priv(ndev);
	u16 txq_idx = skb_get_queue_mapping(skb);
	struct gdma_dev *gd = apc->ac->gdma_dev;
	bool ipv4 = false, ipv6 = false;
	struct mana_tx_package pkg = {};
	struct netdev_queue *net_txq;
	struct mana_stats_tx *tx_stats;
	struct gdma_queue *gdma_sq;
	unsigned int csum_type;
	struct mana_txq *txq;
	struct mana_cq *cq;
	int err, len;

	if (unlikely(!apc->port_is_up))
		goto tx_drop;

	if (skb_cow_head(skb, MANA_HEADROOM))
		goto tx_drop_count;

	txq = &apc->tx_qp[txq_idx].txq;
	gdma_sq = txq->gdma_sq;
	cq = &apc->tx_qp[txq_idx].tx_cq;

	pkg.tx_oob.s_oob.vcq_num = cq->gdma_id;
	pkg.tx_oob.s_oob.vsq_frame = txq->vsq_frame;

	if (txq->vp_offset > MANA_SHORT_VPORT_OFFSET_MAX) {
		pkg.tx_oob.l_oob.long_vp_offset = txq->vp_offset;
		pkt_fmt = MANA_LONG_PKT_FMT;
	} else {
		pkg.tx_oob.s_oob.short_vp_offset = txq->vp_offset;
	}

	pkg.tx_oob.s_oob.pkt_fmt = pkt_fmt;

	if (pkt_fmt == MANA_SHORT_PKT_FMT)
		pkg.wqe_req.inline_oob_size = sizeof(struct mana_tx_short_oob);
	else
		pkg.wqe_req.inline_oob_size = sizeof(struct mana_tx_oob);

	pkg.wqe_req.inline_oob_data = &pkg.tx_oob;
	pkg.wqe_req.flags = 0;
	pkg.wqe_req.client_data_unit = 0;

	pkg.wqe_req.num_sge = 1 + skb_shinfo(skb)->nr_frags;
	WARN_ON_ONCE(pkg.wqe_req.num_sge > 30);

	if (pkg.wqe_req.num_sge <= ARRAY_SIZE(pkg.sgl_array)) {
		pkg.wqe_req.sgl = pkg.sgl_array;
	} else {
		pkg.sgl_ptr = kmalloc_array(pkg.wqe_req.num_sge,
					    sizeof(struct gdma_sge),
					    GFP_ATOMIC);
		if (!pkg.sgl_ptr)
			goto tx_drop_count;

		pkg.wqe_req.sgl = pkg.sgl_ptr;
	}

	if (skb->protocol == htons(ETH_P_IP))
		ipv4 = true;
	else if (skb->protocol == htons(ETH_P_IPV6))
		ipv6 = true;

	if (skb_is_gso(skb)) {
		pkg.tx_oob.s_oob.is_outer_ipv4 = ipv4;
		pkg.tx_oob.s_oob.is_outer_ipv6 = ipv6;

		pkg.tx_oob.s_oob.comp_iphdr_csum = 1;
		pkg.tx_oob.s_oob.comp_tcp_csum = 1;
		pkg.tx_oob.s_oob.trans_off = skb_transport_offset(skb);

		pkg.wqe_req.client_data_unit = skb_shinfo(skb)->gso_size;
		pkg.wqe_req.flags = GDMA_WR_OOB_IN_SGL | GDMA_WR_PAD_BY_SGE0;
		if (ipv4) {
			ip_hdr(skb)->tot_len = 0;
			ip_hdr(skb)->check = 0;
			tcp_hdr(skb)->check =
				~csum_tcpudp_magic(ip_hdr(skb)->saddr,
						   ip_hdr(skb)->daddr, 0,
						   IPPROTO_TCP, 0);
		} else {
			ipv6_hdr(skb)->payload_len = 0;
			tcp_hdr(skb)->check =
				~csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
						 &ipv6_hdr(skb)->daddr, 0,
						 IPPROTO_TCP, 0);
		}
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		csum_type = mana_checksum_info(skb);

		if (csum_type == IPPROTO_TCP) {
			pkg.tx_oob.s_oob.is_outer_ipv4 = ipv4;
			pkg.tx_oob.s_oob.is_outer_ipv6 = ipv6;

			pkg.tx_oob.s_oob.comp_tcp_csum = 1;
			pkg.tx_oob.s_oob.trans_off = skb_transport_offset(skb);

		} else if (csum_type == IPPROTO_UDP) {
			pkg.tx_oob.s_oob.is_outer_ipv4 = ipv4;
			pkg.tx_oob.s_oob.is_outer_ipv6 = ipv6;

			pkg.tx_oob.s_oob.comp_udp_csum = 1;
		} else {
			/* Can't do offload of this type of checksum */
			if (skb_checksum_help(skb))
				goto free_sgl_ptr;
		}
	}

	if (mana_map_skb(skb, apc, &pkg))
		goto free_sgl_ptr;

	skb_queue_tail(&txq->pending_skbs, skb);

	len = skb->len;
	net_txq = netdev_get_tx_queue(ndev, txq_idx);

	err = mana_gd_post_work_request(gdma_sq, &pkg.wqe_req,
					(struct gdma_posted_wqe_info *)skb->cb);
	if (!mana_can_tx(gdma_sq)) {
		netif_tx_stop_queue(net_txq);
		apc->eth_stats.stop_queue++;
	}

	if (err) {
		(void)skb_dequeue_tail(&txq->pending_skbs);
		netdev_warn(ndev, "Failed to post TX OOB: %d\n", err);
		err = NETDEV_TX_BUSY;
		goto tx_busy;
	}

	err = NETDEV_TX_OK;
	atomic_inc(&txq->pending_sends);

	mana_gd_wq_ring_doorbell(gd->gdma_context, gdma_sq);

	/* skb may be freed after mana_gd_post_work_request. Do not use it. */
	skb = NULL;

	tx_stats = &txq->stats;
	u64_stats_update_begin(&tx_stats->syncp);
	tx_stats->packets++;
	tx_stats->bytes += len;
	u64_stats_update_end(&tx_stats->syncp);

tx_busy:
	if (netif_tx_queue_stopped(net_txq) && mana_can_tx(gdma_sq)) {
		netif_tx_wake_queue(net_txq);
		apc->eth_stats.wake_queue++;
	}

	kfree(pkg.sgl_ptr);
	return err;

free_sgl_ptr:
	kfree(pkg.sgl_ptr);
tx_drop_count:
	ndev->stats.tx_dropped++;
tx_drop:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static void mana_get_stats64(struct net_device *ndev,
			     struct rtnl_link_stats64 *st)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	unsigned int num_queues = apc->num_queues;
	struct mana_stats_rx *rx_stats;
	struct mana_stats_tx *tx_stats;
	unsigned int start;
	u64 packets, bytes;
	int q;

	if (!apc->port_is_up)
		return;

	netdev_stats_to_stats64(st, &ndev->stats);

	for (q = 0; q < num_queues; q++) {
		rx_stats = &apc->rxqs[q]->stats;

		do {
			start = u64_stats_fetch_begin_irq(&rx_stats->syncp);
			packets = rx_stats->packets;
			bytes = rx_stats->bytes;
		} while (u64_stats_fetch_retry_irq(&rx_stats->syncp, start));

		st->rx_packets += packets;
		st->rx_bytes += bytes;
	}

	for (q = 0; q < num_queues; q++) {
		tx_stats = &apc->tx_qp[q].txq.stats;

		do {
			start = u64_stats_fetch_begin_irq(&tx_stats->syncp);
			packets = tx_stats->packets;
			bytes = tx_stats->bytes;
		} while (u64_stats_fetch_retry_irq(&tx_stats->syncp, start));

		st->tx_packets += packets;
		st->tx_bytes += bytes;
	}
}

static int mana_get_tx_queue(struct net_device *ndev, struct sk_buff *skb,
			     int old_q)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	u32 hash = skb_get_hash(skb);
	struct sock *sk = skb->sk;
	int txq;

	txq = apc->indir_table[hash & MANA_INDIRECT_TABLE_MASK];

	if (txq != old_q && sk && sk_fullsock(sk) &&
	    rcu_access_pointer(sk->sk_dst_cache))
		sk_tx_queue_set(sk, txq);

	return txq;
}

static u16 mana_select_queue(struct net_device *ndev, struct sk_buff *skb,
			     struct net_device *sb_dev)
{
	int txq;

	if (ndev->real_num_tx_queues == 1)
		return 0;

	txq = sk_tx_queue_get(skb->sk);

	if (txq < 0 || skb->ooo_okay || txq >= ndev->real_num_tx_queues) {
		if (skb_rx_queue_recorded(skb))
			txq = skb_get_rx_queue(skb);
		else
			txq = mana_get_tx_queue(ndev, skb, txq);
	}

	return txq;
}

static const struct net_device_ops mana_devops = {
	.ndo_open		= mana_open,
	.ndo_stop		= mana_close,
	.ndo_select_queue	= mana_select_queue,
	.ndo_start_xmit		= mana_start_xmit,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_get_stats64	= mana_get_stats64,
	.ndo_bpf		= mana_bpf,
};

static void mana_cleanup_port_context(struct mana_port_context *apc)
{
	kfree(apc->rxqs);
	apc->rxqs = NULL;
}

static int mana_init_port_context(struct mana_port_context *apc)
{
	apc->rxqs = kcalloc(apc->num_queues, sizeof(struct mana_rxq *),
			    GFP_KERNEL);

	return !apc->rxqs ? -ENOMEM : 0;
}

static int mana_send_request(struct mana_context *ac, void *in_buf,
			     u32 in_len, void *out_buf, u32 out_len)
{
	struct gdma_context *gc = ac->gdma_dev->gdma_context;
	struct gdma_resp_hdr *resp = out_buf;
	struct gdma_req_hdr *req = in_buf;
	struct device *dev = gc->dev;
	static atomic_t activity_id;
	int err;

	req->dev_id = gc->mana.dev_id;
	req->activity_id = atomic_inc_return(&activity_id);

	err = mana_gd_send_request(gc, in_len, in_buf, out_len,
				   out_buf);
	if (err || resp->status) {
		dev_err(dev, "Failed to send mana message: %d, 0x%x\n",
			err, resp->status);
		return err ? err : -EPROTO;
	}

	if (req->dev_id.as_uint32 != resp->dev_id.as_uint32 ||
	    req->activity_id != resp->activity_id) {
		dev_err(dev, "Unexpected mana message response: %x,%x,%x,%x\n",
			req->dev_id.as_uint32, resp->dev_id.as_uint32,
			req->activity_id, resp->activity_id);
		return -EPROTO;
	}

	return 0;
}

static int mana_verify_resp_hdr(const struct gdma_resp_hdr *resp_hdr,
				const enum mana_command_code expected_code,
				const u32 min_size)
{
	if (resp_hdr->response.msg_type != expected_code)
		return -EPROTO;

	if (resp_hdr->response.msg_version < GDMA_MESSAGE_V1)
		return -EPROTO;

	if (resp_hdr->response.msg_size < min_size)
		return -EPROTO;

	return 0;
}

static int mana_query_device_cfg(struct mana_context *ac, u32 proto_major_ver,
				 u32 proto_minor_ver, u32 proto_micro_ver,
				 u16 *max_num_vports)
{
	struct gdma_context *gc = ac->gdma_dev->gdma_context;
	struct mana_query_device_cfg_resp resp = {};
	struct mana_query_device_cfg_req req = {};
	struct device *dev = gc->dev;
	int err = 0;

	mana_gd_init_req_hdr(&req.hdr, MANA_QUERY_DEV_CONFIG,
			     sizeof(req), sizeof(resp));
	req.proto_major_ver = proto_major_ver;
	req.proto_minor_ver = proto_minor_ver;
	req.proto_micro_ver = proto_micro_ver;

	err = mana_send_request(ac, &req, sizeof(req), &resp, sizeof(resp));
	if (err) {
		dev_err(dev, "Failed to query config: %d", err);
		return err;
	}

	err = mana_verify_resp_hdr(&resp.hdr, MANA_QUERY_DEV_CONFIG,
				   sizeof(resp));
	if (err || resp.hdr.status) {
		dev_err(dev, "Invalid query result: %d, 0x%x\n", err,
			resp.hdr.status);
		if (!err)
			err = -EPROTO;
		return err;
	}

	*max_num_vports = resp.max_num_vports;

	return 0;
}

static int mana_query_vport_cfg(struct mana_port_context *apc, u32 vport_index,
				u32 *max_sq, u32 *max_rq, u32 *num_indir_entry)
{
	struct mana_query_vport_cfg_resp resp = {};
	struct mana_query_vport_cfg_req req = {};
	int err;

	mana_gd_init_req_hdr(&req.hdr, MANA_QUERY_VPORT_CONFIG,
			     sizeof(req), sizeof(resp));

	req.vport_index = vport_index;

	err = mana_send_request(apc->ac, &req, sizeof(req), &resp,
				sizeof(resp));
	if (err)
		return err;

	err = mana_verify_resp_hdr(&resp.hdr, MANA_QUERY_VPORT_CONFIG,
				   sizeof(resp));
	if (err)
		return err;

	if (resp.hdr.status)
		return -EPROTO;

	*max_sq = resp.max_num_sq;
	*max_rq = resp.max_num_rq;
	*num_indir_entry = resp.num_indirection_ent;

	apc->port_handle = resp.vport;
	ether_addr_copy(apc->mac_addr, resp.mac_addr);

	return 0;
}

static int mana_cfg_vport(struct mana_port_context *apc, u32 protection_dom_id,
			  u32 doorbell_pg_id)
{
	struct mana_config_vport_resp resp = {};
	struct mana_config_vport_req req = {};
	int err;

	mana_gd_init_req_hdr(&req.hdr, MANA_CONFIG_VPORT_TX,
			     sizeof(req), sizeof(resp));
	req.vport = apc->port_handle;
	req.pdid = protection_dom_id;
	req.doorbell_pageid = doorbell_pg_id;

	err = mana_send_request(apc->ac, &req, sizeof(req), &resp,
				sizeof(resp));
	if (err) {
		netdev_err(apc->ndev, "Failed to configure vPort: %d\n", err);
		goto out;
	}

	err = mana_verify_resp_hdr(&resp.hdr, MANA_CONFIG_VPORT_TX,
				   sizeof(resp));
	if (err || resp.hdr.status) {
		netdev_err(apc->ndev, "Failed to configure vPort: %d, 0x%x\n",
			   err, resp.hdr.status);
		if (!err)
			err = -EPROTO;

		goto out;
	}

	apc->tx_shortform_allowed = resp.short_form_allowed;
	apc->tx_vp_offset = resp.tx_vport_offset;
out:
	return err;
}

static int mana_cfg_vport_steering(struct mana_port_context *apc,
				   enum TRI_STATE rx,
				   bool update_default_rxobj, bool update_key,
				   bool update_tab)
{
	u16 num_entries = MANA_INDIRECT_TABLE_SIZE;
	struct mana_cfg_rx_steer_req *req = NULL;
	struct mana_cfg_rx_steer_resp resp = {};
	struct net_device *ndev = apc->ndev;
	mana_handle_t *req_indir_tab;
	u32 req_buf_size;
	int err;

	req_buf_size = sizeof(*req) + sizeof(mana_handle_t) * num_entries;
	req = kzalloc(req_buf_size, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	mana_gd_init_req_hdr(&req->hdr, MANA_CONFIG_VPORT_RX, req_buf_size,
			     sizeof(resp));

	req->vport = apc->port_handle;
	req->num_indir_entries = num_entries;
	req->indir_tab_offset = sizeof(*req);
	req->rx_enable = rx;
	req->rss_enable = apc->rss_state;
	req->update_default_rxobj = update_default_rxobj;
	req->update_hashkey = update_key;
	req->update_indir_tab = update_tab;
	req->default_rxobj = apc->default_rxobj;

	if (update_key)
		memcpy(&req->hashkey, apc->hashkey, MANA_HASH_KEY_SIZE);

	if (update_tab) {
		req_indir_tab = (mana_handle_t *)(req + 1);
		memcpy(req_indir_tab, apc->rxobj_table,
		       req->num_indir_entries * sizeof(mana_handle_t));
	}

	err = mana_send_request(apc->ac, req, req_buf_size, &resp,
				sizeof(resp));
	if (err) {
		netdev_err(ndev, "Failed to configure vPort RX: %d\n", err);
		goto out;
	}

	err = mana_verify_resp_hdr(&resp.hdr, MANA_CONFIG_VPORT_RX,
				   sizeof(resp));
	if (err) {
		netdev_err(ndev, "vPort RX configuration failed: %d\n", err);
		goto out;
	}

	if (resp.hdr.status) {
		netdev_err(ndev, "vPort RX configuration failed: 0x%x\n",
			   resp.hdr.status);
		err = -EPROTO;
	}
out:
	kfree(req);
	return err;
}

static int mana_create_wq_obj(struct mana_port_context *apc,
			      mana_handle_t vport,
			      u32 wq_type, struct mana_obj_spec *wq_spec,
			      struct mana_obj_spec *cq_spec,
			      mana_handle_t *wq_obj)
{
	struct mana_create_wqobj_resp resp = {};
	struct mana_create_wqobj_req req = {};
	struct net_device *ndev = apc->ndev;
	int err;

	mana_gd_init_req_hdr(&req.hdr, MANA_CREATE_WQ_OBJ,
			     sizeof(req), sizeof(resp));
	req.vport = vport;
	req.wq_type = wq_type;
	req.wq_gdma_region = wq_spec->gdma_region;
	req.cq_gdma_region = cq_spec->gdma_region;
	req.wq_size = wq_spec->queue_size;
	req.cq_size = cq_spec->queue_size;
	req.cq_moderation_ctx_id = cq_spec->modr_ctx_id;
	req.cq_parent_qid = cq_spec->attached_eq;

	err = mana_send_request(apc->ac, &req, sizeof(req), &resp,
				sizeof(resp));
	if (err) {
		netdev_err(ndev, "Failed to create WQ object: %d\n", err);
		goto out;
	}

	err = mana_verify_resp_hdr(&resp.hdr, MANA_CREATE_WQ_OBJ,
				   sizeof(resp));
	if (err || resp.hdr.status) {
		netdev_err(ndev, "Failed to create WQ object: %d, 0x%x\n", err,
			   resp.hdr.status);
		if (!err)
			err = -EPROTO;
		goto out;
	}

	if (resp.wq_obj == INVALID_MANA_HANDLE) {
		netdev_err(ndev, "Got an invalid WQ object handle\n");
		err = -EPROTO;
		goto out;
	}

	*wq_obj = resp.wq_obj;
	wq_spec->queue_index = resp.wq_id;
	cq_spec->queue_index = resp.cq_id;

	return 0;
out:
	return err;
}

static void mana_destroy_wq_obj(struct mana_port_context *apc, u32 wq_type,
				mana_handle_t wq_obj)
{
	struct mana_destroy_wqobj_resp resp = {};
	struct mana_destroy_wqobj_req req = {};
	struct net_device *ndev = apc->ndev;
	int err;

	mana_gd_init_req_hdr(&req.hdr, MANA_DESTROY_WQ_OBJ,
			     sizeof(req), sizeof(resp));
	req.wq_type = wq_type;
	req.wq_obj_handle = wq_obj;

	err = mana_send_request(apc->ac, &req, sizeof(req), &resp,
				sizeof(resp));
	if (err) {
		netdev_err(ndev, "Failed to destroy WQ object: %d\n", err);
		return;
	}

	err = mana_verify_resp_hdr(&resp.hdr, MANA_DESTROY_WQ_OBJ,
				   sizeof(resp));
	if (err || resp.hdr.status)
		netdev_err(ndev, "Failed to destroy WQ object: %d, 0x%x\n", err,
			   resp.hdr.status);
}

static void mana_destroy_eq(struct mana_context *ac)
{
	struct gdma_context *gc = ac->gdma_dev->gdma_context;
	struct gdma_queue *eq;
	int i;

	if (!ac->eqs)
		return;

	for (i = 0; i < gc->max_num_queues; i++) {
		eq = ac->eqs[i].eq;
		if (!eq)
			continue;

		mana_gd_destroy_queue(gc, eq);
	}

	kfree(ac->eqs);
	ac->eqs = NULL;
}

static int mana_create_eq(struct mana_context *ac)
{
	struct gdma_dev *gd = ac->gdma_dev;
	struct gdma_context *gc = gd->gdma_context;
	struct gdma_queue_spec spec = {};
	int err;
	int i;

	ac->eqs = kcalloc(gc->max_num_queues, sizeof(struct mana_eq),
			  GFP_KERNEL);
	if (!ac->eqs)
		return -ENOMEM;

	spec.type = GDMA_EQ;
	spec.monitor_avl_buf = false;
	spec.queue_size = EQ_SIZE;
	spec.eq.callback = NULL;
	spec.eq.context = ac->eqs;
	spec.eq.log2_throttle_limit = LOG2_EQ_THROTTLE;

	for (i = 0; i < gc->max_num_queues; i++) {
		err = mana_gd_create_mana_eq(gd, &spec, &ac->eqs[i].eq);
		if (err)
			goto out;
	}

	return 0;
out:
	mana_destroy_eq(ac);
	return err;
}

static int mana_fence_rq(struct mana_port_context *apc, struct mana_rxq *rxq)
{
	struct mana_fence_rq_resp resp = {};
	struct mana_fence_rq_req req = {};
	int err;

	init_completion(&rxq->fence_event);

	mana_gd_init_req_hdr(&req.hdr, MANA_FENCE_RQ,
			     sizeof(req), sizeof(resp));
	req.wq_obj_handle =  rxq->rxobj;

	err = mana_send_request(apc->ac, &req, sizeof(req), &resp,
				sizeof(resp));
	if (err) {
		netdev_err(apc->ndev, "Failed to fence RQ %u: %d\n",
			   rxq->rxq_idx, err);
		return err;
	}

	err = mana_verify_resp_hdr(&resp.hdr, MANA_FENCE_RQ, sizeof(resp));
	if (err || resp.hdr.status) {
		netdev_err(apc->ndev, "Failed to fence RQ %u: %d, 0x%x\n",
			   rxq->rxq_idx, err, resp.hdr.status);
		if (!err)
			err = -EPROTO;

		return err;
	}

	if (wait_for_completion_timeout(&rxq->fence_event, 10 * HZ) == 0) {
		netdev_err(apc->ndev, "Failed to fence RQ %u: timed out\n",
			   rxq->rxq_idx);
		return -ETIMEDOUT;
	}

	return 0;
}

static void mana_fence_rqs(struct mana_port_context *apc)
{
	unsigned int rxq_idx;
	struct mana_rxq *rxq;
	int err;

	for (rxq_idx = 0; rxq_idx < apc->num_queues; rxq_idx++) {
		rxq = apc->rxqs[rxq_idx];
		err = mana_fence_rq(apc, rxq);

		/* In case of any error, use sleep instead. */
		if (err)
			msleep(100);
	}
}

static int mana_move_wq_tail(struct gdma_queue *wq, u32 num_units)
{
	u32 used_space_old;
	u32 used_space_new;

	used_space_old = wq->head - wq->tail;
	used_space_new = wq->head - (wq->tail + num_units);

	if (WARN_ON_ONCE(used_space_new > used_space_old))
		return -ERANGE;

	wq->tail += num_units;
	return 0;
}

static void mana_unmap_skb(struct sk_buff *skb, struct mana_port_context *apc)
{
	struct mana_skb_head *ash = (struct mana_skb_head *)skb->head;
	struct gdma_context *gc = apc->ac->gdma_dev->gdma_context;
	struct device *dev = gc->dev;
	int i;

	dma_unmap_single(dev, ash->dma_handle[0], ash->size[0], DMA_TO_DEVICE);

	for (i = 1; i < skb_shinfo(skb)->nr_frags + 1; i++)
		dma_unmap_page(dev, ash->dma_handle[i], ash->size[i],
			       DMA_TO_DEVICE);
}

static void mana_poll_tx_cq(struct mana_cq *cq)
{
	struct gdma_comp *completions = cq->gdma_comp_buf;
	struct gdma_posted_wqe_info *wqe_info;
	unsigned int pkt_transmitted = 0;
	unsigned int wqe_unit_cnt = 0;
	struct mana_txq *txq = cq->txq;
	struct mana_port_context *apc;
	struct netdev_queue *net_txq;
	struct gdma_queue *gdma_wq;
	unsigned int avail_space;
	struct net_device *ndev;
	struct sk_buff *skb;
	bool txq_stopped;
	int comp_read;
	int i;

	ndev = txq->ndev;
	apc = netdev_priv(ndev);

	comp_read = mana_gd_poll_cq(cq->gdma_cq, completions,
				    CQE_POLLING_BUFFER);

	if (comp_read < 1)
		return;

	for (i = 0; i < comp_read; i++) {
		struct mana_tx_comp_oob *cqe_oob;

		if (WARN_ON_ONCE(!completions[i].is_sq))
			return;

		cqe_oob = (struct mana_tx_comp_oob *)completions[i].cqe_data;
		if (WARN_ON_ONCE(cqe_oob->cqe_hdr.client_type !=
				 MANA_CQE_COMPLETION))
			return;

		switch (cqe_oob->cqe_hdr.cqe_type) {
		case CQE_TX_OKAY:
			break;

		case CQE_TX_SA_DROP:
		case CQE_TX_MTU_DROP:
		case CQE_TX_INVALID_OOB:
		case CQE_TX_INVALID_ETH_TYPE:
		case CQE_TX_HDR_PROCESSING_ERROR:
		case CQE_TX_VF_DISABLED:
		case CQE_TX_VPORT_IDX_OUT_OF_RANGE:
		case CQE_TX_VPORT_DISABLED:
		case CQE_TX_VLAN_TAGGING_VIOLATION:
			WARN_ONCE(1, "TX: CQE error %d: ignored.\n",
				  cqe_oob->cqe_hdr.cqe_type);
			break;

		default:
			/* If the CQE type is unexpected, log an error, assert,
			 * and go through the error path.
			 */
			WARN_ONCE(1, "TX: Unexpected CQE type %d: HW BUG?\n",
				  cqe_oob->cqe_hdr.cqe_type);
			return;
		}

		if (WARN_ON_ONCE(txq->gdma_txq_id != completions[i].wq_num))
			return;

		skb = skb_dequeue(&txq->pending_skbs);
		if (WARN_ON_ONCE(!skb))
			return;

		wqe_info = (struct gdma_posted_wqe_info *)skb->cb;
		wqe_unit_cnt += wqe_info->wqe_size_in_bu;

		mana_unmap_skb(skb, apc);

		napi_consume_skb(skb, cq->budget);

		pkt_transmitted++;
	}

	if (WARN_ON_ONCE(wqe_unit_cnt == 0))
		return;

	mana_move_wq_tail(txq->gdma_sq, wqe_unit_cnt);

	gdma_wq = txq->gdma_sq;
	avail_space = mana_gd_wq_avail_space(gdma_wq);

	/* Ensure tail updated before checking q stop */
	smp_mb();

	net_txq = txq->net_txq;
	txq_stopped = netif_tx_queue_stopped(net_txq);

	/* Ensure checking txq_stopped before apc->port_is_up. */
	smp_rmb();

	if (txq_stopped && apc->port_is_up && avail_space >= MAX_TX_WQE_SIZE) {
		netif_tx_wake_queue(net_txq);
		apc->eth_stats.wake_queue++;
	}

	if (atomic_sub_return(pkt_transmitted, &txq->pending_sends) < 0)
		WARN_ON_ONCE(1);

	cq->work_done = pkt_transmitted;
}

static void mana_post_pkt_rxq(struct mana_rxq *rxq)
{
	struct mana_recv_buf_oob *recv_buf_oob;
	u32 curr_index;
	int err;

	curr_index = rxq->buf_index++;
	if (rxq->buf_index == rxq->num_rx_buf)
		rxq->buf_index = 0;

	recv_buf_oob = &rxq->rx_oobs[curr_index];

	err = mana_gd_post_and_ring(rxq->gdma_rq, &recv_buf_oob->wqe_req,
				    &recv_buf_oob->wqe_inf);
	if (WARN_ON_ONCE(err))
		return;

	WARN_ON_ONCE(recv_buf_oob->wqe_inf.wqe_size_in_bu != 1);
}

static struct sk_buff *mana_build_skb(void *buf_va, uint pkt_len,
				      struct xdp_buff *xdp)
{
	struct sk_buff *skb = build_skb(buf_va, PAGE_SIZE);

	if (!skb)
		return NULL;

	if (xdp->data_hard_start) {
		skb_reserve(skb, xdp->data - xdp->data_hard_start);
		skb_put(skb, xdp->data_end - xdp->data);
	} else {
		skb_reserve(skb, XDP_PACKET_HEADROOM);
		skb_put(skb, pkt_len);
	}

	return skb;
}

static void mana_rx_skb(void *buf_va, struct mana_rxcomp_oob *cqe,
			struct mana_rxq *rxq)
{
	struct mana_stats_rx *rx_stats = &rxq->stats;
	struct net_device *ndev = rxq->ndev;
	uint pkt_len = cqe->ppi[0].pkt_len;
	u16 rxq_idx = rxq->rxq_idx;
	struct napi_struct *napi;
	struct xdp_buff xdp = {};
	struct sk_buff *skb;
	u32 hash_value;
	u32 act;

	rxq->rx_cq.work_done++;
	napi = &rxq->rx_cq.napi;

	if (!buf_va) {
		++ndev->stats.rx_dropped;
		return;
	}

	act = mana_run_xdp(ndev, rxq, &xdp, buf_va, pkt_len);

	if (act != XDP_PASS && act != XDP_TX)
		goto drop_xdp;

	skb = mana_build_skb(buf_va, pkt_len, &xdp);

	if (!skb)
		goto drop;

	skb->dev = napi->dev;

	skb->protocol = eth_type_trans(skb, ndev);
	skb_checksum_none_assert(skb);
	skb_record_rx_queue(skb, rxq_idx);

	if ((ndev->features & NETIF_F_RXCSUM) && cqe->rx_iphdr_csum_succeed) {
		if (cqe->rx_tcp_csum_succeed || cqe->rx_udp_csum_succeed)
			skb->ip_summed = CHECKSUM_UNNECESSARY;
	}

	if (cqe->rx_hashtype != 0 && (ndev->features & NETIF_F_RXHASH)) {
		hash_value = cqe->ppi[0].pkt_hash;

		if (cqe->rx_hashtype & MANA_HASH_L4)
			skb_set_hash(skb, hash_value, PKT_HASH_TYPE_L4);
		else
			skb_set_hash(skb, hash_value, PKT_HASH_TYPE_L3);
	}

	u64_stats_update_begin(&rx_stats->syncp);
	rx_stats->packets++;
	rx_stats->bytes += pkt_len;

	if (act == XDP_TX)
		rx_stats->xdp_tx++;
	u64_stats_update_end(&rx_stats->syncp);

	if (act == XDP_TX) {
		skb_set_queue_mapping(skb, rxq_idx);
		mana_xdp_tx(skb, ndev);
		return;
	}

	napi_gro_receive(napi, skb);

	return;

drop_xdp:
	u64_stats_update_begin(&rx_stats->syncp);
	rx_stats->xdp_drop++;
	u64_stats_update_end(&rx_stats->syncp);

drop:
	free_page((unsigned long)buf_va);
	++ndev->stats.rx_dropped;

	return;
}

static void mana_process_rx_cqe(struct mana_rxq *rxq, struct mana_cq *cq,
				struct gdma_comp *cqe)
{
	struct mana_rxcomp_oob *oob = (struct mana_rxcomp_oob *)cqe->cqe_data;
	struct gdma_context *gc = rxq->gdma_rq->gdma_dev->gdma_context;
	struct net_device *ndev = rxq->ndev;
	struct mana_recv_buf_oob *rxbuf_oob;
	struct device *dev = gc->dev;
	void *new_buf, *old_buf;
	struct page *new_page;
	u32 curr, pktlen;
	dma_addr_t da;

	switch (oob->cqe_hdr.cqe_type) {
	case CQE_RX_OKAY:
		break;

	case CQE_RX_TRUNCATED:
		netdev_err(ndev, "Dropped a truncated packet\n");
		return;

	case CQE_RX_COALESCED_4:
		netdev_err(ndev, "RX coalescing is unsupported\n");
		return;

	case CQE_RX_OBJECT_FENCE:
		complete(&rxq->fence_event);
		return;

	default:
		netdev_err(ndev, "Unknown RX CQE type = %d\n",
			   oob->cqe_hdr.cqe_type);
		return;
	}

	if (oob->cqe_hdr.cqe_type != CQE_RX_OKAY)
		return;

	pktlen = oob->ppi[0].pkt_len;

	if (pktlen == 0) {
		/* data packets should never have packetlength of zero */
		netdev_err(ndev, "RX pkt len=0, rq=%u, cq=%u, rxobj=0x%llx\n",
			   rxq->gdma_id, cq->gdma_id, rxq->rxobj);
		return;
	}

	curr = rxq->buf_index;
	rxbuf_oob = &rxq->rx_oobs[curr];
	WARN_ON_ONCE(rxbuf_oob->wqe_inf.wqe_size_in_bu != 1);

	new_page = alloc_page(GFP_ATOMIC);

	if (new_page) {
		da = dma_map_page(dev, new_page, XDP_PACKET_HEADROOM, rxq->datasize,
				  DMA_FROM_DEVICE);

		if (dma_mapping_error(dev, da)) {
			__free_page(new_page);
			new_page = NULL;
		}
	}

	new_buf = new_page ? page_to_virt(new_page) : NULL;

	if (new_buf) {
		dma_unmap_page(dev, rxbuf_oob->buf_dma_addr, rxq->datasize,
			       DMA_FROM_DEVICE);

		old_buf = rxbuf_oob->buf_va;

		/* refresh the rxbuf_oob with the new page */
		rxbuf_oob->buf_va = new_buf;
		rxbuf_oob->buf_dma_addr = da;
		rxbuf_oob->sgl[0].address = rxbuf_oob->buf_dma_addr;
	} else {
		old_buf = NULL; /* drop the packet if no memory */
	}

	mana_rx_skb(old_buf, oob, rxq);

	mana_move_wq_tail(rxq->gdma_rq, rxbuf_oob->wqe_inf.wqe_size_in_bu);

	mana_post_pkt_rxq(rxq);
}

static void mana_poll_rx_cq(struct mana_cq *cq)
{
	struct gdma_comp *comp = cq->gdma_comp_buf;
	int comp_read, i;

	comp_read = mana_gd_poll_cq(cq->gdma_cq, comp, CQE_POLLING_BUFFER);
	WARN_ON_ONCE(comp_read > CQE_POLLING_BUFFER);

	for (i = 0; i < comp_read; i++) {
		if (WARN_ON_ONCE(comp[i].is_sq))
			return;

		/* verify recv cqe references the right rxq */
		if (WARN_ON_ONCE(comp[i].wq_num != cq->rxq->gdma_id))
			return;

		mana_process_rx_cqe(cq->rxq, cq, &comp[i]);
	}
}

static void mana_cq_handler(void *context, struct gdma_queue *gdma_queue)
{
	struct mana_cq *cq = context;
	u8 arm_bit;

	WARN_ON_ONCE(cq->gdma_cq != gdma_queue);

	if (cq->type == MANA_CQ_TYPE_RX)
		mana_poll_rx_cq(cq);
	else
		mana_poll_tx_cq(cq);

	if (cq->work_done < cq->budget &&
	    napi_complete_done(&cq->napi, cq->work_done)) {
		arm_bit = SET_ARM_BIT;
	} else {
		arm_bit = 0;
	}

	mana_gd_ring_cq(gdma_queue, arm_bit);
}

static int mana_poll(struct napi_struct *napi, int budget)
{
	struct mana_cq *cq = container_of(napi, struct mana_cq, napi);

	cq->work_done = 0;
	cq->budget = budget;

	mana_cq_handler(cq, cq->gdma_cq);

	return min(cq->work_done, budget);
}

static void mana_schedule_napi(void *context, struct gdma_queue *gdma_queue)
{
	struct mana_cq *cq = context;

	napi_schedule_irqoff(&cq->napi);
}

static void mana_deinit_cq(struct mana_port_context *apc, struct mana_cq *cq)
{
	struct gdma_dev *gd = apc->ac->gdma_dev;

	if (!cq->gdma_cq)
		return;

	mana_gd_destroy_queue(gd->gdma_context, cq->gdma_cq);
}

static void mana_deinit_txq(struct mana_port_context *apc, struct mana_txq *txq)
{
	struct gdma_dev *gd = apc->ac->gdma_dev;

	if (!txq->gdma_sq)
		return;

	mana_gd_destroy_queue(gd->gdma_context, txq->gdma_sq);
}

static void mana_destroy_txq(struct mana_port_context *apc)
{
	struct napi_struct *napi;
	int i;

	if (!apc->tx_qp)
		return;

	for (i = 0; i < apc->num_queues; i++) {
		napi = &apc->tx_qp[i].tx_cq.napi;
		napi_synchronize(napi);
		napi_disable(napi);
		netif_napi_del(napi);

		mana_destroy_wq_obj(apc, GDMA_SQ, apc->tx_qp[i].tx_object);

		mana_deinit_cq(apc, &apc->tx_qp[i].tx_cq);

		mana_deinit_txq(apc, &apc->tx_qp[i].txq);
	}

	kfree(apc->tx_qp);
	apc->tx_qp = NULL;
}

static int mana_create_txq(struct mana_port_context *apc,
			   struct net_device *net)
{
	struct mana_context *ac = apc->ac;
	struct gdma_dev *gd = ac->gdma_dev;
	struct mana_obj_spec wq_spec;
	struct mana_obj_spec cq_spec;
	struct gdma_queue_spec spec;
	struct gdma_context *gc;
	struct mana_txq *txq;
	struct mana_cq *cq;
	u32 txq_size;
	u32 cq_size;
	int err;
	int i;

	apc->tx_qp = kcalloc(apc->num_queues, sizeof(struct mana_tx_qp),
			     GFP_KERNEL);
	if (!apc->tx_qp)
		return -ENOMEM;

	/*  The minimum size of the WQE is 32 bytes, hence
	 *  MAX_SEND_BUFFERS_PER_QUEUE represents the maximum number of WQEs
	 *  the SQ can store. This value is then used to size other queues
	 *  to prevent overflow.
	 */
	txq_size = MAX_SEND_BUFFERS_PER_QUEUE * 32;
	BUILD_BUG_ON(!PAGE_ALIGNED(txq_size));

	cq_size = MAX_SEND_BUFFERS_PER_QUEUE * COMP_ENTRY_SIZE;
	cq_size = PAGE_ALIGN(cq_size);

	gc = gd->gdma_context;

	for (i = 0; i < apc->num_queues; i++) {
		apc->tx_qp[i].tx_object = INVALID_MANA_HANDLE;

		/* Create SQ */
		txq = &apc->tx_qp[i].txq;

		u64_stats_init(&txq->stats.syncp);
		txq->ndev = net;
		txq->net_txq = netdev_get_tx_queue(net, i);
		txq->vp_offset = apc->tx_vp_offset;
		skb_queue_head_init(&txq->pending_skbs);

		memset(&spec, 0, sizeof(spec));
		spec.type = GDMA_SQ;
		spec.monitor_avl_buf = true;
		spec.queue_size = txq_size;
		err = mana_gd_create_mana_wq_cq(gd, &spec, &txq->gdma_sq);
		if (err)
			goto out;

		/* Create SQ's CQ */
		cq = &apc->tx_qp[i].tx_cq;
		cq->type = MANA_CQ_TYPE_TX;

		cq->txq = txq;

		memset(&spec, 0, sizeof(spec));
		spec.type = GDMA_CQ;
		spec.monitor_avl_buf = false;
		spec.queue_size = cq_size;
		spec.cq.callback = mana_schedule_napi;
		spec.cq.parent_eq = ac->eqs[i].eq;
		spec.cq.context = cq;
		err = mana_gd_create_mana_wq_cq(gd, &spec, &cq->gdma_cq);
		if (err)
			goto out;

		memset(&wq_spec, 0, sizeof(wq_spec));
		memset(&cq_spec, 0, sizeof(cq_spec));

		wq_spec.gdma_region = txq->gdma_sq->mem_info.gdma_region;
		wq_spec.queue_size = txq->gdma_sq->queue_size;

		cq_spec.gdma_region = cq->gdma_cq->mem_info.gdma_region;
		cq_spec.queue_size = cq->gdma_cq->queue_size;
		cq_spec.modr_ctx_id = 0;
		cq_spec.attached_eq = cq->gdma_cq->cq.parent->id;

		err = mana_create_wq_obj(apc, apc->port_handle, GDMA_SQ,
					 &wq_spec, &cq_spec,
					 &apc->tx_qp[i].tx_object);

		if (err)
			goto out;

		txq->gdma_sq->id = wq_spec.queue_index;
		cq->gdma_cq->id = cq_spec.queue_index;

		txq->gdma_sq->mem_info.gdma_region = GDMA_INVALID_DMA_REGION;
		cq->gdma_cq->mem_info.gdma_region = GDMA_INVALID_DMA_REGION;

		txq->gdma_txq_id = txq->gdma_sq->id;

		cq->gdma_id = cq->gdma_cq->id;

		if (WARN_ON(cq->gdma_id >= gc->max_num_cqs)) {
			err = -EINVAL;
			goto out;
		}

		gc->cq_table[cq->gdma_id] = cq->gdma_cq;

		netif_tx_napi_add(net, &cq->napi, mana_poll, NAPI_POLL_WEIGHT);
		napi_enable(&cq->napi);

		mana_gd_ring_cq(cq->gdma_cq, SET_ARM_BIT);
	}

	return 0;
out:
	mana_destroy_txq(apc);
	return err;
}

static void mana_destroy_rxq(struct mana_port_context *apc,
			     struct mana_rxq *rxq, bool validate_state)

{
	struct gdma_context *gc = apc->ac->gdma_dev->gdma_context;
	struct mana_recv_buf_oob *rx_oob;
	struct device *dev = gc->dev;
	struct napi_struct *napi;
	int i;

	if (!rxq)
		return;

	napi = &rxq->rx_cq.napi;

	if (validate_state)
		napi_synchronize(napi);

	napi_disable(napi);

	xdp_rxq_info_unreg(&rxq->xdp_rxq);

	netif_napi_del(napi);

	mana_destroy_wq_obj(apc, GDMA_RQ, rxq->rxobj);

	mana_deinit_cq(apc, &rxq->rx_cq);

	for (i = 0; i < rxq->num_rx_buf; i++) {
		rx_oob = &rxq->rx_oobs[i];

		if (!rx_oob->buf_va)
			continue;

		dma_unmap_page(dev, rx_oob->buf_dma_addr, rxq->datasize,
			       DMA_FROM_DEVICE);

		free_page((unsigned long)rx_oob->buf_va);
		rx_oob->buf_va = NULL;
	}

	if (rxq->gdma_rq)
		mana_gd_destroy_queue(gc, rxq->gdma_rq);

	kfree(rxq);
}

#define MANA_WQE_HEADER_SIZE 16
#define MANA_WQE_SGE_SIZE 16

static int mana_alloc_rx_wqe(struct mana_port_context *apc,
			     struct mana_rxq *rxq, u32 *rxq_size, u32 *cq_size)
{
	struct gdma_context *gc = apc->ac->gdma_dev->gdma_context;
	struct mana_recv_buf_oob *rx_oob;
	struct device *dev = gc->dev;
	struct page *page;
	dma_addr_t da;
	u32 buf_idx;

	WARN_ON(rxq->datasize == 0 || rxq->datasize > PAGE_SIZE);

	*rxq_size = 0;
	*cq_size = 0;

	for (buf_idx = 0; buf_idx < rxq->num_rx_buf; buf_idx++) {
		rx_oob = &rxq->rx_oobs[buf_idx];
		memset(rx_oob, 0, sizeof(*rx_oob));

		page = alloc_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;

		da = dma_map_page(dev, page, XDP_PACKET_HEADROOM, rxq->datasize,
				  DMA_FROM_DEVICE);

		if (dma_mapping_error(dev, da)) {
			__free_page(page);
			return -ENOMEM;
		}

		rx_oob->buf_va = page_to_virt(page);
		rx_oob->buf_dma_addr = da;

		rx_oob->num_sge = 1;
		rx_oob->sgl[0].address = rx_oob->buf_dma_addr;
		rx_oob->sgl[0].size = rxq->datasize;
		rx_oob->sgl[0].mem_key = apc->ac->gdma_dev->gpa_mkey;

		rx_oob->wqe_req.sgl = rx_oob->sgl;
		rx_oob->wqe_req.num_sge = rx_oob->num_sge;
		rx_oob->wqe_req.inline_oob_size = 0;
		rx_oob->wqe_req.inline_oob_data = NULL;
		rx_oob->wqe_req.flags = 0;
		rx_oob->wqe_req.client_data_unit = 0;

		*rxq_size += ALIGN(MANA_WQE_HEADER_SIZE +
				   MANA_WQE_SGE_SIZE * rx_oob->num_sge, 32);
		*cq_size += COMP_ENTRY_SIZE;
	}

	return 0;
}

static int mana_push_wqe(struct mana_rxq *rxq)
{
	struct mana_recv_buf_oob *rx_oob;
	u32 buf_idx;
	int err;

	for (buf_idx = 0; buf_idx < rxq->num_rx_buf; buf_idx++) {
		rx_oob = &rxq->rx_oobs[buf_idx];

		err = mana_gd_post_and_ring(rxq->gdma_rq, &rx_oob->wqe_req,
					    &rx_oob->wqe_inf);
		if (err)
			return -ENOSPC;
	}

	return 0;
}

static struct mana_rxq *mana_create_rxq(struct mana_port_context *apc,
					u32 rxq_idx, struct mana_eq *eq,
					struct net_device *ndev)
{
	struct gdma_dev *gd = apc->ac->gdma_dev;
	struct mana_obj_spec wq_spec;
	struct mana_obj_spec cq_spec;
	struct gdma_queue_spec spec;
	struct mana_cq *cq = NULL;
	struct gdma_context *gc;
	u32 cq_size, rq_size;
	struct mana_rxq *rxq;
	int err;

	gc = gd->gdma_context;

	rxq = kzalloc(struct_size(rxq, rx_oobs, RX_BUFFERS_PER_QUEUE),
		      GFP_KERNEL);
	if (!rxq)
		return NULL;

	rxq->ndev = ndev;
	rxq->num_rx_buf = RX_BUFFERS_PER_QUEUE;
	rxq->rxq_idx = rxq_idx;
	rxq->datasize = ALIGN(MAX_FRAME_SIZE, 64);
	rxq->rxobj = INVALID_MANA_HANDLE;

	err = mana_alloc_rx_wqe(apc, rxq, &rq_size, &cq_size);
	if (err)
		goto out;

	rq_size = PAGE_ALIGN(rq_size);
	cq_size = PAGE_ALIGN(cq_size);

	/* Create RQ */
	memset(&spec, 0, sizeof(spec));
	spec.type = GDMA_RQ;
	spec.monitor_avl_buf = true;
	spec.queue_size = rq_size;
	err = mana_gd_create_mana_wq_cq(gd, &spec, &rxq->gdma_rq);
	if (err)
		goto out;

	/* Create RQ's CQ */
	cq = &rxq->rx_cq;
	cq->type = MANA_CQ_TYPE_RX;
	cq->rxq = rxq;

	memset(&spec, 0, sizeof(spec));
	spec.type = GDMA_CQ;
	spec.monitor_avl_buf = false;
	spec.queue_size = cq_size;
	spec.cq.callback = mana_schedule_napi;
	spec.cq.parent_eq = eq->eq;
	spec.cq.context = cq;
	err = mana_gd_create_mana_wq_cq(gd, &spec, &cq->gdma_cq);
	if (err)
		goto out;

	memset(&wq_spec, 0, sizeof(wq_spec));
	memset(&cq_spec, 0, sizeof(cq_spec));
	wq_spec.gdma_region = rxq->gdma_rq->mem_info.gdma_region;
	wq_spec.queue_size = rxq->gdma_rq->queue_size;

	cq_spec.gdma_region = cq->gdma_cq->mem_info.gdma_region;
	cq_spec.queue_size = cq->gdma_cq->queue_size;
	cq_spec.modr_ctx_id = 0;
	cq_spec.attached_eq = cq->gdma_cq->cq.parent->id;

	err = mana_create_wq_obj(apc, apc->port_handle, GDMA_RQ,
				 &wq_spec, &cq_spec, &rxq->rxobj);
	if (err)
		goto out;

	rxq->gdma_rq->id = wq_spec.queue_index;
	cq->gdma_cq->id = cq_spec.queue_index;

	rxq->gdma_rq->mem_info.gdma_region = GDMA_INVALID_DMA_REGION;
	cq->gdma_cq->mem_info.gdma_region = GDMA_INVALID_DMA_REGION;

	rxq->gdma_id = rxq->gdma_rq->id;
	cq->gdma_id = cq->gdma_cq->id;

	err = mana_push_wqe(rxq);
	if (err)
		goto out;

	if (WARN_ON(cq->gdma_id >= gc->max_num_cqs)) {
		err = -EINVAL;
		goto out;
	}

	gc->cq_table[cq->gdma_id] = cq->gdma_cq;

	netif_napi_add(ndev, &cq->napi, mana_poll, 1);

	WARN_ON(xdp_rxq_info_reg(&rxq->xdp_rxq, ndev, rxq_idx,
				 cq->napi.napi_id));
	WARN_ON(xdp_rxq_info_reg_mem_model(&rxq->xdp_rxq,
					   MEM_TYPE_PAGE_SHARED, NULL));

	napi_enable(&cq->napi);

	mana_gd_ring_cq(cq->gdma_cq, SET_ARM_BIT);
out:
	if (!err)
		return rxq;

	netdev_err(ndev, "Failed to create RXQ: err = %d\n", err);

	mana_destroy_rxq(apc, rxq, false);

	if (cq)
		mana_deinit_cq(apc, cq);

	return NULL;
}

static int mana_add_rx_queues(struct mana_port_context *apc,
			      struct net_device *ndev)
{
	struct mana_context *ac = apc->ac;
	struct mana_rxq *rxq;
	int err = 0;
	int i;

	for (i = 0; i < apc->num_queues; i++) {
		rxq = mana_create_rxq(apc, i, &ac->eqs[i], ndev);
		if (!rxq) {
			err = -ENOMEM;
			goto out;
		}

		u64_stats_init(&rxq->stats.syncp);

		apc->rxqs[i] = rxq;
	}

	apc->default_rxobj = apc->rxqs[0]->rxobj;
out:
	return err;
}

static void mana_destroy_vport(struct mana_port_context *apc)
{
	struct mana_rxq *rxq;
	u32 rxq_idx;

	for (rxq_idx = 0; rxq_idx < apc->num_queues; rxq_idx++) {
		rxq = apc->rxqs[rxq_idx];
		if (!rxq)
			continue;

		mana_destroy_rxq(apc, rxq, true);
		apc->rxqs[rxq_idx] = NULL;
	}

	mana_destroy_txq(apc);
}

static int mana_create_vport(struct mana_port_context *apc,
			     struct net_device *net)
{
	struct gdma_dev *gd = apc->ac->gdma_dev;
	int err;

	apc->default_rxobj = INVALID_MANA_HANDLE;

	err = mana_cfg_vport(apc, gd->pdid, gd->doorbell);
	if (err)
		return err;

	return mana_create_txq(apc, net);
}

static void mana_rss_table_init(struct mana_port_context *apc)
{
	int i;

	for (i = 0; i < MANA_INDIRECT_TABLE_SIZE; i++)
		apc->indir_table[i] =
			ethtool_rxfh_indir_default(i, apc->num_queues);
}

int mana_config_rss(struct mana_port_context *apc, enum TRI_STATE rx,
		    bool update_hash, bool update_tab)
{
	u32 queue_idx;
	int err;
	int i;

	if (update_tab) {
		for (i = 0; i < MANA_INDIRECT_TABLE_SIZE; i++) {
			queue_idx = apc->indir_table[i];
			apc->rxobj_table[i] = apc->rxqs[queue_idx]->rxobj;
		}
	}

	err = mana_cfg_vport_steering(apc, rx, true, update_hash, update_tab);
	if (err)
		return err;

	mana_fence_rqs(apc);

	return 0;
}

static int mana_init_port(struct net_device *ndev)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	u32 max_txq, max_rxq, max_queues;
	int port_idx = apc->port_idx;
	u32 num_indirect_entries;
	int err;

	err = mana_init_port_context(apc);
	if (err)
		return err;

	err = mana_query_vport_cfg(apc, port_idx, &max_txq, &max_rxq,
				   &num_indirect_entries);
	if (err) {
		netdev_err(ndev, "Failed to query info for vPort %d\n",
			   port_idx);
		goto reset_apc;
	}

	max_queues = min_t(u32, max_txq, max_rxq);
	if (apc->max_queues > max_queues)
		apc->max_queues = max_queues;

	if (apc->num_queues > apc->max_queues)
		apc->num_queues = apc->max_queues;

	eth_hw_addr_set(ndev, apc->mac_addr);

	return 0;

reset_apc:
	kfree(apc->rxqs);
	apc->rxqs = NULL;
	return err;
}

int mana_alloc_queues(struct net_device *ndev)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	int err;

	err = mana_create_vport(apc, ndev);
	if (err)
		return err;

	err = netif_set_real_num_tx_queues(ndev, apc->num_queues);
	if (err)
		goto destroy_vport;

	err = mana_add_rx_queues(apc, ndev);
	if (err)
		goto destroy_vport;

	apc->rss_state = apc->num_queues > 1 ? TRI_STATE_TRUE : TRI_STATE_FALSE;

	err = netif_set_real_num_rx_queues(ndev, apc->num_queues);
	if (err)
		goto destroy_vport;

	mana_rss_table_init(apc);

	err = mana_config_rss(apc, TRI_STATE_TRUE, true, true);
	if (err)
		goto destroy_vport;

	mana_chn_setxdp(apc, mana_xdp_get(apc));

	return 0;

destroy_vport:
	mana_destroy_vport(apc);
	return err;
}

int mana_attach(struct net_device *ndev)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	int err;

	ASSERT_RTNL();

	err = mana_init_port(ndev);
	if (err)
		return err;

	if (apc->port_st_save) {
		err = mana_alloc_queues(ndev);
		if (err) {
			mana_cleanup_port_context(apc);
			return err;
		}
	}

	apc->port_is_up = apc->port_st_save;

	/* Ensure port state updated before txq state */
	smp_wmb();

	if (apc->port_is_up)
		netif_carrier_on(ndev);

	netif_device_attach(ndev);

	return 0;
}

static int mana_dealloc_queues(struct net_device *ndev)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	struct mana_txq *txq;
	int i, err;

	if (apc->port_is_up)
		return -EINVAL;

	mana_chn_setxdp(apc, NULL);

	/* No packet can be transmitted now since apc->port_is_up is false.
	 * There is still a tiny chance that mana_poll_tx_cq() can re-enable
	 * a txq because it may not timely see apc->port_is_up being cleared
	 * to false, but it doesn't matter since mana_start_xmit() drops any
	 * new packets due to apc->port_is_up being false.
	 *
	 * Drain all the in-flight TX packets
	 */
	for (i = 0; i < apc->num_queues; i++) {
		txq = &apc->tx_qp[i].txq;

		while (atomic_read(&txq->pending_sends) > 0)
			usleep_range(1000, 2000);
	}

	/* We're 100% sure the queues can no longer be woken up, because
	 * we're sure now mana_poll_tx_cq() can't be running.
	 */

	apc->rss_state = TRI_STATE_FALSE;
	err = mana_config_rss(apc, TRI_STATE_FALSE, false, false);
	if (err) {
		netdev_err(ndev, "Failed to disable vPort: %d\n", err);
		return err;
	}

	mana_destroy_vport(apc);

	return 0;
}

int mana_detach(struct net_device *ndev, bool from_close)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	int err;

	ASSERT_RTNL();

	apc->port_st_save = apc->port_is_up;
	apc->port_is_up = false;

	/* Ensure port state updated before txq state */
	smp_wmb();

	netif_tx_disable(ndev);
	netif_carrier_off(ndev);

	if (apc->port_st_save) {
		err = mana_dealloc_queues(ndev);
		if (err)
			return err;
	}

	if (!from_close) {
		netif_device_detach(ndev);
		mana_cleanup_port_context(apc);
	}

	return 0;
}

static int mana_probe_port(struct mana_context *ac, int port_idx,
			   struct net_device **ndev_storage)
{
	struct gdma_context *gc = ac->gdma_dev->gdma_context;
	struct mana_port_context *apc;
	struct net_device *ndev;
	int err;

	ndev = alloc_etherdev_mq(sizeof(struct mana_port_context),
				 gc->max_num_queues);
	if (!ndev)
		return -ENOMEM;

	*ndev_storage = ndev;

	apc = netdev_priv(ndev);
	apc->ac = ac;
	apc->ndev = ndev;
	apc->max_queues = gc->max_num_queues;
	apc->num_queues = gc->max_num_queues;
	apc->port_handle = INVALID_MANA_HANDLE;
	apc->port_idx = port_idx;

	ndev->netdev_ops = &mana_devops;
	ndev->ethtool_ops = &mana_ethtool_ops;
	ndev->mtu = ETH_DATA_LEN;
	ndev->max_mtu = ndev->mtu;
	ndev->min_mtu = ndev->mtu;
	ndev->needed_headroom = MANA_HEADROOM;
	SET_NETDEV_DEV(ndev, gc->dev);

	netif_carrier_off(ndev);

	netdev_rss_key_fill(apc->hashkey, MANA_HASH_KEY_SIZE);

	err = mana_init_port(ndev);
	if (err)
		goto free_net;

	netdev_lockdep_set_classes(ndev);

	ndev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;
	ndev->hw_features |= NETIF_F_RXCSUM;
	ndev->hw_features |= NETIF_F_TSO | NETIF_F_TSO6;
	ndev->hw_features |= NETIF_F_RXHASH;
	ndev->features = ndev->hw_features;
	ndev->vlan_features = 0;

	err = register_netdev(ndev);
	if (err) {
		netdev_err(ndev, "Unable to register netdev.\n");
		goto reset_apc;
	}

	return 0;

reset_apc:
	kfree(apc->rxqs);
	apc->rxqs = NULL;
free_net:
	*ndev_storage = NULL;
	netdev_err(ndev, "Failed to probe vPort %d: %d\n", port_idx, err);
	free_netdev(ndev);
	return err;
}

int mana_probe(struct gdma_dev *gd, bool resuming)
{
	struct gdma_context *gc = gd->gdma_context;
	struct mana_context *ac = gd->driver_data;
	struct device *dev = gc->dev;
	u16 num_ports = 0;
	int err;
	int i;

	dev_info(dev,
		 "Microsoft Azure Network Adapter protocol version: %d.%d.%d\n",
		 MANA_MAJOR_VERSION, MANA_MINOR_VERSION, MANA_MICRO_VERSION);

	err = mana_gd_register_device(gd);
	if (err)
		return err;

	if (!resuming) {
		ac = kzalloc(sizeof(*ac), GFP_KERNEL);
		if (!ac)
			return -ENOMEM;

		ac->gdma_dev = gd;
		gd->driver_data = ac;
	}

	err = mana_create_eq(ac);
	if (err)
		goto out;

	err = mana_query_device_cfg(ac, MANA_MAJOR_VERSION, MANA_MINOR_VERSION,
				    MANA_MICRO_VERSION, &num_ports);
	if (err)
		goto out;

	if (!resuming) {
		ac->num_ports = num_ports;
	} else {
		if (ac->num_ports != num_ports) {
			dev_err(dev, "The number of vPorts changed: %d->%d\n",
				ac->num_ports, num_ports);
			err = -EPROTO;
			goto out;
		}
	}

	if (ac->num_ports == 0)
		dev_err(dev, "Failed to detect any vPort\n");

	if (ac->num_ports > MAX_PORTS_IN_MANA_DEV)
		ac->num_ports = MAX_PORTS_IN_MANA_DEV;

	if (!resuming) {
		for (i = 0; i < ac->num_ports; i++) {
			err = mana_probe_port(ac, i, &ac->ports[i]);
			if (err)
				break;
		}
	} else {
		for (i = 0; i < ac->num_ports; i++) {
			rtnl_lock();
			err = mana_attach(ac->ports[i]);
			rtnl_unlock();
			if (err)
				break;
		}
	}
out:
	if (err)
		mana_remove(gd, false);

	return err;
}

void mana_remove(struct gdma_dev *gd, bool suspending)
{
	struct gdma_context *gc = gd->gdma_context;
	struct mana_context *ac = gd->driver_data;
	struct device *dev = gc->dev;
	struct net_device *ndev;
	int err;
	int i;

	for (i = 0; i < ac->num_ports; i++) {
		ndev = ac->ports[i];
		if (!ndev) {
			if (i == 0)
				dev_err(dev, "No net device to remove\n");
			goto out;
		}

		/* All cleanup actions should stay after rtnl_lock(), otherwise
		 * other functions may access partially cleaned up data.
		 */
		rtnl_lock();

		err = mana_detach(ndev, false);
		if (err)
			netdev_err(ndev, "Failed to detach vPort %d: %d\n",
				   i, err);

		if (suspending) {
			/* No need to unregister the ndev. */
			rtnl_unlock();
			continue;
		}

		unregister_netdevice(ndev);

		rtnl_unlock();

		free_netdev(ndev);
	}

	mana_destroy_eq(ac);

out:
	mana_gd_deregister_device(gd);

	if (suspending)
		return;

	gd->driver_data = NULL;
	gd->gdma_context = NULL;
	kfree(ac);
}
