// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.

#include <asm/byteorder.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/gro.h>
#include <linux/skbuff.h>
#include <net/page_pool/helpers.h>
#include <linux/if_vlan.h>
#include <net/udp_tunnel.h>
#include <net/dst_metadata.h>
#include <net/netdev_queues.h>

#include "bnge.h"
#include "bnge_hwrm.h"
#include "bnge_hwrm_lib.h"
#include "bnge_netdev.h"
#include "bnge_rmem.h"
#include "bnge_txrx.h"

irqreturn_t bnge_msix(int irq, void *dev_instance)
{
	struct bnge_napi *bnapi = dev_instance;
	struct bnge_nq_ring_info *nqr;
	struct bnge_net *bn;
	u32 cons;

	bn = bnapi->bn;
	nqr = &bnapi->nq_ring;
	cons = RING_CMP(bn, nqr->nq_raw_cons);

	prefetch(&nqr->desc_ring[CP_RING(cons)][CP_IDX(cons)]);
	napi_schedule(&bnapi->napi);
	return IRQ_HANDLED;
}

static struct rx_agg_cmp *bnge_get_tpa_agg(struct bnge_net *bn,
					   struct bnge_rx_ring_info *rxr,
					   u16 agg_id, u16 curr)
{
	struct bnge_tpa_info *tpa_info = &rxr->rx_tpa[agg_id];

	return &tpa_info->agg_arr[curr];
}

static struct rx_agg_cmp *bnge_get_agg(struct bnge_net *bn,
				       struct bnge_cp_ring_info *cpr,
				       u16 cp_cons, u16 curr)
{
	struct rx_agg_cmp *agg;

	cp_cons = RING_CMP(bn, ADV_RAW_CMP(cp_cons, curr));
	agg = (struct rx_agg_cmp *)
		&cpr->desc_ring[CP_RING(cp_cons)][CP_IDX(cp_cons)];
	return agg;
}

static void bnge_reuse_rx_agg_bufs(struct bnge_cp_ring_info *cpr, u16 idx,
				   u16 start, u32 agg_bufs, bool tpa)
{
	struct bnge_napi *bnapi = cpr->bnapi;
	struct bnge_net *bn = bnapi->bn;
	struct bnge_rx_ring_info *rxr;
	u16 prod, sw_prod;
	u32 i;

	rxr = bnapi->rx_ring;
	sw_prod = rxr->rx_sw_agg_prod;
	prod = rxr->rx_agg_prod;

	for (i = 0; i < agg_bufs; i++) {
		struct bnge_sw_rx_agg_bd *cons_rx_buf, *prod_rx_buf;
		struct rx_agg_cmp *agg;
		struct rx_bd *prod_bd;
		netmem_ref netmem;
		u16 cons;

		if (tpa)
			agg = bnge_get_tpa_agg(bn, rxr, idx, start + i);
		else
			agg = bnge_get_agg(bn, cpr, idx, start + i);
		cons = agg->rx_agg_cmp_opaque;
		__clear_bit(cons, rxr->rx_agg_bmap);

		if (unlikely(test_bit(sw_prod, rxr->rx_agg_bmap)))
			sw_prod = bnge_find_next_agg_idx(rxr, sw_prod);

		__set_bit(sw_prod, rxr->rx_agg_bmap);
		prod_rx_buf = &rxr->rx_agg_buf_ring[sw_prod];
		cons_rx_buf = &rxr->rx_agg_buf_ring[cons];

		/* It is possible for sw_prod to be equal to cons, so
		 * set cons_rx_buf->netmem to 0 first.
		 */
		netmem = cons_rx_buf->netmem;
		cons_rx_buf->netmem = 0;
		prod_rx_buf->netmem = netmem;
		prod_rx_buf->offset = cons_rx_buf->offset;

		prod_rx_buf->mapping = cons_rx_buf->mapping;

		prod_bd = &rxr->rx_agg_desc_ring[RX_AGG_RING(bn, prod)]
					[RX_IDX(prod)];

		prod_bd->rx_bd_haddr = cpu_to_le64(cons_rx_buf->mapping);
		prod_bd->rx_bd_opaque = sw_prod;

		prod = NEXT_RX_AGG(prod);
		sw_prod = RING_RX_AGG(bn, NEXT_RX_AGG(sw_prod));
	}
	rxr->rx_agg_prod = prod;
	rxr->rx_sw_agg_prod = sw_prod;
}

static int bnge_agg_bufs_valid(struct bnge_net *bn,
			       struct bnge_cp_ring_info *cpr,
			       u8 agg_bufs, u32 *raw_cons)
{
	struct rx_agg_cmp *agg;
	u16 last;

	*raw_cons = ADV_RAW_CMP(*raw_cons, agg_bufs);
	last = RING_CMP(bn, *raw_cons);
	agg = (struct rx_agg_cmp *)
		&cpr->desc_ring[CP_RING(last)][CP_IDX(last)];
	return RX_AGG_CMP_VALID(bn, agg, *raw_cons);
}

static int bnge_discard_rx(struct bnge_net *bn, struct bnge_cp_ring_info *cpr,
			   u32 *raw_cons, void *cmp)
{
	u32 tmp_raw_cons = *raw_cons;
	struct rx_cmp *rxcmp = cmp;
	u8 cmp_type, agg_bufs = 0;

	cmp_type = RX_CMP_TYPE(rxcmp);

	if (cmp_type == CMP_TYPE_RX_L2_CMP) {
		agg_bufs = (le32_to_cpu(rxcmp->rx_cmp_misc_v1) &
			    RX_CMP_AGG_BUFS) >>
			   RX_CMP_AGG_BUFS_SHIFT;
	} else if (cmp_type == CMP_TYPE_RX_L2_TPA_END_CMP) {
		return 0;
	}

	if (agg_bufs) {
		if (!bnge_agg_bufs_valid(bn, cpr, agg_bufs, &tmp_raw_cons))
			return -EBUSY;
	}
	*raw_cons = tmp_raw_cons;
	return 0;
}

static u32 __bnge_rx_agg_netmems(struct bnge_net *bn,
				 struct bnge_cp_ring_info *cpr,
				 u16 idx, u32 agg_bufs, bool tpa,
				 struct sk_buff *skb)
{
	struct bnge_napi *bnapi = cpr->bnapi;
	struct skb_shared_info *shinfo;
	struct bnge_rx_ring_info *rxr;
	u32 i, total_frag_len = 0;
	u16 prod;

	rxr = bnapi->rx_ring;
	prod = rxr->rx_agg_prod;
	shinfo = skb_shinfo(skb);

	for (i = 0; i < agg_bufs; i++) {
		struct bnge_sw_rx_agg_bd *cons_rx_buf;
		struct rx_agg_cmp *agg;
		u16 cons, frag_len;
		netmem_ref netmem;

		if (tpa)
			agg = bnge_get_tpa_agg(bn, rxr, idx, i);
		else
			agg = bnge_get_agg(bn, cpr, idx, i);
		cons = agg->rx_agg_cmp_opaque;
		frag_len = (le32_to_cpu(agg->rx_agg_cmp_len_flags_type) &
			    RX_AGG_CMP_LEN) >> RX_AGG_CMP_LEN_SHIFT;

		cons_rx_buf = &rxr->rx_agg_buf_ring[cons];
		skb_add_rx_frag_netmem(skb, i, cons_rx_buf->netmem,
				       cons_rx_buf->offset,
				       frag_len, BNGE_RX_PAGE_SIZE);
		__clear_bit(cons, rxr->rx_agg_bmap);

		/* It is possible for bnge_alloc_rx_netmem() to allocate
		 * a sw_prod index that equals the cons index, so we
		 * need to clear the cons entry now.
		 */
		netmem = cons_rx_buf->netmem;
		cons_rx_buf->netmem = 0;

		if (bnge_alloc_rx_netmem(bn, rxr, prod, GFP_ATOMIC) != 0) {
			skb->len -= frag_len;
			skb->data_len -= frag_len;
			skb->truesize -= BNGE_RX_PAGE_SIZE;

			--shinfo->nr_frags;
			cons_rx_buf->netmem = netmem;

			/* Update prod since possibly some netmems have been
			 * allocated already.
			 */
			rxr->rx_agg_prod = prod;
			bnge_reuse_rx_agg_bufs(cpr, idx, i, agg_bufs - i, tpa);
			return 0;
		}

		page_pool_dma_sync_netmem_for_cpu(rxr->page_pool, netmem, 0,
						  BNGE_RX_PAGE_SIZE);

		total_frag_len += frag_len;
		prod = NEXT_RX_AGG(prod);
	}
	rxr->rx_agg_prod = prod;
	return total_frag_len;
}

