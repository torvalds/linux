// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023 Intel Corporation */

#include "idpf.h"

/**
 * idpf_tx_buf_rel - Release a Tx buffer
 * @tx_q: the queue that owns the buffer
 * @tx_buf: the buffer to free
 */
static void idpf_tx_buf_rel(struct idpf_queue *tx_q, struct idpf_tx_buf *tx_buf)
{
	if (tx_buf->skb) {
		if (dma_unmap_len(tx_buf, len))
			dma_unmap_single(tx_q->dev,
					 dma_unmap_addr(tx_buf, dma),
					 dma_unmap_len(tx_buf, len),
					 DMA_TO_DEVICE);
		dev_kfree_skb_any(tx_buf->skb);
	} else if (dma_unmap_len(tx_buf, len)) {
		dma_unmap_page(tx_q->dev,
			       dma_unmap_addr(tx_buf, dma),
			       dma_unmap_len(tx_buf, len),
			       DMA_TO_DEVICE);
	}

	tx_buf->next_to_watch = NULL;
	tx_buf->skb = NULL;
	tx_buf->compl_tag = IDPF_SPLITQ_TX_INVAL_COMPL_TAG;
	dma_unmap_len_set(tx_buf, len, 0);
}

/**
 * idpf_tx_buf_rel_all - Free any empty Tx buffers
 * @txq: queue to be cleaned
 */
static void idpf_tx_buf_rel_all(struct idpf_queue *txq)
{
	u16 i;

	/* Buffers already cleared, nothing to do */
	if (!txq->tx_buf)
		return;

	/* Free all the Tx buffer sk_buffs */
	for (i = 0; i < txq->desc_count; i++)
		idpf_tx_buf_rel(txq, &txq->tx_buf[i]);

	kfree(txq->tx_buf);
	txq->tx_buf = NULL;

	if (!txq->buf_stack.bufs)
		return;

	for (i = 0; i < txq->buf_stack.size; i++)
		kfree(txq->buf_stack.bufs[i]);

	kfree(txq->buf_stack.bufs);
	txq->buf_stack.bufs = NULL;
}

/**
 * idpf_tx_desc_rel - Free Tx resources per queue
 * @txq: Tx descriptor ring for a specific queue
 * @bufq: buffer q or completion q
 *
 * Free all transmit software resources
 */
static void idpf_tx_desc_rel(struct idpf_queue *txq, bool bufq)
{
	if (bufq)
		idpf_tx_buf_rel_all(txq);

	if (!txq->desc_ring)
		return;

	dmam_free_coherent(txq->dev, txq->size, txq->desc_ring, txq->dma);
	txq->desc_ring = NULL;
	txq->next_to_alloc = 0;
	txq->next_to_use = 0;
	txq->next_to_clean = 0;
}

/**
 * idpf_tx_desc_rel_all - Free Tx Resources for All Queues
 * @vport: virtual port structure
 *
 * Free all transmit software resources
 */
static void idpf_tx_desc_rel_all(struct idpf_vport *vport)
{
	int i, j;

	if (!vport->txq_grps)
		return;

	for (i = 0; i < vport->num_txq_grp; i++) {
		struct idpf_txq_group *txq_grp = &vport->txq_grps[i];

		for (j = 0; j < txq_grp->num_txq; j++)
			idpf_tx_desc_rel(txq_grp->txqs[j], true);

		if (idpf_is_queue_model_split(vport->txq_model))
			idpf_tx_desc_rel(txq_grp->complq, false);
	}
}

/**
 * idpf_tx_buf_alloc_all - Allocate memory for all buffer resources
 * @tx_q: queue for which the buffers are allocated
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_tx_buf_alloc_all(struct idpf_queue *tx_q)
{
	int buf_size;
	int i;

	/* Allocate book keeping buffers only. Buffers to be supplied to HW
	 * are allocated by kernel network stack and received as part of skb
	 */
	buf_size = sizeof(struct idpf_tx_buf) * tx_q->desc_count;
	tx_q->tx_buf = kzalloc(buf_size, GFP_KERNEL);
	if (!tx_q->tx_buf)
		return -ENOMEM;

	/* Initialize tx_bufs with invalid completion tags */
	for (i = 0; i < tx_q->desc_count; i++)
		tx_q->tx_buf[i].compl_tag = IDPF_SPLITQ_TX_INVAL_COMPL_TAG;

	/* Initialize tx buf stack for out-of-order completions if
	 * flow scheduling offload is enabled
	 */
	tx_q->buf_stack.bufs =
		kcalloc(tx_q->desc_count, sizeof(struct idpf_tx_stash *),
			GFP_KERNEL);
	if (!tx_q->buf_stack.bufs)
		return -ENOMEM;

	tx_q->buf_stack.size = tx_q->desc_count;
	tx_q->buf_stack.top = tx_q->desc_count;

	for (i = 0; i < tx_q->desc_count; i++) {
		tx_q->buf_stack.bufs[i] = kzalloc(sizeof(*tx_q->buf_stack.bufs[i]),
						  GFP_KERNEL);
		if (!tx_q->buf_stack.bufs[i])
			return -ENOMEM;
	}

	return 0;
}

/**
 * idpf_tx_desc_alloc - Allocate the Tx descriptors
 * @tx_q: the tx ring to set up
 * @bufq: buffer or completion queue
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_tx_desc_alloc(struct idpf_queue *tx_q, bool bufq)
{
	struct device *dev = tx_q->dev;
	u32 desc_sz;
	int err;

	if (bufq) {
		err = idpf_tx_buf_alloc_all(tx_q);
		if (err)
			goto err_alloc;

		desc_sz = sizeof(struct idpf_base_tx_desc);
	} else {
		desc_sz = sizeof(struct idpf_splitq_tx_compl_desc);
	}

	tx_q->size = tx_q->desc_count * desc_sz;

	/* Allocate descriptors also round up to nearest 4K */
	tx_q->size = ALIGN(tx_q->size, 4096);
	tx_q->desc_ring = dmam_alloc_coherent(dev, tx_q->size, &tx_q->dma,
					      GFP_KERNEL);
	if (!tx_q->desc_ring) {
		dev_err(dev, "Unable to allocate memory for the Tx descriptor ring, size=%d\n",
			tx_q->size);
		err = -ENOMEM;
		goto err_alloc;
	}

	tx_q->next_to_alloc = 0;
	tx_q->next_to_use = 0;
	tx_q->next_to_clean = 0;
	set_bit(__IDPF_Q_GEN_CHK, tx_q->flags);

	return 0;

err_alloc:
	idpf_tx_desc_rel(tx_q, bufq);

	return err;
}

/**
 * idpf_tx_desc_alloc_all - allocate all queues Tx resources
 * @vport: virtual port private structure
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_tx_desc_alloc_all(struct idpf_vport *vport)
{
	struct device *dev = &vport->adapter->pdev->dev;
	int err = 0;
	int i, j;

	/* Setup buffer queues. In single queue model buffer queues and
	 * completion queues will be same
	 */
	for (i = 0; i < vport->num_txq_grp; i++) {
		for (j = 0; j < vport->txq_grps[i].num_txq; j++) {
			struct idpf_queue *txq = vport->txq_grps[i].txqs[j];
			u8 gen_bits = 0;
			u16 bufidx_mask;

			err = idpf_tx_desc_alloc(txq, true);
			if (err) {
				dev_err(dev, "Allocation for Tx Queue %u failed\n",
					i);
				goto err_out;
			}

			if (!idpf_is_queue_model_split(vport->txq_model))
				continue;

			txq->compl_tag_cur_gen = 0;

			/* Determine the number of bits in the bufid
			 * mask and add one to get the start of the
			 * generation bits
			 */
			bufidx_mask = txq->desc_count - 1;
			while (bufidx_mask >> 1) {
				txq->compl_tag_gen_s++;
				bufidx_mask = bufidx_mask >> 1;
			}
			txq->compl_tag_gen_s++;

			gen_bits = IDPF_TX_SPLITQ_COMPL_TAG_WIDTH -
							txq->compl_tag_gen_s;
			txq->compl_tag_gen_max = GETMAXVAL(gen_bits);

			/* Set bufid mask based on location of first
			 * gen bit; it cannot simply be the descriptor
			 * ring size-1 since we can have size values
			 * where not all of those bits are set.
			 */
			txq->compl_tag_bufid_m =
				GETMAXVAL(txq->compl_tag_gen_s);
		}

		if (!idpf_is_queue_model_split(vport->txq_model))
			continue;

		/* Setup completion queues */
		err = idpf_tx_desc_alloc(vport->txq_grps[i].complq, false);
		if (err) {
			dev_err(dev, "Allocation for Tx Completion Queue %u failed\n",
				i);
			goto err_out;
		}
	}

err_out:
	if (err)
		idpf_tx_desc_rel_all(vport);

	return err;
}

/**
 * idpf_rx_page_rel - Release an rx buffer page
 * @rxq: the queue that owns the buffer
 * @rx_buf: the buffer to free
 */
static void idpf_rx_page_rel(struct idpf_queue *rxq, struct idpf_rx_buf *rx_buf)
{
	if (unlikely(!rx_buf->page))
		return;

	page_pool_put_full_page(rxq->pp, rx_buf->page, false);

	rx_buf->page = NULL;
	rx_buf->page_offset = 0;
}

/**
 * idpf_rx_hdr_buf_rel_all - Release header buffer memory
 * @rxq: queue to use
 */
static void idpf_rx_hdr_buf_rel_all(struct idpf_queue *rxq)
{
	struct idpf_adapter *adapter = rxq->vport->adapter;

	dma_free_coherent(&adapter->pdev->dev,
			  rxq->desc_count * IDPF_HDR_BUF_SIZE,
			  rxq->rx_buf.hdr_buf_va,
			  rxq->rx_buf.hdr_buf_pa);
	rxq->rx_buf.hdr_buf_va = NULL;
}

/**
 * idpf_rx_buf_rel_all - Free all Rx buffer resources for a queue
 * @rxq: queue to be cleaned
 */
static void idpf_rx_buf_rel_all(struct idpf_queue *rxq)
{
	u16 i;

	/* queue already cleared, nothing to do */
	if (!rxq->rx_buf.buf)
		return;

	/* Free all the bufs allocated and given to hw on Rx queue */
	for (i = 0; i < rxq->desc_count; i++)
		idpf_rx_page_rel(rxq, &rxq->rx_buf.buf[i]);

	if (rxq->rx_hsplit_en)
		idpf_rx_hdr_buf_rel_all(rxq);

	page_pool_destroy(rxq->pp);
	rxq->pp = NULL;

	kfree(rxq->rx_buf.buf);
	rxq->rx_buf.buf = NULL;
}

/**
 * idpf_rx_desc_rel - Free a specific Rx q resources
 * @rxq: queue to clean the resources from
 * @bufq: buffer q or completion q
 * @q_model: single or split q model
 *
 * Free a specific rx queue resources
 */
static void idpf_rx_desc_rel(struct idpf_queue *rxq, bool bufq, s32 q_model)
{
	if (!rxq)
		return;

	if (!bufq && idpf_is_queue_model_split(q_model) && rxq->skb) {
		dev_kfree_skb_any(rxq->skb);
		rxq->skb = NULL;
	}

	if (bufq || !idpf_is_queue_model_split(q_model))
		idpf_rx_buf_rel_all(rxq);

	rxq->next_to_alloc = 0;
	rxq->next_to_clean = 0;
	rxq->next_to_use = 0;
	if (!rxq->desc_ring)
		return;

	dmam_free_coherent(rxq->dev, rxq->size, rxq->desc_ring, rxq->dma);
	rxq->desc_ring = NULL;
}

