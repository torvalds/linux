// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Microsoft Corporation. All rights reserved.
 */

#include "mana_ib.h"

static enum ib_wc_status vendor_error_to_wc_error(uint32_t vendor_error)
{
	switch (vendor_error) {
	case VENDOR_ERR_OK:
		return IB_WC_SUCCESS;
	case VENDOR_ERR_RX_PKT_LEN:
	case VENDOR_ERR_RX_MSG_LEN_OVFL:
		return IB_WC_LOC_LEN_ERR;
	case VENDOR_ERR_TX_GDMA_CORRUPTED_WQE:
	case VENDOR_ERR_TX_PCIE_WQE:
	case VENDOR_ERR_TX_PCIE_MSG:
	case VENDOR_ERR_RX_MALFORMED_WQE:
	case VENDOR_ERR_TX_GDMA_INVALID_STATE:
	case VENDOR_ERR_TX_MISBEHAVING_CLIENT:
	case VENDOR_ERR_TX_RDMA_MALFORMED_WQE_SIZE:
	case VENDOR_ERR_TX_RDMA_MALFORMED_WQE_FIELD:
	case VENDOR_ERR_TX_RDMA_WQE_UNSUPPORTED:
	case VENDOR_ERR_TX_RDMA_WQE_LEN_ERR:
	case VENDOR_ERR_TX_RDMA_MTU_ERR:
		return IB_WC_LOC_QP_OP_ERR;
	case VENDOR_ERR_TX_ATB_MSG_ACCESS_VIOLATION:
	case VENDOR_ERR_TX_ATB_MSG_ADDR_RANGE:
	case VENDOR_ERR_TX_ATB_MSG_CONFIG_ERR:
	case VENDOR_ERR_TX_ATB_WQE_ACCESS_VIOLATION:
	case VENDOR_ERR_TX_ATB_WQE_ADDR_RANGE:
	case VENDOR_ERR_TX_ATB_WQE_CONFIG_ERR:
	case VENDOR_ERR_RX_ATB_SGE_ADDR_RANGE:
	case VENDOR_ERR_RX_ATB_SGE_MISSCONFIG:
		return IB_WC_LOC_PROT_ERR;
	case VENDOR_ERR_RX_ATB_SGE_ADDR_RIGHT:
	case VENDOR_ERR_RX_GFID:
		return IB_WC_LOC_ACCESS_ERR;
	case VENDOR_ERR_RX_MISBEHAVING_CLIENT:
	case VENDOR_ERR_RX_CLIENT_ID:
	case VENDOR_ERR_RX_PCIE:
	case VENDOR_ERR_RX_NO_AVAIL_WQE:
	case VENDOR_ERR_RX_ATB_WQE_MISCONFIG:
	case VENDOR_ERR_RX_ATB_WQE_ADDR_RIGHT:
	case VENDOR_ERR_RX_ATB_WQE_ADDR_RANGE:
	case VENDOR_ERR_TX_RDMA_INVALID_STATE:
	case VENDOR_ERR_TX_RDMA_INVALID_NPT:
	case VENDOR_ERR_TX_RDMA_INVALID_SGID:
	case VENDOR_ERR_TX_RDMA_VFID_MISMATCH:
		return IB_WC_FATAL_ERR;
	case VENDOR_ERR_RX_NOT_EMPTY_ON_DISABLE:
	case VENDOR_ERR_SW_FLUSHED:
		return IB_WC_WR_FLUSH_ERR;
	default:
		return IB_WC_GENERAL_ERR;
	}
}

