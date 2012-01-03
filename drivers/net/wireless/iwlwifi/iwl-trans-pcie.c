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
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/gfp.h>

#include "iwl-trans.h"
#include "iwl-trans-pcie-int.h"
#include "iwl-csr.h"
#include "iwl-prph.h"
#include "iwl-shared.h"
#include "iwl-eeprom.h"
#include "iwl-agn-hw.h"

static int iwl_trans_rx_alloc(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie =
		IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_rx_queue *rxq = &trans_pcie->rxq;
	struct device *dev = bus(trans)->dev;

	memset(&trans_pcie->rxq, 0, sizeof(trans_pcie->rxq));

	spin_lock_init(&rxq->lock);

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

static void iwl_trans_rxq_free_rx_bufs(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie =
		IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_rx_queue *rxq = &trans_pcie->rxq;
	int i;

	/* Fill the rx_used queue with _all_ of the Rx buffers */
	for (i = 0; i < RX_FREE_BUFFERS + RX_QUEUE_SIZE; i++) {
		/* In the reset function, these buffers may have been allocated
		 * to an SKB, so we need to unmap and free potential storage */
		if (rxq->pool[i].page != NULL) {
			dma_unmap_page(bus(trans)->dev, rxq->pool[i].page_dma,
				PAGE_SIZE << hw_params(trans).rx_page_order,
				DMA_FROM_DEVICE);
			__free_pages(rxq->pool[i].page,
				     hw_params(trans).rx_page_order);
			rxq->pool[i].page = NULL;
		}
		list_add_tail(&rxq->pool[i].list, &rxq->rx_used);
	}
}

static void iwl_trans_rx_hw_init(struct iwl_trans *trans,
				 struct iwl_rx_queue *rxq)
{
	u32 rb_size;
	const u32 rfdnlog = RX_QUEUE_SIZE_LOG; /* 256 RBDs */
	u32 rb_timeout = RX_RB_TIMEOUT; /* FIXME: RX_RB_TIMEOUT for all devices? */

	if (iwlagn_mod_params.amsdu_size_8K)
		rb_size = FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_8K;
	else
		rb_size = FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_4K;

	/* Stop Rx DMA */
	iwl_write_direct32(bus(trans), FH_MEM_RCSR_CHNL0_CONFIG_REG, 0);

	/* Reset driver's Rx queue write index */
	iwl_write_direct32(bus(trans), FH_RSCSR_CHNL0_RBDCB_WPTR_REG, 0);

	/* Tell device where to find RBD circular buffer in DRAM */
	iwl_write_direct32(bus(trans), FH_RSCSR_CHNL0_RBDCB_BASE_REG,
			   (u32)(rxq->bd_dma >> 8));

	/* Tell device where in DRAM to update its Rx status */
	iwl_write_direct32(bus(trans), FH_RSCSR_CHNL0_STTS_WPTR_REG,
			   rxq->rb_stts_dma >> 4);

	/* Enable Rx DMA
	 * FH_RCSR_CHNL0_RX_IGNORE_RXF_EMPTY is set because of HW bug in
	 *      the credit mechanism in 5000 HW RX FIFO
	 * Direct rx interrupts to hosts
	 * Rx buffer size 4 or 8k
	 * RB timeout 0x10
	 * 256 RBDs
	 */
	iwl_write_direct32(bus(trans), FH_MEM_RCSR_CHNL0_CONFIG_REG,
			   FH_RCSR_RX_CONFIG_CHNL_EN_ENABLE_VAL |
			   FH_RCSR_CHNL0_RX_IGNORE_RXF_EMPTY |
			   FH_RCSR_CHNL0_RX_CONFIG_IRQ_DEST_INT_HOST_VAL |
			   FH_RCSR_CHNL0_RX_CONFIG_SINGLE_FRAME_MSK |
			   rb_size|
			   (rb_timeout << FH_RCSR_RX_CONFIG_REG_IRQ_RBTH_POS)|
			   (rfdnlog << FH_RCSR_RX_CONFIG_RBDCB_SIZE_POS));

	/* Set interrupt coalescing timer to default (2048 usecs) */
	iwl_write8(bus(trans), CSR_INT_COALESCING, IWL_HOST_INT_TIMEOUT_DEF);
}

static int iwl_rx_init(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie =
		IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_rx_queue *rxq = &trans_pcie->rxq;

	int i, err;
	unsigned long flags;

	if (!rxq->bd) {
		err = iwl_trans_rx_alloc(trans);
		if (err)
			return err;
	}

	spin_lock_irqsave(&rxq->lock, flags);
	INIT_LIST_HEAD(&rxq->rx_free);
	INIT_LIST_HEAD(&rxq->rx_used);

	iwl_trans_rxq_free_rx_bufs(trans);

	for (i = 0; i < RX_QUEUE_SIZE; i++)
		rxq->queue[i] = NULL;

	/* Set us so that we have processed and used all buffers, but have
	 * not restocked the Rx queue with fresh buffers */
	rxq->read = rxq->write = 0;
	rxq->write_actual = 0;
	rxq->free_count = 0;
	spin_unlock_irqrestore(&rxq->lock, flags);

	iwlagn_rx_replenish(trans);

	iwl_trans_rx_hw_init(trans, rxq);

	spin_lock_irqsave(&trans->shrd->lock, flags);
	rxq->need_update = 1;
	iwl_rx_queue_update_write_ptr(trans, rxq);
	spin_unlock_irqrestore(&trans->shrd->lock, flags);

	return 0;
}

static void iwl_trans_pcie_rx_free(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie =
		IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_rx_queue *rxq = &trans_pcie->rxq;

	unsigned long flags;

	/*if rxq->bd is NULL, it means that nothing has been allocated,
	 * exit now */
	if (!rxq->bd) {
		IWL_DEBUG_INFO(trans, "Free NULL rx context\n");
		return;
	}

	spin_lock_irqsave(&rxq->lock, flags);
	iwl_trans_rxq_free_rx_bufs(trans);
	spin_unlock_irqrestore(&rxq->lock, flags);

	dma_free_coherent(bus(trans)->dev, sizeof(__le32) * RX_QUEUE_SIZE,
			  rxq->bd, rxq->bd_dma);
	memset(&rxq->bd_dma, 0, sizeof(rxq->bd_dma));
	rxq->bd = NULL;

	if (rxq->rb_stts)
		dma_free_coherent(bus(trans)->dev,
				  sizeof(struct iwl_rb_status),
				  rxq->rb_stts, rxq->rb_stts_dma);
	else
		IWL_DEBUG_INFO(trans, "Free rxq->rb_stts which is NULL\n");
	memset(&rxq->rb_stts_dma, 0, sizeof(rxq->rb_stts_dma));
	rxq->rb_stts = NULL;
}

static int iwl_trans_rx_stop(struct iwl_trans *trans)
{

	/* stop Rx DMA */
	iwl_write_direct32(bus(trans), FH_MEM_RCSR_CHNL0_CONFIG_REG, 0);
	return iwl_poll_direct_bit(bus(trans), FH_MEM_RSSR_RX_STATUS_REG,
			    FH_RSSR_CHNL0_RX_STATUS_CHNL_IDLE, 1000);
}

static inline int iwlagn_alloc_dma_ptr(struct iwl_trans *trans,
				    struct iwl_dma_ptr *ptr, size_t size)
{
	if (WARN_ON(ptr->addr))
		return -EINVAL;

	ptr->addr = dma_alloc_coherent(bus(trans)->dev, size,
				       &ptr->dma, GFP_KERNEL);
	if (!ptr->addr)
		return -ENOMEM;
	ptr->size = size;
	return 0;
}

static inline void iwlagn_free_dma_ptr(struct iwl_trans *trans,
				    struct iwl_dma_ptr *ptr)
{
	if (unlikely(!ptr->addr))
		return;

	dma_free_coherent(bus(trans)->dev, ptr->size, ptr->addr, ptr->dma);
	memset(ptr, 0, sizeof(*ptr));
}

static int iwl_trans_txq_alloc(struct iwl_trans *trans,
				struct iwl_tx_queue *txq, int slots_num,
				u32 txq_id)
{
	size_t tfd_sz = sizeof(struct iwl_tfd) * TFD_QUEUE_SIZE_MAX;
	int i;

	if (WARN_ON(txq->meta || txq->cmd || txq->skbs || txq->tfds))
		return -EINVAL;

	txq->q.n_window = slots_num;

	txq->meta = kcalloc(slots_num, sizeof(txq->meta[0]), GFP_KERNEL);
	txq->cmd = kcalloc(slots_num, sizeof(txq->cmd[0]), GFP_KERNEL);

	if (!txq->meta || !txq->cmd)
		goto error;

	if (txq_id == trans->shrd->cmd_queue)
		for (i = 0; i < slots_num; i++) {
			txq->cmd[i] = kmalloc(sizeof(struct iwl_device_cmd),
						GFP_KERNEL);
			if (!txq->cmd[i])
				goto error;
		}

	/* Alloc driver data array and TFD circular buffer */
	/* Driver private data, only for Tx (not command) queues,
	 * not shared with device. */
	if (txq_id != trans->shrd->cmd_queue) {
		txq->skbs = kcalloc(TFD_QUEUE_SIZE_MAX, sizeof(txq->skbs[0]),
				    GFP_KERNEL);
		if (!txq->skbs) {
			IWL_ERR(trans, "kmalloc for auxiliary BD "
				  "structures failed\n");
			goto error;
		}
	} else {
		txq->skbs = NULL;
	}

	/* Circular buffer of transmit frame descriptors (TFDs),
	 * shared with device */
	txq->tfds = dma_alloc_coherent(bus(trans)->dev, tfd_sz,
				       &txq->q.dma_addr, GFP_KERNEL);
	if (!txq->tfds) {
		IWL_ERR(trans, "dma_alloc_coherent(%zd) failed\n", tfd_sz);
		goto error;
	}
	txq->q.id = txq_id;

	return 0;
error:
	kfree(txq->skbs);
	txq->skbs = NULL;
	/* since txq->cmd has been zeroed,
	 * all non allocated cmd[i] will be NULL */
	if (txq->cmd && txq_id == trans->shrd->cmd_queue)
		for (i = 0; i < slots_num; i++)
			kfree(txq->cmd[i]);
	kfree(txq->meta);
	kfree(txq->cmd);
	txq->meta = NULL;
	txq->cmd = NULL;

	return -ENOMEM;

}

static int iwl_trans_txq_init(struct iwl_trans *trans, struct iwl_tx_queue *txq,
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
	ret = iwl_queue_init(&txq->q, TFD_QUEUE_SIZE_MAX, slots_num,
			txq_id);
	if (ret)
		return ret;

	/*
	 * Tell nic where to find circular buffer of Tx Frame Descriptors for
	 * given Tx queue, and enable the DMA channel used for that queue.
	 * Circular buffer (TFD queue in DRAM) physical base address */
	iwl_write_direct32(bus(trans), FH_MEM_CBBC_QUEUE(txq_id),
			     txq->q.dma_addr >> 8);

	return 0;
}

/**
 * iwl_tx_queue_unmap -  Unmap any remaining DMA mappings and free skb's
 */
static void iwl_tx_queue_unmap(struct iwl_trans *trans, int txq_id)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_tx_queue *txq = &trans_pcie->txq[txq_id];
	struct iwl_queue *q = &txq->q;
	enum dma_data_direction dma_dir;
	unsigned long flags;
	spinlock_t *lock;

	if (!q->n_bd)
		return;

	/* In the command queue, all the TBs are mapped as BIDI
	 * so unmap them as such.
	 */
	if (txq_id == trans->shrd->cmd_queue) {
		dma_dir = DMA_BIDIRECTIONAL;
		lock = &trans->hcmd_lock;
	} else {
		dma_dir = DMA_TO_DEVICE;
		lock = &trans->shrd->sta_lock;
	}

	spin_lock_irqsave(lock, flags);
	while (q->write_ptr != q->read_ptr) {
		/* The read_ptr needs to bound by q->n_window */
		iwlagn_txq_free_tfd(trans, txq, get_cmd_index(q, q->read_ptr),
				    dma_dir);
		q->read_ptr = iwl_queue_inc_wrap(q->read_ptr, q->n_bd);
	}
	spin_unlock_irqrestore(lock, flags);
}