/**
 * idpf_rx_desc_rel_all - Free Rx Resources for All Queues
 * @vport: virtual port structure
 *
 * Free all rx queues resources
 */
static void idpf_rx_desc_rel_all(struct idpf_vport *vport)
{
	struct idpf_rxq_group *rx_qgrp;
	u16 num_rxq;
	int i, j;

	if (!vport->rxq_grps)
		return;

	for (i = 0; i < vport->num_rxq_grp; i++) {
		rx_qgrp = &vport->rxq_grps[i];

		if (!idpf_is_queue_model_split(vport->rxq_model)) {
			for (j = 0; j < rx_qgrp->singleq.num_rxq; j++)
				idpf_rx_desc_rel(rx_qgrp->singleq.rxqs[j],
						 false, vport->rxq_model);
			continue;
		}

		num_rxq = rx_qgrp->splitq.num_rxq_sets;
		for (j = 0; j < num_rxq; j++)
			idpf_rx_desc_rel(&rx_qgrp->splitq.rxq_sets[j]->rxq,
					 false, vport->rxq_model);

		if (!rx_qgrp->splitq.bufq_sets)
			continue;

		for (j = 0; j < vport->num_bufqs_per_qgrp; j++) {
			struct idpf_bufq_set *bufq_set =
				&rx_qgrp->splitq.bufq_sets[j];

			idpf_rx_desc_rel(&bufq_set->bufq, true,
					 vport->rxq_model);
		}
	}
}

/**
 * idpf_rx_buf_hw_update - Store the new tail and head values
 * @rxq: queue to bump
 * @val: new head index
 */
void idpf_rx_buf_hw_update(struct idpf_queue *rxq, u32 val)
{
	rxq->next_to_use = val;

	if (unlikely(!rxq->tail))
		return;

	/* writel has an implicit memory barrier */
	writel(val, rxq->tail);
}

/**
 * idpf_rx_hdr_buf_alloc_all - Allocate memory for header buffers
 * @rxq: ring to use
 *
 * Returns 0 on success, negative on failure.
 */
static int idpf_rx_hdr_buf_alloc_all(struct idpf_queue *rxq)
{
	struct idpf_adapter *adapter = rxq->vport->adapter;

	rxq->rx_buf.hdr_buf_va =
		dma_alloc_coherent(&adapter->pdev->dev,
				   IDPF_HDR_BUF_SIZE * rxq->desc_count,
				   &rxq->rx_buf.hdr_buf_pa,
				   GFP_KERNEL);
	if (!rxq->rx_buf.hdr_buf_va)
		return -ENOMEM;

	return 0;
}

/**
 * idpf_rx_post_buf_desc - Post buffer to bufq descriptor ring
 * @bufq: buffer queue to post to
 * @buf_id: buffer id to post
 *
 * Returns false if buffer could not be allocated, true otherwise.
 */
static bool idpf_rx_post_buf_desc(struct idpf_queue *bufq, u16 buf_id)
{
	struct virtchnl2_splitq_rx_buf_desc *splitq_rx_desc = NULL;
	u16 nta = bufq->next_to_alloc;
	struct idpf_rx_buf *buf;
	dma_addr_t addr;

	splitq_rx_desc = IDPF_SPLITQ_RX_BUF_DESC(bufq, nta);
	buf = &bufq->rx_buf.buf[buf_id];

	if (bufq->rx_hsplit_en) {
		splitq_rx_desc->hdr_addr =
			cpu_to_le64(bufq->rx_buf.hdr_buf_pa +
				    (u32)buf_id * IDPF_HDR_BUF_SIZE);
	}

	addr = idpf_alloc_page(bufq->pp, buf, bufq->rx_buf_size);
	if (unlikely(addr == DMA_MAPPING_ERROR))
		return false;

	splitq_rx_desc->pkt_addr = cpu_to_le64(addr);
	splitq_rx_desc->qword0.buf_id = cpu_to_le16(buf_id);

	nta++;
	if (unlikely(nta == bufq->desc_count))
		nta = 0;
	bufq->next_to_alloc = nta;

	return true;
}

/**
 * idpf_rx_post_init_bufs - Post initial buffers to bufq
 * @bufq: buffer queue to post working set to
 * @working_set: number of buffers to put in working set
 *
 * Returns true if @working_set bufs were posted successfully, false otherwise.
 */
static bool idpf_rx_post_init_bufs(struct idpf_queue *bufq, u16 working_set)
{
	int i;

	for (i = 0; i < working_set; i++) {
		if (!idpf_rx_post_buf_desc(bufq, i))
			return false;
	}

	idpf_rx_buf_hw_update(bufq,
			      bufq->next_to_alloc & ~(bufq->rx_buf_stride - 1));

	return true;
}

/**
 * idpf_rx_create_page_pool - Create a page pool
 * @rxbufq: RX queue to create page pool for
 *
 * Returns &page_pool on success, casted -errno on failure
 */
static struct page_pool *idpf_rx_create_page_pool(struct idpf_queue *rxbufq)
{
	struct page_pool_params pp = {
		.flags		= PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV,
		.order		= 0,
		.pool_size	= rxbufq->desc_count,
		.nid		= NUMA_NO_NODE,
		.dev		= rxbufq->vport->netdev->dev.parent,
		.max_len	= PAGE_SIZE,
		.dma_dir	= DMA_FROM_DEVICE,
		.offset		= 0,
	};

	if (rxbufq->rx_buf_size == IDPF_RX_BUF_2048)
		pp.flags |= PP_FLAG_PAGE_FRAG;

	return page_pool_create(&pp);
}

/**
 * idpf_rx_buf_alloc_all - Allocate memory for all buffer resources
 * @rxbufq: queue for which the buffers are allocated; equivalent to
 * rxq when operating in singleq mode
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_rx_buf_alloc_all(struct idpf_queue *rxbufq)
{
	int err = 0;

	/* Allocate book keeping buffers */
	rxbufq->rx_buf.buf = kcalloc(rxbufq->desc_count,
				     sizeof(struct idpf_rx_buf), GFP_KERNEL);
	if (!rxbufq->rx_buf.buf) {
		err = -ENOMEM;
		goto rx_buf_alloc_all_out;
	}

	if (rxbufq->rx_hsplit_en) {
		err = idpf_rx_hdr_buf_alloc_all(rxbufq);
		if (err)
			goto rx_buf_alloc_all_out;
	}

	/* Allocate buffers to be given to HW.	 */
	if (idpf_is_queue_model_split(rxbufq->vport->rxq_model)) {
		int working_set = IDPF_RX_BUFQ_WORKING_SET(rxbufq);

		if (!idpf_rx_post_init_bufs(rxbufq, working_set))
			err = -ENOMEM;
	} else {
		if (idpf_rx_singleq_buf_hw_alloc_all(rxbufq,
						     rxbufq->desc_count - 1))
			err = -ENOMEM;
	}

rx_buf_alloc_all_out:
	if (err)
		idpf_rx_buf_rel_all(rxbufq);

	return err;
}

/**
 * idpf_rx_bufs_init - Initialize page pool, allocate rx bufs, and post to HW
 * @rxbufq: RX queue to create page pool for
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_rx_bufs_init(struct idpf_queue *rxbufq)
{
	struct page_pool *pool;

	pool = idpf_rx_create_page_pool(rxbufq);
	if (IS_ERR(pool))
		return PTR_ERR(pool);

	rxbufq->pp = pool;

	return idpf_rx_buf_alloc_all(rxbufq);
}

/**
 * idpf_rx_bufs_init_all - Initialize all RX bufs
 * @vport: virtual port struct
 *
 * Returns 0 on success, negative on failure
 */
int idpf_rx_bufs_init_all(struct idpf_vport *vport)
{
	struct idpf_rxq_group *rx_qgrp;
	struct idpf_queue *q;
	int i, j, err;

	for (i = 0; i < vport->num_rxq_grp; i++) {
		rx_qgrp = &vport->rxq_grps[i];

		/* Allocate bufs for the rxq itself in singleq */
		if (!idpf_is_queue_model_split(vport->rxq_model)) {
			int num_rxq = rx_qgrp->singleq.num_rxq;

			for (j = 0; j < num_rxq; j++) {
				q = rx_qgrp->singleq.rxqs[j];
				err = idpf_rx_bufs_init(q);
				if (err)
					return err;
			}

			continue;
		}

		/* Otherwise, allocate bufs for the buffer queues */
		for (j = 0; j < vport->num_bufqs_per_qgrp; j++) {
			q = &rx_qgrp->splitq.bufq_sets[j].bufq;
			err = idpf_rx_bufs_init(q);
			if (err)
				return err;
		}
	}

	return 0;
}

/**
 * idpf_rx_desc_alloc - Allocate queue Rx resources
 * @rxq: Rx queue for which the resources are setup
 * @bufq: buffer or completion queue
 * @q_model: single or split queue model
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_rx_desc_alloc(struct idpf_queue *rxq, bool bufq, s32 q_model)
{
	struct device *dev = rxq->dev;

	if (bufq)
		rxq->size = rxq->desc_count *
			sizeof(struct virtchnl2_splitq_rx_buf_desc);
	else
		rxq->size = rxq->desc_count *
			sizeof(union virtchnl2_rx_desc);

	/* Allocate descriptors and also round up to nearest 4K */
	rxq->size = ALIGN(rxq->size, 4096);
	rxq->desc_ring = dmam_alloc_coherent(dev, rxq->size,
					     &rxq->dma, GFP_KERNEL);
	if (!rxq->desc_ring) {
		dev_err(dev, "Unable to allocate memory for the Rx descriptor ring, size=%d\n",
			rxq->size);
		return -ENOMEM;
	}

	rxq->next_to_alloc = 0;
	rxq->next_to_clean = 0;
	rxq->next_to_use = 0;
	set_bit(__IDPF_Q_GEN_CHK, rxq->flags);

	return 0;
}

/**
 * idpf_rx_desc_alloc_all - allocate all RX queues resources
 * @vport: virtual port structure
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_rx_desc_alloc_all(struct idpf_vport *vport)
{
	struct device *dev = &vport->adapter->pdev->dev;
	struct idpf_rxq_group *rx_qgrp;
	struct idpf_queue *q;
	int i, j, err;
	u16 num_rxq;

	for (i = 0; i < vport->num_rxq_grp; i++) {
		rx_qgrp = &vport->rxq_grps[i];
		if (idpf_is_queue_model_split(vport->rxq_model))
			num_rxq = rx_qgrp->splitq.num_rxq_sets;
		else
			num_rxq = rx_qgrp->singleq.num_rxq;

		for (j = 0; j < num_rxq; j++) {
			if (idpf_is_queue_model_split(vport->rxq_model))
				q = &rx_qgrp->splitq.rxq_sets[j]->rxq;
			else
				q = rx_qgrp->singleq.rxqs[j];
			err = idpf_rx_desc_alloc(q, false, vport->rxq_model);
			if (err) {
				dev_err(dev, "Memory allocation for Rx Queue %u failed\n",
					i);
				goto err_out;
			}
		}

		if (!idpf_is_queue_model_split(vport->rxq_model))
			continue;

		for (j = 0; j < vport->num_bufqs_per_qgrp; j++) {
			q = &rx_qgrp->splitq.bufq_sets[j].bufq;
			err = idpf_rx_desc_alloc(q, true, vport->rxq_model);
			if (err) {
				dev_err(dev, "Memory allocation for Rx Buffer Queue %u failed\n",
					i);
				goto err_out;
			}
		}
	}

	return 0;

err_out:
	idpf_rx_desc_rel_all(vport);

	return err;
}

/**
 * idpf_txq_group_rel - Release all resources for txq groups
 * @vport: vport to release txq groups on
 */
