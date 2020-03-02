// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Physcial Function ethernet driver
 *
 * Copyright (C) 2020 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/etherdevice.h>
#include <linux/of.h>
#include <linux/if_vlan.h>
#include <linux/iommu.h>
#include <net/ip.h>

#include "otx2_reg.h"
#include "otx2_common.h"
#include "otx2_txrx.h"
#include "otx2_struct.h"

#define DRV_NAME	"octeontx2-nicpf"
#define DRV_STRING	"Marvell OcteonTX2 NIC Physical Function Driver"
#define DRV_VERSION	"1.0"

/* Supported devices */
static const struct pci_device_id otx2_pf_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_OCTEONTX2_RVU_PF) },
	{ 0, }  /* end of table */
};

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION(DRV_STRING);
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, otx2_pf_id_table);

enum {
	TYPE_PFAF,
	TYPE_PFVF,
};

static int otx2_change_mtu(struct net_device *netdev, int new_mtu)
{
	bool if_up = netif_running(netdev);
	int err = 0;

	if (if_up)
		otx2_stop(netdev);

	netdev_info(netdev, "Changing MTU from %d to %d\n",
		    netdev->mtu, new_mtu);
	netdev->mtu = new_mtu;

	if (if_up)
		err = otx2_open(netdev);

	return err;
}

static void otx2_queue_work(struct mbox *mw, struct workqueue_struct *mbox_wq,
			    int first, int mdevs, u64 intr, int type)
{
	struct otx2_mbox_dev *mdev;
	struct otx2_mbox *mbox;
	struct mbox_hdr *hdr;
	int i;

	for (i = first; i < mdevs; i++) {
		/* start from 0 */
		if (!(intr & BIT_ULL(i - first)))
			continue;

		mbox = &mw->mbox;
		mdev = &mbox->dev[i];
		if (type == TYPE_PFAF)
			otx2_sync_mbox_bbuf(mbox, i);
		hdr = mdev->mbase + mbox->rx_start;
		/* The hdr->num_msgs is set to zero immediately in the interrupt
		 * handler to  ensure that it holds a correct value next time
		 * when the interrupt handler is called.
		 * pf->mbox.num_msgs holds the data for use in pfaf_mbox_handler
		 * pf>mbox.up_num_msgs holds the data for use in
		 * pfaf_mbox_up_handler.
		 */
		if (hdr->num_msgs) {
			mw[i].num_msgs = hdr->num_msgs;
			hdr->num_msgs = 0;
			if (type == TYPE_PFAF)
				memset(mbox->hwbase + mbox->rx_start, 0,
				       ALIGN(sizeof(struct mbox_hdr),
					     sizeof(u64)));

			queue_work(mbox_wq, &mw[i].mbox_wrk);
		}

		mbox = &mw->mbox_up;
		mdev = &mbox->dev[i];
		if (type == TYPE_PFAF)
			otx2_sync_mbox_bbuf(mbox, i);
		hdr = mdev->mbase + mbox->rx_start;
		if (hdr->num_msgs) {
			mw[i].up_num_msgs = hdr->num_msgs;
			hdr->num_msgs = 0;
			if (type == TYPE_PFAF)
				memset(mbox->hwbase + mbox->rx_start, 0,
				       ALIGN(sizeof(struct mbox_hdr),
					     sizeof(u64)));

			queue_work(mbox_wq, &mw[i].mbox_up_wrk);
		}
	}
}

static void otx2_process_pfaf_mbox_msg(struct otx2_nic *pf,
				       struct mbox_msghdr *msg)
{
	if (msg->id >= MBOX_MSG_MAX) {
		dev_err(pf->dev,
			"Mbox msg with unknown ID 0x%x\n", msg->id);
		return;
	}

	if (msg->sig != OTX2_MBOX_RSP_SIG) {
		dev_err(pf->dev,
			"Mbox msg with wrong signature %x, ID 0x%x\n",
			 msg->sig, msg->id);
		return;
	}

	switch (msg->id) {
	case MBOX_MSG_READY:
		pf->pcifunc = msg->pcifunc;
		break;
	case MBOX_MSG_MSIX_OFFSET:
		mbox_handler_msix_offset(pf, (struct msix_offset_rsp *)msg);
		break;
	case MBOX_MSG_NPA_LF_ALLOC:
		mbox_handler_npa_lf_alloc(pf, (struct npa_lf_alloc_rsp *)msg);
		break;
	case MBOX_MSG_NIX_LF_ALLOC:
		mbox_handler_nix_lf_alloc(pf, (struct nix_lf_alloc_rsp *)msg);
		break;
	case MBOX_MSG_NIX_TXSCH_ALLOC:
		mbox_handler_nix_txsch_alloc(pf,
					     (struct nix_txsch_alloc_rsp *)msg);
		break;
	case MBOX_MSG_NIX_BP_ENABLE:
		mbox_handler_nix_bp_enable(pf, (struct nix_bp_cfg_rsp *)msg);
		break;
	case MBOX_MSG_CGX_STATS:
		mbox_handler_cgx_stats(pf, (struct cgx_stats_rsp *)msg);
		break;
	default:
		if (msg->rc)
			dev_err(pf->dev,
				"Mbox msg response has err %d, ID 0x%x\n",
				msg->rc, msg->id);
		break;
	}
}

static void otx2_pfaf_mbox_handler(struct work_struct *work)
{
	struct otx2_mbox_dev *mdev;
	struct mbox_hdr *rsp_hdr;
	struct mbox_msghdr *msg;
	struct otx2_mbox *mbox;
	struct mbox *af_mbox;
	struct otx2_nic *pf;
	int offset, id;

	af_mbox = container_of(work, struct mbox, mbox_wrk);
	mbox = &af_mbox->mbox;
	mdev = &mbox->dev[0];
	rsp_hdr = (struct mbox_hdr *)(mdev->mbase + mbox->rx_start);

	offset = mbox->rx_start + ALIGN(sizeof(*rsp_hdr), MBOX_MSG_ALIGN);
	pf = af_mbox->pfvf;

	for (id = 0; id < af_mbox->num_msgs; id++) {
		msg = (struct mbox_msghdr *)(mdev->mbase + offset);
		otx2_process_pfaf_mbox_msg(pf, msg);
		offset = mbox->rx_start + msg->next_msgoff;
		mdev->msgs_acked++;
	}

	otx2_mbox_reset(mbox, 0);
}

