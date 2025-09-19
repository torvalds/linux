// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2025, Advanced Micro Devices, Inc. */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/printk.h>

#include "ionic_fw.h"
#include "ionic_ibdev.h"

#define IONIC_EQ_COUNT_MIN	4
#define IONIC_AQ_COUNT_MIN	1

/* not a valid queue position or negative error status */
#define IONIC_ADMIN_POSTED	0x10000

/* cpu can be held with irq disabled for COUNT * MS  (for create/destroy_ah) */
#define IONIC_ADMIN_BUSY_RETRY_COUNT	2000
#define IONIC_ADMIN_BUSY_RETRY_MS	1

/* admin queue will be considered failed if a command takes longer */
#define IONIC_ADMIN_TIMEOUT	(HZ * 2)
#define IONIC_ADMIN_WARN	(HZ / 8)

/* will poll for admin cq to tolerate and report from missed event */
#define IONIC_ADMIN_DELAY	(HZ / 8)

/* work queue for polling the event queue and admin cq */
struct workqueue_struct *ionic_evt_workq;

static void ionic_admin_timedout(struct ionic_aq *aq)
{
	struct ionic_ibdev *dev = aq->dev;
	unsigned long irqflags;
	u16 pos;

	spin_lock_irqsave(&aq->lock, irqflags);
	if (ionic_queue_empty(&aq->q))
		goto out;

	/* Reset ALL adminq if any one times out */
	if (atomic_read(&aq->admin_state) < IONIC_ADMIN_KILLED)
		queue_work(ionic_evt_workq, &dev->reset_work);

	ibdev_err(&dev->ibdev, "admin command timed out, aq %d after: %ums\n",
		  aq->aqid, (u32)jiffies_to_msecs(jiffies - aq->stamp));

	pos = (aq->q.prod - 1) & aq->q.mask;
	if (pos == aq->q.cons)
		goto out;

	ibdev_warn(&dev->ibdev, "admin pos %u (last posted)\n", pos);
	print_hex_dump(KERN_WARNING, "cmd ", DUMP_PREFIX_OFFSET, 16, 1,
		       ionic_queue_at(&aq->q, pos),
		       BIT(aq->q.stride_log2), true);

out:
	spin_unlock_irqrestore(&aq->lock, irqflags);
}

static void ionic_admin_reset_dwork(struct ionic_ibdev *dev)
{
	if (atomic_read(&dev->admin_state) == IONIC_ADMIN_KILLED)
		return;

	queue_delayed_work(ionic_evt_workq, &dev->admin_dwork,
			   IONIC_ADMIN_DELAY);
}

static void ionic_admin_reset_wdog(struct ionic_aq *aq)
{
	if (atomic_read(&aq->admin_state) == IONIC_ADMIN_KILLED)
		return;

	aq->stamp = jiffies;
	ionic_admin_reset_dwork(aq->dev);
}

static bool ionic_admin_next_cqe(struct ionic_ibdev *dev, struct ionic_cq *cq,
				 struct ionic_v1_cqe **cqe)
{
	struct ionic_v1_cqe *qcqe = ionic_queue_at_prod(&cq->q);

	if (unlikely(cq->color != ionic_v1_cqe_color(qcqe)))
		return false;

	/* Prevent out-of-order reads of the CQE */
	dma_rmb();
	*cqe = qcqe;

	return true;
}

