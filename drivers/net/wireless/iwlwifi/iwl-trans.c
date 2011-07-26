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
#include "iwl-trans-int-pcie.h"
/*TODO remove uneeded includes when the transport layer tx_free will be here */
#include "iwl-agn.h"
#include "iwl-core.h"

static int iwl_trans_rx_alloc(struct iwl_priv *priv)
{
	struct iwl_rx_queue *rxq = &priv->rxq;
	struct device *dev = priv->bus->dev;

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
			dma_unmap_page(priv->bus->dev, rxq->pool[i].page_dma,
				PAGE_SIZE << priv->hw_params.rx_page_order,
				DMA_FROM_DEVICE);
			__iwl_free_pages(priv, rxq->pool[i].page);
			rxq->pool[i].page = NULL;
		}
		list_add_tail(&rxq->pool[i].list, &rxq->rx_used);
	}
}

static void iwl_trans_rx_hw_init(struct iwl_priv *priv,
				 struct iwl_rx_queue *rxq)
{
	u32 rb_size;
	const u32 rfdnlog = RX_QUEUE_SIZE_LOG; /* 256 RBDs */
	u32 rb_timeout = 0; /* FIXME: RX_RB_TIMEOUT for all devices? */

	rb_timeout = RX_RB_TIMEOUT;

	if (iwlagn_mod_params.amsdu_size_8K)
		rb_size = FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_8K;
	else
		rb_size = FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_4K;

	/* Stop Rx DMA */
	iwl_write_direct32(priv, FH_MEM_RCSR_CHNL0_CONFIG_REG, 0);

	/* Reset driver's Rx queue write index */
	iwl_write_direct32(priv, FH_RSCSR_CHNL0_RBDCB_WPTR_REG, 0);

	/* Tell device where to find RBD circular buffer in DRAM */
	iwl_write_direct32(priv, FH_RSCSR_CHNL0_RBDCB_BASE_REG,
			   (u32)(rxq->bd_dma >> 8));

	/* Tell device where in DRAM to update its Rx status */
	iwl_write_direct32(priv, FH_RSCSR_CHNL0_STTS_WPTR_REG,
			   rxq->rb_stts_dma >> 4);

	/* Enable Rx DMA
	 * FH_RCSR_CHNL0_RX_IGNORE_RXF_EMPTY is set because of HW bug in
	 *      the credit mechanism in 5000 HW RX FIFO
	 * Direct rx interrupts to hosts
	 * Rx buffer size 4 or 8k
	 * RB timeout 0x10
	 * 256 RBDs
	 */
	iwl_write_direct32(priv, FH_MEM_RCSR_CHNL0_CONFIG_REG,
			   FH_RCSR_RX_CONFIG_CHNL_EN_ENABLE_VAL |
			   FH_RCSR_CHNL0_RX_IGNORE_RXF_EMPTY |
			   FH_RCSR_CHNL0_RX_CONFIG_IRQ_DEST_INT_HOST_VAL |
			   FH_RCSR_CHNL0_RX_CONFIG_SINGLE_FRAME_MSK |
			   rb_size|
			   (rb_timeout << FH_RCSR_RX_CONFIG_REG_IRQ_RBTH_POS)|
			   (rfdnlog << FH_RCSR_RX_CONFIG_RBDCB_SIZE_POS));

	/* Set interrupt coalescing timer to default (2048 usecs) */
	iwl_write8(priv, CSR_INT_COALESCING, IWL_HOST_INT_TIMEOUT_DEF);
}

