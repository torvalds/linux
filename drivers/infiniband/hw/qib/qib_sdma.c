/*
 * Copyright (c) 2012 Intel Corporation. All rights reserved.
 * Copyright (c) 2007 - 2012 QLogic Corporation. All rights reserved.
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
 *        disclaimer in the documentation and/or other materials
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

#include <linux/spinlock.h>
#include <linux/netdevice.h>
#include <linux/moduleparam.h>

#include "qib.h"
#include "qib_common.h"

/* default pio off, sdma on */
static ushort sdma_descq_cnt = 256;
module_param_named(sdma_descq_cnt, sdma_descq_cnt, ushort, S_IRUGO);
MODULE_PARM_DESC(sdma_descq_cnt, "Number of SDMA descq entries");

/*
 * Bits defined in the send DMA descriptor.
 */
#define SDMA_DESC_LAST          (1ULL << 11)
#define SDMA_DESC_FIRST         (1ULL << 12)
#define SDMA_DESC_DMA_HEAD      (1ULL << 13)
#define SDMA_DESC_USE_LARGE_BUF (1ULL << 14)
#define SDMA_DESC_INTR          (1ULL << 15)
#define SDMA_DESC_COUNT_LSB     16
#define SDMA_DESC_GEN_LSB       30

/* declare all statics here rather than keep sorting */
static int alloc_sdma(struct qib_pportdata *);
static void sdma_complete(struct kref *);
static void sdma_finalput(struct qib_sdma_state *);
static void sdma_get(struct qib_sdma_state *);
static void sdma_put(struct qib_sdma_state *);
static void sdma_set_state(struct qib_pportdata *, enum qib_sdma_states);
static void sdma_start_sw_clean_up(struct qib_pportdata *);
static void sdma_sw_clean_up_task(unsigned long);
static void unmap_desc(struct qib_pportdata *, unsigned);

static void sdma_get(struct qib_sdma_state *ss)
{
	kref_get(&ss->kref);
}

static void sdma_complete(struct kref *kref)
{
	struct qib_sdma_state *ss =
		container_of(kref, struct qib_sdma_state, kref);

	complete(&ss->comp);
}

static void sdma_put(struct qib_sdma_state *ss)
{
	kref_put(&ss->kref, sdma_complete);
}

static void sdma_finalput(struct qib_sdma_state *ss)
{
	sdma_put(ss);
	wait_for_completion(&ss->comp);
}

/*
 * Complete all the sdma requests on the active list, in the correct
 * order, and with appropriate processing.   Called when cleaning up
 * after sdma shutdown, and when new sdma requests are submitted for
 * a link that is down.   This matches what is done for requests
 * that complete normally, it's just the full list.
 *
 * Must be called with sdma_lock held
 */
static void clear_sdma_activelist(struct qib_pportdata *ppd)
{
	struct qib_sdma_txreq *txp, *txp_next;

	list_for_each_entry_safe(txp, txp_next, &ppd->sdma_activelist, list) {
		list_del_init(&txp->list);
		if (txp->flags & QIB_SDMA_TXREQ_F_FREEDESC) {
			unsigned idx;

			idx = txp->start_idx;
			while (idx != txp->next_descq_idx) {
				unmap_desc(ppd, idx);
				if (++idx == ppd->sdma_descq_cnt)
					idx = 0;
			}
		}
		if (txp->callback)
			(*txp->callback)(txp, QIB_SDMA_TXREQ_S_ABORTED);
	}
}

static void sdma_sw_clean_up_task(unsigned long opaque)
{
	struct qib_pportdata *ppd = (struct qib_pportdata *) opaque;
	unsigned long flags;

	spin_lock_irqsave(&ppd->sdma_lock, flags);

	/*
	 * At this point, the following should always be true:
	 * - We are halted, so no more descriptors are getting retired.
	 * - We are not running, so no one is submitting new work.
	 * - Only we can send the e40_sw_cleaned, so we can't start
	 *   running again until we say so.  So, the active list and
	 *   descq are ours to play with.
	 */

	/* Process all retired requests. */
	qib_sdma_make_progress(ppd);

	clear_sdma_activelist(ppd);

	/*
	 * Resync count of added and removed.  It is VERY important that
	 * sdma_descq_removed NEVER decrement - user_sdma depends on it.
	 */
	ppd->sdma_descq_removed = ppd->sdma_descq_added;

	/*
	 * Reset our notion of head and tail.
	 * Note that the HW registers will be reset when switching states
	 * due to calling __qib_sdma_process_event() below.
	 */
	ppd->sdma_descq_tail = 0;
	ppd->sdma_descq_head = 0;
	ppd->sdma_head_dma[0] = 0;
	ppd->sdma_generation = 0;

	__qib_sdma_process_event(ppd, qib_sdma_event_e40_sw_cleaned);

	spin_unlock_irqrestore(&ppd->sdma_lock, flags);
}

