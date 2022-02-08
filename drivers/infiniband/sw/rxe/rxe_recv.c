// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include <linux/skbuff.h>

#include "rxe.h"
#include "rxe_loc.h"

/* check that QP matches packet opcode type and is in a valid state */
static int check_type_state(struct rxe_dev *rxe, struct rxe_pkt_info *pkt,
			    struct rxe_qp *qp)
{
	unsigned int pkt_type;

	if (unlikely(!qp->valid))
		goto err1;

	pkt_type = pkt->opcode & 0xe0;

	switch (qp_type(qp)) {
	case IB_QPT_RC:
		if (unlikely(pkt_type != IB_OPCODE_RC)) {
			pr_warn_ratelimited("bad qp type\n");
			goto err1;
		}
		break;
	case IB_QPT_UC:
		if (unlikely(pkt_type != IB_OPCODE_UC)) {
			pr_warn_ratelimited("bad qp type\n");
			goto err1;
		}
		break;
	case IB_QPT_UD:
	case IB_QPT_SMI:
	case IB_QPT_GSI:
		if (unlikely(pkt_type != IB_OPCODE_UD)) {
			pr_warn_ratelimited("bad qp type\n");
			goto err1;
		}
		break;
	default:
		pr_warn_ratelimited("unsupported qp type\n");
		goto err1;
	}

	if (pkt->mask & RXE_REQ_MASK) {
		if (unlikely(qp->resp.state != QP_STATE_READY))
			goto err1;
	} else if (unlikely(qp->req.state < QP_STATE_READY ||
				qp->req.state > QP_STATE_DRAINED)) {
		goto err1;
	}

	return 0;

err1:
	return -EINVAL;
}

static void set_bad_pkey_cntr(struct rxe_port *port)
{
	spin_lock_bh(&port->port_lock);
	port->attr.bad_pkey_cntr = min((u32)0xffff,
				       port->attr.bad_pkey_cntr + 1);
	spin_unlock_bh(&port->port_lock);
}

static void set_qkey_viol_cntr(struct rxe_port *port)
{
	spin_lock_bh(&port->port_lock);
	port->attr.qkey_viol_cntr = min((u32)0xffff,
					port->attr.qkey_viol_cntr + 1);
	spin_unlock_bh(&port->port_lock);
}

static int check_keys(struct rxe_dev *rxe, struct rxe_pkt_info *pkt,
		      u32 qpn, struct rxe_qp *qp)
{
	struct rxe_port *port = &rxe->port;
	u16 pkey = bth_pkey(pkt);

	pkt->pkey_index = 0;

	if (!pkey_match(pkey, IB_DEFAULT_PKEY_FULL)) {
		pr_warn_ratelimited("bad pkey = 0x%x\n", pkey);
		set_bad_pkey_cntr(port);
		goto err1;
	}

	if (qp_type(qp) == IB_QPT_UD || qp_type(qp) == IB_QPT_GSI) {
		u32 qkey = (qpn == 1) ? GSI_QKEY : qp->attr.qkey;

		if (unlikely(deth_qkey(pkt) != qkey)) {
			pr_warn_ratelimited("bad qkey, got 0x%x expected 0x%x for qpn 0x%x\n",
					    deth_qkey(pkt), qkey, qpn);
			set_qkey_viol_cntr(port);
			goto err1;
		}
	}

	return 0;

err1:
	return -EINVAL;
}

static int check_addr(struct rxe_dev *rxe, struct rxe_pkt_info *pkt,
		      struct rxe_qp *qp)
{
	struct sk_buff *skb = PKT_TO_SKB(pkt);

	if (qp_type(qp) != IB_QPT_RC && qp_type(qp) != IB_QPT_UC)
		goto done;

	if (unlikely(pkt->port_num != qp->attr.port_num)) {
		pr_warn_ratelimited("port %d != qp port %d\n",
				    pkt->port_num, qp->attr.port_num);
		goto err1;
	}

