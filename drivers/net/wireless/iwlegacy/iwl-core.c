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


/* This function both allocates and initializes hw and il. */
struct ieee80211_hw *il_alloc_all(struct il_cfg *cfg)
{
	struct il_priv *il;
	/* mac80211 allocates memory for this device instance, including
	 *   space for this driver's ilate structure */
	struct ieee80211_hw *hw;

	hw = ieee80211_alloc_hw(sizeof(struct il_priv),
				cfg->ops->ieee80211_ops);
	if (hw == NULL) {
		pr_err("%s: Can not allocate network device\n",
		       cfg->name);
		goto out;
	}

	il = hw->priv;
	il->hw = hw;

out:
	return hw;
}
EXPORT_SYMBOL(il_alloc_all);

#define MAX_BIT_RATE_40_MHZ 150 /* Mbps */
#define MAX_BIT_RATE_20_MHZ 72 /* Mbps */
static void il_init_ht_hw_capab(const struct il_priv *il,
			      struct ieee80211_sta_ht_cap *ht_info,
			      enum ieee80211_band band)
{
	u16 max_bit_rate = 0;
	u8 rx_chains_num = il->hw_params.rx_chains_num;
	u8 tx_chains_num = il->hw_params.tx_chains_num;

	ht_info->cap = 0;
	memset(&ht_info->mcs, 0, sizeof(ht_info->mcs));

	ht_info->ht_supported = true;

	ht_info->cap |= IEEE80211_HT_CAP_SGI_20;
	max_bit_rate = MAX_BIT_RATE_20_MHZ;
	if (il->hw_params.ht40_channel & BIT(band)) {
		ht_info->cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
		ht_info->cap |= IEEE80211_HT_CAP_SGI_40;
		ht_info->mcs.rx_mask[4] = 0x01;
		max_bit_rate = MAX_BIT_RATE_40_MHZ;
	}