/*
 * This is called when changing to state qib_sdma_state_s10_hw_start_up_wait
 * as a result of send buffer errors or send DMA descriptor errors.
 * We want to disarm the buffers in these cases.
 */
static void sdma_hw_start_up(struct qib_pportdata *ppd)
{
	struct qib_sdma_state *ss = &ppd->sdma_state;
	unsigned bufno;

	for (bufno = ss->first_sendbuf; bufno < ss->last_sendbuf; ++bufno)
		ppd->dd->f_sendctrl(ppd, QIB_SENDCTRL_DISARM_BUF(bufno));

	ppd->dd->f_sdma_hw_start_up(ppd);
}

static void sdma_sw_tear_down(struct qib_pportdata *ppd)
{
	struct qib_sdma_state *ss = &ppd->sdma_state;

	/* Releasing this reference means the state machine has stopped. */
	sdma_put(ss);
}

static void sdma_start_sw_clean_up(struct qib_pportdata *ppd)
{
	tasklet_hi_schedule(&ppd->sdma_sw_clean_up_task);
}

static void sdma_set_state(struct qib_pportdata *ppd,
	enum qib_sdma_states next_state)
{
	struct qib_sdma_state *ss = &ppd->sdma_state;
	struct sdma_set_state_action *action = ss->set_state_action;
	unsigned op = 0;

	/* debugging bookkeeping */
	ss->previous_state = ss->current_state;
	ss->previous_op = ss->current_op;

	ss->current_state = next_state;

	if (action[next_state].op_enable)
		op |= QIB_SDMA_SENDCTRL_OP_ENABLE;

	if (action[next_state].op_intenable)
		op |= QIB_SDMA_SENDCTRL_OP_INTENABLE;

	if (action[next_state].op_halt)
		op |= QIB_SDMA_SENDCTRL_OP_HALT;

	if (action[next_state].op_drain)
		op |= QIB_SDMA_SENDCTRL_OP_DRAIN;

	if (action[next_state].go_s99_running_tofalse)
		ss->go_s99_running = 0;

	if (action[next_state].go_s99_running_totrue)
		ss->go_s99_running = 1;

	ss->current_op = op;

	ppd->dd->f_sdma_sendctrl(ppd, ss->current_op);
}

static void unmap_desc(struct qib_pportdata *ppd, unsigned head)
{
	__le64 *descqp = &ppd->sdma_descq[head].qw[0];
	u64 desc[2];
	dma_addr_t addr;
	size_t len;

	desc[0] = le64_to_cpu(descqp[0]);
	desc[1] = le64_to_cpu(descqp[1]);

	addr = (desc[1] << 32) | (desc[0] >> 32);
	len = (desc[0] >> 14) & (0x7ffULL << 2);
	dma_unmap_single(&ppd->dd->pcidev->dev, addr, len, DMA_TO_DEVICE);
}

static int alloc_sdma(struct qib_pportdata *ppd)
{
	ppd->sdma_descq_cnt = sdma_descq_cnt;
	if (!ppd->sdma_descq_cnt)
		ppd->sdma_descq_cnt = 256;

	/* Allocate memory for SendDMA descriptor FIFO */
	ppd->sdma_descq = dma_alloc_coherent(&ppd->dd->pcidev->dev,
		ppd->sdma_descq_cnt * sizeof(u64[2]), &ppd->sdma_descq_phys,
		GFP_KERNEL);

	if (!ppd->sdma_descq) {
		qib_dev_err(ppd->dd,
			"failed to allocate SendDMA descriptor FIFO memory\n");
		goto bail;
	}

	/* Allocate memory for DMA of head register to memory */
	ppd->sdma_head_dma = dma_alloc_coherent(&ppd->dd->pcidev->dev,
		PAGE_SIZE, &ppd->sdma_head_phys, GFP_KERNEL);
	if (!ppd->sdma_head_dma) {
		qib_dev_err(ppd->dd,
			"failed to allocate SendDMA head memory\n");
		goto cleanup_descq;
	}
	ppd->sdma_head_dma[0] = 0;
	return 0;

cleanup_descq:
	dma_free_coherent(&ppd->dd->pcidev->dev,
		ppd->sdma_descq_cnt * sizeof(u64[2]), (void *)ppd->sdma_descq,
		ppd->sdma_descq_phys);
	ppd->sdma_descq = NULL;
	ppd->sdma_descq_phys = 0;
bail:
	ppd->sdma_descq_cnt = 0;
	return -ENOMEM;
}

