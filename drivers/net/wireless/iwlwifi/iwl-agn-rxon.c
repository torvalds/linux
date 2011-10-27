/******************************************************************************
 *
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
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
 * Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include "iwl-dev.h"
#include "iwl-agn.h"
#include "iwl-sta.h"
#include "iwl-core.h"
#include "iwl-agn-calib.h"
#include "iwl-helpers.h"
#include "iwl-trans.h"

static int iwlagn_disable_bss(struct iwl_priv *priv,
			      struct iwl_rxon_context *ctx,
			      struct iwl_rxon_cmd *send)
{
	__le32 old_filter = send->filter_flags;
	int ret;

	send->filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	ret = trans_send_cmd_pdu(&priv->trans, ctx->rxon_cmd,
				CMD_SYNC, sizeof(*send), send);

	send->filter_flags = old_filter;

	if (ret)
		IWL_ERR(priv, "Error clearing ASSOC_MSK on BSS (%d)\n", ret);

	return ret;
}

static int iwlagn_disable_pan(struct iwl_priv *priv,
			      struct iwl_rxon_context *ctx,
			      struct iwl_rxon_cmd *send)
{
	struct iwl_notification_wait disable_wait;
	__le32 old_filter = send->filter_flags;
	u8 old_dev_type = send->dev_type;
	int ret;

	iwlagn_init_notification_wait(priv, &disable_wait,
				      REPLY_WIPAN_DEACTIVATION_COMPLETE,
				      NULL, NULL);

	send->filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	send->dev_type = RXON_DEV_TYPE_P2P;
	ret = trans_send_cmd_pdu(&priv->trans, ctx->rxon_cmd,
				CMD_SYNC, sizeof(*send), send);

	send->filter_flags = old_filter;
	send->dev_type = old_dev_type;

	if (ret) {
		IWL_ERR(priv, "Error disabling PAN (%d)\n", ret);
		iwlagn_remove_notification(priv, &disable_wait);
	} else {
		ret = iwlagn_wait_notification(priv, &disable_wait, HZ);
		if (ret)
			IWL_ERR(priv, "Timed out waiting for PAN disable\n");
	}

	return ret;
}

static int iwlagn_disconn_pan(struct iwl_priv *priv,
			      struct iwl_rxon_context *ctx,
			      struct iwl_rxon_cmd *send)
{
	__le32 old_filter = send->filter_flags;
	int ret;

	send->filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	ret = trans_send_cmd_pdu(&priv->trans, ctx->rxon_cmd, CMD_SYNC,
				sizeof(*send), send);

	send->filter_flags = old_filter;

	return ret;
}

static void iwlagn_update_qos(struct iwl_priv *priv,
			      struct iwl_rxon_context *ctx)
{
	int ret;

	if (!ctx->is_active)
		return;

	ctx->qos_data.def_qos_parm.qos_flags = 0;

	if (ctx->qos_data.qos_active)
		ctx->qos_data.def_qos_parm.qos_flags |=
			QOS_PARAM_FLG_UPDATE_EDCA_MSK;

	if (ctx->ht.enabled)
		ctx->qos_data.def_qos_parm.qos_flags |= QOS_PARAM_FLG_TGN_MSK;

	IWL_DEBUG_QOS(priv, "send QoS cmd with Qos active=%d FLAGS=0x%X\n",
		      ctx->qos_data.qos_active,
		      ctx->qos_data.def_qos_parm.qos_flags);

	ret = trans_send_cmd_pdu(&priv->trans, ctx->qos_cmd, CMD_SYNC,
			       sizeof(struct iwl_qosparam_cmd),
			       &ctx->qos_data.def_qos_parm);
	if (ret)
		IWL_ERR(priv, "Failed to update QoS\n");
}

static int iwlagn_update_beacon(struct iwl_priv *priv,
				struct ieee80211_vif *vif)
{
	lockdep_assert_held(&priv->mutex);

	dev_kfree_skb(priv->beacon_skb);
	priv->beacon_skb = ieee80211_beacon_get(priv->hw, vif);
	if (!priv->beacon_skb)
		return -ENOMEM;
	return iwlagn_send_beacon_cmd(priv);
}

static int iwlagn_send_rxon_assoc(struct iwl_priv *priv,
			   struct iwl_rxon_context *ctx)
{
	int ret = 0;
	struct iwl_rxon_assoc_cmd rxon_assoc;
	const struct iwl_rxon_cmd *rxon1 = &ctx->staging;
	const struct iwl_rxon_cmd *rxon2 = &ctx->active;

	if ((rxon1->flags == rxon2->flags) &&
	    (rxon1->filter_flags == rxon2->filter_flags) &&
	    (rxon1->cck_basic_rates == rxon2->cck_basic_rates) &&
	    (rxon1->ofdm_ht_single_stream_basic_rates ==
	     rxon2->ofdm_ht_single_stream_basic_rates) &&
	    (rxon1->ofdm_ht_dual_stream_basic_rates ==
	     rxon2->ofdm_ht_dual_stream_basic_rates) &&
	    (rxon1->ofdm_ht_triple_stream_basic_rates ==
	     rxon2->ofdm_ht_triple_stream_basic_rates) &&
	    (rxon1->acquisition_data == rxon2->acquisition_data) &&
	    (rxon1->rx_chain == rxon2->rx_chain) &&
	    (rxon1->ofdm_basic_rates == rxon2->ofdm_basic_rates)) {
		IWL_DEBUG_INFO(priv, "Using current RXON_ASSOC.  Not resending.\n");
		return 0;
	}

	rxon_assoc.flags = ctx->staging.flags;
	rxon_assoc.filter_flags = ctx->staging.filter_flags;
	rxon_assoc.ofdm_basic_rates = ctx->staging.ofdm_basic_rates;
	rxon_assoc.cck_basic_rates = ctx->staging.cck_basic_rates;
	rxon_assoc.reserved1 = 0;
	rxon_assoc.reserved2 = 0;
	rxon_assoc.reserved3 = 0;
	rxon_assoc.ofdm_ht_single_stream_basic_rates =
	    ctx->staging.ofdm_ht_single_stream_basic_rates;
	rxon_assoc.ofdm_ht_dual_stream_basic_rates =
	    ctx->staging.ofdm_ht_dual_stream_basic_rates;
	rxon_assoc.rx_chain_select_flags = ctx->staging.rx_chain;
	rxon_assoc.ofdm_ht_triple_stream_basic_rates =
		 ctx->staging.ofdm_ht_triple_stream_basic_rates;
	rxon_assoc.acquisition_data = ctx->staging.acquisition_data;

	ret = trans_send_cmd_pdu(&priv->trans, ctx->rxon_assoc_cmd,
				CMD_ASYNC, sizeof(rxon_assoc), &rxon_assoc);
	return ret;
}

static int iwlagn_rxon_disconn(struct iwl_priv *priv,
			       struct iwl_rxon_context *ctx)
{
	int ret;
	struct iwl_rxon_cmd *active = (void *)&ctx->active;

	if (ctx->ctxid == IWL_RXON_CTX_BSS) {
		ret = iwlagn_disable_bss(priv, ctx, &ctx->staging);
	} else {
		ret = iwlagn_disable_pan(priv, ctx, &ctx->staging);
		if (ret)
			return ret;
		if (ctx->vif) {
			ret = iwl_send_rxon_timing(priv, ctx);
			if (ret) {
				IWL_ERR(priv, "Failed to send timing (%d)!\n", ret);
				return ret;
			}
			ret = iwlagn_disconn_pan(priv, ctx, &ctx->staging);
		}
	}
	if (ret)
		return ret;

	/*
	 * Un-assoc RXON clears the station table and WEP
	 * keys, so we have to restore those afterwards.
	 */
	iwl_clear_ucode_stations(priv, ctx);
	/* update -- might need P2P now */
	iwl_update_bcast_station(priv, ctx);
	iwl_restore_stations(priv, ctx);
	ret = iwl_restore_default_wep_keys(priv, ctx);
	if (ret) {
		IWL_ERR(priv, "Failed to restore WEP keys (%d)\n", ret);
		return ret;
	}

	memcpy(active, &ctx->staging, sizeof(*active));
	return 0;
}

