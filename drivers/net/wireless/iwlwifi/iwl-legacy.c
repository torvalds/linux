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
 *****************************************************************************/

#include <linux/kernel.h>
#include <net/mac80211.h>

#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-helpers.h"
#include "iwl-legacy.h"

static void iwl_update_qos(struct iwl_priv *priv, struct iwl_rxon_context *ctx)
{
	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

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

	iwl_send_cmd_pdu_async(priv, ctx->qos_cmd,
			       sizeof(struct iwl_qosparam_cmd),
			       &ctx->qos_data.def_qos_parm, NULL);
}

/**
 * iwl_legacy_mac_config - mac80211 config callback
 */
int iwl_legacy_mac_config(struct ieee80211_hw *hw, u32 changed)
{
	struct iwl_priv *priv = hw->priv;
	const struct iwl_channel_info *ch_info;
	struct ieee80211_conf *conf = &hw->conf;
	struct ieee80211_channel *channel = conf->channel;
	struct iwl_ht_config *ht_conf = &priv->current_ht_config;
	struct iwl_rxon_context *ctx;
	unsigned long flags = 0;
	int ret = 0;
	u16 ch;
	int scan_active = 0;
	bool ht_changed[NUM_IWL_RXON_CTX] = {};

	if (WARN_ON(!priv->cfg->ops->legacy))
		return -EOPNOTSUPP;

	mutex_lock(&priv->mutex);

	IWL_DEBUG_MAC80211(priv, "enter to channel %d changed 0x%X\n",
					channel->hw_value, changed);

	if (unlikely(!priv->cfg->mod_params->disable_hw_scan &&
			test_bit(STATUS_SCANNING, &priv->status))) {
		scan_active = 1;
		IWL_DEBUG_MAC80211(priv, "leave - scanning\n");
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
		if (priv->cfg->ops->hcmd->set_rxon_chain)
			for_each_context(priv, ctx)
				priv->cfg->ops->hcmd->set_rxon_chain(priv, ctx);
	}

	/* during scanning mac80211 will delay channel setting until
	 * scan finish with changed = 0
	 */
	if (!changed || (changed & IEEE80211_CONF_CHANGE_CHANNEL)) {
		if (scan_active)
			goto set_ch_out;

		ch = channel->hw_value;
		ch_info = iwl_get_channel_info(priv, channel->band, ch);
		if (!is_channel_valid(ch_info)) {
			IWL_DEBUG_MAC80211(priv, "leave - invalid channel\n");
			ret = -EINVAL;
			goto set_ch_out;
		}

		spin_lock_irqsave(&priv->lock, flags);

		for_each_context(priv, ctx) {
			/* Configure HT40 channels */
			if (ctx->ht.enabled != conf_is_ht(conf)) {
				ctx->ht.enabled = conf_is_ht(conf);
				ht_changed[ctx->ctxid] = true;
			}
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
			if ((le16_to_cpu(ctx->staging.channel) != ch))
				ctx->staging.flags = 0;

			iwl_set_rxon_channel(priv, channel, ctx);
			iwl_set_rxon_ht(priv, ht_conf);

			iwl_set_flags_for_band(priv, ctx, channel->band,
					       ctx->vif);
		}

		spin_unlock_irqrestore(&priv->lock, flags);

		if (priv->cfg->ops->legacy->update_bcast_stations)
			ret = priv->cfg->ops->legacy->update_bcast_stations(priv);

 set_ch_out:
		/* The list of supported rates and rate mask can be different
		 * for each band; since the band may have changed, reset
		 * the rate mask to what mac80211 lists */
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

	if (!iwl_is_ready(priv)) {
		IWL_DEBUG_MAC80211(priv, "leave - not ready\n");
		goto out;
	}

	if (scan_active)
		goto out;

	for_each_context(priv, ctx) {
		if (memcmp(&ctx->active, &ctx->staging, sizeof(ctx->staging)))
			iwlcore_commit_rxon(priv, ctx);
		else
			IWL_DEBUG_INFO(priv,
				"Not re-sending same RXON configuration.\n");
		if (ht_changed[ctx->ctxid])
			iwl_update_qos(priv, ctx);
	}

out:
	IWL_DEBUG_MAC80211(priv, "leave\n");
	mutex_unlock(&priv->mutex);
	return ret;
}
EXPORT_SYMBOL(iwl_legacy_mac_config);

void iwl_legacy_mac_reset_tsf(struct ieee80211_hw *hw)
{
	struct iwl_priv *priv = hw->priv;
	unsigned long flags;
	/* IBSS can only be the IWL_RXON_CTX_BSS context */
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];

	if (WARN_ON(!priv->cfg->ops->legacy))
		return;

	mutex_lock(&priv->mutex);
	IWL_DEBUG_MAC80211(priv, "enter\n");

	spin_lock_irqsave(&priv->lock, flags);
	memset(&priv->current_ht_config, 0, sizeof(struct iwl_ht_config));
	spin_unlock_irqrestore(&priv->lock, flags);

	spin_lock_irqsave(&priv->lock, flags);

	/* new association get rid of ibss beacon skb */
	if (priv->beacon_skb)
		dev_kfree_skb(priv->beacon_skb);

	priv->beacon_skb = NULL;

	priv->timestamp = 0;

	spin_unlock_irqrestore(&priv->lock, flags);

	iwl_scan_cancel_timeout(priv, 100);
	if (!iwl_is_ready_rf(priv)) {
		IWL_DEBUG_MAC80211(priv, "leave - not ready\n");
		mutex_unlock(&priv->mutex);
		return;
	}

	/* we are restarting association process
	 * clear RXON_FILTER_ASSOC_MSK bit
	 */
	ctx->staging.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	iwlcore_commit_rxon(priv, ctx);

	iwl_set_rate(priv);

	mutex_unlock(&priv->mutex);

	IWL_DEBUG_MAC80211(priv, "leave\n");
}
EXPORT_SYMBOL(iwl_legacy_mac_reset_tsf);

