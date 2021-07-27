// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

#include "efct_driver.h"
#include "efct_hw.h"
#include "efct_unsol.h"

int
efct_hw_init_queues(struct efct_hw *hw)
{
	struct hw_eq *eq = NULL;
	struct hw_cq *cq = NULL;
	struct hw_wq *wq = NULL;
	struct hw_mq *mq = NULL;

	struct hw_eq *eqs[EFCT_HW_MAX_NUM_EQ];
	struct hw_cq *cqs[EFCT_HW_MAX_NUM_EQ];
	struct hw_rq *rqs[EFCT_HW_MAX_NUM_EQ];
	u32 i = 0, j;

	hw->eq_count = 0;
	hw->cq_count = 0;
	hw->mq_count = 0;
	hw->wq_count = 0;
	hw->rq_count = 0;
	hw->hw_rq_count = 0;
	INIT_LIST_HEAD(&hw->eq_list);

	for (i = 0; i < hw->config.n_eq; i++) {
		/* Create EQ */
		eq = efct_hw_new_eq(hw, EFCT_HW_EQ_DEPTH);
		if (!eq) {
			efct_hw_queue_teardown(hw);
			return -ENOMEM;
		}

		eqs[i] = eq;

		/* Create one MQ */
		if (!i) {
			cq = efct_hw_new_cq(eq,
					    hw->num_qentries[SLI4_QTYPE_CQ]);
			if (!cq) {
				efct_hw_queue_teardown(hw);
				return -ENOMEM;
			}

			mq = efct_hw_new_mq(cq, EFCT_HW_MQ_DEPTH);
			if (!mq) {
				efct_hw_queue_teardown(hw);
				return -ENOMEM;
			}
		}

		/* Create WQ */
		cq = efct_hw_new_cq(eq, hw->num_qentries[SLI4_QTYPE_CQ]);
		if (!cq) {
			efct_hw_queue_teardown(hw);
			return -ENOMEM;
		}

		wq = efct_hw_new_wq(cq, hw->num_qentries[SLI4_QTYPE_WQ]);
		if (!wq) {
			efct_hw_queue_teardown(hw);
			return -ENOMEM;
		}
	}

	/* Create CQ set */
	if (efct_hw_new_cq_set(eqs, cqs, i, hw->num_qentries[SLI4_QTYPE_CQ])) {
		efct_hw_queue_teardown(hw);
		return -EIO;
	}

	/* Create RQ set */
	if (efct_hw_new_rq_set(cqs, rqs, i, EFCT_HW_RQ_ENTRIES_DEF)) {
		efct_hw_queue_teardown(hw);
		return -EIO;
	}

	for (j = 0; j < i ; j++) {
		rqs[j]->filter_mask = 0;
		rqs[j]->is_mrq = true;
		rqs[j]->base_mrq_id = rqs[0]->hdr->id;
	}

	hw->hw_mrq_count = i;

	return 0;
}

int
efct_hw_map_wq_cpu(struct efct_hw *hw)
{
	struct efct *efct = hw->os;
	u32 cpu = 0, i;

	/* Init cpu_map array */
	hw->wq_cpu_array = kcalloc(num_possible_cpus(), sizeof(void *),
				   GFP_KERNEL);
	if (!hw->wq_cpu_array)
		return -ENOMEM;

	for (i = 0; i < hw->config.n_eq; i++) {
		const struct cpumask *maskp;

		/* Get a CPU mask for all CPUs affinitized to this vector */
		maskp = pci_irq_get_affinity(efct->pci, i);
		if (!maskp) {
			efc_log_debug(efct, "maskp null for vector:%d\n", i);
			continue;
		}

		/* Loop through all CPUs associated with vector idx */
		for_each_cpu_and(cpu, maskp, cpu_present_mask) {
			efc_log_debug(efct, "CPU:%d irq vector:%d\n", cpu, i);
			hw->wq_cpu_array[cpu] = hw->hw_wq[i];
		}
	}

	return 0;
}

