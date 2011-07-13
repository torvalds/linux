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
#include "iwl-trans.h"

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
	IEEE80211_AC_BE,
	IEEE80211_AC_BK,
	IEEE80211_AC_BK,
	IEEE80211_AC_BE,
	IEEE80211_AC_VI,
	IEEE80211_AC_VI,
	IEEE80211_AC_VO,
	IEEE80211_AC_VO
};

static inline int get_ac_from_tid(u16 tid)
{
	if (likely(tid < ARRAY_SIZE(tid_to_ac)))
		return tid_to_ac[tid];

	/* no support for TIDs 8-15 yet */
	return -EINVAL;
}

static inline int get_fifo_from_tid(struct iwl_rxon_context *ctx, u16 tid)
{
	if (likely(tid < ARRAY_SIZE(tid_to_ac)))
		return ctx->ac_to_fifo[tid_to_ac[tid]];

	/* no support for TIDs 8-15 yet */
	return -EINVAL;
}

static int iwlagn_txq_agg_enable(struct iwl_priv *priv, int txq_id, int sta_id,
				int tid)
{
	if ((IWLAGN_FIRST_AMPDU_QUEUE > txq_id) ||
	    (IWLAGN_FIRST_AMPDU_QUEUE +
		priv->cfg->base_params->num_of_ampdu_queues <= txq_id)) {
		IWL_WARN(priv,
			"queue number out of range: %d, must be %d to %d\n",
			txq_id, IWLAGN_FIRST_AMPDU_QUEUE,
			IWLAGN_FIRST_AMPDU_QUEUE +
			priv->cfg->base_params->num_of_ampdu_queues - 1);
		return -EINVAL;
	}

	/* Modify device's station table to Tx this TID */
	return iwl_sta_tx_modify_enable_tid(priv, sta_id, tid);
}

static void iwlagn_tx_cmd_protection(struct iwl_priv *priv,
				     struct ieee80211_tx_info *info,
				     __le16 fc, __le32 *tx_flags)
{
	if (info->control.rates[0].flags & IEEE80211_TX_RC_USE_RTS_CTS ||
	    info->control.rates[0].flags & IEEE80211_TX_RC_USE_CTS_PROTECT ||
	    info->flags & IEEE80211_TX_CTL_AMPDU)
		*tx_flags |= TX_CMD_FLG_PROT_REQUIRE_MSK;
}

/*
 * handle build REPLY_TX command notification.
 */
static void iwlagn_tx_cmd_build_basic(struct iwl_priv *priv,
					struct sk_buff *skb,
					struct iwl_tx_cmd *tx_cmd,
					struct ieee80211_tx_info *info,
					struct ieee80211_hdr *hdr,
					u8 std_id)
{
	__le16 fc = hdr->frame_control;
	__le32 tx_flags = tx_cmd->tx_flags;

	tx_cmd->stop_time.life_time = TX_CMD_LIFE_TIME_INFINITE;

	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK))
		tx_flags |= TX_CMD_FLG_ACK_MSK;
	else
		tx_flags &= ~TX_CMD_FLG_ACK_MSK;

	if (ieee80211_is_probe_resp(fc))
		tx_flags |= TX_CMD_FLG_TSF_MSK;
	else if (ieee80211_is_back_req(fc))
		tx_flags |= TX_CMD_FLG_ACK_MSK | TX_CMD_FLG_IMM_BA_RSP_MASK;
	else if (info->band == IEEE80211_BAND_2GHZ &&
		 priv->cfg->bt_params &&
		 priv->cfg->bt_params->advanced_bt_coexist &&
		 (ieee80211_is_auth(fc) || ieee80211_is_assoc_req(fc) ||
		 ieee80211_is_reassoc_req(fc) ||
		 skb->protocol == cpu_to_be16(ETH_P_PAE)))
		tx_flags |= TX_CMD_FLG_IGNORE_BT;


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

	iwlagn_tx_cmd_protection(priv, info, fc, &tx_flags);

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
#ifdef CONFIG_IWLWIFI_DEVICE_SVTOOL
		if (priv->tm_fixed_rate) {
			/*
			 * rate overwrite by testmode
			 * we not only send lq command to change rate
			 * we also re-enforce per data pkt base.
			 */
			tx_cmd->tx_flags &= ~TX_CMD_FLG_STA_RATE_MSK;
			memcpy(&tx_cmd->rate_n_flags, &priv->tm_fixed_rate,
			       sizeof(tx_cmd->rate_n_flags));
		}
