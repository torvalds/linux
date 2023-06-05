// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#include <linux/dynamic_debug.h>

#include "core.h"

struct pdsc_wait_context {
	struct pdsc_qcq *qcq;
	struct completion wait_completion;
};

static int pdsc_process_notifyq(struct pdsc_qcq *qcq)
{
	union pds_core_notifyq_comp *comp;
	struct pdsc *pdsc = qcq->pdsc;
	struct pdsc_cq *cq = &qcq->cq;
	struct pdsc_cq_info *cq_info;
	int nq_work = 0;
	u64 eid;

	cq_info = &cq->info[cq->tail_idx];
	comp = cq_info->comp;
	eid = le64_to_cpu(comp->event.eid);
	while (eid > pdsc->last_eid) {
		u16 ecode = le16_to_cpu(comp->event.ecode);

		switch (ecode) {
		case PDS_EVENT_LINK_CHANGE:
			dev_info(pdsc->dev, "NotifyQ LINK_CHANGE ecode %d eid %lld\n",
				 ecode, eid);
			pdsc_notify(PDS_EVENT_LINK_CHANGE, comp);
			break;

		case PDS_EVENT_RESET:
			dev_info(pdsc->dev, "NotifyQ RESET ecode %d eid %lld\n",
				 ecode, eid);
			pdsc_notify(PDS_EVENT_RESET, comp);
			break;

		case PDS_EVENT_XCVR:
			dev_info(pdsc->dev, "NotifyQ XCVR ecode %d eid %lld\n",
				 ecode, eid);
			break;

		default:
			dev_info(pdsc->dev, "NotifyQ ecode %d eid %lld\n",
				 ecode, eid);
			break;
		}

		pdsc->last_eid = eid;
		cq->tail_idx = (cq->tail_idx + 1) & (cq->num_descs - 1);
		cq_info = &cq->info[cq->tail_idx];
		comp = cq_info->comp;
		eid = le64_to_cpu(comp->event.eid);

		nq_work++;
	}

	qcq->accum_work += nq_work;

	return nq_work;
}

void pdsc_process_adminq(struct pdsc_qcq *qcq)
{
	union pds_core_adminq_comp *comp;
	struct pdsc_queue *q = &qcq->q;
	struct pdsc *pdsc = qcq->pdsc;
	struct pdsc_cq *cq = &qcq->cq;
	struct pdsc_q_info *q_info;
	unsigned long irqflags;
	int nq_work = 0;
	int aq_work = 0;
	int credits;

	/* Don't process AdminQ when shutting down */
	if (pdsc->state & BIT_ULL(PDSC_S_STOPPING_DRIVER)) {
		dev_err(pdsc->dev, "%s: called while PDSC_S_STOPPING_DRIVER\n",
			__func__);
		return;
	}

	/* Check for NotifyQ event */
	nq_work = pdsc_process_notifyq(&pdsc->notifyqcq);

	/* Check for empty queue, which can happen if the interrupt was
	 * for a NotifyQ event and there are no new AdminQ completions.
	 */
	if (q->tail_idx == q->head_idx)
		goto credits;

	/* Find the first completion to clean,
	 * run the callback in the related q_info,
	 * and continue while we still match done color
	 */
	spin_lock_irqsave(&pdsc->adminq_lock, irqflags);
	comp = cq->info[cq->tail_idx].comp;
	while (pdsc_color_match(comp->color, cq->done_color)) {
		q_info = &q->info[q->tail_idx];
		q->tail_idx = (q->tail_idx + 1) & (q->num_descs - 1);

		/* Copy out the completion data */
		memcpy(q_info->dest, comp, sizeof(*comp));

		complete_all(&q_info->wc->wait_completion);

		if (cq->tail_idx == cq->num_descs - 1)
			cq->done_color = !cq->done_color;
		cq->tail_idx = (cq->tail_idx + 1) & (cq->num_descs - 1);
		comp = cq->info[cq->tail_idx].comp;

		aq_work++;
	}
	spin_unlock_irqrestore(&pdsc->adminq_lock, irqflags);

	qcq->accum_work += aq_work;

credits:
	/* Return the interrupt credits, one for each completion */
	credits = nq_work + aq_work;
	if (credits)
		pds_core_intr_credits(&pdsc->intr_ctrl[qcq->intx],
				      credits,
				      PDS_CORE_INTR_CRED_REARM);
}

void pdsc_work_thread(struct work_struct *work)
{
	struct pdsc_qcq *qcq = container_of(work, struct pdsc_qcq, work);

	pdsc_process_adminq(qcq);
}

irqreturn_t pdsc_adminq_isr(int irq, void *data)
{
	struct pdsc_qcq *qcq = data;
	struct pdsc *pdsc = qcq->pdsc;

	/* Don't process AdminQ when shutting down */
	if (pdsc->state & BIT_ULL(PDSC_S_STOPPING_DRIVER)) {
		dev_err(pdsc->dev, "%s: called while PDSC_S_STOPPING_DRIVER\n",
			__func__);
		return IRQ_HANDLED;
	}

	queue_work(pdsc->wq, &qcq->work);
	pds_core_intr_mask(&pdsc->intr_ctrl[irq], PDS_CORE_INTR_MASK_CLEAR);

	return IRQ_HANDLED;
}