static int iwl_rx_init(struct iwl_priv *priv)
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

	iwlagn_rx_replenish(priv);

	iwl_trans_rx_hw_init(priv, rxq);

	spin_lock_irqsave(&priv->lock, flags);
	rxq->need_update = 1;
	iwl_rx_queue_update_write_ptr(priv, rxq);
	spin_unlock_irqrestore(&priv->lock, flags);

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

	dma_free_coherent(priv->bus->dev, sizeof(__le32) * RX_QUEUE_SIZE,
			  rxq->bd, rxq->bd_dma);
	memset(&rxq->bd_dma, 0, sizeof(rxq->bd_dma));
	rxq->bd = NULL;

	if (rxq->rb_stts)
		dma_free_coherent(priv->bus->dev,
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

	ptr->addr = dma_alloc_coherent(priv->bus->dev, size,
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

	dma_free_coherent(priv->bus->dev, ptr->size, ptr->addr, ptr->dma);
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
	txq->tfds = dma_alloc_coherent(priv->bus->dev, tfd_sz, &txq->q.dma_addr,
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
	struct device *dev = priv->bus->dev;
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
	trans_tx_free(&priv->trans);

	return ret;
}
static int iwl_tx_init(struct iwl_priv *priv)
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
	iwl_write_prph(priv, SCD_TXFACT, 0);

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
		trans_tx_free(&priv->trans);
	return ret;
}

static void iwl_set_pwr_vmain(struct iwl_priv *priv)
{
/*
 * (for documentation purposes)
 * to set power to V_AUX, do:

		if (pci_pme_capable(priv->pci_dev, PCI_D3cold))
			iwl_set_bits_mask_prph(priv, APMG_PS_CTRL_REG,
					       APMG_PS_CTRL_VAL_PWR_SRC_VAUX,
					       ~APMG_PS_CTRL_MSK_PWR_SRC);
 */

	iwl_set_bits_mask_prph(priv, APMG_PS_CTRL_REG,
			       APMG_PS_CTRL_VAL_PWR_SRC_VMAIN,
			       ~APMG_PS_CTRL_MSK_PWR_SRC);
}

static int iwl_nic_init(struct iwl_priv *priv)
{
	unsigned long flags;

	/* nic_init */
	spin_lock_irqsave(&priv->lock, flags);
	iwl_apm_init(priv);

	/* Set interrupt coalescing calibration timer to default (512 usecs) */
	iwl_write8(priv, CSR_INT_COALESCING, IWL_HOST_INT_CALIB_TIMEOUT_DEF);

	spin_unlock_irqrestore(&priv->lock, flags);

	iwl_set_pwr_vmain(priv);

	priv->cfg->lib->nic_config(priv);

	/* Allocate the RX queue, or reset if it is already allocated */
	iwl_rx_init(priv);

	/* Allocate or reset and init all Tx and Command queues */
	if (iwl_tx_init(priv))
		return -ENOMEM;

	if (priv->cfg->base_params->shadow_reg_enable) {
		/* enable shadow regs in HW */
		iwl_set_bit(priv, CSR_MAC_SHADOW_REG_CTRL,
			0x800FFFFF);
	}

	set_bit(STATUS_INIT, &priv->status);

	return 0;
}

#define HW_READY_TIMEOUT (50)

/* Note: returns poll_bit return value, which is >= 0 if success */
static int iwl_set_hw_ready(struct iwl_priv *priv)
{
	int ret;

	iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
		CSR_HW_IF_CONFIG_REG_BIT_NIC_READY);

	/* See if we got it */
	ret = iwl_poll_bit(priv, CSR_HW_IF_CONFIG_REG,
				CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
				CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
				HW_READY_TIMEOUT);

	IWL_DEBUG_INFO(priv, "hardware%s ready\n", ret < 0 ? " not" : "");
	return ret;
}

/* Note: returns standard 0/-ERROR code */
static int iwl_trans_prepare_card_hw(struct iwl_priv *priv)
{
	int ret;

	IWL_DEBUG_INFO(priv, "iwl_trans_prepare_card_hw enter\n");

	ret = iwl_set_hw_ready(priv);
	if (ret >= 0)
		return 0;

	/* If HW is not ready, prepare the conditions to check again */
	iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
			CSR_HW_IF_CONFIG_REG_PREPARE);

	ret = iwl_poll_bit(priv, CSR_HW_IF_CONFIG_REG,
			~CSR_HW_IF_CONFIG_REG_BIT_NIC_PREPARE_DONE,
			CSR_HW_IF_CONFIG_REG_BIT_NIC_PREPARE_DONE, 150000);

	if (ret < 0)
		return ret;

	/* HW should be ready by now, check again. */
	ret = iwl_set_hw_ready(priv);
	if (ret >= 0)
		return 0;
	return ret;
}

