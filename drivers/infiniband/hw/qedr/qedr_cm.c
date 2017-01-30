/* QLogic qedr NIC Driver
 * Copyright (c) 2015-2016  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <linux/dma-mapping.h>
#include <linux/crc32.h>
#include <linux/iommu.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/udp.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/iw_cm.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_cache.h>

#include "qedr_hsi.h"
#include <linux/qed/qed_if.h>
#include <linux/qed/qed_roce_if.h>
#include "qedr.h"
#include "qedr_hsi.h"
#include "verbs.h"
#include <rdma/qedr-abi.h>
#include "qedr_hsi.h"
#include "qedr_cm.h"

void qedr_inc_sw_gsi_cons(struct qedr_qp_hwq_info *info)
{
	info->gsi_cons = (info->gsi_cons + 1) % info->max_wr;
}

void qedr_store_gsi_qp_cq(struct qedr_dev *dev, struct qedr_qp *qp,
			  struct ib_qp_init_attr *attrs)
{
	dev->gsi_qp_created = 1;
	dev->gsi_sqcq = get_qedr_cq(attrs->send_cq);
	dev->gsi_rqcq = get_qedr_cq(attrs->recv_cq);
	dev->gsi_qp = qp;
}

void qedr_ll2_tx_cb(void *_qdev, struct qed_roce_ll2_packet *pkt)
{
	struct qedr_dev *dev = (struct qedr_dev *)_qdev;
	struct qedr_cq *cq = dev->gsi_sqcq;
	struct qedr_qp *qp = dev->gsi_qp;
	unsigned long flags;

	DP_DEBUG(dev, QEDR_MSG_GSI,
		 "LL2 TX CB: gsi_sqcq=%p, gsi_rqcq=%p, gsi_cons=%d, ibcq_comp=%s\n",
		 dev->gsi_sqcq, dev->gsi_rqcq, qp->sq.gsi_cons,
		 cq->ibcq.comp_handler ? "Yes" : "No");

	dma_free_coherent(&dev->pdev->dev, pkt->header.len, pkt->header.vaddr,
			  pkt->header.baddr);
	kfree(pkt);

	spin_lock_irqsave(&qp->q_lock, flags);
	qedr_inc_sw_gsi_cons(&qp->sq);
	spin_unlock_irqrestore(&qp->q_lock, flags);

	if (cq->ibcq.comp_handler)
		(*cq->ibcq.comp_handler) (&cq->ibcq, cq->ibcq.cq_context);
}

void qedr_ll2_rx_cb(void *_dev, struct qed_roce_ll2_packet *pkt,
		    struct qed_roce_ll2_rx_params *params)
{
	struct qedr_dev *dev = (struct qedr_dev *)_dev;
	struct qedr_cq *cq = dev->gsi_rqcq;
	struct qedr_qp *qp = dev->gsi_qp;
	unsigned long flags;

	spin_lock_irqsave(&qp->q_lock, flags);

	qp->rqe_wr_id[qp->rq.gsi_cons].rc = params->rc;
	qp->rqe_wr_id[qp->rq.gsi_cons].vlan_id = params->vlan_id;
	qp->rqe_wr_id[qp->rq.gsi_cons].sg_list[0].length = pkt->payload[0].len;
	ether_addr_copy(qp->rqe_wr_id[qp->rq.gsi_cons].smac, params->smac);

	qedr_inc_sw_gsi_cons(&qp->rq);

	spin_unlock_irqrestore(&qp->q_lock, flags);

	if (cq->ibcq.comp_handler)
		(*cq->ibcq.comp_handler) (&cq->ibcq, cq->ibcq.cq_context);
}

static void qedr_destroy_gsi_cq(struct qedr_dev *dev,
				struct ib_qp_init_attr *attrs)
{
	struct qed_rdma_destroy_cq_in_params iparams;
	struct qed_rdma_destroy_cq_out_params oparams;
	struct qedr_cq *cq;

	cq = get_qedr_cq(attrs->send_cq);
	iparams.icid = cq->icid;
	dev->ops->rdma_destroy_cq(dev->rdma_ctx, &iparams, &oparams);
	dev->ops->common->chain_free(dev->cdev, &cq->pbl);

	cq = get_qedr_cq(attrs->recv_cq);
	/* if a dedicated recv_cq was used, delete it too */
	if (iparams.icid != cq->icid) {
		iparams.icid = cq->icid;
		dev->ops->rdma_destroy_cq(dev->rdma_ctx, &iparams, &oparams);
		dev->ops->common->chain_free(dev->cdev, &cq->pbl);
	}
}

