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
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/etherdevice.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <net/mac80211.h>

#include "iwl-eeprom.h"
#include "iwl-dev.h"
#include "iwl-debug.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-power.h"
#include "iwl-sta.h"
#include "iwl-helpers.h"


MODULE_DESCRIPTION("iwl-legacy: common functions for 3945 and 4965");
MODULE_VERSION(IWLWIFI_VERSION);
MODULE_AUTHOR(DRV_COPYRIGHT " " DRV_AUTHOR);
MODULE_LICENSE("GPL");

/*
 * set bt_coex_active to true, uCode will do kill/defer
 * every time the priority line is asserted (BT is sending signals on the
 * priority line in the PCIx).
 * set bt_coex_active to false, uCode will ignore the BT activity and
 * perform the normal operation
 *
 * User might experience transmit issue on some platform due to WiFi/BT
 * co-exist problem. The possible behaviors are:
 *   Able to scan and finding all the available AP
 *   Not able to associate with any AP
 * On those platforms, WiFi communication can be restored by set
 * "bt_coex_active" module parameter to "false"
 *
 * default: bt_coex_active = true (BT_COEX_ENABLE)
 */
static bool bt_coex_active = true;
module_param(bt_coex_active, bool, S_IRUGO);
MODULE_PARM_DESC(bt_coex_active, "enable wifi/bluetooth co-exist");

u32 iwlegacy_debug_level;
EXPORT_SYMBOL(iwlegacy_debug_level);