static void free_sdma(struct qib_pportdata *ppd)
{
	struct qib_devdata *dd = ppd->dd;

	if (ppd->sdma_head_dma) {
		dma_free_coherent(&dd->pcidev->dev, PAGE_SIZE,
				  (void *)ppd->sdma_head_dma,
				  ppd->sdma_head_phys);
		ppd->sdma_head_dma = NULL;
		ppd->sdma_head_phys = 0;
	}

	if (ppd->sdma_descq) {
		dma_free_coherent(&dd->pcidev->dev,
				  ppd->sdma_descq_cnt * sizeof(u64[2]),
				  ppd->sdma_descq, ppd->sdma_descq_phys);
		ppd->sdma_descq = NULL;
		ppd->sdma_descq_phys = 0;
	}
}

static inline void make_sdma_desc(struct qib_pportdata *ppd,
				  u64 *sdmadesc, u64 addr, u64 dwlen,
				  u64 dwoffset)
{

	WARN_ON(addr & 3);
	/* SDmaPhyAddr[47:32] */
	sdmadesc[1] = addr >> 32;
	/* SDmaPhyAddr[31:0] */
	sdmadesc[0] = (addr & 0xfffffffcULL) << 32;
	/* SDmaGeneration[1:0] */
	sdmadesc[0] |= (ppd->sdma_generation & 3ULL) <<
		SDMA_DESC_GEN_LSB;
	/* SDmaDwordCount[10:0] */
	sdmadesc[0] |= (dwlen & 0x7ffULL) << SDMA_DESC_COUNT_LSB;
	/* SDmaBufOffset[12:2] */
	sdmadesc[0] |= dwoffset & 0x7ffULL;
}

/* sdma_lock must be held */
int qib_sdma_make_progress(struct qib_pportdata *ppd)
{
	struct list_head *lp = NULL;
	struct qib_sdma_txreq *txp = NULL;
	struct qib_devdata *dd = ppd->dd;
	int progress = 0;
	u16 hwhead;
	u16 idx = 0;

	hwhead = dd->f_sdma_gethead(ppd);

	/* The reason for some of the complexity of this code is that
	 * not all descriptors have corresponding txps.  So, we have to
	 * be able to skip over descs until we wander into the range of
	 * the next txp on the list.
	 */

	if (!list_empty(&ppd->sdma_activelist)) {
		lp = ppd->sdma_activelist.next;
		txp = list_entry(lp, struct qib_sdma_txreq, list);
		idx = txp->start_idx;
	}

	while (ppd->sdma_descq_head != hwhead) {
		/* if desc is part of this txp, unmap if needed */
		if (txp && (txp->flags & QIB_SDMA_TXREQ_F_FREEDESC) &&
		    (idx == ppd->sdma_descq_head)) {
			unmap_desc(ppd, ppd->sdma_descq_head);
			if (++idx == ppd->sdma_descq_cnt)
				idx = 0;
		}

		/* increment dequed desc count */
		ppd->sdma_descq_removed++;

		/* advance head, wrap if needed */
		if (++ppd->sdma_descq_head == ppd->sdma_descq_cnt)
			ppd->sdma_descq_head = 0;

		/* if now past this txp's descs, do the callback */
		if (txp && txp->next_descq_idx == ppd->sdma_descq_head) {
			/* remove from active list */
			list_del_init(&txp->list);
			if (txp->callback)
				(*txp->callback)(txp, QIB_SDMA_TXREQ_S_OK);
			/* see if there is another txp */
			if (list_empty(&ppd->sdma_activelist))
				txp = NULL;
			else {
				lp = ppd->sdma_activelist.next;
				txp = list_entry(lp, struct qib_sdma_txreq,
					list);
				idx = txp->start_idx;
			}
		}
		progress = 1;
	}
	if (progress)
		qib_verbs_sdma_desc_avail(ppd, qib_sdma_descq_freecnt(ppd));
	return progress;
}

/*
 * This is called from interrupt context.
 */
void qib_sdma_intr(struct qib_pportdata *ppd)
{
	unsigned long flags;

	spin_lock_irqsave(&ppd->sdma_lock, flags);

	__qib_sdma_intr(ppd);

	spin_unlock_irqrestore(&ppd->sdma_lock, flags);
}

