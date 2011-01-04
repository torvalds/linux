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
#include <linux/etherdevice.h>
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
#include "iwl-sta.h"

static inline u32 iwlagn_get_scd_ssn(struct iwlagn_tx_resp *tx_resp)
{
	return le32_to_cpup((__le32 *)&tx_resp->status +
			    tx_resp->frame_count) & MAX_SN;
}

static void iwlagn_count_tx_err_status(struct iwl_priv *priv, u16 status)
{
	status &= TX_STATUS_MSK;

	switch (status) {
	case TX_STATUS_POSTPONE_DELAY:
		priv->_agn.reply_tx_stats.pp_delay++;
		break;
	case TX_STATUS_POSTPONE_FEW_BYTES:
		priv->_agn.reply_tx_stats.pp_few_bytes++;
		break;
	case TX_STATUS_POSTPONE_BT_PRIO:
		priv->_agn.reply_tx_stats.pp_bt_prio++;
		break;
	case TX_STATUS_POSTPONE_QUIET_PERIOD:
		priv->_agn.reply_tx_stats.pp_quiet_period++;
		break;
	case TX_STATUS_POSTPONE_CALC_TTAK:
		priv->_agn.reply_tx_stats.pp_calc_ttak++;
		break;
	case TX_STATUS_FAIL_INTERNAL_CROSSED_RETRY:
		priv->_agn.reply_tx_stats.int_crossed_retry++;
		break;
	case TX_STATUS_FAIL_SHORT_LIMIT:
		priv->_agn.reply_tx_stats.short_limit++;
		break;
	case TX_STATUS_FAIL_LONG_LIMIT:
		priv->_agn.reply_tx_stats.long_limit++;
		break;
	case TX_STATUS_FAIL_FIFO_UNDERRUN:
		priv->_agn.reply_tx_stats.fifo_underrun++;
		break;
	case TX_STATUS_FAIL_DRAIN_FLOW:
		priv->_agn.reply_tx_stats.drain_flow++;
		break;
	case TX_STATUS_FAIL_RFKILL_FLUSH:
		priv->_agn.reply_tx_stats.rfkill_flush++;
		break;
	case TX_STATUS_FAIL_LIFE_EXPIRE:
		priv->_agn.reply_tx_stats.life_expire++;
		break;
	case TX_STATUS_FAIL_DEST_PS:
		priv->_agn.reply_tx_stats.dest_ps++;
		break;
	case TX_STATUS_FAIL_HOST_ABORTED:
		priv->_agn.reply_tx_stats.host_abort++;
		break;
	case TX_STATUS_FAIL_BT_RETRY:
		priv->_agn.reply_tx_stats.bt_retry++;
		break;
	case TX_STATUS_FAIL_STA_INVALID:
		priv->_agn.reply_tx_stats.sta_invalid++;
		break;
	case TX_STATUS_FAIL_FRAG_DROPPED:
		priv->_agn.reply_tx_stats.frag_drop++;
		break;
	case TX_STATUS_FAIL_TID_DISABLE:
		priv->_agn.reply_tx_stats.tid_disable++;
		break;
	case TX_STATUS_FAIL_FIFO_FLUSHED:
		priv->_agn.reply_tx_stats.fifo_flush++;
		break;
	case TX_STATUS_FAIL_INSUFFICIENT_CF_POLL:
		priv->_agn.reply_tx_stats.insuff_cf_poll++;
		break;
	case TX_STATUS_FAIL_PASSIVE_NO_RX:
		priv->_agn.reply_tx_stats.fail_hw_drop++;
		break;
	case TX_STATUS_FAIL_NO_BEACON_ON_RADAR:
		priv->_agn.reply_tx_stats.sta_color_mismatch++;
		break;
	default:
		priv->_agn.reply_tx_stats.unknown++;
		break;
	}
}

static void iwlagn_count_agg_tx_err_status(struct iwl_priv *priv, u16 status)
{
	status &= AGG_TX_STATUS_MSK;

	switch (status) {
	case AGG_TX_STATE_UNDERRUN_MSK:
		priv->_agn.reply_agg_tx_stats.underrun++;
		break;
	case AGG_TX_STATE_BT_PRIO_MSK:
		priv->_agn.reply_agg_tx_stats.bt_prio++;
		break;
	case AGG_TX_STATE_FEW_BYTES_MSK:
		priv->_agn.reply_agg_tx_stats.few_bytes++;
		break;
	case AGG_TX_STATE_ABORT_MSK:
		priv->_agn.reply_agg_tx_stats.abort++;
		break;
	case AGG_TX_STATE_LAST_SENT_TTL_MSK:
		priv->_agn.reply_agg_tx_stats.last_sent_ttl++;
		break;
	case AGG_TX_STATE_LAST_SENT_TRY_CNT_MSK:
		priv->_agn.reply_agg_tx_stats.last_sent_try++;
		break;
	case AGG_TX_STATE_LAST_SENT_BT_KILL_MSK:
		priv->_agn.reply_agg_tx_stats.last_sent_bt_kill++;
		break;
	case AGG_TX_STATE_SCD_QUERY_MSK:
		priv->_agn.reply_agg_tx_stats.scd_query++;
		break;
	case AGG_TX_STATE_TEST_BAD_CRC32_MSK:
		priv->_agn.reply_agg_tx_stats.bad_crc32++;
		break;
	case AGG_TX_STATE_RESPONSE_MSK:
		priv->_agn.reply_agg_tx_stats.response++;
		break;
	case AGG_TX_STATE_DUMP_TX_MSK:
		priv->_agn.reply_agg_tx_stats.dump_tx++;
		break;
	case AGG_TX_STATE_DELAY_TX_MSK:
		priv->_agn.reply_agg_tx_stats.delay_tx++;
		break;
	default:
		priv->_agn.reply_agg_tx_stats.unknown++;
		break;
	}
}

static void iwlagn_set_tx_status(struct iwl_priv *priv,
				 struct ieee80211_tx_info *info,
				 struct iwlagn_tx_resp *tx_resp,
				 int txq_id, bool is_agg)
{
	u16  status = le16_to_cpu(tx_resp->status.status);

	info->status.rates[0].count = tx_resp->failure_frame + 1;
	if (is_agg)
		info->flags &= ~IEEE80211_TX_CTL_AMPDU;
	info->flags |= iwl_tx_status_to_mac80211(status);
	iwlagn_hwrate_to_tx_control(priv, le32_to_cpu(tx_resp->rate_n_flags),
				    info);
	if (!iwl_is_tx_success(status))
		iwlagn_count_tx_err_status(priv, status);

	IWL_DEBUG_TX_REPLY(priv, "TXQ %d status %s (0x%08x) rate_n_flags "
			   "0x%x retries %d\n",
			   txq_id,
			   iwl_get_tx_fail_reason(status), status,
			   le32_to_cpu(tx_resp->rate_n_flags),
			   tx_resp->failure_frame);
}

#ifdef CONFIG_IWLWIFI_DEBUG
#define AGG_TX_STATE_FAIL(x) case AGG_TX_STATE_ ## x: return #x

const char *iwl_get_agg_tx_fail_reason(u16 status)
{
	status &= AGG_TX_STATUS_MSK;
	switch (status) {
	case AGG_TX_STATE_TRANSMITTED:
		return "SUCCESS";
		AGG_TX_STATE_FAIL(UNDERRUN_MSK);
		AGG_TX_STATE_FAIL(BT_PRIO_MSK);
		AGG_TX_STATE_FAIL(FEW_BYTES_MSK);
		AGG_TX_STATE_FAIL(ABORT_MSK);
		AGG_TX_STATE_FAIL(LAST_SENT_TTL_MSK);
		AGG_TX_STATE_FAIL(LAST_SENT_TRY_CNT_MSK);
		AGG_TX_STATE_FAIL(LAST_SENT_BT_KILL_MSK);
		AGG_TX_STATE_FAIL(SCD_QUERY_MSK);
		AGG_TX_STATE_FAIL(TEST_BAD_CRC32_MSK);
		AGG_TX_STATE_FAIL(RESPONSE_MSK);
		AGG_TX_STATE_FAIL(DUMP_TX_MSK);
		AGG_TX_STATE_FAIL(DELAY_TX_MSK);
	}

	return "UNKNOWN";
}
#endif /* CONFIG_IWLWIFI_DEBUG */