const u8 iwlegacy_bcast_addr[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
EXPORT_SYMBOL(iwlegacy_bcast_addr);


/* This function both allocates and initializes hw and priv. */
struct ieee80211_hw *iwl_legacy_alloc_all(struct iwl_cfg *cfg)
{
	struct iwl_priv *priv;
	/* mac80211 allocates memory for this device instance, including
	 *   space for this driver's private structure */
	struct ieee80211_hw *hw;

	hw = ieee80211_alloc_hw(sizeof(struct iwl_priv),
				cfg->ops->ieee80211_ops);
	if (hw == NULL) {
		pr_err("%s: Can not allocate network device\n",
		       cfg->name);
		goto out;
	}

	priv = hw->priv;
	priv->hw = hw;

out:
	return hw;
}
EXPORT_SYMBOL(iwl_legacy_alloc_all);

#define MAX_BIT_RATE_40_MHZ 150 /* Mbps */
#define MAX_BIT_RATE_20_MHZ 72 /* Mbps */
static void iwl_legacy_init_ht_hw_capab(const struct iwl_priv *priv,
			      struct ieee80211_sta_ht_cap *ht_info,
			      enum ieee80211_band band)
{
	u16 max_bit_rate = 0;
	u8 rx_chains_num = priv->hw_params.rx_chains_num;
	u8 tx_chains_num = priv->hw_params.tx_chains_num;

	ht_info->cap = 0;
	memset(&ht_info->mcs, 0, sizeof(ht_info->mcs));

	ht_info->ht_supported = true;

	ht_info->cap |= IEEE80211_HT_CAP_SGI_20;
	max_bit_rate = MAX_BIT_RATE_20_MHZ;
	if (priv->hw_params.ht40_channel & BIT(band)) {
		ht_info->cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
		ht_info->cap |= IEEE80211_HT_CAP_SGI_40;
		ht_info->mcs.rx_mask[4] = 0x01;
		max_bit_rate = MAX_BIT_RATE_40_MHZ;
	}

	if (priv->cfg->mod_params->amsdu_size_8K)
		ht_info->cap |= IEEE80211_HT_CAP_MAX_AMSDU;

	ht_info->ampdu_factor = CFG_HT_RX_AMPDU_FACTOR_DEF;
	ht_info->ampdu_density = CFG_HT_MPDU_DENSITY_DEF;

	ht_info->mcs.rx_mask[0] = 0xFF;
	if (rx_chains_num >= 2)
		ht_info->mcs.rx_mask[1] = 0xFF;
	if (rx_chains_num >= 3)
		ht_info->mcs.rx_mask[2] = 0xFF;

	/* Highest supported Rx data rate */
	max_bit_rate *= rx_chains_num;
	WARN_ON(max_bit_rate & ~IEEE80211_HT_MCS_RX_HIGHEST_MASK);
	ht_info->mcs.rx_highest = cpu_to_le16(max_bit_rate);

	/* Tx MCS capabilities */
	ht_info->mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
	if (tx_chains_num != rx_chains_num) {
		ht_info->mcs.tx_params |= IEEE80211_HT_MCS_TX_RX_DIFF;
		ht_info->mcs.tx_params |= ((tx_chains_num - 1) <<
				IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT);
	}
}

/**
 * iwl_legacy_init_geos - Initialize mac80211's geo/channel info based from eeprom
 */
int iwl_legacy_init_geos(struct iwl_priv *priv)
{
	struct iwl_channel_info *ch;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *channels;
	struct ieee80211_channel *geo_ch;
	struct ieee80211_rate *rates;
	int i = 0;
	s8 max_tx_power = 0;

	if (priv->bands[IEEE80211_BAND_2GHZ].n_bitrates ||
	    priv->bands[IEEE80211_BAND_5GHZ].n_bitrates) {
		IWL_DEBUG_INFO(priv, "Geography modes already initialized.\n");
		set_bit(STATUS_GEO_CONFIGURED, &priv->status);
		return 0;
	}

	channels = kzalloc(sizeof(struct ieee80211_channel) *
			   priv->channel_count, GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	rates = kzalloc((sizeof(struct ieee80211_rate) * IWL_RATE_COUNT_LEGACY),
			GFP_KERNEL);
	if (!rates) {
		kfree(channels);
		return -ENOMEM;
	}

	/* 5.2GHz channels start after the 2.4GHz channels */
	sband = &priv->bands[IEEE80211_BAND_5GHZ];
	sband->channels = &channels[ARRAY_SIZE(iwlegacy_eeprom_band_1)];
	/* just OFDM */
	sband->bitrates = &rates[IWL_FIRST_OFDM_RATE];
	sband->n_bitrates = IWL_RATE_COUNT_LEGACY - IWL_FIRST_OFDM_RATE;

	if (priv->cfg->sku & IWL_SKU_N)
		iwl_legacy_init_ht_hw_capab(priv, &sband->ht_cap,
					 IEEE80211_BAND_5GHZ);

	sband = &priv->bands[IEEE80211_BAND_2GHZ];
	sband->channels = channels;
	/* OFDM & CCK */
	sband->bitrates = rates;
	sband->n_bitrates = IWL_RATE_COUNT_LEGACY;

	if (priv->cfg->sku & IWL_SKU_N)
		iwl_legacy_init_ht_hw_capab(priv, &sband->ht_cap,
					 IEEE80211_BAND_2GHZ);

	priv->ieee_channels = channels;
	priv->ieee_rates = rates;

	for (i = 0;  i < priv->channel_count; i++) {
		ch = &priv->channel_info[i];

		if (!iwl_legacy_is_channel_valid(ch))
			continue;

		sband = &priv->bands[ch->band];

		geo_ch = &sband->channels[sband->n_channels++];

		geo_ch->center_freq =
			ieee80211_channel_to_frequency(ch->channel, ch->band);
		geo_ch->max_power = ch->max_power_avg;
		geo_ch->max_antenna_gain = 0xff;
		geo_ch->hw_value = ch->channel;

		if (iwl_legacy_is_channel_valid(ch)) {
			if (!(ch->flags & EEPROM_CHANNEL_IBSS))
				geo_ch->flags |= IEEE80211_CHAN_NO_IBSS;

			if (!(ch->flags & EEPROM_CHANNEL_ACTIVE))
				geo_ch->flags |= IEEE80211_CHAN_PASSIVE_SCAN;

			if (ch->flags & EEPROM_CHANNEL_RADAR)
				geo_ch->flags |= IEEE80211_CHAN_RADAR;

			geo_ch->flags |= ch->ht40_extension_channel;

			if (ch->max_power_avg > max_tx_power)
				max_tx_power = ch->max_power_avg;
		} else {
			geo_ch->flags |= IEEE80211_CHAN_DISABLED;
		}

		IWL_DEBUG_INFO(priv, "Channel %d Freq=%d[%sGHz] %s flag=0x%X\n",
				ch->channel, geo_ch->center_freq,
				iwl_legacy_is_channel_a_band(ch) ?  "5.2" : "2.4",
				geo_ch->flags & IEEE80211_CHAN_DISABLED ?
				"restricted" : "valid",
				 geo_ch->flags);
	}

	priv->tx_power_device_lmt = max_tx_power;
	priv->tx_power_user_lmt = max_tx_power;
	priv->tx_power_next = max_tx_power;

	if ((priv->bands[IEEE80211_BAND_5GHZ].n_channels == 0) &&
	     priv->cfg->sku & IWL_SKU_A) {
		IWL_INFO(priv, "Incorrectly detected BG card as ABG. "
			"Please send your PCI ID 0x%04X:0x%04X to maintainer.\n",
			   priv->pci_dev->device,
			   priv->pci_dev->subsystem_device);
		priv->cfg->sku &= ~IWL_SKU_A;
	}

	IWL_INFO(priv, "Tunable channels: %d 802.11bg, %d 802.11a channels\n",
		   priv->bands[IEEE80211_BAND_2GHZ].n_channels,
		   priv->bands[IEEE80211_BAND_5GHZ].n_channels);

	set_bit(STATUS_GEO_CONFIGURED, &priv->status);

	return 0;
}
EXPORT_SYMBOL(iwl_legacy_init_geos);

/*
 * iwl_legacy_free_geos - undo allocations in iwl_legacy_init_geos
 */
void iwl_legacy_free_geos(struct iwl_priv *priv)
{
	kfree(priv->ieee_channels);
	kfree(priv->ieee_rates);
	clear_bit(STATUS_GEO_CONFIGURED, &priv->status);
}
EXPORT_SYMBOL(iwl_legacy_free_geos);

static bool iwl_legacy_is_channel_extension(struct iwl_priv *priv,
				     enum ieee80211_band band,
				     u16 channel, u8 extension_chan_offset)
{
	const struct iwl_channel_info *ch_info;

	ch_info = iwl_legacy_get_channel_info(priv, band, channel);
	if (!iwl_legacy_is_channel_valid(ch_info))
		return false;

	if (extension_chan_offset == IEEE80211_HT_PARAM_CHA_SEC_ABOVE)
		return !(ch_info->ht40_extension_channel &
					IEEE80211_CHAN_NO_HT40PLUS);
	else if (extension_chan_offset == IEEE80211_HT_PARAM_CHA_SEC_BELOW)
		return !(ch_info->ht40_extension_channel &
					IEEE80211_CHAN_NO_HT40MINUS);

	return false;
}

bool iwl_legacy_is_ht40_tx_allowed(struct iwl_priv *priv,
			    struct iwl_rxon_context *ctx,
			    struct ieee80211_sta_ht_cap *ht_cap)
{
	if (!ctx->ht.enabled || !ctx->ht.is_40mhz)
		return false;

	/*
	 * We do not check for IEEE80211_HT_CAP_SUP_WIDTH_20_40
	 * the bit will not set if it is pure 40MHz case
	 */
	if (ht_cap && !ht_cap->ht_supported)
		return false;

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUGFS
	if (priv->disable_ht40)
		return false;
#endif

	return iwl_legacy_is_channel_extension(priv, priv->band,
			le16_to_cpu(ctx->staging.channel),
			ctx->ht.extension_chan_offset);
}
EXPORT_SYMBOL(iwl_legacy_is_ht40_tx_allowed);

static u16 iwl_legacy_adjust_beacon_interval(u16 beacon_val, u16 max_beacon_val)
{
	u16 new_val;
	u16 beacon_factor;

	/*
	 * If mac80211 hasn't given us a beacon interval, program
	 * the default into the device.
	 */
	if (!beacon_val)
		return DEFAULT_BEACON_INTERVAL;

	/*
	 * If the beacon interval we obtained from the peer
	 * is too large, we'll have to wake up more often
	 * (and in IBSS case, we'll beacon too much)
	 *
	 * For example, if max_beacon_val is 4096, and the
	 * requested beacon interval is 7000, we'll have to
	 * use 3500 to be able to wake up on the beacons.
	 *
	 * This could badly influence beacon detection stats.
	 */

	beacon_factor = (beacon_val + max_beacon_val) / max_beacon_val;
	new_val = beacon_val / beacon_factor;

	if (!new_val)
		new_val = max_beacon_val;

	return new_val;
}

int
iwl_legacy_send_rxon_timing(struct iwl_priv *priv, struct iwl_rxon_context *ctx)
{
	u64 tsf;
	s32 interval_tm, rem;
	struct ieee80211_conf *conf = NULL;
	u16 beacon_int;
	struct ieee80211_vif *vif = ctx->vif;

	conf = iwl_legacy_ieee80211_get_hw_conf(priv->hw);

	lockdep_assert_held(&priv->mutex);

	memset(&ctx->timing, 0, sizeof(struct iwl_rxon_time_cmd));

	ctx->timing.timestamp = cpu_to_le64(priv->timestamp);
	ctx->timing.listen_interval = cpu_to_le16(conf->listen_interval);

	beacon_int = vif ? vif->bss_conf.beacon_int : 0;

	/*
	 * TODO: For IBSS we need to get atim_window from mac80211,
	 *	 for now just always use 0
	 */
	ctx->timing.atim_window = 0;

	beacon_int = iwl_legacy_adjust_beacon_interval(beacon_int,
			priv->hw_params.max_beacon_itrvl * TIME_UNIT);
	ctx->timing.beacon_interval = cpu_to_le16(beacon_int);

	tsf = priv->timestamp; /* tsf is modifed by do_div: copy it */
	interval_tm = beacon_int * TIME_UNIT;
	rem = do_div(tsf, interval_tm);
	ctx->timing.beacon_init_val = cpu_to_le32(interval_tm - rem);

	ctx->timing.dtim_period = vif ? (vif->bss_conf.dtim_period ?: 1) : 1;

	IWL_DEBUG_ASSOC(priv,
			"beacon interval %d beacon timer %d beacon tim %d\n",
			le16_to_cpu(ctx->timing.beacon_interval),
			le32_to_cpu(ctx->timing.beacon_init_val),
			le16_to_cpu(ctx->timing.atim_window));

	return iwl_legacy_send_cmd_pdu(priv, ctx->rxon_timing_cmd,
				sizeof(ctx->timing), &ctx->timing);
}
EXPORT_SYMBOL(iwl_legacy_send_rxon_timing);

void
iwl_legacy_set_rxon_hwcrypto(struct iwl_priv *priv,
				struct iwl_rxon_context *ctx,
				int hw_decrypt)
{
	struct iwl_legacy_rxon_cmd *rxon = &ctx->staging;

	if (hw_decrypt)
		rxon->filter_flags &= ~RXON_FILTER_DIS_DECRYPT_MSK;
	else
		rxon->filter_flags |= RXON_FILTER_DIS_DECRYPT_MSK;

}
EXPORT_SYMBOL(iwl_legacy_set_rxon_hwcrypto);

/* validate RXON structure is valid */
int
iwl_legacy_check_rxon_cmd(struct iwl_priv *priv, struct iwl_rxon_context *ctx)
{
	struct iwl_legacy_rxon_cmd *rxon = &ctx->staging;
	bool error = false;

	if (rxon->flags & RXON_FLG_BAND_24G_MSK) {
		if (rxon->flags & RXON_FLG_TGJ_NARROW_BAND_MSK) {
			IWL_WARN(priv, "check 2.4G: wrong narrow\n");
			error = true;
		}
		if (rxon->flags & RXON_FLG_RADAR_DETECT_MSK) {
			IWL_WARN(priv, "check 2.4G: wrong radar\n");
			error = true;
		}
	} else {
		if (!(rxon->flags & RXON_FLG_SHORT_SLOT_MSK)) {
			IWL_WARN(priv, "check 5.2G: not short slot!\n");
			error = true;
		}
		if (rxon->flags & RXON_FLG_CCK_MSK) {
			IWL_WARN(priv, "check 5.2G: CCK!\n");
			error = true;
		}
	}
	if ((rxon->node_addr[0] | rxon->bssid_addr[0]) & 0x1) {
		IWL_WARN(priv, "mac/bssid mcast!\n");
		error = true;
	}

	/* make sure basic rates 6Mbps and 1Mbps are supported */
	if ((rxon->ofdm_basic_rates & IWL_RATE_6M_MASK) == 0 &&
	    (rxon->cck_basic_rates & IWL_RATE_1M_MASK) == 0) {
		IWL_WARN(priv, "neither 1 nor 6 are basic\n");
		error = true;
	}

	if (le16_to_cpu(rxon->assoc_id) > 2007) {
		IWL_WARN(priv, "aid > 2007\n");
		error = true;
	}

	if ((rxon->flags & (RXON_FLG_CCK_MSK | RXON_FLG_SHORT_SLOT_MSK))
			== (RXON_FLG_CCK_MSK | RXON_FLG_SHORT_SLOT_MSK)) {
		IWL_WARN(priv, "CCK and short slot\n");
		error = true;
	}

	if ((rxon->flags & (RXON_FLG_CCK_MSK | RXON_FLG_AUTO_DETECT_MSK))
			== (RXON_FLG_CCK_MSK | RXON_FLG_AUTO_DETECT_MSK)) {
		IWL_WARN(priv, "CCK and auto detect");
		error = true;
	}

	if ((rxon->flags & (RXON_FLG_AUTO_DETECT_MSK |
			    RXON_FLG_TGG_PROTECT_MSK)) ==
			    RXON_FLG_TGG_PROTECT_MSK) {
		IWL_WARN(priv, "TGg but no auto-detect\n");
		error = true;
	}

	if (error)
		IWL_WARN(priv, "Tuning to channel %d\n",
			    le16_to_cpu(rxon->channel));

	if (error) {
		IWL_ERR(priv, "Invalid RXON\n");
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL(iwl_legacy_check_rxon_cmd);

/**
 * iwl_legacy_full_rxon_required - check if full RXON (vs RXON_ASSOC) cmd is needed
 * @priv: staging_rxon is compared to active_rxon
 *
 * If the RXON structure is changing enough to require a new tune,
 * or is clearing the RXON_FILTER_ASSOC_MSK, then return 1 to indicate that
 * a new tune (full RXON command, rather than RXON_ASSOC cmd) is required.
 */
int iwl_legacy_full_rxon_required(struct iwl_priv *priv,
			   struct iwl_rxon_context *ctx)
{
	const struct iwl_legacy_rxon_cmd *staging = &ctx->staging;
	const struct iwl_legacy_rxon_cmd *active = &ctx->active;

#define CHK(cond)							\
	if ((cond)) {							\
		IWL_DEBUG_INFO(priv, "need full RXON - " #cond "\n");	\
		return 1;						\
	}

#define CHK_NEQ(c1, c2)						\
	if ((c1) != (c2)) {					\
		IWL_DEBUG_INFO(priv, "need full RXON - "	\
			       #c1 " != " #c2 " - %d != %d\n",	\
			       (c1), (c2));			\
		return 1;					\
	}

	/* These items are only settable from the full RXON command */
	CHK(!iwl_legacy_is_associated_ctx(ctx));
	CHK(compare_ether_addr(staging->bssid_addr, active->bssid_addr));
	CHK(compare_ether_addr(staging->node_addr, active->node_addr));
	CHK(compare_ether_addr(staging->wlap_bssid_addr,
				active->wlap_bssid_addr));
	CHK_NEQ(staging->dev_type, active->dev_type);
	CHK_NEQ(staging->channel, active->channel);
	CHK_NEQ(staging->air_propagation, active->air_propagation);
	CHK_NEQ(staging->ofdm_ht_single_stream_basic_rates,
		active->ofdm_ht_single_stream_basic_rates);
	CHK_NEQ(staging->ofdm_ht_dual_stream_basic_rates,
		active->ofdm_ht_dual_stream_basic_rates);
	CHK_NEQ(staging->assoc_id, active->assoc_id);

	/* flags, filter_flags, ofdm_basic_rates, and cck_basic_rates can
	 * be updated with the RXON_ASSOC command -- however only some
	 * flag transitions are allowed using RXON_ASSOC */

	/* Check if we are not switching bands */
	CHK_NEQ(staging->flags & RXON_FLG_BAND_24G_MSK,
		active->flags & RXON_FLG_BAND_24G_MSK);

	/* Check if we are switching association toggle */
	CHK_NEQ(staging->filter_flags & RXON_FILTER_ASSOC_MSK,
		active->filter_flags & RXON_FILTER_ASSOC_MSK);

#undef CHK
#undef CHK_NEQ

	return 0;
}
EXPORT_SYMBOL(iwl_legacy_full_rxon_required);

u8 iwl_legacy_get_lowest_plcp(struct iwl_priv *priv,
			    struct iwl_rxon_context *ctx)
{
	/*
	 * Assign the lowest rate -- should really get this from
	 * the beacon skb from mac80211.
	 */
	if (ctx->staging.flags & RXON_FLG_BAND_24G_MSK)
		return IWL_RATE_1M_PLCP;
	else
		return IWL_RATE_6M_PLCP;
}
EXPORT_SYMBOL(iwl_legacy_get_lowest_plcp);

static void _iwl_legacy_set_rxon_ht(struct iwl_priv *priv,
			     struct iwl_ht_config *ht_conf,
			     struct iwl_rxon_context *ctx)
{
	struct iwl_legacy_rxon_cmd *rxon = &ctx->staging;

	if (!ctx->ht.enabled) {
		rxon->flags &= ~(RXON_FLG_CHANNEL_MODE_MSK |
			RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK |
			RXON_FLG_HT40_PROT_MSK |
			RXON_FLG_HT_PROT_MSK);
		return;
	}

	rxon->flags |= cpu_to_le32(ctx->ht.protection <<
					RXON_FLG_HT_OPERATING_MODE_POS);

	/* Set up channel bandwidth:
	 * 20 MHz only, 20/40 mixed or pure 40 if ht40 ok */
	/* clear the HT channel mode before set the mode */
	rxon->flags &= ~(RXON_FLG_CHANNEL_MODE_MSK |
			 RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK);
	if (iwl_legacy_is_ht40_tx_allowed(priv, ctx, NULL)) {
		/* pure ht40 */
		if (ctx->ht.protection ==
				IEEE80211_HT_OP_MODE_PROTECTION_20MHZ) {
			rxon->flags |= RXON_FLG_CHANNEL_MODE_PURE_40;
			/* Note: control channel is opposite of extension channel */
			switch (ctx->ht.extension_chan_offset) {
			case IEEE80211_HT_PARAM_CHA_SEC_ABOVE:
				rxon->flags &=
					~RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK;
				break;
			case IEEE80211_HT_PARAM_CHA_SEC_BELOW:
				rxon->flags |=
					RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK;
				break;
			}
		} else {
			/* Note: control channel is opposite of extension channel */
			switch (ctx->ht.extension_chan_offset) {
			case IEEE80211_HT_PARAM_CHA_SEC_ABOVE:
				rxon->flags &=
					~(RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK);
				rxon->flags |= RXON_FLG_CHANNEL_MODE_MIXED;
				break;
			case IEEE80211_HT_PARAM_CHA_SEC_BELOW:
				rxon->flags |=
					RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK;
				rxon->flags |= RXON_FLG_CHANNEL_MODE_MIXED;
				break;
			case IEEE80211_HT_PARAM_CHA_SEC_NONE:
			default:
				/* channel location only valid if in Mixed mode */
				IWL_ERR(priv,
					"invalid extension channel offset\n");
				break;
			}
		}
	} else {
		rxon->flags |= RXON_FLG_CHANNEL_MODE_LEGACY;
	}

	if (priv->cfg->ops->hcmd->set_rxon_chain)
		priv->cfg->ops->hcmd->set_rxon_chain(priv, ctx);

	IWL_DEBUG_ASSOC(priv, "rxon flags 0x%X operation mode :0x%X "
			"extension channel offset 0x%x\n",
			le32_to_cpu(rxon->flags), ctx->ht.protection,
			ctx->ht.extension_chan_offset);
}

void iwl_legacy_set_rxon_ht(struct iwl_priv *priv, struct iwl_ht_config *ht_conf)
{
	struct iwl_rxon_context *ctx;

	for_each_context(priv, ctx)
		_iwl_legacy_set_rxon_ht(priv, ht_conf, ctx);
}
EXPORT_SYMBOL(iwl_legacy_set_rxon_ht);

/* Return valid, unused, channel for a passive scan to reset the RF */
u8 iwl_legacy_get_single_channel_number(struct iwl_priv *priv,
				 enum ieee80211_band band)
{
	const struct iwl_channel_info *ch_info;
	int i;
	u8 channel = 0;
	u8 min, max;
	struct iwl_rxon_context *ctx;

	if (band == IEEE80211_BAND_5GHZ) {
		min = 14;
		max = priv->channel_count;
	} else {
		min = 0;
		max = 14;
	}

	for (i = min; i < max; i++) {
		bool busy = false;

		for_each_context(priv, ctx) {
			busy = priv->channel_info[i].channel ==
				le16_to_cpu(ctx->staging.channel);
			if (busy)
				break;
		}

		if (busy)
			continue;

		channel = priv->channel_info[i].channel;
		ch_info = iwl_legacy_get_channel_info(priv, band, channel);
		if (iwl_legacy_is_channel_valid(ch_info))
			break;
	}

	return channel;
}
EXPORT_SYMBOL(iwl_legacy_get_single_channel_number);

/**
 * iwl_legacy_set_rxon_channel - Set the band and channel values in staging RXON
 * @ch: requested channel as a pointer to struct ieee80211_channel

 * NOTE:  Does not commit to the hardware; it sets appropriate bit fields
 * in the staging RXON flag structure based on the ch->band
 */
int
iwl_legacy_set_rxon_channel(struct iwl_priv *priv, struct ieee80211_channel *ch,
			 struct iwl_rxon_context *ctx)
{
	enum ieee80211_band band = ch->band;
	u16 channel = ch->hw_value;

	if ((le16_to_cpu(ctx->staging.channel) == channel) &&
	    (priv->band == band))
		return 0;

	ctx->staging.channel = cpu_to_le16(channel);
	if (band == IEEE80211_BAND_5GHZ)
		ctx->staging.flags &= ~RXON_FLG_BAND_24G_MSK;
	else
		ctx->staging.flags |= RXON_FLG_BAND_24G_MSK;

	priv->band = band;

	IWL_DEBUG_INFO(priv, "Staging channel set to %d [%d]\n", channel, band);

	return 0;
}
EXPORT_SYMBOL(iwl_legacy_set_rxon_channel);

void iwl_legacy_set_flags_for_band(struct iwl_priv *priv,
			    struct iwl_rxon_context *ctx,
			    enum ieee80211_band band,
			    struct ieee80211_vif *vif)
{
	if (band == IEEE80211_BAND_5GHZ) {
		ctx->staging.flags &=
		    ~(RXON_FLG_BAND_24G_MSK | RXON_FLG_AUTO_DETECT_MSK
		      | RXON_FLG_CCK_MSK);
		ctx->staging.flags |= RXON_FLG_SHORT_SLOT_MSK;
	} else {
		/* Copied from iwl_post_associate() */
		if (vif && vif->bss_conf.use_short_slot)
			ctx->staging.flags |= RXON_FLG_SHORT_SLOT_MSK;
		else
			ctx->staging.flags &= ~RXON_FLG_SHORT_SLOT_MSK;

		ctx->staging.flags |= RXON_FLG_BAND_24G_MSK;
		ctx->staging.flags |= RXON_FLG_AUTO_DETECT_MSK;
		ctx->staging.flags &= ~RXON_FLG_CCK_MSK;
	}
}
EXPORT_SYMBOL(iwl_legacy_set_flags_for_band);

/*
 * initialize rxon structure with default values from eeprom
 */
void iwl_legacy_connection_init_rx_config(struct iwl_priv *priv,
				   struct iwl_rxon_context *ctx)
{
	const struct iwl_channel_info *ch_info;

	memset(&ctx->staging, 0, sizeof(ctx->staging));

	if (!ctx->vif) {
		ctx->staging.dev_type = ctx->unused_devtype;
	} else
	switch (ctx->vif->type) {

	case NL80211_IFTYPE_STATION:
		ctx->staging.dev_type = ctx->station_devtype;
		ctx->staging.filter_flags = RXON_FILTER_ACCEPT_GRP_MSK;
		break;

	case NL80211_IFTYPE_ADHOC:
		ctx->staging.dev_type = ctx->ibss_devtype;
		ctx->staging.flags = RXON_FLG_SHORT_PREAMBLE_MSK;
		ctx->staging.filter_flags = RXON_FILTER_BCON_AWARE_MSK |
						  RXON_FILTER_ACCEPT_GRP_MSK;
		break;

	default:
		IWL_ERR(priv, "Unsupported interface type %d\n",
			ctx->vif->type);
		break;
	}

#if 0
	/* TODO:  Figure out when short_preamble would be set and cache from
	 * that */
	if (!hw_to_local(priv->hw)->short_preamble)
		ctx->staging.flags &= ~RXON_FLG_SHORT_PREAMBLE_MSK;
	else
		ctx->staging.flags |= RXON_FLG_SHORT_PREAMBLE_MSK;
#endif

	ch_info = iwl_legacy_get_channel_info(priv, priv->band,
				       le16_to_cpu(ctx->active.channel));

	if (!ch_info)
		ch_info = &priv->channel_info[0];

	ctx->staging.channel = cpu_to_le16(ch_info->channel);
	priv->band = ch_info->band;

	iwl_legacy_set_flags_for_band(priv, ctx, priv->band, ctx->vif);

	ctx->staging.ofdm_basic_rates =
	    (IWL_OFDM_RATES_MASK >> IWL_FIRST_OFDM_RATE) & 0xFF;
	ctx->staging.cck_basic_rates =
	    (IWL_CCK_RATES_MASK >> IWL_FIRST_CCK_RATE) & 0xF;

	/* clear both MIX and PURE40 mode flag */
	ctx->staging.flags &= ~(RXON_FLG_CHANNEL_MODE_MIXED |
					RXON_FLG_CHANNEL_MODE_PURE_40);
	if (ctx->vif)
		memcpy(ctx->staging.node_addr, ctx->vif->addr, ETH_ALEN);

	ctx->staging.ofdm_ht_single_stream_basic_rates = 0xff;
	ctx->staging.ofdm_ht_dual_stream_basic_rates = 0xff;
}
EXPORT_SYMBOL(iwl_legacy_connection_init_rx_config);

void iwl_legacy_set_rate(struct iwl_priv *priv)
{
	const struct ieee80211_supported_band *hw = NULL;
	struct ieee80211_rate *rate;
	struct iwl_rxon_context *ctx;
	int i;

	hw = iwl_get_hw_mode(priv, priv->band);
	if (!hw) {
		IWL_ERR(priv, "Failed to set rate: unable to get hw mode\n");
		return;
	}

	priv->active_rate = 0;

	for (i = 0; i < hw->n_bitrates; i++) {
		rate = &(hw->bitrates[i]);
		if (rate->hw_value < IWL_RATE_COUNT_LEGACY)
			priv->active_rate |= (1 << rate->hw_value);
	}

	IWL_DEBUG_RATE(priv, "Set active_rate = %0x\n", priv->active_rate);

	for_each_context(priv, ctx) {
		ctx->staging.cck_basic_rates =
		    (IWL_CCK_BASIC_RATES_MASK >> IWL_FIRST_CCK_RATE) & 0xF;

		ctx->staging.ofdm_basic_rates =
		   (IWL_OFDM_BASIC_RATES_MASK >> IWL_FIRST_OFDM_RATE) & 0xFF;
	}
}
EXPORT_SYMBOL(iwl_legacy_set_rate);

void iwl_legacy_chswitch_done(struct iwl_priv *priv, bool is_success)
{
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	if (test_and_clear_bit(STATUS_CHANNEL_SWITCH_PENDING, &priv->status))
		ieee80211_chswitch_done(ctx->vif, is_success);
}
EXPORT_SYMBOL(iwl_legacy_chswitch_done);

void iwl_legacy_rx_csa(struct iwl_priv *priv, struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_csa_notification *csa = &(pkt->u.csa_notif);

	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];
	struct iwl_legacy_rxon_cmd *rxon = (void *)&ctx->active;

	if (!test_bit(STATUS_CHANNEL_SWITCH_PENDING, &priv->status))
		return;

	if (!le32_to_cpu(csa->status) && csa->channel == priv->switch_channel) {
		rxon->channel = csa->channel;
		ctx->staging.channel = csa->channel;
		IWL_DEBUG_11H(priv, "CSA notif: channel %d\n",
			      le16_to_cpu(csa->channel));
		iwl_legacy_chswitch_done(priv, true);
	} else {
		IWL_ERR(priv, "CSA notif (fail) : channel %d\n",
			le16_to_cpu(csa->channel));
		iwl_legacy_chswitch_done(priv, false);
	}
}
EXPORT_SYMBOL(iwl_legacy_rx_csa);

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
void iwl_legacy_print_rx_config_cmd(struct iwl_priv *priv,
			     struct iwl_rxon_context *ctx)
{
	struct iwl_legacy_rxon_cmd *rxon = &ctx->staging;

	IWL_DEBUG_RADIO(priv, "RX CONFIG:\n");
	iwl_print_hex_dump(priv, IWL_DL_RADIO, (u8 *) rxon, sizeof(*rxon));
	IWL_DEBUG_RADIO(priv, "u16 channel: 0x%x\n",
				le16_to_cpu(rxon->channel));
	IWL_DEBUG_RADIO(priv, "u32 flags: 0x%08X\n", le32_to_cpu(rxon->flags));
	IWL_DEBUG_RADIO(priv, "u32 filter_flags: 0x%08x\n",
				le32_to_cpu(rxon->filter_flags));
	IWL_DEBUG_RADIO(priv, "u8 dev_type: 0x%x\n", rxon->dev_type);
	IWL_DEBUG_RADIO(priv, "u8 ofdm_basic_rates: 0x%02x\n",
			rxon->ofdm_basic_rates);
	IWL_DEBUG_RADIO(priv, "u8 cck_basic_rates: 0x%02x\n",
				rxon->cck_basic_rates);
	IWL_DEBUG_RADIO(priv, "u8[6] node_addr: %pM\n", rxon->node_addr);
	IWL_DEBUG_RADIO(priv, "u8[6] bssid_addr: %pM\n", rxon->bssid_addr);
	IWL_DEBUG_RADIO(priv, "u16 assoc_id: 0x%x\n",
				le16_to_cpu(rxon->assoc_id));
}
EXPORT_SYMBOL(iwl_legacy_print_rx_config_cmd);
#endif
/**
 * iwl_legacy_irq_handle_error - called for HW or SW error interrupt from card
 */
void iwl_legacy_irq_handle_error(struct iwl_priv *priv)
{
	/* Set the FW error flag -- cleared on iwl_down */
	set_bit(STATUS_FW_ERROR, &priv->status);

	/* Cancel currently queued command. */
	clear_bit(STATUS_HCMD_ACTIVE, &priv->status);

	IWL_ERR(priv, "Loaded firmware version: %s\n",
		priv->hw->wiphy->fw_version);

	priv->cfg->ops->lib->dump_nic_error_log(priv);
	if (priv->cfg->ops->lib->dump_fh)
		priv->cfg->ops->lib->dump_fh(priv, NULL, false);
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
	if (iwl_legacy_get_debug_level(priv) & IWL_DL_FW_ERRORS)
		iwl_legacy_print_rx_config_cmd(priv,
					&priv->contexts[IWL_RXON_CTX_BSS]);
#endif

	wake_up_interruptible(&priv->wait_command_queue);

	/* Keep the restart process from trying to send host
	 * commands by clearing the INIT status bit */
	clear_bit(STATUS_READY, &priv->status);

	if (!test_bit(STATUS_EXIT_PENDING, &priv->status)) {
		IWL_DEBUG(priv, IWL_DL_FW_ERRORS,
			  "Restarting adapter due to uCode error.\n");

		if (priv->cfg->mod_params->restart_fw)
			queue_work(priv->workqueue, &priv->restart);
	}
}
EXPORT_SYMBOL(iwl_legacy_irq_handle_error);

static int iwl_legacy_apm_stop_master(struct iwl_priv *priv)
{
	int ret = 0;

	/* stop device's busmaster DMA activity */
	iwl_legacy_set_bit(priv, CSR_RESET, CSR_RESET_REG_FLAG_STOP_MASTER);

	ret = iwl_poll_bit(priv, CSR_RESET, CSR_RESET_REG_FLAG_MASTER_DISABLED,
			CSR_RESET_REG_FLAG_MASTER_DISABLED, 100);
	if (ret)
		IWL_WARN(priv, "Master Disable Timed Out, 100 usec\n");

	IWL_DEBUG_INFO(priv, "stop master\n");

	return ret;
}

void iwl_legacy_apm_stop(struct iwl_priv *priv)
{
	IWL_DEBUG_INFO(priv, "Stop card, put in low power state\n");

	/* Stop device's DMA activity */
	iwl_legacy_apm_stop_master(priv);

	/* Reset the entire device */
	iwl_legacy_set_bit(priv, CSR_RESET, CSR_RESET_REG_FLAG_SW_RESET);

	udelay(10);

	/*
	 * Clear "initialization complete" bit to move adapter from
	 * D0A* (powered-up Active) --> D0U* (Uninitialized) state.
	 */
	iwl_legacy_clear_bit(priv, CSR_GP_CNTRL,
				CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
}
EXPORT_SYMBOL(iwl_legacy_apm_stop);


/*
 * Start up NIC's basic functionality after it has been reset
 * (e.g. after platform boot, or shutdown via iwl_legacy_apm_stop())
 * NOTE:  This does not load uCode nor start the embedded processor
 */
int iwl_legacy_apm_init(struct iwl_priv *priv)
{
	int ret = 0;
	u16 lctl;

	IWL_DEBUG_INFO(priv, "Init card's basic functions\n");

	/*
	 * Use "set_bit" below rather than "write", to preserve any hardware
	 * bits already set by default after reset.
	 */

	/* Disable L0S exit timer (platform NMI Work/Around) */
	iwl_legacy_set_bit(priv, CSR_GIO_CHICKEN_BITS,
			  CSR_GIO_CHICKEN_BITS_REG_BIT_DIS_L0S_EXIT_TIMER);

	/*
	 * Disable L0s without affecting L1;
	 *  don't wait for ICH L0s (ICH bug W/A)
	 */
	iwl_legacy_set_bit(priv, CSR_GIO_CHICKEN_BITS,
			  CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX);

	/* Set FH wait threshold to maximum (HW error during stress W/A) */
	iwl_legacy_set_bit(priv, CSR_DBG_HPET_MEM_REG,
					CSR_DBG_HPET_MEM_REG_VAL);

	/*
	 * Enable HAP INTA (interrupt from management bus) to
	 * wake device's PCI Express link L1a -> L0s
	 * NOTE:  This is no-op for 3945 (non-existent bit)
	 */
	iwl_legacy_set_bit(priv, CSR_HW_IF_CONFIG_REG,
				    CSR_HW_IF_CONFIG_REG_BIT_HAP_WAKE_L1A);

	/*
	 * HW bug W/A for instability in PCIe bus L0->L0S->L1 transition.
	 * Check if BIOS (or OS) enabled L1-ASPM on this device.
	 * If so (likely), disable L0S, so device moves directly L0->L1;
	 *    costs negligible amount of power savings.
	 * If not (unlikely), enable L0S, so there is at least some
	 *    power savings, even without L1.
	 */
	if (priv->cfg->base_params->set_l0s) {
		lctl = iwl_legacy_pcie_link_ctl(priv);
		if ((lctl & PCI_CFG_LINK_CTRL_VAL_L1_EN) ==
					PCI_CFG_LINK_CTRL_VAL_L1_EN) {
			/* L1-ASPM enabled; disable(!) L0S  */
			iwl_legacy_set_bit(priv, CSR_GIO_REG,
					CSR_GIO_REG_VAL_L0S_ENABLED);
			IWL_DEBUG_POWER(priv, "L1 Enabled; Disabling L0S\n");
		} else {
			/* L1-ASPM disabled; enable(!) L0S */
			iwl_legacy_clear_bit(priv, CSR_GIO_REG,
					CSR_GIO_REG_VAL_L0S_ENABLED);
			IWL_DEBUG_POWER(priv, "L1 Disabled; Enabling L0S\n");
		}
	}

	/* Configure analog phase-lock-loop before activating to D0A */
	if (priv->cfg->base_params->pll_cfg_val)
		iwl_legacy_set_bit(priv, CSR_ANA_PLL_CFG,
			    priv->cfg->base_params->pll_cfg_val);

	/*
	 * Set "initialization complete" bit to move adapter from
	 * D0U* --> D0A* (powered-up active) state.
	 */
	iwl_legacy_set_bit(priv, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

	/*
	 * Wait for clock stabilization; once stabilized, access to
	 * device-internal resources is supported, e.g. iwl_legacy_write_prph()
	 * and accesses to uCode SRAM.
	 */
	ret = iwl_poll_bit(priv, CSR_GP_CNTRL,
			CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
			CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 25000);
	if (ret < 0) {
		IWL_DEBUG_INFO(priv, "Failed to init the card\n");
		goto out;
	}

	/*
	 * Enable DMA and BSM (if used) clocks, wait for them to stabilize.
	 * BSM (Boostrap State Machine) is only in 3945 and 4965.
	 *
	 * Write to "CLK_EN_REG"; "1" bits enable clocks, while "0" bits
	 * do not disable clocks.  This preserves any hardware bits already
	 * set by default in "CLK_CTRL_REG" after reset.
	 */
	if (priv->cfg->base_params->use_bsm)
		iwl_legacy_write_prph(priv, APMG_CLK_EN_REG,
			APMG_CLK_VAL_DMA_CLK_RQT | APMG_CLK_VAL_BSM_CLK_RQT);
	else
		iwl_legacy_write_prph(priv, APMG_CLK_EN_REG,
			APMG_CLK_VAL_DMA_CLK_RQT);
	udelay(20);

	/* Disable L1-Active */
	iwl_legacy_set_bits_prph(priv, APMG_PCIDEV_STT_REG,
			  APMG_PCIDEV_STT_VAL_L1_ACT_DIS);

out:
	return ret;
}
EXPORT_SYMBOL(iwl_legacy_apm_init);


int iwl_legacy_set_tx_power(struct iwl_priv *priv, s8 tx_power, bool force)
{
	int ret;
	s8 prev_tx_power;
	bool defer;
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];

	lockdep_assert_held(&priv->mutex);

	if (priv->tx_power_user_lmt == tx_power && !force)
		return 0;

	if (!priv->cfg->ops->lib->send_tx_power)
		return -EOPNOTSUPP;

	/* 0 dBm mean 1 milliwatt */
	if (tx_power < 0) {
		IWL_WARN(priv,
			 "Requested user TXPOWER %d below 1 mW.\n",
			 tx_power);
		return -EINVAL;
	}

	if (tx_power > priv->tx_power_device_lmt) {
		IWL_WARN(priv,
			"Requested user TXPOWER %d above upper limit %d.\n",
			 tx_power, priv->tx_power_device_lmt);
		return -EINVAL;
	}

	if (!iwl_legacy_is_ready_rf(priv))
		return -EIO;

	/* scan complete and commit_rxon use tx_power_next value,
	 * it always need to be updated for newest request */
	priv->tx_power_next = tx_power;

	/* do not set tx power when scanning or channel changing */
	defer = test_bit(STATUS_SCANNING, &priv->status) ||
		memcmp(&ctx->active, &ctx->staging, sizeof(ctx->staging));
	if (defer && !force) {
		IWL_DEBUG_INFO(priv, "Deferring tx power set\n");
		return 0;
	}

	prev_tx_power = priv->tx_power_user_lmt;
	priv->tx_power_user_lmt = tx_power;

	ret = priv->cfg->ops->lib->send_tx_power(priv);

	/* if fail to set tx_power, restore the orig. tx power */
	if (ret) {
		priv->tx_power_user_lmt = prev_tx_power;
		priv->tx_power_next = prev_tx_power;
	}
	return ret;
}
EXPORT_SYMBOL(iwl_legacy_set_tx_power);

void iwl_legacy_send_bt_config(struct iwl_priv *priv)
{
	struct iwl_bt_cmd bt_cmd = {
		.lead_time = BT_LEAD_TIME_DEF,
		.max_kill = BT_MAX_KILL_DEF,
		.kill_ack_mask = 0,
		.kill_cts_mask = 0,
	};

	if (!bt_coex_active)
		bt_cmd.flags = BT_COEX_DISABLE;
	else
		bt_cmd.flags = BT_COEX_ENABLE;

	IWL_DEBUG_INFO(priv, "BT coex %s\n",
		(bt_cmd.flags == BT_COEX_DISABLE) ? "disable" : "active");

	if (iwl_legacy_send_cmd_pdu(priv, REPLY_BT_CONFIG,
			     sizeof(struct iwl_bt_cmd), &bt_cmd))
		IWL_ERR(priv, "failed to send BT Coex Config\n");
}
EXPORT_SYMBOL(iwl_legacy_send_bt_config);

int iwl_legacy_send_statistics_request(struct iwl_priv *priv, u8 flags, bool clear)
{
	struct iwl_statistics_cmd statistics_cmd = {
		.configuration_flags =
			clear ? IWL_STATS_CONF_CLEAR_STATS : 0,
	};

	if (flags & CMD_ASYNC)
		return iwl_legacy_send_cmd_pdu_async(priv, REPLY_STATISTICS_CMD,
					sizeof(struct iwl_statistics_cmd),
					&statistics_cmd, NULL);
	else
		return iwl_legacy_send_cmd_pdu(priv, REPLY_STATISTICS_CMD,
					sizeof(struct iwl_statistics_cmd),
					&statistics_cmd);
}
EXPORT_SYMBOL(iwl_legacy_send_statistics_request);

void iwl_legacy_rx_pm_sleep_notif(struct iwl_priv *priv,
			   struct iwl_rx_mem_buffer *rxb)
{
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_sleep_notification *sleep = &(pkt->u.sleep_notif);
	IWL_DEBUG_RX(priv, "sleep mode: %d, src: %d\n",
		     sleep->pm_sleep_mode, sleep->pm_wakeup_src);
#endif
}
EXPORT_SYMBOL(iwl_legacy_rx_pm_sleep_notif);

void iwl_legacy_rx_pm_debug_statistics_notif(struct iwl_priv *priv,
				      struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	u32 len = le32_to_cpu(pkt->len_n_flags) & FH_RSCSR_FRAME_SIZE_MSK;
	IWL_DEBUG_RADIO(priv, "Dumping %d bytes of unhandled "
			"notification for %s:\n", len,
			iwl_legacy_get_cmd_string(pkt->hdr.cmd));
	iwl_print_hex_dump(priv, IWL_DL_RADIO, pkt->u.raw, len);
}
EXPORT_SYMBOL(iwl_legacy_rx_pm_debug_statistics_notif);

void iwl_legacy_rx_reply_error(struct iwl_priv *priv,
			struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);

	IWL_ERR(priv, "Error Reply type 0x%08X cmd %s (0x%02X) "
		"seq 0x%04X ser 0x%08X\n",
		le32_to_cpu(pkt->u.err_resp.error_type),
		iwl_legacy_get_cmd_string(pkt->u.err_resp.cmd_id),
		pkt->u.err_resp.cmd_id,
		le16_to_cpu(pkt->u.err_resp.bad_cmd_seq_num),
		le32_to_cpu(pkt->u.err_resp.error_info));
}
EXPORT_SYMBOL(iwl_legacy_rx_reply_error);

void iwl_legacy_clear_isr_stats(struct iwl_priv *priv)
{
	memset(&priv->isr_stats, 0, sizeof(priv->isr_stats));
}

int iwl_legacy_mac_conf_tx(struct ieee80211_hw *hw, u16 queue,
			   const struct ieee80211_tx_queue_params *params)
{
	struct iwl_priv *priv = hw->priv;
	struct iwl_rxon_context *ctx;
	unsigned long flags;
	int q;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	if (!iwl_legacy_is_ready_rf(priv)) {
		IWL_DEBUG_MAC80211(priv, "leave - RF not ready\n");
		return -EIO;
	}

	if (queue >= AC_NUM) {
		IWL_DEBUG_MAC80211(priv, "leave - queue >= AC_NUM %d\n", queue);
		return 0;
	}

	q = AC_NUM - 1 - queue;

	spin_lock_irqsave(&priv->lock, flags);

	for_each_context(priv, ctx) {
		ctx->qos_data.def_qos_parm.ac[q].cw_min =
			cpu_to_le16(params->cw_min);
		ctx->qos_data.def_qos_parm.ac[q].cw_max =
			cpu_to_le16(params->cw_max);
		ctx->qos_data.def_qos_parm.ac[q].aifsn = params->aifs;
		ctx->qos_data.def_qos_parm.ac[q].edca_txop =
				cpu_to_le16((params->txop * 32));

		ctx->qos_data.def_qos_parm.ac[q].reserved1 = 0;
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	IWL_DEBUG_MAC80211(priv, "leave\n");
	return 0;
}
EXPORT_SYMBOL(iwl_legacy_mac_conf_tx);

int iwl_legacy_mac_tx_last_beacon(struct ieee80211_hw *hw)
{
	struct iwl_priv *priv = hw->priv;

	return priv->ibss_manager == IWL_IBSS_MANAGER;
}
EXPORT_SYMBOL_GPL(iwl_legacy_mac_tx_last_beacon);

static int
iwl_legacy_set_mode(struct iwl_priv *priv, struct iwl_rxon_context *ctx)
{
	iwl_legacy_connection_init_rx_config(priv, ctx);

	if (priv->cfg->ops->hcmd->set_rxon_chain)
		priv->cfg->ops->hcmd->set_rxon_chain(priv, ctx);

	return iwl_legacy_commit_rxon(priv, ctx);
}

static int iwl_legacy_setup_interface(struct iwl_priv *priv,
			       struct iwl_rxon_context *ctx)
{
	struct ieee80211_vif *vif = ctx->vif;
	int err;

	lockdep_assert_held(&priv->mutex);

	/*
	 * This variable will be correct only when there's just
	 * a single context, but all code using it is for hardware
	 * that supports only one context.
	 */
	priv->iw_mode = vif->type;

	ctx->is_active = true;

	err = iwl_legacy_set_mode(priv, ctx);
	if (err) {
		if (!ctx->always_active)
			ctx->is_active = false;
		return err;
	}

	return 0;
}

int
iwl_legacy_mac_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct iwl_priv *priv = hw->priv;
	struct iwl_vif_priv *vif_priv = (void *)vif->drv_priv;
	struct iwl_rxon_context *tmp, *ctx = NULL;
	int err;

	IWL_DEBUG_MAC80211(priv, "enter: type %d, addr %pM\n",
			   vif->type, vif->addr);

	mutex_lock(&priv->mutex);

	if (!iwl_legacy_is_ready_rf(priv)) {
		IWL_WARN(priv, "Try to add interface when device not ready\n");
		err = -EINVAL;
		goto out;
	}

	for_each_context(priv, tmp) {
		u32 possible_modes =
			tmp->interface_modes | tmp->exclusive_interface_modes;

		if (tmp->vif) {
			/* check if this busy context is exclusive */
			if (tmp->exclusive_interface_modes &
						BIT(tmp->vif->type)) {
				err = -EINVAL;
				goto out;
			}
			continue;
		}

		if (!(possible_modes & BIT(vif->type)))
			continue;

		/* have maybe usable context w/o interface */
		ctx = tmp;
		break;
	}

	if (!ctx) {
		err = -EOPNOTSUPP;
		goto out;
	}

	vif_priv->ctx = ctx;
	ctx->vif = vif;

	err = iwl_legacy_setup_interface(priv, ctx);
	if (!err)
		goto out;

	ctx->vif = NULL;
	priv->iw_mode = NL80211_IFTYPE_STATION;
 out:
	mutex_unlock(&priv->mutex);

	IWL_DEBUG_MAC80211(priv, "leave\n");
	return err;
}
EXPORT_SYMBOL(iwl_legacy_mac_add_interface);

static void iwl_legacy_teardown_interface(struct iwl_priv *priv,
				   struct ieee80211_vif *vif,
				   bool mode_change)
{
	struct iwl_rxon_context *ctx = iwl_legacy_rxon_ctx_from_vif(vif);

	lockdep_assert_held(&priv->mutex);

	if (priv->scan_vif == vif) {
		iwl_legacy_scan_cancel_timeout(priv, 200);
		iwl_legacy_force_scan_end(priv);
	}

	if (!mode_change) {
		iwl_legacy_set_mode(priv, ctx);
		if (!ctx->always_active)
			ctx->is_active = false;
	}
}

void iwl_legacy_mac_remove_interface(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif)
{
	struct iwl_priv *priv = hw->priv;
	struct iwl_rxon_context *ctx = iwl_legacy_rxon_ctx_from_vif(vif);

	IWL_DEBUG_MAC80211(priv, "enter\n");

	mutex_lock(&priv->mutex);

	WARN_ON(ctx->vif != vif);
	ctx->vif = NULL;

	iwl_legacy_teardown_interface(priv, vif, false);

	memset(priv->bssid, 0, ETH_ALEN);
	mutex_unlock(&priv->mutex);

	IWL_DEBUG_MAC80211(priv, "leave\n");

}
EXPORT_SYMBOL(iwl_legacy_mac_remove_interface);

int iwl_legacy_alloc_txq_mem(struct iwl_priv *priv)
{
	if (!priv->txq)
		priv->txq = kzalloc(
			sizeof(struct iwl_tx_queue) *
				priv->cfg->base_params->num_of_queues,
			GFP_KERNEL);
	if (!priv->txq) {
		IWL_ERR(priv, "Not enough memory for txq\n");
		return -ENOMEM;
	}
	return 0;
}
EXPORT_SYMBOL(iwl_legacy_alloc_txq_mem);

void iwl_legacy_txq_mem(struct iwl_priv *priv)
{
	kfree(priv->txq);
	priv->txq = NULL;
}
EXPORT_SYMBOL(iwl_legacy_txq_mem);

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUGFS

#define IWL_TRAFFIC_DUMP_SIZE	(IWL_TRAFFIC_ENTRY_SIZE * IWL_TRAFFIC_ENTRIES)

void iwl_legacy_reset_traffic_log(struct iwl_priv *priv)
{
	priv->tx_traffic_idx = 0;
	priv->rx_traffic_idx = 0;
	if (priv->tx_traffic)
		memset(priv->tx_traffic, 0, IWL_TRAFFIC_DUMP_SIZE);
	if (priv->rx_traffic)
		memset(priv->rx_traffic, 0, IWL_TRAFFIC_DUMP_SIZE);
}

int iwl_legacy_alloc_traffic_mem(struct iwl_priv *priv)
{
	u32 traffic_size = IWL_TRAFFIC_DUMP_SIZE;

	if (iwlegacy_debug_level & IWL_DL_TX) {
		if (!priv->tx_traffic) {
			priv->tx_traffic =
				kzalloc(traffic_size, GFP_KERNEL);
			if (!priv->tx_traffic)
				return -ENOMEM;
		}
	}
	if (iwlegacy_debug_level & IWL_DL_RX) {
		if (!priv->rx_traffic) {
			priv->rx_traffic =
				kzalloc(traffic_size, GFP_KERNEL);
			if (!priv->rx_traffic)
				return -ENOMEM;
		}
	}
	iwl_legacy_reset_traffic_log(priv);
	return 0;
}
EXPORT_SYMBOL(iwl_legacy_alloc_traffic_mem);

void iwl_legacy_free_traffic_mem(struct iwl_priv *priv)
{
	kfree(priv->tx_traffic);
	priv->tx_traffic = NULL;

	kfree(priv->rx_traffic);
	priv->rx_traffic = NULL;
}
EXPORT_SYMBOL(iwl_legacy_free_traffic_mem);

void iwl_legacy_dbg_log_tx_data_frame(struct iwl_priv *priv,
		      u16 length, struct ieee80211_hdr *header)
{
	__le16 fc;
	u16 len;

	if (likely(!(iwlegacy_debug_level & IWL_DL_TX)))
		return;

	if (!priv->tx_traffic)
		return;

	fc = header->frame_control;
	if (ieee80211_is_data(fc)) {
		len = (length > IWL_TRAFFIC_ENTRY_SIZE)
		       ? IWL_TRAFFIC_ENTRY_SIZE : length;
		memcpy((priv->tx_traffic +
		       (priv->tx_traffic_idx * IWL_TRAFFIC_ENTRY_SIZE)),
		       header, len);
		priv->tx_traffic_idx =
			(priv->tx_traffic_idx + 1) % IWL_TRAFFIC_ENTRIES;
	}
}
EXPORT_SYMBOL(iwl_legacy_dbg_log_tx_data_frame);

void iwl_legacy_dbg_log_rx_data_frame(struct iwl_priv *priv,
		      u16 length, struct ieee80211_hdr *header)
{
	__le16 fc;
	u16 len;

	if (likely(!(iwlegacy_debug_level & IWL_DL_RX)))
		return;

	if (!priv->rx_traffic)
		return;

	fc = header->frame_control;
	if (ieee80211_is_data(fc)) {
		len = (length > IWL_TRAFFIC_ENTRY_SIZE)
		       ? IWL_TRAFFIC_ENTRY_SIZE : length;
		memcpy((priv->rx_traffic +
		       (priv->rx_traffic_idx * IWL_TRAFFIC_ENTRY_SIZE)),
		       header, len);
		priv->rx_traffic_idx =
			(priv->rx_traffic_idx + 1) % IWL_TRAFFIC_ENTRIES;
	}
}
EXPORT_SYMBOL(iwl_legacy_dbg_log_rx_data_frame);

const char *iwl_legacy_get_mgmt_string(int cmd)
{
	switch (cmd) {
		IWL_CMD(MANAGEMENT_ASSOC_REQ);
		IWL_CMD(MANAGEMENT_ASSOC_RESP);
		IWL_CMD(MANAGEMENT_REASSOC_REQ);
		IWL_CMD(MANAGEMENT_REASSOC_RESP);
		IWL_CMD(MANAGEMENT_PROBE_REQ);
		IWL_CMD(MANAGEMENT_PROBE_RESP);
		IWL_CMD(MANAGEMENT_BEACON);
		IWL_CMD(MANAGEMENT_ATIM);
		IWL_CMD(MANAGEMENT_DISASSOC);
		IWL_CMD(MANAGEMENT_AUTH);
		IWL_CMD(MANAGEMENT_DEAUTH);
		IWL_CMD(MANAGEMENT_ACTION);
	default:
		return "UNKNOWN";

	}
}

const char *iwl_legacy_get_ctrl_string(int cmd)
{
	switch (cmd) {
		IWL_CMD(CONTROL_BACK_REQ);
		IWL_CMD(CONTROL_BACK);
		IWL_CMD(CONTROL_PSPOLL);
		IWL_CMD(CONTROL_RTS);
		IWL_CMD(CONTROL_CTS);
		IWL_CMD(CONTROL_ACK);
		IWL_CMD(CONTROL_CFEND);
		IWL_CMD(CONTROL_CFENDACK);
	default:
		return "UNKNOWN";

	}
}

void iwl_legacy_clear_traffic_stats(struct iwl_priv *priv)
{
	memset(&priv->tx_stats, 0, sizeof(struct traffic_stats));
	memset(&priv->rx_stats, 0, sizeof(struct traffic_stats));
}

/*
 * if CONFIG_IWLWIFI_LEGACY_DEBUGFS defined,
 * iwl_legacy_update_stats function will
 * record all the MGMT, CTRL and DATA pkt for both TX and Rx pass
 * Use debugFs to display the rx/rx_statistics
 * if CONFIG_IWLWIFI_LEGACY_DEBUGFS not being defined, then no MGMT and CTRL
 * information will be recorded, but DATA pkt still will be recorded
 * for the reason of iwl_led.c need to control the led blinking based on
 * number of tx and rx data.
 *
 */
void
iwl_legacy_update_stats(struct iwl_priv *priv, bool is_tx, __le16 fc, u16 len)
{
	struct traffic_stats	*stats;

	if (is_tx)
		stats = &priv->tx_stats;
	else
		stats = &priv->rx_stats;

	if (ieee80211_is_mgmt(fc)) {
		switch (fc & cpu_to_le16(IEEE80211_FCTL_STYPE)) {
		case cpu_to_le16(IEEE80211_STYPE_ASSOC_REQ):
			stats->mgmt[MANAGEMENT_ASSOC_REQ]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_ASSOC_RESP):
			stats->mgmt[MANAGEMENT_ASSOC_RESP]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_REASSOC_REQ):
			stats->mgmt[MANAGEMENT_REASSOC_REQ]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_REASSOC_RESP):
			stats->mgmt[MANAGEMENT_REASSOC_RESP]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_PROBE_REQ):
			stats->mgmt[MANAGEMENT_PROBE_REQ]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_PROBE_RESP):
			stats->mgmt[MANAGEMENT_PROBE_RESP]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_BEACON):
			stats->mgmt[MANAGEMENT_BEACON]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_ATIM):
			stats->mgmt[MANAGEMENT_ATIM]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_DISASSOC):
			stats->mgmt[MANAGEMENT_DISASSOC]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_AUTH):
			stats->mgmt[MANAGEMENT_AUTH]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_DEAUTH):
			stats->mgmt[MANAGEMENT_DEAUTH]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_ACTION):
			stats->mgmt[MANAGEMENT_ACTION]++;
			break;
		}
	} else if (ieee80211_is_ctl(fc)) {
		switch (fc & cpu_to_le16(IEEE80211_FCTL_STYPE)) {
		case cpu_to_le16(IEEE80211_STYPE_BACK_REQ):
			stats->ctrl[CONTROL_BACK_REQ]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_BACK):
			stats->ctrl[CONTROL_BACK]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_PSPOLL):
			stats->ctrl[CONTROL_PSPOLL]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_RTS):
			stats->ctrl[CONTROL_RTS]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_CTS):
			stats->ctrl[CONTROL_CTS]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_ACK):
			stats->ctrl[CONTROL_ACK]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_CFEND):
			stats->ctrl[CONTROL_CFEND]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_CFENDACK):
			stats->ctrl[CONTROL_CFENDACK]++;
			break;
		}
	} else {
		/* data */
		stats->data_cnt++;
		stats->data_bytes += len;
	}
}
EXPORT_SYMBOL(iwl_legacy_update_stats);
#endif