void __qib_sdma_intr(struct qib_pportdata *ppd)
{
	if (__qib_sdma_running(ppd)) {
		qib_sdma_make_progress(ppd);
		if (!list_empty(&ppd->sdma_userpending))
			qib_user_sdma_send_desc(ppd, &ppd->sdma_userpending);
	}
}

int qib_setup_sdma(struct qib_pportdata *ppd)
{
	struct qib_devdata *dd = ppd->dd;
	unsigned long flags;
	int ret = 0;

	ret = alloc_sdma(ppd);
	if (ret)
		goto bail;

	/* set consistent sdma state */
	ppd->dd->f_sdma_init_early(ppd);
	spin_lock_irqsave(&ppd->sdma_lock, flags);
	sdma_set_state(ppd, qib_sdma_state_s00_hw_down);
	spin_unlock_irqrestore(&ppd->sdma_lock, flags);

	/* set up reference counting */
	kref_init(&ppd->sdma_state.kref);
	init_completion(&ppd->sdma_state.comp);

	ppd->sdma_generation = 0;
	ppd->sdma_descq_head = 0;
	ppd->sdma_descq_removed = 0;
	ppd->sdma_descq_added = 0;

	ppd->sdma_intrequest = 0;
	INIT_LIST_HEAD(&ppd->sdma_userpending);

	INIT_LIST_HEAD(&ppd->sdma_activelist);

	tasklet_init(&ppd->sdma_sw_clean_up_task, sdma_sw_clean_up_task,
		(unsigned long)ppd);

	ret = dd->f_init_sdma_regs(ppd);
	if (ret)
		goto bail_alloc;

	qib_sdma_process_event(ppd, qib_sdma_event_e10_go_hw_start);

	return 0;

bail_alloc:
	qib_teardown_sdma(ppd);
bail:
	return ret;
}

void qib_teardown_sdma(struct qib_pportdata *ppd)
{
	qib_sdma_process_event(ppd, qib_sdma_event_e00_go_hw_down);

	/*
	 * This waits for the state machine to exit so it is not
	 * necessary to kill the sdma_sw_clean_up_task to make sure
	 * it is not running.
	 */
	sdma_finalput(&ppd->sdma_state);

	free_sdma(ppd);
}

int qib_sdma_running(struct qib_pportdata *ppd)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&ppd->sdma_lock, flags);
	ret = __qib_sdma_running(ppd);
	spin_unlock_irqrestore(&ppd->sdma_lock, flags);

	return ret;
}

/*
 * Complete a request when sdma not running; likely only request
 * but to simplify the code, always queue it, then process the full
 * activelist.  We process the entire list to ensure that this particular
 * request does get it's callback, but in the correct order.
 * Must be called with sdma_lock held
 */
static void complete_sdma_err_req(struct qib_pportdata *ppd,
				  struct qib_verbs_txreq *tx)
{
	struct qib_qp_priv *priv = tx->qp->priv;

	atomic_inc(&priv->s_dma_busy);
	/* no sdma descriptors, so no unmap_desc */
	tx->txreq.start_idx = 0;
	tx->txreq.next_descq_idx = 0;
	list_add_tail(&tx->txreq.list, &ppd->sdma_activelist);
	clear_sdma_activelist(ppd);
}

/*
 * This function queues one IB packet onto the send DMA queue per call.
 * The caller is responsible for checking:
 * 1) The number of send DMA descriptor entries is less than the size of
 *    the descriptor queue.
 * 2) The IB SGE addresses and lengths are 32-bit aligned
 *    (except possibly the last SGE's length)
 * 3) The SGE addresses are suitable for passing to dma_map_single().
 */
int qib_sdma_verbs_send(struct qib_pportdata *ppd,
			struct rvt_sge_state *ss, u32 dwords,
			struct qib_verbs_txreq *tx)
{
	unsigned long flags;
	struct rvt_sge *sge;
	struct rvt_qp *qp;
	int ret = 0;
	u16 tail;
	__le64 *descqp;
	u64 sdmadesc[2];
	u32 dwoffset;
	dma_addr_t addr;
	struct qib_qp_priv *priv;

	spin_lock_irqsave(&ppd->sdma_lock, flags);

retry:
	if (unlikely(!__qib_sdma_running(ppd))) {
		complete_sdma_err_req(ppd, tx);
		goto unlock;
	}