static void iwl_ht_conf(struct iwl_priv *priv,
			struct ieee80211_vif *vif)
{
	struct iwl_ht_config *ht_conf = &priv->current_ht_config;
	struct ieee80211_sta *sta;
	struct ieee80211_bss_conf *bss_conf = &vif->bss_conf;
	struct iwl_rxon_context *ctx = iwl_rxon_ctx_from_vif(vif);

	IWL_DEBUG_ASSOC(priv, "enter:\n");

	if (!ctx->ht.enabled)
		return;

	ctx->ht.protection =
		bss_conf->ht_operation_mode & IEEE80211_HT_OP_MODE_PROTECTION;
	ctx->ht.non_gf_sta_present =
		!!(bss_conf->ht_operation_mode & IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT);

	ht_conf->single_chain_sufficient = false;

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		rcu_read_lock();
		sta = ieee80211_find_sta(vif, bss_conf->bssid);
		if (sta) {
			struct ieee80211_sta_ht_cap *ht_cap = &sta->ht_cap;
			int maxstreams;

			maxstreams = (ht_cap->mcs.tx_params &
				      IEEE80211_HT_MCS_TX_MAX_STREAMS_MASK)
					>> IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT;
			maxstreams += 1;

			if ((ht_cap->mcs.rx_mask[1] == 0) &&
			    (ht_cap->mcs.rx_mask[2] == 0))
				ht_conf->single_chain_sufficient = true;
			if (maxstreams <= 1)
				ht_conf->single_chain_sufficient = true;
		} else {
			/*
			 * If at all, this can only happen through a race
			 * when the AP disconnects us while we're still
			 * setting up the connection, in that case mac80211
			 * will soon tell us about that.
			 */
			ht_conf->single_chain_sufficient = true;
		}
		rcu_read_unlock();
		break;
	case NL80211_IFTYPE_ADHOC:
		ht_conf->single_chain_sufficient = true;
		break;
	default:
		break;
	}

	IWL_DEBUG_ASSOC(priv, "leave\n");
}

static inline void iwl_set_no_assoc(struct iwl_priv *priv,
				    struct ieee80211_vif *vif)
{
	struct iwl_rxon_context *ctx = iwl_rxon_ctx_from_vif(vif);

	iwl_led_disassociate(priv);
	/*
	 * inform the ucode that there is no longer an
	 * association and that no more packets should be
	 * sent
	 */
	ctx->staging.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	ctx->staging.assoc_id = 0;
	iwlcore_commit_rxon(priv, ctx);
}