struct hw_eq *
efct_hw_new_eq(struct efct_hw *hw, u32 entry_count)
{
	struct hw_eq *eq = kzalloc(sizeof(*eq), GFP_KERNEL);

	if (!eq)
		return NULL;

	eq->type = SLI4_QTYPE_EQ;
	eq->hw = hw;
	eq->entry_count = entry_count;
	eq->instance = hw->eq_count++;
	eq->queue = &hw->eq[eq->instance];
	INIT_LIST_HEAD(&eq->cq_list);

	if (sli_queue_alloc(&hw->sli, SLI4_QTYPE_EQ, eq->queue,	entry_count,
			    NULL)) {
		efc_log_err(hw->os, "EQ[%d] alloc failure\n", eq->instance);
		kfree(eq);
		return NULL;
	}

	sli_eq_modify_delay(&hw->sli, eq->queue, 1, 0, 8);
	hw->hw_eq[eq->instance] = eq;
	INIT_LIST_HEAD(&eq->list_entry);
	list_add_tail(&eq->list_entry, &hw->eq_list);
	efc_log_debug(hw->os, "create eq[%2d] id %3d len %4d\n", eq->instance,
		      eq->queue->id, eq->entry_count);
	return eq;
}

struct hw_cq *
efct_hw_new_cq(struct hw_eq *eq, u32 entry_count)
{
	struct efct_hw *hw = eq->hw;
	struct hw_cq *cq = kzalloc(sizeof(*cq), GFP_KERNEL);

	if (!cq)
		return NULL;

	cq->eq = eq;
	cq->type = SLI4_QTYPE_CQ;
	cq->instance = eq->hw->cq_count++;
	cq->entry_count = entry_count;
	cq->queue = &hw->cq[cq->instance];

	INIT_LIST_HEAD(&cq->q_list);

	if (sli_queue_alloc(&hw->sli, SLI4_QTYPE_CQ, cq->queue,
			    cq->entry_count, eq->queue)) {
		efc_log_err(hw->os, "CQ[%d] allocation failure len=%d\n",
			    eq->instance, eq->entry_count);
		kfree(cq);
		return NULL;
	}

	hw->hw_cq[cq->instance] = cq;
	INIT_LIST_HEAD(&cq->list_entry);
	list_add_tail(&cq->list_entry, &eq->cq_list);
	efc_log_debug(hw->os, "create cq[%2d] id %3d len %4d\n", cq->instance,
		      cq->queue->id, cq->entry_count);
	return cq;
}

u32
efct_hw_new_cq_set(struct hw_eq *eqs[], struct hw_cq *cqs[],
		   u32 num_cqs, u32 entry_count)
{
	u32 i;
	struct efct_hw *hw = eqs[0]->hw;
	struct sli4 *sli4 = &hw->sli;
	struct hw_cq *cq = NULL;
	struct sli4_queue *qs[SLI4_MAX_CQ_SET_COUNT];
	struct sli4_queue *assefct[SLI4_MAX_CQ_SET_COUNT];

	/* Initialise CQS pointers to NULL */
	for (i = 0; i < num_cqs; i++)
		cqs[i] = NULL;

	for (i = 0; i < num_cqs; i++) {
		cq = kzalloc(sizeof(*cq), GFP_KERNEL);
		if (!cq)
			goto error;

		cqs[i]          = cq;
		cq->eq          = eqs[i];
		cq->type        = SLI4_QTYPE_CQ;
		cq->instance    = hw->cq_count++;
		cq->entry_count = entry_count;
		cq->queue       = &hw->cq[cq->instance];
		qs[i]           = cq->queue;
		assefct[i]       = eqs[i]->queue;
		INIT_LIST_HEAD(&cq->q_list);
	}

	if (sli_cq_alloc_set(sli4, qs, num_cqs, entry_count, assefct)) {
		efc_log_err(hw->os, "Failed to create CQ Set.\n");
		goto error;
	}

	for (i = 0; i < num_cqs; i++) {
		hw->hw_cq[cqs[i]->instance] = cqs[i];
		INIT_LIST_HEAD(&cqs[i]->list_entry);
		list_add_tail(&cqs[i]->list_entry, &cqs[i]->eq->cq_list);
	}

	return 0;

error:
	for (i = 0; i < num_cqs; i++) {
		kfree(cqs[i]);
		cqs[i] = NULL;
	}
	return -EIO;
}