static int iwlagn_tx_status_reply_tx(struct iwl_priv *priv,
				      struct iwl_ht_agg *agg,
				      struct iwlagn_tx_resp *tx_resp,
				      int txq_id, u16 start_idx)
{
	u16 status;
	struct agg_tx_status *frame_status = &tx_resp->status;
	struct ieee80211_hdr *hdr = NULL;
	int i, sh, idx;
	u16 seq;

	if (agg->wait_for_ba)
		IWL_DEBUG_TX_REPLY(priv, "got tx response w/o block-ack\n");

	agg->frame_count = tx_resp->frame_count;
	agg->start_idx = start_idx;
	agg->rate_n_flags = le32_to_cpu(tx_resp->rate_n_flags);
	agg->bitmap = 0;

	/* # frames attempted by Tx command */
	if (agg->frame_count == 1) {
		/* Only one frame was attempted; no block-ack will arrive */
		idx = start_idx;

		IWL_DEBUG_TX_REPLY(priv, "FrameCnt = %d, StartIdx=%d idx=%d\n",
				   agg->frame_count, agg->start_idx, idx);
		iwlagn_set_tx_status(priv,
				     IEEE80211_SKB_CB(
					priv->txq[txq_id].txb[idx].skb),
				     tx_resp, txq_id, true);
		agg->wait_for_ba = 0;
	} else {
		/* Two or more frames were attempted; expect block-ack */
		u64 bitmap = 0;

		/*
		 * Start is the lowest frame sent. It may not be the first
		 * frame in the batch; we figure this out dynamically during
		 * the following loop.
		 */
		int start = agg->start_idx;

		/* Construct bit-map of pending frames within Tx window */
		for (i = 0; i < agg->frame_count; i++) {
			u16 sc;
			status = le16_to_cpu(frame_status[i].status);
			seq  = le16_to_cpu(frame_status[i].sequence);
			idx = SEQ_TO_INDEX(seq);
			txq_id = SEQ_TO_QUEUE(seq);

			if (status & AGG_TX_STATUS_MSK)
				iwlagn_count_agg_tx_err_status(priv, status);

			if (status & (AGG_TX_STATE_FEW_BYTES_MSK |
				      AGG_TX_STATE_ABORT_MSK))
				continue;

			IWL_DEBUG_TX_REPLY(priv, "FrameCnt = %d, txq_id=%d idx=%d\n",
					   agg->frame_count, txq_id, idx);
			IWL_DEBUG_TX_REPLY(priv, "status %s (0x%08x), "
					   "try-count (0x%08x)\n",
					   iwl_get_agg_tx_fail_reason(status),
					   status & AGG_TX_STATUS_MSK,
					   status & AGG_TX_TRY_MSK);

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

			/*
			 * sh -> how many frames ahead of the starting frame is
			 * the current one?
			 *
			 * Note that all frames sent in the batch must be in a
			 * 64-frame window, so this number should be in [0,63].
			 * If outside of this window, then we've found a new
			 * "first" frame in the batch and need to change start.
			 */
			sh = idx - start;

			/*
			 * If >= 64, out of window. start must be at the front
			 * of the circular buffer, idx must be near the end of
			 * the buffer, and idx is the new "first" frame. Shift
			 * the indices around.
			 */
			if (sh >= 64) {
				/* Shift bitmap by start - idx, wrapped */
				sh = 0x100 - idx + start;
				bitmap = bitmap << sh;
				/* Now idx is the new start so sh = 0 */
				sh = 0;
				start = idx;
			/*
			 * If <= -64 then wraps the 256-pkt circular buffer
			 * (e.g., start = 255 and idx = 0, sh should be 1)
			 */
			} else if (sh <= -64) {
				sh  = 0x100 - start + idx;
			/*
			 * If < 0 but > -64, out of window. idx is before start
			 * but not wrapped. Shift the indices around.
			 */
			} else if (sh < 0) {
				/* Shift by how far start is ahead of idx */
				sh = start - idx;
				bitmap = bitmap << sh;
				/* Now idx is the new start so sh = 0 */
				start = idx;
				sh = 0;
			}
			/* Sequence number start + sh was sent in this batch */
			bitmap |= 1ULL << sh;
			IWL_DEBUG_TX_REPLY(priv, "start=%d bitmap=0x%llx\n",
					   start, (unsigned long long)bitmap);
		}

		/*
		 * Store the bitmap and possibly the new start, if we wrapped
		 * the buffer above
		 */
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

void iwl_check_abort_status(struct iwl_priv *priv,
			    u8 frame_count, u32 status)
{
	if (frame_count == 1 && status == TX_STATUS_FAIL_RFKILL_FLUSH) {
		IWL_ERR(priv, "Tx flush command to flush out all frames\n");
		if (!test_bit(STATUS_EXIT_PENDING, &priv->status))
			queue_work(priv->workqueue, &priv->tx_flush);
	}
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
	struct iwlagn_tx_resp *tx_resp = (void *)&pkt->u.raw[0];
	u32  status = le16_to_cpu(tx_resp->status.status);
	int tid;
	int sta_id;
	int freed;
	unsigned long flags;

	if ((index >= txq->q.n_bd) || (iwl_queue_used(&txq->q, index) == 0)) {
		IWL_ERR(priv, "Read index for DMA queue txq_id (%d) index %d "
			  "is out of range [0-%d] %d %d\n", txq_id,
			  index, txq->q.n_bd, txq->q.write_ptr,
			  txq->q.read_ptr);
		return;
	}

	info = IEEE80211_SKB_CB(txq->txb[txq->q.read_ptr].skb);
	memset(&info->status, 0, sizeof(info->status));

	tid = (tx_resp->ra_tid & IWLAGN_TX_RES_TID_MSK) >>
		IWLAGN_TX_RES_TID_POS;
	sta_id = (tx_resp->ra_tid & IWLAGN_TX_RES_RA_MSK) >>
		IWLAGN_TX_RES_RA_POS;

	spin_lock_irqsave(&priv->sta_lock, flags);
	if (txq->sched_retry) {
		const u32 scd_ssn = iwlagn_get_scd_ssn(tx_resp);
		struct iwl_ht_agg *agg;

		agg = &priv->stations[sta_id].tid[tid].agg;
		/*
		 * If the BT kill count is non-zero, we'll get this
		 * notification again.
		 */
		if (tx_resp->bt_kill_count && tx_resp->frame_count == 1 &&
		    priv->cfg->bt_params &&
		    priv->cfg->bt_params->advanced_bt_coexist) {
			IWL_WARN(priv, "receive reply tx with bt_kill\n");
		}
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
		iwlagn_set_tx_status(priv, info, tx_resp, txq_id, false);
		freed = iwlagn_tx_queue_reclaim(priv, txq_id, index);
		iwl_free_tfds_in_queue(priv, sta_id, tid, freed);

		if (priv->mac80211_registered &&
		    (iwl_queue_space(&txq->q) > txq->q.low_mark))
			iwl_wake_queue(priv, txq_id);
	}

	iwlagn_txq_check_empty(priv, sta_id, tid, txq_id);

	iwl_check_abort_status(priv, tx_resp->frame_count, status);
	spin_unlock_irqrestore(&priv->sta_lock, flags);
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
	struct iwlagn_tx_power_dbm_cmd tx_power_cmd;
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
	tx_power_cmd.flags = IWLAGN_TX_POWER_NO_CLOSED;
	tx_power_cmd.srv_chan_lmt = IWLAGN_TX_POWER_AUTO;

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
	priv->temperature =
		le32_to_cpu(priv->_agn.statistics.general.common.temperature);
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
							EEPROM_CALIB_ALL);
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
		offset = iwl_eeprom_query16(priv, EEPROM_LINK_HOST);
		break;
	case INDIRECT_GENERAL:
		offset = iwl_eeprom_query16(priv, EEPROM_LINK_GENERAL);
		break;
	case INDIRECT_REGULATORY:
		offset = iwl_eeprom_query16(priv, EEPROM_LINK_REGULATORY);
		break;
	case INDIRECT_TXP_LIMIT:
		offset = iwl_eeprom_query16(priv, EEPROM_LINK_TXP_LIMIT);
		break;
	case INDIRECT_TXP_LIMIT_SIZE:
		offset = iwl_eeprom_query16(priv, EEPROM_LINK_TXP_LIMIT_SIZE);
		break;
	case INDIRECT_CALIBRATION:
		offset = iwl_eeprom_query16(priv, EEPROM_LINK_CALIBRATION);
		break;
	case INDIRECT_PROCESS_ADJST:
		offset = iwl_eeprom_query16(priv, EEPROM_LINK_PROCESS_ADJST);
		break;
	case INDIRECT_OTHERS:
		offset = iwl_eeprom_query16(priv, EEPROM_LINK_OTHERS);
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
	BUG_ON(address >= priv->cfg->base_params->eeprom_size);
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

	for (i = 0; i < RX_QUEUE_SIZE; i++)
		rxq->queue[i] = NULL;

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

	if (!priv->cfg->base_params->use_isr_legacy)
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

	return 0;
}

static void iwlagn_set_pwr_vmain(struct iwl_priv *priv)
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

	iwlagn_set_pwr_vmain(priv);

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

	/* Allocate or reset and init all Tx and Command queues */
	if (!priv->txq) {
		ret = iwlagn_txq_ctx_alloc(priv);
		if (ret)
			return ret;
	} else
		iwlagn_txq_ctx_reset(priv);

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

	spin_lock_irqsave(&rxq->lock, flags);
	while ((iwl_rx_queue_space(rxq) > 0) && (rxq->free_count)) {
		/* The overwritten rxb must be a used one */
		rxb = rxq->queue[rxq->write];
		BUG_ON(rxb && rxb->page);

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

		BUG_ON(rxb->page);
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
			  rxq->bd_dma);
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

int iwlagn_hwrate_to_mac80211_idx(u32 rate_n_flags, enum ieee80211_band band)
{
	int idx = 0;
	int band_offset = 0;

	/* HT rate format: mac80211 wants an MCS number, which is just LSB */
	if (rate_n_flags & RATE_MCS_HT_MSK) {
		idx = (rate_n_flags & 0xff);
		return idx;
	/* Legacy rate format, search for match in table */
	} else {
		if (band == IEEE80211_BAND_5GHZ)
			band_offset = IWL_FIRST_OFDM_RATE;
		for (idx = band_offset; idx < IWL_RATE_COUNT_LEGACY; idx++)
			if (iwl_rates[idx].plcp == (rate_n_flags & 0xFF))
				return idx - band_offset;
	}

	return -1;
}

/* Calc max signal level (dBm) among 3 possible receivers */
static inline int iwlagn_calc_rssi(struct iwl_priv *priv,
				struct iwl_rx_phy_res *rx_resp)
{
	return priv->cfg->ops->utils->calc_rssi(priv, rx_resp);
}

static u32 iwlagn_translate_rx_status(struct iwl_priv *priv, u32 decrypt_in)
{
	u32 decrypt_out = 0;

	if ((decrypt_in & RX_RES_STATUS_STATION_FOUND) ==
					RX_RES_STATUS_STATION_FOUND)
		decrypt_out |= (RX_RES_STATUS_STATION_FOUND |
				RX_RES_STATUS_NO_STATION_INFO_MISMATCH);

	decrypt_out |= (decrypt_in & RX_RES_STATUS_SEC_TYPE_MSK);

	/* packet was not encrypted */
	if ((decrypt_in & RX_RES_STATUS_SEC_TYPE_MSK) ==
					RX_RES_STATUS_SEC_TYPE_NONE)
		return decrypt_out;

	/* packet was encrypted with unknown alg */
	if ((decrypt_in & RX_RES_STATUS_SEC_TYPE_MSK) ==
					RX_RES_STATUS_SEC_TYPE_ERR)
		return decrypt_out;

	/* decryption was not done in HW */
	if ((decrypt_in & RX_MPDU_RES_STATUS_DEC_DONE_MSK) !=
					RX_MPDU_RES_STATUS_DEC_DONE_MSK)
		return decrypt_out;

	switch (decrypt_in & RX_RES_STATUS_SEC_TYPE_MSK) {

	case RX_RES_STATUS_SEC_TYPE_CCMP:
		/* alg is CCM: check MIC only */
		if (!(decrypt_in & RX_MPDU_RES_STATUS_MIC_OK))
			/* Bad MIC */
			decrypt_out |= RX_RES_STATUS_BAD_ICV_MIC;
		else
			decrypt_out |= RX_RES_STATUS_DECRYPT_OK;

		break;

	case RX_RES_STATUS_SEC_TYPE_TKIP:
		if (!(decrypt_in & RX_MPDU_RES_STATUS_TTAK_OK)) {
			/* Bad TTAK */
			decrypt_out |= RX_RES_STATUS_BAD_KEY_TTAK;
			break;
		}
		/* fall through if TTAK OK */
	default:
		if (!(decrypt_in & RX_MPDU_RES_STATUS_ICV_OK))
			decrypt_out |= RX_RES_STATUS_BAD_ICV_MIC;
		else
			decrypt_out |= RX_RES_STATUS_DECRYPT_OK;
		break;
	}

	IWL_DEBUG_RX(priv, "decrypt_in:0x%x  decrypt_out = 0x%x\n",
					decrypt_in, decrypt_out);

	return decrypt_out;
}

static void iwlagn_pass_packet_to_mac80211(struct iwl_priv *priv,
					struct ieee80211_hdr *hdr,
					u16 len,
					u32 ampdu_status,
					struct iwl_rx_mem_buffer *rxb,
					struct ieee80211_rx_status *stats)
{
	struct sk_buff *skb;
	__le16 fc = hdr->frame_control;

	/* We only process data packets if the interface is open */
	if (unlikely(!priv->is_open)) {
		IWL_DEBUG_DROP_LIMIT(priv,
		    "Dropping packet while interface is not open.\n");
		return;
	}

	/* In case of HW accelerated crypto and bad decryption, drop */
	if (!priv->cfg->mod_params->sw_crypto &&
	    iwl_set_decrypted_flag(priv, hdr, ampdu_status, stats))
		return;

	skb = dev_alloc_skb(128);
	if (!skb) {
		IWL_ERR(priv, "dev_alloc_skb failed\n");
		return;
	}

	skb_add_rx_frag(skb, 0, rxb->page, (void *)hdr - rxb_addr(rxb), len);

	iwl_update_stats(priv, false, fc, len);
	memcpy(IEEE80211_SKB_RXCB(skb), stats, sizeof(*stats));

	ieee80211_rx(priv->hw, skb);
	priv->alloc_rxb_page--;
	rxb->page = NULL;
}

/* Called for REPLY_RX (legacy ABG frames), or
 * REPLY_RX_MPDU_CMD (HT high-throughput N frames). */
void iwlagn_rx_reply_rx(struct iwl_priv *priv,
				struct iwl_rx_mem_buffer *rxb)
{
	struct ieee80211_hdr *header;
	struct ieee80211_rx_status rx_status;
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_rx_phy_res *phy_res;
	__le32 rx_pkt_status;
	struct iwl_rx_mpdu_res_start *amsdu;
	u32 len;
	u32 ampdu_status;
	u32 rate_n_flags;

	/**
	 * REPLY_RX and REPLY_RX_MPDU_CMD are handled differently.
	 *	REPLY_RX: physical layer info is in this buffer
	 *	REPLY_RX_MPDU_CMD: physical layer info was sent in separate
	 *		command and cached in priv->last_phy_res
	 *
	 * Here we set up local variables depending on which command is
	 * received.
	 */
	if (pkt->hdr.cmd == REPLY_RX) {
		phy_res = (struct iwl_rx_phy_res *)pkt->u.raw;
		header = (struct ieee80211_hdr *)(pkt->u.raw + sizeof(*phy_res)
				+ phy_res->cfg_phy_cnt);

		len = le16_to_cpu(phy_res->byte_count);
		rx_pkt_status = *(__le32 *)(pkt->u.raw + sizeof(*phy_res) +
				phy_res->cfg_phy_cnt + len);
		ampdu_status = le32_to_cpu(rx_pkt_status);
	} else {
		if (!priv->_agn.last_phy_res_valid) {
			IWL_ERR(priv, "MPDU frame without cached PHY data\n");
			return;
		}
		phy_res = &priv->_agn.last_phy_res;
		amsdu = (struct iwl_rx_mpdu_res_start *)pkt->u.raw;
		header = (struct ieee80211_hdr *)(pkt->u.raw + sizeof(*amsdu));
		len = le16_to_cpu(amsdu->byte_count);
		rx_pkt_status = *(__le32 *)(pkt->u.raw + sizeof(*amsdu) + len);
		ampdu_status = iwlagn_translate_rx_status(priv,
				le32_to_cpu(rx_pkt_status));
	}

	if ((unlikely(phy_res->cfg_phy_cnt > 20))) {
		IWL_DEBUG_DROP(priv, "dsp size out of range [0,20]: %d/n",
				phy_res->cfg_phy_cnt);
		return;
	}

	if (!(rx_pkt_status & RX_RES_STATUS_NO_CRC32_ERROR) ||
	    !(rx_pkt_status & RX_RES_STATUS_NO_RXE_OVERFLOW)) {
		IWL_DEBUG_RX(priv, "Bad CRC or FIFO: 0x%08X.\n",
				le32_to_cpu(rx_pkt_status));
		return;
	}

	/* This will be used in several places later */
	rate_n_flags = le32_to_cpu(phy_res->rate_n_flags);

	/* rx_status carries information about the packet to mac80211 */
	rx_status.mactime = le64_to_cpu(phy_res->timestamp);
	rx_status.freq =
		ieee80211_channel_to_frequency(le16_to_cpu(phy_res->channel));
	rx_status.band = (phy_res->phy_flags & RX_RES_PHY_FLAGS_BAND_24_MSK) ?
				IEEE80211_BAND_2GHZ : IEEE80211_BAND_5GHZ;
	rx_status.rate_idx =
		iwlagn_hwrate_to_mac80211_idx(rate_n_flags, rx_status.band);
	rx_status.flag = 0;

	/* TSF isn't reliable. In order to allow smooth user experience,
	 * this W/A doesn't propagate it to the mac80211 */
	/*rx_status.flag |= RX_FLAG_TSFT;*/

	priv->ucode_beacon_time = le32_to_cpu(phy_res->beacon_time_stamp);

	/* Find max signal strength (dBm) among 3 antenna/receiver chains */
	rx_status.signal = iwlagn_calc_rssi(priv, phy_res);

	iwl_dbg_log_rx_data_frame(priv, len, header);
	IWL_DEBUG_STATS_LIMIT(priv, "Rssi %d, TSF %llu\n",
		rx_status.signal, (unsigned long long)rx_status.mactime);

	/*
	 * "antenna number"
	 *
	 * It seems that the antenna field in the phy flags value
	 * is actually a bit field. This is undefined by radiotap,
	 * it wants an actual antenna number but I always get "7"
	 * for most legacy frames I receive indicating that the
	 * same frame was received on all three RX chains.
	 *
	 * I think this field should be removed in favor of a
	 * new 802.11n radiotap field "RX chains" that is defined
	 * as a bitmask.
	 */
	rx_status.antenna =
		(le16_to_cpu(phy_res->phy_flags) & RX_RES_PHY_FLAGS_ANTENNA_MSK)
		>> RX_RES_PHY_FLAGS_ANTENNA_POS;

	/* set the preamble flag if appropriate */
	if (phy_res->phy_flags & RX_RES_PHY_FLAGS_SHORT_PREAMBLE_MSK)
		rx_status.flag |= RX_FLAG_SHORTPRE;

	/* Set up the HT phy flags */
	if (rate_n_flags & RATE_MCS_HT_MSK)
		rx_status.flag |= RX_FLAG_HT;
	if (rate_n_flags & RATE_MCS_HT40_MSK)
		rx_status.flag |= RX_FLAG_40MHZ;
	if (rate_n_flags & RATE_MCS_SGI_MSK)
		rx_status.flag |= RX_FLAG_SHORT_GI;

	iwlagn_pass_packet_to_mac80211(priv, header, len, ampdu_status,
				    rxb, &rx_status);
}

/* Cache phy data (Rx signal strength, etc) for HT frame (REPLY_RX_PHY_CMD).
 * This will be used later in iwl_rx_reply_rx() for REPLY_RX_MPDU_CMD. */
void iwlagn_rx_reply_rx_phy(struct iwl_priv *priv,
			    struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	priv->_agn.last_phy_res_valid = true;
	memcpy(&priv->_agn.last_phy_res, pkt->u.raw,
	       sizeof(struct iwl_rx_phy_res));
}

static int iwl_get_single_channel_for_scan(struct iwl_priv *priv,
					   struct ieee80211_vif *vif,
					   enum ieee80211_band band,
					   struct iwl_scan_channel *scan_ch)
{
	const struct ieee80211_supported_band *sband;
	u16 passive_dwell = 0;
	u16 active_dwell = 0;
	int added = 0;
	u16 channel = 0;

	sband = iwl_get_hw_mode(priv, band);
	if (!sband) {
		IWL_ERR(priv, "invalid band\n");
		return added;
	}

	active_dwell = iwl_get_active_dwell_time(priv, band, 0);
	passive_dwell = iwl_get_passive_dwell_time(priv, band, vif);

	if (passive_dwell <= active_dwell)
		passive_dwell = active_dwell + 1;

	channel = iwl_get_single_channel_number(priv, band);
	if (channel) {
		scan_ch->channel = cpu_to_le16(channel);
		scan_ch->type = SCAN_CHANNEL_TYPE_PASSIVE;
		scan_ch->active_dwell = cpu_to_le16(active_dwell);
		scan_ch->passive_dwell = cpu_to_le16(passive_dwell);
		/* Set txpower levels to defaults */
		scan_ch->dsp_atten = 110;
		if (band == IEEE80211_BAND_5GHZ)
			scan_ch->tx_gain = ((1 << 5) | (3 << 3)) | 3;
		else
			scan_ch->tx_gain = ((1 << 5) | (5 << 3));
		added++;
	} else
		IWL_ERR(priv, "no valid channel found\n");
	return added;
}

static int iwl_get_channels_for_scan(struct iwl_priv *priv,
				     struct ieee80211_vif *vif,
				     enum ieee80211_band band,
				     u8 is_active, u8 n_probes,
				     struct iwl_scan_channel *scan_ch)
{
	struct ieee80211_channel *chan;
	const struct ieee80211_supported_band *sband;
	const struct iwl_channel_info *ch_info;
	u16 passive_dwell = 0;
	u16 active_dwell = 0;
	int added, i;
	u16 channel;

	sband = iwl_get_hw_mode(priv, band);
	if (!sband)
		return 0;

	active_dwell = iwl_get_active_dwell_time(priv, band, n_probes);
	passive_dwell = iwl_get_passive_dwell_time(priv, band, vif);

	if (passive_dwell <= active_dwell)
		passive_dwell = active_dwell + 1;

	for (i = 0, added = 0; i < priv->scan_request->n_channels; i++) {
		chan = priv->scan_request->channels[i];

		if (chan->band != band)
			continue;

		channel = chan->hw_value;
		scan_ch->channel = cpu_to_le16(channel);

		ch_info = iwl_get_channel_info(priv, band, channel);
		if (!is_channel_valid(ch_info)) {
			IWL_DEBUG_SCAN(priv, "Channel %d is INVALID for this band.\n",
					channel);
			continue;
		}

		if (!is_active || is_channel_passive(ch_info) ||
		    (chan->flags & IEEE80211_CHAN_PASSIVE_SCAN))
			scan_ch->type = SCAN_CHANNEL_TYPE_PASSIVE;
		else
			scan_ch->type = SCAN_CHANNEL_TYPE_ACTIVE;

		if (n_probes)
			scan_ch->type |= IWL_SCAN_PROBE_MASK(n_probes);

		scan_ch->active_dwell = cpu_to_le16(active_dwell);
		scan_ch->passive_dwell = cpu_to_le16(passive_dwell);

		/* Set txpower levels to defaults */
		scan_ch->dsp_atten = 110;

		/* NOTE: if we were doing 6Mb OFDM for scans we'd use
		 * power level:
		 * scan_ch->tx_gain = ((1 << 5) | (2 << 3)) | 3;
		 */
		if (band == IEEE80211_BAND_5GHZ)
			scan_ch->tx_gain = ((1 << 5) | (3 << 3)) | 3;
		else
			scan_ch->tx_gain = ((1 << 5) | (5 << 3));

		IWL_DEBUG_SCAN(priv, "Scanning ch=%d prob=0x%X [%s %d]\n",
			       channel, le32_to_cpu(scan_ch->type),
			       (scan_ch->type & SCAN_CHANNEL_TYPE_ACTIVE) ?
				"ACTIVE" : "PASSIVE",
			       (scan_ch->type & SCAN_CHANNEL_TYPE_ACTIVE) ?
			       active_dwell : passive_dwell);

		scan_ch++;
		added++;
	}

	IWL_DEBUG_SCAN(priv, "total channels to scan %d\n", added);
	return added;
}

int iwlagn_request_scan(struct iwl_priv *priv, struct ieee80211_vif *vif)
{
	struct iwl_host_cmd cmd = {
		.id = REPLY_SCAN_CMD,
		.len = sizeof(struct iwl_scan_cmd),
		.flags = CMD_SIZE_HUGE,
	};
	struct iwl_scan_cmd *scan;
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];
	u32 rate_flags = 0;
	u16 cmd_len;
	u16 rx_chain = 0;
	enum ieee80211_band band;
	u8 n_probes = 0;
	u8 rx_ant = priv->hw_params.valid_rx_ant;
	u8 rate;
	bool is_active = false;
	int  chan_mod;
	u8 active_chains;
	u8 scan_tx_antennas = priv->hw_params.valid_tx_ant;
	int ret;

	lockdep_assert_held(&priv->mutex);

	if (vif)
		ctx = iwl_rxon_ctx_from_vif(vif);

	if (!priv->scan_cmd) {
		priv->scan_cmd = kmalloc(sizeof(struct iwl_scan_cmd) +
					 IWL_MAX_SCAN_SIZE, GFP_KERNEL);
		if (!priv->scan_cmd) {
			IWL_DEBUG_SCAN(priv,
				       "fail to allocate memory for scan\n");
			return -ENOMEM;
		}
	}
	scan = priv->scan_cmd;
	memset(scan, 0, sizeof(struct iwl_scan_cmd) + IWL_MAX_SCAN_SIZE);

	scan->quiet_plcp_th = IWL_PLCP_QUIET_THRESH;
	scan->quiet_time = IWL_ACTIVE_QUIET_TIME;

	if (iwl_is_any_associated(priv)) {
		u16 interval = 0;
		u32 extra;
		u32 suspend_time = 100;
		u32 scan_suspend_time = 100;
		unsigned long flags;

		IWL_DEBUG_INFO(priv, "Scanning while associated...\n");
		spin_lock_irqsave(&priv->lock, flags);
		if (priv->is_internal_short_scan)
			interval = 0;
		else
			interval = vif->bss_conf.beacon_int;
		spin_unlock_irqrestore(&priv->lock, flags);

		scan->suspend_time = 0;
		scan->max_out_time = cpu_to_le32(200 * 1024);
		if (!interval)
			interval = suspend_time;

		extra = (suspend_time / interval) << 22;
		scan_suspend_time = (extra |
		    ((suspend_time % interval) * 1024));
		scan->suspend_time = cpu_to_le32(scan_suspend_time);
		IWL_DEBUG_SCAN(priv, "suspend_time 0x%X beacon interval %d\n",
			       scan_suspend_time, interval);
	}

	if (priv->is_internal_short_scan) {
		IWL_DEBUG_SCAN(priv, "Start internal passive scan.\n");
	} else if (priv->scan_request->n_ssids) {
		int i, p = 0;
		IWL_DEBUG_SCAN(priv, "Kicking off active scan\n");
		for (i = 0; i < priv->scan_request->n_ssids; i++) {
			/* always does wildcard anyway */
			if (!priv->scan_request->ssids[i].ssid_len)
				continue;
			scan->direct_scan[p].id = WLAN_EID_SSID;
			scan->direct_scan[p].len =
				priv->scan_request->ssids[i].ssid_len;
			memcpy(scan->direct_scan[p].ssid,
			       priv->scan_request->ssids[i].ssid,
			       priv->scan_request->ssids[i].ssid_len);
			n_probes++;
			p++;
		}
		is_active = true;
	} else
		IWL_DEBUG_SCAN(priv, "Start passive scan.\n");

	scan->tx_cmd.tx_flags = TX_CMD_FLG_SEQ_CTL_MSK;
	scan->tx_cmd.sta_id = ctx->bcast_sta_id;
	scan->tx_cmd.stop_time.life_time = TX_CMD_LIFE_TIME_INFINITE;

	switch (priv->scan_band) {
	case IEEE80211_BAND_2GHZ:
		scan->flags = RXON_FLG_BAND_24G_MSK | RXON_FLG_AUTO_DETECT_MSK;
		chan_mod = le32_to_cpu(
			priv->contexts[IWL_RXON_CTX_BSS].active.flags &
						RXON_FLG_CHANNEL_MODE_MSK)
				       >> RXON_FLG_CHANNEL_MODE_POS;
		if (chan_mod == CHANNEL_MODE_PURE_40) {
			rate = IWL_RATE_6M_PLCP;
		} else {
			rate = IWL_RATE_1M_PLCP;
			rate_flags = RATE_MCS_CCK_MSK;
		}
		/*
		 * Internal scans are passive, so we can indiscriminately set
		 * the BT ignore flag on 2.4 GHz since it applies to TX only.
		 */
		if (priv->cfg->bt_params &&
		    priv->cfg->bt_params->advanced_bt_coexist)
			scan->tx_cmd.tx_flags |= TX_CMD_FLG_IGNORE_BT;
		break;
	case IEEE80211_BAND_5GHZ:
		rate = IWL_RATE_6M_PLCP;
		break;
	default:
		IWL_WARN(priv, "Invalid scan band\n");
		return -EIO;
	}

	/*
	 * If active scanning is requested but a certain channel is
	 * marked passive, we can do active scanning if we detect
	 * transmissions.
	 *
	 * There is an issue with some firmware versions that triggers
	 * a sysassert on a "good CRC threshold" of zero (== disabled),
	 * on a radar channel even though this means that we should NOT
	 * send probes.
	 *
	 * The "good CRC threshold" is the number of frames that we
	 * need to receive during our dwell time on a channel before
	 * sending out probes -- setting this to a huge value will
	 * mean we never reach it, but at the same time work around
	 * the aforementioned issue. Thus use IWL_GOOD_CRC_TH_NEVER
	 * here instead of IWL_GOOD_CRC_TH_DISABLED.
	 */
	scan->good_CRC_th = is_active ? IWL_GOOD_CRC_TH_DEFAULT :
					IWL_GOOD_CRC_TH_NEVER;

	band = priv->scan_band;

	if (priv->cfg->scan_rx_antennas[band])
		rx_ant = priv->cfg->scan_rx_antennas[band];

	if (priv->cfg->scan_tx_antennas[band])
		scan_tx_antennas = priv->cfg->scan_tx_antennas[band];

	if (priv->cfg->bt_params &&
	    priv->cfg->bt_params->advanced_bt_coexist &&
	    priv->bt_full_concurrent) {
		/* operated as 1x1 in full concurrency mode */
		scan_tx_antennas = first_antenna(
			priv->cfg->scan_tx_antennas[band]);
	}

	priv->scan_tx_ant[band] = iwl_toggle_tx_ant(priv, priv->scan_tx_ant[band],
						    scan_tx_antennas);
	rate_flags |= iwl_ant_idx_to_flags(priv->scan_tx_ant[band]);
	scan->tx_cmd.rate_n_flags = iwl_hw_set_rate_n_flags(rate, rate_flags);

	/* In power save mode use one chain, otherwise use all chains */
	if (test_bit(STATUS_POWER_PMI, &priv->status)) {
		/* rx_ant has been set to all valid chains previously */
		active_chains = rx_ant &
				((u8)(priv->chain_noise_data.active_chains));
		if (!active_chains)
			active_chains = rx_ant;

		IWL_DEBUG_SCAN(priv, "chain_noise_data.active_chains: %u\n",
				priv->chain_noise_data.active_chains);

		rx_ant = first_antenna(active_chains);
	}
	if (priv->cfg->bt_params &&
	    priv->cfg->bt_params->advanced_bt_coexist &&
	    priv->bt_full_concurrent) {
		/* operated as 1x1 in full concurrency mode */
		rx_ant = first_antenna(rx_ant);
	}

	/* MIMO is not used here, but value is required */
	rx_chain |= priv->hw_params.valid_rx_ant << RXON_RX_CHAIN_VALID_POS;
	rx_chain |= rx_ant << RXON_RX_CHAIN_FORCE_MIMO_SEL_POS;
	rx_chain |= rx_ant << RXON_RX_CHAIN_FORCE_SEL_POS;
	rx_chain |= 0x1 << RXON_RX_CHAIN_DRIVER_FORCE_POS;
	scan->rx_chain = cpu_to_le16(rx_chain);
	if (!priv->is_internal_short_scan) {
		cmd_len = iwl_fill_probe_req(priv,
					(struct ieee80211_mgmt *)scan->data,
					vif->addr,
					priv->scan_request->ie,
					priv->scan_request->ie_len,
					IWL_MAX_SCAN_SIZE - sizeof(*scan));
	} else {
		/* use bcast addr, will not be transmitted but must be valid */
		cmd_len = iwl_fill_probe_req(priv,
					(struct ieee80211_mgmt *)scan->data,
					iwl_bcast_addr, NULL, 0,
					IWL_MAX_SCAN_SIZE - sizeof(*scan));

	}
	scan->tx_cmd.len = cpu_to_le16(cmd_len);

	scan->filter_flags |= (RXON_FILTER_ACCEPT_GRP_MSK |
			       RXON_FILTER_BCON_AWARE_MSK);

	if (priv->is_internal_short_scan) {
		scan->channel_count =
			iwl_get_single_channel_for_scan(priv, vif, band,
				(void *)&scan->data[le16_to_cpu(
				scan->tx_cmd.len)]);
	} else {
		scan->channel_count =
			iwl_get_channels_for_scan(priv, vif, band,
				is_active, n_probes,
				(void *)&scan->data[le16_to_cpu(
				scan->tx_cmd.len)]);
	}
	if (scan->channel_count == 0) {
		IWL_DEBUG_SCAN(priv, "channel count %d\n", scan->channel_count);
		return -EIO;
	}

	cmd.len += le16_to_cpu(scan->tx_cmd.len) +
	    scan->channel_count * sizeof(struct iwl_scan_channel);
	cmd.data = scan;
	scan->len = cpu_to_le16(cmd.len);

	/* set scan bit here for PAN params */
	set_bit(STATUS_SCAN_HW, &priv->status);

	if (priv->cfg->ops->hcmd->set_pan_params) {
		ret = priv->cfg->ops->hcmd->set_pan_params(priv);
		if (ret)
			return ret;
	}

	ret = iwl_send_cmd_sync(priv, &cmd);
	if (ret) {
		clear_bit(STATUS_SCAN_HW, &priv->status);
		if (priv->cfg->ops->hcmd->set_pan_params)
			priv->cfg->ops->hcmd->set_pan_params(priv);
	}

	return ret;
}