int iwl_legacy_force_reset(struct iwl_priv *priv, bool external)
{
	struct iwl_force_reset *force_reset;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return -EINVAL;

	force_reset = &priv->force_reset;
	force_reset->reset_request_count++;
	if (!external) {
		if (force_reset->last_force_reset_jiffies &&
		    time_after(force_reset->last_force_reset_jiffies +
		    force_reset->reset_duration, jiffies)) {
			IWL_DEBUG_INFO(priv, "force reset rejected\n");
			force_reset->reset_reject_count++;
			return -EAGAIN;
		}
	}
	force_reset->reset_success_count++;
	force_reset->last_force_reset_jiffies = jiffies;

	/*
	 * if the request is from external(ex: debugfs),
	 * then always perform the request in regardless the module
	 * parameter setting
	 * if the request is from internal (uCode error or driver
	 * detect failure), then fw_restart module parameter
	 * need to be check before performing firmware reload
	 */

	if (!external && !priv->cfg->mod_params->restart_fw) {
		IWL_DEBUG_INFO(priv, "Cancel firmware reload based on "
			       "module parameter setting\n");
		return 0;
	}

	IWL_ERR(priv, "On demand firmware reload\n");

	/* Set the FW error flag -- cleared on iwl_down */
	set_bit(STATUS_FW_ERROR, &priv->status);
	wake_up_interruptible(&priv->wait_command_queue);
	/*
	 * Keep the restart process from trying to send host
	 * commands by clearing the INIT status bit
	 */
	clear_bit(STATUS_READY, &priv->status);
	queue_work(priv->workqueue, &priv->restart);

	return 0;
}

