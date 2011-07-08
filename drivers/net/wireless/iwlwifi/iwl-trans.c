/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#include "iwl-dev.h"
#include "iwl-trans.h"
#include "iwl-core.h"
#include "iwl-helpers.h"
/*TODO remove uneeded includes when the transport layer tx_free will be here */
#include "iwl-agn.h"

static int iwl_trans_rx_alloc(struct iwl_priv *priv)
{
	struct iwl_rx_queue *rxq = &priv->rxq;
	struct device *dev = priv->bus.dev;

	memset(&priv->rxq, 0, sizeof(priv->rxq));

	spin_lock_init(&rxq->lock);
	INIT_LIST_HEAD(&rxq->rx_free);
	INIT_LIST_HEAD(&rxq->rx_used);

	if (WARN_ON(rxq->bd || rxq->rb_stts))
		return -EINVAL;

	/* Allocate the circular buffer of Read Buffer Descriptors (RBDs) */
	rxq->bd = dma_alloc_coherent(dev, sizeof(__le32) * RX_QUEUE_SIZE,
				     &rxq->bd_dma, GFP_KERNEL);
	if (!rxq->bd)
		goto err_bd;
	memset(rxq->bd, 0, sizeof(__le32) * RX_QUEUE_SIZE);

	/*Allocate the driver's pointer to receive buffer status */
	rxq->rb_stts = dma_alloc_coherent(dev, sizeof(*rxq->rb_stts),
					  &rxq->rb_stts_dma, GFP_KERNEL);
	if (!rxq->rb_stts)
		goto err_rb_stts;
	memset(rxq->rb_stts, 0, sizeof(*rxq->rb_stts));

	return 0;

err_rb_stts:
	dma_free_coherent(dev, sizeof(__le32) * RX_QUEUE_SIZE,
			rxq->bd, rxq->bd_dma);
	memset(&rxq->bd_dma, 0, sizeof(rxq->bd_dma));
	rxq->bd = NULL;
err_bd:
	return -ENOMEM;
}

static void iwl_trans_rxq_free_rx_bufs(struct iwl_priv *priv)
{
	struct iwl_rx_queue *rxq = &priv->rxq;
	int i;

	/* Fill the rx_used queue with _all_ of the Rx buffers */
	for (i = 0; i < RX_FREE_BUFFERS + RX_QUEUE_SIZE; i++) {
		/* In the reset function, these buffers may have been allocated
		 * to an SKB, so we need to unmap and free potential storage */
		if (rxq->pool[i].page != NULL) {
			dma_unmap_page(priv->bus.dev, rxq->pool[i].page_dma,
				PAGE_SIZE << priv->hw_params.rx_page_order,
				DMA_FROM_DEVICE);
			__iwl_free_pages(priv, rxq->pool[i].page);
			rxq->pool[i].page = NULL;
		}
		list_add_tail(&rxq->pool[i].list, &rxq->rx_used);
	}
}

static int iwl_trans_rx_init(struct iwl_priv *priv)
{
	struct iwl_rx_queue *rxq = &priv->rxq;
	int i, err;
	unsigned long flags;

	if (!rxq->bd) {
		err = iwl_trans_rx_alloc(priv);
		if (err)
			return err;
	}

	spin_lock_irqsave(&rxq->lock, flags);
	INIT_LIST_HEAD(&rxq->rx_free);
	INIT_LIST_HEAD(&rxq->rx_used);

	iwl_trans_rxq_free_rx_bufs(priv);

	for (i = 0; i < RX_QUEUE_SIZE; i++)
		rxq->queue[i] = NULL;

	/* Set us so that we have processed and used all buffers, but have
	 * not restocked the Rx queue with fresh buffers */
	rxq->read = rxq->write = 0;
	rxq->write_actual = 0;
	rxq->free_count = 0;
	spin_unlock_irqrestore(&rxq->lock, flags);

	return 0;
}

static void iwl_trans_rx_free(struct iwl_priv *priv)
{
	struct iwl_rx_queue *rxq = &priv->rxq;
	unsigned long flags;

	/*if rxq->bd is NULL, it means that nothing has been allocated,
	 * exit now */
	if (!rxq->bd) {
		IWL_DEBUG_INFO(priv, "Free NULL rx context\n");
		return;
	}

	spin_lock_irqsave(&rxq->lock, flags);
	iwl_trans_rxq_free_rx_bufs(priv);
	spin_unlock_irqrestore(&rxq->lock, flags);

	dma_free_coherent(priv->bus.dev, sizeof(__le32) * RX_QUEUE_SIZE,
			  rxq->bd, rxq->bd_dma);
	memset(&rxq->bd_dma, 0, sizeof(rxq->bd_dma));
	rxq->bd = NULL;

	if (rxq->rb_stts)
		dma_free_coherent(priv->bus.dev,
				  sizeof(struct iwl_rb_status),
				  rxq->rb_stts, rxq->rb_stts_dma);
	else
		IWL_DEBUG_INFO(priv, "Free rxq->rb_stts which is NULL\n");
	memset(&rxq->rb_stts_dma, 0, sizeof(rxq->rb_stts_dma));
	rxq->rb_stts = NULL;
}