	if (skb->protocol == htons(ETH_P_IP)) {
		struct in_addr *saddr =
			&qp->pri_av.sgid_addr._sockaddr_in.sin_addr;
		struct in_addr *daddr =
			&qp->pri_av.dgid_addr._sockaddr_in.sin_addr;

		if (ip_hdr(skb)->daddr != saddr->s_addr) {
			pr_warn_ratelimited("dst addr %pI4 != qp source addr %pI4\n",
					    &ip_hdr(skb)->daddr,
					    &saddr->s_addr);
			goto err1;
		}

		if (ip_hdr(skb)->saddr != daddr->s_addr) {
			pr_warn_ratelimited("source addr %pI4 != qp dst addr %pI4\n",
					    &ip_hdr(skb)->saddr,
					    &daddr->s_addr);
			goto err1;
		}

	} else if (skb->protocol == htons(ETH_P_IPV6)) {
		struct in6_addr *saddr =
			&qp->pri_av.sgid_addr._sockaddr_in6.sin6_addr;
		struct in6_addr *daddr =
			&qp->pri_av.dgid_addr._sockaddr_in6.sin6_addr;

		if (memcmp(&ipv6_hdr(skb)->daddr, saddr, sizeof(*saddr))) {
			pr_warn_ratelimited("dst addr %pI6 != qp source addr %pI6\n",
					    &ipv6_hdr(skb)->daddr, saddr);
			goto err1;
		}

		if (memcmp(&ipv6_hdr(skb)->saddr, daddr, sizeof(*daddr))) {
			pr_warn_ratelimited("source addr %pI6 != qp dst addr %pI6\n",
					    &ipv6_hdr(skb)->saddr, daddr);
			goto err1;
		}
	}

done:
	return 0;

err1:
	return -EINVAL;
}

static int hdr_check(struct rxe_pkt_info *pkt)
{
	struct rxe_dev *rxe = pkt->rxe;
	struct rxe_port *port = &rxe->port;
	struct rxe_qp *qp = NULL;
	u32 qpn = bth_qpn(pkt);
	int index;
	int err;

	if (unlikely(bth_tver(pkt) != BTH_TVER)) {
		pr_warn_ratelimited("bad tver\n");
		goto err1;
	}

	if (unlikely(qpn == 0)) {
		pr_warn_once("QP 0 not supported");
		goto err1;
	}

	if (qpn != IB_MULTICAST_QPN) {
		index = (qpn == 1) ? port->qp_gsi_index : qpn;

		qp = rxe_pool_get_index(&rxe->qp_pool, index);
		if (unlikely(!qp)) {
			pr_warn_ratelimited("no qp matches qpn 0x%x\n", qpn);
			goto err1;
		}

		err = check_type_state(rxe, pkt, qp);
		if (unlikely(err))
			goto err2;

		err = check_addr(rxe, pkt, qp);
		if (unlikely(err))
			goto err2;

		err = check_keys(rxe, pkt, qpn, qp);
		if (unlikely(err))
			goto err2;
	} else {
		if (unlikely((pkt->mask & RXE_GRH_MASK) == 0)) {
			pr_warn_ratelimited("no grh for mcast qpn\n");
			goto err1;
		}
	}

	pkt->qp = qp;
	return 0;

err2:
	rxe_drop_ref(qp);
err1:
	return -EINVAL;
}

static inline void rxe_rcv_pkt(struct rxe_pkt_info *pkt, struct sk_buff *skb)
{
	if (pkt->mask & RXE_REQ_MASK)
		rxe_resp_queue_pkt(pkt->qp, skb);
	else
		rxe_comp_queue_pkt(pkt->qp, skb);
}

