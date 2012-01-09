/*
 * Marvell Wireless LAN device driver: Channel, Frequence and Power
 *
 * Copyright (C) 2011, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#include "decl.h"
#include "ioctl.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "cfg80211.h"

/* 100mW */
#define MWIFIEX_TX_PWR_DEFAULT     20
/* 100mW */
#define MWIFIEX_TX_PWR_US_DEFAULT      20
/* 50mW */
#define MWIFIEX_TX_PWR_JP_DEFAULT      16
/* 100mW */
#define MWIFIEX_TX_PWR_FR_100MW        20
/* 10mW */
#define MWIFIEX_TX_PWR_FR_10MW         10
/* 100mW */
#define MWIFIEX_TX_PWR_EMEA_DEFAULT    20

static u8 adhoc_rates_b[B_SUPPORTED_RATES] = { 0x82, 0x84, 0x8b, 0x96, 0 };

static u8 adhoc_rates_g[G_SUPPORTED_RATES] = { 0x8c, 0x12, 0x98, 0x24,
					       0xb0, 0x48, 0x60, 0x6c, 0 };

static u8 adhoc_rates_bg[BG_SUPPORTED_RATES] = { 0x82, 0x84, 0x8b, 0x96,
						 0x0c, 0x12, 0x18, 0x24,
						 0x30, 0x48, 0x60, 0x6c, 0 };

static u8 adhoc_rates_a[A_SUPPORTED_RATES] = { 0x8c, 0x12, 0x98, 0x24,
					       0xb0, 0x48, 0x60, 0x6c, 0 };
static u8 supported_rates_a[A_SUPPORTED_RATES] = { 0x0c, 0x12, 0x18, 0x24,
					0xb0, 0x48, 0x60, 0x6c, 0 };
static u16 mwifiex_data_rates[MWIFIEX_SUPPORTED_RATES_EXT] = { 0x02, 0x04,
					0x0B, 0x16, 0x00, 0x0C, 0x12, 0x18,
					0x24, 0x30, 0x48, 0x60, 0x6C, 0x90,
					0x0D, 0x1A, 0x27, 0x34, 0x4E, 0x68,
					0x75, 0x82, 0x0C, 0x1B, 0x36, 0x51,
					0x6C, 0xA2, 0xD8, 0xF3, 0x10E, 0x00 };

static u8 supported_rates_b[B_SUPPORTED_RATES] = { 0x02, 0x04, 0x0b, 0x16, 0 };

static u8 supported_rates_g[G_SUPPORTED_RATES] = { 0x0c, 0x12, 0x18, 0x24,
					0x30, 0x48, 0x60, 0x6c, 0 };

static u8 supported_rates_bg[BG_SUPPORTED_RATES] = { 0x02, 0x04, 0x0b, 0x0c,
					0x12, 0x16, 0x18, 0x24, 0x30, 0x48,
					0x60, 0x6c, 0 };

u16 region_code_index[MWIFIEX_MAX_REGION_CODE] = { 0x10, 0x20, 0x30,
						0x32, 0x40, 0x41, 0xff };

static u8 supported_rates_n[N_SUPPORTED_RATES] = { 0x02, 0x04, 0 };

/*
 * This function maps an index in supported rates table into
 * the corresponding data rate.
 */
u32 mwifiex_index_to_data_rate(u8 index, u8 ht_info)
{
	u16 mcs_rate[4][8] = {
		{0x1b, 0x36, 0x51, 0x6c, 0xa2, 0xd8, 0xf3, 0x10e}
	,			/* LG 40M */
	{0x1e, 0x3c, 0x5a, 0x78, 0xb4, 0xf0, 0x10e, 0x12c}
	,			/* SG 40M */
	{0x0d, 0x1a, 0x27, 0x34, 0x4e, 0x68, 0x75, 0x82}
	,			/* LG 20M */
	{0x0e, 0x1c, 0x2b, 0x39, 0x56, 0x73, 0x82, 0x90}
	};			/* SG 20M */

	u32 rate;

	if (ht_info & BIT(0)) {
		if (index == MWIFIEX_RATE_BITMAP_MCS0) {
			if (ht_info & BIT(2))
				rate = 0x0D;	/* MCS 32 SGI rate */
			else
				rate = 0x0C;	/* MCS 32 LGI rate */
		} else if (index < 8) {
			if (ht_info & BIT(1)) {
				if (ht_info & BIT(2))
					/* SGI, 40M */
					rate = mcs_rate[1][index];
				else
					/* LGI, 40M */
					rate = mcs_rate[0][index];
			} else {
				if (ht_info & BIT(2))
					/* SGI, 20M */
					rate = mcs_rate[3][index];
				else
					/* LGI, 20M */
					rate = mcs_rate[2][index];
			}
		} else
			rate = mwifiex_data_rates[0];
	} else {
		if (index >= MWIFIEX_SUPPORTED_RATES_EXT)
			index = 0;
		rate = mwifiex_data_rates[index];
	}
	return rate;
}