static void otx2_handle_link_event(struct otx2_nic *pf)
{
	struct cgx_link_user_info *linfo = &pf->linfo;
	struct net_device *netdev = pf->netdev;

	pr_info("%s NIC Link is %s %d Mbps %s duplex\n", netdev->name,
		linfo->link_up ? "UP" : "DOWN", linfo->speed,
		linfo->full_duplex ? "Full" : "Half");
	if (linfo->link_up) {
		netif_carrier_on(netdev);
		netif_tx_start_all_queues(netdev);
	} else {
		netif_tx_stop_all_queues(netdev);
		netif_carrier_off(netdev);
	}
}

int otx2_mbox_up_handler_cgx_link_event(struct otx2_nic *pf,
					struct cgx_link_info_msg *msg,
					struct msg_rsp *rsp)
{
	/* Copy the link info sent by AF */
	pf->linfo = msg->link_info;

	/* interface has not been fully configured yet */
	if (pf->flags & OTX2_FLAG_INTF_DOWN)
		return 0;

	otx2_handle_link_event(pf);
	return 0;
}

static int otx2_process_mbox_msg_up(struct otx2_nic *pf,
				    struct mbox_msghdr *req)
{
	/* Check if valid, if not reply with a invalid msg */
	if (req->sig != OTX2_MBOX_REQ_SIG) {
		otx2_reply_invalid_msg(&pf->mbox.mbox_up, 0, 0, req->id);
		return -ENODEV;
	}

	switch (req->id) {
#define M(_name, _id, _fn_name, _req_type, _rsp_type)			\
	case _id: {							\
		struct _rsp_type *rsp;					\
		int err;						\
									\
		rsp = (struct _rsp_type *)otx2_mbox_alloc_msg(		\
			&pf->mbox.mbox_up, 0,				\
			sizeof(struct _rsp_type));			\
		if (!rsp)						\
			return -ENOMEM;					\
									\
		rsp->hdr.id = _id;					\
		rsp->hdr.sig = OTX2_MBOX_RSP_SIG;			\
		rsp->hdr.pcifunc = 0;					\
		rsp->hdr.rc = 0;					\
									\
		err = otx2_mbox_up_handler_ ## _fn_name(		\
			pf, (struct _req_type *)req, rsp);		\
		return err;						\
	}
MBOX_UP_CGX_MESSAGES
#undef M
		break;
	default:
		otx2_reply_invalid_msg(&pf->mbox.mbox_up, 0, 0, req->id);
		return -ENODEV;
	}
	return 0;
}

static void otx2_pfaf_mbox_up_handler(struct work_struct *work)
{
	struct mbox *af_mbox = container_of(work, struct mbox, mbox_up_wrk);
	struct otx2_mbox *mbox = &af_mbox->mbox_up;
	struct otx2_mbox_dev *mdev = &mbox->dev[0];
	struct otx2_nic *pf = af_mbox->pfvf;
	int offset, id, devid = 0;
	struct mbox_hdr *rsp_hdr;
	struct mbox_msghdr *msg;

	rsp_hdr = (struct mbox_hdr *)(mdev->mbase + mbox->rx_start);

	offset = mbox->rx_start + ALIGN(sizeof(*rsp_hdr), MBOX_MSG_ALIGN);

	for (id = 0; id < af_mbox->up_num_msgs; id++) {
		msg = (struct mbox_msghdr *)(mdev->mbase + offset);

		devid = msg->pcifunc & RVU_PFVF_FUNC_MASK;
		/* Skip processing VF's messages */
		if (!devid)
			otx2_process_mbox_msg_up(pf, msg);
		offset = mbox->rx_start + msg->next_msgoff;
	}

	otx2_mbox_msg_send(mbox, 0);
}

static irqreturn_t otx2_pfaf_mbox_intr_handler(int irq, void *pf_irq)
{
	struct otx2_nic *pf = (struct otx2_nic *)pf_irq;
	struct mbox *mbox;

	/* Clear the IRQ */
	otx2_write64(pf, RVU_PF_INT, BIT_ULL(0));

	mbox = &pf->mbox;
	otx2_queue_work(mbox, pf->mbox_wq, 0, 1, 1, TYPE_PFAF);

	return IRQ_HANDLED;
}

static void otx2_disable_mbox_intr(struct otx2_nic *pf)
{
	int vector = pci_irq_vector(pf->pdev, RVU_PF_INT_VEC_AFPF_MBOX);

	/* Disable AF => PF mailbox IRQ */
	otx2_write64(pf, RVU_PF_INT_ENA_W1C, BIT_ULL(0));
	free_irq(vector, pf);
}

static int otx2_register_mbox_intr(struct otx2_nic *pf, bool probe_af)
{
	struct otx2_hw *hw = &pf->hw;
	struct msg_req *req;
	char *irq_name;
	int err;

	/* Register mailbox interrupt handler */
	irq_name = &hw->irq_name[RVU_PF_INT_VEC_AFPF_MBOX * NAME_SIZE];
	snprintf(irq_name, NAME_SIZE, "RVUPFAF Mbox");
	err = request_irq(pci_irq_vector(pf->pdev, RVU_PF_INT_VEC_AFPF_MBOX),
			  otx2_pfaf_mbox_intr_handler, 0, irq_name, pf);
	if (err) {
		dev_err(pf->dev,
			"RVUPF: IRQ registration failed for PFAF mbox irq\n");
		return err;
	}

	/* Enable mailbox interrupt for msgs coming from AF.
	 * First clear to avoid spurious interrupts, if any.
	 */
	otx2_write64(pf, RVU_PF_INT, BIT_ULL(0));
	otx2_write64(pf, RVU_PF_INT_ENA_W1S, BIT_ULL(0));

	if (!probe_af)
		return 0;

	/* Check mailbox communication with AF */
	req = otx2_mbox_alloc_msg_ready(&pf->mbox);
	if (!req) {
		otx2_disable_mbox_intr(pf);
		return -ENOMEM;
	}
	err = otx2_sync_mbox_msg(&pf->mbox);
	if (err) {
		dev_warn(pf->dev,
			 "AF not responding to mailbox, deferring probe\n");
		otx2_disable_mbox_intr(pf);
		return -EPROBE_DEFER;
	}

	return 0;
}

