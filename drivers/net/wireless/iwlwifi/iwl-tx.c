/******************************************************************************
 *
 * Copyright(c) 2003 - 2008 Intel Corporation. All rights reserved.
 *
 * Portions of this file are derived from the ipw3945 project, as well
 * as portions of the ieee80211 subsystem header files.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <net/mac80211.h>
#include "iwl-eeprom.h"
#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-sta.h"
#include "iwl-io.h"
#include "iwl-helpers.h"

/**
 * iwl_hw_txq_free_tfd - Free all chunks referenced by TFD [txq->q.read_ptr]
 *
 * Does NOT advance any TFD circular buffer read/write indexes
 * Does NOT free the TFD itself (which is within circular buffer)
 */
int iwl_hw_txq_free_tfd(struct iwl_priv *priv, struct iwl_tx_queue *txq)
{
	struct iwl_tfd_frame *bd_tmp = (struct iwl_tfd_frame *)&txq->bd[0];
	struct iwl_tfd_frame *bd = &bd_tmp[txq->q.read_ptr];
	struct pci_dev *dev = priv->pci_dev;
	int i;
	int counter = 0;
	int index, is_odd;

	/* Host command buffers stay mapped in memory, nothing to clean */
	if (txq->q.id == IWL_CMD_QUEUE_NUM)
		return 0;

	/* Sanity check on number of chunks */
	counter = IWL_GET_BITS(*bd, num_tbs);
	if (counter > MAX_NUM_OF_TBS) {
		IWL_ERROR("Too many chunks: %i\n", counter);
		/* @todo issue fatal error, it is quite serious situation */
		return 0;
	}

	/* Unmap chunks, if any.
	 * TFD info for odd chunks is different format than for even chunks. */
	for (i = 0; i < counter; i++) {
		index = i / 2;
		is_odd = i & 0x1;

		if (is_odd)
			pci_unmap_single(
				dev,
				IWL_GET_BITS(bd->pa[index], tb2_addr_lo16) |
				(IWL_GET_BITS(bd->pa[index],
					      tb2_addr_hi20) << 16),
				IWL_GET_BITS(bd->pa[index], tb2_len),
				PCI_DMA_TODEVICE);

		else if (i > 0)
			pci_unmap_single(dev,
					 le32_to_cpu(bd->pa[index].tb1_addr),
					 IWL_GET_BITS(bd->pa[index], tb1_len),
					 PCI_DMA_TODEVICE);

		/* Free SKB, if any, for this chunk */
		if (txq->txb[txq->q.read_ptr].skb[i]) {
			struct sk_buff *skb = txq->txb[txq->q.read_ptr].skb[i];

			dev_kfree_skb(skb);
			txq->txb[txq->q.read_ptr].skb[i] = NULL;
		}
	}
	return 0;
}
EXPORT_SYMBOL(iwl_hw_txq_free_tfd);

/**
 * iwl_tx_queue_free - Deallocate DMA queue.
 * @txq: Transmit queue to deallocate.
 *
 * Empty queue by removing and destroying all BD's.
 * Free all buffers.
 * 0-fill, but do not free "txq" descriptor structure.
 */
static void iwl_tx_queue_free(struct iwl_priv *priv, struct iwl_tx_queue *txq)
{
	struct iwl4965_queue *q = &txq->q;
	struct pci_dev *dev = priv->pci_dev;
	int len;

	if (q->n_bd == 0)
		return;

	/* first, empty all BD's */
	for (; q->write_ptr != q->read_ptr;
	     q->read_ptr = iwl_queue_inc_wrap(q->read_ptr, q->n_bd))
		iwl_hw_txq_free_tfd(priv, txq);

	len = sizeof(struct iwl_cmd) * q->n_window;
	if (q->id == IWL_CMD_QUEUE_NUM)
		len += IWL_MAX_SCAN_SIZE;

	/* De-alloc array of command/tx buffers */
	pci_free_consistent(dev, len, txq->cmd, txq->dma_addr_cmd);

	/* De-alloc circular buffer of TFDs */
	if (txq->q.n_bd)
		pci_free_consistent(dev, sizeof(struct iwl_tfd_frame) *
				    txq->q.n_bd, txq->bd, txq->q.dma_addr);

	/* De-alloc array of per-TFD driver data */
	kfree(txq->txb);
	txq->txb = NULL;

	/* 0-fill queue descriptor structure */
	memset(txq, 0, sizeof(*txq));
}

