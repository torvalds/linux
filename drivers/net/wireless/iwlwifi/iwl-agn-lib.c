/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2010 Intel Corporation. All rights reserved.
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
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>

#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-helpers.h"
#include "iwl-agn-hw.h"
#include "iwl-agn.h"

static inline u32 iwlagn_get_scd_ssn(struct iwl5000_tx_resp *tx_resp)
{
	return le32_to_cpup((__le32 *)&tx_resp->status +
			    tx_resp->frame_count) & MAX_SN;
}

static int iwlagn_tx_status_reply_tx(struct iwl_priv *priv,
				      struct iwl_ht_agg *agg,
				      struct iwl5000_tx_resp *tx_resp,
				      int txq_id, u16 start_idx)
{
	u16 status;
	struct agg_tx_status *frame_status = &tx_resp->status;
	struct ieee80211_tx_info *info = NULL;
	struct ieee80211_hdr *hdr = NULL;
	u32 rate_n_flags = le32_to_cpu(tx_resp->rate_n_flags);
	int i, sh, idx;
	u16 seq;

	if (agg->wait_for_ba)
		IWL_DEBUG_TX_REPLY(priv, "got tx response w/o block-ack\n");

	agg->frame_count = tx_resp->frame_count;
	agg->start_idx = start_idx;
	agg->rate_n_flags = rate_n_flags;
	agg->bitmap = 0;

	/* # frames attempted by Tx command */
	if (agg->frame_count == 1) {
		/* Only one frame was attempted; no block-ack will arrive */
		status = le16_to_cpu(frame_status[0].status);
		idx = start_idx;

		/* FIXME: code repetition */
		IWL_DEBUG_TX_REPLY(priv, "FrameCnt = %d, StartIdx=%d idx=%d\n",
				   agg->frame_count, agg->start_idx, idx);

		info = IEEE80211_SKB_CB(priv->txq[txq_id].txb[idx].skb[0]);
		info->status.rates[0].count = tx_resp->failure_frame + 1;
		info->flags &= ~IEEE80211_TX_CTL_AMPDU;
		info->flags |= iwl_tx_status_to_mac80211(status);
		iwl_hwrate_to_tx_control(priv, rate_n_flags, info);

		/* FIXME: code repetition end */

		IWL_DEBUG_TX_REPLY(priv, "1 Frame 0x%x failure :%d\n",
				    status & 0xff, tx_resp->failure_frame);
		IWL_DEBUG_TX_REPLY(priv, "Rate Info rate_n_flags=%x\n", rate_n_flags);

		agg->wait_for_ba = 0;
	} else {
		/* Two or more frames were attempted; expect block-ack */
		u64 bitmap = 0;
		int start = agg->start_idx;

		/* Construct bit-map of pending frames within Tx window */
		for (i = 0; i < agg->frame_count; i++) {
			u16 sc;
			status = le16_to_cpu(frame_status[i].status);
			seq  = le16_to_cpu(frame_status[i].sequence);
			idx = SEQ_TO_INDEX(seq);
			txq_id = SEQ_TO_QUEUE(seq);

			if (status & (AGG_TX_STATE_FEW_BYTES_MSK |
				      AGG_TX_STATE_ABORT_MSK))
				continue;

			IWL_DEBUG_TX_REPLY(priv, "FrameCnt = %d, txq_id=%d idx=%d\n",
					   agg->frame_count, txq_id, idx);

			hdr = iwl_tx_queue_get_hdr(priv, txq_id, idx);
			if (!hdr) {
				IWL_ERR(priv,
					"BUG_ON idx doesn't point to valid skb"
					" idx=%d, txq_id=%d\n", idx, txq_id);
				return -1;
			}

			sc = le16_to_cpu(hdr->seq_ctrl);
			if (idx != (SEQ_TO_SN(sc) & 0xff)) {
				IWL_ERR(priv,
					"BUG_ON idx doesn't match seq control"
					" idx=%d, seq_idx=%d, seq=%d\n",
					  idx, SEQ_TO_SN(sc),
					  hdr->seq_ctrl);
				return -1;
			}

			IWL_DEBUG_TX_REPLY(priv, "AGG Frame i=%d idx %d seq=%d\n",
					   i, idx, SEQ_TO_SN(sc));

			sh = idx - start;
			if (sh > 64) {
				sh = (start - idx) + 0xff;
				bitmap = bitmap << sh;
				sh = 0;
				start = idx;
			} else if (sh < -64)
				sh  = 0xff - (start - idx);
			else if (sh < 0) {
				sh = start - idx;
				start = idx;
				bitmap = bitmap << sh;
				sh = 0;
			}
			bitmap |= 1ULL << sh;
			IWL_DEBUG_TX_REPLY(priv, "start=%d bitmap=0x%llx\n",
					   start, (unsigned long long)bitmap);
		}

		agg->bitmap = bitmap;
		agg->start_idx = start;
		IWL_DEBUG_TX_REPLY(priv, "Frames %d start_idx=%d bitmap=0x%llx\n",
				   agg->frame_count, agg->start_idx,
				   (unsigned long long)agg->bitmap);

		if (bitmap)
			agg->wait_for_ba = 1;
	}
	return 0;
}