static void idpf_txq_group_rel(struct idpf_vport *vport)
{
	int i, j;

	if (!vport->txq_grps)
		return;

	for (i = 0; i < vport->num_txq_grp; i++) {
		struct idpf_txq_group *txq_grp = &vport->txq_grps[i];

		for (j = 0; j < txq_grp->num_txq; j++) {
			kfree(txq_grp->txqs[j]);
			txq_grp->txqs[j] = NULL;
		}
		kfree(txq_grp->complq);
		txq_grp->complq = NULL;
	}
	kfree(vport->txq_grps);
	vport->txq_grps = NULL;
}

/**
 * idpf_rxq_sw_queue_rel - Release software queue resources
 * @rx_qgrp: rx queue group with software queues
 */
static void idpf_rxq_sw_queue_rel(struct idpf_rxq_group *rx_qgrp)
{
	int i, j;

	for (i = 0; i < rx_qgrp->vport->num_bufqs_per_qgrp; i++) {
		struct idpf_bufq_set *bufq_set = &rx_qgrp->splitq.bufq_sets[i];

		for (j = 0; j < bufq_set->num_refillqs; j++) {
			kfree(bufq_set->refillqs[j].ring);
			bufq_set->refillqs[j].ring = NULL;
		}
		kfree(bufq_set->refillqs);
		bufq_set->refillqs = NULL;
	}
}

/**
 * idpf_rxq_group_rel - Release all resources for rxq groups
 * @vport: vport to release rxq groups on
 */
static void idpf_rxq_group_rel(struct idpf_vport *vport)
{
	int i;

	if (!vport->rxq_grps)
		return;

	for (i = 0; i < vport->num_rxq_grp; i++) {
		struct idpf_rxq_group *rx_qgrp = &vport->rxq_grps[i];
		u16 num_rxq;
		int j;

		if (idpf_is_queue_model_split(vport->rxq_model)) {
			num_rxq = rx_qgrp->splitq.num_rxq_sets;
			for (j = 0; j < num_rxq; j++) {
				kfree(rx_qgrp->splitq.rxq_sets[j]);
				rx_qgrp->splitq.rxq_sets[j] = NULL;
			}

			idpf_rxq_sw_queue_rel(rx_qgrp);
			kfree(rx_qgrp->splitq.bufq_sets);
			rx_qgrp->splitq.bufq_sets = NULL;
		} else {
			num_rxq = rx_qgrp->singleq.num_rxq;
			for (j = 0; j < num_rxq; j++) {
				kfree(rx_qgrp->singleq.rxqs[j]);
				rx_qgrp->singleq.rxqs[j] = NULL;
			}
		}
	}
	kfree(vport->rxq_grps);
	vport->rxq_grps = NULL;
}

/**
 * idpf_vport_queue_grp_rel_all - Release all queue groups
 * @vport: vport to release queue groups for
 */
static void idpf_vport_queue_grp_rel_all(struct idpf_vport *vport)
{
	idpf_txq_group_rel(vport);
	idpf_rxq_group_rel(vport);
}

/**
 * idpf_vport_queues_rel - Free memory for all queues
 * @vport: virtual port
 *
 * Free the memory allocated for queues associated to a vport
 */
void idpf_vport_queues_rel(struct idpf_vport *vport)
{
	idpf_tx_desc_rel_all(vport);
	idpf_rx_desc_rel_all(vport);
	idpf_vport_queue_grp_rel_all(vport);

	kfree(vport->txqs);
	vport->txqs = NULL;
}

/**
 * idpf_vport_init_fast_path_txqs - Initialize fast path txq array
 * @vport: vport to init txqs on
 *
 * We get a queue index from skb->queue_mapping and we need a fast way to
 * dereference the queue from queue groups.  This allows us to quickly pull a
 * txq based on a queue index.
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_vport_init_fast_path_txqs(struct idpf_vport *vport)
{
	int i, j, k = 0;

	vport->txqs = kcalloc(vport->num_txq, sizeof(struct idpf_queue *),
			      GFP_KERNEL);

	if (!vport->txqs)
		return -ENOMEM;

	for (i = 0; i < vport->num_txq_grp; i++) {
		struct idpf_txq_group *tx_grp = &vport->txq_grps[i];

		for (j = 0; j < tx_grp->num_txq; j++, k++) {
			vport->txqs[k] = tx_grp->txqs[j];
			vport->txqs[k]->idx = k;
		}
	}

	return 0;
}

/**
 * idpf_vport_init_num_qs - Initialize number of queues
 * @vport: vport to initialize queues
 * @vport_msg: data to be filled into vport
 */
void idpf_vport_init_num_qs(struct idpf_vport *vport,
			    struct virtchnl2_create_vport *vport_msg)
{
	struct idpf_vport_user_config_data *config_data;
	u16 idx = vport->idx;

	config_data = &vport->adapter->vport_config[idx]->user_config;
	vport->num_txq = le16_to_cpu(vport_msg->num_tx_q);
	vport->num_rxq = le16_to_cpu(vport_msg->num_rx_q);
	/* number of txqs and rxqs in config data will be zeros only in the
	 * driver load path and we dont update them there after
	 */
	if (!config_data->num_req_tx_qs && !config_data->num_req_rx_qs) {
		config_data->num_req_tx_qs = le16_to_cpu(vport_msg->num_tx_q);
		config_data->num_req_rx_qs = le16_to_cpu(vport_msg->num_rx_q);
	}

	if (idpf_is_queue_model_split(vport->txq_model))
		vport->num_complq = le16_to_cpu(vport_msg->num_tx_complq);
	if (idpf_is_queue_model_split(vport->rxq_model))
		vport->num_bufq = le16_to_cpu(vport_msg->num_rx_bufq);

	/* Adjust number of buffer queues per Rx queue group. */
	if (!idpf_is_queue_model_split(vport->rxq_model)) {
		vport->num_bufqs_per_qgrp = 0;
		vport->bufq_size[0] = IDPF_RX_BUF_2048;

		return;
	}

	vport->num_bufqs_per_qgrp = IDPF_MAX_BUFQS_PER_RXQ_GRP;
	/* Bufq[0] default buffer size is 4K
	 * Bufq[1] default buffer size is 2K
	 */
	vport->bufq_size[0] = IDPF_RX_BUF_4096;
	vport->bufq_size[1] = IDPF_RX_BUF_2048;
}

/**
 * idpf_vport_calc_num_q_desc - Calculate number of queue groups
 * @vport: vport to calculate q groups for
 */
void idpf_vport_calc_num_q_desc(struct idpf_vport *vport)
{
	struct idpf_vport_user_config_data *config_data;
	int num_bufqs = vport->num_bufqs_per_qgrp;
	u32 num_req_txq_desc, num_req_rxq_desc;
	u16 idx = vport->idx;
	int i;

	config_data =  &vport->adapter->vport_config[idx]->user_config;
	num_req_txq_desc = config_data->num_req_txq_desc;
	num_req_rxq_desc = config_data->num_req_rxq_desc;

	vport->complq_desc_count = 0;
	if (num_req_txq_desc) {
		vport->txq_desc_count = num_req_txq_desc;
		if (idpf_is_queue_model_split(vport->txq_model)) {
			vport->complq_desc_count = num_req_txq_desc;
			if (vport->complq_desc_count < IDPF_MIN_TXQ_COMPLQ_DESC)
				vport->complq_desc_count =
					IDPF_MIN_TXQ_COMPLQ_DESC;
		}
	} else {
		vport->txq_desc_count =	IDPF_DFLT_TX_Q_DESC_COUNT;
		if (idpf_is_queue_model_split(vport->txq_model))
			vport->complq_desc_count =
				IDPF_DFLT_TX_COMPLQ_DESC_COUNT;
	}

	if (num_req_rxq_desc)
		vport->rxq_desc_count = num_req_rxq_desc;
	else
		vport->rxq_desc_count = IDPF_DFLT_RX_Q_DESC_COUNT;

	for (i = 0; i < num_bufqs; i++) {
		if (!vport->bufq_desc_count[i])
			vport->bufq_desc_count[i] =
				IDPF_RX_BUFQ_DESC_COUNT(vport->rxq_desc_count,
							num_bufqs);
	}
}

/**
 * idpf_vport_calc_total_qs - Calculate total number of queues
 * @adapter: private data struct
 * @vport_idx: vport idx to retrieve vport pointer
 * @vport_msg: message to fill with data
 * @max_q: vport max queue info
 *
 * Return 0 on success, error value on failure.
 */
int idpf_vport_calc_total_qs(struct idpf_adapter *adapter, u16 vport_idx,
			     struct virtchnl2_create_vport *vport_msg,
			     struct idpf_vport_max_q *max_q)
{
	int dflt_splitq_txq_grps = 0, dflt_singleq_txqs = 0;
	int dflt_splitq_rxq_grps = 0, dflt_singleq_rxqs = 0;
	u16 num_req_tx_qs = 0, num_req_rx_qs = 0;
	struct idpf_vport_config *vport_config;
	u16 num_txq_grps, num_rxq_grps;
	u32 num_qs;

	vport_config = adapter->vport_config[vport_idx];
	if (vport_config) {
		num_req_tx_qs = vport_config->user_config.num_req_tx_qs;
		num_req_rx_qs = vport_config->user_config.num_req_rx_qs;
	} else {
		int num_cpus;

		/* Restrict num of queues to cpus online as a default
		 * configuration to give best performance. User can always
		 * override to a max number of queues via ethtool.
		 */
		num_cpus = num_online_cpus();

		dflt_splitq_txq_grps = min_t(int, max_q->max_txq, num_cpus);
		dflt_singleq_txqs = min_t(int, max_q->max_txq, num_cpus);
		dflt_splitq_rxq_grps = min_t(int, max_q->max_rxq, num_cpus);
		dflt_singleq_rxqs = min_t(int, max_q->max_rxq, num_cpus);
	}

	if (idpf_is_queue_model_split(le16_to_cpu(vport_msg->txq_model))) {
		num_txq_grps = num_req_tx_qs ? num_req_tx_qs : dflt_splitq_txq_grps;
		vport_msg->num_tx_complq = cpu_to_le16(num_txq_grps *
						       IDPF_COMPLQ_PER_GROUP);
		vport_msg->num_tx_q = cpu_to_le16(num_txq_grps *
						  IDPF_DFLT_SPLITQ_TXQ_PER_GROUP);
	} else {
		num_txq_grps = IDPF_DFLT_SINGLEQ_TX_Q_GROUPS;
		num_qs = num_txq_grps * (num_req_tx_qs ? num_req_tx_qs :
					 dflt_singleq_txqs);
		vport_msg->num_tx_q = cpu_to_le16(num_qs);
		vport_msg->num_tx_complq = 0;
	}
	if (idpf_is_queue_model_split(le16_to_cpu(vport_msg->rxq_model))) {
		num_rxq_grps = num_req_rx_qs ? num_req_rx_qs : dflt_splitq_rxq_grps;
		vport_msg->num_rx_bufq = cpu_to_le16(num_rxq_grps *
						     IDPF_MAX_BUFQS_PER_RXQ_GRP);
		vport_msg->num_rx_q = cpu_to_le16(num_rxq_grps *
						  IDPF_DFLT_SPLITQ_RXQ_PER_GROUP);
	} else {
		num_rxq_grps = IDPF_DFLT_SINGLEQ_RX_Q_GROUPS;
		num_qs = num_rxq_grps * (num_req_rx_qs ? num_req_rx_qs :
					 dflt_singleq_rxqs);
		vport_msg->num_rx_q = cpu_to_le16(num_qs);
		vport_msg->num_rx_bufq = 0;
	}