static void ionic_admin_poll_locked(struct ionic_aq *aq)
{
	struct ionic_cq *cq = &aq->vcq->cq[0];
	struct ionic_admin_wr *wr, *wr_next;
	struct ionic_ibdev *dev = aq->dev;
	u32 wr_strides, avlbl_strides;
	struct ionic_v1_cqe *cqe;
	u32 qtf, qid;
	u16 old_prod;
	u8 type;

	lockdep_assert_held(&aq->lock);

	if (atomic_read(&aq->admin_state) == IONIC_ADMIN_KILLED) {
		list_for_each_entry_safe(wr, wr_next, &aq->wr_prod, aq_ent) {
			INIT_LIST_HEAD(&wr->aq_ent);
			aq->q_wr[wr->status].wr = NULL;
			wr->status = atomic_read(&aq->admin_state);
			complete_all(&wr->work);
		}
		INIT_LIST_HEAD(&aq->wr_prod);

		list_for_each_entry_safe(wr, wr_next, &aq->wr_post, aq_ent) {
			INIT_LIST_HEAD(&wr->aq_ent);
			wr->status = atomic_read(&aq->admin_state);
			complete_all(&wr->work);
		}
		INIT_LIST_HEAD(&aq->wr_post);

		return;
	}

	old_prod = cq->q.prod;

	while (ionic_admin_next_cqe(dev, cq, &cqe)) {
		qtf = ionic_v1_cqe_qtf(cqe);
		qid = ionic_v1_cqe_qtf_qid(qtf);
		type = ionic_v1_cqe_qtf_type(qtf);

		if (unlikely(type != IONIC_V1_CQE_TYPE_ADMIN)) {
			ibdev_warn_ratelimited(&dev->ibdev,
					       "bad cqe type %u\n", type);
			goto cq_next;
		}

		if (unlikely(qid != aq->aqid)) {
			ibdev_warn_ratelimited(&dev->ibdev,
					       "bad cqe qid %u\n", qid);
			goto cq_next;
		}

		if (unlikely(be16_to_cpu(cqe->admin.cmd_idx) != aq->q.cons)) {
			ibdev_warn_ratelimited(&dev->ibdev,
					       "bad idx %u cons %u qid %u\n",
					       be16_to_cpu(cqe->admin.cmd_idx),
					       aq->q.cons, qid);
			goto cq_next;
		}

		if (unlikely(ionic_queue_empty(&aq->q))) {
			ibdev_warn_ratelimited(&dev->ibdev,
					       "bad cqe for empty adminq\n");
			goto cq_next;
		}

		wr = aq->q_wr[aq->q.cons].wr;
		if (wr) {
			aq->q_wr[aq->q.cons].wr = NULL;
			list_del_init(&wr->aq_ent);

			wr->cqe = *cqe;
			wr->status = atomic_read(&aq->admin_state);
			complete_all(&wr->work);
		}

		ionic_queue_consume_entries(&aq->q,
					    aq->q_wr[aq->q.cons].wqe_strides);

cq_next:
		ionic_queue_produce(&cq->q);
		cq->color = ionic_color_wrap(cq->q.prod, cq->color);
	}

	if (old_prod != cq->q.prod) {
		ionic_admin_reset_wdog(aq);
		cq->q.cons = cq->q.prod;
		ionic_dbell_ring(dev->lif_cfg.dbpage, dev->lif_cfg.cq_qtype,
				 ionic_queue_dbell_val(&cq->q));
		queue_work(ionic_evt_workq, &aq->work);
	} else if (!aq->armed) {
		aq->armed = true;
		cq->arm_any_prod = ionic_queue_next(&cq->q, cq->arm_any_prod);
		ionic_dbell_ring(dev->lif_cfg.dbpage, dev->lif_cfg.cq_qtype,
				 cq->q.dbell | IONIC_CQ_RING_ARM |
				 cq->arm_any_prod);
		queue_work(ionic_evt_workq, &aq->work);
	}

	if (atomic_read(&aq->admin_state) != IONIC_ADMIN_ACTIVE)
		return;

	old_prod = aq->q.prod;

	if (ionic_queue_empty(&aq->q) && !list_empty(&aq->wr_post))
		ionic_admin_reset_wdog(aq);

	if (list_empty(&aq->wr_post))
		return;

	do {
		u8 *src;
		int i, src_len;
		size_t stride_len;

		wr = list_first_entry(&aq->wr_post, struct ionic_admin_wr,
				      aq_ent);
		wr_strides = (le16_to_cpu(wr->wqe.len) + ADMIN_WQE_HDR_LEN +
			     (ADMIN_WQE_STRIDE - 1)) >> aq->q.stride_log2;
		avlbl_strides = ionic_queue_length_remaining(&aq->q);

		if (wr_strides > avlbl_strides)
			break;

		list_move(&wr->aq_ent, &aq->wr_prod);
		wr->status = aq->q.prod;
		aq->q_wr[aq->q.prod].wr = wr;
		aq->q_wr[aq->q.prod].wqe_strides = wr_strides;

		src_len = le16_to_cpu(wr->wqe.len);
		src = (uint8_t *)&wr->wqe.cmd;

		/* First stride */
		memcpy(ionic_queue_at_prod(&aq->q), &wr->wqe,
		       ADMIN_WQE_HDR_LEN);
		stride_len = ADMIN_WQE_STRIDE - ADMIN_WQE_HDR_LEN;
		if (stride_len > src_len)
			stride_len = src_len;
		memcpy(ionic_queue_at_prod(&aq->q) + ADMIN_WQE_HDR_LEN,
		       src, stride_len);
		ibdev_dbg(&dev->ibdev, "post admin prod %u (%u strides)\n",
			  aq->q.prod, wr_strides);
		print_hex_dump_debug("wqe ", DUMP_PREFIX_OFFSET, 16, 1,
				     ionic_queue_at_prod(&aq->q),
				     BIT(aq->q.stride_log2), true);
		ionic_queue_produce(&aq->q);

		/* Remaining strides */
		for (i = stride_len; i < src_len; i += stride_len) {
			stride_len = ADMIN_WQE_STRIDE;

			if (i + stride_len > src_len)
				stride_len = src_len - i;

			memcpy(ionic_queue_at_prod(&aq->q), src + i,
			       stride_len);
			print_hex_dump_debug("wqe ", DUMP_PREFIX_OFFSET, 16, 1,
					     ionic_queue_at_prod(&aq->q),
					     BIT(aq->q.stride_log2), true);
			ionic_queue_produce(&aq->q);
		}
	} while (!list_empty(&aq->wr_post));

	if (old_prod != aq->q.prod)
		ionic_dbell_ring(dev->lif_cfg.dbpage, dev->lif_cfg.aq_qtype,
				 ionic_queue_dbell_val(&aq->q));
}

