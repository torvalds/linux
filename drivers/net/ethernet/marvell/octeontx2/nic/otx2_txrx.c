// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Ethernet driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#include <linux/etherdevice.h>
#include <net/ip.h>
#include <net/tso.h>
#include <linux/bpf.h>
#include <linux/bpf_trace.h>

#include "otx2_reg.h"
#include "otx2_common.h"
#include "otx2_struct.h"
#include "otx2_txrx.h"
#include "otx2_ptp.h"
#include "cn10k.h"

#define CQE_ADDR(CQ, idx) ((CQ)->cqe_base + ((CQ)->cqe_size * (idx)))
static bool otx2_xdp_rcv_pkt_handler(struct otx2_nic *pfvf,
				     struct bpf_prog *prog,
				     struct nix_cqe_rx_s *cqe,
				     struct otx2_cq_queue *cq);

static int otx2_nix_cq_op_status(struct otx2_nic *pfvf,
				 struct otx2_cq_queue *cq)
{
	u64 incr = (u64)(cq->cq_idx) << 32;
	u64 status;

	status = otx2_atomic64_fetch_add(incr, pfvf->cq_op_addr);

	if (unlikely(status & BIT_ULL(CQ_OP_STAT_OP_ERR) ||
		     status & BIT_ULL(CQ_OP_STAT_CQ_ERR))) {
		dev_err(pfvf->dev, "CQ stopped due to error");
		return -EINVAL;
	}

	cq->cq_tail = status & 0xFFFFF;
	cq->cq_head = (status >> 20) & 0xFFFFF;
	if (cq->cq_tail < cq->cq_head)
		cq->pend_cqe = (cq->cqe_cnt - cq->cq_head) +
				cq->cq_tail;
	else
		cq->pend_cqe = cq->cq_tail - cq->cq_head;

	return 0;
}

static struct nix_cqe_hdr_s *otx2_get_next_cqe(struct otx2_cq_queue *cq)
{
	struct nix_cqe_hdr_s *cqe_hdr;

	cqe_hdr = (struct nix_cqe_hdr_s *)CQE_ADDR(cq, cq->cq_head);
	if (cqe_hdr->cqe_type == NIX_XQE_TYPE_INVALID)
		return NULL;

	cq->cq_head++;
	cq->cq_head &= (cq->cqe_cnt - 1);

	return cqe_hdr;
}

static unsigned int frag_num(unsigned int i)
{
#ifdef __BIG_ENDIAN
	return (i & ~3) + 3 - (i & 3);
#else
	return i;
#endif
}

static dma_addr_t otx2_dma_map_skb_frag(struct otx2_nic *pfvf,
					struct sk_buff *skb, int seg, int *len)
{
	const skb_frag_t *frag;
	struct page *page;
	int offset;

	/* First segment is always skb->data */
	if (!seg) {
		page = virt_to_page(skb->data);
		offset = offset_in_page(skb->data);
		*len = skb_headlen(skb);
	} else {
		frag = &skb_shinfo(skb)->frags[seg - 1];
		page = skb_frag_page(frag);
		offset = skb_frag_off(frag);
		*len = skb_frag_size(frag);
	}
	return otx2_dma_map_page(pfvf, page, offset, *len, DMA_TO_DEVICE);
}

static void otx2_dma_unmap_skb_frags(struct otx2_nic *pfvf, struct sg_list *sg)
{
	int seg;

	for (seg = 0; seg < sg->num_segs; seg++) {
		otx2_dma_unmap_page(pfvf, sg->dma_addr[seg],
				    sg->size[seg], DMA_TO_DEVICE);
	}
	sg->num_segs = 0;
}

static void otx2_xdp_snd_pkt_handler(struct otx2_nic *pfvf,
				     struct otx2_snd_queue *sq,
				 struct nix_cqe_tx_s *cqe)
{
	struct nix_send_comp_s *snd_comp = &cqe->comp;
	struct sg_list *sg;
	struct page *page;
	u64 pa;

	sg = &sq->sg[snd_comp->sqe_id];

	pa = otx2_iova_to_phys(pfvf->iommu_domain, sg->dma_addr[0]);
	otx2_dma_unmap_page(pfvf, sg->dma_addr[0],
			    sg->size[0], DMA_TO_DEVICE);
	page = virt_to_page(phys_to_virt(pa));
	put_page(page);
}

static void otx2_snd_pkt_handler(struct otx2_nic *pfvf,
				 struct otx2_cq_queue *cq,
				 struct otx2_snd_queue *sq,
				 struct nix_cqe_tx_s *cqe,
				 int budget, int *tx_pkts, int *tx_bytes)
{
	struct nix_send_comp_s *snd_comp = &cqe->comp;
	struct skb_shared_hwtstamps ts;
	struct sk_buff *skb = NULL;
	u64 timestamp, tsns;
	struct sg_list *sg;
	int err;

	if (unlikely(snd_comp->status) && netif_msg_tx_err(pfvf))
		net_err_ratelimited("%s: TX%d: Error in send CQ status:%x\n",
				    pfvf->netdev->name, cq->cint_idx,
				    snd_comp->status);

	sg = &sq->sg[snd_comp->sqe_id];
	skb = (struct sk_buff *)sg->skb;
	if (unlikely(!skb))
		return;

	if (skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS) {
		timestamp = ((u64 *)sq->timestamps->base)[snd_comp->sqe_id];
		if (timestamp != 1) {
			timestamp = pfvf->ptp->convert_tx_ptp_tstmp(timestamp);
			err = otx2_ptp_tstamp2time(pfvf, timestamp, &tsns);
			if (!err) {
				memset(&ts, 0, sizeof(ts));
				ts.hwtstamp = ns_to_ktime(tsns);
				skb_tstamp_tx(skb, &ts);
			}
		}
	}

	*tx_bytes += skb->len;
	(*tx_pkts)++;
	otx2_dma_unmap_skb_frags(pfvf, sg);
	napi_consume_skb(skb, budget);
	sg->skb = (u64)NULL;
}

