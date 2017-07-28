/*
 * Copyright (c) 2016 Chelsio Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define DRV_NAME "cxgbit"
#define DRV_VERSION "1.0.0-ko"
#define pr_fmt(fmt) DRV_NAME ": " fmt

#include "cxgbit.h"

#ifdef CONFIG_CHELSIO_T4_DCB
#include <net/dcbevent.h>
#include "cxgb4_dcb.h"
#endif

LIST_HEAD(cdev_list_head);
/* cdev list lock */
DEFINE_MUTEX(cdev_list_lock);

void _cxgbit_free_cdev(struct kref *kref)
{
	struct cxgbit_device *cdev;

	cdev = container_of(kref, struct cxgbit_device, kref);

	cxgbi_ppm_release(cdev2ppm(cdev));
	kfree(cdev);
}

static void cxgbit_set_mdsl(struct cxgbit_device *cdev)
{
	struct cxgb4_lld_info *lldi = &cdev->lldi;
	u32 mdsl;

#define ULP2_MAX_PKT_LEN 16224
#define ISCSI_PDU_NONPAYLOAD_LEN 312
	mdsl = min_t(u32, lldi->iscsi_iolen - ISCSI_PDU_NONPAYLOAD_LEN,
		     ULP2_MAX_PKT_LEN - ISCSI_PDU_NONPAYLOAD_LEN);
	mdsl = min_t(u32, mdsl, 8192);
	mdsl = min_t(u32, mdsl, (MAX_SKB_FRAGS - 1) * PAGE_SIZE);

	cdev->mdsl = mdsl;
}

static void *cxgbit_uld_add(const struct cxgb4_lld_info *lldi)
{
	struct cxgbit_device *cdev;

	if (is_t4(lldi->adapter_type))
		return ERR_PTR(-ENODEV);

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return ERR_PTR(-ENOMEM);

	kref_init(&cdev->kref);

	cdev->lldi = *lldi;

	cxgbit_set_mdsl(cdev);

	if (cxgbit_ddp_init(cdev) < 0) {
		kfree(cdev);
		return ERR_PTR(-EINVAL);
	}

	if (!test_bit(CDEV_DDP_ENABLE, &cdev->flags))
		pr_info("cdev %s ddp init failed\n",
			pci_name(lldi->pdev));

	if (lldi->fw_vers >= 0x10d2b00)
		set_bit(CDEV_ISO_ENABLE, &cdev->flags);

	spin_lock_init(&cdev->cskq.lock);
	INIT_LIST_HEAD(&cdev->cskq.list);

	mutex_lock(&cdev_list_lock);
	list_add_tail(&cdev->list, &cdev_list_head);
	mutex_unlock(&cdev_list_lock);

	pr_info("cdev %s added for iSCSI target transport\n",
		pci_name(lldi->pdev));

	return cdev;
}

static void cxgbit_close_conn(struct cxgbit_device *cdev)
{
	struct cxgbit_sock *csk;
	struct sk_buff *skb;
	bool wakeup_thread = false;

	spin_lock_bh(&cdev->cskq.lock);
	list_for_each_entry(csk, &cdev->cskq.list, list) {
		skb = alloc_skb(0, GFP_ATOMIC);
		if (!skb)
			continue;

		spin_lock_bh(&csk->rxq.lock);
		__skb_queue_tail(&csk->rxq, skb);
		if (skb_queue_len(&csk->rxq) == 1)
			wakeup_thread = true;
		spin_unlock_bh(&csk->rxq.lock);

		if (wakeup_thread) {
			wake_up(&csk->waitq);
			wakeup_thread = false;
		}
	}
	spin_unlock_bh(&cdev->cskq.lock);
}