void iwlagn_post_scan(struct iwl_priv *priv)
{
	struct iwl_rxon_context *ctx;

	/*
	 * Since setting the RXON may have been deferred while
	 * performing the scan, fire one off if needed
	 */
	for_each_context(priv, ctx)
		if (memcmp(&ctx->staging, &ctx->active, sizeof(ctx->staging)))
			iwlagn_commit_rxon(priv, ctx);

	if (priv->cfg->ops->hcmd->set_pan_params)
		priv->cfg->ops->hcmd->set_pan_params(priv);
}

int iwlagn_manage_ibss_station(struct iwl_priv *priv,
			       struct ieee80211_vif *vif, bool add)
{
	struct iwl_vif_priv *vif_priv = (void *)vif->drv_priv;

	if (add)
		return iwlagn_add_bssid_station(priv, vif_priv->ctx,
						vif->bss_conf.bssid,
						&vif_priv->ibss_bssid_sta_id);
	return iwl_remove_station(priv, vif_priv->ibss_bssid_sta_id,
				  vif->bss_conf.bssid);
}

void iwl_free_tfds_in_queue(struct iwl_priv *priv,
			    int sta_id, int tid, int freed)
{
	lockdep_assert_held(&priv->sta_lock);

	if (priv->stations[sta_id].tid[tid].tfds_in_queue >= freed)
		priv->stations[sta_id].tid[tid].tfds_in_queue -= freed;
	else {
		IWL_DEBUG_TX(priv, "free more than tfds_in_queue (%u:%d)\n",
			priv->stations[sta_id].tid[tid].tfds_in_queue,
			freed);
		priv->stations[sta_id].tid[tid].tfds_in_queue = 0;
	}
}