static void otx2_set_rxtstamp(struct otx2_nic *pfvf,
			      struct sk_buff *skb, void *data)
{
	u64 timestamp, tsns;
	int err;

	if (!(pfvf->flags & OTX2_FLAG_RX_TSTAMP_ENABLED))
		return;

	timestamp = pfvf->ptp->convert_rx_ptp_tstmp(*(u64 *)data);
	/* The first 8 bytes is the timestamp */
	err = otx2_ptp_tstamp2time(pfvf, timestamp, &tsns);
	if (err)
		return;

	skb_hwtstamps(skb)->hwtstamp = ns_to_ktime(tsns);
}

static bool otx2_skb_add_frag(struct otx2_nic *pfvf, struct sk_buff *skb,
			      u64 iova, int len, struct nix_rx_parse_s *parse,
			      int qidx)
{
	struct page *page;
	int off = 0;
	void *va;

	va = phys_to_virt(otx2_iova_to_phys(pfvf->iommu_domain, iova));

	if (likely(!skb_shinfo(skb)->nr_frags)) {
		/* Check if data starts at some nonzero offset
		 * from the start of the buffer.  For now the
		 * only possible offset is 8 bytes in the case
		 * where packet is prepended by a timestamp.
		 */
		if (parse->laptr) {
			otx2_set_rxtstamp(pfvf, skb, va);
			off = OTX2_HW_TIMESTAMP_LEN;
		}
	}

	page = virt_to_page(va);
	if (likely(skb_shinfo(skb)->nr_frags < MAX_SKB_FRAGS)) {
		skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, page,
				va - page_address(page) + off,
				len - off, pfvf->rbsize);

		otx2_dma_unmap_page(pfvf, iova - OTX2_HEAD_ROOM,
				    pfvf->rbsize, DMA_FROM_DEVICE);
		return true;
	}

	/* If more than MAX_SKB_FRAGS fragments are received then
	 * give back those buffer pointers to hardware for reuse.
	 */
	pfvf->hw_ops->aura_freeptr(pfvf, qidx, iova & ~0x07ULL);

	return false;
}

static void otx2_set_rxhash(struct otx2_nic *pfvf,
			    struct nix_cqe_rx_s *cqe, struct sk_buff *skb)
{
	enum pkt_hash_types hash_type = PKT_HASH_TYPE_NONE;
	struct otx2_rss_info *rss;
	u32 hash = 0;

	if (!(pfvf->netdev->features & NETIF_F_RXHASH))
		return;

	rss = &pfvf->hw.rss_info;
	if (rss->flowkey_cfg) {
		if (rss->flowkey_cfg &
		    ~(NIX_FLOW_KEY_TYPE_IPV4 | NIX_FLOW_KEY_TYPE_IPV6))
			hash_type = PKT_HASH_TYPE_L4;
		else
			hash_type = PKT_HASH_TYPE_L3;
		hash = cqe->hdr.flow_tag;
	}
	skb_set_hash(skb, hash, hash_type);
}

static void otx2_free_rcv_seg(struct otx2_nic *pfvf, struct nix_cqe_rx_s *cqe,
			      int qidx)
{
	struct nix_rx_sg_s *sg = &cqe->sg;
	void *end, *start;
	u64 *seg_addr;
	int seg;

	start = (void *)sg;
	end = start + ((cqe->parse.desc_sizem1 + 1) * 16);
	while (start < end) {
		sg = (struct nix_rx_sg_s *)start;
		seg_addr = &sg->seg_addr;
		for (seg = 0; seg < sg->segs; seg++, seg_addr++)
			pfvf->hw_ops->aura_freeptr(pfvf, qidx,
						   *seg_addr & ~0x07ULL);
		start += sizeof(*sg);
	}
}

static bool otx2_check_rcv_errors(struct otx2_nic *pfvf,
				  struct nix_cqe_rx_s *cqe, int qidx)
{
	struct otx2_drv_stats *stats = &pfvf->hw.drv_stats;
	struct nix_rx_parse_s *parse = &cqe->parse;

	if (netif_msg_rx_err(pfvf))
		netdev_err(pfvf->netdev,
			   "RQ%d: Error pkt with errlev:0x%x errcode:0x%x\n",
			   qidx, parse->errlev, parse->errcode);

	if (parse->errlev == NPC_ERRLVL_RE) {
		switch (parse->errcode) {
		case ERRCODE_FCS:
		case ERRCODE_FCS_RCV:
			atomic_inc(&stats->rx_fcs_errs);
			break;
		case ERRCODE_UNDERSIZE:
			atomic_inc(&stats->rx_undersize_errs);
			break;
		case ERRCODE_OVERSIZE:
			atomic_inc(&stats->rx_oversize_errs);
			break;
		case ERRCODE_OL2_LEN_MISMATCH:
			atomic_inc(&stats->rx_len_errs);
			break;
		default:
			atomic_inc(&stats->rx_other_errs);
			break;
		}
	} else if (parse->errlev == NPC_ERRLVL_NIX) {
		switch (parse->errcode) {
		case ERRCODE_OL3_LEN:
		case ERRCODE_OL4_LEN:
		case ERRCODE_IL3_LEN:
		case ERRCODE_IL4_LEN:
			atomic_inc(&stats->rx_len_errs);
			break;
		case ERRCODE_OL4_CSUM:
		case ERRCODE_IL4_CSUM:
			atomic_inc(&stats->rx_csum_errs);
			break;
		default:
			atomic_inc(&stats->rx_other_errs);
			break;
		}
	} else {
		atomic_inc(&stats->rx_other_errs);
		/* For now ignore all the NPC parser errors and
		 * pass the packets to stack.
		 */
		return false;
	}