static struct sk_buff *bnge_rx_agg_netmems_skb(struct bnge_net *bn,
					       struct bnge_cp_ring_info *cpr,
					       struct sk_buff *skb, u16 idx,
					       u32 agg_bufs, bool tpa)
{
	u32 total_frag_len;

	total_frag_len = __bnge_rx_agg_netmems(bn, cpr, idx, agg_bufs,
					       tpa, skb);
	if (!total_frag_len) {
		skb_mark_for_recycle(skb);
		dev_kfree_skb(skb);
		return NULL;
	}

	return skb;
}

static void bnge_sched_reset_rxr(struct bnge_net *bn,
				 struct bnge_rx_ring_info *rxr)
{
	if (!rxr->bnapi->in_reset) {
		rxr->bnapi->in_reset = true;

		/* TODO: Initiate reset task */
	}
	rxr->rx_next_cons = 0xffff;
}

static void bnge_sched_reset_txr(struct bnge_net *bn,
				 struct bnge_tx_ring_info *txr,
				 u16 curr)
{
	struct bnge_napi *bnapi = txr->bnapi;

	if (bnapi->tx_fault)
		return;

	netdev_err(bn->netdev, "Invalid Tx completion (ring:%d tx_hw_cons:%u cons:%u prod:%u curr:%u)",
		   txr->txq_index, txr->tx_hw_cons,
		   txr->tx_cons, txr->tx_prod, curr);
	WARN_ON_ONCE(1);
	bnapi->tx_fault = 1;
	/* TODO: Initiate reset task */
}

static u16 bnge_tpa_alloc_agg_idx(struct bnge_rx_ring_info *rxr, u16 agg_id)
{
	struct bnge_tpa_idx_map *map = rxr->rx_tpa_idx_map;
	u16 idx = agg_id & MAX_TPA_MASK;

	if (test_bit(idx, map->agg_idx_bmap)) {
		idx = find_first_zero_bit(map->agg_idx_bmap, MAX_TPA);
		if (idx >= MAX_TPA)
			return INVALID_HW_RING_ID;
	}
	__set_bit(idx, map->agg_idx_bmap);
	map->agg_id_tbl[agg_id] = idx;
	return idx;
}

static void bnge_free_agg_idx(struct bnge_rx_ring_info *rxr, u16 idx)
{
	struct bnge_tpa_idx_map *map = rxr->rx_tpa_idx_map;

	__clear_bit(idx, map->agg_idx_bmap);
}

static u16 bnge_lookup_agg_idx(struct bnge_rx_ring_info *rxr, u16 agg_id)
{
	struct bnge_tpa_idx_map *map = rxr->rx_tpa_idx_map;

	return map->agg_id_tbl[agg_id];
}

static void bnge_tpa_metadata(struct bnge_tpa_info *tpa_info,
			      struct rx_tpa_start_cmp *tpa_start,
			      struct rx_tpa_start_cmp_ext *tpa_start1)
{
	tpa_info->cfa_code_valid = 1;
	tpa_info->cfa_code = TPA_START_CFA_CODE(tpa_start1);
	tpa_info->vlan_valid = 0;
	if (tpa_info->flags2 & RX_CMP_FLAGS2_META_FORMAT_VLAN) {
		tpa_info->vlan_valid = 1;
		tpa_info->metadata =
			le32_to_cpu(tpa_start1->rx_tpa_start_cmp_metadata);
	}
}

static void bnge_tpa_metadata_v2(struct bnge_tpa_info *tpa_info,
				 struct rx_tpa_start_cmp *tpa_start,
				 struct rx_tpa_start_cmp_ext *tpa_start1)
{
	tpa_info->vlan_valid = 0;
	if (TPA_START_VLAN_VALID(tpa_start)) {
		u32 tpid_sel = TPA_START_VLAN_TPID_SEL(tpa_start);
		u32 vlan_proto = ETH_P_8021Q;

		tpa_info->vlan_valid = 1;
		if (tpid_sel == RX_TPA_START_METADATA1_TPID_8021AD)
			vlan_proto = ETH_P_8021AD;
		tpa_info->metadata = vlan_proto << 16 |
				     TPA_START_METADATA0_TCI(tpa_start1);
	}
}

static void bnge_tpa_start(struct bnge_net *bn, struct bnge_rx_ring_info *rxr,
			   u8 cmp_type, struct rx_tpa_start_cmp *tpa_start,
			   struct rx_tpa_start_cmp_ext *tpa_start1)
{
	struct bnge_sw_rx_bd *cons_rx_buf, *prod_rx_buf;
	struct bnge_tpa_info *tpa_info;
	u16 cons, prod, agg_id;
	struct rx_bd *prod_bd;
	dma_addr_t mapping;

	agg_id = TPA_START_AGG_ID(tpa_start);
	agg_id = bnge_tpa_alloc_agg_idx(rxr, agg_id);
	if (unlikely(agg_id == INVALID_HW_RING_ID)) {
		netdev_warn(bn->netdev, "Unable to allocate agg ID for ring %d, agg 0x%lx\n",
			    rxr->bnapi->index, TPA_START_AGG_ID(tpa_start));
		bnge_sched_reset_rxr(bn, rxr);
		return;
	}
	cons = tpa_start->rx_tpa_start_cmp_opaque;
	prod = rxr->rx_prod;
	cons_rx_buf = &rxr->rx_buf_ring[cons];
	prod_rx_buf = &rxr->rx_buf_ring[RING_RX(bn, prod)];
	tpa_info = &rxr->rx_tpa[agg_id];

	if (unlikely(cons != rxr->rx_next_cons ||
		     TPA_START_ERROR(tpa_start))) {
		netdev_warn(bn->netdev, "TPA cons %x, expected cons %x, error code %lx\n",
			    cons, rxr->rx_next_cons,
			    TPA_START_ERROR_CODE(tpa_start1));
		bnge_sched_reset_rxr(bn, rxr);
		return;
	}
	prod_rx_buf->data = tpa_info->data;
	prod_rx_buf->data_ptr = tpa_info->data_ptr;

	mapping = tpa_info->mapping;
	prod_rx_buf->mapping = mapping;

	prod_bd = &rxr->rx_desc_ring[RX_RING(bn, prod)][RX_IDX(prod)];

	prod_bd->rx_bd_haddr = cpu_to_le64(mapping);

	tpa_info->data = cons_rx_buf->data;
	tpa_info->data_ptr = cons_rx_buf->data_ptr;
	cons_rx_buf->data = NULL;
	tpa_info->mapping = cons_rx_buf->mapping;

	tpa_info->len =
		le32_to_cpu(tpa_start->rx_tpa_start_cmp_len_flags_type) >>
				RX_TPA_START_CMP_LEN_SHIFT;
	if (likely(TPA_START_HASH_VALID(tpa_start))) {
		tpa_info->hash_type = PKT_HASH_TYPE_L4;
		if (TPA_START_IS_IPV6(tpa_start1))
			tpa_info->gso_type = SKB_GSO_TCPV6;
		else
			tpa_info->gso_type = SKB_GSO_TCPV4;
		tpa_info->rss_hash =
			le32_to_cpu(tpa_start->rx_tpa_start_cmp_rss_hash);
	} else {
		tpa_info->hash_type = PKT_HASH_TYPE_NONE;
		tpa_info->gso_type = 0;
		netif_warn(bn, rx_err, bn->netdev, "TPA packet without valid hash\n");
	}
	tpa_info->flags2 = le32_to_cpu(tpa_start1->rx_tpa_start_cmp_flags2);
	tpa_info->hdr_info = le32_to_cpu(tpa_start1->rx_tpa_start_cmp_hdr_info);
	if (cmp_type == CMP_TYPE_RX_L2_TPA_START_CMP)
		bnge_tpa_metadata(tpa_info, tpa_start, tpa_start1);
	else
		bnge_tpa_metadata_v2(tpa_info, tpa_start, tpa_start1);
	tpa_info->agg_count = 0;