static void otx2_pfaf_mbox_destroy(struct otx2_nic *pf)
{
	struct mbox *mbox = &pf->mbox;

	if (pf->mbox_wq) {
		flush_workqueue(pf->mbox_wq);
		destroy_workqueue(pf->mbox_wq);
		pf->mbox_wq = NULL;
	}

	if (mbox->mbox.hwbase)
		iounmap((void __iomem *)mbox->mbox.hwbase);

	otx2_mbox_destroy(&mbox->mbox);
	otx2_mbox_destroy(&mbox->mbox_up);
}

static int otx2_pfaf_mbox_init(struct otx2_nic *pf)
{
	struct mbox *mbox = &pf->mbox;
	void __iomem *hwbase;
	int err;

	mbox->pfvf = pf;
	pf->mbox_wq = alloc_workqueue("otx2_pfaf_mailbox",
				      WQ_UNBOUND | WQ_HIGHPRI |
				      WQ_MEM_RECLAIM, 1);
	if (!pf->mbox_wq)
		return -ENOMEM;

	/* Mailbox is a reserved memory (in RAM) region shared between
	 * admin function (i.e AF) and this PF, shouldn't be mapped as
	 * device memory to allow unaligned accesses.
	 */
	hwbase = ioremap_wc(pci_resource_start(pf->pdev, PCI_MBOX_BAR_NUM),
			    pci_resource_len(pf->pdev, PCI_MBOX_BAR_NUM));
	if (!hwbase) {
		dev_err(pf->dev, "Unable to map PFAF mailbox region\n");
		err = -ENOMEM;
		goto exit;
	}

	err = otx2_mbox_init(&mbox->mbox, hwbase, pf->pdev, pf->reg_base,
			     MBOX_DIR_PFAF, 1);
	if (err)
		goto exit;

	err = otx2_mbox_init(&mbox->mbox_up, hwbase, pf->pdev, pf->reg_base,
			     MBOX_DIR_PFAF_UP, 1);
	if (err)
		goto exit;

	err = otx2_mbox_bbuf_init(mbox, pf->pdev);
	if (err)
		goto exit;

	INIT_WORK(&mbox->mbox_wrk, otx2_pfaf_mbox_handler);
	INIT_WORK(&mbox->mbox_up_wrk, otx2_pfaf_mbox_up_handler);
	otx2_mbox_lock_init(&pf->mbox);

	return 0;
exit:
	otx2_pfaf_mbox_destroy(pf);
	return err;
}

static int otx2_cgx_config_linkevents(struct otx2_nic *pf, bool enable)
{
	struct msg_req *msg;
	int err;

	otx2_mbox_lock(&pf->mbox);
	if (enable)
		msg = otx2_mbox_alloc_msg_cgx_start_linkevents(&pf->mbox);
	else
		msg = otx2_mbox_alloc_msg_cgx_stop_linkevents(&pf->mbox);

	if (!msg) {
		otx2_mbox_unlock(&pf->mbox);
		return -ENOMEM;
	}

	err = otx2_sync_mbox_msg(&pf->mbox);
	otx2_mbox_unlock(&pf->mbox);
	return err;
}

static int otx2_cgx_config_loopback(struct otx2_nic *pf, bool enable)
{
	struct msg_req *msg;
	int err;

	otx2_mbox_lock(&pf->mbox);
	if (enable)
		msg = otx2_mbox_alloc_msg_cgx_intlbk_enable(&pf->mbox);
	else
		msg = otx2_mbox_alloc_msg_cgx_intlbk_disable(&pf->mbox);

	if (!msg) {
		otx2_mbox_unlock(&pf->mbox);
		return -ENOMEM;
	}

	err = otx2_sync_mbox_msg(&pf->mbox);
	otx2_mbox_unlock(&pf->mbox);
	return err;
}

int otx2_set_real_num_queues(struct net_device *netdev,
			     int tx_queues, int rx_queues)
{
	int err;

	err = netif_set_real_num_tx_queues(netdev, tx_queues);
	if (err) {
		netdev_err(netdev,
			   "Failed to set no of Tx queues: %d\n", tx_queues);
		return err;
	}

	err = netif_set_real_num_rx_queues(netdev, rx_queues);
	if (err)
		netdev_err(netdev,
			   "Failed to set no of Rx queues: %d\n", rx_queues);
	return err;
}