int mana_ib_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		      struct uverbs_attr_bundle *attrs)
{
	struct ib_udata *udata = &attrs->driver_udata;
	struct mana_ib_cq *cq = container_of(ibcq, struct mana_ib_cq, ibcq);
	struct mana_ib_create_cq_resp resp = {};
	struct mana_ib_ucontext *mana_ucontext;
	struct ib_device *ibdev = ibcq->device;
	struct mana_ib_create_cq ucmd;
	struct mana_ib_dev *mdev;
	bool is_rnic_cq;
	u32 doorbell;
	u32 buf_size;
	int err;

	mdev = container_of(ibdev, struct mana_ib_dev, ib_dev);

	cq->comp_vector = attr->comp_vector % ibdev->num_comp_vectors;
	cq->cq_handle = INVALID_MANA_HANDLE;
	is_rnic_cq = mana_ib_is_rnic(mdev);

	if (udata) {
		err = ib_copy_validate_udata_in_cm(udata, ucmd, buf_addr,
						   MANA_IB_CREATE_RNIC_CQ);
		if (err)
			return err;

		if ((!is_rnic_cq && attr->cqe > mdev->adapter_caps.max_qp_wr) ||
		    attr->cqe > U32_MAX / COMP_ENTRY_SIZE) {
			ibdev_dbg(ibdev, "CQE %d exceeding limit\n", attr->cqe);
			return -EINVAL;
		}

		cq->cqe = attr->cqe;
		err = mana_ib_create_queue(mdev, ucmd.buf_addr, cq->cqe * COMP_ENTRY_SIZE,
					   &cq->queue, true);
		if (err) {
			ibdev_dbg(ibdev, "Failed to create queue for create cq, %d\n", err);
			return err;
		}

		mana_ucontext = rdma_udata_to_drv_context(udata, struct mana_ib_ucontext,
							  ibucontext);
		doorbell = mana_ucontext->doorbell;
	} else {
		if (attr->cqe > U32_MAX / COMP_ENTRY_SIZE / 2 + 1) {
			ibdev_dbg(ibdev, "CQE %d exceeding limit\n", attr->cqe);
			return -EINVAL;
		}
		buf_size = MANA_PAGE_ALIGN(roundup_pow_of_two(attr->cqe * COMP_ENTRY_SIZE));
		cq->cqe = buf_size / COMP_ENTRY_SIZE;
		err = mana_ib_create_kernel_queue(mdev, buf_size, GDMA_CQ, &cq->queue);
		if (err) {
			ibdev_dbg(ibdev, "Failed to create kernel queue for create cq, %d\n", err);
			return err;
		}
		doorbell = mdev->gdma_dev->doorbell;
	}

	ibcq->cqe = cq->cqe;
	cq->poll_credit = (cq->cqe << (GDMA_CQE_OWNER_BITS - 1)) - 1;

	if (is_rnic_cq) {
		err = mana_ib_gd_create_cq(mdev, cq, doorbell);
		if (err) {
			ibdev_dbg(ibdev, "Failed to create RNIC cq, %d\n", err);
			goto err_destroy_queue;
		}

		err = mana_ib_install_cq_cb(mdev, cq);
		if (err) {
			ibdev_dbg(ibdev, "Failed to install cq callback, %d\n", err);
			goto err_destroy_rnic_cq;
		}
	}

	if (udata) {
		resp.cqid = cq->queue.id;
		err = ib_respond_udata(udata, resp);
		if (err)
			goto err_remove_cq_cb;
	}

	spin_lock_init(&cq->cq_lock);
	INIT_LIST_HEAD(&cq->send_err_qp_list);
	INIT_LIST_HEAD(&cq->recv_err_qp_list);

	return 0;

err_remove_cq_cb:
	mana_ib_remove_cq_cb(mdev, cq);
err_destroy_rnic_cq:
	mana_ib_gd_destroy_cq(mdev, cq);
err_destroy_queue:
	mana_ib_destroy_queue(mdev, &cq->queue);

	return err;
}

int mana_ib_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata)
{
	struct mana_ib_cq *cq = container_of(ibcq, struct mana_ib_cq, ibcq);
	struct ib_device *ibdev = ibcq->device;
	struct mana_ib_dev *mdev;
	int err;

	err = ib_no_udata_io(udata);
	if (err)
		return err;

	mdev = container_of(ibdev, struct mana_ib_dev, ib_dev);

	mana_ib_remove_cq_cb(mdev, cq);

	/* Ignore return code as there is not much we can do about it.
	 * The error message is printed inside.
	 */
	mana_ib_gd_destroy_cq(mdev, cq);

	mana_ib_destroy_queue(mdev, &cq->queue);

	return 0;
}

static void mana_ib_cq_handler(void *ctx, struct gdma_queue *gdma_cq)
{
	struct mana_ib_cq *cq = ctx;

	if (cq->ibcq.comp_handler)
		cq->ibcq.comp_handler(&cq->ibcq, cq->ibcq.cq_context);
}