static void cxgbit_detach_cdev(struct cxgbit_device *cdev)
{
	bool free_cdev = false;

	spin_lock_bh(&cdev->cskq.lock);
	if (list_empty(&cdev->cskq.list))
		free_cdev = true;
	spin_unlock_bh(&cdev->cskq.lock);

	if (free_cdev) {
		mutex_lock(&cdev_list_lock);
		list_del(&cdev->list);
		mutex_unlock(&cdev_list_lock);

		cxgbit_put_cdev(cdev);
	} else {
		cxgbit_close_conn(cdev);
	}
}

static int cxgbit_uld_state_change(void *handle, enum cxgb4_state state)
{
	struct cxgbit_device *cdev = handle;

	switch (state) {
	case CXGB4_STATE_UP:
		set_bit(CDEV_STATE_UP, &cdev->flags);
		pr_info("cdev %s state UP.\n", pci_name(cdev->lldi.pdev));
		break;
	case CXGB4_STATE_START_RECOVERY:
		clear_bit(CDEV_STATE_UP, &cdev->flags);
		cxgbit_close_conn(cdev);
		pr_info("cdev %s state RECOVERY.\n", pci_name(cdev->lldi.pdev));
		break;
	case CXGB4_STATE_DOWN:
		pr_info("cdev %s state DOWN.\n", pci_name(cdev->lldi.pdev));
		break;
	case CXGB4_STATE_DETACH:
		clear_bit(CDEV_STATE_UP, &cdev->flags);
		pr_info("cdev %s state DETACH.\n", pci_name(cdev->lldi.pdev));
		cxgbit_detach_cdev(cdev);
		break;
	default:
		pr_info("cdev %s unknown state %d.\n",
			pci_name(cdev->lldi.pdev), state);
		break;
	}
	return 0;
}

static void
cxgbit_process_ddpvld(struct cxgbit_sock *csk, struct cxgbit_lro_pdu_cb *pdu_cb,
		      u32 ddpvld)
{

	if (ddpvld & (1 << CPL_RX_ISCSI_DDP_STATUS_HCRC_SHIFT)) {
		pr_info("tid 0x%x, status 0x%x, hcrc bad.\n", csk->tid, ddpvld);
		pdu_cb->flags |= PDUCBF_RX_HCRC_ERR;
	}

	if (ddpvld & (1 << CPL_RX_ISCSI_DDP_STATUS_DCRC_SHIFT)) {
		pr_info("tid 0x%x, status 0x%x, dcrc bad.\n", csk->tid, ddpvld);
		pdu_cb->flags |= PDUCBF_RX_DCRC_ERR;
	}

	if (ddpvld & (1 << CPL_RX_ISCSI_DDP_STATUS_PAD_SHIFT))
		pr_info("tid 0x%x, status 0x%x, pad bad.\n", csk->tid, ddpvld);

	if ((ddpvld & (1 << CPL_RX_ISCSI_DDP_STATUS_DDP_SHIFT)) &&
	    (!(pdu_cb->flags & PDUCBF_RX_DATA))) {
		pdu_cb->flags |= PDUCBF_RX_DATA_DDPD;
	}
}

static void
cxgbit_lro_add_packet_rsp(struct sk_buff *skb, u8 op, const __be64 *rsp)
{
	struct cxgbit_lro_cb *lro_cb = cxgbit_skb_lro_cb(skb);
	struct cxgbit_lro_pdu_cb *pdu_cb = cxgbit_skb_lro_pdu_cb(skb,
						lro_cb->pdu_idx);
	struct cpl_rx_iscsi_ddp *cpl = (struct cpl_rx_iscsi_ddp *)(rsp + 1);

	cxgbit_process_ddpvld(lro_cb->csk, pdu_cb, be32_to_cpu(cpl->ddpvld));

	pdu_cb->flags |= PDUCBF_RX_STATUS;
	pdu_cb->ddigest = ntohl(cpl->ulp_crc);
	pdu_cb->pdulen = ntohs(cpl->len);

	if (pdu_cb->flags & PDUCBF_RX_HDR)
		pdu_cb->complete = true;

	lro_cb->pdu_totallen += pdu_cb->pdulen;
	lro_cb->complete = true;
	lro_cb->pdu_idx++;
}