struct hw_mq *
efct_hw_new_mq(struct hw_cq *cq, u32 entry_count)
{
	struct efct_hw *hw = cq->eq->hw;
	struct hw_mq *mq = kzalloc(sizeof(*mq), GFP_KERNEL);

	if (!mq)
		return NULL;

	mq->cq = cq;
	mq->type = SLI4_QTYPE_MQ;
	mq->instance = cq->eq->hw->mq_count++;
	mq->entry_count = entry_count;
	mq->entry_size = EFCT_HW_MQ_DEPTH;
	mq->queue = &hw->mq[mq->instance];

	if (sli_queue_alloc(&hw->sli, SLI4_QTYPE_MQ, mq->queue, mq->entry_size,
			    cq->queue)) {
		efc_log_err(hw->os, "MQ allocation failure\n");
		kfree(mq);
		return NULL;
	}

	hw->hw_mq[mq->instance] = mq;
	INIT_LIST_HEAD(&mq->list_entry);
	list_add_tail(&mq->list_entry, &cq->q_list);
	efc_log_debug(hw->os, "create mq[%2d] id %3d len %4d\n", mq->instance,
		      mq->queue->id, mq->entry_count);
	return mq;
}

struct hw_wq *
efct_hw_new_wq(struct hw_cq *cq, u32 entry_count)
{
	struct efct_hw *hw = cq->eq->hw;
	struct hw_wq *wq = kzalloc(sizeof(*wq), GFP_KERNEL);

	if (!wq)
		return NULL;

	wq->hw = cq->eq->hw;
	wq->cq = cq;
	wq->type = SLI4_QTYPE_WQ;
	wq->instance = cq->eq->hw->wq_count++;
	wq->entry_count = entry_count;
	wq->queue = &hw->wq[wq->instance];
	wq->wqec_set_count = EFCT_HW_WQEC_SET_COUNT;
	wq->wqec_count = wq->wqec_set_count;
	wq->free_count = wq->entry_count - 1;
	INIT_LIST_HEAD(&wq->pending_list);

	if (sli_queue_alloc(&hw->sli, SLI4_QTYPE_WQ, wq->queue,
			    wq->entry_count, cq->queue)) {
		efc_log_err(hw->os, "WQ allocation failure\n");
		kfree(wq);
		return NULL;
	}

	hw->hw_wq[wq->instance] = wq;
	INIT_LIST_HEAD(&wq->list_entry);
	list_add_tail(&wq->list_entry, &cq->q_list);
	efc_log_debug(hw->os, "create wq[%2d] id %3d len %4d cls %d\n",
		      wq->instance, wq->queue->id, wq->entry_count, wq->class);
	return wq;
}