int mana_ib_install_cq_cb(struct mana_ib_dev *mdev, struct mana_ib_cq *cq)
{
	struct gdma_context *gc = mdev_to_gc(mdev);
	struct gdma_queue *gdma_cq;

	if (cq->queue.id >= gc->max_num_cqs)
		return -EINVAL;
	/* Create CQ table entry, sharing a CQ between WQs is not supported */
	if (gc->cq_table[cq->queue.id])
		return -EINVAL;
	if (cq->queue.kmem)
		gdma_cq = cq->queue.kmem;
	else
		gdma_cq = kzalloc_obj(*gdma_cq);
	if (!gdma_cq)
		return -ENOMEM;

	gdma_cq->cq.context = cq;
	gdma_cq->type = GDMA_CQ;
	gdma_cq->cq.callback = mana_ib_cq_handler;
	gdma_cq->id = cq->queue.id;
	gc->cq_table[cq->queue.id] = gdma_cq;
	return 0;
}

void mana_ib_remove_cq_cb(struct mana_ib_dev *mdev, struct mana_ib_cq *cq)
{
	struct gdma_context *gc = mdev_to_gc(mdev);

	if (cq->queue.id >= gc->max_num_cqs || cq->queue.id == INVALID_QUEUE_ID)
		return;

	if (cq->queue.kmem)
	/* Then it will be cleaned and removed by the mana */
		return;

	kfree(gc->cq_table[cq->queue.id]);
	gc->cq_table[cq->queue.id] = NULL;
}

static inline bool gdma_cq_idx_produced(struct gdma_queue *gdma_cq, uint32_t idx)
{
	struct gdma_mem_info *gmi = &gdma_cq->mem_info;
	u32 num_cqe = gdma_cq->queue_size / GDMA_CQE_SIZE;
	u32 expected_bits = (idx / num_cqe) & GDMA_CQE_OWNER_MASK;
	u32 offset = (idx % num_cqe) * GDMA_CQE_SIZE;
	struct gdma_cqe *cqe;

	if (gmi->nr_pages)
		cqe = gmi->pages_va[offset / PAGE_SIZE] +
		      (offset & (PAGE_SIZE - 1));
	else
		cqe = gdma_cq->queue_mem_ptr + offset;

	return cqe->cqe_info.owner_bits == expected_bits;
}

static inline void mana_ib_cq_doorbell(struct mana_ib_cq *cq, uint8_t arm)
{
	struct mana_ib_dev *mdev = container_of(cq->ibcq.device, struct mana_ib_dev, ib_dev);
	struct gdma_queue *gdma_cq = cq->queue.kmem;
	u32 num_cqe, max_credit, idx;

	num_cqe = gdma_cq->queue_size / GDMA_CQE_SIZE;
	max_credit = num_cqe << (GDMA_CQE_OWNER_BITS - 1);
	idx = gdma_cq->head;

	if (cq->poll_credit >= max_credit) {
		if (gdma_cq_idx_produced(gdma_cq, idx + cq->poll_credit - max_credit))
			cq->poll_credit++;
		else
			return;
	} else {
		/* Set index of already polled CQE for unarm */
		cq->poll_credit = max_credit - (arm ? 0 : 1);
	}

	idx += (cq->poll_credit - max_credit);
	idx %= (num_cqe << GDMA_CQE_OWNER_BITS);

	mana_gd_wq_ring_doorbell_ext(mdev_to_gc(mdev), gdma_cq, idx, arm, 0);
}

int mana_ib_arm_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags)
{
	struct mana_ib_cq *cq = container_of(ibcq, struct mana_ib_cq, ibcq);
	struct gdma_queue *gdma_cq = cq->queue.kmem;
	unsigned long irq_flags;

	if (!gdma_cq)
		return -EINVAL;

	spin_lock_irqsave(&cq->cq_lock, irq_flags);
	mana_ib_cq_doorbell(cq, SET_ARM_BIT);
	spin_unlock_irqrestore(&cq->cq_lock, irq_flags);

	return 0;
}

struct mana_cq_poll {
	struct ib_wc *wc;
	int budget;
	int produced;
};

static struct ib_wc *mana_fill_wc(struct mana_ib_qp *qp,
				  struct mana_cq_poll *poll,
				  const struct shadow_wqe_header *wqe,
				  enum ib_wc_opcode opcode, u32 vendor_error)
{
	struct ib_wc *wc = &poll->wc[poll->produced++];