#endif
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

	/* Set up antennas */
	 if (priv->cfg->bt_params &&
	     priv->cfg->bt_params->advanced_bt_coexist &&
	     priv->bt_full_concurrent) {
		/* operated as 1x1 in full concurrency mode */
		priv->mgmt_tx_ant = iwl_toggle_tx_ant(priv, priv->mgmt_tx_ant,
				first_antenna(priv->hw_params.valid_tx_ant));
	} else
		priv->mgmt_tx_ant = iwl_toggle_tx_ant(priv, priv->mgmt_tx_ant,
					      priv->hw_params.valid_tx_ant);
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

	switch (keyconf->cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
		tx_cmd->sec_ctl = TX_CMD_SEC_CCM;
		memcpy(tx_cmd->key, keyconf->key, keyconf->keylen);
		if (info->flags & IEEE80211_TX_CTL_AMPDU)
			tx_cmd->tx_flags |= TX_CMD_FLG_AGG_CCMP_MSK;
		IWL_DEBUG_TX(priv, "tx_cmd with AES hwcrypto\n");
		break;

	case WLAN_CIPHER_SUITE_TKIP:
		tx_cmd->sec_ctl = TX_CMD_SEC_TKIP;
		ieee80211_get_tkip_p2k(keyconf, skb_frag, tx_cmd->key);
		IWL_DEBUG_TX(priv, "tx_cmd with tkip hwcrypto\n");
		break;

	case WLAN_CIPHER_SUITE_WEP104:
		tx_cmd->sec_ctl |= TX_CMD_SEC_KEY128;
		/* fall through */
	case WLAN_CIPHER_SUITE_WEP40:
		tx_cmd->sec_ctl |= (TX_CMD_SEC_WEP |
			(keyconf->keyidx & TX_CMD_SEC_MSK) << TX_CMD_SEC_SHIFT);

		memcpy(&tx_cmd->key[3], keyconf->key, keyconf->keylen);

		IWL_DEBUG_TX(priv, "Configuring packet for WEP encryption "
			     "with key %d\n", keyconf->keyidx);
		break;

	default:
		IWL_ERR(priv, "Unknown encode cipher %x\n", keyconf->cipher);
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
	struct iwl_station_priv *sta_priv = NULL;
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];
	struct iwl_tx_cmd *tx_cmd;
	int txq_id;

	u16 seq_number = 0;
	__le16 fc;
	u8 hdr_len;
	u16 len;
	u8 sta_id;
	u8 tid = 0;
	unsigned long flags;
	bool is_agg = false;

	/*
	 * If the frame needs to go out off-channel, then
	 * we'll have put the PAN context to that channel,
	 * so make the frame go out there.
	 */
	if (info->flags & IEEE80211_TX_CTL_TX_OFFCHAN)
		ctx = &priv->contexts[IWL_RXON_CTX_PAN];
	else if (info->control.vif)
		ctx = iwl_rxon_ctx_from_vif(info->control.vif);

	spin_lock_irqsave(&priv->lock, flags);
	if (iwl_is_rfkill(priv)) {
		IWL_DEBUG_DROP(priv, "Dropping - RF KILL\n");
		goto drop_unlock_priv;
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

	/* For management frames use broadcast id to do not break aggregation */
	if (!ieee80211_is_data(fc))
		sta_id = ctx->bcast_sta_id;
	else {
		/* Find index into station table for destination station */
		sta_id = iwl_sta_id_or_broadcast(priv, ctx, info->control.sta);
		if (sta_id == IWL_INVALID_STATION) {
			IWL_DEBUG_DROP(priv, "Dropping - INVALID STATION: %pM\n",
				       hdr->addr1);
			goto drop_unlock_priv;
		}
	}

	IWL_DEBUG_TX(priv, "station Id %d\n", sta_id);

	if (info->control.sta)
		sta_priv = (void *)info->control.sta->drv_priv;

	if (sta_priv && sta_priv->asleep &&
	    (info->flags & IEEE80211_TX_CTL_PSPOLL_RESPONSE)) {
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

	/*
	 * Send this frame after DTIM -- there's a special queue
	 * reserved for this for contexts that support AP mode.
	 */
	if (info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM) {
		txq_id = ctx->mcast_queue;
		/*
		 * The microcode will clear the more data
		 * bit in the last frame it transmits.
		 */
		hdr->frame_control |=
			cpu_to_le16(IEEE80211_FCTL_MOREDATA);
	} else
		txq_id = ctx->ac_to_queue[skb_get_queue_mapping(skb)];

	/* irqs already disabled/saved above when locking priv->lock */
	spin_lock(&priv->sta_lock);

	if (ieee80211_is_data_qos(fc)) {
		u8 *qc = NULL;
		qc = ieee80211_get_qos_ctl(hdr);
		tid = qc[0] & IEEE80211_QOS_CTL_TID_MASK;

		if (WARN_ON_ONCE(tid >= MAX_TID_COUNT))
			goto drop_unlock_sta;

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
			is_agg = true;
		}
	}

	tx_cmd = trans_get_tx_cmd(&priv->trans, txq_id);
	if (unlikely(!tx_cmd))
		goto drop_unlock_sta;

	/* Copy MAC header from skb into command buffer */
	memcpy(tx_cmd->hdr, hdr, hdr_len);

	/* Total # bytes to be transmitted */
	len = (u16)skb->len;
	tx_cmd->len = cpu_to_le16(len);

	if (info->control.hw_key)
		iwlagn_tx_cmd_build_hwcrypto(priv, info, tx_cmd, skb, sta_id);

	/* TODO need this for burst mode later on */
	iwlagn_tx_cmd_build_basic(priv, skb, tx_cmd, info, hdr, sta_id);
	iwl_dbg_log_tx_data_frame(priv, len, hdr);

	iwlagn_tx_cmd_build_rate(priv, tx_cmd, info, fc);

	iwl_update_stats(priv, true, fc, len);

	if (trans_tx(&priv->trans, skb, tx_cmd, txq_id, fc, is_agg, ctx))
		goto drop_unlock_sta;

	if (ieee80211_is_data_qos(fc)) {
		priv->stations[sta_id].tid[tid].tfds_in_queue++;
		if (!ieee80211_has_morefrags(fc))
			priv->stations[sta_id].tid[tid].seq_number = seq_number;
	}

	spin_unlock(&priv->sta_lock);
	spin_unlock_irqrestore(&priv->lock, flags);

	/*
	 * Avoid atomic ops if it isn't an associated client.
	 * Also, if this is a packet for aggregation, don't
	 * increase the counter because the ucode will stop
	 * aggregation queues when their respective station
	 * goes to sleep.
	 */
	if (sta_priv && sta_priv->client && !is_agg)
		atomic_inc(&sta_priv->pending_frames);

	return 0;

drop_unlock_sta:
	spin_unlock(&priv->sta_lock);
drop_unlock_priv:
	spin_unlock_irqrestore(&priv->lock, flags);
	return -1;
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

	tx_fifo = get_fifo_from_tid(iwl_rxon_ctx_from_vif(vif), tid);
	if (unlikely(tx_fifo < 0))
		return tx_fifo;

	IWL_DEBUG_HT(priv, "TX AGG request on ra = %pM tid = %d\n",
		     sta->addr, tid);

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
	tid_data->agg.tx_fifo = tx_fifo;
	iwl_set_swq_id(&priv->txq[txq_id], get_ac_from_tid(tid), txq_id);
	spin_unlock_irqrestore(&priv->sta_lock, flags);

	ret = iwlagn_txq_agg_enable(priv, txq_id, sta_id, tid);
	if (ret)
		return ret;

	spin_lock_irqsave(&priv->sta_lock, flags);
	tid_data = &priv->stations[sta_id].tid[tid];
	if (tid_data->tfds_in_queue == 0) {
		IWL_DEBUG_HT(priv, "HW queue is empty\n");
		tid_data->agg.state = IWL_AGG_ON;
		ieee80211_start_tx_ba_cb_irqsafe(vif, sta->addr, tid);
	} else {
		IWL_DEBUG_HT(priv, "HW queue is NOT empty: %d packets in HW queue\n",
			     tid_data->tfds_in_queue);
		tid_data->agg.state = IWL_EMPTYING_HW_QUEUE_ADDBA;
	}
	spin_unlock_irqrestore(&priv->sta_lock, flags);
	return ret;
}