#define IWL_FLUSH_WAIT_MS	2000

int iwlagn_wait_tx_queue_empty(struct iwl_priv *priv)
{
	struct iwl_tx_queue *txq;
	struct iwl_queue *q;
	int cnt;
	unsigned long now = jiffies;
	int ret = 0;

	/* waiting for all the tx frames complete might take a while */
	for (cnt = 0; cnt < priv->hw_params.max_txq_num; cnt++) {
		if (cnt == priv->cmd_queue)
			continue;
		txq = &priv->txq[cnt];
		q = &txq->q;
		while (q->read_ptr != q->write_ptr && !time_after(jiffies,
		       now + msecs_to_jiffies(IWL_FLUSH_WAIT_MS)))
				msleep(1);

		if (q->read_ptr != q->write_ptr) {
			IWL_ERR(priv, "fail to flush all tx fifo queues\n");
			ret = -ETIMEDOUT;
			break;
		}
	}
	return ret;
}

#define IWL_TX_QUEUE_MSK	0xfffff

/**
 * iwlagn_txfifo_flush: send REPLY_TXFIFO_FLUSH command to uCode
 *
 * pre-requirements:
 *  1. acquire mutex before calling
 *  2. make sure rf is on and not in exit state
 */
int iwlagn_txfifo_flush(struct iwl_priv *priv, u16 flush_control)
{
	struct iwl_txfifo_flush_cmd flush_cmd;
	struct iwl_host_cmd cmd = {
		.id = REPLY_TXFIFO_FLUSH,
		.len = sizeof(struct iwl_txfifo_flush_cmd),
		.flags = CMD_SYNC,
		.data = &flush_cmd,
	};

	might_sleep();

	memset(&flush_cmd, 0, sizeof(flush_cmd));
	flush_cmd.fifo_control = IWL_TX_FIFO_VO_MSK | IWL_TX_FIFO_VI_MSK |
				 IWL_TX_FIFO_BE_MSK | IWL_TX_FIFO_BK_MSK;
	if (priv->cfg->sku & IWL_SKU_N)
		flush_cmd.fifo_control |= IWL_AGG_TX_QUEUE_MSK;

	IWL_DEBUG_INFO(priv, "fifo queue control: 0X%x\n",
		       flush_cmd.fifo_control);
	flush_cmd.flush_control = cpu_to_le16(flush_control);

	return iwl_send_cmd(priv, &cmd);
}

