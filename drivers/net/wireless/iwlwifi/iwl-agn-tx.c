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
#include <linux/ieee80211.h>

#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-agn-hw.h"
#include "iwl-agn.h"
#include "iwl-trans.h"

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
				      struct ieee80211_hdr *hdr, u8 sta_id)
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


	tx_cmd->sta_id = sta_id;
	if (ieee80211_has_morefrags(fc))
		tx_flags |= TX_CMD_FLG_MORE_FRAG_MSK;

	if (ieee80211_is_data_qos(fc)) {
		u8 *qc = ieee80211_get_qos_ctl(hdr);
		tx_cmd->tid_tspec = qc[0] & 0xf;
		tx_flags &= ~TX_CMD_FLG_SEQ_CTL_MSK;
	} else {
		if (info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ)
			tx_flags |= TX_CMD_FLG_SEQ_CTL_MSK;
		else
			tx_flags &= ~TX_CMD_FLG_SEQ_CTL_MSK;
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

	if (priv->shrd->wowlan) {
		rts_retry_limit = IWLAGN_LOW_RETRY_LIMIT;
		data_retry_limit = IWLAGN_LOW_RETRY_LIMIT;
	} else {
		/* Set retry limit on RTS packets */
		rts_retry_limit = IWLAGN_RTS_DFAULT_RETRY_LIMIT;

		/* Set retry limit on DATA packets and Probe Responses*/
		if (ieee80211_is_probe_resp(fc)) {
			data_retry_limit = IWLAGN_MGMT_DFAULT_RETRY_LIMIT;
			rts_retry_limit =
				min(data_retry_limit, rts_retry_limit);
		} else if (ieee80211_is_back_req(fc))
			data_retry_limit = IWLAGN_BAR_DFAULT_RETRY_LIMIT;
		else
			data_retry_limit = IWLAGN_DEFAULT_TX_RETRY;
	}

	tx_cmd->data_retry_limit = data_retry_limit;
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
				first_antenna(hw_params(priv).valid_tx_ant));
	} else
		priv->mgmt_tx_ant = iwl_toggle_tx_ant(priv, priv->mgmt_tx_ant,
						hw_params(priv).valid_tx_ant);
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
	struct iwl_device_cmd *dev_cmd = NULL;
	struct iwl_tx_cmd *tx_cmd;

	__le16 fc;
	u8 hdr_len;
	u16 len;
	u8 sta_id;
	unsigned long flags;
	bool is_agg = false;

	if (info->control.vif)
		ctx = iwl_rxon_ctx_from_vif(info->control.vif);

	spin_lock_irqsave(&priv->shrd->lock, flags);
	if (iwl_is_rfkill(priv->shrd)) {
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
	    (info->flags & IEEE80211_TX_CTL_POLL_RESPONSE)) {
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

	if (info->flags & IEEE80211_TX_CTL_AMPDU)
		is_agg = true;

	/* irqs already disabled/saved above when locking priv->shrd->lock */
	spin_lock(&priv->shrd->sta_lock);

	dev_cmd = kmem_cache_alloc(priv->tx_cmd_pool, GFP_ATOMIC);

	if (unlikely(!dev_cmd))
		goto drop_unlock_sta;

	memset(dev_cmd, 0, sizeof(*dev_cmd));
	tx_cmd = (struct iwl_tx_cmd *) dev_cmd->payload;

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

	memset(&info->status, 0, sizeof(info->status));

	info->driver_data[0] = ctx;
	info->driver_data[1] = dev_cmd;

	if (iwl_trans_tx(trans(priv), skb, dev_cmd, ctx->ctxid, sta_id))
		goto drop_unlock_sta;

	spin_unlock(&priv->shrd->sta_lock);
	spin_unlock_irqrestore(&priv->shrd->lock, flags);

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
	if (dev_cmd)
		kmem_cache_free(priv->tx_cmd_pool, dev_cmd);
	spin_unlock(&priv->shrd->sta_lock);
drop_unlock_priv:
	spin_unlock_irqrestore(&priv->shrd->lock, flags);
	return -1;
}