	if (tx->txreq.sg_count > qib_sdma_descq_freecnt(ppd)) {
		if (qib_sdma_make_progress(ppd))
			goto retry;
		if (ppd->dd->flags & QIB_HAS_SDMA_TIMEOUT)
			ppd->dd->f_sdma_set_desc_cnt(ppd,
					ppd->sdma_descq_cnt / 2);
		goto busy;
	}

	dwoffset = tx->hdr_dwords;
	make_sdma_desc(ppd, sdmadesc, (u64) tx->txreq.addr, dwoffset, 0);

	sdmadesc[0] |= SDMA_DESC_FIRST;
	if (tx->txreq.flags & QIB_SDMA_TXREQ_F_USELARGEBUF)
		sdmadesc[0] |= SDMA_DESC_USE_LARGE_BUF;

	/* write to the descq */
	tail = ppd->sdma_descq_tail;
	descqp = &ppd->sdma_descq[tail].qw[0];
	*descqp++ = cpu_to_le64(sdmadesc[0]);
	*descqp++ = cpu_to_le64(sdmadesc[1]);

	/* increment the tail */
	if (++tail == ppd->sdma_descq_cnt) {
		tail = 0;
		descqp = &ppd->sdma_descq[0].qw[0];
		++ppd->sdma_generation;
	}

	tx->txreq.start_idx = tail;

	sge = &ss->sge;
	while (dwords) {
		u32 dw;
		u32 len;

		len = dwords << 2;
		if (len > sge->length)
			len = sge->length;
		if (len > sge->sge_length)
			len = sge->sge_length;
		dw = (len + 3) >> 2;
		addr = dma_map_single(&ppd->dd->pcidev->dev, sge->vaddr,
				      dw << 2, DMA_TO_DEVICE);
		if (dma_mapping_error(&ppd->dd->pcidev->dev, addr)) {
			ret = -ENOMEM;
			goto unmap;
		}
		sdmadesc[0] = 0;
		make_sdma_desc(ppd, sdmadesc, (u64) addr, dw, dwoffset);
		/* SDmaUseLargeBuf has to be set in every descriptor */
		if (tx->txreq.flags & QIB_SDMA_TXREQ_F_USELARGEBUF)
			sdmadesc[0] |= SDMA_DESC_USE_LARGE_BUF;
		/* write to the descq */
		*descqp++ = cpu_to_le64(sdmadesc[0]);
		*descqp++ = cpu_to_le64(sdmadesc[1]);

		/* increment the tail */
		if (++tail == ppd->sdma_descq_cnt) {
			tail = 0;
			descqp = &ppd->sdma_descq[0].qw[0];
			++ppd->sdma_generation;
		}
		sge->vaddr += len;
		sge->length -= len;
		sge->sge_length -= len;
		if (sge->sge_length == 0) {
			if (--ss->num_sge)
				*sge = *ss->sg_list++;
		} else if (sge->length == 0 && sge->mr->lkey) {
			if (++sge->n >= RVT_SEGSZ) {
				if (++sge->m >= sge->mr->mapsz)
					break;
				sge->n = 0;
			}
			sge->vaddr =
				sge->mr->map[sge->m]->segs[sge->n].vaddr;
			sge->length =
				sge->mr->map[sge->m]->segs[sge->n].length;
		}

		dwoffset += dw;
		dwords -= dw;
	}