static inline int qedr_check_gsi_qp_attrs(struct qedr_dev *dev,
					  struct ib_qp_init_attr *attrs)
{
	if (attrs->cap.max_recv_sge > QEDR_GSI_MAX_RECV_SGE) {
		DP_ERR(dev,
		       " create gsi qp: failed. max_recv_sge is larger the max %d>%d\n",
		       attrs->cap.max_recv_sge, QEDR_GSI_MAX_RECV_SGE);
		return -EINVAL;
	}

	if (attrs->cap.max_recv_wr > QEDR_GSI_MAX_RECV_WR) {
		DP_ERR(dev,
		       " create gsi qp: failed. max_recv_wr is too large %d>%d\n",
		       attrs->cap.max_recv_wr, QEDR_GSI_MAX_RECV_WR);
		return -EINVAL;
	}

	if (attrs->cap.max_send_wr > QEDR_GSI_MAX_SEND_WR) {
		DP_ERR(dev,
		       " create gsi qp: failed. max_send_wr is too large %d>%d\n",
		       attrs->cap.max_send_wr, QEDR_GSI_MAX_SEND_WR);
		return -EINVAL;
	}

	return 0;
}

struct ib_qp *qedr_create_gsi_qp(struct qedr_dev *dev,
				 struct ib_qp_init_attr *attrs,
				 struct qedr_qp *qp)
{
	struct qed_roce_ll2_params ll2_params;
	int rc;

	rc = qedr_check_gsi_qp_attrs(dev, attrs);
	if (rc)
		return ERR_PTR(rc);

	/* configure and start LL2 */
	memset(&ll2_params, 0, sizeof(ll2_params));
	ll2_params.max_tx_buffers = attrs->cap.max_send_wr;
	ll2_params.max_rx_buffers = attrs->cap.max_recv_wr;
	ll2_params.cbs.tx_cb = qedr_ll2_tx_cb;
	ll2_params.cbs.rx_cb = qedr_ll2_rx_cb;
	ll2_params.cb_cookie = (void *)dev;
	ll2_params.mtu = dev->ndev->mtu;
	ether_addr_copy(ll2_params.mac_address, dev->ndev->dev_addr);
	rc = dev->ops->roce_ll2_start(dev->cdev, &ll2_params);
	if (rc) {
		DP_ERR(dev, "create gsi qp: failed on ll2 start. rc=%d\n", rc);
		return ERR_PTR(rc);
	}

	/* create QP */
	qp->ibqp.qp_num = 1;
	qp->rq.max_wr = attrs->cap.max_recv_wr;
	qp->sq.max_wr = attrs->cap.max_send_wr;

	qp->rqe_wr_id = kcalloc(qp->rq.max_wr, sizeof(*qp->rqe_wr_id),
				GFP_KERNEL);
	if (!qp->rqe_wr_id)
		goto err;
	qp->wqe_wr_id = kcalloc(qp->sq.max_wr, sizeof(*qp->wqe_wr_id),
				GFP_KERNEL);
	if (!qp->wqe_wr_id)
		goto err;

	qedr_store_gsi_qp_cq(dev, qp, attrs);
	ether_addr_copy(dev->gsi_ll2_mac_address, dev->ndev->dev_addr);

	/* the GSI CQ is handled by the driver so remove it from the FW */
	qedr_destroy_gsi_cq(dev, attrs);
	dev->gsi_rqcq->cq_type = QEDR_CQ_TYPE_GSI;
	dev->gsi_rqcq->cq_type = QEDR_CQ_TYPE_GSI;