static int iwlagn_rxon_connect(struct iwl_priv *priv,
			       struct iwl_rxon_context *ctx)
{
	int ret;
	struct iwl_rxon_cmd *active = (void *)&ctx->active;

	/* RXON timing must be before associated RXON */
	if (ctx->ctxid == IWL_RXON_CTX_BSS) {
		ret = iwl_send_rxon_timing(priv, ctx);
		if (ret) {
			IWL_ERR(priv, "Failed to send timing (%d)!\n", ret);
			return ret;
		}
	}
	/* QoS info may be cleared by previous un-assoc RXON */
	iwlagn_update_qos(priv, ctx);

	/*
	 * We'll run into this code path when beaconing is
	 * enabled, but then we also need to send the beacon
	 * to the device.
	 */
	if (ctx->vif && (ctx->vif->type == NL80211_IFTYPE_AP)) {
		ret = iwlagn_update_beacon(priv, ctx->vif);
		if (ret) {
			IWL_ERR(priv,
				"Error sending required beacon (%d)!\n",
				ret);
			return ret;
		}
	}

	priv->start_calib = 0;
	/*
	 * Apply the new configuration.
	 *
	 * Associated RXON doesn't clear the station table in uCode,
	 * so we don't need to restore stations etc. after this.
	 */
	ret = trans_send_cmd_pdu(&priv->trans, ctx->rxon_cmd, CMD_SYNC,
		      sizeof(struct iwl_rxon_cmd), &ctx->staging);
	if (ret) {
		IWL_ERR(priv, "Error setting new RXON (%d)\n", ret);
		return ret;
	}
	memcpy(active, &ctx->staging, sizeof(*active));

