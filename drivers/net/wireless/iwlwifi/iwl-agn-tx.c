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
#include "iwl-sta.h"
#include "iwl-io.h"
#include "iwl-helpers.h"
#include "iwl-agn-hw.h"
#include "iwl-agn.h"

/*
 * mac80211 queues, ACs, hardware queues, FIFOs.
 *
 * Cf. http://wireless.kernel.org/en/developers/Documentation/mac80211/queues
 *
 * Mac80211 uses the following numbers, which we get as from it
 * by way of skb_get_queue_mapping(skb):
 *
 *	VO	0
 *	VI	1
 *	BE	2
 *	BK	3
 *
 *
 * Regular (not A-MPDU) frames are put into hardware queues corresponding
 * to the FIFOs, see comments in iwl-prph.h. Aggregated frames get their
 * own queue per aggregation session (RA/TID combination), such queues are
 * set up to map into FIFOs too, for which we need an AC->FIFO mapping. In
 * order to map frames to the right queue, we also need an AC->hw queue
 * mapping. This is implemented here.
 *
 * Due to the way hw queues are set up (by the hw specific modules like
 * iwl-4965.c, iwl-5000.c etc.), the AC->hw queue mapping is the identity
 * mapping.
 */

static const u8 tid_to_ac[] = {
	/* this matches the mac80211 numbers */
	2, 3, 3, 2, 1, 1, 0, 0
};

static const u8 ac_to_fifo[] = {
	IWL_TX_FIFO_VO,
	IWL_TX_FIFO_VI,
	IWL_TX_FIFO_BE,
	IWL_TX_FIFO_BK,
};

static inline int get_fifo_from_ac(u8 ac)
{
	return ac_to_fifo[ac];
}

static inline int get_ac_from_tid(u16 tid)
{
	if (likely(tid < ARRAY_SIZE(tid_to_ac)))
		return tid_to_ac[tid];

	/* no support for TIDs 8-15 yet */
	return -EINVAL;
}

static inline int get_fifo_from_tid(u16 tid)
{
	if (likely(tid < ARRAY_SIZE(tid_to_ac)))
		return get_fifo_from_ac(tid_to_ac[tid]);

	/* no support for TIDs 8-15 yet */
	return -EINVAL;
}

/**
 * iwlagn_txq_update_byte_cnt_tbl - Set up entry in Tx byte-count array
 */
void iwlagn_txq_update_byte_cnt_tbl(struct iwl_priv *priv,
					    struct iwl_tx_queue *txq,
					    u16 byte_cnt)
{
	struct iwlagn_scd_bc_tbl *scd_bc_tbl = priv->scd_bc_tbls.addr;
	int write_ptr = txq->q.write_ptr;
	int txq_id = txq->q.id;
	u8 sec_ctl = 0;
	u8 sta_id = 0;
	u16 len = byte_cnt + IWL_TX_CRC_SIZE + IWL_TX_DELIMITER_SIZE;
	__le16 bc_ent;

	WARN_ON(len > 0xFFF || write_ptr >= TFD_QUEUE_SIZE_MAX);

	if (txq_id != IWL_CMD_QUEUE_NUM) {
		sta_id = txq->cmd[txq->q.write_ptr]->cmd.tx.sta_id;
		sec_ctl = txq->cmd[txq->q.write_ptr]->cmd.tx.sec_ctl;

		switch (sec_ctl & TX_CMD_SEC_MSK) {
		case TX_CMD_SEC_CCM:
			len += CCMP_MIC_LEN;
			break;
		case TX_CMD_SEC_TKIP:
			len += TKIP_ICV_LEN;
			break;
		case TX_CMD_SEC_WEP:
			len += WEP_IV_LEN + WEP_ICV_LEN;
			break;
		}
	}

	bc_ent = cpu_to_le16((len & 0xFFF) | (sta_id << 12));

	scd_bc_tbl[txq_id].tfd_offset[write_ptr] = bc_ent;

	if (write_ptr < TFD_QUEUE_SIZE_BC_DUP)
		scd_bc_tbl[txq_id].
			tfd_offset[TFD_QUEUE_SIZE_MAX + write_ptr] = bc_ent;
}

void iwlagn_txq_inval_byte_cnt_tbl(struct iwl_priv *priv,
					   struct iwl_tx_queue *txq)
{
	struct iwlagn_scd_bc_tbl *scd_bc_tbl = priv->scd_bc_tbls.addr;
	int txq_id = txq->q.id;
	int read_ptr = txq->q.read_ptr;
	u8 sta_id = 0;
	__le16 bc_ent;

	WARN_ON(read_ptr >= TFD_QUEUE_SIZE_MAX);

	if (txq_id != IWL_CMD_QUEUE_NUM)
		sta_id = txq->cmd[read_ptr]->cmd.tx.sta_id;

	bc_ent = cpu_to_le16(1 | (sta_id << 12));
	scd_bc_tbl[txq_id].tfd_offset[read_ptr] = bc_ent;

	if (read_ptr < TFD_QUEUE_SIZE_BC_DUP)
		scd_bc_tbl[txq_id].
			tfd_offset[TFD_QUEUE_SIZE_MAX + read_ptr] = bc_ent;
}

static int iwlagn_tx_queue_set_q2ratid(struct iwl_priv *priv, u16 ra_tid,
					u16 txq_id)
{
	u32 tbl_dw_addr;
	u32 tbl_dw;
	u16 scd_q2ratid;

	scd_q2ratid = ra_tid & IWL_SCD_QUEUE_RA_TID_MAP_RATID_MSK;

	tbl_dw_addr = priv->scd_base_addr +
			IWLAGN_SCD_TRANSLATE_TBL_OFFSET_QUEUE(txq_id);

	tbl_dw = iwl_read_targ_mem(priv, tbl_dw_addr);

	if (txq_id & 0x1)
		tbl_dw = (scd_q2ratid << 16) | (tbl_dw & 0x0000FFFF);
	else
		tbl_dw = scd_q2ratid | (tbl_dw & 0xFFFF0000);

	iwl_write_targ_mem(priv, tbl_dw_addr, tbl_dw);

	return 0;
}

static void iwlagn_tx_queue_stop_scheduler(struct iwl_priv *priv, u16 txq_id)
{
	/* Simply stop the queue, but don't change any configuration;
	 * the SCD_ACT_EN bit is the write-enable mask for the ACTIVE bit. */
	iwl_write_prph(priv,
		IWLAGN_SCD_QUEUE_STATUS_BITS(txq_id),
		(0 << IWLAGN_SCD_QUEUE_STTS_REG_POS_ACTIVE)|
		(1 << IWLAGN_SCD_QUEUE_STTS_REG_POS_SCD_ACT_EN));
}