/**
 * iwl_tx_queue_free - Deallocate DMA queue.
 * @txq: Transmit queue to deallocate.
 *
 * Empty queue by removing and destroying all BD's.
 * Free all buffers.
 * 0-fill, but do not free "txq" descriptor structure.
 */
static void iwl_tx_queue_free(struct iwl_trans *trans, int txq_id)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_tx_queue *txq = &trans_pcie->txq[txq_id];
	struct device *dev = bus(trans)->dev;
	int i;
	if (WARN_ON(!txq))
		return;

	iwl_tx_queue_unmap(trans, txq_id);

	/* De-alloc array of command/tx buffers */

	if (txq_id == trans->shrd->cmd_queue)
		for (i = 0; i < txq->q.n_window; i++)
			kfree(txq->cmd[i]);

	/* De-alloc circular buffer of TFDs */
	if (txq->q.n_bd) {
		dma_free_coherent(dev, sizeof(struct iwl_tfd) *
				  txq->q.n_bd, txq->tfds, txq->q.dma_addr);
		memset(&txq->q.dma_addr, 0, sizeof(txq->q.dma_addr));
	}

	/* De-alloc array of per-TFD driver data */
	kfree(txq->skbs);
	txq->skbs = NULL;

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
static void iwl_trans_pcie_tx_free(struct iwl_trans *trans)
{
	int txq_id;
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	/* Tx queues */
	if (trans_pcie->txq) {
		for (txq_id = 0;
		     txq_id < hw_params(trans).max_txq_num; txq_id++)
			iwl_tx_queue_free(trans, txq_id);
	}

	kfree(trans_pcie->txq);
	trans_pcie->txq = NULL;

	iwlagn_free_dma_ptr(trans, &trans_pcie->kw);

	iwlagn_free_dma_ptr(trans, &trans_pcie->scd_bc_tbls);
}

/**
 * iwl_trans_tx_alloc - allocate TX context
 * Allocate all Tx DMA structures and initialize them
 *
 * @param priv
 * @return error code
 */
static int iwl_trans_tx_alloc(struct iwl_trans *trans)
{
	int ret;
	int txq_id, slots_num;
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	u16 scd_bc_tbls_size = hw_params(trans).max_txq_num *
			sizeof(struct iwlagn_scd_bc_tbl);

	/*It is not allowed to alloc twice, so warn when this happens.
	 * We cannot rely on the previous allocation, so free and fail */
	if (WARN_ON(trans_pcie->txq)) {
		ret = -EINVAL;
		goto error;
	}

	ret = iwlagn_alloc_dma_ptr(trans, &trans_pcie->scd_bc_tbls,
				   scd_bc_tbls_size);
	if (ret) {
		IWL_ERR(trans, "Scheduler BC Table allocation failed\n");
		goto error;
	}

	/* Alloc keep-warm buffer */
	ret = iwlagn_alloc_dma_ptr(trans, &trans_pcie->kw, IWL_KW_SIZE);
	if (ret) {
		IWL_ERR(trans, "Keep Warm allocation failed\n");
		goto error;
	}

	trans_pcie->txq = kcalloc(hw_params(trans).max_txq_num,
				  sizeof(struct iwl_tx_queue), GFP_KERNEL);
	if (!trans_pcie->txq) {
		IWL_ERR(trans, "Not enough memory for txq\n");
		ret = ENOMEM;
		goto error;
	}

	/* Alloc and init all Tx queues, including the command queue (#4/#9) */
	for (txq_id = 0; txq_id < hw_params(trans).max_txq_num; txq_id++) {
		slots_num = (txq_id == trans->shrd->cmd_queue) ?
					TFD_CMD_SLOTS : TFD_TX_CMD_SLOTS;
		ret = iwl_trans_txq_alloc(trans, &trans_pcie->txq[txq_id],
					  slots_num, txq_id);
		if (ret) {
			IWL_ERR(trans, "Tx %d queue alloc failed\n", txq_id);
			goto error;
		}
	}

	return 0;

error:
	iwl_trans_pcie_tx_free(trans);

	return ret;
}
static int iwl_tx_init(struct iwl_trans *trans)
{
	int ret;
	int txq_id, slots_num;
	unsigned long flags;
	bool alloc = false;
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (!trans_pcie->txq) {
		ret = iwl_trans_tx_alloc(trans);
		if (ret)
			goto error;
		alloc = true;
	}

	spin_lock_irqsave(&trans->shrd->lock, flags);

	/* Turn off all Tx DMA fifos */
	iwl_write_prph(bus(trans), SCD_TXFACT, 0);

	/* Tell NIC where to find the "keep warm" buffer */
	iwl_write_direct32(bus(trans), FH_KW_MEM_ADDR_REG,
			   trans_pcie->kw.dma >> 4);

	spin_unlock_irqrestore(&trans->shrd->lock, flags);

	/* Alloc and init all Tx queues, including the command queue (#4/#9) */
	for (txq_id = 0; txq_id < hw_params(trans).max_txq_num; txq_id++) {
		slots_num = (txq_id == trans->shrd->cmd_queue) ?
					TFD_CMD_SLOTS : TFD_TX_CMD_SLOTS;
		ret = iwl_trans_txq_init(trans, &trans_pcie->txq[txq_id],
					 slots_num, txq_id);
		if (ret) {
			IWL_ERR(trans, "Tx %d queue init failed\n", txq_id);
			goto error;
		}
	}

	return 0;
error:
	/*Upon error, free only if we allocated something */
	if (alloc)
		iwl_trans_pcie_tx_free(trans);
	return ret;
}