	if (!tail)
		descqp = &ppd->sdma_descq[ppd->sdma_descq_cnt].qw[0];
	descqp -= 2;
	descqp[0] |= cpu_to_le64(SDMA_DESC_LAST);
	if (tx->txreq.flags & QIB_SDMA_TXREQ_F_HEADTOHOST)
		descqp[0] |= cpu_to_le64(SDMA_DESC_DMA_HEAD);
	if (tx->txreq.flags & QIB_SDMA_TXREQ_F_INTREQ)
		descqp[0] |= cpu_to_le64(SDMA_DESC_INTR);
	priv = tx->qp->priv;
	atomic_inc(&priv->s_dma_busy);
	tx->txreq.next_descq_idx = tail;
	ppd->dd->f_sdma_update_tail(ppd, tail);
	ppd->sdma_descq_added += tx->txreq.sg_count;
	list_add_tail(&tx->txreq.list, &ppd->sdma_activelist);
	goto unlock;

unmap:
	for (;;) {
		if (!tail)
			tail = ppd->sdma_descq_cnt - 1;
		else
			tail--;
		if (tail == ppd->sdma_descq_tail)
			break;
		unmap_desc(ppd, tail);
	}
	qp = tx->qp;
	priv = qp->priv;
	qib_put_txreq(tx);
	spin_lock(&qp->r_lock);
	spin_lock(&qp->s_lock);
	if (qp->ibqp.qp_type == IB_QPT_RC) {
		/* XXX what about error sending RDMA read responses? */
		if (ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK)
			rvt_error_qp(qp, IB_WC_GENERAL_ERR);
	} else if (qp->s_wqe)
		rvt_send_complete(qp, qp->s_wqe, IB_WC_GENERAL_ERR);
	spin_unlock(&qp->s_lock);
	spin_unlock(&qp->r_lock);
	/* return zero to process the next send work request */
	goto unlock;

busy:
	qp = tx->qp;
	priv = qp->priv;
	spin_lock(&qp->s_lock);
	if (ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK) {
		struct qib_ibdev *dev;

		/*
		 * If we couldn't queue the DMA request, save the info
		 * and try again later rather than destroying the
		 * buffer and undoing the side effects of the copy.
		 */
		tx->ss = ss;
		tx->dwords = dwords;
		priv->s_tx = tx;
		dev = &ppd->dd->verbs_dev;
		spin_lock(&dev->rdi.pending_lock);
		if (list_empty(&priv->iowait)) {
			struct qib_ibport *ibp;

			ibp = &ppd->ibport_data;
			ibp->rvp.n_dmawait++;
			qp->s_flags |= RVT_S_WAIT_DMA_DESC;
			list_add_tail(&priv->iowait, &dev->dmawait);
		}
		spin_unlock(&dev->rdi.pending_lock);
		qp->s_flags &= ~RVT_S_BUSY;
		spin_unlock(&qp->s_lock);
		ret = -EBUSY;
	} else {
		spin_unlock(&qp->s_lock);
		qib_put_txreq(tx);
	}
unlock:
	spin_unlock_irqrestore(&ppd->sdma_lock, flags);
	return ret;
}

/*
 * sdma_lock should be acquired before calling this routine
 */
void dump_sdma_state(struct qib_pportdata *ppd)
{
	struct qib_sdma_desc *descq;
	struct qib_sdma_txreq *txp, *txpnext;
	__le64 *descqp;
	u64 desc[2];
	u64 addr;
	u16 gen, dwlen, dwoffset;
	u16 head, tail, cnt;

	head = ppd->sdma_descq_head;
	tail = ppd->sdma_descq_tail;
	cnt = qib_sdma_descq_freecnt(ppd);
	descq = ppd->sdma_descq;

	qib_dev_porterr(ppd->dd, ppd->port,
		"SDMA ppd->sdma_descq_head: %u\n", head);
	qib_dev_porterr(ppd->dd, ppd->port,
		"SDMA ppd->sdma_descq_tail: %u\n", tail);
	qib_dev_porterr(ppd->dd, ppd->port,
		"SDMA sdma_descq_freecnt: %u\n", cnt);

	/* print info for each entry in the descriptor queue */
	while (head != tail) {
		char flags[6] = { 'x', 'x', 'x', 'x', 'x', 0 };

		descqp = &descq[head].qw[0];
		desc[0] = le64_to_cpu(descqp[0]);
		desc[1] = le64_to_cpu(descqp[1]);
		flags[0] = (desc[0] & 1<<15) ? 'I' : '-';
		flags[1] = (desc[0] & 1<<14) ? 'L' : 'S';
		flags[2] = (desc[0] & 1<<13) ? 'H' : '-';
		flags[3] = (desc[0] & 1<<12) ? 'F' : '-';
		flags[4] = (desc[0] & 1<<11) ? 'L' : '-';
		addr = (desc[1] << 32) | ((desc[0] >> 32) & 0xfffffffcULL);
		gen = (desc[0] >> 30) & 3ULL;
		dwlen = (desc[0] >> 14) & (0x7ffULL << 2);
		dwoffset = (desc[0] & 0x7ffULL) << 2;
		qib_dev_porterr(ppd->dd, ppd->port,
			"SDMA sdmadesc[%u]: flags:%s addr:0x%016llx gen:%u len:%u bytes offset:%u bytes\n",
			 head, flags, addr, gen, dwlen, dwoffset);
		if (++head == ppd->sdma_descq_cnt)
			head = 0;
	}

	/* print dma descriptor indices from the TX requests */
	list_for_each_entry_safe(txp, txpnext, &ppd->sdma_activelist,
				 list)
		qib_dev_porterr(ppd->dd, ppd->port,
			"SDMA txp->start_idx: %u txp->next_descq_idx: %u\n",
			txp->start_idx, txp->next_descq_idx);
}