static void ionic_admin_dwork(struct work_struct *ws)
{
	struct ionic_ibdev *dev =
		container_of(ws, struct ionic_ibdev, admin_dwork.work);
	struct ionic_aq *aq, *bad_aq = NULL;
	bool do_reschedule = false;
	unsigned long irqflags;
	bool do_reset = false;
	u16 pos;
	int i;

	for (i = 0; i < dev->lif_cfg.aq_count; i++) {
		aq = dev->aq_vec[i];

		spin_lock_irqsave(&aq->lock, irqflags);

		if (ionic_queue_empty(&aq->q))
			goto next_aq;

		/* Reschedule if any queue has outstanding work */
		do_reschedule = true;

		if (time_is_after_eq_jiffies(aq->stamp + IONIC_ADMIN_WARN))
			/* Warning threshold not met, nothing to do */
			goto next_aq;

		/* See if polling now makes some progress */
		pos = aq->q.cons;
		ionic_admin_poll_locked(aq);
		if (pos != aq->q.cons) {
			ibdev_dbg(&dev->ibdev,
				  "missed event for acq %d\n", aq->cqid);
			goto next_aq;
		}

		if (time_is_after_eq_jiffies(aq->stamp +
					     IONIC_ADMIN_TIMEOUT)) {
			/* Timeout threshold not met */
			ibdev_dbg(&dev->ibdev, "no progress after %ums\n",
				  (u32)jiffies_to_msecs(jiffies - aq->stamp));
			goto next_aq;
		}

		/* Queue timed out */
		bad_aq = aq;
		do_reset = true;
next_aq:
		spin_unlock_irqrestore(&aq->lock, irqflags);
	}

	if (do_reset)
		/* Reset RDMA lif on a timeout */
		ionic_admin_timedout(bad_aq);
	else if (do_reschedule)
		/* Try to poll again later */
		ionic_admin_reset_dwork(dev);
}

static void ionic_admin_work(struct work_struct *ws)
{
	struct ionic_aq *aq = container_of(ws, struct ionic_aq, work);
	unsigned long irqflags;

	spin_lock_irqsave(&aq->lock, irqflags);
	ionic_admin_poll_locked(aq);
	spin_unlock_irqrestore(&aq->lock, irqflags);
}

static void ionic_admin_post_aq(struct ionic_aq *aq, struct ionic_admin_wr *wr)
{
	unsigned long irqflags;
	bool poll;

	wr->status = IONIC_ADMIN_POSTED;
	wr->aq = aq;

	spin_lock_irqsave(&aq->lock, irqflags);
	poll = list_empty(&aq->wr_post);
	list_add(&wr->aq_ent, &aq->wr_post);
	if (poll)
		ionic_admin_poll_locked(aq);
	spin_unlock_irqrestore(&aq->lock, irqflags);
}

void ionic_admin_post(struct ionic_ibdev *dev, struct ionic_admin_wr *wr)
{
	int aq_idx;

	/* Use cpu id for the adminq selection */
	aq_idx = raw_smp_processor_id() % dev->lif_cfg.aq_count;
	ionic_admin_post_aq(dev->aq_vec[aq_idx], wr);
}

static void ionic_admin_cancel(struct ionic_admin_wr *wr)
{
	struct ionic_aq *aq = wr->aq;
	unsigned long irqflags;

	spin_lock_irqsave(&aq->lock, irqflags);

	if (!list_empty(&wr->aq_ent)) {
		list_del(&wr->aq_ent);
		if (wr->status != IONIC_ADMIN_POSTED)
			aq->q_wr[wr->status].wr = NULL;
	}

	spin_unlock_irqrestore(&aq->lock, irqflags);
}

static int ionic_admin_busy_wait(struct ionic_admin_wr *wr)
{
	struct ionic_aq *aq = wr->aq;
	unsigned long irqflags;
	int try_i;

	for (try_i = 0; try_i < IONIC_ADMIN_BUSY_RETRY_COUNT; ++try_i) {
		if (completion_done(&wr->work))
			return 0;

		mdelay(IONIC_ADMIN_BUSY_RETRY_MS);

		spin_lock_irqsave(&aq->lock, irqflags);
		ionic_admin_poll_locked(aq);
		spin_unlock_irqrestore(&aq->lock, irqflags);
	}

	/*
	 * we timed out. Initiate RDMA LIF reset and indicate
	 * error to caller.
	 */
	ionic_admin_timedout(aq);
	return -ETIMEDOUT;
}

int ionic_admin_wait(struct ionic_ibdev *dev, struct ionic_admin_wr *wr,
		     enum ionic_admin_flags flags)
{
	int rc, timo;

	if (flags & IONIC_ADMIN_F_BUSYWAIT) {
		/* Spin */
		rc = ionic_admin_busy_wait(wr);
	} else if (flags & IONIC_ADMIN_F_INTERRUPT) {
		/*
		 * Interruptible sleep, 1s timeout
		 * This is used for commands which are safe for the caller
		 * to clean up without killing and resetting the adminq.
		 */
		timo = wait_for_completion_interruptible_timeout(&wr->work,
								 HZ);
		if (timo > 0)
			rc = 0;
		else if (timo == 0)
			rc = -ETIMEDOUT;
		else
			rc = timo;
	} else {
		/*
		 * Uninterruptible sleep
		 * This is used for commands which are NOT safe for the
		 * caller to clean up. Cleanup must be handled by the
		 * adminq kill and reset process so that host memory is
		 * not corrupted by the device.
		 */
		wait_for_completion(&wr->work);
		rc = 0;
	}

	if (rc) {
		ibdev_warn(&dev->ibdev, "wait status %d\n", rc);
		ionic_admin_cancel(wr);
	} else if (wr->status == IONIC_ADMIN_KILLED) {
		ibdev_dbg(&dev->ibdev, "admin killed\n");

		/* No error if admin already killed during teardown */
		rc = (flags & IONIC_ADMIN_F_TEARDOWN) ? 0 : -ENODEV;
	} else if (ionic_v1_cqe_error(&wr->cqe)) {
		ibdev_warn(&dev->ibdev, "opcode %u error %u\n",
			   wr->wqe.op,
			   be32_to_cpu(wr->cqe.status_length));
		rc = -EINVAL;
	}
	return rc;
}