static int iwl_trans_start_device(struct iwl_priv *priv)
{
	int ret;

	priv->ucode_owner = IWL_OWNERSHIP_DRIVER;

	if ((priv->cfg->sku & EEPROM_SKU_CAP_AMT_ENABLE) &&
	     iwl_trans_prepare_card_hw(priv)) {
		IWL_WARN(priv, "Exit HW not ready\n");
		return -EIO;
	}

	/* If platform's RF_KILL switch is NOT set to KILL */
	if (iwl_read32(priv, CSR_GP_CNTRL) &
			CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW)
		clear_bit(STATUS_RF_KILL_HW, &priv->status);
	else
		set_bit(STATUS_RF_KILL_HW, &priv->status);

	if (iwl_is_rfkill(priv)) {
		wiphy_rfkill_set_hw_state(priv->hw->wiphy, true);
		iwl_enable_interrupts(priv);
		return -ERFKILL;
	}

	iwl_write32(priv, CSR_INT, 0xFFFFFFFF);

	ret = iwl_nic_init(priv);
	if (ret) {
		IWL_ERR(priv, "Unable to init nic\n");
		return ret;
	}

	/* make sure rfkill handshake bits are cleared */
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR,
		    CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);

	/* clear (again), then enable host interrupts */
	iwl_write32(priv, CSR_INT, 0xFFFFFFFF);
	iwl_enable_interrupts(priv);

	/* really make sure rfkill handshake bits are cleared */
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);
	iwl_write32(priv, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);

	return 0;
}

/*
 * Activate/Deactivate Tx DMA/FIFO channels according tx fifos mask
 * must be called under priv->lock and mac access
 */
static void iwl_trans_txq_set_sched(struct iwl_priv *priv, u32 mask)
{
	iwl_write_prph(priv, SCD_TXFACT, mask);
}

#define IWL_AC_UNSET -1

struct queue_to_fifo_ac {
	s8 fifo, ac;
};

static const struct queue_to_fifo_ac iwlagn_default_queue_to_tx_fifo[] = {
	{ IWL_TX_FIFO_VO, IEEE80211_AC_VO, },
	{ IWL_TX_FIFO_VI, IEEE80211_AC_VI, },
	{ IWL_TX_FIFO_BE, IEEE80211_AC_BE, },
	{ IWL_TX_FIFO_BK, IEEE80211_AC_BK, },
	{ IWLAGN_CMD_FIFO_NUM, IWL_AC_UNSET, },
	{ IWL_TX_FIFO_UNUSED, IWL_AC_UNSET, },
	{ IWL_TX_FIFO_UNUSED, IWL_AC_UNSET, },
	{ IWL_TX_FIFO_UNUSED, IWL_AC_UNSET, },
	{ IWL_TX_FIFO_UNUSED, IWL_AC_UNSET, },
	{ IWL_TX_FIFO_UNUSED, IWL_AC_UNSET, },
};