/*
 * This function maps a data rate value into corresponding index in supported
 * rates table.
 */
u8 mwifiex_data_rate_to_index(u32 rate)
{
	u16 *ptr;

	if (rate) {
		ptr = memchr(mwifiex_data_rates, rate,
				sizeof(mwifiex_data_rates));
		if (ptr)
			return (u8) (ptr - mwifiex_data_rates);
	}
	return 0;
}

/*
 * This function returns the current active data rates.
 *
 * The result may vary depending upon connection status.
 */
u32 mwifiex_get_active_data_rates(struct mwifiex_private *priv, u8 *rates)
{
	if (!priv->media_connected)
		return mwifiex_get_supported_rates(priv, rates);
	else
		return mwifiex_copy_rates(rates, 0,
				       priv->curr_bss_params.data_rates,
				       priv->curr_bss_params.num_of_rates);
}

/*
 * This function locates the Channel-Frequency-Power triplet based upon
 * band and channel parameters.
 */
struct mwifiex_chan_freq_power *
mwifiex_get_cfp_by_band_and_channel_from_cfg80211(struct mwifiex_private
						  *priv, u8 band, u16 channel)
{
	struct mwifiex_chan_freq_power *cfp = NULL;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	int i;

	if (mwifiex_band_to_radio_type(band) == HostCmd_SCAN_RADIO_TYPE_BG)
		sband = priv->wdev->wiphy->bands[IEEE80211_BAND_2GHZ];
	else
		sband = priv->wdev->wiphy->bands[IEEE80211_BAND_5GHZ];

	if (!sband) {
		dev_err(priv->adapter->dev, "%s: cannot find cfp by band %d"
				" & channel %d\n", __func__, band, channel);
		return cfp;
	}

	for (i = 0; i < sband->n_channels; i++) {
		ch = &sband->channels[i];
		if (((ch->hw_value == channel) ||
			(channel == FIRST_VALID_CHANNEL))
			&& !(ch->flags & IEEE80211_CHAN_DISABLED)) {
			priv->cfp.channel = channel;
			priv->cfp.freq = ch->center_freq;
			priv->cfp.max_tx_power = ch->max_power;
			cfp = &priv->cfp;
			break;
		}
	}
	if (i == sband->n_channels)
		dev_err(priv->adapter->dev, "%s: cannot find cfp by band %d"
				" & channel %d\n", __func__, band, channel);

	return cfp;
}

/*
 * This function locates the Channel-Frequency-Power triplet based upon
 * band and frequency parameters.
 */
struct mwifiex_chan_freq_power *
mwifiex_get_cfp_by_band_and_freq_from_cfg80211(struct mwifiex_private *priv,
					       u8 band, u32 freq)
{
	struct mwifiex_chan_freq_power *cfp = NULL;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	int i;

	if (mwifiex_band_to_radio_type(band) == HostCmd_SCAN_RADIO_TYPE_BG)
		sband = priv->wdev->wiphy->bands[IEEE80211_BAND_2GHZ];
	else
		sband = priv->wdev->wiphy->bands[IEEE80211_BAND_5GHZ];

	if (!sband) {
		dev_err(priv->adapter->dev, "%s: cannot find cfp by band %d"
				" & freq %d\n", __func__, band, freq);
		return cfp;
	}

	for (i = 0; i < sband->n_channels; i++) {
		ch = &sband->channels[i];
		if ((ch->center_freq == freq) &&
			!(ch->flags & IEEE80211_CHAN_DISABLED)) {
			priv->cfp.channel = ch->hw_value;
			priv->cfp.freq = freq;
			priv->cfp.max_tx_power = ch->max_power;
			cfp = &priv->cfp;
			break;
		}
	}
	if (i == sband->n_channels)
		dev_err(priv->adapter->dev, "%s: cannot find cfp by band %d"
				" & freq %d\n", __func__, band, freq);

	return cfp;
}