static void iwl_set_pwr_vmain(struct iwl_trans *trans)
{
/*
 * (for documentation purposes)
 * to set power to V_AUX, do:

		if (pci_pme_capable(priv->pci_dev, PCI_D3cold))
			iwl_set_bits_mask_prph(bus(trans), APMG_PS_CTRL_REG,
					       APMG_PS_CTRL_VAL_PWR_SRC_VAUX,
					       ~APMG_PS_CTRL_MSK_PWR_SRC);
 */

	iwl_set_bits_mask_prph(bus(trans), APMG_PS_CTRL_REG,
			       APMG_PS_CTRL_VAL_PWR_SRC_VMAIN,
			       ~APMG_PS_CTRL_MSK_PWR_SRC);
}

static int iwl_nic_init(struct iwl_trans *trans)
{
	unsigned long flags;

	/* nic_init */
	spin_lock_irqsave(&trans->shrd->lock, flags);
	iwl_apm_init(priv(trans));

	/* Set interrupt coalescing calibration timer to default (512 usecs) */
	iwl_write8(bus(trans), CSR_INT_COALESCING,
		IWL_HOST_INT_CALIB_TIMEOUT_DEF);

	spin_unlock_irqrestore(&trans->shrd->lock, flags);

	iwl_set_pwr_vmain(trans);

	iwl_nic_config(priv(trans));

	/* Allocate the RX queue, or reset if it is already allocated */
	iwl_rx_init(trans);

	/* Allocate or reset and init all Tx and Command queues */
	if (iwl_tx_init(trans))
		return -ENOMEM;

	if (hw_params(trans).shadow_reg_enable) {
		/* enable shadow regs in HW */
		iwl_set_bit(bus(trans), CSR_MAC_SHADOW_REG_CTRL,
			0x800FFFFF);
	}

	set_bit(STATUS_INIT, &trans->shrd->status);

	return 0;
}

#define HW_READY_TIMEOUT (50)

/* Note: returns poll_bit return value, which is >= 0 if success */
static int iwl_set_hw_ready(struct iwl_trans *trans)
{
	int ret;

	iwl_set_bit(bus(trans), CSR_HW_IF_CONFIG_REG,
		CSR_HW_IF_CONFIG_REG_BIT_NIC_READY);

	/* See if we got it */
	ret = iwl_poll_bit(bus(trans), CSR_HW_IF_CONFIG_REG,
				CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
				CSR_HW_IF_CONFIG_REG_BIT_NIC_READY,
				HW_READY_TIMEOUT);

	IWL_DEBUG_INFO(trans, "hardware%s ready\n", ret < 0 ? " not" : "");
	return ret;
}

/* Note: returns standard 0/-ERROR code */
static int iwl_trans_pcie_prepare_card_hw(struct iwl_trans *trans)
{
	int ret;

	IWL_DEBUG_INFO(trans, "iwl_trans_prepare_card_hw enter\n");

	ret = iwl_set_hw_ready(trans);
	if (ret >= 0)
		return 0;

	/* If HW is not ready, prepare the conditions to check again */
	iwl_set_bit(bus(trans), CSR_HW_IF_CONFIG_REG,
			CSR_HW_IF_CONFIG_REG_PREPARE);

	ret = iwl_poll_bit(bus(trans), CSR_HW_IF_CONFIG_REG,
			~CSR_HW_IF_CONFIG_REG_BIT_NIC_PREPARE_DONE,
			CSR_HW_IF_CONFIG_REG_BIT_NIC_PREPARE_DONE, 150000);

	if (ret < 0)
		return ret;

	/* HW should be ready by now, check again. */
	ret = iwl_set_hw_ready(trans);
	if (ret >= 0)
		return 0;
	return ret;
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
	{ IWL_TX_FIFO_AUX, IWL_AC_UNSET, },
};

static const u8 iwlagn_bss_ac_to_fifo[] = {
	IWL_TX_FIFO_VO,
	IWL_TX_FIFO_VI,
	IWL_TX_FIFO_BE,
	IWL_TX_FIFO_BK,
};
static const u8 iwlagn_bss_ac_to_queue[] = {
	0, 1, 2, 3,
};
static const u8 iwlagn_pan_ac_to_fifo[] = {
	IWL_TX_FIFO_VO_IPAN,
	IWL_TX_FIFO_VI_IPAN,
	IWL_TX_FIFO_BE_IPAN,
	IWL_TX_FIFO_BK_IPAN,
};
static const u8 iwlagn_pan_ac_to_queue[] = {
	7, 6, 5, 4,
};

static int iwl_trans_pcie_start_device(struct iwl_trans *trans)
{
	int ret;
	struct iwl_trans_pcie *trans_pcie =
		IWL_TRANS_GET_PCIE_TRANS(trans);

	trans->shrd->ucode_owner = IWL_OWNERSHIP_DRIVER;
	trans_pcie->ac_to_queue[IWL_RXON_CTX_BSS] = iwlagn_bss_ac_to_queue;
	trans_pcie->ac_to_queue[IWL_RXON_CTX_PAN] = iwlagn_pan_ac_to_queue;

	trans_pcie->ac_to_fifo[IWL_RXON_CTX_BSS] = iwlagn_bss_ac_to_fifo;
	trans_pcie->ac_to_fifo[IWL_RXON_CTX_PAN] = iwlagn_pan_ac_to_fifo;

	trans_pcie->mcast_queue[IWL_RXON_CTX_BSS] = 0;
	trans_pcie->mcast_queue[IWL_RXON_CTX_PAN] = IWL_IPAN_MCAST_QUEUE;

	if ((hw_params(trans).sku & EEPROM_SKU_CAP_AMT_ENABLE) &&
	     iwl_trans_pcie_prepare_card_hw(trans)) {
		IWL_WARN(trans, "Exit HW not ready\n");
		return -EIO;
	}

	/* If platform's RF_KILL switch is NOT set to KILL */
	if (iwl_read32(bus(trans), CSR_GP_CNTRL) &
			CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW)
		clear_bit(STATUS_RF_KILL_HW, &trans->shrd->status);
	else
		set_bit(STATUS_RF_KILL_HW, &trans->shrd->status);

	if (iwl_is_rfkill(trans->shrd)) {
		iwl_set_hw_rfkill_state(priv(trans), true);
		iwl_enable_interrupts(trans);
		return -ERFKILL;
	}

	iwl_write32(bus(trans), CSR_INT, 0xFFFFFFFF);

	ret = iwl_nic_init(trans);
	if (ret) {
		IWL_ERR(trans, "Unable to init nic\n");
		return ret;
	}

	/* make sure rfkill handshake bits are cleared */
	iwl_write32(bus(trans), CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);
	iwl_write32(bus(trans), CSR_UCODE_DRV_GP1_CLR,
		    CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED);

	/* clear (again), then enable host interrupts */
	iwl_write32(bus(trans), CSR_INT, 0xFFFFFFFF);
	iwl_enable_interrupts(trans);

	/* really make sure rfkill handshake bits are cleared */
	iwl_write32(bus(trans), CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);
	iwl_write32(bus(trans), CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL);

	return 0;
}