static void iwlagn_rx_reply_tx(struct iwl_priv *priv,
				struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	u16 sequence = le16_to_cpu(pkt->hdr.sequence);
	int txq_id = SEQ_TO_QUEUE(sequence);
	int index = SEQ_TO_INDEX(sequence);
	struct iwl_tx_queue *txq = &priv->txq[txq_id];
	struct ieee80211_tx_info *info;
	struct iwl5000_tx_resp *tx_resp = (void *)&pkt->u.raw[0];
	u32  status = le16_to_cpu(tx_resp->status.status);
	int tid;
	int sta_id;
	int freed;

	if ((index >= txq->q.n_bd) || (iwl_queue_used(&txq->q, index) == 0)) {
		IWL_ERR(priv, "Read index for DMA queue txq_id (%d) index %d "
			  "is out of range [0-%d] %d %d\n", txq_id,
			  index, txq->q.n_bd, txq->q.write_ptr,
			  txq->q.read_ptr);
		return;
	}

	info = IEEE80211_SKB_CB(txq->txb[txq->q.read_ptr].skb[0]);
	memset(&info->status, 0, sizeof(info->status));

	tid = (tx_resp->ra_tid & IWL50_TX_RES_TID_MSK) >> IWL50_TX_RES_TID_POS;
	sta_id = (tx_resp->ra_tid & IWL50_TX_RES_RA_MSK) >> IWL50_TX_RES_RA_POS;

	if (txq->sched_retry) {
		const u32 scd_ssn = iwlagn_get_scd_ssn(tx_resp);
		struct iwl_ht_agg *agg = NULL;

		agg = &priv->stations[sta_id].tid[tid].agg;

		iwlagn_tx_status_reply_tx(priv, agg, tx_resp, txq_id, index);

		/* check if BAR is needed */
		if ((tx_resp->frame_count == 1) && !iwl_is_tx_success(status))
			info->flags |= IEEE80211_TX_STAT_AMPDU_NO_BACK;

		if (txq->q.read_ptr != (scd_ssn & 0xff)) {
			index = iwl_queue_dec_wrap(scd_ssn & 0xff, txq->q.n_bd);
			IWL_DEBUG_TX_REPLY(priv, "Retry scheduler reclaim "
					"scd_ssn=%d idx=%d txq=%d swq=%d\n",
					scd_ssn , index, txq_id, txq->swq_id);

			freed = iwlagn_tx_queue_reclaim(priv, txq_id, index);
			iwl_free_tfds_in_queue(priv, sta_id, tid, freed);

			if (priv->mac80211_registered &&
			    (iwl_queue_space(&txq->q) > txq->q.low_mark) &&
			    (agg->state != IWL_EMPTYING_HW_QUEUE_DELBA)) {
				if (agg->state == IWL_AGG_OFF)
					iwl_wake_queue(priv, txq_id);
				else
					iwl_wake_queue(priv, txq->swq_id);
			}
		}
	} else {
		BUG_ON(txq_id != txq->swq_id);

		info->status.rates[0].count = tx_resp->failure_frame + 1;
		info->flags |= iwl_tx_status_to_mac80211(status);
		iwl_hwrate_to_tx_control(priv,
					le32_to_cpu(tx_resp->rate_n_flags),
					info);

		IWL_DEBUG_TX_REPLY(priv, "TXQ %d status %s (0x%08x) rate_n_flags "
				   "0x%x retries %d\n",
				   txq_id,
				   iwl_get_tx_fail_reason(status), status,
				   le32_to_cpu(tx_resp->rate_n_flags),
				   tx_resp->failure_frame);

		freed = iwlagn_tx_queue_reclaim(priv, txq_id, index);
		iwl_free_tfds_in_queue(priv, sta_id, tid, freed);

		if (priv->mac80211_registered &&
		    (iwl_queue_space(&txq->q) > txq->q.low_mark))
			iwl_wake_queue(priv, txq_id);
	}

	iwlagn_txq_check_empty(priv, sta_id, tid, txq_id);

	if (iwl_check_bits(status, TX_ABORT_REQUIRED_MSK))
		IWL_ERR(priv, "TODO:  Implement Tx ABORT REQUIRED!!!\n");
}