	iwl_reprogram_ap_sta(priv, ctx);

	/* IBSS beacon needs to be sent after setting assoc */
	if (ctx->vif && (ctx->vif->type == NL80211_IFTYPE_ADHOC))
		if (iwlagn_update_beacon(priv, ctx->vif))
			IWL_ERR(priv, "Error sending IBSS beacon\n");
	iwl_init_sensitivity(priv);

	/*
	 * If we issue a new RXON command which required a tune then
	 * we must send a new TXPOWER command or we won't be able to
	 * Tx any frames.
	 *
	 * It's expected we set power here if channel is changing.
	 */
	ret = iwl_set_tx_power(priv, priv->tx_power_next, true);
	if (ret) {
		IWL_ERR(priv, "Error sending TX power (%d)\n", ret);
		return ret;
	}

	if ((ctx->vif && ctx->vif->type == NL80211_IFTYPE_STATION) &&
	    priv->cfg->ht_params->smps_mode)
		ieee80211_request_smps(ctx->vif,
				       priv->cfg->ht_params->smps_mode);

	return 0;
}

int iwlagn_set_pan_params(struct iwl_priv *priv)
{
	struct iwl_wipan_params_cmd cmd;
	struct iwl_rxon_context *ctx_bss, *ctx_pan;
	int slot0 = 300, slot1 = 0;
	int ret;

	if (priv->valid_contexts == BIT(IWL_RXON_CTX_BSS))
		return 0;

	BUILD_BUG_ON(NUM_IWL_RXON_CTX != 2);

	lockdep_assert_held(&priv->mutex);

	ctx_bss = &priv->contexts[IWL_RXON_CTX_BSS];
	ctx_pan = &priv->contexts[IWL_RXON_CTX_PAN];

	/*
	 * If the PAN context is inactive, then we don't need
	 * to update the PAN parameters, the last thing we'll
	 * have done before it goes inactive is making the PAN
	 * parameters be WLAN-only.
	 */
	if (!ctx_pan->is_active)
		return 0;

	memset(&cmd, 0, sizeof(cmd));

	/* only 2 slots are currently allowed */
	cmd.num_slots = 2;

	cmd.slots[0].type = 0; /* BSS */
	cmd.slots[1].type = 1; /* PAN */

	if (priv->hw_roc_channel) {
		/* both contexts must be used for this to happen */
		slot1 = priv->hw_roc_duration;
		slot0 = IWL_MIN_SLOT_TIME;
	} else if (ctx_bss->vif && ctx_pan->vif) {
		int bcnint = ctx_pan->beacon_int;
		int dtim = ctx_pan->vif->bss_conf.dtim_period ?: 1;

		/* should be set, but seems unused?? */
		cmd.flags |= cpu_to_le16(IWL_WIPAN_PARAMS_FLG_SLOTTED_MODE);

		if (ctx_pan->vif->type == NL80211_IFTYPE_AP &&
		    bcnint &&
		    bcnint != ctx_bss->beacon_int) {
			IWL_ERR(priv,
				"beacon intervals don't match (%d, %d)\n",
				ctx_bss->beacon_int, ctx_pan->beacon_int);
		} else
			bcnint = max_t(int, bcnint,
				       ctx_bss->beacon_int);
		if (!bcnint)
			bcnint = DEFAULT_BEACON_INTERVAL;
		slot0 = bcnint / 2;
		slot1 = bcnint - slot0;

		if (test_bit(STATUS_SCAN_HW, &priv->status) ||
		    (!ctx_bss->vif->bss_conf.idle &&
		     !ctx_bss->vif->bss_conf.assoc)) {
			slot0 = dtim * bcnint * 3 - IWL_MIN_SLOT_TIME;
			slot1 = IWL_MIN_SLOT_TIME;
		} else if (!ctx_pan->vif->bss_conf.idle &&
			   !ctx_pan->vif->bss_conf.assoc) {
			slot1 = bcnint * 3 - IWL_MIN_SLOT_TIME;
			slot0 = IWL_MIN_SLOT_TIME;
		}
	} else if (ctx_pan->vif) {
		slot0 = 0;
		slot1 = max_t(int, 1, ctx_pan->vif->bss_conf.dtim_period) *
					ctx_pan->beacon_int;
		slot1 = max_t(int, DEFAULT_BEACON_INTERVAL, slot1);

		if (test_bit(STATUS_SCAN_HW, &priv->status)) {
			slot0 = slot1 * 3 - IWL_MIN_SLOT_TIME;
			slot1 = IWL_MIN_SLOT_TIME;
		}
	}

	cmd.slots[0].width = cpu_to_le16(slot0);
	cmd.slots[1].width = cpu_to_le16(slot1);

	ret = trans_send_cmd_pdu(&priv->trans, REPLY_WIPAN_PARAMS, CMD_SYNC,
			sizeof(cmd), &cmd);
	if (ret)
		IWL_ERR(priv, "Error setting PAN parameters (%d)\n", ret);

	return ret;
}