/**
 * iwl_hw_txq_ctx_free - Free TXQ Context
 *
 * Destroy all TX DMA queues and structures
 */
void iwl_hw_txq_ctx_free(struct iwl_priv *priv)
{
	int txq_id;

	/* Tx queues */
	for (txq_id = 0; txq_id < priv->hw_params.max_txq_num; txq_id++)
		iwl_tx_queue_free(priv, &priv->txq[txq_id]);

	/* Keep-warm buffer */
	iwl_kw_free(priv);
}
EXPORT_SYMBOL(iwl_hw_txq_ctx_free);

/**
 * iwl_queue_init - Initialize queue's high/low-water and read/write indexes
 */
static int iwl_queue_init(struct iwl_priv *priv, struct iwl4965_queue *q,
			  int count, int slots_num, u32 id)
{
	q->n_bd = count;
	q->n_window = slots_num;
	q->id = id;

	/* count must be power-of-two size, otherwise iwl_queue_inc_wrap
	 * and iwl_queue_dec_wrap are broken. */
	BUG_ON(!is_power_of_2(count));

	/* slots_num must be power-of-two size, otherwise
	 * get_cmd_index is broken. */
	BUG_ON(!is_power_of_2(slots_num));

	q->low_mark = q->n_window / 4;
	if (q->low_mark < 4)
		q->low_mark = 4;

	q->high_mark = q->n_window / 8;
	if (q->high_mark < 2)
		q->high_mark = 2;

	q->write_ptr = q->read_ptr = 0;

	return 0;
}

/**
 * iwl_tx_queue_alloc - Alloc driver data and TFD CB for one Tx/cmd queue
 */
static int iwl_tx_queue_alloc(struct iwl_priv *priv,
			      struct iwl_tx_queue *txq, u32 id)
{
	struct pci_dev *dev = priv->pci_dev;

	/* Driver private data, only for Tx (not command) queues,
	 * not shared with device. */
	if (id != IWL_CMD_QUEUE_NUM) {
		txq->txb = kmalloc(sizeof(txq->txb[0]) *
				   TFD_QUEUE_SIZE_MAX, GFP_KERNEL);
		if (!txq->txb) {
			IWL_ERROR("kmalloc for auxiliary BD "
				  "structures failed\n");
			goto error;
		}
	} else
		txq->txb = NULL;

	/* Circular buffer of transmit frame descriptors (TFDs),
	 * shared with device */
	txq->bd = pci_alloc_consistent(dev,
			sizeof(txq->bd[0]) * TFD_QUEUE_SIZE_MAX,
			&txq->q.dma_addr);

	if (!txq->bd) {
		IWL_ERROR("pci_alloc_consistent(%zd) failed\n",
			  sizeof(txq->bd[0]) * TFD_QUEUE_SIZE_MAX);
		goto error;
	}
	txq->q.id = id;

	return 0;

 error:
	kfree(txq->txb);
	txq->txb = NULL;

	return -ENOMEM;
}

/*
 * Tell nic where to find circular buffer of Tx Frame Descriptors for
 * given Tx queue, and enable the DMA channel used for that queue.
 *
 * 4965 supports up to 16 Tx queues in DRAM, mapped to up to 8 Tx DMA
 * channels supported in hardware.
 */
static int iwl_hw_tx_queue_init(struct iwl_priv *priv,
				struct iwl_tx_queue *txq)
{
	int rc;
	unsigned long flags;
	int txq_id = txq->q.id;