	return 0;
}

/**
 * idpf_vport_calc_num_q_groups - Calculate number of queue groups
 * @vport: vport to calculate q groups for
 */
void idpf_vport_calc_num_q_groups(struct idpf_vport *vport)
{
	if (idpf_is_queue_model_split(vport->txq_model))
		vport->num_txq_grp = vport->num_txq;
	else
		vport->num_txq_grp = IDPF_DFLT_SINGLEQ_TX_Q_GROUPS;

	if (idpf_is_queue_model_split(vport->rxq_model))
		vport->num_rxq_grp = vport->num_rxq;
	else
		vport->num_rxq_grp = IDPF_DFLT_SINGLEQ_RX_Q_GROUPS;
}

/**
 * idpf_vport_calc_numq_per_grp - Calculate number of queues per group
 * @vport: vport to calculate queues for
 * @num_txq: return parameter for number of TX queues
 * @num_rxq: return parameter for number of RX queues
 */
static void idpf_vport_calc_numq_per_grp(struct idpf_vport *vport,
					 u16 *num_txq, u16 *num_rxq)
{
	if (idpf_is_queue_model_split(vport->txq_model))
		*num_txq = IDPF_DFLT_SPLITQ_TXQ_PER_GROUP;
	else
		*num_txq = vport->num_txq;

	if (idpf_is_queue_model_split(vport->rxq_model))
		*num_rxq = IDPF_DFLT_SPLITQ_RXQ_PER_GROUP;
	else
		*num_rxq = vport->num_rxq;
}

/**
 * idpf_rxq_set_descids - set the descids supported by this queue
 * @vport: virtual port data structure
 * @q: rx queue for which descids are set
 *
 */
static void idpf_rxq_set_descids(struct idpf_vport *vport, struct idpf_queue *q)
{
	if (vport->rxq_model == VIRTCHNL2_QUEUE_MODEL_SPLIT) {
		q->rxdids = VIRTCHNL2_RXDID_2_FLEX_SPLITQ_M;
	} else {
		if (vport->base_rxd)
			q->rxdids = VIRTCHNL2_RXDID_1_32B_BASE_M;
		else
			q->rxdids = VIRTCHNL2_RXDID_2_FLEX_SQ_NIC_M;
	}
}

/**
 * idpf_txq_group_alloc - Allocate all txq group resources
 * @vport: vport to allocate txq groups for
 * @num_txq: number of txqs to allocate for each group
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_txq_group_alloc(struct idpf_vport *vport, u16 num_txq)
{
	int err, i;

	vport->txq_grps = kcalloc(vport->num_txq_grp,
				  sizeof(*vport->txq_grps), GFP_KERNEL);
	if (!vport->txq_grps)
		return -ENOMEM;

	for (i = 0; i < vport->num_txq_grp; i++) {
		struct idpf_txq_group *tx_qgrp = &vport->txq_grps[i];
		struct idpf_adapter *adapter = vport->adapter;
		int j;

		tx_qgrp->vport = vport;
		tx_qgrp->num_txq = num_txq;

		for (j = 0; j < tx_qgrp->num_txq; j++) {
			tx_qgrp->txqs[j] = kzalloc(sizeof(*tx_qgrp->txqs[j]),
						   GFP_KERNEL);
			if (!tx_qgrp->txqs[j]) {
				err = -ENOMEM;
				goto err_alloc;
			}
		}

		for (j = 0; j < tx_qgrp->num_txq; j++) {
			struct idpf_queue *q = tx_qgrp->txqs[j];

			q->dev = &adapter->pdev->dev;
			q->desc_count = vport->txq_desc_count;
			q->tx_max_bufs = idpf_get_max_tx_bufs(adapter);
			q->tx_min_pkt_len = idpf_get_min_tx_pkt_len(adapter);
			q->vport = vport;
			q->txq_grp = tx_qgrp;
			hash_init(q->sched_buf_hash);

			if (!idpf_is_cap_ena(adapter, IDPF_OTHER_CAPS,
					     VIRTCHNL2_CAP_SPLITQ_QSCHED))
				set_bit(__IDPF_Q_FLOW_SCH_EN, q->flags);
		}

		if (!idpf_is_queue_model_split(vport->txq_model))
			continue;

		tx_qgrp->complq = kcalloc(IDPF_COMPLQ_PER_GROUP,
					  sizeof(*tx_qgrp->complq),
					  GFP_KERNEL);
		if (!tx_qgrp->complq) {
			err = -ENOMEM;
			goto err_alloc;
		}

		tx_qgrp->complq->dev = &adapter->pdev->dev;
		tx_qgrp->complq->desc_count = vport->complq_desc_count;
		tx_qgrp->complq->vport = vport;
		tx_qgrp->complq->txq_grp = tx_qgrp;
	}

	return 0;

err_alloc:
	idpf_txq_group_rel(vport);

	return err;
}

/**
 * idpf_rxq_group_alloc - Allocate all rxq group resources
 * @vport: vport to allocate rxq groups for
 * @num_rxq: number of rxqs to allocate for each group
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_rxq_group_alloc(struct idpf_vport *vport, u16 num_rxq)
{
	struct idpf_adapter *adapter = vport->adapter;
	struct idpf_queue *q;
	int i, k, err = 0;

	vport->rxq_grps = kcalloc(vport->num_rxq_grp,
				  sizeof(struct idpf_rxq_group), GFP_KERNEL);
	if (!vport->rxq_grps)
		return -ENOMEM;

	for (i = 0; i < vport->num_rxq_grp; i++) {
		struct idpf_rxq_group *rx_qgrp = &vport->rxq_grps[i];
		int j;

		rx_qgrp->vport = vport;
		if (!idpf_is_queue_model_split(vport->rxq_model)) {
			rx_qgrp->singleq.num_rxq = num_rxq;
			for (j = 0; j < num_rxq; j++) {
				rx_qgrp->singleq.rxqs[j] =
						kzalloc(sizeof(*rx_qgrp->singleq.rxqs[j]),
							GFP_KERNEL);
				if (!rx_qgrp->singleq.rxqs[j]) {
					err = -ENOMEM;
					goto err_alloc;
				}
			}
			goto skip_splitq_rx_init;
		}
		rx_qgrp->splitq.num_rxq_sets = num_rxq;

		for (j = 0; j < num_rxq; j++) {
			rx_qgrp->splitq.rxq_sets[j] =
				kzalloc(sizeof(struct idpf_rxq_set),
					GFP_KERNEL);
			if (!rx_qgrp->splitq.rxq_sets[j]) {
				err = -ENOMEM;
				goto err_alloc;
			}
		}

		rx_qgrp->splitq.bufq_sets = kcalloc(vport->num_bufqs_per_qgrp,
						    sizeof(struct idpf_bufq_set),
						    GFP_KERNEL);
		if (!rx_qgrp->splitq.bufq_sets) {
			err = -ENOMEM;
			goto err_alloc;
		}

		for (j = 0; j < vport->num_bufqs_per_qgrp; j++) {
			struct idpf_bufq_set *bufq_set =
				&rx_qgrp->splitq.bufq_sets[j];
			int swq_size = sizeof(struct idpf_sw_queue);

			q = &rx_qgrp->splitq.bufq_sets[j].bufq;
			q->dev = &adapter->pdev->dev;
			q->desc_count = vport->bufq_desc_count[j];
			q->vport = vport;
			q->rxq_grp = rx_qgrp;
			q->idx = j;
			q->rx_buf_size = vport->bufq_size[j];
			q->rx_buffer_low_watermark = IDPF_LOW_WATERMARK;
			q->rx_buf_stride = IDPF_RX_BUF_STRIDE;
			if (idpf_is_cap_ena_all(adapter, IDPF_HSPLIT_CAPS,
						IDPF_CAP_HSPLIT) &&
			    idpf_is_queue_model_split(vport->rxq_model)) {
				q->rx_hsplit_en = true;
				q->rx_hbuf_size = IDPF_HDR_BUF_SIZE;
			}

			bufq_set->num_refillqs = num_rxq;
			bufq_set->refillqs = kcalloc(num_rxq, swq_size,
						     GFP_KERNEL);
			if (!bufq_set->refillqs) {
				err = -ENOMEM;
				goto err_alloc;
			}
			for (k = 0; k < bufq_set->num_refillqs; k++) {
				struct idpf_sw_queue *refillq =
					&bufq_set->refillqs[k];

				refillq->dev = &vport->adapter->pdev->dev;
				refillq->desc_count =
					vport->bufq_desc_count[j];
				set_bit(__IDPF_Q_GEN_CHK, refillq->flags);
				set_bit(__IDPF_RFLQ_GEN_CHK, refillq->flags);
				refillq->ring = kcalloc(refillq->desc_count,
							sizeof(u16),
							GFP_KERNEL);
				if (!refillq->ring) {
					err = -ENOMEM;
					goto err_alloc;
				}
			}
		}

skip_splitq_rx_init:
		for (j = 0; j < num_rxq; j++) {
			if (!idpf_is_queue_model_split(vport->rxq_model)) {
				q = rx_qgrp->singleq.rxqs[j];
				goto setup_rxq;
			}
			q = &rx_qgrp->splitq.rxq_sets[j]->rxq;
			rx_qgrp->splitq.rxq_sets[j]->refillq0 =
			      &rx_qgrp->splitq.bufq_sets[0].refillqs[j];
			if (vport->num_bufqs_per_qgrp > IDPF_SINGLE_BUFQ_PER_RXQ_GRP)
				rx_qgrp->splitq.rxq_sets[j]->refillq1 =
				      &rx_qgrp->splitq.bufq_sets[1].refillqs[j];

			if (idpf_is_cap_ena_all(adapter, IDPF_HSPLIT_CAPS,
						IDPF_CAP_HSPLIT) &&
			    idpf_is_queue_model_split(vport->rxq_model)) {
				q->rx_hsplit_en = true;
				q->rx_hbuf_size = IDPF_HDR_BUF_SIZE;
			}

setup_rxq:
			q->dev = &adapter->pdev->dev;
			q->desc_count = vport->rxq_desc_count;
			q->vport = vport;
			q->rxq_grp = rx_qgrp;
			q->idx = (i * num_rxq) + j;
			/* In splitq mode, RXQ buffer size should be
			 * set to that of the first buffer queue
			 * associated with this RXQ
			 */
			q->rx_buf_size = vport->bufq_size[0];
			q->rx_buffer_low_watermark = IDPF_LOW_WATERMARK;
			q->rx_max_pkt_size = vport->netdev->mtu +
							IDPF_PACKET_HDR_PAD;
			idpf_rxq_set_descids(vport, q);
		}
	}

err_alloc:
	if (err)
		idpf_rxq_group_rel(vport);

	return err;
}

/**
 * idpf_vport_queue_grp_alloc_all - Allocate all queue groups/resources
 * @vport: vport with qgrps to allocate
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_vport_queue_grp_alloc_all(struct idpf_vport *vport)
{
	u16 num_txq, num_rxq;
	int err;

	idpf_vport_calc_numq_per_grp(vport, &num_txq, &num_rxq);

	err = idpf_txq_group_alloc(vport, num_txq);
	if (err)
		goto err_out;

	err = idpf_rxq_group_alloc(vport, num_rxq);
	if (err)
		goto err_out;

	return 0;

err_out:
	idpf_vport_queue_grp_rel_all(vport);

	return err;
}

/**
 * idpf_vport_queues_alloc - Allocate memory for all queues
 * @vport: virtual port
 *
 * Allocate memory for queues associated with a vport.  Returns 0 on success,
 * negative on failure.
 */