static int iwl_trans_rx_stop(struct iwl_priv *priv)
{

	/* stop Rx DMA */
	iwl_write_direct32(priv, FH_MEM_RCSR_CHNL0_CONFIG_REG, 0);
	return iwl_poll_direct_bit(priv, FH_MEM_RSSR_RX_STATUS_REG,
			    FH_RSSR_CHNL0_RX_STATUS_CHNL_IDLE, 1000);
}

static inline int iwlagn_alloc_dma_ptr(struct iwl_priv *priv,
				    struct iwl_dma_ptr *ptr, size_t size)
{
	if (WARN_ON(ptr->addr))
		return -EINVAL;

	ptr->addr = dma_alloc_coherent(priv->bus.dev, size,
				       &ptr->dma, GFP_KERNEL);
	if (!ptr->addr)
		return -ENOMEM;
	ptr->size = size;
	return 0;
}

static inline void iwlagn_free_dma_ptr(struct iwl_priv *priv,
				    struct iwl_dma_ptr *ptr)
{
	if (unlikely(!ptr->addr))
		return;

	dma_free_coherent(priv->bus.dev, ptr->size, ptr->addr, ptr->dma);
	memset(ptr, 0, sizeof(*ptr));
}

static int iwl_trans_txq_alloc(struct iwl_priv *priv, struct iwl_tx_queue *txq,
		      int slots_num, u32 txq_id)
{
	size_t tfd_sz = priv->hw_params.tfd_size * TFD_QUEUE_SIZE_MAX;
	int i;

	if (WARN_ON(txq->meta || txq->cmd || txq->txb || txq->tfds))
		return -EINVAL;

	txq->q.n_window = slots_num;

	txq->meta = kzalloc(sizeof(txq->meta[0]) * slots_num,
			    GFP_KERNEL);
	txq->cmd = kzalloc(sizeof(txq->cmd[0]) * slots_num,
			   GFP_KERNEL);

	if (!txq->meta || !txq->cmd)
		goto error;

	for (i = 0; i < slots_num; i++) {
		txq->cmd[i] = kmalloc(sizeof(struct iwl_device_cmd),
					GFP_KERNEL);
		if (!txq->cmd[i])
			goto error;
	}

	/* Alloc driver data array and TFD circular buffer */
	/* Driver private data, only for Tx (not command) queues,
	 * not shared with device. */
	if (txq_id != priv->cmd_queue) {
		txq->txb = kzalloc(sizeof(txq->txb[0]) *
				   TFD_QUEUE_SIZE_MAX, GFP_KERNEL);
		if (!txq->txb) {
			IWL_ERR(priv, "kmalloc for auxiliary BD "
				  "structures failed\n");
			goto error;
		}
	} else {
		txq->txb = NULL;
	}

	/* Circular buffer of transmit frame descriptors (TFDs),
	 * shared with device */
	txq->tfds = dma_alloc_coherent(priv->bus.dev, tfd_sz, &txq->q.dma_addr,
				       GFP_KERNEL);
	if (!txq->tfds) {
		IWL_ERR(priv, "dma_alloc_coherent(%zd) failed\n", tfd_sz);
		goto error;
	}
	txq->q.id = txq_id;

	return 0;
error:
	kfree(txq->txb);
	txq->txb = NULL;
	/* since txq->cmd has been zeroed,
	 * all non allocated cmd[i] will be NULL */
	if (txq->cmd)
		for (i = 0; i < slots_num; i++)
			kfree(txq->cmd[i]);
	kfree(txq->meta);
	kfree(txq->cmd);
	txq->meta = NULL;
	txq->cmd = NULL;

	return -ENOMEM;

}