void iwlagn_dev_txfifo_flush(struct iwl_priv *priv, u16 flush_control)
{
	mutex_lock(&priv->mutex);
	ieee80211_stop_queues(priv->hw);
	if (priv->cfg->ops->lib->txfifo_flush(priv, IWL_DROP_ALL)) {
		IWL_ERR(priv, "flush request fail\n");
		goto done;
	}
	IWL_DEBUG_INFO(priv, "wait transmit/flush all frames\n");
	iwlagn_wait_tx_queue_empty(priv);
done:
	ieee80211_wake_queues(priv->hw);
	mutex_unlock(&priv->mutex);
}

/*
 * BT coex
 */
/*
 * Macros to access the lookup table.
 *
 * The lookup table has 7 inputs: bt3_prio, bt3_txrx, bt_rf_act, wifi_req,
* wifi_prio, wifi_txrx and wifi_sh_ant_req.
 *
 * It has three outputs: WLAN_ACTIVE, WLAN_KILL and ANT_SWITCH
 *
 * The format is that "registers" 8 through 11 contain the WLAN_ACTIVE bits
 * one after another in 32-bit registers, and "registers" 0 through 7 contain
 * the WLAN_KILL and ANT_SWITCH bits interleaved (in that order).
 *
 * These macros encode that format.
 */