int iwlagn_tx_agg_stop(struct iwl_priv *priv, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, u16 tid)
{
	int tx_fifo_id, txq_id, sta_id, ssn;
	struct iwl_tid_data *tid_data;
	int write_ptr, read_ptr;
	unsigned long flags;

	tx_fifo_id = get_fifo_from_tid(iwl_rxon_ctx_from_vif(vif), tid);
	if (unlikely(tx_fifo_id < 0))
		return tx_fifo_id;

	sta_id = iwl_sta_id(sta);

	if (sta_id == IWL_INVALID_STATION) {
		IWL_ERR(priv, "Invalid station for AGG tid %d\n", tid);
		return -ENXIO;
	}

	spin_lock_irqsave(&priv->sta_lock, flags);

	tid_data = &priv->stations[sta_id].tid[tid];
	ssn = (tid_data->seq_number & IEEE80211_SCTL_SEQ) >> 4;
	txq_id = tid_data->agg.txq_id;

	switch (priv->stations[sta_id].tid[tid].agg.state) {
	case IWL_EMPTYING_HW_QUEUE_ADDBA:
		/*
		 * This can happen if the peer stops aggregation
		 * again before we've had a chance to drain the
		 * queue we selected previously, i.e. before the
		 * session was really started completely.
		 */
		IWL_DEBUG_HT(priv, "AGG stop before setup done\n");
		goto turn_off;
	case IWL_AGG_ON:
		break;
	default:
		IWL_WARN(priv, "Stopping AGG while state not ON or starting\n");
	}

	write_ptr = priv->txq[txq_id].q.write_ptr;
	read_ptr = priv->txq[txq_id].q.read_ptr;

	/* The queue is not empty */
	if (write_ptr != read_ptr) {
		IWL_DEBUG_HT(priv, "Stopping a non empty AGG HW QUEUE\n");
		priv->stations[sta_id].tid[tid].agg.state =
				IWL_EMPTYING_HW_QUEUE_DELBA;
		spin_unlock_irqrestore(&priv->sta_lock, flags);
		return 0;
	}

	IWL_DEBUG_HT(priv, "HW queue is empty\n");
 turn_off:
	priv->stations[sta_id].tid[tid].agg.state = IWL_AGG_OFF;

	/* do not restore/save irqs */
	spin_unlock(&priv->sta_lock);
	spin_lock(&priv->lock);

	/*
	 * the only reason this call can fail is queue number out of range,
	 * which can happen if uCode is reloaded and all the station
	 * information are lost. if it is outside the range, there is no need
	 * to deactivate the uCode queue, just return "success" to allow
	 *  mac80211 to clean up it own data.
	 */
	trans_txq_agg_disable(&priv->trans, txq_id, ssn, tx_fifo_id);
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
	struct iwl_rxon_context *ctx;

	ctx = &priv->contexts[priv->stations[sta_id].ctxid];

	lockdep_assert_held(&priv->sta_lock);

	switch (priv->stations[sta_id].tid[tid].agg.state) {
	case IWL_EMPTYING_HW_QUEUE_DELBA:
		/* We are reclaiming the last packet of the */
		/* aggregated HW queue */
		if ((txq_id  == tid_data->agg.txq_id) &&
		    (q->read_ptr == q->write_ptr)) {
			u16 ssn = SEQ_TO_SN(tid_data->seq_number);
			int tx_fifo = get_fifo_from_tid(ctx, tid);
			IWL_DEBUG_HT(priv, "HW queue empty: continue DELBA flow\n");
			trans_txq_agg_disable(&priv->trans, txq_id,
				ssn, tx_fifo);
			tid_data->agg.state = IWL_AGG_OFF;
			ieee80211_stop_tx_ba_cb_irqsafe(ctx->vif, addr, tid);
		}
		break;
	case IWL_EMPTYING_HW_QUEUE_ADDBA:
		/* We are reclaiming the last packet of the queue */
		if (tid_data->tfds_in_queue == 0) {
			IWL_DEBUG_HT(priv, "HW queue empty: continue ADDBA flow\n");
			tid_data->agg.state = IWL_AGG_ON;
			ieee80211_start_tx_ba_cb_irqsafe(ctx->vif, addr, tid);
		}
		break;
	}

	return 0;
}

