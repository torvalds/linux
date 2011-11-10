/******************************************************************************
 *
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
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
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>

#include <net/mac80211.h>

#include <asm/div64.h>

#include "iwl-eeprom.h"
#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-agn-calib.h"
#include "iwl-agn.h"
#include "iwl-shared.h"
#include "iwl-bus.h"
#include "iwl-trans.h"

/*****************************************************************************
 *
 * mac80211 entry point functions
 *
 *****************************************************************************/

static const struct ieee80211_iface_limit iwlagn_sta_ap_limits[] = {
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_STATION),
	},
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_AP),
	},
};

static const struct ieee80211_iface_limit iwlagn_2sta_limits[] = {
	{
		.max = 2,
		.types = BIT(NL80211_IFTYPE_STATION),
	},
};

static const struct ieee80211_iface_limit iwlagn_p2p_sta_go_limits[] = {
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_STATION),
	},
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_P2P_GO) |
			 BIT(NL80211_IFTYPE_AP),
	},
};

static const struct ieee80211_iface_limit iwlagn_p2p_2sta_limits[] = {
	{
		.max = 2,
		.types = BIT(NL80211_IFTYPE_STATION),
	},
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_P2P_CLIENT),
	},
};

static const struct ieee80211_iface_combination
iwlagn_iface_combinations_dualmode[] = {
	{ .num_different_channels = 1,
	  .max_interfaces = 2,
	  .beacon_int_infra_match = true,
	  .limits = iwlagn_sta_ap_limits,
	  .n_limits = ARRAY_SIZE(iwlagn_sta_ap_limits),
	},
	{ .num_different_channels = 1,
	  .max_interfaces = 2,
	  .limits = iwlagn_2sta_limits,
	  .n_limits = ARRAY_SIZE(iwlagn_2sta_limits),
	},
};

static const struct ieee80211_iface_combination
iwlagn_iface_combinations_p2p[] = {
	{ .num_different_channels = 1,
	  .max_interfaces = 2,
	  .beacon_int_infra_match = true,
	  .limits = iwlagn_p2p_sta_go_limits,
	  .n_limits = ARRAY_SIZE(iwlagn_p2p_sta_go_limits),
	},
	{ .num_different_channels = 1,
	  .max_interfaces = 2,
	  .limits = iwlagn_p2p_2sta_limits,
	  .n_limits = ARRAY_SIZE(iwlagn_p2p_2sta_limits),
	},
};

/*
 * Not a mac80211 entry point function, but it fits in with all the
 * other mac80211 functions grouped here.
 */
int iwlagn_mac_setup_register(struct iwl_priv *priv,
				  struct iwlagn_ucode_capabilities *capa)
{
	int ret;
	struct ieee80211_hw *hw = priv->hw;
	struct iwl_rxon_context *ctx;

	hw->rate_control_algorithm = "iwl-agn-rs";

	/* Tell mac80211 our characteristics */
	hw->flags = IEEE80211_HW_SIGNAL_DBM |
		    IEEE80211_HW_AMPDU_AGGREGATION |
		    IEEE80211_HW_NEED_DTIM_PERIOD |
		    IEEE80211_HW_SPECTRUM_MGMT |
		    IEEE80211_HW_REPORTS_TX_ACK_STATUS;

	/*
	 * Including the following line will crash some AP's.  This
	 * workaround removes the stimulus which causes the crash until
	 * the AP software can be fixed.
	hw->max_tx_aggregation_subframes = LINK_QUAL_AGG_FRAME_LIMIT_DEF;
	 */

	hw->flags |= IEEE80211_HW_SUPPORTS_PS |
		     IEEE80211_HW_SUPPORTS_DYNAMIC_PS;

	if (priv->cfg->sku & EEPROM_SKU_CAP_11N_ENABLE)
		hw->flags |= IEEE80211_HW_SUPPORTS_DYNAMIC_SMPS |
			     IEEE80211_HW_SUPPORTS_STATIC_SMPS;

	if (capa->flags & IWL_UCODE_TLV_FLAGS_MFP)
		hw->flags |= IEEE80211_HW_MFP_CAPABLE;

	hw->sta_data_size = sizeof(struct iwl_station_priv);
	hw->vif_data_size = sizeof(struct iwl_vif_priv);

	for_each_context(priv, ctx) {
		hw->wiphy->interface_modes |= ctx->interface_modes;
		hw->wiphy->interface_modes |= ctx->exclusive_interface_modes;
	}

	BUILD_BUG_ON(NUM_IWL_RXON_CTX != 2);

	if (hw->wiphy->interface_modes & BIT(NL80211_IFTYPE_P2P_CLIENT)) {
		hw->wiphy->iface_combinations = iwlagn_iface_combinations_p2p;
		hw->wiphy->n_iface_combinations =
			ARRAY_SIZE(iwlagn_iface_combinations_p2p);
	} else if (hw->wiphy->interface_modes & BIT(NL80211_IFTYPE_AP)) {
		hw->wiphy->iface_combinations =
			iwlagn_iface_combinations_dualmode;
		hw->wiphy->n_iface_combinations =
			ARRAY_SIZE(iwlagn_iface_combinations_dualmode);
	}

	hw->wiphy->max_remain_on_channel_duration = 1000;

	hw->wiphy->flags |= WIPHY_FLAG_CUSTOM_REGULATORY |
			    WIPHY_FLAG_DISABLE_BEACON_HINTS |
			    WIPHY_FLAG_IBSS_RSN;

	if (trans(priv)->ucode_wowlan.code.len &&
	    device_can_wakeup(bus(priv)->dev)) {
		hw->wiphy->wowlan.flags = WIPHY_WOWLAN_MAGIC_PKT |
					  WIPHY_WOWLAN_DISCONNECT |
					  WIPHY_WOWLAN_EAP_IDENTITY_REQ |
					  WIPHY_WOWLAN_RFKILL_RELEASE;
		if (!iwlagn_mod_params.sw_crypto)
			hw->wiphy->wowlan.flags |=
				WIPHY_WOWLAN_SUPPORTS_GTK_REKEY |
				WIPHY_WOWLAN_GTK_REKEY_FAILURE;

		hw->wiphy->wowlan.n_patterns = IWLAGN_WOWLAN_MAX_PATTERNS;
		hw->wiphy->wowlan.pattern_min_len =
					IWLAGN_WOWLAN_MIN_PATTERN_LEN;
		hw->wiphy->wowlan.pattern_max_len =
					IWLAGN_WOWLAN_MAX_PATTERN_LEN;
	}

	if (iwlagn_mod_params.power_save)
		hw->wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;
	else
		hw->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;

	hw->wiphy->max_scan_ssids = PROBE_OPTION_MAX;
	/* we create the 802.11 header and a zero-length SSID element */
	hw->wiphy->max_scan_ie_len = capa->max_probe_length - 24 - 2;

	/* Default value; 4 EDCA QOS priorities */
	hw->queues = 4;