	rxr->rx_prod = NEXT_RX(prod);
	cons = RING_RX(bn, NEXT_RX(cons));
	rxr->rx_next_cons = RING_RX(bn, NEXT_RX(cons));
	cons_rx_buf = &rxr->rx_buf_ring[cons];

	bnge_reuse_rx_data(rxr, cons, cons_rx_buf->data);
	rxr->rx_prod = NEXT_RX(rxr->rx_prod);
	cons_rx_buf->data = NULL;
}

static void bnge_abort_tpa(struct bnge_cp_ring_info *cpr, u16 idx, u32 agg_bufs)
{
	if (agg_bufs)
		bnge_reuse_rx_agg_bufs(cpr, idx, 0, agg_bufs, true);
}

static void bnge_tpa_agg(struct bnge_net *bn, struct bnge_rx_ring_info *rxr,
			 struct rx_agg_cmp *rx_agg)
{
	u16 agg_id = TPA_AGG_AGG_ID(rx_agg);
	struct bnge_tpa_info *tpa_info;

	agg_id = bnge_lookup_agg_idx(rxr, agg_id);
	tpa_info = &rxr->rx_tpa[agg_id];

	if (unlikely(tpa_info->agg_count >= MAX_SKB_FRAGS)) {
		netdev_warn(bn->netdev,
			    "TPA completion count %d exceeds limit for ring %d\n",
			    tpa_info->agg_count, rxr->bnapi->index);

		bnge_sched_reset_rxr(bn, rxr);
		return;
	}

	tpa_info->agg_arr[tpa_info->agg_count++] = *rx_agg;
}

void bnge_reuse_rx_data(struct bnge_rx_ring_info *rxr, u16 cons, void *data)
{
	struct bnge_sw_rx_bd *cons_rx_buf, *prod_rx_buf;
	struct bnge_net *bn = rxr->bnapi->bn;
	struct rx_bd *cons_bd, *prod_bd;
	u16 prod = rxr->rx_prod;

	prod_rx_buf = &rxr->rx_buf_ring[RING_RX(bn, prod)];
	cons_rx_buf = &rxr->rx_buf_ring[cons];

	prod_rx_buf->data = data;
	prod_rx_buf->data_ptr = cons_rx_buf->data_ptr;

	prod_rx_buf->mapping = cons_rx_buf->mapping;

	prod_bd = &rxr->rx_desc_ring[RX_RING(bn, prod)][RX_IDX(prod)];
	cons_bd = &rxr->rx_desc_ring[RX_RING(bn, cons)][RX_IDX(cons)];

	prod_bd->rx_bd_haddr = cons_bd->rx_bd_haddr;
}

static void bnge_deliver_skb(struct bnge_net *bn, struct bnge_napi *bnapi,
			     struct sk_buff *skb)
{
	skb_mark_for_recycle(skb);
	skb_record_rx_queue(skb, bnapi->index);
	napi_gro_receive(&bnapi->napi, skb);
}

static struct sk_buff *bnge_copy_skb(struct bnge_napi *bnapi, u8 *data,
				     unsigned int len, dma_addr_t mapping)
{
	struct bnge_net *bn = bnapi->bn;
	struct bnge_dev *bd = bn->bd;
	struct sk_buff *skb;

	skb = napi_alloc_skb(&bnapi->napi, len);
	if (!skb)
		return NULL;

	dma_sync_single_for_cpu(bd->dev, mapping, len, bn->rx_dir);

	memcpy(skb->data - NET_IP_ALIGN, data - NET_IP_ALIGN,
	       len + NET_IP_ALIGN);

	dma_sync_single_for_device(bd->dev, mapping, len, bn->rx_dir);

	skb_put(skb, len);

	return skb;
}

#ifdef CONFIG_INET
static void bnge_gro_tunnel(struct sk_buff *skb, __be16 ip_proto)
{
	struct udphdr *uh = NULL;

	if (ip_proto == htons(ETH_P_IP)) {
		struct iphdr *iph = (struct iphdr *)skb->data;

		if (iph->protocol == IPPROTO_UDP)
			uh = (struct udphdr *)(iph + 1);
	} else {
		struct ipv6hdr *iph = (struct ipv6hdr *)skb->data;

		if (iph->nexthdr == IPPROTO_UDP)
			uh = (struct udphdr *)(iph + 1);
	}
	if (uh) {
		if (uh->check)
			skb_shinfo(skb)->gso_type |= SKB_GSO_UDP_TUNNEL_CSUM;
		else
			skb_shinfo(skb)->gso_type |= SKB_GSO_UDP_TUNNEL;
	}
}

static struct sk_buff *bnge_gro_func(struct bnge_tpa_info *tpa_info,
				     int payload_off, int tcp_ts,
				     struct sk_buff *skb)
{
	u16 outer_ip_off, inner_ip_off, inner_mac_off;
	u32 hdr_info = tpa_info->hdr_info;
	int iphdr_len, nw_off;

	inner_ip_off = BNGE_TPA_INNER_L3_OFF(hdr_info);
	inner_mac_off = BNGE_TPA_INNER_L2_OFF(hdr_info);
	outer_ip_off = BNGE_TPA_OUTER_L3_OFF(hdr_info);

	nw_off = inner_ip_off - ETH_HLEN;
	skb_set_network_header(skb, nw_off);
	iphdr_len = (tpa_info->flags2 & RX_TPA_START_CMP_FLAGS2_IP_TYPE) ?
		     sizeof(struct ipv6hdr) : sizeof(struct iphdr);
	skb_set_transport_header(skb, nw_off + iphdr_len);

	if (inner_mac_off) { /* tunnel */
		__be16 proto = *((__be16 *)(skb->data + outer_ip_off -
					    ETH_HLEN - 2));

		bnge_gro_tunnel(skb, proto);
	}

	return skb;
}

static struct sk_buff *bnge_gro_skb(struct bnge_net *bn,
				    struct bnge_tpa_info *tpa_info,
				    struct rx_tpa_end_cmp *tpa_end,
				    struct rx_tpa_end_cmp_ext *tpa_end1,
				    struct sk_buff *skb)
{
	int payload_off;
	u16 segs;

	segs = TPA_END_TPA_SEGS(tpa_end);
	if (segs == 1)
		return skb;

	NAPI_GRO_CB(skb)->count = segs;
	skb_shinfo(skb)->gso_size =
		le32_to_cpu(tpa_end1->rx_tpa_end_cmp_seg_len);
	skb_shinfo(skb)->gso_type = tpa_info->gso_type;
	payload_off = TPA_END_PAYLOAD_OFF(tpa_end1);
	skb = bnge_gro_func(tpa_info, payload_off,
			    TPA_END_GRO_TS(tpa_end), skb);
	if (likely(skb))
		tcp_gro_complete(skb);

	return skb;
}
#endif