int idpf_vport_queues_alloc(struct idpf_vport *vport)
{
	int err;

	err = idpf_vport_queue_grp_alloc_all(vport);
	if (err)
		goto err_out;

	err = idpf_tx_desc_alloc_all(vport);
	if (err)
		goto err_out;

	err = idpf_rx_desc_alloc_all(vport);
	if (err)
		goto err_out;

	err = idpf_vport_init_fast_path_txqs(vport);
	if (err)
		goto err_out;

	return 0;

err_out:
	idpf_vport_queues_rel(vport);

	return err;
}

/**
 * idpf_tx_splitq_build_ctb - populate command tag and size for queue
 * based scheduling descriptors
 * @desc: descriptor to populate
 * @params: pointer to tx params struct
 * @td_cmd: command to be filled in desc
 * @size: size of buffer
 */
void idpf_tx_splitq_build_ctb(union idpf_tx_flex_desc *desc,
			      struct idpf_tx_splitq_params *params,
			      u16 td_cmd, u16 size)
{
	desc->q.qw1.cmd_dtype =
		cpu_to_le16(params->dtype & IDPF_FLEX_TXD_QW1_DTYPE_M);
	desc->q.qw1.cmd_dtype |=
		cpu_to_le16((td_cmd << IDPF_FLEX_TXD_QW1_CMD_S) &
			    IDPF_FLEX_TXD_QW1_CMD_M);
	desc->q.qw1.buf_size = cpu_to_le16((u16)size);
	desc->q.qw1.l2tags.l2tag1 = cpu_to_le16(params->td_tag);
}

/**
 * idpf_tx_splitq_build_flow_desc - populate command tag and size for flow
 * scheduling descriptors
 * @desc: descriptor to populate
 * @params: pointer to tx params struct
 * @td_cmd: command to be filled in desc
 * @size: size of buffer
 */
void idpf_tx_splitq_build_flow_desc(union idpf_tx_flex_desc *desc,
				    struct idpf_tx_splitq_params *params,
				    u16 td_cmd, u16 size)
{
	desc->flow.qw1.cmd_dtype = (u16)params->dtype | td_cmd;
	desc->flow.qw1.rxr_bufsize = cpu_to_le16((u16)size);
	desc->flow.qw1.compl_tag = cpu_to_le16(params->compl_tag);
}

/**
 * idpf_tx_maybe_stop_common - 1st level check for common Tx stop conditions
 * @tx_q: the queue to be checked
 * @size: number of descriptors we want to assure is available
 *
 * Returns 0 if stop is not needed
 */
static int idpf_tx_maybe_stop_common(struct idpf_queue *tx_q, unsigned int size)
{
	struct netdev_queue *nq;

	if (likely(IDPF_DESC_UNUSED(tx_q) >= size))
		return 0;

	u64_stats_update_begin(&tx_q->stats_sync);
	u64_stats_inc(&tx_q->q_stats.tx.q_busy);
	u64_stats_update_end(&tx_q->stats_sync);

	nq = netdev_get_tx_queue(tx_q->vport->netdev, tx_q->idx);

	return netif_txq_maybe_stop(nq, IDPF_DESC_UNUSED(tx_q), size, size);
}

/**
 * idpf_tx_maybe_stop_splitq - 1st level check for Tx splitq stop conditions
 * @tx_q: the queue to be checked
 * @descs_needed: number of descriptors required for this packet
 *
 * Returns 0 if stop is not needed
 */
static int idpf_tx_maybe_stop_splitq(struct idpf_queue *tx_q,
				     unsigned int descs_needed)
{
	if (idpf_tx_maybe_stop_common(tx_q, descs_needed))
		goto splitq_stop;

	/* If there are too many outstanding completions expected on the
	 * completion queue, stop the TX queue to give the device some time to
	 * catch up
	 */
	if (unlikely(IDPF_TX_COMPLQ_PENDING(tx_q->txq_grp) >
		     IDPF_TX_COMPLQ_OVERFLOW_THRESH(tx_q->txq_grp->complq)))
		goto splitq_stop;

	/* Also check for available book keeping buffers; if we are low, stop
	 * the queue to wait for more completions
	 */
	if (unlikely(IDPF_TX_BUF_RSV_LOW(tx_q)))
		goto splitq_stop;

	return 0;

splitq_stop:
	u64_stats_update_begin(&tx_q->stats_sync);
	u64_stats_inc(&tx_q->q_stats.tx.q_busy);
	u64_stats_update_end(&tx_q->stats_sync);
	netif_stop_subqueue(tx_q->vport->netdev, tx_q->idx);

	return -EBUSY;
}

/**
 * idpf_tx_buf_hw_update - Store the new tail value
 * @tx_q: queue to bump
 * @val: new tail index
 * @xmit_more: more skb's pending
 *
 * The naming here is special in that 'hw' signals that this function is about
 * to do a register write to update our queue status. We know this can only
 * mean tail here as HW should be owning head for TX.
 */
static void idpf_tx_buf_hw_update(struct idpf_queue *tx_q, u32 val,
				  bool xmit_more)
{
	struct netdev_queue *nq;

	nq = netdev_get_tx_queue(tx_q->vport->netdev, tx_q->idx);
	tx_q->next_to_use = val;

	idpf_tx_maybe_stop_common(tx_q, IDPF_TX_DESC_NEEDED);

	/* Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.  (Only
	 * applicable for weak-ordered memory model archs,
	 * such as IA-64).
	 */
	wmb();

	/* notify HW of packet */
	if (netif_xmit_stopped(nq) || !xmit_more)
		writel(val, tx_q->tail);
}

/**
 * idpf_tx_desc_count_required - calculate number of Tx descriptors needed
 * @skb: send buffer
 *
 * Returns number of data descriptors needed for this skb.
 */
static unsigned int idpf_tx_desc_count_required(struct sk_buff *skb)
{
	const struct skb_shared_info *shinfo;
	unsigned int count = 0, i;

	count += !!skb_headlen(skb);

	if (!skb_is_nonlinear(skb))
		return count;

	shinfo = skb_shinfo(skb);
	for (i = 0; i < shinfo->nr_frags; i++) {
		unsigned int size;

		size = skb_frag_size(&shinfo->frags[i]);

		/* We only need to use the idpf_size_to_txd_count check if the
		 * fragment is going to span multiple descriptors,
		 * i.e. size >= 16K.
		 */
		if (size >= SZ_16K)
			count += idpf_size_to_txd_count(size);
		else
			count++;
	}

	return count;
}

/**
 * idpf_tx_dma_map_error - handle TX DMA map errors
 * @txq: queue to send buffer on
 * @skb: send buffer
 * @first: original first buffer info buffer for packet
 * @idx: starting point on ring to unwind
 */
static void idpf_tx_dma_map_error(struct idpf_queue *txq, struct sk_buff *skb,
				  struct idpf_tx_buf *first, u16 idx)
{
	u64_stats_update_begin(&txq->stats_sync);
	u64_stats_inc(&txq->q_stats.tx.dma_map_errs);
	u64_stats_update_end(&txq->stats_sync);

	/* clear dma mappings for failed tx_buf map */
	for (;;) {
		struct idpf_tx_buf *tx_buf;

		tx_buf = &txq->tx_buf[idx];
		idpf_tx_buf_rel(txq, tx_buf);
		if (tx_buf == first)
			break;
		if (idx == 0)
			idx = txq->desc_count;
		idx--;
	}

	if (skb_is_gso(skb)) {
		union idpf_tx_flex_desc *tx_desc;

		/* If we failed a DMA mapping for a TSO packet, we will have
		 * used one additional descriptor for a context
		 * descriptor. Reset that here.
		 */
		tx_desc = IDPF_FLEX_TX_DESC(txq, idx);
		memset(tx_desc, 0, sizeof(struct idpf_flex_tx_ctx_desc));
		if (idx == 0)
			idx = txq->desc_count;
		idx--;
	}

	/* Update tail in case netdev_xmit_more was previously true */
	idpf_tx_buf_hw_update(txq, idx, false);
}

/**
 * idpf_tx_splitq_bump_ntu - adjust NTU and generation
 * @txq: the tx ring to wrap
 * @ntu: ring index to bump
 */
static unsigned int idpf_tx_splitq_bump_ntu(struct idpf_queue *txq, u16 ntu)
{
	ntu++;

	if (ntu == txq->desc_count) {
		ntu = 0;
		txq->compl_tag_cur_gen = IDPF_TX_ADJ_COMPL_TAG_GEN(txq);
	}

	return ntu;
}

/**
 * idpf_tx_splitq_map - Build the Tx flex descriptor
 * @tx_q: queue to send buffer on
 * @params: pointer to splitq params struct
 * @first: first buffer info buffer to use
 *
 * This function loops over the skb data pointed to by *first
 * and gets a physical address for each memory location and programs
 * it and the length into the transmit flex descriptor.
 */
static void idpf_tx_splitq_map(struct idpf_queue *tx_q,
			       struct idpf_tx_splitq_params *params,
			       struct idpf_tx_buf *first)
{
	union idpf_tx_flex_desc *tx_desc;
	unsigned int data_len, size;
	struct idpf_tx_buf *tx_buf;
	u16 i = tx_q->next_to_use;
	struct netdev_queue *nq;
	struct sk_buff *skb;
	skb_frag_t *frag;
	u16 td_cmd = 0;
	dma_addr_t dma;

	skb = first->skb;

	td_cmd = params->offload.td_cmd;

	data_len = skb->data_len;
	size = skb_headlen(skb);

	tx_desc = IDPF_FLEX_TX_DESC(tx_q, i);

	dma = dma_map_single(tx_q->dev, skb->data, size, DMA_TO_DEVICE);

	tx_buf = first;

	params->compl_tag =
		(tx_q->compl_tag_cur_gen << tx_q->compl_tag_gen_s) | i;