void iwlagn_rx_handler_setup(struct iwl_priv *priv)
{
	/* init calibration handlers */
	priv->rx_handlers[CALIBRATION_RES_NOTIFICATION] =
					iwlagn_rx_calib_result;
	priv->rx_handlers[CALIBRATION_COMPLETE_NOTIFICATION] =
					iwlagn_rx_calib_complete;
	priv->rx_handlers[REPLY_TX] = iwlagn_rx_reply_tx;
}

void iwlagn_setup_deferred_work(struct iwl_priv *priv)
{
	/* in agn, the tx power calibration is done in uCode */
	priv->disable_tx_power_cal = 1;
}

int iwlagn_hw_valid_rtc_data_addr(u32 addr)
{
	return (addr >= IWLAGN_RTC_DATA_LOWER_BOUND) &&
		(addr < IWLAGN_RTC_DATA_UPPER_BOUND);
}

int iwlagn_send_tx_power(struct iwl_priv *priv)
{
	struct iwl5000_tx_power_dbm_cmd tx_power_cmd;
	u8 tx_ant_cfg_cmd;

	/* half dBm need to multiply */
	tx_power_cmd.global_lmt = (s8)(2 * priv->tx_power_user_lmt);

	if (priv->tx_power_lmt_in_half_dbm &&
	    priv->tx_power_lmt_in_half_dbm < tx_power_cmd.global_lmt) {
		/*
		 * For the newer devices which using enhanced/extend tx power
		 * table in EEPROM, the format is in half dBm. driver need to
		 * convert to dBm format before report to mac80211.
		 * By doing so, there is a possibility of 1/2 dBm resolution
		 * lost. driver will perform "round-up" operation before
		 * reporting, but it will cause 1/2 dBm tx power over the
		 * regulatory limit. Perform the checking here, if the
		 * "tx_power_user_lmt" is higher than EEPROM value (in
		 * half-dBm format), lower the tx power based on EEPROM
		 */
		tx_power_cmd.global_lmt = priv->tx_power_lmt_in_half_dbm;
	}
	tx_power_cmd.flags = IWL50_TX_POWER_NO_CLOSED;
	tx_power_cmd.srv_chan_lmt = IWL50_TX_POWER_AUTO;

	if (IWL_UCODE_API(priv->ucode_ver) == 1)
		tx_ant_cfg_cmd = REPLY_TX_POWER_DBM_CMD_V1;
	else
		tx_ant_cfg_cmd = REPLY_TX_POWER_DBM_CMD;

	return  iwl_send_cmd_pdu_async(priv, tx_ant_cfg_cmd,
				       sizeof(tx_power_cmd), &tx_power_cmd,
				       NULL);
}

void iwlagn_temperature(struct iwl_priv *priv)
{
	/* store temperature from statistics (in Celsius) */
	priv->temperature = le32_to_cpu(priv->statistics.general.temperature);
	iwl_tt_handler(priv);
}

u16 iwlagn_eeprom_calib_version(struct iwl_priv *priv)
{
	struct iwl_eeprom_calib_hdr {
		u8 version;
		u8 pa_type;
		u16 voltage;
	} *hdr;

	hdr = (struct iwl_eeprom_calib_hdr *)iwl_eeprom_query_addr(priv,
							EEPROM_5000_CALIB_ALL);
	return hdr->version;

}

/*
 * EEPROM
 */