static void rxe_rcv_mcast_pkt(struct rxe_dev *rxe, struct sk_buff *skb)
{
	struct rxe_pkt_info *pkt = SKB_TO_PKT(skb);
	struct rxe_mcg *mcg;
	struct rxe_mca *mca;
	struct rxe_qp *qp;
	union ib_gid dgid;
	int err;

	if (skb->protocol == htons(ETH_P_IP))
		ipv6_addr_set_v4mapped(ip_hdr(skb)->daddr,
				       (struct in6_addr *)&dgid);
	else if (skb->protocol == htons(ETH_P_IPV6))
		memcpy(&dgid, &ipv6_hdr(skb)->daddr, sizeof(dgid));

	/* lookup mcast group corresponding to mgid, takes a ref */
	mcg = rxe_lookup_mcg(rxe, &dgid);
	if (!mcg)
		goto drop;	/* mcast group not registered */

	spin_lock_bh(&rxe->mcg_lock);

	/* this is unreliable datagram service so we let
	 * failures to deliver a multicast packet to a
	 * single QP happen and just move on and try
	 * the rest of them on the list
	 */
	list_for_each_entry(mca, &mcg->qp_list, qp_list) {
		qp = mca->qp;

		/* validate qp for incoming packet */
		err = check_type_state(rxe, pkt, qp);
		if (err)
			continue;

		err = check_keys(rxe, pkt, bth_qpn(pkt), qp);
		if (err)
			continue;

		/* for all but the last QP create a new clone of the
		 * skb and pass to the QP. Pass the original skb to
		 * the last QP in the list.
		 */
		if (mca->qp_list.next != &mcg->qp_list) {
			struct sk_buff *cskb;
			struct rxe_pkt_info *cpkt;

			cskb = skb_clone(skb, GFP_ATOMIC);
			if (unlikely(!cskb))
				continue;

			if (WARN_ON(!ib_device_try_get(&rxe->ib_dev))) {
				kfree_skb(cskb);
				break;
			}

			cpkt = SKB_TO_PKT(cskb);
			cpkt->qp = qp;
			rxe_add_ref(qp);
			rxe_rcv_pkt(cpkt, cskb);
		} else {
			pkt->qp = qp;
			rxe_add_ref(qp);
			rxe_rcv_pkt(pkt, skb);
			skb = NULL;	/* mark consumed */
		}
	}

	spin_unlock_bh(&rxe->mcg_lock);

	kref_put(&mcg->ref_cnt, rxe_cleanup_mcg);

	if (likely(!skb))
		return;

	/* This only occurs if one of the checks fails on the last
	 * QP in the list above
	 */

drop:
	kfree_skb(skb);
	ib_device_put(&rxe->ib_dev);
}

/**
 * rxe_chk_dgid - validate destination IP address
 * @rxe: rxe device that received packet
 * @skb: the received packet buffer
 *
 * Accept any loopback packets
 * Extract IP address from packet and
 * Accept if multicast packet
 * Accept if matches an SGID table entry
 */
static int rxe_chk_dgid(struct rxe_dev *rxe, struct sk_buff *skb)
{
	struct rxe_pkt_info *pkt = SKB_TO_PKT(skb);
	const struct ib_gid_attr *gid_attr;
	union ib_gid dgid;
	union ib_gid *pdgid;

	if (pkt->mask & RXE_LOOPBACK_MASK)
		return 0;

	if (skb->protocol == htons(ETH_P_IP)) {
		ipv6_addr_set_v4mapped(ip_hdr(skb)->daddr,
				       (struct in6_addr *)&dgid);
		pdgid = &dgid;
	} else {
		pdgid = (union ib_gid *)&ipv6_hdr(skb)->daddr;
	}

	if (rdma_is_multicast_addr((struct in6_addr *)pdgid))
		return 0;

	gid_attr = rdma_find_gid_by_port(&rxe->ib_dev, pdgid,
					 IB_GID_TYPE_ROCE_UDP_ENCAP,
					 1, skb->dev);
	if (IS_ERR(gid_attr))
		return PTR_ERR(gid_attr);

	rdma_put_gid_attr(gid_attr);
	return 0;
}

/* rxe_rcv is called from the interface driver */
void rxe_rcv(struct sk_buff *skb)
{
	int err;
	struct rxe_pkt_info *pkt = SKB_TO_PKT(skb);
	struct rxe_dev *rxe = pkt->rxe;

	if (unlikely(skb->len < RXE_BTH_BYTES))
		goto drop;

	if (rxe_chk_dgid(rxe, skb) < 0) {
		pr_warn_ratelimited("failed checking dgid\n");
		goto drop;
	}

	pkt->opcode = bth_opcode(pkt);
	pkt->psn = bth_psn(pkt);
	pkt->qp = NULL;
	pkt->mask |= rxe_opcode[pkt->opcode].mask;

	if (unlikely(skb->len < header_size(pkt)))
		goto drop;

	err = hdr_check(pkt);
	if (unlikely(err))
		goto drop;

	err = rxe_icrc_check(skb, pkt);
	if (unlikely(err))
		goto drop;

	rxe_counter_inc(rxe, RXE_CNT_RCVD_PKTS);

	if (unlikely(bth_qpn(pkt) == IB_MULTICAST_QPN))
		rxe_rcv_mcast_pkt(rxe, skb);
	else
		rxe_rcv_pkt(pkt, skb);

	return;

drop:
	if (pkt->qp)
		rxe_drop_ref(pkt->qp);

	kfree_skb(skb);
	ib_device_put(&rxe->ib_dev);
}