static int ionic_rdma_devcmd(struct ionic_ibdev *dev,
			     struct ionic_admin_ctx *admin)
{
	int rc;

	rc = ionic_adminq_post_wait(dev->lif_cfg.lif, admin);
	if (rc)
		return rc;

	return ionic_error_to_errno(admin->comp.comp.status);
}

int ionic_rdma_reset_devcmd(struct ionic_ibdev *dev)
{
	struct ionic_admin_ctx admin = {
		.work = COMPLETION_INITIALIZER_ONSTACK(admin.work),
		.cmd.rdma_reset = {
			.opcode = IONIC_CMD_RDMA_RESET_LIF,
			.lif_index = cpu_to_le16(dev->lif_cfg.lif_index),
		},
	};

	return ionic_rdma_devcmd(dev, &admin);
}

static int ionic_rdma_queue_devcmd(struct ionic_ibdev *dev,
				   struct ionic_queue *q,
				   u32 qid, u32 cid, u16 opcode)
{
	struct ionic_admin_ctx admin = {
		.work = COMPLETION_INITIALIZER_ONSTACK(admin.work),
		.cmd.rdma_queue = {
			.opcode = opcode,
			.lif_index = cpu_to_le16(dev->lif_cfg.lif_index),
			.qid_ver = cpu_to_le32(qid),
			.cid = cpu_to_le32(cid),
			.dbid = cpu_to_le16(dev->lif_cfg.dbid),
			.depth_log2 = q->depth_log2,
			.stride_log2 = q->stride_log2,
			.dma_addr = cpu_to_le64(q->dma),
		},
	};

	return ionic_rdma_devcmd(dev, &admin);
}

static void ionic_rdma_admincq_comp(struct ib_cq *ibcq, void *cq_context)
{
	struct ionic_aq *aq = cq_context;
	unsigned long irqflags;

	spin_lock_irqsave(&aq->lock, irqflags);
	aq->armed = false;
	if (atomic_read(&aq->admin_state) < IONIC_ADMIN_KILLED)
		queue_work(ionic_evt_workq, &aq->work);
	spin_unlock_irqrestore(&aq->lock, irqflags);
}

static void ionic_rdma_admincq_event(struct ib_event *event, void *cq_context)
{
	struct ionic_aq *aq = cq_context;

	ibdev_err(&aq->dev->ibdev, "admincq event %d\n", event->event);
}

static struct ionic_vcq *ionic_create_rdma_admincq(struct ionic_ibdev *dev,
						   int comp_vector)
{
	struct ib_cq_init_attr attr = {
		.cqe = IONIC_AQ_DEPTH,
		.comp_vector = comp_vector,
	};
	struct ionic_tbl_buf buf = {};
	struct ionic_vcq *vcq;
	struct ionic_cq *cq;
	int rc;

	vcq = kzalloc(sizeof(*vcq), GFP_KERNEL);
	if (!vcq)
		return ERR_PTR(-ENOMEM);

	vcq->ibcq.device = &dev->ibdev;
	vcq->ibcq.comp_handler = ionic_rdma_admincq_comp;
	vcq->ibcq.event_handler = ionic_rdma_admincq_event;
	atomic_set(&vcq->ibcq.usecnt, 0);

	vcq->udma_mask = 1;
	cq = &vcq->cq[0];

	rc = ionic_create_cq_common(vcq, &buf, &attr, NULL, NULL,
				    NULL, NULL, 0);
	if (rc)
		goto err_init;

	rc = ionic_rdma_queue_devcmd(dev, &cq->q, cq->cqid, cq->eqid,
				     IONIC_CMD_RDMA_CREATE_CQ);
	if (rc)
		goto err_cmd;

	return vcq;

err_cmd:
	ionic_destroy_cq_common(dev, cq);
err_init:
	kfree(vcq);

	return ERR_PTR(rc);
}

static struct ionic_aq *__ionic_create_rdma_adminq(struct ionic_ibdev *dev,
						   u32 aqid, u32 cqid)
{
	struct ionic_aq *aq;
	int rc;

	aq = kzalloc(sizeof(*aq), GFP_KERNEL);
	if (!aq)
		return ERR_PTR(-ENOMEM);

	atomic_set(&aq->admin_state, IONIC_ADMIN_KILLED);
	aq->dev = dev;
	aq->aqid = aqid;
	aq->cqid = cqid;
	spin_lock_init(&aq->lock);

	rc = ionic_queue_init(&aq->q, dev->lif_cfg.hwdev, IONIC_EQ_DEPTH,
			      ADMIN_WQE_STRIDE);
	if (rc)
		goto err_q;

	ionic_queue_dbell_init(&aq->q, aq->aqid);

	aq->q_wr = kcalloc((u32)aq->q.mask + 1, sizeof(*aq->q_wr), GFP_KERNEL);
	if (!aq->q_wr) {
		rc = -ENOMEM;
		goto err_wr;
	}

	INIT_LIST_HEAD(&aq->wr_prod);
	INIT_LIST_HEAD(&aq->wr_post);

	INIT_WORK(&aq->work, ionic_admin_work);
	aq->armed = false;

	return aq;

err_wr:
	ionic_queue_destroy(&aq->q, dev->lif_cfg.hwdev);
err_q:
	kfree(aq);

	return ERR_PTR(rc);
}

