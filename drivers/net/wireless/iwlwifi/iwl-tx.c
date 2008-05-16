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

#include <linux/etherdevice.h>
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


int iwl_hw_txq_attach_buf_to_tfd(struct iwl_priv *priv, void *ptr,
				 dma_addr_t addr, u16 len)
{
	int index, is_odd;
	struct iwl_tfd_frame *tfd = ptr;
	u32 num_tbs = IWL_GET_BITS(*tfd, num_tbs);

	/* Each TFD can point to a maximum 20 Tx buffers */
	if ((num_tbs >= MAX_NUM_OF_TBS) || (num_tbs < 0)) {
		IWL_ERROR("Error can not send more than %d chunks\n",
			  MAX_NUM_OF_TBS);
		return -EINVAL;
	}

	index = num_tbs / 2;
	is_odd = num_tbs & 0x1;

	if (!is_odd) {
		tfd->pa[index].tb1_addr = cpu_to_le32(addr);
		IWL_SET_BITS(tfd->pa[index], tb1_addr_hi,
			     iwl_get_dma_hi_address(addr));
		IWL_SET_BITS(tfd->pa[index], tb1_len, len);
	} else {
		IWL_SET_BITS(tfd->pa[index], tb2_addr_lo16,
			     (u32) (addr & 0xffff));
		IWL_SET_BITS(tfd->pa[index], tb2_addr_hi20, addr >> 16);
		IWL_SET_BITS(tfd->pa[index], tb2_len, len);
	}

	IWL_SET_BITS(*tfd, num_tbs, num_tbs + 1);

	return 0;
}
EXPORT_SYMBOL(iwl_hw_txq_attach_buf_to_tfd);

/**
 * iwl_txq_update_write_ptr - Send new write index to hardware
 */