	for (frag = &skb_shinfo(skb)->frags[0];; frag++) {
		unsigned int max_data = IDPF_TX_MAX_DESC_DATA_ALIGNED;

		if (dma_mapping_error(tx_q->dev, dma))
			return idpf_tx_dma_map_error(tx_q, skb, first, i);

		tx_buf->compl_tag = params->compl_tag;

		/* record length, and DMA address */
		dma_unmap_len_set(tx_buf, len, size);
		dma_unmap_addr_set(tx_buf, dma, dma);

		/* buf_addr is in same location for both desc types */
		tx_desc->q.buf_addr = cpu_to_le64(dma);

		/* The stack can send us fragments that are too large for a
		 * single descriptor i.e. frag size > 16K-1. We will need to
		 * split the fragment across multiple descriptors in this case.
		 * To adhere to HW alignment restrictions, the fragment needs
		 * to be split such that the first chunk ends on a 4K boundary
		 * and all subsequent chunks start on a 4K boundary. We still
		 * want to send as much data as possible though, so our
		 * intermediate descriptor chunk size will be 12K.
		 *
		 * For example, consider a 32K fragment mapped to DMA addr 2600.
		 * ------------------------------------------------------------
		 * |                    frag_size = 32K                       |
		 * ------------------------------------------------------------
		 * |2600		  |16384	    |28672
		 *
		 * 3 descriptors will be used for this fragment. The HW expects
		 * the descriptors to contain the following:
		 * ------------------------------------------------------------
		 * | size = 13784         | size = 12K      | size = 6696     |
		 * | dma = 2600           | dma = 16384     | dma = 28672     |
		 * ------------------------------------------------------------
		 *
		 * We need to first adjust the max_data for the first chunk so
		 * that it ends on a 4K boundary. By negating the value of the
		 * DMA address and taking only the low order bits, we're
		 * effectively calculating
		 *	4K - (DMA addr lower order bits) =
		 *				bytes to next boundary.
		 *
		 * Add that to our base aligned max_data (12K) and we have
		 * our first chunk size. In the example above,
		 *	13784 = 12K + (4096-2600)
		 *
		 * After guaranteeing the first chunk ends on a 4K boundary, we
		 * will give the intermediate descriptors 12K chunks and
		 * whatever is left to the final descriptor. This ensures that
		 * all descriptors used for the remaining chunks of the
		 * fragment start on a 4K boundary and we use as few
		 * descriptors as possible.
		 */
		max_data += -dma & (IDPF_TX_MAX_READ_REQ_SIZE - 1);
		while (unlikely(size > IDPF_TX_MAX_DESC_DATA)) {
			idpf_tx_splitq_build_desc(tx_desc, params, td_cmd,
						  max_data);

			tx_desc++;
			i++;

			if (i == tx_q->desc_count) {
				tx_desc = IDPF_FLEX_TX_DESC(tx_q, 0);
				i = 0;
				tx_q->compl_tag_cur_gen =
					IDPF_TX_ADJ_COMPL_TAG_GEN(tx_q);
			}

			/* Since this packet has a buffer that is going to span
			 * multiple descriptors, it's going to leave holes in
			 * to the TX buffer ring. To ensure these holes do not
			 * cause issues in the cleaning routines, we will clear
			 * them of any stale data and assign them the same
			 * completion tag as the current packet. Then when the
			 * packet is being cleaned, the cleaning routines will
			 * simply pass over these holes and finish cleaning the
			 * rest of the packet.
			 */
			memset(&tx_q->tx_buf[i], 0, sizeof(struct idpf_tx_buf));
			tx_q->tx_buf[i].compl_tag = params->compl_tag;

			/* Adjust the DMA offset and the remaining size of the
			 * fragment.  On the first iteration of this loop,
			 * max_data will be >= 12K and <= 16K-1.  On any
			 * subsequent iteration of this loop, max_data will
			 * always be 12K.
			 */
			dma += max_data;
			size -= max_data;

			/* Reset max_data since remaining chunks will be 12K
			 * at most
			 */
			max_data = IDPF_TX_MAX_DESC_DATA_ALIGNED;

			/* buf_addr is in same location for both desc types */
			tx_desc->q.buf_addr = cpu_to_le64(dma);
		}

		if (!data_len)
			break;

		idpf_tx_splitq_build_desc(tx_desc, params, td_cmd, size);
		tx_desc++;
		i++;

		if (i == tx_q->desc_count) {
			tx_desc = IDPF_FLEX_TX_DESC(tx_q, 0);
			i = 0;
			tx_q->compl_tag_cur_gen = IDPF_TX_ADJ_COMPL_TAG_GEN(tx_q);
		}

		size = skb_frag_size(frag);
		data_len -= size;

		dma = skb_frag_dma_map(tx_q->dev, frag, 0, size,
				       DMA_TO_DEVICE);

		tx_buf = &tx_q->tx_buf[i];
	}

	/* record SW timestamp if HW timestamp is not available */
	skb_tx_timestamp(skb);

	/* write last descriptor with RS and EOP bits */
	td_cmd |= params->eop_cmd;
	idpf_tx_splitq_build_desc(tx_desc, params, td_cmd, size);
	i = idpf_tx_splitq_bump_ntu(tx_q, i);

	/* set next_to_watch value indicating a packet is present */
	first->next_to_watch = tx_desc;

	tx_q->txq_grp->num_completions_pending++;

	/* record bytecount for BQL */
	nq = netdev_get_tx_queue(tx_q->vport->netdev, tx_q->idx);
	netdev_tx_sent_queue(nq, first->bytecount);

	idpf_tx_buf_hw_update(tx_q, i, netdev_xmit_more());
}

/**
 * idpf_tso - computes mss and TSO length to prepare for TSO
 * @skb: pointer to skb
 * @off: pointer to struct that holds offload parameters
 *
 * Returns error (negative) if TSO was requested but cannot be applied to the
 * given skb, 0 if TSO does not apply to the given skb, or 1 otherwise.
 */
static int idpf_tso(struct sk_buff *skb, struct idpf_tx_offload_params *off)
{
	const struct skb_shared_info *shinfo = skb_shinfo(skb);
	union {
		struct iphdr *v4;
		struct ipv6hdr *v6;
		unsigned char *hdr;
	} ip;
	union {
		struct tcphdr *tcp;
		struct udphdr *udp;
		unsigned char *hdr;
	} l4;
	u32 paylen, l4_start;
	int err;

	if (!shinfo->gso_size)
		return 0;

	err = skb_cow_head(skb, 0);
	if (err < 0)
		return err;

	ip.hdr = skb_network_header(skb);
	l4.hdr = skb_transport_header(skb);

	/* initialize outer IP header fields */
	if (ip.v4->version == 4) {
		ip.v4->tot_len = 0;
		ip.v4->check = 0;
	} else if (ip.v6->version == 6) {
		ip.v6->payload_len = 0;
	}

	l4_start = skb_transport_offset(skb);

	/* remove payload length from checksum */
	paylen = skb->len - l4_start;

	switch (shinfo->gso_type & ~SKB_GSO_DODGY) {
	case SKB_GSO_TCPV4:
	case SKB_GSO_TCPV6:
		csum_replace_by_diff(&l4.tcp->check,
				     (__force __wsum)htonl(paylen));
		off->tso_hdr_len = __tcp_hdrlen(l4.tcp) + l4_start;
		break;
	case SKB_GSO_UDP_L4:
		csum_replace_by_diff(&l4.udp->check,
				     (__force __wsum)htonl(paylen));
		/* compute length of segmentation header */
		off->tso_hdr_len = sizeof(struct udphdr) + l4_start;
		l4.udp->len = htons(shinfo->gso_size + sizeof(struct udphdr));
		break;
	default:
		return -EINVAL;
	}

	off->tso_len = skb->len - off->tso_hdr_len;
	off->mss = shinfo->gso_size;
	off->tso_segs = shinfo->gso_segs;

	off->tx_flags |= IDPF_TX_FLAGS_TSO;

	return 1;
}

/**
 * __idpf_chk_linearize - Check skb is not using too many buffers
 * @skb: send buffer
 * @max_bufs: maximum number of buffers
 *
 * For TSO we need to count the TSO header and segment payload separately.  As
 * such we need to check cases where we have max_bufs-1 fragments or more as we
 * can potentially require max_bufs+1 DMA transactions, 1 for the TSO header, 1
 * for the segment payload in the first descriptor, and another max_buf-1 for
 * the fragments.
 */
static bool __idpf_chk_linearize(struct sk_buff *skb, unsigned int max_bufs)
{
	const struct skb_shared_info *shinfo = skb_shinfo(skb);
	const skb_frag_t *frag, *stale;
	int nr_frags, sum;

	/* no need to check if number of frags is less than max_bufs - 1 */
	nr_frags = shinfo->nr_frags;
	if (nr_frags < (max_bufs - 1))
		return false;

	/* We need to walk through the list and validate that each group
	 * of max_bufs-2 fragments totals at least gso_size.
	 */
	nr_frags -= max_bufs - 2;
	frag = &shinfo->frags[0];

	/* Initialize size to the negative value of gso_size minus 1.  We use
	 * this as the worst case scenario in which the frag ahead of us only
	 * provides one byte which is why we are limited to max_bufs-2
	 * descriptors for a single transmit as the header and previous
	 * fragment are already consuming 2 descriptors.
	 */
	sum = 1 - shinfo->gso_size;

	/* Add size of frags 0 through 4 to create our initial sum */
	sum += skb_frag_size(frag++);
	sum += skb_frag_size(frag++);
	sum += skb_frag_size(frag++);
	sum += skb_frag_size(frag++);
	sum += skb_frag_size(frag++);

	/* Walk through fragments adding latest fragment, testing it, and
	 * then removing stale fragments from the sum.
	 */
	for (stale = &shinfo->frags[0];; stale++) {
		int stale_size = skb_frag_size(stale);

		sum += skb_frag_size(frag++);

		/* The stale fragment may present us with a smaller
		 * descriptor than the actual fragment size. To account
		 * for that we need to remove all the data on the front and
		 * figure out what the remainder would be in the last
		 * descriptor associated with the fragment.
		 */
		if (stale_size > IDPF_TX_MAX_DESC_DATA) {
			int align_pad = -(skb_frag_off(stale)) &
					(IDPF_TX_MAX_READ_REQ_SIZE - 1);

			sum -= align_pad;
			stale_size -= align_pad;

			do {
				sum -= IDPF_TX_MAX_DESC_DATA_ALIGNED;
				stale_size -= IDPF_TX_MAX_DESC_DATA_ALIGNED;
			} while (stale_size > IDPF_TX_MAX_DESC_DATA);
		}

		/* if sum is negative we failed to make sufficient progress */
		if (sum < 0)
			return true;

		if (!nr_frags--)
			break;

		sum -= stale_size;
	}

	return false;
}

/**
 * idpf_chk_linearize - Check if skb exceeds max descriptors per packet
 * @skb: send buffer
 * @max_bufs: maximum scatter gather buffers for single packet
 * @count: number of buffers this packet needs
 *
 * Make sure we don't exceed maximum scatter gather buffers for a single
 * packet. We have to do some special checking around the boundary (max_bufs-1)
 * if TSO is on since we need count the TSO header and payload separately.
 * E.g.: a packet with 7 fragments can require 9 DMA transactions; 1 for TSO
 * header, 1 for segment payload, and then 7 for the fragments.
 */
static bool idpf_chk_linearize(struct sk_buff *skb, unsigned int max_bufs,
			       unsigned int count)
{
	if (likely(count < max_bufs))
		return false;
	if (skb_is_gso(skb))
		return __idpf_chk_linearize(skb, max_bufs);

	return count > max_bufs;
}

/**
 * idpf_tx_splitq_get_ctx_desc - grab next desc and update buffer ring
 * @txq: queue to put context descriptor on
 *
 * Since the TX buffer rings mimics the descriptor ring, update the tx buffer
 * ring entry to reflect that this index is a context descriptor
 */
static struct idpf_flex_tx_ctx_desc *
idpf_tx_splitq_get_ctx_desc(struct idpf_queue *txq)
{
	struct idpf_flex_tx_ctx_desc *desc;
	int i = txq->next_to_use;

	memset(&txq->tx_buf[i], 0, sizeof(struct idpf_tx_buf));
	txq->tx_buf[i].compl_tag = IDPF_SPLITQ_TX_INVAL_COMPL_TAG;

	/* grab the next descriptor */
	desc = IDPF_FLEX_TX_CTX_DESC(txq, i);
	txq->next_to_use = idpf_tx_splitq_bump_ntu(txq, i);

	return desc;
}

/**
 * idpf_tx_drop_skb - free the SKB and bump tail if necessary
 * @tx_q: queue to send buffer on
 * @skb: pointer to skb
 */
static netdev_tx_t idpf_tx_drop_skb(struct idpf_queue *tx_q,
				    struct sk_buff *skb)
{
	u64_stats_update_begin(&tx_q->stats_sync);
	u64_stats_inc(&tx_q->q_stats.tx.skb_drops);
	u64_stats_update_end(&tx_q->stats_sync);