static void __ionic_destroy_rdma_adminq(struct ionic_ibdev *dev,
					struct ionic_aq *aq)
{
	ionic_queue_destroy(&aq->q, dev->lif_cfg.hwdev);
	kfree(aq);
}

static struct ionic_aq *ionic_create_rdma_adminq(struct ionic_ibdev *dev,
						 u32 aqid, u32 cqid)
{
	struct ionic_aq *aq;
	int rc;

	aq = __ionic_create_rdma_adminq(dev, aqid, cqid);
	if (IS_ERR(aq))
		return aq;

	rc = ionic_rdma_queue_devcmd(dev, &aq->q, aq->aqid, aq->cqid,
				     IONIC_CMD_RDMA_CREATE_ADMINQ);
	if (rc)
		goto err_cmd;

	return aq;

err_cmd:
	__ionic_destroy_rdma_adminq(dev, aq);

	return ERR_PTR(rc);
}

static void ionic_flush_qs(struct ionic_ibdev *dev)
{
	struct ionic_qp *qp, *qp_tmp;
	struct ionic_cq *cq, *cq_tmp;
	LIST_HEAD(flush_list);
	unsigned long index;

	WARN_ON(!irqs_disabled());

	/* Flush qp send and recv */
	xa_lock(&dev->qp_tbl);
	xa_for_each(&dev->qp_tbl, index, qp) {
		kref_get(&qp->qp_kref);
		list_add_tail(&qp->ibkill_flush_ent, &flush_list);
	}
	xa_unlock(&dev->qp_tbl);

	list_for_each_entry_safe(qp, qp_tmp, &flush_list, ibkill_flush_ent) {
		ionic_flush_qp(dev, qp);
		kref_put(&qp->qp_kref, ionic_qp_complete);
		list_del(&qp->ibkill_flush_ent);
	}

	/* Notify completions */
	xa_lock(&dev->cq_tbl);
	xa_for_each(&dev->cq_tbl, index, cq) {
		kref_get(&cq->cq_kref);
		list_add_tail(&cq->ibkill_flush_ent, &flush_list);
	}
	xa_unlock(&dev->cq_tbl);

	list_for_each_entry_safe(cq, cq_tmp, &flush_list, ibkill_flush_ent) {
		ionic_notify_flush_cq(cq);
		kref_put(&cq->cq_kref, ionic_cq_complete);
		list_del(&cq->ibkill_flush_ent);
	}
}

static void ionic_kill_ibdev(struct ionic_ibdev *dev, bool fatal_path)
{
	unsigned long irqflags;
	bool do_flush = false;
	int i;

	/* Mark AQs for drain and flush the QPs while irq is disabled */
	local_irq_save(irqflags);

	/* Mark the admin queue, flushing at most once */
	for (i = 0; i < dev->lif_cfg.aq_count; i++) {
		struct ionic_aq *aq = dev->aq_vec[i];

		spin_lock(&aq->lock);
		if (atomic_read(&aq->admin_state) != IONIC_ADMIN_KILLED) {
			atomic_set(&aq->admin_state, IONIC_ADMIN_KILLED);
			/* Flush incomplete admin commands */
			ionic_admin_poll_locked(aq);
			do_flush = true;
		}
		spin_unlock(&aq->lock);
	}

	if (do_flush)
		ionic_flush_qs(dev);

	local_irq_restore(irqflags);

	/* Post a fatal event if requested */
	if (fatal_path) {
		struct ib_event ev;

		ev.device = &dev->ibdev;
		ev.element.port_num = 1;
		ev.event = IB_EVENT_DEVICE_FATAL;

		ib_dispatch_event(&ev);
	}

	atomic_set(&dev->admin_state, IONIC_ADMIN_KILLED);
}

void ionic_kill_rdma_admin(struct ionic_ibdev *dev, bool fatal_path)
{
	enum ionic_admin_state old_state;
	unsigned long irqflags = 0;
	int i, rc;

	if (!dev->aq_vec)
		return;

	/*
	 * Admin queues are transitioned from active to paused to killed state.
	 * When in paused state, no new commands are issued to the device,
	 * nor are any completed locally. After resetting the lif, it will be
	 * safe to resume the rdma admin queues in the killed state. Commands
	 * will not be issued to the device, but will complete locally with status
	 * IONIC_ADMIN_KILLED. Handling completion will ensure that creating or
	 * modifying resources fails, but destroying resources succeeds.
	 * If there was a failure resetting the lif using this strategy,
	 * then the state of the device is unknown.
	 */
	old_state = atomic_cmpxchg(&dev->admin_state, IONIC_ADMIN_ACTIVE,
				   IONIC_ADMIN_PAUSED);
	if (old_state != IONIC_ADMIN_ACTIVE)
		return;

	/* Pause all the AQs */
	local_irq_save(irqflags);
	for (i = 0; i < dev->lif_cfg.aq_count; i++) {
		struct ionic_aq *aq = dev->aq_vec[i];

		spin_lock(&aq->lock);
		/* pause rdma admin queues to reset lif */
		if (atomic_read(&aq->admin_state) == IONIC_ADMIN_ACTIVE)
			atomic_set(&aq->admin_state, IONIC_ADMIN_PAUSED);
		spin_unlock(&aq->lock);
	}
	local_irq_restore(irqflags);

	rc = ionic_rdma_reset_devcmd(dev);
	if (unlikely(rc)) {
		ibdev_err(&dev->ibdev, "failed to reset rdma %d\n", rc);
		ionic_request_rdma_reset(dev->lif_cfg.lif);
	}

	ionic_kill_ibdev(dev, fatal_path);
}