static void
cxgbit_copy_frags(struct sk_buff *skb, const struct pkt_gl *gl,
		  unsigned int offset)
{
	u8 skb_frag_idx = skb_shinfo(skb)->nr_frags;
	u8 i;

	/* usually there's just one frag */
	__skb_fill_page_desc(skb, skb_frag_idx, gl->frags[0].page,
			     gl->frags[0].offset + offset,
			     gl->frags[0].size - offset);
	for (i = 1; i < gl->nfrags; i++)
		__skb_fill_page_desc(skb, skb_frag_idx + i,
				     gl->frags[i].page,
				     gl->frags[i].offset,
				     gl->frags[i].size);

	skb_shinfo(skb)->nr_frags += gl->nfrags;

	/* get a reference to the last page, we don't own it */
	get_page(gl->frags[gl->nfrags - 1].page);
}

static void
cxgbit_lro_add_packet_gl(struct sk_buff *skb, u8 op, const struct pkt_gl *gl)
{
	struct cxgbit_lro_cb *lro_cb = cxgbit_skb_lro_cb(skb);
	struct cxgbit_lro_pdu_cb *pdu_cb = cxgbit_skb_lro_pdu_cb(skb,
						lro_cb->pdu_idx);
	u32 len, offset;

	if (op == CPL_ISCSI_HDR) {
		struct cpl_iscsi_hdr *cpl = (struct cpl_iscsi_hdr *)gl->va;

		offset = sizeof(struct cpl_iscsi_hdr);
		pdu_cb->flags |= PDUCBF_RX_HDR;
		pdu_cb->seq = ntohl(cpl->seq);
		len = ntohs(cpl->len);
		pdu_cb->hdr = gl->va + offset;
		pdu_cb->hlen = len;
		pdu_cb->hfrag_idx = skb_shinfo(skb)->nr_frags;

		if (unlikely(gl->nfrags > 1))
			cxgbit_skcb_flags(skb) = 0;

		lro_cb->complete = false;
	} else if (op == CPL_ISCSI_DATA) {
		struct cpl_iscsi_data *cpl = (struct cpl_iscsi_data *)gl->va;

		offset = sizeof(struct cpl_iscsi_data);
		pdu_cb->flags |= PDUCBF_RX_DATA;
		len = ntohs(cpl->len);
		pdu_cb->dlen = len;
		pdu_cb->doffset = lro_cb->offset;
		pdu_cb->nr_dfrags = gl->nfrags;
		pdu_cb->dfrag_idx = skb_shinfo(skb)->nr_frags;
		lro_cb->complete = false;
	} else {
		struct cpl_rx_iscsi_cmp *cpl;

		cpl = (struct cpl_rx_iscsi_cmp *)gl->va;
		offset = sizeof(struct cpl_rx_iscsi_cmp);
		pdu_cb->flags |= (PDUCBF_RX_HDR | PDUCBF_RX_STATUS);
		len = be16_to_cpu(cpl->len);
		pdu_cb->hdr = gl->va + offset;
		pdu_cb->hlen = len;
		pdu_cb->hfrag_idx = skb_shinfo(skb)->nr_frags;
		pdu_cb->ddigest = be32_to_cpu(cpl->ulp_crc);
		pdu_cb->pdulen = ntohs(cpl->len);

		if (unlikely(gl->nfrags > 1))
			cxgbit_skcb_flags(skb) = 0;

		cxgbit_process_ddpvld(lro_cb->csk, pdu_cb,
				      be32_to_cpu(cpl->ddpvld));

		if (pdu_cb->flags & PDUCBF_RX_DATA_DDPD) {
			pdu_cb->flags |= PDUCBF_RX_DDP_CMP;
			pdu_cb->complete = true;
		} else if (pdu_cb->flags & PDUCBF_RX_DATA) {
			pdu_cb->complete = true;
		}

		lro_cb->pdu_totallen += pdu_cb->hlen + pdu_cb->dlen;
		lro_cb->complete = true;
		lro_cb->pdu_idx++;
	}

	cxgbit_copy_frags(skb, gl, offset);

	pdu_cb->frags += gl->nfrags;
	lro_cb->offset += len;
	skb->len += len;
	skb->data_len += len;
	skb->truesize += len;
}