	hw->max_listen_interval = IWL_CONN_MAX_LISTEN_INTERVAL;

	if (priv->bands[IEEE80211_BAND_2GHZ].n_channels)
		priv->hw->wiphy->bands[IEEE80211_BAND_2GHZ] =
			&priv->bands[IEEE80211_BAND_2GHZ];
	if (priv->bands[IEEE80211_BAND_5GHZ].n_channels)
		priv->hw->wiphy->bands[IEEE80211_BAND_5GHZ] =
			&priv->bands[IEEE80211_BAND_5GHZ];

	iwl_leds_init(priv);

	ret = ieee80211_register_hw(priv->hw);
	if (ret) {
		IWL_ERR(priv, "Failed to register hw (error %d)\n", ret);
		return ret;
	}
	priv->mac80211_registered = 1;

	return 0;
}

static int __iwl_up(struct iwl_priv *priv)
{
	struct iwl_rxon_context *ctx;
	int ret;

	lockdep_assert_held(&priv->shrd->mutex);

	if (test_bit(STATUS_EXIT_PENDING, &priv->shrd->status)) {
		IWL_WARN(priv, "Exit pending; will not bring the NIC up\n");
		return -EIO;
	}

	for_each_context(priv, ctx) {
		ret = iwlagn_alloc_bcast_station(priv, ctx);
		if (ret) {
			iwl_dealloc_bcast_stations(priv);
			return ret;
		}
	}

	ret = iwlagn_run_init_ucode(priv);
	if (ret) {
		IWL_ERR(priv, "Failed to run INIT ucode: %d\n", ret);
		goto error;
	}

	ret = iwlagn_load_ucode_wait_alive(priv, IWL_UCODE_REGULAR);
	if (ret) {
		IWL_ERR(priv, "Failed to start RT ucode: %d\n", ret);
		goto error;
	}

	ret = iwl_alive_start(priv);
	if (ret)
		goto error;
	return 0;

 error:
	set_bit(STATUS_EXIT_PENDING, &priv->shrd->status);
	__iwl_down(priv);
	clear_bit(STATUS_EXIT_PENDING, &priv->shrd->status);

	IWL_ERR(priv, "Unable to initialize device.\n");
	return ret;
}

static int iwlagn_mac_start(struct ieee80211_hw *hw)
{
	struct iwl_priv *priv = hw->priv;
	int ret;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	/* we should be verifying the device is ready to be opened */
	mutex_lock(&priv->shrd->mutex);
	ret = __iwl_up(priv);
	mutex_unlock(&priv->shrd->mutex);
	if (ret)
		return ret;

	IWL_DEBUG_INFO(priv, "Start UP work done.\n");

	/* Now we should be done, and the READY bit should be set. */
	if (WARN_ON(!test_bit(STATUS_READY, &priv->shrd->status)))
		ret = -EIO;

	iwlagn_led_enable(priv);

	priv->is_open = 1;
	IWL_DEBUG_MAC80211(priv, "leave\n");
	return 0;
}

static void iwlagn_mac_stop(struct ieee80211_hw *hw)
{
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	if (!priv->is_open)
		return;

	priv->is_open = 0;

	iwl_down(priv);

	flush_workqueue(priv->shrd->workqueue);

	/* User space software may expect getting rfkill changes
	 * even if interface is down */
	iwl_write32(bus(priv), CSR_INT, 0xFFFFFFFF);
	iwl_enable_rfkill_int(priv);

	IWL_DEBUG_MAC80211(priv, "leave\n");
}

static void iwlagn_mac_set_rekey_data(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct cfg80211_gtk_rekey_data *data)
{
	struct iwl_priv *priv = hw->priv;

	if (iwlagn_mod_params.sw_crypto)
		return;

	IWL_DEBUG_MAC80211(priv, "enter\n");
	mutex_lock(&priv->shrd->mutex);

	if (priv->contexts[IWL_RXON_CTX_BSS].vif != vif)
		goto out;

	memcpy(priv->kek, data->kek, NL80211_KEK_LEN);
	memcpy(priv->kck, data->kck, NL80211_KCK_LEN);
	priv->replay_ctr =
		cpu_to_le64(be64_to_cpup((__be64 *)&data->replay_ctr));
	priv->have_rekey_data = true;

 out:
	mutex_unlock(&priv->shrd->mutex);
	IWL_DEBUG_MAC80211(priv, "leave\n");
}

#ifdef CONFIG_PM_SLEEP
struct wowlan_key_data {
	struct iwl_rxon_context *ctx;
	struct iwlagn_wowlan_rsc_tsc_params_cmd *rsc_tsc;
	struct iwlagn_wowlan_tkip_params_cmd *tkip;
	const u8 *bssid;
	bool error, use_rsc_tsc, use_tkip;
};

static void iwlagn_convert_p1k(u16 *p1k, __le16 *out)
{
	int i;

	for (i = 0; i < IWLAGN_P1K_SIZE; i++)
		out[i] = cpu_to_le16(p1k[i]);
}