/*
 * Activate/Deactivate Tx DMA/FIFO channels according tx fifos mask
 * must be called under priv->shrd->lock and mac access
 */
static void iwl_trans_txq_set_sched(struct iwl_trans *trans, u32 mask)
{
	iwl_write_prph(bus(trans), SCD_TXFACT, mask);
}

static void iwl_trans_pcie_tx_start(struct iwl_trans *trans)
{
	const struct queue_to_fifo_ac *queue_to_fifo;
	struct iwl_trans_pcie *trans_pcie =
		IWL_TRANS_GET_PCIE_TRANS(trans);
	u32 a;
	unsigned long flags;
	int i, chan;
	u32 reg_val;

	spin_lock_irqsave(&trans->shrd->lock, flags);

	trans_pcie->scd_base_addr =
		iwl_read_prph(bus(trans), SCD_SRAM_BASE_ADDR);
	a = trans_pcie->scd_base_addr + SCD_CONTEXT_MEM_LOWER_BOUND;
	/* reset conext data memory */
	for (; a < trans_pcie->scd_base_addr + SCD_CONTEXT_MEM_UPPER_BOUND;
		a += 4)
		iwl_write_targ_mem(bus(trans), a, 0);
	/* reset tx status memory */
	for (; a < trans_pcie->scd_base_addr + SCD_TX_STTS_MEM_UPPER_BOUND;
		a += 4)
		iwl_write_targ_mem(bus(trans), a, 0);
	for (; a < trans_pcie->scd_base_addr +
	       SCD_TRANS_TBL_OFFSET_QUEUE(hw_params(trans).max_txq_num);
	       a += 4)
		iwl_write_targ_mem(bus(trans), a, 0);

	iwl_write_prph(bus(trans), SCD_DRAM_BASE_ADDR,
		       trans_pcie->scd_bc_tbls.dma >> 10);

	/* Enable DMA channel */
	for (chan = 0; chan < FH_TCSR_CHNL_NUM ; chan++)
		iwl_write_direct32(bus(trans), FH_TCSR_CHNL_TX_CONFIG_REG(chan),
				FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE |
				FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE);

	/* Update FH chicken bits */
	reg_val = iwl_read_direct32(bus(trans), FH_TX_CHICKEN_BITS_REG);
	iwl_write_direct32(bus(trans), FH_TX_CHICKEN_BITS_REG,
			   reg_val | FH_TX_CHICKEN_BITS_SCD_AUTO_RETRY_EN);

	iwl_write_prph(bus(trans), SCD_QUEUECHAIN_SEL,
		SCD_QUEUECHAIN_SEL_ALL(trans));
	iwl_write_prph(bus(trans), SCD_AGGR_SEL, 0);

	/* initiate the queues */
	for (i = 0; i < hw_params(trans).max_txq_num; i++) {
		iwl_write_prph(bus(trans), SCD_QUEUE_RDPTR(i), 0);
		iwl_write_direct32(bus(trans), HBUS_TARG_WRPTR, 0 | (i << 8));
		iwl_write_targ_mem(bus(trans), trans_pcie->scd_base_addr +
				SCD_CONTEXT_QUEUE_OFFSET(i), 0);
		iwl_write_targ_mem(bus(trans), trans_pcie->scd_base_addr +
				SCD_CONTEXT_QUEUE_OFFSET(i) +
				sizeof(u32),
				((SCD_WIN_SIZE <<
				SCD_QUEUE_CTX_REG2_WIN_SIZE_POS) &
				SCD_QUEUE_CTX_REG2_WIN_SIZE_MSK) |
				((SCD_FRAME_LIMIT <<
				SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS) &
				SCD_QUEUE_CTX_REG2_FRAME_LIMIT_MSK));
	}

	iwl_write_prph(bus(trans), SCD_INTERRUPT_MASK,
			IWL_MASK(0, hw_params(trans).max_txq_num));

	/* Activate all Tx DMA/FIFO channels */
	iwl_trans_txq_set_sched(trans, IWL_MASK(0, 7));

	/* map queues to FIFOs */
	if (trans->shrd->valid_contexts != BIT(IWL_RXON_CTX_BSS))
		queue_to_fifo = iwlagn_ipan_queue_to_tx_fifo;
	else
		queue_to_fifo = iwlagn_default_queue_to_tx_fifo;

	iwl_trans_set_wr_ptrs(trans, trans->shrd->cmd_queue, 0);

	/* make sure all queue are not stopped */
	memset(&trans_pcie->queue_stopped[0], 0,
		sizeof(trans_pcie->queue_stopped));
	for (i = 0; i < 4; i++)
		atomic_set(&trans_pcie->queue_stop_count[i], 0);

	/* reset to 0 to enable all the queue first */
	trans_pcie->txq_ctx_active_msk = 0;

	BUILD_BUG_ON(ARRAY_SIZE(iwlagn_default_queue_to_tx_fifo) <
						IWLAGN_FIRST_AMPDU_QUEUE);
	BUILD_BUG_ON(ARRAY_SIZE(iwlagn_ipan_queue_to_tx_fifo) <
						IWLAGN_FIRST_AMPDU_QUEUE);

	for (i = 0; i < IWLAGN_FIRST_AMPDU_QUEUE; i++) {
		int fifo = queue_to_fifo[i].fifo;
		int ac = queue_to_fifo[i].ac;

		iwl_txq_ctx_activate(trans_pcie, i);

		if (fifo == IWL_TX_FIFO_UNUSED)
			continue;

		if (ac != IWL_AC_UNSET)
			iwl_set_swq_id(&trans_pcie->txq[i], ac, i);
		iwl_trans_tx_queue_set_status(trans, &trans_pcie->txq[i],
					      fifo, 0);
	}

	spin_unlock_irqrestore(&trans->shrd->lock, flags);

	/* Enable L1-Active */
	iwl_clear_bits_prph(bus(trans), APMG_PCIDEV_STT_REG,
			  APMG_PCIDEV_STT_VAL_L1_ACT_DIS);
}

/**
 * iwlagn_txq_ctx_stop - Stop all Tx DMA channels
 */
static int iwl_trans_tx_stop(struct iwl_trans *trans)
{
	int ch, txq_id;
	unsigned long flags;
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	/* Turn off all Tx DMA fifos */
	spin_lock_irqsave(&trans->shrd->lock, flags);

	iwl_trans_txq_set_sched(trans, 0);

	/* Stop each Tx DMA channel, and wait for it to be idle */
	for (ch = 0; ch < FH_TCSR_CHNL_NUM; ch++) {
		iwl_write_direct32(bus(trans),
				   FH_TCSR_CHNL_TX_CONFIG_REG(ch), 0x0);
		if (iwl_poll_direct_bit(bus(trans), FH_TSSR_TX_STATUS_REG,
				    FH_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE(ch),
				    1000))
			IWL_ERR(trans, "Failing on timeout while stopping"
			    " DMA channel %d [0x%08x]", ch,
			    iwl_read_direct32(bus(trans),
					      FH_TSSR_TX_STATUS_REG));
	}
	spin_unlock_irqrestore(&trans->shrd->lock, flags);

	if (!trans_pcie->txq) {
		IWL_WARN(trans, "Stopping tx queues that aren't allocated...");
		return 0;
	}

	/* Unmap DMA from host system and free skb's */
	for (txq_id = 0; txq_id < hw_params(trans).max_txq_num; txq_id++)
		iwl_tx_queue_unmap(trans, txq_id);

	return 0;
}