int
iwl_legacy_mac_change_interface(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif,
			enum nl80211_iftype newtype, bool newp2p)
{
	struct iwl_priv *priv = hw->priv;
	struct iwl_rxon_context *ctx = iwl_legacy_rxon_ctx_from_vif(vif);
	struct iwl_rxon_context *tmp;
	u32 interface_modes;
	int err;

	newtype = ieee80211_iftype_p2p(newtype, newp2p);

	mutex_lock(&priv->mutex);

	if (!ctx->vif || !iwl_legacy_is_ready_rf(priv)) {
		/*
		 * Huh? But wait ... this can maybe happen when
		 * we're in the middle of a firmware restart!
		 */
		err = -EBUSY;
		goto out;
	}

	interface_modes = ctx->interface_modes | ctx->exclusive_interface_modes;

	if (!(interface_modes & BIT(newtype))) {
		err = -EBUSY;
		goto out;
	}

	if (ctx->exclusive_interface_modes & BIT(newtype)) {
		for_each_context(priv, tmp) {
			if (ctx == tmp)
				continue;

			if (!tmp->vif)
				continue;

			/*
			 * The current mode switch would be exclusive, but
			 * another context is active ... refuse the switch.
			 */
			err = -EBUSY;
			goto out;
		}
	}

	/* success */
	iwl_legacy_teardown_interface(priv, vif, true);
	vif->type = newtype;
	vif->p2p = newp2p;
	err = iwl_legacy_setup_interface(priv, ctx);
	WARN_ON(err);
	/*
	 * We've switched internally, but submitting to the
	 * device may have failed for some reason. Mask this
	 * error, because otherwise mac80211 will not switch
	 * (and set the interface type back) and we'll be
	 * out of sync with it.
	 */
	err = 0;