#define LUT_VALUE(bt3_prio, bt3_txrx, bt_rf_act, wifi_req, wifi_prio, \
		  wifi_txrx, wifi_sh_ant_req) \
	(bt3_prio | (bt3_txrx << 1) | (bt_rf_act << 2) | (wifi_req << 3) | \
	(wifi_prio << 4) | (wifi_txrx << 5) | (wifi_sh_ant_req << 6))

#define LUT_PTA_WLAN_ACTIVE_OP(lut, op, val) \
	lut[8 + ((val) >> 5)] op (cpu_to_le32(BIT((val) & 0x1f)))
#define LUT_TEST_PTA_WLAN_ACTIVE(lut, bt3_prio, bt3_txrx, bt_rf_act, wifi_req, \
				 wifi_prio, wifi_txrx, wifi_sh_ant_req) \
	(!!(LUT_PTA_WLAN_ACTIVE_OP(lut, &, LUT_VALUE(bt3_prio, bt3_txrx, \
				   bt_rf_act, wifi_req, wifi_prio, wifi_txrx, \
				   wifi_sh_ant_req))))
#define LUT_SET_PTA_WLAN_ACTIVE(lut, bt3_prio, bt3_txrx, bt_rf_act, wifi_req, \
				wifi_prio, wifi_txrx, wifi_sh_ant_req) \
	LUT_PTA_WLAN_ACTIVE_OP(lut, |=, LUT_VALUE(bt3_prio, bt3_txrx, \
			       bt_rf_act, wifi_req, wifi_prio, wifi_txrx, \
			       wifi_sh_ant_req))
#define LUT_CLEAR_PTA_WLAN_ACTIVE(lut, bt3_prio, bt3_txrx, bt_rf_act, \
				  wifi_req, wifi_prio, wifi_txrx, \
				  wifi_sh_ant_req) \
	LUT_PTA_WLAN_ACTIVE_OP(lut, &= ~, LUT_VALUE(bt3_prio, bt3_txrx, \
			       bt_rf_act, wifi_req, wifi_prio, wifi_txrx, \
			       wifi_sh_ant_req))

#define LUT_WLAN_KILL_OP(lut, op, val) \
	lut[(val) >> 4] op (cpu_to_le32(BIT(((val) << 1) & 0x1e)))
#define LUT_TEST_WLAN_KILL(lut, bt3_prio, bt3_txrx, bt_rf_act, wifi_req, \
			   wifi_prio, wifi_txrx, wifi_sh_ant_req) \
	(!!(LUT_WLAN_KILL_OP(lut, &, LUT_VALUE(bt3_prio, bt3_txrx, bt_rf_act, \
			     wifi_req, wifi_prio, wifi_txrx, wifi_sh_ant_req))))
#define LUT_SET_WLAN_KILL(lut, bt3_prio, bt3_txrx, bt_rf_act, wifi_req, \
			  wifi_prio, wifi_txrx, wifi_sh_ant_req) \
	LUT_WLAN_KILL_OP(lut, |=, LUT_VALUE(bt3_prio, bt3_txrx, bt_rf_act, \
			 wifi_req, wifi_prio, wifi_txrx, wifi_sh_ant_req))
#define LUT_CLEAR_WLAN_KILL(lut, bt3_prio, bt3_txrx, bt_rf_act, wifi_req, \
			    wifi_prio, wifi_txrx, wifi_sh_ant_req) \
	LUT_WLAN_KILL_OP(lut, &= ~, LUT_VALUE(bt3_prio, bt3_txrx, bt_rf_act, \
			 wifi_req, wifi_prio, wifi_txrx, wifi_sh_ant_req))

#define LUT_ANT_SWITCH_OP(lut, op, val) \
	lut[(val) >> 4] op (cpu_to_le32(BIT((((val) << 1) & 0x1e) + 1)))
#define LUT_TEST_ANT_SWITCH(lut, bt3_prio, bt3_txrx, bt_rf_act, wifi_req, \
			    wifi_prio, wifi_txrx, wifi_sh_ant_req) \
	(!!(LUT_ANT_SWITCH_OP(lut, &, LUT_VALUE(bt3_prio, bt3_txrx, bt_rf_act, \
			      wifi_req, wifi_prio, wifi_txrx, \
			      wifi_sh_ant_req))))
#define LUT_SET_ANT_SWITCH(lut, bt3_prio, bt3_txrx, bt_rf_act, wifi_req, \
			   wifi_prio, wifi_txrx, wifi_sh_ant_req) \
	LUT_ANT_SWITCH_OP(lut, |=, LUT_VALUE(bt3_prio, bt3_txrx, bt_rf_act, \
			  wifi_req, wifi_prio, wifi_txrx, wifi_sh_ant_req))
#define LUT_CLEAR_ANT_SWITCH(lut, bt3_prio, bt3_txrx, bt_rf_act, wifi_req, \
			     wifi_prio, wifi_txrx, wifi_sh_ant_req) \
	LUT_ANT_SWITCH_OP(lut, &= ~, LUT_VALUE(bt3_prio, bt3_txrx, bt_rf_act, \
			  wifi_req, wifi_prio, wifi_txrx, wifi_sh_ant_req))

static const __le32 iwlagn_def_3w_lookup[12] = {
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaeaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xcc00ff28),
	cpu_to_le32(0x0000aaaa),
	cpu_to_le32(0xcc00aaaa),
	cpu_to_le32(0x0000aaaa),
	cpu_to_le32(0xc0004000),
	cpu_to_le32(0x00004000),
	cpu_to_le32(0xf0005000),
	cpu_to_le32(0xf0004000),
};

static const __le32 iwlagn_concurrent_lookup[12] = {
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0xaaaaaaaa),
	cpu_to_le32(0x00000000),
	cpu_to_le32(0x00000000),
	cpu_to_le32(0x00000000),
	cpu_to_le32(0x00000000),
};

void iwlagn_send_advance_bt_config(struct iwl_priv *priv)
{
	struct iwlagn_bt_cmd bt_cmd = {
		.max_kill = IWLAGN_BT_MAX_KILL_DEFAULT,
		.bt3_timer_t7_value = IWLAGN_BT3_T7_DEFAULT,
		.bt3_prio_sample_time = IWLAGN_BT3_PRIO_SAMPLE_DEFAULT,
		.bt3_timer_t2_value = IWLAGN_BT3_T2_DEFAULT,
	};

	BUILD_BUG_ON(sizeof(iwlagn_def_3w_lookup) !=
			sizeof(bt_cmd.bt3_lookup_table));

	if (priv->cfg->bt_params)
		bt_cmd.prio_boost = priv->cfg->bt_params->bt_prio_boost;
	else
		bt_cmd.prio_boost = 0;
	bt_cmd.kill_ack_mask = priv->kill_ack_mask;
	bt_cmd.kill_cts_mask = priv->kill_cts_mask;
	bt_cmd.valid = priv->bt_valid;
	bt_cmd.tx_prio_boost = 0;
	bt_cmd.rx_prio_boost = 0;

	/*
	 * Configure BT coex mode to "no coexistence" when the
	 * user disabled BT coexistence, we have no interface
	 * (might be in monitor mode), or the interface is in
	 * IBSS mode (no proper uCode support for coex then).
	 */
	if (!bt_coex_active || priv->iw_mode == NL80211_IFTYPE_ADHOC) {
		bt_cmd.flags = 0;
	} else {
		bt_cmd.flags = IWLAGN_BT_FLAG_COEX_MODE_3W <<
					IWLAGN_BT_FLAG_COEX_MODE_SHIFT;
		if (priv->bt_ch_announce)
			bt_cmd.flags |= IWLAGN_BT_FLAG_CHANNEL_INHIBITION;
		IWL_DEBUG_INFO(priv, "BT coex flag: 0X%x\n", bt_cmd.flags);
	}
	if (priv->bt_full_concurrent)
		memcpy(bt_cmd.bt3_lookup_table, iwlagn_concurrent_lookup,
			sizeof(iwlagn_concurrent_lookup));
	else
		memcpy(bt_cmd.bt3_lookup_table, iwlagn_def_3w_lookup,
			sizeof(iwlagn_def_3w_lookup));

	IWL_DEBUG_INFO(priv, "BT coex %s in %s mode\n",
		       bt_cmd.flags ? "active" : "disabled",
		       priv->bt_full_concurrent ?
		       "full concurrency" : "3-wire");

	if (iwl_send_cmd_pdu(priv, REPLY_BT_CONFIG, sizeof(bt_cmd), &bt_cmd))
		IWL_ERR(priv, "failed to send BT Coex Config\n");

	/*
	 * When we are doing a restart, need to also reconfigure BT
	 * SCO to the device. If not doing a restart, bt_sco_active
	 * will always be false, so there's no need to have an extra
	 * variable to check for it.
	 */
	if (priv->bt_sco_active) {
		struct iwlagn_bt_sco_cmd sco_cmd = { .flags = 0 };

		if (priv->bt_sco_active)
			sco_cmd.flags |= IWLAGN_BT_SCO_ACTIVE;
		if (iwl_send_cmd_pdu(priv, REPLY_BT_COEX_SCO,
				     sizeof(sco_cmd), &sco_cmd))
			IWL_ERR(priv, "failed to send BT SCO command\n");
	}
}