void qib_sdma_process_event(struct qib_pportdata *ppd,
	enum qib_sdma_events event)
{
	unsigned long flags;

	spin_lock_irqsave(&ppd->sdma_lock, flags);

	__qib_sdma_process_event(ppd, event);

	if (ppd->sdma_state.current_state == qib_sdma_state_s99_running)
		qib_verbs_sdma_desc_avail(ppd, qib_sdma_descq_freecnt(ppd));

	spin_unlock_irqrestore(&ppd->sdma_lock, flags);
}

void __qib_sdma_process_event(struct qib_pportdata *ppd,
	enum qib_sdma_events event)
{
	struct qib_sdma_state *ss = &ppd->sdma_state;

	switch (ss->current_state) {
	case qib_sdma_state_s00_hw_down:
		switch (event) {
		case qib_sdma_event_e00_go_hw_down:
			break;
		case qib_sdma_event_e30_go_running:
			/*
			 * If down, but running requested (usually result
			 * of link up, then we need to start up.
			 * This can happen when hw down is requested while
			 * bringing the link up with traffic active on
			 * 7220, e.g. */
			ss->go_s99_running = 1;
			/* fall through -- and start dma engine */
		case qib_sdma_event_e10_go_hw_start:
			/* This reference means the state machine is started */
			sdma_get(&ppd->sdma_state);
			sdma_set_state(ppd,
				       qib_sdma_state_s10_hw_start_up_wait);
			break;
		case qib_sdma_event_e20_hw_started:
			break;
		case qib_sdma_event_e40_sw_cleaned:
			sdma_sw_tear_down(ppd);
			break;
		case qib_sdma_event_e50_hw_cleaned:
			break;
		case qib_sdma_event_e60_hw_halted:
			break;
		case qib_sdma_event_e70_go_idle:
			break;
		case qib_sdma_event_e7220_err_halted:
			break;
		case qib_sdma_event_e7322_err_halted:
			break;
		case qib_sdma_event_e90_timer_tick:
			break;
		}
		break;

	case qib_sdma_state_s10_hw_start_up_wait:
		switch (event) {
		case qib_sdma_event_e00_go_hw_down:
			sdma_set_state(ppd, qib_sdma_state_s00_hw_down);
			sdma_sw_tear_down(ppd);
			break;
		case qib_sdma_event_e10_go_hw_start:
			break;
		case qib_sdma_event_e20_hw_started:
			sdma_set_state(ppd, ss->go_s99_running ?
				       qib_sdma_state_s99_running :
				       qib_sdma_state_s20_idle);
			break;
		case qib_sdma_event_e30_go_running:
			ss->go_s99_running = 1;
			break;
		case qib_sdma_event_e40_sw_cleaned:
			break;
		case qib_sdma_event_e50_hw_cleaned:
			break;
		case qib_sdma_event_e60_hw_halted:
			break;
		case qib_sdma_event_e70_go_idle:
			ss->go_s99_running = 0;
			break;
		case qib_sdma_event_e7220_err_halted:
			break;
		case qib_sdma_event_e7322_err_halted:
			break;
		case qib_sdma_event_e90_timer_tick:
			break;
		}
		break;

	case qib_sdma_state_s20_idle:
		switch (event) {
		case qib_sdma_event_e00_go_hw_down:
			sdma_set_state(ppd, qib_sdma_state_s00_hw_down);
			sdma_sw_tear_down(ppd);
			break;
		case qib_sdma_event_e10_go_hw_start:
			break;
		case qib_sdma_event_e20_hw_started:
			break;
		case qib_sdma_event_e30_go_running:
			sdma_set_state(ppd, qib_sdma_state_s99_running);
			ss->go_s99_running = 1;
			break;
		case qib_sdma_event_e40_sw_cleaned:
			break;
		case qib_sdma_event_e50_hw_cleaned:
			break;
		case qib_sdma_event_e60_hw_halted:
			break;
		case qib_sdma_event_e70_go_idle:
			break;
		case qib_sdma_event_e7220_err_halted:
			break;
		case qib_sdma_event_e7322_err_halted:
			break;
		case qib_sdma_event_e90_timer_tick:
			break;
		}
		break;

	case qib_sdma_state_s30_sw_clean_up_wait:
		switch (event) {
		case qib_sdma_event_e00_go_hw_down:
			sdma_set_state(ppd, qib_sdma_state_s00_hw_down);
			break;
		case qib_sdma_event_e10_go_hw_start:
			break;
		case qib_sdma_event_e20_hw_started:
			break;
		case qib_sdma_event_e30_go_running:
			ss->go_s99_running = 1;
			break;
		case qib_sdma_event_e40_sw_cleaned:
			sdma_set_state(ppd,
				       qib_sdma_state_s10_hw_start_up_wait);
			sdma_hw_start_up(ppd);
			break;
		case qib_sdma_event_e50_hw_cleaned:
			break;
		case qib_sdma_event_e60_hw_halted:
			break;
		case qib_sdma_event_e70_go_idle:
			ss->go_s99_running = 0;
			break;
		case qib_sdma_event_e7220_err_halted:
			break;
		case qib_sdma_event_e7322_err_halted:
			break;
		case qib_sdma_event_e90_timer_tick:
			break;
		}
		break;

	case qib_sdma_state_s40_hw_clean_up_wait:
		switch (event) {
		case qib_sdma_event_e00_go_hw_down:
			sdma_set_state(ppd, qib_sdma_state_s00_hw_down);
			sdma_start_sw_clean_up(ppd);
			break;
		case qib_sdma_event_e10_go_hw_start:
			break;
		case qib_sdma_event_e20_hw_started:
			break;
		case qib_sdma_event_e30_go_running:
			ss->go_s99_running = 1;
			break;
		case qib_sdma_event_e40_sw_cleaned:
			break;
		case qib_sdma_event_e50_hw_cleaned:
			sdma_set_state(ppd,
				       qib_sdma_state_s30_sw_clean_up_wait);
			sdma_start_sw_clean_up(ppd);
			break;
		case qib_sdma_event_e60_hw_halted:
			break;
		case qib_sdma_event_e70_go_idle:
			ss->go_s99_running = 0;
			break;
		case qib_sdma_event_e7220_err_halted:
			break;
		case qib_sdma_event_e7322_err_halted:
			break;
		case qib_sdma_event_e90_timer_tick:
			break;
		}
		break;

	case qib_sdma_state_s50_hw_halt_wait:
		switch (event) {
		case qib_sdma_event_e00_go_hw_down:
			sdma_set_state(ppd, qib_sdma_state_s00_hw_down);
			sdma_start_sw_clean_up(ppd);
			break;
		case qib_sdma_event_e10_go_hw_start:
			break;
		case qib_sdma_event_e20_hw_started:
			break;
		case qib_sdma_event_e30_go_running:
			ss->go_s99_running = 1;
			break;
		case qib_sdma_event_e40_sw_cleaned:
			break;
		case qib_sdma_event_e50_hw_cleaned:
			break;
		case qib_sdma_event_e60_hw_halted:
			sdma_set_state(ppd,
				       qib_sdma_state_s40_hw_clean_up_wait);
			ppd->dd->f_sdma_hw_clean_up(ppd);
			break;
		case qib_sdma_event_e70_go_idle:
			ss->go_s99_running = 0;
			break;
		case qib_sdma_event_e7220_err_halted:
			break;
		case qib_sdma_event_e7322_err_halted:
			break;
		case qib_sdma_event_e90_timer_tick:
			break;
		}
		break;

	case qib_sdma_state_s99_running:
		switch (event) {
		case qib_sdma_event_e00_go_hw_down:
			sdma_set_state(ppd, qib_sdma_state_s00_hw_down);
			sdma_start_sw_clean_up(ppd);
			break;
		case qib_sdma_event_e10_go_hw_start:
			break;
		case qib_sdma_event_e20_hw_started:
			break;
		case qib_sdma_event_e30_go_running:
			break;
		case qib_sdma_event_e40_sw_cleaned:
			break;
		case qib_sdma_event_e50_hw_cleaned:
			break;
		case qib_sdma_event_e60_hw_halted:
			sdma_set_state(ppd,
				       qib_sdma_state_s30_sw_clean_up_wait);
			sdma_start_sw_clean_up(ppd);
			break;
		case qib_sdma_event_e70_go_idle:
			sdma_set_state(ppd, qib_sdma_state_s50_hw_halt_wait);
			ss->go_s99_running = 0;
			break;
		case qib_sdma_event_e7220_err_halted:
			sdma_set_state(ppd,
				       qib_sdma_state_s30_sw_clean_up_wait);
			sdma_start_sw_clean_up(ppd);
			break;
		case qib_sdma_event_e7322_err_halted:
			sdma_set_state(ppd, qib_sdma_state_s50_hw_halt_wait);
			break;
		case qib_sdma_event_e90_timer_tick:
			break;
		}
		break;
	}

	ss->last_event = event;
}