static void iwlagn_wowlan_program_keys(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *sta,
				       struct ieee80211_key_conf *key,
				       void *_data)
{
	struct iwl_priv *priv = hw->priv;
	struct wowlan_key_data *data = _data;
	struct iwl_rxon_context *ctx = data->ctx;
	struct aes_sc *aes_sc, *aes_tx_sc = NULL;
	struct tkip_sc *tkip_sc, *tkip_tx_sc = NULL;
	struct iwlagn_p1k_cache *rx_p1ks;
	u8 *rx_mic_key;
	struct ieee80211_key_seq seq;
	u32 cur_rx_iv32 = 0;
	u16 p1k[IWLAGN_P1K_SIZE];
	int ret, i;

	mutex_lock(&priv->shrd->mutex);

	if ((key->cipher == WLAN_CIPHER_SUITE_WEP40 ||
	     key->cipher == WLAN_CIPHER_SUITE_WEP104) &&
	     !sta && !ctx->key_mapping_keys)
		ret = iwl_set_default_wep_key(priv, ctx, key);
	else
		ret = iwl_set_dynamic_key(priv, ctx, key, sta);

	if (ret) {
		IWL_ERR(priv, "Error setting key during suspend!\n");
		data->error = true;
	}

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
		if (sta) {
			tkip_sc = data->rsc_tsc->all_tsc_rsc.tkip.unicast_rsc;
			tkip_tx_sc = &data->rsc_tsc->all_tsc_rsc.tkip.tsc;

			rx_p1ks = data->tkip->rx_uni;

			ieee80211_get_key_tx_seq(key, &seq);
			tkip_tx_sc->iv16 = cpu_to_le16(seq.tkip.iv16);
			tkip_tx_sc->iv32 = cpu_to_le32(seq.tkip.iv32);

			ieee80211_get_tkip_p1k_iv(key, seq.tkip.iv32, p1k);
			iwlagn_convert_p1k(p1k, data->tkip->tx.p1k);

			memcpy(data->tkip->mic_keys.tx,
			       &key->key[NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY],
			       IWLAGN_MIC_KEY_SIZE);

			rx_mic_key = data->tkip->mic_keys.rx_unicast;
		} else {
			tkip_sc = data->rsc_tsc->all_tsc_rsc.tkip.multicast_rsc;
			rx_p1ks = data->tkip->rx_multi;
			rx_mic_key = data->tkip->mic_keys.rx_mcast;
		}

		/*
		 * For non-QoS this relies on the fact that both the uCode and
		 * mac80211 use TID 0 (as they need to to avoid replay attacks)
		 * for checking the IV in the frames.
		 */
		for (i = 0; i < IWLAGN_NUM_RSC; i++) {
			ieee80211_get_key_rx_seq(key, i, &seq);
			tkip_sc[i].iv16 = cpu_to_le16(seq.tkip.iv16);
			tkip_sc[i].iv32 = cpu_to_le32(seq.tkip.iv32);
			/* wrapping isn't allowed, AP must rekey */
			if (seq.tkip.iv32 > cur_rx_iv32)
				cur_rx_iv32 = seq.tkip.iv32;
		}

		ieee80211_get_tkip_rx_p1k(key, data->bssid, cur_rx_iv32, p1k);
		iwlagn_convert_p1k(p1k, rx_p1ks[0].p1k);
		ieee80211_get_tkip_rx_p1k(key, data->bssid,
					  cur_rx_iv32 + 1, p1k);
		iwlagn_convert_p1k(p1k, rx_p1ks[1].p1k);

		memcpy(rx_mic_key,
		       &key->key[NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY],
		       IWLAGN_MIC_KEY_SIZE);

		data->use_tkip = true;
		data->use_rsc_tsc = true;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		if (sta) {
			u8 *pn = seq.ccmp.pn;

			aes_sc = data->rsc_tsc->all_tsc_rsc.aes.unicast_rsc;
			aes_tx_sc = &data->rsc_tsc->all_tsc_rsc.aes.tsc;

			ieee80211_get_key_tx_seq(key, &seq);
			aes_tx_sc->pn = cpu_to_le64(
					(u64)pn[5] |
					((u64)pn[4] << 8) |
					((u64)pn[3] << 16) |
					((u64)pn[2] << 24) |
					((u64)pn[1] << 32) |
					((u64)pn[0] << 40));
		} else
			aes_sc = data->rsc_tsc->all_tsc_rsc.aes.multicast_rsc;

		/*
		 * For non-QoS this relies on the fact that both the uCode and
		 * mac80211 use TID 0 for checking the IV in the frames.
		 */
		for (i = 0; i < IWLAGN_NUM_RSC; i++) {
			u8 *pn = seq.ccmp.pn;

			ieee80211_get_key_rx_seq(key, i, &seq);
			aes_sc->pn = cpu_to_le64(
					(u64)pn[5] |
					((u64)pn[4] << 8) |
					((u64)pn[3] << 16) |
					((u64)pn[2] << 24) |
					((u64)pn[1] << 32) |
					((u64)pn[0] << 40));
		}
		data->use_rsc_tsc = true;
		break;
	}

	mutex_unlock(&priv->shrd->mutex);
}

static int iwlagn_send_patterns(struct iwl_priv *priv,
				struct cfg80211_wowlan *wowlan)
{
	struct iwlagn_wowlan_patterns_cmd *pattern_cmd;
	struct iwl_host_cmd cmd = {
		.id = REPLY_WOWLAN_PATTERNS,
		.dataflags[0] = IWL_HCMD_DFL_NOCOPY,
		.flags = CMD_SYNC,
	};
	int i, err;

	if (!wowlan->n_patterns)
		return 0;

	cmd.len[0] = sizeof(*pattern_cmd) +
		wowlan->n_patterns * sizeof(struct iwlagn_wowlan_pattern);

	pattern_cmd = kmalloc(cmd.len[0], GFP_KERNEL);
	if (!pattern_cmd)
		return -ENOMEM;

	pattern_cmd->n_patterns = cpu_to_le32(wowlan->n_patterns);

	for (i = 0; i < wowlan->n_patterns; i++) {
		int mask_len = DIV_ROUND_UP(wowlan->patterns[i].pattern_len, 8);

		memcpy(&pattern_cmd->patterns[i].mask,
			wowlan->patterns[i].mask, mask_len);
		memcpy(&pattern_cmd->patterns[i].pattern,
			wowlan->patterns[i].pattern,
			wowlan->patterns[i].pattern_len);
		pattern_cmd->patterns[i].mask_size = mask_len;
		pattern_cmd->patterns[i].pattern_size =
			wowlan->patterns[i].pattern_len;
	}

	cmd.data[0] = pattern_cmd;
	err = iwl_trans_send_cmd(trans(priv), &cmd);
	kfree(pattern_cmd);
	return err;
}