u32
efct_hw_new_rq_set(struct hw_cq *cqs[], struct hw_rq *rqs[],
		   u32 num_rq_pairs, u32 entry_count)
{
	struct efct_hw *hw = cqs[0]->eq->hw;
	struct hw_rq *rq = NULL;
	struct sli4_queue *qs[SLI4_MAX_RQ_SET_COUNT * 2] = { NULL };
	u32 i, q_count, size;

	/* Initialise RQS pointers */
	for (i = 0; i < num_rq_pairs; i++)
		rqs[i] = NULL;

	/*
	 * Allocate an RQ object SET, where each element in set
	 * encapsulates 2 SLI queues (for rq pair)
	 */
	for (i = 0, q_count = 0; i < num_rq_pairs; i++, q_count += 2) {
		rq = kzalloc(sizeof(*rq), GFP_KERNEL);
		if (!rq)
			goto error;

		rqs[i] = rq;
		rq->instance = hw->hw_rq_count++;
		rq->cq = cqs[i];
		rq->type = SLI4_QTYPE_RQ;
		rq->entry_count = entry_count;

		/* Header RQ */
		rq->hdr = &hw->rq[hw->rq_count];
		rq->hdr_entry_size = EFCT_HW_RQ_HEADER_SIZE;
		hw->hw_rq_lookup[hw->rq_count] = rq->instance;
		hw->rq_count++;
		qs[q_count] = rq->hdr;

		/* Data RQ */
		rq->data = &hw->rq[hw->rq_count];
		rq->data_entry_size = hw->config.rq_default_buffer_size;
		hw->hw_rq_lookup[hw->rq_count] = rq->instance;
		hw->rq_count++;
		qs[q_count + 1] = rq->data;

		rq->rq_tracker = NULL;
	}

	if (sli_fc_rq_set_alloc(&hw->sli, num_rq_pairs, qs,
				cqs[0]->queue->id,
			    rqs[0]->entry_count,
			    rqs[0]->hdr_entry_size,
			    rqs[0]->data_entry_size)) {
		efc_log_err(hw->os, "RQ Set alloc failure for base CQ=%d\n",
			    cqs[0]->queue->id);
		goto error;
	}

	for (i = 0; i < num_rq_pairs; i++) {
		hw->hw_rq[rqs[i]->instance] = rqs[i];
		INIT_LIST_HEAD(&rqs[i]->list_entry);
		list_add_tail(&rqs[i]->list_entry, &cqs[i]->q_list);
		size = sizeof(struct efc_hw_sequence *) * rqs[i]->entry_count;
		rqs[i]->rq_tracker = kzalloc(size, GFP_KERNEL);
		if (!rqs[i]->rq_tracker)
			goto error;
	}

	return 0;

error:
	for (i = 0; i < num_rq_pairs; i++) {
		if (rqs[i]) {
			kfree(rqs[i]->rq_tracker);
			kfree(rqs[i]);
		}
	}

	return -EIO;
}

void
efct_hw_del_eq(struct hw_eq *eq)
{
	struct hw_cq *cq;
	struct hw_cq *cq_next;

	if (!eq)
		return;

	list_for_each_entry_safe(cq, cq_next, &eq->cq_list, list_entry)
		efct_hw_del_cq(cq);
	list_del(&eq->list_entry);
	eq->hw->hw_eq[eq->instance] = NULL;
	kfree(eq);
}

void
efct_hw_del_cq(struct hw_cq *cq)
{
	struct hw_q *q;
	struct hw_q *q_next;

	if (!cq)
		return;

	list_for_each_entry_safe(q, q_next, &cq->q_list, list_entry) {
		switch (q->type) {
		case SLI4_QTYPE_MQ:
			efct_hw_del_mq((struct hw_mq *)q);
			break;
		case SLI4_QTYPE_WQ:
			efct_hw_del_wq((struct hw_wq *)q);
			break;
		case SLI4_QTYPE_RQ:
			efct_hw_del_rq((struct hw_rq *)q);
			break;
		default:
			break;
		}
	}
	list_del(&cq->list_entry);
	cq->eq->hw->hw_cq[cq->instance] = NULL;
	kfree(cq);
}

void
efct_hw_del_mq(struct hw_mq *mq)
{
	if (!mq)
		return;

	list_del(&mq->list_entry);
	mq->cq->eq->hw->hw_mq[mq->instance] = NULL;
	kfree(mq);
}

void
efct_hw_del_wq(struct hw_wq *wq)
{
	if (!wq)
		return;

	list_del(&wq->list_entry);
	wq->cq->eq->hw->hw_wq[wq->instance] = NULL;
	kfree(wq);
}