int iwlagn_tx_agg_start(struct iwl_priv *priv, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta, u16 tid, u16 *ssn)
{
	struct iwl_vif_priv *vif_priv = (void *)vif->drv_priv;
	int sta_id;
	int ret;

	IWL_DEBUG_HT(priv, "TX AGG request on ra = %pM tid = %d\n",
		     sta->addr, tid);

	sta_id = iwl_sta_id(sta);
	if (sta_id == IWL_INVALID_STATION) {
		IWL_ERR(priv, "Start AGG on invalid station\n");
		return -ENXIO;
	}
	if (unlikely(tid >= IWL_MAX_TID_COUNT))
		return -EINVAL;

	if (priv->shrd->tid_data[sta_id][tid].agg.state != IWL_AGG_OFF) {
		IWL_ERR(priv, "Start AGG when state is not IWL_AGG_OFF !\n");
		return -ENXIO;
	}

	ret = iwl_sta_tx_modify_enable_tid(priv, sta_id, tid);
	if (ret)
		return ret;

	ret = iwl_trans_tx_agg_alloc(trans(priv), vif_priv->ctx->ctxid, sta_id,
				     tid, ssn);

	return ret;
}

int iwlagn_tx_agg_stop(struct iwl_priv *priv, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, u16 tid)
{
	int sta_id;
	struct iwl_vif_priv *vif_priv = (void *)vif->drv_priv;

	sta_id = iwl_sta_id(sta);

	if (sta_id == IWL_INVALID_STATION) {
		IWL_ERR(priv, "Invalid station for AGG tid %d\n", tid);
		return -ENXIO;
	}

	return iwl_trans_tx_agg_disable(trans(priv), vif_priv->ctx->ctxid,
					sta_id, tid);
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

/**
 * translate ucode response to mac80211 tx status control values
 */
static void iwlagn_hwrate_to_tx_control(struct iwl_priv *priv, u32 rate_n_flags,
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

static void iwl_rx_reply_tx_agg(struct iwl_priv *priv,
				struct iwlagn_tx_resp *tx_resp)
{
	struct agg_tx_status *frame_status = &tx_resp->status;
	int tid = (tx_resp->ra_tid & IWLAGN_TX_RES_TID_MSK) >>
		IWLAGN_TX_RES_TID_POS;
	int sta_id = (tx_resp->ra_tid & IWLAGN_TX_RES_RA_MSK) >>
		IWLAGN_TX_RES_RA_POS;
	struct iwl_ht_agg *agg = &priv->shrd->tid_data[sta_id][tid].agg;
	u32 status = le16_to_cpu(tx_resp->status.status);
	int i;

	if (agg->wait_for_ba)
		IWL_DEBUG_TX_REPLY(priv,
			"got tx response w/o block-ack\n");

	agg->rate_n_flags = le32_to_cpu(tx_resp->rate_n_flags);
	agg->wait_for_ba = (tx_resp->frame_count > 1);

	/*
	 * If the BT kill count is non-zero, we'll get this
	 * notification again.
	 */
	if (tx_resp->bt_kill_count && tx_resp->frame_count == 1 &&
	    priv->cfg->bt_params &&
	    priv->cfg->bt_params->advanced_bt_coexist) {
		IWL_DEBUG_COEX(priv, "receive reply tx w/ bt_kill\n");
	}

	if (tx_resp->frame_count == 1)
		return;

	/* Construct bit-map of pending frames within Tx window */
	for (i = 0; i < tx_resp->frame_count; i++) {
		u16 fstatus = le16_to_cpu(frame_status[i].status);

		if (status & AGG_TX_STATUS_MSK)
			iwlagn_count_agg_tx_err_status(priv, fstatus);

		if (status & (AGG_TX_STATE_FEW_BYTES_MSK |
			      AGG_TX_STATE_ABORT_MSK))
			continue;

		IWL_DEBUG_TX_REPLY(priv, "status %s (0x%08x), "
				   "try-count (0x%08x)\n",
				   iwl_get_agg_tx_fail_reason(fstatus),
				   fstatus & AGG_TX_STATUS_MSK,
				   fstatus & AGG_TX_TRY_MSK);
	}
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

static void iwlagn_set_tx_status(struct iwl_priv *priv,
				 struct ieee80211_tx_info *info,
				 struct iwlagn_tx_resp *tx_resp,
				 bool is_agg)
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
}

static void iwl_check_abort_status(struct iwl_priv *priv,
			    u8 frame_count, u32 status)
{
	if (frame_count == 1 && status == TX_STATUS_FAIL_RFKILL_FLUSH) {
		IWL_ERR(priv, "Tx flush command to flush out all frames\n");
		if (!test_bit(STATUS_EXIT_PENDING, &priv->shrd->status))
			queue_work(priv->shrd->workqueue, &priv->tx_flush);
	}
}

int iwlagn_rx_reply_tx(struct iwl_priv *priv, struct iwl_rx_mem_buffer *rxb,
			       struct iwl_device_cmd *cmd)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	u16 sequence = le16_to_cpu(pkt->hdr.sequence);
	int txq_id = SEQ_TO_QUEUE(sequence);
	int cmd_index __maybe_unused = SEQ_TO_INDEX(sequence);
	struct iwlagn_tx_resp *tx_resp = (void *)&pkt->u.raw[0];
	struct ieee80211_hdr *hdr;
	u32 status = le16_to_cpu(tx_resp->status.status);
	u32 ssn = iwlagn_get_scd_ssn(tx_resp);
	int tid;
	int sta_id;
	int freed;
	struct ieee80211_tx_info *info;
	unsigned long flags;
	struct sk_buff_head skbs;
	struct sk_buff *skb;
	struct iwl_rxon_context *ctx;
	bool is_agg = (txq_id >= IWLAGN_FIRST_AMPDU_QUEUE);

	tid = (tx_resp->ra_tid & IWLAGN_TX_RES_TID_MSK) >>
		IWLAGN_TX_RES_TID_POS;
	sta_id = (tx_resp->ra_tid & IWLAGN_TX_RES_RA_MSK) >>
		IWLAGN_TX_RES_RA_POS;

	spin_lock_irqsave(&priv->shrd->sta_lock, flags);

	if (is_agg)
		iwl_rx_reply_tx_agg(priv, tx_resp);

	if (tx_resp->frame_count == 1) {
		__skb_queue_head_init(&skbs);
		/*we can free until ssn % q.n_bd not inclusive */
		iwl_trans_reclaim(trans(priv), sta_id, tid, txq_id,
				  ssn, status, &skbs);
		freed = 0;
		while (!skb_queue_empty(&skbs)) {
			skb = __skb_dequeue(&skbs);
			hdr = (struct ieee80211_hdr *)skb->data;

			if (!ieee80211_is_data_qos(hdr->frame_control))
				priv->last_seq_ctl = tx_resp->seq_ctl;

			info = IEEE80211_SKB_CB(skb);
			ctx = info->driver_data[0];
			kmem_cache_free(priv->tx_cmd_pool,
					(info->driver_data[1]));

			memset(&info->status, 0, sizeof(info->status));

			if (status == TX_STATUS_FAIL_PASSIVE_NO_RX &&
			    iwl_is_associated_ctx(ctx) && ctx->vif &&
			    ctx->vif->type == NL80211_IFTYPE_STATION) {
				ctx->last_tx_rejected = true;
				iwl_trans_stop_queue(trans(priv), txq_id);

				IWL_DEBUG_TX_REPLY(priv,
					   "TXQ %d status %s (0x%08x) "
					   "rate_n_flags 0x%x retries %d\n",
					   txq_id,
					   iwl_get_tx_fail_reason(status),
					   status,
					   le32_to_cpu(tx_resp->rate_n_flags),
					   tx_resp->failure_frame);

				IWL_DEBUG_TX_REPLY(priv,
					   "FrameCnt = %d, idx=%d\n",
					   tx_resp->frame_count, cmd_index);
			}

			/* check if BAR is needed */
			if (is_agg && !iwl_is_tx_success(status))
				info->flags |= IEEE80211_TX_STAT_AMPDU_NO_BACK;
			iwlagn_set_tx_status(priv, IEEE80211_SKB_CB(skb),
				     tx_resp, is_agg);
			if (!is_agg)
				iwlagn_non_agg_tx_status(priv, ctx, hdr->addr1);

			ieee80211_tx_status_irqsafe(priv->hw, skb);

			freed++;
		}

		WARN_ON(!is_agg && freed != 1);
	}

	iwl_check_abort_status(priv, tx_resp->frame_count, status);
	spin_unlock_irqrestore(&priv->shrd->sta_lock, flags);
	return 0;
}

/**
 * iwlagn_rx_reply_compressed_ba - Handler for REPLY_COMPRESSED_BA
 *
 * Handles block-acknowledge notification from device, which reports success
 * of frames sent via aggregation.
 */
int iwlagn_rx_reply_compressed_ba(struct iwl_priv *priv,
				   struct iwl_rx_mem_buffer *rxb,
				   struct iwl_device_cmd *cmd)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_compressed_ba_resp *ba_resp = &pkt->u.compressed_ba;
	struct iwl_ht_agg *agg;
	struct sk_buff_head reclaimed_skbs;
	struct ieee80211_tx_info *info;
	struct ieee80211_hdr *hdr;
	struct sk_buff *skb;
	unsigned long flags;
	int sta_id;
	int tid;
	int freed;

	/* "flow" corresponds to Tx queue */
	u16 scd_flow = le16_to_cpu(ba_resp->scd_flow);

	/* "ssn" is start of block-ack Tx window, corresponds to index
	 * (in Tx queue's circular buffer) of first TFD/frame in window */
	u16 ba_resp_scd_ssn = le16_to_cpu(ba_resp->scd_ssn);

	if (scd_flow >= hw_params(priv).max_txq_num) {
		IWL_ERR(priv,
			"BUG_ON scd_flow is bigger than number of queues\n");
		return 0;
	}

	sta_id = ba_resp->sta_id;
	tid = ba_resp->tid;
	agg = &priv->shrd->tid_data[sta_id][tid].agg;

	spin_lock_irqsave(&priv->shrd->sta_lock, flags);

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
		spin_unlock_irqrestore(&priv->shrd->sta_lock, flags);
		return 0;
	}

	if (unlikely(!agg->wait_for_ba)) {
		if (unlikely(ba_resp->bitmap))
			IWL_ERR(priv, "Received BA when not expected\n");
		spin_unlock_irqrestore(&priv->shrd->sta_lock, flags);
		return 0;
	}

	IWL_DEBUG_TX_REPLY(priv, "REPLY_COMPRESSED_BA [%d] Received from %pM, "
			   "sta_id = %d\n",
			   agg->wait_for_ba,
			   (u8 *) &ba_resp->sta_addr_lo32,
			   ba_resp->sta_id);
	IWL_DEBUG_TX_REPLY(priv, "TID = %d, SeqCtl = %d, bitmap = 0x%llx, "
			   "scd_flow = %d, scd_ssn = %d\n",
			   ba_resp->tid,
			   ba_resp->seq_ctl,
			   (unsigned long long)le64_to_cpu(ba_resp->bitmap),
			   ba_resp->scd_flow,
			   ba_resp->scd_ssn);

	/* Mark that the expected block-ack response arrived */
	agg->wait_for_ba = false;

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

	__skb_queue_head_init(&reclaimed_skbs);

	/* Release all TFDs before the SSN, i.e. all TFDs in front of
	 * block-ack window (we assume that they've been successfully
	 * transmitted ... if not, it's too late anyway). */
	iwl_trans_reclaim(trans(priv), sta_id, tid, scd_flow, ba_resp_scd_ssn,
			  0, &reclaimed_skbs);
	freed = 0;
	while (!skb_queue_empty(&reclaimed_skbs)) {

		skb = __skb_dequeue(&reclaimed_skbs);
		hdr = (struct ieee80211_hdr *)skb->data;

		if (ieee80211_is_data_qos(hdr->frame_control))
			freed++;
		else
			WARN_ON_ONCE(1);

		info = IEEE80211_SKB_CB(skb);
		kmem_cache_free(priv->tx_cmd_pool, (info->driver_data[1]));

		if (freed == 1) {
			/* this is the first skb we deliver in this batch */
			/* put the rate scaling data there */
			info = IEEE80211_SKB_CB(skb);
			memset(&info->status, 0, sizeof(info->status));
			info->flags |= IEEE80211_TX_STAT_ACK;
			info->flags |= IEEE80211_TX_STAT_AMPDU;
			info->status.ampdu_ack_len = ba_resp->txed_2_done;
			info->status.ampdu_len = ba_resp->txed;
			iwlagn_hwrate_to_tx_control(priv, agg->rate_n_flags,
						    info);
		}

		ieee80211_tx_status_irqsafe(priv->hw, skb);
	}

	spin_unlock_irqrestore(&priv->shrd->sta_lock, flags);
	return 0;
}