static void iwl_trans_pcie_stop_device(struct iwl_trans *trans)
{
	unsigned long flags;
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	/* tell the device to stop sending interrupts */
	spin_lock_irqsave(&trans->shrd->lock, flags);
	iwl_disable_interrupts(trans);
	spin_unlock_irqrestore(&trans->shrd->lock, flags);

	/* device going down, Stop using ICT table */
	iwl_disable_ict(trans);

	/*
	 * If a HW restart happens during firmware loading,
	 * then the firmware loading might call this function
	 * and later it might be called again due to the
	 * restart. So don't process again if the device is
	 * already dead.
	 */
	if (test_bit(STATUS_DEVICE_ENABLED, &trans->shrd->status)) {
		iwl_trans_tx_stop(trans);
		iwl_trans_rx_stop(trans);

		/* Power-down device's busmaster DMA clocks */
		iwl_write_prph(bus(trans), APMG_CLK_DIS_REG,
			       APMG_CLK_VAL_DMA_CLK_RQT);
		udelay(5);
	}

	/* Make sure (redundant) we've released our request to stay awake */
	iwl_clear_bit(bus(trans), CSR_GP_CNTRL,
			CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);

	/* Stop the device, and put it in low power state */
	iwl_apm_stop(priv(trans));

	/* Upon stop, the APM issues an interrupt if HW RF kill is set.
	 * Clean again the interrupt here
	 */
	spin_lock_irqsave(&trans->shrd->lock, flags);
	iwl_disable_interrupts(trans);
	spin_unlock_irqrestore(&trans->shrd->lock, flags);

	/* wait to make sure we flush pending tasklet*/
	synchronize_irq(bus(trans)->irq);
	tasklet_kill(&trans_pcie->irq_tasklet);

	/* stop and reset the on-board processor */
	iwl_write32(bus(trans), CSR_RESET, CSR_RESET_REG_FLAG_NEVO_RESET);
}

static int iwl_trans_pcie_tx(struct iwl_trans *trans, struct sk_buff *skb,
		struct iwl_device_cmd *dev_cmd, enum iwl_rxon_context_id ctx,
		u8 sta_id, u8 tid)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct iwl_tx_cmd *tx_cmd = (struct iwl_tx_cmd *) dev_cmd->payload;
	struct iwl_cmd_meta *out_meta;
	struct iwl_tx_queue *txq;
	struct iwl_queue *q;

	dma_addr_t phys_addr = 0;
	dma_addr_t txcmd_phys;
	dma_addr_t scratch_phys;
	u16 len, firstlen, secondlen;
	u8 wait_write_ptr = 0;
	u8 txq_id;
	bool is_agg = false;
	__le16 fc = hdr->frame_control;
	u8 hdr_len = ieee80211_hdrlen(fc);
	u16 __maybe_unused wifi_seq;

	/*
	 * Send this frame after DTIM -- there's a special queue
	 * reserved for this for contexts that support AP mode.
	 */
	if (info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM) {
		txq_id = trans_pcie->mcast_queue[ctx];

		/*
		 * The microcode will clear the more data
		 * bit in the last frame it transmits.
		 */
		hdr->frame_control |=
			cpu_to_le16(IEEE80211_FCTL_MOREDATA);
	} else if (info->flags & IEEE80211_TX_CTL_TX_OFFCHAN)
		txq_id = IWL_AUX_QUEUE;
	else
		txq_id =
		    trans_pcie->ac_to_queue[ctx][skb_get_queue_mapping(skb)];

	/* aggregation is on for this <sta,tid> */
	if (info->flags & IEEE80211_TX_CTL_AMPDU) {
		WARN_ON(tid >= IWL_MAX_TID_COUNT);
		txq_id = trans_pcie->agg_txq[sta_id][tid];
		is_agg = true;
	}

	txq = &trans_pcie->txq[txq_id];
	q = &txq->q;

	/* In AGG mode, the index in the ring must correspond to the WiFi
	 * sequence number. This is a HW requirements to help the SCD to parse
	 * the BA.
	 * Check here that the packets are in the right place on the ring.
	 */
#ifdef CONFIG_IWLWIFI_DEBUG
	wifi_seq = SEQ_TO_SN(le16_to_cpu(hdr->seq_ctrl));
	WARN_ONCE(is_agg && ((wifi_seq & 0xff) != q->write_ptr),
		  "Q: %d WiFi Seq %d tfdNum %d",
		  txq_id, wifi_seq, q->write_ptr);
#endif

	/* Set up driver data for this TFD */
	txq->skbs[q->write_ptr] = skb;
	txq->cmd[q->write_ptr] = dev_cmd;

	dev_cmd->hdr.cmd = REPLY_TX;
	dev_cmd->hdr.sequence = cpu_to_le16((u16)(QUEUE_TO_SEQ(txq_id) |
				INDEX_TO_SEQ(q->write_ptr)));

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
	txcmd_phys = dma_map_single(bus(trans)->dev,
				    &dev_cmd->hdr, firstlen,
				    DMA_BIDIRECTIONAL);
	if (unlikely(dma_mapping_error(bus(trans)->dev, txcmd_phys)))
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
		phys_addr = dma_map_single(bus(trans)->dev, skb->data + hdr_len,
					   secondlen, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(bus(trans)->dev, phys_addr))) {
			dma_unmap_single(bus(trans)->dev,
					 dma_unmap_addr(out_meta, mapping),
					 dma_unmap_len(out_meta, len),
					 DMA_BIDIRECTIONAL);
			return -1;
		}
	}

	/* Attach buffers to TFD */
	iwlagn_txq_attach_buf_to_tfd(trans, txq, txcmd_phys, firstlen, 1);
	if (secondlen > 0)
		iwlagn_txq_attach_buf_to_tfd(trans, txq, phys_addr,
					     secondlen, 0);

	scratch_phys = txcmd_phys + sizeof(struct iwl_cmd_header) +
				offsetof(struct iwl_tx_cmd, scratch);

	/* take back ownership of DMA buffer to enable update */
	dma_sync_single_for_cpu(bus(trans)->dev, txcmd_phys, firstlen,
			DMA_BIDIRECTIONAL);
	tx_cmd->dram_lsb_ptr = cpu_to_le32(scratch_phys);
	tx_cmd->dram_msb_ptr = iwl_get_dma_hi_addr(scratch_phys);

	IWL_DEBUG_TX(trans, "sequence nr = 0X%x\n",
		     le16_to_cpu(dev_cmd->hdr.sequence));
	IWL_DEBUG_TX(trans, "tx_flags = 0X%x\n", le32_to_cpu(tx_cmd->tx_flags));
	iwl_print_hex_dump(trans, IWL_DL_TX, (u8 *)tx_cmd, sizeof(*tx_cmd));
	iwl_print_hex_dump(trans, IWL_DL_TX, (u8 *)tx_cmd->hdr, hdr_len);

	/* Set up entry for this TFD in Tx byte-count array */
	iwl_trans_txq_update_byte_cnt_tbl(trans, txq, le16_to_cpu(tx_cmd->len));

	dma_sync_single_for_device(bus(trans)->dev, txcmd_phys, firstlen,
			DMA_BIDIRECTIONAL);

	trace_iwlwifi_dev_tx(priv(trans),
			     &((struct iwl_tfd *)txq->tfds)[txq->q.write_ptr],
			     sizeof(struct iwl_tfd),
			     &dev_cmd->hdr, firstlen,
			     skb->data + hdr_len, secondlen);

	/* Tell device the write index *just past* this latest filled TFD */
	q->write_ptr = iwl_queue_inc_wrap(q->write_ptr, q->n_bd);
	iwl_txq_update_write_ptr(trans, txq);

	/*
	 * At this point the frame is "transmitted" successfully
	 * and we will get a TX status notification eventually,
	 * regardless of the value of ret. "ret" only indicates
	 * whether or not we should update the write pointer.
	 */
	if (iwl_queue_space(q) < q->high_mark) {
		if (wait_write_ptr) {
			txq->need_update = 1;
			iwl_txq_update_write_ptr(trans, txq);
		} else {
			iwl_stop_queue(trans, txq, "Queue is full");
		}
	}
	return 0;
}

static void iwl_trans_pcie_kick_nic(struct iwl_trans *trans)
{
	/* Remove all resets to allow NIC to operate */
	iwl_write32(bus(trans), CSR_RESET, 0);
}

static int iwl_trans_pcie_request_irq(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie =
		IWL_TRANS_GET_PCIE_TRANS(trans);
	int err;

	trans_pcie->inta_mask = CSR_INI_SET_MASK;

	tasklet_init(&trans_pcie->irq_tasklet, (void (*)(unsigned long))
		iwl_irq_tasklet, (unsigned long)trans);

	iwl_alloc_isr_ict(trans);

	err = request_irq(bus(trans)->irq, iwl_isr_ict, IRQF_SHARED,
		DRV_NAME, trans);
	if (err) {
		IWL_ERR(trans, "Error allocating IRQ %d\n", bus(trans)->irq);
		iwl_free_isr_ict(trans);
		return err;
	}

	INIT_WORK(&trans_pcie->rx_replenish, iwl_bg_rx_replenish);
	return 0;
}