void iwlagn_set_wr_ptrs(struct iwl_priv *priv,
				int txq_id, u32 index)
{
	iwl_write_direct32(priv, HBUS_TARG_WRPTR,
			(index & 0xff) | (txq_id << 8));
	iwl_write_prph(priv, IWLAGN_SCD_QUEUE_RDPTR(txq_id), index);
}

void iwlagn_tx_queue_set_status(struct iwl_priv *priv,
					struct iwl_tx_queue *txq,
					int tx_fifo_id, int scd_retry)
{
	int txq_id = txq->q.id;
	int active = test_bit(txq_id, &priv->txq_ctx_active_msk) ? 1 : 0;

	iwl_write_prph(priv, IWLAGN_SCD_QUEUE_STATUS_BITS(txq_id),
			(active << IWLAGN_SCD_QUEUE_STTS_REG_POS_ACTIVE) |
			(tx_fifo_id << IWLAGN_SCD_QUEUE_STTS_REG_POS_TXF) |
			(1 << IWLAGN_SCD_QUEUE_STTS_REG_POS_WSL) |
			IWLAGN_SCD_QUEUE_STTS_REG_MSK);

	txq->sched_retry = scd_retry;

	IWL_DEBUG_INFO(priv, "%s %s Queue %d on FIFO %d\n",
		       active ? "Activate" : "Deactivate",
		       scd_retry ? "BA" : "AC/CMD", txq_id, tx_fifo_id);
}