static void iwlcore_beacon_update(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif)
{
	struct iwl_priv *priv = hw->priv;
	unsigned long flags;
	__le64 timestamp;
	struct sk_buff *skb = ieee80211_beacon_get(hw, vif);

	if (!skb)
		return;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	lockdep_assert_held(&priv->mutex);

	if (!priv->beacon_ctx) {
		IWL_ERR(priv, "update beacon but no beacon context!\n");
		dev_kfree_skb(skb);
		return;
	}

	spin_lock_irqsave(&priv->lock, flags);

	if (priv->beacon_skb)
		dev_kfree_skb(priv->beacon_skb);

	priv->beacon_skb = skb;

	timestamp = ((struct ieee80211_mgmt *)skb->data)->u.beacon.timestamp;
	priv->timestamp = le64_to_cpu(timestamp);

	IWL_DEBUG_MAC80211(priv, "leave\n");
	spin_unlock_irqrestore(&priv->lock, flags);

	if (!iwl_is_ready_rf(priv)) {
		IWL_DEBUG_MAC80211(priv, "leave - RF not ready\n");
		return;
	}

	priv->cfg->ops->legacy->post_associate(priv);
}

void iwl_legacy_mac_bss_info_changed(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *bss_conf,
				     u32 changes)
{
	struct iwl_priv *priv = hw->priv;
	struct iwl_rxon_context *ctx = iwl_rxon_ctx_from_vif(vif);
	int ret;

	if (WARN_ON(!priv->cfg->ops->legacy))
		return;

	IWL_DEBUG_MAC80211(priv, "changes = 0x%X\n", changes);

	if (!iwl_is_alive(priv))
		return;

	mutex_lock(&priv->mutex);

	if (changes & BSS_CHANGED_QOS) {
		unsigned long flags;

		spin_lock_irqsave(&priv->lock, flags);
		ctx->qos_data.qos_active = bss_conf->qos;
		iwl_update_qos(priv, ctx);
		spin_unlock_irqrestore(&priv->lock, flags);
	}

	if (changes & BSS_CHANGED_BEACON_ENABLED) {
		/*
		 * the add_interface code must make sure we only ever
		 * have a single interface that could be beaconing at
		 * any time.
		 */
		if (vif->bss_conf.enable_beacon)
			priv->beacon_ctx = ctx;
		else
			priv->beacon_ctx = NULL;
	}

	if (changes & BSS_CHANGED_BEACON && vif->type == NL80211_IFTYPE_AP) {
		dev_kfree_skb(priv->beacon_skb);
		priv->beacon_skb = ieee80211_beacon_get(hw, vif);
	}

	if (changes & BSS_CHANGED_BEACON_INT && vif->type == NL80211_IFTYPE_AP)
		iwl_send_rxon_timing(priv, ctx);

	if (changes & BSS_CHANGED_BSSID) {
		IWL_DEBUG_MAC80211(priv, "BSSID %pM\n", bss_conf->bssid);

		/*
		 * If there is currently a HW scan going on in the
		 * background then we need to cancel it else the RXON
		 * below/in post_associate will fail.
		 */
		if (iwl_scan_cancel_timeout(priv, 100)) {
			IWL_WARN(priv, "Aborted scan still in progress after 100ms\n");
			IWL_DEBUG_MAC80211(priv, "leaving - scan abort failed.\n");
			mutex_unlock(&priv->mutex);
			return;
		}

		/* mac80211 only sets assoc when in STATION mode */
		if (vif->type == NL80211_IFTYPE_ADHOC || bss_conf->assoc) {
			memcpy(ctx->staging.bssid_addr,
			       bss_conf->bssid, ETH_ALEN);

			/* currently needed in a few places */
			memcpy(priv->bssid, bss_conf->bssid, ETH_ALEN);
		} else {
			ctx->staging.filter_flags &=
				~RXON_FILTER_ASSOC_MSK;
		}

	}

	/*
	 * This needs to be after setting the BSSID in case
	 * mac80211 decides to do both changes at once because
	 * it will invoke post_associate.
	 */
	if (vif->type == NL80211_IFTYPE_ADHOC && changes & BSS_CHANGED_BEACON)
		iwlcore_beacon_update(hw, vif);

	if (changes & BSS_CHANGED_ERP_PREAMBLE) {
		IWL_DEBUG_MAC80211(priv, "ERP_PREAMBLE %d\n",
				   bss_conf->use_short_preamble);
		if (bss_conf->use_short_preamble)
			ctx->staging.flags |= RXON_FLG_SHORT_PREAMBLE_MSK;
		else
			ctx->staging.flags &= ~RXON_FLG_SHORT_PREAMBLE_MSK;
	}

	if (changes & BSS_CHANGED_ERP_CTS_PROT) {
		IWL_DEBUG_MAC80211(priv, "ERP_CTS %d\n", bss_conf->use_cts_prot);
		if (bss_conf->use_cts_prot && (priv->band != IEEE80211_BAND_5GHZ))
			ctx->staging.flags |= RXON_FLG_TGG_PROTECT_MSK;
		else
			ctx->staging.flags &= ~RXON_FLG_TGG_PROTECT_MSK;
		if (bss_conf->use_cts_prot)
			ctx->staging.flags |= RXON_FLG_SELF_CTS_EN;
		else
			ctx->staging.flags &= ~RXON_FLG_SELF_CTS_EN;
	}

	if (changes & BSS_CHANGED_BASIC_RATES) {
		/* XXX use this information
		 *
		 * To do that, remove code from iwl_set_rate() and put something
		 * like this here:
		 *
		if (A-band)
			ctx->staging.ofdm_basic_rates =
				bss_conf->basic_rates;
		else
			ctx->staging.ofdm_basic_rates =
				bss_conf->basic_rates >> 4;
			ctx->staging.cck_basic_rates =
				bss_conf->basic_rates & 0xF;
		 */
	}

	if (changes & BSS_CHANGED_HT) {
		iwl_ht_conf(priv, vif);

		if (priv->cfg->ops->hcmd->set_rxon_chain)
			priv->cfg->ops->hcmd->set_rxon_chain(priv, ctx);
	}

	if (changes & BSS_CHANGED_ASSOC) {
		IWL_DEBUG_MAC80211(priv, "ASSOC %d\n", bss_conf->assoc);
		if (bss_conf->assoc) {
			priv->timestamp = bss_conf->timestamp;

			iwl_led_associate(priv);

			if (!iwl_is_rfkill(priv))
				priv->cfg->ops->legacy->post_associate(priv);
		} else
			iwl_set_no_assoc(priv, vif);
	}

	if (changes && iwl_is_associated_ctx(ctx) && bss_conf->aid) {
		IWL_DEBUG_MAC80211(priv, "Changes (%#x) while associated\n",
				   changes);
		ret = iwl_send_rxon_assoc(priv, ctx);
		if (!ret) {
			/* Sync active_rxon with latest change. */
			memcpy((void *)&ctx->active,
				&ctx->staging,
				sizeof(struct iwl_rxon_cmd));
		}
	}

	if (changes & BSS_CHANGED_BEACON_ENABLED) {
		if (vif->bss_conf.enable_beacon) {
			memcpy(ctx->staging.bssid_addr,
			       bss_conf->bssid, ETH_ALEN);
			memcpy(priv->bssid, bss_conf->bssid, ETH_ALEN);
			iwl_led_associate(priv);
			priv->cfg->ops->legacy->config_ap(priv);
		} else
			iwl_set_no_assoc(priv, vif);
	}

	if (changes & BSS_CHANGED_IBSS) {
		ret = priv->cfg->ops->legacy->manage_ibss_station(priv, vif,
							bss_conf->ibss_joined);
		if (ret)
			IWL_ERR(priv, "failed to %s IBSS station %pM\n",
				bss_conf->ibss_joined ? "add" : "remove",
				bss_conf->bssid);
	}

	mutex_unlock(&priv->mutex);

	IWL_DEBUG_MAC80211(priv, "leave\n");
}
EXPORT_SYMBOL(iwl_legacy_mac_bss_info_changed);