static struct sk_buff *
cxgbit_lro_init_skb(struct cxgbit_sock *csk, u8 op, const struct pkt_gl *gl,
		    const __be64 *rsp, struct napi_struct *napi)
{
	struct sk_buff *skb;
	struct cxgbit_lro_cb *lro_cb;

	skb = napi_alloc_skb(napi, LRO_SKB_MAX_HEADROOM);

	if (unlikely(!skb))
		return NULL;

	memset(skb->data, 0, LRO_SKB_MAX_HEADROOM);

	cxgbit_skcb_flags(skb) |= SKCBF_RX_LRO;

	lro_cb = cxgbit_skb_lro_cb(skb);

	cxgbit_get_csk(csk);

	lro_cb->csk = csk;

	return skb;
}

static void cxgbit_queue_lro_skb(struct cxgbit_sock *csk, struct sk_buff *skb)
{
	bool wakeup_thread = false;

	spin_lock(&csk->rxq.lock);
	__skb_queue_tail(&csk->rxq, skb);
	if (skb_queue_len(&csk->rxq) == 1)
		wakeup_thread = true;
	spin_unlock(&csk->rxq.lock);

	if (wakeup_thread)
		wake_up(&csk->waitq);
}

static void cxgbit_lro_flush(struct t4_lro_mgr *lro_mgr, struct sk_buff *skb)
{
	struct cxgbit_lro_cb *lro_cb = cxgbit_skb_lro_cb(skb);
	struct cxgbit_sock *csk = lro_cb->csk;

	csk->lro_skb = NULL;

	__skb_unlink(skb, &lro_mgr->lroq);
	cxgbit_queue_lro_skb(csk, skb);

	cxgbit_put_csk(csk);

	lro_mgr->lro_pkts++;
	lro_mgr->lro_session_cnt--;
}

static void cxgbit_uld_lro_flush(struct t4_lro_mgr *lro_mgr)
{
	struct sk_buff *skb;

	while ((skb = skb_peek(&lro_mgr->lroq)))
		cxgbit_lro_flush(lro_mgr, skb);
}

static int
cxgbit_lro_receive(struct cxgbit_sock *csk, u8 op, const __be64 *rsp,
		   const struct pkt_gl *gl, struct t4_lro_mgr *lro_mgr,
		   struct napi_struct *napi)
{
	struct sk_buff *skb;
	struct cxgbit_lro_cb *lro_cb;

	if (!csk) {
		pr_err("%s: csk NULL, op 0x%x.\n", __func__, op);
		goto out;
	}

	if (csk->lro_skb)
		goto add_packet;

start_lro:
	if (lro_mgr->lro_session_cnt >= MAX_LRO_SESSIONS) {
		cxgbit_uld_lro_flush(lro_mgr);
		goto start_lro;
	}

	skb = cxgbit_lro_init_skb(csk, op, gl, rsp, napi);
	if (unlikely(!skb))
		goto out;

	csk->lro_skb = skb;

	__skb_queue_tail(&lro_mgr->lroq, skb);
	lro_mgr->lro_session_cnt++;

add_packet:
	skb = csk->lro_skb;
	lro_cb = cxgbit_skb_lro_cb(skb);

	if ((gl && (((skb_shinfo(skb)->nr_frags + gl->nfrags) >
	    MAX_SKB_FRAGS) || (lro_cb->pdu_totallen >= LRO_FLUSH_LEN_MAX))) ||
	    (lro_cb->pdu_idx >= MAX_SKB_FRAGS)) {
		cxgbit_lro_flush(lro_mgr, skb);
		goto start_lro;
	}