/**
 * iwlagn_commit_rxon - commit staging_rxon to hardware
 *
 * The RXON command in staging_rxon is committed to the hardware and
 * the active_rxon structure is updated with the new data.  This
 * function correctly transitions out of the RXON_ASSOC_MSK state if
 * a HW tune is required based on the RXON structure changes.
 *
 * The connect/disconnect flow should be as the following:
 *
 * 1. make sure send RXON command with association bit unset if not connect
 *	this should include the channel and the band for the candidate
 *	to be connected to
 * 2. Add Station before RXON association with the AP
 * 3. RXON_timing has to send before RXON for connection
 * 4. full RXON command - associated bit set
 * 5. use RXON_ASSOC command to update any flags changes
 */
int iwlagn_commit_rxon(struct iwl_priv *priv, struct iwl_rxon_context *ctx)
{
	/* cast away the const for active_rxon in this function */
	struct iwl_rxon_cmd *active = (void *)&ctx->active;
	bool new_assoc = !!(ctx->staging.filter_flags & RXON_FILTER_ASSOC_MSK);
	int ret;

	lockdep_assert_held(&priv->mutex);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return -EINVAL;

	if (!iwl_is_alive(priv))
		return -EBUSY;

	/* This function hardcodes a bunch of dual-mode assumptions */
	BUILD_BUG_ON(NUM_IWL_RXON_CTX != 2);

	if (!ctx->is_active)
		return 0;

	/* always get timestamp with Rx frame */
	ctx->staging.flags |= RXON_FLG_TSF2HOST_MSK;

	if (ctx->ctxid == IWL_RXON_CTX_PAN && priv->hw_roc_channel) {
		struct ieee80211_channel *chan = priv->hw_roc_channel;

		iwl_set_rxon_channel(priv, chan, ctx);
		iwl_set_flags_for_band(priv, ctx, chan->band, NULL);
		ctx->staging.filter_flags |=
			RXON_FILTER_ASSOC_MSK |
			RXON_FILTER_PROMISC_MSK |
			RXON_FILTER_CTL2HOST_MSK;
		ctx->staging.dev_type = RXON_DEV_TYPE_P2P;
		new_assoc = true;

		if (memcmp(&ctx->staging, &ctx->active,
			   sizeof(ctx->staging)) == 0)
			return 0;
	}

	/*
	 * force CTS-to-self frames protection if RTS-CTS is not preferred
	 * one aggregation protection method
	 */
	if (!(priv->cfg->ht_params &&
	      priv->cfg->ht_params->use_rts_for_aggregation))
		ctx->staging.flags |= RXON_FLG_SELF_CTS_EN;

	if ((ctx->vif && ctx->vif->bss_conf.use_short_slot) ||
	    !(ctx->staging.flags & RXON_FLG_BAND_24G_MSK))
		ctx->staging.flags |= RXON_FLG_SHORT_SLOT_MSK;
	else
		ctx->staging.flags &= ~RXON_FLG_SHORT_SLOT_MSK;

	iwl_print_rx_config_cmd(priv, ctx);
	ret = iwl_check_rxon_cmd(priv, ctx);
	if (ret) {
		IWL_ERR(priv, "Invalid RXON configuration. Not committing.\n");
		return -EINVAL;
	}

	/*
	 * receive commit_rxon request
	 * abort any previous channel switch if still in process
	 */
	if (test_bit(STATUS_CHANNEL_SWITCH_PENDING, &priv->status) &&
	    (priv->switch_channel != ctx->staging.channel)) {
		IWL_DEBUG_11H(priv, "abort channel switch on %d\n",
			      le16_to_cpu(priv->switch_channel));
		iwl_chswitch_done(priv, false);
	}

	/*
	 * If we don't need to send a full RXON, we can use
	 * iwl_rxon_assoc_cmd which is used to reconfigure filter
	 * and other flags for the current radio configuration.
	 */
	if (!iwl_full_rxon_required(priv, ctx)) {
		ret = iwlagn_send_rxon_assoc(priv, ctx);
		if (ret) {
			IWL_ERR(priv, "Error setting RXON_ASSOC (%d)\n", ret);
			return ret;
		}

		memcpy(active, &ctx->staging, sizeof(*active));
		/*
		 * We do not commit tx power settings while channel changing,
		 * do it now if after settings changed.
		 */
		iwl_set_tx_power(priv, priv->tx_power_next, false);

		/* make sure we are in the right PS state */
		iwl_power_update_mode(priv, true);

		return 0;
	}

	iwl_set_rxon_hwcrypto(priv, ctx, !iwlagn_mod_params.sw_crypto);

	IWL_DEBUG_INFO(priv,
		       "Going to commit RXON\n"
		       "  * with%s RXON_FILTER_ASSOC_MSK\n"
		       "  * channel = %d\n"
		       "  * bssid = %pM\n",
		       (new_assoc ? "" : "out"),
		       le16_to_cpu(ctx->staging.channel),
		       ctx->staging.bssid_addr);

	/*
	 * Always clear associated first, but with the correct config.
	 * This is required as for example station addition for the
	 * AP station must be done after the BSSID is set to correctly
	 * set up filters in the device.
	 */
	ret = iwlagn_rxon_disconn(priv, ctx);
	if (ret)
		return ret;

	ret = iwlagn_set_pan_params(priv);
	if (ret)
		return ret;

	if (new_assoc)
		return iwlagn_rxon_connect(priv, ctx);

	return 0;
}