static irqreturn_t otx2_q_intr_handler(int irq, void *data)
{
	struct otx2_nic *pf = data;
	u64 val, *ptr;
	u64 qidx = 0;

	/* CQ */
	for (qidx = 0; qidx < pf->qset.cq_cnt; qidx++) {
		ptr = otx2_get_regaddr(pf, NIX_LF_CQ_OP_INT);
		val = otx2_atomic64_add((qidx << 44), ptr);

		otx2_write64(pf, NIX_LF_CQ_OP_INT, (qidx << 44) |
			     (val & NIX_CQERRINT_BITS));
		if (!(val & (NIX_CQERRINT_BITS | BIT_ULL(42))))
			continue;

		if (val & BIT_ULL(42)) {
			netdev_err(pf->netdev, "CQ%lld: error reading NIX_LF_CQ_OP_INT, NIX_LF_ERR_INT 0x%llx\n",
				   qidx, otx2_read64(pf, NIX_LF_ERR_INT));
		} else {
			if (val & BIT_ULL(NIX_CQERRINT_DOOR_ERR))
				netdev_err(pf->netdev, "CQ%lld: Doorbell error",
					   qidx);
			if (val & BIT_ULL(NIX_CQERRINT_CQE_FAULT))
				netdev_err(pf->netdev, "CQ%lld: Memory fault on CQE write to LLC/DRAM",
					   qidx);
		}

		schedule_work(&pf->reset_task);
	}

	/* SQ */
	for (qidx = 0; qidx < pf->hw.tx_queues; qidx++) {
		ptr = otx2_get_regaddr(pf, NIX_LF_SQ_OP_INT);
		val = otx2_atomic64_add((qidx << 44), ptr);
		otx2_write64(pf, NIX_LF_SQ_OP_INT, (qidx << 44) |
			     (val & NIX_SQINT_BITS));

		if (!(val & (NIX_SQINT_BITS | BIT_ULL(42))))
			continue;

		if (val & BIT_ULL(42)) {
			netdev_err(pf->netdev, "SQ%lld: error reading NIX_LF_SQ_OP_INT, NIX_LF_ERR_INT 0x%llx\n",
				   qidx, otx2_read64(pf, NIX_LF_ERR_INT));
		} else {
			if (val & BIT_ULL(NIX_SQINT_LMT_ERR)) {
				netdev_err(pf->netdev, "SQ%lld: LMT store error NIX_LF_SQ_OP_ERR_DBG:0x%llx",
					   qidx,
					   otx2_read64(pf,
						       NIX_LF_SQ_OP_ERR_DBG));
				otx2_write64(pf, NIX_LF_SQ_OP_ERR_DBG,
					     BIT_ULL(44));
			}
			if (val & BIT_ULL(NIX_SQINT_MNQ_ERR)) {
				netdev_err(pf->netdev, "SQ%lld: Meta-descriptor enqueue error NIX_LF_MNQ_ERR_DGB:0x%llx\n",
					   qidx,
					   otx2_read64(pf, NIX_LF_MNQ_ERR_DBG));
				otx2_write64(pf, NIX_LF_MNQ_ERR_DBG,
					     BIT_ULL(44));
			}
			if (val & BIT_ULL(NIX_SQINT_SEND_ERR)) {
				netdev_err(pf->netdev, "SQ%lld: Send error, NIX_LF_SEND_ERR_DBG 0x%llx",
					   qidx,
					   otx2_read64(pf,
						       NIX_LF_SEND_ERR_DBG));
				otx2_write64(pf, NIX_LF_SEND_ERR_DBG,
					     BIT_ULL(44));
			}
			if (val & BIT_ULL(NIX_SQINT_SQB_ALLOC_FAIL))
				netdev_err(pf->netdev, "SQ%lld: SQB allocation failed",
					   qidx);
		}

		schedule_work(&pf->reset_task);
	}

	return IRQ_HANDLED;
}

static irqreturn_t otx2_cq_intr_handler(int irq, void *cq_irq)
{
	struct otx2_cq_poll *cq_poll = (struct otx2_cq_poll *)cq_irq;
	struct otx2_nic *pf = (struct otx2_nic *)cq_poll->dev;
	int qidx = cq_poll->cint_idx;

	/* Disable interrupts.
	 *
	 * Completion interrupts behave in a level-triggered interrupt
	 * fashion, and hence have to be cleared only after it is serviced.
	 */
	otx2_write64(pf, NIX_LF_CINTX_ENA_W1C(qidx), BIT_ULL(0));

	/* Schedule NAPI */
	napi_schedule_irqoff(&cq_poll->napi);

	return IRQ_HANDLED;
}

static void otx2_disable_napi(struct otx2_nic *pf)
{
	struct otx2_qset *qset = &pf->qset;
	struct otx2_cq_poll *cq_poll;
	int qidx;

	for (qidx = 0; qidx < pf->hw.cint_cnt; qidx++) {
		cq_poll = &qset->napi[qidx];
		napi_disable(&cq_poll->napi);
		netif_napi_del(&cq_poll->napi);
	}
}

static void otx2_free_cq_res(struct otx2_nic *pf)
{
	struct otx2_qset *qset = &pf->qset;
	struct otx2_cq_queue *cq;
	int qidx;

	/* Disable CQs */
	otx2_ctx_disable(&pf->mbox, NIX_AQ_CTYPE_CQ, false);
	for (qidx = 0; qidx < qset->cq_cnt; qidx++) {
		cq = &qset->cq[qidx];
		qmem_free(pf->dev, cq->cqe);
	}
}

static void otx2_free_sq_res(struct otx2_nic *pf)
{
	struct otx2_qset *qset = &pf->qset;
	struct otx2_snd_queue *sq;
	int qidx;

	/* Disable SQs */
	otx2_ctx_disable(&pf->mbox, NIX_AQ_CTYPE_SQ, false);
	/* Free SQB pointers */
	otx2_sq_free_sqbs(pf);
	for (qidx = 0; qidx < pf->hw.tx_queues; qidx++) {
		sq = &qset->sq[qidx];
		qmem_free(pf->dev, sq->sqe);
		qmem_free(pf->dev, sq->tso_hdrs);
		kfree(sq->sg);
		kfree(sq->sqb_ptrs);
	}
}

static int otx2_init_hw_resources(struct otx2_nic *pf)
{
	struct mbox *mbox = &pf->mbox;
	struct otx2_hw *hw = &pf->hw;
	struct msg_req *req;
	int err = 0, lvl;

	/* Set required NPA LF's pool counts
	 * Auras and Pools are used in a 1:1 mapping,
	 * so, aura count = pool count.
	 */
	hw->rqpool_cnt = hw->rx_queues;
	hw->sqpool_cnt = hw->tx_queues;
	hw->pool_cnt = hw->rqpool_cnt + hw->sqpool_cnt;

	/* Get the size of receive buffers to allocate */
	pf->rbsize = RCV_FRAG_LEN(pf->netdev->mtu + OTX2_ETH_HLEN);

	otx2_mbox_lock(mbox);
	/* NPA init */
	err = otx2_config_npa(pf);
	if (err)
		goto exit;

	/* NIX init */
	err = otx2_config_nix(pf);
	if (err)
		goto err_free_npa_lf;

	/* Enable backpressure */
	otx2_nix_config_bp(pf, true);

	/* Init Auras and pools used by NIX RQ, for free buffer ptrs */
	err = otx2_rq_aura_pool_init(pf);
	if (err) {
		otx2_mbox_unlock(mbox);
		goto err_free_nix_lf;
	}
	/* Init Auras and pools used by NIX SQ, for queueing SQEs */
	err = otx2_sq_aura_pool_init(pf);
	if (err) {
		otx2_mbox_unlock(mbox);
		goto err_free_rq_ptrs;
	}

	err = otx2_txsch_alloc(pf);
	if (err) {
		otx2_mbox_unlock(mbox);
		goto err_free_sq_ptrs;
	}

	err = otx2_config_nix_queues(pf);
	if (err) {
		otx2_mbox_unlock(mbox);
		goto err_free_txsch;
	}
	for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++) {
		err = otx2_txschq_config(pf, lvl);
		if (err) {
			otx2_mbox_unlock(mbox);
			goto err_free_nix_queues;
		}
	}
	otx2_mbox_unlock(mbox);
	return err;