static int iwl_trans_pcie_reclaim(struct iwl_trans *trans, int sta_id, int tid,
		      int txq_id, int ssn, u32 status,
		      struct sk_buff_head *skbs)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_tx_queue *txq = &trans_pcie->txq[txq_id];
	/* n_bd is usually 256 => n_bd - 1 = 0xff */
	int tfd_num = ssn & (txq->q.n_bd - 1);
	int freed = 0;

	txq->time_stamp = jiffies;

	if (unlikely(txq_id >= IWLAGN_FIRST_AMPDU_QUEUE &&
		     txq_id != trans_pcie->agg_txq[sta_id][tid])) {
		/*
		 * FIXME: this is a uCode bug which need to be addressed,
		 * log the information and return for now.
		 * Since it is can possibly happen very often and in order
		 * not to fill the syslog, don't use IWL_ERR or IWL_WARN
		 */
		IWL_DEBUG_TX_QUEUES(trans, "Bad queue mapping txq_id %d, "
			"agg_txq[sta_id[tid] %d", txq_id,
			trans_pcie->agg_txq[sta_id][tid]);
		return 1;
	}

	if (txq->q.read_ptr != tfd_num) {
		IWL_DEBUG_TX_REPLY(trans, "[Q %d | AC %d] %d -> %d (%d)\n",
				txq_id, iwl_get_queue_ac(txq), txq->q.read_ptr,
				tfd_num, ssn);
		freed = iwl_tx_queue_reclaim(trans, txq_id, tfd_num, skbs);
		if (iwl_queue_space(&txq->q) > txq->q.low_mark &&
		   (!txq->sched_retry ||
		   status != TX_STATUS_FAIL_PASSIVE_NO_RX))
			iwl_wake_queue(trans, txq, "Packets reclaimed");
	}
	return 0;
}

static void iwl_trans_pcie_free(struct iwl_trans *trans)
{
	iwl_calib_free_results(trans);
	iwl_trans_pcie_tx_free(trans);
	iwl_trans_pcie_rx_free(trans);
	free_irq(bus(trans)->irq, trans);
	iwl_free_isr_ict(trans);
	trans->shrd->trans = NULL;
	kfree(trans);
}

#ifdef CONFIG_PM_SLEEP
static int iwl_trans_pcie_suspend(struct iwl_trans *trans)
{
	/*
	 * This function is called when system goes into suspend state
	 * mac80211 will call iwlagn_mac_stop() from the mac80211 suspend
	 * function first but since iwlagn_mac_stop() has no knowledge of
	 * who the caller is,
	 * it will not call apm_ops.stop() to stop the DMA operation.
	 * Calling apm_ops.stop here to make sure we stop the DMA.
	 *
	 * But of course ... if we have configured WoWLAN then we did other
	 * things already :-)
	 */
	if (!trans->shrd->wowlan) {
		iwl_apm_stop(priv(trans));
	} else {
		iwl_disable_interrupts(trans);
		iwl_clear_bit(bus(trans), CSR_GP_CNTRL,
			      CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	}

	return 0;
}

static int iwl_trans_pcie_resume(struct iwl_trans *trans)
{
	bool hw_rfkill = false;

	iwl_enable_interrupts(trans);

	if (!(iwl_read32(bus(trans), CSR_GP_CNTRL) &
				CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW))
		hw_rfkill = true;

	if (hw_rfkill)
		set_bit(STATUS_RF_KILL_HW, &trans->shrd->status);
	else
		clear_bit(STATUS_RF_KILL_HW, &trans->shrd->status);

	iwl_set_hw_rfkill_state(priv(trans), hw_rfkill);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static void iwl_trans_pcie_wake_any_queue(struct iwl_trans *trans,
					  enum iwl_rxon_context_id ctx,
					  const char *msg)
{
	u8 ac, txq_id;
	struct iwl_trans_pcie *trans_pcie =
		IWL_TRANS_GET_PCIE_TRANS(trans);

	for (ac = 0; ac < AC_NUM; ac++) {
		txq_id = trans_pcie->ac_to_queue[ctx][ac];
		IWL_DEBUG_TX_QUEUES(trans, "Queue Status: Q[%d] %s\n",
			ac,
			(atomic_read(&trans_pcie->queue_stop_count[ac]) > 0)
			      ? "stopped" : "awake");
		iwl_wake_queue(trans, &trans_pcie->txq[txq_id], msg);
	}
}

const struct iwl_trans_ops trans_ops_pcie;

static struct iwl_trans *iwl_trans_pcie_alloc(struct iwl_shared *shrd)
{
	struct iwl_trans *iwl_trans = kzalloc(sizeof(struct iwl_trans) +
					      sizeof(struct iwl_trans_pcie),
					      GFP_KERNEL);
	if (iwl_trans) {
		struct iwl_trans_pcie *trans_pcie =
			IWL_TRANS_GET_PCIE_TRANS(iwl_trans);
		iwl_trans->ops = &trans_ops_pcie;
		iwl_trans->shrd = shrd;
		trans_pcie->trans = iwl_trans;
		spin_lock_init(&iwl_trans->hcmd_lock);
	}

	return iwl_trans;
}

static void iwl_trans_pcie_stop_queue(struct iwl_trans *trans, int txq_id,
				      const char *msg)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	iwl_stop_queue(trans, &trans_pcie->txq[txq_id], msg);
}

#define IWL_FLUSH_WAIT_MS	2000

static int iwl_trans_pcie_wait_tx_queue_empty(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_tx_queue *txq;
	struct iwl_queue *q;
	int cnt;
	unsigned long now = jiffies;
	int ret = 0;

	/* waiting for all the tx frames complete might take a while */
	for (cnt = 0; cnt < hw_params(trans).max_txq_num; cnt++) {
		if (cnt == trans->shrd->cmd_queue)
			continue;
		txq = &trans_pcie->txq[cnt];
		q = &txq->q;
		while (q->read_ptr != q->write_ptr && !time_after(jiffies,
		       now + msecs_to_jiffies(IWL_FLUSH_WAIT_MS)))
			msleep(1);

		if (q->read_ptr != q->write_ptr) {
			IWL_ERR(trans, "fail to flush all tx fifo queues\n");
			ret = -ETIMEDOUT;
			break;
		}
	}
	return ret;
}

/*
 * On every watchdog tick we check (latest) time stamp. If it does not
 * change during timeout period and queue is not empty we reset firmware.
 */
static int iwl_trans_pcie_check_stuck_queue(struct iwl_trans *trans, int cnt)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_tx_queue *txq = &trans_pcie->txq[cnt];
	struct iwl_queue *q = &txq->q;
	unsigned long timeout;

	if (q->read_ptr == q->write_ptr) {
		txq->time_stamp = jiffies;
		return 0;
	}

	timeout = txq->time_stamp +
		  msecs_to_jiffies(hw_params(trans).wd_timeout);

	if (time_after(jiffies, timeout)) {
		IWL_ERR(trans, "Queue %d stuck for %u ms.\n", q->id,
			hw_params(trans).wd_timeout);
		IWL_ERR(trans, "Current SW read_ptr %d write_ptr %d\n",
			q->read_ptr, q->write_ptr);
		IWL_ERR(trans, "Current HW read_ptr %d write_ptr %d\n",
			iwl_read_prph(bus(trans), SCD_QUEUE_RDPTR(cnt))
				& (TFD_QUEUE_SIZE_MAX - 1),
			iwl_read_prph(bus(trans), SCD_QUEUE_WRPTR(cnt)));
		return 1;
	}

	return 0;
}

static const char *get_fh_string(int cmd)
{
	switch (cmd) {
	IWL_CMD(FH_RSCSR_CHNL0_STTS_WPTR_REG);
	IWL_CMD(FH_RSCSR_CHNL0_RBDCB_BASE_REG);
	IWL_CMD(FH_RSCSR_CHNL0_WPTR);
	IWL_CMD(FH_MEM_RCSR_CHNL0_CONFIG_REG);
	IWL_CMD(FH_MEM_RSSR_SHARED_CTRL_REG);
	IWL_CMD(FH_MEM_RSSR_RX_STATUS_REG);
	IWL_CMD(FH_MEM_RSSR_RX_ENABLE_ERR_IRQ2DRV);
	IWL_CMD(FH_TSSR_TX_STATUS_REG);
	IWL_CMD(FH_TSSR_TX_ERROR_REG);
	default:
		return "UNKNOWN";
	}
}