static void iwlagn_bt_traffic_change_work(struct work_struct *work)
{
	struct iwl_priv *priv =
		container_of(work, struct iwl_priv, bt_traffic_change_work);
	struct iwl_rxon_context *ctx;
	int smps_request = -1;

	IWL_DEBUG_INFO(priv, "BT traffic load changes: %d\n",
		       priv->bt_traffic_load);

	switch (priv->bt_traffic_load) {
	case IWL_BT_COEX_TRAFFIC_LOAD_NONE:
		smps_request = IEEE80211_SMPS_AUTOMATIC;
		break;
	case IWL_BT_COEX_TRAFFIC_LOAD_LOW:
		smps_request = IEEE80211_SMPS_DYNAMIC;
		break;
	case IWL_BT_COEX_TRAFFIC_LOAD_HIGH:
	case IWL_BT_COEX_TRAFFIC_LOAD_CONTINUOUS:
		smps_request = IEEE80211_SMPS_STATIC;
		break;
	default:
		IWL_ERR(priv, "Invalid BT traffic load: %d\n",
			priv->bt_traffic_load);
		break;
	}

	mutex_lock(&priv->mutex);

	if (priv->cfg->ops->lib->update_chain_flags)
		priv->cfg->ops->lib->update_chain_flags(priv);

	if (smps_request != -1) {
		for_each_context(priv, ctx) {
			if (ctx->vif && ctx->vif->type == NL80211_IFTYPE_STATION)
				ieee80211_request_smps(ctx->vif, smps_request);
		}
	}

	mutex_unlock(&priv->mutex);
}

static void iwlagn_print_uartmsg(struct iwl_priv *priv,
				struct iwl_bt_uart_msg *uart_msg)
{
	IWL_DEBUG_NOTIF(priv, "Message Type = 0x%X, SSN = 0x%X, "
			"Update Req = 0x%X",
		(BT_UART_MSG_FRAME1MSGTYPE_MSK & uart_msg->frame1) >>
			BT_UART_MSG_FRAME1MSGTYPE_POS,
		(BT_UART_MSG_FRAME1SSN_MSK & uart_msg->frame1) >>
			BT_UART_MSG_FRAME1SSN_POS,
		(BT_UART_MSG_FRAME1UPDATEREQ_MSK & uart_msg->frame1) >>
			BT_UART_MSG_FRAME1UPDATEREQ_POS);

	IWL_DEBUG_NOTIF(priv, "Open connections = 0x%X, Traffic load = 0x%X, "
			"Chl_SeqN = 0x%X, In band = 0x%X",
		(BT_UART_MSG_FRAME2OPENCONNECTIONS_MSK & uart_msg->frame2) >>
			BT_UART_MSG_FRAME2OPENCONNECTIONS_POS,
		(BT_UART_MSG_FRAME2TRAFFICLOAD_MSK & uart_msg->frame2) >>
			BT_UART_MSG_FRAME2TRAFFICLOAD_POS,
		(BT_UART_MSG_FRAME2CHLSEQN_MSK & uart_msg->frame2) >>
			BT_UART_MSG_FRAME2CHLSEQN_POS,
		(BT_UART_MSG_FRAME2INBAND_MSK & uart_msg->frame2) >>
			BT_UART_MSG_FRAME2INBAND_POS);

	IWL_DEBUG_NOTIF(priv, "SCO/eSCO = 0x%X, Sniff = 0x%X, A2DP = 0x%X, "
			"ACL = 0x%X, Master = 0x%X, OBEX = 0x%X",
		(BT_UART_MSG_FRAME3SCOESCO_MSK & uart_msg->frame3) >>
			BT_UART_MSG_FRAME3SCOESCO_POS,
		(BT_UART_MSG_FRAME3SNIFF_MSK & uart_msg->frame3) >>
			BT_UART_MSG_FRAME3SNIFF_POS,
		(BT_UART_MSG_FRAME3A2DP_MSK & uart_msg->frame3) >>
			BT_UART_MSG_FRAME3A2DP_POS,
		(BT_UART_MSG_FRAME3ACL_MSK & uart_msg->frame3) >>
			BT_UART_MSG_FRAME3ACL_POS,
		(BT_UART_MSG_FRAME3MASTER_MSK & uart_msg->frame3) >>
			BT_UART_MSG_FRAME3MASTER_POS,
		(BT_UART_MSG_FRAME3OBEX_MSK & uart_msg->frame3) >>
			BT_UART_MSG_FRAME3OBEX_POS);

	IWL_DEBUG_NOTIF(priv, "Idle duration = 0x%X",
		(BT_UART_MSG_FRAME4IDLEDURATION_MSK & uart_msg->frame4) >>
			BT_UART_MSG_FRAME4IDLEDURATION_POS);

	IWL_DEBUG_NOTIF(priv, "Tx Activity = 0x%X, Rx Activity = 0x%X, "
			"eSCO Retransmissions = 0x%X",
		(BT_UART_MSG_FRAME5TXACTIVITY_MSK & uart_msg->frame5) >>
			BT_UART_MSG_FRAME5TXACTIVITY_POS,
		(BT_UART_MSG_FRAME5RXACTIVITY_MSK & uart_msg->frame5) >>
			BT_UART_MSG_FRAME5RXACTIVITY_POS,
		(BT_UART_MSG_FRAME5ESCORETRANSMIT_MSK & uart_msg->frame5) >>
			BT_UART_MSG_FRAME5ESCORETRANSMIT_POS);

	IWL_DEBUG_NOTIF(priv, "Sniff Interval = 0x%X, Discoverable = 0x%X",
		(BT_UART_MSG_FRAME6SNIFFINTERVAL_MSK & uart_msg->frame6) >>
			BT_UART_MSG_FRAME6SNIFFINTERVAL_POS,
		(BT_UART_MSG_FRAME6DISCOVERABLE_MSK & uart_msg->frame6) >>
			BT_UART_MSG_FRAME6DISCOVERABLE_POS);

	IWL_DEBUG_NOTIF(priv, "Sniff Activity = 0x%X, Inquiry/Page SR Mode = "
			"0x%X, Connectable = 0x%X",
		(BT_UART_MSG_FRAME7SNIFFACTIVITY_MSK & uart_msg->frame7) >>
			BT_UART_MSG_FRAME7SNIFFACTIVITY_POS,
		(BT_UART_MSG_FRAME7INQUIRYPAGESRMODE_MSK & uart_msg->frame7) >>
			BT_UART_MSG_FRAME7INQUIRYPAGESRMODE_POS,
		(BT_UART_MSG_FRAME7CONNECTABLE_MSK & uart_msg->frame7) >>
			BT_UART_MSG_FRAME7CONNECTABLE_POS);
}

static void iwlagn_set_kill_ack_msk(struct iwl_priv *priv,
				     struct iwl_bt_uart_msg *uart_msg)
{
	u8 kill_ack_msk;
	__le32 bt_kill_ack_msg[2] = {
			cpu_to_le32(0xFFFFFFF), cpu_to_le32(0xFFFFFC00) };

	kill_ack_msk = (((BT_UART_MSG_FRAME3A2DP_MSK |
			BT_UART_MSG_FRAME3SNIFF_MSK |
			BT_UART_MSG_FRAME3SCOESCO_MSK) &
			uart_msg->frame3) == 0) ? 1 : 0;
	if (priv->kill_ack_mask != bt_kill_ack_msg[kill_ack_msk]) {
		priv->bt_valid |= IWLAGN_BT_VALID_KILL_ACK_MASK;
		priv->kill_ack_mask = bt_kill_ack_msg[kill_ack_msk];
		/* schedule to send runtime bt_config */
		queue_work(priv->workqueue, &priv->bt_runtime_config);
	}

}

void iwlagn_bt_coex_profile_notif(struct iwl_priv *priv,
					     struct iwl_rx_mem_buffer *rxb)
{
	unsigned long flags;
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_bt_coex_profile_notif *coex = &pkt->u.bt_coex_profile_notif;
	struct iwlagn_bt_sco_cmd sco_cmd = { .flags = 0 };
	struct iwl_bt_uart_msg *uart_msg = &coex->last_bt_uart_msg;
	u8 last_traffic_load;

	IWL_DEBUG_NOTIF(priv, "BT Coex notification:\n");
	IWL_DEBUG_NOTIF(priv, "    status: %d\n", coex->bt_status);
	IWL_DEBUG_NOTIF(priv, "    traffic load: %d\n", coex->bt_traffic_load);
	IWL_DEBUG_NOTIF(priv, "    CI compliance: %d\n",
			coex->bt_ci_compliance);
	iwlagn_print_uartmsg(priv, uart_msg);

