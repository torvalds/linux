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

static void enic_rq_cq_desc_dec(void *cq_desc, u8 cq_desc_size, u8 *type,
				u8 *color, u16 *q_number, u16 *completed_index)
{
	/* type_color is the last field for all cq structs */
	u8 type_color;

	switch (cq_desc_size) {
	case VNIC_RQ_CQ_ENTRY_SIZE_16: {
		struct cq_enet_rq_desc *desc =
			(struct cq_enet_rq_desc *)cq_desc;
		type_color = desc->type_color;

		/* Make sure color bit is read from desc *before* other fields
		 * are read from desc.  Hardware guarantees color bit is last
		 * bit (byte) written.  Adding the rmb() prevents the compiler
		 * and/or CPU from reordering the reads which would potentially
		 * result in reading stale values.
		 */
		rmb();

		*q_number = le16_to_cpu(desc->q_number_rss_type_flags) &
			    CQ_DESC_Q_NUM_MASK;
		*completed_index = le16_to_cpu(desc->completed_index_flags) &
				   CQ_DESC_COMP_NDX_MASK;
		break;
	}
	case VNIC_RQ_CQ_ENTRY_SIZE_32: {
		struct cq_enet_rq_desc_32 *desc =
			(struct cq_enet_rq_desc_32 *)cq_desc;
		type_color = desc->type_color;

		/* Make sure color bit is read from desc *before* other fields
		 * are read from desc.  Hardware guarantees color bit is last
		 * bit (byte) written.  Adding the rmb() prevents the compiler
		 * and/or CPU from reordering the reads which would potentially
		 * result in reading stale values.
		 */
		rmb();

		*q_number = le16_to_cpu(desc->q_number_rss_type_flags) &
			    CQ_DESC_Q_NUM_MASK;
		*completed_index = le16_to_cpu(desc->completed_index_flags) &
				   CQ_DESC_COMP_NDX_MASK;
		*completed_index |= (desc->fetch_index_flags & CQ_DESC_32_FI_MASK) <<
				CQ_DESC_COMP_NDX_BITS;
		break;
	}
	case VNIC_RQ_CQ_ENTRY_SIZE_64: {
		struct cq_enet_rq_desc_64 *desc =
			(struct cq_enet_rq_desc_64 *)cq_desc;
		type_color = desc->type_color;

		/* Make sure color bit is read from desc *before* other fields
		 * are read from desc.  Hardware guarantees color bit is last
		 * bit (byte) written.  Adding the rmb() prevents the compiler
		 * and/or CPU from reordering the reads which would potentially
		 * result in reading stale values.
		 */
		rmb();

		*q_number = le16_to_cpu(desc->q_number_rss_type_flags) &
			    CQ_DESC_Q_NUM_MASK;
		*completed_index = le16_to_cpu(desc->completed_index_flags) &
				   CQ_DESC_COMP_NDX_MASK;
		*completed_index |= (desc->fetch_index_flags & CQ_DESC_64_FI_MASK) <<
				CQ_DESC_COMP_NDX_BITS;
		break;
	}
	}

	*color = (type_color >> CQ_DESC_COLOR_SHIFT) & CQ_DESC_COLOR_MASK;
	*type = type_color & CQ_DESC_TYPE_MASK;
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

/*
 * cq_enet_rq_desc accesses section uses only the 1st 15 bytes of the cq which
 * is identical for all type (16,32 and 64 byte) of cqs.
 */
static void cq_enet_rq_desc_dec(struct cq_enet_rq_desc *desc, u8 *ingress_port,
				u8 *fcoe, u8 *eop, u8 *sop, u8 *rss_type,
				u8 *csum_not_calc, u32 *rss_hash,
				u16 *bytes_written, u8 *packet_error,
				u8 *vlan_stripped, u16 *vlan_tci,
				u16 *checksum, u8 *fcoe_sof,
				u8 *fcoe_fc_crc_ok, u8 *fcoe_enc_error,
				u8 *fcoe_eof, u8 *tcp_udp_csum_ok, u8 *udp,
				u8 *tcp, u8 *ipv4_csum_ok, u8 *ipv6, u8 *ipv4,
				u8 *ipv4_fragment, u8 *fcs_ok)
{
	u16 completed_index_flags;
	u16 q_number_rss_type_flags;
	u16 bytes_written_flags;

	completed_index_flags = le16_to_cpu(desc->completed_index_flags);
	q_number_rss_type_flags =
		le16_to_cpu(desc->q_number_rss_type_flags);
	bytes_written_flags = le16_to_cpu(desc->bytes_written_flags);

	*ingress_port = (completed_index_flags &
		CQ_ENET_RQ_DESC_FLAGS_INGRESS_PORT) ? 1 : 0;
	*fcoe = (completed_index_flags & CQ_ENET_RQ_DESC_FLAGS_FCOE) ?
		1 : 0;
	*eop = (completed_index_flags & CQ_ENET_RQ_DESC_FLAGS_EOP) ?
		1 : 0;
	*sop = (completed_index_flags & CQ_ENET_RQ_DESC_FLAGS_SOP) ?
		1 : 0;

	*rss_type = (u8)((q_number_rss_type_flags >> CQ_DESC_Q_NUM_BITS) &
		CQ_ENET_RQ_DESC_RSS_TYPE_MASK);
	*csum_not_calc = (q_number_rss_type_flags &
		CQ_ENET_RQ_DESC_FLAGS_CSUM_NOT_CALC) ? 1 : 0;

	*rss_hash = le32_to_cpu(desc->rss_hash);

	*bytes_written = bytes_written_flags &
		CQ_ENET_RQ_DESC_BYTES_WRITTEN_MASK;
	*packet_error = (bytes_written_flags &
		CQ_ENET_RQ_DESC_FLAGS_TRUNCATED) ? 1 : 0;
	*vlan_stripped = (bytes_written_flags &
		CQ_ENET_RQ_DESC_FLAGS_VLAN_STRIPPED) ? 1 : 0;

	/*
	 * Tag Control Information(16) = user_priority(3) + cfi(1) + vlan(12)
	 */
	*vlan_tci = le16_to_cpu(desc->vlan);

	if (*fcoe) {
		*fcoe_sof = (u8)(le16_to_cpu(desc->checksum_fcoe) &
			CQ_ENET_RQ_DESC_FCOE_SOF_MASK);
		*fcoe_fc_crc_ok = (desc->flags &
			CQ_ENET_RQ_DESC_FCOE_FC_CRC_OK) ? 1 : 0;
		*fcoe_enc_error = (desc->flags &
			CQ_ENET_RQ_DESC_FCOE_ENC_ERROR) ? 1 : 0;
		*fcoe_eof = (u8)((le16_to_cpu(desc->checksum_fcoe) >>
			CQ_ENET_RQ_DESC_FCOE_EOF_SHIFT) &
			CQ_ENET_RQ_DESC_FCOE_EOF_MASK);
		*checksum = 0;
	} else {
		*fcoe_sof = 0;
		*fcoe_fc_crc_ok = 0;
		*fcoe_enc_error = 0;
		*fcoe_eof = 0;
		*checksum = le16_to_cpu(desc->checksum_fcoe);
	}

	*tcp_udp_csum_ok =
		(desc->flags & CQ_ENET_RQ_DESC_FLAGS_TCP_UDP_CSUM_OK) ? 1 : 0;
	*udp = (desc->flags & CQ_ENET_RQ_DESC_FLAGS_UDP) ? 1 : 0;
	*tcp = (desc->flags & CQ_ENET_RQ_DESC_FLAGS_TCP) ? 1 : 0;
	*ipv4_csum_ok =
		(desc->flags & CQ_ENET_RQ_DESC_FLAGS_IPV4_CSUM_OK) ? 1 : 0;
	*ipv6 = (desc->flags & CQ_ENET_RQ_DESC_FLAGS_IPV6) ? 1 : 0;
	*ipv4 = (desc->flags & CQ_ENET_RQ_DESC_FLAGS_IPV4) ? 1 : 0;
	*ipv4_fragment =
		(desc->flags & CQ_ENET_RQ_DESC_FLAGS_IPV4_FRAGMENT) ? 1 : 0;
	*fcs_ok = (desc->flags & CQ_ENET_RQ_DESC_FLAGS_FCS_OK) ? 1 : 0;
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

static void enic_rq_indicate_buf(struct enic *enic, struct vnic_rq *rq,
				 struct vnic_rq_buf *buf, void *cq_desc,
				 u8 type, u16 q_number, u16 completed_index)
{
	struct sk_buff *skb;
	struct vnic_cq *cq = &enic->cq[enic_cq_rq(enic, rq->index)];
	struct enic_rq_stats *rqstats = &enic->rq[rq->index].stats;
	struct napi_struct *napi;

	u8 eop, sop, ingress_port, vlan_stripped;
	u8 fcoe, fcoe_sof, fcoe_fc_crc_ok, fcoe_enc_error, fcoe_eof;
	u8 tcp_udp_csum_ok, udp, tcp, ipv4_csum_ok;
	u8 ipv6, ipv4, ipv4_fragment, fcs_ok, rss_type, csum_not_calc;
	u8 packet_error;
	u16 bytes_written, vlan_tci, checksum;
	u32 rss_hash;

	rqstats->packets++;

	cq_enet_rq_desc_dec((struct cq_enet_rq_desc *)cq_desc, &ingress_port,
			    &fcoe, &eop, &sop, &rss_type, &csum_not_calc,
			    &rss_hash, &bytes_written, &packet_error,
			    &vlan_stripped, &vlan_tci, &checksum, &fcoe_sof,
			    &fcoe_fc_crc_ok, &fcoe_enc_error, &fcoe_eof,
			    &tcp_udp_csum_ok, &udp, &tcp, &ipv4_csum_ok, &ipv6,
			    &ipv4, &ipv4_fragment, &fcs_ok);

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

static void enic_rq_service(struct enic *enic, void *cq_desc, u8 type,
			    u16 q_number, u16 completed_index)
{
	struct enic_rq_stats *rqstats = &enic->rq[q_number].stats;
	struct vnic_rq *vrq = &enic->rq[q_number].vrq;
	struct vnic_rq_buf *vrq_buf = vrq->to_clean;
	int skipped;

	while (1) {
		skipped = (vrq_buf->index != completed_index);
		if (!skipped)
			enic_rq_indicate_buf(enic, vrq, vrq_buf, cq_desc, type,
					     q_number, completed_index);
		else
			rqstats->desc_skip++;

		vrq->ring.desc_avail++;
		vrq->to_clean = vrq_buf->next;
		vrq_buf = vrq_buf->next;
		if (!skipped)
			break;
	}
}

unsigned int enic_rq_cq_service(struct enic *enic, unsigned int cq_index,
				unsigned int work_to_do)
{
	struct vnic_cq *cq = &enic->cq[cq_index];
	void *cq_desc = vnic_cq_to_clean(cq);
	u16 q_number, completed_index;
	unsigned int work_done = 0;
	u8 type, color;

	enic_rq_cq_desc_dec(cq_desc, enic->ext_cq, &type, &color, &q_number,
			    &completed_index);

	while (color != cq->last_color) {
		enic_rq_service(enic, cq_desc, type, q_number, completed_index);
		vnic_cq_inc_to_clean(cq);

		if (++work_done >= work_to_do)
			break;

		cq_desc = vnic_cq_to_clean(cq);
		enic_rq_cq_desc_dec(cq_desc, enic->ext_cq, &type, &color,
				    &q_number, &completed_index);
	}

	return work_done;
}
