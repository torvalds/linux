/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
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
#include "iwl-trans.h"

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
		priv->reply_tx_stats.pp_delay++;
		break;
	case TX_STATUS_POSTPONE_FEW_BYTES:
		priv->reply_tx_stats.pp_few_bytes++;
		break;
	case TX_STATUS_POSTPONE_BT_PRIO:
		priv->reply_tx_stats.pp_bt_prio++;
		break;
	case TX_STATUS_POSTPONE_QUIET_PERIOD:
		priv->reply_tx_stats.pp_quiet_period++;
		break;
	case TX_STATUS_POSTPONE_CALC_TTAK:
		priv->reply_tx_stats.pp_calc_ttak++;
		break;
	case TX_STATUS_FAIL_INTERNAL_CROSSED_RETRY:
		priv->reply_tx_stats.int_crossed_retry++;
		break;
	case TX_STATUS_FAIL_SHORT_LIMIT:
		priv->reply_tx_stats.short_limit++;
		break;
	case TX_STATUS_FAIL_LONG_LIMIT:
		priv->reply_tx_stats.long_limit++;
		break;
	case TX_STATUS_FAIL_FIFO_UNDERRUN:
		priv->reply_tx_stats.fifo_underrun++;
		break;
	case TX_STATUS_FAIL_DRAIN_FLOW:
		priv->reply_tx_stats.drain_flow++;
		break;
	case TX_STATUS_FAIL_RFKILL_FLUSH:
		priv->reply_tx_stats.rfkill_flush++;
		break;
	case TX_STATUS_FAIL_LIFE_EXPIRE:
		priv->reply_tx_stats.life_expire++;
		break;
	case TX_STATUS_FAIL_DEST_PS:
		priv->reply_tx_stats.dest_ps++;
		break;
	case TX_STATUS_FAIL_HOST_ABORTED:
		priv->reply_tx_stats.host_abort++;
		break;
	case TX_STATUS_FAIL_BT_RETRY:
		priv->reply_tx_stats.bt_retry++;
		break;
	case TX_STATUS_FAIL_STA_INVALID:
		priv->reply_tx_stats.sta_invalid++;
		break;
	case TX_STATUS_FAIL_FRAG_DROPPED:
		priv->reply_tx_stats.frag_drop++;
		break;
	case TX_STATUS_FAIL_TID_DISABLE:
		priv->reply_tx_stats.tid_disable++;
		break;
	case TX_STATUS_FAIL_FIFO_FLUSHED:
		priv->reply_tx_stats.fifo_flush++;
		break;
	case TX_STATUS_FAIL_INSUFFICIENT_CF_POLL:
		priv->reply_tx_stats.insuff_cf_poll++;
		break;
	case TX_STATUS_FAIL_PASSIVE_NO_RX:
		priv->reply_tx_stats.fail_hw_drop++;
		break;
	case TX_STATUS_FAIL_NO_BEACON_ON_RADAR:
		priv->reply_tx_stats.sta_color_mismatch++;
		break;
	default:
		priv->reply_tx_stats.unknown++;
		break;
	}
}

static void iwlagn_count_agg_tx_err_status(struct iwl_priv *priv, u16 status)
{
	status &= AGG_TX_STATUS_MSK;

	switch (status) {
	case AGG_TX_STATE_UNDERRUN_MSK:
		priv->reply_agg_tx_stats.underrun++;
		break;
	case AGG_TX_STATE_BT_PRIO_MSK:
		priv->reply_agg_tx_stats.bt_prio++;
		break;
	case AGG_TX_STATE_FEW_BYTES_MSK:
		priv->reply_agg_tx_stats.few_bytes++;
		break;
	case AGG_TX_STATE_ABORT_MSK:
		priv->reply_agg_tx_stats.abort++;
		break;
	case AGG_TX_STATE_LAST_SENT_TTL_MSK:
		priv->reply_agg_tx_stats.last_sent_ttl++;
		break;
	case AGG_TX_STATE_LAST_SENT_TRY_CNT_MSK:
		priv->reply_agg_tx_stats.last_sent_try++;
		break;
	case AGG_TX_STATE_LAST_SENT_BT_KILL_MSK:
		priv->reply_agg_tx_stats.last_sent_bt_kill++;
		break;
	case AGG_TX_STATE_SCD_QUERY_MSK:
		priv->reply_agg_tx_stats.scd_query++;
		break;
	case AGG_TX_STATE_TEST_BAD_CRC32_MSK:
		priv->reply_agg_tx_stats.bad_crc32++;
		break;
	case AGG_TX_STATE_RESPONSE_MSK:
		priv->reply_agg_tx_stats.response++;
		break;
	case AGG_TX_STATE_DUMP_TX_MSK:
		priv->reply_agg_tx_stats.dump_tx++;
		break;
	case AGG_TX_STATE_DELAY_TX_MSK:
		priv->reply_agg_tx_stats.delay_tx++;
		break;
	default:
		priv->reply_agg_tx_stats.unknown++;
		break;
	}
}

