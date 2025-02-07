// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2024 Cisco Systems, Inc.  All rights reserved.

#include <linux/skbuff.h>
#include <linux/if_vlan.h>
#include <net/busy_poll.h>
#include "enic.h"
#include "enic_res.h"
#include "enic_rq.h"
#include "vnic_rq.h"
#include "cq_enet_desc.h"

#define ENIC_LARGE_PKT_THRESHOLD                1000

static void enic_intr_update_pkt_size(struct vnic_rx_bytes_counter *pkt_size,
				      u32 pkt_len)
{
	if (pkt_len > ENIC_LARGE_PKT_THRESHOLD)
		pkt_size->large_pkt_bytes_cnt += pkt_len;
	else
		pkt_size->small_pkt_bytes_cnt += pkt_len;
}

int enic_rq_service(struct vnic_dev *vdev, struct cq_desc *cq_desc, u8 type,
		    u16 q_number, u16 completed_index, void *opaque)
{
	struct enic *enic = vnic_dev_priv(vdev);

	vnic_rq_service(&enic->rq[q_number].vrq, cq_desc, completed_index,
			VNIC_RQ_RETURN_DESC, enic_rq_indicate_buf, opaque);
	return 0;
}

static void enic_rq_set_skb_flags(struct vnic_rq *vrq, u8 type, u32 rss_hash,
				  u8 rss_type, u8 fcoe, u8 fcoe_fc_crc_ok,
				  u8 vlan_stripped, u8 csum_not_calc,
				  u8 tcp_udp_csum_ok, u8 ipv6, u8 ipv4_csum_ok,
				  u16 vlan_tci, struct sk_buff *skb)
{
	struct enic *enic = vnic_dev_priv(vrq->vdev);
	struct net_device *netdev = enic->netdev;
	struct enic_rq_stats *rqstats =  &enic->rq[vrq->index].stats;
	bool outer_csum_ok = true, encap = false;

	if ((netdev->features & NETIF_F_RXHASH) && rss_hash && type == 3) {
		switch (rss_type) {
		case CQ_ENET_RQ_DESC_RSS_TYPE_TCP_IPv4:
		case CQ_ENET_RQ_DESC_RSS_TYPE_TCP_IPv6:
		case CQ_ENET_RQ_DESC_RSS_TYPE_TCP_IPv6_EX:
			skb_set_hash(skb, rss_hash, PKT_HASH_TYPE_L4);
			rqstats->l4_rss_hash++;
			break;
		case CQ_ENET_RQ_DESC_RSS_TYPE_IPv4:
		case CQ_ENET_RQ_DESC_RSS_TYPE_IPv6:
		case CQ_ENET_RQ_DESC_RSS_TYPE_IPv6_EX:
			skb_set_hash(skb, rss_hash, PKT_HASH_TYPE_L3);
			rqstats->l3_rss_hash++;
			break;
		}
	}
	if (enic->vxlan.vxlan_udp_port_number) {
		switch (enic->vxlan.patch_level) {
		case 0:
			if (fcoe) {
				encap = true;
				outer_csum_ok = fcoe_fc_crc_ok;
			}
			break;
		case 2:
			if (type == 7 && (rss_hash & BIT(0))) {
				encap = true;
				outer_csum_ok = (rss_hash & BIT(1)) &&
						(rss_hash & BIT(2));
			}
			break;
		}
	}

	/* Hardware does not provide whole packet checksum. It only
	 * provides pseudo checksum. Since hw validates the packet
	 * checksum but not provide us the checksum value. use
	 * CHECSUM_UNNECESSARY.
	 *
	 * In case of encap pkt tcp_udp_csum_ok/tcp_udp_csum_ok is
	 * inner csum_ok. outer_csum_ok is set by hw when outer udp
	 * csum is correct or is zero.
	 */
	if ((netdev->features & NETIF_F_RXCSUM) && !csum_not_calc &&
	    tcp_udp_csum_ok && outer_csum_ok && (ipv4_csum_ok || ipv6)) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		skb->csum_level = encap;
		if (encap)
			rqstats->csum_unnecessary_encap++;
		else
			rqstats->csum_unnecessary++;
	}

	if (vlan_stripped) {
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vlan_tci);
		rqstats->vlan_stripped++;
	}
}

static bool enic_rq_pkt_error(struct vnic_rq *vrq, u8 packet_error, u8 fcs_ok,
			      u16 bytes_written)
{
	struct enic *enic = vnic_dev_priv(vrq->vdev);
	struct enic_rq_stats *rqstats = &enic->rq[vrq->index].stats;

	if (packet_error) {
		if (!fcs_ok) {
			if (bytes_written > 0)
				rqstats->bad_fcs++;
			else if (bytes_written == 0)
				rqstats->pkt_truncated++;
		}
		return true;
	}
	return false;
}