void
efct_hw_del_rq(struct hw_rq *rq)
{
	struct efct_hw *hw = NULL;

	if (!rq)
		return;
	/* Free RQ tracker */
	kfree(rq->rq_tracker);
	rq->rq_tracker = NULL;
	list_del(&rq->list_entry);
	hw = rq->cq->eq->hw;
	hw->hw_rq[rq->instance] = NULL;
	kfree(rq);
}

void
efct_hw_queue_teardown(struct efct_hw *hw)
{
	struct hw_eq *eq;
	struct hw_eq *eq_next;

	if (!hw->eq_list.next)
		return;

	list_for_each_entry_safe(eq, eq_next, &hw->eq_list, list_entry)
		efct_hw_del_eq(eq);
}

static inline int
efct_hw_rqpair_find(struct efct_hw *hw, u16 rq_id)
{
	return efct_hw_queue_hash_find(hw->rq_hash, rq_id);
}

static struct efc_hw_sequence *
efct_hw_rqpair_get(struct efct_hw *hw, u16 rqindex, u16 bufindex)
{
	struct sli4_queue *rq_hdr = &hw->rq[rqindex];
	struct efc_hw_sequence *seq = NULL;
	struct hw_rq *rq = hw->hw_rq[hw->hw_rq_lookup[rqindex]];
	unsigned long flags = 0;

	if (bufindex >= rq_hdr->length) {
		efc_log_err(hw->os,
			    "RQidx %d bufidx %d exceed ring len %d for id %d\n",
			    rqindex, bufindex, rq_hdr->length, rq_hdr->id);
		return NULL;
	}

	/* rq_hdr lock also covers rqindex+1 queue */
	spin_lock_irqsave(&rq_hdr->lock, flags);

	seq = rq->rq_tracker[bufindex];
	rq->rq_tracker[bufindex] = NULL;

	if (!seq) {
		efc_log_err(hw->os,
			    "RQbuf NULL, rqidx %d, bufidx %d, cur q idx = %d\n",
			    rqindex, bufindex, rq_hdr->index);
	}

	spin_unlock_irqrestore(&rq_hdr->lock, flags);
	return seq;
}

int
efct_hw_rqpair_process_rq(struct efct_hw *hw, struct hw_cq *cq,
			  u8 *cqe)
{
	u16 rq_id;
	u32 index;
	int rqindex;
	int rq_status;
	u32 h_len;
	u32 p_len;
	struct efc_hw_sequence *seq;
	struct hw_rq *rq;

	rq_status = sli_fc_rqe_rqid_and_index(&hw->sli, cqe,
					      &rq_id, &index);
	if (rq_status != 0) {
		switch (rq_status) {
		case SLI4_FC_ASYNC_RQ_BUF_LEN_EXCEEDED:
		case SLI4_FC_ASYNC_RQ_DMA_FAILURE:
			/* just get RQ buffer then return to chip */
			rqindex = efct_hw_rqpair_find(hw, rq_id);
			if (rqindex < 0) {
				efc_log_debug(hw->os,
					      "status=%#x: lookup fail id=%#x\n",
					     rq_status, rq_id);
				break;
			}

			/* get RQ buffer */
			seq = efct_hw_rqpair_get(hw, rqindex, index);

			/* return to chip */
			if (efct_hw_rqpair_sequence_free(hw, seq)) {
				efc_log_debug(hw->os,
					      "status=%#x,fail rtrn buf to RQ\n",
					     rq_status);
				break;
			}
			break;
		case SLI4_FC_ASYNC_RQ_INSUFF_BUF_NEEDED:
		case SLI4_FC_ASYNC_RQ_INSUFF_BUF_FRM_DISC:
			/*
			 * since RQ buffers were not consumed, cannot return
			 * them to chip
			 */
			efc_log_debug(hw->os, "Warning: RCQE status=%#x,\n",
				      rq_status);
			fallthrough;
		default:
			break;
		}
		return -EIO;
	}

	rqindex = efct_hw_rqpair_find(hw, rq_id);
	if (rqindex < 0) {
		efc_log_debug(hw->os, "Error: rq_id lookup failed for id=%#x\n",
			      rq_id);
		return -EIO;
	}

	rq = hw->hw_rq[hw->hw_rq_lookup[rqindex]];
	rq->use_count++;

	seq = efct_hw_rqpair_get(hw, rqindex, index);
	if (WARN_ON(!seq))
		return -EIO;

	seq->hw = hw;

	sli_fc_rqe_length(&hw->sli, cqe, &h_len, &p_len);
	seq->header->dma.len = h_len;
	seq->payload->dma.len = p_len;
	seq->fcfi = sli_fc_rqe_fcfi(&hw->sli, cqe);
	seq->hw_priv = cq->eq;

	efct_unsolicited_cb(hw->os, seq);

	return 0;
}