static int __pdsc_adminq_post(struct pdsc *pdsc,
			      struct pdsc_qcq *qcq,
			      union pds_core_adminq_cmd *cmd,
			      union pds_core_adminq_comp *comp,
			      struct pdsc_wait_context *wc)
{
	struct pdsc_queue *q = &qcq->q;
	struct pdsc_q_info *q_info;
	unsigned long irqflags;
	unsigned int avail;
	int index;
	int ret;

	spin_lock_irqsave(&pdsc->adminq_lock, irqflags);

	/* Check for space in the queue */
	avail = q->tail_idx;
	if (q->head_idx >= avail)
		avail += q->num_descs - q->head_idx - 1;
	else
		avail -= q->head_idx + 1;
	if (!avail) {
		ret = -ENOSPC;
		goto err_out_unlock;
	}

	/* Check that the FW is running */
	if (!pdsc_is_fw_running(pdsc)) {
		u8 fw_status = ioread8(&pdsc->info_regs->fw_status);

		dev_info(pdsc->dev, "%s: post failed - fw not running %#02x:\n",
			 __func__, fw_status);
		ret = -ENXIO;

		goto err_out_unlock;
	}

	/* Post the request */
	index = q->head_idx;
	q_info = &q->info[index];
	q_info->wc = wc;
	q_info->dest = comp;
	memcpy(q_info->desc, cmd, sizeof(*cmd));

	dev_dbg(pdsc->dev, "head_idx %d tail_idx %d\n",
		q->head_idx, q->tail_idx);
	dev_dbg(pdsc->dev, "post admin queue command:\n");
	dynamic_hex_dump("cmd ", DUMP_PREFIX_OFFSET, 16, 1,
			 cmd, sizeof(*cmd), true);

	q->head_idx = (q->head_idx + 1) & (q->num_descs - 1);

	pds_core_dbell_ring(pdsc->kern_dbpage,
			    q->hw_type, q->dbval | q->head_idx);
	ret = index;

err_out_unlock:
	spin_unlock_irqrestore(&pdsc->adminq_lock, irqflags);
	return ret;
}

int pdsc_adminq_post(struct pdsc *pdsc,
		     union pds_core_adminq_cmd *cmd,
		     union pds_core_adminq_comp *comp,
		     bool fast_poll)
{
	struct pdsc_wait_context wc = {
		.wait_completion =
			COMPLETION_INITIALIZER_ONSTACK(wc.wait_completion),
	};
	unsigned long poll_interval = 1;
	unsigned long poll_jiffies;
	unsigned long time_limit;
	unsigned long time_start;
	unsigned long time_done;
	unsigned long remaining;
	int err = 0;
	int index;

	wc.qcq = &pdsc->adminqcq;
	index = __pdsc_adminq_post(pdsc, &pdsc->adminqcq, cmd, comp, &wc);
	if (index < 0) {
		err = index;
		goto err_out;
	}

	time_start = jiffies;
	time_limit = time_start + HZ * pdsc->devcmd_timeout;
	do {
		/* Timeslice the actual wait to catch IO errors etc early */
		poll_jiffies = msecs_to_jiffies(poll_interval);
		remaining = wait_for_completion_timeout(&wc.wait_completion,
							poll_jiffies);
		if (remaining)
			break;

		if (!pdsc_is_fw_running(pdsc)) {
			u8 fw_status = ioread8(&pdsc->info_regs->fw_status);

			dev_dbg(pdsc->dev, "%s: post wait failed - fw not running %#02x:\n",
				__func__, fw_status);
			err = -ENXIO;
			break;
		}

		/* When fast_poll is not requested, prevent aggressive polling
		 * on failures due to timeouts by doing exponential back off.
		 */
		if (!fast_poll && poll_interval < PDSC_ADMINQ_MAX_POLL_INTERVAL)
			poll_interval <<= 1;
	} while (time_before(jiffies, time_limit));
	time_done = jiffies;
	dev_dbg(pdsc->dev, "%s: elapsed %d msecs\n",
		__func__, jiffies_to_msecs(time_done - time_start));

	/* Check the results */
	if (time_after_eq(time_done, time_limit))
		err = -ETIMEDOUT;

	dev_dbg(pdsc->dev, "read admin queue completion idx %d:\n", index);
	dynamic_hex_dump("comp ", DUMP_PREFIX_OFFSET, 16, 1,
			 comp, sizeof(*comp), true);

	if (remaining && comp->status)
		err = pdsc_err_to_errno(comp->status);

err_out:
	if (err) {
		dev_dbg(pdsc->dev, "%s: opcode %d status %d err %pe\n",
			__func__, cmd->opcode, comp->status, ERR_PTR(err));
		if (err == -ENXIO || err == -ETIMEDOUT)
			queue_work(pdsc->wq, &pdsc->health_work);
	}

	return err;
}
EXPORT_SYMBOL_GPL(pdsc_adminq_post);