int iwlagn_txq_agg_enable(struct iwl_priv *priv, int txq_id,
			  int tx_fifo, int sta_id, int tid, u16 ssn_idx)
{
	unsigned long flags;
	u16 ra_tid;

	if ((IWLAGN_FIRST_AMPDU_QUEUE > txq_id) ||
	    (IWLAGN_FIRST_AMPDU_QUEUE + priv->cfg->num_of_ampdu_queues
	     <= txq_id)) {
		IWL_WARN(priv,
			"queue number out of range: %d, must be %d to %d\n",
			txq_id, IWLAGN_FIRST_AMPDU_QUEUE,
			IWLAGN_FIRST_AMPDU_QUEUE +
			priv->cfg->num_of_ampdu_queues - 1);
		return -EINVAL;
	}

	ra_tid = BUILD_RAxTID(sta_id, tid);

	/* Modify device's station table to Tx this TID */
	iwl_sta_tx_modify_enable_tid(priv, sta_id, tid);

	spin_lock_irqsave(&priv->lock, flags);

	/* Stop this Tx queue before configuring it */
	iwlagn_tx_queue_stop_scheduler(priv, txq_id);

	/* Map receiver-address / traffic-ID to this queue */
	iwlagn_tx_queue_set_q2ratid(priv, ra_tid, txq_id);

	/* Set this queue as a chain-building queue */
	iwl_set_bits_prph(priv, IWLAGN_SCD_QUEUECHAIN_SEL, (1<<txq_id));

	/* enable aggregations for the queue */
	iwl_set_bits_prph(priv, IWLAGN_SCD_AGGR_SEL, (1<<txq_id));

	/* Place first TFD at index corresponding to start sequence number.
	 * Assumes that ssn_idx is valid (!= 0xFFF) */
	priv->txq[txq_id].q.read_ptr = (ssn_idx & 0xff);
	priv->txq[txq_id].q.write_ptr = (ssn_idx & 0xff);
	iwlagn_set_wr_ptrs(priv, txq_id, ssn_idx);

	/* Set up Tx window size and frame limit for this queue */
	iwl_write_targ_mem(priv, priv->scd_base_addr +
			IWLAGN_SCD_CONTEXT_QUEUE_OFFSET(txq_id) +
			sizeof(u32),
			((SCD_WIN_SIZE <<
			IWLAGN_SCD_QUEUE_CTX_REG2_WIN_SIZE_POS) &
			IWLAGN_SCD_QUEUE_CTX_REG2_WIN_SIZE_MSK) |
			((SCD_FRAME_LIMIT <<
			IWLAGN_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS) &
			IWLAGN_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_MSK));

	iwl_set_bits_prph(priv, IWLAGN_SCD_INTERRUPT_MASK, (1 << txq_id));

	/* Set up Status area in SRAM, map to Tx DMA/FIFO, activate the queue */
	iwlagn_tx_queue_set_status(priv, &priv->txq[txq_id], tx_fifo, 1);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

int iwlagn_txq_agg_disable(struct iwl_priv *priv, u16 txq_id,
			   u16 ssn_idx, u8 tx_fifo)
{
	if ((IWLAGN_FIRST_AMPDU_QUEUE > txq_id) ||
	    (IWLAGN_FIRST_AMPDU_QUEUE + priv->cfg->num_of_ampdu_queues
	     <= txq_id)) {
		IWL_ERR(priv,
			"queue number out of range: %d, must be %d to %d\n",
			txq_id, IWLAGN_FIRST_AMPDU_QUEUE,
			IWLAGN_FIRST_AMPDU_QUEUE +
			priv->cfg->num_of_ampdu_queues - 1);
		return -EINVAL;
	}

	iwlagn_tx_queue_stop_scheduler(priv, txq_id);

	iwl_clear_bits_prph(priv, IWLAGN_SCD_AGGR_SEL, (1 << txq_id));

	priv->txq[txq_id].q.read_ptr = (ssn_idx & 0xff);
	priv->txq[txq_id].q.write_ptr = (ssn_idx & 0xff);
	/* supposes that ssn_idx is valid (!= 0xFFF) */
	iwlagn_set_wr_ptrs(priv, txq_id, ssn_idx);

	iwl_clear_bits_prph(priv, IWLAGN_SCD_INTERRUPT_MASK, (1 << txq_id));
	iwl_txq_ctx_deactivate(priv, txq_id);
	iwlagn_tx_queue_set_status(priv, &priv->txq[txq_id], tx_fifo, 0);

	return 0;
}

/*
 * Activate/Deactivate Tx DMA/FIFO channels according tx fifos mask
 * must be called under priv->lock and mac access
 */
void iwlagn_txq_set_sched(struct iwl_priv *priv, u32 mask)
{
	iwl_write_prph(priv, IWLAGN_SCD_TXFACT, mask);
}

static inline int get_queue_from_ac(u16 ac)
{
	return ac;
}

/*
 * handle build REPLY_TX command notification.
 */
static void iwlagn_tx_cmd_build_basic(struct iwl_priv *priv,
				  struct iwl_tx_cmd *tx_cmd,
				  struct ieee80211_tx_info *info,
				  struct ieee80211_hdr *hdr,
				  u8 std_id)
{
	__le16 fc = hdr->frame_control;
	__le32 tx_flags = tx_cmd->tx_flags;

	tx_cmd->stop_time.life_time = TX_CMD_LIFE_TIME_INFINITE;
	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK)) {
		tx_flags |= TX_CMD_FLG_ACK_MSK;
		if (ieee80211_is_mgmt(fc))
			tx_flags |= TX_CMD_FLG_SEQ_CTL_MSK;
		if (ieee80211_is_probe_resp(fc) &&
		    !(le16_to_cpu(hdr->seq_ctrl) & 0xf))
			tx_flags |= TX_CMD_FLG_TSF_MSK;
	} else {
		tx_flags &= (~TX_CMD_FLG_ACK_MSK);
		tx_flags |= TX_CMD_FLG_SEQ_CTL_MSK;
	}

	if (ieee80211_is_back_req(fc))
		tx_flags |= TX_CMD_FLG_ACK_MSK | TX_CMD_FLG_IMM_BA_RSP_MASK;


	tx_cmd->sta_id = std_id;
	if (ieee80211_has_morefrags(fc))
		tx_flags |= TX_CMD_FLG_MORE_FRAG_MSK;

	if (ieee80211_is_data_qos(fc)) {
		u8 *qc = ieee80211_get_qos_ctl(hdr);
		tx_cmd->tid_tspec = qc[0] & 0xf;
		tx_flags &= ~TX_CMD_FLG_SEQ_CTL_MSK;
	} else {
		tx_flags |= TX_CMD_FLG_SEQ_CTL_MSK;
	}

	priv->cfg->ops->utils->rts_tx_cmd_flag(info, &tx_flags);

	if ((tx_flags & TX_CMD_FLG_RTS_MSK) || (tx_flags & TX_CMD_FLG_CTS_MSK))
		tx_flags |= TX_CMD_FLG_FULL_TXOP_PROT_MSK;

	tx_flags &= ~(TX_CMD_FLG_ANT_SEL_MSK);
	if (ieee80211_is_mgmt(fc)) {
		if (ieee80211_is_assoc_req(fc) || ieee80211_is_reassoc_req(fc))
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

#define RTS_DFAULT_RETRY_LIMIT		60

static void iwlagn_tx_cmd_build_rate(struct iwl_priv *priv,
			      struct iwl_tx_cmd *tx_cmd,
			      struct ieee80211_tx_info *info,
			      __le16 fc)
{
	u32 rate_flags;
	int rate_idx;
	u8 rts_retry_limit;
	u8 data_retry_limit;
	u8 rate_plcp;

	/* Set retry limit on DATA packets and Probe Responses*/
	if (ieee80211_is_probe_resp(fc))
		data_retry_limit = 3;
	else
		data_retry_limit = IWLAGN_DEFAULT_TX_RETRY;
	tx_cmd->data_retry_limit = data_retry_limit;

	/* Set retry limit on RTS packets */
	rts_retry_limit = RTS_DFAULT_RETRY_LIMIT;
	if (data_retry_limit < rts_retry_limit)
		rts_retry_limit = data_retry_limit;
	tx_cmd->rts_retry_limit = rts_retry_limit;

	/* DATA packets will use the uCode station table for rate/antenna
	 * selection */
	if (ieee80211_is_data(fc)) {
		tx_cmd->initial_rate_index = 0;
		tx_cmd->tx_flags |= TX_CMD_FLG_STA_RATE_MSK;
		return;
	}

	/**
	 * If the current TX rate stored in mac80211 has the MCS bit set, it's
	 * not really a TX rate.  Thus, we use the lowest supported rate for
	 * this band.  Also use the lowest supported rate if the stored rate
	 * index is invalid.
	 */
	rate_idx = info->control.rates[0].idx;
	if (info->control.rates[0].flags & IEEE80211_TX_RC_MCS ||
			(rate_idx < 0) || (rate_idx > IWL_RATE_COUNT_LEGACY))
		rate_idx = rate_lowest_index(&priv->bands[info->band],
				info->control.sta);
	/* For 5 GHZ band, remap mac80211 rate indices into driver indices */
	if (info->band == IEEE80211_BAND_5GHZ)
		rate_idx += IWL_FIRST_OFDM_RATE;
	/* Get PLCP rate for tx_cmd->rate_n_flags */
	rate_plcp = iwl_rates[rate_idx].plcp;
	/* Zero out flags for this packet */
	rate_flags = 0;

	/* Set CCK flag as needed */
	if ((rate_idx >= IWL_FIRST_CCK_RATE) && (rate_idx <= IWL_LAST_CCK_RATE))
		rate_flags |= RATE_MCS_CCK_MSK;

	/* Set up RTS and CTS flags for certain packets */
	switch (fc & cpu_to_le16(IEEE80211_FCTL_STYPE)) {
	case cpu_to_le16(IEEE80211_STYPE_AUTH):
	case cpu_to_le16(IEEE80211_STYPE_DEAUTH):
	case cpu_to_le16(IEEE80211_STYPE_ASSOC_REQ):
	case cpu_to_le16(IEEE80211_STYPE_REASSOC_REQ):
		if (tx_cmd->tx_flags & TX_CMD_FLG_RTS_MSK) {
			tx_cmd->tx_flags &= ~TX_CMD_FLG_RTS_MSK;
			tx_cmd->tx_flags |= TX_CMD_FLG_CTS_MSK;
		}
		break;
	default:
		break;
	}

	/* Set up antennas */
	priv->mgmt_tx_ant = iwl_toggle_tx_ant(priv, priv->mgmt_tx_ant);
	rate_flags |= iwl_ant_idx_to_flags(priv->mgmt_tx_ant);

	/* Set the rate in the TX cmd */
	tx_cmd->rate_n_flags = iwl_hw_set_rate_n_flags(rate_plcp, rate_flags);
}

static void iwlagn_tx_cmd_build_hwcrypto(struct iwl_priv *priv,
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
		IWL_DEBUG_TX(priv, "tx_cmd with AES hwcrypto\n");
		break;

	case ALG_TKIP:
		tx_cmd->sec_ctl = TX_CMD_SEC_TKIP;
		ieee80211_get_tkip_key(keyconf, skb_frag,
			IEEE80211_TKIP_P2_KEY, tx_cmd->key);
		IWL_DEBUG_TX(priv, "tx_cmd with tkip hwcrypto\n");
		break;

	case ALG_WEP:
		tx_cmd->sec_ctl |= (TX_CMD_SEC_WEP |
			(keyconf->keyidx & TX_CMD_SEC_MSK) << TX_CMD_SEC_SHIFT);

		if (keyconf->keylen == WEP_KEY_LEN_128)
			tx_cmd->sec_ctl |= TX_CMD_SEC_KEY128;

		memcpy(&tx_cmd->key[3], keyconf->key, keyconf->keylen);

		IWL_DEBUG_TX(priv, "Configuring packet for WEP encryption "
			     "with key %d\n", keyconf->keyidx);
		break;

	default:
		IWL_ERR(priv, "Unknown encode alg %d\n", keyconf->alg);
		break;
	}
}