static const struct queue_to_fifo_ac iwlagn_ipan_queue_to_tx_fifo[] = {
	{ IWL_TX_FIFO_VO, IEEE80211_AC_VO, },
	{ IWL_TX_FIFO_VI, IEEE80211_AC_VI, },
	{ IWL_TX_FIFO_BE, IEEE80211_AC_BE, },
	{ IWL_TX_FIFO_BK, IEEE80211_AC_BK, },
	{ IWL_TX_FIFO_BK_IPAN, IEEE80211_AC_BK, },
	{ IWL_TX_FIFO_BE_IPAN, IEEE80211_AC_BE, },
	{ IWL_TX_FIFO_VI_IPAN, IEEE80211_AC_VI, },
	{ IWL_TX_FIFO_VO_IPAN, IEEE80211_AC_VO, },
	{ IWL_TX_FIFO_BE_IPAN, 2, },
	{ IWLAGN_CMD_FIFO_NUM, IWL_AC_UNSET, },
};
static void iwl_trans_tx_start(struct iwl_priv *priv)
{
	const struct queue_to_fifo_ac *queue_to_fifo;
	struct iwl_rxon_context *ctx;
	u32 a;
	unsigned long flags;
	int i, chan;
	u32 reg_val;

	spin_lock_irqsave(&priv->lock, flags);

	priv->scd_base_addr = iwl_read_prph(priv, SCD_SRAM_BASE_ADDR);
	a = priv->scd_base_addr + SCD_CONTEXT_MEM_LOWER_BOUND;
	/* reset conext data memory */
	for (; a < priv->scd_base_addr + SCD_CONTEXT_MEM_UPPER_BOUND;
		a += 4)
		iwl_write_targ_mem(priv, a, 0);
	/* reset tx status memory */
	for (; a < priv->scd_base_addr + SCD_TX_STTS_MEM_UPPER_BOUND;
		a += 4)
		iwl_write_targ_mem(priv, a, 0);
	for (; a < priv->scd_base_addr +
	       SCD_TRANS_TBL_OFFSET_QUEUE(priv->hw_params.max_txq_num); a += 4)
		iwl_write_targ_mem(priv, a, 0);

	iwl_write_prph(priv, SCD_DRAM_BASE_ADDR,
		       priv->scd_bc_tbls.dma >> 10);

	/* Enable DMA channel */
	for (chan = 0; chan < FH_TCSR_CHNL_NUM ; chan++)
		iwl_write_direct32(priv, FH_TCSR_CHNL_TX_CONFIG_REG(chan),
				FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE |
				FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE);

	/* Update FH chicken bits */
	reg_val = iwl_read_direct32(priv, FH_TX_CHICKEN_BITS_REG);
	iwl_write_direct32(priv, FH_TX_CHICKEN_BITS_REG,
			   reg_val | FH_TX_CHICKEN_BITS_SCD_AUTO_RETRY_EN);

	iwl_write_prph(priv, SCD_QUEUECHAIN_SEL,
		SCD_QUEUECHAIN_SEL_ALL(priv));
	iwl_write_prph(priv, SCD_AGGR_SEL, 0);

	/* initiate the queues */
	for (i = 0; i < priv->hw_params.max_txq_num; i++) {
		iwl_write_prph(priv, SCD_QUEUE_RDPTR(i), 0);
		iwl_write_direct32(priv, HBUS_TARG_WRPTR, 0 | (i << 8));
		iwl_write_targ_mem(priv, priv->scd_base_addr +
				SCD_CONTEXT_QUEUE_OFFSET(i), 0);
		iwl_write_targ_mem(priv, priv->scd_base_addr +
				SCD_CONTEXT_QUEUE_OFFSET(i) +
				sizeof(u32),
				((SCD_WIN_SIZE <<
				SCD_QUEUE_CTX_REG2_WIN_SIZE_POS) &
				SCD_QUEUE_CTX_REG2_WIN_SIZE_MSK) |
				((SCD_FRAME_LIMIT <<
				SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS) &
				SCD_QUEUE_CTX_REG2_FRAME_LIMIT_MSK));
	}

	iwl_write_prph(priv, SCD_INTERRUPT_MASK,
			IWL_MASK(0, priv->hw_params.max_txq_num));

	/* Activate all Tx DMA/FIFO channels */
	iwl_trans_txq_set_sched(priv, IWL_MASK(0, 7));

	/* map queues to FIFOs */
	if (priv->valid_contexts != BIT(IWL_RXON_CTX_BSS))
		queue_to_fifo = iwlagn_ipan_queue_to_tx_fifo;
	else
		queue_to_fifo = iwlagn_default_queue_to_tx_fifo;

	iwl_trans_set_wr_ptrs(priv, priv->cmd_queue, 0);

	/* make sure all queue are not stopped */
	memset(&priv->queue_stopped[0], 0, sizeof(priv->queue_stopped));
	for (i = 0; i < 4; i++)
		atomic_set(&priv->queue_stop_count[i], 0);
	for_each_context(priv, ctx)
		ctx->last_tx_rejected = false;

	/* reset to 0 to enable all the queue first */
	priv->txq_ctx_active_msk = 0;

	BUILD_BUG_ON(ARRAY_SIZE(iwlagn_default_queue_to_tx_fifo) != 10);
	BUILD_BUG_ON(ARRAY_SIZE(iwlagn_ipan_queue_to_tx_fifo) != 10);

	for (i = 0; i < 10; i++) {
		int fifo = queue_to_fifo[i].fifo;
		int ac = queue_to_fifo[i].ac;

		iwl_txq_ctx_activate(priv, i);

		if (fifo == IWL_TX_FIFO_UNUSED)
			continue;

		if (ac != IWL_AC_UNSET)
			iwl_set_swq_id(&priv->txq[i], ac, i);
		iwl_trans_tx_queue_set_status(priv, &priv->txq[i], fifo, 0);
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	/* Enable L1-Active */
	iwl_clear_bits_prph(priv, APMG_PCIDEV_STT_REG,
			  APMG_PCIDEV_STT_VAL_L1_ACT_DIS);
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

	iwl_trans_txq_set_sched(priv, 0);

	/* Stop each Tx DMA channel, and wait for it to be idle */
	for (ch = 0; ch < FH_TCSR_CHNL_NUM; ch++) {
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

static void iwl_trans_stop_device(struct iwl_priv *priv)
{
	unsigned long flags;

	/* stop and reset the on-board processor */
	iwl_write32(priv, CSR_RESET, CSR_RESET_REG_FLAG_NEVO_RESET);

	/* tell the device to stop sending interrupts */
	spin_lock_irqsave(&priv->lock, flags);
	iwl_disable_interrupts(priv);
	spin_unlock_irqrestore(&priv->lock, flags);
	trans_sync_irq(&priv->trans);

	/* device going down, Stop using ICT table */
	iwl_disable_ict(priv);

	/*
	 * If a HW restart happens during firmware loading,
	 * then the firmware loading might call this function
	 * and later it might be called again due to the
	 * restart. So don't process again if the device is
	 * already dead.
	 */
	if (test_bit(STATUS_DEVICE_ENABLED, &priv->status)) {
		iwl_trans_tx_stop(priv);
		iwl_trans_rx_stop(priv);

		/* Power-down device's busmaster DMA clocks */
		iwl_write_prph(priv, APMG_CLK_DIS_REG,
			       APMG_CLK_VAL_DMA_CLK_RQT);
		udelay(5);
	}

	/* Make sure (redundant) we've released our request to stay awake */
	iwl_clear_bit(priv, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);

	/* Stop the device, and put it in low power state */
	iwl_apm_stop(priv);
}

static struct iwl_tx_cmd *iwl_trans_get_tx_cmd(struct iwl_priv *priv,
						int txq_id)
{
	struct iwl_tx_queue *txq = &priv->txq[txq_id];
	struct iwl_queue *q = &txq->q;
	struct iwl_device_cmd *dev_cmd;

	if (unlikely(iwl_queue_space(q) < q->high_mark))
		return NULL;

	/*
	 * Set up the Tx-command (not MAC!) header.
	 * Store the chosen Tx queue and TFD index within the sequence field;
	 * after Tx, uCode's Tx response will return this value so driver can
	 * locate the frame within the tx queue and do post-tx processing.
	 */
	dev_cmd = txq->cmd[q->write_ptr];
	memset(dev_cmd, 0, sizeof(*dev_cmd));
	dev_cmd->hdr.cmd = REPLY_TX;
	dev_cmd->hdr.sequence = cpu_to_le16((u16)(QUEUE_TO_SEQ(txq_id) |
				INDEX_TO_SEQ(q->write_ptr)));
	return &dev_cmd->cmd.tx;
}

static int iwl_trans_tx(struct iwl_priv *priv, struct sk_buff *skb,
		struct iwl_tx_cmd *tx_cmd, int txq_id, __le16 fc, bool ampdu,
		struct iwl_rxon_context *ctx)
{
	struct iwl_tx_queue *txq = &priv->txq[txq_id];
	struct iwl_queue *q = &txq->q;
	struct iwl_device_cmd *dev_cmd = txq->cmd[q->write_ptr];
	struct iwl_cmd_meta *out_meta;

	dma_addr_t phys_addr = 0;
	dma_addr_t txcmd_phys;
	dma_addr_t scratch_phys;
	u16 len, firstlen, secondlen;
	u8 wait_write_ptr = 0;
	u8 hdr_len = ieee80211_hdrlen(fc);

	/* Set up driver data for this TFD */
	memset(&(txq->txb[q->write_ptr]), 0, sizeof(struct iwl_tx_info));
	txq->txb[q->write_ptr].skb = skb;
	txq->txb[q->write_ptr].ctx = ctx;

	/* Set up first empty entry in queue's array of Tx/cmd buffers */
	out_meta = &txq->meta[q->write_ptr];

	/*
	 * Use the first empty entry in this queue's command buffer array
	 * to contain the Tx command and MAC header concatenated together
	 * (payload data will be in another buffer).
	 * Size of this varies, due to varying MAC header length.
	 * If end is not dword aligned, we'll have 2 extra bytes at the end
	 * of the MAC header (device reads on dword boundaries).
	 * We'll tell device about this padding later.
	 */
	len = sizeof(struct iwl_tx_cmd) +
		sizeof(struct iwl_cmd_header) + hdr_len;
	firstlen = (len + 3) & ~3;

	/* Tell NIC about any 2-byte padding after MAC header */
	if (firstlen != len)
		tx_cmd->tx_flags |= TX_CMD_FLG_MH_PAD_MSK;

	/* Physical address of this Tx command's header (not MAC header!),
	 * within command buffer array. */
	txcmd_phys = dma_map_single(priv->bus->dev,
				    &dev_cmd->hdr, firstlen,
				    DMA_BIDIRECTIONAL);
	if (unlikely(dma_mapping_error(priv->bus->dev, txcmd_phys)))
		return -1;
	dma_unmap_addr_set(out_meta, mapping, txcmd_phys);
	dma_unmap_len_set(out_meta, len, firstlen);

	if (!ieee80211_has_morefrags(fc)) {
		txq->need_update = 1;
	} else {
		wait_write_ptr = 1;
		txq->need_update = 0;
	}

	/* Set up TFD's 2nd entry to point directly to remainder of skb,
	 * if any (802.11 null frames have no payload). */
	secondlen = skb->len - hdr_len;
	if (secondlen > 0) {
		phys_addr = dma_map_single(priv->bus->dev, skb->data + hdr_len,
					   secondlen, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(priv->bus->dev, phys_addr))) {
			dma_unmap_single(priv->bus->dev,
					 dma_unmap_addr(out_meta, mapping),
					 dma_unmap_len(out_meta, len),
					 DMA_BIDIRECTIONAL);
			return -1;
		}
	}

	/* Attach buffers to TFD */
	iwlagn_txq_attach_buf_to_tfd(priv, txq, txcmd_phys, firstlen, 1);
	if (secondlen > 0)
		iwlagn_txq_attach_buf_to_tfd(priv, txq, phys_addr,
					     secondlen, 0);

	scratch_phys = txcmd_phys + sizeof(struct iwl_cmd_header) +
				offsetof(struct iwl_tx_cmd, scratch);

	/* take back ownership of DMA buffer to enable update */
	dma_sync_single_for_cpu(priv->bus->dev, txcmd_phys, firstlen,
			DMA_BIDIRECTIONAL);
	tx_cmd->dram_lsb_ptr = cpu_to_le32(scratch_phys);
	tx_cmd->dram_msb_ptr = iwl_get_dma_hi_addr(scratch_phys);

	IWL_DEBUG_TX(priv, "sequence nr = 0X%x\n",
		     le16_to_cpu(dev_cmd->hdr.sequence));
	IWL_DEBUG_TX(priv, "tx_flags = 0X%x\n", le32_to_cpu(tx_cmd->tx_flags));
	iwl_print_hex_dump(priv, IWL_DL_TX, (u8 *)tx_cmd, sizeof(*tx_cmd));
	iwl_print_hex_dump(priv, IWL_DL_TX, (u8 *)tx_cmd->hdr, hdr_len);

	/* Set up entry for this TFD in Tx byte-count array */
	if (ampdu)
		iwl_trans_txq_update_byte_cnt_tbl(priv, txq,
					       le16_to_cpu(tx_cmd->len));

	dma_sync_single_for_device(priv->bus->dev, txcmd_phys, firstlen,
			DMA_BIDIRECTIONAL);

	trace_iwlwifi_dev_tx(priv,
			     &((struct iwl_tfd *)txq->tfds)[txq->q.write_ptr],
			     sizeof(struct iwl_tfd),
			     &dev_cmd->hdr, firstlen,
			     skb->data + hdr_len, secondlen);

	/* Tell device the write index *just past* this latest filled TFD */
	q->write_ptr = iwl_queue_inc_wrap(q->write_ptr, q->n_bd);
	iwl_txq_update_write_ptr(priv, txq);

	/*
	 * At this point the frame is "transmitted" successfully
	 * and we will get a TX status notification eventually,
	 * regardless of the value of ret. "ret" only indicates
	 * whether or not we should update the write pointer.
	 */
	if ((iwl_queue_space(q) < q->high_mark) && priv->mac80211_registered) {
		if (wait_write_ptr) {
			txq->need_update = 1;
			iwl_txq_update_write_ptr(priv, txq);
		} else {
			iwl_stop_queue(priv, txq);
		}
	}
	return 0;
}

static void iwl_trans_kick_nic(struct iwl_priv *priv)
{
	/* Remove all resets to allow NIC to operate */
	iwl_write32(priv, CSR_RESET, 0);
}

static void iwl_trans_sync_irq(struct iwl_priv *priv)
{
	/* wait to make sure we flush pending tasklet*/
	synchronize_irq(priv->bus->irq);
	tasklet_kill(&priv->irq_tasklet);
}

static void iwl_trans_free(struct iwl_priv *priv)
{
	free_irq(priv->bus->irq, priv);
	iwl_free_isr_ict(priv);
}

static const struct iwl_trans_ops trans_ops = {
	.start_device = iwl_trans_start_device,
	.prepare_card_hw = iwl_trans_prepare_card_hw,
	.stop_device = iwl_trans_stop_device,

	.tx_start = iwl_trans_tx_start,

	.rx_free = iwl_trans_rx_free,
	.tx_free = iwl_trans_tx_free,

	.send_cmd = iwl_send_cmd,
	.send_cmd_pdu = iwl_send_cmd_pdu,

	.get_tx_cmd = iwl_trans_get_tx_cmd,
	.tx = iwl_trans_tx,

	.txq_agg_disable = iwl_trans_txq_agg_disable,
	.txq_agg_setup = iwl_trans_txq_agg_setup,

	.kick_nic = iwl_trans_kick_nic,

	.sync_irq = iwl_trans_sync_irq,
	.free = iwl_trans_free,
};

int iwl_trans_register(struct iwl_trans *trans, struct iwl_priv *priv)
{
	int err;

	priv->trans.ops = &trans_ops;
	priv->trans.priv = priv;

	tasklet_init(&priv->irq_tasklet, (void (*)(unsigned long))
		iwl_irq_tasklet, (unsigned long)priv);

	iwl_alloc_isr_ict(priv);

	err = request_irq(priv->bus->irq, iwl_isr_ict, IRQF_SHARED,
		DRV_NAME, priv);
	if (err) {
		IWL_ERR(priv, "Error allocating IRQ %d\n", priv->bus->irq);
		iwl_free_isr_ict(priv);
		return err;
	}

	INIT_WORK(&priv->rx_replenish, iwl_bg_rx_replenish);

	return 0;
}