int iwl_dump_fh(struct iwl_trans *trans, char **buf, bool display)
{
	int i;
#ifdef CONFIG_IWLWIFI_DEBUG
	int pos = 0;
	size_t bufsz = 0;
#endif
	static const u32 fh_tbl[] = {
		FH_RSCSR_CHNL0_STTS_WPTR_REG,
		FH_RSCSR_CHNL0_RBDCB_BASE_REG,
		FH_RSCSR_CHNL0_WPTR,
		FH_MEM_RCSR_CHNL0_CONFIG_REG,
		FH_MEM_RSSR_SHARED_CTRL_REG,
		FH_MEM_RSSR_RX_STATUS_REG,
		FH_MEM_RSSR_RX_ENABLE_ERR_IRQ2DRV,
		FH_TSSR_TX_STATUS_REG,
		FH_TSSR_TX_ERROR_REG
	};
#ifdef CONFIG_IWLWIFI_DEBUG
	if (display) {
		bufsz = ARRAY_SIZE(fh_tbl) * 48 + 40;
		*buf = kmalloc(bufsz, GFP_KERNEL);
		if (!*buf)
			return -ENOMEM;
		pos += scnprintf(*buf + pos, bufsz - pos,
				"FH register values:\n");
		for (i = 0; i < ARRAY_SIZE(fh_tbl); i++) {
			pos += scnprintf(*buf + pos, bufsz - pos,
				"  %34s: 0X%08x\n",
				get_fh_string(fh_tbl[i]),
				iwl_read_direct32(bus(trans), fh_tbl[i]));
		}
		return pos;
	}
#endif
	IWL_ERR(trans, "FH register values:\n");
	for (i = 0; i <  ARRAY_SIZE(fh_tbl); i++) {
		IWL_ERR(trans, "  %34s: 0X%08x\n",
			get_fh_string(fh_tbl[i]),
			iwl_read_direct32(bus(trans), fh_tbl[i]));
	}
	return 0;
}

static const char *get_csr_string(int cmd)
{
	switch (cmd) {
	IWL_CMD(CSR_HW_IF_CONFIG_REG);
	IWL_CMD(CSR_INT_COALESCING);
	IWL_CMD(CSR_INT);
	IWL_CMD(CSR_INT_MASK);
	IWL_CMD(CSR_FH_INT_STATUS);
	IWL_CMD(CSR_GPIO_IN);
	IWL_CMD(CSR_RESET);
	IWL_CMD(CSR_GP_CNTRL);
	IWL_CMD(CSR_HW_REV);
	IWL_CMD(CSR_EEPROM_REG);
	IWL_CMD(CSR_EEPROM_GP);
	IWL_CMD(CSR_OTP_GP_REG);
	IWL_CMD(CSR_GIO_REG);
	IWL_CMD(CSR_GP_UCODE_REG);
	IWL_CMD(CSR_GP_DRIVER_REG);
	IWL_CMD(CSR_UCODE_DRV_GP1);
	IWL_CMD(CSR_UCODE_DRV_GP2);
	IWL_CMD(CSR_LED_REG);
	IWL_CMD(CSR_DRAM_INT_TBL_REG);
	IWL_CMD(CSR_GIO_CHICKEN_BITS);
	IWL_CMD(CSR_ANA_PLL_CFG);
	IWL_CMD(CSR_HW_REV_WA_REG);
	IWL_CMD(CSR_DBG_HPET_MEM_REG);
	default:
		return "UNKNOWN";
	}
}

void iwl_dump_csr(struct iwl_trans *trans)
{
	int i;
	static const u32 csr_tbl[] = {
		CSR_HW_IF_CONFIG_REG,
		CSR_INT_COALESCING,
		CSR_INT,
		CSR_INT_MASK,
		CSR_FH_INT_STATUS,
		CSR_GPIO_IN,
		CSR_RESET,
		CSR_GP_CNTRL,
		CSR_HW_REV,
		CSR_EEPROM_REG,
		CSR_EEPROM_GP,
		CSR_OTP_GP_REG,
		CSR_GIO_REG,
		CSR_GP_UCODE_REG,
		CSR_GP_DRIVER_REG,
		CSR_UCODE_DRV_GP1,
		CSR_UCODE_DRV_GP2,
		CSR_LED_REG,
		CSR_DRAM_INT_TBL_REG,
		CSR_GIO_CHICKEN_BITS,
		CSR_ANA_PLL_CFG,
		CSR_HW_REV_WA_REG,
		CSR_DBG_HPET_MEM_REG
	};
	IWL_ERR(trans, "CSR values:\n");
	IWL_ERR(trans, "(2nd byte of CSR_INT_COALESCING is "
		"CSR_INT_PERIODIC_REG)\n");
	for (i = 0; i <  ARRAY_SIZE(csr_tbl); i++) {
		IWL_ERR(trans, "  %25s: 0X%08x\n",
			get_csr_string(csr_tbl[i]),
			iwl_read32(bus(trans), csr_tbl[i]));
	}
}

#ifdef CONFIG_IWLWIFI_DEBUGFS
/* create and remove of files */
#define DEBUGFS_ADD_FILE(name, parent, mode) do {			\
	if (!debugfs_create_file(#name, mode, parent, trans,		\
				 &iwl_dbgfs_##name##_ops))		\
		return -ENOMEM;						\
} while (0)

/* file operation */
#define DEBUGFS_READ_FUNC(name)                                         \
static ssize_t iwl_dbgfs_##name##_read(struct file *file,               \
					char __user *user_buf,          \
					size_t count, loff_t *ppos);

#define DEBUGFS_WRITE_FUNC(name)                                        \
static ssize_t iwl_dbgfs_##name##_write(struct file *file,              \
					const char __user *user_buf,    \
					size_t count, loff_t *ppos);


static int iwl_dbgfs_open_file_generic(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

#define DEBUGFS_READ_FILE_OPS(name)					\
	DEBUGFS_READ_FUNC(name);					\
static const struct file_operations iwl_dbgfs_##name##_ops = {		\
	.read = iwl_dbgfs_##name##_read,				\
	.open = iwl_dbgfs_open_file_generic,				\
	.llseek = generic_file_llseek,					\
};

#define DEBUGFS_WRITE_FILE_OPS(name)                                    \
	DEBUGFS_WRITE_FUNC(name);                                       \
static const struct file_operations iwl_dbgfs_##name##_ops = {          \
	.write = iwl_dbgfs_##name##_write,                              \
	.open = iwl_dbgfs_open_file_generic,				\
	.llseek = generic_file_llseek,					\
};

#define DEBUGFS_READ_WRITE_FILE_OPS(name)				\
	DEBUGFS_READ_FUNC(name);					\
	DEBUGFS_WRITE_FUNC(name);					\
static const struct file_operations iwl_dbgfs_##name##_ops = {		\
	.write = iwl_dbgfs_##name##_write,				\
	.read = iwl_dbgfs_##name##_read,				\
	.open = iwl_dbgfs_open_file_generic,				\
	.llseek = generic_file_llseek,					\
};