	last_traffic_load = priv->notif_bt_traffic_load;
	priv->notif_bt_traffic_load = coex->bt_traffic_load;
	if (priv->iw_mode != NL80211_IFTYPE_ADHOC) {
		if (priv->bt_status != coex->bt_status ||
		    last_traffic_load != coex->bt_traffic_load) {
			if (coex->bt_status) {
				/* BT on */
				if (!priv->bt_ch_announce)
					priv->bt_traffic_load =
						IWL_BT_COEX_TRAFFIC_LOAD_HIGH;
				else
					priv->bt_traffic_load =
						coex->bt_traffic_load;
			} else {
				/* BT off */
				priv->bt_traffic_load =
					IWL_BT_COEX_TRAFFIC_LOAD_NONE;
			}
			priv->bt_status = coex->bt_status;
			queue_work(priv->workqueue,
				   &priv->bt_traffic_change_work);
		}
		if (priv->bt_sco_active !=
		    (uart_msg->frame3 & BT_UART_MSG_FRAME3SCOESCO_MSK)) {
			priv->bt_sco_active = uart_msg->frame3 &
				BT_UART_MSG_FRAME3SCOESCO_MSK;
			if (priv->bt_sco_active)
				sco_cmd.flags |= IWLAGN_BT_SCO_ACTIVE;
			iwl_send_cmd_pdu_async(priv, REPLY_BT_COEX_SCO,
				       sizeof(sco_cmd), &sco_cmd, NULL);
		}
	}

	iwlagn_set_kill_ack_msk(priv, uart_msg);

	/* FIXME: based on notification, adjust the prio_boost */

	spin_lock_irqsave(&priv->lock, flags);
	priv->bt_ci_compliance = coex->bt_ci_compliance;
	spin_unlock_irqrestore(&priv->lock, flags);
}

void iwlagn_bt_rx_handler_setup(struct iwl_priv *priv)
{
	iwlagn_rx_handler_setup(priv);
	priv->rx_handlers[REPLY_BT_COEX_PROFILE_NOTIF] =
		iwlagn_bt_coex_profile_notif;
}

void iwlagn_bt_setup_deferred_work(struct iwl_priv *priv)
{
	iwlagn_setup_deferred_work(priv);

	INIT_WORK(&priv->bt_traffic_change_work,
		  iwlagn_bt_traffic_change_work);
}

void iwlagn_bt_cancel_deferred_work(struct iwl_priv *priv)
{
	cancel_work_sync(&priv->bt_traffic_change_work);
}

static bool is_single_rx_stream(struct iwl_priv *priv)
{
	return priv->current_ht_config.smps == IEEE80211_SMPS_STATIC ||
	       priv->current_ht_config.single_chain_sufficient;
}

#define IWL_NUM_RX_CHAINS_MULTIPLE	3
#define IWL_NUM_RX_CHAINS_SINGLE	2
#define IWL_NUM_IDLE_CHAINS_DUAL	2
#define IWL_NUM_IDLE_CHAINS_SINGLE	1

/*
 * Determine how many receiver/antenna chains to use.
 *
 * More provides better reception via diversity.  Fewer saves power
 * at the expense of throughput, but only when not in powersave to
 * start with.
 *
 * MIMO (dual stream) requires at least 2, but works better with 3.
 * This does not determine *which* chains to use, just how many.
 */
static int iwl_get_active_rx_chain_count(struct iwl_priv *priv)
{
	if (priv->cfg->bt_params &&
	    priv->cfg->bt_params->advanced_bt_coexist &&
	    (priv->bt_full_concurrent ||
	     priv->bt_traffic_load >= IWL_BT_COEX_TRAFFIC_LOAD_HIGH)) {
		/*
		 * only use chain 'A' in bt high traffic load or
		 * full concurrency mode
		 */
		return IWL_NUM_RX_CHAINS_SINGLE;
	}
	/* # of Rx chains to use when expecting MIMO. */
	if (is_single_rx_stream(priv))
		return IWL_NUM_RX_CHAINS_SINGLE;
	else
		return IWL_NUM_RX_CHAINS_MULTIPLE;
}

/*
 * When we are in power saving mode, unless device support spatial
 * multiplexing power save, use the active count for rx chain count.
 */
static int iwl_get_idle_rx_chain_count(struct iwl_priv *priv, int active_cnt)
{
	/* # Rx chains when idling, depending on SMPS mode */
	switch (priv->current_ht_config.smps) {
	case IEEE80211_SMPS_STATIC:
	case IEEE80211_SMPS_DYNAMIC:
		return IWL_NUM_IDLE_CHAINS_SINGLE;
	case IEEE80211_SMPS_OFF:
		return active_cnt;
	default:
		WARN(1, "invalid SMPS mode %d",
		     priv->current_ht_config.smps);
		return active_cnt;
	}
}

/* up to 4 chains */
static u8 iwl_count_chain_bitmap(u32 chain_bitmap)
{
	u8 res;
	res = (chain_bitmap & BIT(0)) >> 0;
	res += (chain_bitmap & BIT(1)) >> 1;
	res += (chain_bitmap & BIT(2)) >> 2;
	res += (chain_bitmap & BIT(3)) >> 3;
	return res;
}

/**
 * iwlagn_set_rxon_chain - Set up Rx chain usage in "staging" RXON image
 *
 * Selects how many and which Rx receivers/antennas/chains to use.
 * This should not be used for scan command ... it puts data in wrong place.
 */
void iwlagn_set_rxon_chain(struct iwl_priv *priv, struct iwl_rxon_context *ctx)
{
	bool is_single = is_single_rx_stream(priv);
	bool is_cam = !test_bit(STATUS_POWER_PMI, &priv->status);
	u8 idle_rx_cnt, active_rx_cnt, valid_rx_cnt;
	u32 active_chains;
	u16 rx_chain;

	/* Tell uCode which antennas are actually connected.
	 * Before first association, we assume all antennas are connected.
	 * Just after first association, iwl_chain_noise_calibration()
	 *    checks which antennas actually *are* connected. */
	if (priv->chain_noise_data.active_chains)
		active_chains = priv->chain_noise_data.active_chains;
	else
		active_chains = priv->hw_params.valid_rx_ant;

	if (priv->cfg->bt_params &&
	    priv->cfg->bt_params->advanced_bt_coexist &&
	    (priv->bt_full_concurrent ||
	     priv->bt_traffic_load >= IWL_BT_COEX_TRAFFIC_LOAD_HIGH)) {
		/*
		 * only use chain 'A' in bt high traffic load or
		 * full concurrency mode
		 */
		active_chains = first_antenna(active_chains);
	}

	rx_chain = active_chains << RXON_RX_CHAIN_VALID_POS;

	/* How many receivers should we use? */
	active_rx_cnt = iwl_get_active_rx_chain_count(priv);
	idle_rx_cnt = iwl_get_idle_rx_chain_count(priv, active_rx_cnt);


	/* correct rx chain count according hw settings
	 * and chain noise calibration
	 */
	valid_rx_cnt = iwl_count_chain_bitmap(active_chains);
	if (valid_rx_cnt < active_rx_cnt)
		active_rx_cnt = valid_rx_cnt;

	if (valid_rx_cnt < idle_rx_cnt)
		idle_rx_cnt = valid_rx_cnt;

	rx_chain |= active_rx_cnt << RXON_RX_CHAIN_MIMO_CNT_POS;
	rx_chain |= idle_rx_cnt  << RXON_RX_CHAIN_CNT_POS;

	ctx->staging.rx_chain = cpu_to_le16(rx_chain);

	if (!is_single && (active_rx_cnt >= IWL_NUM_RX_CHAINS_SINGLE) && is_cam)
		ctx->staging.rx_chain |= RXON_RX_CHAIN_MIMO_FORCE_MSK;
	else
		ctx->staging.rx_chain &= ~RXON_RX_CHAIN_MIMO_FORCE_MSK;

	IWL_DEBUG_ASSOC(priv, "rx_chain=0x%X active=%d idle=%d\n",
			ctx->staging.rx_chain,
			active_rx_cnt, idle_rx_cnt);

	WARN_ON(active_rx_cnt == 0 || idle_rx_cnt == 0 ||
		active_rx_cnt < idle_rx_cnt);
}

u8 iwl_toggle_tx_ant(struct iwl_priv *priv, u8 ant, u8 valid)
{
	int i;
	u8 ind = ant;

	if (priv->band == IEEE80211_BAND_2GHZ &&
	    priv->bt_traffic_load >= IWL_BT_COEX_TRAFFIC_LOAD_HIGH)
		return 0;

	for (i = 0; i < RATE_ANT_NUM - 1; i++) {
		ind = (ind + 1) < RATE_ANT_NUM ?  ind + 1 : 0;
		if (valid & BIT(ind))
			return ind;
	}
	return ant;
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

void iwl_dump_csr(struct iwl_priv *priv)
{
	int i;
	u32 csr_tbl[] = {
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
	IWL_ERR(priv, "CSR values:\n");
	IWL_ERR(priv, "(2nd byte of CSR_INT_COALESCING is "
		"CSR_INT_PERIODIC_REG)\n");
	for (i = 0; i <  ARRAY_SIZE(csr_tbl); i++) {
		IWL_ERR(priv, "  %25s: 0X%08x\n",
			get_csr_string(csr_tbl[i]),
			iwl_read32(priv, csr_tbl[i]));
	}
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

int iwl_dump_fh(struct iwl_priv *priv, char **buf, bool display)
{
	int i;
#ifdef CONFIG_IWLWIFI_DEBUG
	int pos = 0;
	size_t bufsz = 0;
#endif
	u32 fh_tbl[] = {
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
				iwl_read_direct32(priv, fh_tbl[i]));
		}
		return pos;
	}
#endif
	IWL_ERR(priv, "FH register values:\n");
	for (i = 0; i <  ARRAY_SIZE(fh_tbl); i++) {
		IWL_ERR(priv, "  %34s: 0X%08x\n",
			get_fh_string(fh_tbl[i]),
			iwl_read_direct32(priv, fh_tbl[i]));
	}
	return 0;
}
