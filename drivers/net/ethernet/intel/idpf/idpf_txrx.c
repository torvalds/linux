// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023 Intel Corporation */

#include "idpf.h"
#include "idpf_ptp.h"
#include "idpf_virtchnl.h"
#include "xdp.h"
#include "xsk.h"

#define idpf_tx_buf_next(buf)		(*(u32 *)&(buf)->priv)
LIBETH_SQE_CHECK_PRIV(u32);

/**
 * idpf_chk_linearize - Check if skb exceeds max descriptors per packet
 * @skb: send buffer
 * @max_bufs: maximum scatter gather buffers for single packet
 * @count: number of buffers this packet needs
 *
 * Make sure we don't exceed maximum scatter gather buffers for a single
 * packet.
 * TSO case has been handled earlier from idpf_features_check().
 */
static bool idpf_chk_linearize(const struct sk_buff *skb,
			       unsigned int max_bufs,
			       unsigned int count)
{
	if (likely(count <= max_bufs))
		return false;

	if (skb_is_gso(skb))
		return false;

	return true;
}

/**
 * idpf_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 * @txqueue: TX queue
 */
void idpf_tx_timeout(struct net_device *netdev, unsigned int txqueue)
{
	struct idpf_adapter *adapter = idpf_netdev_to_adapter(netdev);

	adapter->tx_timeout_count++;

	netdev_err(netdev, "Detected Tx timeout: Count %d, Queue %d\n",
		   adapter->tx_timeout_count, txqueue);
	if (!idpf_is_reset_in_prog(adapter)) {
		set_bit(IDPF_HR_FUNC_RESET, adapter->flags);
		queue_delayed_work(adapter->vc_event_wq,
				   &adapter->vc_event_task,
				   msecs_to_jiffies(10));
	}
}

static void idpf_tx_buf_clean(struct idpf_tx_queue *txq)
{
	struct libeth_sq_napi_stats ss = { };
	struct xdp_frame_bulk bq;
	struct libeth_cq_pp cp = {
		.dev	= txq->dev,
		.bq	= &bq,
		.ss	= &ss,
	};

	xdp_frame_bulk_init(&bq);

	/* Free all the Tx buffer sk_buffs */
	for (u32 i = 0; i < txq->buf_pool_size; i++)
		libeth_tx_complete_any(&txq->tx_buf[i], &cp);

	xdp_flush_frame_bulk(&bq);
}

/**
 * idpf_tx_buf_rel_all - Free any empty Tx buffers
 * @txq: queue to be cleaned
 */
static void idpf_tx_buf_rel_all(struct idpf_tx_queue *txq)
{
	/* Buffers already cleared, nothing to do */
	if (!txq->tx_buf)
		return;

	if (idpf_queue_has(XSK, txq))
		idpf_xsksq_clean(txq);
	else
		idpf_tx_buf_clean(txq);

	kfree(txq->tx_buf);
	txq->tx_buf = NULL;
}

/**
 * idpf_tx_desc_rel - Free Tx resources per queue
 * @txq: Tx descriptor ring for a specific queue
 *
 * Free all transmit software resources
 */
static void idpf_tx_desc_rel(struct idpf_tx_queue *txq)
{
	bool xdp = idpf_queue_has(XDP, txq);

	if (xdp)
		libeth_xdpsq_deinit_timer(txq->timer);

	idpf_tx_buf_rel_all(txq);

	if (!xdp)
		netdev_tx_reset_subqueue(txq->netdev, txq->idx);

	idpf_xsk_clear_queue(txq, VIRTCHNL2_QUEUE_TYPE_TX);

	if (!txq->desc_ring)
		return;

	if (!xdp && txq->refillq)
		kfree(txq->refillq->ring);

	dmam_free_coherent(txq->dev, txq->size, txq->desc_ring, txq->dma);
	txq->desc_ring = NULL;
	txq->next_to_use = 0;
	txq->next_to_clean = 0;
}

/**
 * idpf_compl_desc_rel - Free completion resources per queue
 * @complq: completion queue
 *
 * Free all completion software resources.
 */
static void idpf_compl_desc_rel(struct idpf_compl_queue *complq)
{
	idpf_xsk_clear_queue(complq, VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION);

	if (!complq->comp)
		return;

	dma_free_coherent(complq->netdev->dev.parent, complq->size,
			  complq->desc_ring, complq->dma);
	complq->desc_ring = NULL;
	complq->next_to_use = 0;
	complq->next_to_clean = 0;
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
			idpf_tx_desc_rel(txq_grp->txqs[j]);

		if (idpf_is_queue_model_split(vport->txq_model))
			idpf_compl_desc_rel(txq_grp->complq);
	}
}

/**
 * idpf_tx_buf_alloc_all - Allocate memory for all buffer resources
 * @tx_q: queue for which the buffers are allocated
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_tx_buf_alloc_all(struct idpf_tx_queue *tx_q)
{
	/* Allocate book keeping buffers only. Buffers to be supplied to HW
	 * are allocated by kernel network stack and received as part of skb
	 */
	if (idpf_queue_has(FLOW_SCH_EN, tx_q))
		tx_q->buf_pool_size = U16_MAX;
	else
		tx_q->buf_pool_size = tx_q->desc_count;
	tx_q->tx_buf = kcalloc(tx_q->buf_pool_size, sizeof(*tx_q->tx_buf),
			       GFP_KERNEL);
	if (!tx_q->tx_buf)
		return -ENOMEM;

	return 0;
}

/**
 * idpf_tx_desc_alloc - Allocate the Tx descriptors
 * @vport: vport to allocate resources for
 * @tx_q: the tx ring to set up
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_tx_desc_alloc(const struct idpf_vport *vport,
			      struct idpf_tx_queue *tx_q)
{
	struct device *dev = tx_q->dev;
	struct idpf_sw_queue *refillq;
	int err;

	err = idpf_tx_buf_alloc_all(tx_q);
	if (err)
		goto err_alloc;

	tx_q->size = tx_q->desc_count * sizeof(*tx_q->base_tx);

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

	tx_q->next_to_use = 0;
	tx_q->next_to_clean = 0;
	idpf_queue_set(GEN_CHK, tx_q);

	idpf_xsk_setup_queue(vport, tx_q, VIRTCHNL2_QUEUE_TYPE_TX);

	if (!idpf_queue_has(FLOW_SCH_EN, tx_q))
		return 0;

	refillq = tx_q->refillq;
	refillq->desc_count = tx_q->buf_pool_size;
	refillq->ring = kcalloc(refillq->desc_count, sizeof(u32),
				GFP_KERNEL);
	if (!refillq->ring) {
		err = -ENOMEM;
		goto err_alloc;
	}

	for (unsigned int i = 0; i < refillq->desc_count; i++)
		refillq->ring[i] =
			FIELD_PREP(IDPF_RFL_BI_BUFID_M, i) |
			FIELD_PREP(IDPF_RFL_BI_GEN_M,
				   idpf_queue_has(GEN_CHK, refillq));

	/* Go ahead and flip the GEN bit since this counts as filling
	 * up the ring, i.e. we already ring wrapped.
	 */
	idpf_queue_change(GEN_CHK, refillq);

	tx_q->last_re = tx_q->desc_count - IDPF_TX_SPLITQ_RE_MIN_GAP;

	return 0;

err_alloc:
	idpf_tx_desc_rel(tx_q);

	return err;
}

/**
 * idpf_compl_desc_alloc - allocate completion descriptors
 * @vport: vport to allocate resources for
 * @complq: completion queue to set up
 *
 * Return: 0 on success, -errno on failure.
 */
static int idpf_compl_desc_alloc(const struct idpf_vport *vport,
				 struct idpf_compl_queue *complq)
{
	u32 desc_size;

	desc_size = idpf_queue_has(FLOW_SCH_EN, complq) ?
		    sizeof(*complq->comp) : sizeof(*complq->comp_4b);
	complq->size = array_size(complq->desc_count, desc_size);

	complq->desc_ring = dma_alloc_coherent(complq->netdev->dev.parent,
					       complq->size, &complq->dma,
					       GFP_KERNEL);
	if (!complq->desc_ring)
		return -ENOMEM;

	complq->next_to_use = 0;
	complq->next_to_clean = 0;
	idpf_queue_set(GEN_CHK, complq);

	idpf_xsk_setup_queue(vport, complq,
			     VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION);

	return 0;
}