static int iwlagn_mac_suspend(struct ieee80211_hw *hw,
			      struct cfg80211_wowlan *wowlan)
{
	struct iwl_priv *priv = hw->priv;
	struct iwlagn_wowlan_wakeup_filter_cmd wakeup_filter_cmd;
	struct iwl_rxon_cmd rxon;
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];
	struct iwlagn_wowlan_kek_kck_material_cmd kek_kck_cmd;
	struct iwlagn_wowlan_tkip_params_cmd tkip_cmd = {};
	struct wowlan_key_data key_data = {
		.ctx = ctx,
		.bssid = ctx->active.bssid_addr,
		.use_rsc_tsc = false,
		.tkip = &tkip_cmd,
		.use_tkip = false,
	};
	struct iwlagn_d3_config_cmd d3_cfg_cmd = {};
	int ret, i;
	u16 seq;

	if (WARN_ON(!wowlan))
		return -EINVAL;

	IWL_DEBUG_MAC80211(priv, "enter\n");
	mutex_lock(&priv->shrd->mutex);

	/* Don't attempt WoWLAN when not associated, tear down instead. */
	if (!ctx->vif || ctx->vif->type != NL80211_IFTYPE_STATION ||
	    !iwl_is_associated_ctx(ctx)) {
		ret = 1;
		goto out;
	}

	key_data.rsc_tsc = kzalloc(sizeof(*key_data.rsc_tsc), GFP_KERNEL);
	if (!key_data.rsc_tsc) {
		ret = -ENOMEM;
		goto out;
	}

	memset(&wakeup_filter_cmd, 0, sizeof(wakeup_filter_cmd));

	/*
	 * We know the last used seqno, and the uCode expects to know that
	 * one, it will increment before TX.
	 */
	seq = le16_to_cpu(priv->last_seq_ctl) & IEEE80211_SCTL_SEQ;
	wakeup_filter_cmd.non_qos_seq = cpu_to_le16(seq);

	/*
	 * For QoS counters, we store the one to use next, so subtract 0x10
	 * since the uCode will add 0x10 before using the value.
	 */
	for (i = 0; i < 8; i++) {
		seq = priv->shrd->tid_data[IWL_AP_ID][i].seq_number;
		seq -= 0x10;
		wakeup_filter_cmd.qos_seq[i] = cpu_to_le16(seq);
	}

	if (wowlan->disconnect)
		wakeup_filter_cmd.enabled |=
			cpu_to_le32(IWLAGN_WOWLAN_WAKEUP_BEACON_MISS |
				    IWLAGN_WOWLAN_WAKEUP_LINK_CHANGE);
	if (wowlan->magic_pkt)
		wakeup_filter_cmd.enabled |=
			cpu_to_le32(IWLAGN_WOWLAN_WAKEUP_MAGIC_PACKET);
	if (wowlan->gtk_rekey_failure)
		wakeup_filter_cmd.enabled |=
			cpu_to_le32(IWLAGN_WOWLAN_WAKEUP_GTK_REKEY_FAIL);
	if (wowlan->eap_identity_req)
		wakeup_filter_cmd.enabled |=
			cpu_to_le32(IWLAGN_WOWLAN_WAKEUP_EAP_IDENT_REQ);
	if (wowlan->four_way_handshake)
		wakeup_filter_cmd.enabled |=
			cpu_to_le32(IWLAGN_WOWLAN_WAKEUP_4WAY_HANDSHAKE);
	if (wowlan->n_patterns)
		wakeup_filter_cmd.enabled |=
			cpu_to_le32(IWLAGN_WOWLAN_WAKEUP_PATTERN_MATCH);

	if (wowlan->rfkill_release)
		d3_cfg_cmd.wakeup_flags |=
			cpu_to_le32(IWLAGN_D3_WAKEUP_RFKILL);

	iwl_scan_cancel_timeout(priv, 200);

	memcpy(&rxon, &ctx->active, sizeof(rxon));

	iwl_trans_stop_device(trans(priv));

	priv->shrd->wowlan = true;

	ret = iwlagn_load_ucode_wait_alive(priv, IWL_UCODE_WOWLAN);
	if (ret)
		goto error;

	/* now configure WoWLAN ucode */
	ret = iwl_alive_start(priv);
	if (ret)
		goto error;

	memcpy(&ctx->staging, &rxon, sizeof(rxon));
	ret = iwlagn_commit_rxon(priv, ctx);
	if (ret)
		goto error;

	ret = iwl_power_update_mode(priv, true);
	if (ret)
		goto error;

	if (!iwlagn_mod_params.sw_crypto) {
		/* mark all keys clear */
		priv->ucode_key_table = 0;
		ctx->key_mapping_keys = 0;

		/*
		 * This needs to be unlocked due to lock ordering
		 * constraints. Since we're in the suspend path
		 * that isn't really a problem though.
		 */
		mutex_unlock(&priv->shrd->mutex);
		ieee80211_iter_keys(priv->hw, ctx->vif,
				    iwlagn_wowlan_program_keys,
				    &key_data);
		mutex_lock(&priv->shrd->mutex);
		if (key_data.error) {
			ret = -EIO;
			goto error;
		}

		if (key_data.use_rsc_tsc) {
			struct iwl_host_cmd rsc_tsc_cmd = {
				.id = REPLY_WOWLAN_TSC_RSC_PARAMS,
				.flags = CMD_SYNC,
				.data[0] = key_data.rsc_tsc,
				.dataflags[0] = IWL_HCMD_DFL_NOCOPY,
				.len[0] = sizeof(*key_data.rsc_tsc),
			};

			ret = iwl_trans_send_cmd(trans(priv), &rsc_tsc_cmd);
			if (ret)
				goto error;
		}

		if (key_data.use_tkip) {
			ret = iwl_trans_send_cmd_pdu(trans(priv),
						 REPLY_WOWLAN_TKIP_PARAMS,
						 CMD_SYNC, sizeof(tkip_cmd),
						 &tkip_cmd);
			if (ret)
				goto error;
		}

		if (priv->have_rekey_data) {
			memset(&kek_kck_cmd, 0, sizeof(kek_kck_cmd));
			memcpy(kek_kck_cmd.kck, priv->kck, NL80211_KCK_LEN);
			kek_kck_cmd.kck_len = cpu_to_le16(NL80211_KCK_LEN);
			memcpy(kek_kck_cmd.kek, priv->kek, NL80211_KEK_LEN);
			kek_kck_cmd.kek_len = cpu_to_le16(NL80211_KEK_LEN);
			kek_kck_cmd.replay_ctr = priv->replay_ctr;

			ret = iwl_trans_send_cmd_pdu(trans(priv),
						 REPLY_WOWLAN_KEK_KCK_MATERIAL,
						 CMD_SYNC, sizeof(kek_kck_cmd),
						 &kek_kck_cmd);
			if (ret)
				goto error;
		}
	}

	ret = iwl_trans_send_cmd_pdu(trans(priv), REPLY_D3_CONFIG, CMD_SYNC,
				     sizeof(d3_cfg_cmd), &d3_cfg_cmd);
	if (ret)
		goto error;

	ret = iwl_trans_send_cmd_pdu(trans(priv), REPLY_WOWLAN_WAKEUP_FILTER,
				 CMD_SYNC, sizeof(wakeup_filter_cmd),
				 &wakeup_filter_cmd);
	if (ret)
		goto error;

	ret = iwlagn_send_patterns(priv, wowlan);
	if (ret)
		goto error;

	device_set_wakeup_enable(bus(priv)->dev, true);

	/* Now let the ucode operate on its own */
	iwl_write32(bus(priv), CSR_UCODE_DRV_GP1_SET,
			  CSR_UCODE_DRV_GP1_BIT_D3_CFG_COMPLETE);

	goto out;

 error:
	priv->shrd->wowlan = false;
	iwlagn_prepare_restart(priv);
	ieee80211_restart_hw(priv->hw);
 out:
	mutex_unlock(&priv->shrd->mutex);
	kfree(key_data.rsc_tsc);
	IWL_DEBUG_MAC80211(priv, "leave\n");

	return ret;
}