int iwlagn_mac_config(struct ieee80211_hw *hw, u32 changed)
{
	struct iwl_priv *priv = hw->priv;
	struct iwl_rxon_context *ctx;
	struct ieee80211_conf *conf = &hw->conf;
	struct ieee80211_channel *channel = conf->channel;
	const struct iwl_channel_info *ch_info;
	int ret = 0;

	IWL_DEBUG_MAC80211(priv, "changed %#x", changed);

	mutex_lock(&priv->mutex);

	if (unlikely(test_bit(STATUS_SCANNING, &priv->status))) {
		IWL_DEBUG_MAC80211(priv, "leave - scanning\n");
		goto out;
	}

	if (!iwl_is_ready(priv)) {
		IWL_DEBUG_MAC80211(priv, "leave - not ready\n");
		goto out;
	}

	if (changed & (IEEE80211_CONF_CHANGE_SMPS |
		       IEEE80211_CONF_CHANGE_CHANNEL)) {
		/* mac80211 uses static for non-HT which is what we want */
		priv->current_ht_config.smps = conf->smps_mode;

		/*
		 * Recalculate chain counts.
		 *
		 * If monitor mode is enabled then mac80211 will
		 * set up the SM PS mode to OFF if an HT channel is
		 * configured.
		 */
		for_each_context(priv, ctx)
			iwlagn_set_rxon_chain(priv, ctx);
	}

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		unsigned long flags;

		ch_info = iwl_get_channel_info(priv, channel->band,
					       channel->hw_value);
		if (!is_channel_valid(ch_info)) {
			IWL_DEBUG_MAC80211(priv, "leave - invalid channel\n");
			ret = -EINVAL;
			goto out;
		}

		spin_lock_irqsave(&priv->lock, flags);

		for_each_context(priv, ctx) {
			/* Configure HT40 channels */
			if (ctx->ht.enabled != conf_is_ht(conf))
				ctx->ht.enabled = conf_is_ht(conf);

			if (ctx->ht.enabled) {
				if (conf_is_ht40_minus(conf)) {
					ctx->ht.extension_chan_offset =
						IEEE80211_HT_PARAM_CHA_SEC_BELOW;
					ctx->ht.is_40mhz = true;
				} else if (conf_is_ht40_plus(conf)) {
					ctx->ht.extension_chan_offset =
						IEEE80211_HT_PARAM_CHA_SEC_ABOVE;
					ctx->ht.is_40mhz = true;
				} else {
					ctx->ht.extension_chan_offset =
						IEEE80211_HT_PARAM_CHA_SEC_NONE;
					ctx->ht.is_40mhz = false;
				}
			} else
				ctx->ht.is_40mhz = false;

			/*
			 * Default to no protection. Protection mode will
			 * later be set from BSS config in iwl_ht_conf
			 */
			ctx->ht.protection = IEEE80211_HT_OP_MODE_PROTECTION_NONE;

			/* if we are switching from ht to 2.4 clear flags
			 * from any ht related info since 2.4 does not
			 * support ht */
			if (le16_to_cpu(ctx->staging.channel) !=
			    channel->hw_value)
				ctx->staging.flags = 0;

			iwl_set_rxon_channel(priv, channel, ctx);
			iwl_set_rxon_ht(priv, &priv->current_ht_config);

			iwl_set_flags_for_band(priv, ctx, channel->band,
					       ctx->vif);
		}

		spin_unlock_irqrestore(&priv->lock, flags);

		iwl_update_bcast_stations(priv);

		/*
		 * The list of supported rates and rate mask can be different
		 * for each band; since the band may have changed, reset
		 * the rate mask to what mac80211 lists.
		 */
		iwl_set_rate(priv);
	}

	if (changed & (IEEE80211_CONF_CHANGE_PS |
			IEEE80211_CONF_CHANGE_IDLE)) {
		ret = iwl_power_update_mode(priv, false);
		if (ret)
			IWL_DEBUG_MAC80211(priv, "Error setting sleep level\n");
	}

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		IWL_DEBUG_MAC80211(priv, "TX Power old=%d new=%d\n",
			priv->tx_power_user_lmt, conf->power_level);

		iwl_set_tx_power(priv, conf->power_level, false);
	}

	for_each_context(priv, ctx) {
		if (!memcmp(&ctx->staging, &ctx->active, sizeof(ctx->staging)))
			continue;
		iwlagn_commit_rxon(priv, ctx);
	}
 out:
	mutex_unlock(&priv->mutex);
	return ret;
}