	/* If RXALL is enabled pass on packets to stack. */
	if (pfvf->netdev->features & NETIF_F_RXALL)
		return false;

	/* Free buffer back to pool */
	if (cqe->sg.segs)
		otx2_free_rcv_seg(pfvf, cqe, qidx);
	return true;
}

static void otx2_rcv_pkt_handler(struct otx2_nic *pfvf,
				 struct napi_struct *napi,
				 struct otx2_cq_queue *cq,
				 struct nix_cqe_rx_s *cqe)
{
	struct nix_rx_parse_s *parse = &cqe->parse;
	struct nix_rx_sg_s *sg = &cqe->sg;
	struct sk_buff *skb = NULL;
	void *end, *start;
	u64 *seg_addr;
	u16 *seg_size;
	int seg;

	if (unlikely(parse->errlev || parse->errcode)) {
		if (otx2_check_rcv_errors(pfvf, cqe, cq->cq_idx))
			return;
	}

	if (pfvf->xdp_prog)
		if (otx2_xdp_rcv_pkt_handler(pfvf, pfvf->xdp_prog, cqe, cq))
			return;

	skb = napi_get_frags(napi);
	if (unlikely(!skb))
		return;

	start = (void *)sg;
	end = start + ((cqe->parse.desc_sizem1 + 1) * 16);
	while (start < end) {
		sg = (struct nix_rx_sg_s *)start;
		seg_addr = &sg->seg_addr;
		seg_size = (void *)sg;
		for (seg = 0; seg < sg->segs; seg++, seg_addr++) {
			if (otx2_skb_add_frag(pfvf, skb, *seg_addr,
					      seg_size[seg], parse, cq->cq_idx))
				cq->pool_ptrs++;
		}
		start += sizeof(*sg);
	}
	otx2_set_rxhash(pfvf, cqe, skb);

	skb_record_rx_queue(skb, cq->cq_idx);
	if (pfvf->netdev->features & NETIF_F_RXCSUM)
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	napi_gro_frags(napi);
}

static int otx2_rx_napi_handler(struct otx2_nic *pfvf,
				struct napi_struct *napi,
				struct otx2_cq_queue *cq, int budget)
{
	struct nix_cqe_rx_s *cqe;
	int processed_cqe = 0;

	if (cq->pend_cqe >= budget)
		goto process_cqe;

	if (otx2_nix_cq_op_status(pfvf, cq) || !cq->pend_cqe)
		return 0;

process_cqe:
	while (likely(processed_cqe < budget) && cq->pend_cqe) {
		cqe = (struct nix_cqe_rx_s *)CQE_ADDR(cq, cq->cq_head);
		if (cqe->hdr.cqe_type == NIX_XQE_TYPE_INVALID ||
		    !cqe->sg.seg_addr) {
			if (!processed_cqe)
				return 0;
			break;
		}
		cq->cq_head++;
		cq->cq_head &= (cq->cqe_cnt - 1);

		otx2_rcv_pkt_handler(pfvf, napi, cq, cqe);

		cqe->hdr.cqe_type = NIX_XQE_TYPE_INVALID;
		cqe->sg.seg_addr = 0x00;
		processed_cqe++;
		cq->pend_cqe--;
	}

	/* Free CQEs to HW */
	otx2_write64(pfvf, NIX_LF_CQ_OP_DOOR,
		     ((u64)cq->cq_idx << 32) | processed_cqe);

	return processed_cqe;
}

void otx2_refill_pool_ptrs(void *dev, struct otx2_cq_queue *cq)
{
	struct otx2_nic *pfvf = dev;
	dma_addr_t bufptr;

	while (cq->pool_ptrs) {
		if (otx2_alloc_buffer(pfvf, cq, &bufptr))
			break;
		otx2_aura_freeptr(pfvf, cq->cq_idx, bufptr + OTX2_HEAD_ROOM);
		cq->pool_ptrs--;
	}
}

static int otx2_tx_napi_handler(struct otx2_nic *pfvf,
				struct otx2_cq_queue *cq, int budget)
{
	int tx_pkts = 0, tx_bytes = 0, qidx;
	struct nix_cqe_tx_s *cqe;
	int processed_cqe = 0;

	if (cq->pend_cqe >= budget)
		goto process_cqe;

	if (otx2_nix_cq_op_status(pfvf, cq) || !cq->pend_cqe)
		return 0;

process_cqe:
	while (likely(processed_cqe < budget) && cq->pend_cqe) {
		cqe = (struct nix_cqe_tx_s *)otx2_get_next_cqe(cq);
		if (unlikely(!cqe)) {
			if (!processed_cqe)
				return 0;
			break;
		}
		if (cq->cq_type == CQ_XDP) {
			qidx = cq->cq_idx - pfvf->hw.rx_queues;
			otx2_xdp_snd_pkt_handler(pfvf, &pfvf->qset.sq[qidx],
						 cqe);
		} else {
			otx2_snd_pkt_handler(pfvf, cq,
					     &pfvf->qset.sq[cq->cint_idx],
					     cqe, budget, &tx_pkts, &tx_bytes);
		}
		cqe->hdr.cqe_type = NIX_XQE_TYPE_INVALID;
		processed_cqe++;
		cq->pend_cqe--;
	}

	/* Free CQEs to HW */
	otx2_write64(pfvf, NIX_LF_CQ_OP_DOOR,
		     ((u64)cq->cq_idx << 32) | processed_cqe);