static void iwlagn_non_agg_tx_status(struct iwl_priv *priv,
				     struct iwl_rxon_context *ctx,
				     const u8 *addr1)
{
	struct ieee80211_sta *sta;
	struct iwl_station_priv *sta_priv;

	rcu_read_lock();
	sta = ieee80211_find_sta(ctx->vif, addr1);
	if (sta) {
		sta_priv = (void *)sta->drv_priv;
		/* avoid atomic ops if this isn't a client */
		if (sta_priv->client &&
		    atomic_dec_return(&sta_priv->pending_frames) == 0)
			ieee80211_sta_block_awake(priv->hw, sta, false);
	}
	rcu_read_unlock();
}

static void iwlagn_tx_status(struct iwl_priv *priv, struct iwl_tx_info *tx_info,
			     bool is_agg)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) tx_info->skb->data;

	if (!is_agg)
		iwlagn_non_agg_tx_status(priv, tx_info->ctx, hdr->addr1);

	ieee80211_tx_status_irqsafe(priv->hw, tx_info->skb);
}

int iwlagn_tx_queue_reclaim(struct iwl_priv *priv, int txq_id, int index)
{
	struct iwl_tx_queue *txq = &priv->txq[txq_id];
	struct iwl_queue *q = &txq->q;
	struct iwl_tx_info *tx_info;
	int nfreed = 0;
	struct ieee80211_hdr *hdr;

	if ((index >= q->n_bd) || (iwl_queue_used(q, index) == 0)) {
		IWL_ERR(priv, "%s: Read index for DMA queue txq id (%d), "
			  "index %d is out of range [0-%d] %d %d.\n", __func__,
			  txq_id, index, q->n_bd, q->write_ptr, q->read_ptr);
		return 0;
	}

	for (index = iwl_queue_inc_wrap(index, q->n_bd);
	     q->read_ptr != index;
	     q->read_ptr = iwl_queue_inc_wrap(q->read_ptr, q->n_bd)) {

		tx_info = &txq->txb[txq->q.read_ptr];

		if (WARN_ON_ONCE(tx_info->skb == NULL))
			continue;

		hdr = (struct ieee80211_hdr *)tx_info->skb->data;
		if (ieee80211_is_data_qos(hdr->frame_control))
			nfreed++;

		iwlagn_tx_status(priv, tx_info,
				 txq_id >= IWLAGN_FIRST_AMPDU_QUEUE);
		tx_info->skb = NULL;

		iwlagn_txq_inval_byte_cnt_tbl(priv, txq);

		iwlagn_txq_free_tfd(priv, txq, txq->q.read_ptr);
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
	int sh;
	u16 seq_ctl = le16_to_cpu(ba_resp->seq_ctl);
	u16 scd_flow = le16_to_cpu(ba_resp->scd_flow);
	struct ieee80211_tx_info *info;
	u64 bitmap, sent_bitmap;

	if (unlikely(!agg->wait_for_ba))  {
		if (unlikely(ba_resp->bitmap))
			IWL_ERR(priv, "Received BA when not expected\n");
		return -EINVAL;
	}

	/* Mark that the expected block-ack response arrived */
	agg->wait_for_ba = 0;
	IWL_DEBUG_TX_REPLY(priv, "BA %d %d\n", agg->start_idx, ba_resp->seq_ctl);

	/* Calculate shift to align block-ack bits with our Tx window bits */
	sh = agg->start_idx - SEQ_TO_INDEX(seq_ctl >> 4);
	if (sh < 0)
		sh += 0x100;

	/*
	 * Check for success or failure according to the
	 * transmitted bitmap and block-ack bitmap
	 */
	bitmap = le64_to_cpu(ba_resp->bitmap) >> sh;
	sent_bitmap = bitmap & agg->bitmap;

	/* Sanity check values reported by uCode */
	if (ba_resp->txed_2_done > ba_resp->txed) {
		IWL_DEBUG_TX_REPLY(priv,
			"bogus sent(%d) and ack(%d) count\n",
			ba_resp->txed, ba_resp->txed_2_done);
		/*
		 * set txed_2_done = txed,
		 * so it won't impact rate scale
		 */
		ba_resp->txed = ba_resp->txed_2_done;
	}
	IWL_DEBUG_HT(priv, "agg frames sent:%d, acked:%d\n",
			ba_resp->txed, ba_resp->txed_2_done);

	/* Find the first ACKed frame to store the TX status */
	while (sent_bitmap && !(sent_bitmap & 1)) {
		agg->start_idx = (agg->start_idx + 1) & 0xff;
		sent_bitmap >>= 1;
	}

	info = IEEE80211_SKB_CB(priv->txq[scd_flow].txb[agg->start_idx].skb);
	memset(&info->status, 0, sizeof(info->status));
	info->flags |= IEEE80211_TX_STAT_ACK;
	info->flags |= IEEE80211_TX_STAT_AMPDU;
	info->status.ampdu_ack_len = ba_resp->txed_2_done;
	info->status.ampdu_len = ba_resp->txed;
	iwlagn_hwrate_to_tx_control(priv, agg->rate_n_flags, info);

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
	unsigned long flags;

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
		/*
		 * FIXME: this is a uCode bug which need to be addressed,
		 * log the information and return for now!
		 * since it is possible happen very often and in order
		 * not to fill the syslog, don't enable the logging by default
		 */
		IWL_DEBUG_TX_REPLY(priv,
			"BA scd_flow %d does not match txq_id %d\n",
			scd_flow, agg->txq_id);
		return;
	}

	/* Find index just before block-ack window */
	index = iwl_queue_dec_wrap(ba_resp_scd_ssn & 0xff, txq->q.n_bd);

	spin_lock_irqsave(&priv->sta_lock, flags);

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
			iwl_wake_queue(priv, txq);

		iwlagn_txq_check_empty(priv, sta_id, tid, scd_flow);
	}

	spin_unlock_irqrestore(&priv->sta_lock, flags);
}