static struct sk_buff *bnge_tpa_end(struct bnge_net *bn,
				    struct bnge_cp_ring_info *cpr,
				    u32 *raw_cons,
				    struct rx_tpa_end_cmp *tpa_end,
				    struct rx_tpa_end_cmp_ext *tpa_end1,
				    u8 *event)
{
	struct bnge_napi *bnapi = cpr->bnapi;
	struct net_device *dev = bn->netdev;
	struct bnge_tpa_info *tpa_info;
	struct bnge_rx_ring_info *rxr;
	u8 *data_ptr, agg_bufs;
	struct sk_buff *skb;
	u16 idx = 0, agg_id;
	dma_addr_t mapping;
	unsigned int len;
	void *data;

	if (unlikely(bnapi->in_reset)) {
		int rc = bnge_discard_rx(bn, cpr, raw_cons, tpa_end);

		if (rc < 0)
			return ERR_PTR(-EBUSY);
		return NULL;
	}

	rxr = bnapi->rx_ring;
	agg_id = TPA_END_AGG_ID(tpa_end);
	agg_id = bnge_lookup_agg_idx(rxr, agg_id);
	agg_bufs = TPA_END_AGG_BUFS(tpa_end1);
	tpa_info = &rxr->rx_tpa[agg_id];
	if (unlikely(agg_bufs != tpa_info->agg_count)) {
		netdev_warn(bn->netdev, "TPA end agg_buf %d != expected agg_bufs %d\n",
			    agg_bufs, tpa_info->agg_count);
		agg_bufs = tpa_info->agg_count;
	}
	tpa_info->agg_count = 0;
	*event |= BNGE_AGG_EVENT;
	bnge_free_agg_idx(rxr, agg_id);
	idx = agg_id;
	data = tpa_info->data;
	data_ptr = tpa_info->data_ptr;
	prefetch(data_ptr);
	len = tpa_info->len;
	mapping = tpa_info->mapping;

	if (unlikely(agg_bufs > MAX_SKB_FRAGS || TPA_END_ERRORS(tpa_end1))) {
		bnge_abort_tpa(cpr, idx, agg_bufs);
		if (agg_bufs > MAX_SKB_FRAGS)
			netdev_warn(bn->netdev, "TPA frags %d exceeded MAX_SKB_FRAGS %d\n",
				    agg_bufs, (int)MAX_SKB_FRAGS);
		return NULL;
	}

	if (len <= bn->rx_copybreak) {
		skb = bnge_copy_skb(bnapi, data_ptr, len, mapping);
		if (!skb) {
			bnge_abort_tpa(cpr, idx, agg_bufs);
			return NULL;
		}
	} else {
		dma_addr_t new_mapping;
		u8 *new_data;

		new_data = __bnge_alloc_rx_frag(bn, &new_mapping, rxr,
						GFP_ATOMIC);
		if (!new_data) {
			bnge_abort_tpa(cpr, idx, agg_bufs);
			return NULL;
		}

		tpa_info->data = new_data;
		tpa_info->data_ptr = new_data + bn->rx_offset;
		tpa_info->mapping = new_mapping;

		skb = napi_build_skb(data, bn->rx_buf_size);
		dma_sync_single_for_cpu(bn->bd->dev, mapping,
					bn->rx_buf_use_size, bn->rx_dir);

		if (!skb) {
			page_pool_free_va(rxr->head_pool, data, true);
			bnge_abort_tpa(cpr, idx, agg_bufs);
			return NULL;
		}
		skb_mark_for_recycle(skb);
		skb_reserve(skb, bn->rx_offset);
		skb_put(skb, len);
	}

	if (agg_bufs) {
		skb = bnge_rx_agg_netmems_skb(bn, cpr, skb, idx, agg_bufs,
					      true);
		/* Page reuse already handled by bnge_rx_agg_netmems_skb(). */
		if (!skb)
			return NULL;
	}

	skb->protocol = eth_type_trans(skb, dev);

	if (tpa_info->hash_type != PKT_HASH_TYPE_NONE)
		skb_set_hash(skb, tpa_info->rss_hash, tpa_info->hash_type);

	if (tpa_info->vlan_valid &&
	    (dev->features & BNGE_HW_FEATURE_VLAN_ALL_RX)) {
		__be16 vlan_proto = htons(tpa_info->metadata >>
					  RX_CMP_FLAGS2_METADATA_TPID_SFT);
		u16 vtag = tpa_info->metadata & RX_CMP_FLAGS2_METADATA_TCI_MASK;

		if (eth_type_vlan(vlan_proto)) {
			__vlan_hwaccel_put_tag(skb, vlan_proto, vtag);
		} else {
			dev_kfree_skb(skb);
			return NULL;
		}
	}

	skb_checksum_none_assert(skb);
	if (likely(tpa_info->flags2 & RX_TPA_START_CMP_FLAGS2_L4_CS_CALC)) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		skb->csum_level =
			(tpa_info->flags2 & RX_CMP_FLAGS2_T_L4_CS_CALC) >> 3;
	}

#ifdef CONFIG_INET
	if (bn->priv_flags & BNGE_NET_EN_GRO)
		skb = bnge_gro_skb(bn, tpa_info, tpa_end, tpa_end1, skb);
#endif

	return skb;
}

static enum pkt_hash_types bnge_rss_ext_op(struct bnge_net *bn,
					   struct rx_cmp *rxcmp)
{
	u8 ext_op = RX_CMP_V3_HASH_TYPE(bn->bd, rxcmp);

	switch (ext_op) {
	case EXT_OP_INNER_4:
	case EXT_OP_OUTER_4:
	case EXT_OP_INNFL_3:
	case EXT_OP_OUTFL_3:
		return PKT_HASH_TYPE_L4;
	default:
		return PKT_HASH_TYPE_L3;
	}
}

static struct sk_buff *bnge_rx_vlan(struct sk_buff *skb, u8 cmp_type,
				    struct rx_cmp *rxcmp,
				    struct rx_cmp_ext *rxcmp1)
{
	__be16 vlan_proto;
	u16 vtag;

	if (cmp_type == CMP_TYPE_RX_L2_CMP) {
		__le32 flags2 = rxcmp1->rx_cmp_flags2;
		u32 meta_data;

		if (!(flags2 & cpu_to_le32(RX_CMP_FLAGS2_META_FORMAT_VLAN)))
			return skb;

		meta_data = le32_to_cpu(rxcmp1->rx_cmp_meta_data);
		vtag = meta_data & RX_CMP_FLAGS2_METADATA_TCI_MASK;
		vlan_proto =
			htons(meta_data >> RX_CMP_FLAGS2_METADATA_TPID_SFT);
		if (eth_type_vlan(vlan_proto))
			__vlan_hwaccel_put_tag(skb, vlan_proto, vtag);
		else
			goto vlan_err;
	} else if (cmp_type == CMP_TYPE_RX_L2_V3_CMP) {
		if (RX_CMP_VLAN_VALID(rxcmp)) {
			u32 tpid_sel = RX_CMP_VLAN_TPID_SEL(rxcmp);

			if (tpid_sel == RX_CMP_METADATA1_TPID_8021Q)
				vlan_proto = htons(ETH_P_8021Q);
			else if (tpid_sel == RX_CMP_METADATA1_TPID_8021AD)
				vlan_proto = htons(ETH_P_8021AD);
			else
				goto vlan_err;
			vtag = RX_CMP_METADATA0_TCI(rxcmp1);
			__vlan_hwaccel_put_tag(skb, vlan_proto, vtag);
		}
	}
	return skb;

vlan_err:
	skb_mark_for_recycle(skb);
	dev_kfree_skb(skb);
	return NULL;
}

static struct sk_buff *bnge_rx_skb(struct bnge_net *bn,
				   struct bnge_rx_ring_info *rxr, u16 cons,
				   void *data, u8 *data_ptr,
				   dma_addr_t dma_addr,
				   unsigned int len)
{
	struct bnge_dev *bd = bn->bd;
	u16 prod = rxr->rx_prod;
	struct sk_buff *skb;
	int err;

	err = bnge_alloc_rx_data(bn, rxr, prod, GFP_ATOMIC);
	if (unlikely(err)) {
		bnge_reuse_rx_data(rxr, cons, data);
		return NULL;
	}

	dma_sync_single_for_cpu(bd->dev, dma_addr, len, bn->rx_dir);
	skb = napi_build_skb(data, bn->rx_buf_size);
	if (!skb) {
		page_pool_free_va(rxr->head_pool, data, true);
		return NULL;
	}

	skb_mark_for_recycle(skb);
	skb_reserve(skb, bn->rx_offset);
	skb_put(skb, len);
	return skb;
}

/* returns the following:
 * 1       - 1 packet successfully received
 * 0       - successful TPA_START, packet not completed yet
 * -EBUSY  - completion ring does not have all the agg buffers yet
 * -ENOMEM - packet aborted due to out of memory
 * -EIO    - packet aborted due to hw error indicated in BD
 */