static int iwlagn_mac_resume(struct ieee80211_hw *hw)
{
	struct iwl_priv *priv = hw->priv;
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];
	struct ieee80211_vif *vif;
	unsigned long flags;
	u32 base, status = 0xffffffff;
	int ret = -EIO;

	IWL_DEBUG_MAC80211(priv, "enter\n");
	mutex_lock(&priv->shrd->mutex);

	iwl_write32(bus(priv), CSR_UCODE_DRV_GP1_CLR,
			  CSR_UCODE_DRV_GP1_BIT_D3_CFG_COMPLETE);

	base = priv->device_pointers.error_event_table;
	if (iwlagn_hw_valid_rtc_data_addr(base)) {
		spin_lock_irqsave(&bus(priv)->reg_lock, flags);
		ret = iwl_grab_nic_access_silent(bus(priv));
		if (ret == 0) {
			iwl_write32(bus(priv), HBUS_TARG_MEM_RADDR, base);
			status = iwl_read32(bus(priv), HBUS_TARG_MEM_RDAT);
			iwl_release_nic_access(bus(priv));
		}
		spin_unlock_irqrestore(&bus(priv)->reg_lock, flags);

#ifdef CONFIG_IWLWIFI_DEBUGFS
		if (ret == 0) {
			struct iwl_trans *trans = trans(priv);
			if (!priv->wowlan_sram)
				priv->wowlan_sram =
					kzalloc(trans->ucode_wowlan.data.len,
						GFP_KERNEL);

			if (priv->wowlan_sram)
				_iwl_read_targ_mem_words(
					bus(priv), 0x800000, priv->wowlan_sram,
					trans->ucode_wowlan.data.len / 4);
		}
#endif
	}

	/* we'll clear ctx->vif during iwlagn_prepare_restart() */
	vif = ctx->vif;

	priv->shrd->wowlan = false;

	device_set_wakeup_enable(bus(priv)->dev, false);

	iwlagn_prepare_restart(priv);

	memset((void *)&ctx->active, 0, sizeof(ctx->active));
	iwl_connection_init_rx_config(priv, ctx);
	iwlagn_set_rxon_chain(priv, ctx);

	mutex_unlock(&priv->shrd->mutex);
	IWL_DEBUG_MAC80211(priv, "leave\n");

	ieee80211_resume_disconnect(vif);

	return 1;
}

#endif

static void iwlagn_mac_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MACDUMP(priv, "enter\n");

	IWL_DEBUG_TX(priv, "dev->xmit(%d bytes) at rate 0x%02x\n", skb->len,
		     ieee80211_get_tx_rate(hw, IEEE80211_SKB_CB(skb))->bitrate);

	if (iwlagn_tx_skb(priv, skb))
		dev_kfree_skb_any(skb);

	IWL_DEBUG_MACDUMP(priv, "leave\n");
}

static void iwlagn_mac_update_tkip_key(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_key_conf *keyconf,
				       struct ieee80211_sta *sta,
				       u32 iv32, u16 *phase1key)
{
	struct iwl_priv *priv = hw->priv;

	iwl_update_tkip_key(priv, vif, keyconf, sta, iv32, phase1key);
}

static int iwlagn_mac_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			      struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta,
			      struct ieee80211_key_conf *key)
{
	struct iwl_priv *priv = hw->priv;
	struct iwl_vif_priv *vif_priv = (void *)vif->drv_priv;
	struct iwl_rxon_context *ctx = vif_priv->ctx;
	int ret;
	bool is_default_wep_key = false;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	if (iwlagn_mod_params.sw_crypto) {
		IWL_DEBUG_MAC80211(priv, "leave - hwcrypto disabled\n");
		return -EOPNOTSUPP;
	}

	/*
	 * We could program these keys into the hardware as well, but we
	 * don't expect much multicast traffic in IBSS and having keys
	 * for more stations is probably more useful.
	 *
	 * Mark key TX-only and return 0.
	 */
	if (vif->type == NL80211_IFTYPE_ADHOC &&
	    !(key->flags & IEEE80211_KEY_FLAG_PAIRWISE)) {
		key->hw_key_idx = WEP_INVALID_OFFSET;
		return 0;
	}

	/* If they key was TX-only, accept deletion */
	if (cmd == DISABLE_KEY && key->hw_key_idx == WEP_INVALID_OFFSET)
		return 0;

	mutex_lock(&priv->shrd->mutex);
	iwl_scan_cancel_timeout(priv, 100);

	BUILD_BUG_ON(WEP_INVALID_OFFSET == IWLAGN_HW_KEY_DEFAULT);

	/*
	 * If we are getting WEP group key and we didn't receive any key mapping
	 * so far, we are in legacy wep mode (group key only), otherwise we are
	 * in 1X mode.
	 * In legacy wep mode, we use another host command to the uCode.
	 */
	if ((key->cipher == WLAN_CIPHER_SUITE_WEP40 ||
	     key->cipher == WLAN_CIPHER_SUITE_WEP104) && !sta) {
		if (cmd == SET_KEY)
			is_default_wep_key = !ctx->key_mapping_keys;
		else
			is_default_wep_key =
				key->hw_key_idx == IWLAGN_HW_KEY_DEFAULT;
	}


	switch (cmd) {
	case SET_KEY:
		if (is_default_wep_key) {
			ret = iwl_set_default_wep_key(priv, vif_priv->ctx, key);
			break;
		}
		ret = iwl_set_dynamic_key(priv, vif_priv->ctx, key, sta);
		if (ret) {
			/*
			 * can't add key for RX, but we don't need it
			 * in the device for TX so still return 0
			 */
			ret = 0;
			key->hw_key_idx = WEP_INVALID_OFFSET;
		}

		IWL_DEBUG_MAC80211(priv, "enable hwcrypto key\n");
		break;
	case DISABLE_KEY:
		if (is_default_wep_key)
			ret = iwl_remove_default_wep_key(priv, ctx, key);
		else
			ret = iwl_remove_dynamic_key(priv, ctx, key, sta);

		IWL_DEBUG_MAC80211(priv, "disable hwcrypto key\n");
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&priv->shrd->mutex);
	IWL_DEBUG_MAC80211(priv, "leave\n");

	return ret;
}