	if (gl)
		cxgbit_lro_add_packet_gl(skb, op, gl);
	else
		cxgbit_lro_add_packet_rsp(skb, op, rsp);

	lro_mgr->lro_merged++;

	return 0;

out:
	return -1;
}

static int
cxgbit_uld_lro_rx_handler(void *hndl, const __be64 *rsp,
			  const struct pkt_gl *gl, struct t4_lro_mgr *lro_mgr,
			  struct napi_struct *napi)
{
	struct cxgbit_device *cdev = hndl;
	struct cxgb4_lld_info *lldi = &cdev->lldi;
	struct cpl_tx_data *rpl = NULL;
	struct cxgbit_sock *csk = NULL;
	unsigned int tid = 0;
	struct sk_buff *skb;
	unsigned int op = *(u8 *)rsp;
	bool lro_flush = true;

	switch (op) {
	case CPL_ISCSI_HDR:
	case CPL_ISCSI_DATA:
	case CPL_RX_ISCSI_CMP:
	case CPL_RX_ISCSI_DDP:
	case CPL_FW4_ACK:
		lro_flush = false;
	case CPL_ABORT_RPL_RSS:
	case CPL_PASS_ESTABLISH:
	case CPL_PEER_CLOSE:
	case CPL_CLOSE_CON_RPL:
	case CPL_ABORT_REQ_RSS:
	case CPL_SET_TCB_RPL:
	case CPL_RX_DATA:
		rpl = gl ? (struct cpl_tx_data *)gl->va :
			   (struct cpl_tx_data *)(rsp + 1);
		tid = GET_TID(rpl);
		csk = lookup_tid(lldi->tids, tid);
		break;
	default:
		break;
	}

	if (csk && csk->lro_skb && lro_flush)
		cxgbit_lro_flush(lro_mgr, csk->lro_skb);

	if (!gl) {
		unsigned int len;

		if (op == CPL_RX_ISCSI_DDP) {
			if (!cxgbit_lro_receive(csk, op, rsp, NULL, lro_mgr,
						napi))
				return 0;
		}

		len = 64 - sizeof(struct rsp_ctrl) - 8;
		skb = napi_alloc_skb(napi, len);
		if (!skb)
			goto nomem;
		__skb_put(skb, len);
		skb_copy_to_linear_data(skb, &rsp[1], len);
	} else {
		if (unlikely(op != *(u8 *)gl->va)) {
			pr_info("? FL 0x%p,RSS%#llx,FL %#llx,len %u.\n",
				gl->va, be64_to_cpu(*rsp),
				get_unaligned_be64(gl->va),
				gl->tot_len);
			return 0;
		}

		if ((op == CPL_ISCSI_HDR) || (op == CPL_ISCSI_DATA) ||
		    (op == CPL_RX_ISCSI_CMP)) {
			if (!cxgbit_lro_receive(csk, op, rsp, gl, lro_mgr,
						napi))
				return 0;
		}

#define RX_PULL_LEN 128
		skb = cxgb4_pktgl_to_skb(gl, RX_PULL_LEN, RX_PULL_LEN);
		if (unlikely(!skb))
			goto nomem;
	}

	rpl = (struct cpl_tx_data *)skb->data;
	op = rpl->ot.opcode;
	cxgbit_skcb_rx_opcode(skb) = op;

	pr_debug("cdev %p, opcode 0x%x(0x%x,0x%x), skb %p.\n",
		 cdev, op, rpl->ot.opcode_tid,
		 ntohl(rpl->ot.opcode_tid), skb);

	if (op < NUM_CPL_CMDS && cxgbit_cplhandlers[op]) {
		cxgbit_cplhandlers[op](cdev, skb);
	} else {
		pr_err("No handler for opcode 0x%x.\n", op);
		__kfree_skb(skb);
	}
	return 0;
nomem:
	pr_err("%s OOM bailing out.\n", __func__);
	return 1;
}