	DP_DEBUG(dev, QEDR_MSG_GSI, "created GSI QP %p\n", qp);

	return &qp->ibqp;

err:
	kfree(qp->rqe_wr_id);

	rc = dev->ops->roce_ll2_stop(dev->cdev);
	if (rc)
		DP_ERR(dev, "create gsi qp: failed destroy on create\n");

	return ERR_PTR(-ENOMEM);
}

int qedr_destroy_gsi_qp(struct qedr_dev *dev)
{
	int rc;

	rc = dev->ops->roce_ll2_stop(dev->cdev);
	if (rc)
		DP_ERR(dev, "destroy gsi qp: failed (rc=%d)\n", rc);
	else
		DP_DEBUG(dev, QEDR_MSG_GSI, "destroy gsi qp: success\n");

	return rc;
}

#define QEDR_MAX_UD_HEADER_SIZE	(100)
#define QEDR_GSI_QPN		(1)
static inline int qedr_gsi_build_header(struct qedr_dev *dev,
					struct qedr_qp *qp,
					struct ib_send_wr *swr,
					struct ib_ud_header *udh,
					int *roce_mode)
{
	bool has_vlan = false, has_grh_ipv6 = true;
	struct ib_ah_attr *ah_attr = &get_qedr_ah(ud_wr(swr)->ah)->attr;
	struct ib_global_route *grh = &ah_attr->grh;
	union ib_gid sgid;
	int send_size = 0;
	u16 vlan_id = 0;
	u16 ether_type;
	struct ib_gid_attr sgid_attr;
	int rc;
	int ip_ver = 0;

	bool has_udp = false;
	int i;

	send_size = 0;
	for (i = 0; i < swr->num_sge; ++i)
		send_size += swr->sg_list[i].length;

	rc = ib_get_cached_gid(qp->ibqp.device, ah_attr->port_num,
			       grh->sgid_index, &sgid, &sgid_attr);
	if (rc) {
		DP_ERR(dev,
		       "gsi post send: failed to get cached GID (port=%d, ix=%d)\n",
		       ah_attr->port_num, grh->sgid_index);
		return rc;
	}

	vlan_id = rdma_vlan_dev_vlan_id(sgid_attr.ndev);
	if (vlan_id < VLAN_CFI_MASK)
		has_vlan = true;
	if (sgid_attr.ndev)
		dev_put(sgid_attr.ndev);

	if (!memcmp(&sgid, &zgid, sizeof(sgid))) {
		DP_ERR(dev, "gsi post send: GID not found GID index %d\n",
		       ah_attr->grh.sgid_index);
		return -ENOENT;
	}

	has_udp = (sgid_attr.gid_type == IB_GID_TYPE_ROCE_UDP_ENCAP);
	if (!has_udp) {
		/* RoCE v1 */
		ether_type = ETH_P_ROCE;
		*roce_mode = ROCE_V1;
	} else if (ipv6_addr_v4mapped((struct in6_addr *)&sgid)) {
		/* RoCE v2 IPv4 */
		ip_ver = 4;
		ether_type = ETH_P_IP;
		has_grh_ipv6 = false;
		*roce_mode = ROCE_V2_IPV4;
	} else {
		/* RoCE v2 IPv6 */
		ip_ver = 6;
		ether_type = ETH_P_IPV6;
		*roce_mode = ROCE_V2_IPV6;
	}

	rc = ib_ud_header_init(send_size, false, true, has_vlan,
			       has_grh_ipv6, ip_ver, has_udp, 0, udh);
	if (rc) {
		DP_ERR(dev, "gsi post send: failed to init header\n");
		return rc;
	}

	/* ENET + VLAN headers */
	ether_addr_copy(udh->eth.dmac_h, ah_attr->dmac);
	ether_addr_copy(udh->eth.smac_h, dev->ndev->dev_addr);
	if (has_vlan) {
		udh->eth.type = htons(ETH_P_8021Q);
		udh->vlan.tag = htons(vlan_id);
		udh->vlan.type = htons(ether_type);
	} else {
		udh->eth.type = htons(ether_type);
	}

	/* BTH */
	udh->bth.solicited_event = !!(swr->send_flags & IB_SEND_SOLICITED);
	udh->bth.pkey = QEDR_ROCE_PKEY_DEFAULT;
	udh->bth.destination_qpn = htonl(ud_wr(swr)->remote_qpn);
	udh->bth.psn = htonl((qp->sq_psn++) & ((1 << 24) - 1));
	udh->bth.opcode = IB_OPCODE_UD_SEND_ONLY;

	/* DETH */
	udh->deth.qkey = htonl(0x80010000);
	udh->deth.source_qpn = htonl(QEDR_GSI_QPN);

	if (has_grh_ipv6) {
		/* GRH / IPv6 header */
		udh->grh.traffic_class = grh->traffic_class;
		udh->grh.flow_label = grh->flow_label;
		udh->grh.hop_limit = grh->hop_limit;
		udh->grh.destination_gid = grh->dgid;
		memcpy(&udh->grh.source_gid.raw, &sgid.raw,
		       sizeof(udh->grh.source_gid.raw));
	} else {
		/* IPv4 header */
		u32 ipv4_addr;

		udh->ip4.protocol = IPPROTO_UDP;
		udh->ip4.tos = htonl(ah_attr->grh.flow_label);
		udh->ip4.frag_off = htons(IP_DF);
		udh->ip4.ttl = ah_attr->grh.hop_limit;

		ipv4_addr = qedr_get_ipv4_from_gid(sgid.raw);
		udh->ip4.saddr = ipv4_addr;
		ipv4_addr = qedr_get_ipv4_from_gid(ah_attr->grh.dgid.raw);
		udh->ip4.daddr = ipv4_addr;
		/* note: checksum is calculated by the device */
	}

	/* UDP */
	if (has_udp) {
		udh->udp.sport = htons(QEDR_ROCE_V2_UDP_SPORT);
		udh->udp.dport = htons(ROCE_V2_UDP_DPORT);
		udh->udp.csum = 0;
		/* UDP length is untouched hence is zero */
	}
	return 0;
}