 out:
	mutex_unlock(&priv->mutex);
	return err;
}
EXPORT_SYMBOL(iwl_legacy_mac_change_interface);

/*
 * On every watchdog tick we check (latest) time stamp. If it does not
 * change during timeout period and queue is not empty we reset firmware.
 */
static int iwl_legacy_check_stuck_queue(struct iwl_priv *priv, int cnt)
{
	struct iwl_tx_queue *txq = &priv->txq[cnt];
	struct iwl_queue *q = &txq->q;
	unsigned long timeout;
	int ret;

	if (q->read_ptr == q->write_ptr) {
		txq->time_stamp = jiffies;
		return 0;
	}

	timeout = txq->time_stamp +
		  msecs_to_jiffies(priv->cfg->base_params->wd_timeout);

	if (time_after(jiffies, timeout)) {
		IWL_ERR(priv, "Queue %d stuck for %u ms.\n",
				q->id, priv->cfg->base_params->wd_timeout);
		ret = iwl_legacy_force_reset(priv, false);
		return (ret == -EAGAIN) ? 0 : 1;
	}

	return 0;
}

/*
 * Making watchdog tick be a quarter of timeout assure we will
 * discover the queue hung between timeout and 1.25*timeout
 */
#define IWL_WD_TICK(timeout) ((timeout) / 4)