int enic_rq_alloc_buf(struct vnic_rq *rq)
{
	struct enic *enic = vnic_dev_priv(rq->vdev);
	struct net_device *netdev = enic->netdev;
	struct enic_rq *erq = &enic->rq[rq->index];
	struct enic_rq_stats *rqstats = &erq->stats;
	unsigned int offset = 0;
	unsigned int len = netdev->mtu + VLAN_ETH_HLEN;
	unsigned int os_buf_index = 0;
	dma_addr_t dma_addr;
	struct vnic_rq_buf *buf = rq->to_use;
	struct page *page;
	unsigned int truesize = len;

	if (buf->os_buf) {
		enic_queue_rq_desc(rq, buf->os_buf, os_buf_index, buf->dma_addr,
				   buf->len);

		return 0;
	}

	page = page_pool_dev_alloc(erq->pool, &offset, &truesize);
	if (unlikely(!page)) {
		rqstats->pp_alloc_fail++;
		return -ENOMEM;
	}
	buf->offset = offset;
	buf->truesize = truesize;
	dma_addr = page_pool_get_dma_addr(page) + offset;
	enic_queue_rq_desc(rq, (void *)page, os_buf_index, dma_addr, len);

	return 0;
}

void enic_free_rq_buf(struct vnic_rq *rq, struct vnic_rq_buf *buf)
{
	struct enic *enic = vnic_dev_priv(rq->vdev);
	struct enic_rq *erq = &enic->rq[rq->index];

	if (!buf->os_buf)
		return;

	page_pool_put_full_page(erq->pool, (struct page *)buf->os_buf, true);
	buf->os_buf = NULL;
}

void enic_rq_indicate_buf(struct vnic_rq *rq, struct cq_desc *cq_desc,
			  struct vnic_rq_buf *buf, int skipped, void *opaque)
{
	struct enic *enic = vnic_dev_priv(rq->vdev);
	struct sk_buff *skb;
	struct vnic_cq *cq = &enic->cq[enic_cq_rq(enic, rq->index)];
	struct enic_rq_stats *rqstats = &enic->rq[rq->index].stats;
	struct napi_struct *napi;

	u8 type, color, eop, sop, ingress_port, vlan_stripped;
	u8 fcoe, fcoe_sof, fcoe_fc_crc_ok, fcoe_enc_error, fcoe_eof;
	u8 tcp_udp_csum_ok, udp, tcp, ipv4_csum_ok;
	u8 ipv6, ipv4, ipv4_fragment, fcs_ok, rss_type, csum_not_calc;
	u8 packet_error;
	u16 q_number, completed_index, bytes_written, vlan_tci, checksum;
	u32 rss_hash;

	rqstats->packets++;
	if (skipped) {
		rqstats->desc_skip++;
		return;
	}

	cq_enet_rq_desc_dec((struct cq_enet_rq_desc *)cq_desc, &type, &color,
			    &q_number, &completed_index, &ingress_port, &fcoe,
			    &eop, &sop, &rss_type, &csum_not_calc, &rss_hash,
			    &bytes_written, &packet_error, &vlan_stripped,
			    &vlan_tci, &checksum, &fcoe_sof, &fcoe_fc_crc_ok,
			    &fcoe_enc_error, &fcoe_eof, &tcp_udp_csum_ok, &udp,
			    &tcp, &ipv4_csum_ok, &ipv6, &ipv4, &ipv4_fragment,
			    &fcs_ok);

	if (enic_rq_pkt_error(rq, packet_error, fcs_ok, bytes_written))
		return;

	if (eop && bytes_written > 0) {
		/* Good receive
		 */
		rqstats->bytes += bytes_written;
		napi = &enic->napi[rq->index];
		skb = napi_get_frags(napi);
		if (unlikely(!skb)) {
			net_warn_ratelimited("%s: skb alloc error rq[%d], desc[%d]\n",
					     enic->netdev->name, rq->index,
					     completed_index);
			rqstats->no_skb++;
			return;
		}

		prefetch(skb->data - NET_IP_ALIGN);

		dma_sync_single_for_cpu(&enic->pdev->dev, buf->dma_addr,
					bytes_written, DMA_FROM_DEVICE);
		skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
				(struct page *)buf->os_buf, buf->offset,
				bytes_written, buf->truesize);
		skb_record_rx_queue(skb, q_number);
		enic_rq_set_skb_flags(rq, type, rss_hash, rss_type, fcoe,
				      fcoe_fc_crc_ok, vlan_stripped,
				      csum_not_calc, tcp_udp_csum_ok, ipv6,
				      ipv4_csum_ok, vlan_tci, skb);
		skb_mark_for_recycle(skb);
		napi_gro_frags(napi);
		if (enic->rx_coalesce_setting.use_adaptive_rx_coalesce)
			enic_intr_update_pkt_size(&cq->pkt_size_counter,
						  bytes_written);
		buf->os_buf = NULL;
		buf->dma_addr = 0;
		buf = buf->next;
	} else {
		/* Buffer overflow
		 */
		rqstats->pkt_truncated++;
	}
}