#ifdef CONFIG_CHELSIO_T4_DCB
struct cxgbit_dcb_work {
	struct dcb_app_type dcb_app;
	struct work_struct work;
};

static void
cxgbit_update_dcb_priority(struct cxgbit_device *cdev, u8 port_id,
			   u8 dcb_priority, u16 port_num)
{
	struct cxgbit_sock *csk;
	struct sk_buff *skb;
	u16 local_port;
	bool wakeup_thread = false;

	spin_lock_bh(&cdev->cskq.lock);
	list_for_each_entry(csk, &cdev->cskq.list, list) {
		if (csk->port_id != port_id)
			continue;

		if (csk->com.local_addr.ss_family == AF_INET6) {
			struct sockaddr_in6 *sock_in6;

			sock_in6 = (struct sockaddr_in6 *)&csk->com.local_addr;
			local_port = ntohs(sock_in6->sin6_port);
		} else {
			struct sockaddr_in *sock_in;

			sock_in = (struct sockaddr_in *)&csk->com.local_addr;
			local_port = ntohs(sock_in->sin_port);
		}

		if (local_port != port_num)
			continue;

		if (csk->dcb_priority == dcb_priority)
			continue;

		skb = alloc_skb(0, GFP_ATOMIC);
		if (!skb)
			continue;

		spin_lock(&csk->rxq.lock);
		__skb_queue_tail(&csk->rxq, skb);
		if (skb_queue_len(&csk->rxq) == 1)
			wakeup_thread = true;
		spin_unlock(&csk->rxq.lock);

		if (wakeup_thread) {
			wake_up(&csk->waitq);
			wakeup_thread = false;
		}
	}
	spin_unlock_bh(&cdev->cskq.lock);
}

static void cxgbit_dcb_workfn(struct work_struct *work)
{
	struct cxgbit_dcb_work *dcb_work;
	struct net_device *ndev;
	struct cxgbit_device *cdev = NULL;
	struct dcb_app_type *iscsi_app;
	u8 priority, port_id = 0xff;

	dcb_work = container_of(work, struct cxgbit_dcb_work, work);
	iscsi_app = &dcb_work->dcb_app;

	if (iscsi_app->dcbx & DCB_CAP_DCBX_VER_IEEE) {
		if (iscsi_app->app.selector != IEEE_8021QAZ_APP_SEL_ANY)
			goto out;

		priority = iscsi_app->app.priority;

	} else if (iscsi_app->dcbx & DCB_CAP_DCBX_VER_CEE) {
		if (iscsi_app->app.selector != DCB_APP_IDTYPE_PORTNUM)
			goto out;

		if (!iscsi_app->app.priority)
			goto out;

		priority = ffs(iscsi_app->app.priority) - 1;
	} else {
		goto out;
	}

	pr_debug("priority for ifid %d is %u\n",
		 iscsi_app->ifindex, priority);

	ndev = dev_get_by_index(&init_net, iscsi_app->ifindex);

	if (!ndev)
		goto out;

	mutex_lock(&cdev_list_lock);
	cdev = cxgbit_find_device(ndev, &port_id);

	dev_put(ndev);

	if (!cdev) {
		mutex_unlock(&cdev_list_lock);
		goto out;
	}

	cxgbit_update_dcb_priority(cdev, port_id, priority,
				   iscsi_app->app.protocol);
	mutex_unlock(&cdev_list_lock);
out:
	kfree(dcb_work);
}

static int
cxgbit_dcbevent_notify(struct notifier_block *nb, unsigned long action,
		       void *data)
{
	struct cxgbit_dcb_work *dcb_work;
	struct dcb_app_type *dcb_app = data;

	dcb_work = kzalloc(sizeof(*dcb_work), GFP_ATOMIC);
	if (!dcb_work)
		return NOTIFY_DONE;

	dcb_work->dcb_app = *dcb_app;
	INIT_WORK(&dcb_work->work, cxgbit_dcb_workfn);
	schedule_work(&dcb_work->work);
	return NOTIFY_OK;
}
#endif