/*
 * start REPLY_TX command process
 */
int iwlagn_tx_skb(struct iwl_priv *priv, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_sta *sta = info->control.sta;
	struct iwl_station_priv *sta_priv = NULL;
	struct iwl_tx_queue *txq;
	struct iwl_queue *q;
	struct iwl_device_cmd *out_cmd;
	struct iwl_cmd_meta *out_meta;
	struct iwl_tx_cmd *tx_cmd;
	int swq_id, txq_id;
	dma_addr_t phys_addr;
	dma_addr_t txcmd_phys;
	dma_addr_t scratch_phys;
	u16 len, len_org, firstlen, secondlen;
	u16 seq_number = 0;
	__le16 fc;
	u8 hdr_len;
	u8 sta_id;
	u8 wait_write_ptr = 0;
	u8 tid = 0;
	u8 *qc = NULL;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	if (iwl_is_rfkill(priv)) {
		IWL_DEBUG_DROP(priv, "Dropping - RF KILL\n");
		goto drop_unlock;
	}

	fc = hdr->frame_control;

#ifdef CONFIG_IWLWIFI_DEBUG
	if (ieee80211_is_auth(fc))
		IWL_DEBUG_TX(priv, "Sending AUTH frame\n");
	else if (ieee80211_is_assoc_req(fc))
		IWL_DEBUG_TX(priv, "Sending ASSOC frame\n");
	else if (ieee80211_is_reassoc_req(fc))
		IWL_DEBUG_TX(priv, "Sending REASSOC frame\n");
#endif

	hdr_len = ieee80211_hdrlen(fc);

	/* Find index into station table for destination station */
	if (!info->control.sta)
		sta_id = priv->hw_params.bcast_sta_id;
	else
		sta_id = iwl_sta_id(info->control.sta);
	if (sta_id == IWL_INVALID_STATION) {
		IWL_DEBUG_DROP(priv, "Dropping - INVALID STATION: %pM\n",
			       hdr->addr1);
		goto drop_unlock;
	}

	IWL_DEBUG_TX(priv, "station Id %d\n", sta_id);

	if (sta)
		sta_priv = (void *)sta->drv_priv;

	if (sta_priv && sta_id != priv->hw_params.bcast_sta_id &&
	    sta_priv->asleep) {
		WARN_ON(!(info->flags & IEEE80211_TX_CTL_PSPOLL_RESPONSE));
		/*
		 * This sends an asynchronous command to the device,
		 * but we can rely on it being processed before the
		 * next frame is processed -- and the next frame to
		 * this station is the one that will consume this
		 * counter.
		 * For now set the counter to just 1 since we do not
		 * support uAPSD yet.
		 */
		iwl_sta_modify_sleep_tx_count(priv, sta_id, 1);
	}

	txq_id = get_queue_from_ac(skb_get_queue_mapping(skb));
	if (ieee80211_is_data_qos(fc)) {
		qc = ieee80211_get_qos_ctl(hdr);
		tid = qc[0] & IEEE80211_QOS_CTL_TID_MASK;
		if (unlikely(tid >= MAX_TID_COUNT))
			goto drop_unlock;
		seq_number = priv->stations[sta_id].tid[tid].seq_number;
		seq_number &= IEEE80211_SCTL_SEQ;
		hdr->seq_ctrl = hdr->seq_ctrl &
				cpu_to_le16(IEEE80211_SCTL_FRAG);
		hdr->seq_ctrl |= cpu_to_le16(seq_number);
		seq_number += 0x10;
		/* aggregation is on for this <sta,tid> */
		if (info->flags & IEEE80211_TX_CTL_AMPDU &&
		    priv->stations[sta_id].tid[tid].agg.state == IWL_AGG_ON) {
			txq_id = priv->stations[sta_id].tid[tid].agg.txq_id;
		}
	}

	txq = &priv->txq[txq_id];
	swq_id = txq->swq_id;
	q = &txq->q;

	if (unlikely(iwl_queue_space(q) < q->high_mark))
		goto drop_unlock;

	if (ieee80211_is_data_qos(fc))
		priv->stations[sta_id].tid[tid].tfds_in_queue++;

	/* Set up driver data for this TFD */
	memset(&(txq->txb[q->write_ptr]), 0, sizeof(struct iwl_tx_info));
	txq->txb[q->write_ptr].skb[0] = skb;

	/* Set up first empty entry in queue's array of Tx/cmd buffers */
	out_cmd = txq->cmd[q->write_ptr];
	out_meta = &txq->meta[q->write_ptr];
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


	/* Total # bytes to be transmitted */
	len = (u16)skb->len;
	tx_cmd->len = cpu_to_le16(len);

	if (info->control.hw_key)
		iwlagn_tx_cmd_build_hwcrypto(priv, info, tx_cmd, skb, sta_id);

	/* TODO need this for burst mode later on */
	iwlagn_tx_cmd_build_basic(priv, tx_cmd, info, hdr, sta_id);
	iwl_dbg_log_tx_data_frame(priv, len, hdr);

	iwlagn_tx_cmd_build_rate(priv, tx_cmd, info, fc);

	iwl_update_stats(priv, true, fc, len);
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
	firstlen = len = (len + 3) & ~3;

	if (len_org != len)
		len_org = 1;
	else
		len_org = 0;

	/* Tell NIC about any 2-byte padding after MAC header */
	if (len_org)
		tx_cmd->tx_flags |= TX_CMD_FLG_MH_PAD_MSK;

	/* Physical address of this Tx command's header (not MAC header!),
	 * within command buffer array. */
	txcmd_phys = pci_map_single(priv->pci_dev,
				    &out_cmd->hdr, len,
				    PCI_DMA_BIDIRECTIONAL);
	pci_unmap_addr_set(out_meta, mapping, txcmd_phys);
	pci_unmap_len_set(out_meta, len, len);
	/* Add buffer containing Tx command and MAC(!) header to TFD's
	 * first entry */
	priv->cfg->ops->lib->txq_attach_buf_to_tfd(priv, txq,
						   txcmd_phys, len, 1, 0);

	if (!ieee80211_has_morefrags(hdr->frame_control)) {
		txq->need_update = 1;
		if (qc)
			priv->stations[sta_id].tid[tid].seq_number = seq_number;
	} else {
		wait_write_ptr = 1;
		txq->need_update = 0;
	}

	/* Set up TFD's 2nd entry to point directly to remainder of skb,
	 * if any (802.11 null frames have no payload). */
	secondlen = len = skb->len - hdr_len;
	if (len) {
		phys_addr = pci_map_single(priv->pci_dev, skb->data + hdr_len,
					   len, PCI_DMA_TODEVICE);
		priv->cfg->ops->lib->txq_attach_buf_to_tfd(priv, txq,
							   phys_addr, len,
							   0, 0);
	}

	scratch_phys = txcmd_phys + sizeof(struct iwl_cmd_header) +
				offsetof(struct iwl_tx_cmd, scratch);

	len = sizeof(struct iwl_tx_cmd) +
		sizeof(struct iwl_cmd_header) + hdr_len;
	/* take back ownership of DMA buffer to enable update */
	pci_dma_sync_single_for_cpu(priv->pci_dev, txcmd_phys,
				    len, PCI_DMA_BIDIRECTIONAL);
	tx_cmd->dram_lsb_ptr = cpu_to_le32(scratch_phys);
	tx_cmd->dram_msb_ptr = iwl_get_dma_hi_addr(scratch_phys);

	IWL_DEBUG_TX(priv, "sequence nr = 0X%x\n",
		     le16_to_cpu(out_cmd->hdr.sequence));
	IWL_DEBUG_TX(priv, "tx_flags = 0X%x\n", le32_to_cpu(tx_cmd->tx_flags));
	iwl_print_hex_dump(priv, IWL_DL_TX, (u8 *)tx_cmd, sizeof(*tx_cmd));
	iwl_print_hex_dump(priv, IWL_DL_TX, (u8 *)tx_cmd->hdr, hdr_len);

	/* Set up entry for this TFD in Tx byte-count array */
	if (info->flags & IEEE80211_TX_CTL_AMPDU)
		priv->cfg->ops->lib->txq_update_byte_cnt_tbl(priv, txq,
						     le16_to_cpu(tx_cmd->len));

	pci_dma_sync_single_for_device(priv->pci_dev, txcmd_phys,
				       len, PCI_DMA_BIDIRECTIONAL);

	trace_iwlwifi_dev_tx(priv,
			     &((struct iwl_tfd *)txq->tfds)[txq->q.write_ptr],
			     sizeof(struct iwl_tfd),
			     &out_cmd->hdr, firstlen,
			     skb->data + hdr_len, secondlen);

	/* Tell device the write index *just past* this latest filled TFD */
	q->write_ptr = iwl_queue_inc_wrap(q->write_ptr, q->n_bd);
	iwl_txq_update_write_ptr(priv, txq);
	spin_unlock_irqrestore(&priv->lock, flags);

	/*
	 * At this point the frame is "transmitted" successfully
	 * and we will get a TX status notification eventually,
	 * regardless of the value of ret. "ret" only indicates
	 * whether or not we should update the write pointer.
	 */

	/* avoid atomic ops if it isn't an associated client */
	if (sta_priv && sta_priv->client)
		atomic_inc(&sta_priv->pending_frames);

	if ((iwl_queue_space(q) < q->high_mark) && priv->mac80211_registered) {
		if (wait_write_ptr) {
			spin_lock_irqsave(&priv->lock, flags);
			txq->need_update = 1;
			iwl_txq_update_write_ptr(priv, txq);
			spin_unlock_irqrestore(&priv->lock, flags);
		} else {
			iwl_stop_queue(priv, txq->swq_id);
		}
	}

	return 0;

drop_unlock:
	spin_unlock_irqrestore(&priv->lock, flags);
	return -1;
}