static int bnge_rx_pkt(struct bnge_net *bn, struct bnge_cp_ring_info *cpr,
		       u32 *raw_cons, u8 *event)
{
	struct bnge_napi *bnapi = cpr->bnapi;
	struct net_device *dev = bn->netdev;
	struct bnge_rx_ring_info *rxr;
	u32 tmp_raw_cons, flags, misc;
	struct bnge_sw_rx_bd *rx_buf;
	struct rx_cmp_ext *rxcmp1;
	u16 cons, prod, cp_cons;
	u8 *data_ptr, cmp_type;
	struct rx_cmp *rxcmp;
	dma_addr_t dma_addr;
	struct sk_buff *skb;
	unsigned int len;
	u8 agg_bufs;
	void *data;
	int rc = 0;

	rxr = bnapi->rx_ring;

	tmp_raw_cons = *raw_cons;
	cp_cons = RING_CMP(bn, tmp_raw_cons);
	rxcmp = (struct rx_cmp *)
			&cpr->desc_ring[CP_RING(cp_cons)][CP_IDX(cp_cons)];

	cmp_type = RX_CMP_TYPE(rxcmp);

	if (cmp_type == CMP_TYPE_RX_TPA_AGG_CMP) {
		bnge_tpa_agg(bn, rxr, (struct rx_agg_cmp *)rxcmp);
		goto next_rx_no_prod_no_len;
	}

	tmp_raw_cons = NEXT_RAW_CMP(tmp_raw_cons);
	cp_cons = RING_CMP(bn, tmp_raw_cons);
	rxcmp1 = (struct rx_cmp_ext *)
			&cpr->desc_ring[CP_RING(cp_cons)][CP_IDX(cp_cons)];

	if (!RX_CMP_VALID(bn, rxcmp1, tmp_raw_cons))
		return -EBUSY;

	/* The valid test of the entry must be done first before
	 * reading any further.
	 */
	dma_rmb();
	prod = rxr->rx_prod;

	if (cmp_type == CMP_TYPE_RX_L2_TPA_START_CMP ||
	    cmp_type == CMP_TYPE_RX_L2_TPA_START_V3_CMP) {
		bnge_tpa_start(bn, rxr, cmp_type,
			       (struct rx_tpa_start_cmp *)rxcmp,
			       (struct rx_tpa_start_cmp_ext *)rxcmp1);

		*event |= BNGE_RX_EVENT;
		goto next_rx_no_prod_no_len;

	} else if (cmp_type == CMP_TYPE_RX_L2_TPA_END_CMP) {
		skb = bnge_tpa_end(bn, cpr, &tmp_raw_cons,
				   (struct rx_tpa_end_cmp *)rxcmp,
				   (struct rx_tpa_end_cmp_ext *)rxcmp1, event);
		if (IS_ERR(skb))
			return -EBUSY;

		rc = -ENOMEM;
		if (likely(skb)) {
			bnge_deliver_skb(bn, bnapi, skb);
			rc = 1;
		}
		*event |= BNGE_RX_EVENT;
		goto next_rx_no_prod_no_len;
	}

	cons = rxcmp->rx_cmp_opaque;
	if (unlikely(cons != rxr->rx_next_cons)) {
		int rc1 = bnge_discard_rx(bn, cpr, &tmp_raw_cons, rxcmp);

		/* 0xffff is forced error, don't print it */
		if (rxr->rx_next_cons != 0xffff)
			netdev_warn(bn->netdev, "RX cons %x != expected cons %x\n",
				    cons, rxr->rx_next_cons);
		bnge_sched_reset_rxr(bn, rxr);
		if (rc1)
			return rc1;
		goto next_rx_no_prod_no_len;
	}
	rx_buf = &rxr->rx_buf_ring[cons];
	data = rx_buf->data;
	data_ptr = rx_buf->data_ptr;
	prefetch(data_ptr);

	misc = le32_to_cpu(rxcmp->rx_cmp_misc_v1);
	agg_bufs = (misc & RX_CMP_AGG_BUFS) >> RX_CMP_AGG_BUFS_SHIFT;

	if (agg_bufs) {
		if (!bnge_agg_bufs_valid(bn, cpr, agg_bufs, &tmp_raw_cons))
			return -EBUSY;

		cp_cons = NEXT_CMP(bn, cp_cons);
		*event |= BNGE_AGG_EVENT;
	}
	*event |= BNGE_RX_EVENT;

	rx_buf->data = NULL;
	if (rxcmp1->rx_cmp_cfa_code_errors_v2 & RX_CMP_L2_ERRORS) {
		bnge_reuse_rx_data(rxr, cons, data);
		if (agg_bufs)
			bnge_reuse_rx_agg_bufs(cpr, cp_cons, 0, agg_bufs,
					       false);
		rc = -EIO;
		goto next_rx_no_len;
	}

	flags = le32_to_cpu(rxcmp->rx_cmp_len_flags_type);
	len = flags >> RX_CMP_LEN_SHIFT;
	dma_addr = rx_buf->mapping;

	if (len <= bn->rx_copybreak) {
		skb = bnge_copy_skb(bnapi, data_ptr, len, dma_addr);
		bnge_reuse_rx_data(rxr, cons, data);
	} else {
		skb = bnge_rx_skb(bn, rxr, cons, data, data_ptr, dma_addr, len);
	}

	if (!skb) {
		if (agg_bufs)
			bnge_reuse_rx_agg_bufs(cpr, cp_cons, 0,
					       agg_bufs, false);
		goto oom_next_rx;
	}

	if (agg_bufs) {
		skb = bnge_rx_agg_netmems_skb(bn, cpr, skb, cp_cons,
					      agg_bufs, false);
		if (!skb)
			goto oom_next_rx;
	}

	if (RX_CMP_HASH_VALID(rxcmp)) {
		enum pkt_hash_types type;

		if (cmp_type == CMP_TYPE_RX_L2_V3_CMP) {
			type = bnge_rss_ext_op(bn, rxcmp);
		} else {
			u32 itypes = RX_CMP_ITYPES(rxcmp);

			if (itypes == RX_CMP_FLAGS_ITYPE_TCP ||
			    itypes == RX_CMP_FLAGS_ITYPE_UDP)
				type = PKT_HASH_TYPE_L4;
			else
				type = PKT_HASH_TYPE_L3;
		}
		skb_set_hash(skb, le32_to_cpu(rxcmp->rx_cmp_rss_hash), type);
	}

	skb->protocol = eth_type_trans(skb, dev);

	if (skb->dev->features & BNGE_HW_FEATURE_VLAN_ALL_RX) {
		skb = bnge_rx_vlan(skb, cmp_type, rxcmp, rxcmp1);
		if (!skb)
			goto next_rx;
	}

	skb_checksum_none_assert(skb);
	if (RX_CMP_L4_CS_OK(rxcmp1)) {
		if (dev->features & NETIF_F_RXCSUM) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			skb->csum_level = RX_CMP_ENCAP(rxcmp1);
		}
	}

	bnge_deliver_skb(bn, bnapi, skb);
	rc = 1;

next_rx:
	/* Update Stats */
next_rx_no_len:
	rxr->rx_prod = NEXT_RX(prod);
	rxr->rx_next_cons = RING_RX(bn, NEXT_RX(cons));

next_rx_no_prod_no_len:
	*raw_cons = tmp_raw_cons;
	return rc;

oom_next_rx:
	rc = -ENOMEM;
	goto next_rx;
}

/* In netpoll mode, if we are using a combined completion ring, we need to
 * discard the rx packets and recycle the buffers.
 */