err_free_nix_queues:
	otx2_free_sq_res(pf);
	otx2_free_cq_res(pf);
	otx2_ctx_disable(mbox, NIX_AQ_CTYPE_RQ, false);
err_free_txsch:
	if (otx2_txschq_stop(pf))
		dev_err(pf->dev, "%s failed to stop TX schedulers\n", __func__);
err_free_sq_ptrs:
	otx2_sq_free_sqbs(pf);
err_free_rq_ptrs:
	otx2_free_aura_ptr(pf, AURA_NIX_RQ);
	otx2_ctx_disable(mbox, NPA_AQ_CTYPE_POOL, true);
	otx2_ctx_disable(mbox, NPA_AQ_CTYPE_AURA, true);
	otx2_aura_pool_free(pf);
err_free_nix_lf:
	otx2_mbox_lock(mbox);
	req = otx2_mbox_alloc_msg_nix_lf_free(mbox);
	if (req) {
		if (otx2_sync_mbox_msg(mbox))
			dev_err(pf->dev, "%s failed to free nixlf\n", __func__);
	}
err_free_npa_lf:
	/* Reset NPA LF */
	req = otx2_mbox_alloc_msg_npa_lf_free(mbox);
	if (req) {
		if (otx2_sync_mbox_msg(mbox))
			dev_err(pf->dev, "%s failed to free npalf\n", __func__);
	}
exit:
	otx2_mbox_unlock(mbox);
	return err;
}

static void otx2_free_hw_resources(struct otx2_nic *pf)
{
	struct otx2_qset *qset = &pf->qset;
	struct mbox *mbox = &pf->mbox;
	struct otx2_cq_queue *cq;
	struct msg_req *req;
	int qidx, err;

	/* Ensure all SQE are processed */
	otx2_sqb_flush(pf);

	/* Stop transmission */
	err = otx2_txschq_stop(pf);
	if (err)
		dev_err(pf->dev, "RVUPF: Failed to stop/free TX schedulers\n");

	otx2_mbox_lock(mbox);
	/* Disable backpressure */
	if (!(pf->pcifunc & RVU_PFVF_FUNC_MASK))
		otx2_nix_config_bp(pf, false);
	otx2_mbox_unlock(mbox);

	/* Disable RQs */
	otx2_ctx_disable(mbox, NIX_AQ_CTYPE_RQ, false);

	/*Dequeue all CQEs */
	for (qidx = 0; qidx < qset->cq_cnt; qidx++) {
		cq = &qset->cq[qidx];
		if (cq->cq_type == CQ_RX)
			otx2_cleanup_rx_cqes(pf, cq);
		else
			otx2_cleanup_tx_cqes(pf, cq);
	}

	otx2_free_sq_res(pf);

	/* Free RQ buffer pointers*/
	otx2_free_aura_ptr(pf, AURA_NIX_RQ);

	otx2_free_cq_res(pf);

	otx2_mbox_lock(mbox);
	/* Reset NIX LF */
	req = otx2_mbox_alloc_msg_nix_lf_free(mbox);
	if (req) {
		if (otx2_sync_mbox_msg(mbox))
			dev_err(pf->dev, "%s failed to free nixlf\n", __func__);
	}
	otx2_mbox_unlock(mbox);

	/* Disable NPA Pool and Aura hw context */
	otx2_ctx_disable(mbox, NPA_AQ_CTYPE_POOL, true);
	otx2_ctx_disable(mbox, NPA_AQ_CTYPE_AURA, true);
	otx2_aura_pool_free(pf);

	otx2_mbox_lock(mbox);
	/* Reset NPA LF */
	req = otx2_mbox_alloc_msg_npa_lf_free(mbox);
	if (req) {
		if (otx2_sync_mbox_msg(mbox))
			dev_err(pf->dev, "%s failed to free npalf\n", __func__);
	}
	otx2_mbox_unlock(mbox);
}