/*
 * This function checks if the data rate is set to auto.
 */
u8
mwifiex_is_rate_auto(struct mwifiex_private *priv)
{
	u32 i;
	int rate_num = 0;

	for (i = 0; i < ARRAY_SIZE(priv->bitmap_rates); i++)
		if (priv->bitmap_rates[i])
			rate_num++;

	if (rate_num > 1)
		return true;
	else
		return false;
}

/*
 * This function converts rate bitmap into rate index.
 */
int mwifiex_get_rate_index(u16 *rate_bitmap, int size)
{
	int i;

	for (i = 0; i < size * 8; i++)
		if (rate_bitmap[i / 16] & (1 << (i % 16)))
			return i;

	return 0;
}

/*
 * This function gets the supported data rates.
 *
 * The function works in both Ad-Hoc and infra mode by printing the
 * band and returning the data rates.
 */
u32 mwifiex_get_supported_rates(struct mwifiex_private *priv, u8 *rates)
{
	u32 k = 0;
	struct mwifiex_adapter *adapter = priv->adapter;
	if (priv->bss_mode == NL80211_IFTYPE_STATION) {
		switch (adapter->config_bands) {
		case BAND_B:
			dev_dbg(adapter->dev, "info: infra band=%d "
				"supported_rates_b\n", adapter->config_bands);
			k = mwifiex_copy_rates(rates, k, supported_rates_b,
					       sizeof(supported_rates_b));
			break;
		case BAND_G:
		case BAND_G | BAND_GN:
			dev_dbg(adapter->dev, "info: infra band=%d "
				"supported_rates_g\n", adapter->config_bands);
			k = mwifiex_copy_rates(rates, k, supported_rates_g,
					       sizeof(supported_rates_g));
			break;
		case BAND_B | BAND_G:
		case BAND_A | BAND_B | BAND_G:
		case BAND_A | BAND_B:
		case BAND_A | BAND_B | BAND_G | BAND_GN | BAND_AN:
		case BAND_B | BAND_G | BAND_GN:
			dev_dbg(adapter->dev, "info: infra band=%d "
				"supported_rates_bg\n", adapter->config_bands);
			k = mwifiex_copy_rates(rates, k, supported_rates_bg,
					       sizeof(supported_rates_bg));
			break;
		case BAND_A:
		case BAND_A | BAND_G:
			dev_dbg(adapter->dev, "info: infra band=%d "
				"supported_rates_a\n", adapter->config_bands);
			k = mwifiex_copy_rates(rates, k, supported_rates_a,
					       sizeof(supported_rates_a));
			break;
		case BAND_A | BAND_AN:
		case BAND_A | BAND_G | BAND_AN | BAND_GN:
			dev_dbg(adapter->dev, "info: infra band=%d "
				"supported_rates_a\n", adapter->config_bands);
			k = mwifiex_copy_rates(rates, k, supported_rates_a,
					       sizeof(supported_rates_a));
			break;
		case BAND_GN:
			dev_dbg(adapter->dev, "info: infra band=%d "
				"supported_rates_n\n", adapter->config_bands);
			k = mwifiex_copy_rates(rates, k, supported_rates_n,
					       sizeof(supported_rates_n));
			break;
		}
	} else {
		/* Ad-hoc mode */
		switch (adapter->adhoc_start_band) {
		case BAND_B:
			dev_dbg(adapter->dev, "info: adhoc B\n");
			k = mwifiex_copy_rates(rates, k, adhoc_rates_b,
					       sizeof(adhoc_rates_b));
			break;
		case BAND_G:
		case BAND_G | BAND_GN:
			dev_dbg(adapter->dev, "info: adhoc G only\n");
			k = mwifiex_copy_rates(rates, k, adhoc_rates_g,
					       sizeof(adhoc_rates_g));
			break;
		case BAND_B | BAND_G:
		case BAND_B | BAND_G | BAND_GN:
			dev_dbg(adapter->dev, "info: adhoc BG\n");
			k = mwifiex_copy_rates(rates, k, adhoc_rates_bg,
					       sizeof(adhoc_rates_bg));
			break;
		case BAND_A:
		case BAND_A | BAND_AN:
			dev_dbg(adapter->dev, "info: adhoc A\n");
			k = mwifiex_copy_rates(rates, k, adhoc_rates_a,
					       sizeof(adhoc_rates_a));
			break;
		}
	}

	return k;
}