/*
 * Watchdog timer callback, we check each tx queue for stuck, if if hung
 * we reset the firmware. If everything is fine just rearm the timer.
 */
void iwl_legacy_bg_watchdog(unsigned long data)
{
	struct iwl_priv *priv = (struct iwl_priv *)data;
	int cnt;
	unsigned long timeout;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	timeout = priv->cfg->base_params->wd_timeout;
	if (timeout == 0)
		return;

	/* monitor and check for stuck cmd queue */
	if (iwl_legacy_check_stuck_queue(priv, priv->cmd_queue))
		return;

	/* monitor and check for other stuck queues */
	if (iwl_legacy_is_any_associated(priv)) {
		for (cnt = 0; cnt < priv->hw_params.max_txq_num; cnt++) {
			/* skip as we already checked the command queue */
			if (cnt == priv->cmd_queue)
				continue;
			if (iwl_legacy_check_stuck_queue(priv, cnt))
				return;
		}
	}

	mod_timer(&priv->watchdog, jiffies +
		  msecs_to_jiffies(IWL_WD_TICK(timeout)));
}
EXPORT_SYMBOL(iwl_legacy_bg_watchdog);

void iwl_legacy_setup_watchdog(struct iwl_priv *priv)
{
	unsigned int timeout = priv->cfg->base_params->wd_timeout;

	if (timeout)
		mod_timer(&priv->watchdog,
			  jiffies + msecs_to_jiffies(IWL_WD_TICK(timeout)));
	else
		del_timer(&priv->watchdog);
}
EXPORT_SYMBOL(iwl_legacy_setup_watchdog);