static int
efct_hw_rqpair_put(struct efct_hw *hw, struct efc_hw_sequence *seq)
{
	struct sli4_queue *rq_hdr = &hw->rq[seq->header->rqindex];
	struct sli4_queue *rq_payload = &hw->rq[seq->payload->rqindex];
	u32 hw_rq_index = hw->hw_rq_lookup[seq->header->rqindex];
	struct hw_rq *rq = hw->hw_rq[hw_rq_index];
	u32 phys_hdr[2];
	u32 phys_payload[2];
	int qindex_hdr;
	int qindex_payload;
	unsigned long flags = 0;

	/* Update the RQ verification lookup tables */
	phys_hdr[0] = upper_32_bits(seq->header->dma.phys);
	phys_hdr[1] = lower_32_bits(seq->header->dma.phys);
	phys_payload[0] = upper_32_bits(seq->payload->dma.phys);
	phys_payload[1] = lower_32_bits(seq->payload->dma.phys);

	/* rq_hdr lock also covers payload / header->rqindex+1 queue */
	spin_lock_irqsave(&rq_hdr->lock, flags);

	/*
	 * Note: The header must be posted last for buffer pair mode because
	 *       posting on the header queue posts the payload queue as well.
	 *       We do not ring the payload queue independently in RQ pair mode.
	 */
	qindex_payload = sli_rq_write(&hw->sli, rq_payload,
				      (void *)phys_payload);
	qindex_hdr = sli_rq_write(&hw->sli, rq_hdr, (void *)phys_hdr);
	if (qindex_hdr < 0 ||
	    qindex_payload < 0) {
		efc_log_err(hw->os, "RQ_ID=%#x write failed\n", rq_hdr->id);
		spin_unlock_irqrestore(&rq_hdr->lock, flags);
		return -EIO;
	}

	/* ensure the indexes are the same */
	WARN_ON(qindex_hdr != qindex_payload);

	/* Update the lookup table */
	if (!rq->rq_tracker[qindex_hdr]) {
		rq->rq_tracker[qindex_hdr] = seq;
	} else {
		efc_log_debug(hw->os,
			      "expected rq_tracker[%d][%d] buffer to be NULL\n",
			      hw_rq_index, qindex_hdr);
	}

	spin_unlock_irqrestore(&rq_hdr->lock, flags);
	return 0;
}

int
efct_hw_rqpair_sequence_free(struct efct_hw *hw, struct efc_hw_sequence *seq)
{
	int rc = 0;

	/*
	 * Post the data buffer first. Because in RQ pair mode, ringing the
	 * doorbell of the header ring will post the data buffer as well.
	 */
	if (efct_hw_rqpair_put(hw, seq)) {
		efc_log_err(hw->os, "error writing buffers\n");
		return -EIO;
	}

	return rc;
}

int
efct_efc_hw_sequence_free(struct efc *efc, struct efc_hw_sequence *seq)
{
	struct efct *efct = efc->base;

	return efct_hw_rqpair_sequence_free(&efct->hw, seq);
}