static inline int qedr_gsi_build_packet(struct qedr_dev *dev,
					struct qedr_qp *qp,
					struct ib_send_wr *swr,
					struct qed_roce_ll2_packet **p_packet)
{
	u8 ud_header_buffer[QEDR_MAX_UD_HEADER_SIZE];
	struct qed_roce_ll2_packet *packet;
	struct pci_dev *pdev = dev->pdev;
	int roce_mode, header_size;
	struct ib_ud_header udh;
	int i, rc;

	*p_packet = NULL;

	rc = qedr_gsi_build_header(dev, qp, swr, &udh, &roce_mode);
	if (rc)
		return rc;

	header_size = ib_ud_header_pack(&udh, &ud_header_buffer);

	packet = kzalloc(sizeof(*packet), GFP_ATOMIC);
	if (!packet)
		return -ENOMEM;

	packet->header.vaddr = dma_alloc_coherent(&pdev->dev, header_size,
						  &packet->header.baddr,
						  GFP_ATOMIC);
	if (!packet->header.vaddr) {
		kfree(packet);
		return -ENOMEM;
	}

	if (ether_addr_equal(udh.eth.smac_h, udh.eth.dmac_h))
		packet->tx_dest = QED_ROCE_LL2_TX_DEST_LB;
	else
		packet->tx_dest = QED_ROCE_LL2_TX_DEST_NW;

	packet->roce_mode = roce_mode;
	memcpy(packet->header.vaddr, ud_header_buffer, header_size);
	packet->header.len = header_size;
	packet->n_seg = swr->num_sge;
	for (i = 0; i < packet->n_seg; i++) {
		packet->payload[i].baddr = swr->sg_list[i].addr;
		packet->payload[i].len = swr->sg_list[i].length;
	}

	*p_packet = packet;

	return 0;
}