irqreturn_t iwl_isr_legacy(int irq, void *data)
{
	struct iwl_priv *priv = data;
	u32 inta, inta_mask;
	u32 inta_fh;
	unsigned long flags;
	if (!priv)
		return IRQ_NONE;

	spin_lock_irqsave(&priv->lock, flags);

	/* Disable (but don't clear!) interrupts here to avoid
	 *    back-to-back ISRs and sporadic interrupts from our NIC.
	 * If we have something to service, the tasklet will re-enable ints.
	 * If we *don't* have something, we'll re-enable before leaving here. */
	inta_mask = iwl_read32(priv, CSR_INT_MASK);  /* just for debug */
	iwl_write32(priv, CSR_INT_MASK, 0x00000000);

	/* Discover which interrupts are active/pending */
	inta = iwl_read32(priv, CSR_INT);
	inta_fh = iwl_read32(priv, CSR_FH_INT_STATUS);

	/* Ignore interrupt if there's nothing in NIC to service.
	 * This may be due to IRQ shared with another device,
	 * or due to sporadic interrupts thrown from our NIC. */
	if (!inta && !inta_fh) {
		IWL_DEBUG_ISR(priv,
			"Ignore interrupt, inta == 0, inta_fh == 0\n");
		goto none;
	}

	if ((inta == 0xFFFFFFFF) || ((inta & 0xFFFFFFF0) == 0xa5a5a5a0)) {
		/* Hardware disappeared. It might have already raised
		 * an interrupt */
		IWL_WARN(priv, "HARDWARE GONE?? INTA == 0x%08x\n", inta);
		goto unplugged;
	}

	IWL_DEBUG_ISR(priv, "ISR inta 0x%08x, enabled 0x%08x, fh 0x%08x\n",
		      inta, inta_mask, inta_fh);

	inta &= ~CSR_INT_BIT_SCD;

	/* iwl_irq_tasklet() will service interrupts and re-enable them */
	if (likely(inta || inta_fh))
		tasklet_schedule(&priv->irq_tasklet);

unplugged:
	spin_unlock_irqrestore(&priv->lock, flags);
	return IRQ_HANDLED;

none:
	/* re-enable interrupts here since we don't have anything to service. */
	/* only Re-enable if disabled by irq */
	if (test_bit(STATUS_INT_ENABLED, &priv->status))
		iwl_enable_interrupts(priv);
	spin_unlock_irqrestore(&priv->lock, flags);
	return IRQ_NONE;
}
EXPORT_SYMBOL(iwl_isr_legacy);