static u32 eeprom_indirect_address(const struct iwl_priv *priv, u32 address)
{
	u16 offset = 0;

	if ((address & INDIRECT_ADDRESS) == 0)
		return address;

	switch (address & INDIRECT_TYPE_MSK) {
	case INDIRECT_HOST:
		offset = iwl_eeprom_query16(priv, EEPROM_5000_LINK_HOST);
		break;
	case INDIRECT_GENERAL:
		offset = iwl_eeprom_query16(priv, EEPROM_5000_LINK_GENERAL);
		break;
	case INDIRECT_REGULATORY:
		offset = iwl_eeprom_query16(priv, EEPROM_5000_LINK_REGULATORY);
		break;
	case INDIRECT_CALIBRATION:
		offset = iwl_eeprom_query16(priv, EEPROM_5000_LINK_CALIBRATION);
		break;
	case INDIRECT_PROCESS_ADJST:
		offset = iwl_eeprom_query16(priv, EEPROM_5000_LINK_PROCESS_ADJST);
		break;
	case INDIRECT_OTHERS:
		offset = iwl_eeprom_query16(priv, EEPROM_5000_LINK_OTHERS);
		break;
	default:
		IWL_ERR(priv, "illegal indirect type: 0x%X\n",
		address & INDIRECT_TYPE_MSK);
		break;
	}

	/* translate the offset from words to byte */
	return (address & ADDRESS_MSK) + (offset << 1);
}

const u8 *iwlagn_eeprom_query_addr(const struct iwl_priv *priv,
					   size_t offset)
{
	u32 address = eeprom_indirect_address(priv, offset);
	BUG_ON(address >= priv->cfg->eeprom_size);
	return &priv->eeprom[address];
}

struct iwl_mod_params iwlagn_mod_params = {
	.amsdu_size_8K = 1,
	.restart_fw = 1,
	/* the rest are 0 by default */
};

void iwlagn_rx_queue_reset(struct iwl_priv *priv, struct iwl_rx_queue *rxq)
{
	unsigned long flags;
	int i;
	spin_lock_irqsave(&rxq->lock, flags);
	INIT_LIST_HEAD(&rxq->rx_free);
	INIT_LIST_HEAD(&rxq->rx_used);
	/* Fill the rx_used queue with _all_ of the Rx buffers */
	for (i = 0; i < RX_FREE_BUFFERS + RX_QUEUE_SIZE; i++) {
		/* In the reset function, these buffers may have been allocated
		 * to an SKB, so we need to unmap and free potential storage */
		if (rxq->pool[i].page != NULL) {
			pci_unmap_page(priv->pci_dev, rxq->pool[i].page_dma,
				PAGE_SIZE << priv->hw_params.rx_page_order,
				PCI_DMA_FROMDEVICE);
			__iwl_free_pages(priv, rxq->pool[i].page);
			rxq->pool[i].page = NULL;
		}
		list_add_tail(&rxq->pool[i].list, &rxq->rx_used);
	}

	/* Set us so that we have processed and used all buffers, but have
	 * not restocked the Rx queue with fresh buffers */
	rxq->read = rxq->write = 0;
	rxq->write_actual = 0;
	rxq->free_count = 0;
	spin_unlock_irqrestore(&rxq->lock, flags);
}

int iwlagn_rx_init(struct iwl_priv *priv, struct iwl_rx_queue *rxq)
{
	u32 rb_size;
	const u32 rfdnlog = RX_QUEUE_SIZE_LOG; /* 256 RBDs */
	u32 rb_timeout = 0; /* FIXME: RX_RB_TIMEOUT for all devices? */

	if (!priv->cfg->use_isr_legacy)
		rb_timeout = RX_RB_TIMEOUT;

	if (priv->cfg->mod_params->amsdu_size_8K)
		rb_size = FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_8K;
	else
		rb_size = FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_4K;

	/* Stop Rx DMA */
	iwl_write_direct32(priv, FH_MEM_RCSR_CHNL0_CONFIG_REG, 0);

	/* Reset driver's Rx queue write index */
	iwl_write_direct32(priv, FH_RSCSR_CHNL0_RBDCB_WPTR_REG, 0);

	/* Tell device where to find RBD circular buffer in DRAM */
	iwl_write_direct32(priv, FH_RSCSR_CHNL0_RBDCB_BASE_REG,
			   (u32)(rxq->dma_addr >> 8));

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

	return 0;
}