static int iwlagn_mac_ampdu_action(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   enum ieee80211_ampdu_mlme_action action,
				   struct ieee80211_sta *sta, u16 tid, u16 *ssn,
				   u8 buf_size)
{
	struct iwl_priv *priv = hw->priv;
	int ret = -EINVAL;
	struct iwl_station_priv *sta_priv = (void *) sta->drv_priv;
	struct iwl_rxon_context *ctx =  iwl_rxon_ctx_from_vif(vif);

	IWL_DEBUG_HT(priv, "A-MPDU action on addr %pM tid %d\n",
		     sta->addr, tid);

	if (!(priv->cfg->sku & EEPROM_SKU_CAP_11N_ENABLE))
		return -EACCES;

	IWL_DEBUG_MAC80211(priv, "enter\n");
	mutex_lock(&priv->shrd->mutex);

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		IWL_DEBUG_HT(priv, "start Rx\n");
		ret = iwl_sta_rx_agg_start(priv, sta, tid, *ssn);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		IWL_DEBUG_HT(priv, "stop Rx\n");
		ret = iwl_sta_rx_agg_stop(priv, sta, tid);
		if (test_bit(STATUS_EXIT_PENDING, &priv->shrd->status))
			ret = 0;
		break;
	case IEEE80211_AMPDU_TX_START:
		IWL_DEBUG_HT(priv, "start Tx\n");
		ret = iwlagn_tx_agg_start(priv, vif, sta, tid, ssn);
		break;
	case IEEE80211_AMPDU_TX_STOP:
		IWL_DEBUG_HT(priv, "stop Tx\n");
		ret = iwlagn_tx_agg_stop(priv, vif, sta, tid);
		if ((ret == 0) && (priv->agg_tids_count > 0)) {
			priv->agg_tids_count--;
			IWL_DEBUG_HT(priv, "priv->agg_tids_count = %u\n",
				     priv->agg_tids_count);
		}
		if (test_bit(STATUS_EXIT_PENDING, &priv->shrd->status))
			ret = 0;
		if (!priv->agg_tids_count && priv->cfg->ht_params &&
		    priv->cfg->ht_params->use_rts_for_aggregation) {
			/*
			 * switch off RTS/CTS if it was previously enabled
			 */
			sta_priv->lq_sta.lq.general_params.flags &=
				~LINK_QUAL_FLAGS_SET_STA_TLC_RTS_MSK;
			iwl_send_lq_cmd(priv, iwl_rxon_ctx_from_vif(vif),
					&sta_priv->lq_sta.lq, CMD_ASYNC, false);
		}
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		buf_size = min_t(int, buf_size, LINK_QUAL_AGG_FRAME_LIMIT_DEF);

		iwl_trans_tx_agg_setup(trans(priv), ctx->ctxid, iwl_sta_id(sta),
				tid, buf_size);

		/*
		 * If the limit is 0, then it wasn't initialised yet,
		 * use the default. We can do that since we take the
		 * minimum below, and we don't want to go above our
		 * default due to hardware restrictions.
		 */
		if (sta_priv->max_agg_bufsize == 0)
			sta_priv->max_agg_bufsize =
				LINK_QUAL_AGG_FRAME_LIMIT_DEF;

		/*
		 * Even though in theory the peer could have different
		 * aggregation reorder buffer sizes for different sessions,
		 * our ucode doesn't allow for that and has a global limit
		 * for each station. Therefore, use the minimum of all the
		 * aggregation sessions and our default value.
		 */
		sta_priv->max_agg_bufsize =
			min(sta_priv->max_agg_bufsize, buf_size);

		if (priv->cfg->ht_params &&
		    priv->cfg->ht_params->use_rts_for_aggregation) {
			/*
			 * switch to RTS/CTS if it is the prefer protection
			 * method for HT traffic
			 */

			sta_priv->lq_sta.lq.general_params.flags |=
				LINK_QUAL_FLAGS_SET_STA_TLC_RTS_MSK;
		}
		priv->agg_tids_count++;
		IWL_DEBUG_HT(priv, "priv->agg_tids_count = %u\n",
			     priv->agg_tids_count);

		sta_priv->lq_sta.lq.agg_params.agg_frame_cnt_limit =
			sta_priv->max_agg_bufsize;

		iwl_send_lq_cmd(priv, iwl_rxon_ctx_from_vif(vif),
				&sta_priv->lq_sta.lq, CMD_ASYNC, false);

		IWL_INFO(priv, "Tx aggregation enabled on ra = %pM tid = %d\n",
			 sta->addr, tid);
		ret = 0;
		break;
	}
	mutex_unlock(&priv->shrd->mutex);
	IWL_DEBUG_MAC80211(priv, "leave\n");
	return ret;
}

static int iwlagn_mac_sta_add(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta)
{
	struct iwl_priv *priv = hw->priv;
	struct iwl_station_priv *sta_priv = (void *)sta->drv_priv;
	struct iwl_vif_priv *vif_priv = (void *)vif->drv_priv;
	bool is_ap = vif->type == NL80211_IFTYPE_STATION;
	int ret = 0;
	u8 sta_id;

	IWL_DEBUG_MAC80211(priv, "received request to add station %pM\n",
			sta->addr);
	mutex_lock(&priv->shrd->mutex);
	IWL_DEBUG_INFO(priv, "proceeding to add station %pM\n",
			sta->addr);
	sta_priv->sta_id = IWL_INVALID_STATION;

	atomic_set(&sta_priv->pending_frames, 0);
	if (vif->type == NL80211_IFTYPE_AP)
		sta_priv->client = true;

	ret = iwl_add_station_common(priv, vif_priv->ctx, sta->addr,
				     is_ap, sta, &sta_id);
	if (ret) {
		IWL_ERR(priv, "Unable to add station %pM (%d)\n",
			sta->addr, ret);
		/* Should we return success if return code is EEXIST ? */
		goto out;
	}

	sta_priv->sta_id = sta_id;

	/* Initialize rate scaling */
	IWL_DEBUG_INFO(priv, "Initializing rate scaling for station %pM\n",
		       sta->addr);
	iwl_rs_rate_init(priv, sta, sta_id);
 out:
	mutex_unlock(&priv->shrd->mutex);
	IWL_DEBUG_MAC80211(priv, "leave\n");

	return ret;
}

static void iwlagn_mac_channel_switch(struct ieee80211_hw *hw,
				struct ieee80211_channel_switch *ch_switch)
{
	struct iwl_priv *priv = hw->priv;
	const struct iwl_channel_info *ch_info;
	struct ieee80211_conf *conf = &hw->conf;
	struct ieee80211_channel *channel = ch_switch->channel;
	struct iwl_ht_config *ht_conf = &priv->current_ht_config;
	/*
	 * MULTI-FIXME
	 * When we add support for multiple interfaces, we need to
	 * revisit this. The channel switch command in the device
	 * only affects the BSS context, but what does that really
	 * mean? And what if we get a CSA on the second interface?
	 * This needs a lot of work.
	 */
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];
	u16 ch;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	mutex_lock(&priv->shrd->mutex);

	if (iwl_is_rfkill(priv->shrd))
		goto out;

	if (test_bit(STATUS_EXIT_PENDING, &priv->shrd->status) ||
	    test_bit(STATUS_SCANNING, &priv->shrd->status) ||
	    test_bit(STATUS_CHANNEL_SWITCH_PENDING, &priv->shrd->status))
		goto out;

	if (!iwl_is_associated_ctx(ctx))
		goto out;

	if (!priv->cfg->lib->set_channel_switch)
		goto out;

	ch = channel->hw_value;
	if (le16_to_cpu(ctx->active.channel) == ch)
		goto out;

	ch_info = iwl_get_channel_info(priv, channel->band, ch);
	if (!is_channel_valid(ch_info)) {
		IWL_DEBUG_MAC80211(priv, "invalid channel\n");
		goto out;
	}

	spin_lock_irq(&priv->shrd->lock);

	priv->current_ht_config.smps = conf->smps_mode;

	/* Configure HT40 channels */
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

	if ((le16_to_cpu(ctx->staging.channel) != ch))
		ctx->staging.flags = 0;

	iwl_set_rxon_channel(priv, channel, ctx);
	iwl_set_rxon_ht(priv, ht_conf);
	iwl_set_flags_for_band(priv, ctx, channel->band, ctx->vif);

	spin_unlock_irq(&priv->shrd->lock);

	iwl_set_rate(priv);
	/*
	 * at this point, staging_rxon has the
	 * configuration for channel switch
	 */
	set_bit(STATUS_CHANNEL_SWITCH_PENDING, &priv->shrd->status);
	priv->switch_channel = cpu_to_le16(ch);
	if (priv->cfg->lib->set_channel_switch(priv, ch_switch)) {
		clear_bit(STATUS_CHANNEL_SWITCH_PENDING, &priv->shrd->status);
		priv->switch_channel = 0;
		ieee80211_chswitch_done(ctx->vif, false);
	}