	if (likely(tx_pkts)) {
		struct netdev_queue *txq;

		txq = netdev_get_tx_queue(pfvf->netdev, cq->cint_idx);
		netdev_tx_completed_queue(txq, tx_pkts, tx_bytes);
		/* Check if queue was stopped earlier due to ring full */
		smp_mb();
		if (netif_tx_queue_stopped(txq) &&
		    netif_carrier_ok(pfvf->netdev))
			netif_tx_wake_queue(txq);
	}
	return 0;
}

int otx2_napi_handler(struct napi_struct *napi, int budget)
{
	struct otx2_cq_queue *rx_cq = NULL;
	struct otx2_cq_poll *cq_poll;
	int workdone = 0, cq_idx, i;
	struct otx2_cq_queue *cq;
	struct otx2_qset *qset;
	struct otx2_nic *pfvf;

	cq_poll = container_of(napi, struct otx2_cq_poll, napi);
	pfvf = (struct otx2_nic *)cq_poll->dev;
	qset = &pfvf->qset;

	for (i = 0; i < CQS_PER_CINT; i++) {
		cq_idx = cq_poll->cq_ids[i];
		if (unlikely(cq_idx == CINT_INVALID_CQ))
			continue;
		cq = &qset->cq[cq_idx];
		if (cq->cq_type == CQ_RX) {
			rx_cq = cq;
			workdone += otx2_rx_napi_handler(pfvf, napi,
							 cq, budget);
		} else {
			workdone += otx2_tx_napi_handler(pfvf, cq, budget);
		}
	}

	if (rx_cq && rx_cq->pool_ptrs)
		pfvf->hw_ops->refill_pool_ptrs(pfvf, rx_cq);
	/* Clear the IRQ */
	otx2_write64(pfvf, NIX_LF_CINTX_INT(cq_poll->cint_idx), BIT_ULL(0));

	if (workdone < budget && napi_complete_done(napi, workdone)) {
		/* If interface is going down, don't re-enable IRQ */
		if (pfvf->flags & OTX2_FLAG_INTF_DOWN)
			return workdone;

		/* Re-enable interrupts */
		otx2_write64(pfvf, NIX_LF_CINTX_ENA_W1S(cq_poll->cint_idx),
			     BIT_ULL(0));
	}
	return workdone;
}

void otx2_sqe_flush(void *dev, struct otx2_snd_queue *sq,
		    int size, int qidx)
{
	u64 status;

	/* Packet data stores should finish before SQE is flushed to HW */
	dma_wmb();

	do {
		memcpy(sq->lmt_addr, sq->sqe_base, size);
		status = otx2_lmt_flush(sq->io_addr);
	} while (status == 0);

	sq->head++;
	sq->head &= (sq->sqe_cnt - 1);
}

#define MAX_SEGS_PER_SG	3
/* Add SQE scatter/gather subdescriptor structure */
static bool otx2_sqe_add_sg(struct otx2_nic *pfvf, struct otx2_snd_queue *sq,
			    struct sk_buff *skb, int num_segs, int *offset)
{
	struct nix_sqe_sg_s *sg = NULL;
	u64 dma_addr, *iova = NULL;
	u16 *sg_lens = NULL;
	int seg, len;

	sq->sg[sq->head].num_segs = 0;

	for (seg = 0; seg < num_segs; seg++) {
		if ((seg % MAX_SEGS_PER_SG) == 0) {
			sg = (struct nix_sqe_sg_s *)(sq->sqe_base + *offset);
			sg->ld_type = NIX_SEND_LDTYPE_LDD;
			sg->subdc = NIX_SUBDC_SG;
			sg->segs = 0;
			sg_lens = (void *)sg;
			iova = (void *)sg + sizeof(*sg);
			/* Next subdc always starts at a 16byte boundary.
			 * So if sg->segs is whether 2 or 3, offset += 16bytes.
			 */
			if ((num_segs - seg) >= (MAX_SEGS_PER_SG - 1))
				*offset += sizeof(*sg) + (3 * sizeof(u64));
			else
				*offset += sizeof(*sg) + sizeof(u64);
		}
		dma_addr = otx2_dma_map_skb_frag(pfvf, skb, seg, &len);
		if (dma_mapping_error(pfvf->dev, dma_addr))
			return false;

		sg_lens[frag_num(seg % MAX_SEGS_PER_SG)] = len;
		sg->segs++;
		*iova++ = dma_addr;

		/* Save DMA mapping info for later unmapping */
		sq->sg[sq->head].dma_addr[seg] = dma_addr;
		sq->sg[sq->head].size[seg] = len;
		sq->sg[sq->head].num_segs++;
	}

	sq->sg[sq->head].skb = (u64)skb;
	return true;
}

/* Add SQE extended header subdescriptor */
static void otx2_sqe_add_ext(struct otx2_nic *pfvf, struct otx2_snd_queue *sq,
			     struct sk_buff *skb, int *offset)
{
	struct nix_sqe_ext_s *ext;

