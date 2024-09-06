// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2021, Microsoft Corporation. */

#include <uapi/linux/bpf.h>

#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/filter.h>
#include <linux/mm.h>
#include <linux/pci.h>

#include <net/checksum.h>
#include <net/ip6_checksum.h>
#include <net/page_pool/helpers.h>
#include <net/xdp.h>

#include <net/mana/mana.h>
#include <net/mana/mana_auxiliary.h>

static DEFINE_IDA(mana_adev_ida);

static int mana_adev_idx_alloc(void)
{
	return ida_alloc(&mana_adev_ida, GFP_KERNEL);
}

static void mana_adev_idx_free(int idx)
{
	ida_free(&mana_adev_ida, idx);
}

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

static void mana_add_sge(struct mana_tx_package *tp, struct mana_skb_head *ash,
			 int sg_i, dma_addr_t da, int sge_len, u32 gpa_mkey)
{
	ash->dma_handle[sg_i] = da;
	ash->size[sg_i] = sge_len;

	tp->wqe_req.sgl[sg_i].address = da;
	tp->wqe_req.sgl[sg_i].mem_key = gpa_mkey;
	tp->wqe_req.sgl[sg_i].size = sge_len;
}

static int mana_map_skb(struct sk_buff *skb, struct mana_port_context *apc,
			struct mana_tx_package *tp, int gso_hs)
{
	struct mana_skb_head *ash = (struct mana_skb_head *)skb->head;
	int hsg = 1; /* num of SGEs of linear part */
	struct gdma_dev *gd = apc->ac->gdma_dev;
	int skb_hlen = skb_headlen(skb);
	int sge0_len, sge1_len = 0;
	struct gdma_context *gc;
	struct device *dev;
	skb_frag_t *frag;
	dma_addr_t da;
	int sg_i;
	int i;

	gc = gd->gdma_context;
	dev = gc->dev;

	if (gso_hs && gso_hs < skb_hlen) {
		sge0_len = gso_hs;
		sge1_len = skb_hlen - gso_hs;
	} else {
		sge0_len = skb_hlen;
	}

	da = dma_map_single(dev, skb->data, sge0_len, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, da))
		return -ENOMEM;

	mana_add_sge(tp, ash, 0, da, sge0_len, gd->gpa_mkey);

	if (sge1_len) {
		sg_i = 1;
		da = dma_map_single(dev, skb->data + sge0_len, sge1_len,
				    DMA_TO_DEVICE);
		if (dma_mapping_error(dev, da))
			goto frag_err;

		mana_add_sge(tp, ash, sg_i, da, sge1_len, gd->gpa_mkey);
		hsg = 2;
	}

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		sg_i = hsg + i;

		frag = &skb_shinfo(skb)->frags[i];
		da = skb_frag_dma_map(dev, frag, 0, skb_frag_size(frag),
				      DMA_TO_DEVICE);
		if (dma_mapping_error(dev, da))
			goto frag_err;

		mana_add_sge(tp, ash, sg_i, da, skb_frag_size(frag),
			     gd->gpa_mkey);
	}

	return 0;

frag_err:
	for (i = sg_i - 1; i >= hsg; i--)
		dma_unmap_page(dev, ash->dma_handle[i], ash->size[i],
			       DMA_TO_DEVICE);

	for (i = hsg - 1; i >= 0; i--)
		dma_unmap_single(dev, ash->dma_handle[i], ash->size[i],
				 DMA_TO_DEVICE);

	return -ENOMEM;
}

/* Handle the case when GSO SKB linear length is too large.
 * MANA NIC requires GSO packets to put only the packet header to SGE0.
 * So, we need 2 SGEs for the skb linear part which contains more than the
 * header.
 * Return a positive value for the number of SGEs, or a negative value
 * for an error.
 */
static int mana_fix_skb_head(struct net_device *ndev, struct sk_buff *skb,
			     int gso_hs)
{
	int num_sge = 1 + skb_shinfo(skb)->nr_frags;
	int skb_hlen = skb_headlen(skb);

	if (gso_hs < skb_hlen) {
		num_sge++;
	} else if (gso_hs > skb_hlen) {
		if (net_ratelimit())
			netdev_err(ndev,
				   "TX nonlinear head: hs:%d, skb_hlen:%d\n",
				   gso_hs, skb_hlen);

		return -EINVAL;
	}

	return num_sge;
}

/* Get the GSO packet's header size */
static int mana_get_gso_hs(struct sk_buff *skb)
{
	int gso_hs;

	if (skb->encapsulation) {
		gso_hs = skb_inner_tcp_all_headers(skb);
	} else {
		if (skb_shinfo(skb)->gso_type & SKB_GSO_UDP_L4) {
			gso_hs = skb_transport_offset(skb) +
				 sizeof(struct udphdr);
		} else {
			gso_hs = skb_tcp_all_headers(skb);
		}
	}

	return gso_hs;
}