/**
 * idpf_tx_desc_alloc_all - allocate all queues Tx resources
 * @vport: virtual port private structure
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_tx_desc_alloc_all(struct idpf_vport *vport)
{
	int err = 0;
	int i, j;

	/* Setup buffer queues. In single queue model buffer queues and
	 * completion queues will be same
	 */
	for (i = 0; i < vport->num_txq_grp; i++) {
		for (j = 0; j < vport->txq_grps[i].num_txq; j++) {
			struct idpf_tx_queue *txq = vport->txq_grps[i].txqs[j];

			err = idpf_tx_desc_alloc(vport, txq);
			if (err) {
				pci_err(vport->adapter->pdev,
					"Allocation for Tx Queue %u failed\n",
					i);
				goto err_out;
			}
		}

		if (!idpf_is_queue_model_split(vport->txq_model))
			continue;

		/* Setup completion queues */
		err = idpf_compl_desc_alloc(vport, vport->txq_grps[i].complq);
		if (err) {
			pci_err(vport->adapter->pdev,
				"Allocation for Tx Completion Queue %u failed\n",
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
 * @rx_buf: the buffer to free
 */
static void idpf_rx_page_rel(struct libeth_fqe *rx_buf)
{
	if (unlikely(!rx_buf->netmem))
		return;

	libeth_rx_recycle_slow(rx_buf->netmem);

	rx_buf->netmem = 0;
	rx_buf->offset = 0;
}

/**
 * idpf_rx_hdr_buf_rel_all - Release header buffer memory
 * @bufq: queue to use
 */
static void idpf_rx_hdr_buf_rel_all(struct idpf_buf_queue *bufq)
{
	struct libeth_fq fq = {
		.fqes	= bufq->hdr_buf,
		.pp	= bufq->hdr_pp,
	};

	for (u32 i = 0; i < bufq->desc_count; i++)
		idpf_rx_page_rel(&bufq->hdr_buf[i]);

	libeth_rx_fq_destroy(&fq);
	bufq->hdr_buf = NULL;
	bufq->hdr_pp = NULL;
}

/**
 * idpf_rx_buf_rel_bufq - Free all Rx buffer resources for a buffer queue
 * @bufq: queue to be cleaned
 */
static void idpf_rx_buf_rel_bufq(struct idpf_buf_queue *bufq)
{
	struct libeth_fq fq = {
		.fqes	= bufq->buf,
		.pp	= bufq->pp,
	};

	/* queue already cleared, nothing to do */
	if (!bufq->buf)
		return;

	if (idpf_queue_has(XSK, bufq)) {
		idpf_xskfq_rel(bufq);
		return;
	}

	/* Free all the bufs allocated and given to hw on Rx queue */
	for (u32 i = 0; i < bufq->desc_count; i++)
		idpf_rx_page_rel(&bufq->buf[i]);

	if (idpf_queue_has(HSPLIT_EN, bufq))
		idpf_rx_hdr_buf_rel_all(bufq);

	libeth_rx_fq_destroy(&fq);
	bufq->buf = NULL;
	bufq->pp = NULL;
}

/**
 * idpf_rx_buf_rel_all - Free all Rx buffer resources for a receive queue
 * @rxq: queue to be cleaned
 */
static void idpf_rx_buf_rel_all(struct idpf_rx_queue *rxq)
{
	struct libeth_fq fq = {
		.fqes	= rxq->rx_buf,
		.pp	= rxq->pp,
	};

	if (!rxq->rx_buf)
		return;

	for (u32 i = 0; i < rxq->desc_count; i++)
		idpf_rx_page_rel(&rxq->rx_buf[i]);

	libeth_rx_fq_destroy(&fq);
	rxq->rx_buf = NULL;
	rxq->pp = NULL;
}

/**
 * idpf_rx_desc_rel - Free a specific Rx q resources
 * @rxq: queue to clean the resources from
 * @dev: device to free DMA memory
 * @model: single or split queue model
 *
 * Free a specific rx queue resources
 */
static void idpf_rx_desc_rel(struct idpf_rx_queue *rxq, struct device *dev,
			     u32 model)
{
	if (!rxq)
		return;

	if (!idpf_queue_has(XSK, rxq))
		libeth_xdp_return_stash(&rxq->xdp);

	if (!idpf_is_queue_model_split(model))
		idpf_rx_buf_rel_all(rxq);

	idpf_xsk_clear_queue(rxq, VIRTCHNL2_QUEUE_TYPE_RX);

	rxq->next_to_alloc = 0;
	rxq->next_to_clean = 0;
	rxq->next_to_use = 0;
	if (!rxq->desc_ring)
		return;

	dmam_free_coherent(dev, rxq->size, rxq->desc_ring, rxq->dma);
	rxq->desc_ring = NULL;
}

/**
 * idpf_rx_desc_rel_bufq - free buffer queue resources
 * @bufq: buffer queue to clean the resources from
 * @dev: device to free DMA memory
 */
static void idpf_rx_desc_rel_bufq(struct idpf_buf_queue *bufq,
				  struct device *dev)
{
	if (!bufq)
		return;

	idpf_rx_buf_rel_bufq(bufq);
	idpf_xsk_clear_queue(bufq, VIRTCHNL2_QUEUE_TYPE_RX_BUFFER);

	bufq->next_to_alloc = 0;
	bufq->next_to_clean = 0;
	bufq->next_to_use = 0;

	if (!bufq->split_buf)
		return;

	dma_free_coherent(dev, bufq->size, bufq->split_buf, bufq->dma);
	bufq->split_buf = NULL;
}

/**
 * idpf_rx_desc_rel_all - Free Rx Resources for All Queues
 * @vport: virtual port structure
 *
 * Free all rx queues resources
 */
static void idpf_rx_desc_rel_all(struct idpf_vport *vport)
{
	struct device *dev = &vport->adapter->pdev->dev;
	struct idpf_rxq_group *rx_qgrp;
	u16 num_rxq;
	int i, j;

	if (!vport->rxq_grps)
		return;

	for (i = 0; i < vport->num_rxq_grp; i++) {
		rx_qgrp = &vport->rxq_grps[i];

		if (!idpf_is_queue_model_split(vport->rxq_model)) {
			for (j = 0; j < rx_qgrp->singleq.num_rxq; j++)
				idpf_rx_desc_rel(rx_qgrp->singleq.rxqs[j], dev,
						 VIRTCHNL2_QUEUE_MODEL_SINGLE);
			continue;
		}

		num_rxq = rx_qgrp->splitq.num_rxq_sets;
		for (j = 0; j < num_rxq; j++)
			idpf_rx_desc_rel(&rx_qgrp->splitq.rxq_sets[j]->rxq,
					 dev, VIRTCHNL2_QUEUE_MODEL_SPLIT);

		if (!rx_qgrp->splitq.bufq_sets)
			continue;

		for (j = 0; j < vport->num_bufqs_per_qgrp; j++) {
			struct idpf_bufq_set *bufq_set =
				&rx_qgrp->splitq.bufq_sets[j];

			idpf_rx_desc_rel_bufq(&bufq_set->bufq, dev);
		}
	}
}

/**
 * idpf_rx_buf_hw_update - Store the new tail and head values
 * @bufq: queue to bump
 * @val: new head index
 */
static void idpf_rx_buf_hw_update(struct idpf_buf_queue *bufq, u32 val)
{
	bufq->next_to_use = val;

	if (unlikely(!bufq->tail))
		return;

	/* writel has an implicit memory barrier */
	writel(val, bufq->tail);
}

/**
 * idpf_rx_hdr_buf_alloc_all - Allocate memory for header buffers
 * @bufq: ring to use
 *
 * Returns 0 on success, negative on failure.
 */
static int idpf_rx_hdr_buf_alloc_all(struct idpf_buf_queue *bufq)
{
	struct libeth_fq fq = {
		.count	= bufq->desc_count,
		.type	= LIBETH_FQE_HDR,
		.xdp	= idpf_xdp_enabled(bufq->q_vector->vport),
		.nid	= idpf_q_vector_to_mem(bufq->q_vector),
	};
	int ret;

	ret = libeth_rx_fq_create(&fq, &bufq->q_vector->napi);
	if (ret)
		return ret;

	bufq->hdr_pp = fq.pp;
	bufq->hdr_buf = fq.fqes;
	bufq->hdr_truesize = fq.truesize;
	bufq->rx_hbuf_size = fq.buf_len;

	return 0;
}

/**
 * idpf_post_buf_refill - Post buffer id to refill queue
 * @refillq: refill queue to post to
 * @buf_id: buffer id to post
 */
static void idpf_post_buf_refill(struct idpf_sw_queue *refillq, u16 buf_id)
{
	u32 nta = refillq->next_to_use;

	/* store the buffer ID and the SW maintained GEN bit to the refillq */
	refillq->ring[nta] =
		FIELD_PREP(IDPF_RFL_BI_BUFID_M, buf_id) |
		FIELD_PREP(IDPF_RFL_BI_GEN_M,
			   idpf_queue_has(GEN_CHK, refillq));

	if (unlikely(++nta == refillq->desc_count)) {
		nta = 0;
		idpf_queue_change(GEN_CHK, refillq);
	}

	refillq->next_to_use = nta;
}

/**
 * idpf_rx_post_buf_desc - Post buffer to bufq descriptor ring
 * @bufq: buffer queue to post to
 * @buf_id: buffer id to post
 *
 * Returns false if buffer could not be allocated, true otherwise.
 */
static bool idpf_rx_post_buf_desc(struct idpf_buf_queue *bufq, u16 buf_id)
{
	struct virtchnl2_splitq_rx_buf_desc *splitq_rx_desc = NULL;
	struct libeth_fq_fp fq = {
		.count	= bufq->desc_count,
	};
	u16 nta = bufq->next_to_alloc;
	dma_addr_t addr;

	splitq_rx_desc = &bufq->split_buf[nta];

	if (idpf_queue_has(HSPLIT_EN, bufq)) {
		fq.pp = bufq->hdr_pp;
		fq.fqes = bufq->hdr_buf;
		fq.truesize = bufq->hdr_truesize;

		addr = libeth_rx_alloc(&fq, buf_id);
		if (addr == DMA_MAPPING_ERROR)
			return false;

		splitq_rx_desc->hdr_addr = cpu_to_le64(addr);
	}

	fq.pp = bufq->pp;
	fq.fqes = bufq->buf;
	fq.truesize = bufq->truesize;

	addr = libeth_rx_alloc(&fq, buf_id);
	if (addr == DMA_MAPPING_ERROR)
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
static bool idpf_rx_post_init_bufs(struct idpf_buf_queue *bufq,
				   u16 working_set)
{
	int i;

	for (i = 0; i < working_set; i++) {
		if (!idpf_rx_post_buf_desc(bufq, i))
			return false;
	}

	idpf_rx_buf_hw_update(bufq, ALIGN_DOWN(bufq->next_to_alloc,
					       IDPF_RX_BUF_STRIDE));

	return true;
}

/**
 * idpf_rx_buf_alloc_singleq - Allocate memory for all buffer resources
 * @rxq: queue for which the buffers are allocated
 *
 * Return: 0 on success, -ENOMEM on failure.
 */
static int idpf_rx_buf_alloc_singleq(struct idpf_rx_queue *rxq)
{
	if (idpf_rx_singleq_buf_hw_alloc_all(rxq, rxq->desc_count - 1))
		goto err;

	return 0;

err:
	idpf_rx_buf_rel_all(rxq);

	return -ENOMEM;
}

/**
 * idpf_rx_bufs_init_singleq - Initialize page pool and allocate Rx bufs
 * @rxq: buffer queue to create page pool for
 *
 * Return: 0 on success, -errno on failure.
 */
static int idpf_rx_bufs_init_singleq(struct idpf_rx_queue *rxq)
{
	struct libeth_fq fq = {
		.count	= rxq->desc_count,
		.type	= LIBETH_FQE_MTU,
		.nid	= idpf_q_vector_to_mem(rxq->q_vector),
	};
	int ret;

	ret = libeth_rx_fq_create(&fq, &rxq->q_vector->napi);
	if (ret)
		return ret;

	rxq->pp = fq.pp;
	rxq->rx_buf = fq.fqes;
	rxq->truesize = fq.truesize;
	rxq->rx_buf_size = fq.buf_len;

	return idpf_rx_buf_alloc_singleq(rxq);
}

/**
 * idpf_rx_buf_alloc_all - Allocate memory for all buffer resources
 * @rxbufq: queue for which the buffers are allocated
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_rx_buf_alloc_all(struct idpf_buf_queue *rxbufq)
{
	int err = 0;

	if (idpf_queue_has(HSPLIT_EN, rxbufq)) {
		err = idpf_rx_hdr_buf_alloc_all(rxbufq);
		if (err)
			goto rx_buf_alloc_all_out;
	}

	/* Allocate buffers to be given to HW.	 */
	if (!idpf_rx_post_init_bufs(rxbufq, IDPF_RX_BUFQ_WORKING_SET(rxbufq)))
		err = -ENOMEM;

rx_buf_alloc_all_out:
	if (err)
		idpf_rx_buf_rel_bufq(rxbufq);

	return err;
}

/**
 * idpf_rx_bufs_init - Initialize page pool, allocate rx bufs, and post to HW
 * @bufq: buffer queue to create page pool for
 * @type: type of Rx buffers to allocate
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_rx_bufs_init(struct idpf_buf_queue *bufq,
			     enum libeth_fqe_type type)
{
	struct libeth_fq fq = {
		.truesize	= bufq->truesize,
		.count		= bufq->desc_count,
		.type		= type,
		.hsplit		= idpf_queue_has(HSPLIT_EN, bufq),
		.xdp		= idpf_xdp_enabled(bufq->q_vector->vport),
		.nid		= idpf_q_vector_to_mem(bufq->q_vector),
	};
	int ret;

	if (idpf_queue_has(XSK, bufq))
		return idpf_xskfq_init(bufq);

	ret = libeth_rx_fq_create(&fq, &bufq->q_vector->napi);
	if (ret)
		return ret;

	bufq->pp = fq.pp;
	bufq->buf = fq.fqes;
	bufq->truesize = fq.truesize;
	bufq->rx_buf_size = fq.buf_len;

	return idpf_rx_buf_alloc_all(bufq);
}

/**
 * idpf_rx_bufs_init_all - Initialize all RX bufs
 * @vport: virtual port struct
 *
 * Returns 0 on success, negative on failure
 */
int idpf_rx_bufs_init_all(struct idpf_vport *vport)
{
	bool split = idpf_is_queue_model_split(vport->rxq_model);
	int i, j, err;

	idpf_xdp_copy_prog_to_rqs(vport, vport->xdp_prog);

	for (i = 0; i < vport->num_rxq_grp; i++) {
		struct idpf_rxq_group *rx_qgrp = &vport->rxq_grps[i];
		u32 truesize = 0;

		/* Allocate bufs for the rxq itself in singleq */
		if (!split) {
			int num_rxq = rx_qgrp->singleq.num_rxq;

			for (j = 0; j < num_rxq; j++) {
				struct idpf_rx_queue *q;

				q = rx_qgrp->singleq.rxqs[j];
				err = idpf_rx_bufs_init_singleq(q);
				if (err)
					return err;
			}

			continue;
		}

		/* Otherwise, allocate bufs for the buffer queues */
		for (j = 0; j < vport->num_bufqs_per_qgrp; j++) {
			enum libeth_fqe_type type;
			struct idpf_buf_queue *q;

			q = &rx_qgrp->splitq.bufq_sets[j].bufq;
			q->truesize = truesize;

			type = truesize ? LIBETH_FQE_SHORT : LIBETH_FQE_MTU;

			err = idpf_rx_bufs_init(q, type);
			if (err)
				return err;

			truesize = q->truesize >> 1;
		}
	}

	return 0;
}

/**
 * idpf_rx_desc_alloc - Allocate queue Rx resources
 * @vport: vport to allocate resources for
 * @rxq: Rx queue for which the resources are setup
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_rx_desc_alloc(const struct idpf_vport *vport,
			      struct idpf_rx_queue *rxq)
{
	struct device *dev = &vport->adapter->pdev->dev;

	rxq->size = rxq->desc_count * sizeof(union virtchnl2_rx_desc);

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
	idpf_queue_set(GEN_CHK, rxq);

	idpf_xsk_setup_queue(vport, rxq, VIRTCHNL2_QUEUE_TYPE_RX);

	return 0;
}

/**
 * idpf_bufq_desc_alloc - Allocate buffer queue descriptor ring
 * @vport: vport to allocate resources for
 * @bufq: buffer queue for which the resources are set up
 *
 * Return: 0 on success, -ENOMEM on failure.
 */
static int idpf_bufq_desc_alloc(const struct idpf_vport *vport,
				struct idpf_buf_queue *bufq)
{
	struct device *dev = &vport->adapter->pdev->dev;

	bufq->size = array_size(bufq->desc_count, sizeof(*bufq->split_buf));

	bufq->split_buf = dma_alloc_coherent(dev, bufq->size, &bufq->dma,
					     GFP_KERNEL);
	if (!bufq->split_buf)
		return -ENOMEM;

	bufq->next_to_alloc = 0;
	bufq->next_to_clean = 0;
	bufq->next_to_use = 0;
	idpf_queue_set(GEN_CHK, bufq);

	idpf_xsk_setup_queue(vport, bufq, VIRTCHNL2_QUEUE_TYPE_RX_BUFFER);

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
	struct idpf_rxq_group *rx_qgrp;
	int i, j, err;
	u16 num_rxq;

	for (i = 0; i < vport->num_rxq_grp; i++) {
		rx_qgrp = &vport->rxq_grps[i];
		if (idpf_is_queue_model_split(vport->rxq_model))
			num_rxq = rx_qgrp->splitq.num_rxq_sets;
		else
			num_rxq = rx_qgrp->singleq.num_rxq;

		for (j = 0; j < num_rxq; j++) {
			struct idpf_rx_queue *q;

			if (idpf_is_queue_model_split(vport->rxq_model))
				q = &rx_qgrp->splitq.rxq_sets[j]->rxq;
			else
				q = rx_qgrp->singleq.rxqs[j];

			err = idpf_rx_desc_alloc(vport, q);
			if (err) {
				pci_err(vport->adapter->pdev,
					"Memory allocation for Rx Queue %u failed\n",
					i);
				goto err_out;
			}
		}

		if (!idpf_is_queue_model_split(vport->rxq_model))
			continue;

		for (j = 0; j < vport->num_bufqs_per_qgrp; j++) {
			struct idpf_buf_queue *q;

			q = &rx_qgrp->splitq.bufq_sets[j].bufq;

			err = idpf_bufq_desc_alloc(vport, q);
			if (err) {
				pci_err(vport->adapter->pdev,
					"Memory allocation for Rx Buffer Queue %u failed\n",
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

static int idpf_init_queue_set(const struct idpf_queue_set *qs)
{
	const struct idpf_vport *vport = qs->vport;
	bool splitq;
	int err;

	splitq = idpf_is_queue_model_split(vport->rxq_model);

	for (u32 i = 0; i < qs->num; i++) {
		const struct idpf_queue_ptr *q = &qs->qs[i];
		struct idpf_buf_queue *bufq;

		switch (q->type) {
		case VIRTCHNL2_QUEUE_TYPE_RX:
			err = idpf_rx_desc_alloc(vport, q->rxq);
			if (err)
				break;

			err = idpf_xdp_rxq_info_init(q->rxq);
			if (err)
				break;

			if (!splitq)
				err = idpf_rx_bufs_init_singleq(q->rxq);

			break;
		case VIRTCHNL2_QUEUE_TYPE_RX_BUFFER:
			bufq = q->bufq;

			err = idpf_bufq_desc_alloc(vport, bufq);
			if (err)
				break;

			for (u32 j = 0; j < bufq->q_vector->num_bufq; j++) {
				struct idpf_buf_queue * const *bufqs;
				enum libeth_fqe_type type;
				u32 ts;

				bufqs = bufq->q_vector->bufq;
				if (bufqs[j] != bufq)
					continue;

				if (j) {
					type = LIBETH_FQE_SHORT;
					ts = bufqs[j - 1]->truesize >> 1;
				} else {
					type = LIBETH_FQE_MTU;
					ts = 0;
				}

				bufq->truesize = ts;

				err = idpf_rx_bufs_init(bufq, type);
				break;
			}

			break;
		case VIRTCHNL2_QUEUE_TYPE_TX:
			err = idpf_tx_desc_alloc(vport, q->txq);
			break;
		case VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION:
			err = idpf_compl_desc_alloc(vport, q->complq);
			break;
		default:
			continue;
		}

		if (err)
			return err;
	}

	return 0;
}

static void idpf_clean_queue_set(const struct idpf_queue_set *qs)
{
	const struct idpf_vport *vport = qs->vport;
	struct device *dev = vport->netdev->dev.parent;

	for (u32 i = 0; i < qs->num; i++) {
		const struct idpf_queue_ptr *q = &qs->qs[i];

		switch (q->type) {
		case VIRTCHNL2_QUEUE_TYPE_RX:
			idpf_xdp_rxq_info_deinit(q->rxq, vport->rxq_model);
			idpf_rx_desc_rel(q->rxq, dev, vport->rxq_model);
			break;
		case VIRTCHNL2_QUEUE_TYPE_RX_BUFFER:
			idpf_rx_desc_rel_bufq(q->bufq, dev);
			break;
		case VIRTCHNL2_QUEUE_TYPE_TX:
			idpf_tx_desc_rel(q->txq);

			if (idpf_queue_has(XDP, q->txq)) {
				q->txq->pending = 0;
				q->txq->xdp_tx = 0;
			} else {
				q->txq->txq_grp->num_completions_pending = 0;
			}

			writel(q->txq->next_to_use, q->txq->tail);
			break;
		case VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION:
			idpf_compl_desc_rel(q->complq);
			q->complq->num_completions = 0;
			break;
		default:
			break;
		}
	}
}

static void idpf_qvec_ena_irq(struct idpf_q_vector *qv)
{
	if (qv->num_txq) {
		u32 itr;

		if (IDPF_ITR_IS_DYNAMIC(qv->tx_intr_mode))
			itr = qv->vport->tx_itr_profile[qv->tx_dim.profile_ix];
		else
			itr = qv->tx_itr_value;

		idpf_vport_intr_write_itr(qv, itr, true);
	}

	if (qv->num_rxq) {
		u32 itr;

		if (IDPF_ITR_IS_DYNAMIC(qv->rx_intr_mode))
			itr = qv->vport->rx_itr_profile[qv->rx_dim.profile_ix];
		else
			itr = qv->rx_itr_value;

		idpf_vport_intr_write_itr(qv, itr, false);
	}

	if (qv->num_txq || qv->num_rxq)
		idpf_vport_intr_update_itr_ena_irq(qv);
}

/**
 * idpf_vector_to_queue_set - create a queue set associated with the given
 *			      queue vector
 * @qv: queue vector corresponding to the queue pair
 *
 * Returns a pointer to a dynamically allocated array of pointers to all
 * queues associated with a given queue vector (@qv).
 * Please note that the caller is responsible to free the memory allocated
 * by this function using kfree().
 *
 * Return: &idpf_queue_set on success, %NULL in case of error.
 */
static struct idpf_queue_set *
idpf_vector_to_queue_set(struct idpf_q_vector *qv)
{
	bool xdp = qv->vport->xdp_txq_offset && !qv->num_xsksq;
	struct idpf_vport *vport = qv->vport;
	struct idpf_queue_set *qs;
	u32 num;

	num = qv->num_rxq + qv->num_bufq + qv->num_txq + qv->num_complq;
	num += xdp ? qv->num_rxq * 2 : qv->num_xsksq * 2;
	if (!num)
		return NULL;

	qs = idpf_alloc_queue_set(vport, num);
	if (!qs)
		return NULL;

	num = 0;

	for (u32 i = 0; i < qv->num_bufq; i++) {
		qs->qs[num].type = VIRTCHNL2_QUEUE_TYPE_RX_BUFFER;
		qs->qs[num++].bufq = qv->bufq[i];
	}

	for (u32 i = 0; i < qv->num_rxq; i++) {
		qs->qs[num].type = VIRTCHNL2_QUEUE_TYPE_RX;
		qs->qs[num++].rxq = qv->rx[i];
	}

	for (u32 i = 0; i < qv->num_txq; i++) {
		qs->qs[num].type = VIRTCHNL2_QUEUE_TYPE_TX;
		qs->qs[num++].txq = qv->tx[i];
	}

	for (u32 i = 0; i < qv->num_complq; i++) {
		qs->qs[num].type = VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION;
		qs->qs[num++].complq = qv->complq[i];
	}

	if (!vport->xdp_txq_offset)
		goto finalize;

	if (xdp) {
		for (u32 i = 0; i < qv->num_rxq; i++) {
			u32 idx = vport->xdp_txq_offset + qv->rx[i]->idx;

			qs->qs[num].type = VIRTCHNL2_QUEUE_TYPE_TX;
			qs->qs[num++].txq = vport->txqs[idx];

			qs->qs[num].type = VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION;
			qs->qs[num++].complq = vport->txqs[idx]->complq;
		}
	} else {
		for (u32 i = 0; i < qv->num_xsksq; i++) {
			qs->qs[num].type = VIRTCHNL2_QUEUE_TYPE_TX;
			qs->qs[num++].txq = qv->xsksq[i];

			qs->qs[num].type = VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION;
			qs->qs[num++].complq = qv->xsksq[i]->complq;
		}
	}

finalize:
	if (num != qs->num) {
		kfree(qs);
		return NULL;
	}

	return qs;
}

static int idpf_qp_enable(const struct idpf_queue_set *qs, u32 qid)
{
	struct idpf_vport *vport = qs->vport;
	struct idpf_q_vector *q_vector;
	int err;

	q_vector = idpf_find_rxq_vec(vport, qid);

	err = idpf_init_queue_set(qs);
	if (err) {
		netdev_err(vport->netdev, "Could not initialize queues in pair %u: %pe\n",
			   qid, ERR_PTR(err));
		return err;
	}

	if (!vport->xdp_txq_offset)
		goto config;

	q_vector->xsksq = kcalloc(DIV_ROUND_UP(vport->num_rxq_grp,
					       vport->num_q_vectors),
				  sizeof(*q_vector->xsksq), GFP_KERNEL);
	if (!q_vector->xsksq)
		return -ENOMEM;

	for (u32 i = 0; i < qs->num; i++) {
		const struct idpf_queue_ptr *q = &qs->qs[i];

		if (q->type != VIRTCHNL2_QUEUE_TYPE_TX)
			continue;

		if (!idpf_queue_has(XSK, q->txq))
			continue;

		idpf_xsk_init_wakeup(q_vector);

		q->txq->q_vector = q_vector;
		q_vector->xsksq[q_vector->num_xsksq++] = q->txq;
	}

config:
	err = idpf_send_config_queue_set_msg(qs);
	if (err) {
		netdev_err(vport->netdev, "Could not configure queues in pair %u: %pe\n",
			   qid, ERR_PTR(err));
		return err;
	}

	err = idpf_send_enable_queue_set_msg(qs);
	if (err) {
		netdev_err(vport->netdev, "Could not enable queues in pair %u: %pe\n",
			   qid, ERR_PTR(err));
		return err;
	}

	napi_enable(&q_vector->napi);
	idpf_qvec_ena_irq(q_vector);

	netif_start_subqueue(vport->netdev, qid);

	return 0;
}

static int idpf_qp_disable(const struct idpf_queue_set *qs, u32 qid)
{
	struct idpf_vport *vport = qs->vport;
	struct idpf_q_vector *q_vector;
	int err;

	q_vector = idpf_find_rxq_vec(vport, qid);
	netif_stop_subqueue(vport->netdev, qid);

	writel(0, q_vector->intr_reg.dyn_ctl);
	napi_disable(&q_vector->napi);

	err = idpf_send_disable_queue_set_msg(qs);
	if (err) {
		netdev_err(vport->netdev, "Could not disable queues in pair %u: %pe\n",
			   qid, ERR_PTR(err));
		return err;
	}

	idpf_clean_queue_set(qs);

	kfree(q_vector->xsksq);
	q_vector->num_xsksq = 0;

	return 0;
}

/**
 * idpf_qp_switch - enable or disable queues associated with queue pair
 * @vport: vport to switch the pair for
 * @qid: index of the queue pair to switch
 * @en: whether to enable or disable the pair
 *
 * Return: 0 on success, -errno on failure.
 */
int idpf_qp_switch(struct idpf_vport *vport, u32 qid, bool en)
{
	struct idpf_q_vector *q_vector = idpf_find_rxq_vec(vport, qid);
	struct idpf_queue_set *qs __free(kfree) = NULL;

	if (idpf_find_txq_vec(vport, qid) != q_vector)
		return -EINVAL;

	qs = idpf_vector_to_queue_set(q_vector);
	if (!qs)
		return -ENOMEM;

	return en ? idpf_qp_enable(qs, qid) : idpf_qp_disable(qs, qid);
}

/**
 * idpf_txq_group_rel - Release all resources for txq groups
 * @vport: vport to release txq groups on
 */
static void idpf_txq_group_rel(struct idpf_vport *vport)
{
	bool split, flow_sch_en;
	int i, j;

	if (!vport->txq_grps)
		return;

	split = idpf_is_queue_model_split(vport->txq_model);
	flow_sch_en = !idpf_is_cap_ena(vport->adapter, IDPF_OTHER_CAPS,
				       VIRTCHNL2_CAP_SPLITQ_QSCHED);

	for (i = 0; i < vport->num_txq_grp; i++) {
		struct idpf_txq_group *txq_grp = &vport->txq_grps[i];

		for (j = 0; j < txq_grp->num_txq; j++) {
			if (flow_sch_en) {
				kfree(txq_grp->txqs[j]->refillq);
				txq_grp->txqs[j]->refillq = NULL;
			}

			kfree(txq_grp->txqs[j]);
			txq_grp->txqs[j] = NULL;
		}

		if (!split)
			continue;

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
	idpf_xdp_copy_prog_to_rqs(vport, NULL);

	idpf_tx_desc_rel_all(vport);
	idpf_rx_desc_rel_all(vport);

	idpf_xdpsqs_put(vport);
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
	struct idpf_ptp_vport_tx_tstamp_caps *caps = vport->tx_tstamp_caps;
	struct work_struct *tstamp_task = &vport->tstamp_task;
	int i, j, k = 0;

	vport->txqs = kcalloc(vport->num_txq, sizeof(*vport->txqs),
			      GFP_KERNEL);

	if (!vport->txqs)
		return -ENOMEM;

	for (i = 0; i < vport->num_txq_grp; i++) {
		struct idpf_txq_group *tx_grp = &vport->txq_grps[i];

		for (j = 0; j < tx_grp->num_txq; j++, k++) {
			vport->txqs[k] = tx_grp->txqs[j];
			vport->txqs[k]->idx = k;

			if (!caps)
				continue;

			vport->txqs[k]->cached_tstamp_caps = caps;
			vport->txqs[k]->tstamp_task = tstamp_task;
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

	vport->xdp_prog = config_data->xdp_prog;
	if (idpf_xdp_enabled(vport)) {
		vport->xdp_txq_offset = config_data->num_req_tx_qs;
		vport->num_xdp_txq = le16_to_cpu(vport_msg->num_tx_q) -
				     vport->xdp_txq_offset;
		vport->xdpsq_share = libeth_xdpsq_shared(vport->num_xdp_txq);
	} else {
		vport->xdp_txq_offset = 0;
		vport->num_xdp_txq = 0;
		vport->xdpsq_share = false;
	}

	/* Adjust number of buffer queues per Rx queue group. */
	if (!idpf_is_queue_model_split(vport->rxq_model)) {
		vport->num_bufqs_per_qgrp = 0;

		return;
	}

	vport->num_bufqs_per_qgrp = IDPF_MAX_BUFQS_PER_RXQ_GRP;
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
	struct idpf_vport_user_config_data *user;
	struct idpf_vport_config *vport_config;
	u16 num_txq_grps, num_rxq_grps;
	u32 num_qs, num_xdpsq;

	vport_config = adapter->vport_config[vport_idx];
	if (vport_config) {
		num_req_tx_qs = vport_config->user_config.num_req_tx_qs;
		num_req_rx_qs = vport_config->user_config.num_req_rx_qs;
	} else {
		u32 num_cpus = netif_get_num_default_rss_queues();

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

	if (!vport_config)
		return 0;

	user = &vport_config->user_config;
	user->num_req_rx_qs = le16_to_cpu(vport_msg->num_rx_q);
	user->num_req_tx_qs = le16_to_cpu(vport_msg->num_tx_q);

	if (vport_config->user_config.xdp_prog)
		num_xdpsq = libeth_xdpsq_num(user->num_req_rx_qs,
					     user->num_req_tx_qs,
					     vport_config->max_q.max_txq);
	else
		num_xdpsq = 0;

	vport_msg->num_tx_q = cpu_to_le16(user->num_req_tx_qs + num_xdpsq);
	if (idpf_is_queue_model_split(le16_to_cpu(vport_msg->txq_model)))
		vport_msg->num_tx_complq = vport_msg->num_tx_q;

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
static void idpf_rxq_set_descids(const struct idpf_vport *vport,
				 struct idpf_rx_queue *q)
{
	if (idpf_is_queue_model_split(vport->rxq_model))
		return;

	if (vport->base_rxd)
		q->rxdids = VIRTCHNL2_RXDID_1_32B_BASE_M;
	else
		q->rxdids = VIRTCHNL2_RXDID_2_FLEX_SQ_NIC_M;
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
	bool split, flow_sch_en;
	int i;

	vport->txq_grps = kcalloc(vport->num_txq_grp,
				  sizeof(*vport->txq_grps), GFP_KERNEL);
	if (!vport->txq_grps)
		return -ENOMEM;

	split = idpf_is_queue_model_split(vport->txq_model);
	flow_sch_en = !idpf_is_cap_ena(vport->adapter, IDPF_OTHER_CAPS,
				       VIRTCHNL2_CAP_SPLITQ_QSCHED);

	for (i = 0; i < vport->num_txq_grp; i++) {
		struct idpf_txq_group *tx_qgrp = &vport->txq_grps[i];
		struct idpf_adapter *adapter = vport->adapter;
		int j;

		tx_qgrp->vport = vport;
		tx_qgrp->num_txq = num_txq;

		for (j = 0; j < tx_qgrp->num_txq; j++) {
			tx_qgrp->txqs[j] = kzalloc(sizeof(*tx_qgrp->txqs[j]),
						   GFP_KERNEL);
			if (!tx_qgrp->txqs[j])
				goto err_alloc;
		}

		for (j = 0; j < tx_qgrp->num_txq; j++) {
			struct idpf_tx_queue *q = tx_qgrp->txqs[j];

			q->dev = &adapter->pdev->dev;
			q->desc_count = vport->txq_desc_count;
			q->tx_max_bufs = idpf_get_max_tx_bufs(adapter);
			q->tx_min_pkt_len = idpf_get_min_tx_pkt_len(adapter);
			q->netdev = vport->netdev;
			q->txq_grp = tx_qgrp;
			q->rel_q_id = j;

			if (!split) {
				q->clean_budget = vport->compln_clean_budget;
				idpf_queue_assign(CRC_EN, q,
						  vport->crc_enable);
			}

			if (!flow_sch_en)
				continue;

			idpf_queue_set(FLOW_SCH_EN, q);

			q->refillq = kzalloc(sizeof(*q->refillq), GFP_KERNEL);
			if (!q->refillq)
				goto err_alloc;

			idpf_queue_set(GEN_CHK, q->refillq);
			idpf_queue_set(RFL_GEN_CHK, q->refillq);
		}

		if (!split)
			continue;

		tx_qgrp->complq = kcalloc(IDPF_COMPLQ_PER_GROUP,
					  sizeof(*tx_qgrp->complq),
					  GFP_KERNEL);
		if (!tx_qgrp->complq)
			goto err_alloc;

		tx_qgrp->complq->desc_count = vport->complq_desc_count;
		tx_qgrp->complq->txq_grp = tx_qgrp;
		tx_qgrp->complq->netdev = vport->netdev;
		tx_qgrp->complq->clean_budget = vport->compln_clean_budget;

		if (flow_sch_en)
			idpf_queue_set(FLOW_SCH_EN, tx_qgrp->complq);
	}

	return 0;

err_alloc:
	idpf_txq_group_rel(vport);

	return -ENOMEM;
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
	int i, k, err = 0;
	bool hs;

	vport->rxq_grps = kcalloc(vport->num_rxq_grp,
				  sizeof(struct idpf_rxq_group), GFP_KERNEL);
	if (!vport->rxq_grps)
		return -ENOMEM;

	hs = idpf_vport_get_hsplit(vport) == ETHTOOL_TCP_DATA_SPLIT_ENABLED;

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
			struct idpf_buf_queue *q;

			q = &rx_qgrp->splitq.bufq_sets[j].bufq;
			q->desc_count = vport->bufq_desc_count[j];
			q->rx_buffer_low_watermark = IDPF_LOW_WATERMARK;

			idpf_queue_assign(HSPLIT_EN, q, hs);

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

				refillq->desc_count =
					vport->bufq_desc_count[j];
				idpf_queue_set(GEN_CHK, refillq);
				idpf_queue_set(RFL_GEN_CHK, refillq);
				refillq->ring = kcalloc(refillq->desc_count,
							sizeof(*refillq->ring),
							GFP_KERNEL);
				if (!refillq->ring) {
					err = -ENOMEM;
					goto err_alloc;
				}
			}
		}

skip_splitq_rx_init:
		for (j = 0; j < num_rxq; j++) {
			struct idpf_rx_queue *q;

			if (!idpf_is_queue_model_split(vport->rxq_model)) {
				q = rx_qgrp->singleq.rxqs[j];
				goto setup_rxq;
			}
			q = &rx_qgrp->splitq.rxq_sets[j]->rxq;
			rx_qgrp->splitq.rxq_sets[j]->refillq[0] =
			      &rx_qgrp->splitq.bufq_sets[0].refillqs[j];
			if (vport->num_bufqs_per_qgrp > IDPF_SINGLE_BUFQ_PER_RXQ_GRP)
				rx_qgrp->splitq.rxq_sets[j]->refillq[1] =
				      &rx_qgrp->splitq.bufq_sets[1].refillqs[j];

			idpf_queue_assign(HSPLIT_EN, q, hs);

setup_rxq:
			q->desc_count = vport->rxq_desc_count;
			q->rx_ptype_lkup = vport->rx_ptype_lkup;
			q->bufq_sets = rx_qgrp->splitq.bufq_sets;
			q->idx = (i * num_rxq) + j;
			q->rx_buffer_low_watermark = IDPF_LOW_WATERMARK;
			q->rx_max_pkt_size = vport->netdev->mtu +
							LIBETH_RX_LL_LEN;
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

	err = idpf_vport_init_fast_path_txqs(vport);
	if (err)
		goto err_out;

	err = idpf_xdpsqs_get(vport);
	if (err)
		goto err_out;

	err = idpf_tx_desc_alloc_all(vport);
	if (err)
		goto err_out;

	err = idpf_rx_desc_alloc_all(vport);
	if (err)
		goto err_out;

	return 0;

err_out:
	idpf_vport_queues_rel(vport);

	return err;
}

/**
 * idpf_tx_read_tstamp - schedule a work to read Tx timestamp value
 * @txq: queue to read the timestamp from
 * @skb: socket buffer to provide Tx timestamp value
 *
 * Schedule a work to read Tx timestamp value generated once the packet is
 * transmitted.
 */
static void idpf_tx_read_tstamp(struct idpf_tx_queue *txq, struct sk_buff *skb)
{
	struct idpf_ptp_vport_tx_tstamp_caps *tx_tstamp_caps;
	struct idpf_ptp_tx_tstamp_status *tx_tstamp_status;

	tx_tstamp_caps = txq->cached_tstamp_caps;
	spin_lock_bh(&tx_tstamp_caps->status_lock);

	for (u32 i = 0; i < tx_tstamp_caps->num_entries; i++) {
		tx_tstamp_status = &tx_tstamp_caps->tx_tstamp_status[i];
		if (tx_tstamp_status->state != IDPF_PTP_FREE)
			continue;

		tx_tstamp_status->skb = skb;
		tx_tstamp_status->state = IDPF_PTP_REQUEST;

		/* Fetch timestamp from completion descriptor through
		 * virtchnl msg to report to stack.
		 */
		queue_work(system_unbound_wq, txq->tstamp_task);
		break;
	}

	spin_unlock_bh(&tx_tstamp_caps->status_lock);
}

#define idpf_tx_splitq_clean_bump_ntc(txq, ntc, desc, buf)	\
do {								\
	if (unlikely(++(ntc) == (txq)->desc_count)) {		\
		ntc = 0;					\
		buf = (txq)->tx_buf;				\
		desc = &(txq)->flex_tx[0];			\
	} else {						\
		(buf)++;					\
		(desc)++;					\
	}							\
} while (0)

/**
 * idpf_tx_splitq_clean - Reclaim resources from buffer queue
 * @tx_q: Tx queue to clean
 * @end: queue index until which it should be cleaned
 * @napi_budget: Used to determine if we are in netpoll
 * @cleaned: pointer to stats struct to track cleaned packets/bytes
 * @descs_only: true if queue is using flow-based scheduling and should
 * not clean buffers at this time
 *
 * Cleans the queue descriptor ring. If the queue is using queue-based
 * scheduling, the buffers will be cleaned as well. If the queue is using
 * flow-based scheduling, only the descriptors are cleaned at this time.
 * Separate packet completion events will be reported on the completion queue,
 * and the buffers will be cleaned separately. The stats are not updated from
 * this function when using flow-based scheduling.
 */
static void idpf_tx_splitq_clean(struct idpf_tx_queue *tx_q, u16 end,
				 int napi_budget,
				 struct libeth_sq_napi_stats *cleaned,
				 bool descs_only)
{
	union idpf_tx_flex_desc *next_pending_desc = NULL;
	union idpf_tx_flex_desc *tx_desc;
	u32 ntc = tx_q->next_to_clean;
	struct libeth_cq_pp cp = {
		.dev	= tx_q->dev,
		.ss	= cleaned,
		.napi	= napi_budget,
	};
	struct idpf_tx_buf *tx_buf;

	if (descs_only) {
		/* Bump ring index to mark as cleaned. */
		tx_q->next_to_clean = end;
		return;
	}

	tx_desc = &tx_q->flex_tx[ntc];
	next_pending_desc = &tx_q->flex_tx[end];
	tx_buf = &tx_q->tx_buf[ntc];

	while (tx_desc != next_pending_desc) {
		u32 eop_idx;

		/* If this entry in the ring was used as a context descriptor,
		 * it's corresponding entry in the buffer ring is reserved. We
		 * can skip this descriptor since there is no buffer to clean.
		 */
		if (tx_buf->type <= LIBETH_SQE_CTX)
			goto fetch_next_txq_desc;

		if (unlikely(tx_buf->type != LIBETH_SQE_SKB))
			break;

		eop_idx = tx_buf->rs_idx;
		libeth_tx_complete(tx_buf, &cp);

		/* unmap remaining buffers */
		while (ntc != eop_idx) {
			idpf_tx_splitq_clean_bump_ntc(tx_q, ntc,
						      tx_desc, tx_buf);

			/* unmap any remaining paged data */
			libeth_tx_complete(tx_buf, &cp);
		}

fetch_next_txq_desc:
		idpf_tx_splitq_clean_bump_ntc(tx_q, ntc, tx_desc, tx_buf);
	}

	tx_q->next_to_clean = ntc;
}

/**
 * idpf_tx_clean_bufs - clean flow scheduling TX queue buffers
 * @txq: queue to clean
 * @buf_id: packet's starting buffer ID, from completion descriptor
 * @cleaned: pointer to stats struct to track cleaned packets/bytes
 * @budget: Used to determine if we are in netpoll
 *
 * Clean all buffers associated with the packet starting at buf_id. Returns the
 * byte/segment count for the cleaned packet.
 */
static void idpf_tx_clean_bufs(struct idpf_tx_queue *txq, u32 buf_id,
			       struct libeth_sq_napi_stats *cleaned,
			       int budget)
{
	struct idpf_tx_buf *tx_buf = NULL;
	struct libeth_cq_pp cp = {
		.dev	= txq->dev,
		.ss	= cleaned,
		.napi	= budget,
	};

	tx_buf = &txq->tx_buf[buf_id];
	if (tx_buf->type == LIBETH_SQE_SKB) {
		if (skb_shinfo(tx_buf->skb)->tx_flags & SKBTX_IN_PROGRESS)
			idpf_tx_read_tstamp(txq, tx_buf->skb);

		libeth_tx_complete(tx_buf, &cp);
		idpf_post_buf_refill(txq->refillq, buf_id);
	}

	while (idpf_tx_buf_next(tx_buf) != IDPF_TXBUF_NULL) {
		buf_id = idpf_tx_buf_next(tx_buf);

		tx_buf = &txq->tx_buf[buf_id];
		libeth_tx_complete(tx_buf, &cp);
		idpf_post_buf_refill(txq->refillq, buf_id);
	}
}

/**
 * idpf_tx_handle_rs_completion - clean a single packet and all of its buffers
 * whether on the buffer ring or in the hash table
 * @txq: Tx ring to clean
 * @desc: pointer to completion queue descriptor to extract completion
 * information from
 * @cleaned: pointer to stats struct to track cleaned packets/bytes
 * @budget: Used to determine if we are in netpoll
 *
 * Returns bytes/packets cleaned
 */
static void idpf_tx_handle_rs_completion(struct idpf_tx_queue *txq,
					 struct idpf_splitq_tx_compl_desc *desc,
					 struct libeth_sq_napi_stats *cleaned,
					 int budget)
{
	/* RS completion contains queue head for queue based scheduling or
	 * completion tag for flow based scheduling.
	 */
	u16 rs_compl_val = le16_to_cpu(desc->common.q_head_compl_tag.q_head);

	if (!idpf_queue_has(FLOW_SCH_EN, txq)) {
		idpf_tx_splitq_clean(txq, rs_compl_val, budget, cleaned, false);
		return;
	}

	idpf_tx_clean_bufs(txq, rs_compl_val, cleaned, budget);
}

/**
 * idpf_tx_clean_complq - Reclaim resources on completion queue
 * @complq: Tx ring to clean
 * @budget: Used to determine if we are in netpoll
 * @cleaned: returns number of packets cleaned
 *
 * Returns true if there's any budget left (e.g. the clean is finished)
 */
static bool idpf_tx_clean_complq(struct idpf_compl_queue *complq, int budget,
				 int *cleaned)
{
	struct idpf_splitq_tx_compl_desc *tx_desc;
	s16 ntc = complq->next_to_clean;
	struct idpf_netdev_priv *np;
	unsigned int complq_budget;
	bool complq_ok = true;
	int i;

	complq_budget = complq->clean_budget;
	tx_desc = &complq->comp[ntc];
	ntc -= complq->desc_count;

	do {
		struct libeth_sq_napi_stats cleaned_stats = { };
		struct idpf_tx_queue *tx_q;
		__le16 hw_head;
		int rel_tx_qid;
		u8 ctype;	/* completion type */
		u16 gen;

		/* if the descriptor isn't done, no work yet to do */
		gen = le16_get_bits(tx_desc->common.qid_comptype_gen,
				    IDPF_TXD_COMPLQ_GEN_M);
		if (idpf_queue_has(GEN_CHK, complq) != gen)
			break;

		/* Find necessary info of TX queue to clean buffers */
		rel_tx_qid = le16_get_bits(tx_desc->common.qid_comptype_gen,
					   IDPF_TXD_COMPLQ_QID_M);
		if (rel_tx_qid >= complq->txq_grp->num_txq ||
		    !complq->txq_grp->txqs[rel_tx_qid]) {
			netdev_err(complq->netdev, "TxQ not found\n");
			goto fetch_next_desc;
		}
		tx_q = complq->txq_grp->txqs[rel_tx_qid];

		/* Determine completion type */
		ctype = le16_get_bits(tx_desc->common.qid_comptype_gen,
				      IDPF_TXD_COMPLQ_COMPL_TYPE_M);
		switch (ctype) {
		case IDPF_TXD_COMPLT_RE:
			hw_head = tx_desc->common.q_head_compl_tag.q_head;

			idpf_tx_splitq_clean(tx_q, le16_to_cpu(hw_head),
					     budget, &cleaned_stats, true);
			break;
		case IDPF_TXD_COMPLT_RS:
			idpf_tx_handle_rs_completion(tx_q, tx_desc,
						     &cleaned_stats, budget);
			break;
		default:
			netdev_err(tx_q->netdev,
				   "Unknown TX completion type: %d\n", ctype);
			goto fetch_next_desc;
		}

		u64_stats_update_begin(&tx_q->stats_sync);
		u64_stats_add(&tx_q->q_stats.packets, cleaned_stats.packets);
		u64_stats_add(&tx_q->q_stats.bytes, cleaned_stats.bytes);
		tx_q->cleaned_pkts += cleaned_stats.packets;
		tx_q->cleaned_bytes += cleaned_stats.bytes;
		complq->num_completions++;
		u64_stats_update_end(&tx_q->stats_sync);

fetch_next_desc:
		tx_desc++;
		ntc++;
		if (unlikely(!ntc)) {
			ntc -= complq->desc_count;
			tx_desc = &complq->comp[0];
			idpf_queue_change(GEN_CHK, complq);
		}

		prefetch(tx_desc);

		/* update budget accounting */
		complq_budget--;
	} while (likely(complq_budget));

	/* Store the state of the complq to be used later in deciding if a
	 * TXQ can be started again
	 */
	if (unlikely(IDPF_TX_COMPLQ_PENDING(complq->txq_grp) >
		     IDPF_TX_COMPLQ_OVERFLOW_THRESH(complq)))
		complq_ok = false;

	np = netdev_priv(complq->netdev);
	for (i = 0; i < complq->txq_grp->num_txq; ++i) {
		struct idpf_tx_queue *tx_q = complq->txq_grp->txqs[i];
		struct netdev_queue *nq;
		bool dont_wake;

		/* We didn't clean anything on this queue, move along */
		if (!tx_q->cleaned_bytes)
			continue;

		*cleaned += tx_q->cleaned_pkts;

		/* Update BQL */
		nq = netdev_get_tx_queue(tx_q->netdev, tx_q->idx);

		dont_wake = !complq_ok || np->state != __IDPF_VPORT_UP ||
			    !netif_carrier_ok(tx_q->netdev);
		/* Check if the TXQ needs to and can be restarted */
		__netif_txq_completed_wake(nq, tx_q->cleaned_pkts, tx_q->cleaned_bytes,
					   IDPF_DESC_UNUSED(tx_q), IDPF_TX_WAKE_THRESH,
					   dont_wake);

		/* Reset cleaned stats for the next time this queue is
		 * cleaned
		 */
		tx_q->cleaned_bytes = 0;
		tx_q->cleaned_pkts = 0;
	}

	ntc += complq->desc_count;
	complq->next_to_clean = ntc;

	return !!complq_budget;
}

/**
 * idpf_wait_for_sw_marker_completion - wait for SW marker of disabled Tx queue
 * @txq: disabled Tx queue
 *
 * When Tx queue is requested for disabling, the CP sends a special completion
 * descriptor called "SW marker", meaning the queue is ready to be destroyed.
 * If, for some reason, the marker is not received within 500 ms, break the
 * polling to not hang the driver.
 */
void idpf_wait_for_sw_marker_completion(const struct idpf_tx_queue *txq)
{
	struct idpf_compl_queue *complq;
	unsigned long timeout;
	bool flow, gen_flag;
	u32 ntc;

	if (!idpf_queue_has(SW_MARKER, txq))
		return;

	complq = idpf_queue_has(XDP, txq) ? txq->complq : txq->txq_grp->complq;
	ntc = complq->next_to_clean;

	flow = idpf_queue_has(FLOW_SCH_EN, complq);
	gen_flag = idpf_queue_has(GEN_CHK, complq);

	timeout = jiffies + msecs_to_jiffies(IDPF_WAIT_FOR_MARKER_TIMEO);

	do {
		struct idpf_splitq_4b_tx_compl_desc *tx_desc;
		struct idpf_tx_queue *target;
		u32 ctype_gen, id;

		tx_desc = flow ? &complq->comp[ntc].common :
			  &complq->comp_4b[ntc];
		ctype_gen = le16_to_cpu(tx_desc->qid_comptype_gen);

		if (!!(ctype_gen & IDPF_TXD_COMPLQ_GEN_M) != gen_flag) {
			usleep_range(500, 1000);
			continue;
		}

		if (FIELD_GET(IDPF_TXD_COMPLQ_COMPL_TYPE_M, ctype_gen) !=
		    IDPF_TXD_COMPLT_SW_MARKER)
			goto next;

		id = FIELD_GET(IDPF_TXD_COMPLQ_QID_M, ctype_gen);
		target = complq->txq_grp->txqs[id];

		idpf_queue_clear(SW_MARKER, target);
		if (target == txq)
			break;

next:
		if (unlikely(++ntc == complq->desc_count)) {
			ntc = 0;
			gen_flag = !gen_flag;
		}
	} while (time_before(jiffies, timeout));

	idpf_queue_assign(GEN_CHK, complq, gen_flag);
	complq->next_to_clean = ntc;
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
		le16_encode_bits(params->dtype, IDPF_FLEX_TXD_QW1_DTYPE_M);
	desc->q.qw1.cmd_dtype |=
		le16_encode_bits(td_cmd, IDPF_FLEX_TXD_QW1_CMD_M);
	desc->q.qw1.buf_size = cpu_to_le16(size);
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
	*(u32 *)&desc->flow.qw1.cmd_dtype = (u8)(params->dtype | td_cmd);
	desc->flow.qw1.rxr_bufsize = cpu_to_le16((u16)size);
	desc->flow.qw1.compl_tag = cpu_to_le16(params->compl_tag);
}

/**
 * idpf_tx_splitq_has_room - check if enough Tx splitq resources are available
 * @tx_q: the queue to be checked
 * @descs_needed: number of descriptors required for this packet
 * @bufs_needed: number of Tx buffers required for this packet
 *
 * Return: 0 if no room available, 1 otherwise
 */
static int idpf_txq_has_room(struct idpf_tx_queue *tx_q, u32 descs_needed,
			     u32 bufs_needed)
{
	if (IDPF_DESC_UNUSED(tx_q) < descs_needed ||
	    IDPF_TX_COMPLQ_PENDING(tx_q->txq_grp) >
		IDPF_TX_COMPLQ_OVERFLOW_THRESH(tx_q->txq_grp->complq) ||
	    idpf_tx_splitq_get_free_bufs(tx_q->refillq) < bufs_needed)
		return 0;
	return 1;
}

/**
 * idpf_tx_maybe_stop_splitq - 1st level check for Tx splitq stop conditions
 * @tx_q: the queue to be checked
 * @descs_needed: number of descriptors required for this packet
 * @bufs_needed: number of buffers needed for this packet
 *
 * Return: 0 if stop is not needed
 */
static int idpf_tx_maybe_stop_splitq(struct idpf_tx_queue *tx_q,
				     u32 descs_needed,
				     u32 bufs_needed)
{
	/* Since we have multiple resources to check for splitq, our
	 * start,stop_thrs becomes a boolean check instead of a count
	 * threshold.
	 */
	if (netif_subqueue_maybe_stop(tx_q->netdev, tx_q->idx,
				      idpf_txq_has_room(tx_q, descs_needed,
							bufs_needed),
				      1, 1))
		return 0;

	u64_stats_update_begin(&tx_q->stats_sync);
	u64_stats_inc(&tx_q->q_stats.q_busy);
	u64_stats_update_end(&tx_q->stats_sync);

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
void idpf_tx_buf_hw_update(struct idpf_tx_queue *tx_q, u32 val,
			   bool xmit_more)
{
	struct netdev_queue *nq;

	nq = netdev_get_tx_queue(tx_q->netdev, tx_q->idx);
	tx_q->next_to_use = val;

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
 * idpf_tx_res_count_required - get number of Tx resources needed for this pkt
 * @txq: queue to send buffer on
 * @skb: send buffer
 * @bufs_needed: (output) number of buffers needed for this skb.
 *
 * Return: number of data descriptors and buffers needed for this skb.
 */
unsigned int idpf_tx_res_count_required(struct idpf_tx_queue *txq,
					struct sk_buff *skb,
					u32 *bufs_needed)
{
	const struct skb_shared_info *shinfo;
	unsigned int count = 0, i;

	count += !!skb_headlen(skb);

	if (!skb_is_nonlinear(skb))
		return count;

	shinfo = skb_shinfo(skb);
	*bufs_needed += shinfo->nr_frags;
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

	if (idpf_chk_linearize(skb, txq->tx_max_bufs, count)) {
		if (__skb_linearize(skb))
			return 0;

		count = idpf_size_to_txd_count(skb->len);
		u64_stats_update_begin(&txq->stats_sync);
		u64_stats_inc(&txq->q_stats.linearize);
		u64_stats_update_end(&txq->stats_sync);
	}

	return count;
}

/**
 * idpf_tx_splitq_bump_ntu - adjust NTU and generation
 * @txq: the tx ring to wrap
 * @ntu: ring index to bump
 */
static unsigned int idpf_tx_splitq_bump_ntu(struct idpf_tx_queue *txq, u16 ntu)
{
	ntu++;

	if (ntu == txq->desc_count)
		ntu = 0;

	return ntu;
}

/**
 * idpf_tx_get_free_buf_id - get a free buffer ID from the refill queue
 * @refillq: refill queue to get buffer ID from
 * @buf_id: return buffer ID
 *
 * Return: true if a buffer ID was found, false if not
 */
static bool idpf_tx_get_free_buf_id(struct idpf_sw_queue *refillq,
				    u32 *buf_id)
{
	u32 ntc = refillq->next_to_clean;
	u32 refill_desc;

	refill_desc = refillq->ring[ntc];

	if (unlikely(idpf_queue_has(RFL_GEN_CHK, refillq) !=
		     !!(refill_desc & IDPF_RFL_BI_GEN_M)))
		return false;

	*buf_id = FIELD_GET(IDPF_RFL_BI_BUFID_M, refill_desc);

	if (unlikely(++ntc == refillq->desc_count)) {
		idpf_queue_change(RFL_GEN_CHK, refillq);
		ntc = 0;
	}

	refillq->next_to_clean = ntc;

	return true;
}

/**
 * idpf_tx_splitq_pkt_err_unmap - Unmap buffers and bump tail in case of error
 * @txq: Tx queue to unwind
 * @params: pointer to splitq params struct
 * @first: starting buffer for packet to unmap
 */
static void idpf_tx_splitq_pkt_err_unmap(struct idpf_tx_queue *txq,
					 struct idpf_tx_splitq_params *params,
					 struct idpf_tx_buf *first)
{
	struct idpf_sw_queue *refillq = txq->refillq;
	struct libeth_sq_napi_stats ss = { };
	struct idpf_tx_buf *tx_buf = first;
	struct libeth_cq_pp cp = {
		.dev    = txq->dev,
		.ss     = &ss,
	};

	u64_stats_update_begin(&txq->stats_sync);
	u64_stats_inc(&txq->q_stats.dma_map_errs);
	u64_stats_update_end(&txq->stats_sync);

	libeth_tx_complete(tx_buf, &cp);
	while (idpf_tx_buf_next(tx_buf) != IDPF_TXBUF_NULL) {
		tx_buf = &txq->tx_buf[idpf_tx_buf_next(tx_buf)];
		libeth_tx_complete(tx_buf, &cp);
	}

	/* Update tail in case netdev_xmit_more was previously true. */
	idpf_tx_buf_hw_update(txq, params->prev_ntu, false);

	if (!refillq)
		return;

	/* Restore refillq state to avoid leaking tags. */
	if (params->prev_refill_gen != idpf_queue_has(RFL_GEN_CHK, refillq))
		idpf_queue_change(RFL_GEN_CHK, refillq);
	refillq->next_to_clean = params->prev_refill_ntc;
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
static void idpf_tx_splitq_map(struct idpf_tx_queue *tx_q,
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
	u32 next_buf_id;
	u16 td_cmd = 0;
	dma_addr_t dma;

	skb = first->skb;

	td_cmd = params->offload.td_cmd;

	data_len = skb->data_len;
	size = skb_headlen(skb);

	tx_desc = &tx_q->flex_tx[i];

	dma = dma_map_single(tx_q->dev, skb->data, size, DMA_TO_DEVICE);

	tx_buf = first;
	first->nr_frags = 0;

	for (frag = &skb_shinfo(skb)->frags[0];; frag++) {
		unsigned int max_data = IDPF_TX_MAX_DESC_DATA_ALIGNED;

		if (unlikely(dma_mapping_error(tx_q->dev, dma))) {
			idpf_tx_buf_next(tx_buf) = IDPF_TXBUF_NULL;
			return idpf_tx_splitq_pkt_err_unmap(tx_q, params,
							    first);
		}

		first->nr_frags++;
		tx_buf->type = LIBETH_SQE_FRAG;

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

			if (unlikely(++i == tx_q->desc_count)) {
				tx_desc = &tx_q->flex_tx[0];
				i = 0;
			} else {
				tx_desc++;
			}

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

		if (unlikely(++i == tx_q->desc_count)) {
			tx_desc = &tx_q->flex_tx[0];
			i = 0;
		} else {
			tx_desc++;
		}

		if (idpf_queue_has(FLOW_SCH_EN, tx_q)) {
			if (unlikely(!idpf_tx_get_free_buf_id(tx_q->refillq,
							      &next_buf_id))) {
				idpf_tx_buf_next(tx_buf) = IDPF_TXBUF_NULL;
				return idpf_tx_splitq_pkt_err_unmap(tx_q, params,
								    first);
			}
		} else {
			next_buf_id = i;
		}
		idpf_tx_buf_next(tx_buf) = next_buf_id;
		tx_buf = &tx_q->tx_buf[next_buf_id];

		size = skb_frag_size(frag);
		data_len -= size;

		dma = skb_frag_dma_map(tx_q->dev, frag, 0, size,
				       DMA_TO_DEVICE);
	}

	/* record SW timestamp if HW timestamp is not available */
	skb_tx_timestamp(skb);

	first->type = LIBETH_SQE_SKB;

	/* write last descriptor with RS and EOP bits */
	first->rs_idx = i;
	idpf_tx_buf_next(tx_buf) = IDPF_TXBUF_NULL;
	td_cmd |= params->eop_cmd;
	idpf_tx_splitq_build_desc(tx_desc, params, td_cmd, size);
	i = idpf_tx_splitq_bump_ntu(tx_q, i);

	tx_q->txq_grp->num_completions_pending++;

	/* record bytecount for BQL */
	nq = netdev_get_tx_queue(tx_q->netdev, tx_q->idx);
	netdev_tx_sent_queue(nq, first->bytes);

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
int idpf_tso(struct sk_buff *skb, struct idpf_tx_offload_params *off)
{
	const struct skb_shared_info *shinfo;
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

	if (!skb_is_gso(skb))
		return 0;

	err = skb_cow_head(skb, 0);
	if (err < 0)
		return err;

	shinfo = skb_shinfo(skb);

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
 * idpf_tx_splitq_get_ctx_desc - grab next desc and update buffer ring
 * @txq: queue to put context descriptor on
 *
 * Since the TX buffer rings mimics the descriptor ring, update the tx buffer
 * ring entry to reflect that this index is a context descriptor
 */
static union idpf_flex_tx_ctx_desc *
idpf_tx_splitq_get_ctx_desc(struct idpf_tx_queue *txq)
{
	union idpf_flex_tx_ctx_desc *desc;
	int i = txq->next_to_use;

	/* grab the next descriptor */
	desc = &txq->flex_ctx[i];
	txq->next_to_use = idpf_tx_splitq_bump_ntu(txq, i);

	return desc;
}

/**
 * idpf_tx_drop_skb - free the SKB and bump tail if necessary
 * @tx_q: queue to send buffer on
 * @skb: pointer to skb
 */
netdev_tx_t idpf_tx_drop_skb(struct idpf_tx_queue *tx_q, struct sk_buff *skb)
{
	u64_stats_update_begin(&tx_q->stats_sync);
	u64_stats_inc(&tx_q->q_stats.skb_drops);
	u64_stats_update_end(&tx_q->stats_sync);

	idpf_tx_buf_hw_update(tx_q, tx_q->next_to_use, false);

	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

#if (IS_ENABLED(CONFIG_PTP_1588_CLOCK))
/**
 * idpf_tx_tstamp - set up context descriptor for hardware timestamp
 * @tx_q: queue to send buffer on
 * @skb: pointer to the SKB we're sending
 * @off: pointer to the offload struct
 *
 * Return: Positive index number on success, negative otherwise.
 */
static int idpf_tx_tstamp(struct idpf_tx_queue *tx_q, struct sk_buff *skb,
			  struct idpf_tx_offload_params *off)
{
	int err, idx;

	/* only timestamp the outbound packet if the user has requested it */
	if (likely(!(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)))
		return -1;

	if (!idpf_ptp_get_txq_tstamp_capability(tx_q))
		return -1;

	/* Tx timestamps cannot be sampled when doing TSO */
	if (off->tx_flags & IDPF_TX_FLAGS_TSO)
		return -1;

	/* Grab an open timestamp slot */
	err = idpf_ptp_request_ts(tx_q, skb, &idx);
	if (err) {
		u64_stats_update_begin(&tx_q->stats_sync);
		u64_stats_inc(&tx_q->q_stats.tstamp_skipped);
		u64_stats_update_end(&tx_q->stats_sync);

		return -1;
	}

	off->tx_flags |= IDPF_TX_FLAGS_TSYN;

	return idx;
}

/**
 * idpf_tx_set_tstamp_desc - Set the Tx descriptor fields needed to generate
 *			     PHY Tx timestamp
 * @ctx_desc: Context descriptor
 * @idx: Index of the Tx timestamp latch
 */
static void idpf_tx_set_tstamp_desc(union idpf_flex_tx_ctx_desc *ctx_desc,
				    u32 idx)
{
	ctx_desc->tsyn.qw1 = le64_encode_bits(IDPF_TX_DESC_DTYPE_CTX,
					      IDPF_TX_CTX_DTYPE_M) |
			     le64_encode_bits(IDPF_TX_CTX_DESC_TSYN,
					      IDPF_TX_CTX_CMD_M) |
			     le64_encode_bits(idx, IDPF_TX_CTX_TSYN_REG_M);
}
#else /* CONFIG_PTP_1588_CLOCK */
static int idpf_tx_tstamp(struct idpf_tx_queue *tx_q, struct sk_buff *skb,
			  struct idpf_tx_offload_params *off)
{
	return -1;
}

static void idpf_tx_set_tstamp_desc(union idpf_flex_tx_ctx_desc *ctx_desc,
				    u32 idx)
{ }
#endif /* CONFIG_PTP_1588_CLOCK */

/**
 * idpf_tx_splitq_need_re - check whether RE bit needs to be set
 * @tx_q: pointer to Tx queue
 *
 * Return: true if RE bit needs to be set, false otherwise
 */
static bool idpf_tx_splitq_need_re(struct idpf_tx_queue *tx_q)
{
	int gap = tx_q->next_to_use - tx_q->last_re;

	gap += (gap < 0) ? tx_q->desc_count : 0;

	return gap >= IDPF_TX_SPLITQ_RE_MIN_GAP;
}

/**
 * idpf_tx_splitq_frame - Sends buffer on Tx ring using flex descriptors
 * @skb: send buffer
 * @tx_q: queue to send buffer on
 *
 * Returns NETDEV_TX_OK if sent, else an error code
 */
static netdev_tx_t idpf_tx_splitq_frame(struct sk_buff *skb,
					struct idpf_tx_queue *tx_q)
{
	struct idpf_tx_splitq_params tx_params = {
		.prev_ntu = tx_q->next_to_use,
	};
	union idpf_flex_tx_ctx_desc *ctx_desc;
	struct idpf_tx_buf *first;
	u32 count, buf_count = 1;
	int tso, idx;
	u32 buf_id;

	count = idpf_tx_res_count_required(tx_q, skb, &buf_count);
	if (unlikely(!count))
		return idpf_tx_drop_skb(tx_q, skb);

	tso = idpf_tso(skb, &tx_params.offload);
	if (unlikely(tso < 0))
		return idpf_tx_drop_skb(tx_q, skb);

	/* Check for splitq specific TX resources */
	count += (IDPF_TX_DESCS_PER_CACHE_LINE + tso);
	if (idpf_tx_maybe_stop_splitq(tx_q, count, buf_count)) {
		idpf_tx_buf_hw_update(tx_q, tx_q->next_to_use, false);

		return NETDEV_TX_BUSY;
	}

	if (tso) {
		/* If tso is needed, set up context desc */
		ctx_desc = idpf_tx_splitq_get_ctx_desc(tx_q);

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
		u64_stats_inc(&tx_q->q_stats.lso_pkts);
		u64_stats_update_end(&tx_q->stats_sync);
	}

	idx = idpf_tx_tstamp(tx_q, skb, &tx_params.offload);
	if (idx != -1) {
		ctx_desc = idpf_tx_splitq_get_ctx_desc(tx_q);
		idpf_tx_set_tstamp_desc(ctx_desc, idx);
	}

	if (idpf_queue_has(FLOW_SCH_EN, tx_q)) {
		struct idpf_sw_queue *refillq = tx_q->refillq;

		/* Save refillq state in case of a packet rollback.  Otherwise,
		 * the tags will be leaked since they will be popped from the
		 * refillq but never reposted during cleaning.
		 */
		tx_params.prev_refill_gen =
			idpf_queue_has(RFL_GEN_CHK, refillq);
		tx_params.prev_refill_ntc = refillq->next_to_clean;

		if (unlikely(!idpf_tx_get_free_buf_id(tx_q->refillq,
						      &buf_id))) {
			if (tx_params.prev_refill_gen !=
			    idpf_queue_has(RFL_GEN_CHK, refillq))
				idpf_queue_change(RFL_GEN_CHK, refillq);
			refillq->next_to_clean = tx_params.prev_refill_ntc;

			tx_q->next_to_use = tx_params.prev_ntu;
			return idpf_tx_drop_skb(tx_q, skb);
		}
		tx_params.compl_tag = buf_id;

		tx_params.dtype = IDPF_TX_DESC_DTYPE_FLEX_FLOW_SCHE;
		tx_params.eop_cmd = IDPF_TXD_FLEX_FLOW_CMD_EOP;
		/* Set the RE bit to periodically "clean" the descriptor ring.
		 * MIN_GAP is set to MIN_RING size to ensure it will be set at
		 * least once each time around the ring.
		 */
		if (idpf_tx_splitq_need_re(tx_q)) {
			tx_params.eop_cmd |= IDPF_TXD_FLEX_FLOW_CMD_RE;
			tx_q->txq_grp->num_completions_pending++;
			tx_q->last_re = tx_q->next_to_use;
		}

		if (skb->ip_summed == CHECKSUM_PARTIAL)
			tx_params.offload.td_cmd |= IDPF_TXD_FLEX_FLOW_CMD_CS_EN;

	} else {
		buf_id = tx_q->next_to_use;

		tx_params.dtype = IDPF_TX_DESC_DTYPE_FLEX_L2TAG1_L2TAG2;
		tx_params.eop_cmd = IDPF_TXD_LAST_DESC_CMD;

		if (skb->ip_summed == CHECKSUM_PARTIAL)
			tx_params.offload.td_cmd |= IDPF_TX_FLEX_DESC_CMD_CS_EN;
	}

	first = &tx_q->tx_buf[buf_id];
	first->skb = skb;

	if (tso) {
		first->packets = tx_params.offload.tso_segs;
		first->bytes = skb->len +
			((first->packets - 1) * tx_params.offload.tso_hdr_len);
	} else {
		first->packets = 1;
		first->bytes = max_t(unsigned int, skb->len, ETH_ZLEN);
	}

	idpf_tx_splitq_map(tx_q, &tx_params, first);

	return NETDEV_TX_OK;
}

/**
 * idpf_tx_start - Selects the right Tx queue to send buffer
 * @skb: send buffer
 * @netdev: network interface device structure
 *
 * Returns NETDEV_TX_OK if sent, else an error code
 */
netdev_tx_t idpf_tx_start(struct sk_buff *skb, struct net_device *netdev)
{
	const struct idpf_vport *vport = idpf_netdev_to_vport(netdev);
	struct idpf_tx_queue *tx_q;

	if (unlikely(skb_get_queue_mapping(skb) >=
		     vport->num_txq - vport->num_xdp_txq)) {
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

	if (idpf_is_queue_model_split(vport->txq_model))
		return idpf_tx_splitq_frame(skb, tx_q);
	else
		return idpf_tx_singleq_frame(skb, tx_q);
}

/**
 * idpf_rx_hash - set the hash value in the skb
 * @rxq: Rx descriptor ring packet is being transacted on
 * @skb: pointer to current skb being populated
 * @rx_desc: Receive descriptor
 * @decoded: Decoded Rx packet type related fields
 */
static void
idpf_rx_hash(const struct idpf_rx_queue *rxq, struct sk_buff *skb,
	     const struct virtchnl2_rx_flex_desc_adv_nic_3 *rx_desc,
	     struct libeth_rx_pt decoded)
{
	u32 hash;

	if (!libeth_rx_pt_has_hash(rxq->xdp_rxq.dev, decoded))
		return;

	hash = le16_to_cpu(rx_desc->hash1) |
	       (rx_desc->ff2_mirrid_hash2.hash2 << 16) |
	       (rx_desc->hash3 << 24);

	libeth_rx_pt_set_hash(skb, hash, decoded);
}

/**
 * idpf_rx_csum - Indicate in skb if checksum is good
 * @rxq: Rx descriptor ring packet is being transacted on
 * @skb: pointer to current skb being populated
 * @csum_bits: checksum fields extracted from the descriptor
 * @decoded: Decoded Rx packet type related fields
 *
 * skb->protocol must be set before this function is called
 */
static void idpf_rx_csum(struct idpf_rx_queue *rxq, struct sk_buff *skb,
			 struct libeth_rx_csum csum_bits,
			 struct libeth_rx_pt decoded)
{
	bool ipv4, ipv6;

	/* check if Rx checksum is enabled */
	if (!libeth_rx_pt_has_checksum(rxq->xdp_rxq.dev, decoded))
		return;

	/* check if HW has decoded the packet and checksum */
	if (unlikely(!csum_bits.l3l4p))
		return;

	ipv4 = libeth_rx_pt_get_ip_ver(decoded) == LIBETH_RX_PT_OUTER_IPV4;
	ipv6 = libeth_rx_pt_get_ip_ver(decoded) == LIBETH_RX_PT_OUTER_IPV6;

	if (unlikely(ipv4 && (csum_bits.ipe || csum_bits.eipe)))
		goto checksum_fail;

	if (unlikely(ipv6 && csum_bits.ipv6exadd))
		return;

	/* check for L4 errors and handle packets that were not able to be
	 * checksummed
	 */
	if (unlikely(csum_bits.l4e))
		goto checksum_fail;

	if (!csum_bits.raw_csum_valid ||
	    decoded.inner_prot == LIBETH_RX_PT_INNER_SCTP) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		return;
	}

	skb->csum = csum_unfold((__force __sum16)~swab16(csum_bits.raw_csum));
	skb->ip_summed = CHECKSUM_COMPLETE;

	return;

checksum_fail:
	u64_stats_update_begin(&rxq->stats_sync);
	u64_stats_inc(&rxq->q_stats.hw_csum_err);
	u64_stats_update_end(&rxq->stats_sync);
}

/**
 * idpf_rx_splitq_extract_csum_bits - Extract checksum bits from descriptor
 * @rx_desc: receive descriptor
 *
 * Return: parsed checksum status.
 **/
static struct libeth_rx_csum
idpf_rx_splitq_extract_csum_bits(const struct virtchnl2_rx_flex_desc_adv_nic_3 *rx_desc)
{
	struct libeth_rx_csum csum = { };
	u8 qword0, qword1;

	qword0 = rx_desc->status_err0_qw0;
	qword1 = rx_desc->status_err0_qw1;

	csum.ipe = FIELD_GET(VIRTCHNL2_RX_FLEX_DESC_ADV_STATUS0_XSUM_IPE_M,
			     qword1);
	csum.eipe = FIELD_GET(VIRTCHNL2_RX_FLEX_DESC_ADV_STATUS0_XSUM_EIPE_M,
			      qword1);
	csum.l4e = FIELD_GET(VIRTCHNL2_RX_FLEX_DESC_ADV_STATUS0_XSUM_L4E_M,
			     qword1);
	csum.l3l4p = FIELD_GET(VIRTCHNL2_RX_FLEX_DESC_ADV_STATUS0_L3L4P_M,
			       qword1);
	csum.ipv6exadd = FIELD_GET(VIRTCHNL2_RX_FLEX_DESC_ADV_STATUS0_IPV6EXADD_M,
				   qword0);
	csum.raw_csum_valid =
		!le16_get_bits(rx_desc->ptype_err_fflags0,
			       VIRTCHNL2_RX_FLEX_DESC_ADV_RAW_CSUM_INV_M);
	csum.raw_csum = le16_to_cpu(rx_desc->misc.raw_cs);

	return csum;
}

/**
 * idpf_rx_rsc - Set the RSC fields in the skb
 * @rxq : Rx descriptor ring packet is being transacted on
 * @skb : pointer to current skb being populated
 * @rx_desc: Receive descriptor
 * @decoded: Decoded Rx packet type related fields
 *
 * Return 0 on success and error code on failure
 *
 * Populate the skb fields with the total number of RSC segments, RSC payload
 * length and packet type.
 */
static int idpf_rx_rsc(struct idpf_rx_queue *rxq, struct sk_buff *skb,
		       const struct virtchnl2_rx_flex_desc_adv_nic_3 *rx_desc,
		       struct libeth_rx_pt decoded)
{
	u16 rsc_segments, rsc_seg_len;
	bool ipv4, ipv6;
	int len;

	if (unlikely(libeth_rx_pt_get_ip_ver(decoded) ==
		     LIBETH_RX_PT_OUTER_L2))
		return -EINVAL;

	rsc_seg_len = le16_to_cpu(rx_desc->misc.rscseglen);
	if (unlikely(!rsc_seg_len))
		return -EINVAL;

	ipv4 = libeth_rx_pt_get_ip_ver(decoded) == LIBETH_RX_PT_OUTER_IPV4;
	ipv6 = libeth_rx_pt_get_ip_ver(decoded) == LIBETH_RX_PT_OUTER_IPV6;

	if (unlikely(!(ipv4 ^ ipv6)))
		return -EINVAL;

	rsc_segments = DIV_ROUND_UP(skb->data_len, rsc_seg_len);

	NAPI_GRO_CB(skb)->count = rsc_segments;
	skb_shinfo(skb)->gso_size = rsc_seg_len;

	skb_reset_network_header(skb);

	if (ipv4) {
		struct iphdr *ipv4h = ip_hdr(skb);

		skb_shinfo(skb)->gso_type = SKB_GSO_TCPV4;

		/* Reset and set transport header offset in skb */
		skb_set_transport_header(skb, sizeof(struct iphdr));
		len = skb->len - skb_transport_offset(skb);

		/* Compute the TCP pseudo header checksum*/
		tcp_hdr(skb)->check =
			~tcp_v4_check(len, ipv4h->saddr, ipv4h->daddr, 0);
	} else {
		struct ipv6hdr *ipv6h = ipv6_hdr(skb);

		skb_shinfo(skb)->gso_type = SKB_GSO_TCPV6;
		skb_set_transport_header(skb, sizeof(struct ipv6hdr));
		len = skb->len - skb_transport_offset(skb);
		tcp_hdr(skb)->check =
			~tcp_v6_check(len, &ipv6h->saddr, &ipv6h->daddr, 0);
	}

	tcp_gro_complete(skb);

	u64_stats_update_begin(&rxq->stats_sync);
	u64_stats_inc(&rxq->q_stats.rsc_pkts);
	u64_stats_update_end(&rxq->stats_sync);

	return 0;
}

/**
 * idpf_rx_hwtstamp - check for an RX timestamp and pass up the stack
 * @rxq: pointer to the rx queue that receives the timestamp
 * @rx_desc: pointer to rx descriptor containing timestamp
 * @skb: skb to put timestamp in
 */
static void
idpf_rx_hwtstamp(const struct idpf_rx_queue *rxq,
		 const struct virtchnl2_rx_flex_desc_adv_nic_3 *rx_desc,
		 struct sk_buff *skb)
{
	u64 cached_time, ts_ns;
	u32 ts_high;

	if (!(rx_desc->ts_low & VIRTCHNL2_RX_FLEX_TSTAMP_VALID))
		return;

	cached_time = READ_ONCE(rxq->cached_phc_time);

	ts_high = le32_to_cpu(rx_desc->ts_high);
	ts_ns = idpf_ptp_tstamp_extend_32b_to_64b(cached_time, ts_high);

	*skb_hwtstamps(skb) = (struct skb_shared_hwtstamps) {
		.hwtstamp = ns_to_ktime(ts_ns),
	};
}

/**
 * __idpf_rx_process_skb_fields - Populate skb header fields from Rx descriptor
 * @rxq: Rx descriptor ring packet is being transacted on
 * @skb: pointer to current skb being populated
 * @rx_desc: Receive descriptor
 *
 * This function checks the ring, descriptor, and packet information in
 * order to populate the hash, checksum, protocol, and
 * other fields within the skb.
 */
static int
__idpf_rx_process_skb_fields(struct idpf_rx_queue *rxq, struct sk_buff *skb,
			     const struct virtchnl2_rx_flex_desc_adv_nic_3 *rx_desc)
{
	struct libeth_rx_csum csum_bits;
	struct libeth_rx_pt decoded;
	u16 rx_ptype;

	rx_ptype = le16_get_bits(rx_desc->ptype_err_fflags0,
				 VIRTCHNL2_RX_FLEX_DESC_ADV_PTYPE_M);
	decoded = rxq->rx_ptype_lkup[rx_ptype];

	/* process RSS/hash */
	idpf_rx_hash(rxq, skb, rx_desc, decoded);

	if (idpf_queue_has(PTP, rxq))
		idpf_rx_hwtstamp(rxq, rx_desc, skb);

	if (le16_get_bits(rx_desc->hdrlen_flags,
			  VIRTCHNL2_RX_FLEX_DESC_ADV_RSC_M))
		return idpf_rx_rsc(rxq, skb, rx_desc, decoded);

	csum_bits = idpf_rx_splitq_extract_csum_bits(rx_desc);
	idpf_rx_csum(rxq, skb, csum_bits, decoded);

	return 0;
}

bool idpf_rx_process_skb_fields(struct sk_buff *skb,
				const struct libeth_xdp_buff *xdp,
				struct libeth_rq_napi_stats *rs)
{
	struct idpf_rx_queue *rxq;

	rxq = libeth_xdp_buff_to_rq(xdp, typeof(*rxq), xdp_rxq);

	return !__idpf_rx_process_skb_fields(rxq, skb, xdp->desc);
}

LIBETH_XDP_DEFINE_START();
LIBETH_XDP_DEFINE_RUN(static idpf_xdp_run_pass, idpf_xdp_run_prog,
		      idpf_xdp_tx_flush_bulk, idpf_rx_process_skb_fields);
LIBETH_XDP_DEFINE_FINALIZE(static idpf_xdp_finalize_rx, idpf_xdp_tx_flush_bulk,
			   idpf_xdp_tx_finalize);
LIBETH_XDP_DEFINE_END();

/**
 * idpf_rx_hsplit_wa - handle header buffer overflows and split errors
 * @hdr: Rx buffer for the headers
 * @buf: Rx buffer for the payload
 * @data_len: number of bytes received to the payload buffer
 *
 * When a header buffer overflow occurs or the HW was unable do parse the
 * packet type to perform header split, the whole frame gets placed to the
 * payload buffer. We can't build a valid skb around a payload buffer when
 * the header split is active since it doesn't reserve any head- or tailroom.
 * In that case, copy either the whole frame when it's short or just the
 * Ethernet header to the header buffer to be able to build an skb and adjust
 * the data offset in the payload buffer, IOW emulate the header split.
 *
 * Return: number of bytes copied to the header buffer.
 */
static u32 idpf_rx_hsplit_wa(const struct libeth_fqe *hdr,
			     struct libeth_fqe *buf, u32 data_len)
{
	u32 copy = data_len <= L1_CACHE_BYTES ? data_len : ETH_HLEN;
	struct page *hdr_page, *buf_page;
	const void *src;
	void *dst;

	if (unlikely(netmem_is_net_iov(buf->netmem)) ||
	    !libeth_rx_sync_for_cpu(buf, copy))
		return 0;

	hdr_page = __netmem_to_page(hdr->netmem);
	buf_page = __netmem_to_page(buf->netmem);
	dst = page_address(hdr_page) + hdr->offset +
		pp_page_to_nmdesc(hdr_page)->pp->p.offset;
	src = page_address(buf_page) + buf->offset +
		pp_page_to_nmdesc(buf_page)->pp->p.offset;

	memcpy(dst, src, LARGEST_ALIGN(copy));
	buf->offset += copy;

	return copy;
}

/**
 * idpf_rx_splitq_test_staterr - tests bits in Rx descriptor
 * status and error fields
 * @stat_err_field: field from descriptor to test bits in
 * @stat_err_bits: value to mask
 *
 */
static bool idpf_rx_splitq_test_staterr(const u8 stat_err_field,
					const u8 stat_err_bits)
{
	return !!(stat_err_field & stat_err_bits);
}

/**
 * idpf_rx_splitq_is_eop - process handling of EOP buffers
 * @rx_desc: Rx descriptor for current buffer
 *
 * If the buffer is an EOP buffer, this function exits returning true,
 * otherwise return false indicating that this is in fact a non-EOP buffer.
 */
static bool idpf_rx_splitq_is_eop(struct virtchnl2_rx_flex_desc_adv_nic_3 *rx_desc)
{
	/* if we are the last buffer then there is nothing else to do */
	return likely(idpf_rx_splitq_test_staterr(rx_desc->status_err0_qw1,
						  IDPF_RXD_EOF_SPLITQ));
}

/**
 * idpf_rx_splitq_clean - Clean completed descriptors from Rx queue
 * @rxq: Rx descriptor queue to retrieve receive buffer queue
 * @budget: Total limit on number of packets to process
 *
 * This function provides a "bounce buffer" approach to Rx interrupt
 * processing. The advantage to this is that on systems that have
 * expensive overhead for IOMMU access this provides a means of avoiding
 * it by maintaining the mapping of the page to the system.
 *
 * Returns amount of work completed
 */
static int idpf_rx_splitq_clean(struct idpf_rx_queue *rxq, int budget)
{
	struct idpf_buf_queue *rx_bufq = NULL;
	struct libeth_rq_napi_stats rs = { };
	u16 ntc = rxq->next_to_clean;
	LIBETH_XDP_ONSTACK_BUFF(xdp);
	LIBETH_XDP_ONSTACK_BULK(bq);

	libeth_xdp_tx_init_bulk(&bq, rxq->xdp_prog, rxq->xdp_rxq.dev,
				rxq->xdpsqs, rxq->num_xdp_txq);
	libeth_xdp_init_buff(xdp, &rxq->xdp, &rxq->xdp_rxq);

	/* Process Rx packets bounded by budget */
	while (likely(rs.packets < budget)) {
		struct virtchnl2_rx_flex_desc_adv_nic_3 *rx_desc;
		struct libeth_fqe *hdr, *rx_buf = NULL;
		struct idpf_sw_queue *refillq = NULL;
		struct idpf_rxq_set *rxq_set = NULL;
		unsigned int pkt_len = 0;
		unsigned int hdr_len = 0;
		u16 gen_id, buf_id = 0;
		int bufq_id;
		u8 rxdid;

		/* get the Rx desc from Rx queue based on 'next_to_clean' */
		rx_desc = &rxq->rx[ntc].flex_adv_nic_3_wb;

		/* if the descriptor isn't done, no work yet to do */
		gen_id = le16_get_bits(rx_desc->pktlen_gen_bufq_id,
				       VIRTCHNL2_RX_FLEX_DESC_ADV_GEN_M);
		if (idpf_queue_has(GEN_CHK, rxq) != gen_id)
			break;

		dma_rmb();

		rxdid = FIELD_GET(VIRTCHNL2_RX_FLEX_DESC_ADV_RXDID_M,
				  rx_desc->rxdid_ucast);
		if (rxdid != VIRTCHNL2_RXDID_2_FLEX_SPLITQ) {
			IDPF_RX_BUMP_NTC(rxq, ntc);
			u64_stats_update_begin(&rxq->stats_sync);
			u64_stats_inc(&rxq->q_stats.bad_descs);
			u64_stats_update_end(&rxq->stats_sync);
			continue;
		}

		pkt_len = le16_get_bits(rx_desc->pktlen_gen_bufq_id,
					VIRTCHNL2_RX_FLEX_DESC_ADV_LEN_PBUF_M);

		bufq_id = le16_get_bits(rx_desc->pktlen_gen_bufq_id,
					VIRTCHNL2_RX_FLEX_DESC_ADV_BUFQ_ID_M);

		rxq_set = container_of(rxq, struct idpf_rxq_set, rxq);
		refillq = rxq_set->refillq[bufq_id];

		/* retrieve buffer from the rxq */
		rx_bufq = &rxq->bufq_sets[bufq_id].bufq;

		buf_id = le16_to_cpu(rx_desc->buf_id);

		rx_buf = &rx_bufq->buf[buf_id];

		if (!rx_bufq->hdr_pp)
			goto payload;

#define __HBO_BIT	VIRTCHNL2_RX_FLEX_DESC_ADV_STATUS0_HBO_M
#define __HDR_LEN_MASK	VIRTCHNL2_RX_FLEX_DESC_ADV_LEN_HDR_M
		if (likely(!(rx_desc->status_err0_qw1 & __HBO_BIT)))
			/* If a header buffer overflow, occurs, i.e. header is
			 * too large to fit in the header split buffer, HW will
			 * put the entire packet, including headers, in the
			 * data/payload buffer.
			 */
			hdr_len = le16_get_bits(rx_desc->hdrlen_flags,
						__HDR_LEN_MASK);
#undef __HDR_LEN_MASK
#undef __HBO_BIT

		hdr = &rx_bufq->hdr_buf[buf_id];

		if (unlikely(!hdr_len && !xdp->data)) {
			hdr_len = idpf_rx_hsplit_wa(hdr, rx_buf, pkt_len);
			/* If failed, drop both buffers by setting len to 0 */
			pkt_len -= hdr_len ? : pkt_len;

			u64_stats_update_begin(&rxq->stats_sync);
			u64_stats_inc(&rxq->q_stats.hsplit_buf_ovf);
			u64_stats_update_end(&rxq->stats_sync);
		}

		if (libeth_xdp_process_buff(xdp, hdr, hdr_len))
			rs.hsplit++;

		hdr->netmem = 0;

payload:
		libeth_xdp_process_buff(xdp, rx_buf, pkt_len);
		rx_buf->netmem = 0;

		idpf_post_buf_refill(refillq, buf_id);
		IDPF_RX_BUMP_NTC(rxq, ntc);

		/* skip if it is non EOP desc */
		if (!idpf_rx_splitq_is_eop(rx_desc) || unlikely(!xdp->data))
			continue;

		idpf_xdp_run_pass(xdp, &bq, rxq->napi, &rs, rx_desc);
	}

	idpf_xdp_finalize_rx(&bq);

	rxq->next_to_clean = ntc;
	libeth_xdp_save_buff(&rxq->xdp, xdp);

	u64_stats_update_begin(&rxq->stats_sync);
	u64_stats_add(&rxq->q_stats.packets, rs.packets);
	u64_stats_add(&rxq->q_stats.bytes, rs.bytes);
	u64_stats_add(&rxq->q_stats.hsplit_pkts, rs.hsplit);
	u64_stats_update_end(&rxq->stats_sync);

	return rs.packets;
}

/**
 * idpf_rx_update_bufq_desc - Update buffer queue descriptor
 * @bufq: Pointer to the buffer queue
 * @buf_id: buffer ID
 * @buf_desc: Buffer queue descriptor
 *
 * Return 0 on success and negative on failure.
 */
static int idpf_rx_update_bufq_desc(struct idpf_buf_queue *bufq, u32 buf_id,
				    struct virtchnl2_splitq_rx_buf_desc *buf_desc)
{
	struct libeth_fq_fp fq = {
		.pp		= bufq->pp,
		.fqes		= bufq->buf,
		.truesize	= bufq->truesize,
		.count		= bufq->desc_count,
	};
	dma_addr_t addr;

	addr = libeth_rx_alloc(&fq, buf_id);
	if (addr == DMA_MAPPING_ERROR)
		return -ENOMEM;

	buf_desc->pkt_addr = cpu_to_le64(addr);
	buf_desc->qword0.buf_id = cpu_to_le16(buf_id);

	if (!idpf_queue_has(HSPLIT_EN, bufq))
		return 0;

	fq.pp = bufq->hdr_pp;
	fq.fqes = bufq->hdr_buf;
	fq.truesize = bufq->hdr_truesize;

	addr = libeth_rx_alloc(&fq, buf_id);
	if (addr == DMA_MAPPING_ERROR)
		return -ENOMEM;

	buf_desc->hdr_addr = cpu_to_le64(addr);

	return 0;
}

/**
 * idpf_rx_clean_refillq - Clean refill queue buffers
 * @bufq: buffer queue to post buffers back to
 * @refillq: refill queue to clean
 *
 * This function takes care of the buffer refill management
 */
static void idpf_rx_clean_refillq(struct idpf_buf_queue *bufq,
				  struct idpf_sw_queue *refillq)
{
	struct virtchnl2_splitq_rx_buf_desc *buf_desc;
	u16 bufq_nta = bufq->next_to_alloc;
	u16 ntc = refillq->next_to_clean;
	int cleaned = 0;

	buf_desc = &bufq->split_buf[bufq_nta];

	/* make sure we stop at ring wrap in the unlikely case ring is full */
	while (likely(cleaned < refillq->desc_count)) {
		u32 buf_id, refill_desc = refillq->ring[ntc];
		bool failure;

		if (idpf_queue_has(RFL_GEN_CHK, refillq) !=
		    !!(refill_desc & IDPF_RFL_BI_GEN_M))
			break;

		buf_id = FIELD_GET(IDPF_RFL_BI_BUFID_M, refill_desc);
		failure = idpf_rx_update_bufq_desc(bufq, buf_id, buf_desc);
		if (failure)
			break;

		if (unlikely(++ntc == refillq->desc_count)) {
			idpf_queue_change(RFL_GEN_CHK, refillq);
			ntc = 0;
		}

		if (unlikely(++bufq_nta == bufq->desc_count)) {
			buf_desc = &bufq->split_buf[0];
			bufq_nta = 0;
		} else {
			buf_desc++;
		}

		cleaned++;
	}

	if (!cleaned)
		return;

	/* We want to limit how many transactions on the bus we trigger with
	 * tail writes so we only do it in strides. It's also important we
	 * align the write to a multiple of 8 as required by HW.
	 */
	if (((bufq->next_to_use <= bufq_nta ? 0 : bufq->desc_count) +
	    bufq_nta - bufq->next_to_use) >= IDPF_RX_BUF_POST_STRIDE)
		idpf_rx_buf_hw_update(bufq, ALIGN_DOWN(bufq_nta,
						       IDPF_RX_BUF_POST_STRIDE));

	/* update next to alloc since we have filled the ring */
	refillq->next_to_clean = ntc;
	bufq->next_to_alloc = bufq_nta;
}

/**
 * idpf_rx_clean_refillq_all - Clean all refill queues
 * @bufq: buffer queue with refill queues
 * @nid: ID of the closest NUMA node with memory
 *
 * Iterates through all refill queues assigned to the buffer queue assigned to
 * this vector.  Returns true if clean is complete within budget, false
 * otherwise.
 */
static void idpf_rx_clean_refillq_all(struct idpf_buf_queue *bufq, int nid)
{
	struct idpf_bufq_set *bufq_set;
	int i;

	page_pool_nid_changed(bufq->pp, nid);
	if (bufq->hdr_pp)
		page_pool_nid_changed(bufq->hdr_pp, nid);

	bufq_set = container_of(bufq, struct idpf_bufq_set, bufq);
	for (i = 0; i < bufq_set->num_refillqs; i++)
		idpf_rx_clean_refillq(bufq, &bufq_set->refillqs[i]);
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
	struct idpf_q_vector *q_vector = (struct idpf_q_vector *)data;

	q_vector->total_events++;
	napi_schedule_irqoff(&q_vector->napi);

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
	for (u32 v_idx = 0; v_idx < vport->num_q_vectors; v_idx++) {
		struct idpf_q_vector *q_vector = &vport->q_vectors[v_idx];

		kfree(q_vector->xsksq);
		q_vector->xsksq = NULL;
		kfree(q_vector->complq);
		q_vector->complq = NULL;
		kfree(q_vector->bufq);
		q_vector->bufq = NULL;
		kfree(q_vector->tx);
		q_vector->tx = NULL;
		kfree(q_vector->rx);
		q_vector->rx = NULL;
	}

	kfree(vport->q_vectors);
	vport->q_vectors = NULL;
}

static void idpf_q_vector_set_napi(struct idpf_q_vector *q_vector, bool link)
{
	struct napi_struct *napi = link ? &q_vector->napi : NULL;
	struct net_device *dev = q_vector->vport->netdev;

	for (u32 i = 0; i < q_vector->num_rxq; i++)
		netif_queue_set_napi(dev, q_vector->rx[i]->idx,
				     NETDEV_QUEUE_TYPE_RX, napi);

	for (u32 i = 0; i < q_vector->num_txq; i++)
		netif_queue_set_napi(dev, q_vector->tx[i]->idx,
				     NETDEV_QUEUE_TYPE_TX, napi);
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

		idpf_q_vector_set_napi(q_vector, false);
		kfree(free_irq(irq_num, q_vector));
	}
}

/**
 * idpf_vport_intr_dis_irq_all - Disable all interrupt
 * @vport: main vport structure
 */
static void idpf_vport_intr_dis_irq_all(struct idpf_vport *vport)
{
	struct idpf_q_vector *q_vector = vport->q_vectors;
	int q_idx;

	writel(0, vport->noirq_dyn_ctl);

	for (q_idx = 0; q_idx < vport->num_q_vectors; q_idx++)
		writel(0, q_vector[q_idx].intr_reg.dyn_ctl);
}

/**
 * idpf_vport_intr_buildreg_itr - Enable default interrupt generation settings
 * @q_vector: pointer to q_vector
 */
static u32 idpf_vport_intr_buildreg_itr(struct idpf_q_vector *q_vector)
{
	u32 itr_val = q_vector->intr_reg.dyn_ctl_intena_m;
	int type = IDPF_NO_ITR_UPDATE_IDX;
	u16 itr = 0;

	if (q_vector->wb_on_itr) {
		/*
		 * Trigger a software interrupt when exiting wb_on_itr, to make
		 * sure we catch any pending write backs that might have been
		 * missed due to interrupt state transition.
		 */
		itr_val |= q_vector->intr_reg.dyn_ctl_swint_trig_m |
			   q_vector->intr_reg.dyn_ctl_sw_itridx_ena_m;
		type = IDPF_SW_ITR_UPDATE_IDX;
		itr = IDPF_ITR_20K;
	}

	itr &= IDPF_ITR_MASK;
	/* Don't clear PBA because that can cause lost interrupts that
	 * came in while we were cleaning/polling
	 */
	itr_val |= (type << q_vector->intr_reg.dyn_ctl_itridx_s) |
		   (itr << (q_vector->intr_reg.dyn_ctl_intrvl_s - 1));

	return itr_val;
}

/**
 * idpf_update_dim_sample - Update dim sample with packets and bytes
 * @q_vector: the vector associated with the interrupt
 * @dim_sample: dim sample to update
 * @dim: dim instance structure
 * @packets: total packets
 * @bytes: total bytes
 *
 * Update the dim sample with the packets and bytes which are passed to this
 * function. Set the dim state appropriately if the dim settings gets stale.
 */
static void idpf_update_dim_sample(struct idpf_q_vector *q_vector,
				   struct dim_sample *dim_sample,
				   struct dim *dim, u64 packets, u64 bytes)
{
	dim_update_sample(q_vector->total_events, packets, bytes, dim_sample);
	dim_sample->comp_ctr = 0;

	/* if dim settings get stale, like when not updated for 1 second or
	 * longer, force it to start again. This addresses the frequent case
	 * of an idle queue being switched to by the scheduler.
	 */
	if (ktime_ms_delta(dim_sample->time, dim->start_sample.time) >= HZ)
		dim->state = DIM_START_MEASURE;
}

/**
 * idpf_net_dim - Update net DIM algorithm
 * @q_vector: the vector associated with the interrupt
 *
 * Create a DIM sample and notify net_dim() so that it can possibly decide
 * a new ITR value based on incoming packets, bytes, and interrupts.
 *
 * This function is a no-op if the queue is not configured to dynamic ITR.
 */
static void idpf_net_dim(struct idpf_q_vector *q_vector)
{
	struct dim_sample dim_sample = { };
	u64 packets, bytes;
	u32 i;

	if (!IDPF_ITR_IS_DYNAMIC(q_vector->tx_intr_mode))
		goto check_rx_itr;

	for (i = 0, packets = 0, bytes = 0; i < q_vector->num_txq; i++) {
		struct idpf_tx_queue *txq = q_vector->tx[i];
		unsigned int start;

		do {
			start = u64_stats_fetch_begin(&txq->stats_sync);
			packets += u64_stats_read(&txq->q_stats.packets);
			bytes += u64_stats_read(&txq->q_stats.bytes);
		} while (u64_stats_fetch_retry(&txq->stats_sync, start));
	}

	idpf_update_dim_sample(q_vector, &dim_sample, &q_vector->tx_dim,
			       packets, bytes);
	net_dim(&q_vector->tx_dim, &dim_sample);

check_rx_itr:
	if (!IDPF_ITR_IS_DYNAMIC(q_vector->rx_intr_mode))
		return;

	for (i = 0, packets = 0, bytes = 0; i < q_vector->num_rxq; i++) {
		struct idpf_rx_queue *rxq = q_vector->rx[i];
		unsigned int start;

		do {
			start = u64_stats_fetch_begin(&rxq->stats_sync);
			packets += u64_stats_read(&rxq->q_stats.packets);
			bytes += u64_stats_read(&rxq->q_stats.bytes);
		} while (u64_stats_fetch_retry(&rxq->stats_sync, start));
	}

	idpf_update_dim_sample(q_vector, &dim_sample, &q_vector->rx_dim,
			       packets, bytes);
	net_dim(&q_vector->rx_dim, &dim_sample);
}

/**
 * idpf_vport_intr_update_itr_ena_irq - Update itr and re-enable MSIX interrupt
 * @q_vector: q_vector for which itr is being updated and interrupt enabled
 *
 * Update the net_dim() algorithm and re-enable the interrupt associated with
 * this vector.
 */
void idpf_vport_intr_update_itr_ena_irq(struct idpf_q_vector *q_vector)
{
	u32 intval;

	/* net_dim() updates ITR out-of-band using a work item */
	idpf_net_dim(q_vector);

	intval = idpf_vport_intr_buildreg_itr(q_vector);
	q_vector->wb_on_itr = false;

	writel(intval, q_vector->intr_reg.dyn_ctl);
}

/**
 * idpf_vport_intr_req_irq - get MSI-X vectors from the OS for the vport
 * @vport: main vport structure
 */
static int idpf_vport_intr_req_irq(struct idpf_vport *vport)
{
	struct idpf_adapter *adapter = vport->adapter;
	const char *drv_name, *if_name, *vec_name;
	int vector, err, irq_num, vidx;

	drv_name = dev_driver_string(&adapter->pdev->dev);
	if_name = netdev_name(vport->netdev);

	for (vector = 0; vector < vport->num_q_vectors; vector++) {
		struct idpf_q_vector *q_vector = &vport->q_vectors[vector];
		char *name;

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

		name = kasprintf(GFP_KERNEL, "%s-%s-%s-%d", drv_name, if_name,
				 vec_name, vidx);

		err = request_irq(irq_num, idpf_vport_intr_clean_queues, 0,
				  name, q_vector);
		if (err) {
			netdev_err(vport->netdev,
				   "Request_irq failed, error: %d\n", err);
			goto free_q_irqs;
		}

		idpf_q_vector_set_napi(q_vector, true);
	}

	return 0;

free_q_irqs:
	while (--vector >= 0) {
		vidx = vport->q_vector_idxs[vector];
		irq_num = adapter->msix_entries[vidx].vector;
		kfree(free_irq(irq_num, &vport->q_vectors[vector]));
	}

	return err;
}

/**
 * idpf_vport_intr_write_itr - Write ITR value to the ITR register
 * @q_vector: q_vector structure
 * @itr: Interrupt throttling rate
 * @tx: Tx or Rx ITR
 */
void idpf_vport_intr_write_itr(struct idpf_q_vector *q_vector, u16 itr, bool tx)
{
	struct idpf_intr_reg *intr_reg;

	if (tx && !q_vector->tx)
		return;
	else if (!tx && !q_vector->rx)
		return;

	intr_reg = &q_vector->intr_reg;
	writel(ITR_REG_ALIGN(itr) >> IDPF_ITR_GRAN_S,
	       tx ? intr_reg->tx_itr : intr_reg->rx_itr);
}

/**
 * idpf_vport_intr_ena_irq_all - Enable IRQ for the given vport
 * @vport: main vport structure
 */
static void idpf_vport_intr_ena_irq_all(struct idpf_vport *vport)
{
	bool dynamic;
	int q_idx;
	u16 itr;

	for (q_idx = 0; q_idx < vport->num_q_vectors; q_idx++) {
		struct idpf_q_vector *qv = &vport->q_vectors[q_idx];

		/* Set the initial ITR values */
		if (qv->num_txq) {
			dynamic = IDPF_ITR_IS_DYNAMIC(qv->tx_intr_mode);
			itr = vport->tx_itr_profile[qv->tx_dim.profile_ix];
			idpf_vport_intr_write_itr(qv, dynamic ?
						  itr : qv->tx_itr_value,
						  true);
		}

		if (qv->num_rxq) {
			dynamic = IDPF_ITR_IS_DYNAMIC(qv->rx_intr_mode);
			itr = vport->rx_itr_profile[qv->rx_dim.profile_ix];
			idpf_vport_intr_write_itr(qv, dynamic ?
						  itr : qv->rx_itr_value,
						  false);
		}

		if (qv->num_txq || qv->num_rxq)
			idpf_vport_intr_update_itr_ena_irq(qv);
	}

	writel(vport->noirq_dyn_ctl_ena, vport->noirq_dyn_ctl);
}

/**
 * idpf_vport_intr_deinit - Release all vector associations for the vport
 * @vport: main vport structure
 */
void idpf_vport_intr_deinit(struct idpf_vport *vport)
{
	idpf_vport_intr_dis_irq_all(vport);
	idpf_vport_intr_napi_dis_all(vport);
	idpf_vport_intr_napi_del_all(vport);
	idpf_vport_intr_rel_irq(vport);
}

/**
 * idpf_tx_dim_work - Call back from the stack
 * @work: work queue structure
 */
static void idpf_tx_dim_work(struct work_struct *work)
{
	struct idpf_q_vector *q_vector;
	struct idpf_vport *vport;
	struct dim *dim;
	u16 itr;

	dim = container_of(work, struct dim, work);
	q_vector = container_of(dim, struct idpf_q_vector, tx_dim);
	vport = q_vector->vport;

	if (dim->profile_ix >= ARRAY_SIZE(vport->tx_itr_profile))
		dim->profile_ix = ARRAY_SIZE(vport->tx_itr_profile) - 1;

	/* look up the values in our local table */
	itr = vport->tx_itr_profile[dim->profile_ix];

	idpf_vport_intr_write_itr(q_vector, itr, true);

	dim->state = DIM_START_MEASURE;
}

/**
 * idpf_rx_dim_work - Call back from the stack
 * @work: work queue structure
 */
static void idpf_rx_dim_work(struct work_struct *work)
{
	struct idpf_q_vector *q_vector;
	struct idpf_vport *vport;
	struct dim *dim;
	u16 itr;

	dim = container_of(work, struct dim, work);
	q_vector = container_of(dim, struct idpf_q_vector, rx_dim);
	vport = q_vector->vport;

	if (dim->profile_ix >= ARRAY_SIZE(vport->rx_itr_profile))
		dim->profile_ix = ARRAY_SIZE(vport->rx_itr_profile) - 1;

	/* look up the values in our local table */
	itr = vport->rx_itr_profile[dim->profile_ix];

	idpf_vport_intr_write_itr(q_vector, itr, false);

	dim->state = DIM_START_MEASURE;
}

/**
 * idpf_init_dim - Set up dynamic interrupt moderation
 * @qv: q_vector structure
 */
static void idpf_init_dim(struct idpf_q_vector *qv)
{
	INIT_WORK(&qv->tx_dim.work, idpf_tx_dim_work);
	qv->tx_dim.mode = DIM_CQ_PERIOD_MODE_START_FROM_EQE;
	qv->tx_dim.profile_ix = IDPF_DIM_DEFAULT_PROFILE_IX;

	INIT_WORK(&qv->rx_dim.work, idpf_rx_dim_work);
	qv->rx_dim.mode = DIM_CQ_PERIOD_MODE_START_FROM_EQE;
	qv->rx_dim.profile_ix = IDPF_DIM_DEFAULT_PROFILE_IX;
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

		idpf_init_dim(q_vector);
		napi_enable(&q_vector->napi);
	}
}

/**
 * idpf_tx_splitq_clean_all- Clean completion queues
 * @q_vec: queue vector
 * @budget: Used to determine if we are in netpoll
 * @cleaned: returns number of packets cleaned
 *
 * Returns false if clean is not complete else returns true
 */
static bool idpf_tx_splitq_clean_all(struct idpf_q_vector *q_vec,
				     int budget, int *cleaned)
{
	u16 num_complq = q_vec->num_complq;
	bool clean_complete = true;
	int i, budget_per_q;

	if (unlikely(!num_complq))
		return true;

	budget_per_q = DIV_ROUND_UP(budget, num_complq);

	for (i = 0; i < num_complq; i++)
		clean_complete &= idpf_tx_clean_complq(q_vec->complq[i],
						       budget_per_q, cleaned);

	return clean_complete;
}

/**
 * idpf_rx_splitq_clean_all- Clean completion queues
 * @q_vec: queue vector
 * @budget: Used to determine if we are in netpoll
 * @cleaned: returns number of packets cleaned
 *
 * Returns false if clean is not complete else returns true
 */
static bool idpf_rx_splitq_clean_all(struct idpf_q_vector *q_vec, int budget,
				     int *cleaned)
{
	u16 num_rxq = q_vec->num_rxq;
	bool clean_complete = true;
	int pkts_cleaned = 0;
	int i, budget_per_q;
	int nid;

	/* We attempt to distribute budget to each Rx queue fairly, but don't
	 * allow the budget to go below 1 because that would exit polling early.
	 */
	budget_per_q = num_rxq ? max(budget / num_rxq, 1) : 0;
	for (i = 0; i < num_rxq; i++) {
		struct idpf_rx_queue *rxq = q_vec->rx[i];
		int pkts_cleaned_per_q;

		pkts_cleaned_per_q = idpf_queue_has(XSK, rxq) ?
				     idpf_xskrq_poll(rxq, budget_per_q) :
				     idpf_rx_splitq_clean(rxq, budget_per_q);
		/* if we clean as many as budgeted, we must not be done */
		if (pkts_cleaned_per_q >= budget_per_q)
			clean_complete = false;
		pkts_cleaned += pkts_cleaned_per_q;
	}
	*cleaned = pkts_cleaned;

	nid = numa_mem_id();

	for (i = 0; i < q_vec->num_bufq; i++) {
		if (!idpf_queue_has(XSK, q_vec->bufq[i]))
			idpf_rx_clean_refillq_all(q_vec->bufq[i], nid);
	}

	return clean_complete;
}

/**
 * idpf_vport_splitq_napi_poll - NAPI handler
 * @napi: struct from which you get q_vector
 * @budget: budget provided by stack
 */
static int idpf_vport_splitq_napi_poll(struct napi_struct *napi, int budget)
{
	struct idpf_q_vector *q_vector =
				container_of(napi, struct idpf_q_vector, napi);
	bool clean_complete = true;
	int work_done = 0;

	/* Handle case where we are called by netpoll with a budget of 0 */
	if (unlikely(!budget)) {
		idpf_tx_splitq_clean_all(q_vector, budget, &work_done);

		return 0;
	}

	for (u32 i = 0; i < q_vector->num_xsksq; i++)
		clean_complete &= idpf_xsk_xmit(q_vector->xsksq[i]);

	clean_complete &= idpf_tx_splitq_clean_all(q_vector, budget,
						   &work_done);
	clean_complete &= idpf_rx_splitq_clean_all(q_vector, budget,
						   &work_done);

	/* If work not completed, return budget and polling will return */
	if (!clean_complete) {
		idpf_vport_intr_set_wb_on_itr(q_vector);
		return budget;
	}

	work_done = min_t(int, work_done, budget - 1);

	/* Exit the polling mode, but don't re-enable interrupts if stack might
	 * poll us due to busy-polling
	 */
	if (napi_complete_done(napi, work_done))
		idpf_vport_intr_update_itr_ena_irq(q_vector);
	else
		idpf_vport_intr_set_wb_on_itr(q_vector);

	return work_done;
}

/**
 * idpf_vport_intr_map_vector_to_qs - Map vectors to queues
 * @vport: virtual port
 *
 * Mapping for vectors to queues
 */
static void idpf_vport_intr_map_vector_to_qs(struct idpf_vport *vport)
{
	u16 num_txq_grp = vport->num_txq_grp - vport->num_xdp_txq;
	bool split = idpf_is_queue_model_split(vport->rxq_model);
	struct idpf_rxq_group *rx_qgrp;
	struct idpf_txq_group *tx_qgrp;
	u32 i, qv_idx, q_index;

	for (i = 0, qv_idx = 0; i < vport->num_rxq_grp; i++) {
		u16 num_rxq;

		if (qv_idx >= vport->num_q_vectors)
			qv_idx = 0;

		rx_qgrp = &vport->rxq_grps[i];
		if (split)
			num_rxq = rx_qgrp->splitq.num_rxq_sets;
		else
			num_rxq = rx_qgrp->singleq.num_rxq;

		for (u32 j = 0; j < num_rxq; j++) {
			struct idpf_rx_queue *q;

			if (split)
				q = &rx_qgrp->splitq.rxq_sets[j]->rxq;
			else
				q = rx_qgrp->singleq.rxqs[j];
			q->q_vector = &vport->q_vectors[qv_idx];
			q_index = q->q_vector->num_rxq;
			q->q_vector->rx[q_index] = q;
			q->q_vector->num_rxq++;

			if (split)
				q->napi = &q->q_vector->napi;
		}

		if (split) {
			for (u32 j = 0; j < vport->num_bufqs_per_qgrp; j++) {
				struct idpf_buf_queue *bufq;

				bufq = &rx_qgrp->splitq.bufq_sets[j].bufq;
				bufq->q_vector = &vport->q_vectors[qv_idx];
				q_index = bufq->q_vector->num_bufq;
				bufq->q_vector->bufq[q_index] = bufq;
				bufq->q_vector->num_bufq++;
			}
		}

		qv_idx++;
	}

	split = idpf_is_queue_model_split(vport->txq_model);

	for (i = 0, qv_idx = 0; i < num_txq_grp; i++) {
		u16 num_txq;

		if (qv_idx >= vport->num_q_vectors)
			qv_idx = 0;

		tx_qgrp = &vport->txq_grps[i];
		num_txq = tx_qgrp->num_txq;

		for (u32 j = 0; j < num_txq; j++) {
			struct idpf_tx_queue *q;

			q = tx_qgrp->txqs[j];
			q->q_vector = &vport->q_vectors[qv_idx];
			q->q_vector->tx[q->q_vector->num_txq++] = q;
		}

		if (split) {
			struct idpf_compl_queue *q = tx_qgrp->complq;

			q->q_vector = &vport->q_vectors[qv_idx];
			q->q_vector->complq[q->q_vector->num_complq++] = q;
		}

		qv_idx++;
	}

	for (i = 0; i < vport->num_xdp_txq; i++) {
		struct idpf_tx_queue *xdpsq;
		struct idpf_q_vector *qv;

		xdpsq = vport->txqs[vport->xdp_txq_offset + i];
		if (!idpf_queue_has(XSK, xdpsq))
			continue;

		qv = idpf_find_rxq_vec(vport, i);
		idpf_xsk_init_wakeup(qv);

		xdpsq->q_vector = qv;
		qv->xsksq[qv->num_xsksq++] = xdpsq;
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

		vport->noirq_v_idx = vport->q_vector_idxs[i];

		return 0;
	}

	total_vecs = idpf_get_reserved_vecs(adapter);
	vecids = kcalloc(total_vecs, sizeof(u16), GFP_KERNEL);
	if (!vecids)
		return -ENOMEM;

	idpf_get_vec_ids(adapter, vecids, total_vecs, &ac->vchunks);

	for (i = 0; i < vport->num_q_vectors; i++)
		vport->q_vectors[i].v_idx = vecids[vport->q_vector_idxs[i]];

	vport->noirq_v_idx = vecids[vport->q_vector_idxs[i]];

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
	u16 v_idx, qv_idx;
	int irq_num;

	if (idpf_is_queue_model_split(vport->txq_model))
		napi_poll = idpf_vport_splitq_napi_poll;
	else
		napi_poll = idpf_vport_singleq_napi_poll;

	for (v_idx = 0; v_idx < vport->num_q_vectors; v_idx++) {
		struct idpf_q_vector *q_vector = &vport->q_vectors[v_idx];
		qv_idx = vport->q_vector_idxs[v_idx];
		irq_num = vport->adapter->msix_entries[qv_idx].vector;

		netif_napi_add_config(vport->netdev, &q_vector->napi,
				      napi_poll, v_idx);
		netif_napi_set_irq(&q_vector->napi, irq_num);
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
	struct idpf_vport_user_config_data *user_config;
	struct idpf_q_vector *q_vector;
	struct idpf_q_coalesce *q_coal;
	u32 complqs_per_vector, v_idx;
	u16 idx = vport->idx;

	user_config = &vport->adapter->vport_config[idx]->user_config;
	vport->q_vectors = kcalloc(vport->num_q_vectors,
				   sizeof(struct idpf_q_vector), GFP_KERNEL);
	if (!vport->q_vectors)
		return -ENOMEM;

	txqs_per_vector = DIV_ROUND_UP(vport->num_txq_grp,
				       vport->num_q_vectors);
	rxqs_per_vector = DIV_ROUND_UP(vport->num_rxq_grp,
				       vport->num_q_vectors);
	bufqs_per_vector = vport->num_bufqs_per_qgrp *
			   DIV_ROUND_UP(vport->num_rxq_grp,
					vport->num_q_vectors);
	complqs_per_vector = DIV_ROUND_UP(vport->num_txq_grp,
					  vport->num_q_vectors);

	for (v_idx = 0; v_idx < vport->num_q_vectors; v_idx++) {
		q_vector = &vport->q_vectors[v_idx];
		q_coal = &user_config->q_coalesce[v_idx];
		q_vector->vport = vport;

		q_vector->tx_itr_value = q_coal->tx_coalesce_usecs;
		q_vector->tx_intr_mode = q_coal->tx_intr_mode;
		q_vector->tx_itr_idx = VIRTCHNL2_ITR_IDX_1;

		q_vector->rx_itr_value = q_coal->rx_coalesce_usecs;
		q_vector->rx_intr_mode = q_coal->rx_intr_mode;
		q_vector->rx_itr_idx = VIRTCHNL2_ITR_IDX_0;

		q_vector->tx = kcalloc(txqs_per_vector, sizeof(*q_vector->tx),
				       GFP_KERNEL);
		if (!q_vector->tx)
			goto error;

		q_vector->rx = kcalloc(rxqs_per_vector, sizeof(*q_vector->rx),
				       GFP_KERNEL);
		if (!q_vector->rx)
			goto error;

		if (!idpf_is_queue_model_split(vport->rxq_model))
			continue;

		q_vector->bufq = kcalloc(bufqs_per_vector,
					 sizeof(*q_vector->bufq),
					 GFP_KERNEL);
		if (!q_vector->bufq)
			goto error;

		q_vector->complq = kcalloc(complqs_per_vector,
					   sizeof(*q_vector->complq),
					   GFP_KERNEL);
		if (!q_vector->complq)
			goto error;

		if (!vport->xdp_txq_offset)
			continue;

		q_vector->xsksq = kcalloc(rxqs_per_vector,
					  sizeof(*q_vector->xsksq),
					  GFP_KERNEL);
		if (!q_vector->xsksq)
			goto error;
	}

	return 0;

error:
	idpf_vport_intr_rel(vport);

	return -ENOMEM;
}

/**
 * idpf_vport_intr_init - Setup all vectors for the given vport
 * @vport: virtual port
 *
 * Returns 0 on success or negative on failure
 */
int idpf_vport_intr_init(struct idpf_vport *vport)
{
	int err;

	err = idpf_vport_intr_init_vec_idx(vport);
	if (err)
		return err;

	idpf_vport_intr_map_vector_to_qs(vport);
	idpf_vport_intr_napi_add_all(vport);

	err = vport->adapter->dev_ops.reg_ops.intr_reg_init(vport);
	if (err)
		goto unroll_vectors_alloc;

	err = idpf_vport_intr_req_irq(vport);
	if (err)
		goto unroll_vectors_alloc;

	return 0;

unroll_vectors_alloc:
	idpf_vport_intr_napi_del_all(vport);

	return err;
}

void idpf_vport_intr_ena(struct idpf_vport *vport)
{
	idpf_vport_intr_napi_ena_all(vport);
	idpf_vport_intr_ena_irq_all(vport);
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