	if (il->cfg->mod_params->amsdu_size_8K)
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
 * il_init_geos - Initialize mac80211's geo/channel info based from eeprom
 */
int il_init_geos(struct il_priv *il)
{
	struct il_channel_info *ch;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *channels;
	struct ieee80211_channel *geo_ch;
	struct ieee80211_rate *rates;
	int i = 0;
	s8 max_tx_power = 0;

	if (il->bands[IEEE80211_BAND_2GHZ].n_bitrates ||
	    il->bands[IEEE80211_BAND_5GHZ].n_bitrates) {
		IL_DEBUG_INFO(il, "Geography modes already initialized.\n");
		set_bit(STATUS_GEO_CONFIGURED, &il->status);
		return 0;
	}

	channels = kzalloc(sizeof(struct ieee80211_channel) *
			   il->channel_count, GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	rates = kzalloc((sizeof(struct ieee80211_rate) * IL_RATE_COUNT_LEGACY),
			GFP_KERNEL);
	if (!rates) {
		kfree(channels);
		return -ENOMEM;
	}

	/* 5.2GHz channels start after the 2.4GHz channels */
	sband = &il->bands[IEEE80211_BAND_5GHZ];
	sband->channels = &channels[ARRAY_SIZE(iwlegacy_eeprom_band_1)];
	/* just OFDM */
	sband->bitrates = &rates[IL_FIRST_OFDM_RATE];
	sband->n_bitrates = IL_RATE_COUNT_LEGACY - IL_FIRST_OFDM_RATE;

	if (il->cfg->sku & IL_SKU_N)
		il_init_ht_hw_capab(il, &sband->ht_cap,
					 IEEE80211_BAND_5GHZ);

	sband = &il->bands[IEEE80211_BAND_2GHZ];
	sband->channels = channels;
	/* OFDM & CCK */
	sband->bitrates = rates;
	sband->n_bitrates = IL_RATE_COUNT_LEGACY;

	if (il->cfg->sku & IL_SKU_N)
		il_init_ht_hw_capab(il, &sband->ht_cap,
					 IEEE80211_BAND_2GHZ);

	il->ieee_channels = channels;
	il->ieee_rates = rates;

	for (i = 0;  i < il->channel_count; i++) {
		ch = &il->channel_info[i];

		if (!il_is_channel_valid(ch))
			continue;

		sband = &il->bands[ch->band];

		geo_ch = &sband->channels[sband->n_channels++];

		geo_ch->center_freq =
			ieee80211_channel_to_frequency(ch->channel, ch->band);
		geo_ch->max_power = ch->max_power_avg;
		geo_ch->max_antenna_gain = 0xff;
		geo_ch->hw_value = ch->channel;

		if (il_is_channel_valid(ch)) {
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

		IL_DEBUG_INFO(il, "Channel %d Freq=%d[%sGHz] %s flag=0x%X\n",
				ch->channel, geo_ch->center_freq,
				il_is_channel_a_band(ch) ?  "5.2" : "2.4",
				geo_ch->flags & IEEE80211_CHAN_DISABLED ?
				"restricted" : "valid",
				 geo_ch->flags);
	}

	il->tx_power_device_lmt = max_tx_power;
	il->tx_power_user_lmt = max_tx_power;
	il->tx_power_next = max_tx_power;

	if ((il->bands[IEEE80211_BAND_5GHZ].n_channels == 0) &&
	     il->cfg->sku & IL_SKU_A) {
		IL_INFO(il, "Incorrectly detected BG card as ABG. "
			"Please send your PCI ID 0x%04X:0x%04X to maintainer.\n",
			   il->pci_dev->device,
			   il->pci_dev->subsystem_device);
		il->cfg->sku &= ~IL_SKU_A;
	}

	IL_INFO(il, "Tunable channels: %d 802.11bg, %d 802.11a channels\n",
		   il->bands[IEEE80211_BAND_2GHZ].n_channels,
		   il->bands[IEEE80211_BAND_5GHZ].n_channels);

	set_bit(STATUS_GEO_CONFIGURED, &il->status);

	return 0;
}
EXPORT_SYMBOL(il_init_geos);

/*
 * il_free_geos - undo allocations in il_init_geos
 */
void il_free_geos(struct il_priv *il)
{
	kfree(il->ieee_channels);
	kfree(il->ieee_rates);
	clear_bit(STATUS_GEO_CONFIGURED, &il->status);
}
EXPORT_SYMBOL(il_free_geos);

static bool il_is_channel_extension(struct il_priv *il,
				     enum ieee80211_band band,
				     u16 channel, u8 extension_chan_offset)
{
	const struct il_channel_info *ch_info;

	ch_info = il_get_channel_info(il, band, channel);
	if (!il_is_channel_valid(ch_info))
		return false;

	if (extension_chan_offset == IEEE80211_HT_PARAM_CHA_SEC_ABOVE)
		return !(ch_info->ht40_extension_channel &
					IEEE80211_CHAN_NO_HT40PLUS);
	else if (extension_chan_offset == IEEE80211_HT_PARAM_CHA_SEC_BELOW)
		return !(ch_info->ht40_extension_channel &
					IEEE80211_CHAN_NO_HT40MINUS);

	return false;
}

bool il_is_ht40_tx_allowed(struct il_priv *il,
			    struct il_rxon_context *ctx,
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
	if (il->disable_ht40)
		return false;
#endif

	return il_is_channel_extension(il, il->band,
			le16_to_cpu(ctx->staging.channel),
			ctx->ht.extension_chan_offset);
}
EXPORT_SYMBOL(il_is_ht40_tx_allowed);

static u16 il_adjust_beacon_interval(u16 beacon_val, u16 max_beacon_val)
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
il_send_rxon_timing(struct il_priv *il, struct il_rxon_context *ctx)
{
	u64 tsf;
	s32 interval_tm, rem;
	struct ieee80211_conf *conf = NULL;
	u16 beacon_int;
	struct ieee80211_vif *vif = ctx->vif;

	conf = il_ieee80211_get_hw_conf(il->hw);

	lockdep_assert_held(&il->mutex);

	memset(&ctx->timing, 0, sizeof(struct il_rxon_time_cmd));

	ctx->timing.timestamp = cpu_to_le64(il->timestamp);
	ctx->timing.listen_interval = cpu_to_le16(conf->listen_interval);

	beacon_int = vif ? vif->bss_conf.beacon_int : 0;

	/*
	 * TODO: For IBSS we need to get atim_window from mac80211,
	 *	 for now just always use 0
	 */
	ctx->timing.atim_window = 0;

	beacon_int = il_adjust_beacon_interval(beacon_int,
			il->hw_params.max_beacon_itrvl * TIME_UNIT);
	ctx->timing.beacon_interval = cpu_to_le16(beacon_int);

	tsf = il->timestamp; /* tsf is modifed by do_div: copy it */
	interval_tm = beacon_int * TIME_UNIT;
	rem = do_div(tsf, interval_tm);
	ctx->timing.beacon_init_val = cpu_to_le32(interval_tm - rem);

	ctx->timing.dtim_period = vif ? (vif->bss_conf.dtim_period ?: 1) : 1;

	IL_DEBUG_ASSOC(il,
			"beacon interval %d beacon timer %d beacon tim %d\n",
			le16_to_cpu(ctx->timing.beacon_interval),
			le32_to_cpu(ctx->timing.beacon_init_val),
			le16_to_cpu(ctx->timing.atim_window));

	return il_send_cmd_pdu(il, ctx->rxon_timing_cmd,
				sizeof(ctx->timing), &ctx->timing);
}
EXPORT_SYMBOL(il_send_rxon_timing);

void
il_set_rxon_hwcrypto(struct il_priv *il,
				struct il_rxon_context *ctx,
				int hw_decrypt)
{
	struct il_rxon_cmd *rxon = &ctx->staging;

	if (hw_decrypt)
		rxon->filter_flags &= ~RXON_FILTER_DIS_DECRYPT_MSK;
	else
		rxon->filter_flags |= RXON_FILTER_DIS_DECRYPT_MSK;

}
EXPORT_SYMBOL(il_set_rxon_hwcrypto);

/* validate RXON structure is valid */
int
il_check_rxon_cmd(struct il_priv *il, struct il_rxon_context *ctx)
{
	struct il_rxon_cmd *rxon = &ctx->staging;
	bool error = false;

	if (rxon->flags & RXON_FLG_BAND_24G_MSK) {
		if (rxon->flags & RXON_FLG_TGJ_NARROW_BAND_MSK) {
			IL_WARN(il, "check 2.4G: wrong narrow\n");
			error = true;
		}
		if (rxon->flags & RXON_FLG_RADAR_DETECT_MSK) {
			IL_WARN(il, "check 2.4G: wrong radar\n");
			error = true;
		}
	} else {
		if (!(rxon->flags & RXON_FLG_SHORT_SLOT_MSK)) {
			IL_WARN(il, "check 5.2G: not short slot!\n");
			error = true;
		}
		if (rxon->flags & RXON_FLG_CCK_MSK) {
			IL_WARN(il, "check 5.2G: CCK!\n");
			error = true;
		}
	}
	if ((rxon->node_addr[0] | rxon->bssid_addr[0]) & 0x1) {
		IL_WARN(il, "mac/bssid mcast!\n");
		error = true;
	}

	/* make sure basic rates 6Mbps and 1Mbps are supported */
	if ((rxon->ofdm_basic_rates & IL_RATE_6M_MASK) == 0 &&
	    (rxon->cck_basic_rates & IL_RATE_1M_MASK) == 0) {
		IL_WARN(il, "neither 1 nor 6 are basic\n");
		error = true;
	}

	if (le16_to_cpu(rxon->assoc_id) > 2007) {
		IL_WARN(il, "aid > 2007\n");
		error = true;
	}

	if ((rxon->flags & (RXON_FLG_CCK_MSK | RXON_FLG_SHORT_SLOT_MSK))
			== (RXON_FLG_CCK_MSK | RXON_FLG_SHORT_SLOT_MSK)) {
		IL_WARN(il, "CCK and short slot\n");
		error = true;
	}

	if ((rxon->flags & (RXON_FLG_CCK_MSK | RXON_FLG_AUTO_DETECT_MSK))
			== (RXON_FLG_CCK_MSK | RXON_FLG_AUTO_DETECT_MSK)) {
		IL_WARN(il, "CCK and auto detect");
		error = true;
	}

	if ((rxon->flags & (RXON_FLG_AUTO_DETECT_MSK |
			    RXON_FLG_TGG_PROTECT_MSK)) ==
			    RXON_FLG_TGG_PROTECT_MSK) {
		IL_WARN(il, "TGg but no auto-detect\n");
		error = true;
	}

	if (error)
		IL_WARN(il, "Tuning to channel %d\n",
			    le16_to_cpu(rxon->channel));

	if (error) {
		IL_ERR(il, "Invalid RXON\n");
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL(il_check_rxon_cmd);

/**
 * il_full_rxon_required - check if full RXON (vs RXON_ASSOC) cmd is needed
 * @il: staging_rxon is compared to active_rxon
 *
 * If the RXON structure is changing enough to require a new tune,
 * or is clearing the RXON_FILTER_ASSOC_MSK, then return 1 to indicate that
 * a new tune (full RXON command, rather than RXON_ASSOC cmd) is required.
 */
int il_full_rxon_required(struct il_priv *il,
			   struct il_rxon_context *ctx)
{
	const struct il_rxon_cmd *staging = &ctx->staging;
	const struct il_rxon_cmd *active = &ctx->active;

#define CHK(cond)							\
	if ((cond)) {							\
		IL_DEBUG_INFO(il, "need full RXON - " #cond "\n");	\
		return 1;						\
	}

#define CHK_NEQ(c1, c2)						\
	if ((c1) != (c2)) {					\
		IL_DEBUG_INFO(il, "need full RXON - "	\
			       #c1 " != " #c2 " - %d != %d\n",	\
			       (c1), (c2));			\
		return 1;					\
	}

	/* These items are only settable from the full RXON command */
	CHK(!il_is_associated_ctx(ctx));
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
EXPORT_SYMBOL(il_full_rxon_required);

u8 il_get_lowest_plcp(struct il_priv *il,
			    struct il_rxon_context *ctx)
{
	/*
	 * Assign the lowest rate -- should really get this from
	 * the beacon skb from mac80211.
	 */
	if (ctx->staging.flags & RXON_FLG_BAND_24G_MSK)
		return IL_RATE_1M_PLCP;
	else
		return IL_RATE_6M_PLCP;
}
EXPORT_SYMBOL(il_get_lowest_plcp);

static void _il_set_rxon_ht(struct il_priv *il,
			     struct il_ht_config *ht_conf,
			     struct il_rxon_context *ctx)
{
	struct il_rxon_cmd *rxon = &ctx->staging;

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
	if (il_is_ht40_tx_allowed(il, ctx, NULL)) {
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
				IL_ERR(il,
					"invalid extension channel offset\n");
				break;
			}
		}
	} else {
		rxon->flags |= RXON_FLG_CHANNEL_MODE_LEGACY;
	}

	if (il->cfg->ops->hcmd->set_rxon_chain)
		il->cfg->ops->hcmd->set_rxon_chain(il, ctx);

	IL_DEBUG_ASSOC(il, "rxon flags 0x%X operation mode :0x%X "
			"extension channel offset 0x%x\n",
			le32_to_cpu(rxon->flags), ctx->ht.protection,
			ctx->ht.extension_chan_offset);
}

void il_set_rxon_ht(struct il_priv *il, struct il_ht_config *ht_conf)
{
	struct il_rxon_context *ctx;

	for_each_context(il, ctx)
		_il_set_rxon_ht(il, ht_conf, ctx);
}
EXPORT_SYMBOL(il_set_rxon_ht);

/* Return valid, unused, channel for a passive scan to reset the RF */
u8 il_get_single_channel_number(struct il_priv *il,
				 enum ieee80211_band band)
{
	const struct il_channel_info *ch_info;
	int i;
	u8 channel = 0;
	u8 min, max;
	struct il_rxon_context *ctx;

	if (band == IEEE80211_BAND_5GHZ) {
		min = 14;
		max = il->channel_count;
	} else {
		min = 0;
		max = 14;
	}

	for (i = min; i < max; i++) {
		bool busy = false;

		for_each_context(il, ctx) {
			busy = il->channel_info[i].channel ==
				le16_to_cpu(ctx->staging.channel);
			if (busy)
				break;
		}

		if (busy)
			continue;

		channel = il->channel_info[i].channel;
		ch_info = il_get_channel_info(il, band, channel);
		if (il_is_channel_valid(ch_info))
			break;
	}

	return channel;
}
EXPORT_SYMBOL(il_get_single_channel_number);

/**
 * il_set_rxon_channel - Set the band and channel values in staging RXON
 * @ch: requested channel as a pointer to struct ieee80211_channel

 * NOTE:  Does not commit to the hardware; it sets appropriate bit fields
 * in the staging RXON flag structure based on the ch->band
 */
int
il_set_rxon_channel(struct il_priv *il, struct ieee80211_channel *ch,
			 struct il_rxon_context *ctx)
{
	enum ieee80211_band band = ch->band;
	u16 channel = ch->hw_value;

	if ((le16_to_cpu(ctx->staging.channel) == channel) &&
	    (il->band == band))
		return 0;

	ctx->staging.channel = cpu_to_le16(channel);
	if (band == IEEE80211_BAND_5GHZ)
		ctx->staging.flags &= ~RXON_FLG_BAND_24G_MSK;
	else
		ctx->staging.flags |= RXON_FLG_BAND_24G_MSK;

	il->band = band;

	IL_DEBUG_INFO(il, "Staging channel set to %d [%d]\n", channel, band);

	return 0;
}
EXPORT_SYMBOL(il_set_rxon_channel);

void il_set_flags_for_band(struct il_priv *il,
			    struct il_rxon_context *ctx,
			    enum ieee80211_band band,
			    struct ieee80211_vif *vif)
{
	if (band == IEEE80211_BAND_5GHZ) {
		ctx->staging.flags &=
		    ~(RXON_FLG_BAND_24G_MSK | RXON_FLG_AUTO_DETECT_MSK
		      | RXON_FLG_CCK_MSK);
		ctx->staging.flags |= RXON_FLG_SHORT_SLOT_MSK;
	} else {
		/* Copied from il_post_associate() */
		if (vif && vif->bss_conf.use_short_slot)
			ctx->staging.flags |= RXON_FLG_SHORT_SLOT_MSK;
		else
			ctx->staging.flags &= ~RXON_FLG_SHORT_SLOT_MSK;

		ctx->staging.flags |= RXON_FLG_BAND_24G_MSK;
		ctx->staging.flags |= RXON_FLG_AUTO_DETECT_MSK;
		ctx->staging.flags &= ~RXON_FLG_CCK_MSK;
	}
}
EXPORT_SYMBOL(il_set_flags_for_band);

/*
 * initialize rxon structure with default values from eeprom
 */
void il_connection_init_rx_config(struct il_priv *il,
				   struct il_rxon_context *ctx)
{
	const struct il_channel_info *ch_info;

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
		IL_ERR(il, "Unsupported interface type %d\n",
			ctx->vif->type);
		break;
	}

#if 0
	/* TODO:  Figure out when short_preamble would be set and cache from
	 * that */
	if (!hw_to_local(il->hw)->short_preamble)
		ctx->staging.flags &= ~RXON_FLG_SHORT_PREAMBLE_MSK;
	else
		ctx->staging.flags |= RXON_FLG_SHORT_PREAMBLE_MSK;
#endif

	ch_info = il_get_channel_info(il, il->band,
				       le16_to_cpu(ctx->active.channel));

	if (!ch_info)
		ch_info = &il->channel_info[0];

	ctx->staging.channel = cpu_to_le16(ch_info->channel);
	il->band = ch_info->band;

	il_set_flags_for_band(il, ctx, il->band, ctx->vif);

	ctx->staging.ofdm_basic_rates =
	    (IL_OFDM_RATES_MASK >> IL_FIRST_OFDM_RATE) & 0xFF;
	ctx->staging.cck_basic_rates =
	    (IL_CCK_RATES_MASK >> IL_FIRST_CCK_RATE) & 0xF;

	/* clear both MIX and PURE40 mode flag */
	ctx->staging.flags &= ~(RXON_FLG_CHANNEL_MODE_MIXED |
					RXON_FLG_CHANNEL_MODE_PURE_40);
	if (ctx->vif)
		memcpy(ctx->staging.node_addr, ctx->vif->addr, ETH_ALEN);

	ctx->staging.ofdm_ht_single_stream_basic_rates = 0xff;
	ctx->staging.ofdm_ht_dual_stream_basic_rates = 0xff;
}
EXPORT_SYMBOL(il_connection_init_rx_config);

void il_set_rate(struct il_priv *il)
{
	const struct ieee80211_supported_band *hw = NULL;
	struct ieee80211_rate *rate;
	struct il_rxon_context *ctx;
	int i;

	hw = il_get_hw_mode(il, il->band);
	if (!hw) {
		IL_ERR(il, "Failed to set rate: unable to get hw mode\n");
		return;
	}

	il->active_rate = 0;

	for (i = 0; i < hw->n_bitrates; i++) {
		rate = &(hw->bitrates[i]);
		if (rate->hw_value < IL_RATE_COUNT_LEGACY)
			il->active_rate |= (1 << rate->hw_value);
	}

	IL_DEBUG_RATE(il, "Set active_rate = %0x\n", il->active_rate);

	for_each_context(il, ctx) {
		ctx->staging.cck_basic_rates =
		    (IL_CCK_BASIC_RATES_MASK >> IL_FIRST_CCK_RATE) & 0xF;

		ctx->staging.ofdm_basic_rates =
		   (IL_OFDM_BASIC_RATES_MASK >> IL_FIRST_OFDM_RATE) & 0xFF;
	}
}
EXPORT_SYMBOL(il_set_rate);

void il_chswitch_done(struct il_priv *il, bool is_success)
{
	struct il_rxon_context *ctx = &il->contexts[IL_RXON_CTX_BSS];

	if (test_bit(STATUS_EXIT_PENDING, &il->status))
		return;

	if (test_and_clear_bit(STATUS_CHANNEL_SWITCH_PENDING, &il->status))
		ieee80211_chswitch_done(ctx->vif, is_success);
}
EXPORT_SYMBOL(il_chswitch_done);

void il_rx_csa(struct il_priv *il, struct il_rx_mem_buffer *rxb)
{
	struct il_rx_packet *pkt = rxb_addr(rxb);
	struct il_csa_notification *csa = &(pkt->u.csa_notif);

	struct il_rxon_context *ctx = &il->contexts[IL_RXON_CTX_BSS];
	struct il_rxon_cmd *rxon = (void *)&ctx->active;

	if (!test_bit(STATUS_CHANNEL_SWITCH_PENDING, &il->status))
		return;

	if (!le32_to_cpu(csa->status) && csa->channel == il->switch_channel) {
		rxon->channel = csa->channel;
		ctx->staging.channel = csa->channel;
		IL_DEBUG_11H(il, "CSA notif: channel %d\n",
			      le16_to_cpu(csa->channel));
		il_chswitch_done(il, true);
	} else {
		IL_ERR(il, "CSA notif (fail) : channel %d\n",
			le16_to_cpu(csa->channel));
		il_chswitch_done(il, false);
	}
}
EXPORT_SYMBOL(il_rx_csa);

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
void il_print_rx_config_cmd(struct il_priv *il,
			     struct il_rxon_context *ctx)
{
	struct il_rxon_cmd *rxon = &ctx->staging;

	IL_DEBUG_RADIO(il, "RX CONFIG:\n");
	il_print_hex_dump(il, IL_DL_RADIO, (u8 *) rxon, sizeof(*rxon));
	IL_DEBUG_RADIO(il, "u16 channel: 0x%x\n",
				le16_to_cpu(rxon->channel));
	IL_DEBUG_RADIO(il, "u32 flags: 0x%08X\n", le32_to_cpu(rxon->flags));
	IL_DEBUG_RADIO(il, "u32 filter_flags: 0x%08x\n",
				le32_to_cpu(rxon->filter_flags));
	IL_DEBUG_RADIO(il, "u8 dev_type: 0x%x\n", rxon->dev_type);
	IL_DEBUG_RADIO(il, "u8 ofdm_basic_rates: 0x%02x\n",
			rxon->ofdm_basic_rates);
	IL_DEBUG_RADIO(il, "u8 cck_basic_rates: 0x%02x\n",
				rxon->cck_basic_rates);
	IL_DEBUG_RADIO(il, "u8[6] node_addr: %pM\n", rxon->node_addr);
	IL_DEBUG_RADIO(il, "u8[6] bssid_addr: %pM\n", rxon->bssid_addr);
	IL_DEBUG_RADIO(il, "u16 assoc_id: 0x%x\n",
				le16_to_cpu(rxon->assoc_id));
}
EXPORT_SYMBOL(il_print_rx_config_cmd);
#endif
/**
 * il_irq_handle_error - called for HW or SW error interrupt from card
 */
void il_irq_handle_error(struct il_priv *il)
{
	/* Set the FW error flag -- cleared on il_down */
	set_bit(STATUS_FW_ERROR, &il->status);

	/* Cancel currently queued command. */
	clear_bit(STATUS_HCMD_ACTIVE, &il->status);

	IL_ERR(il, "Loaded firmware version: %s\n",
		il->hw->wiphy->fw_version);

	il->cfg->ops->lib->dump_nic_error_log(il);
	if (il->cfg->ops->lib->dump_fh)
		il->cfg->ops->lib->dump_fh(il, NULL, false);
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
	if (il_get_debug_level(il) & IL_DL_FW_ERRORS)
		il_print_rx_config_cmd(il,
					&il->contexts[IL_RXON_CTX_BSS]);
#endif

	wake_up(&il->wait_command_queue);

	/* Keep the restart process from trying to send host
	 * commands by clearing the INIT status bit */
	clear_bit(STATUS_READY, &il->status);

	if (!test_bit(STATUS_EXIT_PENDING, &il->status)) {
		IL_DEBUG(il, IL_DL_FW_ERRORS,
			  "Restarting adapter due to uCode error.\n");

		if (il->cfg->mod_params->restart_fw)
			queue_work(il->workqueue, &il->restart);
	}
}
EXPORT_SYMBOL(il_irq_handle_error);

static int il_apm_stop_master(struct il_priv *il)
{
	int ret = 0;

	/* stop device's busmaster DMA activity */
	il_set_bit(il, CSR_RESET, CSR_RESET_REG_FLAG_STOP_MASTER);

	ret = il_poll_bit(il, CSR_RESET, CSR_RESET_REG_FLAG_MASTER_DISABLED,
			CSR_RESET_REG_FLAG_MASTER_DISABLED, 100);
	if (ret)
		IL_WARN(il, "Master Disable Timed Out, 100 usec\n");

	IL_DEBUG_INFO(il, "stop master\n");

	return ret;
}

void il_apm_stop(struct il_priv *il)
{
	IL_DEBUG_INFO(il, "Stop card, put in low power state\n");

	/* Stop device's DMA activity */
	il_apm_stop_master(il);

	/* Reset the entire device */
	il_set_bit(il, CSR_RESET, CSR_RESET_REG_FLAG_SW_RESET);

	udelay(10);

	/*
	 * Clear "initialization complete" bit to move adapter from
	 * D0A* (powered-up Active) --> D0U* (Uninitialized) state.
	 */
	il_clear_bit(il, CSR_GP_CNTRL,
				CSR_GP_CNTRL_REG_FLAG_INIT_DONE);
}
EXPORT_SYMBOL(il_apm_stop);


/*
 * Start up NIC's basic functionality after it has been reset
 * (e.g. after platform boot, or shutdown via il_apm_stop())
 * NOTE:  This does not load uCode nor start the embedded processor
 */
int il_apm_init(struct il_priv *il)
{
	int ret = 0;
	u16 lctl;

	IL_DEBUG_INFO(il, "Init card's basic functions\n");

	/*
	 * Use "set_bit" below rather than "write", to preserve any hardware
	 * bits already set by default after reset.
	 */

	/* Disable L0S exit timer (platform NMI Work/Around) */
	il_set_bit(il, CSR_GIO_CHICKEN_BITS,
			  CSR_GIO_CHICKEN_BITS_REG_BIT_DIS_L0S_EXIT_TIMER);

	/*
	 * Disable L0s without affecting L1;
	 *  don't wait for ICH L0s (ICH bug W/A)
	 */
	il_set_bit(il, CSR_GIO_CHICKEN_BITS,
			  CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX);

	/* Set FH wait threshold to maximum (HW error during stress W/A) */
	il_set_bit(il, CSR_DBG_HPET_MEM_REG,
					CSR_DBG_HPET_MEM_REG_VAL);

	/*
	 * Enable HAP INTA (interrupt from management bus) to
	 * wake device's PCI Express link L1a -> L0s
	 * NOTE:  This is no-op for 3945 (non-existent bit)
	 */
	il_set_bit(il, CSR_HW_IF_CONFIG_REG,
				    CSR_HW_IF_CONFIG_REG_BIT_HAP_WAKE_L1A);

	/*
	 * HW bug W/A for instability in PCIe bus L0->L0S->L1 transition.
	 * Check if BIOS (or OS) enabled L1-ASPM on this device.
	 * If so (likely), disable L0S, so device moves directly L0->L1;
	 *    costs negligible amount of power savings.
	 * If not (unlikely), enable L0S, so there is at least some
	 *    power savings, even without L1.
	 */
	if (il->cfg->base_params->set_l0s) {
		lctl = il_pcie_link_ctl(il);
		if ((lctl & PCI_CFG_LINK_CTRL_VAL_L1_EN) ==
					PCI_CFG_LINK_CTRL_VAL_L1_EN) {
			/* L1-ASPM enabled; disable(!) L0S  */
			il_set_bit(il, CSR_GIO_REG,
					CSR_GIO_REG_VAL_L0S_ENABLED);
			IL_DEBUG_POWER(il, "L1 Enabled; Disabling L0S\n");
		} else {
			/* L1-ASPM disabled; enable(!) L0S */
			il_clear_bit(il, CSR_GIO_REG,
					CSR_GIO_REG_VAL_L0S_ENABLED);
			IL_DEBUG_POWER(il, "L1 Disabled; Enabling L0S\n");
		}
	}

	/* Configure analog phase-lock-loop before activating to D0A */
	if (il->cfg->base_params->pll_cfg_val)
		il_set_bit(il, CSR_ANA_PLL_CFG,
			    il->cfg->base_params->pll_cfg_val);

	/*
	 * Set "initialization complete" bit to move adapter from
	 * D0U* --> D0A* (powered-up active) state.
	 */
	il_set_bit(il, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

	/*
	 * Wait for clock stabilization; once stabilized, access to
	 * device-internal resources is supported, e.g. il_write_prph()
	 * and accesses to uCode SRAM.
	 */
	ret = il_poll_bit(il, CSR_GP_CNTRL,
			CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY,
			CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY, 25000);
	if (ret < 0) {
		IL_DEBUG_INFO(il, "Failed to init the card\n");
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
	if (il->cfg->base_params->use_bsm)
		il_write_prph(il, APMG_CLK_EN_REG,
			APMG_CLK_VAL_DMA_CLK_RQT | APMG_CLK_VAL_BSM_CLK_RQT);
	else
		il_write_prph(il, APMG_CLK_EN_REG,
			APMG_CLK_VAL_DMA_CLK_RQT);
	udelay(20);

	/* Disable L1-Active */
	il_set_bits_prph(il, APMG_PCIDEV_STT_REG,
			  APMG_PCIDEV_STT_VAL_L1_ACT_DIS);

out:
	return ret;
}
EXPORT_SYMBOL(il_apm_init);


int il_set_tx_power(struct il_priv *il, s8 tx_power, bool force)
{
	int ret;
	s8 prev_tx_power;
	bool defer;
	struct il_rxon_context *ctx = &il->contexts[IL_RXON_CTX_BSS];

	lockdep_assert_held(&il->mutex);

	if (il->tx_power_user_lmt == tx_power && !force)
		return 0;

	if (!il->cfg->ops->lib->send_tx_power)
		return -EOPNOTSUPP;

	/* 0 dBm mean 1 milliwatt */
	if (tx_power < 0) {
		IL_WARN(il,
			 "Requested user TXPOWER %d below 1 mW.\n",
			 tx_power);
		return -EINVAL;
	}

	if (tx_power > il->tx_power_device_lmt) {
		IL_WARN(il,
			"Requested user TXPOWER %d above upper limit %d.\n",
			 tx_power, il->tx_power_device_lmt);
		return -EINVAL;
	}

	if (!il_is_ready_rf(il))
		return -EIO;

	/* scan complete and commit_rxon use tx_power_next value,
	 * it always need to be updated for newest request */
	il->tx_power_next = tx_power;

	/* do not set tx power when scanning or channel changing */
	defer = test_bit(STATUS_SCANNING, &il->status) ||
		memcmp(&ctx->active, &ctx->staging, sizeof(ctx->staging));
	if (defer && !force) {
		IL_DEBUG_INFO(il, "Deferring tx power set\n");
		return 0;
	}

	prev_tx_power = il->tx_power_user_lmt;
	il->tx_power_user_lmt = tx_power;

	ret = il->cfg->ops->lib->send_tx_power(il);

	/* if fail to set tx_power, restore the orig. tx power */
	if (ret) {
		il->tx_power_user_lmt = prev_tx_power;
		il->tx_power_next = prev_tx_power;
	}
	return ret;
}
EXPORT_SYMBOL(il_set_tx_power);

void il_send_bt_config(struct il_priv *il)
{
	struct il_bt_cmd bt_cmd = {
		.lead_time = BT_LEAD_TIME_DEF,
		.max_kill = BT_MAX_KILL_DEF,
		.kill_ack_mask = 0,
		.kill_cts_mask = 0,
	};

	if (!bt_coex_active)
		bt_cmd.flags = BT_COEX_DISABLE;
	else
		bt_cmd.flags = BT_COEX_ENABLE;

	IL_DEBUG_INFO(il, "BT coex %s\n",
		(bt_cmd.flags == BT_COEX_DISABLE) ? "disable" : "active");

	if (il_send_cmd_pdu(il, REPLY_BT_CONFIG,
			     sizeof(struct il_bt_cmd), &bt_cmd))
		IL_ERR(il, "failed to send BT Coex Config\n");
}
EXPORT_SYMBOL(il_send_bt_config);

int il_send_statistics_request(struct il_priv *il, u8 flags, bool clear)
{
	struct il_statistics_cmd statistics_cmd = {
		.configuration_flags =
			clear ? IL_STATS_CONF_CLEAR_STATS : 0,
	};

	if (flags & CMD_ASYNC)
		return il_send_cmd_pdu_async(il, REPLY_STATISTICS_CMD,
					sizeof(struct il_statistics_cmd),
					&statistics_cmd, NULL);
	else
		return il_send_cmd_pdu(il, REPLY_STATISTICS_CMD,
					sizeof(struct il_statistics_cmd),
					&statistics_cmd);
}
EXPORT_SYMBOL(il_send_statistics_request);

void il_rx_pm_sleep_notif(struct il_priv *il,
			   struct il_rx_mem_buffer *rxb)
{
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
	struct il_rx_packet *pkt = rxb_addr(rxb);
	struct il_sleep_notification *sleep = &(pkt->u.sleep_notif);
	IL_DEBUG_RX(il, "sleep mode: %d, src: %d\n",
		     sleep->pm_sleep_mode, sleep->pm_wakeup_src);
#endif
}
EXPORT_SYMBOL(il_rx_pm_sleep_notif);

void il_rx_pm_debug_statistics_notif(struct il_priv *il,
				      struct il_rx_mem_buffer *rxb)
{
	struct il_rx_packet *pkt = rxb_addr(rxb);
	u32 len = le32_to_cpu(pkt->len_n_flags) & FH_RSCSR_FRAME_SIZE_MSK;
	IL_DEBUG_RADIO(il, "Dumping %d bytes of unhandled "
			"notification for %s:\n", len,
			il_get_cmd_string(pkt->hdr.cmd));
	il_print_hex_dump(il, IL_DL_RADIO, pkt->u.raw, len);
}
EXPORT_SYMBOL(il_rx_pm_debug_statistics_notif);

void il_rx_reply_error(struct il_priv *il,
			struct il_rx_mem_buffer *rxb)
{
	struct il_rx_packet *pkt = rxb_addr(rxb);

	IL_ERR(il, "Error Reply type 0x%08X cmd %s (0x%02X) "
		"seq 0x%04X ser 0x%08X\n",
		le32_to_cpu(pkt->u.err_resp.error_type),
		il_get_cmd_string(pkt->u.err_resp.cmd_id),
		pkt->u.err_resp.cmd_id,
		le16_to_cpu(pkt->u.err_resp.bad_cmd_seq_num),
		le32_to_cpu(pkt->u.err_resp.error_info));
}
EXPORT_SYMBOL(il_rx_reply_error);

void il_clear_isr_stats(struct il_priv *il)
{
	memset(&il->isr_stats, 0, sizeof(il->isr_stats));
}

int il_mac_conf_tx(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif, u16 queue,
			   const struct ieee80211_tx_queue_params *params)
{
	struct il_priv *il = hw->priv;
	struct il_rxon_context *ctx;
	unsigned long flags;
	int q;

	IL_DEBUG_MAC80211(il, "enter\n");

	if (!il_is_ready_rf(il)) {
		IL_DEBUG_MAC80211(il, "leave - RF not ready\n");
		return -EIO;
	}

	if (queue >= AC_NUM) {
		IL_DEBUG_MAC80211(il, "leave - queue >= AC_NUM %d\n", queue);
		return 0;
	}

	q = AC_NUM - 1 - queue;

	spin_lock_irqsave(&il->lock, flags);

	for_each_context(il, ctx) {
		ctx->qos_data.def_qos_parm.ac[q].cw_min =
			cpu_to_le16(params->cw_min);
		ctx->qos_data.def_qos_parm.ac[q].cw_max =
			cpu_to_le16(params->cw_max);
		ctx->qos_data.def_qos_parm.ac[q].aifsn = params->aifs;
		ctx->qos_data.def_qos_parm.ac[q].edca_txop =
				cpu_to_le16((params->txop * 32));

		ctx->qos_data.def_qos_parm.ac[q].reserved1 = 0;
	}

	spin_unlock_irqrestore(&il->lock, flags);

	IL_DEBUG_MAC80211(il, "leave\n");
	return 0;
}
EXPORT_SYMBOL(il_mac_conf_tx);

int il_mac_tx_last_beacon(struct ieee80211_hw *hw)
{
	struct il_priv *il = hw->priv;

	return il->ibss_manager == IL_IBSS_MANAGER;
}
EXPORT_SYMBOL_GPL(il_mac_tx_last_beacon);

static int
il_set_mode(struct il_priv *il, struct il_rxon_context *ctx)
{
	il_connection_init_rx_config(il, ctx);

	if (il->cfg->ops->hcmd->set_rxon_chain)
		il->cfg->ops->hcmd->set_rxon_chain(il, ctx);

	return il_commit_rxon(il, ctx);
}

static int il_setup_interface(struct il_priv *il,
			       struct il_rxon_context *ctx)
{
	struct ieee80211_vif *vif = ctx->vif;
	int err;

	lockdep_assert_held(&il->mutex);

	/*
	 * This variable will be correct only when there's just
	 * a single context, but all code using it is for hardware
	 * that supports only one context.
	 */
	il->iw_mode = vif->type;

	ctx->is_active = true;

	err = il_set_mode(il, ctx);
	if (err) {
		if (!ctx->always_active)
			ctx->is_active = false;
		return err;
	}

	return 0;
}

int
il_mac_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct il_priv *il = hw->priv;
	struct il_vif_priv *vif_priv = (void *)vif->drv_priv;
	struct il_rxon_context *tmp, *ctx = NULL;
	int err;

	IL_DEBUG_MAC80211(il, "enter: type %d, addr %pM\n",
			   vif->type, vif->addr);

	mutex_lock(&il->mutex);

	if (!il_is_ready_rf(il)) {
		IL_WARN(il, "Try to add interface when device not ready\n");
		err = -EINVAL;
		goto out;
	}

	for_each_context(il, tmp) {
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

	err = il_setup_interface(il, ctx);
	if (!err)
		goto out;

	ctx->vif = NULL;
	il->iw_mode = NL80211_IFTYPE_STATION;
 out:
	mutex_unlock(&il->mutex);

	IL_DEBUG_MAC80211(il, "leave\n");
	return err;
}
EXPORT_SYMBOL(il_mac_add_interface);

static void il_teardown_interface(struct il_priv *il,
				   struct ieee80211_vif *vif,
				   bool mode_change)
{
	struct il_rxon_context *ctx = il_rxon_ctx_from_vif(vif);

	lockdep_assert_held(&il->mutex);

	if (il->scan_vif == vif) {
		il_scan_cancel_timeout(il, 200);
		il_force_scan_end(il);
	}

	if (!mode_change) {
		il_set_mode(il, ctx);
		if (!ctx->always_active)
			ctx->is_active = false;
	}
}

void il_mac_remove_interface(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif)
{
	struct il_priv *il = hw->priv;
	struct il_rxon_context *ctx = il_rxon_ctx_from_vif(vif);

	IL_DEBUG_MAC80211(il, "enter\n");

	mutex_lock(&il->mutex);

	WARN_ON(ctx->vif != vif);
	ctx->vif = NULL;

	il_teardown_interface(il, vif, false);

	memset(il->bssid, 0, ETH_ALEN);
	mutex_unlock(&il->mutex);

	IL_DEBUG_MAC80211(il, "leave\n");

}
EXPORT_SYMBOL(il_mac_remove_interface);

int il_alloc_txq_mem(struct il_priv *il)
{
	if (!il->txq)
		il->txq = kzalloc(
			sizeof(struct il_tx_queue) *
				il->cfg->base_params->num_of_queues,
			GFP_KERNEL);
	if (!il->txq) {
		IL_ERR(il, "Not enough memory for txq\n");
		return -ENOMEM;
	}
	return 0;
}
EXPORT_SYMBOL(il_alloc_txq_mem);

void il_txq_mem(struct il_priv *il)
{
	kfree(il->txq);
	il->txq = NULL;
}
EXPORT_SYMBOL(il_txq_mem);

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUGFS

#define IL_TRAFFIC_DUMP_SIZE	(IL_TRAFFIC_ENTRY_SIZE * IL_TRAFFIC_ENTRIES)

void il_reset_traffic_log(struct il_priv *il)
{
	il->tx_traffic_idx = 0;
	il->rx_traffic_idx = 0;
	if (il->tx_traffic)
		memset(il->tx_traffic, 0, IL_TRAFFIC_DUMP_SIZE);
	if (il->rx_traffic)
		memset(il->rx_traffic, 0, IL_TRAFFIC_DUMP_SIZE);
}

int il_alloc_traffic_mem(struct il_priv *il)
{
	u32 traffic_size = IL_TRAFFIC_DUMP_SIZE;

	if (iwlegacy_debug_level & IL_DL_TX) {
		if (!il->tx_traffic) {
			il->tx_traffic =
				kzalloc(traffic_size, GFP_KERNEL);
			if (!il->tx_traffic)
				return -ENOMEM;
		}
	}
	if (iwlegacy_debug_level & IL_DL_RX) {
		if (!il->rx_traffic) {
			il->rx_traffic =
				kzalloc(traffic_size, GFP_KERNEL);
			if (!il->rx_traffic)
				return -ENOMEM;
		}
	}
	il_reset_traffic_log(il);
	return 0;
}
EXPORT_SYMBOL(il_alloc_traffic_mem);

void il_free_traffic_mem(struct il_priv *il)
{
	kfree(il->tx_traffic);
	il->tx_traffic = NULL;

	kfree(il->rx_traffic);
	il->rx_traffic = NULL;
}
EXPORT_SYMBOL(il_free_traffic_mem);

void il_dbg_log_tx_data_frame(struct il_priv *il,
		      u16 length, struct ieee80211_hdr *header)
{
	__le16 fc;
	u16 len;

	if (likely(!(iwlegacy_debug_level & IL_DL_TX)))
		return;

	if (!il->tx_traffic)
		return;

	fc = header->frame_control;
	if (ieee80211_is_data(fc)) {
		len = (length > IL_TRAFFIC_ENTRY_SIZE)
		       ? IL_TRAFFIC_ENTRY_SIZE : length;
		memcpy((il->tx_traffic +
		       (il->tx_traffic_idx * IL_TRAFFIC_ENTRY_SIZE)),
		       header, len);
		il->tx_traffic_idx =
			(il->tx_traffic_idx + 1) % IL_TRAFFIC_ENTRIES;
	}
}
EXPORT_SYMBOL(il_dbg_log_tx_data_frame);

void il_dbg_log_rx_data_frame(struct il_priv *il,
		      u16 length, struct ieee80211_hdr *header)
{
	__le16 fc;
	u16 len;

	if (likely(!(iwlegacy_debug_level & IL_DL_RX)))
		return;

	if (!il->rx_traffic)
		return;

	fc = header->frame_control;
	if (ieee80211_is_data(fc)) {
		len = (length > IL_TRAFFIC_ENTRY_SIZE)
		       ? IL_TRAFFIC_ENTRY_SIZE : length;
		memcpy((il->rx_traffic +
		       (il->rx_traffic_idx * IL_TRAFFIC_ENTRY_SIZE)),
		       header, len);
		il->rx_traffic_idx =
			(il->rx_traffic_idx + 1) % IL_TRAFFIC_ENTRIES;
	}
}
EXPORT_SYMBOL(il_dbg_log_rx_data_frame);

const char *il_get_mgmt_string(int cmd)
{
	switch (cmd) {
		IL_CMD(MANAGEMENT_ASSOC_REQ);
		IL_CMD(MANAGEMENT_ASSOC_RESP);
		IL_CMD(MANAGEMENT_REASSOC_REQ);
		IL_CMD(MANAGEMENT_REASSOC_RESP);
		IL_CMD(MANAGEMENT_PROBE_REQ);
		IL_CMD(MANAGEMENT_PROBE_RESP);
		IL_CMD(MANAGEMENT_BEACON);
		IL_CMD(MANAGEMENT_ATIM);
		IL_CMD(MANAGEMENT_DISASSOC);
		IL_CMD(MANAGEMENT_AUTH);
		IL_CMD(MANAGEMENT_DEAUTH);
		IL_CMD(MANAGEMENT_ACTION);
	default:
		return "UNKNOWN";

	}
}

const char *il_get_ctrl_string(int cmd)
{
	switch (cmd) {
		IL_CMD(CONTROL_BACK_REQ);
		IL_CMD(CONTROL_BACK);
		IL_CMD(CONTROL_PSPOLL);
		IL_CMD(CONTROL_RTS);
		IL_CMD(CONTROL_CTS);
		IL_CMD(CONTROL_ACK);
		IL_CMD(CONTROL_CFEND);
		IL_CMD(CONTROL_CFENDACK);
	default:
		return "UNKNOWN";

	}
}

void il_clear_traffic_stats(struct il_priv *il)
{
	memset(&il->tx_stats, 0, sizeof(struct traffic_stats));
	memset(&il->rx_stats, 0, sizeof(struct traffic_stats));
}

/*
 * if CONFIG_IWLWIFI_LEGACY_DEBUGFS defined,
 * il_update_stats function will
 * record all the MGMT, CTRL and DATA pkt for both TX and Rx pass
 * Use debugFs to display the rx/rx_statistics
 * if CONFIG_IWLWIFI_LEGACY_DEBUGFS not being defined, then no MGMT and CTRL
 * information will be recorded, but DATA pkt still will be recorded
 * for the reason of il_led.c need to control the led blinking based on
 * number of tx and rx data.
 *
 */
void
il_update_stats(struct il_priv *il, bool is_tx, __le16 fc, u16 len)
{
	struct traffic_stats	*stats;

	if (is_tx)
		stats = &il->tx_stats;
	else
		stats = &il->rx_stats;

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
EXPORT_SYMBOL(il_update_stats);
#endif

int il_force_reset(struct il_priv *il, bool external)
{
	struct il_force_reset *force_reset;

	if (test_bit(STATUS_EXIT_PENDING, &il->status))
		return -EINVAL;

	force_reset = &il->force_reset;
	force_reset->reset_request_count++;
	if (!external) {
		if (force_reset->last_force_reset_jiffies &&
		    time_after(force_reset->last_force_reset_jiffies +
		    force_reset->reset_duration, jiffies)) {
			IL_DEBUG_INFO(il, "force reset rejected\n");
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

	if (!external && !il->cfg->mod_params->restart_fw) {
		IL_DEBUG_INFO(il, "Cancel firmware reload based on "
			       "module parameter setting\n");
		return 0;
	}

	IL_ERR(il, "On demand firmware reload\n");

	/* Set the FW error flag -- cleared on il_down */
	set_bit(STATUS_FW_ERROR, &il->status);
	wake_up(&il->wait_command_queue);
	/*
	 * Keep the restart process from trying to send host
	 * commands by clearing the INIT status bit
	 */
	clear_bit(STATUS_READY, &il->status);
	queue_work(il->workqueue, &il->restart);

	return 0;
}

int
il_mac_change_interface(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif,
			enum nl80211_iftype newtype, bool newp2p)
{
	struct il_priv *il = hw->priv;
	struct il_rxon_context *ctx = il_rxon_ctx_from_vif(vif);
	struct il_rxon_context *tmp;
	u32 interface_modes;
	int err;

	newtype = ieee80211_iftype_p2p(newtype, newp2p);

	mutex_lock(&il->mutex);

	if (!ctx->vif || !il_is_ready_rf(il)) {
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
		for_each_context(il, tmp) {
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
	il_teardown_interface(il, vif, true);
	vif->type = newtype;
	vif->p2p = newp2p;
	err = il_setup_interface(il, ctx);
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
	mutex_unlock(&il->mutex);
	return err;
}
EXPORT_SYMBOL(il_mac_change_interface);

/*
 * On every watchdog tick we check (latest) time stamp. If it does not
 * change during timeout period and queue is not empty we reset firmware.
 */
static int il_check_stuck_queue(struct il_priv *il, int cnt)
{
	struct il_tx_queue *txq = &il->txq[cnt];
	struct il_queue *q = &txq->q;
	unsigned long timeout;
	int ret;

	if (q->read_ptr == q->write_ptr) {
		txq->time_stamp = jiffies;
		return 0;
	}

	timeout = txq->time_stamp +
		  msecs_to_jiffies(il->cfg->base_params->wd_timeout);

	if (time_after(jiffies, timeout)) {
		IL_ERR(il, "Queue %d stuck for %u ms.\n",
				q->id, il->cfg->base_params->wd_timeout);
		ret = il_force_reset(il, false);
		return (ret == -EAGAIN) ? 0 : 1;
	}

	return 0;
}

/*
 * Making watchdog tick be a quarter of timeout assure we will
 * discover the queue hung between timeout and 1.25*timeout
 */
#define IL_WD_TICK(timeout) ((timeout) / 4)

/*
 * Watchdog timer callback, we check each tx queue for stuck, if if hung
 * we reset the firmware. If everything is fine just rearm the timer.
 */
void il_bg_watchdog(unsigned long data)
{
	struct il_priv *il = (struct il_priv *)data;
	int cnt;
	unsigned long timeout;

	if (test_bit(STATUS_EXIT_PENDING, &il->status))
		return;

	timeout = il->cfg->base_params->wd_timeout;
	if (timeout == 0)
		return;

	/* monitor and check for stuck cmd queue */
	if (il_check_stuck_queue(il, il->cmd_queue))
		return;

	/* monitor and check for other stuck queues */
	if (il_is_any_associated(il)) {
		for (cnt = 0; cnt < il->hw_params.max_txq_num; cnt++) {
			/* skip as we already checked the command queue */
			if (cnt == il->cmd_queue)
				continue;
			if (il_check_stuck_queue(il, cnt))
				return;
		}
	}

	mod_timer(&il->watchdog, jiffies +
		  msecs_to_jiffies(IL_WD_TICK(timeout)));
}
EXPORT_SYMBOL(il_bg_watchdog);

void il_setup_watchdog(struct il_priv *il)
{
	unsigned int timeout = il->cfg->base_params->wd_timeout;

	if (timeout)
		mod_timer(&il->watchdog,
			  jiffies + msecs_to_jiffies(IL_WD_TICK(timeout)));
	else
		del_timer(&il->watchdog);
}
EXPORT_SYMBOL(il_setup_watchdog);

/*
 * extended beacon time format
 * time in usec will be changed into a 32-bit value in extended:internal format
 * the extended part is the beacon counts
 * the internal part is the time in usec within one beacon interval
 */
u32
il_usecs_to_beacons(struct il_priv *il,
					u32 usec, u32 beacon_interval)
{
	u32 quot;
	u32 rem;
	u32 interval = beacon_interval * TIME_UNIT;

	if (!interval || !usec)
		return 0;

	quot = (usec / interval) &
		(il_beacon_time_mask_high(il,
		il->hw_params.beacon_time_tsf_bits) >>
		il->hw_params.beacon_time_tsf_bits);
	rem = (usec % interval) & il_beacon_time_mask_low(il,
				   il->hw_params.beacon_time_tsf_bits);

	return (quot << il->hw_params.beacon_time_tsf_bits) + rem;
}
EXPORT_SYMBOL(il_usecs_to_beacons);

/* base is usually what we get from ucode with each received frame,
 * the same as HW timer counter counting down
 */
__le32 il_add_beacon_time(struct il_priv *il, u32 base,
			   u32 addon, u32 beacon_interval)
{
	u32 base_low = base & il_beacon_time_mask_low(il,
					il->hw_params.beacon_time_tsf_bits);
	u32 addon_low = addon & il_beacon_time_mask_low(il,
					il->hw_params.beacon_time_tsf_bits);
	u32 interval = beacon_interval * TIME_UNIT;
	u32 res = (base & il_beacon_time_mask_high(il,
				il->hw_params.beacon_time_tsf_bits)) +
				(addon & il_beacon_time_mask_high(il,
				il->hw_params.beacon_time_tsf_bits));

	if (base_low > addon_low)
		res += base_low - addon_low;
	else if (base_low < addon_low) {
		res += interval + base_low - addon_low;
		res += (1 << il->hw_params.beacon_time_tsf_bits);
	} else
		res += (1 << il->hw_params.beacon_time_tsf_bits);

	return cpu_to_le32(res);
}
EXPORT_SYMBOL(il_add_beacon_time);

#ifdef CONFIG_PM

int il_pci_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct il_priv *il = pci_get_drvdata(pdev);

	/*
	 * This function is called when system goes into suspend state
	 * mac80211 will call il_mac_stop() from the mac80211 suspend function
	 * first but since il_mac_stop() has no knowledge of who the caller is,
	 * it will not call apm_ops.stop() to stop the DMA operation.
	 * Calling apm_ops.stop here to make sure we stop the DMA.
	 */
	il_apm_stop(il);

	return 0;
}
EXPORT_SYMBOL(il_pci_suspend);

int il_pci_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct il_priv *il = pci_get_drvdata(pdev);
	bool hw_rfkill = false;

	/*
	 * We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state.
	 */
	pci_write_config_byte(pdev, PCI_CFG_RETRY_TIMEOUT, 0x00);

	il_enable_interrupts(il);

	if (!(il_read32(il, CSR_GP_CNTRL) &
				CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW))
		hw_rfkill = true;

	if (hw_rfkill)
		set_bit(STATUS_RF_KILL_HW, &il->status);
	else
		clear_bit(STATUS_RF_KILL_HW, &il->status);

	wiphy_rfkill_set_hw_state(il->hw->wiphy, hw_rfkill);

	return 0;
}
EXPORT_SYMBOL(il_pci_resume);

const struct dev_pm_ops il_pm_ops = {
	.suspend = il_pci_suspend,
	.resume = il_pci_resume,
	.freeze = il_pci_suspend,
	.thaw = il_pci_resume,
	.poweroff = il_pci_suspend,
	.restore = il_pci_resume,
};
EXPORT_SYMBOL(il_pm_ops);

#endif /* CONFIG_PM */

static void
il_update_qos(struct il_priv *il, struct il_rxon_context *ctx)
{
	if (test_bit(STATUS_EXIT_PENDING, &il->status))
		return;

	if (!ctx->is_active)
		return;

	ctx->qos_data.def_qos_parm.qos_flags = 0;

	if (ctx->qos_data.qos_active)
		ctx->qos_data.def_qos_parm.qos_flags |=
			QOS_PARAM_FLG_UPDATE_EDCA_MSK;

	if (ctx->ht.enabled)
		ctx->qos_data.def_qos_parm.qos_flags |= QOS_PARAM_FLG_TGN_MSK;

	IL_DEBUG_QOS(il, "send QoS cmd with Qos active=%d FLAGS=0x%X\n",
		      ctx->qos_data.qos_active,
		      ctx->qos_data.def_qos_parm.qos_flags);

	il_send_cmd_pdu_async(il, ctx->qos_cmd,
			       sizeof(struct il_qosparam_cmd),
			       &ctx->qos_data.def_qos_parm, NULL);
}

/**
 * il_mac_config - mac80211 config callback
 */
int il_mac_config(struct ieee80211_hw *hw, u32 changed)
{
	struct il_priv *il = hw->priv;
	const struct il_channel_info *ch_info;
	struct ieee80211_conf *conf = &hw->conf;
	struct ieee80211_channel *channel = conf->channel;
	struct il_ht_config *ht_conf = &il->current_ht_config;
	struct il_rxon_context *ctx;
	unsigned long flags = 0;
	int ret = 0;
	u16 ch;
	int scan_active = 0;
	bool ht_changed[NUM_IL_RXON_CTX] = {};

	if (WARN_ON(!il->cfg->ops->legacy))
		return -EOPNOTSUPP;

	mutex_lock(&il->mutex);

	IL_DEBUG_MAC80211(il, "enter to channel %d changed 0x%X\n",
					channel->hw_value, changed);

	if (unlikely(test_bit(STATUS_SCANNING, &il->status))) {
		scan_active = 1;
		IL_DEBUG_MAC80211(il, "scan active\n");
	}

	if (changed & (IEEE80211_CONF_CHANGE_SMPS |
		       IEEE80211_CONF_CHANGE_CHANNEL)) {
		/* mac80211 uses static for non-HT which is what we want */
		il->current_ht_config.smps = conf->smps_mode;

		/*
		 * Recalculate chain counts.
		 *
		 * If monitor mode is enabled then mac80211 will
		 * set up the SM PS mode to OFF if an HT channel is
		 * configured.
		 */
		if (il->cfg->ops->hcmd->set_rxon_chain)
			for_each_context(il, ctx)
				il->cfg->ops->hcmd->set_rxon_chain(il, ctx);
	}

	/* during scanning mac80211 will delay channel setting until
	 * scan finish with changed = 0
	 */
	if (!changed || (changed & IEEE80211_CONF_CHANGE_CHANNEL)) {
		if (scan_active)
			goto set_ch_out;

		ch = channel->hw_value;
		ch_info = il_get_channel_info(il, channel->band, ch);
		if (!il_is_channel_valid(ch_info)) {
			IL_DEBUG_MAC80211(il, "leave - invalid channel\n");
			ret = -EINVAL;
			goto set_ch_out;
		}

		if (il->iw_mode == NL80211_IFTYPE_ADHOC &&
		    !il_is_channel_ibss(ch_info)) {
			IL_DEBUG_MAC80211(il, "leave - not IBSS channel\n");
			ret = -EINVAL;
			goto set_ch_out;
		}

		spin_lock_irqsave(&il->lock, flags);

		for_each_context(il, ctx) {
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
			 * later be set from BSS config in il_ht_conf
			 */
			ctx->ht.protection =
					IEEE80211_HT_OP_MODE_PROTECTION_NONE;

			/* if we are switching from ht to 2.4 clear flags
			 * from any ht related info since 2.4 does not
			 * support ht */
			if ((le16_to_cpu(ctx->staging.channel) != ch))
				ctx->staging.flags = 0;

			il_set_rxon_channel(il, channel, ctx);
			il_set_rxon_ht(il, ht_conf);

			il_set_flags_for_band(il, ctx, channel->band,
					       ctx->vif);
		}

		spin_unlock_irqrestore(&il->lock, flags);

		if (il->cfg->ops->legacy->update_bcast_stations)
			ret =
			il->cfg->ops->legacy->update_bcast_stations(il);

 set_ch_out:
		/* The list of supported rates and rate mask can be different
		 * for each band; since the band may have changed, reset
		 * the rate mask to what mac80211 lists */
		il_set_rate(il);
	}

	if (changed & (IEEE80211_CONF_CHANGE_PS |
			IEEE80211_CONF_CHANGE_IDLE)) {
		ret = il_power_update_mode(il, false);
		if (ret)
			IL_DEBUG_MAC80211(il, "Error setting sleep level\n");
	}

	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		IL_DEBUG_MAC80211(il, "TX Power old=%d new=%d\n",
			il->tx_power_user_lmt, conf->power_level);

		il_set_tx_power(il, conf->power_level, false);
	}

	if (!il_is_ready(il)) {
		IL_DEBUG_MAC80211(il, "leave - not ready\n");
		goto out;
	}

	if (scan_active)
		goto out;

	for_each_context(il, ctx) {
		if (memcmp(&ctx->active, &ctx->staging, sizeof(ctx->staging)))
			il_commit_rxon(il, ctx);
		else
			IL_DEBUG_INFO(il,
				"Not re-sending same RXON configuration.\n");
		if (ht_changed[ctx->ctxid])
			il_update_qos(il, ctx);
	}

out:
	IL_DEBUG_MAC80211(il, "leave\n");
	mutex_unlock(&il->mutex);
	return ret;
}
EXPORT_SYMBOL(il_mac_config);

void il_mac_reset_tsf(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif)
{
	struct il_priv *il = hw->priv;
	unsigned long flags;
	/* IBSS can only be the IL_RXON_CTX_BSS context */
	struct il_rxon_context *ctx = &il->contexts[IL_RXON_CTX_BSS];

	if (WARN_ON(!il->cfg->ops->legacy))
		return;

	mutex_lock(&il->mutex);
	IL_DEBUG_MAC80211(il, "enter\n");

	spin_lock_irqsave(&il->lock, flags);
	memset(&il->current_ht_config, 0, sizeof(struct il_ht_config));
	spin_unlock_irqrestore(&il->lock, flags);

	spin_lock_irqsave(&il->lock, flags);

	/* new association get rid of ibss beacon skb */
	if (il->beacon_skb)
		dev_kfree_skb(il->beacon_skb);

	il->beacon_skb = NULL;

	il->timestamp = 0;

	spin_unlock_irqrestore(&il->lock, flags);

	il_scan_cancel_timeout(il, 100);
	if (!il_is_ready_rf(il)) {
		IL_DEBUG_MAC80211(il, "leave - not ready\n");
		mutex_unlock(&il->mutex);
		return;
	}

	/* we are restarting association process
	 * clear RXON_FILTER_ASSOC_MSK bit
	 */
	ctx->staging.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	il_commit_rxon(il, ctx);

	il_set_rate(il);

	mutex_unlock(&il->mutex);

	IL_DEBUG_MAC80211(il, "leave\n");
}
EXPORT_SYMBOL(il_mac_reset_tsf);

static void il_ht_conf(struct il_priv *il,
			struct ieee80211_vif *vif)
{
	struct il_ht_config *ht_conf = &il->current_ht_config;
	struct ieee80211_sta *sta;
	struct ieee80211_bss_conf *bss_conf = &vif->bss_conf;
	struct il_rxon_context *ctx = il_rxon_ctx_from_vif(vif);

	IL_DEBUG_ASSOC(il, "enter:\n");

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

	IL_DEBUG_ASSOC(il, "leave\n");
}

static inline void il_set_no_assoc(struct il_priv *il,
				    struct ieee80211_vif *vif)
{
	struct il_rxon_context *ctx = il_rxon_ctx_from_vif(vif);

	/*
	 * inform the ucode that there is no longer an
	 * association and that no more packets should be
	 * sent
	 */
	ctx->staging.filter_flags &= ~RXON_FILTER_ASSOC_MSK;
	ctx->staging.assoc_id = 0;
	il_commit_rxon(il, ctx);
}

static void il_beacon_update(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif)
{
	struct il_priv *il = hw->priv;
	unsigned long flags;
	__le64 timestamp;
	struct sk_buff *skb = ieee80211_beacon_get(hw, vif);

	if (!skb)
		return;

	IL_DEBUG_MAC80211(il, "enter\n");

	lockdep_assert_held(&il->mutex);

	if (!il->beacon_ctx) {
		IL_ERR(il, "update beacon but no beacon context!\n");
		dev_kfree_skb(skb);
		return;
	}

	spin_lock_irqsave(&il->lock, flags);

	if (il->beacon_skb)
		dev_kfree_skb(il->beacon_skb);

	il->beacon_skb = skb;

	timestamp = ((struct ieee80211_mgmt *)skb->data)->u.beacon.timestamp;
	il->timestamp = le64_to_cpu(timestamp);

	IL_DEBUG_MAC80211(il, "leave\n");
	spin_unlock_irqrestore(&il->lock, flags);

	if (!il_is_ready_rf(il)) {
		IL_DEBUG_MAC80211(il, "leave - RF not ready\n");
		return;
	}

	il->cfg->ops->legacy->post_associate(il);
}

void il_mac_bss_info_changed(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *bss_conf,
				     u32 changes)
{
	struct il_priv *il = hw->priv;
	struct il_rxon_context *ctx = il_rxon_ctx_from_vif(vif);
	int ret;

	if (WARN_ON(!il->cfg->ops->legacy))
		return;

	IL_DEBUG_MAC80211(il, "changes = 0x%X\n", changes);

	mutex_lock(&il->mutex);

	if (!il_is_alive(il)) {
		mutex_unlock(&il->mutex);
		return;
	}

	if (changes & BSS_CHANGED_QOS) {
		unsigned long flags;

		spin_lock_irqsave(&il->lock, flags);
		ctx->qos_data.qos_active = bss_conf->qos;
		il_update_qos(il, ctx);
		spin_unlock_irqrestore(&il->lock, flags);
	}

	if (changes & BSS_CHANGED_BEACON_ENABLED) {
		/*
		 * the add_interface code must make sure we only ever
		 * have a single interface that could be beaconing at
		 * any time.
		 */
		if (vif->bss_conf.enable_beacon)
			il->beacon_ctx = ctx;
		else
			il->beacon_ctx = NULL;
	}

	if (changes & BSS_CHANGED_BSSID) {
		IL_DEBUG_MAC80211(il, "BSSID %pM\n", bss_conf->bssid);

		/*
		 * If there is currently a HW scan going on in the
		 * background then we need to cancel it else the RXON
		 * below/in post_associate will fail.
		 */
		if (il_scan_cancel_timeout(il, 100)) {
			IL_WARN(il,
				"Aborted scan still in progress after 100ms\n");
			IL_DEBUG_MAC80211(il,
				"leaving - scan abort failed.\n");
			mutex_unlock(&il->mutex);
			return;
		}

		/* mac80211 only sets assoc when in STATION mode */
		if (vif->type == NL80211_IFTYPE_ADHOC || bss_conf->assoc) {
			memcpy(ctx->staging.bssid_addr,
			       bss_conf->bssid, ETH_ALEN);

			/* currently needed in a few places */
			memcpy(il->bssid, bss_conf->bssid, ETH_ALEN);
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
		il_beacon_update(hw, vif);

	if (changes & BSS_CHANGED_ERP_PREAMBLE) {
		IL_DEBUG_MAC80211(il, "ERP_PREAMBLE %d\n",
				   bss_conf->use_short_preamble);
		if (bss_conf->use_short_preamble)
			ctx->staging.flags |= RXON_FLG_SHORT_PREAMBLE_MSK;
		else
			ctx->staging.flags &= ~RXON_FLG_SHORT_PREAMBLE_MSK;
	}

	if (changes & BSS_CHANGED_ERP_CTS_PROT) {
		IL_DEBUG_MAC80211(il,
			"ERP_CTS %d\n", bss_conf->use_cts_prot);
		if (bss_conf->use_cts_prot &&
			(il->band != IEEE80211_BAND_5GHZ))
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
		 * To do that, remove code from il_set_rate() and put something
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
		il_ht_conf(il, vif);

		if (il->cfg->ops->hcmd->set_rxon_chain)
			il->cfg->ops->hcmd->set_rxon_chain(il, ctx);
	}

	if (changes & BSS_CHANGED_ASSOC) {
		IL_DEBUG_MAC80211(il, "ASSOC %d\n", bss_conf->assoc);
		if (bss_conf->assoc) {
			il->timestamp = bss_conf->timestamp;

			if (!il_is_rfkill(il))
				il->cfg->ops->legacy->post_associate(il);
		} else
			il_set_no_assoc(il, vif);
	}

	if (changes && il_is_associated_ctx(ctx) && bss_conf->aid) {
		IL_DEBUG_MAC80211(il, "Changes (%#x) while associated\n",
				   changes);
		ret = il_send_rxon_assoc(il, ctx);
		if (!ret) {
			/* Sync active_rxon with latest change. */
			memcpy((void *)&ctx->active,
				&ctx->staging,
				sizeof(struct il_rxon_cmd));
		}
	}

	if (changes & BSS_CHANGED_BEACON_ENABLED) {
		if (vif->bss_conf.enable_beacon) {
			memcpy(ctx->staging.bssid_addr,
			       bss_conf->bssid, ETH_ALEN);
			memcpy(il->bssid, bss_conf->bssid, ETH_ALEN);
			il->cfg->ops->legacy->config_ap(il);
		} else
			il_set_no_assoc(il, vif);
	}

	if (changes & BSS_CHANGED_IBSS) {
		ret = il->cfg->ops->legacy->manage_ibss_station(il, vif,
							bss_conf->ibss_joined);
		if (ret)
			IL_ERR(il, "failed to %s IBSS station %pM\n",
				bss_conf->ibss_joined ? "add" : "remove",
				bss_conf->bssid);
	}

	mutex_unlock(&il->mutex);

	IL_DEBUG_MAC80211(il, "leave\n");
}
EXPORT_SYMBOL(il_mac_bss_info_changed);

irqreturn_t il_isr(int irq, void *data)
{
	struct il_priv *il = data;
	u32 inta, inta_mask;
	u32 inta_fh;
	unsigned long flags;
	if (!il)
		return IRQ_NONE;

	spin_lock_irqsave(&il->lock, flags);

	/* Disable (but don't clear!) interrupts here to avoid
	 *    back-to-back ISRs and sporadic interrupts from our NIC.
	 * If we have something to service, the tasklet will re-enable ints.
	 * If we *don't* have something, we'll re-enable before leaving here. */
	inta_mask = il_read32(il, CSR_INT_MASK);  /* just for debug */
	il_write32(il, CSR_INT_MASK, 0x00000000);

	/* Discover which interrupts are active/pending */
	inta = il_read32(il, CSR_INT);
	inta_fh = il_read32(il, CSR_FH_INT_STATUS);

	/* Ignore interrupt if there's nothing in NIC to service.
	 * This may be due to IRQ shared with another device,
	 * or due to sporadic interrupts thrown from our NIC. */
	if (!inta && !inta_fh) {
		IL_DEBUG_ISR(il,
			"Ignore interrupt, inta == 0, inta_fh == 0\n");
		goto none;
	}

	if ((inta == 0xFFFFFFFF) || ((inta & 0xFFFFFFF0) == 0xa5a5a5a0)) {
		/* Hardware disappeared. It might have already raised
		 * an interrupt */
		IL_WARN(il, "HARDWARE GONE?? INTA == 0x%08x\n", inta);
		goto unplugged;
	}

	IL_DEBUG_ISR(il, "ISR inta 0x%08x, enabled 0x%08x, fh 0x%08x\n",
		      inta, inta_mask, inta_fh);

	inta &= ~CSR_INT_BIT_SCD;

	/* il_irq_tasklet() will service interrupts and re-enable them */
	if (likely(inta || inta_fh))
		tasklet_schedule(&il->irq_tasklet);

unplugged:
	spin_unlock_irqrestore(&il->lock, flags);
	return IRQ_HANDLED;

none:
	/* re-enable interrupts here since we don't have anything to service. */
	/* only Re-enable if disabled by irq */
	if (test_bit(STATUS_INT_ENABLED, &il->status))
		il_enable_interrupts(il);
	spin_unlock_irqrestore(&il->lock, flags);
	return IRQ_NONE;
}
EXPORT_SYMBOL(il_isr);

/*
 *  il_tx_cmd_protection: Set rts/cts. 3945 and 4965 only share this
 *  function.
 */
void il_tx_cmd_protection(struct il_priv *il,
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
EXPORT_SYMBOL(il_tx_cmd_protection);