	ext = (struct nix_sqe_ext_s *)(sq->sqe_base + *offset);
	ext->subdc = NIX_SUBDC_EXT;
	if (skb_shinfo(skb)->gso_size) {
		ext->lso = 1;
		ext->lso_sb = skb_transport_offset(skb) + tcp_hdrlen(skb);
		ext->lso_mps = skb_shinfo(skb)->gso_size;

		/* Only TSOv4 and TSOv6 GSO offloads are supported */
		if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV4) {
			ext->lso_format = pfvf->hw.lso_tsov4_idx;

			/* HW adds payload size to 'ip_hdr->tot_len' while
			 * sending TSO segment, hence set payload length
			 * in IP header of the packet to just header length.
			 */
			ip_hdr(skb)->tot_len =
				htons(ext->lso_sb - skb_network_offset(skb));
		} else if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6) {
			ext->lso_format = pfvf->hw.lso_tsov6_idx;

			ipv6_hdr(skb)->payload_len =
				htons(ext->lso_sb - skb_network_offset(skb));
		} else if (skb_shinfo(skb)->gso_type & SKB_GSO_UDP_L4) {
			__be16 l3_proto = vlan_get_protocol(skb);
			struct udphdr *udph = udp_hdr(skb);
			u16 iplen;

			ext->lso_sb = skb_transport_offset(skb) +
					sizeof(struct udphdr);

			/* HW adds payload size to length fields in IP and
			 * UDP headers while segmentation, hence adjust the
			 * lengths to just header sizes.
			 */
			iplen = htons(ext->lso_sb - skb_network_offset(skb));
			if (l3_proto == htons(ETH_P_IP)) {
				ip_hdr(skb)->tot_len = iplen;
				ext->lso_format = pfvf->hw.lso_udpv4_idx;
			} else {
				ipv6_hdr(skb)->payload_len = iplen;
				ext->lso_format = pfvf->hw.lso_udpv6_idx;
			}

			udph->len = htons(sizeof(struct udphdr));
		}
	} else if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
		ext->tstmp = 1;
	}

#define OTX2_VLAN_PTR_OFFSET     (ETH_HLEN - ETH_TLEN)
	if (skb_vlan_tag_present(skb)) {
		if (skb->vlan_proto == htons(ETH_P_8021Q)) {
			ext->vlan1_ins_ena = 1;
			ext->vlan1_ins_ptr = OTX2_VLAN_PTR_OFFSET;
			ext->vlan1_ins_tci = skb_vlan_tag_get(skb);
		} else if (skb->vlan_proto == htons(ETH_P_8021AD)) {
			ext->vlan0_ins_ena = 1;
			ext->vlan0_ins_ptr = OTX2_VLAN_PTR_OFFSET;
			ext->vlan0_ins_tci = skb_vlan_tag_get(skb);
		}
	}

	*offset += sizeof(*ext);
}

static void otx2_sqe_add_mem(struct otx2_snd_queue *sq, int *offset,
			     int alg, u64 iova)
{
	struct nix_sqe_mem_s *mem;

	mem = (struct nix_sqe_mem_s *)(sq->sqe_base + *offset);
	mem->subdc = NIX_SUBDC_MEM;
	mem->alg = alg;
	mem->wmem = 1; /* wait for the memory operation */
	mem->addr = iova;

	*offset += sizeof(*mem);
}

/* Add SQE header subdescriptor structure */
static void otx2_sqe_add_hdr(struct otx2_nic *pfvf, struct otx2_snd_queue *sq,
			     struct nix_sqe_hdr_s *sqe_hdr,
			     struct sk_buff *skb, u16 qidx)
{
	int proto = 0;

	/* Check if SQE was framed before, if yes then no need to
	 * set these constants again and again.
	 */
	if (!sqe_hdr->total) {
		/* Don't free Tx buffers to Aura */
		sqe_hdr->df = 1;
		sqe_hdr->aura = sq->aura_id;
		/* Post a CQE Tx after pkt transmission */
		sqe_hdr->pnc = 1;
		sqe_hdr->sq = qidx;
	}
	sqe_hdr->total = skb->len;
	/* Set SQE identifier which will be used later for freeing SKB */
	sqe_hdr->sqe_id = sq->head;

	/* Offload TCP/UDP checksum to HW */
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		sqe_hdr->ol3ptr = skb_network_offset(skb);
		sqe_hdr->ol4ptr = skb_transport_offset(skb);
		/* get vlan protocol Ethertype */
		if (eth_type_vlan(skb->protocol))
			skb->protocol = vlan_get_protocol(skb);

		if (skb->protocol == htons(ETH_P_IP)) {
			proto = ip_hdr(skb)->protocol;
			/* In case of TSO, HW needs this to be explicitly set.
			 * So set this always, instead of adding a check.
			 */
			sqe_hdr->ol3type = NIX_SENDL3TYPE_IP4_CKSUM;
		} else if (skb->protocol == htons(ETH_P_IPV6)) {
			proto = ipv6_hdr(skb)->nexthdr;
			sqe_hdr->ol3type = NIX_SENDL3TYPE_IP6;
		}

		if (proto == IPPROTO_TCP)
			sqe_hdr->ol4type = NIX_SENDL4TYPE_TCP_CKSUM;
		else if (proto == IPPROTO_UDP)
			sqe_hdr->ol4type = NIX_SENDL4TYPE_UDP_CKSUM;
	}
}

static int otx2_dma_map_tso_skb(struct otx2_nic *pfvf,
				struct otx2_snd_queue *sq,
				struct sk_buff *skb, int sqe, int hdr_len)
{
	int num_segs = skb_shinfo(skb)->nr_frags + 1;
	struct sg_list *sg = &sq->sg[sqe];
	u64 dma_addr;
	int seg, len;

	sg->num_segs = 0;

	/* Get payload length at skb->data */
	len = skb_headlen(skb) - hdr_len;

	for (seg = 0; seg < num_segs; seg++) {
		/* Skip skb->data, if there is no payload */
		if (!seg && !len)
			continue;
		dma_addr = otx2_dma_map_skb_frag(pfvf, skb, seg, &len);
		if (dma_mapping_error(pfvf->dev, dma_addr))
			goto unmap;

		/* Save DMA mapping info for later unmapping */
		sg->dma_addr[sg->num_segs] = dma_addr;
		sg->size[sg->num_segs] = len;
		sg->num_segs++;
	}
	return 0;
unmap:
	otx2_dma_unmap_skb_frags(pfvf, sg);
	return -EINVAL;
}