static void ionic_reset_work(struct work_struct *ws)
{
	struct ionic_ibdev *dev =
		container_of(ws, struct ionic_ibdev, reset_work);

	ionic_kill_rdma_admin(dev, true);
}

static bool ionic_next_eqe(struct ionic_eq *eq, struct ionic_v1_eqe *eqe)
{
	struct ionic_v1_eqe *qeqe;
	bool color;

	qeqe = ionic_queue_at_prod(&eq->q);
	color = ionic_v1_eqe_color(qeqe);

	/* cons is color for eq */
	if (eq->q.cons != color)
		return false;

	/* Prevent out-of-order reads of the EQE */
	dma_rmb();

	ibdev_dbg(&eq->dev->ibdev, "poll eq prod %u\n", eq->q.prod);
	print_hex_dump_debug("eqe ", DUMP_PREFIX_OFFSET, 16, 1,
			     qeqe, BIT(eq->q.stride_log2), true);
	*eqe = *qeqe;

	return true;
}

static void ionic_cq_event(struct ionic_ibdev *dev, u32 cqid, u8 code)
{
	unsigned long irqflags;
	struct ib_event ibev;
	struct ionic_cq *cq;

	xa_lock_irqsave(&dev->cq_tbl, irqflags);
	cq = xa_load(&dev->cq_tbl, cqid);
	if (cq)
		kref_get(&cq->cq_kref);
	xa_unlock_irqrestore(&dev->cq_tbl, irqflags);

	if (!cq) {
		ibdev_dbg(&dev->ibdev,
			  "missing cqid %#x code %u\n", cqid, code);
		return;
	}

	switch (code) {
	case IONIC_V1_EQE_CQ_NOTIFY:
		if (cq->vcq->ibcq.comp_handler)
			cq->vcq->ibcq.comp_handler(&cq->vcq->ibcq,
						   cq->vcq->ibcq.cq_context);
		break;

	case IONIC_V1_EQE_CQ_ERR:
		if (cq->vcq->ibcq.event_handler) {
			ibev.event = IB_EVENT_CQ_ERR;
			ibev.device = &dev->ibdev;
			ibev.element.cq = &cq->vcq->ibcq;

			cq->vcq->ibcq.event_handler(&ibev,
						    cq->vcq->ibcq.cq_context);
		}
		break;

	default:
		ibdev_dbg(&dev->ibdev,
			  "unrecognized cqid %#x code %u\n", cqid, code);
		break;
	}

	kref_put(&cq->cq_kref, ionic_cq_complete);
}

static void ionic_qp_event(struct ionic_ibdev *dev, u32 qpid, u8 code)
{
	unsigned long irqflags;
	struct ib_event ibev;
	struct ionic_qp *qp;

	xa_lock_irqsave(&dev->qp_tbl, irqflags);
	qp = xa_load(&dev->qp_tbl, qpid);
	if (qp)
		kref_get(&qp->qp_kref);
	xa_unlock_irqrestore(&dev->qp_tbl, irqflags);

	if (!qp) {
		ibdev_dbg(&dev->ibdev,
			  "missing qpid %#x code %u\n", qpid, code);
		return;
	}

	ibev.device = &dev->ibdev;
	ibev.element.qp = &qp->ibqp;

	switch (code) {
	case IONIC_V1_EQE_SQ_DRAIN:
		ibev.event = IB_EVENT_SQ_DRAINED;
		break;

	case IONIC_V1_EQE_QP_COMM_EST:
		ibev.event = IB_EVENT_COMM_EST;
		break;

	case IONIC_V1_EQE_QP_LAST_WQE:
		ibev.event = IB_EVENT_QP_LAST_WQE_REACHED;
		break;

	case IONIC_V1_EQE_QP_ERR:
		ibev.event = IB_EVENT_QP_FATAL;
		break;

	case IONIC_V1_EQE_QP_ERR_REQUEST:
		ibev.event = IB_EVENT_QP_REQ_ERR;
		break;

	case IONIC_V1_EQE_QP_ERR_ACCESS:
		ibev.event = IB_EVENT_QP_ACCESS_ERR;
		break;

	default:
		ibdev_dbg(&dev->ibdev,
			  "unrecognized qpid %#x code %u\n", qpid, code);
		goto out;
	}

	if (qp->ibqp.event_handler)
		qp->ibqp.event_handler(&ibev, qp->ibqp.qp_context);

out:
	kref_put(&qp->qp_kref, ionic_qp_complete);
}

static u16 ionic_poll_eq(struct ionic_eq *eq, u16 budget)
{
	struct ionic_ibdev *dev = eq->dev;
	struct ionic_v1_eqe eqe;
	u16 npolled = 0;
	u8 type, code;
	u32 evt, qid;

	while (npolled < budget) {
		if (!ionic_next_eqe(eq, &eqe))
			break;

		ionic_queue_produce(&eq->q);

		/* cons is color for eq */
		eq->q.cons = ionic_color_wrap(eq->q.prod, eq->q.cons);

		++npolled;

		evt = ionic_v1_eqe_evt(&eqe);
		type = ionic_v1_eqe_evt_type(evt);
		code = ionic_v1_eqe_evt_code(evt);
		qid = ionic_v1_eqe_evt_qid(evt);

		switch (type) {
		case IONIC_V1_EQE_TYPE_CQ:
			ionic_cq_event(dev, qid, code);
			break;

		case IONIC_V1_EQE_TYPE_QP:
			ionic_qp_event(dev, qid, code);
			break;

		default:
			ibdev_dbg(&dev->ibdev,
				  "unknown event %#x type %u\n", evt, type);
		}
	}

	return npolled;
}