static inline int iwlagn_alloc_dma_ptr(struct iwl_priv *priv,
				    struct iwl_dma_ptr *ptr, size_t size)
{
	ptr->addr = dma_alloc_coherent(&priv->pci_dev->dev, size, &ptr->dma,
				       GFP_KERNEL);
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

	dma_free_coherent(&priv->pci_dev->dev, ptr->size, ptr->addr, ptr->dma);
	memset(ptr, 0, sizeof(*ptr));
}

/**
 * iwlagn_hw_txq_ctx_free - Free TXQ Context
 *
 * Destroy all TX DMA queues and structures
 */
void iwlagn_hw_txq_ctx_free(struct iwl_priv *priv)
{
	int txq_id;

	/* Tx queues */
	if (priv->txq) {
		for (txq_id = 0; txq_id < priv->hw_params.max_txq_num; txq_id++)
			if (txq_id == IWL_CMD_QUEUE_NUM)
				iwl_cmd_queue_free(priv);
			else
				iwl_tx_queue_free(priv, txq_id);
	}
	iwlagn_free_dma_ptr(priv, &priv->kw);

	iwlagn_free_dma_ptr(priv, &priv->scd_bc_tbls);

	/* free tx queue structure */
	iwl_free_txq_mem(priv);
}

/**
 * iwlagn_txq_ctx_alloc - allocate TX queue context
 * Allocate all Tx DMA structures and initialize them
 *
 * @param priv
 * @return error code
 */
int iwlagn_txq_ctx_alloc(struct iwl_priv *priv)
{
	int ret;
	int txq_id, slots_num;
	unsigned long flags;

	/* Free all tx/cmd queues and keep-warm buffer */
	iwlagn_hw_txq_ctx_free(priv);

	ret = iwlagn_alloc_dma_ptr(priv, &priv->scd_bc_tbls,
				priv->hw_params.scd_bc_tbls_size);
	if (ret) {
		IWL_ERR(priv, "Scheduler BC Table allocation failed\n");
		goto error_bc_tbls;
	}
	/* Alloc keep-warm buffer */
	ret = iwlagn_alloc_dma_ptr(priv, &priv->kw, IWL_KW_SIZE);
	if (ret) {
		IWL_ERR(priv, "Keep Warm allocation failed\n");
		goto error_kw;
	}

	/* allocate tx queue structure */
	ret = iwl_alloc_txq_mem(priv);
	if (ret)
		goto error;

	spin_lock_irqsave(&priv->lock, flags);

	/* Turn off all Tx DMA fifos */
	priv->cfg->ops->lib->txq_set_sched(priv, 0);

	/* Tell NIC where to find the "keep warm" buffer */
	iwl_write_direct32(priv, FH_KW_MEM_ADDR_REG, priv->kw.dma >> 4);

	spin_unlock_irqrestore(&priv->lock, flags);

	/* Alloc and init all Tx queues, including the command queue (#4) */
	for (txq_id = 0; txq_id < priv->hw_params.max_txq_num; txq_id++) {
		slots_num = (txq_id == IWL_CMD_QUEUE_NUM) ?
					TFD_CMD_SLOTS : TFD_TX_CMD_SLOTS;
		ret = iwl_tx_queue_init(priv, &priv->txq[txq_id], slots_num,
				       txq_id);
		if (ret) {
			IWL_ERR(priv, "Tx %d queue init failed\n", txq_id);
			goto error;
		}
	}

	return ret;

 error:
	iwlagn_hw_txq_ctx_free(priv);
	iwlagn_free_dma_ptr(priv, &priv->kw);
 error_kw:
	iwlagn_free_dma_ptr(priv, &priv->scd_bc_tbls);
 error_bc_tbls:
	return ret;
}