static u64 otx2_tso_frag_dma_addr(struct otx2_snd_queue *sq,
				  struct sk_buff *skb, int seg,
				  u64 seg_addr, int hdr_len, int sqe)
{
	struct sg_list *sg = &sq->sg[sqe];
	const skb_frag_t *frag;
	int offset;

	if (seg < 0)
		return sg->dma_addr[0] + (seg_addr - (u64)skb->data);

	frag = &skb_shinfo(skb)->frags[seg];
	offset = seg_addr - (u64)skb_frag_address(frag);
	if (skb_headlen(skb) - hdr_len)
		seg++;
	return sg->dma_addr[seg] + offset;
}

static void otx2_sqe_tso_add_sg(struct otx2_snd_queue *sq,
				struct sg_list *list, int *offset)
{
	struct nix_sqe_sg_s *sg = NULL;
	u16 *sg_lens = NULL;
	u64 *iova = NULL;
	int seg;

	/* Add SG descriptors with buffer addresses */
	for (seg = 0; seg < list->num_segs; seg++) {
		if ((seg % MAX_SEGS_PER_SG) == 0) {
			sg = (struct nix_sqe_sg_s *)(sq->sqe_base + *offset);
			sg->ld_type = NIX_SEND_LDTYPE_LDD;
			sg->subdc = NIX_SUBDC_SG;
			sg->segs = 0;
			sg_lens = (void *)sg;
			iova = (void *)sg + sizeof(*sg);
			/* Next subdc always starts at a 16byte boundary.
			 * So if sg->segs is whether 2 or 3, offset += 16bytes.
			 */
			if ((list->num_segs - seg) >= (MAX_SEGS_PER_SG - 1))
				*offset += sizeof(*sg) + (3 * sizeof(u64));
			else
				*offset += sizeof(*sg) + sizeof(u64);
		}
		sg_lens[frag_num(seg % MAX_SEGS_PER_SG)] = list->size[seg];
		*iova++ = list->dma_addr[seg];
		sg->segs++;
	}
}

static void otx2_sq_append_tso(struct otx2_nic *pfvf, struct otx2_snd_queue *sq,
			       struct sk_buff *skb, u16 qidx)
{
	struct netdev_queue *txq = netdev_get_tx_queue(pfvf->netdev, qidx);
	int hdr_len, tcp_data, seg_len, pkt_len, offset;
	struct nix_sqe_hdr_s *sqe_hdr;
	int first_sqe = sq->head;
	struct sg_list list;
	struct tso_t tso;

	hdr_len = tso_start(skb, &tso);

	/* Map SKB's fragments to DMA.
	 * It's done here to avoid mapping for every TSO segment's packet.
	 */
	if (otx2_dma_map_tso_skb(pfvf, sq, skb, first_sqe, hdr_len)) {
		dev_kfree_skb_any(skb);
		return;
	}

	netdev_tx_sent_queue(txq, skb->len);

	tcp_data = skb->len - hdr_len;
	while (tcp_data > 0) {
		char *hdr;

		seg_len = min_t(int, skb_shinfo(skb)->gso_size, tcp_data);
		tcp_data -= seg_len;

		/* Set SQE's SEND_HDR */
		memset(sq->sqe_base, 0, sq->sqe_size);
		sqe_hdr = (struct nix_sqe_hdr_s *)(sq->sqe_base);
		otx2_sqe_add_hdr(pfvf, sq, sqe_hdr, skb, qidx);
		offset = sizeof(*sqe_hdr);

		/* Add TSO segment's pkt header */
		hdr = sq->tso_hdrs->base + (sq->head * TSO_HEADER_SIZE);
		tso_build_hdr(skb, hdr, &tso, seg_len, tcp_data == 0);
		list.dma_addr[0] =
			sq->tso_hdrs->iova + (sq->head * TSO_HEADER_SIZE);
		list.size[0] = hdr_len;
		list.num_segs = 1;

		/* Add TSO segment's payload data fragments */
		pkt_len = hdr_len;
		while (seg_len > 0) {
			int size;

			size = min_t(int, tso.size, seg_len);

			list.size[list.num_segs] = size;
			list.dma_addr[list.num_segs] =
				otx2_tso_frag_dma_addr(sq, skb,
						       tso.next_frag_idx - 1,
						       (u64)tso.data, hdr_len,
						       first_sqe);
			list.num_segs++;
			pkt_len += size;
			seg_len -= size;
			tso_build_data(skb, &tso, size);
		}
		sqe_hdr->total = pkt_len;
		otx2_sqe_tso_add_sg(sq, &list, &offset);

		/* DMA mappings and skb needs to be freed only after last
		 * TSO segment is transmitted out. So set 'PNC' only for
		 * last segment. Also point last segment's sqe_id to first
		 * segment's SQE index where skb address and DMA mappings
		 * are saved.
		 */
		if (!tcp_data) {
			sqe_hdr->pnc = 1;
			sqe_hdr->sqe_id = first_sqe;
			sq->sg[first_sqe].skb = (u64)skb;
		} else {
			sqe_hdr->pnc = 0;
		}

		sqe_hdr->sizem1 = (offset / 16) - 1;

		/* Flush SQE to HW */
		pfvf->hw_ops->sqe_flush(pfvf, sq, offset, qidx);
	}
}

static bool is_hw_tso_supported(struct otx2_nic *pfvf,
				struct sk_buff *skb)
{
	int payload_len, last_seg_size;

	if (test_bit(HW_TSO, &pfvf->hw.cap_flag))
		return true;

	/* On 96xx A0, HW TSO not supported */
	if (!is_96xx_B0(pfvf->pdev))
		return false;

	/* HW has an issue due to which when the payload of the last LSO
	 * segment is shorter than 16 bytes, some header fields may not
	 * be correctly modified, hence don't offload such TSO segments.
	 */