static void ionic_poll_eq_work(struct work_struct *work)
{
	struct ionic_eq *eq = container_of(work, struct ionic_eq, work);
	u32 npolled;

	if (unlikely(!eq->enable) || WARN_ON(eq->armed))
		return;

	npolled = ionic_poll_eq(eq, IONIC_EQ_WORK_BUDGET);
	if (npolled == IONIC_EQ_WORK_BUDGET) {
		ionic_intr_credits(eq->dev->lif_cfg.intr_ctrl, eq->intr,
				   npolled, 0);
		queue_work(ionic_evt_workq, &eq->work);
	} else {
		xchg(&eq->armed, 1);
		ionic_intr_credits(eq->dev->lif_cfg.intr_ctrl, eq->intr,
				   0, IONIC_INTR_CRED_UNMASK);
	}
}

static irqreturn_t ionic_poll_eq_isr(int irq, void *eqptr)
{
	struct ionic_eq *eq = eqptr;
	int was_armed;
	u32 npolled;

	was_armed = xchg(&eq->armed, 0);

	if (unlikely(!eq->enable) || !was_armed)
		return IRQ_HANDLED;

	npolled = ionic_poll_eq(eq, IONIC_EQ_ISR_BUDGET);
	if (npolled == IONIC_EQ_ISR_BUDGET) {
		ionic_intr_credits(eq->dev->lif_cfg.intr_ctrl, eq->intr,
				   npolled, 0);
		queue_work(ionic_evt_workq, &eq->work);
	} else {
		xchg(&eq->armed, 1);
		ionic_intr_credits(eq->dev->lif_cfg.intr_ctrl, eq->intr,
				   0, IONIC_INTR_CRED_UNMASK);
	}

	return IRQ_HANDLED;
}

static struct ionic_eq *ionic_create_eq(struct ionic_ibdev *dev, int eqid)
{
	struct ionic_intr_info intr_obj = { };
	struct ionic_eq *eq;
	int rc;

	eq = kzalloc(sizeof(*eq), GFP_KERNEL);
	if (!eq)
		return ERR_PTR(-ENOMEM);

	eq->dev = dev;

	rc = ionic_queue_init(&eq->q, dev->lif_cfg.hwdev, IONIC_EQ_DEPTH,
			      sizeof(struct ionic_v1_eqe));
	if (rc)
		goto err_q;

	eq->eqid = eqid;

	eq->armed = true;
	eq->enable = false;
	INIT_WORK(&eq->work, ionic_poll_eq_work);

	rc = ionic_intr_alloc(dev->lif_cfg.lif, &intr_obj);
	if (rc < 0)
		goto err_intr;

	eq->irq = intr_obj.vector;
	eq->intr = intr_obj.index;

	ionic_queue_dbell_init(&eq->q, eq->eqid);

	/* cons is color for eq */
	eq->q.cons = true;

	snprintf(eq->name, sizeof(eq->name), "%s-%d-%d-eq",
		 "ionr", dev->lif_cfg.lif_index, eq->eqid);

	ionic_intr_mask(dev->lif_cfg.intr_ctrl, eq->intr, IONIC_INTR_MASK_SET);
	ionic_intr_mask_assert(dev->lif_cfg.intr_ctrl, eq->intr, IONIC_INTR_MASK_SET);
	ionic_intr_coal_init(dev->lif_cfg.intr_ctrl, eq->intr, 0);
	ionic_intr_clean(dev->lif_cfg.intr_ctrl, eq->intr);

	eq->enable = true;

	rc = request_irq(eq->irq, ionic_poll_eq_isr, 0, eq->name, eq);
	if (rc)
		goto err_irq;

	rc = ionic_rdma_queue_devcmd(dev, &eq->q, eq->eqid, eq->intr,
				     IONIC_CMD_RDMA_CREATE_EQ);
	if (rc)
		goto err_cmd;

	ionic_intr_mask(dev->lif_cfg.intr_ctrl, eq->intr, IONIC_INTR_MASK_CLEAR);

	return eq;

err_cmd:
	eq->enable = false;
	free_irq(eq->irq, eq);
	flush_work(&eq->work);
err_irq:
	ionic_intr_free(dev->lif_cfg.lif, eq->intr);
err_intr:
	ionic_queue_destroy(&eq->q, dev->lif_cfg.hwdev);
err_q:
	kfree(eq);

	return ERR_PTR(rc);
}

static void ionic_destroy_eq(struct ionic_eq *eq)
{
	struct ionic_ibdev *dev = eq->dev;

	eq->enable = false;
	free_irq(eq->irq, eq);
	flush_work(&eq->work);

	ionic_intr_free(dev->lif_cfg.lif, eq->intr);
	ionic_queue_destroy(&eq->q, dev->lif_cfg.hwdev);
	kfree(eq);
}