/*
 * extended beacon time format
 * time in usec will be changed into a 32-bit value in extended:internal format
 * the extended part is the beacon counts
 * the internal part is the time in usec within one beacon interval
 */
u32
iwl_legacy_usecs_to_beacons(struct iwl_priv *priv,
					u32 usec, u32 beacon_interval)
{
	u32 quot;
	u32 rem;
	u32 interval = beacon_interval * TIME_UNIT;

	if (!interval || !usec)
		return 0;

	quot = (usec / interval) &
		(iwl_legacy_beacon_time_mask_high(priv,
		priv->hw_params.beacon_time_tsf_bits) >>
		priv->hw_params.beacon_time_tsf_bits);
	rem = (usec % interval) & iwl_legacy_beacon_time_mask_low(priv,
				   priv->hw_params.beacon_time_tsf_bits);

	return (quot << priv->hw_params.beacon_time_tsf_bits) + rem;
}
EXPORT_SYMBOL(iwl_legacy_usecs_to_beacons);

/* base is usually what we get from ucode with each received frame,
 * the same as HW timer counter counting down
 */
__le32 iwl_legacy_add_beacon_time(struct iwl_priv *priv, u32 base,
			   u32 addon, u32 beacon_interval)
{
	u32 base_low = base & iwl_legacy_beacon_time_mask_low(priv,
					priv->hw_params.beacon_time_tsf_bits);
	u32 addon_low = addon & iwl_legacy_beacon_time_mask_low(priv,
					priv->hw_params.beacon_time_tsf_bits);
	u32 interval = beacon_interval * TIME_UNIT;
	u32 res = (base & iwl_legacy_beacon_time_mask_high(priv,
				priv->hw_params.beacon_time_tsf_bits)) +
				(addon & iwl_legacy_beacon_time_mask_high(priv,
				priv->hw_params.beacon_time_tsf_bits));

	if (base_low > addon_low)
		res += base_low - addon_low;
	else if (base_low < addon_low) {
		res += interval + base_low - addon_low;
		res += (1 << priv->hw_params.beacon_time_tsf_bits);
	} else
		res += (1 << priv->hw_params.beacon_time_tsf_bits);

	return cpu_to_le32(res);
}
EXPORT_SYMBOL(iwl_legacy_add_beacon_time);

#ifdef CONFIG_PM

int iwl_legacy_pci_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct iwl_priv *priv = pci_get_drvdata(pdev);

	/*
	 * This function is called when system goes into suspend state
	 * mac80211 will call iwl_mac_stop() from the mac80211 suspend function
	 * first but since iwl_mac_stop() has no knowledge of who the caller is,
	 * it will not call apm_ops.stop() to stop the DMA operation.
	 * Calling apm_ops.stop here to make sure we stop the DMA.
	 */
	iwl_legacy_apm_stop(priv);

	return 0;
}
EXPORT_SYMBOL(iwl_legacy_pci_suspend);