static int iwl_trans_txq_init(struct iwl_priv *priv, struct iwl_tx_queue *txq,
		      int slots_num, u32 txq_id)
{
	int ret;

	txq->need_update = 0;
	memset(txq->meta, 0, sizeof(txq->meta[0]) * slots_num);

	/*
	 * For the default queues 0-3, set up the swq_id
	 * already -- all others need to get one later
	 * (if they need one at all).
	 */
	if (txq_id < 4)
		iwl_set_swq_id(txq, txq_id, txq_id);

	/* TFD_QUEUE_SIZE_MAX must be power-of-two size, otherwise
	 * iwl_queue_inc_wrap and iwl_queue_dec_wrap are broken. */
	BUILD_BUG_ON(TFD_QUEUE_SIZE_MAX & (TFD_QUEUE_SIZE_MAX - 1));

	/* Initialize queue's high/low-water marks, and head/tail indexes */
	ret = iwl_queue_init(priv, &txq->q, TFD_QUEUE_SIZE_MAX, slots_num,
			txq_id);
	if (ret)
		return ret;

	/*
	 * Tell nic where to find circular buffer of Tx Frame Descriptors for
	 * given Tx queue, and enable the DMA channel used for that queue.
	 * Circular buffer (TFD queue in DRAM) physical base address */
	iwl_write_direct32(priv, FH_MEM_CBBC_QUEUE(txq_id),
			     txq->q.dma_addr >> 8);

	return 0;
}

/**
 * iwl_tx_queue_unmap -  Unmap any remaining DMA mappings and free skb's
 */
static void iwl_tx_queue_unmap(struct iwl_priv *priv, int txq_id)
{
	struct iwl_tx_queue *txq = &priv->txq[txq_id];
	struct iwl_queue *q = &txq->q;

	if (!q->n_bd)
		return;

	while (q->write_ptr != q->read_ptr) {
		/* The read_ptr needs to bound by q->n_window */
		iwlagn_txq_free_tfd(priv, txq, get_cmd_index(q, q->read_ptr));
		q->read_ptr = iwl_queue_inc_wrap(q->read_ptr, q->n_bd);
	}
}

/**
 * iwl_tx_queue_free - Deallocate DMA queue.
 * @txq: Transmit queue to deallocate.
 *
 * Empty queue by removing and destroying all BD's.
 * Free all buffers.
 * 0-fill, but do not free "txq" descriptor structure.
 */
static void iwl_tx_queue_free(struct iwl_priv *priv, int txq_id)
{
	struct iwl_tx_queue *txq = &priv->txq[txq_id];
	struct device *dev = priv->bus.dev;
	int i;
	if (WARN_ON(!txq))
		return;

	iwl_tx_queue_unmap(priv, txq_id);

	/* De-alloc array of command/tx buffers */
	for (i = 0; i < txq->q.n_window; i++)
		kfree(txq->cmd[i]);

	/* De-alloc circular buffer of TFDs */
	if (txq->q.n_bd) {
		dma_free_coherent(dev, priv->hw_params.tfd_size *
				  txq->q.n_bd, txq->tfds, txq->q.dma_addr);
		memset(&txq->q.dma_addr, 0, sizeof(txq->q.dma_addr));
	}

	/* De-alloc array of per-TFD driver data */
	kfree(txq->txb);
	txq->txb = NULL;

	/* deallocate arrays */
	kfree(txq->cmd);
	kfree(txq->meta);
	txq->cmd = NULL;
	txq->meta = NULL;

	/* 0-fill queue descriptor structure */
	memset(txq, 0, sizeof(*txq));
}

/**
 * iwl_trans_tx_free - Free TXQ Context
 *
 * Destroy all TX DMA queues and structures
 */
static void iwl_trans_tx_free(struct iwl_priv *priv)
{
	int txq_id;

	/* Tx queues */
	if (priv->txq) {
		for (txq_id = 0; txq_id < priv->hw_params.max_txq_num; txq_id++)
			iwl_tx_queue_free(priv, txq_id);
	}

	kfree(priv->txq);
	priv->txq = NULL;

	iwlagn_free_dma_ptr(priv, &priv->kw);

	iwlagn_free_dma_ptr(priv, &priv->scd_bc_tbls);
}

/**
 * iwl_trans_tx_alloc - allocate TX context
 * Allocate all Tx DMA structures and initialize them
 *
 * @param priv
 * @return error code
 */