static void iwlagn_set_tx_status(struct iwl_priv *priv,
				 struct ieee80211_tx_info *info,
				 struct iwl_rxon_context *ctx,
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

	if (status == TX_STATUS_FAIL_PASSIVE_NO_RX &&
	    iwl_is_associated_ctx(ctx) && ctx->vif &&
	    ctx->vif->type == NL80211_IFTYPE_STATION) {
		ctx->last_tx_rejected = true;
		iwl_stop_queue(priv, &priv->txq[txq_id]);
	}

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
		struct iwl_tx_info *txb;

		/* Only one frame was attempted; no block-ack will arrive */
		idx = start_idx;

		IWL_DEBUG_TX_REPLY(priv, "FrameCnt = %d, StartIdx=%d idx=%d\n",
				   agg->frame_count, agg->start_idx, idx);
		txb = &priv->txq[txq_id].txb[idx];
		iwlagn_set_tx_status(priv, IEEE80211_SKB_CB(txb->skb),
				     txb->ctx, tx_resp, txq_id, true);
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

void iwlagn_rx_reply_tx(struct iwl_priv *priv, struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	u16 sequence = le16_to_cpu(pkt->hdr.sequence);
	int txq_id = SEQ_TO_QUEUE(sequence);
	int index = SEQ_TO_INDEX(sequence);
	struct iwl_tx_queue *txq = &priv->txq[txq_id];
	struct ieee80211_tx_info *info;
	struct iwlagn_tx_resp *tx_resp = (void *)&pkt->u.raw[0];
	struct ieee80211_hdr *hdr;
	struct iwl_tx_info *txb;
	u32 status = le16_to_cpu(tx_resp->status.status);
	int tid;
	int sta_id;
	int freed;
	unsigned long flags;

	if ((index >= txq->q.n_bd) || (iwl_queue_used(&txq->q, index) == 0)) {
		IWL_ERR(priv, "%s: Read index for DMA queue txq_id (%d) "
			  "index %d is out of range [0-%d] %d %d\n", __func__,
			  txq_id, index, txq->q.n_bd, txq->q.write_ptr,
			  txq->q.read_ptr);
		return;
	}

	txq->time_stamp = jiffies;
	txb = &txq->txb[txq->q.read_ptr];
	info = IEEE80211_SKB_CB(txb->skb);
	memset(&info->status, 0, sizeof(info->status));

	tid = (tx_resp->ra_tid & IWLAGN_TX_RES_TID_MSK) >>
		IWLAGN_TX_RES_TID_POS;
	sta_id = (tx_resp->ra_tid & IWLAGN_TX_RES_RA_MSK) >>
		IWLAGN_TX_RES_RA_POS;

	spin_lock_irqsave(&priv->sta_lock, flags);

	hdr = (void *)txb->skb->data;
	if (!ieee80211_is_data_qos(hdr->frame_control))
		priv->last_seq_ctl = tx_resp->seq_ctl;

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
			IWL_DEBUG_COEX(priv, "receive reply tx with bt_kill\n");
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
			    (agg->state != IWL_EMPTYING_HW_QUEUE_DELBA))
				iwl_wake_queue(priv, txq);
		}
	} else {
		iwlagn_set_tx_status(priv, info, txb->ctx, tx_resp,
				     txq_id, false);
		freed = iwlagn_tx_queue_reclaim(priv, txq_id, index);
		iwl_free_tfds_in_queue(priv, sta_id, tid, freed);

		if (priv->mac80211_registered &&
		    iwl_queue_space(&txq->q) > txq->q.low_mark &&
		    status != TX_STATUS_FAIL_PASSIVE_NO_RX)
			iwl_wake_queue(priv, txq);
	}

	iwlagn_txq_check_empty(priv, sta_id, tid, txq_id);

	iwl_check_abort_status(priv, tx_resp->frame_count, status);
	spin_unlock_irqrestore(&priv->sta_lock, flags);
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

	if (WARN_ONCE(test_bit(STATUS_SCAN_HW, &priv->status),
		      "TX Power requested while scanning!\n"))
		return -EAGAIN;

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

	return trans_send_cmd_pdu(&priv->trans, tx_ant_cfg_cmd, CMD_SYNC,
			sizeof(tx_power_cmd), &tx_power_cmd);
}