int qedr_gsi_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
		       struct ib_send_wr **bad_wr)
{
	struct qed_roce_ll2_packet *pkt = NULL;
	struct qedr_qp *qp = get_qedr_qp(ibqp);
	struct qed_roce_ll2_tx_params params;
	struct qedr_dev *dev = qp->dev;
	unsigned long flags;
	int rc;

	if (qp->state != QED_ROCE_QP_STATE_RTS) {
		*bad_wr = wr;
		DP_ERR(dev,
		       "gsi post recv: failed to post rx buffer. state is %d and not QED_ROCE_QP_STATE_RTS\n",
		       qp->state);
		return -EINVAL;
	}

	if (wr->num_sge > RDMA_MAX_SGE_PER_SQ_WQE) {
		DP_ERR(dev, "gsi post send: num_sge is too large (%d>%d)\n",
		       wr->num_sge, RDMA_MAX_SGE_PER_SQ_WQE);
		rc = -EINVAL;
		goto err;
	}

	if (wr->opcode != IB_WR_SEND) {
		DP_ERR(dev,
		       "gsi post send: failed due to unsupported opcode %d\n",
		       wr->opcode);
		rc = -EINVAL;
		goto err;
	}

	memset(&params, 0, sizeof(params));

	spin_lock_irqsave(&qp->q_lock, flags);

	rc = qedr_gsi_build_packet(dev, qp, wr, &pkt);
	if (rc) {
		spin_unlock_irqrestore(&qp->q_lock, flags);
		goto err;
	}

	rc = dev->ops->roce_ll2_tx(dev->cdev, pkt, &params);
	if (!rc) {
		qp->wqe_wr_id[qp->sq.prod].wr_id = wr->wr_id;
		qedr_inc_sw_prod(&qp->sq);
		DP_DEBUG(qp->dev, QEDR_MSG_GSI,
			 "gsi post send: opcode=%d, in_irq=%ld, irqs_disabled=%d, wr_id=%llx\n",
			 wr->opcode, in_irq(), irqs_disabled(), wr->wr_id);
	} else {
		if (rc == QED_ROCE_TX_HEAD_FAILURE) {
			/* TX failed while posting header - release resources */
			dma_free_coherent(&dev->pdev->dev, pkt->header.len,
					  pkt->header.vaddr, pkt->header.baddr);
			kfree(pkt);
		} else if (rc == QED_ROCE_TX_FRAG_FAILURE) {
			/* NTD since TX failed while posting a fragment. We will
			 * release the resources on TX callback
			 */
		}

		DP_ERR(dev, "gsi post send: failed to transmit (rc=%d)\n", rc);
		rc = -EAGAIN;
		*bad_wr = wr;
	}

	spin_unlock_irqrestore(&qp->q_lock, flags);

	if (wr->next) {
		DP_ERR(dev,
		       "gsi post send: failed second WR. Only one WR may be passed at a time\n");
		*bad_wr = wr->next;
		rc = -EINVAL;
	}

	return rc;

err:
	*bad_wr = wr;
	return rc;
}

int qedr_gsi_post_recv(struct ib_qp *ibqp, struct ib_recv_wr *wr,
		       struct ib_recv_wr **bad_wr)
{
	struct qedr_dev *dev = get_qedr_dev(ibqp->device);
	struct qedr_qp *qp = get_qedr_qp(ibqp);
	struct qed_roce_ll2_buffer buf;
	unsigned long flags;
	int status = 0;
	int rc;

	if ((qp->state != QED_ROCE_QP_STATE_RTR) &&
	    (qp->state != QED_ROCE_QP_STATE_RTS)) {
		*bad_wr = wr;
		DP_ERR(dev,
		       "gsi post recv: failed to post rx buffer. state is %d and not QED_ROCE_QP_STATE_RTR/S\n",
		       qp->state);
		return -EINVAL;
	}

	memset(&buf, 0, sizeof(buf));

	spin_lock_irqsave(&qp->q_lock, flags);