netdev_tx_t mana_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	enum mana_tx_pkt_format pkt_fmt = MANA_SHORT_PKT_FMT;
	struct mana_port_context *apc = netdev_priv(ndev);
	int gso_hs = 0; /* zero for non-GSO pkts */
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
	tx_stats = &txq->stats;

	pkg.tx_oob.s_oob.vcq_num = cq->gdma_id;
	pkg.tx_oob.s_oob.vsq_frame = txq->vsq_frame;

	if (txq->vp_offset > MANA_SHORT_VPORT_OFFSET_MAX) {
		pkg.tx_oob.l_oob.long_vp_offset = txq->vp_offset;
		pkt_fmt = MANA_LONG_PKT_FMT;
	} else {
		pkg.tx_oob.s_oob.short_vp_offset = txq->vp_offset;
	}

	if (skb_vlan_tag_present(skb)) {
		pkt_fmt = MANA_LONG_PKT_FMT;
		pkg.tx_oob.l_oob.inject_vlan_pri_tag = 1;
		pkg.tx_oob.l_oob.pcp = skb_vlan_tag_get_prio(skb);
		pkg.tx_oob.l_oob.dei = skb_vlan_tag_get_cfi(skb);
		pkg.tx_oob.l_oob.vlan_id = skb_vlan_tag_get_id(skb);
	}

	pkg.tx_oob.s_oob.pkt_fmt = pkt_fmt;

	if (pkt_fmt == MANA_SHORT_PKT_FMT) {
		pkg.wqe_req.inline_oob_size = sizeof(struct mana_tx_short_oob);
		u64_stats_update_begin(&tx_stats->syncp);
		tx_stats->short_pkt_fmt++;
		u64_stats_update_end(&tx_stats->syncp);
	} else {
		pkg.wqe_req.inline_oob_size = sizeof(struct mana_tx_oob);
		u64_stats_update_begin(&tx_stats->syncp);
		tx_stats->long_pkt_fmt++;
		u64_stats_update_end(&tx_stats->syncp);
	}

	pkg.wqe_req.inline_oob_data = &pkg.tx_oob;
	pkg.wqe_req.flags = 0;
	pkg.wqe_req.client_data_unit = 0;

	pkg.wqe_req.num_sge = 1 + skb_shinfo(skb)->nr_frags;

	if (skb->protocol == htons(ETH_P_IP))
		ipv4 = true;
	else if (skb->protocol == htons(ETH_P_IPV6))
		ipv6 = true;

	if (skb_is_gso(skb)) {
		int num_sge;

		gso_hs = mana_get_gso_hs(skb);

		num_sge = mana_fix_skb_head(ndev, skb, gso_hs);
		if (num_sge > 0)
			pkg.wqe_req.num_sge = num_sge;
		else
			goto tx_drop_count;

		u64_stats_update_begin(&tx_stats->syncp);
		if (skb->encapsulation) {
			tx_stats->tso_inner_packets++;
			tx_stats->tso_inner_bytes += skb->len - gso_hs;
		} else {
			tx_stats->tso_packets++;
			tx_stats->tso_bytes += skb->len - gso_hs;
		}
		u64_stats_update_end(&tx_stats->syncp);

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

		u64_stats_update_begin(&tx_stats->syncp);
		tx_stats->csum_partial++;
		u64_stats_update_end(&tx_stats->syncp);

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
				goto tx_drop_count;
		}
	}

	WARN_ON_ONCE(pkg.wqe_req.num_sge > MAX_TX_WQE_SGL_ENTRIES);

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

	if (mana_map_skb(skb, apc, &pkg, gso_hs)) {
		u64_stats_update_begin(&tx_stats->syncp);
		tx_stats->mana_map_err++;
		u64_stats_update_end(&tx_stats->syncp);
		goto free_sgl_ptr;
	}

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
			start = u64_stats_fetch_begin(&rx_stats->syncp);
			packets = rx_stats->packets;
			bytes = rx_stats->bytes;
		} while (u64_stats_fetch_retry(&rx_stats->syncp, start));

		st->rx_packets += packets;
		st->rx_bytes += bytes;
	}

	for (q = 0; q < num_queues; q++) {
		tx_stats = &apc->tx_qp[q].txq.stats;

		do {
			start = u64_stats_fetch_begin(&tx_stats->syncp);
			packets = tx_stats->packets;
			bytes = tx_stats->bytes;
		} while (u64_stats_fetch_retry(&tx_stats->syncp, start));

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

	txq = apc->indir_table[hash & (apc->indir_table_sz - 1)];

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

/* Release pre-allocated RX buffers */
void mana_pre_dealloc_rxbufs(struct mana_port_context *mpc)
{
	struct device *dev;
	int i;

	dev = mpc->ac->gdma_dev->gdma_context->dev;

	if (!mpc->rxbufs_pre)
		goto out1;

	if (!mpc->das_pre)
		goto out2;

	while (mpc->rxbpre_total) {
		i = --mpc->rxbpre_total;
		dma_unmap_single(dev, mpc->das_pre[i], mpc->rxbpre_datasize,
				 DMA_FROM_DEVICE);
		put_page(virt_to_head_page(mpc->rxbufs_pre[i]));
	}

	kfree(mpc->das_pre);
	mpc->das_pre = NULL;

out2:
	kfree(mpc->rxbufs_pre);
	mpc->rxbufs_pre = NULL;

out1:
	mpc->rxbpre_datasize = 0;
	mpc->rxbpre_alloc_size = 0;
	mpc->rxbpre_headroom = 0;
}

/* Get a buffer from the pre-allocated RX buffers */
static void *mana_get_rxbuf_pre(struct mana_rxq *rxq, dma_addr_t *da)
{
	struct net_device *ndev = rxq->ndev;
	struct mana_port_context *mpc;
	void *va;

	mpc = netdev_priv(ndev);

	if (!mpc->rxbufs_pre || !mpc->das_pre || !mpc->rxbpre_total) {
		netdev_err(ndev, "No RX pre-allocated bufs\n");
		return NULL;
	}

	/* Check sizes to catch unexpected coding error */
	if (mpc->rxbpre_datasize != rxq->datasize) {
		netdev_err(ndev, "rxbpre_datasize mismatch: %u: %u\n",
			   mpc->rxbpre_datasize, rxq->datasize);
		return NULL;
	}

	if (mpc->rxbpre_alloc_size != rxq->alloc_size) {
		netdev_err(ndev, "rxbpre_alloc_size mismatch: %u: %u\n",
			   mpc->rxbpre_alloc_size, rxq->alloc_size);
		return NULL;
	}

	if (mpc->rxbpre_headroom != rxq->headroom) {
		netdev_err(ndev, "rxbpre_headroom mismatch: %u: %u\n",
			   mpc->rxbpre_headroom, rxq->headroom);
		return NULL;
	}

	mpc->rxbpre_total--;

	*da = mpc->das_pre[mpc->rxbpre_total];
	va = mpc->rxbufs_pre[mpc->rxbpre_total];
	mpc->rxbufs_pre[mpc->rxbpre_total] = NULL;

	/* Deallocate the array after all buffers are gone */
	if (!mpc->rxbpre_total)
		mana_pre_dealloc_rxbufs(mpc);

	return va;
}

/* Get RX buffer's data size, alloc size, XDP headroom based on MTU */
static void mana_get_rxbuf_cfg(int mtu, u32 *datasize, u32 *alloc_size,
			       u32 *headroom)
{
	if (mtu > MANA_XDP_MTU_MAX)
		*headroom = 0; /* no support for XDP */
	else
		*headroom = XDP_PACKET_HEADROOM;

	*alloc_size = SKB_DATA_ALIGN(mtu + MANA_RXBUF_PAD + *headroom);

	/* Using page pool in this case, so alloc_size is PAGE_SIZE */
	if (*alloc_size < PAGE_SIZE)
		*alloc_size = PAGE_SIZE;

	*datasize = mtu + ETH_HLEN;
}

int mana_pre_alloc_rxbufs(struct mana_port_context *mpc, int new_mtu, int num_queues)
{
	struct device *dev;
	struct page *page;
	dma_addr_t da;
	int num_rxb;
	void *va;
	int i;

	mana_get_rxbuf_cfg(new_mtu, &mpc->rxbpre_datasize,
			   &mpc->rxbpre_alloc_size, &mpc->rxbpre_headroom);

	dev = mpc->ac->gdma_dev->gdma_context->dev;

	num_rxb = num_queues * mpc->rx_queue_size;

	WARN(mpc->rxbufs_pre, "mana rxbufs_pre exists\n");
	mpc->rxbufs_pre = kmalloc_array(num_rxb, sizeof(void *), GFP_KERNEL);
	if (!mpc->rxbufs_pre)
		goto error;

	mpc->das_pre = kmalloc_array(num_rxb, sizeof(dma_addr_t), GFP_KERNEL);
	if (!mpc->das_pre)
		goto error;

	mpc->rxbpre_total = 0;

	for (i = 0; i < num_rxb; i++) {
		if (mpc->rxbpre_alloc_size > PAGE_SIZE) {
			va = netdev_alloc_frag(mpc->rxbpre_alloc_size);
			if (!va)
				goto error;

			page = virt_to_head_page(va);
			/* Check if the frag falls back to single page */
			if (compound_order(page) <
			    get_order(mpc->rxbpre_alloc_size)) {
				put_page(page);
				goto error;
			}
		} else {
			page = dev_alloc_page();
			if (!page)
				goto error;

			va = page_to_virt(page);
		}

		da = dma_map_single(dev, va + mpc->rxbpre_headroom,
				    mpc->rxbpre_datasize, DMA_FROM_DEVICE);
		if (dma_mapping_error(dev, da)) {
			put_page(virt_to_head_page(va));
			goto error;
		}

		mpc->rxbufs_pre[i] = va;
		mpc->das_pre[i] = da;
		mpc->rxbpre_total = i + 1;
	}

	return 0;

error:
	mana_pre_dealloc_rxbufs(mpc);
	return -ENOMEM;
}

static int mana_change_mtu(struct net_device *ndev, int new_mtu)
{
	struct mana_port_context *mpc = netdev_priv(ndev);
	unsigned int old_mtu = ndev->mtu;
	int err;

	/* Pre-allocate buffers to prevent failure in mana_attach later */
	err = mana_pre_alloc_rxbufs(mpc, new_mtu, mpc->num_queues);
	if (err) {
		netdev_err(ndev, "Insufficient memory for new MTU\n");
		return err;
	}

	err = mana_detach(ndev, false);
	if (err) {
		netdev_err(ndev, "mana_detach failed: %d\n", err);
		goto out;
	}

	WRITE_ONCE(ndev->mtu, new_mtu);

	err = mana_attach(ndev);
	if (err) {
		netdev_err(ndev, "mana_attach failed: %d\n", err);
		WRITE_ONCE(ndev->mtu, old_mtu);
	}

out:
	mana_pre_dealloc_rxbufs(mpc);
	return err;
}

static const struct net_device_ops mana_devops = {
	.ndo_open		= mana_open,
	.ndo_stop		= mana_close,
	.ndo_select_queue	= mana_select_queue,
	.ndo_start_xmit		= mana_start_xmit,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_get_stats64	= mana_get_stats64,
	.ndo_bpf		= mana_bpf,
	.ndo_xdp_xmit		= mana_xdp_xmit,
	.ndo_change_mtu		= mana_change_mtu,
};

static void mana_cleanup_port_context(struct mana_port_context *apc)
{
	kfree(apc->rxqs);
	apc->rxqs = NULL;
}

static void mana_cleanup_indir_table(struct mana_port_context *apc)
{
	apc->indir_table_sz = 0;
	kfree(apc->indir_table);
	kfree(apc->rxobj_table);
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

static int mana_pf_register_hw_vport(struct mana_port_context *apc)
{
	struct mana_register_hw_vport_resp resp = {};
	struct mana_register_hw_vport_req req = {};
	int err;

	mana_gd_init_req_hdr(&req.hdr, MANA_REGISTER_HW_PORT,
			     sizeof(req), sizeof(resp));
	req.attached_gfid = 1;
	req.is_pf_default_vport = 1;
	req.allow_all_ether_types = 1;

	err = mana_send_request(apc->ac, &req, sizeof(req), &resp,
				sizeof(resp));
	if (err) {
		netdev_err(apc->ndev, "Failed to register hw vPort: %d\n", err);
		return err;
	}

	err = mana_verify_resp_hdr(&resp.hdr, MANA_REGISTER_HW_PORT,
				   sizeof(resp));
	if (err || resp.hdr.status) {
		netdev_err(apc->ndev, "Failed to register hw vPort: %d, 0x%x\n",
			   err, resp.hdr.status);
		return err ? err : -EPROTO;
	}

	apc->port_handle = resp.hw_vport_handle;
	return 0;
}

static void mana_pf_deregister_hw_vport(struct mana_port_context *apc)
{
	struct mana_deregister_hw_vport_resp resp = {};
	struct mana_deregister_hw_vport_req req = {};
	int err;

	mana_gd_init_req_hdr(&req.hdr, MANA_DEREGISTER_HW_PORT,
			     sizeof(req), sizeof(resp));
	req.hw_vport_handle = apc->port_handle;

	err = mana_send_request(apc->ac, &req, sizeof(req), &resp,
				sizeof(resp));
	if (err) {
		netdev_err(apc->ndev, "Failed to unregister hw vPort: %d\n",
			   err);
		return;
	}

	err = mana_verify_resp_hdr(&resp.hdr, MANA_DEREGISTER_HW_PORT,
				   sizeof(resp));
	if (err || resp.hdr.status)
		netdev_err(apc->ndev,
			   "Failed to deregister hw vPort: %d, 0x%x\n",
			   err, resp.hdr.status);
}

static int mana_pf_register_filter(struct mana_port_context *apc)
{
	struct mana_register_filter_resp resp = {};
	struct mana_register_filter_req req = {};
	int err;

	mana_gd_init_req_hdr(&req.hdr, MANA_REGISTER_FILTER,
			     sizeof(req), sizeof(resp));
	req.vport = apc->port_handle;
	memcpy(req.mac_addr, apc->mac_addr, ETH_ALEN);

	err = mana_send_request(apc->ac, &req, sizeof(req), &resp,
				sizeof(resp));
	if (err) {
		netdev_err(apc->ndev, "Failed to register filter: %d\n", err);
		return err;
	}

	err = mana_verify_resp_hdr(&resp.hdr, MANA_REGISTER_FILTER,
				   sizeof(resp));
	if (err || resp.hdr.status) {
		netdev_err(apc->ndev, "Failed to register filter: %d, 0x%x\n",
			   err, resp.hdr.status);
		return err ? err : -EPROTO;
	}

	apc->pf_filter_handle = resp.filter_handle;
	return 0;
}

static void mana_pf_deregister_filter(struct mana_port_context *apc)
{
	struct mana_deregister_filter_resp resp = {};
	struct mana_deregister_filter_req req = {};
	int err;

	mana_gd_init_req_hdr(&req.hdr, MANA_DEREGISTER_FILTER,
			     sizeof(req), sizeof(resp));
	req.filter_handle = apc->pf_filter_handle;

	err = mana_send_request(apc->ac, &req, sizeof(req), &resp,
				sizeof(resp));
	if (err) {
		netdev_err(apc->ndev, "Failed to unregister filter: %d\n",
			   err);
		return;
	}

	err = mana_verify_resp_hdr(&resp.hdr, MANA_DEREGISTER_FILTER,
				   sizeof(resp));
	if (err || resp.hdr.status)
		netdev_err(apc->ndev,
			   "Failed to deregister filter: %d, 0x%x\n",
			   err, resp.hdr.status);
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

	req.hdr.resp.msg_version = GDMA_MESSAGE_V2;

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

	if (resp.hdr.response.msg_version == GDMA_MESSAGE_V2)
		gc->adapter_mtu = resp.adapter_mtu;
	else
		gc->adapter_mtu = ETH_FRAME_LEN;

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
	if (resp.num_indirection_ent > 0 &&
	    resp.num_indirection_ent <= MANA_INDIRECT_TABLE_MAX_SIZE &&
	    is_power_of_2(resp.num_indirection_ent)) {
		*num_indir_entry = resp.num_indirection_ent;
	} else {
		netdev_warn(apc->ndev,
			    "Setting indirection table size to default %d for vPort %d\n",
			    MANA_INDIRECT_TABLE_DEF_SIZE, apc->port_idx);
		*num_indir_entry = MANA_INDIRECT_TABLE_DEF_SIZE;
	}

	apc->port_handle = resp.vport;
	ether_addr_copy(apc->mac_addr, resp.mac_addr);

	return 0;
}

void mana_uncfg_vport(struct mana_port_context *apc)
{
	mutex_lock(&apc->vport_mutex);
	apc->vport_use_count--;
	WARN_ON(apc->vport_use_count < 0);
	mutex_unlock(&apc->vport_mutex);
}
EXPORT_SYMBOL_NS(mana_uncfg_vport, NET_MANA);

int mana_cfg_vport(struct mana_port_context *apc, u32 protection_dom_id,
		   u32 doorbell_pg_id)
{
	struct mana_config_vport_resp resp = {};
	struct mana_config_vport_req req = {};
	int err;

	/* This function is used to program the Ethernet port in the hardware
	 * table. It can be called from the Ethernet driver or the RDMA driver.
	 *
	 * For Ethernet usage, the hardware supports only one active user on a
	 * physical port. The driver checks on the port usage before programming
	 * the hardware when creating the RAW QP (RDMA driver) or exposing the
	 * device to kernel NET layer (Ethernet driver).
	 *
	 * Because the RDMA driver doesn't know in advance which QP type the
	 * user will create, it exposes the device with all its ports. The user
	 * may not be able to create RAW QP on a port if this port is already
	 * in used by the Ethernet driver from the kernel.
	 *
	 * This physical port limitation only applies to the RAW QP. For RC QP,
	 * the hardware doesn't have this limitation. The user can create RC
	 * QPs on a physical port up to the hardware limits independent of the
	 * Ethernet usage on the same port.
	 */
	mutex_lock(&apc->vport_mutex);
	if (apc->vport_use_count > 0) {
		mutex_unlock(&apc->vport_mutex);
		return -EBUSY;
	}
	apc->vport_use_count++;
	mutex_unlock(&apc->vport_mutex);

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

	netdev_info(apc->ndev, "Configured vPort %llu PD %u DB %u\n",
		    apc->port_handle, protection_dom_id, doorbell_pg_id);
out:
	if (err)
		mana_uncfg_vport(apc);

	return err;
}
EXPORT_SYMBOL_NS(mana_cfg_vport, NET_MANA);

static int mana_cfg_vport_steering(struct mana_port_context *apc,
				   enum TRI_STATE rx,
				   bool update_default_rxobj, bool update_key,
				   bool update_tab)
{
	struct mana_cfg_rx_steer_req_v2 *req;
	struct mana_cfg_rx_steer_resp resp = {};
	struct net_device *ndev = apc->ndev;
	u32 req_buf_size;
	int err;

	req_buf_size = struct_size(req, indir_tab, apc->indir_table_sz);
	req = kzalloc(req_buf_size, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	mana_gd_init_req_hdr(&req->hdr, MANA_CONFIG_VPORT_RX, req_buf_size,
			     sizeof(resp));

	req->hdr.req.msg_version = GDMA_MESSAGE_V2;

	req->vport = apc->port_handle;
	req->num_indir_entries = apc->indir_table_sz;
	req->indir_tab_offset = offsetof(struct mana_cfg_rx_steer_req_v2,
					 indir_tab);
	req->rx_enable = rx;
	req->rss_enable = apc->rss_state;
	req->update_default_rxobj = update_default_rxobj;
	req->update_hashkey = update_key;
	req->update_indir_tab = update_tab;
	req->default_rxobj = apc->default_rxobj;
	req->cqe_coalescing_enable = 0;

	if (update_key)
		memcpy(&req->hashkey, apc->hashkey, MANA_HASH_KEY_SIZE);

	if (update_tab)
		memcpy(req->indir_tab, apc->rxobj_table,
		       flex_array_size(req, indir_tab, req->num_indir_entries));

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

	netdev_info(ndev, "Configured steering vPort %llu entries %u\n",
		    apc->port_handle, apc->indir_table_sz);
out:
	kfree(req);
	return err;
}

int mana_create_wq_obj(struct mana_port_context *apc,
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
EXPORT_SYMBOL_NS(mana_create_wq_obj, NET_MANA);

void mana_destroy_wq_obj(struct mana_port_context *apc, u32 wq_type,
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
EXPORT_SYMBOL_NS(mana_destroy_wq_obj, NET_MANA);

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
		spec.eq.msix_index = (i + 1) % gc->num_msix_usable;
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
	int hsg, i;

	/* Number of SGEs of linear part */
	hsg = (skb_is_gso(skb) && skb_headlen(skb) > ash->size[0]) ? 2 : 1;

	for (i = 0; i < hsg; i++)
		dma_unmap_single(dev, ash->dma_handle[i], ash->size[i],
				 DMA_TO_DEVICE);

	for (i = hsg; i < skb_shinfo(skb)->nr_frags + hsg; i++)
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
			if (net_ratelimit())
				netdev_err(ndev, "TX: CQE error %d\n",
					   cqe_oob->cqe_hdr.cqe_type);

			apc->eth_stats.tx_cqe_err++;
			break;

		default:
			/* If the CQE type is unknown, log an error,
			 * and still free the SKB, update tail, etc.
			 */
			if (net_ratelimit())
				netdev_err(ndev, "TX: unknown CQE type %d\n",
					   cqe_oob->cqe_hdr.cqe_type);

			apc->eth_stats.tx_cqe_unknown_type++;
			break;
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

	err = mana_gd_post_work_request(rxq->gdma_rq, &recv_buf_oob->wqe_req,
					&recv_buf_oob->wqe_inf);
	if (WARN_ON_ONCE(err))
		return;

	WARN_ON_ONCE(recv_buf_oob->wqe_inf.wqe_size_in_bu != 1);
}

static struct sk_buff *mana_build_skb(struct mana_rxq *rxq, void *buf_va,
				      uint pkt_len, struct xdp_buff *xdp)
{
	struct sk_buff *skb = napi_build_skb(buf_va, rxq->alloc_size);

	if (!skb)
		return NULL;

	if (xdp->data_hard_start) {
		skb_reserve(skb, xdp->data - xdp->data_hard_start);
		skb_put(skb, xdp->data_end - xdp->data);
		return skb;
	}

	skb_reserve(skb, rxq->headroom);
	skb_put(skb, pkt_len);

	return skb;
}

static void mana_rx_skb(void *buf_va, bool from_pool,
			struct mana_rxcomp_oob *cqe, struct mana_rxq *rxq)
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

	if (act == XDP_REDIRECT && !rxq->xdp_rc)
		return;

	if (act != XDP_PASS && act != XDP_TX)
		goto drop_xdp;

	skb = mana_build_skb(rxq, buf_va, pkt_len, &xdp);

	if (!skb)
		goto drop;

	if (from_pool)
		skb_mark_for_recycle(skb);

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

	if (cqe->rx_vlantag_present) {
		u16 vlan_tci = cqe->rx_vlan_id;

		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vlan_tci);
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
	if (from_pool) {
		page_pool_recycle_direct(rxq->page_pool,
					 virt_to_head_page(buf_va));
	} else {
		WARN_ON_ONCE(rxq->xdp_save_va);
		/* Save for reuse */
		rxq->xdp_save_va = buf_va;
	}

	++ndev->stats.rx_dropped;

	return;
}

static void *mana_get_rxfrag(struct mana_rxq *rxq, struct device *dev,
			     dma_addr_t *da, bool *from_pool, bool is_napi)
{
	struct page *page;
	void *va;

	*from_pool = false;

	/* Reuse XDP dropped page if available */
	if (rxq->xdp_save_va) {
		va = rxq->xdp_save_va;
		rxq->xdp_save_va = NULL;
	} else if (rxq->alloc_size > PAGE_SIZE) {
		if (is_napi)
			va = napi_alloc_frag(rxq->alloc_size);
		else
			va = netdev_alloc_frag(rxq->alloc_size);

		if (!va)
			return NULL;

		page = virt_to_head_page(va);
		/* Check if the frag falls back to single page */
		if (compound_order(page) < get_order(rxq->alloc_size)) {
			put_page(page);
			return NULL;
		}
	} else {
		page = page_pool_dev_alloc_pages(rxq->page_pool);
		if (!page)
			return NULL;

		*from_pool = true;
		va = page_to_virt(page);
	}

	*da = dma_map_single(dev, va + rxq->headroom, rxq->datasize,
			     DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, *da)) {
		if (*from_pool)
			page_pool_put_full_page(rxq->page_pool, page, false);
		else
			put_page(virt_to_head_page(va));

		return NULL;
	}

	return va;
}

/* Allocate frag for rx buffer, and save the old buf */
static void mana_refill_rx_oob(struct device *dev, struct mana_rxq *rxq,
			       struct mana_recv_buf_oob *rxoob, void **old_buf,
			       bool *old_fp)
{
	bool from_pool;
	dma_addr_t da;
	void *va;

	va = mana_get_rxfrag(rxq, dev, &da, &from_pool, true);
	if (!va)
		return;

	dma_unmap_single(dev, rxoob->sgl[0].address, rxq->datasize,
			 DMA_FROM_DEVICE);
	*old_buf = rxoob->buf_va;
	*old_fp = rxoob->from_pool;

	rxoob->buf_va = va;
	rxoob->sgl[0].address = da;
	rxoob->from_pool = from_pool;
}

static void mana_process_rx_cqe(struct mana_rxq *rxq, struct mana_cq *cq,
				struct gdma_comp *cqe)
{
	struct mana_rxcomp_oob *oob = (struct mana_rxcomp_oob *)cqe->cqe_data;
	struct gdma_context *gc = rxq->gdma_rq->gdma_dev->gdma_context;
	struct net_device *ndev = rxq->ndev;
	struct mana_recv_buf_oob *rxbuf_oob;
	struct mana_port_context *apc;
	struct device *dev = gc->dev;
	void *old_buf = NULL;
	u32 curr, pktlen;
	bool old_fp;

	apc = netdev_priv(ndev);

	switch (oob->cqe_hdr.cqe_type) {
	case CQE_RX_OKAY:
		break;

	case CQE_RX_TRUNCATED:
		++ndev->stats.rx_dropped;
		rxbuf_oob = &rxq->rx_oobs[rxq->buf_index];
		netdev_warn_once(ndev, "Dropped a truncated packet\n");
		goto drop;

	case CQE_RX_COALESCED_4:
		netdev_err(ndev, "RX coalescing is unsupported\n");
		apc->eth_stats.rx_coalesced_err++;
		return;

	case CQE_RX_OBJECT_FENCE:
		complete(&rxq->fence_event);
		return;

	default:
		netdev_err(ndev, "Unknown RX CQE type = %d\n",
			   oob->cqe_hdr.cqe_type);
		apc->eth_stats.rx_cqe_unknown_type++;
		return;
	}

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

	mana_refill_rx_oob(dev, rxq, rxbuf_oob, &old_buf, &old_fp);

	/* Unsuccessful refill will have old_buf == NULL.
	 * In this case, mana_rx_skb() will drop the packet.
	 */
	mana_rx_skb(old_buf, old_fp, oob, rxq);

drop:
	mana_move_wq_tail(rxq->gdma_rq, rxbuf_oob->wqe_inf.wqe_size_in_bu);

	mana_post_pkt_rxq(rxq);
}

static void mana_poll_rx_cq(struct mana_cq *cq)
{
	struct gdma_comp *comp = cq->gdma_comp_buf;
	struct mana_rxq *rxq = cq->rxq;
	int comp_read, i;

	comp_read = mana_gd_poll_cq(cq->gdma_cq, comp, CQE_POLLING_BUFFER);
	WARN_ON_ONCE(comp_read > CQE_POLLING_BUFFER);

	rxq->xdp_flush = false;

	for (i = 0; i < comp_read; i++) {
		if (WARN_ON_ONCE(comp[i].is_sq))
			return;

		/* verify recv cqe references the right rxq */
		if (WARN_ON_ONCE(comp[i].wq_num != cq->rxq->gdma_id))
			return;

		mana_process_rx_cqe(rxq, cq, &comp[i]);
	}

	if (comp_read > 0) {
		struct gdma_context *gc = rxq->gdma_rq->gdma_dev->gdma_context;

		mana_gd_wq_ring_doorbell(gc, rxq->gdma_rq);
	}

	if (rxq->xdp_flush)
		xdp_do_flush();
}

static int mana_cq_handler(void *context, struct gdma_queue *gdma_queue)
{
	struct mana_cq *cq = context;
	int w;

	WARN_ON_ONCE(cq->gdma_cq != gdma_queue);

	if (cq->type == MANA_CQ_TYPE_RX)
		mana_poll_rx_cq(cq);
	else
		mana_poll_tx_cq(cq);

	w = cq->work_done;
	cq->work_done_since_doorbell += w;

	if (w < cq->budget) {
		mana_gd_ring_cq(gdma_queue, SET_ARM_BIT);
		cq->work_done_since_doorbell = 0;
		napi_complete_done(&cq->napi, w);
	} else if (cq->work_done_since_doorbell >
		   cq->gdma_cq->queue_size / COMP_ENTRY_SIZE * 4) {
		/* MANA hardware requires at least one doorbell ring every 8
		 * wraparounds of CQ even if there is no need to arm the CQ.
		 * This driver rings the doorbell as soon as we have exceeded
		 * 4 wraparounds.
		 */
		mana_gd_ring_cq(gdma_queue, 0);
		cq->work_done_since_doorbell = 0;
	}

	return w;
}

static int mana_poll(struct napi_struct *napi, int budget)
{
	struct mana_cq *cq = container_of(napi, struct mana_cq, napi);
	int w;

	cq->work_done = 0;
	cq->budget = budget;

	w = mana_cq_handler(cq, cq->gdma_cq);

	return min(w, budget);
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
		if (apc->tx_qp[i].txq.napi_initialized) {
			napi_synchronize(napi);
			napi_disable(napi);
			netif_napi_del(napi);
			apc->tx_qp[i].txq.napi_initialized = false;
		}
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
	 *  apc->tx_queue_size represents the maximum number of WQEs
	 *  the SQ can store. This value is then used to size other queues
	 *  to prevent overflow.
	 *  Also note that the txq_size is always going to be MANA_PAGE_ALIGNED,
	 *  as min val of apc->tx_queue_size is 128 and that would make
	 *  txq_size 128*32 = 4096 and the other higher values of apc->tx_queue_size
	 *  are always power of two
	 */
	txq_size = apc->tx_queue_size * 32;

	cq_size = apc->tx_queue_size * COMP_ENTRY_SIZE;

	gc = gd->gdma_context;

	for (i = 0; i < apc->num_queues; i++) {
		apc->tx_qp[i].tx_object = INVALID_MANA_HANDLE;

		/* Create SQ */
		txq = &apc->tx_qp[i].txq;

		u64_stats_init(&txq->stats.syncp);
		txq->ndev = net;
		txq->net_txq = netdev_get_tx_queue(net, i);
		txq->vp_offset = apc->tx_vp_offset;
		txq->napi_initialized = false;
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

		wq_spec.gdma_region = txq->gdma_sq->mem_info.dma_region_handle;
		wq_spec.queue_size = txq->gdma_sq->queue_size;

		cq_spec.gdma_region = cq->gdma_cq->mem_info.dma_region_handle;
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

		txq->gdma_sq->mem_info.dma_region_handle =
			GDMA_INVALID_DMA_REGION;
		cq->gdma_cq->mem_info.dma_region_handle =
			GDMA_INVALID_DMA_REGION;

		txq->gdma_txq_id = txq->gdma_sq->id;

		cq->gdma_id = cq->gdma_cq->id;

		if (WARN_ON(cq->gdma_id >= gc->max_num_cqs)) {
			err = -EINVAL;
			goto out;
		}

		gc->cq_table[cq->gdma_id] = cq->gdma_cq;

		netif_napi_add_tx(net, &cq->napi, mana_poll);
		napi_enable(&cq->napi);
		txq->napi_initialized = true;

		mana_gd_ring_cq(cq->gdma_cq, SET_ARM_BIT);
	}

	return 0;
out:
	mana_destroy_txq(apc);
	return err;
}

static void mana_destroy_rxq(struct mana_port_context *apc,
			     struct mana_rxq *rxq, bool napi_initialized)

{
	struct gdma_context *gc = apc->ac->gdma_dev->gdma_context;
	struct mana_recv_buf_oob *rx_oob;
	struct device *dev = gc->dev;
	struct napi_struct *napi;
	struct page *page;
	int i;

	if (!rxq)
		return;

	napi = &rxq->rx_cq.napi;

	if (napi_initialized) {
		napi_synchronize(napi);

		napi_disable(napi);

		netif_napi_del(napi);
	}
	xdp_rxq_info_unreg(&rxq->xdp_rxq);

	mana_destroy_wq_obj(apc, GDMA_RQ, rxq->rxobj);

	mana_deinit_cq(apc, &rxq->rx_cq);

	if (rxq->xdp_save_va)
		put_page(virt_to_head_page(rxq->xdp_save_va));

	for (i = 0; i < rxq->num_rx_buf; i++) {
		rx_oob = &rxq->rx_oobs[i];

		if (!rx_oob->buf_va)
			continue;

		dma_unmap_single(dev, rx_oob->sgl[0].address,
				 rx_oob->sgl[0].size, DMA_FROM_DEVICE);

		page = virt_to_head_page(rx_oob->buf_va);

		if (rx_oob->from_pool)
			page_pool_put_full_page(rxq->page_pool, page, false);
		else
			put_page(page);

		rx_oob->buf_va = NULL;
	}

	page_pool_destroy(rxq->page_pool);

	if (rxq->gdma_rq)
		mana_gd_destroy_queue(gc, rxq->gdma_rq);

	kfree(rxq);
}

static int mana_fill_rx_oob(struct mana_recv_buf_oob *rx_oob, u32 mem_key,
			    struct mana_rxq *rxq, struct device *dev)
{
	struct mana_port_context *mpc = netdev_priv(rxq->ndev);
	bool from_pool = false;
	dma_addr_t da;
	void *va;

	if (mpc->rxbufs_pre)
		va = mana_get_rxbuf_pre(rxq, &da);
	else
		va = mana_get_rxfrag(rxq, dev, &da, &from_pool, false);

	if (!va)
		return -ENOMEM;

	rx_oob->buf_va = va;
	rx_oob->from_pool = from_pool;

	rx_oob->sgl[0].address = da;
	rx_oob->sgl[0].size = rxq->datasize;
	rx_oob->sgl[0].mem_key = mem_key;

	return 0;
}

#define MANA_WQE_HEADER_SIZE 16
#define MANA_WQE_SGE_SIZE 16

static int mana_alloc_rx_wqe(struct mana_port_context *apc,
			     struct mana_rxq *rxq, u32 *rxq_size, u32 *cq_size)
{
	struct gdma_context *gc = apc->ac->gdma_dev->gdma_context;
	struct mana_recv_buf_oob *rx_oob;
	struct device *dev = gc->dev;
	u32 buf_idx;
	int ret;

	WARN_ON(rxq->datasize == 0);

	*rxq_size = 0;
	*cq_size = 0;

	for (buf_idx = 0; buf_idx < rxq->num_rx_buf; buf_idx++) {
		rx_oob = &rxq->rx_oobs[buf_idx];
		memset(rx_oob, 0, sizeof(*rx_oob));

		rx_oob->num_sge = 1;

		ret = mana_fill_rx_oob(rx_oob, apc->ac->gdma_dev->gpa_mkey, rxq,
				       dev);
		if (ret)
			return ret;

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

static int mana_create_page_pool(struct mana_rxq *rxq, struct gdma_context *gc)
{
	struct mana_port_context *mpc = netdev_priv(rxq->ndev);
	struct page_pool_params pprm = {};
	int ret;

	pprm.pool_size = mpc->rx_queue_size;
	pprm.nid = gc->numa_node;
	pprm.napi = &rxq->rx_cq.napi;
	pprm.netdev = rxq->ndev;

	rxq->page_pool = page_pool_create(&pprm);

	if (IS_ERR(rxq->page_pool)) {
		ret = PTR_ERR(rxq->page_pool);
		rxq->page_pool = NULL;
		return ret;
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

	rxq = kzalloc(struct_size(rxq, rx_oobs, apc->rx_queue_size),
		      GFP_KERNEL);
	if (!rxq)
		return NULL;

	rxq->ndev = ndev;
	rxq->num_rx_buf = apc->rx_queue_size;
	rxq->rxq_idx = rxq_idx;
	rxq->rxobj = INVALID_MANA_HANDLE;

	mana_get_rxbuf_cfg(ndev->mtu, &rxq->datasize, &rxq->alloc_size,
			   &rxq->headroom);

	/* Create page pool for RX queue */
	err = mana_create_page_pool(rxq, gc);
	if (err) {
		netdev_err(ndev, "Create page pool err:%d\n", err);
		goto out;
	}

	err = mana_alloc_rx_wqe(apc, rxq, &rq_size, &cq_size);
	if (err)
		goto out;

	rq_size = MANA_PAGE_ALIGN(rq_size);
	cq_size = MANA_PAGE_ALIGN(cq_size);

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
	wq_spec.gdma_region = rxq->gdma_rq->mem_info.dma_region_handle;
	wq_spec.queue_size = rxq->gdma_rq->queue_size;

	cq_spec.gdma_region = cq->gdma_cq->mem_info.dma_region_handle;
	cq_spec.queue_size = cq->gdma_cq->queue_size;
	cq_spec.modr_ctx_id = 0;
	cq_spec.attached_eq = cq->gdma_cq->cq.parent->id;

	err = mana_create_wq_obj(apc, apc->port_handle, GDMA_RQ,
				 &wq_spec, &cq_spec, &rxq->rxobj);
	if (err)
		goto out;

	rxq->gdma_rq->id = wq_spec.queue_index;
	cq->gdma_cq->id = cq_spec.queue_index;

	rxq->gdma_rq->mem_info.dma_region_handle = GDMA_INVALID_DMA_REGION;
	cq->gdma_cq->mem_info.dma_region_handle = GDMA_INVALID_DMA_REGION;

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

	netif_napi_add_weight(ndev, &cq->napi, mana_poll, 1);

	WARN_ON(xdp_rxq_info_reg(&rxq->xdp_rxq, ndev, rxq_idx,
				 cq->napi.napi_id));
	WARN_ON(xdp_rxq_info_reg_mem_model(&rxq->xdp_rxq, MEM_TYPE_PAGE_POOL,
					   rxq->page_pool));

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
	struct gdma_dev *gd = apc->ac->gdma_dev;
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
	mana_uncfg_vport(apc);

	if (gd->gdma_context->is_pf)
		mana_pf_deregister_hw_vport(apc);
}

static int mana_create_vport(struct mana_port_context *apc,
			     struct net_device *net)
{
	struct gdma_dev *gd = apc->ac->gdma_dev;
	int err;

	apc->default_rxobj = INVALID_MANA_HANDLE;

	if (gd->gdma_context->is_pf) {
		err = mana_pf_register_hw_vport(apc);
		if (err)
			return err;
	}

	err = mana_cfg_vport(apc, gd->pdid, gd->doorbell);
	if (err)
		return err;

	return mana_create_txq(apc, net);
}

static int mana_rss_table_alloc(struct mana_port_context *apc)
{
	if (!apc->indir_table_sz) {
		netdev_err(apc->ndev,
			   "Indirection table size not set for vPort %d\n",
			   apc->port_idx);
		return -EINVAL;
	}

	apc->indir_table = kcalloc(apc->indir_table_sz, sizeof(u32), GFP_KERNEL);
	if (!apc->indir_table)
		return -ENOMEM;

	apc->rxobj_table = kcalloc(apc->indir_table_sz, sizeof(mana_handle_t), GFP_KERNEL);
	if (!apc->rxobj_table) {
		kfree(apc->indir_table);
		return -ENOMEM;
	}

	return 0;
}

static void mana_rss_table_init(struct mana_port_context *apc)
{
	int i;

	for (i = 0; i < apc->indir_table_sz; i++)
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
		for (i = 0; i < apc->indir_table_sz; i++) {
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

void mana_query_gf_stats(struct mana_port_context *apc)
{
	struct mana_query_gf_stat_resp resp = {};
	struct mana_query_gf_stat_req req = {};
	struct net_device *ndev = apc->ndev;
	int err;

	mana_gd_init_req_hdr(&req.hdr, MANA_QUERY_GF_STAT,
			     sizeof(req), sizeof(resp));
	req.req_stats = STATISTICS_FLAGS_RX_DISCARDS_NO_WQE |
			STATISTICS_FLAGS_RX_ERRORS_VPORT_DISABLED |
			STATISTICS_FLAGS_HC_RX_BYTES |
			STATISTICS_FLAGS_HC_RX_UCAST_PACKETS |
			STATISTICS_FLAGS_HC_RX_UCAST_BYTES |
			STATISTICS_FLAGS_HC_RX_MCAST_PACKETS |
			STATISTICS_FLAGS_HC_RX_MCAST_BYTES |
			STATISTICS_FLAGS_HC_RX_BCAST_PACKETS |
			STATISTICS_FLAGS_HC_RX_BCAST_BYTES |
			STATISTICS_FLAGS_TX_ERRORS_GF_DISABLED |
			STATISTICS_FLAGS_TX_ERRORS_VPORT_DISABLED |
			STATISTICS_FLAGS_TX_ERRORS_INVAL_VPORT_OFFSET_PACKETS |
			STATISTICS_FLAGS_TX_ERRORS_VLAN_ENFORCEMENT |
			STATISTICS_FLAGS_TX_ERRORS_ETH_TYPE_ENFORCEMENT |
			STATISTICS_FLAGS_TX_ERRORS_SA_ENFORCEMENT |
			STATISTICS_FLAGS_TX_ERRORS_SQPDID_ENFORCEMENT |
			STATISTICS_FLAGS_TX_ERRORS_CQPDID_ENFORCEMENT |
			STATISTICS_FLAGS_TX_ERRORS_MTU_VIOLATION |
			STATISTICS_FLAGS_TX_ERRORS_INVALID_OOB |
			STATISTICS_FLAGS_HC_TX_BYTES |
			STATISTICS_FLAGS_HC_TX_UCAST_PACKETS |
			STATISTICS_FLAGS_HC_TX_UCAST_BYTES |
			STATISTICS_FLAGS_HC_TX_MCAST_PACKETS |
			STATISTICS_FLAGS_HC_TX_MCAST_BYTES |
			STATISTICS_FLAGS_HC_TX_BCAST_PACKETS |
			STATISTICS_FLAGS_HC_TX_BCAST_BYTES |
			STATISTICS_FLAGS_TX_ERRORS_GDMA_ERROR;

	err = mana_send_request(apc->ac, &req, sizeof(req), &resp,
				sizeof(resp));
	if (err) {
		netdev_err(ndev, "Failed to query GF stats: %d\n", err);
		return;
	}
	err = mana_verify_resp_hdr(&resp.hdr, MANA_QUERY_GF_STAT,
				   sizeof(resp));
	if (err || resp.hdr.status) {
		netdev_err(ndev, "Failed to query GF stats: %d, 0x%x\n", err,
			   resp.hdr.status);
		return;
	}

	apc->eth_stats.hc_rx_discards_no_wqe = resp.rx_discards_nowqe;
	apc->eth_stats.hc_rx_err_vport_disabled = resp.rx_err_vport_disabled;
	apc->eth_stats.hc_rx_bytes = resp.hc_rx_bytes;
	apc->eth_stats.hc_rx_ucast_pkts = resp.hc_rx_ucast_pkts;
	apc->eth_stats.hc_rx_ucast_bytes = resp.hc_rx_ucast_bytes;
	apc->eth_stats.hc_rx_bcast_pkts = resp.hc_rx_bcast_pkts;
	apc->eth_stats.hc_rx_bcast_bytes = resp.hc_rx_bcast_bytes;
	apc->eth_stats.hc_rx_mcast_pkts = resp.hc_rx_mcast_pkts;
	apc->eth_stats.hc_rx_mcast_bytes = resp.hc_rx_mcast_bytes;
	apc->eth_stats.hc_tx_err_gf_disabled = resp.tx_err_gf_disabled;
	apc->eth_stats.hc_tx_err_vport_disabled = resp.tx_err_vport_disabled;
	apc->eth_stats.hc_tx_err_inval_vportoffset_pkt =
					     resp.tx_err_inval_vport_offset_pkt;
	apc->eth_stats.hc_tx_err_vlan_enforcement =
					     resp.tx_err_vlan_enforcement;
	apc->eth_stats.hc_tx_err_eth_type_enforcement =
					     resp.tx_err_ethtype_enforcement;
	apc->eth_stats.hc_tx_err_sa_enforcement = resp.tx_err_SA_enforcement;
	apc->eth_stats.hc_tx_err_sqpdid_enforcement =
					     resp.tx_err_SQPDID_enforcement;
	apc->eth_stats.hc_tx_err_cqpdid_enforcement =
					     resp.tx_err_CQPDID_enforcement;
	apc->eth_stats.hc_tx_err_mtu_violation = resp.tx_err_mtu_violation;
	apc->eth_stats.hc_tx_err_inval_oob = resp.tx_err_inval_oob;
	apc->eth_stats.hc_tx_bytes = resp.hc_tx_bytes;
	apc->eth_stats.hc_tx_ucast_pkts = resp.hc_tx_ucast_pkts;
	apc->eth_stats.hc_tx_ucast_bytes = resp.hc_tx_ucast_bytes;
	apc->eth_stats.hc_tx_bcast_pkts = resp.hc_tx_bcast_pkts;
	apc->eth_stats.hc_tx_bcast_bytes = resp.hc_tx_bcast_bytes;
	apc->eth_stats.hc_tx_mcast_pkts = resp.hc_tx_mcast_pkts;
	apc->eth_stats.hc_tx_mcast_bytes = resp.hc_tx_mcast_bytes;
	apc->eth_stats.hc_tx_err_gdma = resp.tx_err_gdma;
}

static int mana_init_port(struct net_device *ndev)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	u32 max_txq, max_rxq, max_queues;
	int port_idx = apc->port_idx;
	int err;

	err = mana_init_port_context(apc);
	if (err)
		return err;

	err = mana_query_vport_cfg(apc, port_idx, &max_txq, &max_rxq,
				   &apc->indir_table_sz);
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
	mana_cleanup_port_context(apc);
	return err;
}

int mana_alloc_queues(struct net_device *ndev)
{
	struct mana_port_context *apc = netdev_priv(ndev);
	struct gdma_dev *gd = apc->ac->gdma_dev;
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

	if (gd->gdma_context->is_pf) {
		err = mana_pf_register_filter(apc);
		if (err)
			goto destroy_vport;
	}

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
	unsigned long timeout = jiffies + 120 * HZ;
	struct gdma_dev *gd = apc->ac->gdma_dev;
	struct mana_txq *txq;
	struct sk_buff *skb;
	int i, err;
	u32 tsleep;

	if (apc->port_is_up)
		return -EINVAL;

	mana_chn_setxdp(apc, NULL);

	if (gd->gdma_context->is_pf)
		mana_pf_deregister_filter(apc);

	/* No packet can be transmitted now since apc->port_is_up is false.
	 * There is still a tiny chance that mana_poll_tx_cq() can re-enable
	 * a txq because it may not timely see apc->port_is_up being cleared
	 * to false, but it doesn't matter since mana_start_xmit() drops any
	 * new packets due to apc->port_is_up being false.
	 *
	 * Drain all the in-flight TX packets.
	 * A timeout of 120 seconds for all the queues is used.
	 * This will break the while loop when h/w is not responding.
	 * This value of 120 has been decided here considering max
	 * number of queues.
	 */

	for (i = 0; i < apc->num_queues; i++) {
		txq = &apc->tx_qp[i].txq;
		tsleep = 1000;
		while (atomic_read(&txq->pending_sends) > 0 &&
		       time_before(jiffies, timeout)) {
			usleep_range(tsleep, tsleep + 1000);
			tsleep <<= 1;
		}
		if (atomic_read(&txq->pending_sends)) {
			err = pcie_flr(to_pci_dev(gd->gdma_context->dev));
			if (err) {
				netdev_err(ndev, "flr failed %d with %d pkts pending in txq %u\n",
					   err, atomic_read(&txq->pending_sends),
					   txq->gdma_txq_id);
			}
			break;
		}
	}

	for (i = 0; i < apc->num_queues; i++) {
		txq = &apc->tx_qp[i].txq;
		while ((skb = skb_dequeue(&txq->pending_skbs))) {
			mana_unmap_skb(skb, apc);
			dev_kfree_skb_any(skb);
		}
		atomic_set(&txq->pending_sends, 0);
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
	apc->tx_queue_size = DEF_TX_BUFFERS_PER_QUEUE;
	apc->rx_queue_size = DEF_RX_BUFFERS_PER_QUEUE;
	apc->port_handle = INVALID_MANA_HANDLE;
	apc->pf_filter_handle = INVALID_MANA_HANDLE;
	apc->port_idx = port_idx;

	mutex_init(&apc->vport_mutex);
	apc->vport_use_count = 0;

	ndev->netdev_ops = &mana_devops;
	ndev->ethtool_ops = &mana_ethtool_ops;
	ndev->mtu = ETH_DATA_LEN;
	ndev->max_mtu = gc->adapter_mtu - ETH_HLEN;
	ndev->min_mtu = ETH_MIN_MTU;
	ndev->needed_headroom = MANA_HEADROOM;
	ndev->dev_port = port_idx;
	SET_NETDEV_DEV(ndev, gc->dev);

	netif_carrier_off(ndev);

	netdev_rss_key_fill(apc->hashkey, MANA_HASH_KEY_SIZE);

	err = mana_init_port(ndev);
	if (err)
		goto free_net;

	err = mana_rss_table_alloc(apc);
	if (err)
		goto reset_apc;

	netdev_lockdep_set_classes(ndev);

	ndev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;
	ndev->hw_features |= NETIF_F_RXCSUM;
	ndev->hw_features |= NETIF_F_TSO | NETIF_F_TSO6;
	ndev->hw_features |= NETIF_F_RXHASH;
	ndev->features = ndev->hw_features | NETIF_F_HW_VLAN_CTAG_TX |
			 NETIF_F_HW_VLAN_CTAG_RX;
	ndev->vlan_features = ndev->features;
	xdp_set_features_flag(ndev, NETDEV_XDP_ACT_BASIC |
			      NETDEV_XDP_ACT_REDIRECT |
			      NETDEV_XDP_ACT_NDO_XMIT);

	err = register_netdev(ndev);
	if (err) {
		netdev_err(ndev, "Unable to register netdev.\n");
		goto free_indir;
	}

	return 0;

free_indir:
	mana_cleanup_indir_table(apc);
reset_apc:
	mana_cleanup_port_context(apc);
free_net:
	*ndev_storage = NULL;
	netdev_err(ndev, "Failed to probe vPort %d: %d\n", port_idx, err);
	free_netdev(ndev);
	return err;
}

static void adev_release(struct device *dev)
{
	struct mana_adev *madev = container_of(dev, struct mana_adev, adev.dev);

	kfree(madev);
}

static void remove_adev(struct gdma_dev *gd)
{
	struct auxiliary_device *adev = gd->adev;
	int id = adev->id;

	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);

	mana_adev_idx_free(id);
	gd->adev = NULL;
}

static int add_adev(struct gdma_dev *gd)
{
	struct auxiliary_device *adev;
	struct mana_adev *madev;
	int ret;

	madev = kzalloc(sizeof(*madev), GFP_KERNEL);
	if (!madev)
		return -ENOMEM;

	adev = &madev->adev;
	ret = mana_adev_idx_alloc();
	if (ret < 0)
		goto idx_fail;
	adev->id = ret;

	adev->name = "rdma";
	adev->dev.parent = gd->gdma_context->dev;
	adev->dev.release = adev_release;
	madev->mdev = gd;

	ret = auxiliary_device_init(adev);
	if (ret)
		goto init_fail;

	/* madev is owned by the auxiliary device */
	madev = NULL;
	ret = auxiliary_device_add(adev);
	if (ret)
		goto add_fail;

	gd->adev = adev;
	return 0;

add_fail:
	auxiliary_device_uninit(adev);

init_fail:
	mana_adev_idx_free(adev->id);

idx_fail:
	kfree(madev);

	return ret;
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
			/* we log the port for which the probe failed and stop
			 * probes for subsequent ports.
			 * Note that we keep running ports, for which the probes
			 * were successful, unless add_adev fails too
			 */
			if (err) {
				dev_err(dev, "Probe Failed for port %d\n", i);
				break;
			}
		}
	} else {
		for (i = 0; i < ac->num_ports; i++) {
			rtnl_lock();
			err = mana_attach(ac->ports[i]);
			rtnl_unlock();
			/* we log the port for which the attach failed and stop
			 * attach for subsequent ports
			 * Note that we keep running ports, for which the attach
			 * were successful, unless add_adev fails too
			 */
			if (err) {
				dev_err(dev, "Attach Failed for port %d\n", i);
				break;
			}
		}
	}

	err = add_adev(gd);
out:
	if (err)
		mana_remove(gd, false);

	return err;
}

void mana_remove(struct gdma_dev *gd, bool suspending)
{
	struct gdma_context *gc = gd->gdma_context;
	struct mana_context *ac = gd->driver_data;
	struct mana_port_context *apc;
	struct device *dev = gc->dev;
	struct net_device *ndev;
	int err;
	int i;

	/* adev currently doesn't support suspending, always remove it */
	if (gd->adev)
		remove_adev(gd);

	for (i = 0; i < ac->num_ports; i++) {
		ndev = ac->ports[i];
		apc = netdev_priv(ndev);
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
		mana_cleanup_indir_table(apc);

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

struct net_device *mana_get_primary_netdev_rcu(struct mana_context *ac, u32 port_index)
{
	struct net_device *ndev;

	RCU_LOCKDEP_WARN(!rcu_read_lock_held(),
			 "Taking primary netdev without holding the RCU read lock");
	if (port_index >= ac->num_ports)
		return NULL;

	/* When mana is used in netvsc, the upper netdevice should be returned. */
	if (ac->ports[port_index]->flags & IFF_SLAVE)
		ndev = netdev_master_upper_dev_get_rcu(ac->ports[port_index]);
	else
		ndev = ac->ports[port_index];

	return ndev;
}
EXPORT_SYMBOL_NS(mana_get_primary_netdev_rcu, NET_MANA);