out:
	mutex_unlock(&priv->shrd->mutex);
	IWL_DEBUG_MAC80211(priv, "leave\n");
}

static void iwlagn_configure_filter(struct ieee80211_hw *hw,
				    unsigned int changed_flags,
				    unsigned int *total_flags,
				    u64 multicast)
{
	struct iwl_priv *priv = hw->priv;
	__le32 filter_or = 0, filter_nand = 0;
	struct iwl_rxon_context *ctx;

#define CHK(test, flag)	do { \
	if (*total_flags & (test))		\
		filter_or |= (flag);		\
	else					\
		filter_nand |= (flag);		\
	} while (0)

	IWL_DEBUG_MAC80211(priv, "Enter: changed: 0x%x, total: 0x%x\n",
			changed_flags, *total_flags);

	CHK(FIF_OTHER_BSS | FIF_PROMISC_IN_BSS, RXON_FILTER_PROMISC_MSK);
	/* Setting _just_ RXON_FILTER_CTL2HOST_MSK causes FH errors */
	CHK(FIF_CONTROL, RXON_FILTER_CTL2HOST_MSK | RXON_FILTER_PROMISC_MSK);
	CHK(FIF_BCN_PRBRESP_PROMISC, RXON_FILTER_BCON_AWARE_MSK);

#undef CHK

	mutex_lock(&priv->shrd->mutex);

	for_each_context(priv, ctx) {
		ctx->staging.filter_flags &= ~filter_nand;
		ctx->staging.filter_flags |= filter_or;

		/*
		 * Not committing directly because hardware can perform a scan,
		 * but we'll eventually commit the filter flags change anyway.
		 */
	}

	mutex_unlock(&priv->shrd->mutex);

	/*
	 * Receiving all multicast frames is always enabled by the
	 * default flags setup in iwl_connection_init_rx_config()
	 * since we currently do not support programming multicast
	 * filters into the device.
	 */
	*total_flags &= FIF_OTHER_BSS | FIF_ALLMULTI | FIF_PROMISC_IN_BSS |
			FIF_BCN_PRBRESP_PROMISC | FIF_CONTROL;
}

static void iwlagn_mac_flush(struct ieee80211_hw *hw, bool drop)
{
	struct iwl_priv *priv = hw->priv;

	mutex_lock(&priv->shrd->mutex);
	IWL_DEBUG_MAC80211(priv, "enter\n");

	if (test_bit(STATUS_EXIT_PENDING, &priv->shrd->status)) {
		IWL_DEBUG_TX(priv, "Aborting flush due to device shutdown\n");
		goto done;
	}
	if (iwl_is_rfkill(priv->shrd)) {
		IWL_DEBUG_TX(priv, "Aborting flush due to RF Kill\n");
		goto done;
	}

	/*
	 * mac80211 will not push any more frames for transmit
	 * until the flush is completed
	 */
	if (drop) {
		IWL_DEBUG_MAC80211(priv, "send flush command\n");
		if (iwlagn_txfifo_flush(priv, IWL_DROP_ALL)) {
			IWL_ERR(priv, "flush request fail\n");
			goto done;
		}
	}
	IWL_DEBUG_MAC80211(priv, "wait transmit/flush all frames\n");
	iwl_trans_wait_tx_queue_empty(trans(priv));
done:
	mutex_unlock(&priv->shrd->mutex);
	IWL_DEBUG_MAC80211(priv, "leave\n");
}

static int iwlagn_mac_remain_on_channel(struct ieee80211_hw *hw,
				     struct ieee80211_channel *channel,
				     enum nl80211_channel_type channel_type,
				     int duration)
{
	struct iwl_priv *priv = hw->priv;
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_PAN];
	int err = 0;

	if (!(priv->shrd->valid_contexts & BIT(IWL_RXON_CTX_PAN)))
		return -EOPNOTSUPP;

	if (!(ctx->interface_modes & BIT(NL80211_IFTYPE_P2P_CLIENT)))
		return -EOPNOTSUPP;

	IWL_DEBUG_MAC80211(priv, "enter\n");
	mutex_lock(&priv->shrd->mutex);

	if (test_bit(STATUS_SCAN_HW, &priv->shrd->status)) {
		err = -EBUSY;
		goto out;
	}

	priv->hw_roc_channel = channel;
	priv->hw_roc_chantype = channel_type;
	priv->hw_roc_duration = duration;
	priv->hw_roc_start_notified = false;
	cancel_delayed_work(&priv->hw_roc_disable_work);

	if (!ctx->is_active) {
		static const struct iwl_qos_info default_qos_data = {
			.def_qos_parm = {
				.ac[0] = {
					.cw_min = cpu_to_le16(3),
					.cw_max = cpu_to_le16(7),
					.aifsn = 2,
					.edca_txop = cpu_to_le16(1504),
				},
				.ac[1] = {
					.cw_min = cpu_to_le16(7),
					.cw_max = cpu_to_le16(15),
					.aifsn = 2,
					.edca_txop = cpu_to_le16(3008),
				},
				.ac[2] = {
					.cw_min = cpu_to_le16(15),
					.cw_max = cpu_to_le16(1023),
					.aifsn = 3,
				},
				.ac[3] = {
					.cw_min = cpu_to_le16(15),
					.cw_max = cpu_to_le16(1023),
					.aifsn = 7,
				},
			},
		};

		ctx->is_active = true;
		ctx->qos_data = default_qos_data;
		ctx->staging.dev_type = RXON_DEV_TYPE_P2P;
		memcpy(ctx->staging.node_addr,
		       priv->contexts[IWL_RXON_CTX_BSS].staging.node_addr,
		       ETH_ALEN);
		memcpy(ctx->staging.bssid_addr,
		       priv->contexts[IWL_RXON_CTX_BSS].staging.node_addr,
		       ETH_ALEN);
		err = iwlagn_commit_rxon(priv, ctx);
		if (err)
			goto out;
		ctx->staging.filter_flags |= RXON_FILTER_ASSOC_MSK |
					     RXON_FILTER_PROMISC_MSK |
					     RXON_FILTER_CTL2HOST_MSK;

		err = iwlagn_commit_rxon(priv, ctx);
		if (err) {
			iwlagn_disable_roc(priv);
			goto out;
		}
		priv->hw_roc_setup = true;
	}

	err = iwl_scan_initiate(priv, ctx->vif, IWL_SCAN_ROC, channel->band);
	if (err)
		iwlagn_disable_roc(priv);

 out:
	mutex_unlock(&priv->shrd->mutex);
	IWL_DEBUG_MAC80211(priv, "leave\n");

	return err;
}