void iwlagn_txq_ctx_reset(struct iwl_priv *priv)
{
	int txq_id, slots_num;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	/* Turn off all Tx DMA fifos */
	priv->cfg->ops->lib->txq_set_sched(priv, 0);

	/* Tell NIC where to find the "keep warm" buffer */
	iwl_write_direct32(priv, FH_KW_MEM_ADDR_REG, priv->kw.dma >> 4);

	spin_unlock_irqrestore(&priv->lock, flags);

	/* Alloc and init all Tx queues, including the command queue (#4) */
	for (txq_id = 0; txq_id < priv->hw_params.max_txq_num; txq_id++) {
		slots_num = txq_id == IWL_CMD_QUEUE_NUM ?
			    TFD_CMD_SLOTS : TFD_TX_CMD_SLOTS;
		iwl_tx_queue_reset(priv, &priv->txq[txq_id], slots_num, txq_id);
	}
}

/**
 * iwlagn_txq_ctx_stop - Stop all Tx DMA channels
 */
void iwlagn_txq_ctx_stop(struct iwl_priv *priv)
{
	int ch;
	unsigned long flags;

	/* Turn off all Tx DMA fifos */
	spin_lock_irqsave(&priv->lock, flags);

	priv->cfg->ops->lib->txq_set_sched(priv, 0);

	/* Stop each Tx DMA channel, and wait for it to be idle */
	for (ch = 0; ch < priv->hw_params.dma_chnl_num; ch++) {
		iwl_write_direct32(priv, FH_TCSR_CHNL_TX_CONFIG_REG(ch), 0x0);
		iwl_poll_direct_bit(priv, FH_TSSR_TX_STATUS_REG,
				    FH_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE(ch),
				    1000);
	}
	spin_unlock_irqrestore(&priv->lock, flags);
}

/*
 * Find first available (lowest unused) Tx Queue, mark it "active".
 * Called only when finding queue for aggregation.
 * Should never return anything < 7, because they should already
 * be in use as EDCA AC (0-3), Command (4), reserved (5, 6)
 */
static int iwlagn_txq_ctx_activate_free(struct iwl_priv *priv)
{
	int txq_id;

	for (txq_id = 0; txq_id < priv->hw_params.max_txq_num; txq_id++)
		if (!test_and_set_bit(txq_id, &priv->txq_ctx_active_msk))
			return txq_id;
	return -1;
}

int iwlagn_tx_agg_start(struct iwl_priv *priv, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta, u16 tid, u16 *ssn)
{
	int sta_id;
	int tx_fifo;
	int txq_id;
	int ret;
	unsigned long flags;
	struct iwl_tid_data *tid_data;

	tx_fifo = get_fifo_from_tid(tid);
	if (unlikely(tx_fifo < 0))
		return tx_fifo;

	IWL_WARN(priv, "%s on ra = %pM tid = %d\n",
			__func__, sta->addr, tid);

	sta_id = iwl_sta_id(sta);
	if (sta_id == IWL_INVALID_STATION) {
		IWL_ERR(priv, "Start AGG on invalid station\n");
		return -ENXIO;
	}
	if (unlikely(tid >= MAX_TID_COUNT))
		return -EINVAL;

	if (priv->stations[sta_id].tid[tid].agg.state != IWL_AGG_OFF) {
		IWL_ERR(priv, "Start AGG when state is not IWL_AGG_OFF !\n");
		return -ENXIO;
	}

	txq_id = iwlagn_txq_ctx_activate_free(priv);
	if (txq_id == -1) {
		IWL_ERR(priv, "No free aggregation queue available\n");
		return -ENXIO;
	}

	spin_lock_irqsave(&priv->sta_lock, flags);
	tid_data = &priv->stations[sta_id].tid[tid];
	*ssn = SEQ_TO_SN(tid_data->seq_number);
	tid_data->agg.txq_id = txq_id;
	priv->txq[txq_id].swq_id = iwl_virtual_agg_queue_num(get_ac_from_tid(tid), txq_id);
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	ret = priv->cfg->ops->lib->txq_agg_enable(priv, txq_id, tx_fifo,
						  sta_id, tid, *ssn);
	if (ret)
		return ret;

	if (tid_data->tfds_in_queue == 0) {
		IWL_DEBUG_HT(priv, "HW queue is empty\n");
		tid_data->agg.state = IWL_AGG_ON;
		ieee80211_start_tx_ba_cb_irqsafe(vif, sta->addr, tid);
	} else {
		IWL_DEBUG_HT(priv, "HW queue is NOT empty: %d packets in HW queue\n",
			     tid_data->tfds_in_queue);
		tid_data->agg.state = IWL_EMPTYING_HW_QUEUE_ADDBA;
	}
	return ret;
}