static void iwlagn_check_needed_chains(struct iwl_priv *priv,
				       struct iwl_rxon_context *ctx,
				       struct ieee80211_bss_conf *bss_conf)
{
	struct ieee80211_vif *vif = ctx->vif;
	struct iwl_rxon_context *tmp;
	struct ieee80211_sta *sta;
	struct iwl_ht_config *ht_conf = &priv->current_ht_config;
	struct ieee80211_sta_ht_cap *ht_cap;
	bool need_multiple;

	lockdep_assert_held(&priv->mutex);

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		rcu_read_lock();
		sta = ieee80211_find_sta(vif, bss_conf->bssid);
		if (!sta) {
			/*
			 * If at all, this can only happen through a race
			 * when the AP disconnects us while we're still
			 * setting up the connection, in that case mac80211
			 * will soon tell us about that.
			 */
			need_multiple = false;
			rcu_read_unlock();
			break;
		}

		ht_cap = &sta->ht_cap;

		need_multiple = true;

		/*
		 * If the peer advertises no support for receiving 2 and 3
		 * stream MCS rates, it can't be transmitting them either.
		 */
		if (ht_cap->mcs.rx_mask[1] == 0 &&
		    ht_cap->mcs.rx_mask[2] == 0) {
			need_multiple = false;
		} else if (!(ht_cap->mcs.tx_params &
						IEEE80211_HT_MCS_TX_DEFINED)) {
			/* If it can't TX MCS at all ... */
			need_multiple = false;
		} else if (ht_cap->mcs.tx_params &
						IEEE80211_HT_MCS_TX_RX_DIFF) {
			int maxstreams;

			/*
			 * But if it can receive them, it might still not
			 * be able to transmit them, which is what we need
			 * to check here -- so check the number of streams
			 * it advertises for TX (if different from RX).
			 */

			maxstreams = (ht_cap->mcs.tx_params &
				 IEEE80211_HT_MCS_TX_MAX_STREAMS_MASK);
			maxstreams >>=
				IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT;
			maxstreams += 1;

			if (maxstreams <= 1)
				need_multiple = false;
		}