int otx2_open(struct net_device *netdev)
{
	struct otx2_nic *pf = netdev_priv(netdev);
	struct otx2_cq_poll *cq_poll = NULL;
	struct otx2_qset *qset = &pf->qset;
	int err = 0, qidx, vec;
	char *irq_name;

	netif_carrier_off(netdev);

	pf->qset.cq_cnt = pf->hw.rx_queues + pf->hw.tx_queues;
	/* RQ and SQs are mapped to different CQs,
	 * so find out max CQ IRQs (i.e CINTs) needed.
	 */
	pf->hw.cint_cnt = max(pf->hw.rx_queues, pf->hw.tx_queues);
	qset->napi = kcalloc(pf->hw.cint_cnt, sizeof(*cq_poll), GFP_KERNEL);
	if (!qset->napi)
		return -ENOMEM;

	/* CQ size of RQ */
	qset->rqe_cnt = qset->rqe_cnt ? qset->rqe_cnt : Q_COUNT(Q_SIZE_256);
	/* CQ size of SQ */
	qset->sqe_cnt = qset->sqe_cnt ? qset->sqe_cnt : Q_COUNT(Q_SIZE_4K);

	err = -ENOMEM;
	qset->cq = kcalloc(pf->qset.cq_cnt,
			   sizeof(struct otx2_cq_queue), GFP_KERNEL);
	if (!qset->cq)
		goto err_free_mem;

	qset->sq = kcalloc(pf->hw.tx_queues,
			   sizeof(struct otx2_snd_queue), GFP_KERNEL);
	if (!qset->sq)
		goto err_free_mem;

	qset->rq = kcalloc(pf->hw.rx_queues,
			   sizeof(struct otx2_rcv_queue), GFP_KERNEL);
	if (!qset->rq)
		goto err_free_mem;

	err = otx2_init_hw_resources(pf);
	if (err)
		goto err_free_mem;

	/* Register NAPI handler */
	for (qidx = 0; qidx < pf->hw.cint_cnt; qidx++) {
		cq_poll = &qset->napi[qidx];
		cq_poll->cint_idx = qidx;
		/* RQ0 & SQ0 are mapped to CINT0 and so on..
		 * 'cq_ids[0]' points to RQ's CQ and
		 * 'cq_ids[1]' points to SQ's CQ and
		 */
		cq_poll->cq_ids[CQ_RX] =
			(qidx <  pf->hw.rx_queues) ? qidx : CINT_INVALID_CQ;
		cq_poll->cq_ids[CQ_TX] = (qidx < pf->hw.tx_queues) ?
				      qidx + pf->hw.rx_queues : CINT_INVALID_CQ;
		cq_poll->dev = (void *)pf;
		netif_napi_add(netdev, &cq_poll->napi,
			       otx2_napi_handler, NAPI_POLL_WEIGHT);
		napi_enable(&cq_poll->napi);
	}

	/* Set maximum frame size allowed in HW */
	err = otx2_hw_set_mtu(pf, netdev->mtu);
	if (err)
		goto err_disable_napi;

	/* Initialize RSS */
	err = otx2_rss_init(pf);
	if (err)
		goto err_disable_napi;

	/* Register Queue IRQ handlers */
	vec = pf->hw.nix_msixoff + NIX_LF_QINT_VEC_START;
	irq_name = &pf->hw.irq_name[vec * NAME_SIZE];

	snprintf(irq_name, NAME_SIZE, "%s-qerr", pf->netdev->name);

	err = request_irq(pci_irq_vector(pf->pdev, vec),
			  otx2_q_intr_handler, 0, irq_name, pf);
	if (err) {
		dev_err(pf->dev,
			"RVUPF%d: IRQ registration failed for QERR\n",
			rvu_get_pf(pf->pcifunc));
		goto err_disable_napi;
	}

	/* Enable QINT IRQ */
	otx2_write64(pf, NIX_LF_QINTX_ENA_W1S(0), BIT_ULL(0));

	/* Register CQ IRQ handlers */
	vec = pf->hw.nix_msixoff + NIX_LF_CINT_VEC_START;
	for (qidx = 0; qidx < pf->hw.cint_cnt; qidx++) {
		irq_name = &pf->hw.irq_name[vec * NAME_SIZE];

		snprintf(irq_name, NAME_SIZE, "%s-rxtx-%d", pf->netdev->name,
			 qidx);

		err = request_irq(pci_irq_vector(pf->pdev, vec),
				  otx2_cq_intr_handler, 0, irq_name,
				  &qset->napi[qidx]);
		if (err) {
			dev_err(pf->dev,
				"RVUPF%d: IRQ registration failed for CQ%d\n",
				rvu_get_pf(pf->pcifunc), qidx);
			goto err_free_cints;
		}
		vec++;

		otx2_config_irq_coalescing(pf, qidx);

		/* Enable CQ IRQ */
		otx2_write64(pf, NIX_LF_CINTX_INT(qidx), BIT_ULL(0));
		otx2_write64(pf, NIX_LF_CINTX_ENA_W1S(qidx), BIT_ULL(0));
	}

	otx2_set_cints_affinity(pf);

	pf->flags &= ~OTX2_FLAG_INTF_DOWN;
	/* 'intf_down' may be checked on any cpu */
	smp_wmb();

	/* we have already received link status notification */
	if (pf->linfo.link_up && !(pf->pcifunc & RVU_PFVF_FUNC_MASK))
		otx2_handle_link_event(pf);

	err = otx2_rxtx_enable(pf, true);
	if (err)
		goto err_free_cints;

	return 0;

err_free_cints:
	otx2_free_cints(pf, qidx);
	vec = pci_irq_vector(pf->pdev,
			     pf->hw.nix_msixoff + NIX_LF_QINT_VEC_START);
	otx2_write64(pf, NIX_LF_QINTX_ENA_W1C(0), BIT_ULL(0));
	synchronize_irq(vec);
	free_irq(vec, pf);
err_disable_napi:
	otx2_disable_napi(pf);
	otx2_free_hw_resources(pf);
err_free_mem:
	kfree(qset->sq);
	kfree(qset->cq);
	kfree(qset->rq);
	kfree(qset->napi);
	return err;
}

int otx2_stop(struct net_device *netdev)
{
	struct otx2_nic *pf = netdev_priv(netdev);
	struct otx2_cq_poll *cq_poll = NULL;
	struct otx2_qset *qset = &pf->qset;
	int qidx, vec, wrk;

	netif_carrier_off(netdev);
	netif_tx_stop_all_queues(netdev);

	pf->flags |= OTX2_FLAG_INTF_DOWN;
	/* 'intf_down' may be checked on any cpu */
	smp_wmb();

	/* First stop packet Rx/Tx */
	otx2_rxtx_enable(pf, false);

	/* Cleanup Queue IRQ */
	vec = pci_irq_vector(pf->pdev,
			     pf->hw.nix_msixoff + NIX_LF_QINT_VEC_START);
	otx2_write64(pf, NIX_LF_QINTX_ENA_W1C(0), BIT_ULL(0));
	synchronize_irq(vec);
	free_irq(vec, pf);

	/* Cleanup CQ NAPI and IRQ */
	vec = pf->hw.nix_msixoff + NIX_LF_CINT_VEC_START;
	for (qidx = 0; qidx < pf->hw.cint_cnt; qidx++) {
		/* Disable interrupt */
		otx2_write64(pf, NIX_LF_CINTX_ENA_W1C(qidx), BIT_ULL(0));

		synchronize_irq(pci_irq_vector(pf->pdev, vec));

		cq_poll = &qset->napi[qidx];
		napi_synchronize(&cq_poll->napi);
		vec++;
	}

	netif_tx_disable(netdev);

	otx2_free_hw_resources(pf);
	otx2_free_cints(pf, pf->hw.cint_cnt);
	otx2_disable_napi(pf);

	for (qidx = 0; qidx < netdev->num_tx_queues; qidx++)
		netdev_tx_reset_queue(netdev_get_tx_queue(netdev, qidx));

	for (wrk = 0; wrk < pf->qset.cq_cnt; wrk++)
		cancel_delayed_work_sync(&pf->refill_wrk[wrk].pool_refill_work);
	devm_kfree(pf->dev, pf->refill_wrk);

	kfree(qset->sq);
	kfree(qset->cq);
	kfree(qset->rq);
	kfree(qset->napi);
	/* Do not clear RQ/SQ ringsize settings */
	memset((void *)qset + offsetof(struct otx2_qset, sqe_cnt), 0,
	       sizeof(*qset) - offsetof(struct otx2_qset, sqe_cnt));
	return 0;
}