static enum target_prot_op cxgbit_get_sup_prot_ops(struct iscsi_conn *conn)
{
	return TARGET_PROT_NORMAL;
}

static struct iscsit_transport cxgbit_transport = {
	.name			= DRV_NAME,
	.transport_type		= ISCSI_CXGBIT,
	.rdma_shutdown		= false,
	.priv_size		= sizeof(struct cxgbit_cmd),
	.owner			= THIS_MODULE,
	.iscsit_setup_np	= cxgbit_setup_np,
	.iscsit_accept_np	= cxgbit_accept_np,
	.iscsit_free_np		= cxgbit_free_np,
	.iscsit_free_conn	= cxgbit_free_conn,
	.iscsit_get_login_rx	= cxgbit_get_login_rx,
	.iscsit_put_login_tx	= cxgbit_put_login_tx,
	.iscsit_immediate_queue	= iscsit_immediate_queue,
	.iscsit_response_queue	= iscsit_response_queue,
	.iscsit_get_dataout	= iscsit_build_r2ts_for_cmd,
	.iscsit_queue_data_in	= iscsit_queue_rsp,
	.iscsit_queue_status	= iscsit_queue_rsp,
	.iscsit_xmit_pdu	= cxgbit_xmit_pdu,
	.iscsit_get_r2t_ttt	= cxgbit_get_r2t_ttt,
	.iscsit_get_rx_pdu	= cxgbit_get_rx_pdu,
	.iscsit_validate_params	= cxgbit_validate_params,
	.iscsit_release_cmd	= cxgbit_release_cmd,
	.iscsit_aborted_task	= iscsit_aborted_task,
	.iscsit_get_sup_prot_ops = cxgbit_get_sup_prot_ops,
};

static struct cxgb4_uld_info cxgbit_uld_info = {
	.name		= DRV_NAME,
	.nrxq		= MAX_ULD_QSETS,
	.ntxq		= MAX_ULD_QSETS,
	.rxq_size	= 1024,
	.lro		= true,
	.add		= cxgbit_uld_add,
	.state_change	= cxgbit_uld_state_change,
	.lro_rx_handler = cxgbit_uld_lro_rx_handler,
	.lro_flush	= cxgbit_uld_lro_flush,
};

#ifdef CONFIG_CHELSIO_T4_DCB
static struct notifier_block cxgbit_dcbevent_nb = {
	.notifier_call = cxgbit_dcbevent_notify,
};
#endif

static int __init cxgbit_init(void)
{
	cxgb4_register_uld(CXGB4_ULD_ISCSIT, &cxgbit_uld_info);
	iscsit_register_transport(&cxgbit_transport);

#ifdef CONFIG_CHELSIO_T4_DCB
	pr_info("%s dcb enabled.\n", DRV_NAME);
	register_dcbevent_notifier(&cxgbit_dcbevent_nb);
#endif
	BUILD_BUG_ON(FIELD_SIZEOF(struct sk_buff, cb) <
		     sizeof(union cxgbit_skb_cb));
	return 0;
}

static void __exit cxgbit_exit(void)
{
	struct cxgbit_device *cdev, *tmp;

#ifdef CONFIG_CHELSIO_T4_DCB
	unregister_dcbevent_notifier(&cxgbit_dcbevent_nb);
#endif
	mutex_lock(&cdev_list_lock);
	list_for_each_entry_safe(cdev, tmp, &cdev_list_head, list) {
		list_del(&cdev->list);
		cxgbit_put_cdev(cdev);
	}
	mutex_unlock(&cdev_list_lock);
	iscsit_unregister_transport(&cxgbit_transport);
	cxgb4_unregister_uld(CXGB4_ULD_ISCSIT);
}

module_init(cxgbit_init);
module_exit(cxgbit_exit);

MODULE_DESCRIPTION("Chelsio iSCSI target offload driver");
MODULE_AUTHOR("Chelsio Communications");
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