	payload_len = skb->len - (skb_transport_offset(skb) + tcp_hdrlen(skb));
	last_seg_size = payload_len % skb_shinfo(skb)->gso_size;
	if (last_seg_size && last_seg_size < 16)
		return false;

	return true;
}

static int otx2_get_sqe_count(struct otx2_nic *pfvf, struct sk_buff *skb)
{
	if (!skb_shinfo(skb)->gso_size)
		return 1;

	/* HW TSO */
	if (is_hw_tso_supported(pfvf, skb))
		return 1;

	/* SW TSO */
	return skb_shinfo(skb)->gso_segs;
}

static void otx2_set_txtstamp(struct otx2_nic *pfvf, struct sk_buff *skb,
			      struct otx2_snd_queue *sq, int *offset)
{
	u64 iova;

	if (!skb_shinfo(skb)->gso_size &&
	    skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		iova = sq->timestamps->iova + (sq->head * sizeof(u64));
		otx2_sqe_add_mem(sq, offset, NIX_SENDMEMALG_E_SETTSTMP, iova);
	} else {
		skb_tx_timestamp(skb);
	}
}

bool otx2_sq_append_skb(struct net_device *netdev, struct otx2_snd_queue *sq,
			struct sk_buff *skb, u16 qidx)
{
	struct netdev_queue *txq = netdev_get_tx_queue(netdev, qidx);
	struct otx2_nic *pfvf = netdev_priv(netdev);
	int offset, num_segs, free_sqe;
	struct nix_sqe_hdr_s *sqe_hdr;

	/* Check if there is room for new SQE.
	 * 'Num of SQBs freed to SQ's pool - SQ's Aura count'
	 * will give free SQE count.
	 */
	free_sqe = (sq->num_sqbs - *sq->aura_fc_addr) * sq->sqe_per_sqb;

	if (free_sqe < sq->sqe_thresh ||
	    free_sqe < otx2_get_sqe_count(pfvf, skb))
		return false;

	num_segs = skb_shinfo(skb)->nr_frags + 1;

	/* If SKB doesn't fit in a single SQE, linearize it.
	 * TODO: Consider adding JUMP descriptor instead.
	 */
	if (unlikely(num_segs > OTX2_MAX_FRAGS_IN_SQE)) {
		if (__skb_linearize(skb)) {
			dev_kfree_skb_any(skb);
			return true;
		}
		num_segs = skb_shinfo(skb)->nr_frags + 1;
	}

	if (skb_shinfo(skb)->gso_size && !is_hw_tso_supported(pfvf, skb)) {
		/* Insert vlan tag before giving pkt to tso */
		if (skb_vlan_tag_present(skb))
			skb = __vlan_hwaccel_push_inside(skb);
		otx2_sq_append_tso(pfvf, sq, skb, qidx);
		return true;
	}

	/* Set SQE's SEND_HDR.
	 * Do not clear the first 64bit as it contains constant info.
	 */
	memset(sq->sqe_base + 8, 0, sq->sqe_size - 8);
	sqe_hdr = (struct nix_sqe_hdr_s *)(sq->sqe_base);
	otx2_sqe_add_hdr(pfvf, sq, sqe_hdr, skb, qidx);
	offset = sizeof(*sqe_hdr);

	/* Add extended header if needed */
	otx2_sqe_add_ext(pfvf, sq, skb, &offset);

	/* Add SG subdesc with data frags */
	if (!otx2_sqe_add_sg(pfvf, sq, skb, num_segs, &offset)) {
		otx2_dma_unmap_skb_frags(pfvf, &sq->sg[sq->head]);
		return false;
	}

	otx2_set_txtstamp(pfvf, skb, sq, &offset);

	sqe_hdr->sizem1 = (offset / 16) - 1;

	netdev_tx_sent_queue(txq, skb->len);

	/* Flush SQE to HW */
	pfvf->hw_ops->sqe_flush(pfvf, sq, offset, qidx);

	return true;
}
EXPORT_SYMBOL(otx2_sq_append_skb);

void otx2_cleanup_rx_cqes(struct otx2_nic *pfvf, struct otx2_cq_queue *cq)
{
	struct nix_cqe_rx_s *cqe;
	int processed_cqe = 0;
	u64 iova, pa;

	if (pfvf->xdp_prog)
		xdp_rxq_info_unreg(&cq->xdp_rxq);

	if (otx2_nix_cq_op_status(pfvf, cq) || !cq->pend_cqe)
		return;

	while (cq->pend_cqe) {
		cqe = (struct nix_cqe_rx_s *)otx2_get_next_cqe(cq);
		processed_cqe++;
		cq->pend_cqe--;

		if (!cqe)
			continue;
		if (cqe->sg.segs > 1) {
			otx2_free_rcv_seg(pfvf, cqe, cq->cq_idx);
			continue;
		}
		iova = cqe->sg.seg_addr - OTX2_HEAD_ROOM;
		pa = otx2_iova_to_phys(pfvf->iommu_domain, iova);
		otx2_dma_unmap_page(pfvf, iova, pfvf->rbsize, DMA_FROM_DEVICE);
		put_page(virt_to_page(phys_to_virt(pa)));
	}

	/* Free CQEs to HW */
	otx2_write64(pfvf, NIX_LF_CQ_OP_DOOR,
		     ((u64)cq->cq_idx << 32) | processed_cqe);
}