static netdev_tx_t otx2_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct otx2_nic *pf = netdev_priv(netdev);
	int qidx = skb_get_queue_mapping(skb);
	struct otx2_snd_queue *sq;
	struct netdev_queue *txq;

	/* Check for minimum and maximum packet length */
	if (skb->len <= ETH_HLEN ||
	    (!skb_shinfo(skb)->gso_size && skb->len > pf->max_frs)) {
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	sq = &pf->qset.sq[qidx];
	txq = netdev_get_tx_queue(netdev, qidx);

	if (!otx2_sq_append_skb(netdev, sq, skb, qidx)) {
		netif_tx_stop_queue(txq);

		/* Check again, incase SQBs got freed up */
		smp_mb();
		if (((sq->num_sqbs - *sq->aura_fc_addr) * sq->sqe_per_sqb)
							> sq->sqe_thresh)
			netif_tx_wake_queue(txq);

		return NETDEV_TX_BUSY;
	}

	return NETDEV_TX_OK;
}

static void otx2_set_rx_mode(struct net_device *netdev)
{
	struct otx2_nic *pf = netdev_priv(netdev);
	struct nix_rx_mode *req;

	if (!(netdev->flags & IFF_UP))
		return;

	otx2_mbox_lock(&pf->mbox);
	req = otx2_mbox_alloc_msg_nix_set_rx_mode(&pf->mbox);
	if (!req) {
		otx2_mbox_unlock(&pf->mbox);
		return;
	}

	req->mode = NIX_RX_MODE_UCAST;

	/* We don't support MAC address filtering yet */
	if (netdev->flags & IFF_PROMISC)
		req->mode |= NIX_RX_MODE_PROMISC;
	else if (netdev->flags & (IFF_ALLMULTI | IFF_MULTICAST))
		req->mode |= NIX_RX_MODE_ALLMULTI;

	otx2_sync_mbox_msg(&pf->mbox);
	otx2_mbox_unlock(&pf->mbox);
}

static int otx2_set_features(struct net_device *netdev,
			     netdev_features_t features)
{
	netdev_features_t changed = features ^ netdev->features;
	struct otx2_nic *pf = netdev_priv(netdev);

	if ((changed & NETIF_F_LOOPBACK) && netif_running(netdev))
		return otx2_cgx_config_loopback(pf,
						features & NETIF_F_LOOPBACK);
	return 0;
}

static void otx2_reset_task(struct work_struct *work)
{
	struct otx2_nic *pf = container_of(work, struct otx2_nic, reset_task);

	if (!netif_running(pf->netdev))
		return;

	otx2_stop(pf->netdev);
	pf->reset_count++;
	otx2_open(pf->netdev);
	netif_trans_update(pf->netdev);
}

static const struct net_device_ops otx2_netdev_ops = {
	.ndo_open		= otx2_open,
	.ndo_stop		= otx2_stop,
	.ndo_start_xmit		= otx2_xmit,
	.ndo_set_mac_address    = otx2_set_mac_address,
	.ndo_change_mtu		= otx2_change_mtu,
	.ndo_set_rx_mode	= otx2_set_rx_mode,
	.ndo_set_features	= otx2_set_features,
	.ndo_tx_timeout		= otx2_tx_timeout,
	.ndo_get_stats64	= otx2_get_stats64,
};

static int otx2_check_pf_usable(struct otx2_nic *nic)
{
	u64 rev;

	rev = otx2_read64(nic, RVU_PF_BLOCK_ADDRX_DISC(BLKADDR_RVUM));
	rev = (rev >> 12) & 0xFF;
	/* Check if AF has setup revision for RVUM block,
	 * otherwise this driver probe should be deferred
	 * until AF driver comes up.
	 */
	if (!rev) {
		dev_warn(nic->dev,
			 "AF is not initialized, deferring probe\n");
		return -EPROBE_DEFER;
	}
	return 0;
}

static int otx2_realloc_msix_vectors(struct otx2_nic *pf)
{
	struct otx2_hw *hw = &pf->hw;
	int num_vec, err;

	/* NPA interrupts are inot registered, so alloc only
	 * upto NIX vector offset.
	 */
	num_vec = hw->nix_msixoff;
	num_vec += NIX_LF_CINT_VEC_START + hw->max_queues;

	otx2_disable_mbox_intr(pf);
	pci_free_irq_vectors(hw->pdev);
	pci_free_irq_vectors(hw->pdev);
	err = pci_alloc_irq_vectors(hw->pdev, num_vec, num_vec, PCI_IRQ_MSIX);
	if (err < 0) {
		dev_err(pf->dev, "%s: Failed to realloc %d IRQ vectors\n",
			__func__, num_vec);
		return err;
	}

	return otx2_register_mbox_intr(pf, false);
}