void iwlagn_temperature(struct iwl_priv *priv)
{
	/* store temperature from correct statistics (in Celsius) */
	priv->temperature = le32_to_cpu(priv->statistics.common.temperature);
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

const u8 *iwl_eeprom_query_addr(const struct iwl_priv *priv, size_t offset)
{
	u32 address = eeprom_indirect_address(priv, offset);
	BUG_ON(address >= priv->cfg->base_params->eeprom_size);
	return &priv->eeprom[address];
}

struct iwl_mod_params iwlagn_mod_params = {
	.amsdu_size_8K = 1,
	.restart_fw = 1,
	.plcp_check = true,
	.bt_coex_active = true,
	.no_sleep_autoadjust = true,
	.power_level = IWL_POWER_INDEX_1,
	/* the rest are 0 by default */
};

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

static int iwl_fill_offch_tx(struct iwl_priv *priv, void *data, size_t maxlen)
{
	struct sk_buff *skb = priv->offchan_tx_skb;

	if (skb->len < maxlen)
		maxlen = skb->len;

	memcpy(data, skb->data, maxlen);

	return maxlen;
}

int iwlagn_request_scan(struct iwl_priv *priv, struct ieee80211_vif *vif)
{
	struct iwl_host_cmd cmd = {
		.id = REPLY_SCAN_CMD,
		.len = { sizeof(struct iwl_scan_cmd), },
		.flags = CMD_SYNC,
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

	if (priv->scan_type != IWL_SCAN_OFFCH_TX &&
	    iwl_is_any_associated(priv)) {
		u16 interval = 0;
		u32 extra;
		u32 suspend_time = 100;
		u32 scan_suspend_time = 100;

		IWL_DEBUG_INFO(priv, "Scanning while associated...\n");
		switch (priv->scan_type) {
		case IWL_SCAN_OFFCH_TX:
			WARN_ON(1);
			break;
		case IWL_SCAN_RADIO_RESET:
			interval = 0;
			break;
		case IWL_SCAN_NORMAL:
			interval = vif->bss_conf.beacon_int;
			break;
		}

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
	} else if (priv->scan_type == IWL_SCAN_OFFCH_TX) {
		scan->suspend_time = 0;
		scan->max_out_time =
			cpu_to_le32(1024 * priv->offchan_tx_timeout);
	}

	switch (priv->scan_type) {
	case IWL_SCAN_RADIO_RESET:
		IWL_DEBUG_SCAN(priv, "Start internal passive scan.\n");
		break;
	case IWL_SCAN_NORMAL:
		if (priv->scan_request->n_ssids) {
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
		break;
	case IWL_SCAN_OFFCH_TX:
		IWL_DEBUG_SCAN(priv, "Start offchannel TX scan.\n");
		break;
	}

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
	 *
	 * This was fixed in later versions along with some other
	 * scan changes, and the threshold behaves as a flag in those
	 * versions.
	 */
	if (priv->new_scan_threshold_behaviour)
		scan->good_CRC_th = is_active ? IWL_GOOD_CRC_TH_DEFAULT :
						IWL_GOOD_CRC_TH_DISABLED;
	else
		scan->good_CRC_th = is_active ? IWL_GOOD_CRC_TH_DEFAULT :
						IWL_GOOD_CRC_TH_NEVER;

	band = priv->scan_band;

	if (priv->cfg->scan_rx_antennas[band])
		rx_ant = priv->cfg->scan_rx_antennas[band];

	if (band == IEEE80211_BAND_2GHZ &&
	    priv->cfg->bt_params &&
	    priv->cfg->bt_params->advanced_bt_coexist) {
		/* transmit 2.4 GHz probes only on first antenna */
		scan_tx_antennas = first_antenna(scan_tx_antennas);
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
	switch (priv->scan_type) {
	case IWL_SCAN_NORMAL:
		cmd_len = iwl_fill_probe_req(priv,
					(struct ieee80211_mgmt *)scan->data,
					vif->addr,
					priv->scan_request->ie,
					priv->scan_request->ie_len,
					IWL_MAX_SCAN_SIZE - sizeof(*scan));
		break;
	case IWL_SCAN_RADIO_RESET:
		/* use bcast addr, will not be transmitted but must be valid */
		cmd_len = iwl_fill_probe_req(priv,
					(struct ieee80211_mgmt *)scan->data,
					iwl_bcast_addr, NULL, 0,
					IWL_MAX_SCAN_SIZE - sizeof(*scan));
		break;
	case IWL_SCAN_OFFCH_TX:
		cmd_len = iwl_fill_offch_tx(priv, scan->data,
					    IWL_MAX_SCAN_SIZE
					     - sizeof(*scan)
					     - sizeof(struct iwl_scan_channel));
		scan->scan_flags |= IWL_SCAN_FLAGS_ACTION_FRAME_TX;
		break;
	default:
		BUG();
	}
	scan->tx_cmd.len = cpu_to_le16(cmd_len);

	scan->filter_flags |= (RXON_FILTER_ACCEPT_GRP_MSK |
			       RXON_FILTER_BCON_AWARE_MSK);

	switch (priv->scan_type) {
	case IWL_SCAN_RADIO_RESET:
		scan->channel_count =
			iwl_get_single_channel_for_scan(priv, vif, band,
				(void *)&scan->data[cmd_len]);
		break;
	case IWL_SCAN_NORMAL:
		scan->channel_count =
			iwl_get_channels_for_scan(priv, vif, band,
				is_active, n_probes,
				(void *)&scan->data[cmd_len]);
		break;
	case IWL_SCAN_OFFCH_TX: {
		struct iwl_scan_channel *scan_ch;

		scan->channel_count = 1;

		scan_ch = (void *)&scan->data[cmd_len];
		scan_ch->type = SCAN_CHANNEL_TYPE_ACTIVE;
		scan_ch->channel =
			cpu_to_le16(priv->offchan_tx_chan->hw_value);
		scan_ch->active_dwell =
			cpu_to_le16(priv->offchan_tx_timeout);
		scan_ch->passive_dwell = 0;

		/* Set txpower levels to defaults */
		scan_ch->dsp_atten = 110;

		/* NOTE: if we were doing 6Mb OFDM for scans we'd use
		 * power level:
		 * scan_ch->tx_gain = ((1 << 5) | (2 << 3)) | 3;
		 */
		if (priv->offchan_tx_chan->band == IEEE80211_BAND_5GHZ)
			scan_ch->tx_gain = ((1 << 5) | (3 << 3)) | 3;
		else
			scan_ch->tx_gain = ((1 << 5) | (5 << 3));
		}
		break;
	}

	if (scan->channel_count == 0) {
		IWL_DEBUG_SCAN(priv, "channel count %d\n", scan->channel_count);
		return -EIO;
	}

	cmd.len[0] += le16_to_cpu(scan->tx_cmd.len) +
	    scan->channel_count * sizeof(struct iwl_scan_channel);
	cmd.data[0] = scan;
	cmd.dataflags[0] = IWL_HCMD_DFL_NOCOPY;
	scan->len = cpu_to_le16(cmd.len[0]);

	/* set scan bit here for PAN params */
	set_bit(STATUS_SCAN_HW, &priv->status);

	ret = iwlagn_set_pan_params(priv);
	if (ret)
		return ret;

	ret = trans_send_cmd(&priv->trans, &cmd);
	if (ret) {
		clear_bit(STATUS_SCAN_HW, &priv->status);
		iwlagn_set_pan_params(priv);
	}

	return ret;
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
		.len = { sizeof(struct iwl_txfifo_flush_cmd), },
		.flags = CMD_SYNC,
		.data = { &flush_cmd, },
	};

	might_sleep();

	memset(&flush_cmd, 0, sizeof(flush_cmd));
	if (flush_control & BIT(IWL_RXON_CTX_BSS))
		flush_cmd.fifo_control = IWL_SCD_VO_MSK | IWL_SCD_VI_MSK |
				 IWL_SCD_BE_MSK | IWL_SCD_BK_MSK |
				 IWL_SCD_MGMT_MSK;
	if ((flush_control & BIT(IWL_RXON_CTX_PAN)) &&
	    (priv->valid_contexts != BIT(IWL_RXON_CTX_BSS)))
		flush_cmd.fifo_control |= IWL_PAN_SCD_VO_MSK |
				IWL_PAN_SCD_VI_MSK | IWL_PAN_SCD_BE_MSK |
				IWL_PAN_SCD_BK_MSK | IWL_PAN_SCD_MGMT_MSK |
				IWL_PAN_SCD_MULTICAST_MSK;

	if (priv->cfg->sku & EEPROM_SKU_CAP_11N_ENABLE)
		flush_cmd.fifo_control |= IWL_AGG_TX_QUEUE_MSK;

	IWL_DEBUG_INFO(priv, "fifo queue control: 0X%x\n",
		       flush_cmd.fifo_control);
	flush_cmd.flush_control = cpu_to_le16(flush_control);

	return trans_send_cmd(&priv->trans, &cmd);
}

void iwlagn_dev_txfifo_flush(struct iwl_priv *priv, u16 flush_control)
{
	mutex_lock(&priv->mutex);
	ieee80211_stop_queues(priv->hw);
	if (iwlagn_txfifo_flush(priv, IWL_DROP_ALL)) {
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
	cpu_to_le32(0xf0005000),
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
	struct iwl_basic_bt_cmd basic = {
		.max_kill = IWLAGN_BT_MAX_KILL_DEFAULT,
		.bt3_timer_t7_value = IWLAGN_BT3_T7_DEFAULT,
		.bt3_prio_sample_time = IWLAGN_BT3_PRIO_SAMPLE_DEFAULT,
		.bt3_timer_t2_value = IWLAGN_BT3_T2_DEFAULT,
	};
	struct iwl6000_bt_cmd bt_cmd_6000;
	struct iwl2000_bt_cmd bt_cmd_2000;
	int ret;

	BUILD_BUG_ON(sizeof(iwlagn_def_3w_lookup) !=
			sizeof(basic.bt3_lookup_table));

	if (priv->cfg->bt_params) {
		if (priv->cfg->bt_params->bt_session_2) {
			bt_cmd_2000.prio_boost = cpu_to_le32(
				priv->cfg->bt_params->bt_prio_boost);
			bt_cmd_2000.tx_prio_boost = 0;
			bt_cmd_2000.rx_prio_boost = 0;
		} else {
			bt_cmd_6000.prio_boost =
				priv->cfg->bt_params->bt_prio_boost;
			bt_cmd_6000.tx_prio_boost = 0;
			bt_cmd_6000.rx_prio_boost = 0;
		}
	} else {
		IWL_ERR(priv, "failed to construct BT Coex Config\n");
		return;
	}

	basic.kill_ack_mask = priv->kill_ack_mask;
	basic.kill_cts_mask = priv->kill_cts_mask;
	basic.valid = priv->bt_valid;

	/*
	 * Configure BT coex mode to "no coexistence" when the
	 * user disabled BT coexistence, we have no interface
	 * (might be in monitor mode), or the interface is in
	 * IBSS mode (no proper uCode support for coex then).
	 */
	if (!iwlagn_mod_params.bt_coex_active ||
	    priv->iw_mode == NL80211_IFTYPE_ADHOC) {
		basic.flags = IWLAGN_BT_FLAG_COEX_MODE_DISABLED;
	} else {
		basic.flags = IWLAGN_BT_FLAG_COEX_MODE_3W <<
					IWLAGN_BT_FLAG_COEX_MODE_SHIFT;

		if (!priv->bt_enable_pspoll)
			basic.flags |= IWLAGN_BT_FLAG_SYNC_2_BT_DISABLE;
		else
			basic.flags &= ~IWLAGN_BT_FLAG_SYNC_2_BT_DISABLE;

		if (priv->bt_ch_announce)
			basic.flags |= IWLAGN_BT_FLAG_CHANNEL_INHIBITION;
		IWL_DEBUG_COEX(priv, "BT coex flag: 0X%x\n", basic.flags);
	}
	priv->bt_enable_flag = basic.flags;
	if (priv->bt_full_concurrent)
		memcpy(basic.bt3_lookup_table, iwlagn_concurrent_lookup,
			sizeof(iwlagn_concurrent_lookup));
	else
		memcpy(basic.bt3_lookup_table, iwlagn_def_3w_lookup,
			sizeof(iwlagn_def_3w_lookup));

	IWL_DEBUG_COEX(priv, "BT coex %s in %s mode\n",
		       basic.flags ? "active" : "disabled",
		       priv->bt_full_concurrent ?
		       "full concurrency" : "3-wire");

	if (priv->cfg->bt_params->bt_session_2) {
		memcpy(&bt_cmd_2000.basic, &basic,
			sizeof(basic));
		ret = trans_send_cmd_pdu(&priv->trans, REPLY_BT_CONFIG,
			CMD_SYNC, sizeof(bt_cmd_2000), &bt_cmd_2000);
	} else {
		memcpy(&bt_cmd_6000.basic, &basic,
			sizeof(basic));
		ret = trans_send_cmd_pdu(&priv->trans, REPLY_BT_CONFIG,
			CMD_SYNC, sizeof(bt_cmd_6000), &bt_cmd_6000);
	}
	if (ret)
		IWL_ERR(priv, "failed to send BT Coex Config\n");

}

void iwlagn_bt_adjust_rssi_monitor(struct iwl_priv *priv, bool rssi_ena)
{
	struct iwl_rxon_context *ctx, *found_ctx = NULL;
	bool found_ap = false;

	lockdep_assert_held(&priv->mutex);

	/* Check whether AP or GO mode is active. */
	if (rssi_ena) {
		for_each_context(priv, ctx) {
			if (ctx->vif && ctx->vif->type == NL80211_IFTYPE_AP &&
			    iwl_is_associated_ctx(ctx)) {
				found_ap = true;
				break;
			}
		}
	}

	/*
	 * If disable was received or If GO/AP mode, disable RSSI
	 * measurements.
	 */
	if (!rssi_ena || found_ap) {
		if (priv->cur_rssi_ctx) {
			ctx = priv->cur_rssi_ctx;
			ieee80211_disable_rssi_reports(ctx->vif);
			priv->cur_rssi_ctx = NULL;
		}
		return;
	}

	/*
	 * If rssi measurements need to be enabled, consider all cases now.
	 * Figure out how many contexts are active.
	 */
	for_each_context(priv, ctx) {
		if (ctx->vif && ctx->vif->type == NL80211_IFTYPE_STATION &&
		    iwl_is_associated_ctx(ctx)) {
			found_ctx = ctx;
			break;
		}
	}

	/*
	 * rssi monitor already enabled for the correct interface...nothing
	 * to do.
	 */
	if (found_ctx == priv->cur_rssi_ctx)
		return;

	/*
	 * Figure out if rssi monitor is currently enabled, and needs
	 * to be changed. If rssi monitor is already enabled, disable
	 * it first else just enable rssi measurements on the
	 * interface found above.
	 */
	if (priv->cur_rssi_ctx) {
		ctx = priv->cur_rssi_ctx;
		if (ctx->vif)
			ieee80211_disable_rssi_reports(ctx->vif);
	}

	priv->cur_rssi_ctx = found_ctx;

	if (!found_ctx)
		return;

	ieee80211_enable_rssi_reports(found_ctx->vif,
			IWLAGN_BT_PSP_MIN_RSSI_THRESHOLD,
			IWLAGN_BT_PSP_MAX_RSSI_THRESHOLD);
}

static bool iwlagn_bt_traffic_is_sco(struct iwl_bt_uart_msg *uart_msg)
{
	return BT_UART_MSG_FRAME3SCOESCO_MSK & uart_msg->frame3 >>
			BT_UART_MSG_FRAME3SCOESCO_POS;
}

static void iwlagn_bt_traffic_change_work(struct work_struct *work)
{
	struct iwl_priv *priv =
		container_of(work, struct iwl_priv, bt_traffic_change_work);
	struct iwl_rxon_context *ctx;
	int smps_request = -1;

	if (priv->bt_enable_flag == IWLAGN_BT_FLAG_COEX_MODE_DISABLED) {
		/* bt coex disabled */
		return;
	}

	/*
	 * Note: bt_traffic_load can be overridden by scan complete and
	 * coex profile notifications. Ignore that since only bad consequence
	 * can be not matching debug print with actual state.
	 */
	IWL_DEBUG_COEX(priv, "BT traffic load changes: %d\n",
		       priv->bt_traffic_load);

	switch (priv->bt_traffic_load) {
	case IWL_BT_COEX_TRAFFIC_LOAD_NONE:
		if (priv->bt_status)
			smps_request = IEEE80211_SMPS_DYNAMIC;
		else
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

	/*
	 * We can not send command to firmware while scanning. When the scan
	 * complete we will schedule this work again. We do check with mutex
	 * locked to prevent new scan request to arrive. We do not check
	 * STATUS_SCANNING to avoid race when queue_work two times from
	 * different notifications, but quit and not perform any work at all.
	 */
	if (test_bit(STATUS_SCAN_HW, &priv->status))
		goto out;

	iwl_update_chain_flags(priv);

	if (smps_request != -1) {
		priv->current_ht_config.smps = smps_request;
		for_each_context(priv, ctx) {
			if (ctx->vif && ctx->vif->type == NL80211_IFTYPE_STATION)
				ieee80211_request_smps(ctx->vif, smps_request);
		}
	}

	/*
	 * Dynamic PS poll related functionality. Adjust RSSI measurements if
	 * necessary.
	 */
	iwlagn_bt_coex_rssi_monitor(priv);
out:
	mutex_unlock(&priv->mutex);
}

/*
 * If BT sco traffic, and RSSI monitor is enabled, move measurements to the
 * correct interface or disable it if this is the last interface to be
 * removed.
 */
void iwlagn_bt_coex_rssi_monitor(struct iwl_priv *priv)
{
	if (priv->bt_is_sco &&
	    priv->bt_traffic_load == IWL_BT_COEX_TRAFFIC_LOAD_CONTINUOUS)
		iwlagn_bt_adjust_rssi_monitor(priv, true);
	else
		iwlagn_bt_adjust_rssi_monitor(priv, false);
}

static void iwlagn_print_uartmsg(struct iwl_priv *priv,
				struct iwl_bt_uart_msg *uart_msg)
{
	IWL_DEBUG_COEX(priv, "Message Type = 0x%X, SSN = 0x%X, "
			"Update Req = 0x%X",
		(BT_UART_MSG_FRAME1MSGTYPE_MSK & uart_msg->frame1) >>
			BT_UART_MSG_FRAME1MSGTYPE_POS,
		(BT_UART_MSG_FRAME1SSN_MSK & uart_msg->frame1) >>
			BT_UART_MSG_FRAME1SSN_POS,
		(BT_UART_MSG_FRAME1UPDATEREQ_MSK & uart_msg->frame1) >>
			BT_UART_MSG_FRAME1UPDATEREQ_POS);

	IWL_DEBUG_COEX(priv, "Open connections = 0x%X, Traffic load = 0x%X, "
			"Chl_SeqN = 0x%X, In band = 0x%X",
		(BT_UART_MSG_FRAME2OPENCONNECTIONS_MSK & uart_msg->frame2) >>
			BT_UART_MSG_FRAME2OPENCONNECTIONS_POS,
		(BT_UART_MSG_FRAME2TRAFFICLOAD_MSK & uart_msg->frame2) >>
			BT_UART_MSG_FRAME2TRAFFICLOAD_POS,
		(BT_UART_MSG_FRAME2CHLSEQN_MSK & uart_msg->frame2) >>
			BT_UART_MSG_FRAME2CHLSEQN_POS,
		(BT_UART_MSG_FRAME2INBAND_MSK & uart_msg->frame2) >>
			BT_UART_MSG_FRAME2INBAND_POS);

	IWL_DEBUG_COEX(priv, "SCO/eSCO = 0x%X, Sniff = 0x%X, A2DP = 0x%X, "
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

	IWL_DEBUG_COEX(priv, "Idle duration = 0x%X",
		(BT_UART_MSG_FRAME4IDLEDURATION_MSK & uart_msg->frame4) >>
			BT_UART_MSG_FRAME4IDLEDURATION_POS);

	IWL_DEBUG_COEX(priv, "Tx Activity = 0x%X, Rx Activity = 0x%X, "
			"eSCO Retransmissions = 0x%X",
		(BT_UART_MSG_FRAME5TXACTIVITY_MSK & uart_msg->frame5) >>
			BT_UART_MSG_FRAME5TXACTIVITY_POS,
		(BT_UART_MSG_FRAME5RXACTIVITY_MSK & uart_msg->frame5) >>
			BT_UART_MSG_FRAME5RXACTIVITY_POS,
		(BT_UART_MSG_FRAME5ESCORETRANSMIT_MSK & uart_msg->frame5) >>
			BT_UART_MSG_FRAME5ESCORETRANSMIT_POS);

	IWL_DEBUG_COEX(priv, "Sniff Interval = 0x%X, Discoverable = 0x%X",
		(BT_UART_MSG_FRAME6SNIFFINTERVAL_MSK & uart_msg->frame6) >>
			BT_UART_MSG_FRAME6SNIFFINTERVAL_POS,
		(BT_UART_MSG_FRAME6DISCOVERABLE_MSK & uart_msg->frame6) >>
			BT_UART_MSG_FRAME6DISCOVERABLE_POS);

	IWL_DEBUG_COEX(priv, "Sniff Activity = 0x%X, Page = "
			"0x%X, Inquiry = 0x%X, Connectable = 0x%X",
		(BT_UART_MSG_FRAME7SNIFFACTIVITY_MSK & uart_msg->frame7) >>
			BT_UART_MSG_FRAME7SNIFFACTIVITY_POS,
		(BT_UART_MSG_FRAME7PAGE_MSK & uart_msg->frame7) >>
			BT_UART_MSG_FRAME7PAGE_POS,
		(BT_UART_MSG_FRAME7INQUIRY_MSK & uart_msg->frame7) >>
			BT_UART_MSG_FRAME7INQUIRY_POS,
		(BT_UART_MSG_FRAME7CONNECTABLE_MSK & uart_msg->frame7) >>
			BT_UART_MSG_FRAME7CONNECTABLE_POS);
}

static void iwlagn_set_kill_msk(struct iwl_priv *priv,
				struct iwl_bt_uart_msg *uart_msg)
{
	u8 kill_msk;
	static const __le32 bt_kill_ack_msg[2] = {
		IWLAGN_BT_KILL_ACK_MASK_DEFAULT,
		IWLAGN_BT_KILL_ACK_CTS_MASK_SCO };
	static const __le32 bt_kill_cts_msg[2] = {
		IWLAGN_BT_KILL_CTS_MASK_DEFAULT,
		IWLAGN_BT_KILL_ACK_CTS_MASK_SCO };

	kill_msk = (BT_UART_MSG_FRAME3SCOESCO_MSK & uart_msg->frame3)
		? 1 : 0;
	if (priv->kill_ack_mask != bt_kill_ack_msg[kill_msk] ||
	    priv->kill_cts_mask != bt_kill_cts_msg[kill_msk]) {
		priv->bt_valid |= IWLAGN_BT_VALID_KILL_ACK_MASK;
		priv->kill_ack_mask = bt_kill_ack_msg[kill_msk];
		priv->bt_valid |= IWLAGN_BT_VALID_KILL_CTS_MASK;
		priv->kill_cts_mask = bt_kill_cts_msg[kill_msk];

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
	struct iwl_bt_uart_msg *uart_msg = &coex->last_bt_uart_msg;

	if (priv->bt_enable_flag == IWLAGN_BT_FLAG_COEX_MODE_DISABLED) {
		/* bt coex disabled */
		return;
	}

	IWL_DEBUG_COEX(priv, "BT Coex notification:\n");
	IWL_DEBUG_COEX(priv, "    status: %d\n", coex->bt_status);
	IWL_DEBUG_COEX(priv, "    traffic load: %d\n", coex->bt_traffic_load);
	IWL_DEBUG_COEX(priv, "    CI compliance: %d\n",
			coex->bt_ci_compliance);
	iwlagn_print_uartmsg(priv, uart_msg);

	priv->last_bt_traffic_load = priv->bt_traffic_load;
	priv->bt_is_sco = iwlagn_bt_traffic_is_sco(uart_msg);

	if (priv->iw_mode != NL80211_IFTYPE_ADHOC) {
		if (priv->bt_status != coex->bt_status ||
		    priv->last_bt_traffic_load != coex->bt_traffic_load) {
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
	}

	iwlagn_set_kill_msk(priv, uart_msg);

	/* FIXME: based on notification, adjust the prio_boost */

	spin_lock_irqsave(&priv->lock, flags);
	priv->bt_ci_compliance = coex->bt_ci_compliance;
	spin_unlock_irqrestore(&priv->lock, flags);
}

void iwlagn_bt_rx_handler_setup(struct iwl_priv *priv)
{
	priv->rx_handlers[REPLY_BT_COEX_PROFILE_NOTIF] =
		iwlagn_bt_coex_profile_notif;
}

void iwlagn_bt_setup_deferred_work(struct iwl_priv *priv)
{
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

/* notification wait support */
void iwlagn_init_notification_wait(struct iwl_priv *priv,
				   struct iwl_notification_wait *wait_entry,
				   u8 cmd,
				   void (*fn)(struct iwl_priv *priv,
					      struct iwl_rx_packet *pkt,
					      void *data),
				   void *fn_data)
{
	wait_entry->fn = fn;
	wait_entry->fn_data = fn_data;
	wait_entry->cmd = cmd;
	wait_entry->triggered = false;
	wait_entry->aborted = false;

	spin_lock_bh(&priv->notif_wait_lock);
	list_add(&wait_entry->list, &priv->notif_waits);
	spin_unlock_bh(&priv->notif_wait_lock);
}

int iwlagn_wait_notification(struct iwl_priv *priv,
			     struct iwl_notification_wait *wait_entry,
			     unsigned long timeout)
{
	int ret;

	ret = wait_event_timeout(priv->notif_waitq,
				 wait_entry->triggered || wait_entry->aborted,
				 timeout);

	spin_lock_bh(&priv->notif_wait_lock);
	list_del(&wait_entry->list);
	spin_unlock_bh(&priv->notif_wait_lock);

	if (wait_entry->aborted)
		return -EIO;

	/* return value is always >= 0 */
	if (ret <= 0)
		return -ETIMEDOUT;
	return 0;
}

void iwlagn_remove_notification(struct iwl_priv *priv,
				struct iwl_notification_wait *wait_entry)
{
	spin_lock_bh(&priv->notif_wait_lock);
	list_del(&wait_entry->list);
	spin_unlock_bh(&priv->notif_wait_lock);
}