static int iwlagn_mac_cancel_remain_on_channel(struct ieee80211_hw *hw)
{
	struct iwl_priv *priv = hw->priv;

	if (!(priv->shrd->valid_contexts & BIT(IWL_RXON_CTX_PAN)))
		return -EOPNOTSUPP;

	IWL_DEBUG_MAC80211(priv, "enter\n");
	mutex_lock(&priv->shrd->mutex);
	iwl_scan_cancel_timeout(priv, priv->hw_roc_duration);
	iwlagn_disable_roc(priv);
	mutex_unlock(&priv->shrd->mutex);
	IWL_DEBUG_MAC80211(priv, "leave\n");

	return 0;
}

static int iwlagn_mac_tx_sync(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      const u8 *bssid,
			      enum ieee80211_tx_sync_type type)
{
	struct iwl_priv *priv = hw->priv;
	struct iwl_vif_priv *vif_priv = (void *)vif->drv_priv;
	struct iwl_rxon_context *ctx = vif_priv->ctx;
	int ret;
	u8 sta_id;

	IWL_DEBUG_MAC80211(priv, "enter\n");
	mutex_lock(&priv->shrd->mutex);

	if (iwl_is_associated_ctx(ctx)) {
		ret = 0;
		goto out;
	}

	if (ctx->preauth_bssid || test_bit(STATUS_SCAN_HW,
	    &priv->shrd->status)) {
		ret = -EBUSY;
		goto out;
	}

	ret = iwl_add_station_common(priv, ctx, bssid, true, NULL, &sta_id);
	if (ret)
		goto out;

	if (WARN_ON(sta_id != ctx->ap_sta_id)) {
		ret = -EIO;
		goto out_remove_sta;
	}

	memcpy(ctx->bssid, bssid, ETH_ALEN);
	ctx->preauth_bssid = true;

	ret = iwlagn_commit_rxon(priv, ctx);

	if (ret == 0)
		goto out;

 out_remove_sta:
	iwl_remove_station(priv, sta_id, bssid);
 out:
	mutex_unlock(&priv->shrd->mutex);
	IWL_DEBUG_MAC80211(priv, "leave\n");

	return ret;
}

static void iwlagn_mac_finish_tx_sync(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   const u8 *bssid,
				   enum ieee80211_tx_sync_type type)
{
	struct iwl_priv *priv = hw->priv;
	struct iwl_vif_priv *vif_priv = (void *)vif->drv_priv;
	struct iwl_rxon_context *ctx = vif_priv->ctx;

	IWL_DEBUG_MAC80211(priv, "enter\n");
	mutex_lock(&priv->shrd->mutex);

	if (iwl_is_associated_ctx(ctx))
		goto out;

	iwl_remove_station(priv, ctx->ap_sta_id, bssid);
	ctx->preauth_bssid = false;
	/* no need to commit */
 out:
	mutex_unlock(&priv->shrd->mutex);
	IWL_DEBUG_MAC80211(priv, "leave\n");
}

static void iwlagn_mac_rssi_callback(struct ieee80211_hw *hw,
			   enum ieee80211_rssi_event rssi_event)
{
	struct iwl_priv *priv = hw->priv;

	IWL_DEBUG_MAC80211(priv, "enter\n");
	mutex_lock(&priv->shrd->mutex);

	if (priv->cfg->bt_params &&
			priv->cfg->bt_params->advanced_bt_coexist) {
		if (rssi_event == RSSI_EVENT_LOW)
			priv->bt_enable_pspoll = true;
		else if (rssi_event == RSSI_EVENT_HIGH)
			priv->bt_enable_pspoll = false;

		iwlagn_send_advance_bt_config(priv);
	} else {
		IWL_DEBUG_MAC80211(priv, "Advanced BT coex disabled,"
				"ignoring RSSI callback\n");
	}

	mutex_unlock(&priv->shrd->mutex);
	IWL_DEBUG_MAC80211(priv, "leave\n");
}

static int iwlagn_mac_set_tim(struct ieee80211_hw *hw,
			   struct ieee80211_sta *sta, bool set)
{
	struct iwl_priv *priv = hw->priv;

	queue_work(priv->shrd->workqueue, &priv->beacon_update);

	return 0;
}

struct ieee80211_ops iwlagn_hw_ops = {
	.tx = iwlagn_mac_tx,
	.start = iwlagn_mac_start,
	.stop = iwlagn_mac_stop,
#ifdef CONFIG_PM_SLEEP
	.suspend = iwlagn_mac_suspend,
	.resume = iwlagn_mac_resume,
#endif
	.add_interface = iwlagn_mac_add_interface,
	.remove_interface = iwlagn_mac_remove_interface,
	.change_interface = iwlagn_mac_change_interface,
	.config = iwlagn_mac_config,
	.configure_filter = iwlagn_configure_filter,
	.set_key = iwlagn_mac_set_key,
	.update_tkip_key = iwlagn_mac_update_tkip_key,
	.set_rekey_data = iwlagn_mac_set_rekey_data,
	.conf_tx = iwlagn_mac_conf_tx,
	.bss_info_changed = iwlagn_bss_info_changed,
	.ampdu_action = iwlagn_mac_ampdu_action,
	.hw_scan = iwlagn_mac_hw_scan,
	.sta_notify = iwlagn_mac_sta_notify,
	.sta_add = iwlagn_mac_sta_add,
	.sta_remove = iwlagn_mac_sta_remove,
	.channel_switch = iwlagn_mac_channel_switch,
	.flush = iwlagn_mac_flush,
	.tx_last_beacon = iwlagn_mac_tx_last_beacon,
	.remain_on_channel = iwlagn_mac_remain_on_channel,
	.cancel_remain_on_channel = iwlagn_mac_cancel_remain_on_channel,
	.rssi_callback = iwlagn_mac_rssi_callback,
	CFG80211_TESTMODE_CMD(iwlagn_mac_testmode_cmd)
	CFG80211_TESTMODE_DUMP(iwlagn_mac_testmode_dump)
	.tx_sync = iwlagn_mac_tx_sync,
	.finish_tx_sync = iwlagn_mac_finish_tx_sync,
	.set_tim = iwlagn_mac_set_tim,
};

/* This function both allocates and initializes hw and priv. */
struct ieee80211_hw *iwl_alloc_all(void)
{
	struct iwl_priv *priv;
	/* mac80211 allocates memory for this device instance, including
	 *   space for this driver's private structure */
	struct ieee80211_hw *hw;

	hw = ieee80211_alloc_hw(sizeof(struct iwl_priv), &iwlagn_hw_ops);
	if (!hw)
		goto out;

	priv = hw->priv;
	priv->hw = hw;

out:
	return hw;
}