static int bnge_force_rx_discard(struct bnge_net *bn,
				 struct bnge_cp_ring_info *cpr,
				 u32 *raw_cons, u8 *event)
{
	u32 tmp_raw_cons = *raw_cons;
	struct rx_cmp_ext *rxcmp1;
	struct rx_cmp *rxcmp;
	u16 cp_cons;
	u8 cmp_type;
	int rc;

	cp_cons = RING_CMP(bn, tmp_raw_cons);
	rxcmp = (struct rx_cmp *)
			&cpr->desc_ring[CP_RING(cp_cons)][CP_IDX(cp_cons)];

	tmp_raw_cons = NEXT_RAW_CMP(tmp_raw_cons);
	cp_cons = RING_CMP(bn, tmp_raw_cons);
	rxcmp1 = (struct rx_cmp_ext *)
			&cpr->desc_ring[CP_RING(cp_cons)][CP_IDX(cp_cons)];

	if (!RX_CMP_VALID(bn, rxcmp1, tmp_raw_cons))
		return -EBUSY;

	/* The valid test of the entry must be done first before
	 * reading any further.
	 */
	dma_rmb();
	cmp_type = RX_CMP_TYPE(rxcmp);
	if (cmp_type == CMP_TYPE_RX_L2_CMP ||
	    cmp_type == CMP_TYPE_RX_L2_V3_CMP) {
		rxcmp1->rx_cmp_cfa_code_errors_v2 |=
			cpu_to_le32(RX_CMPL_ERRORS_CRC_ERROR);
	} else if (cmp_type == CMP_TYPE_RX_L2_TPA_END_CMP) {
		struct rx_tpa_end_cmp_ext *tpa_end1;

		tpa_end1 = (struct rx_tpa_end_cmp_ext *)rxcmp1;
		tpa_end1->rx_tpa_end_cmp_errors_v2 |=
			cpu_to_le32(RX_TPA_END_CMP_ERRORS);
	}
	rc = bnge_rx_pkt(bn, cpr, raw_cons, event);
	return rc;
}

static void __bnge_tx_int(struct bnge_net *bn, struct bnge_tx_ring_info *txr,
			  int budget)
{
	u16 hw_cons = txr->tx_hw_cons;
	struct bnge_dev *bd = bn->bd;
	unsigned int tx_bytes = 0;
	unsigned int tx_pkts = 0;
	struct netdev_queue *txq;
	u16 cons = txr->tx_cons;
	skb_frag_t *frag;

	txq = netdev_get_tx_queue(bn->netdev, txr->txq_index);

	while (SW_TX_RING(bn, cons) != hw_cons) {
		struct bnge_sw_tx_bd *tx_buf;
		struct sk_buff *skb;
		int j, last;

		tx_buf = &txr->tx_buf_ring[SW_TX_RING(bn, cons)];
		skb = tx_buf->skb;
		if (unlikely(!skb)) {
			bnge_sched_reset_txr(bn, txr, cons);
			return;
		}

		cons = NEXT_TX(cons);
		tx_pkts++;
		tx_bytes += skb->len;
		tx_buf->skb = NULL;

		dma_unmap_single(bd->dev, dma_unmap_addr(tx_buf, mapping),
				 skb_headlen(skb), DMA_TO_DEVICE);
		last = tx_buf->nr_frags;

		for (j = 0; j < last; j++) {
			frag = &skb_shinfo(skb)->frags[j];
			cons = NEXT_TX(cons);
			tx_buf = &txr->tx_buf_ring[SW_TX_RING(bn, cons)];
			netmem_dma_unmap_page_attrs(bd->dev,
						    dma_unmap_addr(tx_buf,
								   mapping),
						    skb_frag_size(frag),
						    DMA_TO_DEVICE, 0);
		}

		cons = NEXT_TX(cons);

		napi_consume_skb(skb, budget);
	}

	WRITE_ONCE(txr->tx_cons, cons);

	__netif_txq_completed_wake(txq, tx_pkts, tx_bytes,
				   bnge_tx_avail(bn, txr), bn->tx_wake_thresh,
				   (READ_ONCE(txr->dev_state) ==
				    BNGE_DEV_STATE_CLOSING));
}

static void bnge_tx_int(struct bnge_net *bn, struct bnge_napi *bnapi,
			int budget)
{
	struct bnge_tx_ring_info *txr;
	int i;

	bnge_for_each_napi_tx(i, bnapi, txr) {
		if (txr->tx_hw_cons != SW_TX_RING(bn, txr->tx_cons))
			__bnge_tx_int(bn, txr, budget);
	}

	bnapi->events &= ~BNGE_TX_CMP_EVENT;
}

static void __bnge_poll_work_done(struct bnge_net *bn, struct bnge_napi *bnapi,
				  int budget)
{
	struct bnge_rx_ring_info *rxr = bnapi->rx_ring;

	if ((bnapi->events & BNGE_TX_CMP_EVENT) && !bnapi->tx_fault)
		bnge_tx_int(bn, bnapi, budget);

	if ((bnapi->events & BNGE_RX_EVENT)) {
		bnge_db_write(bn->bd, &rxr->rx_db, rxr->rx_prod);
		bnapi->events &= ~BNGE_RX_EVENT;
	}

	if (bnapi->events & BNGE_AGG_EVENT) {
		bnge_db_write(bn->bd, &rxr->rx_agg_db, rxr->rx_agg_prod);
		bnapi->events &= ~BNGE_AGG_EVENT;
	}
}

static void
bnge_hwrm_update_token(struct bnge_dev *bd, u16 seq_id,
		       enum bnge_hwrm_wait_state state)
{
	struct bnge_hwrm_wait_token *token;

	rcu_read_lock();
	hlist_for_each_entry_rcu(token, &bd->hwrm_pending_list, node) {
		if (token->seq_id == seq_id) {
			WRITE_ONCE(token->state, state);
			rcu_read_unlock();
			return;
		}
	}
	rcu_read_unlock();
	dev_err(bd->dev, "Invalid hwrm seq id %d\n", seq_id);
}

static int bnge_hwrm_handler(struct bnge_dev *bd, struct tx_cmp *txcmp)
{
	struct hwrm_cmpl *h_cmpl = (struct hwrm_cmpl *)txcmp;
	u16 cmpl_type = TX_CMP_TYPE(txcmp), seq_id;

	switch (cmpl_type) {
	case CMPL_BASE_TYPE_HWRM_DONE:
		seq_id = le16_to_cpu(h_cmpl->sequence_id);
		bnge_hwrm_update_token(bd, seq_id, BNGE_HWRM_COMPLETE);
		break;

	case CMPL_BASE_TYPE_HWRM_ASYNC_EVENT:
	default:
		break;
	}

	return 0;
}

static int __bnge_poll_work(struct bnge_net *bn, struct bnge_cp_ring_info *cpr,
			    int budget)
{
	struct bnge_napi *bnapi = cpr->bnapi;
	u32 raw_cons = cpr->cp_raw_cons;
	struct tx_cmp *txcmp;
	int rx_pkts = 0;
	u8 event = 0;
	u32 cons;

	cpr->has_more_work = 0;
	cpr->had_work_done = 1;
	while (1) {
		u8 cmp_type;
		int rc;

		cons = RING_CMP(bn, raw_cons);
		txcmp = &cpr->desc_ring[CP_RING(cons)][CP_IDX(cons)];

		if (!TX_CMP_VALID(bn, txcmp, raw_cons))
			break;

		/* The valid test of the entry must be done first before
		 * reading any further.
		 */
		dma_rmb();
		cmp_type = TX_CMP_TYPE(txcmp);
		if (cmp_type == CMP_TYPE_TX_L2_CMP ||
		    cmp_type == CMP_TYPE_TX_L2_COAL_CMP) {
			u32 opaque = txcmp->tx_cmp_opaque;
			struct bnge_tx_ring_info *txr;
			u16 tx_freed;

			txr = bnapi->tx_ring[TX_OPAQUE_RING(opaque)];
			event |= BNGE_TX_CMP_EVENT;
			if (cmp_type == CMP_TYPE_TX_L2_COAL_CMP)
				txr->tx_hw_cons = TX_CMP_SQ_CONS_IDX(txcmp);
			else
				txr->tx_hw_cons = TX_OPAQUE_PROD(bn, opaque);
			tx_freed = ((txr->tx_hw_cons - txr->tx_cons) &
				    bn->tx_ring_mask);
			/* return full budget so NAPI will complete. */
			if (unlikely(tx_freed >= bn->tx_wake_thresh)) {
				rx_pkts = budget;
				raw_cons = NEXT_RAW_CMP(raw_cons);
				if (budget)
					cpr->has_more_work = 1;
				break;
			}
		} else if (cmp_type >= CMP_TYPE_RX_L2_CMP &&
			   cmp_type <= CMP_TYPE_RX_L2_TPA_START_V3_CMP) {
			if (likely(budget))
				rc = bnge_rx_pkt(bn, cpr, &raw_cons, &event);
			else
				rc = bnge_force_rx_discard(bn, cpr, &raw_cons,
							   &event);
			if (likely(rc >= 0))
				rx_pkts += rc;
			/* Increment rx_pkts when rc is -ENOMEM to count towards
			 * the NAPI budget.  Otherwise, we may potentially loop
			 * here forever if we consistently cannot allocate
			 * buffers.
			 */
			else if (rc == -ENOMEM && budget)
				rx_pkts++;
			else if (rc == -EBUSY)	/* partial completion */
				break;
		} else if (unlikely(cmp_type == CMPL_BASE_TYPE_HWRM_DONE ||
				    cmp_type == CMPL_BASE_TYPE_HWRM_FWD_REQ ||
				    cmp_type == CMPL_BA_TY_HWRM_ASY_EVT)) {
			bnge_hwrm_handler(bn->bd, txcmp);
		}
		raw_cons = NEXT_RAW_CMP(raw_cons);

		if (rx_pkts && rx_pkts == budget) {
			cpr->has_more_work = 1;
			break;
		}
	}

	cpr->cp_raw_cons = raw_cons;
	bnapi->events |= event;
	return rx_pkts;
}