static ssize_t iwl_dbgfs_tx_queue_read(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct iwl_trans *trans = file->private_data;
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_tx_queue *txq;
	struct iwl_queue *q;
	char *buf;
	int pos = 0;
	int cnt;
	int ret;
	const size_t bufsz = sizeof(char) * 64 * hw_params(trans).max_txq_num;

	if (!trans_pcie->txq) {
		IWL_ERR(trans, "txq not ready\n");
		return -EAGAIN;
	}
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (cnt = 0; cnt < hw_params(trans).max_txq_num; cnt++) {
		txq = &trans_pcie->txq[cnt];
		q = &txq->q;
		pos += scnprintf(buf + pos, bufsz - pos,
				"hwq %.2d: read=%u write=%u stop=%d"
				" swq_id=%#.2x (ac %d/hwq %d)\n",
				cnt, q->read_ptr, q->write_ptr,
				!!test_bit(cnt, trans_pcie->queue_stopped),
				txq->swq_id, txq->swq_id & 3,
				(txq->swq_id >> 2) & 0x1f);
		if (cnt >= 4)
			continue;
		/* for the ACs, display the stop count too */
		pos += scnprintf(buf + pos, bufsz - pos,
			"        stop-count: %d\n",
			atomic_read(&trans_pcie->queue_stop_count[cnt]));
	}
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_rx_queue_read(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos) {
	struct iwl_trans *trans = file->private_data;
	struct iwl_trans_pcie *trans_pcie =
		IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_rx_queue *rxq = &trans_pcie->rxq;
	char buf[256];
	int pos = 0;
	const size_t bufsz = sizeof(buf);

	pos += scnprintf(buf + pos, bufsz - pos, "read: %u\n",
						rxq->read);
	pos += scnprintf(buf + pos, bufsz - pos, "write: %u\n",
						rxq->write);
	pos += scnprintf(buf + pos, bufsz - pos, "free_count: %u\n",
						rxq->free_count);
	if (rxq->rb_stts) {
		pos += scnprintf(buf + pos, bufsz - pos, "closed_rb_num: %u\n",
			 le16_to_cpu(rxq->rb_stts->closed_rb_num) &  0x0FFF);
	} else {
		pos += scnprintf(buf + pos, bufsz - pos,
					"closed_rb_num: Not Allocated\n");
	}
	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_log_event_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_trans *trans = file->private_data;
	char *buf;
	int pos = 0;
	ssize_t ret = -ENOMEM;

	ret = pos = iwl_dump_nic_event_log(trans, true, &buf, true);
	if (buf) {
		ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
		kfree(buf);
	}
	return ret;
}

static ssize_t iwl_dbgfs_log_event_write(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_trans *trans = file->private_data;
	u32 event_log_flag;
	char buf[8];
	int buf_size;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%d", &event_log_flag) != 1)
		return -EFAULT;
	if (event_log_flag == 1)
		iwl_dump_nic_event_log(trans, true, NULL, false);

	return count;
}

static ssize_t iwl_dbgfs_interrupt_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos) {

	struct iwl_trans *trans = file->private_data;
	struct iwl_trans_pcie *trans_pcie =
		IWL_TRANS_GET_PCIE_TRANS(trans);
	struct isr_statistics *isr_stats = &trans_pcie->isr_stats;

	int pos = 0;
	char *buf;
	int bufsz = 24 * 64; /* 24 items * 64 char per item */
	ssize_t ret;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		IWL_ERR(trans, "Can not allocate Buffer\n");
		return -ENOMEM;
	}

	pos += scnprintf(buf + pos, bufsz - pos,
			"Interrupt Statistics Report:\n");

	pos += scnprintf(buf + pos, bufsz - pos, "HW Error:\t\t\t %u\n",
		isr_stats->hw);
	pos += scnprintf(buf + pos, bufsz - pos, "SW Error:\t\t\t %u\n",
		isr_stats->sw);
	if (isr_stats->sw || isr_stats->hw) {
		pos += scnprintf(buf + pos, bufsz - pos,
			"\tLast Restarting Code:  0x%X\n",
			isr_stats->err_code);
	}
#ifdef CONFIG_IWLWIFI_DEBUG
	pos += scnprintf(buf + pos, bufsz - pos, "Frame transmitted:\t\t %u\n",
		isr_stats->sch);
	pos += scnprintf(buf + pos, bufsz - pos, "Alive interrupt:\t\t %u\n",
		isr_stats->alive);
#endif
	pos += scnprintf(buf + pos, bufsz - pos,
		"HW RF KILL switch toggled:\t %u\n", isr_stats->rfkill);

	pos += scnprintf(buf + pos, bufsz - pos, "CT KILL:\t\t\t %u\n",
		isr_stats->ctkill);

	pos += scnprintf(buf + pos, bufsz - pos, "Wakeup Interrupt:\t\t %u\n",
		isr_stats->wakeup);

	pos += scnprintf(buf + pos, bufsz - pos,
		"Rx command responses:\t\t %u\n", isr_stats->rx);

	pos += scnprintf(buf + pos, bufsz - pos, "Tx/FH interrupt:\t\t %u\n",
		isr_stats->tx);

	pos += scnprintf(buf + pos, bufsz - pos, "Unexpected INTA:\t\t %u\n",
		isr_stats->unhandled);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_interrupt_write(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_trans *trans = file->private_data;
	struct iwl_trans_pcie *trans_pcie =
		IWL_TRANS_GET_PCIE_TRANS(trans);
	struct isr_statistics *isr_stats = &trans_pcie->isr_stats;

	char buf[8];
	int buf_size;
	u32 reset_flag;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%x", &reset_flag) != 1)
		return -EFAULT;
	if (reset_flag == 0)
		memset(isr_stats, 0, sizeof(*isr_stats));

	return count;
}

static ssize_t iwl_dbgfs_csr_write(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_trans *trans = file->private_data;
	char buf[8];
	int buf_size;
	int csr;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%d", &csr) != 1)
		return -EFAULT;

	iwl_dump_csr(trans);

	return count;
}

static ssize_t iwl_dbgfs_fh_reg_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_trans *trans = file->private_data;
	char *buf;
	int pos = 0;
	ssize_t ret = -EFAULT;

	ret = pos = iwl_dump_fh(trans, &buf, true);
	if (buf) {
		ret = simple_read_from_buffer(user_buf,
					      count, ppos, buf, pos);
		kfree(buf);
	}

	return ret;
}

DEBUGFS_READ_WRITE_FILE_OPS(log_event);
DEBUGFS_READ_WRITE_FILE_OPS(interrupt);
DEBUGFS_READ_FILE_OPS(fh_reg);
DEBUGFS_READ_FILE_OPS(rx_queue);
DEBUGFS_READ_FILE_OPS(tx_queue);
DEBUGFS_WRITE_FILE_OPS(csr);

/*
 * Create the debugfs files and directories
 *
 */
static int iwl_trans_pcie_dbgfs_register(struct iwl_trans *trans,
					struct dentry *dir)
{
	DEBUGFS_ADD_FILE(rx_queue, dir, S_IRUSR);
	DEBUGFS_ADD_FILE(tx_queue, dir, S_IRUSR);
	DEBUGFS_ADD_FILE(log_event, dir, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(interrupt, dir, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(csr, dir, S_IWUSR);
	DEBUGFS_ADD_FILE(fh_reg, dir, S_IRUSR);
	return 0;
}
#else
static int iwl_trans_pcie_dbgfs_register(struct iwl_trans *trans,
					struct dentry *dir)
{ return 0; }

#endif /*CONFIG_IWLWIFI_DEBUGFS */

const struct iwl_trans_ops trans_ops_pcie = {
	.alloc = iwl_trans_pcie_alloc,
	.request_irq = iwl_trans_pcie_request_irq,
	.start_device = iwl_trans_pcie_start_device,
	.prepare_card_hw = iwl_trans_pcie_prepare_card_hw,
	.stop_device = iwl_trans_pcie_stop_device,

	.tx_start = iwl_trans_pcie_tx_start,
	.wake_any_queue = iwl_trans_pcie_wake_any_queue,

	.send_cmd = iwl_trans_pcie_send_cmd,

	.tx = iwl_trans_pcie_tx,
	.reclaim = iwl_trans_pcie_reclaim,

	.tx_agg_disable = iwl_trans_pcie_tx_agg_disable,
	.tx_agg_alloc = iwl_trans_pcie_tx_agg_alloc,
	.tx_agg_setup = iwl_trans_pcie_tx_agg_setup,

	.kick_nic = iwl_trans_pcie_kick_nic,

	.free = iwl_trans_pcie_free,
	.stop_queue = iwl_trans_pcie_stop_queue,

	.dbgfs_register = iwl_trans_pcie_dbgfs_register,

	.wait_tx_queue_empty = iwl_trans_pcie_wait_tx_queue_empty,
	.check_stuck_queue = iwl_trans_pcie_check_stuck_queue,

#ifdef CONFIG_PM_SLEEP
	.suspend = iwl_trans_pcie_suspend,
	.resume = iwl_trans_pcie_resume,
#endif
};