		rcu_read_unlock();
		break;
	case NL80211_IFTYPE_ADHOC:
		/* currently */
		need_multiple = false;
		break;
	default:
		/* only AP really */
		need_multiple = true;
		break;
	}

	ctx->ht_need_multiple_chains = need_multiple;

	if (!need_multiple) {
		/* check all contexts */
		for_each_context(priv, tmp) {
			if (!tmp->vif)
				continue;
			if (tmp->ht_need_multiple_chains) {
				need_multiple = true;
				break;
			}
		}
	}

	ht_conf->single_chain_sufficient = !need_multiple;
}

static void iwlagn_chain_noise_reset(struct iwl_priv *priv)
{
	struct iwl_chain_noise_data *data = &priv->chain_noise_data;
	int ret;

	if ((data->state == IWL_CHAIN_NOISE_ALIVE) &&
	    iwl_is_any_associated(priv)) {
		struct iwl_calib_chain_noise_reset_cmd cmd;

		/* clear data for chain noise calibration algorithm */
		data->chain_noise_a = 0;
		data->chain_noise_b = 0;
		data->chain_noise_c = 0;
		data->chain_signal_a = 0;
		data->chain_signal_b = 0;
		data->chain_signal_c = 0;
		data->beacon_count = 0;

		memset(&cmd, 0, sizeof(cmd));
		iwl_set_calib_hdr(&cmd.hdr,
			priv->phy_calib_chain_noise_reset_cmd);
		ret = trans_send_cmd_pdu(&priv->trans,
					REPLY_PHY_CALIBRATION_CMD,
					CMD_SYNC, sizeof(cmd), &cmd);
		if (ret)
			IWL_ERR(priv,
				"Could not send REPLY_PHY_CALIBRATION_CMD\n");
		data->state = IWL_CHAIN_NOISE_ACCUMULATE;
		IWL_DEBUG_CALIB(priv, "Run chain_noise_calibrate\n");
	}
}