void otx2_cleanup_tx_cqes(struct otx2_nic *pfvf, struct otx2_cq_queue *cq)
{
	struct sk_buff *skb = NULL;
	struct otx2_snd_queue *sq;
	struct nix_cqe_tx_s *cqe;
	int processed_cqe = 0;
	struct sg_list *sg;

	sq = &pfvf->qset.sq[cq->cint_idx];

	if (otx2_nix_cq_op_status(pfvf, cq) || !cq->pend_cqe)
		return;

	while (cq->pend_cqe) {
		cqe = (struct nix_cqe_tx_s *)otx2_get_next_cqe(cq);
		processed_cqe++;
		cq->pend_cqe--;

		if (!cqe)
			continue;
		sg = &sq->sg[cqe->comp.sqe_id];
		skb = (struct sk_buff *)sg->skb;
		if (skb) {
			otx2_dma_unmap_skb_frags(pfvf, sg);
			dev_kfree_skb_any(skb);
			sg->skb = (u64)NULL;
		}
	}

	/* Free CQEs to HW */
	otx2_write64(pfvf, NIX_LF_CQ_OP_DOOR,
		     ((u64)cq->cq_idx << 32) | processed_cqe);
}

int otx2_rxtx_enable(struct otx2_nic *pfvf, bool enable)
{
	struct msg_req *msg;
	int err;

	mutex_lock(&pfvf->mbox.lock);
	if (enable)
		msg = otx2_mbox_alloc_msg_nix_lf_start_rx(&pfvf->mbox);
	else
		msg = otx2_mbox_alloc_msg_nix_lf_stop_rx(&pfvf->mbox);

	if (!msg) {
		mutex_unlock(&pfvf->mbox.lock);
		return -ENOMEM;
	}

	err = otx2_sync_mbox_msg(&pfvf->mbox);
	mutex_unlock(&pfvf->mbox.lock);
	return err;
}

static void otx2_xdp_sqe_add_sg(struct otx2_snd_queue *sq, u64 dma_addr,
				int len, int *offset)
{
	struct nix_sqe_sg_s *sg = NULL;
	u64 *iova = NULL;

	sg = (struct nix_sqe_sg_s *)(sq->sqe_base + *offset);
	sg->ld_type = NIX_SEND_LDTYPE_LDD;
	sg->subdc = NIX_SUBDC_SG;
	sg->segs = 1;
	sg->seg1_size = len;
	iova = (void *)sg + sizeof(*sg);
	*iova = dma_addr;
	*offset += sizeof(*sg) + sizeof(u64);

	sq->sg[sq->head].dma_addr[0] = dma_addr;
	sq->sg[sq->head].size[0] = len;
	sq->sg[sq->head].num_segs = 1;
}

bool otx2_xdp_sq_append_pkt(struct otx2_nic *pfvf, u64 iova, int len, u16 qidx)
{
	struct nix_sqe_hdr_s *sqe_hdr;
	struct otx2_snd_queue *sq;
	int offset, free_sqe;

	sq = &pfvf->qset.sq[qidx];
	free_sqe = (sq->num_sqbs - *sq->aura_fc_addr) * sq->sqe_per_sqb;
	if (free_sqe < sq->sqe_thresh)
		return false;

	memset(sq->sqe_base + 8, 0, sq->sqe_size - 8);

	sqe_hdr = (struct nix_sqe_hdr_s *)(sq->sqe_base);

	if (!sqe_hdr->total) {
		sqe_hdr->aura = sq->aura_id;
		sqe_hdr->df = 1;
		sqe_hdr->sq = qidx;
		sqe_hdr->pnc = 1;
	}
	sqe_hdr->total = len;
	sqe_hdr->sqe_id = sq->head;

	offset = sizeof(*sqe_hdr);

	otx2_xdp_sqe_add_sg(sq, iova, len, &offset);
	sqe_hdr->sizem1 = (offset / 16) - 1;
	pfvf->hw_ops->sqe_flush(pfvf, sq, offset, qidx);

	return true;
}

static bool otx2_xdp_rcv_pkt_handler(struct otx2_nic *pfvf,
				     struct bpf_prog *prog,
				     struct nix_cqe_rx_s *cqe,
				     struct otx2_cq_queue *cq)
{
	unsigned char *hard_start, *data;
	int qidx = cq->cq_idx;
	struct xdp_buff xdp;
	struct page *page;
	u64 iova, pa;
	u32 act;
	int err;

	iova = cqe->sg.seg_addr - OTX2_HEAD_ROOM;
	pa = otx2_iova_to_phys(pfvf->iommu_domain, iova);
	page = virt_to_page(phys_to_virt(pa));

	xdp_init_buff(&xdp, pfvf->rbsize, &cq->xdp_rxq);

	data = (unsigned char *)phys_to_virt(pa);
	hard_start = page_address(page);
	xdp_prepare_buff(&xdp, hard_start, data - hard_start,
			 cqe->sg.seg_size, false);

	act = bpf_prog_run_xdp(prog, &xdp);

	switch (act) {
	case XDP_PASS:
		break;
	case XDP_TX:
		qidx += pfvf->hw.tx_queues;
		cq->pool_ptrs++;
		return otx2_xdp_sq_append_pkt(pfvf, iova,
					      cqe->sg.seg_size, qidx);
	case XDP_REDIRECT:
		cq->pool_ptrs++;
		err = xdp_do_redirect(pfvf->netdev, &xdp, prog);

		otx2_dma_unmap_page(pfvf, iova, pfvf->rbsize,
				    DMA_FROM_DEVICE);
		if (!err)
			return true;
		put_page(page);
		break;
	default:
		bpf_warn_invalid_xdp_action(pfvf->netdev, prog, act);
		break;
	case XDP_ABORTED:
		trace_xdp_exception(pfvf->netdev, prog, act);
		break;
	case XDP_DROP:
		otx2_dma_unmap_page(pfvf, iova, pfvf->rbsize,
				    DMA_FROM_DEVICE);
		put_page(page);
		cq->pool_ptrs++;
		return true;
	}
	return false;
}