#ifdef CONFIG_IWLWIFI_DEBUG
const char *iwl_get_tx_fail_reason(u32 status)
{
#define TX_STATUS_FAIL(x) case TX_STATUS_FAIL_ ## x: return #x
#define TX_STATUS_POSTPONE(x) case TX_STATUS_POSTPONE_ ## x: return #x

	switch (status & TX_STATUS_MSK) {
	case TX_STATUS_SUCCESS:
		return "SUCCESS";
	TX_STATUS_POSTPONE(DELAY);
	TX_STATUS_POSTPONE(FEW_BYTES);
	TX_STATUS_POSTPONE(BT_PRIO);
	TX_STATUS_POSTPONE(QUIET_PERIOD);
	TX_STATUS_POSTPONE(CALC_TTAK);
	TX_STATUS_FAIL(INTERNAL_CROSSED_RETRY);
	TX_STATUS_FAIL(SHORT_LIMIT);
	TX_STATUS_FAIL(LONG_LIMIT);
	TX_STATUS_FAIL(FIFO_UNDERRUN);
	TX_STATUS_FAIL(DRAIN_FLOW);
	TX_STATUS_FAIL(RFKILL_FLUSH);
	TX_STATUS_FAIL(LIFE_EXPIRE);
	TX_STATUS_FAIL(DEST_PS);
	TX_STATUS_FAIL(HOST_ABORTED);
	TX_STATUS_FAIL(BT_RETRY);
	TX_STATUS_FAIL(STA_INVALID);
	TX_STATUS_FAIL(FRAG_DROPPED);
	TX_STATUS_FAIL(TID_DISABLE);
	TX_STATUS_FAIL(FIFO_FLUSHED);
	TX_STATUS_FAIL(INSUFFICIENT_CF_POLL);
	TX_STATUS_FAIL(PASSIVE_NO_RX);
	TX_STATUS_FAIL(NO_BEACON_ON_RADAR);
	}

	return "UNKNOWN";

#undef TX_STATUS_FAIL
#undef TX_STATUS_POSTPONE
}
#endif /* CONFIG_IWLWIFI_DEBUG */