int iwlagn_hw_nic_init(struct iwl_priv *priv)
{
	unsigned long flags;
	struct iwl_rx_queue *rxq = &priv->rxq;
	int ret;

	/* nic_init */
	spin_lock_irqsave(&priv->lock, flags);
	priv->cfg->ops->lib->apm_ops.init(priv);

	/* Set interrupt coalescing calibration timer to default (512 usecs) */
	iwl_write8(priv, CSR_INT_COALESCING, IWL_HOST_INT_CALIB_TIMEOUT_DEF);

	spin_unlock_irqrestore(&priv->lock, flags);

	ret = priv->cfg->ops->lib->apm_ops.set_pwr_src(priv, IWL_PWR_SRC_VMAIN);

	priv->cfg->ops->lib->apm_ops.config(priv);

	/* Allocate the RX queue, or reset if it is already allocated */
	if (!rxq->bd) {
		ret = iwl_rx_queue_alloc(priv);
		if (ret) {
			IWL_ERR(priv, "Unable to initialize Rx queue\n");
			return -ENOMEM;
		}
	} else
		iwlagn_rx_queue_reset(priv, rxq);

	iwlagn_rx_replenish(priv);

	iwlagn_rx_init(priv, rxq);

	spin_lock_irqsave(&priv->lock, flags);

	rxq->need_update = 1;
	iwl_rx_queue_update_write_ptr(priv, rxq);

	spin_unlock_irqrestore(&priv->lock, flags);

	/* Allocate and init all Tx and Command queues */
	ret = iwlagn_txq_ctx_reset(priv);
	if (ret)
		return ret;

	set_bit(STATUS_INIT, &priv->status);

	return 0;
}

/**
 * iwlagn_dma_addr2rbd_ptr - convert a DMA address to a uCode read buffer ptr
 */
static inline __le32 iwlagn_dma_addr2rbd_ptr(struct iwl_priv *priv,
					  dma_addr_t dma_addr)
{
	return cpu_to_le32((u32)(dma_addr >> 8));
}

/**
 * iwlagn_rx_queue_restock - refill RX queue from pre-allocated pool
 *
 * If there are slots in the RX queue that need to be restocked,
 * and we have free pre-allocated buffers, fill the ranks as much
 * as we can, pulling from rx_free.
 *
 * This moves the 'write' index forward to catch up with 'processed', and
 * also updates the memory address in the firmware to reference the new
 * target buffer.
 */
void iwlagn_rx_queue_restock(struct iwl_priv *priv)
{
	struct iwl_rx_queue *rxq = &priv->rxq;
	struct list_head *element;
	struct iwl_rx_mem_buffer *rxb;
	unsigned long flags;
	int write;

	spin_lock_irqsave(&rxq->lock, flags);
	write = rxq->write & ~0x7;
	while ((iwl_rx_queue_space(rxq) > 0) && (rxq->free_count)) {
		/* Get next free Rx buffer, remove from free list */
		element = rxq->rx_free.next;
		rxb = list_entry(element, struct iwl_rx_mem_buffer, list);
		list_del(element);

		/* Point to Rx buffer via next RBD in circular buffer */
		rxq->bd[rxq->write] = iwlagn_dma_addr2rbd_ptr(priv,
							      rxb->page_dma);
		rxq->queue[rxq->write] = rxb;
		rxq->write = (rxq->write + 1) & RX_QUEUE_MASK;
		rxq->free_count--;
	}
	spin_unlock_irqrestore(&rxq->lock, flags);
	/* If the pre-allocated buffer pool is dropping low, schedule to
	 * refill it */
	if (rxq->free_count <= RX_LOW_WATERMARK)
		queue_work(priv->workqueue, &priv->rx_replenish);


	/* If we've added more space for the firmware to place data, tell it.
	 * Increment device's write pointer in multiples of 8. */
	if (rxq->write_actual != (rxq->write & ~0x7)) {
		spin_lock_irqsave(&rxq->lock, flags);
		rxq->need_update = 1;
		spin_unlock_irqrestore(&rxq->lock, flags);
		iwl_rx_queue_update_write_ptr(priv, rxq);
	}
}

/**
 * iwlagn_rx_replenish - Move all used packet from rx_used to rx_free
 *
 * When moving to rx_free an SKB is allocated for the slot.
 *
 * Also restock the Rx queue via iwl_rx_queue_restock.
 * This is called as a scheduled work item (except for during initialization)
 */