static int otx2_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct net_device *netdev;
	struct otx2_nic *pf;
	struct otx2_hw *hw;
	int err, qcount;
	int num_vec;

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device\n");
		return err;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "PCI request regions failed 0x%x\n", err);
		return err;
	}

	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(48));
	if (err) {
		dev_err(dev, "DMA mask config failed, abort\n");
		goto err_release_regions;
	}

	pci_set_master(pdev);

	/* Set number of queues */
	qcount = min_t(int, num_online_cpus(), OTX2_MAX_CQ_CNT);

	netdev = alloc_etherdev_mqs(sizeof(*pf), qcount, qcount);
	if (!netdev) {
		err = -ENOMEM;
		goto err_release_regions;
	}

	pci_set_drvdata(pdev, netdev);
	SET_NETDEV_DEV(netdev, &pdev->dev);
	pf = netdev_priv(netdev);
	pf->netdev = netdev;
	pf->pdev = pdev;
	pf->dev = dev;
	pf->flags |= OTX2_FLAG_INTF_DOWN;

	hw = &pf->hw;
	hw->pdev = pdev;
	hw->rx_queues = qcount;
	hw->tx_queues = qcount;
	hw->max_queues = qcount;

	num_vec = pci_msix_vec_count(pdev);
	hw->irq_name = devm_kmalloc_array(&hw->pdev->dev, num_vec, NAME_SIZE,
					  GFP_KERNEL);
	if (!hw->irq_name)
		goto err_free_netdev;

	hw->affinity_mask = devm_kcalloc(&hw->pdev->dev, num_vec,
					 sizeof(cpumask_var_t), GFP_KERNEL);
	if (!hw->affinity_mask)
		goto err_free_netdev;

	/* Map CSRs */
	pf->reg_base = pcim_iomap(pdev, PCI_CFG_REG_BAR_NUM, 0);
	if (!pf->reg_base) {
		dev_err(dev, "Unable to map physical function CSRs, aborting\n");
		err = -ENOMEM;
		goto err_free_netdev;
	}

	err = otx2_check_pf_usable(pf);
	if (err)
		goto err_free_netdev;

	err = pci_alloc_irq_vectors(hw->pdev, RVU_PF_INT_VEC_CNT,
				    RVU_PF_INT_VEC_CNT, PCI_IRQ_MSIX);
	if (err < 0) {
		dev_err(dev, "%s: Failed to alloc %d IRQ vectors\n",
			__func__, num_vec);
		goto err_free_netdev;
	}

	/* Init PF <=> AF mailbox stuff */
	err = otx2_pfaf_mbox_init(pf);
	if (err)
		goto err_free_irq_vectors;

	/* Register mailbox interrupt */
	err = otx2_register_mbox_intr(pf, true);
	if (err)
		goto err_mbox_destroy;

	/* Request AF to attach NPA and NIX LFs to this PF.
	 * NIX and NPA LFs are needed for this PF to function as a NIC.
	 */
	err = otx2_attach_npa_nix(pf);
	if (err)
		goto err_disable_mbox_intr;

	err = otx2_realloc_msix_vectors(pf);
	if (err)
		goto err_detach_rsrc;

	err = otx2_set_real_num_queues(netdev, hw->tx_queues, hw->rx_queues);
	if (err)
		goto err_detach_rsrc;

	otx2_setup_dev_hw_settings(pf);

	/* Assign default mac address */
	otx2_get_mac_from_af(netdev);

	/* NPA's pool is a stack to which SW frees buffer pointers via Aura.
	 * HW allocates buffer pointer from stack and uses it for DMA'ing
	 * ingress packet. In some scenarios HW can free back allocated buffer
	 * pointers to pool. This makes it impossible for SW to maintain a
	 * parallel list where physical addresses of buffer pointers (IOVAs)
	 * given to HW can be saved for later reference.
	 *
	 * So the only way to convert Rx packet's buffer address is to use
	 * IOMMU's iova_to_phys() handler which translates the address by
	 * walking through the translation tables.
	 */
	pf->iommu_domain = iommu_get_domain_for_dev(dev);

	netdev->hw_features = (NETIF_F_RXCSUM | NETIF_F_IP_CSUM |
			       NETIF_F_IPV6_CSUM | NETIF_F_RXHASH |
			       NETIF_F_SG | NETIF_F_TSO | NETIF_F_TSO6);
	netdev->features |= netdev->hw_features;

	netdev->hw_features |= NETIF_F_LOOPBACK | NETIF_F_RXALL;

	netdev->gso_max_segs = OTX2_MAX_GSO_SEGS;
	netdev->watchdog_timeo = OTX2_TX_TIMEOUT;

	netdev->netdev_ops = &otx2_netdev_ops;

	/* MTU range: 64 - 9190 */
	netdev->min_mtu = OTX2_MIN_MTU;
	netdev->max_mtu = OTX2_MAX_MTU;

	INIT_WORK(&pf->reset_task, otx2_reset_task);

	err = register_netdev(netdev);
	if (err) {
		dev_err(dev, "Failed to register netdevice\n");
		goto err_detach_rsrc;
	}

	otx2_set_ethtool_ops(netdev);

	/* Enable link notifications */
	otx2_cgx_config_linkevents(pf, true);

	return 0;

err_detach_rsrc:
	otx2_detach_resources(&pf->mbox);
err_disable_mbox_intr:
	otx2_disable_mbox_intr(pf);
err_mbox_destroy:
	otx2_pfaf_mbox_destroy(pf);
err_free_irq_vectors:
	pci_free_irq_vectors(hw->pdev);
err_free_netdev:
	pci_set_drvdata(pdev, NULL);
	free_netdev(netdev);
err_release_regions:
	pci_release_regions(pdev);
	return err;
}

static void otx2_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct otx2_nic *pf;

	if (!netdev)
		return;

	pf = netdev_priv(netdev);

	/* Disable link notifications */
	otx2_cgx_config_linkevents(pf, false);

	unregister_netdev(netdev);
	otx2_detach_resources(&pf->mbox);
	otx2_disable_mbox_intr(pf);
	otx2_pfaf_mbox_destroy(pf);
	pci_free_irq_vectors(pf->pdev);
	pci_set_drvdata(pdev, NULL);
	free_netdev(netdev);

	pci_release_regions(pdev);
}

static struct pci_driver otx2_pf_driver = {
	.name = DRV_NAME,
	.id_table = otx2_pf_id_table,
	.probe = otx2_probe,
	.shutdown = otx2_remove,
	.remove = otx2_remove,
};

static int __init otx2_rvupf_init_module(void)
{
	pr_info("%s: %s\n", DRV_NAME, DRV_STRING);

	return pci_register_driver(&otx2_pf_driver);
}

static void __exit otx2_rvupf_cleanup_module(void)
{
	pci_unregister_driver(&otx2_pf_driver);
}

module_init(otx2_rvupf_init_module);
module_exit(otx2_rvupf_cleanup_module);