	spin_lock_irqsave(&priv->lock, flags);
	rc = iwl_grab_nic_access(priv);
	if (rc) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return rc;
	}

	/* Circular buffer (TFD queue in DRAM) physical base address */
	iwl_write_direct32(priv, FH_MEM_CBBC_QUEUE(txq_id),
			     txq->q.dma_addr >> 8);

	/* Enable DMA channel, using same id as for TFD queue */
	iwl_write_direct32(
		priv, FH_TCSR_CHNL_TX_CONFIG_REG(txq_id),
		FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE |
		FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE_VAL);
	iwl_release_nic_access(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

/**
 * iwl_tx_queue_init - Allocate and initialize one tx/cmd queue
 */
static int iwl_tx_queue_init(struct iwl_priv *priv,
			     struct iwl_tx_queue *txq,
			     int slots_num, u32 txq_id)
{
	struct pci_dev *dev = priv->pci_dev;
	int len;
	int rc = 0;

	/*
	 * Alloc buffer array for commands (Tx or other types of commands).
	 * For the command queue (#4), allocate command space + one big
	 * command for scan, since scan command is very huge; the system will
	 * not have two scans at the same time, so only one is needed.
	 * For normal Tx queues (all other queues), no super-size command
	 * space is needed.
	 */
	len = sizeof(struct iwl_cmd) * slots_num;
	if (txq_id == IWL_CMD_QUEUE_NUM)
		len +=  IWL_MAX_SCAN_SIZE;
	txq->cmd = pci_alloc_consistent(dev, len, &txq->dma_addr_cmd);
	if (!txq->cmd)
		return -ENOMEM;

	/* Alloc driver data array and TFD circular buffer */
	rc = iwl_tx_queue_alloc(priv, txq, txq_id);
	if (rc) {
		pci_free_consistent(dev, len, txq->cmd, txq->dma_addr_cmd);

		return -ENOMEM;
	}
	txq->need_update = 0;

	/* TFD_QUEUE_SIZE_MAX must be power-of-two size, otherwise
	 * iwl_queue_inc_wrap and iwl_queue_dec_wrap are broken. */
	BUILD_BUG_ON(TFD_QUEUE_SIZE_MAX & (TFD_QUEUE_SIZE_MAX - 1));

	/* Initialize queue's high/low-water marks, and head/tail indexes */
	iwl_queue_init(priv, &txq->q, TFD_QUEUE_SIZE_MAX, slots_num, txq_id);

	/* Tell device where to find queue */
	iwl_hw_tx_queue_init(priv, txq);

	return 0;
}

/**
 * iwl_txq_ctx_reset - Reset TX queue context
 * Destroys all DMA structures and initialise them again
 *
 * @param priv
 * @return error code
 */
int iwl_txq_ctx_reset(struct iwl_priv *priv)
{
	int ret = 0;
	int txq_id, slots_num;

	iwl_kw_free(priv);

	/* Free all tx/cmd queues and keep-warm buffer */
	iwl_hw_txq_ctx_free(priv);

	/* Alloc keep-warm buffer */
	ret = iwl_kw_alloc(priv);
	if (ret) {
		IWL_ERROR("Keep Warm allocation failed");
		goto error_kw;
	}

	/* Turn off all Tx DMA fifos */
	ret = priv->cfg->ops->lib->disable_tx_fifo(priv);
	if (unlikely(ret))
		goto error_reset;

	/* Tell nic where to find the keep-warm buffer */
	ret = iwl_kw_init(priv);
	if (ret) {
		IWL_ERROR("kw_init failed\n");
		goto error_reset;
	}

	/* Alloc and init all (default 16) Tx queues,
	 * including the command queue (#4) */
	for (txq_id = 0; txq_id < priv->hw_params.max_txq_num; txq_id++) {
		slots_num = (txq_id == IWL_CMD_QUEUE_NUM) ?
					TFD_CMD_SLOTS : TFD_TX_CMD_SLOTS;
		ret = iwl_tx_queue_init(priv, &priv->txq[txq_id], slots_num,
				       txq_id);
		if (ret) {
			IWL_ERROR("Tx %d queue init failed\n", txq_id);
			goto error;
		}
	}

	return ret;

 error:
	iwl_hw_txq_ctx_free(priv);
 error_reset:
	iwl_kw_free(priv);
 error_kw:
	return ret;
}