int iwl_txq_update_write_ptr(struct iwl_priv *priv, struct iwl_tx_queue *txq)
{
	u32 reg = 0;
	int ret = 0;
	int txq_id = txq->q.id;

	if (txq->need_update == 0)
		return ret;

	/* if we're trying to save power */
	if (test_bit(STATUS_POWER_PMI, &priv->status)) {
		/* wake up nic if it's powered down ...
		 * uCode will wake up, and interrupt us again, so next
		 * time we'll skip this part. */
		reg = iwl_read32(priv, CSR_UCODE_DRV_GP1);

		if (reg & CSR_UCODE_DRV_GP1_BIT_MAC_SLEEP) {
			IWL_DEBUG_INFO("Requesting wakeup, GP1 = 0x%x\n", reg);
			iwl_set_bit(priv, CSR_GP_CNTRL,
				    CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
			return ret;
		}

		/* restore this queue's parameters in nic hardware. */
		ret = iwl_grab_nic_access(priv);
		if (ret)
			return ret;
		iwl_write_direct32(priv, HBUS_TARG_WRPTR,
				     txq->q.write_ptr | (txq_id << 8));
		iwl_release_nic_access(priv);

	/* else not in power-save mode, uCode will never sleep when we're
	 * trying to tx (during RFKILL, we're not trying to tx). */
	} else
		iwl_write32(priv, HBUS_TARG_WRPTR,
			    txq->q.write_ptr | (txq_id << 8));

	txq->need_update = 0;

	return ret;
}
EXPORT_SYMBOL(iwl_txq_update_write_ptr);


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
	struct iwl_queue *q = &txq->q;
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

/*************** DMA-QUEUE-GENERAL-FUNCTIONS  *****
 * DMA services
 *
 * Theory of operation
 *
 * A Tx or Rx queue resides in host DRAM, and is comprised of a circular buffer
 * of buffer descriptors, each of which points to one or more data buffers for
 * the device to read from or fill.  Driver and device exchange status of each
 * queue via "read" and "write" pointers.  Driver keeps minimum of 2 empty
 * entries in each circular buffer, to protect against confusing empty and full
 * queue states.
 *
 * The device reads or writes the data in the queues via the device's several
 * DMA/FIFO channels.  Each queue is mapped to a single DMA channel.
 *
 * For Tx queue, there are low mark and high mark limits. If, after queuing
 * the packet for Tx, free space become < low mark, Tx queue stopped. When
 * reclaiming packets (on 'tx done IRQ), if free space become > high mark,
 * Tx queue resumed.
 *
 * See more detailed info in iwl-4965-hw.h.
 ***************************************************/

int iwl_queue_space(const struct iwl_queue *q)
{
	int s = q->read_ptr - q->write_ptr;

	if (q->read_ptr > q->write_ptr)
		s -= q->n_bd;

	if (s <= 0)
		s += q->n_window;
	/* keep some reserve to not confuse empty and full situations */
	s -= 2;
	if (s < 0)
		s = 0;
	return s;
}
EXPORT_SYMBOL(iwl_queue_space);


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
static int iwl_queue_init(struct iwl_priv *priv, struct iwl_queue *q,
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

/*
 * handle build REPLY_TX command notification.
 */
static void iwl_tx_cmd_build_basic(struct iwl_priv *priv,
				  struct iwl_tx_cmd *tx_cmd,
				  struct ieee80211_tx_info *info,
				  struct ieee80211_hdr *hdr,
				  int is_unicast, u8 std_id)
{
	u16 fc = le16_to_cpu(hdr->frame_control);
	__le32 tx_flags = tx_cmd->tx_flags;

	tx_cmd->stop_time.life_time = TX_CMD_LIFE_TIME_INFINITE;
	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK)) {
		tx_flags |= TX_CMD_FLG_ACK_MSK;
		if ((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT)
			tx_flags |= TX_CMD_FLG_SEQ_CTL_MSK;
		if (ieee80211_is_probe_response(fc) &&
		    !(le16_to_cpu(hdr->seq_ctrl) & 0xf))
			tx_flags |= TX_CMD_FLG_TSF_MSK;
	} else {
		tx_flags &= (~TX_CMD_FLG_ACK_MSK);
		tx_flags |= TX_CMD_FLG_SEQ_CTL_MSK;
	}

	if (ieee80211_is_back_request(fc))
		tx_flags |= TX_CMD_FLG_ACK_MSK | TX_CMD_FLG_IMM_BA_RSP_MASK;


	tx_cmd->sta_id = std_id;
	if (ieee80211_get_morefrag(hdr))
		tx_flags |= TX_CMD_FLG_MORE_FRAG_MSK;

	if (ieee80211_is_qos_data(fc)) {
		u8 *qc = ieee80211_get_qos_ctrl(hdr, ieee80211_get_hdrlen(fc));
		tx_cmd->tid_tspec = qc[0] & 0xf;
		tx_flags &= ~TX_CMD_FLG_SEQ_CTL_MSK;
	} else {
		tx_flags |= TX_CMD_FLG_SEQ_CTL_MSK;
	}

	if (info->flags & IEEE80211_TX_CTL_USE_RTS_CTS) {
		tx_flags |= TX_CMD_FLG_RTS_MSK;
		tx_flags &= ~TX_CMD_FLG_CTS_MSK;
	} else if (info->flags & IEEE80211_TX_CTL_USE_CTS_PROTECT) {
		tx_flags &= ~TX_CMD_FLG_RTS_MSK;
		tx_flags |= TX_CMD_FLG_CTS_MSK;
	}

	if ((tx_flags & TX_CMD_FLG_RTS_MSK) || (tx_flags & TX_CMD_FLG_CTS_MSK))
		tx_flags |= TX_CMD_FLG_FULL_TXOP_PROT_MSK;

	tx_flags &= ~(TX_CMD_FLG_ANT_SEL_MSK);
	if ((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT) {
		if ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_ASSOC_REQ ||
		    (fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_REASSOC_REQ)
			tx_cmd->timeout.pm_frame_timeout = cpu_to_le16(3);
		else
			tx_cmd->timeout.pm_frame_timeout = cpu_to_le16(2);
	} else {
		tx_cmd->timeout.pm_frame_timeout = 0;
	}

	tx_cmd->driver_txop = 0;
	tx_cmd->tx_flags = tx_flags;
	tx_cmd->next_frame_len = 0;
}

#define RTS_HCCA_RETRY_LIMIT		3
#define RTS_DFAULT_RETRY_LIMIT		60

static void iwl_tx_cmd_build_rate(struct iwl_priv *priv,
			      struct iwl_tx_cmd *tx_cmd,
			      struct ieee80211_tx_info *info,
			      u16 fc, int sta_id,
			      int is_hcca)
{
	u8 rts_retry_limit = 0;
	u8 data_retry_limit = 0;
	u8 rate_plcp;
	u16 rate_flags = 0;
	int rate_idx;

	rate_idx = min(ieee80211_get_tx_rate(priv->hw, info)->hw_value & 0xffff,
			IWL_RATE_COUNT - 1);

	rate_plcp = iwl_rates[rate_idx].plcp;

	rts_retry_limit = (is_hcca) ?
	    RTS_HCCA_RETRY_LIMIT : RTS_DFAULT_RETRY_LIMIT;

	if ((rate_idx >= IWL_FIRST_CCK_RATE) && (rate_idx <= IWL_LAST_CCK_RATE))
		rate_flags |= RATE_MCS_CCK_MSK;


	if (ieee80211_is_probe_response(fc)) {
		data_retry_limit = 3;
		if (data_retry_limit < rts_retry_limit)
			rts_retry_limit = data_retry_limit;
	} else
		data_retry_limit = IWL_DEFAULT_TX_RETRY;

	if (priv->data_retry_limit != -1)
		data_retry_limit = priv->data_retry_limit;


	if (ieee80211_is_data(fc)) {
		tx_cmd->initial_rate_index = 0;
		tx_cmd->tx_flags |= TX_CMD_FLG_STA_RATE_MSK;
	} else {
		switch (fc & IEEE80211_FCTL_STYPE) {
		case IEEE80211_STYPE_AUTH:
		case IEEE80211_STYPE_DEAUTH:
		case IEEE80211_STYPE_ASSOC_REQ:
		case IEEE80211_STYPE_REASSOC_REQ:
			if (tx_cmd->tx_flags & TX_CMD_FLG_RTS_MSK) {
				tx_cmd->tx_flags &= ~TX_CMD_FLG_RTS_MSK;
				tx_cmd->tx_flags |= TX_CMD_FLG_CTS_MSK;
			}
			break;
		default:
			break;
		}

		/* Alternate between antenna A and B for successive frames */
		if (priv->use_ant_b_for_management_frame) {
			priv->use_ant_b_for_management_frame = 0;
			rate_flags |= RATE_MCS_ANT_B_MSK;
		} else {
			priv->use_ant_b_for_management_frame = 1;
			rate_flags |= RATE_MCS_ANT_A_MSK;
		}
	}

	tx_cmd->rts_retry_limit = rts_retry_limit;
	tx_cmd->data_retry_limit = data_retry_limit;
	tx_cmd->rate_n_flags = iwl4965_hw_set_rate_n_flags(rate_plcp, rate_flags);
}

static void iwl_tx_cmd_build_hwcrypto(struct iwl_priv *priv,
				      struct ieee80211_tx_info *info,
				      struct iwl_tx_cmd *tx_cmd,
				      struct sk_buff *skb_frag,
				      int sta_id)
{
	struct ieee80211_key_conf *keyconf = info->control.hw_key;

	switch (keyconf->alg) {
	case ALG_CCMP:
		tx_cmd->sec_ctl = TX_CMD_SEC_CCM;
		memcpy(tx_cmd->key, keyconf->key, keyconf->keylen);
		if (info->flags & IEEE80211_TX_CTL_AMPDU)
			tx_cmd->tx_flags |= TX_CMD_FLG_AGG_CCMP_MSK;
		IWL_DEBUG_TX("tx_cmd with aes hwcrypto\n");
		break;

	case ALG_TKIP:
		tx_cmd->sec_ctl = TX_CMD_SEC_TKIP;
		ieee80211_get_tkip_key(keyconf, skb_frag,
			IEEE80211_TKIP_P2_KEY, tx_cmd->key);
		IWL_DEBUG_TX("tx_cmd with tkip hwcrypto\n");
		break;

	case ALG_WEP:
		tx_cmd->sec_ctl |= (TX_CMD_SEC_WEP |
			(keyconf->keyidx & TX_CMD_SEC_MSK) << TX_CMD_SEC_SHIFT);

		if (keyconf->keylen == WEP_KEY_LEN_128)
			tx_cmd->sec_ctl |= TX_CMD_SEC_KEY128;

		memcpy(&tx_cmd->key[3], keyconf->key, keyconf->keylen);

		IWL_DEBUG_TX("Configuring packet for WEP encryption "
			     "with key %d\n", keyconf->keyidx);
		break;

	default:
		printk(KERN_ERR "Unknown encode alg %d\n", keyconf->alg);
		break;
	}
}

static void iwl_update_tx_stats(struct iwl_priv *priv, u16 fc, u16 len)
{
	/* 0 - mgmt, 1 - cnt, 2 - data */
	int idx = (fc & IEEE80211_FCTL_FTYPE) >> 2;
	priv->tx_stats[idx].cnt++;
	priv->tx_stats[idx].bytes += len;
}

/*
 * start REPLY_TX command process
 */
int iwl_tx_skb(struct iwl_priv *priv, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct iwl_tfd_frame *tfd;
	u32 *control_flags;
	int txq_id = skb_get_queue_mapping(skb);
	struct iwl_tx_queue *txq = NULL;
	struct iwl_queue *q = NULL;
	dma_addr_t phys_addr;
	dma_addr_t txcmd_phys;
	dma_addr_t scratch_phys;
	struct iwl_cmd *out_cmd = NULL;
	struct iwl_tx_cmd *tx_cmd;
	u16 len, idx, len_org;
	u16 seq_number = 0;
	u8 id, hdr_len, unicast;
	u8 sta_id;
	u16 fc;
	u8 wait_write_ptr = 0;
	u8 tid = 0;
	u8 *qc = NULL;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&priv->lock, flags);
	if (iwl_is_rfkill(priv)) {
		IWL_DEBUG_DROP("Dropping - RF KILL\n");
		goto drop_unlock;
	}

	if (!priv->vif) {
		IWL_DEBUG_DROP("Dropping - !priv->vif\n");
		goto drop_unlock;
	}

	if ((ieee80211_get_tx_rate(priv->hw, info)->hw_value & 0xFF) ==
	     IWL_INVALID_RATE) {
		IWL_ERROR("ERROR: No TX rate available.\n");
		goto drop_unlock;
	}

	unicast = !is_multicast_ether_addr(hdr->addr1);
	id = 0;

	fc = le16_to_cpu(hdr->frame_control);

#ifdef CONFIG_IWLWIFI_DEBUG
	if (ieee80211_is_auth(fc))
		IWL_DEBUG_TX("Sending AUTH frame\n");
	else if (ieee80211_is_assoc_request(fc))
		IWL_DEBUG_TX("Sending ASSOC frame\n");
	else if (ieee80211_is_reassoc_request(fc))
		IWL_DEBUG_TX("Sending REASSOC frame\n");
#endif

	/* drop all data frame if we are not associated */
	if (((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA) &&
	   (!iwl_is_associated(priv) ||
	    ((priv->iw_mode == IEEE80211_IF_TYPE_STA) && !priv->assoc_id) ||
	    !priv->assoc_station_added)) {
		IWL_DEBUG_DROP("Dropping - !iwl_is_associated\n");
		goto drop_unlock;
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	hdr_len = ieee80211_get_hdrlen(fc);

	/* Find (or create) index into station table for destination station */
	sta_id = iwl_get_sta_id(priv, hdr);
	if (sta_id == IWL_INVALID_STATION) {
		DECLARE_MAC_BUF(mac);

		IWL_DEBUG_DROP("Dropping - INVALID STATION: %s\n",
			       print_mac(mac, hdr->addr1));
		goto drop;
	}

	IWL_DEBUG_TX("station Id %d\n", sta_id);

	if (ieee80211_is_qos_data(fc)) {
		qc = ieee80211_get_qos_ctrl(hdr, hdr_len);
		tid = qc[0] & 0xf;
		seq_number = priv->stations[sta_id].tid[tid].seq_number &
				IEEE80211_SCTL_SEQ;
		hdr->seq_ctrl = cpu_to_le16(seq_number) |
			(hdr->seq_ctrl &
				__constant_cpu_to_le16(IEEE80211_SCTL_FRAG));
		seq_number += 0x10;
#ifdef CONFIG_IWL4965_HT
		/* aggregation is on for this <sta,tid> */
		if (info->flags & IEEE80211_TX_CTL_AMPDU)
			txq_id = priv->stations[sta_id].tid[tid].agg.txq_id;
		priv->stations[sta_id].tid[tid].tfds_in_queue++;
#endif /* CONFIG_IWL4965_HT */
	}

	/* Descriptor for chosen Tx queue */
	txq = &priv->txq[txq_id];
	q = &txq->q;

	spin_lock_irqsave(&priv->lock, flags);

	/* Set up first empty TFD within this queue's circular TFD buffer */
	tfd = &txq->bd[q->write_ptr];
	memset(tfd, 0, sizeof(*tfd));
	control_flags = (u32 *) tfd;
	idx = get_cmd_index(q, q->write_ptr, 0);

	/* Set up driver data for this TFD */
	memset(&(txq->txb[q->write_ptr]), 0, sizeof(struct iwl_tx_info));
	txq->txb[q->write_ptr].skb[0] = skb;

	/* Set up first empty entry in queue's array of Tx/cmd buffers */
	out_cmd = &txq->cmd[idx];
	tx_cmd = &out_cmd->cmd.tx;
	memset(&out_cmd->hdr, 0, sizeof(out_cmd->hdr));
	memset(tx_cmd, 0, sizeof(struct iwl_tx_cmd));

	/*
	 * Set up the Tx-command (not MAC!) header.
	 * Store the chosen Tx queue and TFD index within the sequence field;
	 * after Tx, uCode's Tx response will return this value so driver can
	 * locate the frame within the tx queue and do post-tx processing.
	 */
	out_cmd->hdr.cmd = REPLY_TX;
	out_cmd->hdr.sequence = cpu_to_le16((u16)(QUEUE_TO_SEQ(txq_id) |
				INDEX_TO_SEQ(q->write_ptr)));

	/* Copy MAC header from skb into command buffer */
	memcpy(tx_cmd->hdr, hdr, hdr_len);

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

	len_org = len;
	len = (len + 3) & ~3;

	if (len_org != len)
		len_org = 1;
	else
		len_org = 0;

	/* Physical address of this Tx command's header (not MAC header!),
	 * within command buffer array. */
	txcmd_phys = txq->dma_addr_cmd + sizeof(struct iwl_cmd) * idx +
		     offsetof(struct iwl_cmd, hdr);

	/* Add buffer containing Tx command and MAC(!) header to TFD's
	 * first entry */
	iwl_hw_txq_attach_buf_to_tfd(priv, tfd, txcmd_phys, len);

	if (!(info->flags & IEEE80211_TX_CTL_DO_NOT_ENCRYPT))
		iwl_tx_cmd_build_hwcrypto(priv, info, tx_cmd, skb, sta_id);

	/* Set up TFD's 2nd entry to point directly to remainder of skb,
	 * if any (802.11 null frames have no payload). */
	len = skb->len - hdr_len;
	if (len) {
		phys_addr = pci_map_single(priv->pci_dev, skb->data + hdr_len,
					   len, PCI_DMA_TODEVICE);
		iwl_hw_txq_attach_buf_to_tfd(priv, tfd, phys_addr, len);
	}

	/* Tell NIC about any 2-byte padding after MAC header */
	if (len_org)
		tx_cmd->tx_flags |= TX_CMD_FLG_MH_PAD_MSK;

	/* Total # bytes to be transmitted */
	len = (u16)skb->len;
	tx_cmd->len = cpu_to_le16(len);
	/* TODO need this for burst mode later on */
	iwl_tx_cmd_build_basic(priv, tx_cmd, info, hdr, unicast, sta_id);

	/* set is_hcca to 0; it probably will never be implemented */
	iwl_tx_cmd_build_rate(priv, tx_cmd, info, fc, sta_id, 0);

	iwl_update_tx_stats(priv, fc, len);

	scratch_phys = txcmd_phys + sizeof(struct iwl_cmd_header) +
		offsetof(struct iwl_tx_cmd, scratch);
	tx_cmd->dram_lsb_ptr = cpu_to_le32(scratch_phys);
	tx_cmd->dram_msb_ptr = iwl_get_dma_hi_address(scratch_phys);

	if (!ieee80211_get_morefrag(hdr)) {
		txq->need_update = 1;
		if (qc)
			priv->stations[sta_id].tid[tid].seq_number = seq_number;
	} else {
		wait_write_ptr = 1;
		txq->need_update = 0;
	}

	iwl_print_hex_dump(priv, IWL_DL_TX, (u8 *)tx_cmd, sizeof(*tx_cmd));

	iwl_print_hex_dump(priv, IWL_DL_TX, (u8 *)tx_cmd->hdr, hdr_len);

	/* Set up entry for this TFD in Tx byte-count array */
	priv->cfg->ops->lib->txq_update_byte_cnt_tbl(priv, txq, len);

	/* Tell device the write index *just past* this latest filled TFD */
	q->write_ptr = iwl_queue_inc_wrap(q->write_ptr, q->n_bd);
	ret = iwl_txq_update_write_ptr(priv, txq);
	spin_unlock_irqrestore(&priv->lock, flags);

	if (ret)
		return ret;

	if ((iwl_queue_space(q) < q->high_mark)
	    && priv->mac80211_registered) {
		if (wait_write_ptr) {
			spin_lock_irqsave(&priv->lock, flags);
			txq->need_update = 1;
			iwl_txq_update_write_ptr(priv, txq);
			spin_unlock_irqrestore(&priv->lock, flags);
		}

		ieee80211_stop_queue(priv->hw, skb_get_queue_mapping(skb));
	}

	return 0;

drop_unlock:
	spin_unlock_irqrestore(&priv->lock, flags);
drop:
	return -1;
}
EXPORT_SYMBOL(iwl_tx_skb);

/*************** HOST COMMAND QUEUE FUNCTIONS   *****/

/**
 * iwl_enqueue_hcmd - enqueue a uCode command
 * @priv: device private data point
 * @cmd: a point to the ucode command structure
 *
 * The function returns < 0 values to indicate the operation is
 * failed. On success, it turns the index (> 0) of command in the
 * command queue.
 */
int iwl_enqueue_hcmd(struct iwl_priv *priv, struct iwl_host_cmd *cmd)
{
	struct iwl_tx_queue *txq = &priv->txq[IWL_CMD_QUEUE_NUM];
	struct iwl_queue *q = &txq->q;
	struct iwl_tfd_frame *tfd;
	u32 *control_flags;
	struct iwl_cmd *out_cmd;
	u32 idx;
	u16 fix_size;
	dma_addr_t phys_addr;
	int ret;
	unsigned long flags;

	cmd->len = priv->cfg->ops->utils->get_hcmd_size(cmd->id, cmd->len);
	fix_size = (u16)(cmd->len + sizeof(out_cmd->hdr));

	/* If any of the command structures end up being larger than
	 * the TFD_MAX_PAYLOAD_SIZE, and it sent as a 'small' command then
	 * we will need to increase the size of the TFD entries */
	BUG_ON((fix_size > TFD_MAX_PAYLOAD_SIZE) &&
	       !(cmd->meta.flags & CMD_SIZE_HUGE));

	if (iwl_is_rfkill(priv)) {
		IWL_DEBUG_INFO("Not sending command - RF KILL");
		return -EIO;
	}

	if (iwl_queue_space(q) < ((cmd->meta.flags & CMD_ASYNC) ? 2 : 1)) {
		IWL_ERROR("No space for Tx\n");
		return -ENOSPC;
	}

	spin_lock_irqsave(&priv->hcmd_lock, flags);

	tfd = &txq->bd[q->write_ptr];
	memset(tfd, 0, sizeof(*tfd));

	control_flags = (u32 *) tfd;

	idx = get_cmd_index(q, q->write_ptr, cmd->meta.flags & CMD_SIZE_HUGE);
	out_cmd = &txq->cmd[idx];

	out_cmd->hdr.cmd = cmd->id;
	memcpy(&out_cmd->meta, &cmd->meta, sizeof(cmd->meta));
	memcpy(&out_cmd->cmd.payload, cmd->data, cmd->len);

	/* At this point, the out_cmd now has all of the incoming cmd
	 * information */

	out_cmd->hdr.flags = 0;
	out_cmd->hdr.sequence = cpu_to_le16(QUEUE_TO_SEQ(IWL_CMD_QUEUE_NUM) |
			INDEX_TO_SEQ(q->write_ptr));
	if (out_cmd->meta.flags & CMD_SIZE_HUGE)
		out_cmd->hdr.sequence |= cpu_to_le16(SEQ_HUGE_FRAME);

	phys_addr = txq->dma_addr_cmd + sizeof(txq->cmd[0]) * idx +
			offsetof(struct iwl_cmd, hdr);
	iwl_hw_txq_attach_buf_to_tfd(priv, tfd, phys_addr, fix_size);

	IWL_DEBUG_HC("Sending command %s (#%x), seq: 0x%04X, "
		     "%d bytes at %d[%d]:%d\n",
		     get_cmd_string(out_cmd->hdr.cmd),
		     out_cmd->hdr.cmd, le16_to_cpu(out_cmd->hdr.sequence),
		     fix_size, q->write_ptr, idx, IWL_CMD_QUEUE_NUM);

	txq->need_update = 1;

	/* Set up entry in queue's byte count circular buffer */
	priv->cfg->ops->lib->txq_update_byte_cnt_tbl(priv, txq, 0);

	/* Increment and update queue's write index */
	q->write_ptr = iwl_queue_inc_wrap(q->write_ptr, q->n_bd);
	ret = iwl_txq_update_write_ptr(priv, txq);

	spin_unlock_irqrestore(&priv->hcmd_lock, flags);
	return ret ? ret : idx;
}