	memset(wc, 0, sizeof(*wc));
	wc->wr_id = wqe->wr_id;
	wc->status = vendor_error_to_wc_error(vendor_error);
	wc->opcode = opcode;
	wc->vendor_err = vendor_error;
	wc->qp = &qp->ibqp;

	return wc;
}

static void mana_complete_send(struct mana_ib_qp *qp,
			       struct mana_cq_poll *poll, u32 vendor_error)
{
	struct shadow_queue *shadow = &qp->shadow_sq;
	struct shadow_wqe_header *wqe = shadow_queue_get_next_to_consume(shadow);
	struct gdma_queue *queue;

	if (!wqe)
		return;

	if (vendor_error || !(wqe->flags & MANA_WQ_NO_SIGNAL_WC))
		mana_fill_wc(qp, poll, wqe, wqe->send_opcode, vendor_error);

	queue = mana_qp_get_sq(qp)->kmem;
	queue->tail += wqe->wqe_size_in_bu;
	shadow_queue_advance_consumer(shadow);
}

static void handle_ud_sq_cqe(struct mana_ib_qp *qp, struct mana_rdma_cqe *rdma_cqe,
			     struct mana_cq_poll *poll)
{
	u32 offset = rdma_cqe->ud_send.tx_wqe_offset & MANA_WQE_OFFSET_MASK;
	struct shadow_queue *shadow = &qp->shadow_sq;
	struct shadow_wqe_header *wqe;
	u64 idx = shadow->cons_idx;
	u32 to_complete = 0;
	u64 prod_idx;

	/* Pair with posting's release of the initialized shadow entries. */
	prod_idx = smp_load_acquire(&shadow->prod_idx);
	/* Find the target before retiring any entries: the CQE may be stale. */
	for (; idx != prod_idx; idx++) {
		wqe = shadow_queue_get_element(shadow, idx);
		to_complete++;
		if (wqe->wqe_offset_or_psn == offset)
			break;
		if (!(wqe->flags & MANA_WQ_NO_SIGNAL_WC))
			return;
	}
	if (idx == prod_idx)
		return;

	for (; to_complete; to_complete--)
		mana_complete_send(qp, poll, VENDOR_ERR_OK);
}

static void handle_rq_cqe(struct mana_ib_qp *qp, struct gdma_comp *cqe,
			  struct mana_cq_poll *poll)
{
	struct mana_rdma_cqe *rdma_cqe = (struct mana_rdma_cqe *)cqe->cqe_data;
	u32 offset = rdma_cqe->ud_recv.rx_wqe_offset / GDMA_WQE_BU_SIZE;
	struct mana_ib_queue *rq = mana_qp_get_rq(qp);
	struct gdma_queue *wq = rq->kmem;
	struct shadow_wqe_header *wqe;
	struct ib_wc *wc;

	wqe = shadow_queue_get_next_to_consume(&qp->shadow_rq);
	if (!wqe || wqe->wqe_offset_or_psn != (offset & MANA_WQE_OFFSET_MASK))
		return;

	wc = mana_fill_wc(qp, poll, wqe, IB_WC_RECV, VENDOR_ERR_OK);
	switch (rdma_cqe->cqe_type) {
	case CQE_TYPE_UD_SEND_IMM:
		wc->ex.imm_data = cpu_to_be32(rdma_cqe->ud_recv.imm_data);
		wc->wc_flags |= IB_WC_WITH_IMM;
		fallthrough;
	case CQE_TYPE_UD_SEND:
		wc->byte_len = rdma_cqe->ud_recv.msg_len;
		wc->src_qp = rdma_cqe->ud_recv.src_qpn;
		wc->wc_flags |= IB_WC_GRH;
		break;
	default:
		break;
	}

	wq->tail += wqe->wqe_size_in_bu;
	shadow_queue_advance_consumer(&qp->shadow_rq);
}

static bool mana_handle_cqe(struct mana_ib_cq *cq, struct mana_ib_dev *mdev,
			    struct mana_cq_poll *poll)
{
	struct gdma_comp *cqe = &cq->pending_cqe;
	struct mana_rdma_cqe *rdma_cqe = (struct mana_rdma_cqe *)cqe->cqe_data;
	struct mana_ib_qp *qp = mana_get_qp_ref(mdev, cqe->wq_num, cqe->is_sq);