static void __bnge_poll_cqs_done(struct bnge_net *bn, struct bnge_napi *bnapi,
				 u64 dbr_type, int budget)
{
	struct bnge_nq_ring_info *nqr = &bnapi->nq_ring;
	int i;

	for (i = 0; i < nqr->cp_ring_count; i++) {
		struct bnge_cp_ring_info *cpr = &nqr->cp_ring_arr[i];
		struct bnge_db_info *db;

		if (cpr->had_work_done) {
			u32 tgl = 0;

			if (dbr_type == DBR_TYPE_CQ_ARMALL) {
				cpr->had_nqe_notify = 0;
				tgl = cpr->toggle;
			}
			db = &cpr->cp_db;
			bnge_writeq(bn->bd,
				    db->db_key64 | dbr_type | DB_TOGGLE(tgl) |
				    DB_RING_IDX(db, cpr->cp_raw_cons),
				    db->doorbell);
			cpr->had_work_done = 0;
		}
	}
	__bnge_poll_work_done(bn, bnapi, budget);
}

static int __bnge_poll_cqs(struct bnge_net *bn, struct bnge_napi *bnapi,
			   int budget)
{
	struct bnge_nq_ring_info *nqr = &bnapi->nq_ring;
	int i, work_done = 0;

	for (i = 0; i < nqr->cp_ring_count; i++) {
		struct bnge_cp_ring_info *cpr = &nqr->cp_ring_arr[i];

		if (cpr->had_nqe_notify) {
			work_done += __bnge_poll_work(bn, cpr,
						      budget - work_done);
			nqr->has_more_work |= cpr->has_more_work;
		}
	}
	return work_done;
}

int bnge_napi_poll(struct napi_struct *napi, int budget)
{
	struct bnge_napi *bnapi = container_of(napi, struct bnge_napi, napi);
	struct bnge_nq_ring_info *nqr = &bnapi->nq_ring;
	u32 raw_cons = nqr->nq_raw_cons;
	struct bnge_net *bn = bnapi->bn;
	struct bnge_dev *bd = bn->bd;
	struct nqe_cn *nqcmp;
	int work_done = 0;
	u32 cons;

	if (nqr->has_more_work) {
		nqr->has_more_work = 0;
		work_done = __bnge_poll_cqs(bn, bnapi, budget);
	}

	while (1) {
		u16 type;

		cons = RING_CMP(bn, raw_cons);
		nqcmp = &nqr->desc_ring[CP_RING(cons)][CP_IDX(cons)];

		if (!NQ_CMP_VALID(bn, nqcmp, raw_cons)) {
			if (nqr->has_more_work)
				break;

			__bnge_poll_cqs_done(bn, bnapi, DBR_TYPE_CQ_ARMALL,
					     budget);
			nqr->nq_raw_cons = raw_cons;
			if (napi_complete_done(napi, work_done))
				BNGE_DB_NQ_ARM(bd, &nqr->nq_db,
					       nqr->nq_raw_cons);
			goto poll_done;
		}

		/* The valid test of the entry must be done first before
		 * reading any further.
		 */
		dma_rmb();

		type = le16_to_cpu(nqcmp->type);
		if (NQE_CN_TYPE(type) == NQ_CN_TYPE_CQ_NOTIFICATION) {
			u32 idx = le32_to_cpu(nqcmp->cq_handle_low);
			u32 cq_type = BNGE_NQ_HDL_TYPE(idx);
			struct bnge_cp_ring_info *cpr;

			/* No more budget for RX work */
			if (budget && work_done >= budget &&
			    cq_type == BNGE_NQ_HDL_TYPE_RX)
				break;

			idx = BNGE_NQ_HDL_IDX(idx);
			cpr = &nqr->cp_ring_arr[idx];
			cpr->had_nqe_notify = 1;
			cpr->toggle = NQE_CN_TOGGLE(type);
			work_done += __bnge_poll_work(bn, cpr,
						      budget - work_done);
			nqr->has_more_work |= cpr->has_more_work;
		} else {
			bnge_hwrm_handler(bn->bd, (struct tx_cmp *)nqcmp);
		}
		raw_cons = NEXT_RAW_CMP(raw_cons);
	}

	__bnge_poll_cqs_done(bn, bnapi, DBR_TYPE_CQ, budget);
	if (raw_cons != nqr->nq_raw_cons) {
		nqr->nq_raw_cons = raw_cons;
		BNGE_DB_NQ(bd, &nqr->nq_db, raw_cons);
	}
poll_done:
	return work_done;
}

static u16 bnge_xmit_get_cfa_action(struct sk_buff *skb)
{
	struct metadata_dst *md_dst = skb_metadata_dst(skb);

	if (!md_dst || md_dst->type != METADATA_HW_PORT_MUX)
		return 0;

	return md_dst->u.port_info.port_id;
}

static const u16 bnge_lhint_arr[] = {
	TX_BD_FLAGS_LHINT_512_AND_SMALLER,
	TX_BD_FLAGS_LHINT_512_TO_1023,
	TX_BD_FLAGS_LHINT_1024_TO_2047,
	TX_BD_FLAGS_LHINT_1024_TO_2047,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
	TX_BD_FLAGS_LHINT_2048_AND_LARGER,
};

static void bnge_txr_db_kick(struct bnge_net *bn, struct bnge_tx_ring_info *txr,
			     u16 prod)
{
	/* Sync BD data before updating doorbell */
	wmb();
	bnge_db_write(bn->bd, &txr->tx_db, prod);
	txr->kick_pending = 0;
}

static u32 bnge_get_gso_hdr_len(struct sk_buff *skb)
{
	bool udp_gso = !!(skb_shinfo(skb)->gso_type & SKB_GSO_UDP_L4);
	u32 hdr_len;

	if (skb->encapsulation) {
		if (udp_gso)
			hdr_len = skb_inner_transport_offset(skb) +
				  sizeof(struct udphdr);
		else
			hdr_len = skb_inner_tcp_all_headers(skb);
	} else if (udp_gso) {
		hdr_len = skb_transport_offset(skb) + sizeof(struct udphdr);
	} else {
		hdr_len = skb_tcp_all_headers(skb);
	}

	return hdr_len;
}