void iwlagn_bss_info_changed(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *bss_conf,
			     u32 changes)
{
	struct iwl_priv *priv = hw->priv;
	struct iwl_rxon_context *ctx = iwl_rxon_ctx_from_vif(vif);
	int ret;
	bool force = false;

	mutex_lock(&priv->mutex);

	if (unlikely(!iwl_is_ready(priv))) {
		IWL_DEBUG_MAC80211(priv, "leave - not ready\n");
		mutex_unlock(&priv->mutex);
		return;
        }

	if (unlikely(!ctx->vif)) {
		IWL_DEBUG_MAC80211(priv, "leave - vif is NULL\n");
		mutex_unlock(&priv->mutex);
		return;
	}

	if (changes & BSS_CHANGED_BEACON_INT)
		force = true;

	if (changes & BSS_CHANGED_QOS) {
		ctx->qos_data.qos_active = bss_conf->qos;
		iwlagn_update_qos(priv, ctx);
	}

	ctx->staging.assoc_id = cpu_to_le16(vif->bss_conf.aid);
	if (vif->bss_conf.use_short_preamble)
		ctx->staging.flags |= RXON_FLG_SHORT_PREAMBLE_MSK;
	else
		ctx->staging.flags &= ~RXON_FLG_SHORT_PREAMBLE_MSK;

	if (changes & BSS_CHANGED_ASSOC) {
		if (bss_conf->assoc) {
			priv->timestamp = bss_conf->timestamp;
			ctx->staging.filter_flags |= RXON_FILTER_ASSOC_MSK;
		} else {
			/*
			 * If we disassociate while there are pending
			 * frames, just wake up the queues and let the
			 * frames "escape" ... This shouldn't really
			 * be happening to start with, but we should
			 * not get stuck in this case either since it
			 * can happen if userspace gets confused.
			 */
			if (ctx->last_tx_rejected) {
				ctx->last_tx_rejected = false;
				iwl_wake_any_queue(priv, ctx);
			}
			ctx->staging.filter_flags &= ~RXON_FILTER_ASSOC_MSK;

			if (ctx->ctxid == IWL_RXON_CTX_BSS)
				priv->have_rekey_data = false;
		}

		iwlagn_bt_coex_rssi_monitor(priv);
	}

	if (ctx->ht.enabled) {
		ctx->ht.protection = bss_conf->ht_operation_mode &
					IEEE80211_HT_OP_MODE_PROTECTION;
		ctx->ht.non_gf_sta_present = !!(bss_conf->ht_operation_mode &
					IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT);
		iwlagn_check_needed_chains(priv, ctx, bss_conf);
		iwl_set_rxon_ht(priv, &priv->current_ht_config);
	}

	iwlagn_set_rxon_chain(priv, ctx);

	if (bss_conf->use_cts_prot && (priv->band != IEEE80211_BAND_5GHZ))
		ctx->staging.flags |= RXON_FLG_TGG_PROTECT_MSK;
	else
		ctx->staging.flags &= ~RXON_FLG_TGG_PROTECT_MSK;

	if (bss_conf->use_cts_prot)
		ctx->staging.flags |= RXON_FLG_SELF_CTS_EN;
	else
		ctx->staging.flags &= ~RXON_FLG_SELF_CTS_EN;

	memcpy(ctx->staging.bssid_addr, bss_conf->bssid, ETH_ALEN);

	if (vif->type == NL80211_IFTYPE_AP ||
	    vif->type == NL80211_IFTYPE_ADHOC) {
		if (vif->bss_conf.enable_beacon) {
			ctx->staging.filter_flags |= RXON_FILTER_ASSOC_MSK;
			priv->beacon_ctx = ctx;
		} else {
			ctx->staging.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
			priv->beacon_ctx = NULL;
		}
	}

	if (force || memcmp(&ctx->staging, &ctx->active, sizeof(ctx->staging)))
		iwlagn_commit_rxon(priv, ctx);

	if (changes & BSS_CHANGED_ASSOC && bss_conf->assoc) {
		/*
		 * The chain noise calibration will enable PM upon
		 * completion. If calibration has already been run
		 * then we need to enable power management here.
		 */
		if (priv->chain_noise_data.state == IWL_CHAIN_NOISE_DONE)
			iwl_power_update_mode(priv, false);

		/* Enable RX differential gain and sensitivity calibrations */
		if (!priv->disable_chain_noise_cal)
			iwlagn_chain_noise_reset(priv);
		priv->start_calib = 1;
	}

	if (changes & BSS_CHANGED_IBSS) {
		ret = iwlagn_manage_ibss_station(priv, vif,
						 bss_conf->ibss_joined);
		if (ret)
			IWL_ERR(priv, "failed to %s IBSS station %pM\n",
				bss_conf->ibss_joined ? "add" : "remove",
				bss_conf->bssid);
	}

	if (changes & BSS_CHANGED_BEACON && vif->type == NL80211_IFTYPE_ADHOC &&
	    priv->beacon_ctx) {
		if (iwlagn_update_beacon(priv, vif))
			IWL_ERR(priv, "Error sending IBSS beacon\n");
	}

	mutex_unlock(&priv->mutex);
}

void iwlagn_post_scan(struct iwl_priv *priv)
{
	struct iwl_rxon_context *ctx;

	/*
	 * We do not commit power settings while scan is pending,
	 * do it now if the settings changed.
	 */
	iwl_power_set_mode(priv, &priv->power_data.sleep_cmd_next, false);
	iwl_set_tx_power(priv, priv->tx_power_next, false);

	/*
	 * Since setting the RXON may have been deferred while
	 * performing the scan, fire one off if needed
	 */
	for_each_context(priv, ctx)
		if (memcmp(&ctx->staging, &ctx->active, sizeof(ctx->staging)))
			iwlagn_commit_rxon(priv, ctx);

	iwlagn_set_pan_params(priv);
}