int iwl_legacy_pci_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct iwl_priv *priv = pci_get_drvdata(pdev);
	bool hw_rfkill = false;

	/*
	 * We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state.
	 */
	pci_write_config_byte(pdev, PCI_CFG_RETRY_TIMEOUT, 0x00);

	iwl_legacy_enable_interrupts(priv);

	if (!(iwl_read32(priv, CSR_GP_CNTRL) &
				CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW))
		hw_rfkill = true;

	if (hw_rfkill)
		set_bit(STATUS_RF_KILL_HW, &priv->status);
	else
		clear_bit(STATUS_RF_KILL_HW, &priv->status);

	wiphy_rfkill_set_hw_state(priv->hw->wiphy, hw_rfkill);

	return 0;
}
EXPORT_SYMBOL(iwl_legacy_pci_resume);

const struct dev_pm_ops iwl_legacy_pm_ops = {
	.suspend = iwl_legacy_pci_suspend,
	.resume = iwl_legacy_pci_resume,
	.freeze = iwl_legacy_pci_suspend,
	.thaw = iwl_legacy_pci_resume,
	.poweroff = iwl_legacy_pci_suspend,
	.restore = iwl_legacy_pci_resume,
};
EXPORT_SYMBOL(iwl_legacy_pm_ops);

#endif /* CONFIG_PM */

static void
iwl_legacy_update_qos(struct iwl_priv *priv, struct iwl_rxon_context *ctx)
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

	iwl_legacy_send_cmd_pdu_async(priv, ctx->qos_cmd,
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

	if (unlikely(test_bit(STATUS_SCANNING, &priv->status))) {
		scan_active = 1;
		IWL_DEBUG_MAC80211(priv, "scan active\n");
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
		ch_info = iwl_legacy_get_channel_info(priv, channel->band, ch);
		if (!iwl_legacy_is_channel_valid(ch_info)) {
			IWL_DEBUG_MAC80211(priv, "leave - invalid channel\n");
			ret = -EINVAL;
			goto set_ch_out;
		}

		if (priv->iw_mode == NL80211_IFTYPE_ADHOC &&
		    !iwl_legacy_is_channel_ibss(ch_info)) {
			IWL_DEBUG_MAC80211(priv, "leave - not IBSS channel\n");
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
			ctx->ht.protection =
					IEEE80211_HT_OP_MODE_PROTECTION_NONE;

			/* if we are switching from ht to 2.4 clear flags
			 * from any ht related info since 2.4 does not
			 * support ht */
			if ((le16_to_cpu(ctx->staging.channel) != ch))
				ctx->staging.flags = 0;

			iwl_legacy_set_rxon_channel(priv, channel, ctx);
			iwl_legacy_set_rxon_ht(priv, ht_conf);

			iwl_legacy_set_flags_for_band(priv, ctx, channel->band,
					       ctx->vif);
		}

		spin_unlock_irqrestore(&priv->lock, flags);

		if (priv->cfg->ops->legacy->update_bcast_stations)
			ret =
			priv->cfg->ops->legacy->update_bcast_stations(priv);

 set_ch_out:
		/* The list of supported rates and rate mask can be different
		 * for each band; since the band may have changed, reset
		 * the rate mask to what mac80211 lists */
		iwl_legacy_set_rate(priv);
	}

	if (changed & (IEEE80211_CONF_CHANGE_PS |
			IEEE80211_CONF_CHANGE_IDLE)) {
		ret = iwl_legacy_power_update_mode(priv, false);
		if (ret)
			IWL_DEBUG_MAC80211(priv, "Error setting sleep level\n");
	}

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		IWL_DEBUG_MAC80211(priv, "TX Power old=%d new=%d\n",
			priv->tx_power_user_lmt, conf->power_level);

		iwl_legacy_set_tx_power(priv, conf->power_level, false);
	}

	if (!iwl_legacy_is_ready(priv)) {
		IWL_DEBUG_MAC80211(priv, "leave - not ready\n");
		goto out;
	}

	if (scan_active)
		goto out;

	for_each_context(priv, ctx) {
		if (memcmp(&ctx->active, &ctx->staging, sizeof(ctx->staging)))
			iwl_legacy_commit_rxon(priv, ctx);
		else
			IWL_DEBUG_INFO(priv,
				"Not re-sending same RXON configuration.\n");
		if (ht_changed[ctx->ctxid])
			iwl_legacy_update_qos(priv, ctx);
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

	iwl_legacy_scan_cancel_timeout(priv, 100);
	if (!iwl_legacy_is_ready_rf(priv)) {
		IWL_DEBUG_MAC80211(priv, "leave - not ready\n");
		mutex_unlock(&priv->mutex);
		return;
	}

	/* we are restarting association process
	 * clear RXON_FILTER_ASSOC_MSK bit
	 */
	ctx->staging.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	iwl_legacy_commit_rxon(priv, ctx);

	iwl_legacy_set_rate(priv);

	mutex_unlock(&priv->mutex);

	IWL_DEBUG_MAC80211(priv, "leave\n");
}
EXPORT_SYMBOL(iwl_legacy_mac_reset_tsf);

static void iwl_legacy_ht_conf(struct iwl_priv *priv,
			struct ieee80211_vif *vif)
{
	struct iwl_ht_config *ht_conf = &priv->current_ht_config;
	struct ieee80211_sta *sta;
	struct ieee80211_bss_conf *bss_conf = &vif->bss_conf;
	struct iwl_rxon_context *ctx = iwl_legacy_rxon_ctx_from_vif(vif);

	IWL_DEBUG_ASSOC(priv, "enter:\n");

	if (!ctx->ht.enabled)
		return;

	ctx->ht.protection =
		bss_conf->ht_operation_mode & IEEE80211_HT_OP_MODE_PROTECTION;
	ctx->ht.non_gf_sta_present =
		!!(bss_conf->ht_operation_mode &
				IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT);

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

static inline void iwl_legacy_set_no_assoc(struct iwl_priv *priv,
				    struct ieee80211_vif *vif)
{
	struct iwl_rxon_context *ctx = iwl_legacy_rxon_ctx_from_vif(vif);

	/*
	 * inform the ucode that there is no longer an
	 * association and that no more packets should be
	 * sent
	 */
	ctx->staging.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	ctx->staging.assoc_id = 0;
	iwl_legacy_commit_rxon(priv, ctx);
}

static void iwl_legacy_beacon_update(struct ieee80211_hw *hw,
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

	if (!iwl_legacy_is_ready_rf(priv)) {
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
	struct iwl_rxon_context *ctx = iwl_legacy_rxon_ctx_from_vif(vif);
	int ret;

	if (WARN_ON(!priv->cfg->ops->legacy))
		return;

	IWL_DEBUG_MAC80211(priv, "changes = 0x%X\n", changes);

	mutex_lock(&priv->mutex);

	if (!iwl_legacy_is_alive(priv)) {
		mutex_unlock(&priv->mutex);
		return;
	}

	if (changes & BSS_CHANGED_QOS) {
		unsigned long flags;

		spin_lock_irqsave(&priv->lock, flags);
		ctx->qos_data.qos_active = bss_conf->qos;
		iwl_legacy_update_qos(priv, ctx);
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

	if (changes & BSS_CHANGED_BSSID) {
		IWL_DEBUG_MAC80211(priv, "BSSID %pM\n", bss_conf->bssid);

		/*
		 * If there is currently a HW scan going on in the
		 * background then we need to cancel it else the RXON
		 * below/in post_associate will fail.
		 */
		if (iwl_legacy_scan_cancel_timeout(priv, 100)) {
			IWL_WARN(priv,
				"Aborted scan still in progress after 100ms\n");
			IWL_DEBUG_MAC80211(priv,
				"leaving - scan abort failed.\n");
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
		iwl_legacy_beacon_update(hw, vif);

	if (changes & BSS_CHANGED_ERP_PREAMBLE) {
		IWL_DEBUG_MAC80211(priv, "ERP_PREAMBLE %d\n",
				   bss_conf->use_short_preamble);
		if (bss_conf->use_short_preamble)
			ctx->staging.flags |= RXON_FLG_SHORT_PREAMBLE_MSK;
		else
			ctx->staging.flags &= ~RXON_FLG_SHORT_PREAMBLE_MSK;
	}

	if (changes & BSS_CHANGED_ERP_CTS_PROT) {
		IWL_DEBUG_MAC80211(priv,
			"ERP_CTS %d\n", bss_conf->use_cts_prot);
		if (bss_conf->use_cts_prot &&
			(priv->band != IEEE80211_BAND_5GHZ))
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
		 * To do that, remove code from iwl_legacy_set_rate() and put something
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
		iwl_legacy_ht_conf(priv, vif);

		if (priv->cfg->ops->hcmd->set_rxon_chain)
			priv->cfg->ops->hcmd->set_rxon_chain(priv, ctx);
	}

	if (changes & BSS_CHANGED_ASSOC) {
		IWL_DEBUG_MAC80211(priv, "ASSOC %d\n", bss_conf->assoc);
		if (bss_conf->assoc) {
			priv->timestamp = bss_conf->timestamp;

			if (!iwl_legacy_is_rfkill(priv))
				priv->cfg->ops->legacy->post_associate(priv);
		} else
			iwl_legacy_set_no_assoc(priv, vif);
	}

	if (changes && iwl_legacy_is_associated_ctx(ctx) && bss_conf->aid) {
		IWL_DEBUG_MAC80211(priv, "Changes (%#x) while associated\n",
				   changes);
		ret = iwl_legacy_send_rxon_assoc(priv, ctx);
		if (!ret) {
			/* Sync active_rxon with latest change. */
			memcpy((void *)&ctx->active,
				&ctx->staging,
				sizeof(struct iwl_legacy_rxon_cmd));
		}
	}

	if (changes & BSS_CHANGED_BEACON_ENABLED) {
		if (vif->bss_conf.enable_beacon) {
			memcpy(ctx->staging.bssid_addr,
			       bss_conf->bssid, ETH_ALEN);
			memcpy(priv->bssid, bss_conf->bssid, ETH_ALEN);
			priv->cfg->ops->legacy->config_ap(priv);
		} else
			iwl_legacy_set_no_assoc(priv, vif);
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

irqreturn_t iwl_legacy_isr(int irq, void *data)
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
		iwl_legacy_enable_interrupts(priv);
	spin_unlock_irqrestore(&priv->lock, flags);
	return IRQ_NONE;
}
EXPORT_SYMBOL(iwl_legacy_isr);

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