	idpf_tx_buf_hw_update(tx_q, tx_q->next_to_use, false);

	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

/**
 * idpf_tx_splitq_frame - Sends buffer on Tx ring using flex descriptors
 * @skb: send buffer
 * @tx_q: queue to send buffer on
 *
 * Returns NETDEV_TX_OK if sent, else an error code
 */
static netdev_tx_t idpf_tx_splitq_frame(struct sk_buff *skb,
					struct idpf_queue *tx_q)
{
	struct idpf_tx_splitq_params tx_params = { };
	struct idpf_tx_buf *first;
	unsigned int count;
	int tso;

	count = idpf_tx_desc_count_required(skb);
	if (idpf_chk_linearize(skb, tx_q->tx_max_bufs, count)) {
		if (__skb_linearize(skb))
			return idpf_tx_drop_skb(tx_q, skb);

		count = idpf_size_to_txd_count(skb->len);
		u64_stats_update_begin(&tx_q->stats_sync);
		u64_stats_inc(&tx_q->q_stats.tx.linearize);
		u64_stats_update_end(&tx_q->stats_sync);
	}

	tso = idpf_tso(skb, &tx_params.offload);
	if (unlikely(tso < 0))
		return idpf_tx_drop_skb(tx_q, skb);

	/* Check for splitq specific TX resources */
	count += (IDPF_TX_DESCS_PER_CACHE_LINE + tso);
	if (idpf_tx_maybe_stop_splitq(tx_q, count)) {
		idpf_tx_buf_hw_update(tx_q, tx_q->next_to_use, false);

		return NETDEV_TX_BUSY;
	}

	if (tso) {
		/* If tso is needed, set up context desc */
		struct idpf_flex_tx_ctx_desc *ctx_desc =
			idpf_tx_splitq_get_ctx_desc(tx_q);

		ctx_desc->tso.qw1.cmd_dtype =
				cpu_to_le16(IDPF_TX_DESC_DTYPE_FLEX_TSO_CTX |
					    IDPF_TX_FLEX_CTX_DESC_CMD_TSO);
		ctx_desc->tso.qw0.flex_tlen =
				cpu_to_le32(tx_params.offload.tso_len &
					    IDPF_TXD_FLEX_CTX_TLEN_M);
		ctx_desc->tso.qw0.mss_rt =
				cpu_to_le16(tx_params.offload.mss &
					    IDPF_TXD_FLEX_CTX_MSS_RT_M);
		ctx_desc->tso.qw0.hdr_len = tx_params.offload.tso_hdr_len;

		u64_stats_update_begin(&tx_q->stats_sync);
		u64_stats_inc(&tx_q->q_stats.tx.lso_pkts);
		u64_stats_update_end(&tx_q->stats_sync);
	}

	/* record the location of the first descriptor for this packet */
	first = &tx_q->tx_buf[tx_q->next_to_use];
	first->skb = skb;

	if (tso) {
		first->gso_segs = tx_params.offload.tso_segs;
		first->bytecount = skb->len +
			((first->gso_segs - 1) * tx_params.offload.tso_hdr_len);
	} else {
		first->gso_segs = 1;
		first->bytecount = max_t(unsigned int, skb->len, ETH_ZLEN);
	}

	if (test_bit(__IDPF_Q_FLOW_SCH_EN, tx_q->flags)) {
		tx_params.dtype = IDPF_TX_DESC_DTYPE_FLEX_FLOW_SCHE;
		tx_params.eop_cmd = IDPF_TXD_FLEX_FLOW_CMD_EOP;
		/* Set the RE bit to catch any packets that may have not been
		 * stashed during RS completion cleaning. MIN_GAP is set to
		 * MIN_RING size to ensure it will be set at least once each
		 * time around the ring.
		 */
		if (!(tx_q->next_to_use % IDPF_TX_SPLITQ_RE_MIN_GAP)) {
			tx_params.eop_cmd |= IDPF_TXD_FLEX_FLOW_CMD_RE;
			tx_q->txq_grp->num_completions_pending++;
		}

		if (skb->ip_summed == CHECKSUM_PARTIAL)
			tx_params.offload.td_cmd |= IDPF_TXD_FLEX_FLOW_CMD_CS_EN;

	} else {
		tx_params.dtype = IDPF_TX_DESC_DTYPE_FLEX_L2TAG1_L2TAG2;
		tx_params.eop_cmd = IDPF_TXD_LAST_DESC_CMD;

		if (skb->ip_summed == CHECKSUM_PARTIAL)
			tx_params.offload.td_cmd |= IDPF_TX_FLEX_DESC_CMD_CS_EN;
	}

	idpf_tx_splitq_map(tx_q, &tx_params, first);

	return NETDEV_TX_OK;
}

/**
 * idpf_tx_splitq_start - Selects the right Tx queue to send buffer
 * @skb: send buffer
 * @netdev: network interface device structure
 *
 * Returns NETDEV_TX_OK if sent, else an error code
 */
netdev_tx_t idpf_tx_splitq_start(struct sk_buff *skb,
				 struct net_device *netdev)
{
	struct idpf_vport *vport = idpf_netdev_to_vport(netdev);
	struct idpf_queue *tx_q;

	if (unlikely(skb_get_queue_mapping(skb) >= vport->num_txq)) {
		dev_kfree_skb_any(skb);

		return NETDEV_TX_OK;
	}

	tx_q = vport->txqs[skb_get_queue_mapping(skb)];

	/* hardware can't handle really short frames, hardware padding works
	 * beyond this point
	 */
	if (skb_put_padto(skb, tx_q->tx_min_pkt_len)) {
		idpf_tx_buf_hw_update(tx_q, tx_q->next_to_use, false);

		return NETDEV_TX_OK;
	}