int iwlagn_tx_agg_stop(struct iwl_priv *priv, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, u16 tid)
{
	int tx_fifo_id, txq_id, sta_id, ssn = -1;
	struct iwl_tid_data *tid_data;
	int write_ptr, read_ptr;
	unsigned long flags;

	tx_fifo_id = get_fifo_from_tid(tid);
	if (unlikely(tx_fifo_id < 0))
		return tx_fifo_id;

	sta_id = iwl_sta_id(sta);

	if (sta_id == IWL_INVALID_STATION) {
		IWL_ERR(priv, "Invalid station for AGG tid %d\n", tid);
		return -ENXIO;
	}

	if (priv->stations[sta_id].tid[tid].agg.state ==
				IWL_EMPTYING_HW_QUEUE_ADDBA) {
		IWL_DEBUG_HT(priv, "AGG stop before setup done\n");
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		priv->stations[sta_id].tid[tid].agg.state = IWL_AGG_OFF;
		return 0;
	}

	if (priv->stations[sta_id].tid[tid].agg.state != IWL_AGG_ON)
		IWL_WARN(priv, "Stopping AGG while state not ON or starting\n");

	tid_data = &priv->stations[sta_id].tid[tid];
	ssn = (tid_data->seq_number & IEEE80211_SCTL_SEQ) >> 4;
	txq_id = tid_data->agg.txq_id;
	write_ptr = priv->txq[txq_id].q.write_ptr;
	read_ptr = priv->txq[txq_id].q.read_ptr;

	/* The queue is not empty */
	if (write_ptr != read_ptr) {
		IWL_DEBUG_HT(priv, "Stopping a non empty AGG HW QUEUE\n");
		priv->stations[sta_id].tid[tid].agg.state =
				IWL_EMPTYING_HW_QUEUE_DELBA;
		return 0;
	}

	IWL_DEBUG_HT(priv, "HW queue is empty\n");
	priv->stations[sta_id].tid[tid].agg.state = IWL_AGG_OFF;

	spin_lock_irqsave(&priv->lock, flags);
	/*
	 * the only reason this call can fail is queue number out of range,
	 * which can happen if uCode is reloaded and all the station
	 * information are lost. if it is outside the range, there is no need
	 * to deactivate the uCode queue, just return "success" to allow
	 *  mac80211 to clean up it own data.
	 */
	priv->cfg->ops->lib->txq_agg_disable(priv, txq_id, ssn,
						   tx_fifo_id);
	spin_unlock_irqrestore(&priv->lock, flags);

	ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);

	return 0;
}

int iwlagn_txq_check_empty(struct iwl_priv *priv,
			   int sta_id, u8 tid, int txq_id)
{
	struct iwl_queue *q = &priv->txq[txq_id].q;
	u8 *addr = priv->stations[sta_id].sta.sta.addr;
	struct iwl_tid_data *tid_data = &priv->stations[sta_id].tid[tid];

	switch (priv->stations[sta_id].tid[tid].agg.state) {
	case IWL_EMPTYING_HW_QUEUE_DELBA:
		/* We are reclaiming the last packet of the */
		/* aggregated HW queue */
		if ((txq_id  == tid_data->agg.txq_id) &&
		    (q->read_ptr == q->write_ptr)) {
			u16 ssn = SEQ_TO_SN(tid_data->seq_number);
			int tx_fifo = get_fifo_from_tid(tid);
			IWL_DEBUG_HT(priv, "HW queue empty: continue DELBA flow\n");
			priv->cfg->ops->lib->txq_agg_disable(priv, txq_id,
							     ssn, tx_fifo);
			tid_data->agg.state = IWL_AGG_OFF;
			ieee80211_stop_tx_ba_cb_irqsafe(priv->vif, addr, tid);
		}
		break;
	case IWL_EMPTYING_HW_QUEUE_ADDBA:
		/* We are reclaiming the last packet of the queue */
		if (tid_data->tfds_in_queue == 0) {
			IWL_DEBUG_HT(priv, "HW queue empty: continue ADDBA flow\n");
			tid_data->agg.state = IWL_AGG_ON;
			ieee80211_start_tx_ba_cb_irqsafe(priv->vif, addr, tid);
		}
		break;
	}
	return 0;
}

static void iwlagn_tx_status(struct iwl_priv *priv, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_sta *sta;
	struct iwl_station_priv *sta_priv;

	rcu_read_lock();
	sta = ieee80211_find_sta(priv->vif, hdr->addr1);
	if (sta) {
		sta_priv = (void *)sta->drv_priv;
		/* avoid atomic ops if this isn't a client */
		if (sta_priv->client &&
		    atomic_dec_return(&sta_priv->pending_frames) == 0)
			ieee80211_sta_block_awake(priv->hw, sta, false);
	}
	rcu_read_unlock();

	ieee80211_tx_status_irqsafe(priv->hw, skb);
}

int iwlagn_tx_queue_reclaim(struct iwl_priv *priv, int txq_id, int index)
{
	struct iwl_tx_queue *txq = &priv->txq[txq_id];
	struct iwl_queue *q = &txq->q;
	struct iwl_tx_info *tx_info;
	int nfreed = 0;
	struct ieee80211_hdr *hdr;

	if ((index >= q->n_bd) || (iwl_queue_used(q, index) == 0)) {
		IWL_ERR(priv, "Read index for DMA queue txq id (%d), index %d, "
			  "is out of range [0-%d] %d %d.\n", txq_id,
			  index, q->n_bd, q->write_ptr, q->read_ptr);
		return 0;
	}

	for (index = iwl_queue_inc_wrap(index, q->n_bd);
	     q->read_ptr != index;
	     q->read_ptr = iwl_queue_inc_wrap(q->read_ptr, q->n_bd)) {

		tx_info = &txq->txb[txq->q.read_ptr];
		iwlagn_tx_status(priv, tx_info->skb[0]);

		hdr = (struct ieee80211_hdr *)tx_info->skb[0]->data;
		if (hdr && ieee80211_is_data_qos(hdr->frame_control))
			nfreed++;
		tx_info->skb[0] = NULL;

		if (priv->cfg->ops->lib->txq_inval_byte_cnt_tbl)
			priv->cfg->ops->lib->txq_inval_byte_cnt_tbl(priv, txq);

		priv->cfg->ops->lib->txq_free_tfd(priv, txq);
	}
	return nfreed;
}

/**
 * iwlagn_tx_status_reply_compressed_ba - Update tx status from block-ack
 *
 * Go through block-ack's bitmap of ACK'd frames, update driver's record of
 * ACK vs. not.  This gets sent to mac80211, then to rate scaling algo.
 */
static int iwlagn_tx_status_reply_compressed_ba(struct iwl_priv *priv,
				 struct iwl_ht_agg *agg,
				 struct iwl_compressed_ba_resp *ba_resp)

{
	int i, sh, ack;
	u16 seq_ctl = le16_to_cpu(ba_resp->seq_ctl);
	u16 scd_flow = le16_to_cpu(ba_resp->scd_flow);
	u64 bitmap;
	int successes = 0;
	struct ieee80211_tx_info *info;

	if (unlikely(!agg->wait_for_ba))  {
		IWL_ERR(priv, "Received BA when not expected\n");
		return -EINVAL;
	}

	/* Mark that the expected block-ack response arrived */
	agg->wait_for_ba = 0;
	IWL_DEBUG_TX_REPLY(priv, "BA %d %d\n", agg->start_idx, ba_resp->seq_ctl);