static int iwl_trans_tx_alloc(struct iwl_priv *priv)
{
	int ret;
	int txq_id, slots_num;

	/*It is not allowed to alloc twice, so warn when this happens.
	 * We cannot rely on the previous allocation, so free and fail */
	if (WARN_ON(priv->txq)) {
		ret = -EINVAL;
		goto error;
	}

	ret = iwlagn_alloc_dma_ptr(priv, &priv->scd_bc_tbls,
				priv->hw_params.scd_bc_tbls_size);
	if (ret) {
		IWL_ERR(priv, "Scheduler BC Table allocation failed\n");
		goto error;
	}

	/* Alloc keep-warm buffer */
	ret = iwlagn_alloc_dma_ptr(priv, &priv->kw, IWL_KW_SIZE);
	if (ret) {
		IWL_ERR(priv, "Keep Warm allocation failed\n");
		goto error;
	}

	priv->txq = kzalloc(sizeof(struct iwl_tx_queue) *
			priv->cfg->base_params->num_of_queues, GFP_KERNEL);
	if (!priv->txq) {
		IWL_ERR(priv, "Not enough memory for txq\n");
		ret = ENOMEM;
		goto error;
	}

	/* Alloc and init all Tx queues, including the command queue (#4/#9) */
	for (txq_id = 0; txq_id < priv->hw_params.max_txq_num; txq_id++) {
		slots_num = (txq_id == priv->cmd_queue) ?
					TFD_CMD_SLOTS : TFD_TX_CMD_SLOTS;
		ret = iwl_trans_txq_alloc(priv, &priv->txq[txq_id], slots_num,
				       txq_id);
		if (ret) {
			IWL_ERR(priv, "Tx %d queue alloc failed\n", txq_id);
			goto error;
		}
	}

	return 0;

error:
	priv->trans.ops->tx_free(priv);

	return ret;
}
static int iwl_trans_tx_init(struct iwl_priv *priv)
{
	int ret;
	int txq_id, slots_num;
	unsigned long flags;
	bool alloc = false;

	if (!priv->txq) {
		ret = iwl_trans_tx_alloc(priv);
		if (ret)
			goto error;
		alloc = true;
	}

	spin_lock_irqsave(&priv->lock, flags);

	/* Turn off all Tx DMA fifos */
	iwl_write_prph(priv, IWLAGN_SCD_TXFACT, 0);

	/* Tell NIC where to find the "keep warm" buffer */
	iwl_write_direct32(priv, FH_KW_MEM_ADDR_REG, priv->kw.dma >> 4);

	spin_unlock_irqrestore(&priv->lock, flags);

	/* Alloc and init all Tx queues, including the command queue (#4/#9) */
	for (txq_id = 0; txq_id < priv->hw_params.max_txq_num; txq_id++) {
		slots_num = (txq_id == priv->cmd_queue) ?
					TFD_CMD_SLOTS : TFD_TX_CMD_SLOTS;
		ret = iwl_trans_txq_init(priv, &priv->txq[txq_id], slots_num,
				       txq_id);
		if (ret) {
			IWL_ERR(priv, "Tx %d queue init failed\n", txq_id);
			goto error;
		}
	}

	return 0;
error:
	/*Upon error, free only if we allocated something */
	if (alloc)
		priv->trans.ops->tx_free(priv);
	return ret;
}

/**
 * iwlagn_txq_ctx_stop - Stop all Tx DMA channels
 */
static int iwl_trans_tx_stop(struct iwl_priv *priv)
{
	int ch, txq_id;
	unsigned long flags;

	/* Turn off all Tx DMA fifos */
	spin_lock_irqsave(&priv->lock, flags);

	iwlagn_txq_set_sched(priv, 0);

	/* Stop each Tx DMA channel, and wait for it to be idle */
	for (ch = 0; ch < priv->hw_params.dma_chnl_num; ch++) {
		iwl_write_direct32(priv, FH_TCSR_CHNL_TX_CONFIG_REG(ch), 0x0);
		if (iwl_poll_direct_bit(priv, FH_TSSR_TX_STATUS_REG,
				    FH_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE(ch),
				    1000))
			IWL_ERR(priv, "Failing on timeout while stopping"
			    " DMA channel %d [0x%08x]", ch,
			    iwl_read_direct32(priv, FH_TSSR_TX_STATUS_REG));
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	if (!priv->txq) {
		IWL_WARN(priv, "Stopping tx queues that aren't allocated...");
		return 0;
	}

	/* Unmap DMA from host system and free skb's */
	for (txq_id = 0; txq_id < priv->hw_params.max_txq_num; txq_id++)
		iwl_tx_queue_unmap(priv, txq_id);

	return 0;
}

static const struct iwl_trans_ops trans_ops = {
	.rx_init = iwl_trans_rx_init,
	.rx_stop = iwl_trans_rx_stop,
	.rx_free = iwl_trans_rx_free,

	.tx_init = iwl_trans_tx_init,
	.tx_stop = iwl_trans_tx_stop,
	.tx_free = iwl_trans_tx_free,
};

void iwl_trans_register(struct iwl_trans *trans)
{
	trans->ops = &trans_ops;
}