	if (!qp)
		return true;

	switch (rdma_cqe->cqe_type) {
	case CQE_TYPE_UD_SEND:
		if (cqe->is_sq) {
			handle_ud_sq_cqe(qp, rdma_cqe, poll);
			break;
		}
		fallthrough;
	case CQE_TYPE_UD_SEND_IMM:
		handle_rq_cqe(qp, cqe, poll);
		break;
	default:
		ibdev_warn_ratelimited(qp->ibqp.device, "Unexpected CQE type %u\n",
				       rdma_cqe->cqe_type);
		break;
	}
	mana_put_qp_ref(qp);
	return true;
}

static void mana_flush_completions(struct mana_ib_cq *cq, struct mana_cq_poll *poll)
{
	struct shadow_wqe_header *wqe;
	struct mana_ib_qp *qp;

	if (poll->produced >= poll->budget)
		return;

	list_for_each_entry(qp, &cq->send_err_qp_list, send_err_node) {
		while (poll->produced < poll->budget &&
		       shadow_queue_get_next_to_consume(&qp->shadow_sq))
			mana_complete_send(qp, poll, VENDOR_ERR_SW_FLUSHED);
		if (poll->produced == poll->budget)
			return;
	}

	list_for_each_entry(qp, &cq->recv_err_qp_list, recv_err_node) {
		while (poll->produced < poll->budget &&
		       (wqe = shadow_queue_get_next_to_consume(&qp->shadow_rq))) {
			mana_fill_wc(qp, poll, wqe, IB_WC_RECV, VENDOR_ERR_SW_FLUSHED);
			shadow_queue_advance_consumer(&qp->shadow_rq);
		}
		if (poll->produced == poll->budget)
			return;
	}
}

static void mana_drain_gsi_sq(struct mana_ib_qp *qp)
{
	struct mana_ib_cq *cq = container_of(qp->ibqp.send_cq, struct mana_ib_cq, ibcq);
	unsigned long flags;

	spin_lock_irqsave(&cq->cq_lock, flags);
	if (list_empty(&qp->send_err_node))
		list_add_tail(&qp->send_err_node, &cq->send_err_qp_list);
	spin_unlock_irqrestore(&cq->cq_lock, flags);

	if (cq->ibcq.comp_handler)
		cq->ibcq.comp_handler(&cq->ibcq, cq->ibcq.cq_context);
}

void mana_drain_gsi_sqs(struct mana_ib_dev *mdev)
{
	struct mana_ib_qp *qp;
	u32 port;

	/* One GSI QP per port, indexed in the QP table by (port << 24 | MANA_GSI_QPN) */
	for (port = 1; port <= mdev->ib_dev.phys_port_cnt; port++) {
		qp = mana_get_qp_ref(mdev, (port << 24) | MANA_GSI_QPN, false);
		if (!qp)
			continue;

		mana_drain_gsi_sq(qp);
		mana_put_qp_ref(qp);
	}
}

int mana_ib_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc)
{
	struct mana_ib_cq *cq = container_of(ibcq, struct mana_ib_cq, ibcq);
	struct mana_ib_dev *mdev = container_of(ibcq->device, struct mana_ib_dev, ib_dev);
	struct mana_cq_poll poll = { .wc = wc, .budget = num_entries, .produced = 0 };
	struct gdma_queue *queue = cq->queue.kmem;
	unsigned long flags;
	bool consumed;
	int comp_read;

	if (!queue)
		return -EINVAL;

	spin_lock_irqsave(&cq->cq_lock, flags);
	while (poll.produced < poll.budget) {
		if (!cq->has_pending_cqe) {
			comp_read = mana_gd_poll_cq(queue, &cq->pending_cqe, 1);
			if (comp_read < 0) {
				if (!poll.produced)
					poll.produced = comp_read;
				goto out;
			}
			if (!comp_read)
				break;

			cq->poll_credit--;
			if (!cq->poll_credit)
				mana_ib_cq_doorbell(cq, 0);
		}

		consumed = mana_handle_cqe(cq, mdev, &poll);
		cq->has_pending_cqe = !consumed;
	}

	mana_flush_completions(cq, &poll);
out:
	spin_unlock_irqrestore(&cq->cq_lock, flags);

	return poll.produced;
}