	/* Calculate shift to align block-ack bits with our Tx window bits */
	sh = agg->start_idx - SEQ_TO_INDEX(seq_ctl >> 4);
	if (sh < 0) /* tbw something is wrong with indices */
		sh += 0x100;

	/* don't use 64-bit values for now */
	bitmap = le64_to_cpu(ba_resp->bitmap) >> sh;

	if (agg->frame_count > (64 - sh)) {
		IWL_DEBUG_TX_REPLY(priv, "more frames than bitmap size");
		return -1;
	}

	/* check for success or failure according to the
	 * transmitted bitmap and block-ack bitmap */
	bitmap &= agg->bitmap;

	/* For each frame attempted in aggregation,
	 * update driver's record of tx frame's status. */
	for (i = 0; i < agg->frame_count ; i++) {
		ack = bitmap & (1ULL << i);
		successes += !!ack;
		IWL_DEBUG_TX_REPLY(priv, "%s ON i=%d idx=%d raw=%d\n",
			ack ? "ACK" : "NACK", i, (agg->start_idx + i) & 0xff,
			agg->start_idx + i);
	}

	info = IEEE80211_SKB_CB(priv->txq[scd_flow].txb[agg->start_idx].skb[0]);
	memset(&info->status, 0, sizeof(info->status));
	info->flags |= IEEE80211_TX_STAT_ACK;
	info->flags |= IEEE80211_TX_STAT_AMPDU;
	info->status.ampdu_ack_len = successes;
	info->status.ampdu_ack_map = bitmap;
	info->status.ampdu_len = agg->frame_count;
	iwlagn_hwrate_to_tx_control(priv, agg->rate_n_flags, info);

	IWL_DEBUG_TX_REPLY(priv, "Bitmap %llx\n", (unsigned long long)bitmap);

	return 0;
}

/**
 * translate ucode response to mac80211 tx status control values
 */
void iwlagn_hwrate_to_tx_control(struct iwl_priv *priv, u32 rate_n_flags,
				  struct ieee80211_tx_info *info)
{
	struct ieee80211_tx_rate *r = &info->control.rates[0];

	info->antenna_sel_tx =
		((rate_n_flags & RATE_MCS_ANT_ABC_MSK) >> RATE_MCS_ANT_POS);
	if (rate_n_flags & RATE_MCS_HT_MSK)
		r->flags |= IEEE80211_TX_RC_MCS;
	if (rate_n_flags & RATE_MCS_GF_MSK)
		r->flags |= IEEE80211_TX_RC_GREEN_FIELD;
	if (rate_n_flags & RATE_MCS_HT40_MSK)
		r->flags |= IEEE80211_TX_RC_40_MHZ_WIDTH;
	if (rate_n_flags & RATE_MCS_DUP_MSK)
		r->flags |= IEEE80211_TX_RC_DUP_DATA;
	if (rate_n_flags & RATE_MCS_SGI_MSK)
		r->flags |= IEEE80211_TX_RC_SHORT_GI;
	r->idx = iwlagn_hwrate_to_mac80211_idx(rate_n_flags, info->band);
}

/**
 * iwlagn_rx_reply_compressed_ba - Handler for REPLY_COMPRESSED_BA
 *
 * Handles block-acknowledge notification from device, which reports success
 * of frames sent via aggregation.
 */
void iwlagn_rx_reply_compressed_ba(struct iwl_priv *priv,
					   struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_compressed_ba_resp *ba_resp = &pkt->u.compressed_ba;
	struct iwl_tx_queue *txq = NULL;
	struct iwl_ht_agg *agg;
	int index;
	int sta_id;
	int tid;

	/* "flow" corresponds to Tx queue */
	u16 scd_flow = le16_to_cpu(ba_resp->scd_flow);

	/* "ssn" is start of block-ack Tx window, corresponds to index
	 * (in Tx queue's circular buffer) of first TFD/frame in window */
	u16 ba_resp_scd_ssn = le16_to_cpu(ba_resp->scd_ssn);

	if (scd_flow >= priv->hw_params.max_txq_num) {
		IWL_ERR(priv,
			"BUG_ON scd_flow is bigger than number of queues\n");
		return;
	}

	txq = &priv->txq[scd_flow];
	sta_id = ba_resp->sta_id;
	tid = ba_resp->tid;
	agg = &priv->stations[sta_id].tid[tid].agg;
	if (unlikely(agg->txq_id != scd_flow)) {
		IWL_ERR(priv, "BA scd_flow %d does not match txq_id %d\n",
			scd_flow, agg->txq_id);
		return;
	}

	/* Find index just before block-ack window */
	index = iwl_queue_dec_wrap(ba_resp_scd_ssn & 0xff, txq->q.n_bd);

	/* TODO: Need to get this copy more safely - now good for debug */

	IWL_DEBUG_TX_REPLY(priv, "REPLY_COMPRESSED_BA [%d] Received from %pM, "
			   "sta_id = %d\n",
			   agg->wait_for_ba,
			   (u8 *) &ba_resp->sta_addr_lo32,
			   ba_resp->sta_id);
	IWL_DEBUG_TX_REPLY(priv, "TID = %d, SeqCtl = %d, bitmap = 0x%llx, scd_flow = "
			   "%d, scd_ssn = %d\n",
			   ba_resp->tid,
			   ba_resp->seq_ctl,
			   (unsigned long long)le64_to_cpu(ba_resp->bitmap),
			   ba_resp->scd_flow,
			   ba_resp->scd_ssn);
	IWL_DEBUG_TX_REPLY(priv, "DAT start_idx = %d, bitmap = 0x%llx\n",
			   agg->start_idx,
			   (unsigned long long)agg->bitmap);

	/* Update driver's record of ACK vs. not for each frame in window */
	iwlagn_tx_status_reply_compressed_ba(priv, agg, ba_resp);

	/* Release all TFDs before the SSN, i.e. all TFDs in front of
	 * block-ack window (we assume that they've been successfully
	 * transmitted ... if not, it's too late anyway). */
	if (txq->q.read_ptr != (ba_resp_scd_ssn & 0xff)) {
		/* calculate mac80211 ampdu sw queue to wake */
		int freed = iwlagn_tx_queue_reclaim(priv, scd_flow, index);
		iwl_free_tfds_in_queue(priv, sta_id, tid, freed);

		if ((iwl_queue_space(&txq->q) > txq->q.low_mark) &&
		    priv->mac80211_registered &&
		    (agg->state != IWL_EMPTYING_HW_QUEUE_DELBA))
			iwl_wake_queue(priv, txq->swq_id);

		iwlagn_txq_check_empty(priv, sta_id, tid, scd_flow);
	}
}