int ionic_create_rdma_admin(struct ionic_ibdev *dev)
{
	int eq_i = 0, aq_i = 0, rc = 0;
	struct ionic_vcq *vcq;
	struct ionic_aq *aq;
	struct ionic_eq *eq;

	dev->eq_vec = NULL;
	dev->aq_vec = NULL;

	INIT_WORK(&dev->reset_work, ionic_reset_work);
	INIT_DELAYED_WORK(&dev->admin_dwork, ionic_admin_dwork);
	atomic_set(&dev->admin_state, IONIC_ADMIN_KILLED);

	if (dev->lif_cfg.aq_count > IONIC_AQ_COUNT) {
		ibdev_dbg(&dev->ibdev, "limiting adminq count to %d\n",
			  IONIC_AQ_COUNT);
		dev->lif_cfg.aq_count = IONIC_AQ_COUNT;
	}

	if (dev->lif_cfg.eq_count > IONIC_EQ_COUNT) {
		dev_dbg(&dev->ibdev.dev, "limiting eventq count to %d\n",
			IONIC_EQ_COUNT);
		dev->lif_cfg.eq_count = IONIC_EQ_COUNT;
	}

	/* need at least two eq and one aq */
	if (dev->lif_cfg.eq_count < IONIC_EQ_COUNT_MIN ||
	    dev->lif_cfg.aq_count < IONIC_AQ_COUNT_MIN) {
		rc = -EINVAL;
		goto out;
	}

	dev->eq_vec = kmalloc_array(dev->lif_cfg.eq_count, sizeof(*dev->eq_vec),
				    GFP_KERNEL);
	if (!dev->eq_vec) {
		rc = -ENOMEM;
		goto out;
	}

	for (eq_i = 0; eq_i < dev->lif_cfg.eq_count; ++eq_i) {
		eq = ionic_create_eq(dev, eq_i + dev->lif_cfg.eq_base);
		if (IS_ERR(eq)) {
			rc = PTR_ERR(eq);

			if (eq_i < IONIC_EQ_COUNT_MIN) {
				ibdev_err(&dev->ibdev,
					  "fail create eq %d\n", rc);
				goto out;
			}

			/* ok, just fewer eq than device supports */
			ibdev_dbg(&dev->ibdev, "eq count %d want %d rc %d\n",
				  eq_i, dev->lif_cfg.eq_count, rc);

			rc = 0;
			break;
		}

		dev->eq_vec[eq_i] = eq;
	}

	dev->lif_cfg.eq_count = eq_i;

	dev->aq_vec = kmalloc_array(dev->lif_cfg.aq_count, sizeof(*dev->aq_vec),
				    GFP_KERNEL);
	if (!dev->aq_vec) {
		rc = -ENOMEM;
		goto out;
	}

	/* Create one CQ per AQ */
	for (aq_i = 0; aq_i < dev->lif_cfg.aq_count; ++aq_i) {
		vcq = ionic_create_rdma_admincq(dev, aq_i % eq_i);
		if (IS_ERR(vcq)) {
			rc = PTR_ERR(vcq);

			if (!aq_i) {
				ibdev_err(&dev->ibdev,
					  "failed to create acq %d\n", rc);
				goto out;
			}

			/* ok, just fewer adminq than device supports */
			ibdev_dbg(&dev->ibdev, "acq count %d want %d rc %d\n",
				  aq_i, dev->lif_cfg.aq_count, rc);
			break;
		}

		aq = ionic_create_rdma_adminq(dev, aq_i + dev->lif_cfg.aq_base,
					      vcq->cq[0].cqid);
		if (IS_ERR(aq)) {
			/* Clean up the dangling CQ */
			ionic_destroy_cq_common(dev, &vcq->cq[0]);
			kfree(vcq);

			rc = PTR_ERR(aq);

			if (!aq_i) {
				ibdev_err(&dev->ibdev,
					  "failed to create aq %d\n", rc);
				goto out;
			}

			/* ok, just fewer adminq than device supports */
			ibdev_dbg(&dev->ibdev, "aq count %d want %d rc %d\n",
				  aq_i, dev->lif_cfg.aq_count, rc);
			break;
		}

		vcq->ibcq.cq_context = aq;
		aq->vcq = vcq;

		atomic_set(&aq->admin_state, IONIC_ADMIN_ACTIVE);
		dev->aq_vec[aq_i] = aq;
	}

	atomic_set(&dev->admin_state, IONIC_ADMIN_ACTIVE);
out:
	dev->lif_cfg.eq_count = eq_i;
	dev->lif_cfg.aq_count = aq_i;

	return rc;
}

void ionic_destroy_rdma_admin(struct ionic_ibdev *dev)
{
	struct ionic_vcq *vcq;
	struct ionic_aq *aq;
	struct ionic_eq *eq;

	/*
	 * Killing the admin before destroy makes sure all admin and
	 * completions are flushed. admin_state = IONIC_ADMIN_KILLED
	 * stops queueing up further works.
	 */
	cancel_delayed_work_sync(&dev->admin_dwork);
	cancel_work_sync(&dev->reset_work);

	if (dev->aq_vec) {
		while (dev->lif_cfg.aq_count > 0) {
			aq = dev->aq_vec[--dev->lif_cfg.aq_count];
			vcq = aq->vcq;

			cancel_work_sync(&aq->work);

			__ionic_destroy_rdma_adminq(dev, aq);
			if (vcq) {
				ionic_destroy_cq_common(dev, &vcq->cq[0]);
				kfree(vcq);
			}
		}

		kfree(dev->aq_vec);
	}

	if (dev->eq_vec) {
		while (dev->lif_cfg.eq_count > 0) {
			eq = dev->eq_vec[--dev->lif_cfg.eq_count];
			ionic_destroy_eq(eq);
		}

		kfree(dev->eq_vec);
	}
}