netdev_tx_t bnge_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	u32 len, free_size, vlan_tag_flags, cfa_action, flags;
	struct bnge_net *bn = netdev_priv(dev);
	struct bnge_tx_ring_info *txr;
	struct bnge_dev *bd = bn->bd;
	struct bnge_sw_tx_bd *tx_buf;
	struct tx_bd *txbd, *txbd0;
	struct netdev_queue *txq;
	struct tx_bd_ext *txbd1;
	u16 prod, last_frag;
	unsigned int length;
	dma_addr_t mapping;
	__le32 lflags = 0;
	skb_frag_t *frag;
	int i;

	i = skb_get_queue_mapping(skb);
	txq = netdev_get_tx_queue(dev, i);
	txr = &bn->tx_ring[bn->tx_ring_map[i]];
	prod = txr->tx_prod;

	free_size = bnge_tx_avail(bn, txr);
	if (unlikely(free_size < skb_shinfo(skb)->nr_frags + 2)) {
		/* We must have raced with NAPI cleanup */
		if (net_ratelimit() && txr->kick_pending)
			netif_warn(bn, tx_err, dev,
				   "bnge: ring busy w/ flush pending!\n");
		if (!netif_txq_try_stop(txq, bnge_tx_avail(bn, txr),
					bn->tx_wake_thresh))
			return NETDEV_TX_BUSY;
	}

	last_frag = skb_shinfo(skb)->nr_frags;

	txbd = &txr->tx_desc_ring[TX_RING(bn, prod)][TX_IDX(prod)];

	tx_buf = &txr->tx_buf_ring[SW_TX_RING(bn, prod)];
	tx_buf->skb = skb;
	tx_buf->nr_frags = last_frag;

	vlan_tag_flags = 0;
	cfa_action = bnge_xmit_get_cfa_action(skb);
	if (skb_vlan_tag_present(skb)) {
		vlan_tag_flags = TX_BD_CFA_META_KEY_VLAN |
				 skb_vlan_tag_get(skb);
		/* Currently supports 8021Q, 8021AD vlan offloads
		 * QINQ1, QINQ2, QINQ3 vlan headers are deprecated
		 */
		if (skb->vlan_proto == htons(ETH_P_8021Q))
			vlan_tag_flags |= 1 << TX_BD_CFA_META_TPID_SHIFT;
	}

	if (unlikely(skb->no_fcs))
		lflags |= cpu_to_le32(TX_BD_FLAGS_NO_CRC);

	if (eth_skb_pad(skb))
		goto tx_kick_pending;

	len = skb_headlen(skb);

	mapping = dma_map_single(bd->dev, skb->data, len, DMA_TO_DEVICE);

	if (unlikely(dma_mapping_error(bd->dev, mapping)))
		goto tx_free;

	dma_unmap_addr_set(tx_buf, mapping, mapping);
	flags = (len << TX_BD_LEN_SHIFT) | TX_BD_TYPE_LONG_TX_BD |
		TX_BD_CNT(last_frag + 2);

	txbd->tx_bd_haddr = cpu_to_le64(mapping);
	txbd->tx_bd_opaque = SET_TX_OPAQUE(bn, txr, prod, 2 + last_frag);

	prod = NEXT_TX(prod);
	txbd1 = (struct tx_bd_ext *)
		&txr->tx_desc_ring[TX_RING(bn, prod)][TX_IDX(prod)];

	if (skb_is_gso(skb)) {
		u32 hdr_len = bnge_get_gso_hdr_len(skb);

		lflags |= cpu_to_le32(TX_BD_FLAGS_LSO | TX_BD_FLAGS_T_IPID |
				      (hdr_len << (TX_BD_HSIZE_SHIFT - 1)));
		length = skb_shinfo(skb)->gso_size;
		txbd1->tx_bd_mss = cpu_to_le32(length);
		length += hdr_len;
	} else {
		length = skb->len;
		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			lflags |= cpu_to_le32(TX_BD_FLAGS_TCP_UDP_CHKSUM);
			txbd1->tx_bd_mss = 0;
		}
	}

	flags |= bnge_lhint_arr[length >> 9];

	txbd->tx_bd_len_flags_type = cpu_to_le32(flags);
	txbd1->tx_bd_hsize_lflags = lflags;
	txbd1->tx_bd_cfa_meta = cpu_to_le32(vlan_tag_flags);
	txbd1->tx_bd_cfa_action =
			cpu_to_le32(cfa_action << TX_BD_CFA_ACTION_SHIFT);
	txbd0 = txbd;
	for (i = 0; i < last_frag; i++) {
		frag = &skb_shinfo(skb)->frags[i];

		prod = NEXT_TX(prod);
		txbd = &txr->tx_desc_ring[TX_RING(bn, prod)][TX_IDX(prod)];

		len = skb_frag_size(frag);
		mapping = skb_frag_dma_map(bd->dev, frag, 0, len,
					   DMA_TO_DEVICE);

		if (unlikely(dma_mapping_error(bd->dev, mapping)))
			goto tx_dma_error;

		tx_buf = &txr->tx_buf_ring[SW_TX_RING(bn, prod)];
		netmem_dma_unmap_addr_set(skb_frag_netmem(frag), tx_buf,
					  mapping, mapping);

		txbd->tx_bd_haddr = cpu_to_le64(mapping);

		flags = len << TX_BD_LEN_SHIFT;
		txbd->tx_bd_len_flags_type = cpu_to_le32(flags);
	}

	flags &= ~TX_BD_LEN;
	txbd->tx_bd_len_flags_type =
		cpu_to_le32(((len) << TX_BD_LEN_SHIFT) | flags |
			    TX_BD_FLAGS_PACKET_END);

	netdev_tx_sent_queue(txq, skb->len);

	prod = NEXT_TX(prod);
	WRITE_ONCE(txr->tx_prod, prod);

	if (!netdev_xmit_more() || netif_xmit_stopped(txq)) {
		bnge_txr_db_kick(bn, txr, prod);
	} else {
		if (free_size >= bn->tx_wake_thresh)
			txbd0->tx_bd_len_flags_type |=
				cpu_to_le32(TX_BD_FLAGS_NO_CMPL);
		txr->kick_pending = 1;
	}

	if (unlikely(bnge_tx_avail(bn, txr) <= MAX_SKB_FRAGS + 1)) {
		if (netdev_xmit_more()) {
			txbd0->tx_bd_len_flags_type &=
				cpu_to_le32(~TX_BD_FLAGS_NO_CMPL);
			bnge_txr_db_kick(bn, txr, prod);
		}

		netif_txq_try_stop(txq, bnge_tx_avail(bn, txr),
				   bn->tx_wake_thresh);
	}
	return NETDEV_TX_OK;

tx_dma_error:
	last_frag = i;

	/* start back at beginning and unmap skb */
	prod = txr->tx_prod;
	tx_buf = &txr->tx_buf_ring[SW_TX_RING(bn, prod)];
	dma_unmap_single(bd->dev, dma_unmap_addr(tx_buf, mapping),
			 skb_headlen(skb), DMA_TO_DEVICE);
	prod = NEXT_TX(prod);

	/* unmap remaining mapped pages */
	for (i = 0; i < last_frag; i++) {
		prod = NEXT_TX(prod);
		tx_buf = &txr->tx_buf_ring[SW_TX_RING(bn, prod)];
		frag = &skb_shinfo(skb)->frags[i];
		netmem_dma_unmap_page_attrs(bd->dev,
					    dma_unmap_addr(tx_buf, mapping),
					    skb_frag_size(frag),
					    DMA_TO_DEVICE, 0);
	}

tx_free:
	dev_kfree_skb_any(skb);

tx_kick_pending:
	if (txr->kick_pending)
		bnge_txr_db_kick(bn, txr, txr->tx_prod);
	txr->tx_buf_ring[SW_TX_RING(bn, txr->tx_prod)].skb = NULL;
	dev_core_stats_tx_dropped_inc(dev);
	return NETDEV_TX_OK;
}

netdev_features_t bnge_features_check(struct sk_buff *skb,
				      struct net_device *dev,
				      netdev_features_t features)
{
	u32 len;

	features = vlan_features_check(skb, features);
#if (MAX_SKB_FRAGS > TX_MAX_FRAGS)
	if (skb_shinfo(skb)->nr_frags > TX_MAX_FRAGS)
		features &= ~NETIF_F_SG;
#endif

	if (skb_is_gso(skb))
		len = bnge_get_gso_hdr_len(skb) + skb_shinfo(skb)->gso_size;
	else
		len = skb->len;

	len >>= 9;
	if (unlikely(len >= ARRAY_SIZE(bnge_lhint_arr)))
		features &= ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);

	return features;
}