	while (wr) {
		if (wr->num_sge > QEDR_GSI_MAX_RECV_SGE) {
			DP_ERR(dev,
			       "gsi post recv: failed to post rx buffer. too many sges %d>%d\n",
			       wr->num_sge, QEDR_GSI_MAX_RECV_SGE);
			goto err;
		}

		buf.baddr = wr->sg_list[0].addr;
		buf.len = wr->sg_list[0].length;

		rc = dev->ops->roce_ll2_post_rx_buffer(dev->cdev, &buf, 0, 1);
		if (rc) {
			DP_ERR(dev,
			       "gsi post recv: failed to post rx buffer (rc=%d)\n",
			       rc);
			goto err;
		}

		memset(&qp->rqe_wr_id[qp->rq.prod], 0,
		       sizeof(qp->rqe_wr_id[qp->rq.prod]));
		qp->rqe_wr_id[qp->rq.prod].sg_list[0] = wr->sg_list[0];
		qp->rqe_wr_id[qp->rq.prod].wr_id = wr->wr_id;

		qedr_inc_sw_prod(&qp->rq);

		wr = wr->next;
	}

	spin_unlock_irqrestore(&qp->q_lock, flags);

	return status;
err:
	spin_unlock_irqrestore(&qp->q_lock, flags);
	*bad_wr = wr;
	return -ENOMEM;
}

int qedr_gsi_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc)
{
	struct qedr_dev *dev = get_qedr_dev(ibcq->device);
	struct qedr_cq *cq = get_qedr_cq(ibcq);
	struct qedr_qp *qp = dev->gsi_qp;
	unsigned long flags;
	int i = 0;

	spin_lock_irqsave(&cq->cq_lock, flags);

	while (i < num_entries && qp->rq.cons != qp->rq.gsi_cons) {
		memset(&wc[i], 0, sizeof(*wc));

		wc[i].qp = &qp->ibqp;
		wc[i].wr_id = qp->rqe_wr_id[qp->rq.cons].wr_id;
		wc[i].opcode = IB_WC_RECV;
		wc[i].pkey_index = 0;
		wc[i].status = (qp->rqe_wr_id[qp->rq.cons].rc) ?
		    IB_WC_GENERAL_ERR : IB_WC_SUCCESS;
		/* 0 - currently only one recv sg is supported */
		wc[i].byte_len = qp->rqe_wr_id[qp->rq.cons].sg_list[0].length;
		wc[i].wc_flags |= IB_WC_GRH | IB_WC_IP_CSUM_OK;
		ether_addr_copy(wc[i].smac, qp->rqe_wr_id[qp->rq.cons].smac);
		wc[i].wc_flags |= IB_WC_WITH_SMAC;
		if (qp->rqe_wr_id[qp->rq.cons].vlan_id) {
			wc[i].wc_flags |= IB_WC_WITH_VLAN;
			wc[i].vlan_id = qp->rqe_wr_id[qp->rq.cons].vlan_id;
		}

		qedr_inc_sw_cons(&qp->rq);
		i++;
	}

	while (i < num_entries && qp->sq.cons != qp->sq.gsi_cons) {
		memset(&wc[i], 0, sizeof(*wc));

		wc[i].qp = &qp->ibqp;
		wc[i].wr_id = qp->wqe_wr_id[qp->sq.cons].wr_id;
		wc[i].opcode = IB_WC_SEND;
		wc[i].status = IB_WC_SUCCESS;

		qedr_inc_sw_cons(&qp->sq);
		i++;
	}

	spin_unlock_irqrestore(&cq->cq_lock, flags);

	DP_DEBUG(dev, QEDR_MSG_GSI,
		 "gsi poll_cq: requested entries=%d, actual=%d, qp->rq.cons=%d, qp->rq.gsi_cons=%x, qp->sq.cons=%d, qp->sq.gsi_cons=%d, qp_num=%d\n",
		 num_entries, i, qp->rq.cons, qp->rq.gsi_cons, qp->sq.cons,
		 qp->sq.gsi_cons, qp->ibqp.qp_num);

	return i;
}