void iwlagn_rx_allocate(struct iwl_priv *priv, gfp_t priority)
{
	struct iwl_rx_queue *rxq = &priv->rxq;
	struct list_head *element;
	struct iwl_rx_mem_buffer *rxb;
	struct page *page;
	unsigned long flags;
	gfp_t gfp_mask = priority;

	while (1) {
		spin_lock_irqsave(&rxq->lock, flags);
		if (list_empty(&rxq->rx_used)) {
			spin_unlock_irqrestore(&rxq->lock, flags);
			return;
		}
		spin_unlock_irqrestore(&rxq->lock, flags);

		if (rxq->free_count > RX_LOW_WATERMARK)
			gfp_mask |= __GFP_NOWARN;

		if (priv->hw_params.rx_page_order > 0)
			gfp_mask |= __GFP_COMP;

		/* Alloc a new receive buffer */
		page = alloc_pages(gfp_mask, priv->hw_params.rx_page_order);
		if (!page) {
			if (net_ratelimit())
				IWL_DEBUG_INFO(priv, "alloc_pages failed, "
					       "order: %d\n",
					       priv->hw_params.rx_page_order);

			if ((rxq->free_count <= RX_LOW_WATERMARK) &&
			    net_ratelimit())
				IWL_CRIT(priv, "Failed to alloc_pages with %s. Only %u free buffers remaining.\n",
					 priority == GFP_ATOMIC ?  "GFP_ATOMIC" : "GFP_KERNEL",
					 rxq->free_count);
			/* We don't reschedule replenish work here -- we will
			 * call the restock method and if it still needs
			 * more buffers it will schedule replenish */
			return;
		}

		spin_lock_irqsave(&rxq->lock, flags);

		if (list_empty(&rxq->rx_used)) {
			spin_unlock_irqrestore(&rxq->lock, flags);
			__free_pages(page, priv->hw_params.rx_page_order);
			return;
		}
		element = rxq->rx_used.next;
		rxb = list_entry(element, struct iwl_rx_mem_buffer, list);
		list_del(element);

		spin_unlock_irqrestore(&rxq->lock, flags);

		rxb->page = page;
		/* Get physical address of the RB */
		rxb->page_dma = pci_map_page(priv->pci_dev, page, 0,
				PAGE_SIZE << priv->hw_params.rx_page_order,
				PCI_DMA_FROMDEVICE);
		/* dma address must be no more than 36 bits */
		BUG_ON(rxb->page_dma & ~DMA_BIT_MASK(36));
		/* and also 256 byte aligned! */
		BUG_ON(rxb->page_dma & DMA_BIT_MASK(8));

		spin_lock_irqsave(&rxq->lock, flags);

		list_add_tail(&rxb->list, &rxq->rx_free);
		rxq->free_count++;
		priv->alloc_rxb_page++;

		spin_unlock_irqrestore(&rxq->lock, flags);
	}
}

void iwlagn_rx_replenish(struct iwl_priv *priv)
{
	unsigned long flags;

	iwlagn_rx_allocate(priv, GFP_KERNEL);

	spin_lock_irqsave(&priv->lock, flags);
	iwlagn_rx_queue_restock(priv);
	spin_unlock_irqrestore(&priv->lock, flags);
}

void iwlagn_rx_replenish_now(struct iwl_priv *priv)
{
	iwlagn_rx_allocate(priv, GFP_ATOMIC);

	iwlagn_rx_queue_restock(priv);
}

/* Assumes that the skb field of the buffers in 'pool' is kept accurate.
 * If an SKB has been detached, the POOL needs to have its SKB set to NULL
 * This free routine walks the list of POOL entries and if SKB is set to
 * non NULL it is unmapped and freed
 */
void iwlagn_rx_queue_free(struct iwl_priv *priv, struct iwl_rx_queue *rxq)
{
	int i;
	for (i = 0; i < RX_QUEUE_SIZE + RX_FREE_BUFFERS; i++) {
		if (rxq->pool[i].page != NULL) {
			pci_unmap_page(priv->pci_dev, rxq->pool[i].page_dma,
				PAGE_SIZE << priv->hw_params.rx_page_order,
				PCI_DMA_FROMDEVICE);
			__iwl_free_pages(priv, rxq->pool[i].page);
			rxq->pool[i].page = NULL;
		}
	}

	dma_free_coherent(&priv->pci_dev->dev, 4 * RX_QUEUE_SIZE, rxq->bd,
			  rxq->dma_addr);
	dma_free_coherent(&priv->pci_dev->dev, sizeof(struct iwl_rb_status),
			  rxq->rb_stts, rxq->rb_stts_dma);
	rxq->bd = NULL;
	rxq->rb_stts  = NULL;
}

int iwlagn_rxq_stop(struct iwl_priv *priv)
{

	/* stop Rx DMA */
	iwl_write_direct32(priv, FH_MEM_RCSR_CHNL0_CONFIG_REG, 0);
	iwl_poll_direct_bit(priv, FH_MEM_RSSR_RX_STATUS_REG,
			    FH_RSSR_CHNL0_RX_STATUS_CHNL_IDLE, 1000);

	return 0;
}
