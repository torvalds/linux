// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 */

#include "baseband.h"
#include "channel.h"
#include "device.h"
#include "rf.h"

static struct ieee80211_rate vnt_rates_bg[] = {
	{ .bitrate = 10,  .hw_value = RATE_1M },
	{ .bitrate = 20,  .hw_value = RATE_2M },
	{ .bitrate = 55,  .hw_value = RATE_5M },
	{ .bitrate = 110, .hw_value = RATE_11M },
	{ .bitrate = 60,  .hw_value = RATE_6M },
	{ .bitrate = 90,  .hw_value = RATE_9M },
	{ .bitrate = 120, .hw_value = RATE_12M },
	{ .bitrate = 180, .hw_value = RATE_18M },
	{ .bitrate = 240, .hw_value = RATE_24M },
	{ .bitrate = 360, .hw_value = RATE_36M },
	{ .bitrate = 480, .hw_value = RATE_48M },
	{ .bitrate = 540, .hw_value = RATE_54M },
};

static struct ieee80211_channel vnt_channels_2ghz[] = {
	{ .center_freq = 2412, .hw_value = 1 },
	{ .center_freq = 2417, .hw_value = 2 },
	{ .center_freq = 2422, .hw_value = 3 },
	{ .center_freq = 2427, .hw_value = 4 },
	{ .center_freq = 2432, .hw_value = 5 },
	{ .center_freq = 2437, .hw_value = 6 },
	{ .center_freq = 2442, .hw_value = 7 },
	{ .center_freq = 2447, .hw_value = 8 },
	{ .center_freq = 2452, .hw_value = 9 },
	{ .center_freq = 2457, .hw_value = 10 },
	{ .center_freq = 2462, .hw_value = 11 },
	{ .center_freq = 2467, .hw_value = 12 },
	{ .center_freq = 2472, .hw_value = 13 },
	{ .center_freq = 2484, .hw_value = 14 }
};

static struct ieee80211_supported_band vnt_supported_2ghz_band = {
	.channels = vnt_channels_2ghz,
	.n_channels = ARRAY_SIZE(vnt_channels_2ghz),
	.bitrates = vnt_rates_bg,
	.n_bitrates = ARRAY_SIZE(vnt_rates_bg),
};

static void vnt_init_band(struct vnt_private *priv,
			  struct ieee80211_supported_band *supported_band,
			  enum nl80211_band band)
{
	int i;

	for (i = 0; i < supported_band->n_channels; i++) {
		supported_band->channels[i].max_power = 0x3f;
		supported_band->channels[i].flags =
			IEEE80211_CHAN_NO_HT40;
	}

	priv->hw->wiphy->bands[band] = supported_band;
}

void vnt_init_bands(struct vnt_private *priv)
{
	vnt_init_band(priv, &vnt_supported_2ghz_band, NL80211_BAND_2GHZ);
}

/**
 * set_channel() - Set NIC media channel
 *
 * @priv: The adapter to be set
 * @ch: Channel to be set
 *
 * Return Value: true if succeeded; false if failed.
 *
 */
bool set_channel(struct vnt_private *priv, struct ieee80211_channel *ch)
{
	bool ret = true;

	if (priv->byCurrentCh == ch->hw_value)
		return ret;

	/* Set VGA to max sensitivity */
	if (priv->bUpdateBBVGA &&
	    priv->byBBVGACurrent != priv->abyBBVGA[0]) {
		priv->byBBVGACurrent = priv->abyBBVGA[0];

		bb_set_vga_gain_offset(priv, priv->byBBVGACurrent);
	}

	/* clear NAV */
	MACvRegBitsOn(priv->port_offset, MAC_REG_MACCR, MACCR_CLRNAV);

	/* TX_PE will reserve 3 us for MAX2829 A mode only,
	 * it is for better TX throughput
	 */

	priv->byCurrentCh = ch->hw_value;
	ret &= RFbSelectChannel(priv, priv->byRFType,
				ch->hw_value);

	/* Init Synthesizer Table */
	if (priv->bEnablePSMode)
		rf_write_wake_prog_syn(priv, priv->byRFType, ch->hw_value);

	bb_software_reset(priv);

	if (priv->local_id > REV_ID_VT3253_B1) {
		unsigned long flags;

		spin_lock_irqsave(&priv->lock, flags);

		/* set HW default power register */
		MACvSelectPage1(priv->port_offset);
		RFbSetPower(priv, RATE_1M, priv->byCurrentCh);
		iowrite8(priv->byCurPwr, priv->port_offset + MAC_REG_PWRCCK);
		RFbSetPower(priv, RATE_6M, priv->byCurrentCh);
		iowrite8(priv->byCurPwr, priv->port_offset + MAC_REG_PWROFDM);
		MACvSelectPage0(priv->port_offset);

		spin_unlock_irqrestore(&priv->lock, flags);
	}

	if (priv->byBBType == BB_TYPE_11B)
		RFbSetPower(priv, RATE_1M, priv->byCurrentCh);
	else
		RFbSetPower(priv, RATE_6M, priv->byCurrentCh);

	return ret;
}