/*
 *  iwl_legacy_tx_cmd_protection: Set rts/cts. 3945 and 4965 only share this
 *  function.
 */
void iwl_legacy_tx_cmd_protection(struct iwl_priv *priv,
			       struct ieee80211_tx_info *info,
			       __le16 fc, __le32 *tx_flags)
{
	if (info->control.rates[0].flags & IEEE80211_TX_RC_USE_RTS_CTS) {
		*tx_flags |= TX_CMD_FLG_RTS_MSK;
		*tx_flags &= ~TX_CMD_FLG_CTS_MSK;
		*tx_flags |= TX_CMD_FLG_FULL_TXOP_PROT_MSK;

		if (!ieee80211_is_mgmt(fc))
			return;

		switch (fc & cpu_to_le16(IEEE80211_FCTL_STYPE)) {
		case cpu_to_le16(IEEE80211_STYPE_AUTH):
		case cpu_to_le16(IEEE80211_STYPE_DEAUTH):
		case cpu_to_le16(IEEE80211_STYPE_ASSOC_REQ):
		case cpu_to_le16(IEEE80211_STYPE_REASSOC_REQ):
			*tx_flags &= ~TX_CMD_FLG_RTS_MSK;
			*tx_flags |= TX_CMD_FLG_CTS_MSK;
			break;
		}
	} else if (info->control.rates[0].flags &
		   IEEE80211_TX_RC_USE_CTS_PROTECT) {
		*tx_flags &= ~TX_CMD_FLG_RTS_MSK;
		*tx_flags |= TX_CMD_FLG_CTS_MSK;
		*tx_flags |= TX_CMD_FLG_FULL_TXOP_PROT_MSK;
	}
}
EXPORT_SYMBOL(iwl_legacy_tx_cmd_protection);