	return idpf_tx_splitq_frame(skb, tx_q);
}

/**
 * idpf_vport_intr_clean_queues - MSIX mode Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a q_vector
 *
 */
static irqreturn_t idpf_vport_intr_clean_queues(int __always_unused irq,
						void *data)
{
	/* stub */
	return IRQ_HANDLED;
}

/**
 * idpf_vport_intr_napi_del_all - Unregister napi for all q_vectors in vport
 * @vport: virtual port structure
 *
 */
static void idpf_vport_intr_napi_del_all(struct idpf_vport *vport)
{
	u16 v_idx;

	for (v_idx = 0; v_idx < vport->num_q_vectors; v_idx++)
		netif_napi_del(&vport->q_vectors[v_idx].napi);
}

/**
 * idpf_vport_intr_napi_dis_all - Disable NAPI for all q_vectors in the vport
 * @vport: main vport structure
 */
static void idpf_vport_intr_napi_dis_all(struct idpf_vport *vport)
{
	int v_idx;

	for (v_idx = 0; v_idx < vport->num_q_vectors; v_idx++)
		napi_disable(&vport->q_vectors[v_idx].napi);
}

/**
 * idpf_vport_intr_rel - Free memory allocated for interrupt vectors
 * @vport: virtual port
 *
 * Free the memory allocated for interrupt vectors  associated to a vport
 */
void idpf_vport_intr_rel(struct idpf_vport *vport)
{
	int i, j, v_idx;

	for (v_idx = 0; v_idx < vport->num_q_vectors; v_idx++) {
		struct idpf_q_vector *q_vector = &vport->q_vectors[v_idx];

		kfree(q_vector->bufq);
		q_vector->bufq = NULL;
		kfree(q_vector->tx);
		q_vector->tx = NULL;
		kfree(q_vector->rx);
		q_vector->rx = NULL;
	}

	/* Clean up the mapping of queues to vectors */
	for (i = 0; i < vport->num_rxq_grp; i++) {
		struct idpf_rxq_group *rx_qgrp = &vport->rxq_grps[i];

		if (idpf_is_queue_model_split(vport->rxq_model))
			for (j = 0; j < rx_qgrp->splitq.num_rxq_sets; j++)
				rx_qgrp->splitq.rxq_sets[j]->rxq.q_vector = NULL;
		else
			for (j = 0; j < rx_qgrp->singleq.num_rxq; j++)
				rx_qgrp->singleq.rxqs[j]->q_vector = NULL;
	}

	if (idpf_is_queue_model_split(vport->txq_model))
		for (i = 0; i < vport->num_txq_grp; i++)
			vport->txq_grps[i].complq->q_vector = NULL;
	else
		for (i = 0; i < vport->num_txq_grp; i++)
			for (j = 0; j < vport->txq_grps[i].num_txq; j++)
				vport->txq_grps[i].txqs[j]->q_vector = NULL;

	kfree(vport->q_vectors);
	vport->q_vectors = NULL;
}

/**
 * idpf_vport_intr_rel_irq - Free the IRQ association with the OS
 * @vport: main vport structure
 */
static void idpf_vport_intr_rel_irq(struct idpf_vport *vport)
{
	struct idpf_adapter *adapter = vport->adapter;
	int vector;

	for (vector = 0; vector < vport->num_q_vectors; vector++) {
		struct idpf_q_vector *q_vector = &vport->q_vectors[vector];
		int irq_num, vidx;

		/* free only the irqs that were actually requested */
		if (!q_vector)
			continue;

		vidx = vport->q_vector_idxs[vector];
		irq_num = adapter->msix_entries[vidx].vector;

		/* clear the affinity_mask in the IRQ descriptor */
		irq_set_affinity_hint(irq_num, NULL);
		free_irq(irq_num, q_vector);
	}
}

/**
 * idpf_vport_intr_req_irq - get MSI-X vectors from the OS for the vport
 * @vport: main vport structure
 * @basename: name for the vector
 */
static int idpf_vport_intr_req_irq(struct idpf_vport *vport, char *basename)
{
	struct idpf_adapter *adapter = vport->adapter;
	int vector, err, irq_num, vidx;
	const char *vec_name;

	for (vector = 0; vector < vport->num_q_vectors; vector++) {
		struct idpf_q_vector *q_vector = &vport->q_vectors[vector];

		vidx = vport->q_vector_idxs[vector];
		irq_num = adapter->msix_entries[vidx].vector;

		if (q_vector->num_rxq && q_vector->num_txq)
			vec_name = "TxRx";
		else if (q_vector->num_rxq)
			vec_name = "Rx";
		else if (q_vector->num_txq)
			vec_name = "Tx";
		else
			continue;

		q_vector->name = kasprintf(GFP_KERNEL, "%s-%s-%d",
					   basename, vec_name, vidx);

		err = request_irq(irq_num, idpf_vport_intr_clean_queues, 0,
				  q_vector->name, q_vector);
		if (err) {
			netdev_err(vport->netdev,
				   "Request_irq failed, error: %d\n", err);
			goto free_q_irqs;
		}
		/* assign the mask for this irq */
		irq_set_affinity_hint(irq_num, &q_vector->affinity_mask);
	}

	return 0;

free_q_irqs:
	while (--vector >= 0) {
		vidx = vport->q_vector_idxs[vector];
		irq_num = adapter->msix_entries[vidx].vector;
		free_irq(irq_num, &vport->q_vectors[vector]);
	}

	return err;
}

/**
 * idpf_vport_intr_deinit - Release all vector associations for the vport
 * @vport: main vport structure
 */
void idpf_vport_intr_deinit(struct idpf_vport *vport)
{
	idpf_vport_intr_napi_dis_all(vport);
	idpf_vport_intr_napi_del_all(vport);
	idpf_vport_intr_rel_irq(vport);
}

/**
 * idpf_vport_intr_napi_ena_all - Enable NAPI for all q_vectors in the vport
 * @vport: main vport structure
 */
static void idpf_vport_intr_napi_ena_all(struct idpf_vport *vport)
{
	int q_idx;

	for (q_idx = 0; q_idx < vport->num_q_vectors; q_idx++) {
		struct idpf_q_vector *q_vector = &vport->q_vectors[q_idx];

		napi_enable(&q_vector->napi);
	}
}

/**
 * idpf_vport_splitq_napi_poll - NAPI handler
 * @napi: struct from which you get q_vector
 * @budget: budget provided by stack
 */
static int idpf_vport_splitq_napi_poll(struct napi_struct *napi, int budget)
{
	/* stub */
	return 0;
}

/**
 * idpf_vport_intr_map_vector_to_qs - Map vectors to queues
 * @vport: virtual port
 *
 * Mapping for vectors to queues
 */
static void idpf_vport_intr_map_vector_to_qs(struct idpf_vport *vport)
{
	u16 num_txq_grp = vport->num_txq_grp;
	int i, j, qv_idx, bufq_vidx = 0;
	struct idpf_rxq_group *rx_qgrp;
	struct idpf_txq_group *tx_qgrp;
	struct idpf_queue *q, *bufq;
	u16 q_index;

	for (i = 0, qv_idx = 0; i < vport->num_rxq_grp; i++) {
		u16 num_rxq;

		rx_qgrp = &vport->rxq_grps[i];
		if (idpf_is_queue_model_split(vport->rxq_model))
			num_rxq = rx_qgrp->splitq.num_rxq_sets;
		else
			num_rxq = rx_qgrp->singleq.num_rxq;

		for (j = 0; j < num_rxq; j++) {
			if (qv_idx >= vport->num_q_vectors)
				qv_idx = 0;

			if (idpf_is_queue_model_split(vport->rxq_model))
				q = &rx_qgrp->splitq.rxq_sets[j]->rxq;
			else
				q = rx_qgrp->singleq.rxqs[j];
			q->q_vector = &vport->q_vectors[qv_idx];
			q_index = q->q_vector->num_rxq;
			q->q_vector->rx[q_index] = q;
			q->q_vector->num_rxq++;
			qv_idx++;
		}

		if (idpf_is_queue_model_split(vport->rxq_model)) {
			for (j = 0; j < vport->num_bufqs_per_qgrp; j++) {
				bufq = &rx_qgrp->splitq.bufq_sets[j].bufq;
				bufq->q_vector = &vport->q_vectors[bufq_vidx];
				q_index = bufq->q_vector->num_bufq;
				bufq->q_vector->bufq[q_index] = bufq;
				bufq->q_vector->num_bufq++;
			}
			if (++bufq_vidx >= vport->num_q_vectors)
				bufq_vidx = 0;
		}
	}

	for (i = 0, qv_idx = 0; i < num_txq_grp; i++) {
		u16 num_txq;

		tx_qgrp = &vport->txq_grps[i];
		num_txq = tx_qgrp->num_txq;

		if (idpf_is_queue_model_split(vport->txq_model)) {
			if (qv_idx >= vport->num_q_vectors)
				qv_idx = 0;

			q = tx_qgrp->complq;
			q->q_vector = &vport->q_vectors[qv_idx];
			q_index = q->q_vector->num_txq;
			q->q_vector->tx[q_index] = q;
			q->q_vector->num_txq++;
			qv_idx++;
		} else {
			for (j = 0; j < num_txq; j++) {
				if (qv_idx >= vport->num_q_vectors)
					qv_idx = 0;

				q = tx_qgrp->txqs[j];
				q->q_vector = &vport->q_vectors[qv_idx];
				q_index = q->q_vector->num_txq;
				q->q_vector->tx[q_index] = q;
				q->q_vector->num_txq++;

				qv_idx++;
			}
		}
	}
}

/**
 * idpf_vport_intr_init_vec_idx - Initialize the vector indexes
 * @vport: virtual port
 *
 * Initialize vector indexes with values returened over mailbox
 */
static int idpf_vport_intr_init_vec_idx(struct idpf_vport *vport)
{
	struct idpf_adapter *adapter = vport->adapter;
	struct virtchnl2_alloc_vectors *ac;
	u16 *vecids, total_vecs;
	int i;

	ac = adapter->req_vec_chunks;
	if (!ac) {
		for (i = 0; i < vport->num_q_vectors; i++)
			vport->q_vectors[i].v_idx = vport->q_vector_idxs[i];

		return 0;
	}

	total_vecs = idpf_get_reserved_vecs(adapter);
	vecids = kcalloc(total_vecs, sizeof(u16), GFP_KERNEL);
	if (!vecids)
		return -ENOMEM;

	idpf_get_vec_ids(adapter, vecids, total_vecs, &ac->vchunks);

	for (i = 0; i < vport->num_q_vectors; i++)
		vport->q_vectors[i].v_idx = vecids[vport->q_vector_idxs[i]];

	kfree(vecids);

	return 0;
}

/**
 * idpf_vport_intr_napi_add_all- Register napi handler for all qvectors
 * @vport: virtual port structure
 */
static void idpf_vport_intr_napi_add_all(struct idpf_vport *vport)
{
	int (*napi_poll)(struct napi_struct *napi, int budget);
	u16 v_idx;

	if (idpf_is_queue_model_split(vport->txq_model))
		napi_poll = idpf_vport_splitq_napi_poll;
	else
		napi_poll = idpf_vport_singleq_napi_poll;

	for (v_idx = 0; v_idx < vport->num_q_vectors; v_idx++) {
		struct idpf_q_vector *q_vector = &vport->q_vectors[v_idx];

		netif_napi_add(vport->netdev, &q_vector->napi, napi_poll);

		/* only set affinity_mask if the CPU is online */
		if (cpu_online(v_idx))
			cpumask_set_cpu(v_idx, &q_vector->affinity_mask);
	}
}

/**
 * idpf_vport_intr_alloc - Allocate memory for interrupt vectors
 * @vport: virtual port
 *
 * We allocate one q_vector per queue interrupt. If allocation fails we
 * return -ENOMEM.
 */
int idpf_vport_intr_alloc(struct idpf_vport *vport)
{
	u16 txqs_per_vector, rxqs_per_vector, bufqs_per_vector;
	struct idpf_q_vector *q_vector;
	int v_idx, err;

	vport->q_vectors = kcalloc(vport->num_q_vectors,
				   sizeof(struct idpf_q_vector), GFP_KERNEL);
	if (!vport->q_vectors)
		return -ENOMEM;

	txqs_per_vector = DIV_ROUND_UP(vport->num_txq, vport->num_q_vectors);
	rxqs_per_vector = DIV_ROUND_UP(vport->num_rxq, vport->num_q_vectors);
	bufqs_per_vector = vport->num_bufqs_per_qgrp *
			   DIV_ROUND_UP(vport->num_rxq_grp,
					vport->num_q_vectors);

	for (v_idx = 0; v_idx < vport->num_q_vectors; v_idx++) {
		q_vector = &vport->q_vectors[v_idx];
		q_vector->vport = vport;

		q_vector->tx_itr_value = IDPF_ITR_TX_DEF;
		q_vector->tx_intr_mode = IDPF_ITR_DYNAMIC;
		q_vector->tx_itr_idx = VIRTCHNL2_ITR_IDX_1;

		q_vector->rx_itr_value = IDPF_ITR_RX_DEF;
		q_vector->rx_intr_mode = IDPF_ITR_DYNAMIC;
		q_vector->rx_itr_idx = VIRTCHNL2_ITR_IDX_0;

		q_vector->tx = kcalloc(txqs_per_vector,
				       sizeof(struct idpf_queue *),
				       GFP_KERNEL);
		if (!q_vector->tx) {
			err = -ENOMEM;
			goto error;
		}

		q_vector->rx = kcalloc(rxqs_per_vector,
				       sizeof(struct idpf_queue *),
				       GFP_KERNEL);
		if (!q_vector->rx) {
			err = -ENOMEM;
			goto error;
		}

		if (!idpf_is_queue_model_split(vport->rxq_model))
			continue;

		q_vector->bufq = kcalloc(bufqs_per_vector,
					 sizeof(struct idpf_queue *),
					 GFP_KERNEL);
		if (!q_vector->bufq) {
			err = -ENOMEM;
			goto error;
		}
	}

	return 0;

error:
	idpf_vport_intr_rel(vport);

	return err;
}

/**
 * idpf_vport_intr_init - Setup all vectors for the given vport
 * @vport: virtual port
 *
 * Returns 0 on success or negative on failure
 */
int idpf_vport_intr_init(struct idpf_vport *vport)
{
	char *int_name;
	int err;

	err = idpf_vport_intr_init_vec_idx(vport);
	if (err)
		return err;

	idpf_vport_intr_map_vector_to_qs(vport);
	idpf_vport_intr_napi_add_all(vport);
	idpf_vport_intr_napi_ena_all(vport);

	err = vport->adapter->dev_ops.reg_ops.intr_reg_init(vport);
	if (err)
		goto unroll_vectors_alloc;

	int_name = kasprintf(GFP_KERNEL, "%s-%s",
			     dev_driver_string(&vport->adapter->pdev->dev),
			     vport->netdev->name);

	err = idpf_vport_intr_req_irq(vport, int_name);
	if (err)
		goto unroll_vectors_alloc;

	return 0;

unroll_vectors_alloc:
	idpf_vport_intr_napi_dis_all(vport);
	idpf_vport_intr_napi_del_all(vport);

	return err;
}

/**
 * idpf_config_rss - Send virtchnl messages to configure RSS
 * @vport: virtual port
 *
 * Return 0 on success, negative on failure
 */
int idpf_config_rss(struct idpf_vport *vport)
{
	int err;

	err = idpf_send_get_set_rss_key_msg(vport, false);
	if (err)
		return err;

	return idpf_send_get_set_rss_lut_msg(vport, false);
}

/**
 * idpf_fill_dflt_rss_lut - Fill the indirection table with the default values
 * @vport: virtual port structure
 */
static void idpf_fill_dflt_rss_lut(struct idpf_vport *vport)
{
	struct idpf_adapter *adapter = vport->adapter;
	u16 num_active_rxq = vport->num_rxq;
	struct idpf_rss_data *rss_data;
	int i;

	rss_data = &adapter->vport_config[vport->idx]->user_config.rss_data;

	for (i = 0; i < rss_data->rss_lut_size; i++) {
		rss_data->rss_lut[i] = i % num_active_rxq;
		rss_data->cached_lut[i] = rss_data->rss_lut[i];
	}
}

/**
 * idpf_init_rss - Allocate and initialize RSS resources
 * @vport: virtual port
 *
 * Return 0 on success, negative on failure
 */
int idpf_init_rss(struct idpf_vport *vport)
{
	struct idpf_adapter *adapter = vport->adapter;
	struct idpf_rss_data *rss_data;
	u32 lut_size;

	rss_data = &adapter->vport_config[vport->idx]->user_config.rss_data;

	lut_size = rss_data->rss_lut_size * sizeof(u32);
	rss_data->rss_lut = kzalloc(lut_size, GFP_KERNEL);
	if (!rss_data->rss_lut)
		return -ENOMEM;

	rss_data->cached_lut = kzalloc(lut_size, GFP_KERNEL);
	if (!rss_data->cached_lut) {
		kfree(rss_data->rss_lut);
		rss_data->rss_lut = NULL;

		return -ENOMEM;
	}

	/* Fill the default RSS lut values */
	idpf_fill_dflt_rss_lut(vport);

	return idpf_config_rss(vport);
}

/**
 * idpf_deinit_rss - Release RSS resources
 * @vport: virtual port
 *
 */
void idpf_deinit_rss(struct idpf_vport *vport)
{
	struct idpf_adapter *adapter = vport->adapter;
	struct idpf_rss_data *rss_data;

	rss_data = &adapter->vport_config[vport->idx]->user_config.rss_data;
	kfree(rss_data->cached_lut);
	rss_data->cached_lut = NULL;
	kfree(rss_data->rss_lut);
	rss_data->rss_lut = NULL;
}
