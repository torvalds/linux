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

static void bnge_sched_reset_rxr(struct bnge_net *bn,
				 struct bnge_rx_ring_info *rxr)
{
	if (!rxr->bnapi->in_reset) {
		rxr->bnapi->in_reset = true;

		/* TODO: Initiate reset task */
	}
	rxr->rx_next_cons = 0xffff;
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
	struct bnge_sw_rx_bd *rx_buf;
	struct rx_cmp_ext *rxcmp1;
	u16 cons, prod, cp_cons;
	u32 tmp_raw_cons, flags;
	u8 *data_ptr, cmp_type;
	struct rx_cmp *rxcmp;
	dma_addr_t dma_addr;
	struct sk_buff *skb;
	unsigned int len;
	void *data;
	int rc = 0;

	rxr = bnapi->rx_ring;

	tmp_raw_cons = *raw_cons;
	cp_cons = RING_CMP(bn, tmp_raw_cons);
	rxcmp = (struct rx_cmp *)
			&cpr->desc_ring[CP_RING(cp_cons)][CP_IDX(cp_cons)];

	cmp_type = RX_CMP_TYPE(rxcmp);

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

	cons = rxcmp->rx_cmp_opaque;
	if (unlikely(cons != rxr->rx_next_cons)) {
		/* 0xffff is forced error, don't print it */
		if (rxr->rx_next_cons != 0xffff)
			netdev_warn(bn->netdev, "RX cons %x != expected cons %x\n",
				    cons, rxr->rx_next_cons);
		bnge_sched_reset_rxr(bn, rxr);
		goto next_rx_no_prod_no_len;
	}
	rx_buf = &rxr->rx_buf_ring[cons];
	data = rx_buf->data;
	data_ptr = rx_buf->data_ptr;
	prefetch(data_ptr);

	*event |= BNGE_RX_EVENT;

	rx_buf->data = NULL;
	if (rxcmp1->rx_cmp_cfa_code_errors_v2 & RX_CMP_L2_ERRORS) {
		bnge_reuse_rx_data(rxr, cons, data);
		rc = -EIO;
		goto next_rx_no_len;
	}

	flags = le32_to_cpu(rxcmp->rx_cmp_len_flags_type);
	len = flags >> RX_CMP_LEN_SHIFT;
	dma_addr = rx_buf->mapping;

	if (len <= bn->rx_copybreak) {
		skb = bnge_copy_skb(bnapi, data_ptr, len, dma_addr);
		bnge_reuse_rx_data(rxr, cons, data);
		if (!skb)
			goto oom_next_rx;
	} else {
		skb = bnge_rx_skb(bn, rxr, cons, data, data_ptr, dma_addr, len);
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
	}
	rc = bnge_rx_pkt(bn, cpr, raw_cons, event);
	return rc;
}

static void __bnge_poll_work_done(struct bnge_net *bn, struct bnge_napi *bnapi,
				  int budget)
{
	struct bnge_rx_ring_info *rxr = bnapi->rx_ring;

	if ((bnapi->events & BNGE_RX_EVENT)) {
		bnge_db_write(bn->bd, &rxr->rx_db, rxr->rx_prod);
		bnapi->events &= ~BNGE_RX_EVENT;
	}
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
			/*
			 * Tx Compl Processing
			 */
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
